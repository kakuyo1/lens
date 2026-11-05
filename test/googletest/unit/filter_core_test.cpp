/**
 * @file filter_core_test.cpp
 * @brief FilterCore and KnownStore, offline: no network, no API key, CI-safe.
 *
 * The corpus half replays test/eval_corpus.json.
 * Change behaviour by changing the corpus first; touch src/ once this goes red.
 */

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/domain/filter_core.h"
#include "core/document/known_store.h"
#include "support.h"

namespace {

using lens::core::Candidate;
using lens::core::Difficulty;
using lens::core::KnownStore;

// TEST_F pastes the fixture name into a class definition, so it has to be unqualified.
using lens::test::CoreTest;

/// @return Just the surface forms, which is what the corpus asserts on.
std::vector<std::string> surfaces(const std::vector<Candidate>& candidates)
{
    std::vector<std::string> out;
    out.reserve(candidates.size());
    for (const auto& candidate : candidates)
        out.push_back(candidate.surface);
    return out;
}

/// @return Just the lemmas, for the corpus entries that spell out an expected reduction.
std::vector<std::string> lemmas(const std::vector<Candidate>& candidates)
{
    std::vector<std::string> out;
    out.reserve(candidates.size());
    for (const auto& candidate : candidates)
        out.push_back(candidate.lemma);
    return out;
}

nlohmann::json readCorpus()
{
    std::ifstream in(lens::test::sourceDir() / "test" / "eval_corpus.json");
    EXPECT_TRUE(in) << "cannot open the corpus";
    if (!in) return {};

    nlohmann::json corpus = nlohmann::json::parse(in, nullptr, false);
    EXPECT_FALSE(corpus.is_discarded()) << "the corpus is not valid JSON";
    EXPECT_TRUE(corpus.is_array()) << "the corpus is not a JSON array";
    return corpus.is_array() ? corpus : nlohmann::json{};
}

/// @brief A KnownStore test on a fresh temp file, seeded with keys the store does not own.
struct KnownStoreTest : ::testing::Test {
protected:
    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() / "lens_known_store_test.json";
        std::filesystem::remove(path);

        // Unrelated keys, so a test can prove save() carries through what it never read.
        std::ofstream out(path);
        out << R"({"API-KEY":"sk-selftest","URL":"https://example.invalid"})";
    }

    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    /// @brief Mark a known and a new word, move off the defaults, cache one entry, persist.
    void writeSeededStore()
    {
        auto store = KnownStore::load(path);
        store.mark("ubiquitous", true);
        store.mark("resilience", false);
        store.setLevel(4);
        store.setExplanationLang("zh");
        // A real Chinese definition, kept non-ASCII on purpose: this is what proves the JSON
        // round trip survives UTF-8, which an ASCII stand-in would not. The IPA stands in for
        // the same reason: a bubble has to be able to show the pronunciation from the cache.
        store.cachePut("ubiquitous", {"/juːˈbɪkwɪtəs/", "existing everywhere", "无处不在的"});
        store.save();
    }

    std::filesystem::path path;
};

/// @return The state @p word gets when selected on its own at @p level is Mastered.
bool masteredAt(const char* word, int level)
{
    const auto words = lens::core::filterWords(word, {}, lens::core::difficultyForLevel(level));
    return !words.empty() && words.front().state == lens::core::CandidateState::Mastered;
}

/// A word and the levels (0..7, UI-SPEC.md 4.4 order) that must master it.
struct LevelRow {
    const char*      word;
    std::vector<int> levels;
};

/// @brief Expect each word to be Mastered at exactly the levels its row lists, across all eight levels.
void expectMasteredAtLevels(const std::vector<LevelRow>& rows)
{
    for (int level = 0; level < 8; ++level)
        for (const auto& row : rows) {
            const bool want = std::find(row.levels.begin(), row.levels.end(), level) != row.levels.end();
            EXPECT_EQ(masteredAt(row.word, level), want) << row.word << " at level " << level;
        }
}

/// @brief Tests that load the small books under test/fixtures/wordbooks, and put the real ones back after.
struct WordbookTest : CoreTest {
protected:
    void SetUp() override
    {
        CoreTest::SetUp();
        lens::core::loadWordbooks(lens::test::sourceDir() / "test" / "fixtures" / "wordbooks");
    }

    void TearDown() override
    {
        lens::core::loadWordbooks(lens::test::sourceDir() / "data" / "wordbooks");
    }
};

} // namespace

TEST_F(CoreTest, ReplaysTheSampleCorpus)
{
    const nlohmann::json corpus = readCorpus();
    ASSERT_FALSE(corpus.empty());
    ASSERT_TRUE(corpus.is_array());

    std::size_t mismatched = 0;
    std::size_t index      = 0;
    for (const auto& item : corpus) {
        ++index;
        SCOPED_TRACE("corpus #" + std::to_string(index) + " " + item.value("note", std::string()));

        const std::string text        = item.at("text").get<std::string>();
        const auto        expect      = item.at("expect").get<std::vector<std::string>>();
        const std::size_t minFreqRank = item.value("minFreqRank", std::size_t{0});
        const std::string expectKind  = item.value("expectKind", std::string());

        std::unordered_set<std::string> known;
        for (const auto& word : item.value("known", std::vector<std::string>{}))
            known.insert(word);

        // The kind comes from classifySelection: the shape of the selection decides the channel.
        // The candidate list comes from filterWords directly, because a sentence now rides the
        // sentence channel while still exercising the word pipeline that fills `expect`.
        lens::core::Selection got;
        ASSERT_NO_THROW(got = lens::core::classifySelection(text, known, Difficulty::byRank(minFreqRank)))
            << "text: " << text;
        const std::string kind = [&] {
            switch (got.kind) {
                case lens::core::SelectionKind::Word: return "Word";
                case lens::core::SelectionKind::Entity: return "Entity";
                case lens::core::SelectionKind::Translation: return "Translation";
                case lens::core::SelectionKind::Sentence: break;
            }
            return "Sentence";
        }();
        EXPECT_EQ(kind, expectKind) << "text: " << text;
        bool ok = kind == expectKind;

        if (expectKind == "Entity" or expectKind == "Translation") {
            // An entity phrase is not a word candidate list, and a translation never has one: both
            // channels take the selection whole, so filterWords is not their contract.
            if (!ok) ++mismatched;
            continue;
        }

        std::vector<Candidate> words;
        ASSERT_NO_THROW(words = lens::core::filterWords(text, known, Difficulty::byRank(minFreqRank))) << "text: " << text;
        EXPECT_EQ(surfaces(words), expect) << "text: " << text;
        ok = ok && surfaces(words) == expect;
        if (item.contains("expectLemmas")) {
            const auto wantLemmas = item.at("expectLemmas").get<std::vector<std::string>>();
            EXPECT_EQ(lemmas(words), wantLemmas) << "text: " << text;
            ok = ok && lemmas(words) == wantLemmas;
        }
        if (!ok) ++mismatched;
    }

    // Also the measurement: how many excerpts the pipeline gets wrong. Printed because a red
    // run is otherwise a wall of failures with no total, and docs/metrics reads this line.
    std::cout << "\ncorpus: " << corpus.size() << " entries, " << mismatched << " mismatched\n";
}

TEST_F(CoreTest, KeepsAFormThatWordNetAlsoListsAsItsOwnLemma)
{
    // adj.exc carries both "offer offer" and "offer off". The identity wins in that file, so the noun/verb a
    // reader selects is not explained as the comparative of off; a mapping from another part of speech still
    // stands, which is what keeps "is be" and "testes testis" working.
    EXPECT_EQ(lens::core::lemmatize("offer"), "offer");
    EXPECT_EQ(lens::core::lemmatize("offered"), "offer");
    EXPECT_EQ(lens::core::lemmatize("is"), "be");
    EXPECT_EQ(lens::core::lemmatize("testes"), "testis");
}

TEST_F(CoreTest, RoutesASentenceToTheSentenceChannelButStillFiltersItsWords)
{
    constexpr const char* text = "The ubiquitous nature of modern software makes resilience essential.";

    // The channel is the shape of the selection: a sentence is a sentence, so the action bar
    // translates or explains it whole instead of explaining one word.
    EXPECT_EQ(lens::core::classifySelection(text, {}, Difficulty::byRank(3000)).kind, lens::core::SelectionKind::Sentence);

    // filterWords still produces the candidate list the corpus pins, in first-appearance order,
    // with the in-band article marked rather than dropped (TODO.md item 0) -- which is why the
    // list opens with the article.
    const auto words = lens::core::filterWords(text, {}, Difficulty::byRank(3000));
    ASSERT_FALSE(words.empty());
    EXPECT_EQ(words.front().surface, "the");
    EXPECT_EQ(words.front().state, lens::core::CandidateState::Mastered);

    // The candidate still fresh to the reader is what a word request would carry; a regression
    // that reordered the list shows up here rather than in a bubble naming the wrong word.
    const auto fresh = std::find_if(words.begin(), words.end(), [](const Candidate& candidate) {
        return candidate.state == lens::core::CandidateState::New;
    });
    ASSERT_NE(fresh, words.end());
    EXPECT_EQ(fresh->surface, "ubiquitous");
}

TEST_F(CoreTest, RoutesASelectionHoldingHanToTheTranslationChannelWhateverItsShape)
{
    // One character is a word in Chinese, two is the common size of one, and a whole passage is a
    // sentence: all of them are the same request, because the channel follows the language rather
    // than the shape (API.md section 1). The candidates never run -- the Latin word inside the
    // mixed excerpt is not a lookup, it is part of the text being translated.
    for (const char* text : {"猫", "苹果", "中文文本", "日本語", "混合mixed文本", "这一段话里有好几个词。"}) {
        SCOPED_TRACE(text);
        const auto selection = lens::core::classifySelection(text, {}, Difficulty::byRank(3000));
        EXPECT_EQ(selection.kind, lens::core::SelectionKind::Translation);
        EXPECT_TRUE(selection.candidates.empty());
    }

    // Nothing that is not Han turns into one: a label the reader picked is a label, Hangul is not
    // Han at all, and a run of full-width punctuation has no letter in it to read.
    for (const char* text : {"한국어", "。", "！？"}) {
        SCOPED_TRACE(text);
        EXPECT_EQ(lens::core::classifySelection(text, {}, Difficulty::byRank(3000)).kind, lens::core::SelectionKind::Sentence);
    }

    // The English shapes are untouched, Han or no Han.
    EXPECT_EQ(lens::core::classifySelection("resilience", {}, Difficulty::byRank(3000)).kind, lens::core::SelectionKind::Word);
    EXPECT_EQ(lens::core::classifySelection("Kubernetes", {}, Difficulty::byRank(3000)).kind, lens::core::SelectionKind::Entity);
    EXPECT_EQ(lens::core::classifySelection("New York", {}, Difficulty::byRank(0)).kind, lens::core::SelectionKind::Entity);
}

TEST_F(CoreTest, RoutesALoneTokenByItsPresenceInTheWordList)
{
    // A lone token the dictionary knows is a word to look up, whatever its case.
    const struct {
        const char* text;
        const char* surface;
    } words[] = {{"am", "am"}, {"NASA", "nasa"}, {"the", "the"}, {"resilience", "resilience"}};
    for (const auto& item : words) {
        SCOPED_TRACE(item.text);
        const auto selection = lens::core::classifySelection(item.text, {}, Difficulty::byRank(3000));
        EXPECT_EQ(selection.kind, lens::core::SelectionKind::Word);
        ASSERT_EQ(selection.candidates.size(), 1u);
        EXPECT_EQ(selection.candidates.front().surface, item.surface);
    }

    // A lone token the dictionary does not know is a name, not a word: QML, Kubernetes.
    for (const char* text : {"QML", "qml", "kubernetes"}) {
        SCOPED_TRACE(text);
        const auto selection = lens::core::classifySelection(text, {}, Difficulty::byRank(3000));
        EXPECT_EQ(selection.kind, lens::core::SelectionKind::Entity);
        EXPECT_TRUE(selection.candidates.empty());
    }

    // A lone dictionary word still reduces to its lemma.
    const auto reduced = lens::core::classifySelection("RUNNING", {}, Difficulty::byRank(0));
    EXPECT_EQ(reduced.kind, lens::core::SelectionKind::Word);
    ASSERT_EQ(reduced.candidates.size(), 1u);
    EXPECT_EQ(reduced.candidates.front().lemma, "run");

    // One letter is under the floor, and a digit inside disqualifies (MP3).
    EXPECT_EQ(lens::core::classifySelection("a", {}, Difficulty::byRank(3000)).kind, lens::core::SelectionKind::Sentence);
    EXPECT_EQ(lens::core::classifySelection("MP3", {}, Difficulty::byRank(3000)).kind, lens::core::SelectionKind::Sentence);

    // The same term inside continuous prose makes the selection a sentence, and the gates still
    // keep it out of the candidate list.
    EXPECT_EQ(lens::core::classifySelection("the qml source", {}, Difficulty::byRank(3000)).kind, lens::core::SelectionKind::Sentence);
    for (const auto& candidate : lens::core::filterWords("the qml source", {}, Difficulty::byRank(3000)))
        EXPECT_NE(candidate.surface, "qml") << "a lone-selection token inside prose must stay skipped";
}

TEST_F(CoreTest, ClassifiesJunkAsASentence)
{
    // Glued junk and a stray number: nothing in it survives the filter, so the word channel
    // has nothing to send.
    EXPECT_EQ(lens::core::classifySelection("https://example.com/k8s report_final.txt 42 version2", {}, Difficulty::byRank(0)).kind,
              lens::core::SelectionKind::Sentence);
    // Nothing was selected at all.
    EXPECT_EQ(lens::core::classifySelection("", {}, Difficulty::byRank(0)).kind, lens::core::SelectionKind::Sentence);
    // A lone word the reader already knows is still a word: the mark annotates the candidate,
    // it does not take it away, because an explicit selection is a request to explain it --
    // the reader cannot be assumed to remember a word they marked (TODO.md item 0).
    const auto known = lens::core::classifySelection("ubiquitous", {"ubiquitous"}, Difficulty::byRank(3000));
    EXPECT_EQ(known.kind, lens::core::SelectionKind::Word);
    ASSERT_EQ(known.candidates.size(), 1u);
    EXPECT_EQ(known.candidates.front().state, lens::core::CandidateState::Known);
    // A lone word inside the level's band is the same story from the other side.
    const auto mastered = lens::core::classifySelection("nature", {}, Difficulty::byRank(3000));
    EXPECT_EQ(mastered.kind, lens::core::SelectionKind::Word);
    ASSERT_EQ(mastered.candidates.size(), 1u);
    EXPECT_EQ(mastered.candidates.front().state, lens::core::CandidateState::Mastered);
}

TEST_F(CoreTest, ClassifiesTitleCaseAndAcronymPhrasesAsEntities)
{
    const auto entity = lens::core::classifySelection("New York", {}, Difficulty::byRank(0));
    EXPECT_EQ(entity.kind, lens::core::SelectionKind::Entity);
    EXPECT_TRUE(entity.candidates.empty());

    const auto anotherEntity = lens::core::classifySelection("United States", {}, Difficulty::byRank(0));
    EXPECT_EQ(anotherEntity.kind, lens::core::SelectionKind::Entity);

    // An all-caps acronym phrase is a name too: QML API is an entity, not a two-word sentence.
    const auto acronym = lens::core::classifySelection("QML API", {}, Difficulty::byRank(0));
    EXPECT_EQ(acronym.kind, lens::core::SelectionKind::Entity);
    EXPECT_TRUE(acronym.candidates.empty());
    EXPECT_EQ(lens::core::classifySelection("the QML API", {}, Difficulty::byRank(0)).kind, lens::core::SelectionKind::Entity);

    // A lowercase word that is not a connector keeps it a sentence.
    EXPECT_EQ(lens::core::classifySelection("QML API server", {}, Difficulty::byRank(0)).kind, lens::core::SelectionKind::Sentence);

    const auto singleToken = lens::core::classifySelection("London", {}, Difficulty::byRank(0));
    EXPECT_NE(singleToken.kind, lens::core::SelectionKind::Entity);

    const auto mixedSentence = lens::core::classifySelection("I visited New York", {}, Difficulty::byRank(0));
    EXPECT_NE(mixedSentence.kind, lens::core::SelectionKind::Entity);
}

TEST_F(CoreTest, DefinitionLinksOfferWordsAboveTheBandAndUnlistedOnes)
{
    // "knack" ranks far above the cut-off; "a", "for" and "others" sit inside it and stay plain.
    const auto above = lens::core::definitionLinks("a knack for noticing what others miss", Difficulty::byRank(4000));
    ASSERT_EQ(above.size(), 1u);
    EXPECT_EQ(above.front().lemma, "knack");
    EXPECT_EQ(above.front().start, 2u);
    EXPECT_EQ(above.front().end, 7u);

    // Offsets are bytes: the em dash is three of them. The word is not in the list, so it is offered.
    const auto unlisted = lens::core::definitionLinks("\xe2\x80\x94 tintinnabulate", Difficulty::byRank(4000));
    ASSERT_EQ(unlisted.size(), 1u);
    EXPECT_EQ(unlisted.front().lemma, "tintinnabulate");
    EXPECT_EQ(unlisted.front().start, 4u);
    EXPECT_EQ(unlisted.front().end, 18u);

    // The cut-off is inclusive: a word ranked exactly at it is inside the band. "miss" is its own lemma (rank 274).
    EXPECT_TRUE(lens::core::definitionLinks("miss", Difficulty::byRank(274)).empty());
    EXPECT_EQ(lens::core::definitionLinks("miss", Difficulty::byRank(273)).size(), 1u);
}

TEST_F(CoreTest, DifficultyForLevelClampsToTheTable)
{
    EXPECT_EQ(lens::core::difficultyForLevel(-3).bookMask, lens::core::difficultyForLevel(0).bookMask);
    EXPECT_EQ(lens::core::difficultyForLevel(99).bookMask, lens::core::difficultyForLevel(7).bookMask);
    EXPECT_EQ(lens::core::difficultyForLevel(2).floorRank, 3000u);
}

TEST_F(WordbookTest, EachLevelMastersItsOwnBooksTheBaseAndTheFloor)
{
    // The fixture books hold one word each. "time" is inside the frequency floor, so every level masters it;
    // "garden" is in the base book every level takes. The toefl book lists "pragmatics", whose lemma is
    // "pragmatic": the row passes only if books are keyed by lemma.
    expectMasteredAtLevels({
        {"time", {0, 1, 2, 3, 4, 5, 6, 7}},
        {"garden", {0, 1, 2, 3, 4, 5, 6, 7}},
        {"vivid", {1}},            // c1-c2
        {"chamber", {2, 3, 6, 7}}, // cet4, which CET-6, IELTS and TOEFL include
        {"meticulous", {3}},       // cet6
        {"ambiguous", {4, 5}},     // tem4, which TEM-8 includes
        {"tenacious", {5}},        // tem8
        {"eloquent", {6}},         // ielts
        {"pragmatic", {7}},        // toefl
    });
}

TEST_F(WordbookTest, KnownOutranksAWordTheLevelMasters)
{
    const auto words = lens::core::filterWords("journey", {"journey"}, lens::core::difficultyForLevel(2));
    ASSERT_EQ(words.size(), 1u);
    EXPECT_EQ(words.front().state, lens::core::CandidateState::Known);
}

TEST_F(WordbookTest, AMissingWordbookLeavesOnlyTheFloor)
{
    // A directory with no books is the worst case of missing files: each one warns, nothing is listed, and startup goes on.
    lens::core::loadWordbooks(lens::test::sourceDir() / "test" / "fixtures" / "no-wordbooks");
    EXPECT_FALSE(masteredAt("chamber", 2));
    EXPECT_FALSE(masteredAt("vivid", 1));
    EXPECT_TRUE(masteredAt("time", 2)) << "the frequency floor does not depend on any book";
}

TEST_F(WordbookTest, DefinitionLinksLeaveOutTheWordsTheLevelMasters)
{
    const auto difficulty = lens::core::difficultyForLevel(2);
    EXPECT_TRUE(lens::core::definitionLinks("journey garden", difficulty).empty());

    const auto links = lens::core::definitionLinks("ambiguous", difficulty);
    ASSERT_EQ(links.size(), 1u);
    EXPECT_EQ(links.front().lemma, "ambiguous");
}

TEST_F(CoreTest, RealWordbooksMasterEachLevelsOwnBooks)
{
    lens::core::loadWordbooks(lens::test::sourceDir() / "data" / "wordbooks");
    // Each sample is above the frequency floor and sits only in the books named, so those books alone can master it.
    expectMasteredAtLevels({
        {"garden", {0, 1, 2, 3, 4, 5, 6, 7}}, // b1-b2, the base
        {"aberration", {1}},                  // c1-c2
        {"auto", {2, 3, 6, 7}},               // cet4, which CET-6, IELTS and TOEFL include
        {"ambient", {3}},                     // cet6
        {"acknowledgement", {4, 5}},          // tem4, which TEM-8 includes
        {"abominable", {5}},                  // tem8
        {"abortion", {6}},                    // ielts
        {"abandonment", {7}},                 // toefl
    });
}

TEST_F(CoreTest, InflectedFormsMasterTheirBaseAtEveryLevel)
{
    // The word list ranks each inflection above its base, so the lemma is the inflected form. The books list the
    // base, and B1-B2, which every level takes, lists all four.
    lens::core::loadWordbooks(lens::test::sourceDir() / "data" / "wordbooks");
    expectMasteredAtLevels({
        {"shoes", {0, 1, 2, 3, 4, 5, 6, 7}},
        {"heels", {0, 1, 2, 3, 4, 5, 6, 7}},
        {"arrived", {0, 1, 2, 3, 4, 5, 6, 7}},
        {"disappeared", {0, 1, 2, 3, 4, 5, 6, 7}},
    });
}

TEST_F(CoreTest, EverydayWordsInTheFirstThreeThousandAreMasteredAtEveryLevel)
{
    // Ranked 1518 and 1354: above the old 1000 floor, no exam book lists them, and every level should know them.
    expectMasteredAtLevels({
        {"older", {0, 1, 2, 3, 4, 5, 6, 7}},
        {"lay", {0, 1, 2, 3, 4, 5, 6, 7}},
    });
}

TEST_F(CoreTest, InformalSpellingsAreNotVocabularyOnTheScanPath)
{
    // The scan path takes a single page word with no explicit-selection exemption, so the dialect spellings drop out.
    const auto words = lens::core::filterWords("runnin the fer movin", {}, lens::core::Difficulty::byRank(0), 3, false);
    EXPECT_EQ(surfaces(words), std::vector<std::string>{"the"});
}

TEST_F(CoreTest, TheDialectFormsSeenOnTheScanDropOut)
{
    // Forms the scan log showed as candidates in the Crawdads reading session: -in verbs and dialect spellings.
    const auto words = lens::core::filterWords("mornin askin brung wif the", {}, lens::core::Difficulty::byRank(0), 3, false);
    EXPECT_EQ(surfaces(words), std::vector<std::string>{"the"});
}

TEST_F(CoreTest, CommentLinesOfTheInformalFileAreNotSpellings)
{
    // "maintained" appears only in the file's header comment; a comment line must not reach the list.
    const auto words = lens::core::filterWords("maintained the", {}, lens::core::Difficulty::byRank(0), 3, false);
    EXPECT_EQ(surfaces(words), (std::vector<std::string>{"maintained", "the"}));
}

TEST_F(CoreTest, AKnownBaseCoversItsInflectedForms)
{
    const auto words = lens::core::filterWords("shoes", {"shoe"}, lens::core::difficultyForLevel(2));
    ASSERT_EQ(words.size(), 1u);
    EXPECT_EQ(words.front().state, lens::core::CandidateState::Known);
}

TEST_F(KnownStoreTest, FreshStoreHoldsTheDocumentedDefaults)
{
    const auto store = KnownStore::load(path);
    EXPECT_FALSE(store.isKnown("ubiquitous"));
    EXPECT_EQ(store.level(), 2); // CET-4
    EXPECT_EQ(store.explanationLang(), "en");
}

TEST_F(KnownStoreTest, MarkSeparatesKnownFromNewWords)
{
    auto store = KnownStore::load(path);
    store.mark("ubiquitous", true);
    store.mark("resilience", false);

    EXPECT_TRUE(store.isKnown("ubiquitous"));
    EXPECT_FALSE(store.isKnown("resilience"));
    EXPECT_EQ(store.known().count("ubiquitous"), 1);
    // A new word is still offered again, so it stays out of the set handed to filterWords.
    EXPECT_EQ(store.known().count("resilience"), 0);
}

TEST_F(KnownStoreTest, ChangingTheLevelLeavesTheKnownSetAlone)
{
    auto store = KnownStore::load(path);
    store.mark("ubiquitous", true);
    const auto before = store.known();
    store.setLevel(6);
    EXPECT_EQ(store.known(), before);
}

TEST_F(KnownStoreTest, RejectsALevelOutsideTheRange)
{
    auto store = KnownStore::load(path);
    EXPECT_THROW(store.setLevel(99), std::out_of_range);
}

TEST_F(KnownStoreTest, SurvivesASaveAndReload)
{
    writeSeededStore();

    const auto store = KnownStore::load(path);
    EXPECT_TRUE(store.isKnown("ubiquitous"));
    EXPECT_FALSE(store.isKnown("resilience"));
    EXPECT_EQ(store.level(), 4);
    EXPECT_EQ(store.explanationLang(), "zh");

    const auto hit = store.cacheGet("ubiquitous");
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->ipa, "/juːˈbɪkwɪtəs/") << "the pronunciation did not survive the round trip";
    EXPECT_EQ(hit->en, "existing everywhere");
    EXPECT_EQ(hit->zh, "无处不在的");
}

TEST_F(KnownStoreTest, ChainLabelAndCollocationsSurviveASaveAndReload)
{
    writeSeededStore();

    auto store = KnownStore::load(path);
    store.cachePut("ubiquitous", {.ipa = "/juːˈbɪkwɪtəs/", .en = "existing everywhere", .zh = "无处不在的", .signpost = "found everywhere", .collocations = {"~ presence", "~ in cities"}}, {.language = "en"});
    store.save();

    const auto hit = KnownStore::load(path).cacheGet("ubiquitous", {.language = "en"});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->signpost, "found everywhere");
    EXPECT_EQ(hit->collocations, (std::vector<std::string>{"~ presence", "~ in cities"}));
}

/// The picture advice, stored with the sense it came with: both copies survive the round trip, and
/// an entry that never carried them -- the ones written before the fields existed -- reads as
/// "no picture" rather than as damaged.
TEST_F(KnownStoreTest, PictureAdviceSurvivesASaveAndReloadAndAbsentIsNoPicture)
{
    writeSeededStore();

    auto store = KnownStore::load(path);
    store.cachePut("trowel",
                   {.ipa        = "/ˈtraʊəl/",
                    .en         = "a small hand tool",
                    .zh         = "抹刀",
                    .senses     = {{"a small hand tool", "抹刀", "", true, "garden trowel"}},
                    .imageable  = true,
                    .imageQuery = "garden trowel"},
                   {.language = "en"});
    store.save();

    const auto hit = KnownStore::load(path).cacheGet("trowel", {.language = "en"});
    ASSERT_TRUE(hit.has_value());
    EXPECT_TRUE(hit->imageable);
    EXPECT_EQ(hit->imageQuery, "garden trowel");
    ASSERT_EQ(hit->senses.size(), 1);
    EXPECT_TRUE(hit->senses.front().imageable);
    EXPECT_EQ(hit->senses.front().imageQuery, "garden trowel");

    // The seeded entry predates the fields, and is still a usable explanation.
    const auto old = KnownStore::load(path).cacheGet("ubiquitous", {.language = "zh"});
    ASSERT_TRUE(old.has_value());
    EXPECT_FALSE(old->imageable);
    EXPECT_TRUE(old->imageQuery.empty());
}

/// A stored entry whose advice is the wrong type is still an entry: both fields are optional and
/// advisory, and throwing the definition away over them would lose far more than it saves.
TEST_F(KnownStoreTest, AMistypedStoredPictureAdviceIsIgnoredRatherThanFatal)
{
    std::ofstream(path) << R"({"cache":{"en":{"trowel":{"ipa":"","en":"a small hand tool","zh":"抹刀","imageable":"yes","imageQuery":5}}}})";

    const auto hit = KnownStore::load(path).cacheGet("trowel", {.language = "en"});
    ASSERT_TRUE(hit.has_value()) << "the definition was dropped over an advisory field";
    EXPECT_FALSE(hit->imageable);
    EXPECT_TRUE(hit->imageQuery.empty());
}

/// An entry with no ipa key reads as empty rather than as a bubble that is missing a line. That
/// miss is the migration: the next lookup asks the model and writes a complete entry, and the
/// unreadable entry goes out of the file on the save that follows. An empty ipa is a legitimate
/// entry (an acronym has none), so only the missing key is dropped.
TEST_F(KnownStoreTest, ACachedEntryFromBeforeTheIPAFieldIsNotAnEntry)
{
    std::ofstream(path) << R"({"cache":{"en":{"ubiquitous":{"en":"existing everywhere","zh":"无处不在的"}}}})";

    auto store = KnownStore::load(path);
    EXPECT_FALSE(store.cacheGet("ubiquitous").has_value());

    store.cachePut("resilience", {"/rɪˈzɪliəns/", "the capacity to recover", "恢复力"});
    store.save();

    std::ifstream in(path);
    ASSERT_TRUE(in);
    const auto doc = nlohmann::json::parse(in, nullptr, false);
    ASSERT_TRUE(doc.is_object());
    EXPECT_FALSE(doc["cache"]["en"].contains("ubiquitous")) << "the unreadable entry should not be written back";
    EXPECT_TRUE(doc["cache"]["en"].contains("resilience"));
}

TEST_F(KnownStoreTest, KeysTheCacheByExplanationLanguage)
{
    writeSeededStore();

    auto store = KnownStore::load(path);
    store.setExplanationLang("en");
    EXPECT_FALSE(store.cacheGet("ubiquitous").has_value());
}

TEST_F(KnownStoreTest, SaveWritesValidJsonAndKeepsKeysItDoesNotOwn)
{
    writeSeededStore();

    std::ifstream in(path);
    ASSERT_TRUE(in);
    const auto doc = nlohmann::json::parse(in, nullptr, false);
    ASSERT_FALSE(doc.is_discarded());
    ASSERT_TRUE(doc.is_object());
    EXPECT_EQ(doc.value("API-KEY", std::string()), "sk-selftest");
    EXPECT_EQ(doc.value("URL", std::string()), "https://example.invalid");
}