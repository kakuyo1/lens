#pragma once

/**
 * @file glass_backdrop.h
 * @brief The desktop the glass cards are drawn over, and the image provider that hands it to QML.
 *
 * The desktop is taken as a sheet a little larger than the window, so that a drag moves the card over the same
 * picture instead of taking a new one (docs/adr/0039). The picture is kept in memory only: it is never written to
 * disk, never logged, and dropped when the glass themes are off (docs/adr/0034, docs/adr/0035). QML reads the
 * blurred sheet through `image://glass/sheet` and the liquid theme's displacement map through `image://glass/map`.
 * Both are images, not QML types, so the surfaces need no item class of their own.
 */

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QRect>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <atomic>

namespace lens::app {

class GlassBackdrop : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.glassBackdrop")
    /// Whether the text over this picture should be light. Decided on every capture.
    Q_PROPERTY(bool lightText READ lightText NOTIFY pictureChanged)
    /// Changes with every capture, so QML's image cache never serves an earlier picture.
    Q_PROPERTY(int serial READ serial NOTIFY pictureChanged)
    /// The rectangle of the desktop the picture covers, in device-independent pixels. Empty when none is held.
    Q_PROPERTY(QRect sheet READ sheet NOTIFY pictureChanged)

public:
    explicit GlassBackdrop(QObject* parent = nullptr);
    ~GlassBackdrop() override;

    bool  lightText() const;
    int   serial() const;
    QRect sheet() const;

    /// @brief Make sure the picture covers @p window, taking a new sheet only when the window has come near its edge.
    /// @param window The window the glass is drawn in, in device-independent coordinates.
    /// @param fillAlpha The white the card sits under, 0 to 1; the text tone is judged over the window.
    /// @note A window that is not on one screen clears the picture rather than stitching one.
    Q_INVOKABLE void follow(QRect window, double fillAlpha);
    /// @brief Whether the picture still covers @p window with room to spare, so that no new sheet is needed.
    Q_INVOKABLE bool covers(QRect window) const;
    /// @brief Drop the picture.
    Q_INVOKABLE void clear();
    /// @brief Keep @p window out of screen captures while the glass shows, so the picture needs no hiding of the card.
    /// @param window The settings window. Not a QWindow in QML, so it is checked here.
    /// @param excluded True to keep it out of captures; false to take the exclusion back.
    /// @return True when the window is now excluded. False off Windows, before the window has a native handle, or when
    ///         the OS refuses; the caller then hides the card for each capture as before.
    Q_INVOKABLE bool keepOutOfCapture(QObject* window, bool excluded);
    /// @brief Refresh the sheet about fifteen times a second, so video behind the window shows through the glass.
    /// @param on True to start refreshing; false to stop.
    /// @note Meant for the window kept out of captures: a refresh of a window that is not would photograph the card.
    Q_INVOKABLE void setLive(bool on);

    /// @return A copy of the picture, empty when none is held. Safe to call from the image provider's thread.
    QImage picture() const;

signals:
    void pictureChanged();

private:
    /// @brief Take a picture of the desktop under @p region, the sheet, and judge the text tone over @p window.
    /// @note A region that is empty or crosses two screens clears the picture rather than stitching one.
    void capture(QRect region, QRect window, double fillAlpha);
    /// @brief Start one refresh of the sheet on the pool. A tick that finds the last one still running is skipped.
    void refreshLive();
    /// @brief Hold a new sheet and its text tone, and announce it to QML.
    void publish(QRect region, QRect screen, QImage picture, bool lightText);

    mutable QMutex mutex_;
    QImage         image_;
    bool           lightText_ = false;
    int            serial_    = 0;
    QRect          sheet_;
    /// The geometry of the screen the sheet was taken on: a side on that edge has nothing beyond it to read.
    QRect sheetScreen_;
    /// Whether keepOutOfCapture() last kept its window out, so that each change is logged once.
    bool keptOut_ = false;
    /// The card the glass is drawn in, and its fill, for a refresh to judge the text tone over.
    QRect  window_;
    double fillAlpha_ = 0.0;
    /// Bumped whenever the sheet is retaken or dropped, so a refresh that began before the change is discarded.
    int generation_ = 0;
    /// Drives the refresh while setLive() is on.
    QTimer live_;
    /// Set while a refresh is on the pool; cleared on the GUI thread when its result comes back.
    std::atomic<bool> liveBusy_{false};
    /// Runs the desktop reads off the GUI thread. One thread: a read is a few milliseconds and two would only contend.
    QThreadPool pool_;
};

/**
 * @brief Serves `image://glass/sheet?` and `image://glass/map?` for the glass cards.
 *
 * The sheet query carries the blur and the saturation and a serial that changes with each capture; the map query
 * carries the card's size and corner radius. Each request runs on the worker thread, so a slider drag does not wait
 * on the GUI thread.
 *
 * Requests arrive on the image loader's worker threads, which is why the picture is copied out of
 * GlassBackdrop under its lock rather than shared.
 */
class GlassImageProvider final : public QQuickImageProvider {
public:
    explicit GlassImageProvider(GlassBackdrop const& backdrop);

    QImage requestImage(QString const& id, QSize* size, QSize const& requestedSize) override;

private:
    /// The liquid theme's map for this card, rebuilt only when the card's size, corners or scale change.
    QImage cachedMap(QSize size, double radius, double scale);

    GlassBackdrop const& backdrop_;
    QMutex               mapMutex_;
    QString              mapKey_;
    QImage               map_;
};

} // namespace lens::app
