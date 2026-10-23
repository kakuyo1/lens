/**
 * @file pet_flight_test.cpp
 * @brief The thrown pet's geometry, offline: the release threshold, gravity, the walls, the floor slide and the
 *        landing (docs/adr/0090).
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>

#include "app/pet/pet_flight.h"

namespace {

using lens::app::pet::PetFlight;
using namespace std::chrono_literals;

QRect const kArea{0, 0, 1920, 1040};
QSize const kPet{264, 264};

/// @return The floor's top-left y, where the window's bottom sits on the work area's bottom.
int floorTop(QRect area = kArea, QSize size = kPet)
{
    return area.bottom() - size.height() + 1;
}

/// @return Whether the whole window lies inside the work area.
bool inside(QPoint at, QRect area = kArea, QSize size = kPet)
{
    return area.contains(QRect{at, size});
}

TEST(PetFlight, OnlyAFastReleaseIsAThrow)
{
    EXPECT_FALSE(PetFlight::isFling(QPointF{0.0, 0.0}));
    EXPECT_FALSE(PetFlight::isFling(QPointF{300.0, 300.0}));
    EXPECT_FALSE(PetFlight::isFling(QPointF{600.0, 600.0}));
    EXPECT_FALSE(PetFlight::isFling(QPointF{std::numeric_limits<double>::quiet_NaN(), 10.0}));
    EXPECT_TRUE(PetFlight::isFling(QPointF{700.0, 700.0}));
    EXPECT_TRUE(PetFlight::isFling(QPointF{-900.0, 0.0}));
}

TEST(PetFlight, TheReleaseSpeedIsCapped)
{
    PetFlight flight{QPoint{500, 400}, QPointF{9000.0, 0.0}, kPet, kArea};
    flight.advance(10ms);
    EXPECT_LE(std::hypot(flight.velocity().x(), flight.velocity().y()), PetFlight::kMaxSpeed + 1.0);
}

TEST(PetFlight, GravityBringsAThrowDownAndItLands)
{
    PetFlight flight{QPoint{500, 300}, QPointF{700.0, -1500.0}, kPet, kArea};

    auto step = lens::app::pet::FlightStep{};
    for (auto i = 0; i < 400 and not step.landed; ++i)
        step = flight.advance(20ms);

    ASSERT_TRUE(step.landed);
    EXPECT_LE(flight.elapsed(), PetFlight::kMaxDuration);
    EXPECT_EQ(step.position.y(), floorTop());
    EXPECT_TRUE(inside(step.position));
}

TEST(PetFlight, TheWindowNeverLeavesItsWorkArea)
{
    // Throw at the top-right corner hard enough to hit both walls before gravity makes it settle.
    PetFlight flight{QPoint{1400, 200}, QPointF{2600.0, -1800.0}, kPet, kArea};

    auto bounced = false;
    auto step    = lens::app::pet::FlightStep{};
    for (auto i = 0; i < 400 and not step.landed; ++i) {
        step = flight.advance(20ms);
        EXPECT_TRUE(inside(step.position)) << step.position.x() << "," << step.position.y();
        if (flight.velocity().x() < 0.0) bounced = true;
    }

    EXPECT_TRUE(bounced) << "the window never bounced off the right wall";
    EXPECT_TRUE(step.landed);
}

TEST(PetFlight, TheCeilingTurnsTheThrowAround)
{
    PetFlight flight{QPoint{500, 40}, QPointF{0.0, -2000.0}, kPet, kArea};

    // A bounce is found inside a tick, so the sampled position is a few pixels below the ceiling rather than exactly
    // on it: the velocity's direction is what proves the contact.
    auto nearTop = false;
    auto turned  = false;
    auto step    = lens::app::pet::FlightStep{};
    for (auto i = 0; i < 60 and not step.landed; ++i) {
        step = flight.advance(10ms);
        if (step.position.y() <= kArea.top() + 5) nearTop = true;
        if (nearTop and flight.velocity().y() > 0.0) turned = true;
        EXPECT_GE(step.position.y(), kArea.top());
    }

    EXPECT_TRUE(nearTop);
    EXPECT_TRUE(turned);
}

TEST(PetFlight, AHorizontalThrowSlidesAlongTheFloorAndStops)
{
    auto const start = QPoint{400, floorTop()};
    PetFlight  flight{start, QPointF{1500.0, 0.0}, kPet, kArea};

    auto step = lens::app::pet::FlightStep{};
    for (auto i = 0; i < 200 and not step.landed; ++i)
        step = flight.advance(20ms);

    ASSERT_TRUE(step.landed);
    EXPECT_GT(step.position.x(), start.x()) << "the slide did not travel";
    EXPECT_EQ(step.position.y(), floorTop());
    EXPECT_LT(flight.elapsed(), PetFlight::kMaxDuration);
}

TEST(PetFlight, AThrowStaysInsideTheScreenItStartedOn)
{
    // A monitor to the right of the primary, with a taskbar and a short top edge: the geometry must follow the
    // rectangle it was given rather than the desktop's origin.
    QRect const area{1920, 60, 1600, 900};
    PetFlight   flight{QPoint{2500, 400}, QPointF{-2600.0, -1200.0}, kPet, area};

    auto step = lens::app::pet::FlightStep{};
    for (auto i = 0; i < 400 and not step.landed; ++i) {
        step = flight.advance(20ms);
        EXPECT_TRUE(inside(step.position, area)) << step.position.x() << "," << step.position.y();
    }
    EXPECT_TRUE(step.landed);
    EXPECT_EQ(step.position.y(), floorTop(area));
}

TEST(PetFlight, AReleaseOutsideTheAreaComesBackInside)
{
    PetFlight flight{QPoint{-500, 2000}, QPointF{0.0, 0.0}, kPet, kArea};
    EXPECT_TRUE(inside(flight.position()));
}

TEST(PetFlight, AnExtremeThrowStillLandsWithinTheLimit)
{
    PetFlight flight{QPoint{800, 400}, QPointF{2600.0, -2600.0}, kPet, kArea};

    auto step = lens::app::pet::FlightStep{};
    for (auto i = 0; i < 400 and not step.landed; ++i)
        step = flight.advance(20ms);

    ASSERT_TRUE(step.landed);
    EXPECT_LE(flight.elapsed(), PetFlight::kMaxDuration);
    EXPECT_TRUE(inside(step.position));
}

} // namespace
