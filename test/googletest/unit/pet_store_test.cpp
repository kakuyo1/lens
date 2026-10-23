/**
 * @file pet_store_test.cpp
 * @brief The PET section, offline: a percentage clamps to 0..100, a stored 1..4 scale is converted once, and the
 *        size, the outfit and the position each belong to one pet (PHASE3 3.6, docs/adr/0096).
 */

#include <filesystem>

#include <gtest/gtest.h>

#include "core/document/pet_store.h"
#include "core/document/known_store.h"

namespace {

using lens::core::pet::kMaxSize;
using lens::core::pet::kMinSize;
using lens::core::pet::magnificationOf;
using lens::core::pet::Outfit;
using lens::core::pet::PetStore;
using lens::core::pet::SavedPosition;
using lens::core::pet::sizeFromScale;
using lens::core::KnownStore;

TEST(PetStore, SizeFallsBackUntilTheReaderChoosesOne)
{
    auto const path  = std::filesystem::temp_directory_path() / "lens-pet-store-fallback.json";
    auto       store = KnownStore::load(path);
    auto       pet   = PetStore{store};

    EXPECT_EQ(pet.size("dog", 67), 67);
}

TEST(PetStore, OutfitReadsBackAsItWasWritten)
{
    auto const path  = std::filesystem::temp_directory_path() / "lens-pet-store-outfit.json";
    auto       store = KnownStore::load(path);
    auto       pet   = PetStore{store};

    pet.setOutfit("dog", Outfit{.head = std::string{"hat"}, .face = std::nullopt, .body = std::nullopt});

    auto const worn = pet.outfit("dog");
    EXPECT_EQ(worn.head, std::optional<std::string>{"hat"});
    EXPECT_FALSE(worn.face);
    EXPECT_FALSE(worn.body);
}

TEST(PetStore, SizeClampsToTheSliderRange)
{
    auto const path  = std::filesystem::temp_directory_path() / "lens-pet-store-clamp.json";
    auto       store = KnownStore::load(path);
    auto       pet   = PetStore{store};

    pet.setSize("dog", 200);
    EXPECT_EQ(pet.size("dog", 67), kMaxSize);

    pet.setSize("dog", -5);
    EXPECT_EQ(pet.size("dog", 67), kMinSize);

    pet.setSize("dog", 40);
    EXPECT_EQ(pet.size("dog", 67), 40);
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
    // The scale belonged to the pet the document names, so the percentage lands in that pet's entry.
    EXPECT_EQ(pet.size("dog", 0), 67);

    // The conversion ran once: the document now holds the percentage and no longer the ambiguous old key.
    auto const& held = store.document().at("PET");
    EXPECT_TRUE(held.at("pets").at("dog").contains("size"));
    EXPECT_FALSE(held.contains("scale"));
    EXPECT_FALSE(held.contains("size"));

    // Writing a size never writes the old key back.
    pet.setSize("dog", 10);
    EXPECT_FALSE(store.document().at("PET").contains("scale"));
}

TEST(PetStore, EachPetRemembersItsOwnSizeOutfitAndPosition)
{
    auto const path  = std::filesystem::temp_directory_path() / "lens-pet-store-per-pet.json";
    auto       store = KnownStore::load(path);
    auto       pet   = PetStore{store};

    pet.setSize("dog", 40);
    pet.setSize("mochi", 80);
    EXPECT_EQ(pet.size("dog", 0), 40);
    EXPECT_EQ(pet.size("mochi", 0), 80);
    EXPECT_EQ(pet.size("owl", 25), 25) << "a pet with no memory keeps its own fallback";

    pet.setOutfit("dog", Outfit{.head = std::string{"hat"}, .face = std::string{"glasses"}, .body = std::nullopt});
    EXPECT_EQ(pet.outfit("dog").head, std::optional<std::string>{"hat"});
    EXPECT_FALSE(pet.outfit("mochi").head) << "one pet's clothes are not another's";

    pet.setPosition("dog", SavedPosition{"SCREEN1", 120, 40});
    pet.setPosition("mochi", SavedPosition{"SCREEN2", -30, 500});
    EXPECT_EQ(pet.position("dog")->screen, std::string{"SCREEN1"});
    EXPECT_EQ(pet.position("dog")->x, 120);
    EXPECT_EQ(pet.position("mochi")->screen, std::string{"SCREEN2"});
    EXPECT_EQ(pet.position("mochi")->x, -30);
    EXPECT_FALSE(pet.position("owl").has_value());
}

TEST(PetStore, TheFlatEveryPetValuesMoveIntoTheChosenPetsEntry)
{
    auto const path = std::filesystem::temp_directory_path() / "lens-pet-store-fold.json";
    std::filesystem::remove(path);
    auto store = KnownStore::load(path);
    {
        auto section = store.section("PET");
        section.set<std::string>("pet", "mochi");
        section.set<int>("size", 55);
        section.object("position").set<std::string>("screen", "SCREEN1");
        section.object("position").set<int>("x", 12);
        section.object("position").set<int>("y", 34);
        section.object("accessories").set<std::string>("head", "sprout");
    }

    auto pet = PetStore{store};
    // The flat keys belonged to whatever pet was on, which is the one the document names.
    EXPECT_EQ(pet.size("mochi", 0), 55);
    EXPECT_EQ(pet.outfit("mochi").head, std::optional<std::string>{"sprout"});
    EXPECT_EQ(pet.position("mochi")->x, 12);
    EXPECT_FALSE(pet.outfit("dog").head);

    // The move ran once: the flat keys are gone, and later writes never bring them back.
    auto const& held = store.document().at("PET");
    EXPECT_FALSE(held.contains("size"));
    EXPECT_FALSE(held.contains("position"));
    EXPECT_FALSE(held.contains("accessories"));
    pet.setSize("dog", 20);
    EXPECT_FALSE(held.contains("size"));
}

} // namespace
