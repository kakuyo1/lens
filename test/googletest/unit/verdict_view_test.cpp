/**
 * @file verdict_view_test.cpp
 * @brief The verdict projection: one place mints the tokens QML reads and parses the press back
 *        (docs/adr/0166).
 */

#include <gtest/gtest.h>

#include <QString>

#include <filesystem>
#include <string>
#include <unordered_set>

#include "app/duties/verdict_view.h"

namespace {

using lens::app::verdictFromToken;
using lens::app::verdictOf;
using lens::app::verdictToken;
using lens::core::CandidateState;
using lens::core::WordVerdict;

TEST(VerdictView, TheTokensAreMintedOnce)
{
    EXPECT_EQ(verdictToken(WordVerdict::None), QStringLiteral("new"));
    EXPECT_EQ(verdictToken(WordVerdict::Known), QStringLiteral("known"));
    EXPECT_EQ(verdictToken(WordVerdict::Mastered), QStringLiteral("mastered"));
    // The scan's judged states share the same vocabulary, payload included.
    EXPECT_EQ(verdictToken(CandidateState::New), QStringLiteral("new"));
    EXPECT_EQ(verdictToken(CandidateState::Known), QStringLiteral("known"));
    EXPECT_EQ(verdictToken(CandidateState::Mastered), QStringLiteral("mastered"));
}

TEST(VerdictView, TheStoreAnswersTheBadgeInOneCall)
{
    lens::core::KnownStore store = lens::core::KnownStore::load(std::filesystem::temp_directory_path() / "lens_verdict_view_test.json");

    EXPECT_EQ(verdictToken(store, "serendipity"), QStringLiteral("new"));
    store.mark("serendipity", WordVerdict::Known);
    EXPECT_EQ(verdictToken(store, "serendipity"), QStringLiteral("known"));
    store.mark("serendipity", WordVerdict::Mastered);
    EXPECT_EQ(verdictToken(store, "serendipity"), QStringLiteral("mastered"));
    store.mark("serendipity", WordVerdict::None);
    EXPECT_EQ(verdictToken(store, "serendipity"), QStringLiteral("new"));
}

TEST(VerdictView, ThePressIsParsedOnceAndANonsenseTokenIsTheClearVote)
{
    EXPECT_EQ(verdictFromToken(QStringLiteral("mastered")), WordVerdict::Mastered);
    EXPECT_EQ(verdictFromToken(QStringLiteral("known")), WordVerdict::Known);
    EXPECT_EQ(verdictFromToken(QStringLiteral("new")), WordVerdict::None);
    EXPECT_EQ(verdictFromToken(QStringLiteral("nonsense")), WordVerdict::None);
    EXPECT_EQ(verdictFromToken(QString{}), WordVerdict::None);
    // The mint and the parse are inverses: the verdict a pill pressed round-trips.
    for (auto const verdict : {WordVerdict::None, WordVerdict::Known, WordVerdict::Mastered})
        EXPECT_EQ(verdictFromToken(verdictToken(verdict)), verdict);
}

TEST(VerdictView, TheSetsAnswerWithTheStrongerVerdictFirst)
{
    const std::unordered_set<std::string> known{"river", "forest"};
    const std::unordered_set<std::string> mastered{"garden", "river"};
    EXPECT_EQ(verdictOf("garden", known, mastered), WordVerdict::Mastered);
    EXPECT_EQ(verdictOf("river", known, mastered), WordVerdict::Mastered);
    EXPECT_EQ(verdictOf("forest", known, mastered), WordVerdict::Known);
    EXPECT_EQ(verdictOf("stone", known, mastered), WordVerdict::None);
    // The popup's rows and its export scope read the same verdict through the same token.
    EXPECT_EQ(verdictToken(verdictOf("garden", known, mastered)), QStringLiteral("mastered"));
    EXPECT_EQ(verdictToken(verdictOf("forest", known, mastered)), QStringLiteral("known"));
    EXPECT_EQ(verdictToken(verdictOf("stone", known, mastered)), QStringLiteral("new"));
}

}
