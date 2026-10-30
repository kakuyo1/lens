/**
 * @file pet_ceremony_test.cpp
 * @brief One ceremony, offline: the entrance from an edge, the goodbye, the switch and their fallbacks
 *        (ADR 0076, 0089).
 */

#include <algorithm>
#include <chrono>
#include <optional>

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QSize>

#include "app/pet/pet_ceremony.h"
#include "app/pet/pet_wander.h"

namespace {

using lens::app::pet::EdgeSide;
using lens::app::pet::Gait;
using lens::app::pet::PetCeremony;
using lens::app::pet::PetWander;
using lens::core::pet::Role;
using namespace std::chrono_literals;

QRect const  kArea{0, 0, 1200, 800};
QSize const  kPet{200, 200};
QPoint const kHome{500, 300};

PetCeremony::Roles roles()
{
    return PetCeremony::Roles{400ms, 600ms};
}

PetCeremony::Stage stageAt(QPoint at, bool canWalk = true, PetCeremony::Roles played = roles())
{
    return PetCeremony::Stage{at, kPet, kArea, kHome, Gait::Glide, canWalk, played};
}

/// @brief The exact spot the module walks out to: the nearest edge, with the window's row kept.
QPoint edgeFrom(QPoint at)
{
    auto const side = PetWander::nearestEdge(kArea, kPet, at);
    auto const high = kArea.bottom() - kPet.height() + 1;
    auto const y    = high < kArea.top() ? kArea.top() : std::clamp(at.y(), kArea.top(), high);
    return QPoint{side == EdgeSide::Left ? kArea.left() : kArea.right() - kPet.width() + 1, y};
}

std::chrono::milliseconds walkTime(QPoint from, QPoint to)
{
    return PetWander::walkDuration({from, to});
}

TEST(PetCeremony, AnEntranceWalksFromTheOffScreenEdgeAndLooksBeforeSettling)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome);
    auto const from     = PetWander::offscreenBase(kArea, kPet, kHome, EdgeSide::Left);

    auto const start = ceremony.beginEntrance(stage, false);
    EXPECT_TRUE(ceremony.active());
    EXPECT_FALSE(ceremony.leaving());
    EXPECT_EQ(ceremony.side(), EdgeSide::Left);
    ASSERT_TRUE(start.position);
    EXPECT_EQ(*start.position, from);
    EXPECT_TRUE(start.startWalk);

    auto const duration = walkTime(from, kHome);
    auto const half     = ceremony.tick(duration / 2, stage);
    ASSERT_TRUE(half.position);
    EXPECT_EQ(*half.position, PetWander::walkAt({from, kHome}, 0.5, Gait::Glide, kPet.width()));
    EXPECT_FALSE(half.stopWalk);

    auto const arrived = ceremony.tick(duration - duration / 2, stage);
    ASSERT_TRUE(arrived.position);
    EXPECT_EQ(*arrived.position, kHome);
    EXPECT_TRUE(arrived.stopWalk);
    ASSERT_TRUE(arrived.role);
    EXPECT_EQ(*arrived.role, Role::LookAround);
    EXPECT_FALSE(arrived.settled);

    auto const done = ceremony.tick(400ms + PetCeremony::kSettlePause, stage);
    EXPECT_TRUE(done.settled);
    EXPECT_FALSE(ceremony.active());
}

TEST(PetCeremony, AnEntranceFromHereAtHomeLooksAndSettlesWithoutAWalk)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome);

    auto const start = ceremony.beginEntrance(stage, true);
    EXPECT_FALSE(start.startWalk);
    ASSERT_TRUE(start.position);
    EXPECT_EQ(*start.position, kHome);
    ASSERT_TRUE(start.role);
    EXPECT_EQ(*start.role, Role::LookAround);

    EXPECT_TRUE(ceremony.tick(400ms + PetCeremony::kSettlePause, stage).settled);
}

TEST(PetCeremony, APetWithoutWalkArtLooksWhereItStands)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome, false);

    auto const start = ceremony.beginEntrance(stage, false);
    EXPECT_FALSE(start.startWalk);
    ASSERT_TRUE(start.position);
    EXPECT_EQ(*start.position, kHome);
    ASSERT_TRUE(start.role);
    EXPECT_EQ(*start.role, Role::LookAround);
}

TEST(PetCeremony, APetWithoutARoleSettlesAtOnce)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome, false, {});

    auto const start = ceremony.beginEntrance(stage, false);
    EXPECT_TRUE(start.settled);
    EXPECT_FALSE(start.role);
    EXPECT_FALSE(ceremony.active());
}

TEST(PetCeremony, AGoodbyeWalksOutWithALookAndAHeartBeforeHiding)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome);
    auto const edge     = edgeFrom(kHome);

    auto const walk = ceremony.beginGoodbye(stage);
    EXPECT_TRUE(walk.startWalk);
    EXPECT_TRUE(ceremony.leaving());

    auto const atEdge = ceremony.tick(walkTime(kHome, edge), stage);
    ASSERT_TRUE(atEdge.position);
    EXPECT_EQ(*atEdge.position, edge);
    EXPECT_TRUE(atEdge.stopWalk);
    ASSERT_TRUE(atEdge.role);
    EXPECT_EQ(*atEdge.role, Role::LookAround);

    auto const heart = ceremony.tick(400ms + PetCeremony::kLookToHeart, stage);
    ASSERT_TRUE(heart.role);
    EXPECT_EQ(*heart.role, Role::Encourage);

    auto const exit = ceremony.tick(600ms + PetCeremony::kByeToExit, stage);
    EXPECT_TRUE(exit.startWalk);
    EXPECT_FALSE(exit.position) << "the exit walk starts where the goodbye already stands";

    auto const gone = ceremony.tick(walkTime(edge, PetWander::offscreenBase(kArea, kPet, edge, EdgeSide::Left)), stage);
    EXPECT_TRUE(gone.hidden);
    EXPECT_FALSE(ceremony.active());
    ASSERT_TRUE(gone.position);
    EXPECT_EQ(*gone.position, PetWander::offscreenBase(kArea, kPet, edge, EdgeSide::Left));
}

TEST(PetCeremony, AGoodbyeAlreadyAtTheEdgeGoesStraightToItsLook)
{
    auto       ceremony = PetCeremony{};
    auto const edge     = edgeFrom(kHome);
    auto const stage    = stageAt(edge);

    auto const start = ceremony.beginGoodbye(stage);
    EXPECT_FALSE(start.startWalk);
    ASSERT_TRUE(start.role);
    EXPECT_EQ(*start.role, Role::LookAround);
}

TEST(PetCeremony, AGoodbyeWithoutWalkArtHidesAtOnce)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome, false);

    auto const start = ceremony.beginGoodbye(stage);
    EXPECT_TRUE(start.hidden);
    EXPECT_FALSE(ceremony.active());
}

TEST(PetCeremony, AGoodbyePastTheEdgeHidesWithoutAWalk)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(QPoint{-800, 300});

    auto const start = ceremony.beginGoodbye(stage);
    EXPECT_TRUE(start.hidden);
    EXPECT_FALSE(start.startWalk);
}

TEST(PetCeremony, ASwitchLoadsThePendingPetAndBringsTheNewOneIn)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome);
    auto const out      = PetWander::offscreenBase(kArea, kPet, kHome, EdgeSide::Left);

    auto const walk = ceremony.beginSwitch(stage);
    EXPECT_TRUE(walk.startWalk);

    auto const gone = ceremony.tick(walkTime(kHome, out), stage);
    EXPECT_TRUE(gone.stopWalk);
    EXPECT_FALSE(gone.applyPending) << "the swap waits one tick before it loads";

    auto const load = ceremony.tick(20ms, stage);
    EXPECT_TRUE(load.applyPending);

    // The swap waits for the window to take its new size, then hands back the clamped home.
    ceremony.tick(20ms, stage);
    ceremony.tick(20ms, stage);
    auto const enter = ceremony.tick(20ms, stage);
    EXPECT_EQ(enter.home, (std::optional<QPoint>{kHome}));
    EXPECT_TRUE(enter.startWalk);
    ASSERT_TRUE(enter.position);
    EXPECT_EQ(*enter.position, out);

    auto const arrived = ceremony.tick(walkTime(out, kHome), stage);
    ASSERT_TRUE(arrived.role);
    EXPECT_EQ(*arrived.role, Role::LookAround);
    EXPECT_TRUE(ceremony.tick(400ms + PetCeremony::kSettlePause, stage).settled);
}

TEST(PetCeremony, ASwitchWithoutWalkArtAppliesThePetAndSettles)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome, false);

    auto const start = ceremony.beginSwitch(stage);
    EXPECT_TRUE(start.applyPending);
    EXPECT_TRUE(start.settled);
    EXPECT_FALSE(ceremony.active());
}

TEST(PetCeremony, ARefusedWalkFallsThroughToTheLook)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome);
    ceremony.beginEntrance(stage, false);

    auto const fallback = ceremony.walkFailed();
    ASSERT_TRUE(fallback.position);
    EXPECT_EQ(*fallback.position, kHome);
    ASSERT_TRUE(fallback.role);
    EXPECT_EQ(*fallback.role, Role::LookAround);
}

TEST(PetCeremony, ARefusedRoleMovesTheGoodbyeOn)
{
    auto       ceremony = PetCeremony{};
    auto const stage    = stageAt(kHome);
    auto const edge     = edgeFrom(kHome);
    ceremony.beginGoodbye(stage);
    ceremony.tick(walkTime(kHome, edge), stage);

    auto const heart = ceremony.roleFailed();
    ASSERT_TRUE(heart.role);
    EXPECT_EQ(*heart.role, Role::Encourage);

    auto const exit = ceremony.roleFailed();
    EXPECT_TRUE(exit.startWalk);
}

TEST(PetCeremony, TheSwapClampsTheReadersHomeIntoTheNewSize)
{
    auto       ceremony = PetCeremony{};
    auto const out      = PetWander::offscreenBase(kArea, kPet, kHome, EdgeSide::Left);
    ceremony.beginSwitch(stageAt(kHome));
    ceremony.tick(walkTime(kHome, out), stageAt(kHome));
    ceremony.tick(20ms, stageAt(kHome));

    // The new pet is larger and the old home no longer fits: the module hands back the clamped spot.
    auto bigger = stageAt(kHome);
    bigger.home = QPoint{1190, 300};
    ceremony.tick(20ms, bigger);
    ceremony.tick(20ms, bigger);
    auto const enter = ceremony.tick(20ms, bigger);
    EXPECT_EQ(enter.home, (std::optional<QPoint>{QPoint{1000, 300}}));
}

TEST(PetCeremony, StopForgetsTheCeremonyWhereItIs)
{
    auto ceremony = PetCeremony{};
    ceremony.beginEntrance(stageAt(kHome), false);
    EXPECT_TRUE(ceremony.active());

    ceremony.stop();
    EXPECT_FALSE(ceremony.active());
    auto const after = ceremony.tick(20ms, stageAt(kHome));
    EXPECT_FALSE(after.position);
    EXPECT_FALSE(after.settled);
    EXPECT_FALSE(after.hidden);
}

} // namespace
