/** @file pencil_duty.cpp
 * @brief The pencil layer's words, the signposts and the hover, read on a timer while Alt is held.
 */
#include "pencil_duty.h"

#include <QStringList>
#include <QVariantMap>
#include <QWindow>

#include <utility>

#include "app/platform/hover_word_lookup.h"
#include "app/platform/window_native.h"
#include "capture_duty.h"
#include "explanation_duty.h"
#include "util/log.h"

namespace lens::app {
namespace {

/// How often the signposts and the pointer are read while Alt is held: a stored signpost shows within
/// this, and nothing is read between the Alt presses.
constexpr int kPencilTickMs = 120;

}

void PencilDuty::keepOutOfCapture(QObject* const object)
{
    auto* const window = qobject_cast<QWindow*>(object);
    if (window == nullptr) return;
    auto const rule = [](QWindow const& shown) {
        if (not excludeFromCapture(shown)) LENS_WARN("the pencil layer can appear in screen captures: Windows refused the exclusion");
        // Clicks pass through by the Win32 style: Qt.WindowTransparentForInput painted this full-screen layer black.
        setPassthrough(shown, true);
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

void PencilDuty::placeWords(QVariantList const words, QRect const screen, QPoint const screenOrigin, double const scale)
{
    words_.clear();
    for (auto const& value : words) {
        auto const map = value.toMap();
        words_.push_back({.lemma   = map.value(QStringLiteral("lemma")).toString(),
                          .surface = map.value(QStringLiteral("surface")).toString(),
                          .box     = QRect{map.value(QStringLiteral("x")).toInt(), map.value(QStringLiteral("y")).toInt(), map.value(QStringLiteral("width")).toInt(), map.value(QStringLiteral("height")).toInt()}});
    }
    screen_       = screen;
    screenOrigin_ = screenOrigin;
    scale_        = scale > 0 ? scale : 1.0;
    lookedUp_     = -1;
    if (altHeld_) {
        QStringList lemmas;
        for (auto const& word : words_)
            lemmas << word.lemma;
        explanation_.prefetchSignposts(lemmas);
        timer_.start();
    }
    refresh();
}

void PencilDuty::clear()
{
    words_.clear();
    lookedUp_ = -1;
    timer_.stop();
    refresh();
}

void PencilDuty::altDown()
{
    if (altHeld_) return;
    altHeld_  = true;
    lookedUp_ = -1;
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
    timer_.stop();
    refresh();
}

int PencilDuty::wordAt(QPoint const at) const
{
    // The scan's boxes are the glyphs' own; the hit area takes a little of the line above and below.
    for (int index = 0; index < words_.size(); ++index)
        if (words_.at(index).box.adjusted(-2, -6, 2, 6).contains(at)) return index;
    return -1;
}

void PencilDuty::refresh()
{
    int const hovered = altHeld_ ? wordAt(hoverCursorPos()) : -1;

    QVariantList next;
    next.reserve(words_.size());
    for (int index = 0; index < words_.size(); ++index) {
        auto const& word = words_.at(index);
        // The signpost is read from the cache each time: an answer that landed since the last read shows now.
        QString const signpost = altHeld_ ? explanation_.signpostOf(word.lemma) : QString{};
        next.append(QVariantMap{
            {QStringLiteral("lemma"), word.lemma},
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
