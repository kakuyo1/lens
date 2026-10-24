/**
 * @file pet_data_test.cpp
 * @brief The shipped pet data, offline: data/pet/dog/*.json must pass the parsers the app runs at start (PHASE3 3.8).
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <QImage>
#include <QPointF>
#include <QString>

#include "app/pet/pet_assets.h"
#include "core/domain/pet/pet_config.h"
#include "core/domain/pet/pet_package.h"

namespace {

using lens::core::pet::parseAccessories;
using lens::core::pet::parseAnchors;
using lens::core::pet::parseAnimations;
using nlohmann::json;

json loadAt(std::filesystem::path const& directory, char const* name)
{
    auto const path   = directory / name;
    auto       stream = std::ifstream{path};
    EXPECT_TRUE(stream.is_open()) << path.string();
    return json::parse(stream);
}

json load(char const* name)
{
    return loadAt(std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet" / "dog", name);
}

TEST(PetData, AnimationsLoadAndCarryTheBlink)
{
    auto const animations = parseAnimations(load("animations.json"));

    // The fourteen role actions (angry, ask and dizzy included), the three idle extras no role names, the four
    // scenes, and the walk and edge-peek loops.
    EXPECT_EQ(animations.playbook.actions.size(), 23u);
    EXPECT_EQ(animations.blink.frames, 3);
    EXPECT_FALSE(animations.blink.loop);
}

TEST(PetData, EveryRoleAndTheIdlePoolArePlayable)
{
    auto const playbook = parseAnimations(load("animations.json")).playbook;

    // The dog maps every role, stretch, angry, ask and dizzy included. Its pool is the document's list followed by
    // every action no role names, which the loader adds by name.
    EXPECT_EQ(playbook.roles.size(), 14u);
    EXPECT_EQ(playbook.idlePool, (std::vector<std::string>{"look_around", "stretch", "scratch", "sniff", "wag"}));
}

TEST(PetData, AnchorsMatchTheFrameCountOfEveryAction)
{
    auto const animations = parseAnimations(load("animations.json"));

    EXPECT_NO_THROW(parseAnchors(load("anchors.json"), animations.playbook));
}

TEST(PetData, ExpressionListsCoverEveryFrame)
{
    auto const doc = load("animations.json");

    for (auto const& [name, entry] : doc.at("actions").items()) {
        if (not entry.contains("expressions")) continue;
        EXPECT_EQ(entry.at("expressions").size(), entry.at("frames").get<std::size_t>()) << name;
    }
}

TEST(PetData, AccessoriesLoadWithEveryWardrobePiece)
{
    auto const animations = parseAnimations(load("animations.json"));
    auto const catalogue  = parseAccessories(load("accessories.json"), animations.playbook);

    EXPECT_EQ(catalogue.size(), 9u);
}

TEST(PetData, EveryShippedPetWearsThreePiecesInEachSlot)
{
    for (auto const& entry : std::filesystem::directory_iterator{std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet"}) {
        if (not entry.is_directory()) continue;
        auto const animations = parseAnimations(loadAt(entry.path(), "animations.json"));
        auto const catalogue  = parseAccessories(loadAt(entry.path(), "accessories.json"), animations.playbook);

        auto perSlot = std::map<lens::core::pet::Slot, int>{};
        for (auto const& accessory : catalogue) {
            ++perSlot[accessory.slot];
            // A chip that changes nothing while the pet stands idle reads as broken, so every shipped piece shows then.
            auto const shows = std::find(accessory.supportedActions.begin(), accessory.supportedActions.end(), "idle");
            EXPECT_TRUE(shows != accessory.supportedActions.end()) << entry.path().filename().string() << " " << accessory.id;
        }
        EXPECT_EQ(catalogue.size(), 9u) << entry.path().filename().string();
        EXPECT_EQ(perSlot[lens::core::pet::Slot::Head], 3) << entry.path().filename().string();
        EXPECT_EQ(perSlot[lens::core::pet::Slot::Face], 3) << entry.path().filename().string();
        EXPECT_EQ(perSlot[lens::core::pet::Slot::Body], 3) << entry.path().filename().string();
    }
}

TEST(PetData, EverySheetIsTheSizeItsFrameCountSays)
{
    auto const directory = std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet" / "dog";

    EXPECT_NO_THROW(lens::app::pet::PetAssets::load(directory));
}

TEST(PetData, TheAskActionIsAPoseAndDrawsNoEffect)
{
    // The review sign stands beside the pet, so the ask action bobs the body and holds no hand or other effect sheet.
    auto const directory = std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet" / "dog";
    auto const ask       = lens::app::pet::PetAssets::load(directory).sheets.at("ask");

    EXPECT_TRUE(ask.effect.isEmpty());
    EXPECT_FALSE(ask.body.isEmpty());
}

TEST(PetData, EveryShippedPetHasTheDizzyLandingWithStars)
{
    for (auto const& entry : std::filesystem::directory_iterator{std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet"}) {
        if (not entry.is_directory()) continue;
        auto const animations = parseAnimations(loadAt(entry.path(), "animations.json"));
        ASSERT_EQ(animations.playbook.roles.count(lens::core::pet::Role::Dizzy), 1u) << entry.path().filename().string();

        auto const name   = animations.playbook.roles.at(lens::core::pet::Role::Dizzy);
        auto const assets = lens::app::pet::PetAssets::load(entry.path());
        EXPECT_FALSE(assets.sheets.at(name).body.isEmpty()) << name;
        EXPECT_FALSE(assets.sheets.at(name).effect.isEmpty()) << name << " has no star sheet";
        EXPECT_FALSE(animations.playbook.actions.at(name).loop) << name;
    }
}

TEST(PetData, EveryShippedPetWalksAndPeeks)
{
    auto pets = 0;
    for (auto const& entry : std::filesystem::directory_iterator{std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet"}) {
        if (not entry.is_directory()) continue;
        ++pets;
        auto const animations = parseAnimations(loadAt(entry.path(), "animations.json"));
        ASSERT_TRUE(animations.movement.has_value()) << entry.path().filename().string();

        auto const& walk = animations.playbook.actions.at(animations.movement->walk);
        EXPECT_TRUE(walk.loop) << entry.path().filename().string();
        ASSERT_TRUE(animations.movement->peek.has_value()) << entry.path().filename().string();
        auto const& peek = animations.playbook.actions.at(*animations.movement->peek);
        EXPECT_TRUE(peek.loop) << entry.path().filename().string();

        // A worn accessory stays on during the walk and the peek: the pet should not undress when it moves.
        for (auto const& accessory : parseAccessories(loadAt(entry.path(), "accessories.json"), animations.playbook)) {
            auto const supports = [&accessory](std::string const& name) {
                return std::find(accessory.supportedActions.begin(), accessory.supportedActions.end(), name) != accessory.supportedActions.end();
            };
            if (not supports("idle")) continue;
            EXPECT_TRUE(supports(animations.movement->walk)) << accessory.id;
            EXPECT_TRUE(supports(*animations.movement->peek)) << accessory.id;
        }
    }
    EXPECT_EQ(pets, 7);
}

TEST(PetData, EveryShippedPetHasTwoOwnScenesAndTwoSharedOnes)
{
    auto const scenesDoc   = loadAt(std::filesystem::path{LENS_SOURCE_DIR} / "data" / "scenes", "scenes.json");
    auto       sceneFrames = std::map<std::string, int>{};
    for (auto const& scene : scenesDoc.at("scenes"))
        sceneFrames.emplace(scene.at("id").get<std::string>(), scene.at("frames").get<int>());

    auto exclusiveOwners = std::map<std::string, int>{};
    for (auto const& entry : std::filesystem::directory_iterator{std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet"}) {
        if (not entry.is_directory()) continue;
        auto const pet        = entry.path().filename().string();
        auto const animations = parseAnimations(loadAt(entry.path(), "animations.json"));
        auto       own        = 0;
        auto       shared     = 0;
        for (auto const& [name, spec] : animations.playbook.actions) {
            auto const scene = sceneFrames.find(name);
            if (scene == sceneFrames.end()) continue;
            if (name.rfind(pet + "_", 0) == 0) {
                ++own;
                ++exclusiveOwners[name];
            } else {
                ++shared;
            }
            EXPECT_EQ(spec.frames, scene->second) << name;
            ASSERT_TRUE(spec.duration.has_value()) << name;
            EXPECT_EQ(*spec.duration, std::chrono::milliseconds{30000}) << name;
        }
        EXPECT_EQ(own, 2) << pet;
        EXPECT_EQ(shared, 2) << pet;
    }
    for (auto const& [name, owners] : exclusiveOwners)
        EXPECT_EQ(owners, 1) << name;
}

TEST(PetData, EveryShippedPetIsAcceptedWhole)
{
    // Each folder under data/pet is a pet the picker offers, so the app must accept every one of them.
    auto pets = 0;
    for (auto const& entry : std::filesystem::directory_iterator{std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet"}) {
        if (not entry.is_directory()) continue;
        ++pets;
        EXPECT_NO_THROW(lens::app::pet::PetAssets::load(entry.path())) << entry.path().string();
    }
    EXPECT_GT(pets, 0);
}

/// A scratch copy of data/pet/dog for one test, removed when the test ends. The copy sits in a folder named for the pet,
/// since the id must equal the folder name (docs/pet-package-format.md, rule 11).
class ScratchPet {
public:
    explicit ScratchPet(std::string const& name)
        : holder_{std::filesystem::temp_directory_path() / ("lens-pet-data-" + name)}, root_{holder_ / "dog"}
    {
        std::filesystem::remove_all(holder_);
        std::filesystem::create_directories(holder_);
        std::filesystem::copy(std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet" / "dog", root_, std::filesystem::copy_options::recursive);
    }
    ~ScratchPet()
    {
        std::filesystem::remove_all(holder_);
    }

    std::filesystem::path const& path() const
    {
        return root_;
    }

    /// Writes `document` over `file`, pretty-printed, so a test can change one field of a shipped document.
    void writeJson(std::string const& file, json const& document) const
    {
        auto out = std::ofstream{root_ / file, std::ios::binary};
        out << document.dump(2);
    }

    /// Replaces the first `from` in `file` with `to`; the test fails if `from` is not in the file.
    void replaceIn(std::string const& file, std::string const& from, std::string const& to) const
    {
        auto const where = root_ / file;
        auto       text  = std::string{};
        {
            auto in = std::ifstream{where, std::ios::binary};
            text.assign(std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{});
        }
        auto const at = text.find(from);
        ASSERT_NE(at, std::string::npos) << from;
        text.replace(at, from.size(), to);
        auto out = std::ofstream{where, std::ios::binary};
        out << text;
    }

private:
    std::filesystem::path holder_;
    std::filesystem::path root_;
};

TEST(PetData, AScaleOutsideTheSliderRangeRefusesThePet)
{
    auto scratch = ScratchPet{"scale"};
    scratch.replaceIn("pet.json", "\"scale\": 3", "\"scale\": 99");

    EXPECT_THROW(lens::app::pet::PetAssets::load(scratch.path()), std::runtime_error);
}

TEST(PetData, ASheetOfTheWrongWidthRefusesThePet)
{
    // study.png is six frames wide; idle.png is declared as five, so the wrong sheet must be refused.
    auto scratch = ScratchPet{"sheet"};
    std::filesystem::copy_file(scratch.path() / "sprites/body/study.png", scratch.path() / "sprites/body/idle.png", std::filesystem::copy_options::overwrite_existing);

    EXPECT_THROW(lens::app::pet::PetAssets::load(scratch.path()), std::runtime_error);
}

/// Writes `sheet` over the idle body sheet of a scratch pet, at the size the sheet already has.
void writeIdleSheet(ScratchPet const& scratch, QImage const& sheet)
{
    ASSERT_TRUE(sheet.save(QString::fromStdWString((scratch.path() / "sprites/body/idle.png").wstring()), "PNG"));
}

TEST(PetData, TheIdleFigureCentreIsMeasuredFromItsPixels)
{
    // One block in columns 60 to 69 of the idle frame: its centre is 65, where the canvas centre is 44.
    auto scratch = ScratchPet{"ink"};
    auto sheet   = QImage{440, 88, QImage::Format_ARGB32};
    sheet.fill(Qt::transparent);
    for (auto y = 20; y < 60; ++y) {
        for (auto x = 60; x < 70; ++x)
            sheet.setPixel(x, y, qRgba(0, 0, 0, 255));
    }
    writeIdleSheet(scratch, sheet);

    auto const assets = lens::app::pet::PetAssets::load(scratch.path());
    EXPECT_DOUBLE_EQ(assets.inkCentre, 65.0);
    EXPECT_DOUBLE_EQ(assets.inkLeft, 60.0);
    EXPECT_DOUBLE_EQ(assets.inkTop, 20.0);
    EXPECT_DOUBLE_EQ(assets.inkRight, 70.0);
    EXPECT_DOUBLE_EQ(assets.inkBottom, 60.0);
}

TEST(PetData, AnIdleFrameWithNothingDrawnCentresOnTheCanvas)
{
    auto scratch = ScratchPet{"blank"};
    auto sheet   = QImage{440, 88, QImage::Format_ARGB32};
    sheet.fill(Qt::transparent);
    writeIdleSheet(scratch, sheet);

    auto const assets = lens::app::pet::PetAssets::load(scratch.path());
    EXPECT_DOUBLE_EQ(assets.inkCentre, 44.0);
    EXPECT_DOUBLE_EQ(assets.inkLeft, 0.0);
    EXPECT_DOUBLE_EQ(assets.inkTop, 0.0);
    EXPECT_DOUBLE_EQ(assets.inkRight, 88.0);
    EXPECT_DOUBLE_EQ(assets.inkBottom, 88.0);
}

TEST(PetData, LayersStackInTheSpecOrder)
{
    // PHASE3 3.7: body, expression, body accessories, face, head accessories, effect.
    auto const layers      = load("pet.json").at("layers");
    auto const accessories = load("accessories.json").at("accessories");
    auto const zOf         = [&accessories](char const* slot) {
        auto zs = std::vector<int>{};
        for (auto const& item : accessories) {
            if (item.at("slot") == slot) zs.push_back(item.at("zIndex").get<int>());
        }
        return zs;
    };

    auto const body       = layers.at("body").get<int>();
    auto const expression = layers.at("expression").get<int>();
    auto const effect     = layers.at("effect").get<int>();
    auto const bodyWorn   = zOf("body");
    auto const faceWorn   = zOf("face");
    auto const headWorn   = zOf("head");

    ASSERT_FALSE(bodyWorn.empty());
    ASSERT_FALSE(faceWorn.empty());
    ASSERT_FALSE(headWorn.empty());
    EXPECT_LT(body, expression);
    EXPECT_LT(expression, *std::min_element(bodyWorn.begin(), bodyWorn.end()));
    EXPECT_LT(*std::max_element(bodyWorn.begin(), bodyWorn.end()), *std::min_element(faceWorn.begin(), faceWorn.end()));
    EXPECT_LT(*std::max_element(faceWorn.begin(), faceWorn.end()), *std::min_element(headWorn.begin(), headWorn.end()));
    EXPECT_LT(*std::max_element(headWorn.begin(), headWorn.end()), effect);
}

/// @return The reason `load` refuses the pet with, or nothing when it is accepted.
std::optional<lens::core::pet::Refusal> refusalOfLoad(std::filesystem::path const& directory)
{
    try {
        lens::app::pet::PetAssets::load(directory);
    } catch (lens::core::pet::Refused const& refused) {
        return refused.reason();
    }
    return std::nullopt;
}

/// A PNG whose header declares `width` by `height` and whose body is nothing: the file is 24 bytes, and a decoder would
/// allocate for the declared size. This is the decompression-bomb shape.
void writeDeclaredPng(std::filesystem::path const& file, std::uint32_t width, std::uint32_t height)
{
    auto bytes = std::string{"\x89PNG\r\n\x1a\n\x00\x00\x00\x0DIHDR", 16};
    for (auto const value : {width, height}) {
        bytes += static_cast<char>(value >> 24);
        bytes += static_cast<char>(value >> 16);
        bytes += static_cast<char>(value >> 8);
        bytes += static_cast<char>(value);
    }
    auto out = std::ofstream{file, std::ios::binary};
    out << bytes;
}

/// Creates a directory junction at `link` pointing at `target`. It needs no privilege, unlike a symbolic link, and it is
/// the link an archive can carry. Removing it is the caller's job: a recursive delete would walk into `target`.
void makeJunction(std::filesystem::path const& link, std::filesystem::path const& target)
{
    auto const command = "cmd /c mklink /J \"" + link.string() + "\" \"" + target.string() + "\"";
    ASSERT_EQ(std::system(command.c_str()), 0);
}

TEST(PetData, AnAssetPathThatClimbsOutOfThePetIsPathEscapes)
{
    // The path lands back inside the pet, but it is spelled with `..`, and the spelling is refused on its own.
    auto scratch = ScratchPet{"dotdot"};
    scratch.replaceIn("animations.json", "\"sheet\": \"sprites/body/idle.png\"", "\"sheet\": \"../dog/sprites/body/idle.png\"");

    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::PathEscapes);
}

TEST(PetData, AnAbsoluteAssetPathIsPathEscapes)
{
    auto scratch = ScratchPet{"absolute"};
    // A sheet outside the pet, named at run time: a machine path has no place in the source.
    auto const outside              = (std::filesystem::temp_directory_path() / "idle.png").generic_string();
    auto       doc                  = json::parse(std::ifstream{scratch.path() / "animations.json"});
    doc["actions"]["idle"]["sheet"] = outside;
    scratch.writeJson("animations.json", doc);

    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::PathEscapes);
}

TEST(PetData, AnAssetReachedThroughAJunctionIsPathEscapes)
{
    // The junction points at a folder outside the pet that really holds a PNG, so a path that only looks inside would
    // load it. The junction is removed before the scratch is, since a recursive delete would follow it.
    auto scratch = ScratchPet{"junction"};
    auto outside = std::filesystem::temp_directory_path() / "lens-pet-data-junction-target";
    std::filesystem::remove_all(outside);
    std::filesystem::create_directories(outside);
    std::filesystem::copy_file(scratch.path() / "sprites/body/idle.png", outside / "idle.png");
    makeJunction(scratch.path() / "sprites/linked", outside);
    auto const link = scratch.path() / "sprites/linked";

    scratch.replaceIn("animations.json", "\"sheet\": \"sprites/body/idle.png\"", "\"sheet\": \"sprites/linked/idle.png\"");
    auto const reason = refusalOfLoad(scratch.path());

    std::filesystem::remove(link);
    EXPECT_EQ(reason, lens::core::pet::Refusal::PathEscapes);
    EXPECT_TRUE(std::filesystem::exists(outside / "idle.png")) << "the junction's target must survive the refusal";
    std::filesystem::remove_all(outside);
}

TEST(PetData, AJunctionAnywhereInThePetIsRefusedEvenIfUnused)
{
    // The tree walk refuses a link before any document is read, so an unreferenced junction still refuses the pet.
    auto scratch = ScratchPet{"junction-unused"};
    auto outside = std::filesystem::temp_directory_path() / "lens-pet-data-unused-target";
    std::filesystem::remove_all(outside);
    std::filesystem::create_directories(outside);
    makeJunction(scratch.path() / "stray", outside);

    auto const reason = refusalOfLoad(scratch.path());

    std::filesystem::remove(scratch.path() / "stray");
    EXPECT_EQ(reason, lens::core::pet::Refusal::PathEscapes);
    std::filesystem::remove_all(outside);
}

TEST(PetData, ADeclaredSizeFarBeyondTheSheetLimitIsTooLargeBeforeAnyDecode)
{
    auto scratch = ScratchPet{"bomb"};
    writeDeclaredPng(scratch.path() / "sprites/body/idle.png", 65535, 65535);

    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::TooLarge);
}

TEST(PetData, AnExpressionIndexPastTheSheetIsExpressionIndex)
{
    auto scratch                           = ScratchPet{"expression"};
    auto doc                               = json::parse(std::ifstream{scratch.path() / "animations.json"});
    doc["actions"]["blink"]["expressions"] = json::array({1, 2, 99});
    scratch.writeJson("animations.json", doc);

    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::ExpressionIndex);
}

TEST(PetData, ANonPngAssetWithAPngNameIsBadAssetType)
{
    auto scratch = ScratchPet{"svg"};
    auto out     = std::ofstream{scratch.path() / "sprites/accessories/headwear/flat_cap.png", std::ios::binary};
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\"/>";
    out.close();

    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::BadAssetType);
}

TEST(PetData, AMissingSheetIsUnreadable)
{
    auto scratch = ScratchPet{"missing"};
    std::filesystem::remove(scratch.path() / "sprites/body/idle.png");

    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::Unreadable);
}

TEST(PetData, AWrongSizedSheetIsSheetSize)
{
    auto scratch = ScratchPet{"wrong-size"};
    std::filesystem::copy_file(scratch.path() / "sprites/body/study.png", scratch.path() / "sprites/body/idle.png", std::filesystem::copy_options::overwrite_existing);

    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::SheetSize);
}

TEST(PetData, AFolderNameThatIsNotThePetIdIsBadName)
{
    // The id in pet.json must be the folder's name. A copy under another name is refused, not renamed for the reader.
    auto scratch = ScratchPet{"renamed"};
    auto renamed = scratch.path().parent_path() / "lounge";
    std::filesystem::remove_all(renamed);
    std::filesystem::rename(scratch.path(), renamed);

    auto const reason = refusalOfLoad(renamed);

    std::filesystem::rename(renamed, scratch.path());
    EXPECT_EQ(reason, lens::core::pet::Refusal::BadName);
}

TEST(PetData, ANameMadeOfInvisibleCharactersIsBadName)
{
    auto scratch = ScratchPet{"invisible-name"};
    auto doc     = json::parse(std::ifstream{scratch.path() / "pet.json"});
    doc["name"]  = "\u200B\u202E\u200D";
    scratch.writeJson("pet.json", doc);

    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::BadName);
}

TEST(PetData, AName32CharactersLongIsAcceptedAndOneLongerIsTooLarge)
{
    auto scratch = ScratchPet{"long-name"};
    auto doc     = json::parse(std::ifstream{scratch.path() / "pet.json"});

    doc["name"] = std::string(32, 'n');
    scratch.writeJson("pet.json", doc);
    EXPECT_FALSE(refusalOfLoad(scratch.path()).has_value());

    doc["name"] = std::string(33, 'n');
    scratch.writeJson("pet.json", doc);
    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::TooLarge);
}

TEST(PetData, ANameIsCleanedOfControlAndFormatCharactersAndNormalised)
{
    // "e" followed by a combining acute is one character once composed, and the bidi override and the zero-width space
    // are removed without refusing the pet.
    auto scratch = ScratchPet{"clean-name"};
    auto doc     = json::parse(std::ifstream{scratch.path() / "pet.json"});
    doc["name"]  = "\u202Ee\u0301\u200B";
    scratch.writeJson("pet.json", doc);

    auto const assets = lens::app::pet::PetAssets::load(scratch.path());
    EXPECT_EQ(assets.name, QString::fromUtf8("\u00e9"));
}

TEST(PetData, APngNamedWithAnotherExtensionIsBadAssetType)
{
    // The type comes from the magic, and the name must agree with it: a PNG called .gif is refused.
    auto scratch = ScratchPet{"gif-name"};
    std::filesystem::copy_file(scratch.path() / "sprites/body/idle.png", scratch.path() / "sprites/body/idle.gif");
    scratch.replaceIn("animations.json", "\"sheet\": \"sprites/body/idle.png\"", "\"sheet\": \"sprites/body/idle.gif\"");

    EXPECT_EQ(refusalOfLoad(scratch.path()), lens::core::pet::Refusal::BadAssetType);
}

TEST(PetData, ThePetNameIsTheIdWhenThePackageGivesNone)
{
    auto scratch = ScratchPet{"no-name"};

    EXPECT_EQ(lens::app::pet::PetAssets::load(scratch.path()).name, QStringLiteral("dog"));
}

} // namespace
