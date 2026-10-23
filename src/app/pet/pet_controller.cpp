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
#include <QTime>
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
using core::pet::Role;
using core::pet::Slot;

/// One tick of the single frame clock. Frames are at most 24 fps, so a tick is never longer than a frame.
constexpr std::chrono::milliseconds kTick{20};
/// Time between blinks while the pet is awake. The prototype blinks on the same cadence.
constexpr std::chrono::milliseconds kBlinkEvery{4000};
/// The ceremony's beats: a moment to stand after the look, a breath between the look and the heart, and one after the
/// heart before the pet walks out.
constexpr std::chrono::milliseconds kSettlePause{140};
constexpr std::chrono::milliseconds kLookToHeart{120};
constexpr std::chrono::milliseconds kByeToExit{240};
/// How long a switch keeps the window off screen while the new pet's QML window takes its size.
constexpr std::chrono::milliseconds kSwapSettle{60};
/// A reading this long earns the after-reading context: the pet answers a session, not a glance (ADR 0093).
constexpr std::chrono::milliseconds kLongReading{180'000};
/// How long after a long reading the after-reading context stays up.
constexpr std::chrono::milliseconds kReadingAfterglow{120'000};
/// How long after a stroll ends the after-return context stays up.
constexpr std::chrono::milliseconds kReturnAfterglow{90'000};

QPoint toPoint(core::pet::Point const& point)
{
    return QPoint{point.x, point.y};
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
    // A launch with the pet already on opens where the reader left it: the entrance belongs to turning the pet on,
    // not to starting the application over it.
    shown_ = enabled();
}

bool PetController::isLeaving(CeremonyPhase phase)
{
    return phase == CeremonyPhase::Leaving or phase == CeremonyPhase::LeaveLook or phase == CeremonyPhase::LeaveBye or
           phase == CeremonyPhase::Exiting;
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
    flight_.reset();
    resetThrowMood();
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
    reviewLook_ = {};
    behavior_   = {};
    try {
        auto const* entry = catalog_.find(id);
        if (not entry) throw std::runtime_error{"the pet is not in the catalogue: " + id};
        assets_ = PetAssets::load(entry->directory);
    } catch (std::exception const& e) {
        assets_.reset();
        LENS_ERROR("the desktop pet is off: {}", e.what());
    }
    if (assets_) {
        behavior_ = assets_->animations.behavior;
        // The idle timer, the scene pick, the strolls and the hide roll draw from one stream. A scene is offered
        // only to a shipped pet: a package may use another canvas size, and the app's props are drawn for 88.
        auto random = [this] { return randomUnit(); };
        auto mood   = [this] { return currentMood(); };
        auto scenes = [this, id]() -> std::optional<core::pet::ActionName> {
            if (not isShipped(id)) return std::nullopt;
            auto const& playbook = assets_->animations.playbook;
            struct Choice {
                core::pet::ActionName name;
                double                weight;
            };
            auto        choices = std::vector<Choice>{};
            auto const& now     = currentMood();
            for (auto const& scene : scenes_.all())
                if (playbook.actions.count(scene.id) != 0)
                    choices.push_back({scene.id, core::pet::sceneWeight(scene.mood, now)});
            // A four-scene pool reads as a pool only if the same one does not come straight back.
            if (choices.size() > 1) std::erase_if(choices, [this](Choice const& choice) { return choice.name == lastScene_; });
            if (choices.empty()) return std::nullopt;
            // A weighted draw: every scene keeps a chance in every stretch, the weights only tilt it (ADR 0093).
            auto total = 0.0;
            for (auto const& choice : choices)
                total += choice.weight;
            auto roll   = randomUnit() * total;
            auto picked = choices.back();
            for (auto const& choice : choices) {
                if (roll < choice.weight) {
                    picked = choice;
                    break;
                }
                roll -= choice.weight;
            }
            lastScene_ = picked.name;
            return picked.name;
        };
        machine_  = std::make_unique<core::pet::PetStateMachine>(assets_->animations.playbook, random, scenes, behavior_, mood);
        wardrobe_ = std::make_unique<core::pet::Wardrobe>(assets_->catalogue);

        if (auto const& movement = assets_->animations.movement) {
            wander_          = std::make_unique<PetWander>(gaitOf(movement->style), movement->range, random, behavior_.hideChance);
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
        hover_ = std::make_unique<core::pet::HoverSense>(timing, behavior_.gazeRadius);

        auto const outfit = store_.outfit(id);
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
    // The reading's span belongs to the reader, not to the machine: the after-reading context starts when a long
    // one ends, whatever the pet was busy with meanwhile (ADR 0093).
    if (event == PetEvent::ExplanationShown) {
        readingSince_ = std::chrono::steady_clock::now();
    } else if (event == PetEvent::ExplanationHidden and readingSince_) {
        if (std::chrono::steady_clock::now() - *readingSince_ >= kLongReading)
            readingAfterUntil_ = std::chrono::steady_clock::now() + kReadingAfterglow;
        readingSince_.reset();
    }
    // The sign reports its side after ReviewAsked; the events that take it down leave no direction behind.
    if (event == PetEvent::ReviewAsked or event == PetEvent::ReviewRemembered or event == PetEvent::ReviewForgotten or
        event == PetEvent::ReviewIgnored)
        reviewLook_ = {};
    if (not machine_) return;
    angryRepeatsLeft_ = 0;
    if (flight_) {
        // An outside event owns the body: put the throw down where it is and answer. The landing spot becomes home.
        endFlight(window_ ? window_->position() : QPoint{}, true);
    }
    if (ceremony_) {
        // A pet on its way out is not called back by a learning event. One arriving, or changing pets, lands at its
        // outcome first: the event is what the reader is doing, and the walk has nowhere else to be.
        if (isLeaving(ceremony_->phase)) return;
        finishCeremonyNow();
    }
    // An outside event voids the current wait: the next stroll is drawn again once the pet is idle.
    wanderWait_ = nextWanderWait();
    if (hiding_ and event == PetEvent::ReviewAsked) {
        if (hiding_->leaving) {
            // The pet is already coming out; the question meets it where it lands, as a dragged-out hide's does.
            settleOutOfHide();
        } else {
            // The sign stands beside the pet on the hidden sliver's inner side; the pet itself stays where the hide
            // put it.
            hiding_->asking    = true;
            hiding_->slide     = PetWander::kHideSlide;
            hiding_->peekClock = std::chrono::milliseconds{0};
            if (window_) window_->setPosition(hiding_->base);
        }
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

bool PetController::shown() const
{
    return shown_;
}

bool PetController::passthrough() const
{
    return store_.passthrough();
}

bool PetController::availableForReview() const
{
    // A ceremony, a throw and its landing's angry repeat own the pet's body, so no question arrives on top of one.
    if (not enabled() or not machine_ or dragging_ or ceremony_ or flight_ or angryRepeatsLeft_ > 0 or hideAfterDrag_)
        return false;
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
    // The picker shows the reader's choice while the old pet is still walking out, so it does not snap back.
    if (not pendingPet_.empty()) return QString::fromStdString(pendingPet_);
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
    // A reader who has never moved the slider gets the package's declared 1..4 scale, as a percentage. The size
    // belongs to the pet on screen, which is the one the settings name (ADR 0096).
    return store_.size(store_.pet(), assets_ ? sizeFromScale(assets_->scale) : kMinSize);
}

double PetController::scale() const
{
    return magnificationOf(size());
}

double PetController::inkCentre() const
{
    return assets_ ? assets_->inkCentre : 0.0;
}

QRectF PetController::inkBox() const
{
    if (not assets_) return {};
    return QRectF{assets_->inkLeft,
                  assets_->inkTop,
                  assets_->inkRight - assets_->inkLeft,
                  assets_->inkBottom - assets_->inkTop};
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
    // A throw was measured for the old window; the size change puts the pet down where it is and lets fitOnScreen
    // pull the new window into the work area. Changing size ends a run of throws.
    if (flight_) endFlight(window_ ? window_->position() : QPoint{}, true);
    resetThrowMood();
    // The ceremony's path was measured for the old window; a size change lands it at home and lets fitOnScreen pull
    // the new window into the work area.
    if (ceremony_) finishCeremonyNow();
    store_.setSize(store_.pet(), percent);
    emit settingsChanged();
}

void PetController::setRunning(bool on)
{
    desktopRunning_ = on;
    if (not on) {
        // The window is going away under a throw: stop it without leaving the pickup pose behind.
        if (flight_) {
            flight_.reset();
            if (machine_) machine_->rest();
        }
        resetThrowMood();
        // The window was taken away under a ceremony (the application is closing, or the scene asked it down):
        // stop the ceremony rather than leave it half done.
        if (ceremony_) {
            ceremony_.reset();
            pendingPet_.clear();
            hideAfterDrag_ = false;
            if (machine_) machine_->rest();
        }
        cancelStroll();
        endHiding();
        resetHover();
    }
    updateClock();
}

void PetController::setPreviewing(bool on)
{
    previewing_ = on;
    if (on) resetThrowMood();
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
    // A preview takes the body: a throw is put down where it is and that spot becomes home.
    if (flight_) endFlight(window_ ? window_->position() : QPoint{}, true);
    resetThrowMood();
    if (ceremony_) finishCeremonyNow();
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
    // preview, if one was showing, would otherwise keep it off screen. A throw is put down first and its landing
    // becomes home.
    if (flight_) endFlight(window_ ? window_->position() : QPoint{}, true);
    resetThrowMood();
    if (ceremony_) finishCeremonyNow();
    preview_.reset();
    resetHover();
    lastScene_ = name;
    machine_->playScene(name);
    sync(std::chrono::milliseconds{0});
}

void PetController::dragStart()
{
    if (flight_) {
        // Catching a pet in the air ends the throw where it is; this drag, not the throw, decides where it lands.
        endFlight(window_ ? window_->position() : QPoint{}, false);
    }
    angryRepeatsLeft_ = 0;

    if (ceremony_) {
        // Grabbing the pet ends its ceremony where it stands. A goodbye was taking the window off screen, so the drop
        // completes it instead of leaving a turned-off pet on the desktop.
        hideAfterDrag_ = isLeaving(ceremony_->phase);
        abortCeremony();
    }
    dragging_ = true;
    if (stroll_ != StrollPhase::Waiting) cancelStroll();
    if (hiding_) hiding_->dragging = true;
    handle(PetEvent::DragStart);
}

void PetController::dragEnd()
{
    // A release that is not a throw ends the run of landed throws.
    resetThrowMood();
    dragging_ = false;
    if (hideAfterDrag_) {
        hideAfterDrag_ = false;
        handle(PetEvent::DragEnd);
        setShown(false);
        wanderWait_ = nextWanderWait();
        return;
    }
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

    // A release that leaves the window against a screen edge is the reader asking the pet to hide: it tucks itself
    // in, without the stroll's probability roll (ADR 0095). A throw's landing is not that release, and a pet without
    // peek art keeps sitting where it was dropped.
    if (not previewing_ and wander_ and window_ and assets_) {
        auto const& movement = assets_->animations.movement;
        if (movement and movement->peek) {
            if (auto const side = PetWander::dropEdge(window_->position(), window_->size(), wander_->area()))
                enterHiding(*side);
        }
    }
}

bool PetController::release(QPointF velocity)
{
    if (not dragging_ or not window_) return false;
    auto const at   = window_->position();
    auto const size = window_->size();
    // A pet that was not pulled out of its edge stays hidden: a fast release must not fling the sliver.
    if (hiding_ and not PetWander::draggedOut(at, size, hiding_->area)) return false;
    if (hideAfterDrag_ or motionReduced_ or not PetFlight::isFling(velocity)) return false;

    auto const center = at + QPoint{size.width() / 2, size.height() / 2};
    auto const area   = areaAt(center);
    if (not area.isValid() or size.width() <= 0 or size.height() <= 0) return false;

    if (hiding_) hiding_.reset();
    dragging_         = false;
    angryRepeatsLeft_ = 0;
    flight_           = PetFlight{at, velocity, size, area};
    if (wander_) wander_->setBounds(size, area);
    resetHover();
    LENS_DEBUG("pet flight: thrown at ({},{}) px/s from ({},{}) in a {}x{} area",
               std::lround(velocity.x()),
               std::lround(velocity.y()),
               at.x(),
               at.y(),
               area.width(),
               area.height());
    return true;
}

void PetController::landFlight()
{
    if (not flight_ or not flight_->landed()) return;
    auto const at      = flight_->position();
    auto const elapsed = flight_->elapsed();
    flight_.reset();
    if (window_) window_->setPosition(at);

    // The pickup pose is the throw's; the landing is where the drag ends. The pet stays here: the landing spot
    // becomes its home (ADR 0092).
    handle(PetEvent::DragEnd);
    if (window_) savePosition(at);

    auto const angry = throwMood_.landed(randomUnit());
    if (machine_ and machine_->idle()) {
        // A pet with the optional dizzy art shows its stars; a package without it falls back to the poke reaction.
        auto const dizzy = machine_->playRole(Role::Dizzy);
        if (not dizzy) machine_->playRole(Role::ClickReact);
        // Only a dizzy landing can turn into anger, and only after the run has built up.
        if (dizzy and angry) angryRepeatsLeft_ = PetThrowMood::kAngryRepeats;
    }
    LENS_DEBUG("pet flight: landed at ({},{}) after {} ms", at.x(), at.y(), elapsed.count());
}

void PetController::updateLandingAngry()
{
    if (angryRepeatsLeft_ <= 0) return;
    if (not machine_ or not enabled() or not desktopRunning_) {
        angryRepeatsLeft_ = 0;
        return;
    }
    if (dragging_ or flight_ or ceremony_ or hiding_ or previewing_) return;
    if (not machine_->idle()) return;

    if (machine_->playRole(Role::Angry)) {
        --angryRepeatsLeft_;
    } else {
        // The pet has no angry art (or it is not a one-shot): there is nothing to repeat.
        angryRepeatsLeft_ = 0;
    }
}

void PetController::endFlight(QPoint at, bool saveHome)
{
    if (not flight_) return;
    flight_.reset();
    if (window_ and window_->position() != at) window_->setPosition(at);

    // The pickup pose is the throw's; the machine learns the drag ended here.
    handle(PetEvent::DragEnd);
    if (saveHome and window_) savePosition(at);
    resetThrowMood();
}

void PetController::resetThrowMood()
{
    throwMood_.reset();
    angryRepeatsLeft_ = 0;
}

void PetController::click()
{
    // A click on the visible sliver calls the pet out rather than poking it: no reaction, no anger count (ADR 0095).
    // A ceremony is the pet's own business too: a click left over from catching it does not cut the walk.
    if (hiding_) {
        lureOut();
        return;
    }
    if (ceremony_) return;
    resetThrowMood();
    handle(PetEvent::Click);
}

void PetController::stopWandering()
{
    if (stroll_ != StrollPhase::Waiting) cancelStroll();
    if (hiding_) return;
    wanderWait_ = nextWanderWait();
    hoverPause_ = true;
}

void PetController::setReviewLook(QString side)
{
    auto const look = side == QStringLiteral("left")    ? QPoint{-1, 0}
                      : side == QStringLiteral("right") ? QPoint{1, 0}
                                                        : QPoint{};
    if (look == reviewLook_) return;
    reviewLook_ = look;
    if (look.isNull()) {
        // The sign is gone: forget the hover round it interrupted, and put the eyes back where the idle face has them.
        resetHover();
    } else {
        setGaze(look);
    }
}

void PetController::toggleAccessory(QString id)
{
    if (not wardrobe_ or not knownAccessory(id)) return;
    wardrobe_->toggle(id.toStdString());
    store_.setOutfit(store_.pet(), outfitOf(*wardrobe_));
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
    if (name == store_.pet() and pendingPet_.empty()) return;
    if (name == pendingPet_) return;

    // Catching a pet in the air is not a request to stop: only a real pet change ends the throw, at the current spot.
    if (flight_) endFlight(window_ ? window_->position() : QPoint{}, true);
    resetThrowMood();

    // With no window under the tests, with the pet off, or with motion reduced, the choice lands at once.
    if (not enabled() or not shown_ or not window_ or motionReduced_ or not canWalkCeremony()) {
        if (ceremony_) {
            if (isLeaving(ceremony_->phase))
                hideNow();
            else
                abortCeremony();
        }
        applyPet(name);
        return;
    }

    // A new choice interrupts the ceremony before it; the pet it interrupted is the one that walks out.
    if (ceremony_) abortCeremony();
    if (hiding_) endHiding();
    pendingPet_ = name;
    startSwitch();
}

void PetController::confirmPet(QString id)
{
    auto const name = id.toStdString();
    if (not catalog_.find(name)) return;
    store_.confirm(name);
    setPet(id);
}

void PetController::refresh()
{
    // The section is opened afresh: a ceremony from the last visit does not carry over into the new catalogue.
    if (ceremony_) {
        if (isLeaving(ceremony_->phase))
            hideNow();
        else
            abortCeremony();
    }
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
    pendingPet_.clear();
    loadPet(id);
    // A pet that will not load keeps the one the reader had, so the picker stays usable.
    if (not assets_) {
        store_.setPet(previous);
        loadPet(previous);
    }
    // Accessories the new pet does not have leave its own saved outfit too, so they do not come back
    // when the reader switches again.
    if (wardrobe_) store_.setOutfit(store_.pet(), outfitOf(*wardrobe_));
    emit settingsChanged();
    emit frameChanged();
    emit outfitChanged();
}

void PetController::switchToDefault(std::string const& gone)
{
    pendingPet_.clear();
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
    if (on == store_.enabled()) return;
    store_.setEnabled(on);
    hideAfterDrag_ = false;
    resetThrowMood();

    if (on) {
        if (ceremony_ and isLeaving(ceremony_->phase)) {
            // Turned back on during its goodbye: it walks back in from wherever it had got to, unless motion is
            // reduced, where it simply stands at home again.
            if (motionReduced_)
                settleAtHome();
            else
                startEntrance(true);
        } else if (not shown_) {
            if (motionReduced_ or not canWalkCeremony()) {
                settleAtHome();
            } else {
                startEntrance(false);
            }
        } else {
            // A drag can hold a turned-off pet on screen; turning it on just keeps it where it is.
            if (ceremony_) abortCeremony();
            if (machine_) machine_->rest();
        }
    } else {
        // A throw ends where it is; the pet then leaves from that spot like any other.
        if (flight_) endFlight(window_ ? window_->position() : QPoint{}, true);
        if (ceremony_ and isLeaving(ceremony_->phase)) {
            // The goodbye is already playing.
        } else if (hiding_) {
            // It is already at the edge and mostly out of sight; there is no goodbye left to show.
            endHiding();
            hideNow();
        } else if (not window_ or not shown_ or motionReduced_ or not canWalkCeremony()) {
            hideNow();
        } else if (dragging_) {
            if (ceremony_) abortCeremony();
            hideAfterDrag_ = true;
        } else {
            // A switch in progress gives way to the goodbye; the pet already loaded is the one that leaves.
            if (ceremony_) abortCeremony();
            startLeave();
        }
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

void PetController::setMotionReduced(bool on)
{
    if (on == motionReduced_) return;
    motionReduced_ = on;
    if (on and flight_) {
        // Reduced motion skips the rest of the arc: the pet stays where it is, and that spot becomes home.
        endFlight(window_ ? window_->position() : QPoint{}, true);
    }
    resetThrowMood();
    if (not on or not ceremony_) return;
    // A switch that had not loaded its pet yet still completes; a goodbye becomes a plain hide.
    finishCeremonyNow();
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
    // The screen changed under a throw: put the pet down where it is and let the safe-spot rule below choose the new
    // home.
    if (flight_) endFlight(window_->position(), true);
    resetThrowMood();
    if (ceremony_) finishCeremonyNow();
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

bool PetController::canWalkCeremony() const
{
    if (not assets_ or not machine_ or not wander_ or not window_) return false;
    auto const& movement = assets_->animations.movement;
    return movement.has_value() and assets_->animations.playbook.actions.count(movement->walk) != 0;
}

void PetController::startEntrance(bool fromHere)
{
    hideAfterDrag_ = false;
    if (machine_) {
        machine_->stopAmbient();
        machine_->rest();
    }
    if (not window_ or not wander_) {
        settleAtHome();
        return;
    }

    auto const size = window_->size();
    auto const here = window_->position();
    auto const area = areaAt(here);
    if (not area.isValid()) {
        settleAtHome();
        return;
    }

    wander_->setBounds(size, area);
    if (not homeReady_) {
        wander_->setHome(here);
        homeReady_ = true;
    }
    auto const home = clampedWithin(wander_->home(), size, area);
    auto const side = PetWander::nearestEdge(area, size, home);
    auto const from = fromHere ? here : PetWander::offscreenBase(area, size, home, side);
    wander_->setHome(home);

    // Fully off screen before the window is shown, so an entrance never starts with a pop at home.
    if (not fromHere) window_->setPosition(from);
    setShown(true);

    Ceremony started;
    started.phase = CeremonyPhase::Entering;
    started.side  = side;
    started.walk  = WanderStep{from, home};
    started.home  = home;
    ceremony_     = started;
    wanderWait_   = nextWanderWait();
    if (beginCeremonyWalk(CeremonyPhase::Entering, from, home)) {
        LENS_DEBUG("pet ceremony: entering from the {} edge", side == EdgeSide::Left ? "left" : "right");
        return;
    }

    // A pet with no walk art still looks around from home rather than simply appearing.
    window_->setPosition(home);
    if (beginCeremonyRole(CeremonyPhase::EnterLook, Role::LookAround, kSettlePause)) return;
    settleAtHome();
}

void PetController::startLeave()
{
    if (machine_) {
        machine_->stopAmbient();
        machine_->rest();
    }
    if (not canWalkCeremony()) {
        hideNow();
        return;
    }

    auto const size = window_->size();
    auto const here = window_->position();
    auto const area = areaAt(here);
    if (not area.isValid() or not QRect{here, size}.intersects(area)) {
        // Already past the edge: there is nothing left to walk out of and nobody can see a goodbye.
        hideNow();
        return;
    }

    auto const side = PetWander::nearestEdge(area, size, here);
    auto const high = area.bottom() - size.height() + 1;
    auto const y    = high < area.top() ? area.top() : std::clamp(here.y(), area.top(), high);
    auto const edge = QPoint{side == EdgeSide::Left ? area.left() : area.right() - size.width() + 1, y};

    Ceremony started;
    started.phase = CeremonyPhase::Leaving;
    started.side  = side;
    started.walk  = WanderStep{here, edge};
    started.home  = wander_ ? wander_->home() : here;
    ceremony_     = started;
    if (beginCeremonyWalk(CeremonyPhase::Leaving, here, edge)) {
        LENS_DEBUG("pet ceremony: goodbye at the {} edge", side == EdgeSide::Left ? "left" : "right");
        return;
    }
    if (beginCeremonyRole(CeremonyPhase::LeaveLook, Role::LookAround, kLookToHeart)) return;
    if (beginCeremonyRole(CeremonyPhase::LeaveBye, Role::Encourage, kByeToExit)) return;
    beginExitWalk();
}

void PetController::startSwitch()
{
    if (pendingPet_.empty()) return;
    if (machine_) {
        machine_->stopAmbient();
        machine_->rest();
    }
    if (not canWalkCeremony()) {
        applyPendingPet();
        settleAtHome();
        return;
    }

    auto const size = window_->size();
    auto const here = window_->position();
    auto const area = areaAt(here);
    if (not area.isValid()) {
        applyPendingPet();
        settleAtHome();
        return;
    }

    auto const side = PetWander::nearestEdge(area, size, here);
    auto const out  = PetWander::offscreenBase(area, size, here, side);
    auto const home = wander_ and homeReady_ ? clampedWithin(wander_->home(), size, area) : here;

    Ceremony started;
    started.phase = CeremonyPhase::SwitchOut;
    started.side  = side;
    started.walk  = WanderStep{here, out};
    started.home  = home;
    ceremony_     = started;
    if (beginCeremonyWalk(CeremonyPhase::SwitchOut, here, out)) {
        LENS_DEBUG("pet ceremony: switching at the {} edge", side == EdgeSide::Left ? "left" : "right");
        return;
    }

    // Already off screen, or the walk cannot play: swap without walking out.
    ceremony_->phase    = CeremonyPhase::Swap;
    ceremony_->duration = kSwapSettle;
    ceremony_->loaded   = false;
}

void PetController::updateCeremony(std::chrono::milliseconds step)
{
    if (not ceremony_) return;
    if (not window_ or not assets_) {
        hideNow();
        return;
    }

    auto& c = *ceremony_;
    c.elapsed += step;

    switch (c.phase) {
        case CeremonyPhase::Entering:
        case CeremonyPhase::Leaving:
        case CeremonyPhase::Exiting:
        case CeremonyPhase::SwitchOut: {
            auto const gait   = assets_->animations.movement ? gaitOf(assets_->animations.movement->style) : Gait::Glide;
            auto const done   = c.duration.count() == 0 or c.elapsed >= c.duration;
            auto const amount = done ? 1.0
                                     : static_cast<double>(c.elapsed.count()) / static_cast<double>(c.duration.count());
            window_->setPosition(PetWander::walkAt(c.walk, amount, gait, window_->size().width()));
            if (not done) return;
            window_->setPosition(c.walk.to);
            if (machine_) machine_->stopAmbient();

            switch (c.phase) {
                case CeremonyPhase::Entering:
                    LENS_DEBUG("pet ceremony: arrived at home");
                    if (beginCeremonyRole(CeremonyPhase::EnterLook, Role::LookAround, kSettlePause)) return;
                    settleAtHome();
                    return;
                case CeremonyPhase::Leaving:
                    if (beginCeremonyRole(CeremonyPhase::LeaveLook, Role::LookAround, kLookToHeart)) return;
                    if (beginCeremonyRole(CeremonyPhase::LeaveBye, Role::Encourage, kByeToExit)) return;
                    beginExitWalk();
                    return;
                case CeremonyPhase::Exiting:
                    hideNow();
                    return;
                case CeremonyPhase::SwitchOut:
                    LENS_DEBUG("pet ceremony: the old pet is off screen; loading {}", pendingPet_);
                    c.phase    = CeremonyPhase::Swap;
                    c.elapsed  = std::chrono::milliseconds{0};
                    c.duration = kSwapSettle;
                    c.loaded   = false;
                    return;
                default:
                    return;
            }
        }
        case CeremonyPhase::EnterLook:
            if (c.elapsed >= c.duration) settleAtHome();
            return;
        case CeremonyPhase::LeaveLook:
            if (c.elapsed < c.duration) return;
            if (beginCeremonyRole(CeremonyPhase::LeaveBye, Role::Encourage, kByeToExit)) return;
            beginExitWalk();
            return;
        case CeremonyPhase::LeaveBye:
            if (c.elapsed >= c.duration) beginExitWalk();
            return;
        case CeremonyPhase::Swap:
            if (not c.loaded) {
                applyPendingPet();
                c.loaded  = true;
                c.elapsed = std::chrono::milliseconds{0};
                return;
            }
            if (c.elapsed < c.duration) return;
            {
                auto const area = areaAt(c.home);
                auto const size = window_->size();
                auto const home = area.isValid() ? clampedWithin(c.home, size, area) : c.home;
                c.home          = home;
                window_->setPosition(area.isValid() ? PetWander::offscreenBase(area, size, home, c.side) : home);
                if (wander_) {
                    wander_->setHome(home);
                    wander_->setBounds(size, area);
                    homeReady_ = area.isValid();
                }
                if (machine_) {
                    machine_->stopAmbient();
                    machine_->rest();
                }
                wanderWait_ = nextWanderWait();
                if (beginCeremonyWalk(CeremonyPhase::Entering, window_->position(), home)) {
                    LENS_DEBUG("pet ceremony: the new pet enters");
                    return;
                }
                window_->setPosition(home);
                if (beginCeremonyRole(CeremonyPhase::EnterLook, Role::LookAround, kSettlePause)) return;
                settleAtHome();
            }
            return;
    }
}

bool PetController::beginCeremonyWalk(CeremonyPhase phase, QPoint from, QPoint to)
{
    if (not ceremony_ or not canWalkCeremony()) return false;
    auto const& movement = assets_->animations.movement;
    ceremony_->phase     = phase;
    ceremony_->walk      = WanderStep{from, to};
    ceremony_->elapsed   = std::chrono::milliseconds{0};
    ceremony_->duration  = PetWander::walkDuration(ceremony_->walk);
    LENS_DEBUG("pet ceremony: walk ({},{}) -> ({},{}) in {} ms", from.x(), from.y(), to.x(), to.y(), ceremony_->duration.count());
    if (ceremony_->duration.count() == 0) return true;
    if (not machine_->playAmbient(movement->walk)) {
        LENS_WARN("the pet ceremony cannot play the walk action: {}", movement->walk);
        return false;
    }
    return true;
}

bool PetController::beginCeremonyRole(CeremonyPhase phase, Role role, std::chrono::milliseconds beat)
{
    if (not ceremony_ or not machine_) return false;
    auto const playback = rolePlayback(role);
    if (not machine_->playRole(role)) return false;
    ceremony_->phase    = phase;
    ceremony_->elapsed  = std::chrono::milliseconds{0};
    ceremony_->duration = playback + beat;
    LENS_DEBUG("pet ceremony: {} plays for {} ms", assets_->animations.playbook.roles.at(role), ceremony_->duration.count());
    return true;
}

std::chrono::milliseconds PetController::rolePlayback(Role role) const
{
    if (not assets_) return std::chrono::milliseconds{0};
    auto const& playbook = assets_->animations.playbook;
    auto const  named    = playbook.roles.find(role);
    if (named == playbook.roles.end()) return std::chrono::milliseconds{0};
    auto const found = playbook.actions.find(named->second);
    if (found == playbook.actions.end()) return std::chrono::milliseconds{0};
    return std::chrono::milliseconds{found->second.frames * 1000 / found->second.fps};
}

void PetController::beginExitWalk()
{
    if (not ceremony_ or not window_) {
        hideNow();
        return;
    }
    auto const from = window_->position();
    auto const area = areaAt(from);
    if (not area.isValid()) {
        hideNow();
        return;
    }
    auto const to = PetWander::offscreenBase(area, window_->size(), from, ceremony_->side);
    if (not beginCeremonyWalk(CeremonyPhase::Exiting, from, to)) hideNow();
}

void PetController::settleAtHome()
{
    if (machine_) {
        machine_->stopAmbient();
        machine_->rest();
    }
    ceremony_.reset();
    pendingPet_.clear();
    hideAfterDrag_ = false;
    if (window_ and wander_) {
        auto const home = homeReady_ ? wander_->home() : window_->position();
        auto const area = areaAt(home);
        if (area.isValid()) {
            auto const at = clampedWithin(home, window_->size(), area);
            window_->setPosition(at);
            wander_->setHome(at);
            wander_->setBounds(window_->size(), area);
            homeReady_ = true;
        } else {
            window_->setPosition(home);
        }
    }
    wanderWait_ = nextWanderWait();
    setShown(true);
    LENS_DEBUG("pet ceremony: settled at home");
}

void PetController::finishCeremonyNow()
{
    if (not ceremony_) return;
    if (isLeaving(ceremony_->phase)) {
        hideNow();
        return;
    }
    if (not pendingPet_.empty()) applyPendingPet();
    settleAtHome();
}

void PetController::abortCeremony()
{
    if (not ceremony_) return;
    if (machine_) {
        machine_->stopAmbient();
        machine_->rest();
    }
    pendingPet_.clear();
    ceremony_.reset();
    wanderWait_ = nextWanderWait();
}

void PetController::hideNow()
{
    if (machine_) {
        machine_->stopAmbient();
        machine_->rest();
    }
    cancelStroll();
    endHiding();
    resetHover();
    ceremony_.reset();
    pendingPet_.clear();
    hideAfterDrag_ = false;
    wanderWait_    = nextWanderWait();
    setShown(false);
    LENS_DEBUG("pet ceremony: off screen");
}

void PetController::applyPendingPet()
{
    if (pendingPet_.empty()) return;
    auto const target  = pendingPet_;
    auto const inherit = wander_ and homeReady_ ? wander_->home() : (window_ ? window_->position() : QPoint{});
    auto const area    = window_ ? areaAt(window_->position()) : QRect{};
    applyPet(target);
    if (not window_ or not ceremony_) return;

    auto const size = window_->size();
    // The new pet returns to the nest it remembers, on the screen it was left on; one that has no nest yet inherits
    // the spot the swap happened on (ADR 0096).
    auto at = inherit;
    if (auto const nest = nestOf(store_.pet(), size))
        at = *nest;
    else if (area.isValid())
        at = clampedWithin(inherit, size, area);
    auto const nestArea = areaAt(at);
    if (wander_) {
        wander_->setHome(at);
        wander_->setBounds(size, nestArea);
        homeReady_ = nestArea.isValid();
    }
    ceremony_->home = at;
    // A pet on screen for the first time keeps the spot it lands on as its own nest, so the inheritance survives
    // a restart (ADR 0096).
    if (not store_.position(store_.pet())) savePosition(at);

    if (assets_ and area.isValid()) {
        // The QML window takes its new size on the next event turn. Park it against the larger of the two until then,
        // so a wider pet cannot show a sliver while the swap finishes.
        auto const expected = static_cast<int>(std::lround(assets_->canvas * scale()));
        auto const padded   = QSize{expected + 64, expected + 64};
        window_->setPosition(PetWander::offscreenBase(area, padded, at, ceremony_->side));
    }
}

void PetController::setShown(bool on)
{
    if (shown_ == on) return;
    shown_ = on;
    emit shownChanged();
}

QPoint PetController::cursorPos() const
{
    return QCursor::pos();
}

QPoint PetController::initialPosition(QSize size)
{
    // The saved nest of the pet the settings name, or the corner when that no longer fits.
    auto at = bottomRightOf(size, QGuiApplication::primaryScreen()->availableGeometry());
    if (auto const nest = nestOf(store_.pet(), size)) at = *nest;
    if (wander_) {
        wander_->setHome(at);
        wander_->setBounds(size, areaAt(at));
        homeReady_ = true;
    }
    return at;
}

std::optional<QPoint> PetController::nestOf(std::string const& id, QSize size) const
{
    auto const saved = store_.position(id);
    if (not saved) return std::nullopt;
    for (auto const* screen : QGuiApplication::screens()) {
        if (deviceNameOf(*screen).toStdString() != saved->screen) continue;
        auto const area = screen->availableGeometry();
        auto const spot = area.topLeft() + QPoint{saved->x, saved->y};
        if (fitsWithin(spot, size, area)) return spot;
        return std::nullopt;
    }
    return std::nullopt;
}

void PetController::fitOnScreen()
{
    if (not window_) return;
    // A throw was measured for the old size; the resize puts the pet down and the clamp below writes the safe spot as
    // its home.
    if (flight_) endFlight(window_->position(), true);
    resetThrowMood();
    // A ceremony owns the window's path. A switch resizes the window deliberately while the pet is off screen; the
    // arrival measures its target again once the new size is in place.
    if (ceremony_) return;
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
    store_.setPosition(store_.pet(), SavedPosition{device.toStdString(), offset.x(), offset.y()});
}

void PetController::tick()
{
    machine_->advance(kTick);
    throwMood_.advance(kTick);
    updateFlight(kTick);
    updateLandingAngry();
    updateCeremony(kTick);
    updateWander(kTick);
    updateHiding(kTick);
    updateHover(kTick);
    sync(kTick);
}

void PetController::updateFlight(std::chrono::milliseconds step)
{
    if (not flight_) return;
    if (not window_ or not machine_ or not enabled() or not desktopRunning_) {
        endFlight(window_ ? window_->position() : QPoint{}, true);
        return;
    }
    auto const next = flight_->advance(step);
    window_->setPosition(next.position);
    if (next.landed) landFlight();
}

void PetController::updateWander(std::chrono::milliseconds step)
{
    if (not machine_ or not assets_ or not window_ or not wander_ or hiding_ or ceremony_ or flight_ or
        angryRepeatsLeft_ > 0)
        return;
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

    if (hidden.leaving) {
        // A click on the sliver called the pet out: the window slides back to where the hide began, eased like the
        // slide out, and the hide ends there.
        hidden.leave += step;
        if (hidden.leave < PetWander::kHideSlide) {
            window_->setPosition(PetWander::slideAt(hidden.leaveFrom, hidden.leaveTo, hidden.leave));
            return;
        }
        settleOutOfHide();
        return;
    }

    if (hidden.slide < PetWander::kHideSlide) {
        hidden.slide = std::min(PetWander::kHideSlide, hidden.slide + step);
        window_->setPosition(PetWander::slideAt(hidden.from, hidden.base, hidden.slide));
    } else if (hidden.asking) {
        // The question and its sign stand beside the pet; the peek waits until the reaction is over.
        window_->setPosition(hidden.base);
    } else {
        hidden.peekClock += step;
        // A pointer near the visible sliver earns a further lean toward the screen, ramped so it reads as a lean.
        // The peek itself keeps its own cycle underneath (ADR 0095).
        auto const target = hover_ and hover_->watching() ? static_cast<double>(PetWander::kHoverLean) : 0.0;
        auto const rate   = static_cast<double>(PetWander::kHoverLean) / PetWander::kHoverLeanTime.count();
        hidden.lean       = hidden.lean < target ? std::min(target, hidden.lean + rate * step.count())
                                                 : std::max(target, hidden.lean - rate * step.count());
        auto at           = hidden.base;
        at.rx() += (hidden.side == EdgeSide::Left ? 1 : -1) *
                   (PetWander::peekOffset(hidden.peekClock) + static_cast<int>(std::lround(hidden.lean)));
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
    // A pounce is one leap; every other gait takes one to the behavior's step count.
    stepsLeft_ = gaitOf(assets_->animations.movement->style) == Gait::Pounce
                     ? 1
                     : 1 + static_cast<int>(randomUnit() * behavior_.strollSteps);
    if (not beginStep()) finishStroll();
}

void PetController::finishStroll()
{
    stroll_    = StrollPhase::Waiting;
    stepsLeft_ = 0;
    step_.reset();
    hoverPause_ = false;
    wanderWait_ = nextWanderWait();
    // The walk is over and the pet is back at rest: the after-return context is up, and it looks around once
    // before the next idle turn is drawn (ADR 0093, 0098).
    returnAfterUntil_ = std::chrono::steady_clock::now() + kReturnAfterglow;
    if (machine_ and machine_->idle()) machine_->playRole(Role::LookAround);
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
    // A drag that never pulled the pet out puts the hide back as it was, including one caught mid-slide from a click.
    hiding_->leaving   = false;
    hiding_->lean      = 0.0;
    hiding_->slide     = PetWander::kHideSlide;
    hiding_->peekClock = std::chrono::milliseconds{0};
    window_->setPosition(hiding_->base);
}

void PetController::lureOut()
{
    if (not hiding_ or not window_ or hiding_->dragging or hiding_->leaving) return;
    auto& hidden     = *hiding_;
    hidden.leaving   = true;
    hidden.leave     = std::chrono::milliseconds{0};
    hidden.leaveFrom = window_->position();
    auto const size  = window_->size();
    hidden.leaveTo   = clampedWithin(hidden.from, size, hidden.area);
}

void PetController::settleOutOfHide()
{
    if (not hiding_) return;
    auto const at   = hiding_->leaveTo;
    auto const area = hiding_->area;
    auto const size = window_ ? window_->size() : QSize{};
    hiding_.reset();
    if (machine_ and machine_->ambient()) machine_->stopAmbient();
    if (window_) {
        window_->setPosition(at);
        savePosition(at);
    }
    if (wander_ and size.isValid()) {
        wander_->setHome(at);
        wander_->setBounds(size, area);
        homeReady_ = true;
    }
    stroll_     = StrollPhase::Waiting;
    wanderWait_ = nextWanderWait();
}

void PetController::refitHiding()
{
    if (not hiding_ or not window_ or not assets_) return;
    auto&      hidden = *hiding_;
    auto const size   = window_->size();
    auto const scale  = static_cast<double>(size.width()) / static_cast<double>(assets_->canvas);
    hidden.base       = PetWander::hiddenBase(hidden.area, size, hidden.side, hidden.base, assets_->peekInkLeft * scale, assets_->peekInkRight * scale);
    hidden.slide      = PetWander::kHideSlide;
    hidden.peekClock  = std::chrono::milliseconds{0};
    window_->setPosition(hidden.base);
}

std::chrono::milliseconds PetController::nextWanderWait()
{
    auto const spread = static_cast<double>((behavior_.wanderWaitMax - behavior_.wanderWaitMin).count());
    return behavior_.wanderWaitMin +
           std::chrono::milliseconds{static_cast<std::chrono::milliseconds::rep>(randomUnit() * spread)};
}

core::pet::PetMood PetController::currentMood() const
{
    auto       mood          = core::pet::PetMood{};
    auto const now           = QTime::currentTime();
    mood.minuteOfDay         = now.hour() * 60 + now.minute();
    auto const tick          = std::chrono::steady_clock::now();
    mood.justFinishedReading = tick < readingAfterUntil_;
    mood.justReturned        = tick < returnAfterUntil_;
    return mood;
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

bool PetController::cursorOnPet() const
{
    if (not window_ or not window_->isVisible() or store_.passthrough()) return false;
    return QRect{window_->position(), window_->size()}.contains(QCursor::pos());
}

void PetController::updateHover(std::chrono::milliseconds step)
{
    // A review question is not hover: while the sign or its open card is up, the eyes go to that side even when the
    // pet is not idle, clicks pass through, or the peek is paused (ADR 0097).
    if (not reviewLook_.isNull()) {
        setGaze(reviewLook_);
        return;
    }

    // Hover is the desktop pet's own sense. It has no meaning while clicks pass through, while the settings preview
    // is open, or before a window exists to measure against.
    if (not hover_ or not assets_ or not window_ or not desktopRunning_ or previewing_ or ceremony_ or flight_ or
        angryRepeatsLeft_ > 0 or store_.passthrough() or not window_->isVisible()) {
        resetHover();
        return;
    }

    // A hidden pet keeps its own, smaller share: eyes and a deeper lean toward the sliver, never a reaction.
    if (hiding_) {
        updateHiddenHover(step);
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

void PetController::updateHiddenHover(std::chrono::milliseconds step)
{
    auto& hidden = *hiding_;
    // The window still sliding out, a sign that is up, a click's slide back in, or a reaction playing: none of them
    // are the pet peeking on its own terms, so the approach is forgotten and the lean withdraws.
    if (hidden.dragging or hidden.leaving or hidden.asking or hidden.slide < PetWander::kHideSlide or
        not machine_->ambient() or not window_->isVisible()) {
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

    // The zone is tracked against the visible sliver only, and idle is always false: a hidden pet looks and leans,
    // but never greets (ADR 0095).
    hover_->update(step, sliverDistanceAt(cursor), false);

    if (hover_->watching())
        setGaze(gazeToward(at, (assets_->peekInkLeft + assets_->peekInkRight) / 2.0, (assets_->inkTop + assets_->inkBottom) / 2.0));
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
    return gazeToward(cursor, assets_->inkCentre, (assets_->inkTop + assets_->inkBottom) / 2.0);
}

QPoint PetController::gazeToward(QPointF cursor, double centreX, double middleY) const
{
    auto const dx = cursor.x() - centreX;
    auto const dy = cursor.y() - middleY;
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

double PetController::sliverDistanceAt(QPoint cursor) const
{
    auto const size   = window_->size();
    auto const origin = window_->position();
    auto const sx     = static_cast<double>(size.width()) / assets_->canvas;
    auto const sy     = static_cast<double>(size.height()) / assets_->canvas;
    // The sliver is the peek ink clipped at the work-area edge; the rows above and below the figure never count.
    auto        x1   = origin.x() + assets_->peekInkLeft * sx;
    auto        x2   = origin.x() + assets_->peekInkRight * sx;
    auto const  y1   = origin.y() + assets_->inkTop * sy;
    auto const  y2   = origin.y() + assets_->inkBottom * sy;
    auto const& area = hiding_->area;
    if (hiding_->side == EdgeSide::Left)
        x1 = std::max(x1, static_cast<double>(area.left()));
    else
        x2 = std::min(x2, static_cast<double>(area.right() + 1));
    auto const dx = std::max({x1 - cursor.x(), 0.0, cursor.x() - x2});
    auto const dy = std::max({y1 - cursor.y(), 0.0, cursor.y() - y2});
    return std::hypot(dx, dy) / sx;
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
        sheets_ = QVariantMap{{"body", urlOf(pictures.body)}, {"effect", urlOf(pictures.effect)}, {"expression", urlOf(assets.expressionSheet)}};
    }
    action_     = name;
    frameIndex_ = frame;
    expression_ = expression;
    anchors_    = assets.anchors.at(action).at(static_cast<std::size_t>(frame));

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
