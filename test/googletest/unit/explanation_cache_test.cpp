/**
 * @file explanation_cache_test.cpp
 * @brief The explanation cache module's interface: the context a request goes out under, the mask
 *        that makes the sense mode the word channel's business, and what a stored entry is worth.
 *
 * Every case here is about the module rather than about one call site: the duty and the review
 * ask the module instead of building a context and re-deriving the example rule, so these cases
 * put each rule on the table without a client, a cost duty, a pricing file or an event loop --
 * just a store on a temp file.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "core/document/explanation_cache.h"
#include "core/document/known_store.h"

namespace {

using lens::core::CacheContext;
using lens::core::ExplanationCache;
using lens::core::KnownStore;
using lens::core::SelectionKind;
using lens::core::WordCache;

constexpr auto kLanguage = "en";

/// @brief A module over a store on a fresh temp file, with the unrelated keys save() must carry.
struct ExplanationCacheTest : ::testing::Test {
protected:
    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() / "lens_explanation_cache_test.json";
        std::filesystem::remove(path);
        std::ofstream{path} << R"({"untouched":"sentinel"})";
        store = KnownStore::load(path);
        cache = std::make_unique<ExplanationCache>(*store);
    }

    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    /// @return A stored entry the reader can be served whole.
    static WordCache anEntry()
    {
        return WordCache{.ipa = "/bæŋk/", .en = "a place for money", .zh = "银行"};
    }

    std::filesystem::path             path;
    std::optional<KnownStore>         store;
    std::unique_ptr<ExplanationCache> cache;
};

/// The one spelling of a rule the duty and the queue each wrote their own copy of.
TEST_F(ExplanationCacheTest, TheSenseModeCountsOnlyOnTheWordChannel)
{
    store->document()["multiSense"] = "true";
    auto const wordContext          = cache->readerContext();
    EXPECT_TRUE(wordContext.multipleSenses);

    EXPECT_EQ(cache->contextFor(SelectionKind::Word), wordContext);
    for (auto const kind : {SelectionKind::Entity, SelectionKind::Sentence, SelectionKind::Translation})
        EXPECT_EQ(cache->contextFor(kind), (CacheContext{kLanguage, false})) << "the sense mode reached a non-word channel";

    // The same mask over a context a caller already holds: a stream preview and the queue's
    // staleness check compare contexts they captured earlier.
    EXPECT_EQ(ExplanationCache::masked({kLanguage, true}, SelectionKind::Word), (CacheContext{kLanguage, true}));
    EXPECT_FALSE(ExplanationCache::masked({kLanguage, true}, SelectionKind::Sentence).multipleSenses);
}

/// What the settings say, before any masking: the queue's "now" for telling an answer in flight
/// from the settings having moved.
TEST_F(ExplanationCacheTest, TheReaderContextFollowsTheSettings)
{
    EXPECT_EQ(cache->readerContext(), (CacheContext{kLanguage, false}));
    store->document()["multiSense"] = "true";
    EXPECT_EQ(cache->readerContext(), (CacheContext{kLanguage, true}));
    store->setExplanationLang("ja");
    EXPECT_EQ(cache->readerContext(), (CacheContext{"ja", true}));
}

/// The toggles read through the settings table's rows, not a restated key and default: a key the
/// document holds as the wrong shape, as null, or not at all answers the row's own default
/// (docs/adr/0083).
TEST_F(ExplanationCacheTest, TheTogglesReadThroughTheTableRows)
{
    auto& document         = store->document();
    document["multiSense"] = 3; // a number is not a toggle
    document["example"]    = nullptr;

    EXPECT_EQ(cache->readerContext(), (CacheContext{kLanguage, false}));
    cache->store("bank", anEntry(), {kLanguage, false});
    EXPECT_EQ(cache->look("bank", {kLanguage, false}).state, ExplanationCache::State::Hit);
}

TEST_F(ExplanationCacheTest, NothingStoredUnderTheContextIsMissing)
{
    EXPECT_EQ(cache->look("bank", {kLanguage, false}).state, ExplanationCache::State::Missing);
    // The entry under one context is not an entry under another (docs/adr/0019).
    cache->store("bank", anEntry(), {kLanguage, false});
    EXPECT_EQ(cache->look("bank", {kLanguage, true}).state, ExplanationCache::State::Missing);
    EXPECT_EQ(cache->look("bank", {"ja", false}).state, ExplanationCache::State::Missing);
    EXPECT_EQ(cache->look("river", {kLanguage, false}).state, ExplanationCache::State::Missing);
}

/// docs/adr/0033: an entry bought without asking for an example is not an answer while the setting
/// is on -- the reader is owed the sentence, and it is bought alone.
TEST_F(ExplanationCacheTest, AnEntryWithoutAnExampleIsABuyWhenTheSettingAsksForOne)
{
    cache->store("bank", anEntry(), {kLanguage, false});
    store->document()["example"] = "true";

    auto const found = cache->look("bank", {kLanguage, false});
    EXPECT_EQ(found.state, ExplanationCache::State::Rebuy);
    // Carried anyway: the hover review shows what is stored rather than spending money on it.
    EXPECT_EQ(found.entry.en, "a place for money");
}

/// The other half of the tri-state: the model was asked and had no sentence to give, which is a
/// settled answer and is never asked for again.
TEST_F(ExplanationCacheTest, AnEmptyExampleIsAnAnswerAndIsAHit)
{
    auto entry    = anEntry();
    entry.example = std::string{};
    cache->store("bank", entry, {kLanguage, false});
    store->document()["example"] = "true";

    EXPECT_EQ(cache->look("bank", {kLanguage, false}).state, ExplanationCache::State::Hit);
}

/// With the setting off nothing is owed, so whatever is stored is served whole.
TEST_F(ExplanationCacheTest, WithTheExampleSettingOffAnyStoredEntryIsAHit)
{
    cache->store("bank", anEntry(), {kLanguage, false});
    cache->store("river", anEntry(), {kLanguage, false});
    store->document()["example"] = "false";

    EXPECT_EQ(cache->look("bank", {kLanguage, false}).state, ExplanationCache::State::Hit);
    EXPECT_EQ(cache->look("river", {kLanguage, false}).state, ExplanationCache::State::Hit);
}

/// A hit is the entry itself, not a placeholder: the card is drawn from it.
TEST_F(ExplanationCacheTest, AHitCarriesTheStoredEntry)
{
    auto entry    = anEntry();
    entry.example = std::string{"The bank raised its rate."};
    entry.senses  = {{"a financial institution", "银行", "a financial institution"}};
    cache->store("bank", entry, {kLanguage, false});

    auto const found = cache->look("bank", {kLanguage, false});
    EXPECT_EQ(found.state, ExplanationCache::State::Hit);
    EXPECT_EQ(found.entry.example.value_or("<absent>"), "The bank raised its rate.");
    ASSERT_EQ(found.entry.senses.size(), 1u);
    EXPECT_EQ(found.entry.senses.front().zh, "银行");
}

/// The key's shape survives a save and a reload: a key is a value, and the document is still
/// keyed by language, then lemma, with the multiple-sense entry nested (docs/adr/0019).
TEST_F(ExplanationCacheTest, EntriesSurviveASaveAndReloadUnderTheirOwnContext)
{
    // Each context's entry carries what its own reading needs: a multiple entry has senses, and a
    // third-language entry has the translation readCache requires of it.
    auto multiple        = anEntry();
    multiple.senses      = {{"a financial institution", "银行", "a financial institution"}};
    auto japanese        = anEntry();
    japanese.translation = "銀行";
    cache->store("bank", multiple, {kLanguage, true});
    cache->store("bank", japanese, {"ja", false});
    store->save();

    auto             restored = KnownStore::load(path);
    ExplanationCache reloaded{restored};
    EXPECT_EQ(reloaded.look("bank", {kLanguage, true}).state, ExplanationCache::State::Hit);
    EXPECT_EQ(reloaded.look("bank", {"ja", false}).state, ExplanationCache::State::Hit);
    EXPECT_EQ(reloaded.look("bank", {kLanguage, false}).state, ExplanationCache::State::Missing);
    EXPECT_EQ(restored.document().at("untouched"), "sentinel");
}

}
