/**
 * @file pet_store_test.cpp
 * @brief The PET section's size setting, offline: a percentage clamps to 0..100, a stored 1..4 scale is converted once,
 *        and the outfit round-trips (PHASE3 3.6).
 */

#include <filesystem>

#include <gtest/gtest.h>

#include "core/document/pet_store.h"
#include "core/document/known_store.h"

namespace {

using lens::core::pet::kMaxSize;
using lens::core::pet::kMinSize;
using lens::core::pet::magnificationOf;
using lens::core::pet::PetStore;
using lens::core::pet::sizeFromScale;
using lens::core::KnownStore;

TEST(PetStore, SizeFallsBackUntilTheReaderChoosesOne)
{
    auto const path  = std::filesystem::temp_directory_path() / "lens-pet-store-fallback.json";
    auto       store = KnownStore::load(path);
    auto       pet   = PetStore{store};

    EXPECT_EQ(pet.size(67), 67);
}

TEST(PetStore, OutfitReadsBackAsItWasWritten)
{
    auto const path  = std::filesystem::temp_directory_path() / "lens-pet-store-outfit.json";
    auto       store = KnownStore::load(path);
    auto       pet   = PetStore{store};

    pet.setOutfit(lens::core::pet::Outfit{.head = std::string{"hat"}, .face = std::nullopt, .body = std::nullopt});

    auto const worn = pet.outfit();
    EXPECT_EQ(worn.head, std::optional<std::string>{"hat"});
    EXPECT_FALSE(worn.face);
    EXPECT_FALSE(worn.body);
}

TEST(PetStore, SizeClampsToTheSliderRange)
{
    auto const path  = std::filesystem::temp_directory_path() / "lens-pet-store-clamp.json";
    auto       store = KnownStore::load(path);
    auto       pet   = PetStore{store};

    pet.setSize(200);
    EXPECT_EQ(pet.size(67), kMaxSize);

    pet.setSize(-5);
    EXPECT_EQ(pet.size(67), kMinSize);

    pet.setSize(40);
    EXPECT_EQ(pet.size(67), 40);
}

TEST(PetStore, SizeAndMagnificationConvert)
{
    // The package's whole-number scale, and the magnification the percentage it becomes draws.
    EXPECT_EQ(sizeFromScale(1), kMinSize);
    EXPECT_EQ(sizeFromScale(2), 33);
    EXPECT_EQ(sizeFromScale(3), 67);
    EXPECT_EQ(sizeFromScale(4), kMaxSize);

    EXPECT_DOUBLE_EQ(magnificationOf(0), 1.0);
    EXPECT_DOUBLE_EQ(magnificationOf(50), 2.5);
    EXPECT_DOUBLE_EQ(magnificationOf(100), 4.0);
}

TEST(PetStore, AnOldScaleIsConvertedOnLoadAndTheOldKeyIsTakenAway)
{
    auto const path = std::filesystem::temp_directory_path() / "lens-pet-store-migrate.json";
    std::filesystem::remove(path);
    auto store = KnownStore::load(path);
    store.section("PET").set<int>("scale", 3);

    auto pet = PetStore{store};
    EXPECT_EQ(pet.size(0), 67);

    // The conversion ran once: the document now holds the percentage and no longer the ambiguous old key.
    auto const& held = store.document().at("PET");
    EXPECT_TRUE(held.contains("size"));
    EXPECT_FALSE(held.contains("scale"));

    // Writing a size never writes the old key back.
    pet.setSize(10);
    EXPECT_FALSE(store.document().at("PET").contains("scale"));
}

} // namespace
