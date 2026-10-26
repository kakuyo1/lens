/**
 * @file pet_stage.cpp
 * @brief Implementation of PetStage: the lottery, the moment, the entry timeline and the frame.
 */

#include "app/pet/pet_stage.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lens::app::pet {

using namespace std::chrono_literals;

namespace {

constexpr double kPi{3.14159265358979323846};

/// @return @p value clamped to 0..1, for the entry's progress fractions.
double clampUnit(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double easeOutCubic(double t)
{
    auto const v = clampUnit(t);
    return 1.0 - std::pow(1.0 - v, 3.0);
}

double easeInOutCubic(double t)
{
    auto const v = clampUnit(t);
    return v < 0.5 ? 4.0 * v * v * v : 1.0 - std::pow(-2.0 * v + 2.0, 3.0) / 2.0;
}

} // namespace

PetStage::PetStage(core::pet::RandomSource roll)
    : roll_(std::move(roll))
{}

void PetStage::readingBegan(std::chrono::steady_clock::time_point now)
{
    readingSince_ = now;
}

void PetStage::readingEnded(std::chrono::steady_clock::time_point now)
{
    if (readingSince_ and now - *readingSince_ >= kLongReading) readingAfter_ = now + kReadingAfterglow;
    readingSince_.reset();
}

void PetStage::returnedHome(std::chrono::steady_clock::time_point now)
{
    returnAfter_ = now + kReturnAfterglow;
}

core::pet::PetMood PetStage::mood(std::chrono::steady_clock::time_point now, int minuteOfDay) const
{
    auto mood                = core::pet::PetMood{};
    mood.minuteOfDay         = minuteOfDay;
    mood.justFinishedReading = now < readingAfter_;
    mood.justReturned        = now < returnAfter_;
    return mood;
}

std::optional<core::pet::ActionName> PetStage::draw(std::vector<Offer> const& offers, core::pet::PetMood const& mood)
{
    struct Choice {
        core::pet::ActionName name;
        double                weight;
    };
    auto choices = std::vector<Choice>{};
    for (auto const& offer : offers)
        choices.push_back({offer.name, core::pet::sceneWeight(offer.mood, mood) * offer.favour});

    // A four-scene pool reads as a pool only if the same one does not come straight back; the last scene alone in
    // the pool is still offered, so a draw never leaves the pet with nothing.
    if (choices.size() > 1) std::erase_if(choices, [this](Choice const& choice) { return choice.name == last_; });
    if (choices.empty()) return std::nullopt;

    // A weighted draw: every scene keeps a chance in every stretch, the weights only tilt it (ADR 0093).
    auto total = 0.0;
    for (auto const& choice : choices)
        total += choice.weight;
    auto roll   = roll_() * total;
    auto picked = choices.back();
    for (auto const& choice : choices) {
        if (roll < choice.weight) {
            picked = choice;
            break;
        }
        roll -= choice.weight;
    }
    last_ = picked.name;
    return picked.name;
}

void PetStage::notePlayed(core::pet::ActionName const& name)
{
    last_ = name;
}

void PetStage::beginEntry(Scene const& scene, Pose pose, double inkTop)
{
    if (not scene.entry) return;
    entry_     = EntryState{&scene, pose, inkTop, 0ms};
    sceneId_   = scene.id;
    justBegan_ = true;
    impact_.reset();
    frame_ = frameAt();
}

void PetStage::landEntry()
{
    entry_.reset();
    impact_.reset();
    frame_     = EntryFrame{};
    justBegan_ = false;
}

void PetStage::stopEntry()
{
    landEntry();
    sceneId_.clear();
}

void PetStage::reset()
{
    stopEntry();
    last_.clear();
}

std::optional<PetStage::Impact> PetStage::impact() const
{
    if (not impact_) return std::nullopt;
    auto const progress = kImpact.count() > 0 ? clampUnit(static_cast<double>(impact_->elapsed.count()) /
                                                          static_cast<double>(kImpact.count()))
                                              : 1.0;
    return Impact{impact_->kind, impact_->at, progress};
}

PetStage::EntryStep PetStage::tick(std::chrono::milliseconds step)
{
    auto answer = EntryStep{};

    // The particles a landing started outlive the entry. The one already running is advanced before a new one can
    // start, so a landing's own tick does not age it.
    if (impact_) {
        impact_->elapsed += step;
        if (impact_->elapsed >= kImpact) impact_.reset();
    }

    if (not entry_) return answer;

    // The tick an entry began on shows its first frame rather than moving it on.
    if (justBegan_)
        justBegan_ = false;
    else
        entry_->elapsed += step;

    auto const& scene    = *entry_->scene;
    auto const& entry    = *scene.entry;
    auto const  switchAt = entry.style == SceneEntryStyle::Fade ? entry.propMs : entry.prepMs + entry.moveMs;
    if (entry_->elapsed.count() >= switchAt) {
        // The landing: the caller shows the scene's own first frame, and the impact starts here.
        if (entry.impact) impact_ = ImpactState{entry.impact->kind, entry.impact->at, 0ms};
        entry_.reset();
        frame_        = EntryFrame{};
        answer.landed = true;
        return answer;
    }

    frame_       = frameAt();
    answer.frame = frame_;
    return answer;
}

PetStage::EntryFrame PetStage::frameAt() const
{
    auto const& scene   = *entry_->scene;
    auto const& entry   = *scene.entry;
    auto const  target  = scene.perFrame.empty() ? SceneFrame{1.0, 0.0, 0.0} : scene.perFrame.front();
    auto const  elapsed = entry_->elapsed.count();

    auto       answer = EntryFrame{};
    auto const prop   = entry.propMs > 0 ? easeOutCubic(static_cast<double>(elapsed) / entry.propMs) : 1.0;
    answer.propAlpha  = prop;
    answer.propOffset = (1.0 - prop) * entry.propRise;

    if (entry.style == SceneEntryStyle::Fade) {
        // The scene pose is already where it belongs; only the props and their rise move.
        answer.shift     = QPointF{target.dx, target.dy};
        answer.scale     = target.scale;
        answer.inFront   = false;
        answer.scenePose = true;
        return answer;
    }

    auto const progress = entry.moveMs > 0 ? clampUnit(static_cast<double>(elapsed - entry.prepMs) / entry.moveMs) : 1.0;
    auto const eased    = entry.style == SceneEntryStyle::Hop ? easeInOutCubic(progress) : easeOutCubic(progress);
    auto       x        = target.dx * eased;
    auto       y        = target.dy * eased;
    if (entry.style == SceneEntryStyle::Hop) {
        // A tall pet has little headroom in the canvas. The idle bob can lift the body another 3 px, so cap the arc
        // against the measured ink top rather than clipping ears at the top of the window.
        auto const headroom = std::max(0.0, entry_->inkTop + target.dy * 0.5 - 4.0);
        y -= std::min(entry.arc, headroom) * std::sin(kPi * progress);
    }
    answer.shift     = QPointF{x, y};
    answer.scale     = 1.0 + (target.scale - 1.0) * eased;
    answer.scenePose = false;
    answer.poseFrame = poseFrameAt(entry_->elapsed);

    if (entry.style == SceneEntryStyle::Hop) {
        answer.inFront = progress < entry.cross;
        return answer;
    }

    // A settle wears the scene pose in front and folds the idle body out over the last stretch of the slide.
    answer.inFront      = true;
    auto const switchAt = entry.prepMs + entry.moveMs;
    auto const blend    = kSceneCrossfade.count() > 0
                              ? clampUnit(static_cast<double>(elapsed - (switchAt - kSceneCrossfade.count())) /
                                       static_cast<double>(kSceneCrossfade.count()))
                              : 1.0;
    answer.sceneAlpha   = easeInOutCubic(blend);
    answer.idleAlpha    = 1.0 - answer.sceneAlpha;
    return answer;
}

int PetStage::poseFrameAt(std::chrono::milliseconds elapsed) const
{
    auto const& pose = entry_->pose;
    if (pose.frames <= 0) return 0;
    auto const frame = static_cast<int>(pose.frame + elapsed.count() * pose.fps / 1000);
    return pose.loop ? frame % pose.frames : std::min(frame, pose.frames - 1);
}

PetStage::Frame PetStage::frameOf(Scene const& scene, int frame, QPointF feet)
{
    // The props were drawn for the catalogue's feet; the pet's own feet are measured when it loads.
    auto answer  = Frame{};
    answer.align = QPointF{feet.x() - scene.foot.x(), feet.y() - scene.foot.y()};
    if (scene.perFrame.empty()) return answer;

    auto const  index = std::clamp(frame, 0, static_cast<int>(scene.perFrame.size()) - 1);
    auto const& one   = scene.perFrame[static_cast<std::size_t>(index)];
    answer.index      = index;
    answer.shift      = QPointF{one.dx, one.dy};
    answer.scale      = one.scale;
    return answer;
}

} // namespace lens::app::pet
