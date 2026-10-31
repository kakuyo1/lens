/**
 * @file pet_package_test.cpp
 * @brief The pet package contract, offline: each refusal reason of skills/lens-pet-maker/references/pet-package-format.md section 6 is reached by
 *        the rule that owns it, and each limit of section 4 refuses at its boundary (PHASE3 3.8).
 */

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/domain/pet/pet_config.h"
#include "core/domain/pet/pet_package.h"

namespace {

using lens::core::pet::isContainedSpelling;
using lens::core::pet::isPetId;
using lens::core::pet::isPlainLabel;
using lens::core::pet::parseAccessories;
using lens::core::pet::parseAnchors;
using lens::core::pet::parseAnimations;
using lens::core::pet::parsePetDocument;
using lens::core::pet::readPngSize;
using lens::core::pet::Refusal;
using lens::core::pet::Refused;
using nlohmann::json;

json shipped(char const* name)
{
    auto const path   = std::filesystem::path{LENS_SOURCE_DIR} / "data" / "pet" / "dog" / name;
    auto       stream = std::ifstream{path};
    return json::parse(stream);
}

/// @return The reason `call` is refused for, or nothing when it is accepted.
template <typename Call>
std::optional<Refusal> refusalOf(Call call)
{
    try {
        call();
    } catch (Refused const& refused) {
        return refused.reason();
    }
    return std::nullopt;
}

TEST(PetPackageSpelling, AcceptsRelativePathsUnderTheFolder)
{
    EXPECT_TRUE(isContainedSpelling("sprites/body/idle.png"));
    EXPECT_TRUE(isContainedSpelling("sprites\\body\\idle.png"));
    EXPECT_TRUE(isContainedSpelling("sprites/./body/idle.png"));
    EXPECT_TRUE(isContainedSpelling("..b/idle.png")) << "a name that starts with two dots is not a parent segment";
}

TEST(PetPackageSpelling, RefusesAnythingThatCanLeaveTheFolder)
{
    EXPECT_FALSE(isContainedSpelling(""));
    EXPECT_FALSE(isContainedSpelling("/etc/passwd"));
    EXPECT_FALSE(isContainedSpelling("\\\\server\\share\\x.png")) << "UNC";
    EXPECT_FALSE(isContainedSpelling("C:x.png")) << "drive-relative";
    EXPECT_FALSE(isContainedSpelling("/outside/x.png"));
    EXPECT_FALSE(isContainedSpelling("../x.png"));
    EXPECT_FALSE(isContainedSpelling("sprites/../../x.png"));
    EXPECT_FALSE(isContainedSpelling("sprites\\..\\..\\x.png"));
    EXPECT_FALSE(isContainedSpelling(std::string_view{"a\0b.png", 5}));
}

TEST(PetPackageId, MatchesTheLowercaseIdPattern)
{
    EXPECT_TRUE(isPetId("dog"));
    EXPECT_TRUE(isPetId("white_fox_2"));
    EXPECT_TRUE(isPetId(std::string(32, 'a')));

    EXPECT_FALSE(isPetId(""));
    EXPECT_FALSE(isPetId("Dog"));
    EXPECT_FALSE(isPetId("a-b"));
    EXPECT_FALSE(isPetId("a b"));
    EXPECT_FALSE(isPetId(std::string(33, 'a')));
}

TEST(PetPackageLabel, AcceptsPlainTextUpToTheLimit)
{
    EXPECT_TRUE(isPlainLabel("celebrate"));
    EXPECT_TRUE(isPlainLabel("白狐"));
    EXPECT_TRUE(isPlainLabel(std::string(32, 'x')));

    EXPECT_FALSE(isPlainLabel(""));
    EXPECT_FALSE(isPlainLabel(std::string(33, 'x')));
    EXPECT_FALSE(isPlainLabel("tab\there")) << "a control character";
    EXPECT_FALSE(isPlainLabel("\xE2\x80\x8B"
                              "zero"))
        << "U+200B, zero-width space";
    EXPECT_FALSE(isPlainLabel("\xE2\x80\xAE"
                              "rtl"))
        << "U+202E, a bidi override";
    EXPECT_FALSE(isPlainLabel("\xC3\x28")) << "not well-formed UTF-8";
}

TEST(PetPackageSheet, ReadsTheDeclaredSizeFromTheHeaderOnly)
{
    // Signature, IHDR length 13, "IHDR", then width 300 and height 88 big-endian.
    auto const head = std::string{"\x89PNG\r\n\x1a\n\x00\x00\x00\x0DIHDR\x00\x00\x01\x2C\x00\x00\x00\x58", 24};

    auto const size = readPngSize(head);
    ASSERT_TRUE(size.has_value());
    EXPECT_EQ(size->width, 300u);
    EXPECT_EQ(size->height, 88u);
}

TEST(PetPackageSheet, RefusesWhatIsNotAPngHeader)
{
    EXPECT_FALSE(readPngSize("<svg xmlns=\"http://www.w3.org/2000/svg\"/>"));
    EXPECT_FALSE(readPngSize(std::string{"\x89PNG\r\n\x1a\n", 8})) << "the signature alone has no IHDR";
    EXPECT_FALSE(readPngSize(std::string{"\x89PNG\r\n\x1a\n\x00\x00\x00\x0DXXXX\x00\x00\x00\x01\x00\x00\x00\x01", 24}))
        << "no IHDR chunk type";
}

TEST(PetPackageDocument, AcceptsTheShippedPetJson)
{
    auto const pet = parsePetDocument(shipped("pet.json"));

    EXPECT_EQ(pet.id, "dog");
    EXPECT_EQ(pet.canvas, 88);
    EXPECT_EQ(pet.scale, 3);
    EXPECT_FALSE(pet.name.has_value());
}

TEST(PetPackageDocument, RefusesAMissingOrWrongFormatAsFormat)
{
    auto missing = shipped("pet.json");
    missing.erase("format");
    EXPECT_EQ(refusalOf([&] { parsePetDocument(missing); }), Refusal::Format);

    auto other      = shipped("pet.json");
    other["format"] = 2;
    EXPECT_EQ(refusalOf([&] { parsePetDocument(other); }), Refusal::Format);
}

TEST(PetPackageDocument, RefusesAnIdThatIsNotAPetIdAsBadName)
{
    auto doc  = shipped("pet.json");
    doc["id"] = "Dog Two";
    EXPECT_EQ(refusalOf([&] { parsePetDocument(doc); }), Refusal::BadName);
}

TEST(PetPackageDocument, CanvasAndScaleBelowTheirMinimumAreFormatAndAboveAreTooLarge)
{
    auto low      = shipped("pet.json");
    low["canvas"] = 8;
    EXPECT_EQ(refusalOf([&] { parsePetDocument(low); }), Refusal::Format);

    auto high      = shipped("pet.json");
    high["canvas"] = 256;
    EXPECT_EQ(refusalOf([&] { parsePetDocument(high); }), Refusal::TooLarge);

    auto scale     = shipped("pet.json");
    scale["scale"] = 9;
    EXPECT_EQ(refusalOf([&] { parsePetDocument(scale); }), Refusal::TooLarge);
}

TEST(PetPackageDocument, ReadsTheOptionalNameAsWritten)
{
    auto doc    = shipped("pet.json");
    doc["name"] = "\u767d\u72d0";

    auto const pet = parsePetDocument(doc);
    ASSERT_TRUE(pet.name.has_value());
    EXPECT_EQ(*pet.name, "\u767d\u72d0");
}

TEST(PetPackageAnimations, FramesAboveSixtyFourAreTooLargeAndZeroIsBadAnimation)
{
    auto tooMany                         = shipped("animations.json");
    tooMany["actions"]["idle"]["frames"] = 65;
    EXPECT_EQ(refusalOf([&] { parseAnimations(tooMany); }), Refusal::TooLarge);

    auto none                         = shipped("animations.json");
    none["actions"]["idle"]["frames"] = 0;
    EXPECT_EQ(refusalOf([&] { parseAnimations(none); }), Refusal::BadAnimation);
}

TEST(PetPackageAnimations, FramesAtTheLimitAreAccepted)
{
    auto doc                         = shipped("animations.json");
    doc["actions"]["idle"]["frames"] = 64;
    EXPECT_NO_THROW(parseAnimations(doc));
}

TEST(PetPackageAnimations, FrameRateAboveTwentyFourIsTooLarge)
{
    auto doc                      = shipped("animations.json");
    doc["actions"]["idle"]["fps"] = 25;
    EXPECT_EQ(refusalOf([&] { parseAnimations(doc); }), Refusal::TooLarge);
}

TEST(PetPackageAnimations, MoreThanThirtyTwoActionsIsTooLarge)
{
    auto doc = shipped("animations.json");
    for (auto i = 0; i < 22; ++i) {
        doc["actions"]["extra_" + std::to_string(i)] = {{"frames", 1}, {"fps", 1}, {"loop", false}};
    }
    EXPECT_EQ(refusalOf([&] { parseAnimations(doc); }), Refusal::TooLarge);
}

TEST(PetPackageAnimations, AnIdlePoolOfSeventeenIsTooLarge)
{
    auto doc = shipped("animations.json");
    for (auto i = 0; i < 17; ++i)
        doc["idlePool"].push_back("look_around");
    EXPECT_EQ(refusalOf([&] { parseAnimations(doc); }), Refusal::TooLarge);
}

TEST(PetPackageAnimations, AnActionNameThatIsNotPlainIsBadAnimation)
{
    auto doc                      = shipped("animations.json");
    doc["actions"]["wob\u202Ble"] = doc["actions"]["idle"];
    EXPECT_EQ(refusalOf([&] { parseAnimations(doc); }), Refusal::BadAnimation);
}

TEST(PetPackageAnimations, AMissingRequiredRoleIsMissingRole)
{
    auto doc = shipped("animations.json");
    doc["roles"].erase("study");
    EXPECT_EQ(refusalOf([&] { parseAnimations(doc); }), Refusal::MissingRole);
}

TEST(PetPackageAnimations, AnUnknownRoleIsBadAnimation)
{
    auto doc              = shipped("animations.json");
    doc["roles"]["dance"] = "idle";
    EXPECT_EQ(refusalOf([&] { parseAnimations(doc); }), Refusal::BadAnimation);
}

TEST(PetPackageAnchors, AnchorsOfTheWrongCountAreBadAnchors)
{
    auto const animations = parseAnimations(shipped("animations.json"));
    auto       anchors    = shipped("anchors.json");
    anchors["actions"]["idle"].erase(0);
    EXPECT_EQ(refusalOf([&] { parseAnchors(anchors, animations.playbook); }), Refusal::BadAnchors);
}

TEST(PetPackageAccessories, AnAccessoryIdThatIsNotPlainIsBadAccessory)
{
    auto const playbook         = parseAnimations(shipped("animations.json")).playbook;
    auto       doc              = shipped("accessories.json");
    doc["accessories"][0]["id"] = "hat\u200B";
    EXPECT_EQ(refusalOf([&] { parseAccessories(doc, playbook); }), Refusal::BadAccessory);
}

TEST(PetPackageAccessories, MoreThanThirtyTwoIsTooLarge)
{
    auto const playbook = parseAnimations(shipped("animations.json")).playbook;
    auto       doc      = shipped("accessories.json");
    for (auto i = 0; i < 30; ++i)
        doc["accessories"].push_back(doc["accessories"][0]);
    EXPECT_EQ(refusalOf([&] { parseAccessories(doc, playbook); }), Refusal::TooLarge);
}

TEST(PetPackageLabel, RefusesTheSoftHyphenAndTheArabicAndTagFormatCharacters)
{
    EXPECT_FALSE(isPlainLabel("a\xC2\xAD"
                              "b"))
        << "U+00AD, soft hyphen";
    EXPECT_FALSE(isPlainLabel("\xD8\x80"
                              "a"))
        << "U+0600, Arabic number sign";
    EXPECT_FALSE(isPlainLabel("\xF3\xA0\x80\x81")) << "U+E0001, language tag";
}

} // namespace
