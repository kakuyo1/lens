/** @file pencil_duty.cpp
 * @brief The pencil layer's words, the signposts, the hover and the floating mark button, read on a timer.
 */
#include "pencil_duty.h"

#include <QStringList>
#include <QVariantMap>
#include <QWindow>

#include <algorithm>
#include <utility>

#include "app/platform/hover_word_lookup.h"
#include "app/platform/window_native.h"
#include "capture_duty.h"
#include "explanation_duty.h"
#include "util/log.h"

namespace lens::app {
namespace {

/// How often the pointer and the marks are re-read while words stand on the page: a stored signpost
/// shows within this, and the mark button's dwell is counted in the same ticks.
constexpr int kPencilTickMs = 120;
/// How long the pointer must rest on a word before its mark button rises (docs/adr/0160).
constexpr int kMarkHoverMs = 400;

}

void PencilDuty::keepOutOfCapture(QObject* const object, bool const passthrough)
{
    auto* const window = qobject_cast<QWindow*>(object);
    if (window == nullptr) return;
    auto const rule = [passthrough](QWindow const& shown) {
        if (not excludeFromCapture(shown)) LENS_WARN("the pencil layer can appear in screen captures: Windows refused the exclusion");
        // Clicks pass through by the Win32 style: Qt.WindowTransparentForInput painted this full-screen layer black.
        setPassthrough(shown, passthrough);
        LENS_INFO("pencil window shown: passthrough={}", passthrough);
    };
    connect(window, &QWindow::visibleChanged, window, [window, rule](bool const on) {
        if (on) rule(*window);
    });
    if (window->isVisible()) rule(*window);
}

PencilDuty::PencilDuty(CaptureDuty& capture, ExplanationDuty& explanation, QObject* const parent)
    : QObject(parent), capture_(capture), explanation_(explanation)
{
    timer_.setInterval(kPencilTickMs);
    connect(&timer_, &QTimer::timeout, this, &PencilDuty::refresh);
}

bool PencilDuty::visible() const
{
    return not marks_.isEmpty();
}

bool PencilDuty::altHeld() const
{
    return altHeld_;
}

QRect PencilDuty::screen() const
{
    return screen_;
}

QVariantList PencilDuty::marks() const
{
    return marks_;
}

QVariantMap PencilDuty::markButton() const
{
    return markButton_;
}

void PencilDuty::reportMarkButtonGeometry(QPointF const pos, QSizeF const size)
{
    if (size.width() > 0 and size.height() > 0)
        markButtonSize_ = size;
    markButtonPos_ = pos;
}

void PencilDuty::placeWords(QVariantList const words, QRect const screen, QPoint const screenOrigin, double const scale)
{
    words_.clear();
    for (auto const& value : words) {
        auto const map     = value.toMap();
        auto const verdict = map.value(QStringLiteral("verdict")).toString();
        words_.push_back({.lemma   = map.value(QStringLiteral("lemma")).toString(),
                          .surface = map.value(QStringLiteral("surface")).toString(),
                          .verdict = verdict.isEmpty() ? QStringLiteral("new") : verdict,
                          .box     = QRect{map.value(QStringLiteral("x")).toInt(), map.value(QStringLiteral("y")).toInt(), map.value(QStringLiteral("width")).toInt(), map.value(QStringLiteral("height")).toInt()}});
    }
    screen_       = screen;
    screenOrigin_ = screenOrigin;
    scale_        = scale > 0 ? scale : 1.0;
    lookedUp_     = -1;
    // The frames keep coming while the button stands: it is re-anchored on the fresh boxes by the
    // refresh below, and only taken down when its word is no longer on the page at all.
    if (not markButton_.isEmpty()) {
        const QString lemma = markButton_.value(QStringLiteral("lemma")).toString();
        const bool    gone  = std::none_of(words_.cbegin(), words_.cend(), [&lemma](Word const& word) { return word.lemma == lemma; });
        if (gone) {
            markLemma_.clear();
            markStreak_ = 0;
            hideMarkButton();
        }
    }
    if (altHeld_) {
        QStringList lemmas;
        for (auto const& word : words_)
            lemmas << word.lemma;
        explanation_.prefetchSignposts(lemmas);
    }
    if (words_.isEmpty())
        timer_.stop();
    else
        timer_.start();
    refresh();
}

void PencilDuty::clear()
{
    words_.clear();
    lookedUp_ = -1;
    markLemma_.clear();
    markStreak_ = 0;
    hideMarkButton();
    timer_.stop();
    refresh();
}

void PencilDuty::altDown()
{
    if (altHeld_) return;
    altHeld_  = true;
    lookedUp_ = -1;
    // The signposts and the bubble own the pointer while Alt is held: the button stands down.
    markLemma_.clear();
    markStreak_ = 0;
    hideMarkButton();
    QStringList lemmas;
    for (auto const& word : words_)
        lemmas << word.lemma;
    explanation_.prefetchSignposts(lemmas);
    timer_.start();
    refresh();
}

void PencilDuty::altUp()
{
    altHeld_  = false;
    lookedUp_ = -1;
    if (words_.isEmpty())
        timer_.stop();
    else
        timer_.start(); // the non-Alt hover keeps polling for the mark button
    refresh();
}

void PencilDuty::verdictChanged(QString const lemma, QString const verdict)
{
    if (verdict == QStringLiteral("mastered")) {
        // A retired word's line is gone for good on this page: the scan no longer places it, so
        // the occurrences leave now rather than on a frame that will not come.
        words_.erase(std::remove_if(words_.begin(), words_.end(), [&lemma](Word const& word) { return word.lemma == lemma; }),
                     words_.end());
    } else {
        for (auto& word : words_)
            if (word.lemma == lemma) word.verdict = verdict;
    }
    if (not markButton_.isEmpty() and markButton_.value(QStringLiteral("lemma")).toString() == lemma) {
        markLemma_.clear();
        markStreak_ = 0;
        hideMarkButton();
    }
    lookedUp_ = -1;
    refresh();
}

int PencilDuty::wordAt(QPoint const at) const
{
    // The scan's boxes are the glyphs' own; the hit area takes a little of the line above and below.
    for (int index = 0; index < words_.size(); ++index)
        if (words_.at(index).box.adjusted(-2, -6, 2, 6).contains(at)) return index;
    return -1;
}

QPointF PencilDuty::cursorDip() const
{
    const QPoint physical = hoverCursorPos();
    return QPointF{(physical.x() - screenOrigin_.x()) / scale_ + screen_.x(),
                   (physical.y() - screenOrigin_.y()) / scale_ + screen_.y()};
}

void PencilDuty::hideMarkButton()
{
    if (markButton_.isEmpty()) return;
    markButton_.clear();
    emit changed();
}

void PencilDuty::updateMarkHover()
{
    const int     hovered = wordAt(hoverCursorPos());
    const QPointF at      = cursorDip();

    // The button's own rectangle, while it stands: the geometry the window itself reports, so the
    // keep-alive never drifts from what is drawn -- placeX clamps at the screen edge, and a
    // rectangle recomputed here would sit over the word instead of over the button there
    // (docs/adr/0160). It is flush above the word, so the path from the word to it crosses no dead
    // ground and the union with the word box is contiguous.
    QRectF buttonRect;
    if (not markButton_.isEmpty() and markButtonSize_.width() > 0)
        buttonRect = QRectF{markButtonPos_, markButtonSize_};
    if (hovered < 0 and not buttonRect.contains(at)) {
        markLemma_.clear();
        markStreak_ = 0;
        hideMarkButton();
        return;
    }
    if (hovered < 0) return; // the pointer is on the button: it stays exactly where it is

    auto const& word = words_.at(hovered);
    if (word.lemma != markLemma_) {
        markLemma_ = word.lemma;
        // A line of dashed words sits shoulder to shoulder, so re-dwelling on every crossing would
        // make a standing button impossible to follow: once it stands, it moves to the word the
        // pointer is on at once. The first raise still waits out the dwell.
        markStreak_ = markButton_.isEmpty() ? 0 : kMarkHoverMs;
        if (markButton_.isEmpty()) {
            hideMarkButton();
            return;
        }
    }
    markStreak_ += kPencilTickMs;
    if (markStreak_ < kMarkHoverMs) return;

    QVariantMap next{{QStringLiteral("lemma"), word.lemma},
                     {QStringLiteral("x"), screen_.x() + (word.box.x() - screenOrigin_.x()) / scale_},
                     {QStringLiteral("y"), screen_.y() + (word.box.y() - screenOrigin_.y()) / scale_},
                     {QStringLiteral("width"), word.box.width() / scale_}};
    if (next != markButton_) {
        markButton_ = std::move(next);
        emit changed();
    }
}

void PencilDuty::refresh()
{
    int const hovered = altHeld_ ? wordAt(hoverCursorPos()) : -1;
    if (altHeld_)
        hideMarkButton(); // the signposts and the bubble own the pointer while Alt is held
    else
        updateMarkHover();

    QVariantList next;
    next.reserve(words_.size());
    for (int index = 0; index < words_.size(); ++index) {
        auto const& word = words_.at(index);
        // The signpost is read from the cache each time: an answer that landed since the last read shows now.
        QString const signpost = altHeld_ ? explanation_.signpostOf(word.lemma) : QString{};
        next.append(QVariantMap{
            {QStringLiteral("lemma"), word.lemma},
            {QStringLiteral("verdict"), word.verdict},
            {QStringLiteral("x"), (word.box.x() - screenOrigin_.x()) / scale_},
            {QStringLiteral("y"), (word.box.y() - screenOrigin_.y()) / scale_},
            {QStringLiteral("width"), word.box.width() / scale_},
            {QStringLiteral("height"), word.box.height() / scale_},
            {QStringLiteral("signpost"), signpost},
            {QStringLiteral("hovered"), index == hovered},
        });
    }

    // The bubble is asked for once per word the pointer comes onto, not on every tick it stays there.
    if (hovered < 0) {
        lookedUp_ = -1;
    } else if (hovered != lookedUp_) {
        lookedUp_        = hovered;
        auto const& word = words_.at(hovered);
        capture_.lookUpWord(word.surface, QPoint{word.box.left(), word.box.bottom()});
    }

    if (next != marks_) {
        marks_ = std::move(next);
        emit changed();
    }
}

}
