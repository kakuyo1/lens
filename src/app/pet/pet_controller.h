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
 * The idle lottery weighs each scene by the mood and by the accessories worn when it draws (ADR 0093, 0102). The
 * lottery, the entry timeline and the frame belong to PetStage (ADR 0123); this class reads both clocks, gathers the
 * offers and publishes what the stage answers.
 */

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "app/pet/pet_assets.h"
#include "app/pet/pet_catalog.h"
#include "app/pet/pet_ceremony.h"
#include "app/pet/pet_hide.h"
#include "app/pet/pet_placement.h"
#include "app/pet/pet_scenes.h"
#include "app/pet/pet_stage.h"
#include "app/pet/pet_stroll.h"
#include "app/pet/pet_throw.h"
#include "app/pet/pet_wander.h"
#include "core/document/pet_store.h"
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
    /// Whether the pet's window is on screen. Not the same as enabled: a pet being turned off keeps its window up
    /// for the goodbye, so the scene must bind visibility to this rather than to enabled.
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    Q_PROPERTY(bool passthrough READ passthrough NOTIFY settingsChanged)
    Q_PROPERTY(QString pet READ pet NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList pets READ pets NOTIFY catalogChanged)
    Q_PROPERTY(QVariantList refusals READ refusals NOTIFY catalogChanged)
    Q_PROPERTY(QString action READ action NOTIFY frameChanged)
    Q_PROPERTY(int frameIndex READ frameIndex NOTIFY frameChanged)
    Q_PROPERTY(int expression READ expression NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap sheets READ sheets NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap anchors READ anchors NOTIFY frameChanged)
    /// Canvas-pixel offset applied to the expression layer only, so the face can follow a nearby pointer or the
    /// review sign while the body stays put. Zero when the pointer is away, no question is up, and the pet is busy.
    Q_PROPERTY(QPoint gaze READ gaze NOTIFY gazeChanged)
    Q_PROPERTY(QVariantList wornAccessories READ wornAccessories NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap layers READ layers NOTIFY settingsChanged)
    Q_PROPERTY(int canvas READ canvas NOTIFY settingsChanged)
    Q_PROPERTY(int size READ size NOTIFY settingsChanged)
    Q_PROPERTY(double scale READ scale NOTIFY settingsChanged)
    Q_PROPERTY(double inkCentre READ inkCentre NOTIFY settingsChanged)
    /// The idle figure's ink box in the sprite's own canvas units, as {x, y, width, height}. The review sign stands outside it
    /// and on its bottom edge.
    Q_PROPERTY(QRectF inkBox READ inkBox NOTIFY settingsChanged)
    /// The pet's own review sign as {frames, every, beatMs, margin, foot, wordTop, wordColor, bins}; empty when it has none.
    Q_PROPERTY(QVariantMap reviewSign READ reviewSign NOTIFY settingsChanged)
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
    /// The entry choreography in progress (ADR 0101): `active`, `inFront`, `propAlpha`, `propOffset`, `idleAlpha`,
    /// `sceneAlpha`, the scene pose's `sceneSheets` / `sceneAnchors` / `sceneExpression`, and the landing `impact`
    /// `{kind, at, progress}`. Outside an entry the map holds neutral defaults; an impact may outlive `active`.
    Q_PROPERTY(QVariantMap sceneEntry READ sceneEntry NOTIFY frameChanged)
    Q_PROPERTY(QVariantList accessories READ accessories NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap wornIn READ wornIn NOTIFY outfitChanged)

public:
    /**
     * @param shippedRoot The data/pet folder that came with the install.
     * @param installedRoot The reader's pets folder, %APPDATA%\Lens\pets.
     * @param store The PET section of the settings document, owned by main().
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
    bool shown() const;
    bool passthrough() const;
    /// @return Whether a review question may go up: the pet is idle, or hiding at a screen edge and peeking, and
    ///         not being dragged, thrown, or in its landing's angry repeat. A hide does not block the sign; a reaction
    ///         or a reading finishes first (docs/adr/0076, 0090, 0092).
    bool availableForReview() const;
    /// @return The folder name of the pet the settings choose.
    QString pet() const;
    /// @return Every pet the settings page offers, as {id, name, external}. The name is the package's own for an installed pet.
    QVariantList pets() const;
    /**
     * @brief One still of a shipped pet's idle figure, for the card the picker raises beside its list (UI-SPEC.md 4.4).
     *
     * Read from the pet's own folder rather than from what is on screen, so the card can picture a pet that is not
     * the one in force. Only a shipped pet has one: an installed package is the reader's own art, and the card is the
     * app's picture of the pets it came with.
     *
     * @param id A folder name from pets().
     * @return {canvas, foot, sheets, anchors, expression}, keyed as the sprite layer reads them, or an empty map for
     *         an installed package, an unknown id, or a folder that will not load. Read once and kept until the next
     *         refresh().
     */
    Q_INVOKABLE QVariantMap portrait(QString id);
    /// @return Every installed package that was refused, as {id, reason}. The reason is the Refusal name, such as "BadName".
    QVariantList refusals() const;
    QString      action() const;
    int          frameIndex() const;
    int          expression() const;
    QVariantMap  sheets() const;
    QVariantMap  anchors() const;
    QPoint       gaze() const;
    QVariantList wornAccessories() const;
    QVariantMap  layers() const;
    int          canvas() const;
    int          size() const;
    double       scale() const;
    double       inkCentre() const;
    QRectF       inkBox() const;
    /// @return The review sign of the pet on screen, keyed as the QML reads it; empty when the pet has none.
    QVariantMap reviewSign() const;
    QPointF     foot() const;
    int         minSize() const;
    int         maxSize() const;
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
    QVariantMap sceneEntry() const;
    /// @return Every accessory in the catalogue as {id, slot, sceneWeights?}, for the settings page to lay out. The
    ///         weights include only scenes this pet plays; an accessory without one carries no sceneWeights key.
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
    /// @brief Shows an action in place of the machine's, from its first frame. Ignored for an unknown name.
    /// @param action Name in animations.json, such as "celebrate".
    Q_INVOKABLE void preview(QString action);
    /// @brief Plays a scene now, for its duration, then back to idle. Ignored for a name this pet has no scene for.
    /// @param id A scene id from scenes(), such as "swing".
    Q_INVOKABLE void setScene(QString id);
    Q_INVOKABLE void dragStart();
    Q_INVOKABLE void dragEnd();
    /**
     * @brief Ends a drag with the reader's release velocity.
     * @param velocity The pointer's velocity at release, in DIPs per second.
     * @return True when the pet was thrown and the controller owns its position; false for a drop, which the caller
     *         finishes with dragEnd() and savePosition(). A throw is ignored when motion is reduced, when the pet is
     *         still hidden at an edge, or when the release is too slow to be one. A landed throw stays
     *         where it lands and that spot becomes home; there is no walk back (ADR 0092).
     */
    Q_INVOKABLE bool release(QPointF velocity);
    Q_INVOKABLE void click();
    /// @brief Ends a stroll at once, as the pointer landing on the pet should. A hide is left alone: only a drag ends it.
    Q_INVOKABLE void stopWandering();
    /// @brief Turns the eyes toward the review sign, or the open card, while a question is up (ADR 0097).
    ///        The sign chooses its side from the room on screen, so only the direction travels, never the word.
    /// @param side "left" or "right" while the sign or its card is visible; empty clears the look.
    Q_INVOKABLE void setReviewLook(QString side);
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
    /// @brief Tells the ceremonies whether motion is reduced. The pet window mirrors Tokens.motion.reduced here, so the
    ///        Windows preference and the application's animation switch both collapse the ceremony to its end state.
    /// @param on True to play every ceremony instantly; a ceremony already running lands at once.
    Q_INVOKABLE void setMotionReduced(bool on);
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
    /// @brief The expression layer's gaze offset changed.
    void gazeChanged();
    void settingsChanged();
    /// @brief The pet's window went on screen or came off it (see shown()).
    void shownChanged();
    void outfitChanged();
    /// @brief The scan found different pets or refusals. Only the picker's lists depend on it, so it is not sent per setting.
    void catalogChanged();
    /// @brief An installed pet was chosen but not yet confirmed. The settings ask the reader, then call confirmPet().
    void confirmationRequested(QString id, QString name);
    /// @brief The chosen pet was gone or refused, so the shipped default is in use. Shown as a notice (docs/adr/0043).
    void fellBack(QString title, QString body);

private:
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
    /// @brief Moves the throw on: the window through its arc, then the landing's role and the angry repeat it owes.
    void updateThrow(std::chrono::milliseconds step);
    /// @brief Stops the throw by kind, putting the pet down where it is and ending the pickup pose.
    /// @param at Where the window ends.
    /// @param saveHome Whether `at` becomes the pet's home.
    /// @param why What interrupted it (ADR 0092).
    void endThrow(QPoint at, bool saveHome, ThrowStop why);
    /// @brief Waits for the next stroll, or moves the window through the current step and its pauses.
    void updateWander(std::chrono::milliseconds step);
    /// @brief Slides the window out of its edge, plays the peek loop, and resumes it after a reaction.
    void updateHiding(std::chrono::milliseconds step);
    /// @brief Executes what one stroll tick answered: the window, the walk loop, a hide, the return look.
    void runStrollStep(PetStroll::Step answer);
    /// @brief Ends the stroll where it is, stopping its walk loop.
    void cancelStroll();
    /// @brief Starts a hide against `side`: stops the walk, computes the hidden base, and plays the peek loop.
    void enterHiding(EdgeSide side);
    /// @brief Leaves a hide without moving the window: used when the pet is turned off or the layout changed.
    void endHiding();
    /// @brief Answers a click on the visible sliver: the hide slides back to where it began and ends there (ADR 0095).
    void lureOut();
    /// @brief Finishes a lured-out hide, putting the pet at `at` and making that spot its home (ADR 0095).
    /// @param at Where the call-out landed.
    void settleOutOfHide(QPoint at);
    /// @brief Recomputes a hide's base after the window or its screen changed.
    /// @param area The work area it hides on; the same one unless the screen changed.
    void refitHiding(QRect area);
    /// @return Whether this pet has the walk loop a ceremony moves with.
    bool canWalkCeremony() const;
    /// @brief Starts the entrance from the nearest edge of home, or from where the pet is when `fromHere`.
    void startEntrance(bool fromHere);
    /// @brief Starts the goodbye: to the nearest edge, a look, a heart, then off screen.
    void startLeave();
    /// @brief Starts the switch the reader chose: the old pet leaves, the pending one enters from the same edge.
    void startSwitch();
    /// @brief Moves the ceremony on and plays what it answers.
    void updateCeremony(std::chrono::milliseconds step);
    /// @brief The window, the pet and the reader's home as the ceremony reads them this tick.
    PetCeremony::Stage ceremonyStage() const;
    /// @brief The one-shot roles this pet can play, and how long each runs.
    PetCeremony::Roles ceremonyRoles() const;
    /// @return How long the action mapped to `role` runs, or nothing when the pet has no art for it.
    std::optional<std::chrono::milliseconds> rolePlayback(core::pet::Role role) const;
    /// @brief Executes what one ceremony tick answered, playing its fallbacks when a walk or a role cannot start.
    void runCeremonyStep(PetCeremony::Step answer);
    /// @brief Starts the pet's walk loop for a ceremony phase; false when the machine refuses it.
    bool playCeremonyWalk();
    /// @brief Plays one of the ceremony's one-shot roles; false when the machine refuses it.
    bool playCeremonyRole(core::pet::Role role);
    /// @brief Ends the ceremony on screen, with the pet at home and at rest.
    void settleAtHome();
    /// @brief Ends the ceremony at its outcome now: hides a goodbye, otherwise completes a pending switch and settles.
    void finishCeremonyNow();
    /// @brief Ends the ceremony where it is, with the pet still on screen.
    void abortCeremony();
    /// @brief Ends the ceremony and takes the window off screen.
    void hideNow();
    /// @brief Applies the pet a switch is waiting on, while the window is off screen.
    void applyPendingPet();
    /// @brief Records `at` as the pet's home, the stroll's geometry staying in step with it.
    void adoptHome(QPoint at);
    /// @brief Reads the screens now attached into the placement.
    void syncScreens();
    /// @brief Starts the entry choreography for a scene that is now the machine's action.
    void beginSceneEntry(Scene const& scene, core::pet::ActionName const& action);
    /// @brief Publishes a scene's props, transform and alignment from one frame; the only writer of the scene values.
    void applyScene(Scene const& scene, PetStage::Frame const& frame);
    /// @brief Publishes the pet's place in the scene; the only writer of the scene shift and scale.
    void publishSceneTransform(QPointF shift, double scale);
    /// @brief Returns the scene values to their neutral, so the QML stage draws no props.
    void clearScene();
    /// @brief Changes shown() and announces it when it moved.
    void setShown(bool on);
    /// @brief The moment now, for the idle turn: the local day stretch and whether an after-context is up (ADR 0093).
    core::pet::PetMood currentMood() const;
    /// @return The work area of the screen nearest `at`, or an invalid rectangle when no screen is attached.
    QRect areaAt(QPoint at) const;
    /// @return The reader's saved spot, on the screen the window was left on, when that screen is still attached
    ///         and the spot still holds the window. Nothing otherwise (ADR 0100).
    std::optional<QPoint> savedSpot(QSize size) const;
    /// @return The attached screen whose work area is exactly `area`, or null.
    QScreen* screenForArea(QRect area) const;
    /// @return The attached screen called `name`, or null.
    QScreen* screenNamed(QString const& name) const;
    /// @return Whether the pointer is inside the pet window, which suspends wandering unless clicks pass through.
    bool   cursorOnPet() const;
    double randomUnit();
    /// @brief Polls the cursor and turns proximity into eyes and, when the pet is idle, its hover reactions.
    void updateHover(std::chrono::milliseconds step);
    /// @brief The hidden pet's share of hover: eyes and a deeper lean toward a pointer near the visible sliver, but
    ///        never a greeting or a reaction (ADR 0095).
    void updateHiddenHover(std::chrono::milliseconds step);
    /// @brief Forgets the current approach and centers the eyes.
    void resetHover();
    /// @brief Changes the gaze offset and announces it when it moved.
    void setGaze(QPoint offset);
    /// @return The eye offset for a pointer at `cursor`, in canvas units, along the dominant axis only.
    QPoint gazeOffsetAt(QPointF cursor) const;
    /// @return The same offset measured against another figure's centre, such as the peek sliver's.
    QPoint gazeToward(QPointF cursor, double centreX, double middleY) const;
    /// @return Canvas pixels from `at` to the idle figure's ink box; zero inside it.
    double inkDistanceAt(QPointF at) const;
    /// @brief Reads the shown action and blink into the properties, and emits when the frame they show changed.
    void sync(std::chrono::milliseconds step);
    /// @brief Publish the frame properties and note that a visible frame was produced.
    void pulse();
    /// @brief Restart a stopped clock, or return a silently frozen pet to idle.
    void checkHealth();
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
    /// The picker's postcards by pet id: read on the first hover and dropped when the catalogue is rescanned.
    std::map<std::string, QVariantMap> portraits_;
    /// The app's own scenes, read once: each shipped pet plays the four its action table names. A package may use another
    /// canvas, so its action names are never read as scenes.
    PetScenes scenes_;
    bool      scenesAllowed_{false};

    std::optional<PetAssets>                    assets_;
    core::pet::PetStore&                        store_;
    std::unique_ptr<core::pet::PetStateMachine> machine_;
    std::unique_ptr<core::pet::Wardrobe>        wardrobe_;
    /// Proximity lives beside the action machine: it reads the cursor and asks the machine for an idle reaction.
    std::unique_ptr<core::pet::HoverSense> hover_;

    /// @brief The hide in progress: the slide, the peek, the pointer's lean, the ask's pause and the call-out.
    PetHide hide_;

    /// One random stream for the idle timer, the scene pick, the strolls and the hide roll.
    std::mt19937 random_{std::random_device{}()};
    /// The stroll geometry. Empty for a pet whose data has no movement block.
    std::unique_ptr<PetWander> wander_;
    /// The idle stroll: the wait, the steps, the pauses and the hide roll.
    PetStroll stroll_{[this] { return randomUnit(); }};
    /// Whether the reader is holding the pet, hide or not.
    bool dragging_{false};
    /// The throw in progress and the run of landings behind it. The state machine stays in its pickup pose until the
    /// landing releases it.
    PetThrow throw_{[this] { return randomUnit(); }};

    /// The pet's temperament, from its behavior block; the shipped defaults while no pet is loaded.
    core::pet::BehaviorSpec behavior_{};

    /// Whether the window is on screen. `enabled()` asks for the pet, this says where its window is: a goodbye keeps
    /// it true after the switch has gone off.
    bool shown_{false};
    /// The reader's reduced-motion answer, mirrored from Tokens.motion.reduced by the pet window.
    bool motionReduced_{false};
    /// The entrance, goodbye or switch in progress.
    PetCeremony ceremony_;
    /// The screens attached and the one home the pet belongs to.
    PetPlacement placement_;
    /// The pet the reader chose while the old one is still walking out; pet() reports it meanwhile.
    std::string pendingPet_;
    /// A drag that caught a goodbye keeps the pet under the pointer until the drop, then hides it.
    bool hideAfterDrag_{false};

    QTimer clock_;
    /// Watches a frame clock that is running but no longer producing a visible frame, and a clock
    /// that was stopped while the pet is still on screen. Recovery is logged; it is a last resort.
    QTimer   health_;
    QWindow* window_ = nullptr;
    /// The clock runs while either of these is set.
    bool desktopRunning_{false};
    bool previewing_{false};
    /// The action the settings preview shows in place of the machine's, when one is set.
    std::optional<core::pet::ActionName> preview_;

    /// The action the frame properties were last computed for, and how long it has played.
    core::pet::ActionName     shownAction_;
    std::chrono::milliseconds actionElapsed_{0};
    /// When a frame was last published, for the health watchdog.
    std::chrono::steady_clock::time_point lastFramePulse_{std::chrono::steady_clock::now()};
    /// Blink timing: the wait to the next blink, and the time into the one running.
    std::chrono::milliseconds blinkWait_{0};
    std::chrono::milliseconds blinkElapsed_{0};
    bool                      blinkRunning_{false};

    QString            action_;
    int                frameIndex_{0};
    int                expression_{0};
    QVariantMap        sheets_;
    core::pet::Anchors anchors_{};
    QPoint             gaze_;
    /// The review sign's side, or its open card's, while either is visible; empty when no question is up.
    QPoint reviewLook_;

    /// The scene the shown action names, and the frame's transform. All empty or neutral outside a scene.
    QString     scene_;
    int         sceneFrame_{0};
    QVariantMap sceneSheets_;
    QPointF     sceneShift_{};
    QPointF     sceneAlign_{};
    double      sceneScale_{1.0};
    QVariantMap sceneLayers_;
    /// The stage: the idle lottery, the entry timeline and the two after-contexts (ADR 0123). The controller reads
    /// both clocks, gathers the offers and publishes the frame it answers.
    PetStage stage_{[this] { return randomUnit(); }};
    /// The scene pose an entry shows beside the body it holds: the sheets, anchors and expression of the scene
    /// action's first frame, and the action whose body a hop or a settle holds until it lands.
    struct EntryPose {
        core::pet::ActionName poseAction;
        QVariantMap           sheets;
        QVariantMap           anchors;
        int                   expression{0};
    };
    std::optional<EntryPose> entryPose_;
    /// Whether an entry was dropped outside sync, so the next normal frame must republish the scene pose.
    bool sceneEntryDirty_{false};
};

} // namespace lens::app::pet
