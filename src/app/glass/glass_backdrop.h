#pragma once

/**
 * @file glass_backdrop.h
 * @brief The desktop the glass cards are drawn over: one sheet per glass surface, and the image provider that hands
 *        them to QML.
 *
 * Each glass surface (the settings window, the tray menu, a panel, a bubble) has its own GlassSheet: the desktop taken
 * as a sheet a little larger than its window, so a drag moves the card over the same picture instead of taking a new
 * one (docs/adr/0039). The sheets share one reader: one worker thread and one refresh timer, so however many surfaces
 * are live, a GDI screen is read about fifteen times a second in all (docs/adr/0041). A screen with a live frame source
 * -- a duplicated output (docs/adr/0072) or Windows Graphics Capture when duplication refuses (docs/adr/0087) -- is read
 * from that source instead, up to the screen's refresh rate, and only when the desktop behind the card has actually
 * changed. The pictures are kept in memory only: never written to disk, never logged, and
 * dropped when the glass themes are off (docs/adr/0034, docs/adr/0035). QML receives each sheet through GlassSheetItem,
 * one live texture replaced in place, and the liquid theme's displacement map through
 * `image://glass/<surface>/map` (docs/adr/0071).
 */

#include "glass_math.h"
#include "glass_source.h"
#include "app/glass_settings.h"

#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QRect>
#include <QThreadPool>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

class QScreen;

namespace lens::app {

class GlassBackdrop;
class GlassCapture;
class GlassDuplication;

/**
 * @brief The desktop under one glass surface's window, and the text tone the card's text takes over it.
 *
 * A sheet is taken by follow() and refreshed while setLive() is on. It holds the rectangle it was taken at, which the
 * shader reads its window's place from, and a serial that changes with each picture, which keeps QML's image cache
 * honest.
 */
class GlassSheet : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by GlassBackdrop; reached as Controller.glassBackdrop.surface(name)")
    /// The surface's name, as the image URLs and the surfaces spell it.
    Q_PROPERTY(QString name READ name CONSTANT)
    /// Whether the text over this picture should be light. Read from the backdrop; a live refresh keeps it
    /// until the other tone reads decisively better (settleTone).
    Q_PROPERTY(bool lightText READ lightText NOTIFY pictureChanged)
    /// Changes with every capture, so a reader can tell one picture from the next.
    Q_PROPERTY(int serial READ serial NOTIFY pictureChanged)
    /// The rectangle of the desktop the picture covers, in device-independent pixels. Empty when none is held.
    Q_PROPERTY(QRect sheet READ sheet NOTIFY pictureChanged)
    /// Whether the card wants the sheet refreshed: a live source at up to the screen's refresh rate on the Smooth
    /// tier, about fifteen a second from GDI. A card that draws no backdrop turns it off and keeps the sheet for the text
    /// tone alone (docs/adr/0069). The Eco tier keeps the refresh timer off even while this is true (docs/adr/0073), so
    /// the setting page reads the tier, not this property, to say what is happening.
    Q_PROPERTY(bool live READ live NOTIFY liveChanged)
    /// Whether the picture in force came from a source that follows frames (duplication or Windows Graphics Capture).
    /// The settings page reads this to say what the Smooth and Auto tiers are doing on the screen the panel is on
    /// (docs/adr/0073, docs/adr/0087).
    Q_PROPERTY(bool duplicated READ duplicated NOTIFY captureChanged)
    /// Whether every live source was tried for this sheet and the screen refused or lost them, leaving GDI. The settings
    /// page's note says so in words, without an error code.
    Q_PROPERTY(bool duplicationRefused READ duplicationRefused NOTIFY captureChanged)

public:
    GlassSheet(QString name, GlassBackdrop* owner);

    QString name() const;
    bool    lightText() const;
    int     serial() const;
    QRect   sheet() const;
    /// @return How many pictures publish() has put in force. Counted apart from serial(), which also moves when the
    ///         picture is dropped.
    int publishes() const;
    /// @return Whether the card wants the sheet refreshed; the Eco tier may keep the timer off regardless. Read on the
    ///         GUI thread only, like setLive().
    bool live() const;
    /// @return Whether the picture in force came from a duplicated frame. GUI thread only.
    bool duplicated() const;
    /// @return Whether a duplication attempt for this sheet failed. GUI thread only.
    bool duplicationRefused() const;

    /// @brief Make sure the picture covers @p window, taking a new sheet only when the window has come near its edge.
    /// @param window The window the glass is drawn in, in device-independent coordinates.
    /// @param fillAlpha The white the card sits under, 0 to 1; the text tone is judged over the window.
    /// @note A window that is not on one screen clears the picture rather than stitching one.
    Q_INVOKABLE void follow(QRect window, double fillAlpha);
    /// @brief The same, off the GUI thread, for a window being dragged: a retake reads the desktop for some 20 ms, and
    ///        a drag that ran it here would stutter. The picture in force serves until the new one lands.
    /// @param window The window the glass is drawn in, in device-independent coordinates.
    /// @param fillAlpha The white the card sits under, 0 to 1; the text tone is judged over the window.
    /// @note A read already in flight for the same sheet is kept; the next move asks again. Meant for a window that is
    ///       kept out of captures, since the read happens while the window is showing.
    Q_INVOKABLE void followDeferred(QRect window, double fillAlpha);
    /// @brief Whether the picture still covers @p window with room to spare, so that no new sheet is needed.
    Q_INVOKABLE bool covers(QRect window) const;
    /// @brief Drop the picture, and stop refreshing it.
    Q_INVOKABLE void clear();
    /// @brief Keep @p window out of screen captures while the glass shows, so the picture needs no hiding of the card.
    /// @param window The surface's window. Not a QWindow in QML, so it is checked here.
    /// @param excluded True to keep it out of captures; false to take the exclusion back.
    /// @return True when the window is now excluded. False off Windows, before the window has a native handle, or when
    ///         the OS refuses; the caller then hides the card for each capture as before.
    Q_INVOKABLE bool keepOutOfCapture(QObject* window, bool excluded);
    /// @brief Refresh the sheet while it is live, so video behind the window shows through the glass.
    /// @param on True to start refreshing; false to stop.
    /// @note Meant for a window kept out of captures: a refresh of a window that is not would photograph the card.
    Q_INVOKABLE void setLive(bool on);

    /// @return A copy of the picture, empty when none is held. Safe to call from the image provider's thread.
    QImage picture() const;

signals:
    void pictureChanged();
    /// @brief The sheet started or stopped refreshing, so the backdrop starts or stops its timer.
    void liveChanged();
    /// @brief The picture's source or the duplication refusal changed, for the settings page's note.
    void captureChanged();

private:
    friend class GlassBackdrop;

    /// @return Whether a deferred read is in flight for the picture in force. GUI thread only.
    bool reading() const
    {
        return pending_ != 0 and pending_ == generation_;
    }

    /// @brief Take a picture of the desktop under @p region, the sheet, and judge the text tone over @p window.
    /// @note A region that is empty or crosses two screens clears the picture rather than stitching one.
    void capture(QRect region, QRect window, double fillAlpha);
    /// @brief Hold a new sheet and the tone its read settles on, and announce it to QML.
    /// @param window The card the tone was judged over.
    /// @param fillAlpha The white the card sits under. Together with the picture's fingerprint they say whether a
    ///        refresh has anything new to put in force.
    /// @param fingerprint The picture's fingerprint when its source took one on the worker, or 0 to take it here.
    /// @param read The backdrop's two 10th-percentile contrasts.
    /// @param fresh True for a sheet just taken, which its read decides outright; false for a live refresh of a settled
    ///        sheet, whose tone gives way only to a decisive read that repeats (settleTone).
    /// @param duplicated Whether the picture came from a duplicated frame rather than GDI.
    void publish(QRect           region,
                 QRect           screen,
                 QRect           window,
                 double          fillAlpha,
                 QImage          picture,
                 ToneRead const& read,
                 quint64         fingerprint,
                 bool            fresh,
                 bool            duplicated = false);
    /// @brief Record whether the last duplication attempt was refused, and tell QML if that changed.
    void noteDuplicationRefused(bool refused);

    QString        name_;
    mutable QMutex mutex_;
    QImage         image_;
    bool           lightText_ = false;
    int            serial_    = 0;
    int            publishes_ = 0;
    QRect          sheet_;
    /// The geometry of the screen the sheet was taken on: a side on that edge has nothing beyond it to read.
    QRect sheetScreen_;
    /// Whether keepOutOfCapture() last kept its window out, so that each change is logged once.
    bool keptOut_ = false;
    /// The card the glass is drawn in, and its fill, for a refresh to judge the text tone over.
    QRect  window_;
    double fillAlpha_ = 0.0;
    /// Reads in a row that have asked for the other text tone, for settleTone()'s hold. Reset by a fresh sheet.
    int toneStreak_ = 0;
    /// The fingerprint of the picture in force, from pictureFingerprint(): a refresh that produces the same one again
    /// is the same sheet and is not published a second time. It is also the card and the fill that picture was judged
    /// under, since the tone comes from the window's place within the sheet and not from the sheet as a whole.
    quint64 fingerprint_ = 0;
    QRect   publishedWindow_;
    double  publishedFillAlpha_ = 0.0;
    /// Bumped whenever the sheet is retaken or dropped, so a refresh that began before the change is discarded.
    int generation_ = 0;
    /// The generation of a deferred read still on the pool, or 0 when none is in flight. Only followDeferred() starts
    /// one, and the GUI thread owns this field; a live refresh skips the sheet while it is set.
    int pending_ = 0;
    /// Set while setLive() is on. Only the GUI thread reads or writes it.
    bool live_ = false;
    /// Whether the picture in force came from duplication, and whether duplication was refused for this sheet.
    bool duplicated_         = false;
    bool duplicationRefused_ = false;
    /// The backdrop that owns this sheet, and the pool a deferred read runs on.
    GlassBackdrop* owner_ = nullptr;
};

/**
 * @brief Owns the glass sheets of every glass surface, and reads the desktop for whichever of them is live.
 *
 * The surfaces are a fixed set, so the sheets are created once and the image provider can look them up from its own
 * threads without a lock. One refresh runs at a time: each tick reads the next live sheet in turn (nextLive), so the
 * cost is fixed however many surfaces show glass. A screen with a working duplication session is served from its frames
 * and sets the tick to the fast pace: sixty a second on Automatic, the screen's own refresh period on Smooth. The GDI
 * screens keep the old fifteen-a-second budget, shared between them, so a duplicated screen never makes a GDI read pay
 * for a frame it cannot use.
 */
class GlassBackdrop : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.glassBackdrop")

public:
    explicit GlassBackdrop(QObject* parent = nullptr);
    ~GlassBackdrop() override;

    /// @brief The sheet of the named glass surface, or nothing when no surface has that name.
    /// @param name One of the surface names: "settings", "tray", "stats", and so on.
    Q_INVOKABLE GlassSheet* surface(QString const& name) const;
    /**
     * @brief Set the reader's quality tier: what the live refresh may read, and how quickly (docs/adr/0073).
     * @param quality The tier; Auto and Smooth may duplicate, Standard is GDI, Eco stops the live refresh.
     * @note Takes effect at once: a tier that stopped the refresh restarts it, and one that stopped it leaves the
     *       picture on screen as it was.
     */
    void setQuality(GlassQuality quality);

    /// @return The tier in force.
    GlassQuality quality() const;

private:
    friend class GlassSheet;

    /// One screen's capture sessions and the order its reads keep: the duplicated output while it can, Windows
    /// Graphics Capture next, GDI otherwise. The rectangle is the key: two monitors of the same model report the same
    /// name, and their screens do not overlap.
    struct Session {
        QRect                             desktop;
        std::unique_ptr<GlassDuplication> duplication;
        std::unique_ptr<GlassCapture>     capture;
        std::unique_ptr<FallbackSource>   chain;
    };

    /// @brief Start the next refresh on the pool, for the next live sheet in turn. A tick that finds one still running
    ///        is skipped; a GDI sheet is only due again once the shared fifteen-a-second budget has elapsed.
    void refreshNext();
    /// @brief Run the timer while any sheet is live, stop it when none is, and set the pace its reads allow.
    void updateLive();
    /// @brief The session for @p screen, opened lazily and reused. GUI thread only.
    Session& sessionFor(QScreen const& screen);

    /// Every sheet, in the order the surfaces are listed. The sheets are children of this object.
    std::vector<GlassSheet*> sheets_;
    /// The index of the sheet the last refresh served, so the next one starts after it.
    std::size_t cursor_ = 0;
    /// Set while the refresh is on the pool; cleared on the GUI thread when its result comes back.
    bool busy_ = false;
    /// Set while a chained read is feeding itself on the Smooth tier. The GUI timer's busy ticks are its normal state,
    /// not a skip, and counting them would drown the tally (docs/adr/0074).
    bool chaining_ = false;
    /// Drives the refresh while any sheet is live.
    QTimer live_;
    /// Runs the desktop reads off the GUI thread. One thread: a read is a few milliseconds and two would only contend.
    QThreadPool pool_;
    /// The GDI source a screen without duplication reads through, and that a fresh sheet and a drag's retake use on
    /// every screen. Declared before the sessions: their chains hold a reference to it.
    GdiSource gdi_;
    /// One session per screen that has needed one, opened on the worker's first read and reused after.
    std::vector<Session> duplications_;
    /// The GDI reads' shared clock: a screen without duplication is read at most once per old refresh period, however
    /// fast the timer runs for another screen's duplicated frames.
    QElapsedTimer gdiPace_;
    /// The reader's tier; only the GUI thread reads or writes it.
    GlassQuality quality_ = GlassQuality::Auto;
    /// How long the current summary window has been open. The tally is logged and restarted when it expires, on the
    /// tick rather than on its own timer: a tick is what proves the refresh is actually running.
    QElapsedTimer window_;
    /// Whether the screen inventory has been logged, so it is written once for the process.
    bool screensLogged_ = false;
};

/**
 * @brief Serves `image://glass/<surface>/map?` for the liquid theme's displacement map.
 *
 * The map query carries the card's size and corner radius and is rebuilt only when those change; the sheet itself goes
 * through GlassSheetItem, not this provider (docs/adr/0071). Each request runs on the worker thread, so a slider drag
 * does not wait on the GUI thread.
 *
 * Requests arrive on the image loader's worker threads, which is why the picture is copied out of
 * GlassSheet under its lock rather than shared.
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
