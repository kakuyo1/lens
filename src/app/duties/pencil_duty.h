#pragma once

/**
 * @file pencil_duty.h
 * @brief The pencil layer's state: the dashed line under each word the page holds, the signpost shown
 *        beside it while Alt is held, and the bubble of the word under the pointer.
 *
 * The words come from CaptureDuty's page scan, placed on the page by the OCR and the page's text. Nothing
 * here reads the screen. Positions are kept in physical pixels until they reach QML, where the layer
 * window's device-independent coordinates are the ones the view uses.
 */

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include <QtQml/qqmlregistration.h>

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

public:
    PencilDuty(CaptureDuty& capture, ExplanationDuty& explanation, QObject* parent = nullptr);

    /// @return Whether the layer has words to draw under: the scan placed some on the page.
    bool visible() const;
    /// @return Whether the Alt key is held.
    bool altHeld() const;
    /// @return The device-independent geometry of the monitor the layer covers.
    QRect screen() const;
    /// @return One entry per word: {lemma, x, y, width, height, signpost, hovered}, in device-independent
    ///         pixels relative to the layer. The signpost is empty unless Alt is held and one is stored.
    QVariantList marks() const;

public slots:
    /// @brief The scan placed words on the page; @p words are {lemma, surface, x, y, width, height} in physical pixels.
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

public:
    /// @brief Keep the layer window out of screen captures each time it shows, so the scan never reads its own lines.
    Q_INVOKABLE void keepOutOfCapture(QObject* window);

signals:
    void changed();

private:
    struct Word {
        QString lemma;
        QString surface;
        QRect   box; ///< Physical screen pixels.
    };

    /// @return The index of the word under @p at, a little generous about its edges; -1 when none.
    int wordAt(QPoint at) const;
    /// @brief Recompute the marks from the current words, the Alt state and the pointer, and say so when they changed.
    void refresh();

    CaptureDuty&     capture_;
    ExplanationDuty& explanation_;
    QVector<Word>    words_;
    QRect            screen_;
    QPoint           screenOrigin_;
    double           scale_    = 1.0;
    bool             altHeld_  = false;
    int              lookedUp_ = -1; ///< The word whose bubble was last asked for while Alt is held.
    QVariantList     marks_;
    QTimer           timer_;
};

}
