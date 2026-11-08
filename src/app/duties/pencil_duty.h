#pragma once

/**
 * @file pencil_duty.h
 * @brief The pencil layer's state: the dashed line under each word the page holds, the signpost shown
 *        beside it while Alt is held, the bubble of the word under the pointer, and the floating mark
 *        button a resting pointer raises over a dashed word.
 *
 * The words come from CaptureDuty's page scan, placed on the page by the OCR and the page's text. Nothing
 * here reads the screen beyond the pointer's position, polled on a timer -- the layer itself takes no
 * input, so hover is geometry, not events (docs/adr/0160). Positions are kept in physical pixels until
 * they reach QML, where the layer window's device-independent coordinates are the ones the view uses.
 */

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <QtQml/qqmlregistration.h>

#include <chrono>

namespace lens::app {

class CaptureDuty;
class ExplanationDuty;

/**
 * @brief The duty behind the pencil layer window (UI-SPEC.md 4.20).
 *
 * Exposed to QML as `Controller.pencil`. The layer is visible while the scan has placed words on the
 * page, and it draws the signposts and takes the bubble only while the Alt key is held.
 */
class PencilDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.pencil")
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(bool altHeld READ altHeld NOTIFY changed)
    Q_PROPERTY(QRect screen READ screen NOTIFY changed)
    Q_PROPERTY(QVariantList marks READ marks NOTIFY changed)
    /// The floating mark button, when one stands: {lemma, x, y, width} -- the anchor word's top-left
    /// in device-independent screen coordinates and the word's width, which the button centres on.
    /// Empty when no button stands (docs/adr/0160).
    Q_PROPERTY(QVariantMap markButton READ markButton NOTIFY changed)

public:
    PencilDuty(CaptureDuty& capture, ExplanationDuty& explanation, QObject* parent = nullptr);

    /// @return Whether the layer has words to draw under: the scan placed some on the page.
    bool visible() const;
    /// @return Whether the Alt key is held.
    bool altHeld() const;
    /// @return The device-independent geometry of the monitor the layer covers.
    QRect screen() const;
    /// @return One entry per word: {lemma, verdict, x, y, width, height, signpost, hovered}, in
    ///         device-independent pixels relative to the layer. Verdict is "new" or "known" -- the
    ///         line the word draws. The signpost is empty unless Alt is held and one is stored.
    QVariantList marks() const;
    QVariantMap  markButton() const;

public slots:
    /// @brief The scan placed words on the page; @p words are {lemma, surface, verdict, x, y, width, height}
    ///        in physical pixels.
    /// @param screen       The monitor's geometry in device-independent pixels.
    /// @param screenOrigin The monitor's top-left in physical pixels.
    /// @param scale        The monitor's device pixels per device-independent pixel.
    void placeWords(QVariantList words, QRect screen, QPoint screenOrigin, double scale);
    /// @brief The placed words no longer describe the page; nothing is drawn until the next scan.
    void clear();
    /// @brief The Alt key went down: ask for the signposts of the words that have none stored yet.
    void altDown();
    /// @brief The Alt key came up: the signposts and the bubble go with it.
    void altUp();
    /// @brief The reader changed @p lemma's verdict: its occurrences take the new line, and a
    ///        mastered one leaves the layer at once -- the scan no longer places it.
    void verdictChanged(QString lemma, QString verdict);
    /// @brief The reader is holding the mark button's pill down: nothing may move or take the
    ///        button while that lasts, so a scan frame arriving mid-press cannot swallow the click
    ///        (docs/adr/0161). The QML pill reports both edges of the hold.
    void setMarkPressHold(bool held);

public:
    /// @brief Keep @p window out of screen captures each time it shows, so the scan never reads the
    ///        layer's own lines. @p passthrough also makes clicks fall through; the mark button's
    ///        window must take clicks, so it passes false (docs/adr/0160).
    Q_INVOKABLE void keepOutOfCapture(QObject* window, bool passthrough = true);
    /// @brief The button window reports its own geometry, so the keep-alive rectangle is the one
    ///        actually drawn -- placeX clamps at the screen edge, and a recomputed rectangle would
    ///        drift from the real window there (docs/adr/0160).
    Q_INVOKABLE void reportMarkButtonGeometry(QPointF pos, QSizeF size);
    /// @brief Test hook: pin the pointer the hover reads to @p physical, so a qtest can raise the
    ///        mark button without moving the reader's mouse. The pin holds until the next call;
    ///        tests pin elsewhere to simulate the pointer leaving.
    Q_INVOKABLE void pinCursorForTest(QPoint physical);

signals:
    void changed();

private:
    struct Word {
        QString lemma;
        QString surface;
        QString verdict = QStringLiteral("new"); ///< "new" or "known": which line the word draws.
        QRect   box;                             ///< Physical screen pixels.
    };

    /// @return The index of the word under @p at, a little generous about its edges; -1 when none.
    int wordAt(QPoint at) const;
    /// @brief Recompute the marks from the current words, the Alt state and the pointer, and say so when they changed.
    void refresh();
    /// @brief The non-Alt half of refresh(): a pointer resting on a word raises the mark button,
    ///        one that leaves the word and the button takes it back down (docs/adr/0160).
    void updateMarkHover();
    /// @brief Take the mark button down, when one stands.
    void hideMarkButton();
    /// @return The pointer the hover reads: the pinned one when a test pinned it, else the real cursor.
    QPoint cursorPos() const;
    /// @return The pointer in device-independent screen coordinates, the frame the mark button's
    ///         keep-alive rectangle lives in.
    QPointF cursorDip() const;

    CaptureDuty&     capture_;
    ExplanationDuty& explanation_;
    QVector<Word>    words_;
    QRect            screen_;
    QPoint           screenOrigin_;
    double           scale_      = 1.0;
    bool             altHeld_    = false;
    int              lookedUp_   = -1; ///< The word whose bubble was last asked for while Alt is held.
    int              markStreak_ = 0;  ///< Ticks the pointer has rested on markLemma_'s word.
    QString          markLemma_;       ///< The word the pointer's dwell is anchored to; a standing button follows it across the line.
    QVariantMap      markButton_;
    QPointF          markButtonPos_;          ///< The button window's own report of its position.
    QSizeF           markButtonSize_{96, 26}; ///< The button window's own report of its size, for the keep-alive rectangle.
    /// The standing button's word box, in device-independent pixels: the keep-alive's second half
    /// while the word is missing from the placed words (docs/adr/0161).
    QRectF markWordRect_{};
    /// When the button's word last went missing from the placed words; the grace runs from here,
    /// and a frame with the word back clears it (docs/adr/0161).
    std::chrono::steady_clock::time_point markMissingAt_ = {};
    bool                                  markPressHold_ = false; ///< The pill is held down: the button neither moves nor goes.
    /// The pinned pointer a test set, read instead of the real cursor while cursorPinned_ holds.
    bool         cursorPinned_ = false;
    QPoint       pinnedCursor_{};
    QVariantList marks_;
    QTimer       timer_;
};

}
