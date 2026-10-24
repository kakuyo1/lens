/**
 * @file pet_accessory_test.cpp
 * @brief Wardrobe, offline: one accessory per slot, and per-action visibility (PHASE3 3.4, 3.9).
 */

#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "core/domain/pet/accessory.h"

namespace {

using lens::core::pet::AccessorySpec;
using lens::core::pet::ActionName;
using lens::core::pet::Slot;
using lens::core::pet::Wardrobe;

/// Every action but sleep: sleep hides all accessories (PHASE3 3.4).
std::vector<ActionName> awakeActions()
{
    return {"idle", "study", "thinking", "celebrate", "encourage", "click_react", "pickup", "look_around", "yawn", "stretch"};
}

Wardrobe makeWardrobe()
{
    return Wardrobe{{
        AccessorySpec{"hat", Slot::Head, awakeActions()},
        AccessorySpec{"beanie", Slot::Head, awakeActions()},
        AccessorySpec{"glasses", Slot::Face, awakeActions()},
        AccessorySpec{"bowtie", Slot::Body, awakeActions()},
    }};
}

TEST(Wardrobe, WearingAnAccessoryPutsItOnItsSlot)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    EXPECT_EQ(wardrobe.wornIn(Slot::Head), "hat");
    EXPECT_FALSE(wardrobe.wornIn(Slot::Face).has_value());
}

TEST(Wardrobe, OneAccessoryPerSlotSoASecondReplacesTheFirst)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    wardrobe.toggle("beanie");
    EXPECT_EQ(wardrobe.wornIn(Slot::Head), "beanie");
}

TEST(Wardrobe, TogglingTheWornAccessoryTakesItOff)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    wardrobe.toggle("hat");
    EXPECT_FALSE(wardrobe.wornIn(Slot::Head).has_value());
}

TEST(Wardrobe, DifferentSlotsDoNotInterfere)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    wardrobe.toggle("glasses");
    wardrobe.toggle("bowtie");
    EXPECT_EQ(wardrobe.wornIn(Slot::Head), "hat");
    EXPECT_EQ(wardrobe.wornIn(Slot::Face), "glasses");
    EXPECT_EQ(wardrobe.wornIn(Slot::Body), "bowtie");
}

TEST(Wardrobe, AccessoryIsHiddenInAnActionItDoesNotSupportAndReappears)
{
    auto wardrobe = Wardrobe{{AccessorySpec{"hat", Slot::Head, {"idle", "study"}}}};
    wardrobe.toggle("hat");
    EXPECT_EQ(wardrobe.visibleFor("study"), std::vector<std::string>{"hat"});
    EXPECT_TRUE(wardrobe.visibleFor("sleep").empty());

    // Hidden, not removed: it is still on its slot when the dog wakes.
    EXPECT_EQ(wardrobe.wornIn(Slot::Head), "hat");
    EXPECT_EQ(wardrobe.visibleFor("idle"), std::vector<std::string>{"hat"});
}

TEST(Wardrobe, SleepHidesEveryAccessory)
{
    auto wardrobe = makeWardrobe();
    wardrobe.toggle("hat");
    wardrobe.toggle("glasses");
    wardrobe.toggle("bowtie");
    EXPECT_TRUE(wardrobe.visibleFor("sleep").empty());
}

TEST(Wardrobe, AnActionNameTheCodeNeverHeardOfIsVisibleWhenTheAccessoryNamesIt)
{
    // Action names are data, so an accessory can support any action the pet defines.
    auto wardrobe = Wardrobe{{AccessorySpec{"hat", Slot::Head, {"wobble"}}}};
    wardrobe.toggle("hat");
    EXPECT_EQ(wardrobe.visibleFor("wobble"), std::vector<std::string>{"hat"});
}

TEST(Wardrobe, TogglingAnUnknownAccessoryIsAnError)
{
    auto wardrobe = makeWardrobe();
    EXPECT_THROW(wardrobe.toggle("crown"), std::invalid_argument);
}

TEST(Wardrobe, ASceneWeightOnlyAppliesWhileTheAccessoryIsWorn)
{
    auto wardrobe = Wardrobe{{
        AccessorySpec{"glasses", Slot::Face, awakeActions(), {{"desk", 2.5}, {"owl_books", 2.0}}},
        AccessorySpec{"hat", Slot::Head, awakeActions()},
    }};

    EXPECT_DOUBLE_EQ(wardrobe.sceneWeightFor("desk"), 1.0);
    wardrobe.toggle("glasses");
    EXPECT_DOUBLE_EQ(wardrobe.sceneWeightFor("desk"), 2.5);
    EXPECT_DOUBLE_EQ(wardrobe.sceneWeightFor("owl_books"), 2.0);
    EXPECT_DOUBLE_EQ(wardrobe.sceneWeightFor("swing"), 1.0);

    wardrobe.toggle("glasses");
    EXPECT_DOUBLE_EQ(wardrobe.sceneWeightFor("desk"), 1.0);

    // A piece without a sceneWeights block leaves the lottery exactly as it was.
    wardrobe.toggle("hat");
    EXPECT_DOUBLE_EQ(wardrobe.sceneWeightFor("desk"), 1.0);
}

TEST(Wardrobe, WornAccessoriesMultiplyTheSameScene)
{
    auto wardrobe = Wardrobe{{
        AccessorySpec{"glasses", Slot::Face, awakeActions(), {{"desk", 2.0}}},
        AccessorySpec{"scarf", Slot::Body, awakeActions(), {{"desk", 3.0}}},
        AccessorySpec{"bowtie", Slot::Body, awakeActions()},
    }};

    wardrobe.toggle("glasses");
    wardrobe.toggle("scarf");
    EXPECT_DOUBLE_EQ(wardrobe.sceneWeightFor("desk"), 6.0);

    // Replacing a body piece drops its factor but leaves the face piece in place.
    wardrobe.toggle("bowtie");
    EXPECT_DOUBLE_EQ(wardrobe.sceneWeightFor("desk"), 2.0);
}

} // namespace
