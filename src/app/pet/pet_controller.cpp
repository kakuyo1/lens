/**
 * @file pet_controller.cpp
 * @brief Implementation of PetController: the frame clock, the blink, and what the scene reads.
 */

#include "app/pet/pet_controller.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <random>
#include <stdexcept>
#include <utility>

#include <QCoreApplication>
#include <QCursor>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QWindow>

#include "app/paths.h"
#include "app/pet/pet_placement.h"
#include "app/platform/window_native.h"
#include "core/domain/pet/pet_package.h"
#include "util/log.h"

namespace lens::app::pet {

using core::pet::kDefaultPet;
using core::pet::kMaxSize;
using core::pet::kMinSize;
using core::pet::magnificationOf;
using core::pet::Outfit;
using core::pet::PetStore;
using core::pet::SavedPosition;
using core::pet::sizeFromScale;

PetController* PetController::instance_ = nullptr;

namespace {

using core::pet::PetEvent;
using core::pet::Slot;

/// One tick of the single frame clock. Frames are at most 24 fps, so a tick is never longer than a frame.
constexpr std::chrono::milliseconds kTick{20};
/// Time between blinks while the pet is awake. The prototype blinks on the same cadence.
constexpr std::chrono::milliseconds kBlinkEvery{4000};
/// The wait between two strolls while truly idle: 120 to 300 seconds, counted in idle time (ADR 0076).
constexpr std::chrono::milliseconds kWanderWaitMin{120'000};
constexpr std::chrono::milliseconds kWanderWaitMax{300'000};

QPoint toPoint(core::pet::Point const& point)
{
    return QPoint{point.x, point.y};
}

/// @return The grips of the ask action's first frame, keyed "left" and "right": where the sign hangs at the start of a question.
QVariantMap restGripsOf(PetAssets const& assets)
{
    auto       rest = QVariantMap{};
    auto const hold = assets.animations.playbook.roles.find(core::pet::Role::Ask);
    if (hold == assets.animations.playbook.roles.end()) return rest;
    auto const& pictures = assets.sheets.at(hold->second);
    if (not pictures.gripRight.empty()) rest["right"] = pictures.gripRight.front();
    if (not pictures.gripLeft.empty()) rest["left"] = pictures.gripLeft.front();
    return rest;
}

/// @return The point an accessory in `slot` is drawn at, for one frame's anchors.
QPoint pointFor(core::pet::Anchors const& anchors, Slot slot)
{
    switch (slot) {
        case Slot::Head:
            return toPoint(anchors.head);
        case Slot::Face:
            return toPoint(anchors.face);
        case Slot::Body:
            return toPoint(anchors.body);
    }
    return {};
}

/// @return The file as a URL string, which is what an Image source takes. A Windows path would read as a scheme.
QString urlOf(QString const& path)
{
    return QUrl::fromLocalFile(path).toString();
}

/// @return What the store should hold for the accessories the wardrobe is wearing.
Outfit outfitOf(core::pet::Wardrobe const& wardrobe)
{
    return Outfit{wardrobe.wornIn(Slot::Head), wardrobe.wornIn(Slot::Face), wardrobe.wornIn(Slot::Body)};
}

/// @return The slot's name as the data and the settings page spell it.
QString slotName(Slot slot)
{
    switch (slot) {
        case Slot::Head:
            return QStringLiteral("head");
        case Slot::Face:
            return QStringLiteral("face");
        case Slot::Body:
            return QStringLiteral("body");
    }
    return {};
}

/// @return The refusal's name as the settings page spells it. The page turns the name into the reader's sentence.
QString reasonName(core::pet::Refusal reason)
{
    switch (reason) {
        case core::pet::Refusal::Unreadable:
            return QStringLiteral("Unreadable");
        case core::pet::Refusal::Format:
            return QStringLiteral("Format");
        case core::pet::Refusal::TooLarge:
            return QStringLiteral("TooLarge");
        case core::pet::Refusal::BadName:
            return QStringLiteral("BadName");
        case core::pet::Refusal::MissingRole:
            return QStringLiteral("MissingRole");
        case core::pet::Refusal::BadAnimation:
            return QStringLiteral("BadAnimation");
        case core::pet::Refusal::BadAnchors:
            return QStringLiteral("BadAnchors");
        case core::pet::Refusal::BadAccessory:
            return QStringLiteral("BadAccessory");
        case core::pet::Refusal::BadAssetType:
            return QStringLiteral("BadAssetType");
        case core::pet::Refusal::SheetSize:
            return QStringLiteral("SheetSize");
        case core::pet::Refusal::ExpressionIndex:
            return QStringLiteral("ExpressionIndex");
        case core::pet::Refusal::PathEscapes:
            return QStringLiteral("PathEscapes");
        case core::pet::Refusal::IdTaken:
            return QStringLiteral("IdTaken");
    }
    return {};
}

} // namespace

PetController::PetController(std::filesystem::path shippedRoot, std::filesystem::path installedRoot, PetStore& store, QObject* parent)
    : QObject(parent), shippedRoot_(std::move(shippedRoot)), installedRoot_(std::move(installedRoot)), store_(store)
{
    clock_.setInterval(kTick);
    connect(&clock_, &QTimer::timeout, this, &PetController::tick);
    // The scenes sit beside the shipped pets: data/pet next to data/scenes.
    scenes_           = PetScenes::load(shippedRoot_.parent_path() / "scenes");
    catalog_          = scanPets(shippedRoot_, installedRoot_);
    auto const chosen = store_.pet();
    if (usable(chosen)) {
        loadPet(chosen);
    } else {
        switchToDefault(chosen);
    }
}

PetController::~PetController() = default;

void PetController::loadPet(std::string const& id)
{
    // A hidden window belongs to the pet that is going away; the replacement starts from the reader's home, not
    // from the off-screen sliver the old pet was hiding in.
    if (hiding_ and window_ and wander_ and homeReady_) window_->setPosition(wander_->home());

    // What is on screen belongs to the pet that is going away, so none of it carries over.
    preview_.reset();
    hiding_.reset();
    stroll_ = StrollPhase::Waiting;
    step_.reset();
    stepsLeft_  = 0;
    hoverPause_ = false;
    homeReady_  = false;
    dragging_   = false;
    wander_.reset();
    stepDuration_ = std::chrono::milliseconds{0};
    action_.clear();
    shownAction_.clear();
    sheets_.clear();
    handGrips_.clear();
    handRest_.clear();
    anchors_      = {};
    frameIndex_   = 0;
    expression_   = 0;
    blinkRunning_ = false;
    blinkWait_    = kBlinkEvery;
    lastScene_.clear();
    scene_.clear();
    sceneSheets_.clear();
    sceneLayers_.clear();
    sceneFrame_ = 0;
    sceneShift_ = {};
    sceneAlign_ = {};
    sceneScale_ = 1.0;
    // Scenes are the app's props, drawn for the shipped pets' canvas; a package never gets them.
    scenesAllowed_ = isShipped(id);

    machine_.reset();
    wardrobe_.reset();
    hover_.reset();
    setGaze({});
    try {
        auto const* entry = catalog_.find(id);
        if (not entry) throw std::runtime_error{"the pet is not in the catalogue: " + id};
        assets_ = PetAssets::load(entry->directory);
    } catch (std::exception const& e) {
        assets_.reset();
        LENS_ERROR("the desktop pet is off: {}", e.what());
    }
    if (assets_) {
        // The idle timer, the scene pick, the strolls and the hide roll draw from one stream. A scene is offered
        // only to a shipped pet: a package may use another canvas size, and the app's props are drawn for 88.
        auto random = [this] { return randomUnit(); };
        auto scenes = [this, id]() -> std::optional<core::pet::ActionName> {
            if (not isShipped(id)) return std::nullopt;
            auto const& playbook = assets_->animations.playbook;
            auto        choices  = std::vector<core::pet::ActionName>{};
            for (auto const& scene : scenes_.all())
                if (playbook.actions.count(scene.id) != 0) choices.push_back(scene.id);
            // A four-scene pool reads as a pool only if the same one does not come straight back.
            if (choices.size() > 1)
                std::erase(choices, lastScene_);
            if (choices.empty()) return std::nullopt;
            auto const index = std::min(static_cast<std::size_t>(randomUnit() * static_cast<double>(choices.size())),
                                        choices.size() - 1);
            lastScene_       = choices[index];
            return choices[index];
        };
        machine_  = std::make_unique<core::pet::PetStateMachine>(assets_->animations.playbook, random, scenes);
        wardrobe_ = std::make_unique<core::pet::Wardrobe>(assets_->catalogue);
        handRest_ = restGripsOf(*assets_);

        if (auto const& movement = assets_->animations.movement) {
            wander_          = std::make_unique<PetWander>(gaitOf(movement->style), movement->range, random);
            auto const& walk = assets_->animations.playbook.actions.at(movement->walk);
            stepDuration_    = std::chrono::milliseconds{walk.frames * 1000 / walk.fps};
            wanderWait_      = nextWanderWait();
        }

        auto timing = core::pet::HoverTiming{};
        if (auto const& hover = assets_->animations.hover) {
            timing.noticeDelay = hover->noticeDelay;
            timing.dwellDelay  = hover->dwellDelay;
            timing.cooldown    = hover->cooldown;
        }
        hover_ = std::make_unique<core::pet::HoverSense>(timing);

        auto const outfit = store_.outfit();
        for (auto const& [slot, worn] : {std::pair{Slot::Head, outfit.head}, std::pair{Slot::Face, outfit.face}, std::pair{Slot::Body, outfit.body}}) {
            if (worn and wearsInSlot(*worn, slot)) wardrobe_->toggle(*worn);
        }
        sync(std::chrono::milliseconds{0});
    }
    updateClock();
}

void PetController::provide(PetController* instance)
{
    instance_ = instance;
}

PetController* PetController::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    Q_ASSERT(instance_ != nullptr);
    QQmlEngine::setObjectOwnership(instance_, QQmlEngine::CppOwnership);
    return instance_;
}

void PetController::handle(PetEvent event)
{
    if (not machine_) return;
    // An outside event voids the current wait: the next stroll is drawn again once the pet is idle.
    wanderWait_ = nextWanderWait();
    if (hiding_ and event == PetEvent::ReviewAsked) {
        // Let the window in before the sign window lays itself out, so the hand it chooses is the one on screen.
        hiding_->asking    = true;
        hiding_->slide     = PetWander::kHideSlide;
        hiding_->peekClock = std::chrono::milliseconds{0};
        if (window_) window_->setPosition(hiding_->base + QPoint{hiding_->askShift, 0});
    }
    resetHover();
    machine_->handle(event);
    sync(std::chrono::milliseconds{0});
}

bool PetController::available() const
{
    return assets_.has_value();
}

bool PetController::enabled() const
{
    return available() and store_.enabled();
}

bool PetController::passthrough() const
{
    return store_.passthrough();
}

bool PetController::availableForReview() const
{
    if (not enabled() or not machine_ or dragging_) return false;
    // A hidden pet may still be asked while it is peeking: the sign goes up at the edge and the hide survives the
    // answer. A reaction or a reading plays out first, as it does for an idle pet.
    return machine_->idle() or (hiding_ and machine_->ambient());
}

double PetController::randomUnit()
{
    return std::uniform_real_distribution<double>{0.0, 1.0}(random_);
}

QString PetController::pet() const
{
    return QString::fromStdString(store_.pet());
}

QVariantList PetController::pets() const
{
    auto list = QVariantList{};
    for (auto const& entry : catalog_.pets) {
        list.append(QVariantMap{{"id", QString::fromStdString(entry.id)}, {"name", entry.name}, {"external", entry.source == PetSource::Installed}});
    }
    return list;
}

QVariantList PetController::refusals() const
{
    auto list = QVariantList{};
    for (auto const& refused : catalog_.refused) {
        list.append(QVariantMap{{"id", refused.id}, {"reason", reasonName(refused.reason)}});
    }
    return list;
}

QString PetController::action() const
{
    return action_;
}

int PetController::frameIndex() const
{
    return frameIndex_;
}

int PetController::expression() const
{
    return expression_;
}

QVariantMap PetController::sheets() const
{
    return sheets_;
}

QString PetController::handSide() const
{
    return handSide_;
}

QVariantMap PetController::handGrips() const
{
    return handGrips_;
}

QVariantMap PetController::handRest() const
{
    return handRest_;
}

void PetController::setHandSide(QString side)
{
    if ((side != QStringLiteral("left") and side != QStringLiteral("right")) or side == handSide_) return;
    handSide_ = side;
    emit handSideChanged();
}

QVariantMap PetController::anchors() const
{
    return QVariantMap{{"body", toPoint(anchors_.body)}, {"face", toPoint(anchors_.face)}};
}

QPoint PetController::gaze() const
{
    return gaze_;
}

QVariantList PetController::wornAccessories() const
{
    QVariantList worn;
    if (not wardrobe_) return worn;

    auto const& catalogue = assets_->catalogue;
    for (auto const& id : wardrobe_->visibleFor(shownAction())) {
        auto const spec = std::find_if(catalogue.begin(), catalogue.end(), [&id](auto const& candidate) { return candidate.id == id; });
        if (spec == catalogue.end()) continue;
        auto const& art = assets_->accessories.at(id);
        auto const  at  = pointFor(anchors_, spec->slot);
        worn.append(QVariantMap{{"asset", urlOf(art.asset)}, {"zIndex", art.zIndex}, {"x", at.x()}, {"y", at.y()}});
    }
    return worn;
}

QStringList PetController::actions() const
{
    auto names = QStringList{};
    if (not assets_) return names;
    // The actions that play a role come first, in role order, so the settings pills keep the pet's order; then the rest.
    auto const& playbook = assets_->animations.playbook;
    for (auto const& entry : playbook.roles) {
        auto const name = QString::fromStdString(entry.second);
        if (not names.contains(name)) names.append(name);
    }
    for (auto const& entry : playbook.actions) {
        // A scene is a prop rig the idle turn draws on, not a chip in the settings page.
        if (scenesAllowed_ and scenes_.find(entry.first) != nullptr) continue;
        auto const name = QString::fromStdString(entry.first);
        if (not names.contains(name)) names.append(name);
    }
    return names;
}

QStringList PetController::scenes() const
{
    auto ids = QStringList{};
    if (not assets_ or not scenesAllowed_) return ids;
    auto const& playbook = assets_->animations.playbook;
    for (auto const& scene : scenes_.all()) {
        if (playbook.actions.count(scene.id) != 0) ids.append(QString::fromStdString(scene.id));
    }
    return ids;
}

QString PetController::scene() const
{
    return scene_;
}

int PetController::sceneFrame() const
{
    return sceneFrame_;
}

QVariantMap PetController::sceneSheets() const
{
    return sceneSheets_;
}

QPointF PetController::sceneShift() const
{
    return sceneShift_;
}

QPointF PetController::sceneAlign() const
{
    return sceneAlign_;
}

double PetController::sceneScale() const
{
    return sceneScale_;
}

QVariantMap PetController::sceneLayers() const
{
    return sceneLayers_;
}

QVariantList PetController::accessories() const
{
    auto list = QVariantList{};
    if (not assets_) return list;
    for (auto const& spec : assets_->catalogue) {
        list.append(QVariantMap{{"id", QString::fromStdString(spec.id)}, {"slot", slotName(spec.slot)}});
    }
    return list;
}

QVariantMap PetController::wornIn() const
{
    auto worn = QVariantMap{};
    if (not wardrobe_) return worn;
    for (auto const slot : {Slot::Head, Slot::Face, Slot::Body}) {
        worn.insert(slotName(slot), QString::fromStdString(wardrobe_->wornIn(slot).value_or(std::string{})));
    }
    return worn;
}

QVariantMap PetController::layers() const
{
    if (not assets_) return {};
    return QVariantMap{{"body", assets_->layers.body}, {"expression", assets_->layers.expression}, {"effect", assets_->layers.effect}};
}

int PetController::canvas() const
{
    return assets_ ? assets_->canvas : 0;
}

int PetController::size() const
{
    // A reader who has never moved the slider gets the package's declared 1..4 scale, as a percentage.
    return store_.size(assets_ ? sizeFromScale(assets_->scale) : kMinSize);
}

double PetController::scale() const
{
    return magnificationOf(size());
}

double PetController::inkCentre() const
{
    return assets_ ? assets_->inkCentre : 0.0;
}

QPointF PetController::foot() const
{
    return assets_ ? QPointF{assets_->inkCentre, assets_->inkBottom} : QPointF{};
}

int PetController::minSize() const
{
    return kMinSize;
}

int PetController::maxSize() const
{
    return kMaxSize;
}

void PetController::setSize(int percent)
{
    store_.setSize(percent);
    emit settingsChanged();
}

void PetController::setRunning(bool on)
{
    desktopRunning_ = on;
    if (not on) {
        cancelStroll();
        endHiding();
        resetHover();
    }
    updateClock();
}

void PetController::setPreviewing(bool on)
{
    previewing_ = on;
    resetHover();
    if (not on and preview_) {
        preview_.reset();
        sync(std::chrono::milliseconds{0});
    }
    updateClock();
}

void PetController::preview(QString action)
{
    auto const picked = actionNamed(action);
    if (not picked) return;
    // From the first frame even when it is the action already shown, so a second press plays it again.
    preview_       = picked;
    actionElapsed_ = std::chrono::milliseconds{0};
    resetHover();
    sync(std::chrono::milliseconds{0});
}

void PetController::setScene(QString id)
{
    if (not machine_) return;
    auto const name = id.toStdString();
    // A scene is real, not a preview: the machine plays it for its duration and then returns to idle. A card's
    // preview, if one was showing, would otherwise keep it off screen.
    preview_.reset();
    resetHover();
    lastScene_ = name;
    machine_->playScene(name);
    sync(std::chrono::milliseconds{0});
}

void PetController::dragStart()
{
    dragging_ = true;
    if (stroll_ != StrollPhase::Waiting) cancelStroll();
    if (hiding_) hiding_->dragging = true;
    handle(PetEvent::DragStart);
}

void PetController::dragEnd()
{
    dragging_ = false;
    if (hiding_) {
        auto& hidden    = *hiding_;
        hidden.dragging = false;
        auto const at   = window_ ? window_->position() : hidden.base;
        auto const size = window_ ? window_->size() : QSize{};
        if (window_ and PetWander::draggedOut(at, size, hidden.area)) {
            auto const area = hidden.area;
            hiding_.reset();
            handle(PetEvent::DragEnd);
            if (wander_) {
                wander_->setHome(at);
                wander_->setBounds(size, area);
            }
            homeReady_  = true;
            stroll_     = StrollPhase::Waiting;
            wanderWait_ = nextWanderWait();
        } else {
            // Not far enough out: the pet keeps hiding, and the drag leaves no trace in the settings.
            handle(PetEvent::DragEnd);
            snapBackToHide();
        }
        return;
    }

    handle(PetEvent::DragEnd);
    if (wander_ and window_) {
        wander_->setHome(window_->position());
        wander_->setBounds(window_->size(), areaAt(window_->position()));
        homeReady_ = true;
    }
    stroll_     = StrollPhase::Waiting;
    wanderWait_ = nextWanderWait();
}

void PetController::click()
{
    // A hidden pet only peeks and is dragged out: a click neither wakes it nor plays the poke reaction.
    if (hiding_) return;
    handle(PetEvent::Click);
}

void PetController::stopWandering()
{
    if (stroll_ != StrollPhase::Waiting) cancelStroll();
    if (hiding_) return;
    wanderWait_ = nextWanderWait();
    hoverPause_ = true;
}

void PetController::toggleAccessory(QString id)
{
    if (not wardrobe_ or not knownAccessory(id)) return;
    wardrobe_->toggle(id.toStdString());
    store_.setOutfit(outfitOf(*wardrobe_));
    emit frameChanged();
    emit outfitChanged();
}

void PetController::setPet(QString id)
{
    auto const name  = id.toStdString();
    auto const entry = catalog_.find(name);
    if (not entry) return;
    if (entry->source == PetSource::Installed and not isConfirmed(name)) {
        emit confirmationRequested(id, entry->name);
        return;
    }
    applyPet(name);
}

void PetController::confirmPet(QString id)
{
    auto const name = id.toStdString();
    if (not catalog_.find(name)) return;
    store_.confirm(name);
    applyPet(name);
}

void PetController::refresh()
{
    catalog_          = scanPets(shippedRoot_, installedRoot_);
    auto const chosen = store_.pet();
    if (not usable(chosen)) switchToDefault(chosen);
    emit catalogChanged();
    emit settingsChanged();
    emit frameChanged();
    emit outfitChanged();
}

void PetController::openPacksFolder()
{
    auto error = std::error_code{};
    std::filesystem::create_directories(installedRoot_, error);
    if (error) LENS_WARN("the pets folder could not be created: {}", error.message());
    QDesktopServices::openUrl(QUrl::fromLocalFile(widen(installedRoot_)));
}

void PetController::applyPet(std::string const& id)
{
    auto const previous = store_.pet();
    store_.setPet(id);
    loadPet(id);
    // A pet that will not load keeps the one the reader had, so the picker stays usable.
    if (not assets_) {
        store_.setPet(previous);
        loadPet(previous);
    }
    // Accessories the new pet does not have leave the saved outfit too, so they do not come back
    // when the reader switches again.
    if (wardrobe_) store_.setOutfit(outfitOf(*wardrobe_));
    emit settingsChanged();
    emit frameChanged();
    emit outfitChanged();
}

void PetController::switchToDefault(std::string const& gone)
{
    store_.setPet(kDefaultPet);
    loadPet(kDefaultPet);
    if (isShipped(gone)) return;

    // Queued, so the notice is raised once the caller has connected to the signal; at start-up that is after the constructor.
    auto const title = QCoreApplication::translate("SettingsPopup", "Desktop companion");
    auto const body  = QCoreApplication::translate("SettingsPopup", "This pet package is no longer installed. Switched to %1.")
                          .arg(QCoreApplication::translate("SettingsPopup", "Dog"));
    QTimer::singleShot(0, this, [this, title, body] { emit fellBack(title, body); });
}

bool PetController::usable(std::string const& id) const
{
    auto const* entry = catalog_.find(id);
    return entry != nullptr and (entry->source == PetSource::Shipped or isConfirmed(id));
}

bool PetController::isConfirmed(std::string const& id) const
{
    auto const ids = store_.confirmed();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

bool PetController::isShipped(std::string const& id) const
{
    auto error = std::error_code{};
    return core::pet::isPetId(id) and std::filesystem::is_directory(shippedRoot_ / id, error);
}

void PetController::setEnabled(bool on)
{
    store_.setEnabled(on);
    if (not on) {
        cancelStroll();
        endHiding();
        resetHover();
        wanderWait_ = nextWanderWait();
        // Turning the pet off puts it back where the reader left it, so switching it on starts from home again.
        if (window_ and wander_ and homeReady_) window_->setPosition(wander_->home());
    }
    if (window_ and window_->isVisible()) ::lens::app::setPassthrough(*window_, enabled() and store_.passthrough());
    emit settingsChanged();
}

void PetController::setPassthrough(bool on)
{
    store_.setPassthrough(on);
    if (on) resetHover();
    if (window_ and window_->isVisible()) ::lens::app::setPassthrough(*window_, enabled() and on);
    emit settingsChanged();
}

void PetController::attachWindow(QObject* object)
{
    window_ = qobject_cast<QWindow*>(object);
    if (not window_) return;
    connect(window_, &QObject::destroyed, this, [this] { window_ = nullptr; });
    onShown(window_, [this](QWindow const& window) {
        if (not excludeFromCapture(window)) LENS_WARN("the desktop pet can appear in screen captures: Windows refused the exclusion");
        ::lens::app::setPassthrough(window, enabled() and store_.passthrough());
    });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &PetController::watchScreen);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &PetController::keepOnScreen);
    for (auto* screen : QGuiApplication::screens())
        watchScreen(screen);
}

void PetController::onShown(QWindow* window, std::function<void(QWindow const&)> const& rule)
{
    if (not window) return;
    QObject::connect(window, &QWindow::visibleChanged, window, [window, rule](bool on) {
        if (on) rule(*window);
    });
    if (window->isVisible()) rule(*window);
}

void PetController::watchScreen(QScreen* screen)
{
    connect(screen, &QScreen::geometryChanged, this, &PetController::keepOnScreen);
    connect(screen, &QScreen::availableGeometryChanged, this, &PetController::keepOnScreen);
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, &PetController::keepOnScreen);
}

void PetController::keepOutOfCapture(QObject* object)
{
    onShown(qobject_cast<QWindow*>(object), [](QWindow const& window) {
        if (not excludeFromCapture(window)) LENS_WARN("a window that must stay out of screen captures can appear in them: Windows refused the exclusion");
    });
}

void PetController::keepOnScreen()
{
    if (not window_) return;
    auto areas = QList<QRect>{};
    for (auto const* screen : QGuiApplication::screens())
        areas.append(screen->availableGeometry());
    if (areas.isEmpty()) return;

    // A hide survives a change on another screen; if the work area it was hiding on is gone, the pet comes back out
    // into the nearest safe place rather than staying clipped to a screen that no longer exists.
    if (hiding_) {
        if (auto const* screen = screenNamed(hiding_->screen)) {
            hiding_->area = screen->availableGeometry();
            refitHiding();
            return;
        }
        endHiding();
    }

    cancelStroll();
    auto const at   = window_->position();
    auto const kept = placeWithin(at, window_->size(), areas, QGuiApplication::primaryScreen()->availableGeometry());
    if (kept != at) window_->setPosition(kept);

    if (wander_) {
        // Keep the reader's home while its screen still holds it; otherwise the safe spot is the new home.
        auto const home  = wander_->home();
        auto const still = std::any_of(areas.begin(), areas.end(), [this, home](QRect const& area) {
            return fitsWithin(home, window_->size(), area);
        });
        wander_->setHome(still ? home : kept);
        wander_->setBounds(window_->size(), areaAt(kept));
        homeReady_ = true;
    }
    stroll_     = StrollPhase::Waiting;
    wanderWait_ = nextWanderWait();
}

QPoint PetController::cursorPos() const
{
    return QCursor::pos();
}

QPoint PetController::initialPosition(QSize size)
{
    auto at = bottomRightOf(size, QGuiApplication::primaryScreen()->availableGeometry());
    if (auto const saved = store_.position()) {
        for (auto const* screen : QGuiApplication::screens()) {
            if (deviceNameOf(*screen).toStdString() != saved->screen) continue;
            auto const area = screen->availableGeometry();
            auto const spot = area.topLeft() + QPoint{saved->x, saved->y};
            if (fitsWithin(spot, size, area)) at = spot;
            break;
        }
    }
    if (wander_) {
        wander_->setHome(at);
        wander_->setBounds(size, areaAt(at));
        homeReady_ = true;
    }
    return at;
}

void PetController::fitOnScreen()
{
    if (not window_) return;
    // A hidden window is deliberately off screen; resizing it only means recomputing how far it stays out.
    if (hiding_) {
        refitHiding();
        return;
    }
    // A resize ends the step: its path was computed for the old size and home.
    cancelStroll();
    wanderWait_ = nextWanderWait();
    auto areas  = QList<QRect>{};
    for (auto const* screen : QGuiApplication::screens())
        areas.append(screen->availableGeometry());
    // The window grows right and down from its top-left, so a screen that holds the top-left is the one it must fit.
    auto const index = indexOfNearestArea(window_->position(), areas);
    if (index < 0) return;
    auto const at = clampedWithin(window_->position(), window_->size(), areas.at(index));
    if (at == window_->position()) return;
    window_->setPosition(at);
    // A remembered spot that no longer fits must not come back at the old size on the next start.
    savePosition(at);
}

void PetController::savePosition(QPoint at)
{
    // A hidden window's off-screen spot is not a home; only a drag out of the hide writes one.
    if (hiding_) return;
    if (wander_) {
        wander_->setHome(at);
        wander_->setBounds(window_ ? window_->size() : QSize{}, areaAt(at));
        homeReady_ = true;
    }
    auto const screens = QGuiApplication::screens();
    auto       areas   = QList<QRect>{};
    for (auto const* screen : screens)
        areas.append(screen->availableGeometry());
    auto const index = indexOfNearestArea(at, areas);
    if (index < 0) return;

    // A window dropped half off the edge still belongs to the screen nearest it; its offset may then be negative.
    auto const* screen = screens.at(index);
    auto const  device = deviceNameOf(*screen);
    if (device.isEmpty()) return;
    auto const offset = at - screen->availableGeometry().topLeft();
    store_.setPosition(SavedPosition{device.toStdString(), offset.x(), offset.y()});
}

void PetController::tick()
{
    machine_->advance(kTick);
    updateWander(kTick);
    updateHiding(kTick);
    updateHover(kTick);
    sync(kTick);
}

void PetController::updateWander(std::chrono::milliseconds step)
{
    if (not machine_ or not assets_ or not window_ or not wander_ or hiding_) return;
    auto const& movement = assets_->animations.movement;
    if (not movement) return;
    if (not desktopRunning_ or previewing_ or not enabled()) {
        cancelStroll();
        return;
    }

    auto const area = areaAt(window_->position());
    if (not area.isValid()) {
        cancelStroll();
        return;
    }
    wander_->setBounds(window_->size(), area);
    if (not homeReady_) {
        wander_->setHome(window_->position());
        homeReady_ = true;
    }

    // The wait counts idle time only. The pet's own idle fidgets are part of being idle, so they pause it
    // rather than drawing a new one; every outside event has already drawn a fresh wait in handle().
    // A step's own ambient walk is also not idle: that is the stroll running, not an interruption.
    auto const walking = stroll_ == StrollPhase::Walking and machine_->ambient();
    if (not machine_->idle() and not walking) {
        if (stroll_ != StrollPhase::Waiting) cancelStroll();
        return;
    }

    // A pointer on the pet borrows the idle time for its hover, so a stroll does not start or continue under it.
    if (cursorOnPet()) {
        if (stroll_ != StrollPhase::Waiting) cancelStroll();
        if (not hoverPause_) {
            hoverPause_ = true;
            wanderWait_ = nextWanderWait();
        }
        return;
    }
    hoverPause_ = false;

    switch (stroll_) {
        case StrollPhase::Waiting:
            if (wanderWait_ <= std::chrono::milliseconds{0}) {
                startStroll();
            } else {
                wanderWait_ -= step;
            }
            break;
        case StrollPhase::Walking: {
            stepElapsed_ += step;
            auto const progress = std::min(1.0, static_cast<double>(stepElapsed_.count()) / static_cast<double>(stepDuration_.count()));
            window_->setPosition(wander_->positionAt(*step_, progress));
            if (progress < 1.0) break;

            window_->setPosition(step_->to);
            auto const side = movement->peek ? wander_->edgeAt(step_->to) : std::nullopt;
            if (side and wander_->hides(randomUnit())) {
                enterHiding(*side);
                break;
            }
            machine_->stopAmbient();
            stroll_    = StrollPhase::Pausing;
            pauseLeft_ = nextPause();
            break;
        }
        case StrollPhase::Pausing:
            pauseLeft_ -= step;
            if (pauseLeft_ > std::chrono::milliseconds{0}) break;
            --stepsLeft_;
            if (stepsLeft_ > 0) {
                if (not beginStep()) finishStroll();
            } else {
                finishStroll();
            }
            break;
    }
}

void PetController::updateHiding(std::chrono::milliseconds step)
{
    if (not hiding_ or not window_ or not assets_) return;
    auto& hidden = *hiding_;
    if (hidden.dragging) return;

    if (hidden.asking and machine_->idle()) {
        // The question and its reaction are over: the pet goes back out and picks the peek up from the start.
        hidden.asking    = false;
        hidden.peekClock = std::chrono::milliseconds{0};
    }

    if (hidden.slide < PetWander::kHideSlide) {
        hidden.slide = std::min(PetWander::kHideSlide, hidden.slide + step);
        window_->setPosition(PetWander::slideAt(hidden.from, hidden.base, hidden.slide));
    } else if (hidden.asking) {
        // The sign's hand must stay on screen, which can mean letting the pet in a little for as long as it is up.
        window_->setPosition(hidden.base + QPoint{hidden.askShift, 0});
    } else {
        hidden.peekClock += step;
        auto at = hidden.base;
        at.rx() += (hidden.side == EdgeSide::Left ? 1 : -1) * PetWander::peekOffset(hidden.peekClock);
        window_->setPosition(at);
    }

    // A reaction or the sign ends into idle; the hide keeps going and picks its peek loop back up here.
    auto const& movement = assets_->animations.movement;
    if (movement and movement->peek and machine_->idle()) machine_->playAmbient(*movement->peek);
}

bool PetController::beginStep()
{
    if (not wander_ or not machine_ or not window_ or not assets_) return false;
    auto const& movement = assets_->animations.movement;
    if (not movement) return false;

    auto const step = wander_->nextStep(window_->position());
    if (not step) return false;
    if (not machine_->playAmbient(movement->walk)) return false;

    step_        = *step;
    stepElapsed_ = std::chrono::milliseconds{0};
    stroll_      = StrollPhase::Walking;
    return true;
}

void PetController::startStroll()
{
    if (not assets_ or not assets_->animations.movement) return;
    // A pounce is one leap; every other gait takes one or two steps.
    stepsLeft_ = gaitOf(assets_->animations.movement->style) == Gait::Pounce ? 1 : 1 + (randomUnit() < 0.5 ? 0 : 1);
    if (not beginStep()) finishStroll();
}

void PetController::finishStroll()
{
    stroll_    = StrollPhase::Waiting;
    stepsLeft_ = 0;
    step_.reset();
    hoverPause_ = false;
    wanderWait_ = nextWanderWait();
}

void PetController::cancelStroll()
{
    if (machine_ and machine_->ambient()) machine_->stopAmbient();
    stroll_    = StrollPhase::Waiting;
    stepsLeft_ = 0;
    step_.reset();
}

void PetController::enterHiding(EdgeSide side)
{
    if (not wander_ or not window_ or not assets_) return;
    auto const& movement = assets_->animations.movement;
    if (not movement or not movement->peek) return;

    machine_->stopAmbient();
    auto const size  = window_->size();
    auto const area  = wander_->area();
    auto const scale = static_cast<double>(size.width()) / static_cast<double>(assets_->canvas);

    Hiding hidden;
    hidden.side = side;
    hidden.area = area;
    hidden.from = window_->position();
    hidden.base = PetWander::hiddenBase(area, size, side, hidden.from, assets_->peekInkLeft * scale, assets_->peekInkRight * scale);
    if (auto const* screen = screenForArea(area)) hidden.screen = deviceNameOf(*screen);
    updateAskShift(hidden);
    hiding_    = hidden;
    stroll_    = StrollPhase::Waiting;
    stepsLeft_ = 0;
    step_.reset();
    machine_->playAmbient(*movement->peek);
}

void PetController::endHiding()
{
    if (not hiding_) return;
    hiding_.reset();
    if (machine_ and machine_->ambient()) machine_->stopAmbient();
}

void PetController::snapBackToHide()
{
    if (not hiding_ or not window_) return;
    hiding_->slide     = PetWander::kHideSlide;
    hiding_->peekClock = std::chrono::milliseconds{0};
    window_->setPosition(hiding_->base + QPoint{hiding_->asking ? hiding_->askShift : 0, 0});
}

void PetController::refitHiding()
{
    if (not hiding_ or not window_ or not assets_) return;
    auto&      hidden = *hiding_;
    auto const size   = window_->size();
    auto const scale  = static_cast<double>(size.width()) / static_cast<double>(assets_->canvas);
    hidden.base       = PetWander::hiddenBase(hidden.area, size, hidden.side, hidden.base, assets_->peekInkLeft * scale, assets_->peekInkRight * scale);
    updateAskShift(hidden);
    hidden.slide     = PetWander::kHideSlide;
    hidden.peekClock = std::chrono::milliseconds{0};
    window_->setPosition(hidden.base + QPoint{hidden.asking ? hidden.askShift : 0, 0});
}

std::chrono::milliseconds PetController::nextWanderWait()
{
    auto const spread = static_cast<double>((kWanderWaitMax - kWanderWaitMin).count());
    return kWanderWaitMin + std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(randomUnit() * spread)};
}

std::chrono::milliseconds PetController::nextPause()
{
    auto const gait = assets_ and assets_->animations.movement ? gaitOf(assets_->animations.movement->style) : Gait::Glide;
    auto const low  = gait == Gait::Pounce ? 600 : gait == Gait::Float ? 400
                                               : gait == Gait::Glide   ? 300
                                                                       : 220;
    auto const high = gait == Gait::Pounce ? 1200 : gait == Gait::Float ? 900
                                                : gait == Gait::Glide   ? 800
                                                                        : 620;
    return std::chrono::milliseconds{low + static_cast<int>(randomUnit() * (high - low))};
}

QRect PetController::areaAt(QPoint at) const
{
    auto areas = QList<QRect>{};
    for (auto const* screen : QGuiApplication::screens())
        areas.append(screen->availableGeometry());
    auto const index = indexOfNearestArea(at, areas);
    return index < 0 ? QRect{} : areas.at(index);
}

QScreen* PetController::screenForArea(QRect area) const
{
    for (auto* screen : QGuiApplication::screens())
        if (screen->availableGeometry() == area) return screen;
    return nullptr;
}

QScreen* PetController::screenNamed(QString const& name) const
{
    if (name.isEmpty()) return nullptr;
    for (auto* screen : QGuiApplication::screens())
        if (deviceNameOf(*screen) == name) return screen;
    return nullptr;
}

void PetController::updateAskShift(Hiding& hidden) const
{
    hidden.askShift = 0;
    if (not window_ or not assets_ or assets_->canvas <= 0) return;

    // The inner hand is the right one at the left edge and the left one at the right edge (ADR 0076). A sign
    // brings the window in just far enough for that hand's grip to clear the screen edge.
    auto const hand = hidden.side == EdgeSide::Right ? QStringLiteral("left") : QStringLiteral("right");
    if (not handRest_.contains(hand)) return;

    auto const scale   = static_cast<double>(window_->size().width()) / static_cast<double>(assets_->canvas);
    auto const grip    = static_cast<int>(std::lround(handRest_.value(hand).toPointF().x() * scale));
    auto const desired = hidden.side == EdgeSide::Right ? hidden.area.right() - 16 - grip
                                                        : hidden.area.left() + 16 - grip;
    hidden.askShift    = hidden.side == EdgeSide::Right ? std::min(0, desired - hidden.base.x())
                                                        : std::max(0, desired - hidden.base.x());
}

bool PetController::cursorOnPet() const
{
    if (not window_ or not window_->isVisible() or store_.passthrough()) return false;
    return QRect{window_->position(), window_->size()}.contains(QCursor::pos());
}

void PetController::updateHover(std::chrono::milliseconds step)
{
    // Hover is the desktop pet's own sense. It has no meaning while the window is hidden, while clicks pass through,
    // while the settings preview is open, or before a window exists to measure against.
    if (not hover_ or not assets_ or not window_ or not desktopRunning_ or previewing_ or hiding_ or store_.passthrough()) {
        resetHover();
        return;
    }

    auto const& hover              = assets_->animations.hover;
    auto const& action             = machine_->action();
    auto const  hoverActionPlaying = hover and
                                    ((hover->notice and action == *hover->notice) or (hover->dwell and action == *hover->dwell));
    // Hover only borrows idle time. While the pet reads, sleeps, asks, drags, is in a scene or plays any other
    // one-shot, the approach is forgotten: a long reading must not leave a reaction due the moment it ends.
    if (not machine_->idle() and not hoverActionPlaying) {
        resetHover();
        return;
    }
    if (not window_->isVisible()) {
        resetHover();
        return;
    }

    auto const size = window_->size();
    if (size.width() <= 0 or size.height() <= 0) {
        resetHover();
        return;
    }
    auto const origin = window_->position();
    auto const cursor = QCursor::pos();
    auto const sx     = static_cast<double>(size.width()) / assets_->canvas;
    auto const sy     = static_cast<double>(size.height()) / assets_->canvas;
    auto const at     = QPointF{(cursor.x() - origin.x()) / sx, (cursor.y() - origin.y()) / sy};

    auto const result = hover_->update(step, inkDistanceAt(at), machine_->idle());
    if (result.notice or result.dwell) wanderWait_ = nextWanderWait();
    if (result.notice and hover and hover->notice) machine_->playHover(*hover->notice);
    if (result.dwell and hover and hover->dwell) machine_->playHover(*hover->dwell);

    // The eyes follow only while the pet is idle. During a reaction, a reading or a drag the expression is the
    // action's own; a brief snap back to centre is less disruptive than moving a face that is already expressive.
    if (machine_->idle() and hover_->watching())
        setGaze(gazeOffsetAt(at));
    else
        setGaze({});
}

void PetController::resetHover()
{
    if (hover_) hover_->reset();
    setGaze({});
}

void PetController::setGaze(QPoint offset)
{
    if (offset == gaze_) return;
    gaze_ = offset;
    emit gazeChanged();
}

QPoint PetController::gazeOffsetAt(QPointF cursor) const
{
    if (not assets_) return {};
    auto const middleY = (assets_->inkTop + assets_->inkBottom) / 2.0;
    auto const dx      = cursor.x() - assets_->inkCentre;
    auto const dy      = cursor.y() - middleY;
    if (std::abs(dx) < 2.0 and std::abs(dy) < 2.0) return {};
    if (std::abs(dx) >= std::abs(dy) * 1.15) return {dx > 0 ? 1 : -1, 0};
    if (std::abs(dy) >= std::abs(dx) * 1.15) return {0, dy > 0 ? 1 : -1};
    return {};
}

double PetController::inkDistanceAt(QPointF at) const
{
    auto const dx = std::max({assets_->inkLeft - at.x(), 0.0, at.x() - assets_->inkRight});
    auto const dy = std::max({assets_->inkTop - at.y(), 0.0, at.y() - assets_->inkBottom});
    return std::hypot(dx, dy);
}

void PetController::sync(std::chrono::milliseconds step)
{
    auto const& assets = *assets_;
    auto const  action = shownAction();
    if (action != shownAction_) {
        shownAction_   = action;
        actionElapsed_ = std::chrono::milliseconds{0};
    } else {
        actionElapsed_ += step;
    }

    // The blink runs on the expression layer while the action allows it: a wait, then a short run of expressions.
    auto const& blink       = assets.animations.blink;
    auto const  blinkLength = std::chrono::milliseconds{blink.frames * 1000 / blink.fps};
    // A preview shows an action the machine is not playing, so its blink flag is read from the spec instead.
    auto const blinks = preview_ ? assets.animations.playbook.actions.at(action).blinks : machine_->blinking();
    if (not blinks) {
        blinkRunning_ = false;
        blinkWait_    = kBlinkEvery;
    } else if (blinkRunning_) {
        blinkElapsed_ += step;
        if (blinkElapsed_ >= blinkLength) {
            blinkRunning_ = false;
            blinkWait_    = kBlinkEvery;
        }
    } else {
        blinkWait_ -= step;
        if (blinkWait_ <= std::chrono::milliseconds{0}) {
            blinkRunning_ = true;
            blinkElapsed_ = std::chrono::milliseconds{0};
        }
    }

    auto const& spec     = assets.animations.playbook.actions.at(action);
    auto const& pictures = assets.sheets.at(action);
    auto        frame    = static_cast<int>(actionElapsed_.count() * spec.fps / 1000);
    if (preview_ and not spec.loop and frame >= spec.frames) {
        // A one-shot preview has played through: the pet goes back to what the machine has it doing.
        preview_.reset();
        sync(std::chrono::milliseconds{0});
        return;
    }
    frame = spec.loop ? frame % spec.frames : std::min(frame, spec.frames - 1);

    auto expression = 0;
    if (blinkRunning_) {
        auto const& cells = assets.blinkExpressions;
        auto const  cell  = static_cast<std::size_t>(blinkElapsed_.count() * blink.fps / 1000);
        if (not cells.empty()) expression = cells[std::min(cell, cells.size() - 1)];
    } else if (static_cast<std::size_t>(frame) < pictures.expressions.size()) {
        expression = pictures.expressions[static_cast<std::size_t>(frame)];
    }

    auto const name    = QString::fromStdString(action);
    auto const changed = name != action_ or frame != frameIndex_ or expression != expression_;
    if (name != action_) {
        sheets_ = QVariantMap{{"body", urlOf(pictures.body)}, {"effect", urlOf(pictures.effect)}, {"effectLeft", urlOf(pictures.effectLeft)}, {"expression", urlOf(assets.expressionSheet)}};
    }
    action_       = name;
    frameIndex_   = frame;
    expression_   = expression;
    auto const at = static_cast<std::size_t>(frame);
    if (at < pictures.gripRight.size()) handGrips_["right"] = pictures.gripRight[at];
    if (at < pictures.gripLeft.size()) handGrips_["left"] = pictures.gripLeft[at];
    anchors_ = assets.anchors.at(action).at(static_cast<std::size_t>(frame));

    if (auto const* scene = scenesAllowed_ ? scenes_.find(action) : nullptr;
        scene and static_cast<std::size_t>(frame) < scene->perFrame.size()) {
        auto const& f = scene->perFrame[static_cast<std::size_t>(frame)];
        scene_        = QString::fromStdString(scene->id);
        sceneFrame_   = frame;
        sceneShift_   = QPointF{f.dx, f.dy};
        sceneScale_   = f.scale;
        // The props were drawn for the catalogue's feet; the pet's own feet are measured when it loads.
        sceneAlign_  = QPointF{assets.inkCentre - scene->foot.x(), assets.inkBottom - scene->foot.y()};
        sceneSheets_ = QVariantMap{{"back", urlOf(scene->backSheet)}, {"front", urlOf(scene->frontSheet)}};
        sceneLayers_ = QVariantMap{{"back", scene->backLayer}, {"front", scene->frontLayer}};
    } else {
        scene_.clear();
        sceneSheets_.clear();
        sceneLayers_.clear();
        sceneFrame_ = 0;
        sceneShift_ = {};
        sceneAlign_ = {};
        sceneScale_ = 1.0;
    }
    if (changed) emit frameChanged();
}

void PetController::updateClock()
{
    if (machine_ and (desktopRunning_ or previewing_)) {
        clock_.start();
    } else {
        clock_.stop();
    }
}

core::pet::ActionName PetController::shownAction() const
{
    return preview_.value_or(machine_->action());
}

std::optional<core::pet::ActionName> PetController::actionNamed(QString const& name) const
{
    if (not assets_) return std::nullopt;
    auto const action = name.toStdString();
    // The blink is an expression entry, not an action, so it is never a name the settings page can preview.
    if (assets_->animations.playbook.actions.count(action) == 0) return std::nullopt;
    return action;
}

bool PetController::wearsInSlot(std::string const& id, Slot slot) const
{
    if (not assets_) return false;
    auto const& catalogue = assets_->catalogue;
    return std::any_of(catalogue.begin(), catalogue.end(), [&id, slot](auto const& spec) { return spec.id == id and spec.slot == slot; });
}

bool PetController::knownAccessory(QString const& id) const
{
    if (not assets_) return false;
    auto const  name      = id.toStdString();
    auto const& catalogue = assets_->catalogue;
    return std::any_of(catalogue.begin(), catalogue.end(), [&name](auto const& spec) { return spec.id == name; });
}

} // namespace lens::app::pet
