/**
 * @file pet_taskbar_test.cpp
 * @brief The taskbar trip's geometry, offline: which strip counts, the projectile leap, the bounce, the steady
 *        round walk and the hopped climb back to the start.
 */

#include <algorithm>
#include <chrono>
#include <iterator>
#include <vector>

#include <gtest/gtest.h>

#include <QPoint>
#include <QRect>
#include <QSize>

#include "app/pet/pet_taskbar.h"

namespace {

using lens::app::pet::PetTaskbar;
using namespace std::chrono_literals;

/// One tick the controller would apply: where the window goes and what it must start or stop.
struct Playback {
    QPoint position;
    bool   startWalk{false};
    bool   stopWalk{false};
    bool   finished{false};
};

/// @return Every tick's answer until the trip ends or `limit` passes.
std::vector<Playback> drive(PetTaskbar& trip, std::chrono::milliseconds limit = 30s, std::chrono::milliseconds tick = 16ms)
{
    auto answers = std::vector<Playback>{};
    for (auto elapsed = 0ms; elapsed < limit and trip.active(); elapsed += tick) {
        auto const step = trip.tick(tick);
        answers.push_back(Playback{step.position, step.startWalk, step.stopWalk, step.finished});
    }
    return answers;
}

/// @return The first answer that asks for the walk loop, or the end.
std::vector<Playback>::const_iterator firstWalk(std::vector<Playback> const& answers)
{
    return std::find_if(answers.begin(), answers.end(), [](Playback const& answer) { return answer.startWalk; });
}

TEST(PetTaskbar, OnlyAFixedBottomTaskbarCounts)
{
    QRect const screen{0, 0, 1920, 1080};
    QRect const work{0, 0, 1920, 1032};

    EXPECT_EQ(PetTaskbar::stripOf(screen, work, false), (QRect{0, 1032, 1920, 48}));
    EXPECT_TRUE(PetTaskbar::stripOf(screen, work, true).isEmpty()) << "an auto-hidden taskbar is not a shelf";
    EXPECT_TRUE(PetTaskbar::stripOf(screen, QRect{48, 0, 1872, 1080}, false).isEmpty()) << "a side taskbar is a wall";
    EXPECT_TRUE(PetTaskbar::stripOf(screen, QRect{0, 0, 1920, 1080}, false).isEmpty()) << "no bar, no trip";
    EXPECT_TRUE(PetTaskbar::stripOf(screen, QRect{0, 0, 1904, 1032}, false).isEmpty()) << "a second bar is not the taskbar";
}

TEST(PetTaskbar, TheTripNeedsRoomForTheLeapAndAWalk)
{
    QRect const area{0, 0, 1000, 700};
    QSize const pet{100, 100};

    EXPECT_TRUE(PetTaskbar::hasRoom(QPoint{500, 300}, pet, area));
    EXPECT_TRUE(PetTaskbar::hasRoom(QPoint{0, 300}, pet, area));
    EXPECT_FALSE(PetTaskbar::hasRoom(QPoint{450, 300}, QSize{1000, 100}, area)) << "a window as wide as the area has nowhere to go";
    EXPECT_FALSE(PetTaskbar::hasRoom(QPoint{500, 300}, QSize{100, 800}, area)) << "a window taller than the area does not fit the shelf";
    EXPECT_FALSE(PetTaskbar::hasRoom(QPoint{500, -5}, pet, area)) << "outside the work area is not a start";
}

TEST(PetTaskbar, BeginRefusesAFullWidthWindow)
{
    PetTaskbar trip;
    EXPECT_FALSE(trip.begin(QPoint{0, 300}, QSize{1000, 100}, QRect{0, 0, 1000, 700}));
    EXPECT_FALSE(trip.active());
}

TEST(PetTaskbar, TheLeapArcsTowardTheRoomierSideAndBouncesOnTheTaskbarTop)
{
    QRect const area{0, 0, 1920, 1040};
    QSize const pet{100, 100};
    auto const  floor = area.bottom() - pet.height() + 1;

    PetTaskbar trip;
    ASSERT_TRUE(trip.begin(QPoint{500, 100}, pet, area));
    auto const answers = drive(trip);
    auto const walking = firstWalk(answers);
    ASSERT_NE(walking, answers.end());

    // The room is to the right, so the leap travels right, arches up first, and lands on the taskbar's top edge.
    auto const landing = std::find_if(answers.begin(), walking, [floor](Playback const& answer) { return answer.position.y() == floor; });
    ASSERT_NE(landing, walking);
    EXPECT_GT(landing->position.x(), 500) << "the leap goes toward the roomier side";
    auto lowest = 100;
    for (auto it = answers.begin(); it != landing; ++it) {
        lowest = std::min(lowest, it->position.y());
        EXPECT_GE(it->position.x(), 500) << "the leap never doubles back";
    }
    EXPECT_LT(lowest, 100) << "the small arc rises before the drop";

    // It leaves the taskbar once more before the walk starts: the bounce, then a pause.
    auto const bounce  = std::find_if(landing, walking, [floor](Playback const& answer) { return answer.position.y() < floor; });
    auto const settled = std::find_if(bounce, walking, [floor](Playback const& answer) { return answer.position.y() == floor; });
    ASSERT_NE(bounce, walking) << "the landing bounces";
    ASSERT_NE(settled, walking) << "the bounce comes back down before the walk";
    EXPECT_EQ(bounce->position.x(), landing->position.x()) << "the bounce stays at the landing spot";
}

TEST(PetTaskbar, TheWalkGoesAlmostToTheEndAndComesBackToTheLanding)
{
    QRect const area{0, 0, 1000, 700};
    QSize const pet{100, 100};
    auto const  floor = area.bottom() - pet.height() + 1;

    PetTaskbar trip;
    ASSERT_TRUE(trip.begin(QPoint{500, 300}, pet, area));
    EXPECT_EQ(trip.origin(), QPoint(500, 300));

    auto const answers = drive(trip);
    ASSERT_FALSE(answers.empty());
    EXPECT_TRUE(answers.back().finished);
    EXPECT_EQ(answers.back().position, QPoint(500, 300)) << "the hopped climb ends back at the origin";
    EXPECT_FALSE(trip.active());

    auto const first  = std::find_if(answers.begin(), answers.end(), [](Playback const& answer) { return answer.startWalk; });
    auto const turn   = std::find_if(first, answers.end(), [](Playback const& answer) { return answer.stopWalk; });
    auto const back   = std::find_if(turn, answers.end(), [](Playback const& answer) { return answer.startWalk; });
    auto const finish = std::find_if(back, answers.end(), [](Playback const& answer) { return answer.stopWalk; });
    ASSERT_NE(first, answers.end());
    ASSERT_NE(turn, answers.end());
    ASSERT_NE(back, answers.end());
    ASSERT_NE(finish, answers.end());

    EXPECT_LT(first->position.x(), 500) << "the leap lands left of the start, on the roomier side";
    EXPECT_LE(turn->position.x(), 10) << "the pet walks almost to the area's left end";
    EXPECT_EQ(back->position.x(), turn->position.x()) << "the turn happens where the first leg ended";
    EXPECT_EQ(finish->position.x(), first->position.x()) << "the second leg ends at the bounce landing, not the start";
    EXPECT_EQ(turn->position.y(), floor);
    EXPECT_EQ(finish->position.y(), floor);

    // Both legs are steady and unhurried, and the hopped climb back takes seconds too.
    auto const legTicks   = std::distance(first, turn);
    auto const climbTicks = std::distance(finish, answers.end() - 1);
    EXPECT_GE(legTicks * 16, 4000) << "the walk leg is slow";
    EXPECT_GE(climbTicks * 16, 2000) << "the climb is not a snap";
}

TEST(PetTaskbar, TheWalkIsUniformOnTheTaskbarLine)
{
    QRect const area{0, 0, 1000, 700};
    QSize const pet{100, 100};
    auto const  floor = area.bottom() - pet.height() + 1;

    PetTaskbar trip;
    ASSERT_TRUE(trip.begin(QPoint{500, 300}, pet, area));
    auto const answers = drive(trip);

    auto const first = std::find_if(answers.begin(), answers.end(), [](Playback const& answer) { return answer.startWalk; });
    auto const turn  = std::find_if(first, answers.end(), [](Playback const& answer) { return answer.stopWalk; });
    ASSERT_NE(first, answers.end());
    ASSERT_NE(turn, answers.end());

    auto smallest = 1000;
    auto largest  = 0;
    for (auto it = first; it != turn; ++it) {
        EXPECT_EQ(it->position.y(), floor) << "the walk stays on the taskbar line";
        auto const next = std::next(it);
        if (next == turn) break;
        auto const step = std::abs(next->position.x() - it->position.x());
        smallest        = std::min(smallest, step);
        largest         = std::max(largest, step);
    }
    EXPECT_LE(largest - smallest, 2) << "every tick covers the same ground, so the pace is steady";
}

TEST(PetTaskbar, TheClimbHopsBackUpAndPlaysTheWalk)
{
    QRect const area{0, 0, 1000, 700};
    QSize const pet{100, 100};
    auto const  floor = area.bottom() - pet.height() + 1;

    PetTaskbar trip;
    ASSERT_TRUE(trip.begin(QPoint{500, 300}, pet, area));
    auto const answers = drive(trip);

    auto const first  = std::find_if(answers.begin(), answers.end(), [](Playback const& answer) { return answer.startWalk; });
    auto const turn   = std::find_if(first, answers.end(), [](Playback const& answer) { return answer.stopWalk; });
    auto const back   = std::find_if(turn, answers.end(), [](Playback const& answer) { return answer.startWalk; });
    auto const finish = std::find_if(back, answers.end(), [](Playback const& answer) { return answer.stopWalk; });
    ASSERT_NE(first, answers.end());
    ASSERT_NE(turn, answers.end());
    ASSERT_NE(back, answers.end());
    ASSERT_NE(finish, answers.end());

    // The climb starts a walk of its own and then bounces upward: some steps rise, some settle down a little.
    auto const climbWalk = std::find_if(finish + 1, answers.end(), [](Playback const& answer) { return answer.startWalk; });
    ASSERT_NE(climbWalk, answers.end()) << "the climb uses the walk loop";
    auto rises    = false;
    auto settles  = false;
    auto previous = floor;
    for (auto it = climbWalk; it != answers.end(); ++it) {
        rises    = rises or it->position.y() < previous;
        settles  = settles or it->position.y() > previous;
        previous = it->position.y();
    }
    EXPECT_TRUE(rises);
    EXPECT_TRUE(settles) << "a hop overshoots and settles rather than floating evenly";
    EXPECT_EQ(answers.back().position, QPoint(500, 300));
}

TEST(PetTaskbar, TheWindowStaysInsideTheWorkAreaForTheWholeTrip)
{
    QRect const area{0, 0, 1000, 700};
    QSize const pet{100, 100};
    auto const  floor = area.bottom() - pet.height() + 1;

    PetTaskbar trip;
    ASSERT_TRUE(trip.begin(QPoint{500, 300}, pet, area));
    for (auto const& answer : drive(trip)) {
        EXPECT_LE(answer.position.y(), floor) << "the walk never sinks into the taskbar";
        EXPECT_TRUE(area.contains(QRect{answer.position, pet})) << "the window stays inside the work area";
    }
}

TEST(PetTaskbar, ItStillWalksWhenItStartsOnTheTaskbar)
{
    QRect const area{0, 0, 1000, 700};
    QSize const pet{100, 100};
    auto const  floor = area.bottom() - pet.height() + 1;
    auto const  from  = QPoint{500, floor};

    PetTaskbar trip;
    ASSERT_TRUE(trip.begin(from, pet, area));
    auto const answers = drive(trip);
    auto const walking = firstWalk(answers);
    ASSERT_NE(walking, answers.end()) << "a pet already on the taskbar still takes the round walk";
    EXPECT_TRUE(answers.back().finished);
    EXPECT_EQ(answers.back().position, from);
}

TEST(PetTaskbar, StopLeavesTheTripInactive)
{
    PetTaskbar trip;
    ASSERT_TRUE(trip.begin(QPoint{500, 300}, QSize{100, 100}, QRect{0, 0, 1000, 700}));
    trip.stop();
    EXPECT_FALSE(trip.active());
    EXPECT_TRUE(drive(trip).empty());
}

} // namespace
