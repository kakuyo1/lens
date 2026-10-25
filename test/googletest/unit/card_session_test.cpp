/**
 * @file card_session_test.cpp
 * @brief The standing card's identity, review life, sense cycle and peek requests, without a duty.
 */

#include <gtest/gtest.h>

#include <initializer_list>
#include <utility>

#include "app/duties/card_session.h"

namespace {

using namespace lens;
using lens::app::CardKind;
using lens::app::CardSession;
using lens::app::PendingSelection;
using lens::core::CacheContext;
using lens::core::SelectionKind;

PendingSelection word(QString const& lemma)
{
    PendingSelection selection;
    selection.kind       = SelectionKind::Word;
    selection.text       = lemma;
    selection.surface    = lemma;
    selection.lemma      = lemma;
    selection.anchor     = QPoint{100, 200};
    selection.anchorSize = QSize{40, 20};
    return selection;
}

llm::Explanation answer(QString const& title, QString const& en, QString const& signpost = {}, QString const& zh = QStringLiteral("中文"))
{
    llm::Explanation explanation;
    explanation.title       = title;
    explanation.en          = en;
    explanation.zh          = zh;
    explanation.translation = QStringLiteral("translation");
    explanation.signpost    = signpost;
    return explanation;
}

llm::Explanation withSenses(std::initializer_list<llm::Sense> senses)
{
    llm::Explanation explanation;
    explanation.senses = QVector<llm::Sense>{senses};
    return explanation;
}

llm::Sense sense(QString const& en, QString const& zh, QString const& hint)
{
    return llm::Sense{en, zh, QStringLiteral("translation"), hint, false, {}};
}

llm::Usage paid()
{
    return llm::Usage{12, 9, QStringLiteral("test-model")};
}

llm::Usage unpaid()
{
    return llm::Usage{};
}

CacheContext now(std::string language = "en", bool const multipleSenses = false)
{
    return CacheContext{std::move(language), multipleSenses};
}

CardSession::PeekFacts spanishPeek(bool const mayAsk = true)
{
    CardSession::PeekFacts facts;
    facts.now    = now("en");
    facts.mayAsk = mayAsk;
    facts.mother = QStringLiteral("es");
    return facts;
}

/// @return A card raised on @p lemma whose single-sense cycle draws @p en.
void raiseCard(CardSession& session, QString const& lemma, CardKind const kind = CardKind::Selection, QString const& en = {})
{
    session.raise(word(lemma), kind, lemma);
    session.holdAnswer(now(), answer(lemma, en.isEmpty() ? QStringLiteral("first sense") : en, QStringLiteral("label")));
}

} // namespace

TEST(CardSession, TheKindSettlesWhatTheCardOffers)
{
    CardSession session;
    EXPECT_FALSE(session.raise(word(QStringLiteral("bank")), CardKind::Selection, QStringLiteral("bank")));

    auto face = session.presentation();
    EXPECT_EQ(face.kind, CardKind::Selection);
    EXPECT_TRUE(face.countsDown);
    EXPECT_TRUE(face.offersVerdicts);
    EXPECT_TRUE(face.wordCard);

    // A refresh of the same card is the same card; the view must not re-place it.
    EXPECT_TRUE(session.raise(word(QStringLiteral("bank")), CardKind::Selection, QStringLiteral("bank")));
    auto moved   = word(QStringLiteral("bank"));
    moved.anchor = QPoint{500, 600};
    EXPECT_FALSE(session.raise(moved, CardKind::Selection, QStringLiteral("bank")));

    // The word list's hover replay: the pointer's to read, no clock and no verdict.
    session.raise(word(QStringLiteral("bank")), CardKind::Review, QStringLiteral("bank"));
    face = session.presentation();
    EXPECT_FALSE(face.countsDown);
    EXPECT_FALSE(face.offersVerdicts);
    EXPECT_TRUE(face.wordCard);

    // The pet's answer: a clock, still no verdict.
    session.raise(word(QStringLiteral("bank")), CardKind::ReviewAnswer, QStringLiteral("bank"));
    face = session.presentation();
    EXPECT_TRUE(face.countsDown);
    EXPECT_FALSE(face.offersVerdicts);

    // A sentence is not a word card, and has no verdict to give.
    PendingSelection sentence;
    sentence.kind = SelectionKind::Sentence;
    sentence.text = QStringLiteral("a sentence");
    session.raise(sentence, CardKind::Selection, sentence.text);
    face = session.presentation();
    EXPECT_FALSE(face.wordCard);
    EXPECT_FALSE(face.offersVerdicts);
    EXPECT_TRUE(face.countsDown);

    session.clear();
    EXPECT_FALSE(session.standing());
    EXPECT_EQ(session.presentation().kind, CardKind::None);
}

TEST(CardSession, AReviewCardWaitsForThePointerAndOnlyAReviewCardDoes)
{
    CardSession session;
    EXPECT_EQ(session.dismissReview(), CardSession::ReviewDismissal::NotReview);

    session.raise(word(QStringLiteral("bank")), CardKind::Review, QStringLiteral("bank"));
    session.holdReviewCard(true);
    // The row's dismissal lands only after the pointer has left the card as well.
    EXPECT_EQ(session.dismissReview(), CardSession::ReviewDismissal::Wait);
    EXPECT_FALSE(session.holdReviewCard(true));
    EXPECT_TRUE(session.holdReviewCard(false));

    // The panel closing takes the card down held or not.
    session.raise(word(QStringLiteral("bank")), CardKind::Review, QStringLiteral("bank"));
    session.holdReviewCard(true);
    EXPECT_TRUE(session.dismissReviewNow());

    // A pointer that never arrived on it: the row's dismissal is immediate.
    session.raise(word(QStringLiteral("bank")), CardKind::Review, QStringLiteral("bank"));
    EXPECT_EQ(session.dismissReview(), CardSession::ReviewDismissal::Dismiss);

    // A selection card is nobody's to take down by leaving a row.
    session.raise(word(QStringLiteral("bank")), CardKind::Selection, QStringLiteral("bank"));
    EXPECT_EQ(session.dismissReview(), CardSession::ReviewDismissal::NotReview);
    EXPECT_FALSE(session.dismissReviewNow());
}

TEST(CardSession, TheCycleGrowsExhaustsAndWrapsToTheMostCommonSense)
{
    CardSession session;
    raiseCard(session, QStringLiteral("bank"), CardKind::Selection, QStringLiteral("a place for money"));

    auto cycle = session.standingCycle(now());
    ASSERT_TRUE(cycle);
    EXPECT_EQ(cycle->index, 0);
    EXPECT_TRUE(cycle->offered);
    EXPECT_EQ(cycle->sense.en, QStringLiteral("a place for money"));
    EXPECT_EQ(cycle->label, QStringLiteral("label"));
    EXPECT_TRUE(cycle->hint.isEmpty()) << "the entry offered a sense no request had paid for";

    // The click asks for the next sense, on the word channel's own list.
    auto step = session.cycleSense(now(), /*mayAsk=*/true);
    ASSERT_EQ(step.kind, CardSession::SenseStep::Kind::Ask);
    EXPECT_TRUE(step.advance);
    EXPECT_EQ(step.lemma, QStringLiteral("bank"));
    ASSERT_EQ(step.known.size(), 1);
    EXPECT_EQ(step.known.front(), QStringLiteral("- a place for money"));
    cycle = session.standingCycle(now());
    ASSERT_TRUE(cycle);
    EXPECT_TRUE(cycle->fetching);
    EXPECT_TRUE(cycle->hint.isEmpty()) << "a hint was drawn for a sense still on the wire";

    // The answer grows the cycle and the click's advance moves onto it.
    const auto grown = session.senseAnswered(QStringLiteral("bank"),
                                             withSenses({sense(QStringLiteral("the land beside a river"), QStringLiteral("河岸"), QStringLiteral("riverside"))}),
                                             paid(),
                                             now());
    EXPECT_EQ(grown.result, CardSession::SenseOutcome::Result::Grown);
    ASSERT_TRUE(grown.view);
    EXPECT_EQ(grown.view->index, 1);
    EXPECT_EQ(grown.view->sense.en, QStringLiteral("the land beside a river"));
    EXPECT_EQ(grown.view->label, QStringLiteral("riverside"));

    // A whole answer with no sense says there is nothing further; the entry turns back.
    step = session.cycleSense(now(), /*mayAsk=*/true);
    ASSERT_EQ(step.kind, CardSession::SenseStep::Kind::Ask);
    const auto exhausted = session.senseAnswered(QStringLiteral("bank"), llm::Explanation{}, paid(), now());
    EXPECT_EQ(exhausted.result, CardSession::SenseOutcome::Result::Exhausted);
    ASSERT_TRUE(exhausted.view);
    EXPECT_TRUE(exhausted.view->offered);
    EXPECT_TRUE(exhausted.view->exhausted);
    EXPECT_EQ(exhausted.view->hint, QStringLiteral("label")) << "the way back carried no label for the first sense";

    const auto wrapped = session.cycleSense(now(), /*mayAsk=*/true);
    ASSERT_EQ(wrapped.kind, CardSession::SenseStep::Kind::Swap);
    EXPECT_EQ(wrapped.view.index, 0);
    EXPECT_EQ(wrapped.view.hint, QStringLiteral("riverside"));

    // One sense and exhausted: the entry has nowhere to go and a stray press does nothing.
    raiseCard(session, QStringLiteral("solitary"), CardKind::Selection, QStringLiteral("only sense"));
    session.cycleSense(now(), /*mayAsk=*/true);
    session.senseAnswered(QStringLiteral("solitary"), llm::Explanation{}, paid(), now());
    const auto still = session.standingCycle(now());
    ASSERT_TRUE(still);
    EXPECT_FALSE(still->offered);
    EXPECT_EQ(session.cycleSense(now(), /*mayAsk=*/true).why, CardSession::SenseStep::Why::Exhausted);
}

TEST(CardSession, TheHoverBuysTheNextSenseWithoutSwitchingToIt)
{
    CardSession session;
    raiseCard(session, QStringLiteral("bank"));

    auto step = session.ensureNextSense(now(), /*mayAsk=*/true);
    ASSERT_EQ(step.kind, CardSession::SenseStep::Kind::Ask);
    EXPECT_FALSE(step.advance);

    session.senseAnswered(QStringLiteral("bank"),
                          withSenses({sense(QStringLiteral("second"), QStringLiteral("第二"), QStringLiteral("two"))}),
                          paid(),
                          now());
    auto cycle = session.standingCycle(now());
    ASSERT_TRUE(cycle);
    EXPECT_EQ(cycle->index, 0) << "a hover's answer switched the card onto the sense it bought";
    EXPECT_EQ(cycle->hint, QStringLiteral("two"));

    // The hover's word is known now; a second hover asks for nothing.
    EXPECT_EQ(session.ensureNextSense(now(), /*mayAsk=*/true).kind, CardSession::SenseStep::Kind::None);
    // The click moves onto the sense the hover already bought, without a request.
    const auto click = session.cycleSense(now(), /*mayAsk=*/true);
    ASSERT_EQ(click.kind, CardSession::SenseStep::Kind::Swap);
    EXPECT_EQ(click.view.index, 1);
}

TEST(CardSession, AnAnswerLandsUnderTheWordThatAskedForIt)
{
    CardSession session;
    raiseCard(session, QStringLiteral("alpha"), CardKind::Selection, QStringLiteral("first alpha"));
    ASSERT_EQ(session.cycleSense(now(), true).kind, CardSession::SenseStep::Kind::Ask);

    // The reader moves to beta and asks for its sense while alpha's request is still out.
    raiseCard(session, QStringLiteral("beta"), CardKind::Selection, QStringLiteral("first beta"));
    ASSERT_EQ(session.cycleSense(now(), true).kind, CardSession::SenseStep::Kind::Ask);

    // Alpha's answer arrives first: it belongs to alpha's cycle, not to the card standing.
    const auto alpha = session.senseAnswered(QStringLiteral("alpha"),
                                             withSenses({sense(QStringLiteral("second alpha"), QStringLiteral("第二甲"), QStringLiteral("alpha two"))}),
                                             paid(),
                                             now());
    EXPECT_EQ(alpha.result, CardSession::SenseOutcome::Result::Grown);
    EXPECT_FALSE(alpha.view) << "alpha's answer swapped beta's card";
    EXPECT_EQ(session.cycleSense(now(), true).why, CardSession::SenseStep::Why::Waiting)
        << "beta's request was taken away by alpha's answer";

    // Beta's own answer lands on beta's card.
    const auto beta = session.senseAnswered(QStringLiteral("beta"),
                                            withSenses({sense(QStringLiteral("second beta"), QStringLiteral("第二乙"), QStringLiteral("beta two"))}),
                                            paid(),
                                            now());
    EXPECT_EQ(beta.result, CardSession::SenseOutcome::Result::Grown);
    ASSERT_TRUE(beta.view);
    EXPECT_EQ(beta.view->index, 1);
    EXPECT_EQ(beta.view->sense.en, QStringLiteral("second beta"));

    // And alpha's cycle is not stuck in fetching: the sense it bought is one press away.
    raiseCard(session, QStringLiteral("alpha"), CardKind::Selection, QStringLiteral("first alpha"));
    const auto alphaAgain = session.cycleSense(now(), true);
    ASSERT_EQ(alphaAgain.kind, CardSession::SenseStep::Kind::Swap);
    EXPECT_EQ(alphaAgain.view.index, 1);
    EXPECT_EQ(alphaAgain.view.sense.en, QStringLiteral("second alpha"));
}

TEST(CardSession, APeekLandsUnderTheWordAndSenseItWasBoughtFor)
{
    CardSession session;
    raiseCard(session, QStringLiteral("alpha"));
    ASSERT_EQ(session.requestPeek(spanishPeek()).kind, CardSession::PeekStep::Kind::Ask);

    // The reader moves to beta and holds the globe there while alpha's request is still out.
    raiseCard(session, QStringLiteral("beta"));
    ASSERT_EQ(session.requestPeek(spanishPeek()).kind, CardSession::PeekStep::Kind::Ask);

    // Alpha's line is stored for alpha, and drawn nowhere: beta's card is the one standing.
    EXPECT_FALSE(session.peekAnswered(QStringLiteral("alpha"), QStringLiteral("la línea de alpha"), now()));
    // Beta's request is still the one out, so a second hold waits instead of buying again.
    const auto waiting = session.requestPeek(spanishPeek());
    EXPECT_EQ(waiting.kind, CardSession::PeekStep::Kind::None);
    EXPECT_EQ(waiting.why, CardSession::PeekStep::Why::InFlight);

    // Beta's own line reaches beta's card.
    const auto beta = session.peekAnswered(QStringLiteral("beta"), QStringLiteral("la línea de beta"), now());
    ASSERT_TRUE(beta);
    EXPECT_EQ(*beta, QStringLiteral("la línea de beta"));

    // Going back to alpha finds the line it paid for, with no request.
    raiseCard(session, QStringLiteral("alpha"));
    const auto alpha = session.requestPeek(spanishPeek());
    ASSERT_EQ(alpha.kind, CardSession::PeekStep::Kind::Show);
    EXPECT_EQ(alpha.line, QStringLiteral("la línea de alpha"));
}

TEST(CardSession, ThePeekFollowsTheSenseTheCardShows)
{
    CardSession session;
    raiseCard(session, QStringLiteral("bank"));
    ASSERT_EQ(session.cycleSense(now(), true).kind, CardSession::SenseStep::Kind::Ask);
    session.senseAnswered(QStringLiteral("bank"),
                          withSenses({sense(QStringLiteral("the land beside a river"), QStringLiteral("河岸"), QStringLiteral("riverside"))}),
                          paid(),
                          now());

    // The hold asks for the sense the card shows, and the wire names it.
    auto step = session.requestPeek(spanishPeek());
    ASSERT_EQ(step.kind, CardSession::PeekStep::Kind::Ask);
    EXPECT_EQ(step.senseIndex, 1);
    EXPECT_EQ(step.senseEn, QStringLiteral("the land beside a river"));
    ASSERT_TRUE(session.peekAnswered(QStringLiteral("bank"), QStringLiteral("la orilla"), now()));

    // A fresh draw restarts at the answer's own sense: the hold asks for that one, not the line
    // already bought for the second.
    raiseCard(session, QStringLiteral("bank"));
    step = session.requestPeek(spanishPeek());
    ASSERT_EQ(step.kind, CardSession::PeekStep::Kind::Ask);
    EXPECT_EQ(step.senseIndex, 0);
    EXPECT_TRUE(step.senseEn.isEmpty()) << "the first sense went out with a sense line";
    ASSERT_TRUE(session.peekAnswered(QStringLiteral("bank"), QStringLiteral("la orilla cero"), now()));

    // Back on the second sense, its line is waiting.
    ASSERT_EQ(session.cycleSense(now(), true).kind, CardSession::SenseStep::Kind::Swap);
    const auto again = session.requestPeek(spanishPeek());
    ASSERT_EQ(again.kind, CardSession::PeekStep::Kind::Show);
    EXPECT_EQ(again.line, QStringLiteral("la orilla"));
}

TEST(CardSession, AChinesePeekReadsThePairTheAnswerAlreadyCarried)
{
    CardSession session;
    session.raise(word(QStringLiteral("bank")), CardKind::Selection, QStringLiteral("bank"));
    session.holdAnswer(now(), answer(QStringLiteral("bank"), QStringLiteral("a place for money"), QStringLiteral("label"), QStringLiteral("银行")));

    auto facts   = spanishPeek();
    facts.mother = QStringLiteral("zh");
    facts.ownZh  = QStringLiteral("银行");
    auto step    = session.requestPeek(facts);
    ASSERT_EQ(step.kind, CardSession::PeekStep::Kind::Show);
    EXPECT_EQ(step.line, QStringLiteral("银行"));

    // A swapped sense's pair wins over the word's most common one.
    ASSERT_EQ(session.cycleSense(now(), true).kind, CardSession::SenseStep::Kind::Ask);
    session.senseAnswered(QStringLiteral("bank"),
                          withSenses({sense(QStringLiteral("the land beside a river"), QStringLiteral("河岸"), QStringLiteral("riverside"))}),
                          paid(),
                          now());
    step = session.requestPeek(facts);
    ASSERT_EQ(step.kind, CardSession::PeekStep::Kind::Show);
    EXPECT_EQ(step.line, QStringLiteral("河岸"));
}

TEST(CardSession, TheGlobeOnlyExistsOnAWordCardInEnglish)
{
    CardSession session;
    EXPECT_EQ(session.requestPeek(spanishPeek()).why, CardSession::PeekStep::Why::NoWordCard);

    PendingSelection sentence;
    sentence.kind = SelectionKind::Sentence;
    sentence.text = QStringLiteral("a sentence");
    session.raise(sentence, CardKind::Selection, sentence.text);
    EXPECT_EQ(session.requestPeek(spanishPeek()).why, CardSession::PeekStep::Why::NoWordCard);

    raiseCard(session, QStringLiteral("bank"));
    auto chineseExplanation = spanishPeek();
    chineseExplanation.now  = now("zh");
    EXPECT_EQ(session.requestPeek(chineseExplanation).why, CardSession::PeekStep::Why::NotEnglish);

    auto englishMother   = spanishPeek();
    englishMother.mother = QStringLiteral("en");
    EXPECT_EQ(session.requestPeek(englishMother).why, CardSession::PeekStep::Why::NoMotherTongue);

    // A held peek out of budget buys nothing and says nothing.
    EXPECT_EQ(session.requestPeek(spanishPeek(/*mayAsk=*/false)).why, CardSession::PeekStep::Why::Budget);
    // ... and nothing was recorded as in flight, so a later hold may ask.
    EXPECT_EQ(session.requestPeek(spanishPeek()).kind, CardSession::PeekStep::Kind::Ask);
}

TEST(CardSession, ARefusedSenseLeavesTheEntryUp)
{
    CardSession session;
    raiseCard(session, QStringLiteral("bank"));
    ASSERT_EQ(session.cycleSense(now(), true).kind, CardSession::SenseStep::Kind::Ask);

    const auto refused = session.senseAnswered(QStringLiteral("bank"), llm::Explanation{}, unpaid(), now());
    EXPECT_EQ(refused.result, CardSession::SenseOutcome::Result::Refused);
    EXPECT_FALSE(refused.view);

    auto cycle = session.standingCycle(now());
    ASSERT_TRUE(cycle);
    EXPECT_FALSE(cycle->fetching) << "a request that never reached the model left the cycle stuck";
    EXPECT_TRUE(cycle->offered);
    EXPECT_EQ(session.cycleSense(now(), true).kind, CardSession::SenseStep::Kind::Ask);
}

TEST(CardSession, AChainStepNamesTheCardItWasAskedFrom)
{
    CardSession session;
    EXPECT_FALSE(session.chainStep(QStringLiteral("bank")));

    session.raise(word(QStringLiteral("bank")), CardKind::Selection, QStringLiteral("bank"));
    const auto step = session.chainStep(QStringLiteral("river"));
    ASSERT_TRUE(step);
    EXPECT_TRUE(step->chain);
    EXPECT_EQ(step->parent, QStringLiteral("bank"));
    EXPECT_EQ(step->anchor, QPoint(100, 200));
    EXPECT_TRUE(session.chainStanding(QStringLiteral("bank")));
    EXPECT_FALSE(session.chainStanding(QStringLiteral("river")));

    session.clear();
    EXPECT_FALSE(session.chainStanding(QStringLiteral("bank")));
}

TEST(CardSession, DeletingAWordTakesItsOwnStateAndNoOtherWords)
{
    CardSession session;
    raiseCard(session, QStringLiteral("alpha"), CardKind::Selection, QStringLiteral("first alpha"));
    session.cycleSense(now(), true);
    session.requestPeek(spanishPeek());

    // Beta is the card standing, and its two requests are the ones out.
    raiseCard(session, QStringLiteral("beta"), CardKind::Selection, QStringLiteral("first beta"));
    session.cycleSense(now(), true);
    session.requestPeek(spanishPeek());

    // Alpha goes: beta's card stays, and beta's answers still land on it.
    EXPECT_FALSE(session.invalidateWord(QStringLiteral("Alpha"))) << "a deletion compared case-sensitively";
    const auto beta = session.senseAnswered(QStringLiteral("beta"),
                                            withSenses({sense(QStringLiteral("second beta"), QStringLiteral("第二乙"), QStringLiteral("beta two"))}),
                                            paid(),
                                            now());
    EXPECT_EQ(beta.result, CardSession::SenseOutcome::Result::Grown);
    ASSERT_TRUE(beta.view);

    // The requests out for the deleted word are gone: their late answers are not filed anywhere.
    EXPECT_FALSE(session.peekAnswered(QStringLiteral("alpha"), QStringLiteral("too late"), now()));
    EXPECT_FALSE(session.senseAnswered(QStringLiteral("alpha"), withSenses({sense("late", "晚", "late")}), paid(), now()).view);

    // Alpha's bought sense and line went with it: the word is asked about afresh.
    raiseCard(session, QStringLiteral("alpha"), CardKind::Selection, QStringLiteral("first alpha"));
    ASSERT_EQ(session.cycleSense(now(), true).kind, CardSession::SenseStep::Kind::Ask)
        << "the deleted word's cycle survived";
    const auto freshPeek = session.requestPeek(spanishPeek());
    EXPECT_EQ(freshPeek.kind, CardSession::PeekStep::Kind::Ask) << "the deleted word's peek line survived";

    // And deleting the standing word takes the card down.
    EXPECT_TRUE(session.invalidateWord(QStringLiteral("alpha")));
    EXPECT_FALSE(session.standing());
}
