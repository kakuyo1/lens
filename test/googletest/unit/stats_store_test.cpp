/**
 * @file stats_store_test.cpp
 * @brief StatsStore, offline: the history and the daily tallies behind the statistics
 *        surfaces, the words export rendered from that history, and the one thing that makes
 *        the store work -- sharing KnownStore's document.
 *
 * The interesting failure is not arithmetic, it is ownership: two objects writing the same
 * file. The reload case here is what would catch it.
 */

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/document/known_store.h"
#include "core/document/stats_store.h"

namespace {

using lens::core::DailyUsage;
using lens::core::ExportScope;
using lens::core::HistoryEntry;
using lens::core::KnownStore;
using lens::core::StatsStore;
using lens::core::exportLemmas;

/// @brief A StatsStore bound to a fresh temp file, deleted either way.
struct StatsStoreTest : ::testing::Test {
protected:
    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() / "lens_stats_store_test.json";
        std::filesystem::remove(path);
    }

    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    std::filesystem::path path;
};

} // namespace

TEST_F(StatsStoreTest, RecordsPopsNewestFirst)
{
    auto       store = KnownStore::load(path);
    StatsStore stats(store.document());

    stats.recordPop("first", "2026-10-03 09:00", false);
    stats.recordPop("second", "2026-10-03 09:01", false);

    const auto& history = stats.history();
    ASSERT_EQ(history.size(), 2u);
    EXPECT_EQ(history[0].lemma, "second");
    EXPECT_EQ(history[1].lemma, "first");
    EXPECT_EQ(history[0].verdict, "new") << "an unmarked pop is stamped new, not left empty";
    EXPECT_EQ(stats.daily().at("2026-10-03").pops, 2);
    EXPECT_EQ(stats.daily().at("2026-10-03").fresh, 2) << "the pop books its own tally";
}

TEST_F(StatsStoreTest, VerdictAttachesToTheNewestEntryOfThatWord)
{
    auto       store = KnownStore::load(path);
    StatsStore stats(store.document());

    stats.recordPop("apple", "2026-10-03 09:00", false);
    stats.recordPop("pear", "2026-10-03 09:01", false);
    stats.recordPop("apple", "2026-10-03 09:02", false);
    stats.recordVerdict("apple", "2026-10-03 09:03", "known");

    const auto& history = stats.history();
    ASSERT_EQ(history.size(), 3u);
    EXPECT_EQ(history[0].verdict, "known") << "the newest 'apple' takes the verdict";
    EXPECT_EQ(history[1].verdict, "new") << "'pear' is a different word";
    EXPECT_EQ(history[2].verdict, "new") << "the older 'apple' keeps its own pop's stamp";

    const DailyUsage& day = stats.daily().at("2026-10-03");
    EXPECT_EQ(day.pops, 3);
    EXPECT_EQ(day.learned, 1);
    EXPECT_EQ(day.fresh, 2) << "the marked word moved out of new, it did not grow a second count";
}

/// A verdict press is a move, not a one-shot: the day's tally has to follow the word from the
/// column its pop booked into to the column the press names, never counting in both.
TEST_F(StatsStoreTest, ReMarkingAWordMovesTheRowAndTheDayCount)
{
    auto       store = KnownStore::load(path);
    StatsStore stats(store.document());

    stats.recordPop("apple", "2026-10-03 09:00", false);
    EXPECT_EQ(stats.daily().at("2026-10-03").fresh, 1) << "the pop starts the word in new";

    stats.recordVerdict("apple", "2026-10-03 09:01", "known");

    const auto& history = stats.history();
    ASSERT_EQ(history.size(), 1u);
    EXPECT_EQ(history[0].verdict, "known");
    EXPECT_EQ(stats.daily().at("2026-10-03").fresh, 0) << "the word left the new column";
    EXPECT_EQ(stats.daily().at("2026-10-03").learned, 1);

    stats.recordVerdict("apple", "2026-10-03 09:02", "new");

    EXPECT_EQ(history[0].verdict, "new") << "the stamp is what the tally follows, so the second press has to reach it";
    EXPECT_EQ(stats.daily().at("2026-10-03").learned, 0) << "the word is not known any more";
    EXPECT_EQ(stats.daily().at("2026-10-03").fresh, 1) << "one word, settled once, counted once";

    stats.recordVerdict("apple", "2026-10-03 09:03", "new"); // the pill it already carries

    EXPECT_EQ(stats.daily().at("2026-10-03").fresh, 1) << "pressing the same verdict again is not a second word";
}

TEST_F(StatsStoreTest, VerdictWithNoMatchingEntryStillMovesTheDay)
{
    auto       store = KnownStore::load(path);
    StatsStore stats(store.document());

    // The pop may have aged out of the history window; the tally still happened.
    stats.recordVerdict("gone", "2026-10-03 09:00", "new");

    EXPECT_TRUE(stats.history().empty());
    EXPECT_EQ(stats.daily().at("2026-10-03").fresh, 1);
}

TEST_F(StatsStoreTest, UsageLandsOnTheDayItWasSpent)
{
    auto       store = KnownStore::load(path);
    StatsStore stats(store.document());

    stats.recordUsage("2026-10-03 09:00", 1200, 340);
    stats.recordUsage("2026-10-04 00:01", 100, 20);

    const auto& daily = stats.daily();
    EXPECT_EQ(daily.at("2026-10-03").promptTokens, 1200);
    EXPECT_EQ(daily.at("2026-10-03").completionTokens, 340);
    EXPECT_EQ(daily.at("2026-10-04").promptTokens, 100);
    EXPECT_EQ(daily.at("2026-10-04").pops, 0) << "a usage record is not a pop";
}

TEST_F(StatsStoreTest, SurvivesASaveAndReloadThroughKnownStore)
{
    {
        auto       store = KnownStore::load(path);
        StatsStore stats(store.document());
        stats.recordPop("ubiquitous", "2026-10-03 14:20", false);
        stats.recordVerdict("ubiquitous", "2026-10-03 14:21", "new"); // the pill the pop already stamped
        stats.recordUsage("2026-10-03 14:21", 900, 120);
        store.save(); // the document owner writes everything, stats keys included
    }

    auto       reloaded = KnownStore::load(path);
    StatsStore stats(reloaded.document());

    ASSERT_EQ(stats.history().size(), 1u);
    EXPECT_EQ(stats.history()[0].lemma, "ubiquitous");
    EXPECT_EQ(stats.history()[0].minute, "2026-10-03 14:20");
    EXPECT_EQ(stats.history()[0].verdict, "new");
    EXPECT_EQ(stats.daily().at("2026-10-03").pops, 1);
    EXPECT_EQ(stats.daily().at("2026-10-03").fresh, 1);
    EXPECT_EQ(stats.daily().at("2026-10-03").promptTokens, 900);
}

TEST_F(StatsStoreTest, SaveKeepsBothStoresKeys)
{
    {
        auto store = KnownStore::load(path);
        store.mark("ubiquitous", true);
        store.setLevel(3);
        StatsStore stats(store.document());
        stats.recordPop("resilience", "2026-10-03 14:20", false);
        store.save();
    }

    auto reloaded = KnownStore::load(path);
    EXPECT_TRUE(reloaded.isKnown("ubiquitous")) << "the word mark survived the stats write";
    EXPECT_EQ(reloaded.level(), 3);
    StatsStore stats(reloaded.document());
    ASSERT_EQ(stats.history().size(), 1u);
    EXPECT_EQ(stats.history()[0].lemma, "resilience");
}

TEST_F(StatsStoreTest, RemovingWordClearsMarksCachesAndHistoryButKeepsUsage)
{
    auto store = KnownStore::load(path);
    store.mark("apple", true);
    store.cachePut("apple", {"/a/", "apple", "苹果"}, {"en", false});
    // Named rather than positional: five of the six fields are strings, and the one carrying the
    // senses is the one this case is about.
    store.cachePut("apple", {.ipa = "/a/", .en = "apple", .zh = "苹果", .senses = {{"apple", "苹果", {}}}}, {"zh", true});

    StatsStore stats(store.document());
    stats.recordPop("apple", "2026-10-03 09:00", false);
    stats.recordVerdict("apple", "2026-10-03 09:01", "known");
    stats.recordUsage("2026-10-03 09:02", 1200, 300, "deepseek-flash");
    ASSERT_TRUE(store.removeLemma("apple"));
    ASSERT_TRUE(stats.removeLemma("apple"));
    store.save();

    auto       reloaded = KnownStore::load(path);
    StatsStore reloadedStats(reloaded.document());
    EXPECT_FALSE(reloaded.isKnown("apple"));
    EXPECT_FALSE(reloaded.cacheGet("apple", {"en", false}));
    EXPECT_FALSE(reloaded.cacheGet("apple", {"zh", true}));
    EXPECT_TRUE(reloadedStats.history().empty());
    ASSERT_EQ(reloadedStats.daily().at("2026-10-03").promptTokens, 1200);
    EXPECT_EQ(reloadedStats.daily().at("2026-10-03").completionTokens, 300);
    EXPECT_EQ(reloadedStats.daily().at("2026-10-03").pops, 0);
    EXPECT_EQ(reloadedStats.daily().at("2026-10-03").learned, 0);
}

TEST_F(StatsStoreTest, DropsTheOldestEntriesPastTheCap)
{
    // Seed the document rather than recording two thousand pops: each record rewrites the
    // whole array, so building the backlog here keeps the case to one rewrite. The stored
    // order is newest first, so the array runs w1999 down to w0.
    nlohmann::json history = nlohmann::json::array();
    for (std::size_t i = StatsStore::kMaxHistory; i > 0; --i)
        history.push_back({{"word", "w" + std::to_string(i - 1)}, {"time", "2026-10-01 00:00"}, {"verdict", ""}});
    std::ofstream(path) << nlohmann::json{{"history", history}}.dump();

    auto       store = KnownStore::load(path);
    StatsStore stats(store.document());
    ASSERT_EQ(stats.history().size(), StatsStore::kMaxHistory);
    ASSERT_EQ(stats.history().front().lemma, "w1999") << "newest first";
    ASSERT_EQ(stats.history().back().lemma, "w0") << "seeded oldest last";

    stats.recordPop("newest", "2026-10-03 14:20", false);

    const auto& after = stats.history();
    ASSERT_EQ(after.size(), StatsStore::kMaxHistory);
    EXPECT_EQ(after.front().lemma, "newest");
    EXPECT_NE(after.back().lemma, "w0") << "the oldest entry fell off the end";
}

/// The export is a pure rendering of the history and the known set, so these cases build both
/// directly rather than through the store: what is being pinned is the text's shape.
TEST(WordExport, AllScopeIsEveryLemmaOnceNewestFirst)
{
    const std::unordered_set<std::string> none;
    const std::vector<HistoryEntry>       history{
              {"second", "2026-10-03 09:02", "new"},
              {"first", "2026-10-03 09:01", "known"},
              {"second", "2026-10-03 09:00", "new"}, // a repeat is one line, the same dedup the list does
    };

    // One lemma per line, newest first, ending with a newline -- the shape data/wordlist.txt
    // has, which is what the reader's importer will be reading.
    EXPECT_EQ(exportLemmas(history, ExportScope::All, none), "second\nfirst\n");
}

/// The scope follows the known set, not the stamps: a word marked known exports as known even
/// when its newest entry is a later pop, and an unmarked word exports as new.
TEST(WordExport, KnownAndNewFollowTheKnownSet)
{
    const std::unordered_set<std::string> known{"first", "second"};
    const std::vector<HistoryEntry>       history{
              {"second", "2026-10-03 09:02", "new"}, // popped again after the mark; the stamp is stale
              {"first", "2026-10-03 09:01", "known"},
              {"second", "2026-10-03 09:00", "known"},
              {"third", "2026-10-02 20:00", "new"},
    };

    EXPECT_EQ(exportLemmas(history, ExportScope::Known, known), "second\nfirst\n");
    EXPECT_EQ(exportLemmas(history, ExportScope::New, known), "third\n");
}

TEST(WordExport, NothingMatchingIsAnEmptyString)
{
    const std::unordered_set<std::string> known{"first"};
    const std::vector<HistoryEntry>       history{{"first", "2026-10-03 09:01", "known"}};
    EXPECT_TRUE(exportLemmas(history, ExportScope::New, known).empty());
    EXPECT_TRUE(exportLemmas({}, ExportScope::All, {}).empty());
}

TEST(WordExport, ALemmaIsWrittenThroughAsItIs)
{
    // The lemmas come out of the wordlist, but nothing here may reshape them: one stray
    // conversion and the reader's file carries a different word. Non-ASCII is where that shows.
    const std::unordered_set<std::string> none;
    const std::vector<HistoryEntry>       history{{"naïve", "2026-10-03 09:01", "new"}, {"café", "2026-10-03 09:00", "new"}};
    EXPECT_EQ(exportLemmas(history, ExportScope::All, none), "naïve\ncafé\n");
}

TEST_F(StatsStoreTest, IgnoresMalformedEntriesInTheDocument)
{
    const nlohmann::json doc{
        {"history",
         nlohmann::json::array({nlohmann::json{{"word", "kept"}, {"time", "2026-10-03 09:00"}},
                                nlohmann::json{{"word", "no-time"}},
                                nlohmann::json{{"time", "2026-10-03 09:00"}},
                                "not an object"})},
        {"daily", nlohmann::json{{"2026-10-03", nlohmann::json{{"pops", 4}}}, {"bad", 7}, {"2026-10-0", nlohmann::json{{"pops", 9}}}}}};
    std::ofstream(path) << doc.dump();

    auto       store = KnownStore::load(path);
    StatsStore stats(store.document());

    ASSERT_EQ(stats.history().size(), 1u);
    EXPECT_EQ(stats.history()[0].lemma, "kept");
    ASSERT_EQ(stats.daily().size(), 1u) << "only the well-formed date survives";
    EXPECT_EQ(stats.daily().at("2026-10-03").pops, 4);
    EXPECT_EQ(stats.daily().at("2026-10-03").learned, 0) << "a missing key reads as zero";
}
