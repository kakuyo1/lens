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
 */

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "app/pet/pet_assets.h"
#include "app/pet/pet_catalog.h"
#include "app/pet/pet_store.h"
#include "core/pet/accessory.h"
#include "core/pet/pet_state.h"

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
    Q_PROPERTY(QVariantMap anchors READ anchors NOTIFY frameChanged)
    Q_PROPERTY(QVariantList wornAccessories READ wornAccessories NOTIFY frameChanged)
    Q_PROPERTY(QVariantMap layers READ layers NOTIFY settingsChanged)
    Q_PROPERTY(int canvas READ canvas NOTIFY settingsChanged)
    Q_PROPERTY(int scale READ scale NOTIFY settingsChanged)
    Q_PROPERTY(double inkCentre READ inkCentre NOTIFY settingsChanged)
    Q_PROPERTY(int minScale READ minScale CONSTANT)
    Q_PROPERTY(int maxScale READ maxScale CONSTANT)
    Q_PROPERTY(QStringList actions READ actions NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList accessories READ accessories NOTIFY settingsChanged)
    Q_PROPERTY(QVariantMap wornIn READ wornIn NOTIFY outfitChanged)

public:
    /**
     * @param shippedRoot The data/pet folder that came with the install.
     * @param installedRoot The reader's pets folder, %APPDATA%\Lens\pets.
     * @param store The PET section of the settings document, owned by main().
     * @param parent QObject parent.
     */
    PetController(std::filesystem::path shippedRoot, std::filesystem::path installedRoot, PetStore& store, QObject* parent = nullptr);
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
    QVariantMap  anchors() const;
    QVariantList wornAccessories() const;
    QVariantMap  layers() const;
    int          canvas() const;
    int          scale() const;
    double       inkCentre() const;
    int          minScale() const;
    int          maxScale() const;
    /// @return Every action the pet can play, in the order of the state machine's enum.
    QStringList actions() const;
    /// @return Every accessory in the catalogue as {id, slot}, for the settings page to lay out.
    QVariantList accessories() const;
    /// @return The accessory worn in each slot, keyed by slot name; an empty string where the slot is empty.
    QVariantMap wornIn() const;

    /// @brief Stores the magnification the reader picked on the size slider.
    Q_INVOKABLE void setScale(int times);
    /// @brief Runs the desktop window's share of the frame clock. The window calls it with its visibility.
    Q_INVOKABLE void setRunning(bool on);
    /// @brief Runs the frame clock for the settings preview. Turning it off also ends any preview.
    Q_INVOKABLE void setPreviewing(bool on);
    /// @brief Shows an action in place of the machine's, from its first frame. Ignored for an unknown name.
    /// @param action Name in animations.json, such as "celebrate".
    Q_INVOKABLE void preview(QString action);
    Q_INVOKABLE void dragStart();
    Q_INVOKABLE void dragEnd();
    Q_INVOKABLE void click();
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
    /// @return The mouse position in the same coordinates a window's x and y use.
    Q_INVOKABLE QPoint cursorPos() const;
    /// @return Where the window should open at this size: the saved spot if it still fits, else the primary screen's corner.
    Q_INVOKABLE QPoint initialPosition(QSize size) const;
    /// @brief Records where the window was dropped, relative to the screen it landed on.
    Q_INVOKABLE void savePosition(QPoint at);

signals:
    void frameChanged();
    void settingsChanged();
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
    /// @brief Moves the window back into a visible area when the screens changed under it (PHASE3 3.5).
    void keepOnScreen();

    static PetController* instance_;

    /// The shipped pets, and the reader's folder. The catalogue is what both scans last found.
    std::filesystem::path shippedRoot_;
    std::filesystem::path installedRoot_;
    PetCatalog            catalog_;

    std::optional<PetAssets>                    assets_;
    PetStore&                                   store_;
    std::unique_ptr<core::pet::PetStateMachine> machine_;
    std::unique_ptr<core::pet::Wardrobe>        wardrobe_;
    QTimer                                      clock_;
    QWindow*                                    window_ = nullptr;
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
    core::pet::Anchors anchors_{};
};

} // namespace lens::app::pet
