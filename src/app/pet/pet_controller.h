#pragma once

/**
 * @file pet_controller.h
 * @brief The Qt shell of the desktop pet: the one frame clock, the properties the scene draws, and the event entry
 *        (PHASE3 3.5, 3.7).
 *
 * The state machine in core chooses the action. This class advances the frame index and the blink, and turns the
 * machine's state into properties for QML. It receives event types only, never text or a selection.
 *
 * The one exception is the settings preview: preview() shows a chosen action in place of the machine's, and the
 * machine keeps running underneath. The same pet is on the desktop, so the preview shows there too while it lasts.
 *
 * A scene (ADR 0049) is a named action the app ships props for. While one plays, the scene* properties carry the
 * sheet urls, the pet's per-frame shift and scale, and the alignment between this pet's feet and the catalogue's.
 */

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "app/pet/pet_assets.h"
#include "app/pet/pet_catalog.h"
#include "app/pet/pet_scenes.h"
#include "core/document/pet_store.h"
#include "app/pet/pet_wander.h"
#include "core/domain/pet/accessory.h"
#include "core/domain/pet/hover_sense.h"
#include "core/domain/pet/pet_state.h"

class QJSEngine;
class QQmlEngine;
class QScreen;
class QWindow;

namespace lens::app::pet {

class PetController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Pet)
    QML_SINGLETON
    Q_PROPERTY(bool available READ available NOTIFY settingsChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY settingsChanged)
    Q_PROPERTY(bool passthrough READ passthrough NOTIFY settingsChanged)
    Q_PROPERTY(QString pet READ pet NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList pets READ pets NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList refusals READ refusals NOTIFY catalogChanged)
    Q_PROPERTY(QString action READ action NOTIFY frameChanged)
    Q_PROPERTY(int frameIndex READ frameIndex NOTIFY frameChanged)
    Q_PROPERTY(int expression READ expression NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap sheets READ sheets NOTIFY frameChanged)
    /// Which hand holds the sign, "left" or "right". The review sign chooses it and the pet draws that hand.
    Q_PROPERTY(QString handSide READ handSide NOTIFY handSideChanged)
    /// Where each hand grips the pole in the shown frame, in the sprite's own canvas units, keyed "left" and "right". Kept from
    /// the last frame that showed a hand, so the sign stays put while the pet does something else; empty until the pet has
    /// shown its ask action.
    Q_PROPERTY(QVariantMap handGrips READ handGrips NOTIFY frameChanged)
    /// Where each hand grips the pole at the start of the ask action, in the same units, keyed "left" and "right". The review
    /// sign chooses its hand from these, so the choice does not depend on which frame happens to be showing.
    Q_PROPERTY(QVariantMap handRest READ handRest NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap anchors READ anchors NOTIFY frameChanged)
    /// Canvas-pixel offset applied to the expression layer only, so the face can follow a nearby pointer while the
    /// body stays put. Zero when the pointer is away, the pet is busy, or the settings preview is open.
    Q_PROPERTY(QPoint gaze READ gaze NOTIFY gazeChanged)
    Q_PROPERTY(QVariantList wornAccessories READ wornAccessories NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap layers READ layers NOTIFY settingsChanged)
    Q_PROPERTY(int canvas READ canvas NOTIFY settingsChanged)
    Q_PROPERTY(int size READ size NOTIFY settingsChanged)
    Q_PROPERTY(double scale READ scale NOTIFY settingsChanged)
    Q_PROPERTY(double inkCentre READ inkCentre NOTIFY settingsChanged)
    /// Where the idle figure's feet are in the sprite's own 88 px canvas: a scene scales the pet about this.
    Q_PROPERTY(QPointF foot READ foot NOTIFY settingsChanged)
    Q_PROPERTY(int minSize READ minSize CONSTANT)
    Q_PROPERTY(int maxSize READ maxSize CONSTANT)
    Q_PROPERTY(QStringList actions READ actions NOTIFY settingsChanged)
    /// The scenes this pet can play, in catalogue order. Empty for an installed package (ADR 0049).
    Q_PROPERTY(QStringList scenes READ scenes NOTIFY settingsChanged)
    /// Non-empty while the pet is inside a scene (ADR 0049). The rest describe how the scene draws this frame.
    Q_PROPERTY(QString scene READ scene NOTIFY frameChanged)
    Q_PROPERTY(int sceneFrame READ sceneFrame NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap sceneSheets READ sceneSheets NOTIFY frameChanged)
    Q_PROPERTY(QPointF sceneShift READ sceneShift NOTIFY frameChanged)
    Q_PROPERTY(QPointF sceneAlign READ sceneAlign NOTIFY frameChanged)
    Q_PROPERTY(double sceneScale READ sceneScale NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap sceneLayers READ sceneLayers NOTIFY frameChanged)
    Q_PROPERTY(QVariantList accessories READ accessories NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap wornIn READ wornIn NOTIFY outfitChanged)

public:
    /**
     * @param shippedRoot The data/pet folder that came with the install.
     * @param installedRoot The reader's pets folder, %APPDATA%\Lens\pets.
     * @param store The PET section of the settings document, owned by main().
     * @param parent QObject parent.
     */
    PetController(std::filesystem::path shippedRoot, std::filesystem::path installedRoot, core::pet::PetStore& store, QObject* parent = nullptr);
    ~PetController() override;

    /// @brief Hand the QML engine the one instance built by the composition root.
    static void provide(PetController* instance);

    /// @brief Factory used by the QML singleton registration.
    static PetController* create(QQmlEngine* engine, QJSEngine* scriptEngine);

    /// @brief Applies one event to the state machine. Called by the composition root, which maps app signals to types.
    void handle(core::pet::PetEvent event);

    bool available() const;
    bool enabled() const;
    bool passthrough() const;
    /// @return Whether a review question may go up: the pet is idle, or hiding at a screen edge and peeking, and
    ///         not being dragged. A hide does not block the sign; a reaction or a reading finishes first
    ///         (docs/adr/0076).
    bool availableForReview() const;
    /// @return The folder name of the pet the settings choose.
    QString pet() const;
    /// @return Every pet the settings page offers, as {id, name, external}. The name is the package's own for an installed pet.
    QVariantList pets() const;
    /// @return Every installed package that was refused, as {id, reason}. The reason is the Refusal name, such as "BadName".
    QVariantList refusals() const;
    QString      action() const;
    int          frameIndex() const;
    int          expression() const;
    QVariantMap  sheets() const;
    QString      handSide() const;
    QVariantMap  handGrips() const;
    QVariantMap  handRest() const;
    QVariantMap  anchors() const;
    QPoint       gaze() const;
    QVariantList wornAccessories() const;
    QVariantMap  layers() const;
    int          canvas() const;
    int          size() const;
    double       scale() const;
    double       inkCentre() const;
    QPointF      foot() const;
    int          minSize() const;
    int          maxSize() const;
    /// @return Every action the pet can play, in the order of the state machine's enum. Scenes are not actions:
    ///         they have their own entries below and are played only by the idle turn.
    QStringList actions() const;
    QStringList scenes() const;
    QString     scene() const;
    int         sceneFrame() const;
    QVariantMap sceneSheets() const;
    QPointF     sceneShift() const;
    QPointF     sceneAlign() const;
    double      sceneScale() const;
    QVariantMap sceneLayers() const;
    /// @return Every accessory in the catalogue as {id, slot}, for the settings page to lay out.
    QVariantList accessories() const;
    /// @return The accessory worn in each slot, keyed by slot name; an empty string where the slot is empty.
    QVariantMap wornIn() const;

    /// @brief Stores the size the reader picked on the size slider.
    /// @param percent 0 for today's ×1, 100 for ×4; clamped to minSize..maxSize.
    Q_INVOKABLE void setSize(int percent);
    /// @brief Runs the desktop window's share of the frame clock. The window calls it with its visibility.
    Q_INVOKABLE void setRunning(bool on);
    /// @brief Runs the frame clock for the settings preview. Turning it off also ends any preview.
    Q_INVOKABLE void setPreviewing(bool on);
    /// @brief Chooses the hand that holds the sign. Anything but "left" or "right" is ignored.
    /// @param side "left" or "right".
    Q_INVOKABLE void setHandSide(QString side);
    /// @brief Shows an action in place of the machine's, from its first frame. Ignored for an unknown name.
    /// @param action Name in animations.json, such as "celebrate".
    Q_INVOKABLE void preview(QString action);
    /// @brief Plays a scene now, for its duration, then back to idle. Ignored for a name this pet has no scene for.
    /// @param id A scene id from scenes(), such as "swing".
    Q_INVOKABLE void setScene(QString id);
    Q_INVOKABLE void dragStart();
    Q_INVOKABLE void dragEnd();
    Q_INVOKABLE void click();
    /// @brief Ends a stroll at once, as the pointer landing on the pet should. A hide is left alone: only a drag ends it.
    Q_INVOKABLE void stopWandering();
    /// @param id Id of an accessory in the catalogue; puts it on, takes it off, or swaps it in its slot.
    Q_INVOKABLE void toggleAccessory(QString id);
    /// @brief Chooses a pet from pets(). An installed one the reader has not confirmed is not switched to: confirmationRequested
    ///        is emitted instead, and confirmPet() completes the choice.
    /// @param id A folder name from pets(). Anything else is ignored.
    Q_INVOKABLE void setPet(QString id);
    /// @brief Records the reader's confirmation of an installed pet, and switches to it.
    Q_INVOKABLE void confirmPet(QString id);
    /// @brief Rescans both folders. The settings call it each time the extensions section opens; a chosen pet that has gone
    ///        or been refused gives way to the shipped default, with a notice.
    Q_INVOKABLE void refresh();
    /// @brief Opens the reader's pets folder in the file manager, creating it first when it is not there yet.
    Q_INVOKABLE void openPacksFolder();
    Q_INVOKABLE void setEnabled(bool on);
    Q_INVOKABLE void setPassthrough(bool on);
    /// @param window The QML Window, whose native handle takes capture exclusion and pass-through.
    Q_INVOKABLE void attachWindow(QObject* window);
    /// @brief Keeps a QML window out of screen captures, as the pet's own is. The review sign needs it: the scan must not read its word.
    /// @param window The QML Window to exclude.
    Q_INVOKABLE void keepOutOfCapture(QObject* window);
    /// @return The mouse position in the same coordinates a window's x and y use.
    Q_INVOKABLE QPoint cursorPos() const;
    /// @return Where the window should open at this size: the saved spot if it still fits, else the primary screen's
    ///         corner. Also records that spot as the stroll's home.
    Q_INVOKABLE QPoint initialPosition(QSize size);
    /// @brief Nudges the window the least distance that keeps it whole on its screen, and saves where it landed.
    ///        Called after the size changes; does nothing before the window is attached.
    Q_INVOKABLE void fitOnScreen();
    /// @brief Records where the window was dropped, relative to the screen it landed on.
    Q_INVOKABLE void savePosition(QPoint at);

signals:
    void frameChanged();
    void handSideChanged();
    /// @brief The expression layer's gaze offset changed.
    void gazeChanged();
    void settingsChanged();
    void outfitChanged();
    /// @brief The scan found different pets or refusals. Only the picker's lists depend on it, so it is not sent per setting.
    void catalogChanged();
    /// @brief An installed pet was chosen but not yet confirmed. The settings ask the reader, then call confirmPet().
    void confirmationRequested(QString id, QString name);
    /// @brief The chosen pet was gone or refused, so the shipped default is in use. Shown as a notice (docs/adr/0043).
    void fellBack(QString title, QString body);

private:
    struct Hiding;

    /// @brief Loads the pet in folder `id` from the catalogue, and starts it from its first frame. A pet the catalogue does
    ///        not hold, or that fails to load, leaves the pet off.
    void loadPet(std::string const& id);
    /// @brief Stores `id` as the chosen pet and loads it. A pet that will not load keeps the one the reader had.
    void applyPet(std::string const& id);
    /// @brief Switches to the shipped default after `gone` disappeared or was refused, and says so unless `gone` was shipped.
    void switchToDefault(std::string const& gone);
    /// @return Whether the reader has confirmed the installed pet `id`.
    bool isConfirmed(std::string const& id) const;
    /// @return Whether the pet `id` may be loaded: a shipped pet always, an installed one only once the reader confirmed it.
    bool usable(std::string const& id) const;
    /// @return Whether the shipped folder holds a pet called `id`, whether or not it is valid.
    bool isShipped(std::string const& id) const;
    /// @brief Moves the state machine on by `step` and refreshes what the scene reads.
    void tick();
    /// @brief Waits for the next stroll, or moves the window through the current step and its pauses. Does nothing
    ///        while hidden, previewing, dragging, or busy.
    void updateWander(std::chrono::milliseconds step);
    /// @brief Slides the window out of its edge, plays the peek loop, and resumes it after a reaction.
    void updateHiding(std::chrono::milliseconds step);
    /// @brief Starts one walk cycle from where the window is now, or returns false when there is no room.
    bool beginStep();
    /// @brief Draws the step count and starts the first step.
    void startStroll();
    /// @brief Ends the stroll and draws the next long wait.
    void finishStroll();
    /// @brief Stops a walk in progress and returns the state machine to idle.
    void cancelStroll();
    /// @brief Starts a hide against `side`: stops the walk, computes the hidden base, and plays the peek loop.
    void enterHiding(EdgeSide side);
    /// @brief Leaves a hide without moving the window: used when the pet is turned off or the layout changed.
    void endHiding();
    /// @brief Puts the window back at its hidden base after a drag that did not pull it out far enough.
    void snapBackToHide();
    /// @brief Recomputes a hide's base after the window changed size.
    void refitHiding();
    /// @return A fresh wait before the next stroll, 120 to 300 seconds.
    std::chrono::milliseconds nextWanderWait();
    /// @return How long the pet pauses between two steps, by gait.
    std::chrono::milliseconds nextPause();
    /// @return The work area of the screen nearest `at`, or an invalid rectangle when no screen is attached.
    QRect areaAt(QPoint at) const;
    /// @return The attached screen whose work area is exactly `area`, or null.
    QScreen* screenForArea(QRect area) const;
    /// @return The attached screen called `name`, or null.
    QScreen* screenNamed(QString const& name) const;
    /// @brief Recomputes how far a hidden window lets itself in while the sign is up, from the inner hand's grip.
    void updateAskShift(Hiding& hidden) const;
    /// @return Whether the pointer is inside the pet window, which suspends wandering unless clicks pass through.
    bool   cursorOnPet() const;
    double randomUnit();
    /// @brief Polls the cursor and turns proximity into eyes and, when the pet is idle, its hover reactions.
    void updateHover(std::chrono::milliseconds step);
    /// @brief Forgets the current approach and centers the eyes.
    void resetHover();
    /// @brief Changes the gaze offset and announces it when it moved.
    void setGaze(QPoint offset);
    /// @return The eye offset for a pointer at `cursor`, in canvas units, along the dominant axis only.
    QPoint gazeOffsetAt(QPointF cursor) const;
    /// @return Canvas pixels from `at` to the idle figure's ink box; zero inside it.
    double inkDistanceAt(QPointF at) const;
    /// @brief Reads the shown action and blink into the properties, and emits when the frame they show changed.
    void sync(std::chrono::milliseconds step);
    /// @brief Starts the clock while the window or the preview needs it, and stops it otherwise.
    void updateClock();
    /// @return The action on screen: the preview's while one is set, else the machine's.
    core::pet::ActionName shownAction() const;
    /// @return The action called `name` in the data, if there is one.
    std::optional<core::pet::ActionName> actionNamed(QString const& name) const;
    /// @return Whether `id` is an accessory the catalogue knows.
    bool knownAccessory(QString const& id) const;
    /// @return Whether the catalogue puts the accessory `id` in `slot`.
    bool wearsInSlot(std::string const& id, core::pet::Slot slot) const;
    /// @brief Connects a screen's geometry changes to keepOnScreen().
    void watchScreen(QScreen* screen);
    /// @brief Runs a rule that needs the window's native handle, and never takes that handle before the
    ///        window has been on screen. Asking a QWindow for its handle creates the platform window, and
    ///        one created before its first show keeps the transparent pixels of its square black.
    /// @param window The QML window to watch. It is the connection's own context, so the rule cannot outlive it.
    /// @param rule What to do with the window, on every show from now on.
    void onShown(QWindow* window, std::function<void(QWindow const&)> const& rule);
    /// @brief Moves the window back into a visible area when the screens changed under it (PHASE3 3.5).
    void keepOnScreen();

    static PetController* instance_;

    /// The shipped pets, and the reader's folder. The catalogue is what both scans last found.
    std::filesystem::path shippedRoot_;
    std::filesystem::path installedRoot_;
    PetCatalog            catalog_;
    /// The app's own scenes, read once: every shipped pet plays the same five. A package may use another
    /// canvas, so its action names are never read as scenes.
    PetScenes scenes_;
    bool      scenesAllowed_{false};

    std::optional<PetAssets>                    assets_;
    core::pet::PetStore&                        store_;
    std::unique_ptr<core::pet::PetStateMachine> machine_;
    std::unique_ptr<core::pet::Wardrobe>        wardrobe_;
    /// Proximity lives beside the action machine: it reads the cursor and asks the machine for an idle reaction.
    std::unique_ptr<core::pet::HoverSense> hover_;

    /// @brief A hide in progress. `base` is the window's top-left once fully slid out; `from` is where it started.
    struct Hiding {
        EdgeSide side;
        /// The screen it hid on, by device name, so a taskbar or DPI change on that screen does not end the hide.
        QString                   screen;
        QRect                     area;
        QPoint                    base;
        QPoint                    from;
        std::chrono::milliseconds slide{0};
        std::chrono::milliseconds peekClock{0};
        /// How far the window moves inward while the sign is up, so the inner hand and its pole stay on screen.
        int askShift{0};
        /// Whether a review question set the inward shift; it lasts through the reaction that follows.
        bool asking{false};
        bool dragging{false};
    };
    enum class StrollPhase : std::uint8_t {
        Waiting,
        Walking,
        Pausing,
    };

    /// One random stream for the idle timer, the scene pick, the strolls and the hide roll.
    std::mt19937 random_{std::random_device{}()};
    /// The stroll geometry. Empty for a pet whose data has no movement block.
    std::unique_ptr<PetWander> wander_;
    StrollPhase                stroll_{StrollPhase::Waiting};
    std::optional<WanderStep>  step_;
    std::chrono::milliseconds  stepElapsed_{0};
    std::chrono::milliseconds  stepDuration_{0};
    std::chrono::milliseconds  pauseLeft_{0};
    std::chrono::milliseconds  wanderWait_{0};
    /// Steps still to take in this stroll, including the one running.
    int stepsLeft_{0};
    /// Whether the pointer is on the pet and has already voided this wait.
    bool hoverPause_{false};
    /// Whether the home point is known; false after a load and until the window has a position.
    bool homeReady_{false};
    /// Whether the reader is holding the pet, hide or not.
    bool                  dragging_{false};
    std::optional<Hiding> hiding_;

    QTimer   clock_;
    QWindow* window_ = nullptr;
    /// The clock runs while either of these is set.
    bool desktopRunning_{false};
    bool previewing_{false};
    /// The action the settings preview shows in place of the machine's, when one is set.
    std::optional<core::pet::ActionName> preview_;

    /// The action the frame properties were last computed for, and how long it has played.
    core::pet::ActionName     shownAction_;
    std::chrono::milliseconds actionElapsed_{0};
    /// Blink timing: the wait to the next blink, and the time into the one running.
    std::chrono::milliseconds blinkWait_{0};
    std::chrono::milliseconds blinkElapsed_{0};
    bool                      blinkRunning_{false};

    QString            action_;
    int                frameIndex_{0};
    int                expression_{0};
    QVariantMap        sheets_;
    QString            handSide_{QStringLiteral("left")};
    QVariantMap        handGrips_;
    QVariantMap        handRest_;
    core::pet::Anchors anchors_{};
    QPoint             gaze_;

    /// The scene the shown action names, and the frame's transform. All empty or neutral outside a scene.
    QString     scene_;
    int         sceneFrame_{0};
    QVariantMap sceneSheets_;
    QPointF     sceneShift_{};
    QPointF     sceneAlign_{};
    double      sceneScale_{1.0};
    QVariantMap sceneLayers_{};
};

} // namespace lens::app::pet
