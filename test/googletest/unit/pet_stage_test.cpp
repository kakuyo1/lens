/**
 * @file pet_stage_test.cpp
 * @brief The scene stage, offline: the idle lottery, the two after-contexts, the entry timeline and the frame
 *        (ADR 0049, 0093, 0101, 0102).
 */

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <QPointF>
#include <QString>

#include "app/pet/pet_scenes.h"
#include "app/pet/pet_stage.h"
#include "core/domain/pet/pet_mood.h"

namespace {

using lens::app::pet::PetStage;
using lens::app::pet::Scene;
using lens::app::pet::SceneEntry;
using lens::app::pet::SceneEntryStyle;
using lens::app::pet::SceneFrame;
using lens::app::pet::SceneImpact;
using lens::core::pet::DayPeriod;
using lens::core::pet::SceneMood;
using namespace std::chrono_literals;

/// @brief A stage whose lottery always draws @p value, so a test reads the weights rather than the dice.
PetStage stageRolling(double value)
{
    return PetStage{[value] { return value; }};
}

std::chrono::steady_clock::time_point at(std::chrono::seconds since)
{
    return std::chrono::steady_clock::time_point{since};
}

/// @return An offer whose weight for the afternoon is @p weight; the tests draw at 12:00, which is the afternoon.
PetStage::Offer offerOf(std::string name, double weight, double favour = 1.0)
{
    auto mood      = SceneMood{};
    mood.afternoon = weight;
    return PetStage::Offer{std::move(name), mood, favour};
}

SceneEntry entryOf(SceneEntryStyle style, int prepMs, int moveMs, int propMs)
{
    auto entry     = SceneEntry{};
    entry.style    = style;
    entry.prepMs   = prepMs;
    entry.moveMs   = moveMs;
    entry.propMs   = propMs;
    entry.propRise = 4.0;
    entry.arc      = 12.0;
    entry.cross    = 0.5;
    return entry;
}

/// @brief The shipped shape of a hop: a beat of preparation, then 400 ms of flight.
SceneEntry hopEntry()
{
    return entryOf(SceneEntryStyle::Hop, 100, 400, 200);
}

Scene makeScene(std::string id, std::optional<SceneEntry> entry = std::nullopt)
{
    auto scene       = Scene{};
    scene.id         = std::move(id);
    scene.name       = QString::fromStdString(scene.id);
    scene.frames     = 3;
    scene.fps        = 10;
    scene.foot       = QPointF{50.0, 80.0};
    scene.backLayer  = 0;
    scene.frontLayer = 20;
    scene.backSheet  = QStringLiteral("back.png");
    scene.frontSheet = QStringLiteral("front.png");
    scene.perFrame   = {SceneFrame{1.0, 0.0, 0.0}, SceneFrame{1.2, -6.0, -2.0}, SceneFrame{1.1, -4.0, -1.0}};
    scene.entry      = std::move(entry);
    return scene;
}

/// @brief The idle loop a hop holds: eight frames at 10 fps, already on its third when the hop began.
PetStage::Pose idlePose()
{
    return PetStage::Pose{8, 10, true, 2};
}

// ---------------------------------------------------------------- the lottery

TEST(PetStage, TheLotteryWeightsTheScenesTheMomentAndTheOutfitGive)
{
    auto const mood   = stageRolling(0.0).mood(at(0s), 12 * 60);
    auto const offers = std::vector<PetStage::Offer>{offerOf("swing", 1.0), offerOf("nap", 3.0)};

    // The pool weighs 4, so a roll at 0.75 lands at 3: past the first scene's 1 and inside the second's 3.
    auto heavy = stageRolling(0.75);
    EXPECT_EQ(heavy.draw(offers, mood), std::optional<std::string>{"nap"});

    auto light = stageRolling(0.2);
    EXPECT_EQ(light.draw(offers, mood), std::optional<std::string>{"swing"});
}

TEST(PetStage, AWornAccessoryTiltsTheLotteryTowardItsScene)
{
    auto const mood = stageRolling(0.0).mood(at(0s), 12 * 60);
    // Two equal scenes weigh 2: a roll at 0.3 stays inside the first one.
    auto plain = stageRolling(0.3);
    EXPECT_EQ(plain.draw({offerOf("swing", 1.0), offerOf("nap", 1.0)}, mood), std::optional<std::string>{"swing"});

    // The same roll on a pool the accessory has tilted to 5 lands at 1.5, inside the second scene's 4.
    auto tilted = stageRolling(0.3);
    EXPECT_EQ(tilted.draw({offerOf("swing", 1.0), offerOf("nap", 1.0, 4.0)}, mood), std::optional<std::string>{"nap"});
}

TEST(PetStage, TheLotteryNeverBringsTheSameSceneStraightBack)
{
    auto       stage = stageRolling(0.0);
    auto const mood  = stage.mood(at(0s), 12 * 60);
    auto const pool  = std::vector<PetStage::Offer>{offerOf("swing", 1.0), offerOf("nap", 1.0)};

    auto const first  = stage.draw(pool, mood);
    auto const second = stage.draw(pool, mood);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_NE(*first, *second) << "the same scene came straight back";
    EXPECT_EQ(stage.lastPlayed(), *second);
}

TEST(PetStage, ALoneSceneStillComesBack)
{
    auto       stage = stageRolling(0.5);
    auto const mood  = stage.mood(at(0s), 12 * 60);
    auto const pool  = std::vector<PetStage::Offer>{offerOf("swing", 1.0)};

    EXPECT_EQ(stage.draw(pool, mood), std::optional<std::string>{"swing"});
    EXPECT_EQ(stage.draw(pool, mood), std::optional<std::string>{"swing"}) << "the memory held back the only scene";
}

TEST(PetStage, NoOfferDrawsNothing)
{
    auto stage = stageRolling(0.5);
    EXPECT_FALSE(stage.draw({}, stage.mood(at(0s), 12 * 60)));
    EXPECT_TRUE(stage.lastPlayed().empty());
}

TEST(PetStage, ASceneTheSettingsStartedIsNotDrawnBack)
{
    auto       stage = stageRolling(0.0);
    auto const mood  = stage.mood(at(0s), 12 * 60);
    auto const pool  = std::vector<PetStage::Offer>{offerOf("swing", 1.0), offerOf("nap", 1.0)};

    stage.notePlayed("swing");
    EXPECT_EQ(stage.draw(pool, mood), std::optional<std::string>{"nap"});
}

// ---------------------------------------------------------------- the moment

TEST(PetStage, ALongReadingEarnsTheAfterReadingContext)
{
    auto       stage = stageRolling(0.5);
    auto const began = at(1000s);
    auto const ended = began + PetStage::kLongReading;

    stage.readingBegan(began);
    stage.readingEnded(ended);
    EXPECT_TRUE(stage.mood(ended, 600).justFinishedReading);
    EXPECT_TRUE(stage.mood(ended + PetStage::kReadingAfterglow - 1s, 600).justFinishedReading);
    EXPECT_FALSE(stage.mood(ended + PetStage::kReadingAfterglow, 600).justFinishedReading) << "the context outlasted its afterglow";
}

TEST(PetStage, AShortReadingEarnsNothing)
{
    auto       stage = stageRolling(0.5);
    auto const began = at(1000s);
    auto const ended = began + PetStage::kLongReading - 1s;

    stage.readingBegan(began);
    stage.readingEnded(ended);
    EXPECT_FALSE(stage.mood(ended, 600).justFinishedReading) << "a glance earned a session's context";
}

TEST(PetStage, AReadingThatNeverBeganEndsQuietly)
{
    auto stage = stageRolling(0.5);
    stage.readingEnded(at(1000s));
    EXPECT_FALSE(stage.mood(at(1000s), 600).justFinishedReading);
}

TEST(PetStage, AStrollEarnsTheAfterReturnContext)
{
    auto       stage = stageRolling(0.5);
    auto const home  = at(2000s);

    stage.returnedHome(home);
    EXPECT_TRUE(stage.mood(home + PetStage::kReturnAfterglow - 1s, 600).justReturned);
    EXPECT_FALSE(stage.mood(home + PetStage::kReturnAfterglow, 600).justReturned) << "the context outlasted its afterglow";
}

TEST(PetStage, TheMomentCarriesTheMinuteTheCallerRead)
{
    auto       stage = stageRolling(0.5);
    auto const mood  = stage.mood(at(0s), 13 * 60 + 30);

    EXPECT_EQ(mood.minuteOfDay, 13 * 60 + 30);
    EXPECT_EQ(lens::core::pet::dayPeriodOf(mood.minuteOfDay), DayPeriod::Afternoon);
    EXPECT_FALSE(mood.justFinishedReading);
    EXPECT_FALSE(mood.justReturned);
}

// ---------------------------------------------------------------- the entry

TEST(PetStage, AHopHoldsTheIdlePoseUntilItLands)
{
    auto const scene = makeScene("swing", hopEntry());
    auto       stage = stageRolling(0.5);
    stage.beginEntry(scene, idlePose(), 40.0);

    auto const first = stage.tick(20ms);
    ASSERT_TRUE(first.frame);
    EXPECT_FALSE(first.frame->scenePose) << "the hop wore the scene pose before it landed";
    EXPECT_EQ(first.frame->poseFrame, 2) << "the hop restarted the idle bob";
    EXPECT_DOUBLE_EQ(first.frame->propAlpha, 0.0);
    EXPECT_TRUE(first.frame->inFront);
    EXPECT_FALSE(first.landed);

    auto const moving = stage.tick(100ms);
    ASSERT_TRUE(moving.frame);
    EXPECT_EQ(moving.frame->poseFrame, 3) << "the held pose did not advance";
    EXPECT_NEAR(moving.frame->propAlpha, 0.875, 1e-9);
}

TEST(PetStage, AHopLandsWhenItsPreparationAndFlightAreSpent)
{
    auto const scene = makeScene("swing", hopEntry());
    auto       stage = stageRolling(0.5);
    stage.beginEntry(scene, idlePose(), 40.0);
    stage.tick(0ms);

    auto const almost = stage.tick(499ms);
    ASSERT_TRUE(almost.frame);
    EXPECT_FALSE(almost.landed);

    auto const landing = stage.tick(1ms);
    EXPECT_TRUE(landing.landed);
    EXPECT_FALSE(landing.frame);
    EXPECT_FALSE(stage.entering());
    EXPECT_EQ(stage.sceneId(), std::string{"swing"}) << "the scene was forgotten at its own landing";
    EXPECT_DOUBLE_EQ(stage.frame().propAlpha, 1.0) << "the props did not stay in after the landing";
}

TEST(PetStage, AHopsArcIsCappedByThePetsHeadroom)
{
    auto const scene = makeScene("swing", hopEntry());

    // A pet whose ink starts 8 canvas pixels down has 4 px of headroom above it.
    auto low = stageRolling(0.5);
    low.beginEntry(scene, idlePose(), 8.0);
    low.tick(0ms);
    auto const capped = low.tick(300ms);
    ASSERT_TRUE(capped.frame);
    EXPECT_DOUBLE_EQ(capped.frame->shift.y(), -4.0) << "the arc lifted the pet past the window's edge";
    EXPECT_FALSE(capped.frame->inFront) << "the pet stayed in front of the props past its crossing point";

    // A pet with room to spare flies the arc the data asks for.
    auto tall = stageRolling(0.5);
    tall.beginEntry(scene, idlePose(), 60.0);
    tall.tick(0ms);
    auto const free = tall.tick(300ms);
    ASSERT_TRUE(free.frame);
    EXPECT_DOUBLE_EQ(free.frame->shift.y(), -12.0) << "the arc did not reach its full height";
}

TEST(PetStage, AFadeShowsTheScenePoseWhileThePropsRise)
{
    auto const scene = makeScene("swing", entryOf(SceneEntryStyle::Fade, 100, 400, 200));
    auto       stage = stageRolling(0.5);
    stage.beginEntry(scene, idlePose(), 40.0);

    auto const first = stage.tick(0ms);
    ASSERT_TRUE(first.frame);
    EXPECT_TRUE(first.frame->scenePose) << "a fade held the idle pose";
    EXPECT_DOUBLE_EQ(first.frame->propAlpha, 0.0);
    EXPECT_DOUBLE_EQ(first.frame->propOffset, 4.0);
    EXPECT_FALSE(first.frame->inFront);

    auto const half = stage.tick(100ms);
    ASSERT_TRUE(half.frame);
    EXPECT_NEAR(half.frame->propAlpha, 0.875, 1e-9);
    EXPECT_NEAR(half.frame->propOffset, 0.5, 1e-9);

    EXPECT_TRUE(stage.tick(100ms).landed) << "a fade landed later than its props";
}

TEST(PetStage, ASettleFoldsTheScenePoseInOverItsLastStretch)
{
    auto const scene = makeScene("swing", entryOf(SceneEntryStyle::Settle, 100, 400, 200));
    auto       stage = stageRolling(0.5);
    stage.beginEntry(scene, idlePose(), 40.0);
    stage.tick(0ms);

    // The slide is 500 ms long, so the 180 ms fold starts at 320.
    auto const before = stage.tick(319ms);
    ASSERT_TRUE(before.frame);
    EXPECT_DOUBLE_EQ(before.frame->sceneAlpha, 0.0) << "the fold started early";
    EXPECT_DOUBLE_EQ(before.frame->idleAlpha, 1.0);
    EXPECT_TRUE(before.frame->inFront);
    EXPECT_FALSE(before.frame->scenePose);

    auto const half = stage.tick(91ms);
    ASSERT_TRUE(half.frame);
    EXPECT_NEAR(half.frame->sceneAlpha, 0.5, 1e-9);
    EXPECT_NEAR(half.frame->idleAlpha, 0.5, 1e-9);
}

TEST(PetStage, ASceneWithoutAnEntryNeverBegins)
{
    auto const scene = makeScene("swing");
    auto       stage = stageRolling(0.5);
    stage.beginEntry(scene, idlePose(), 40.0);

    EXPECT_FALSE(stage.entering());
    EXPECT_TRUE(stage.sceneId().empty());
    EXPECT_FALSE(stage.tick(20ms).frame);
}

TEST(PetStage, ALandingCanLeaveParticlesBehind)
{
    auto entry       = hopEntry();
    entry.impact     = SceneImpact{QStringLiteral("dust"), QPointF{4.0, 2.0}};
    auto const scene = makeScene("dog_burying", entry);
    auto       stage = stageRolling(0.5);
    stage.beginEntry(scene, idlePose(), 40.0);
    stage.tick(0ms);

    EXPECT_TRUE(stage.tick(500ms).landed);
    auto const started = stage.impact();
    ASSERT_TRUE(started);
    EXPECT_EQ(started->kind, QStringLiteral("dust"));
    EXPECT_EQ(started->at, QPointF(4.0, 2.0));
    EXPECT_DOUBLE_EQ(started->progress, 0.0);

    stage.tick(160ms);
    auto const half = stage.impact();
    ASSERT_TRUE(half);
    EXPECT_NEAR(half->progress, 0.5, 1e-9);

    stage.tick(160ms);
    EXPECT_FALSE(stage.impact()) << "the particles outlived their 320 ms";
}

TEST(PetStage, LandingAnEntryKeepsTheSceneEntered)
{
    auto const scene = makeScene("swing", hopEntry());
    auto       stage = stageRolling(0.5);
    stage.beginEntry(scene, idlePose(), 40.0);

    stage.landEntry();
    EXPECT_FALSE(stage.entering());
    EXPECT_EQ(stage.sceneId(), std::string{"swing"}) << "the scene would replay its entry";
    EXPECT_FALSE(stage.impact());
    EXPECT_DOUBLE_EQ(stage.frame().propAlpha, 1.0);
    EXPECT_FALSE(stage.frame().inFront);
}

TEST(PetStage, StoppingTheEntryForgetsTheSceneAndItsParticles)
{
    auto entry       = hopEntry();
    entry.impact     = SceneImpact{QStringLiteral("dust"), QPointF{4.0, 2.0}};
    auto const scene = makeScene("dog_burying", entry);
    auto       stage = stageRolling(0.5);
    stage.beginEntry(scene, idlePose(), 40.0);
    stage.tick(0ms);
    stage.tick(500ms);
    ASSERT_TRUE(stage.impact());

    stage.stopEntry();
    EXPECT_FALSE(stage.impact());
    EXPECT_TRUE(stage.sceneId().empty());
    EXPECT_FALSE(stage.entering());
    EXPECT_DOUBLE_EQ(stage.frame().propAlpha, 1.0);
}

TEST(PetStage, ResettingForgetsThePetAndKeepsTheMoment)
{
    auto       stage = stageRolling(0.0);
    auto const pool  = std::vector<PetStage::Offer>{offerOf("swing", 1.0), offerOf("nap", 1.0)};
    stage.returnedHome(at(100s));
    ASSERT_EQ(stage.draw(pool, stage.mood(at(100s), 600)), std::optional<std::string>{"swing"});

    stage.reset();
    EXPECT_TRUE(stage.lastPlayed().empty());
    EXPECT_EQ(stage.draw(pool, stage.mood(at(100s), 600)), std::optional<std::string>{"swing"})
        << "the lottery remembered the last pet's scene";
    EXPECT_TRUE(stage.mood(at(100s), 600).justReturned) << "the reader's after-context was cleared with the pet";
}

// ---------------------------------------------------------------- the frame

TEST(PetStage, AFrameCarriesTheScenesShiftScaleAndThePetsAlignment)
{
    auto const scene = makeScene("swing", hopEntry());
    // The pet's feet sit a pixel and a half left of the catalogue's and a cell lower.
    auto const one = PetStage::frameOf(scene, 1, QPointF{48.5, 83.0});

    EXPECT_EQ(one.index, 1);
    EXPECT_EQ(one.shift, QPointF(-6.0, -2.0));
    EXPECT_DOUBLE_EQ(one.scale, 1.2);
    EXPECT_EQ(one.align, QPointF(-1.5, 3.0));
}

TEST(PetStage, AFramePastTheDataReadsTheLastOne)
{
    auto const scene = makeScene("swing", hopEntry());
    auto const one   = PetStage::frameOf(scene, 9, QPointF{50.0, 80.0});

    EXPECT_EQ(one.index, 2) << "the frame read past the scene's data";
    EXPECT_EQ(one.shift, QPointF(-4.0, -1.0));
}

TEST(PetStage, ASceneWithoutFramesStaysNeutral)
{
    auto scene     = makeScene("swing", hopEntry());
    scene.perFrame = {};
    auto const one = PetStage::frameOf(scene, 0, QPointF{48.5, 83.0});

    EXPECT_EQ(one.index, 0);
    EXPECT_EQ(one.shift, QPointF(0.0, 0.0));
    EXPECT_DOUBLE_EQ(one.scale, 1.0);
    EXPECT_EQ(one.align, QPointF(-1.5, 3.0));
}

} // namespace
