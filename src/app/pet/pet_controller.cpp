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
/// The pet's frame clock should publish a visible frame well inside this window; a longer silence
/// means the state machine or the clock has stalled, not that the pet is resting.
constexpr std::chrono::milliseconds kStallAfter{4000};
constexpr int                       kHealthIntervalMs{2000};
/// Time between blinks while the pet is awake. The prototype blinks on the same cadence.
constexpr std::chrono::milliseconds kBlinkEvery{4000};

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

/// @return Whether an accessory covers the eyes. A postcard dresses the pet without glasses, so the face stays
///         visible: the face slot takes the first piece that is not one of these, and wears nothing when every piece
///         in it is. Head and body are never filtered.
bool isEyewear(std::string const& id)
{
    static constexpr char const* kEyewear[] = {
        "wire_glasses",
        "cat_shades",
        "spectacles",
        "round_shades",
        "heart_frames",
        "monocle",
        "star_frames",
        "glasses",
        "candy_shades",
        "readers",
        "sleep_mask",
        "bubble_glasses",
        "dive_mask",
        "raindrop_glasses",
        "cloud_frames",
    };
    for (auto const name : kEyewear)
        if (id == name) return true;
    return false;
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
    // A defensive check around the frame clock: if it is stopped while the window is still up, or
    // if it runs without publishing a frame, return the pet to idle instead of leaving it frozen.
    health_.setInterval(kHealthIntervalMs);
    connect(&health_, &QTimer::timeout, this, &PetController::checkHealth);
    health_.start();
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

PetController::~PetController() = default;

void PetController::loadPet(std::string const& id)
{
    // A hidden window belongs to the pet that is going away; the replacement starts from the reader's home, not
    // from the off-screen sliver the old pet was hiding in.
    if (hide_.active() and window_ and wander_ and placement_.home()) window_->setPosition(*placement_.home());

    // What is on screen belongs to the pet that is going away, so none of it carries over.
    preview_.reset();
    hide_.stop();
    throw_.stop(ThrowStop::PetChanged);
    stroll_.stop();
    placement_.forgetHome();
    dragging_ = false;
    wander_.reset();
    action_.clear();
    shownAction_.clear();
    sheets_.clear();
    anchors_      = {};
    frameIndex_   = 0;
    expression_   = 0;
    blinkRunning_ = false;
    blinkWait_    = kBlinkEvery;
    // The lottery's memory, the entry and its impact belong to the pet that is going away; the two after-contexts
    // belong to the reader, so they stay.
    stage_.reset();
    entryPose_.reset();
    clearScene();
    sceneEntryDirty_ = false;
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
        assets_ = PetAssets::load(entry->directory, entry->source);
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
            auto        offers   = std::vector<PetStage::Offer>{};
            for (auto const& scene : scenes_.all())
                if (playbook.actions.count(scene.id) != 0) {
                    // A worn accessory tilts the lottery toward the scenes it names (ADR 0102); without one this is 1.
                    offers.push_back({scene.id, scene.mood, wardrobe_ ? wardrobe_->sceneWeightFor(scene.id) : 1.0});
                }
            return stage_.draw(offers, currentMood());
        };
        machine_  = std::make_unique<core::pet::PetStateMachine>(assets_->animations.playbook, random, scenes, behavior_, mood);
        wardrobe_ = std::make_unique<core::pet::Wardrobe>(assets_->catalogue);

        auto gait      = Gait::Glide;
        auto walkCycle = std::chrono::milliseconds{0};
        auto canHide   = false;
        if (auto const& movement = assets_->animations.movement) {
            wander_          = std::make_unique<PetWander>(gaitOf(movement->style), movement->range, random, behavior_.hideChance);
            auto const& walk = assets_->animations.playbook.actions.at(movement->walk);
            gait             = gaitOf(movement->style);
            walkCycle        = std::chrono::milliseconds{walk.frames * 1000 / walk.fps};
            canHide          = movement->peek.has_value();
        }
        stroll_.load(behavior_, gait, walkCycle, canHide);

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
    lastFramePulse_ = std::chrono::steady_clock::now();
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
        stage_.readingBegan(std::chrono::steady_clock::now());
    } else if (event == PetEvent::ExplanationHidden) {
        stage_.readingEnded(std::chrono::steady_clock::now());
    }
    // The sign reports its side after ReviewAsked; the events that take it down leave no direction behind.
    if (event == PetEvent::ReviewAsked or event == PetEvent::ReviewRemembered or event == PetEvent::ReviewForgotten or
        event == PetEvent::ReviewIgnored)
        reviewLook_ = {};
    if (not machine_) return;
    // DragStart and DragEnd only arrive here after the drag has already settled the throw.
    if (throw_.active() and event != PetEvent::DragStart and event != PetEvent::DragEnd) {
        // An outside event owns the body: put the throw down where it is and answer. The landing spot becomes home.
        // A click starts the run over; a learning event only cancels the plays still owed (ADR 0092).
        endThrow(window_ ? window_->position() : QPoint{}, true, event == PetEvent::Click ? ThrowStop::Clicked : ThrowStop::Learning);
    }
    if (ceremony_.active()) {
        // A pet on its way out is not called back by a learning event. One arriving, or changing pets, lands at its
        // outcome first: the event is what the reader is doing, and the walk has nowhere else to be.
        if (ceremony_.leaving()) return;
        finishCeremonyNow();
    }
    // An outside event voids the current wait: the next stroll is drawn again once the pet is idle.
    stroll_.wait();
    if (hide_.active() and event == PetEvent::ReviewAsked) {
        auto const answer = hide_.ask();
        if (answer.left)
            settleOutOfHide(answer.position);
        else if (window_)
            window_->setPosition(answer.position);
    }
    resetHover();
    machine_->handle(event);
    sync(std::chrono::milliseconds{0});
}

PetPose* PetController::pose()
{
    return &pose_;
}

PetPicker* PetController::picker()
{
    return &picker_;
}

PetHost* PetController::host()
{
    return &host_;
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
    if (not enabled() or not machine_ or dragging_ or ceremony_.active() or throw_.flying() or throw_.repeatsOwed() or hideAfterDrag_)
        return false;
    // A hidden pet may still be asked while it is peeking: the sign goes up at the edge and the hide survives the
    // answer. A reaction or a reading plays out first, as it does for an idle pet.
    return machine_->idle() or (hide_.active() and machine_->ambient());
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

QVariantMap PetController::portrait(QString id)
{
    auto const key = id.toStdString();
    if (auto const found = portraits_.find(key); found != portraits_.end())
        return found->second;

    // Only a shipped pet is pictured: a package's art is the reader's, and the card stands for the pets Lens came
    // with (UI-SPEC.md 4.4). An id the catalogue does not hold, and a folder that fails its own checks, both answer
    // with nothing -- the picker raises no card rather than an empty one.
    auto card = QVariantMap{};
    if (auto const* entry = catalog_.find(key); entry and entry->source == PetSource::Shipped) {
        try {
            auto const  assets   = PetAssets::load(entry->directory, entry->source);
            auto const  idle     = assets.animations.playbook.roles.at(core::pet::Role::Idle);
            auto const& frames   = assets.anchors.at(idle);
            auto const& pictures = assets.sheets.at(idle);
            if (not frames.empty()) {
                card = QVariantMap{
                    {"canvas", assets.canvas},
                    {"foot", QPointF{assets.inkCentre, assets.inkBottom}},
                    {"sheets", QVariantMap{{"body", urlOf(pictures.body)}, {"expression", urlOf(assets.expressionSheet)}}},
                    {"anchors", QVariantMap{{"body", toPoint(frames.front().body)}, {"face", toPoint(frames.front().face)}}},
                    {"expression", pictures.expressions.empty() ? 0 : pictures.expressions.front()},
                };

                // A card dresses the pet: one piece per slot, head then body then face, the order the settings page
                // lays the slots out. The face slot skips eyewear so the face stays visible; head and body always
                // find a piece, so a card never shows a bare pet. A pet whose face slot is all eyewear wears two.
                auto outfit = QVariantList{};
                for (auto const slot : {Slot::Head, Slot::Body, Slot::Face}) {
                    for (auto const& spec : assets.catalogue) {
                        if (spec.slot != slot) continue;
                        if (slot == Slot::Face && isEyewear(spec.id)) continue;
                        auto const art = assets.accessories.find(spec.id);
                        if (art == assets.accessories.end()) continue;
                        auto const at = pointFor(frames.front(), spec.slot);
                        outfit.append(QVariantMap{{"id", QString::fromStdString(spec.id)},
                                                  {"asset", urlOf(art->second.asset)},
                                                  {"zIndex", art->second.zIndex},
                                                  {"x", at.x()},
                                                  {"y", at.y()}});
                        break;
                    }
                }
                card.insert("outfit", outfit);
            }
        } catch (std::exception const& e) {
            LENS_WARN("the pet picker has no card for {}: {}", key, e.what());
        }
    }
    return portraits_.emplace(key, card).first->second;
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
    // An entry draws the idle pose while the machine is already in the scene, so the pieces follow the pose on screen.
    auto const pose = stage_.entering() and entryPose_ ? entryPose_->poseAction : shownAction();
    for (auto const& id : wardrobe_->visibleFor(pose)) {
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

QVariantMap PetController::sceneEntry() const
{
    auto impact = QVariantMap{};
    if (auto const running = stage_.impact()) {
        impact = QVariantMap{{"kind", running->kind}, {"at", running->at}, {"progress", running->progress}};
    }
    auto const  active = stage_.entering();
    auto const& entry  = stage_.frame();
    return QVariantMap{{"active", active},
                       {"inFront", entry.inFront},
                       {"propAlpha", entry.propAlpha},
                       {"propOffset", entry.propOffset},
                       {"idleAlpha", entry.idleAlpha},
                       {"sceneAlpha", entry.sceneAlpha},
                       {"sceneSheets", active and entryPose_ ? entryPose_->sheets : QVariantMap{}},
                       {"sceneAnchors", active and entryPose_ ? entryPose_->anchors : QVariantMap{}},
                       {"sceneExpression", active and entryPose_ ? entryPose_->expression : 0},
                       {"impact", impact}};
}

QVariantList PetController::accessories() const
{
    auto list = QVariantList{};
    if (not assets_) return list;
    for (auto const& spec : assets_->catalogue) {
        auto item = QVariantMap{{"id", QString::fromStdString(spec.id)}, {"slot", slotName(spec.slot)}};
        // Only a scene this pet actually plays can be tilted, so the settings line never promises a place it cannot go.
        if (scenesAllowed_) {
            auto weights = QVariantMap{};
            for (auto const& [id, weight] : spec.sceneWeights) {
                if (scenes_.find(id) != nullptr and assets_->animations.playbook.actions.count(id) != 0)
                    weights.insert(QString::fromStdString(id), weight);
            }
            if (not weights.isEmpty()) item.insert("sceneWeights", weights);
        }
        list.append(item);
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

QVariantMap PetController::reviewSign() const
{
    if (not assets_ or not assets_->reviewSign) return {};
    auto const& sign = *assets_->reviewSign;
    auto        bins = QVariantList{};
    for (auto const& bin : sign.bins) {
        bins.push_back(QVariantMap{{"width", bin.width},
                                   {"W", bin.boardWidth},
                                   {"cellWidth", bin.cellWidth},
                                   {"cellHeight", bin.cellHeight},
                                   {"source", QUrl::fromLocalFile(bin.sheet)}});
    }
    return QVariantMap{{"frames", sign.frames},
                       {"every", sign.every},
                       {"beatMs", sign.beatMs},
                       {"margin", sign.margin},
                       {"foot", sign.foot},
                       {"wordTop", sign.wordTop},
                       {"wordColor", sign.wordColor},
                       {"bins", bins}};
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
    endThrow(window_ ? window_->position() : QPoint{}, true, ThrowStop::Resized);
    // The ceremony's path was measured for the old window; a size change lands it at home and lets fitOnScreen pull
    // the new window into the work area.
    if (ceremony_.active()) finishCeremonyNow();
    store_.setSize(store_.pet(), percent);
    emit settingsChanged();
}

void PetController::setRunning(bool on)
{
    desktopRunning_ = on;
    if (on)
        lastFramePulse_ = std::chrono::steady_clock::now();
    if (not on) {
        // The window is going away under a throw: stop it without leaving the pickup pose behind.
        if (throw_.flying() and machine_) machine_->rest();
        throw_.stop(ThrowStop::WindowGone);
        // The window was taken away under a ceremony (the application is closing, or the scene asked it down):
        // stop the ceremony rather than leave it half done.
        if (ceremony_.active()) {
            ceremony_.stop();
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
    // A preview takes the body: a throw is put down where it is and that spot becomes home.
    if (on) endThrow(window_ ? window_->position() : QPoint{}, true, ThrowStop::Staged);
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
    endThrow(window_ ? window_->position() : QPoint{}, true, ThrowStop::Staged);
    if (ceremony_.active()) finishCeremonyNow();
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
    endThrow(window_ ? window_->position() : QPoint{}, true, ThrowStop::Staged);
    if (ceremony_.active()) finishCeremonyNow();
    preview_.reset();
    resetHover();
    stage_.notePlayed(name);
    machine_->playScene(name);
    // The settings page calls this directly, so the entry starts here rather than waiting for the idle roll's sync.
    if (auto const* scene = scenesAllowed_ ? scenes_.find(name) : nullptr;
        scene and machine_->action() == name)
        beginSceneEntry(*scene, name);
    sync(std::chrono::milliseconds{0});
}

void PetController::applyScene(Scene const& scene, PetStage::Frame const& frame)
{
    if (not assets_) return;
    scene_      = QString::fromStdString(scene.id);
    sceneFrame_ = frame.index;
    publishSceneTransform(frame.shift, frame.scale);
    sceneAlign_  = frame.align;
    sceneSheets_ = QVariantMap{{"back", urlOf(scene.backSheet)}, {"front", urlOf(scene.frontSheet)}};
    sceneLayers_ = QVariantMap{{"back", scene.backLayer}, {"front", scene.frontLayer}};
}

void PetController::publishSceneTransform(QPointF shift, double scale)
{
    sceneShift_ = shift;
    sceneScale_ = scale;
}

void PetController::clearScene()
{
    scene_.clear();
    sceneSheets_.clear();
    sceneLayers_.clear();
    sceneFrame_ = 0;
    publishSceneTransform({}, 1.0);
    sceneAlign_ = {};
}

void PetController::beginSceneEntry(Scene const& scene, core::pet::ActionName const& action)
{
    if (not assets_ or not scene.entry or motionReduced_) return;

    // A hop holds the idle body until it lands. Settle and fade already wear the scene body; settle only folds the
    // idle body out over the last third of the slide.
    auto poseAction = action;
    if (scene.entry->style != SceneEntryStyle::Fade) {
        auto const idle = assets_->animations.playbook.roles.find(Role::Idle);
        if (idle == assets_->animations.playbook.roles.end()) return;
        poseAction = idle->second;
    }
    auto const& playbook = assets_->animations.playbook;
    auto const  poseSpec = playbook.actions.find(poseAction);
    if (poseSpec == playbook.actions.end()) return;

    // A hop that catches the pet mid-bob carries on from the frame it was on rather than restarting the idle loop.
    auto poseStartFrame = 0;
    if (action_ == QString::fromStdString(poseAction) and frameIndex_ >= 0 and frameIndex_ < poseSpec->second.frames)
        poseStartFrame = frameIndex_;

    auto const& scenePictures = assets_->sheets.at(action);
    auto const& sceneAnchors  = assets_->anchors.at(action).at(0);
    entryPose_                = EntryPose{poseAction,
                           QVariantMap{{"body", urlOf(scenePictures.body)},
                                                      {"effect", urlOf(scenePictures.effect)},
                                                      {"expression", urlOf(assets_->expressionSheet)}},
                           QVariantMap{{"body", toPoint(sceneAnchors.body)}, {"face", toPoint(sceneAnchors.face)}},
                           scenePictures.expressions.empty() ? 0 : scenePictures.expressions.front()};

    stage_.beginEntry(scene,
                      PetStage::Pose{poseSpec->second.frames, poseSpec->second.fps, poseSpec->second.loop, poseStartFrame},
                      assets_->inkTop);
    applyScene(scene, PetStage::frameOf(scene, 0, foot()));

    action_          = QString::fromStdString(action);
    sceneEntryDirty_ = false;
    setGaze({});
    blinkRunning_ = false;
    blinkWait_    = kBlinkEvery;
}

void PetController::dragStart()
{
    if (throw_.flying()) {
        // Catching a pet in the air ends the throw where it is; this drag, not the throw, decides where it lands.
        endThrow(window_ ? window_->position() : QPoint{}, false, ThrowStop::Caught);
    } else {
        // Picking up a settled pet takes the plays its landing still owed; the run goes on with the next throw.
        throw_.stop(ThrowStop::Held);
    }

    if (ceremony_.active()) {
        // Grabbing the pet ends its ceremony where it stands. A goodbye was taking the window off screen, so the drop
        // completes it instead of leaving a turned-off pet on the desktop.
        hideAfterDrag_ = ceremony_.leaving();
        abortCeremony();
    }
    dragging_ = true;
    cancelStroll();
    if (hide_.active()) hide_.dragStart();
    handle(PetEvent::DragStart);
}

void PetController::dragEnd()
{
    // A release that is not a throw ends the run of landed throws.
    throw_.stop(ThrowStop::Dropped);
    dragging_ = false;
    if (hideAfterDrag_) {
        hideAfterDrag_ = false;
        handle(PetEvent::DragEnd);
        setShown(false);
        stroll_.wait();
        return;
    }
    if (hide_.active()) {
        auto const at   = window_ ? window_->position() : QPoint{};
        auto const size = window_ ? window_->size() : QSize{};
        auto const area = hide_.area();
        auto const drop = hide_.drop(at, size);
        handle(PetEvent::DragEnd);
        if (drop.pulledOut) {
            adoptHome(at);
            if (wander_ and size.isValid()) wander_->setBounds(size, area);
            stroll_.wait();
        } else if (window_) {
            // Not far enough out: the pet keeps hiding, and the drag leaves no trace in the settings.
            window_->setPosition(drop.snapBackTo);
        }
        return;
    }

    handle(PetEvent::DragEnd);
    if (wander_ and window_) {
        adoptHome(window_->position());
        wander_->setBounds(window_->size(), areaAt(window_->position()));
    }
    stroll_.wait();

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
    if (hide_.active() and not hide_.pulledOut(at, size)) return false;
    if (hideAfterDrag_ or motionReduced_) return false;

    auto const center = at + QPoint{size.width() / 2, size.height() / 2};
    auto const area   = areaAt(center);
    if (not area.isValid() or size.width() <= 0 or size.height() <= 0) return false;
    if (not throw_.begin(at, velocity, size, area)) return false;

    if (hide_.active()) hide_.stop();
    dragging_ = false;
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

void PetController::endThrow(QPoint at, bool saveHome, ThrowStop why)
{
    auto const flying = throw_.flying();
    throw_.stop(why);
    if (not flying) return;

    if (window_ and window_->position() != at) window_->setPosition(at);
    // The pickup pose is the throw's; the machine learns the drag ended where the throw was put down.
    handle(PetEvent::DragEnd);
    if (saveHome and window_) savePosition(at);
}

void PetController::click()
{
    // A click on the visible sliver calls the pet out rather than poking it: no reaction, no anger count (ADR 0095).
    // A ceremony is the pet's own business too: a click left over from catching it does not cut the walk.
    if (hide_.active()) {
        lureOut();
        return;
    }
    if (ceremony_.active()) return;
    handle(PetEvent::Click);
}

void PetController::stopWandering()
{
    if (hide_.active()) {
        stroll_.stop();
        return;
    }
    stroll_.hover();
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
    pulse();
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
    endThrow(window_ ? window_->position() : QPoint{}, true, ThrowStop::PetChanged);

    // With no window under the tests, with the pet off, or with motion reduced, the choice lands at once.
    if (not enabled() or not shown_ or not window_ or motionReduced_ or not canWalkCeremony()) {
        if (ceremony_.active()) {
            if (ceremony_.leaving())
                hideNow();
            else
                abortCeremony();
        }
        applyPet(name);
        return;
    }

    // A new choice interrupts the ceremony before it; the pet it interrupted is the one that walks out.
    if (ceremony_.active()) abortCeremony();
    if (hide_.active()) endHiding();
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
    if (ceremony_.active()) {
        if (ceremony_.leaving())
            hideNow();
        else
            abortCeremony();
    }
    catalog_ = scanPets(shippedRoot_, installedRoot_);
    // A rescan is the one moment a folder may have changed under a card already read.
    portraits_.clear();
    auto const chosen = store_.pet();
    if (not usable(chosen)) switchToDefault(chosen);
    emit catalogChanged();
    emit settingsChanged();
    pulse();
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
    pulse();
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
    // A throw ends where it is; the pet then leaves from that spot like any other.
    endThrow(window_ ? window_->position() : QPoint{}, true, ThrowStop::Toggled);

    if (on) {
        if (ceremony_.active() and ceremony_.leaving()) {
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
            if (ceremony_.active()) abortCeremony();
            if (machine_) machine_->rest();
        }
    } else {
        if (ceremony_.active() and ceremony_.leaving()) {
            // The goodbye is already playing.
        } else if (hide_.active()) {
            // It is already at the edge and mostly out of sight; there is no goodbye left to show.
            endHiding();
            hideNow();
        } else if (not window_ or not shown_ or motionReduced_ or not canWalkCeremony()) {
            hideNow();
        } else if (dragging_) {
            if (ceremony_.active()) abortCeremony();
            hideAfterDrag_ = true;
        } else {
            // A switch in progress gives way to the goodbye; the pet already loaded is the one that leaves.
            if (ceremony_.active()) abortCeremony();
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
    // Reduced motion skips the rest of the arc: the pet stays where it is, and that spot becomes home. Turning it
    // off is not an interruption: it neither moves the pet nor ends a run of landings.
    if (on) endThrow(window_ ? window_->position() : QPoint{}, true, ThrowStop::MotionReduced);
    // A scene entry is a movement too: reduced motion puts the pet straight into its scene pose, and the scene stays
    // remembered as entered so turning the setting back off does not replay the entry.
    if (on and (stage_.entering() or stage_.impact())) {
        stage_.landEntry();
        entryPose_.reset();
        sceneEntryDirty_ = true;
        actionElapsed_   = std::chrono::milliseconds{0};
        // The dirty flag makes sync republish the scene pose even when the scene frame was already 0; QML then drops
        // the overlay and the props' opacity returns to 1.
        if (assets_ and machine_) sync(std::chrono::milliseconds{0});
    }
    if (not on or not ceremony_.active()) return;
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
    syncScreens();
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
    // A screen that just arrived is not in the placement yet; the others are, so one read covers them all.
    syncScreens();
}

void PetController::keepOutOfCapture(QObject* object)
{
    onShown(qobject_cast<QWindow*>(object), [](QWindow const& window) {
        if (not excludeFromCapture(window)) LENS_WARN("a window that must stay out of screen captures can appear in them: Windows refused the exclusion");
    });
}

void PetController::syncScreens()
{
    auto areas = QList<QRect>{};
    for (auto const* screen : QGuiApplication::screens())
        areas.append(screen->availableGeometry());
    auto primary = QRect{};
    if (auto const* screen = QGuiApplication::primaryScreen()) primary = screen->availableGeometry();
    placement_.setScreens(areas, primary);
}

void PetController::adoptHome(QPoint at)
{
    placement_.setHome(at);
    if (wander_) wander_->setHome(at);
}

void PetController::keepOnScreen()
{
    if (not window_) return;
    // The screen changed under a throw: put the pet down where it is and let the safe-spot rule below choose the new
    // home.
    endThrow(window_->position(), true, ThrowStop::ScreenChanged);
    if (ceremony_.active()) finishCeremonyNow();
    syncScreens();
    if (placement_.empty()) return;

    // A hide survives a change on another screen; if the work area it was hiding on is gone, the pet comes back out
    // into the nearest safe place rather than staying clipped to a screen that no longer exists.
    if (hide_.active()) {
        if (auto const* screen = screenNamed(hide_.screen())) {
            refitHiding(screen->availableGeometry());
            return;
        }
        endHiding();
    }

    cancelStroll();
    auto const at   = window_->position();
    auto const kept = placement_.keep(at, window_->size());
    if (kept != at) window_->setPosition(kept);

    if (wander_) {
        // Keep the reader's home while its screen still holds it; otherwise the safe spot is the new home.
        auto const home  = placement_.home().value_or(kept);
        auto const still = placement_.holds(home, window_->size());
        adoptHome(still ? home : kept);
        wander_->setBounds(window_->size(), areaAt(kept));
    }
    stroll_.wait();
}

bool PetController::canWalkCeremony() const
{
    if (not assets_ or not machine_ or not wander_ or not window_) return false;
    auto const& movement = assets_->animations.movement;
    return movement.has_value() and assets_->animations.playbook.actions.count(movement->walk) != 0;
}

PetCeremony::Stage PetController::ceremonyStage() const
{
    auto stage = PetCeremony::Stage{};
    if (not window_) return stage;
    stage.position = window_->position();
    stage.size     = window_->size();
    stage.area     = areaAt(stage.position);
    stage.home     = placement_.home().value_or(stage.position);
    stage.gait     = assets_ and assets_->animations.movement ? gaitOf(assets_->animations.movement->style) : Gait::Glide;
    stage.canWalk  = canWalkCeremony();
    stage.roles    = ceremonyRoles();
    return stage;
}

PetCeremony::Roles PetController::ceremonyRoles() const
{
    return PetCeremony::Roles{rolePlayback(Role::LookAround), rolePlayback(Role::Encourage)};
}

std::optional<std::chrono::milliseconds> PetController::rolePlayback(Role role) const
{
    if (not assets_) return std::nullopt;
    auto const& playbook = assets_->animations.playbook;
    auto const  named    = playbook.roles.find(role);
    if (named == playbook.roles.end()) return std::nullopt;
    auto const found = playbook.actions.find(named->second);
    if (found == playbook.actions.end()) return std::nullopt;
    return std::chrono::milliseconds{found->second.frames * 1000 / found->second.fps};
}

bool PetController::playCeremonyWalk()
{
    if (not machine_ or not assets_ or not assets_->animations.movement) return false;
    auto const& walk = assets_->animations.movement->walk;
    if (machine_->playAmbient(walk)) {
        LENS_DEBUG("pet ceremony: walking ({})", walk);
        return true;
    }
    LENS_WARN("the pet ceremony cannot play the walk action: {}", walk);
    return false;
}

bool PetController::playCeremonyRole(Role role)
{
    if (not machine_ or not machine_->playRole(role)) return false;
    if (assets_ and assets_->animations.playbook.roles.count(role))
        LENS_DEBUG("pet ceremony: {} plays", assets_->animations.playbook.roles.at(role));
    return true;
}

void PetController::runCeremonyStep(PetCeremony::Step answer)
{
    // A walk or a role the machine refuses is not a ceremony failure: the module carries the phase on without it.
    auto carry = [&answer](PetCeremony::Step next) {
        next.stopWalk = next.stopWalk or answer.stopWalk;
        if (not next.position) next.position = answer.position;
        if (not next.home) next.home = answer.home;
        next.applyPending = next.applyPending or answer.applyPending;
        return next;
    };
    for (;;) {
        if (answer.stopWalk and machine_) {
            machine_->stopAmbient();
            answer.stopWalk = false;
            LENS_DEBUG("pet ceremony: the walk ended");
        }
        if (answer.startWalk and not playCeremonyWalk()) {
            answer = carry(ceremony_.walkFailed());
            continue;
        }
        if (answer.role and not playCeremonyRole(*answer.role)) {
            answer = carry(ceremony_.roleFailed());
            continue;
        }
        break;
    }

    if (answer.position and window_) window_->setPosition(*answer.position);
    if (answer.home) {
        // The window took its new size: the clamp the module made is the reader's spot now, unless it moved.
        if (placement_.home() != answer.home) savePosition(*answer.home);
        if (wander_ and window_) {
            adoptHome(*answer.home);
            wander_->setBounds(window_->size(), areaAt(*answer.home));
        }
    }
    if (answer.applyPending) {
        LENS_DEBUG("pet ceremony: the old pet is off screen; loading {}", pendingPet_);
        applyPendingPet();
    }
    if (answer.settled) settleAtHome();
    if (answer.hidden) hideNow();
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

    syncScreens();
    auto const size = window_->size();
    auto const here = window_->position();
    auto const area = areaAt(here);
    if (not area.isValid()) {
        settleAtHome();
        return;
    }

    wander_->setBounds(size, area);
    if (not placement_.home()) adoptHome(here);
    adoptHome(clampedWithin(*placement_.home(), size, area));

    // Drawn before the ceremony starts, so a fallback to the look keeps one wait for the next stroll.
    stroll_.wait();
    runCeremonyStep(ceremony_.beginEntrance(ceremonyStage(), fromHere));
    setShown(true);
    if (ceremony_.active())
        LENS_DEBUG("pet ceremony: entering from the {} edge", ceremony_.side() == EdgeSide::Left ? "left" : "right");
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

    syncScreens();
    runCeremonyStep(ceremony_.beginGoodbye(ceremonyStage()));
    if (ceremony_.active())
        LENS_DEBUG("pet ceremony: goodbye at the {} edge", ceremony_.side() == EdgeSide::Left ? "left" : "right");
}

void PetController::startSwitch()
{
    if (pendingPet_.empty()) return;
    if (machine_) {
        machine_->stopAmbient();
        machine_->rest();
    }
    if (not window_ or not wander_) {
        applyPendingPet();
        settleAtHome();
        return;
    }

    syncScreens();
    runCeremonyStep(ceremony_.beginSwitch(ceremonyStage()));
    if (ceremony_.active())
        LENS_DEBUG("pet ceremony: switching at the {} edge", ceremony_.side() == EdgeSide::Left ? "left" : "right");
}

void PetController::updateCeremony(std::chrono::milliseconds step)
{
    if (not ceremony_.active()) return;
    if (not window_ or not assets_) {
        hideNow();
        return;
    }
    runCeremonyStep(ceremony_.tick(step, ceremonyStage()));
}

void PetController::settleAtHome()
{
    if (machine_) {
        machine_->stopAmbient();
        machine_->rest();
    }
    ceremony_.stop();
    pendingPet_.clear();
    hideAfterDrag_ = false;
    if (window_ and wander_) {
        auto const home = placement_.home().value_or(window_->position());
        auto const area = areaAt(home);
        if (area.isValid()) {
            auto const at = clampedWithin(home, window_->size(), area);
            window_->setPosition(at);
            adoptHome(at);
            wander_->setBounds(window_->size(), area);
        } else {
            window_->setPosition(home);
        }
    }
    stroll_.wait();
    setShown(true);
    LENS_DEBUG("pet ceremony: settled at home");
}

void PetController::finishCeremonyNow()
{
    if (not ceremony_.active()) return;
    if (ceremony_.leaving()) {
        hideNow();
        return;
    }
    if (not pendingPet_.empty()) applyPendingPet();
    settleAtHome();
}

void PetController::abortCeremony()
{
    if (not ceremony_.active()) return;
    if (machine_) {
        machine_->stopAmbient();
        machine_->rest();
    }
    pendingPet_.clear();
    ceremony_.stop();
    stroll_.wait();
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
    ceremony_.stop();
    pendingPet_.clear();
    hideAfterDrag_ = false;
    stroll_.wait();
    setShown(false);
    LENS_DEBUG("pet ceremony: off screen");
}

void PetController::applyPendingPet()
{
    if (pendingPet_.empty()) return;
    auto const target  = pendingPet_;
    auto const inherit = placement_.home().value_or(window_ ? window_->position() : QPoint{});
    auto const area    = window_ ? areaAt(window_->position()) : QRect{};
    applyPet(target);
    if (not window_ or not ceremony_.active()) return;

    auto const size = window_->size();
    // Switching keeps the window where it is: the spot belongs to the reader, not to the pet that is leaving
    // (ADR 0100). The swap clamps it again once the new window size is in place.
    auto const at       = area.isValid() ? clampedWithin(inherit, size, area) : inherit;
    auto const spotArea = areaAt(at);
    if (wander_) {
        adoptHome(at);
        wander_->setBounds(size, spotArea);
    }

    if (assets_ and area.isValid()) {
        // The QML window takes its new size on the next event turn. Park it against the larger of the two until then,
        // so a wider pet cannot show a sliver while the swap finishes.
        auto const expected = static_cast<int>(std::lround(assets_->canvas * scale()));
        auto const padded   = QSize{expected + 64, expected + 64};
        window_->setPosition(PetWander::offscreenBase(area, padded, at, ceremony_.side()));
    }
}

void PetController::setShown(bool on)
{
    if (shown_ == on) return;
    shown_ = on;
    if (on)
        lastFramePulse_ = std::chrono::steady_clock::now();
    emit shownChanged();
}

QPoint PetController::cursorPos() const
{
    return QCursor::pos();
}

QPoint PetController::initialPosition(QSize size)
{
    // The reader's saved spot, or the corner when that no longer fits.
    auto at = bottomRightOf(size, QGuiApplication::primaryScreen()->availableGeometry());
    if (auto const spot = savedSpot(size)) at = *spot;
    syncScreens();
    adoptHome(at);
    if (wander_) wander_->setBounds(size, areaAt(at));
    return at;
}

std::optional<QPoint> PetController::savedSpot(QSize size) const
{
    auto const saved = store_.position();
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
    endThrow(window_->position(), true, ThrowStop::Resized);
    // A ceremony owns the window's path. A switch resizes the window deliberately while the pet is off screen; the
    // arrival measures its target again once the new size is in place.
    if (ceremony_.active()) return;
    // A hidden window is deliberately off screen; resizing it only means recomputing how far it stays out.
    if (hide_.active()) {
        refitHiding(hide_.area());
        return;
    }
    // A resize ends the step: its path was computed for the old size and home.
    cancelStroll();
    stroll_.wait();
    syncScreens();
    // The window grows right and down from its top-left, so a screen that holds the top-left is the one it must fit.
    auto const at = placement_.fit(window_->position(), window_->size());
    if (at == window_->position()) return;
    window_->setPosition(at);
    // A remembered spot that no longer fits must not come back at the old size on the next start.
    savePosition(at);
}

void PetController::savePosition(QPoint at)
{
    // A hidden window's off-screen spot is not a home; only a drag out of the hide writes one.
    if (hide_.active()) return;
    adoptHome(at);
    if (wander_) wander_->setBounds(window_ ? window_->size() : QSize{}, areaAt(at));

    auto const screens = QGuiApplication::screens();
    auto       areas   = QList<QRect>{};
    for (auto const* screen : screens)
        areas.append(screen->availableGeometry());
    auto primary = QRect{};
    if (auto const* screen = QGuiApplication::primaryScreen()) primary = screen->availableGeometry();
    placement_.setScreens(areas, primary);
    auto const index = placement_.nearest(at);
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
    updateThrow(kTick);
    updateCeremony(kTick);
    updateWander(kTick);
    updateHiding(kTick);
    updateHover(kTick);
    sync(kTick);
}

void PetController::updateThrow(std::chrono::milliseconds step)
{
    if (not window_ or not machine_ or not enabled() or not desktopRunning_) {
        if (throw_.flying()) endThrow(window_ ? window_->position() : QPoint{}, true, ThrowStop::WindowGone);
        throw_.dropRepeats();
        return;
    }

    auto const canPlay = machine_->idle() and not dragging_ and not ceremony_.active() and not hide_.active() and not previewing_;
    auto const answer  = throw_.tick(step, canPlay);
    if (answer.position and window_) window_->setPosition(*answer.position);

    if (answer.landed) {
        // The pickup pose is the throw's; the landing is where the drag ends. The pet stays here: the landing spot
        // becomes its home (ADR 0092).
        handle(PetEvent::DragEnd);
        if (window_ and answer.position) savePosition(*answer.position);

        // A pet with the optional dizzy art shows its stars; a package without it falls back to the poke reaction.
        // Only a dizzy landing can turn into anger, and only after the run has built up.
        auto const dizzy = answer.role and (*answer.role) == Role::Dizzy and machine_->playRole(*answer.role);
        if (not dizzy) machine_->playRole(Role::ClickReact);
        throw_.landingReacted(dizzy);
        auto const at = answer.position.value_or(QPoint{});
        LENS_DEBUG("pet flight: landed at ({},{}) after {} ms", at.x(), at.y(), answer.elapsed.count());
        return;
    }
    if (answer.role and not machine_->playRole(*answer.role)) {
        // The pet has no angry art (or it is not a one-shot): there is nothing to repeat.
        throw_.dropRepeats();
    }
}

void PetController::updateWander(std::chrono::milliseconds step)
{
    if (not machine_ or not assets_ or not window_ or not wander_ or hide_.active() or ceremony_.active() or
        throw_.flying() or throw_.repeatsOwed())
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
    if (not placement_.home()) adoptHome(window_->position());

    // The wait counts idle time only. The pet's own idle fidgets are part of being idle, so they pause it
    // rather than drawing a new one; every outside event has already drawn a fresh wait in handle().
    // A step's own ambient walk is also not idle: that is the stroll running, not an interruption.
    auto const walking = stroll_.walking() and machine_->ambient();
    if (not machine_->idle() and not walking) {
        cancelStroll();
        return;
    }

    // A pointer on the pet borrows the idle time for its hover, so a stroll does not start or continue under it.
    runStrollStep(stroll_.tick(step, PetStroll::Facts{cursorOnPet()}, *wander_, window_->position()));
}

void PetController::runStrollStep(PetStroll::Step answer)
{
    if (answer.stopWalk and machine_ and machine_->ambient()) machine_->stopAmbient();
    if (answer.position and window_) window_->setPosition(*answer.position);

    if (answer.startWalk) {
        auto const started = machine_ and assets_ and assets_->animations.movement and
                             machine_->playAmbient(assets_->animations.movement->walk);
        if (not started) answer = stroll_.walkFailed();
    }
    if (answer.hide) {
        enterHiding(*answer.hide);
        return;
    }
    if (answer.finished) {
        // The walk is over and the pet is back at rest: the after-return context is up, and it looks around once
        // before the next idle turn is drawn (ADR 0093, 0098).
        stage_.returnedHome(std::chrono::steady_clock::now());
        if (machine_ and machine_->idle()) machine_->playRole(Role::LookAround);
    }
}

void PetController::updateHiding(std::chrono::milliseconds step)
{
    if (not hide_.active() or not window_ or not assets_) return;
    auto const answer = hide_.tick(step, machine_->idle(), hover_ and hover_->watching());
    if (answer.position) window_->setPosition(*answer.position);

    if (answer.left) {
        // A click on the sliver called the pet out: the window has slid back to where the hide began.
        settleOutOfHide(*answer.position);
        return;
    }

    // A reaction or the sign ends into idle; the hide keeps going and picks its peek loop back up here.
    if (answer.loopPeek) {
        auto const& movement = assets_->animations.movement;
        if (movement and movement->peek) machine_->playAmbient(*movement->peek);
    }
}

void PetController::cancelStroll()
{
    if (machine_ and machine_->ambient()) machine_->stopAmbient();
    stroll_.stop();
}

void PetController::enterHiding(EdgeSide side)
{
    if (not wander_ or not window_ or not assets_) return;
    auto const& movement = assets_->animations.movement;
    if (not movement or not movement->peek) return;

    machine_->stopAmbient();
    auto const* screen = screenForArea(wander_->area());
    hide_.begin(side,
                window_->position(),
                window_->size(),
                wander_->area(),
                screen ? deviceNameOf(*screen) : QString{},
                PetHide::Ink{static_cast<double>(assets_->canvas),
                             assets_->peekInkLeft,
                             assets_->peekInkRight,
                             assets_->inkTop,
                             assets_->inkBottom});
    machine_->playAmbient(*movement->peek);
}

void PetController::endHiding()
{
    if (not hide_.active()) return;
    hide_.stop();
    if (machine_ and machine_->ambient()) machine_->stopAmbient();
}

void PetController::lureOut()
{
    if (not hide_.active() or not window_) return;
    hide_.lureOut(window_->position(), window_->size());
}

void PetController::settleOutOfHide(QPoint at)
{
    if (not hide_.active()) return;
    auto const area = hide_.area();
    auto const size = window_ ? window_->size() : QSize{};
    hide_.stop();
    if (machine_ and machine_->ambient()) machine_->stopAmbient();
    if (window_) {
        window_->setPosition(at);
        savePosition(at);
    }
    if (wander_ and size.isValid()) {
        adoptHome(at);
        wander_->setBounds(size, area);
    }
    stroll_.wait();
}

void PetController::refitHiding(QRect area)
{
    if (not hide_.active() or not window_) return;
    window_->setPosition(hide_.refit(area, window_->size()));
}

core::pet::PetMood PetController::currentMood() const
{
    // The stage owns the two after-contexts; the wall clock is the adapter's to read and hand in.
    auto const now = QTime::currentTime();
    return stage_.mood(std::chrono::steady_clock::now(), now.hour() * 60 + now.minute());
}

QRect PetController::areaAt(QPoint at) const
{
    return placement_.areaAt(at);
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
    if (not hover_ or not assets_ or not window_ or not desktopRunning_ or previewing_ or ceremony_.active() or
        throw_.flying() or throw_.repeatsOwed() or store_.passthrough() or not window_->isVisible()) {
        resetHover();
        return;
    }

    // A hidden pet keeps its own, smaller share: eyes and a deeper lean toward the sliver, never a reaction.
    if (hide_.active()) {
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
    if (result.notice or result.dwell) stroll_.wait();
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
    // The window still sliding out, a sign that is up, a click's slide back in, or a reaction playing: none of them
    // are the pet peeking on its own terms, so the approach is forgotten and the lean withdraws.
    if (not hide_.peeking() or not machine_->ambient() or not window_->isVisible()) {
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
    hover_->update(step, hide_.distanceToSliver(cursor, origin), false);

    if (hover_->watching())
        setGaze(gazeToward(at, hide_.ink().centre().x(), hide_.ink().centre().y()));
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

void PetController::sync(std::chrono::milliseconds step)
{
    auto const& assets = *assets_;
    auto const  action = shownAction();

    // A real scene starts its entry here. A previewed scene is a still, not an entry, and reduced motion lands directly.
    auto const* currentScene    = (scenesAllowed_ and not preview_) ? scenes_.find(action) : nullptr;
    auto const  impactWasActive = stage_.impact().has_value();
    auto const& enteredScene    = stage_.sceneId();
    if ((not enteredScene.empty() and (not currentScene or currentScene->id != enteredScene)) or
        (not currentScene and stage_.impact())) {
        // The scene changed under an entry, or ended while its particles were still going: both belong to the scene
        // that is gone.
        stage_.stopEntry();
        entryPose_.reset();
        sceneEntryDirty_ = true;
    }

    if (currentScene and currentScene->entry and not motionReduced_ and stage_.sceneId() != currentScene->id)
        beginSceneEntry(*currentScene, action);

    auto const entryVisualDirty = sceneEntryDirty_;
    sceneEntryDirty_            = false;

    if (action != shownAction_) {
        shownAction_   = action;
        actionElapsed_ = std::chrono::milliseconds{0};
    } else {
        actionElapsed_ += step;
    }

    // The landing is decided before the frame is computed, so its first scene frame is frame 0.
    auto const sceneStep         = stage_.tick(step);
    auto const entryJustFinished = sceneStep.landed;
    if (entryJustFinished) {
        entryPose_.reset();
        actionElapsed_ = std::chrono::milliseconds{0};
    }

    if (sceneStep.frame) {
        auto const& frame = *sceneStep.frame;
        publishSceneTransform(frame.shift, frame.scale);
        if (frame.scenePose or not entryPose_) {
            // A fade shows the scene's own first frame while the props come in.
            auto const& scenePictures = assets.sheets.at(action);
            sheets_                   = QVariantMap{{"body", urlOf(scenePictures.body)},
                                                    {"effect", urlOf(scenePictures.effect)},
                                                    {"expression", urlOf(assets.expressionSheet)}};
            frameIndex_               = 0;
            anchors_                  = assets.anchors.at(action).at(0);
            expression_               = scenePictures.expressions.empty() ? 0 : scenePictures.expressions.front();
        } else {
            // A hop or a settle holds the idle body until it lands, on the frame the idle loop had reached.
            auto const& poseAction   = entryPose_->poseAction;
            auto const& posePictures = assets.sheets.at(poseAction);
            sheets_                  = QVariantMap{{"body", urlOf(posePictures.body)},
                                                   {"effect", urlOf(posePictures.effect)},
                                                   {"expression", urlOf(assets.expressionSheet)}};
            frameIndex_              = frame.poseFrame;
            anchors_                 = assets.anchors.at(poseAction).at(static_cast<std::size_t>(frame.poseFrame));
            expression_              = static_cast<std::size_t>(frame.poseFrame) < posePictures.expressions.size()
                                           ? posePictures.expressions[static_cast<std::size_t>(frame.poseFrame)]
                                           : 0;
        }
        action_ = QString::fromStdString(action);
        pulse();
        return;
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
    if (name != action_ or entryJustFinished or entryVisualDirty) {
        sheets_ = QVariantMap{{"body", urlOf(pictures.body)}, {"effect", urlOf(pictures.effect)}, {"expression", urlOf(assets.expressionSheet)}};
    }
    action_     = name;
    frameIndex_ = frame;
    expression_ = expression;
    anchors_    = assets.anchors.at(action).at(static_cast<std::size_t>(frame));

    if (auto const* scene = scenesAllowed_ ? scenes_.find(action) : nullptr) {
        applyScene(*scene, PetStage::frameOf(*scene, frame, foot()));
    } else {
        clearScene();
    }
    if (changed or entryJustFinished or entryVisualDirty or impactWasActive or stage_.impact()) pulse();
}

void PetController::updateClock()
{
    if (machine_ and (desktopRunning_ or previewing_)) {
        clock_.start();
    } else {
        clock_.stop();
    }
}

void PetController::pulse()
{
    lastFramePulse_ = std::chrono::steady_clock::now();
    emit frameChanged();
}

void PetController::checkHealth()
{
    if (not machine_ or not shown_ or not desktopRunning_ or previewing_ or preview_ or dragging_)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (not clock_.isActive()) {
        // The window is still up, so the pet must not be left on a still frame because its clock
        // was stopped by a transition that did not start it again.
        LENS_WARN("pet frame clock stopped while the pet was on screen; restarting it");
        clock_.start();
        lastFramePulse_ = now;
        return;
    }
    if (now - lastFramePulse_ < kStallAfter)
        return;

    // The clock is running but no visible frame has been published for long enough that every
    // shipped action should have moved. Return to idle rather than leave a still pet on screen.
    const auto quiet = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFramePulse_);
    LENS_WARN("pet animation published no frame for {} ms; returning to idle", quiet.count());
    cancelStroll();
    resetHover();
    // One call clears the entry, its impact and the scene it belonged to, so the watchdog cannot leave half of an
    // entry behind.
    stage_.stopEntry();
    entryPose_.reset();
    sceneEntryDirty_ = true;
    actionElapsed_   = std::chrono::milliseconds{0};
    machine_->rest();
    sync(std::chrono::milliseconds{0});
    lastFramePulse_ = std::chrono::steady_clock::now();
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
