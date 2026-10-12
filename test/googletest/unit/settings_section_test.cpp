/**
 * @file settings_section_test.cpp
 * @brief The settings document's read/write unit, and who may write which key.
 *
 * The last cases are the point of the module: they are the document's key set written out as a
 * declaration, and they are what makes "these seven keys are re-stamped, do not write them through
 * a section" a rule something can fail on rather than a fact that only lives in KnownStore::save().
 *
 * The tables name the document's top-level keys and its nested objects. What lives inside those
 * objects -- `UPDATE.autoCheck`, `PET.scale` and the rest -- is not enumerated here, and nothing
 * checks it against the tables.
 */

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/known_store.h"
#include "core/settings_section.h"
#include "core/stats_store.h"

namespace {

using lens::core::KnownStore;
using lens::core::SettingsSection;
using lens::core::StatsStore;
using nlohmann::json;

/// A document of its own and a count of its commits, so a case can see what a read writes.
struct Document {
    json body    = json::object();
    int  commits = 0;

    /// @param name Object to bind to; empty for the document itself.
    SettingsSection section(std::string_view name = {})
    {
        std::vector<std::string> path;
        if (!name.empty())
            path.emplace_back(name);
        return SettingsSection{body, std::move(path), [this] { ++commits; }};
    }
};

const std::filesystem::path scratchPath(const char* name)
{
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

} // namespace

TEST(SettingsSectionRead, TakesATextFlagAsWellAsARealOne)
{
    Document doc;
    doc.body["real"] = true;
    doc.body["text"] = "true";
    doc.body["no"]   = "false";
    doc.body["junk"] = "yes";
    doc.body["null"] = nullptr;

    const auto keys = doc.section();
    EXPECT_TRUE(keys.value<bool>("real", false));
    EXPECT_TRUE(keys.value<bool>("text", false));
    EXPECT_FALSE(keys.value<bool>("no", true));
    EXPECT_TRUE(keys.value<bool>("junk", true)) << "a string that is neither true nor false is the default";
    EXPECT_TRUE(keys.value<bool>("null", true)) << "a cleared key reads as the default";
    EXPECT_TRUE(keys.value<bool>("absent", true));
    EXPECT_EQ(doc.commits, 0) << "a read never saves";
}

TEST(SettingsSectionRead, TakesANumberHeldAsTextOnlyWhenTheWholeStringIsOne)
{
    Document doc;
    doc.body["real"]  = 3;
    doc.body["text"]  = "4";
    doc.body["part"]  = "3x";
    doc.body["money"] = "1.25";

    const auto keys = doc.section();
    EXPECT_EQ(keys.value<int>("real", 0), 3);
    EXPECT_EQ(keys.value<int>("text", 0), 4);
    EXPECT_EQ(keys.value<int>("part", 0), 0) << "\"3x\" is not a number the document meant";
    EXPECT_DOUBLE_EQ(keys.value<double>("money", 0.0), 1.25) << "the legacy text form of a number reads too";
}

TEST(SettingsSectionRead, ReadsStringsAndArraysAndNothingElse)
{
    Document doc;
    doc.body["text"]  = "dark";
    doc.body["other"] = 7;
    doc.body["ids"]   = json::array({"a", "b", 3});
    doc.body["flag"]  = true;

    const auto keys = doc.section();
    EXPECT_EQ(keys.value<std::string>("text", ""), "dark");
    EXPECT_EQ(keys.value<std::string>("other", "fallback"), "fallback");
    EXPECT_EQ(keys.value<std::string>("flag", "fallback"), "fallback") << "a real bool is not text";
    EXPECT_EQ(keys.value<std::vector<std::string>>("ids", {}), (std::vector<std::string>{"a", "b"}))
        << "one entry of the wrong type is skipped, not the whole list";
    EXPECT_TRUE(keys.value<std::vector<std::string>>("text", {}).empty()) << "a string is not a list";
}

TEST(SettingsSectionRead, TellsAnAbsentKeyFromAClearedOne)
{
    Document doc;
    doc.body["cleared"] = nullptr;
    doc.body["empty"]   = "";

    const auto keys = doc.section();
    EXPECT_TRUE(keys.has("cleared"));
    EXPECT_TRUE(keys.has("empty"));
    EXPECT_FALSE(keys.has("absent"));
}

TEST(SettingsSectionWrite, KeepsTheEncodingAKeyAlreadyHas)
{
    Document doc;
    doc.body["textFlag"]   = "false";
    doc.body["textNumber"] = "3";
    doc.body["textMoney"]  = "1.25";

    auto keys = doc.section();
    keys.set<bool>("textFlag", true);
    keys.set<int>("textNumber", 5);
    keys.set<double>("textMoney", 2.5);

    // 1.2.0 reads the first of these with documentString(...) == "true"; a real bool would read
    // as false there. See docs/adr/0024.
    EXPECT_EQ(doc.body["textFlag"], "true");
    EXPECT_EQ(doc.body["textNumber"], "5");
    EXPECT_EQ(doc.body["textMoney"], "2.5");
    EXPECT_EQ(doc.commits, 3) << "every write is saved";
}

TEST(SettingsSectionWrite, GivesAKeyThatIsNotThereYetItsOwnType)
{
    Document doc;
    auto     keys = doc.section();
    keys.set<bool>("flag", true);
    keys.set<int>("count", 4);
    keys.set<double>("money", 1.5);
    keys.set<std::string>("theme", "dark");
    keys.set<std::vector<std::string>>("ids", {"a", "b"});

    EXPECT_TRUE(doc.body["flag"].is_boolean());
    EXPECT_TRUE(doc.body["count"].is_number_integer());
    EXPECT_TRUE(doc.body["money"].is_number());
    EXPECT_TRUE(doc.body["theme"].is_string());
    EXPECT_EQ(doc.body["ids"], json::array({"a", "b"}));
}

TEST(SettingsSectionWrite, KeepsAbsentNullAndEmptyApart)
{
    Document doc;
    auto     worn = doc.section().object("PET").object("accessories");
    worn.set<std::string>("head", "");
    worn.clear("face");

    const auto pet = doc.body.at("PET").at("accessories");
    EXPECT_TRUE(pet.contains("head")) << "an empty id is a value the document holds";
    EXPECT_EQ(pet.at("head"), "");
    EXPECT_TRUE(pet.at("face").is_null()) << "clear() writes null, which is what a bare slot is";
    EXPECT_FALSE(pet.contains("body")) << "a key nobody wrote stays absent";
    EXPECT_EQ(doc.section("PET").object("accessories").value<std::string>("face", "fallback"), "fallback");
}

TEST(SettingsSectionWrite, CreatesNothingUntilSomethingIsWritten)
{
    Document doc;
    auto     pet = doc.section("PET");
    EXPECT_FALSE(pet.value<bool>("enabled", false));
    EXPECT_EQ(doc.commits, 0);
    EXPECT_FALSE(doc.body.contains("PET")) << "reading a section must not bring one into being";

    pet.set<bool>("enabled", true);
    EXPECT_TRUE(doc.body.at("PET").at("enabled").get<bool>());
    EXPECT_EQ(doc.commits, 1);

    // A write two objects down makes the ones above it.
    doc.section().object("PET").object("position").set<int>("x", 5);
    EXPECT_EQ(doc.body.at("PET").at("position").at("x"), 5);
}

TEST(SettingsSectionWrite, RemovesAKeyAltogetherAndSavesOnlyWhenSomethingWent)
{
    Document doc;
    doc.body["PET"] = json{{"enabled", true}, {"passthrough", false}};

    auto pet = doc.section("PET");
    pet.remove("enabled");
    EXPECT_FALSE(doc.body.at("PET").contains("enabled"));
    EXPECT_EQ(doc.commits, 1);

    pet.remove("never-there");
    EXPECT_EQ(doc.commits, 1) << "nothing was taken away, so there is nothing to save";
}

// ---------------------------------------------------------------------------------------------
// The document's keys, as a declaration. Before this, "which module may write which key" was a
// fact you had to read KnownStore::save() and StatsStore::writeBack() to learn.
// ---------------------------------------------------------------------------------------------

namespace {

/// @brief A top-level key the document carries, and the module that writes it.
struct DocumentKey {
    const char* key;
    /// The module that writes it. Two names means two writers, which is a question this table
    /// makes visible rather than answers: see docs/adr/0024, "后果".
    const char* writer;
};

/// @brief A nested object the document carries, and the module that writes it.
struct DocumentObject {
    const char* name;
    const char* writer;
};

/// @brief A key a strong-typed owner re-stamps from its own state, so a section must not write it.
struct StampedKey {
    const char* key;
    const char* owner;
};

const DocumentKey kScalarKeys[] = {
    {"API-KEY", "AppController"},
    {"MODEL", "ExplanationDuty + AppController"},
    {"URL", "AppController + ExplanationDuty"},
    {"PROVIDER", "ExplanationDuty"},
    {"multiSense", "AppController"},
    {"etymology", "AppController"},
    {"example", "AppController"},
    {"theme", "AppController"},
    {"animationsEnabled", "AppController"},
    {"uiLanguage", "AppController"},
    {"selectionCapture", "AppController"},
    {"clipboardPolicy", "AppController"},
    {"bubbleDismissSeconds", "AppController"},
    {"bubblePlacement", "AppController"},
    {"ocrHotkey", "AppController"},
    {"ocrCapture", "CaptureDuty"},
    {"autoScan", "CaptureDuty"},
    {"minimumWordLength", "CaptureDuty"},
    {"scanWhitelist", "CaptureDuty"},
    {"tesseractExecutable", "CaptureDuty"},
    {"tesseractDataDirectory", "CaptureDuty"},
};

const DocumentObject kObjects[] = {
    {"UPDATE", "UpdateDuty"},
    {"PET", "PetStore"},
    {"MODELS", "ExplanationDuty"},
};

const StampedKey kStampedKeys[] = {
    {"level", "KnownStore"},
    {"explanationLang", "KnownStore"},
    {"known", "KnownStore"},
    {"cache", "KnownStore"},
    {"history", "StatsStore"},
    {"daily", "StatsStore"},
    {"dailyBudget", "StatsStore"},
};

/// @return Whether the tables name @p key at the top level.
bool named(const std::string& key)
{
    for (const DocumentKey& entry : kScalarKeys)
        if (key == entry.key)
            return true;
    for (const DocumentObject& entry : kObjects)
        if (key == entry.name)
            return true;
    for (const StampedKey& entry : kStampedKeys)
        if (key == entry.key)
            return true;
    return false;
}

} // namespace

/// A scalar written through the section comes back after a save and a load. This is the half the
/// section is responsible for; the seven below are the half it is not.
TEST(SettingsDocumentKeys, EveryScalarKeySurvivesBeingWrittenThroughTheSection)
{
    const auto path  = scratchPath("lens-settings-section-scalars.json");
    KnownStore store = KnownStore::load(path);

    for (const DocumentKey& entry : kScalarKeys)
        store.section().set<std::string>(entry.key, "written-by-the-test");

    KnownStore reloaded = KnownStore::load(path);
    for (const DocumentKey& entry : kScalarKeys)
        EXPECT_EQ(reloaded.section().value<std::string>(entry.key, ""), "written-by-the-test")
            << entry.key << " is written by " << entry.writer;

    std::filesystem::remove(path);
}

TEST(SettingsDocumentKeys, EveryNestedObjectSurvivesBeingWrittenThroughItsSection)
{
    const auto path  = scratchPath("lens-settings-section-objects.json");
    KnownStore store = KnownStore::load(path);

    for (const DocumentObject& entry : kObjects) {
        store.section(entry.name).set<std::string>("sentinel", "untouched");
        store.section(entry.name).object("probe").set<std::string>("x", "written-by-the-test");
    }

    KnownStore reloaded = KnownStore::load(path);
    for (const DocumentObject& entry : kObjects) {
        EXPECT_EQ(reloaded.section(entry.name).object("probe").value<std::string>("x", ""), "written-by-the-test")
            << entry.name << " is written by " << entry.writer;
        // Writing one key of an object leaves its siblings where they were.
        EXPECT_EQ(reloaded.section(entry.name).value<std::string>("sentinel", ""), "untouched") << entry.name;
    }

    std::filesystem::remove(path);
}

/// The store's own four keys are rebuilt from its typed members on every save, so what a section
/// puts under one of them never reaches the file. save() stamps them over its own copy of the
/// document, which is why this is asserted after a reload rather than in memory.
TEST(SettingsDocumentKeys, TheStoreRestampsItsFourKeysAtSaveTime)
{
    const auto path  = scratchPath("lens-settings-section-stamped-store.json");
    KnownStore store = KnownStore::load(path);

    // set() commits, and the commit is save(), so each of these is on disk and already wrong.
    store.section().set<int>("level", 7);
    store.section().set<std::string>("explanationLang", "es");
    store.document()["known"]["ghost-lemma"] = true;
    store.document()["cache"]["ghost-lang"]  = json::object();
    store.save();

    KnownStore reloaded = KnownStore::load(path);
    EXPECT_EQ(reloaded.document().at("level"), 2) << "level_ is what save() writes, not what was put there";
    EXPECT_EQ(reloaded.document().at("explanationLang"), "en");
    EXPECT_FALSE(reloaded.document().at("known").contains("ghost-lemma"));
    EXPECT_FALSE(reloaded.document().at("cache").contains("ghost-lang"));

    std::filesystem::remove(path);
}

/// The stats section's three are rebuilt by its own write-back, so a direct write survives a
/// save() and is lost the next time the reader's statistics move -- which is the worse of the two
/// because it is not even immediate.
TEST(SettingsDocumentKeys, TheStatsStoreRestampsItsThreeOnItsNextWrite)
{
    const auto path  = scratchPath("lens-settings-section-stamped-stats.json");
    KnownStore store = KnownStore::load(path);
    StatsStore stats{store.document()};

    store.section().set<double>("dailyBudget", 99.0);
    EXPECT_DOUBLE_EQ(store.document().at("dailyBudget"), 99.0) << "save() leaves the stats keys alone";

    store.document()["history"] = json::array({json{{"word", "ghost"}, {"time", "2026-01-01 00:00"}, {"verdict", "new"}}});
    store.document()["daily"]   = json{{"2026-01-01", {{"pops", 99}}}};
    ASSERT_TRUE(stats.setDailyBudget(1.0));

    EXPECT_DOUBLE_EQ(store.document().at("dailyBudget"), 1.0) << "the owner's setter is the only way in";
    EXPECT_TRUE(store.document().at("history").empty()) << "writeBack() rebuilds history from its own list";
    EXPECT_TRUE(store.document().at("daily").empty()) << "and the day tallies with it";

    std::filesystem::remove(path);
}

/// The other direction: a top-level key the fixture carries that the tables do not name is drift.
/// @note A narrow check, and the name is not to be read as more than it says. The fixture is the
///       qtest one: eight keys, none of them a setting a duty writes, so what this catches is a key
///       someone hand-added there while trying a setting out. A key added to the code and left out
///       of the tables is invisible to it, and so is a key that was deleted while its entry stayed.
///       Catching those needs the whole application to run, which is not this suite. This is the
///       half that stays uncovered.
TEST(SettingsDocumentKeys, TheShippedFixtureCarriesNoKeyTheTablesDoNotName)
{
    const auto    fixture = std::filesystem::path{LENS_SOURCE_DIR} / "test" / "qtest" / "surfaces" / "settings.json";
    std::ifstream in(fixture);
    ASSERT_TRUE(in) << "the qtest fixture is the other half of this check: " << fixture.string();

    const json document = json::parse(in, nullptr, false);
    ASSERT_TRUE(document.is_object());
    for (auto entry = document.begin(); entry != document.end(); ++entry)
        EXPECT_TRUE(named(entry.key())) << "the document carries a key the tables do not name: " << entry.key();
}
