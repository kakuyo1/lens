/**
 * @file explanation_exchange_test.cpp
 * @brief The one explanation transaction, offline: the four doors, the cache's answer, the budget's
 *        gate, the request's shape and the batch's settlement.
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

#include "app/duties/explanation_exchange.h"
#include "core/document/known_store.h"

namespace {

using lens::app::AskOrigin;
using lens::app::ExchangeFacts;
using lens::app::ExplanationExchange;
using lens::app::PendingSelection;
using lens::core::CacheContext;
using lens::core::KnownStore;
using lens::core::SelectionKind;
using lens::core::WordCache;
using lens::llm::Explanation;

/// @brief A transaction over a store on a fresh temp file, with no client, clock or event loop.
struct ExplanationExchangeTest : ::testing::Test {
protected:
    void SetUp() override
    {
        path = std::filesystem::temp_directory_path() / "lens_explanation_exchange_test.json";
        std::filesystem::remove(path);
        std::ofstream{path} << R"({"untouched":"sentinel"})";
        store    = KnownStore::load(path);
        exchange = std::make_unique<ExplanationExchange>(*store);
    }

    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    static ExchangeFacts facts(bool mayAsk = true, bool example = false)
    {
        return ExchangeFacts{CacheContext{"en", false}, mayAsk, example};
    }

    static PendingSelection wordOf(QString const& lemma)
    {
        PendingSelection selection;
        selection.anchor  = QPoint{200, 400};
        selection.text    = lemma;
        selection.kind    = SelectionKind::Word;
        selection.surface = lemma;
        selection.lemma   = lemma;
        return selection;
    }

    static PendingSelection textOf(QString const& text, SelectionKind const kind)
    {
        PendingSelection selection;
        selection.anchor = QPoint{200, 400};
        selection.text   = text;
        selection.kind   = kind;
        return selection;
    }

    static Explanation answerOf(QString const& title, QString const& en = QStringLiteral("a place for money"))
    {
        Explanation answer;
        answer.title = title;
        answer.en    = en;
        return answer;
    }

    void storeEntry(QString const& lemma, std::optional<std::string> const& example = std::nullopt)
    {
        WordCache entry{.ipa = "/bæŋk/", .en = lemma.toStdString(), .zh = "银行"};
        entry.example = example;
        store->cachePut(lemma.toStdString(), entry, CacheContext{"en", false});
    }

    std::filesystem::path                path;
    std::optional<KnownStore>            store;
    std::unique_ptr<ExplanationExchange> exchange;
};

TEST_F(ExplanationExchangeTest, TheActionBarServesAStoredWordWithoutRequesting)
{
    storeEntry("bank");

    auto const outcome = exchange->ask(wordOf("bank"), AskOrigin::Selection, facts());
    ASSERT_EQ(outcome.kind, ExplanationExchange::Outcome::Kind::Card);
    EXPECT_EQ(outcome.card.selection.lemma, QStringLiteral("bank"));
    EXPECT_EQ(outcome.card.kind, lens::app::CardKind::Selection);
    EXPECT_FALSE(outcome.card.reveal);
    EXPECT_TRUE(outcome.card.notifyPet);
    EXPECT_EQ(outcome.popLemma, QStringLiteral("bank"));
    EXPECT_FALSE(exchange->inFlight());
    EXPECT_TRUE(exchange->card().standing());
}

TEST_F(ExplanationExchangeTest, AMissIsQueuedAndTheRequestCarriesItsKindAndContext)
{
    auto const asked = exchange->ask(wordOf("river"), AskOrigin::Selection, facts());
    EXPECT_EQ(asked.kind, ExplanationExchange::Outcome::Kind::None);
    EXPECT_FALSE(exchange->inFlight());

    auto const sent = exchange->take(facts(), false);
    ASSERT_EQ(sent.kind, ExplanationExchange::Outcome::Kind::Send);
    EXPECT_EQ(sent.request.channel, lens::llm::Channel::Word);
    EXPECT_EQ(sent.request.words, (QStringList{QStringLiteral("river")}));
    EXPECT_EQ(sent.request.language, QStringLiteral("en"));
    EXPECT_FALSE(sent.request.multipleSenses);
    EXPECT_TRUE(exchange->inFlight());

    auto const shown = exchange->settle({answerOf("river", "a river")}, facts());
    ASSERT_EQ(shown.kind, ExplanationExchange::Outcome::Kind::Card);
    EXPECT_EQ(shown.card.answer.en, QStringLiteral("a river"));
    ASSERT_TRUE(store->cacheGet("river", CacheContext{"en", false}));
    EXPECT_EQ(store->cacheGet("river", CacheContext{"en", false})->en, "a river");
    EXPECT_FALSE(exchange->inFlight());
}

TEST_F(ExplanationExchangeTest, TheBudgetDropsAReadersMissButAScanWaits)
{
    auto const asked = exchange->ask(wordOf("river"), AskOrigin::Selection, facts(/*mayAsk=*/false));
    EXPECT_EQ(asked.kind, ExplanationExchange::Outcome::Kind::None);
    EXPECT_EQ(exchange->take(facts(false), false).kind, ExplanationExchange::Outcome::Kind::None);
    EXPECT_FALSE(exchange->inFlight());

    // The word list's own work is not thrown away: it waits for a budget that may come back.
    exchange->ask(wordOf("river"), AskOrigin::Scan, facts(false));
    EXPECT_EQ(exchange->take(facts(false), false).kind, ExplanationExchange::Outcome::Kind::None);
    EXPECT_EQ(exchange->take(facts(), false).kind, ExplanationExchange::Outcome::Kind::Send);
}

TEST_F(ExplanationExchangeTest, TheScanCountsEveryHitAndShowsOnlyTheFirst)
{
    storeEntry("bank");
    storeEntry("river");

    auto const first = exchange->ask(wordOf("bank"), AskOrigin::Scan, facts());
    ASSERT_EQ(first.kind, ExplanationExchange::Outcome::Kind::Card);
    EXPECT_EQ(first.popLemma, QStringLiteral("bank"));

    // A second hit still counts, but the card the pointer already has keeps its word.
    auto const second = exchange->ask(wordOf("river"), AskOrigin::Scan, facts());
    EXPECT_EQ(second.kind, ExplanationExchange::Outcome::Kind::None);
    EXPECT_EQ(second.popLemma, QStringLiteral("river"));
    ASSERT_TRUE(exchange->card().subject());
    EXPECT_EQ(exchange->card().subject()->lemma, QStringLiteral("bank"));
}

TEST_F(ExplanationExchangeTest, TheHoverShowsStoredWorkAndBuysNothing)
{
    store->document()["example"] = "true";
    storeEntry("bank"); // bought without an example: the cache calls it a rebuy, the hover still shows it.

    auto const shown = exchange->ask(wordOf("bank"), AskOrigin::Review, facts());
    ASSERT_EQ(shown.kind, ExplanationExchange::Outcome::Kind::Card);
    EXPECT_EQ(shown.card.kind, lens::app::CardKind::Review);
    EXPECT_FALSE(shown.card.reveal);
    EXPECT_TRUE(shown.popLemma.isEmpty()) << "a hover counted as a review pop";
    EXPECT_FALSE(exchange->inFlight());

    auto const missing = exchange->ask(wordOf("missing"), AskOrigin::Review, facts());
    ASSERT_EQ(missing.kind, ExplanationExchange::Outcome::Kind::Notice);
    EXPECT_EQ(missing.noticeLemma, QStringLiteral("missing"));
    EXPECT_FALSE(exchange->inFlight());
}

TEST_F(ExplanationExchangeTest, ThePetsSignShowsOnlyAWholeStoredAnswer)
{
    storeEntry("bank");
    auto const shown = exchange->ask(wordOf("bank"), AskOrigin::ReviewAnswer, facts());
    ASSERT_EQ(shown.kind, ExplanationExchange::Outcome::Kind::Card);
    EXPECT_EQ(shown.card.kind, lens::app::CardKind::ReviewAnswer);
    EXPECT_FALSE(shown.card.notifyPet);
    EXPECT_TRUE(shown.popLemma.isEmpty());

    store->document()["example"] = "true"; // the stored entry is a rebuy now
    EXPECT_EQ(exchange->ask(wordOf("bank"), AskOrigin::ReviewAnswer, facts()).kind, ExplanationExchange::Outcome::Kind::None);
    EXPECT_EQ(exchange->ask(wordOf("missing"), AskOrigin::ReviewAnswer, facts()).kind, ExplanationExchange::Outcome::Kind::None);
}

TEST_F(ExplanationExchangeTest, AHeldRequestIsAnsweredFromTheCacheWhenTheBatchLands)
{
    exchange->ask(wordOf("bank"), AskOrigin::Selection, facts());
    ASSERT_EQ(exchange->take(facts(), false).kind, ExplanationExchange::Outcome::Kind::Send);

    // The reader asks again while the first answer is on its way: it waits.
    exchange->ask(wordOf("bank"), AskOrigin::Selection, facts());
    ASSERT_EQ(exchange->settle({answerOf("bank")}, facts()).kind, ExplanationExchange::Outcome::Kind::Card);

    // The batch just stored the word the held request wanted; it comes out of the cache, not the wire.
    auto const held = exchange->take(facts(), false);
    ASSERT_EQ(held.kind, ExplanationExchange::Outcome::Kind::Card);
    EXPECT_EQ(held.popLemma, QStringLiteral("bank"));
    EXPECT_FALSE(exchange->inFlight());
}

TEST_F(ExplanationExchangeTest, EachKindPicksItsChannelAndItsWords)
{
    struct Case {
        SelectionKind      kind;
        lens::llm::Channel channel;
        QString            text;
    };
    for (auto const& one : {Case{SelectionKind::Word, lens::llm::Channel::Word, QStringLiteral("bank")},
                            Case{SelectionKind::Entity, lens::llm::Channel::Entity, QStringLiteral("European Central Bank")},
                            Case{SelectionKind::Sentence, lens::llm::Channel::Sentence, QStringLiteral("The bank was closed.")},
                            Case{SelectionKind::Translation, lens::llm::Channel::Translation, QStringLiteral("今天天气不错。")}}) {
        auto const selection = one.kind == SelectionKind::Word ? wordOf(one.text) : textOf(one.text, one.kind);
        exchange->ask(selection, AskOrigin::Selection, facts());
        auto const sent = exchange->take(facts(), false);
        ASSERT_EQ(sent.kind, ExplanationExchange::Outcome::Kind::Send) << one.text.toStdString();
        EXPECT_EQ(sent.request.channel, one.channel) << one.text.toStdString();
        EXPECT_EQ(sent.request.words, (QStringList{one.text}));
        EXPECT_FALSE(sent.request.etymology);
        exchange->settle({}, facts());
    }
}

TEST_F(ExplanationExchangeTest, ABreakdownSentenceCarriesItsPreset)
{
    auto selection   = textOf(QStringLiteral("The bank was closed."), SelectionKind::Sentence);
    selection.preset = lens::app::kBreakdownPreset;
    exchange->ask(selection, AskOrigin::Selection, facts());
    auto const sent = exchange->take(facts(), false);
    ASSERT_EQ(sent.kind, ExplanationExchange::Outcome::Kind::Send);
    EXPECT_EQ(sent.request.channel, lens::llm::Channel::Sentence);
    EXPECT_EQ(sent.request.preset, lens::app::kBreakdownPreset);
    EXPECT_EQ(sent.request.words, (QStringList{selection.text}));
}

TEST_F(ExplanationExchangeTest, AReaderWhoMovedOnStillStoresTheAnswerButSeesNoCard)
{
    exchange->ask(wordOf("bank"), AskOrigin::Selection, facts());
    ASSERT_EQ(exchange->take(facts(), false).kind, ExplanationExchange::Outcome::Kind::Send);
    exchange->setSelection(wordOf("river"));

    auto const shown = exchange->settle({answerOf("bank")}, facts());
    EXPECT_EQ(shown.kind, ExplanationExchange::Outcome::Kind::None);
    ASSERT_TRUE(store->cacheGet("bank", CacheContext{"en", false})) << "the paid-for answer was not stored";
}

TEST_F(ExplanationExchangeTest, AChainStepGoesOntoItsStandingParentAndCountsItsPop)
{
    storeEntry("bank");
    storeEntry("river");
    ASSERT_EQ(exchange->ask(wordOf("bank"), AskOrigin::Selection, facts()).kind, ExplanationExchange::Outcome::Kind::Card);

    auto const step = exchange->card().chainStep(QStringLiteral("river"));
    ASSERT_TRUE(step);
    auto const chain = exchange->ask(*step, AskOrigin::Selection, facts());
    ASSERT_EQ(chain.kind, ExplanationExchange::Outcome::Kind::Chain);
    EXPECT_EQ(chain.selection.parent, QStringLiteral("bank"));
    EXPECT_EQ(chain.popLemma, QStringLiteral("river"));

    // A step under a card the reader has moved on from is dropped, though the word still counted.
    exchange->card().clear();
    auto const lost = exchange->ask(*step, AskOrigin::Selection, facts());
    EXPECT_EQ(lost.kind, ExplanationExchange::Outcome::Kind::None);
    EXPECT_EQ(lost.popLemma, QStringLiteral("river"));
}

TEST_F(ExplanationExchangeTest, InvalidatingAWordTakesItsCardDown)
{
    storeEntry("bank");
    ASSERT_EQ(exchange->ask(wordOf("bank"), AskOrigin::Selection, facts()).kind, ExplanationExchange::Outcome::Kind::Card);

    EXPECT_TRUE(exchange->invalidateWord("bank"));
    EXPECT_FALSE(exchange->card().standing());
}

} // namespace
