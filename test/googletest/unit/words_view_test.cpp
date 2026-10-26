/**
 * @file words_view_test.cpp
 * @brief The words rows, the export vocabulary and the import rule, with the clock passed in.
 */

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QVariantMap>
#include <gtest/gtest.h>

#include <string>
#include <unordered_set>
#include <vector>

#include "app/words_view.h"

namespace {

using lens::app::dayLabel;
using lens::app::importLemmas;
using lens::app::importRows;
using lens::app::parseScope;
using lens::app::rowInScope;
using lens::app::wordRows;

} // namespace

TEST(WordsView, ScopeVocabularyIsTheExportsOwn)
{
    EXPECT_EQ(parseScope(QStringLiteral("all")), lens::core::ExportScope::All);
    EXPECT_EQ(parseScope(QStringLiteral("known")), lens::core::ExportScope::Known);
    EXPECT_EQ(parseScope(QStringLiteral("new")), lens::core::ExportScope::New);
    EXPECT_FALSE(parseScope(QStringLiteral("nonsense")).has_value());
    EXPECT_FALSE(parseScope(QString{}).has_value());
}

TEST(WordsView, DayBucketTakesTheDayItIsJudgedAgainst)
{
    const QDateTime moment = QDateTime::fromString(QStringLiteral("2026-10-17 23:59"), QStringLiteral("yyyy-MM-dd HH:mm"));

    EXPECT_EQ(dayLabel(moment, QDate(2026, 10, 17)), QStringLiteral("today"));
    EXPECT_EQ(dayLabel(moment, QDate(2026, 10, 18)), QStringLiteral("yesterday"));
    EXPECT_EQ(dayLabel(moment, QDate(2026, 10, 19)), QStringLiteral("earlier"));
    EXPECT_TRUE(dayLabel(QDateTime{}, QDate(2026, 10, 18)).isEmpty());
}

TEST(WordsView, RowsKeepTheNewestEntryPerLemmaAndCountEveryPop)
{
    const std::vector<lens::core::HistoryEntry> history{
        {"bank", "2026-10-18 09:00", ""},
        {"bank", "2026-10-16 08:00", "new"},
        {"river", "2026-10-17 10:00", "known"},
        {"ghost", "not a time", ""},
    };

    const auto rows = wordRows(QDate(2026, 10, 18), history);
    ASSERT_EQ(rows.size(), 3);

    const auto bank = rows.at(0).toMap();
    EXPECT_EQ(bank.value("word").toString(), QStringLiteral("bank"));
    EXPECT_EQ(bank.value("day").toString(), QStringLiteral("today"));
    EXPECT_EQ(bank.value("date").toString(), QStringLiteral("10-18"));
    EXPECT_EQ(bank.value("time").toString(), QStringLiteral("09:00"));
    EXPECT_TRUE(bank.value("verdict").toString().isEmpty()) << "the newest entry decides the verdict";
    EXPECT_EQ(bank.value("pops").toInt(), 2);

    const auto river = rows.at(1).toMap();
    EXPECT_EQ(river.value("day").toString(), QStringLiteral("yesterday"));
    EXPECT_EQ(river.value("verdict").toString(), QStringLiteral("known"));

    const auto ghost = rows.at(2).toMap();
    EXPECT_TRUE(ghost.value("day").toString().isEmpty());
    EXPECT_TRUE(ghost.value("date").toString().isEmpty());
    EXPECT_TRUE(ghost.value("time").toString().isEmpty());
}

TEST(WordsView, TheFilterAndAnExportShareTheScopeRule)
{
    const lens::app::WordRow known{"bank", "today", "10-20", "09:00", "known", 2};
    const lens::app::WordRow fresh{"river", "today", "10-20", "10:00", "new", 1};
    const lens::app::WordRow unmarked{"ghost", "today", "10-20", "11:00", "", 1};

    EXPECT_TRUE(rowInScope(known, lens::core::ExportScope::All));
    EXPECT_TRUE(rowInScope(fresh, lens::core::ExportScope::All));
    EXPECT_TRUE(rowInScope(unmarked, lens::core::ExportScope::All));
    EXPECT_TRUE(rowInScope(known, lens::core::ExportScope::Known));
    EXPECT_FALSE(rowInScope(fresh, lens::core::ExportScope::Known));
    EXPECT_FALSE(rowInScope(unmarked, lens::core::ExportScope::Known));
    EXPECT_TRUE(rowInScope(fresh, lens::core::ExportScope::New));
    EXPECT_FALSE(rowInScope(known, lens::core::ExportScope::New));
    EXPECT_FALSE(rowInScope(unmarked, lens::core::ExportScope::New));
}

TEST(WordsView, ImportReadsTextAndTheTwoJsonShapes)
{
    const auto text = importLemmas(QByteArrayLiteral("  bank \n\nriver\nbank\n"), false);
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, (std::vector<std::string>{"bank", "river", "bank"}));

    const auto json = importLemmas(QByteArrayLiteral(R"(["bank", {"word":"river"}, {"word":" "}, 7])"), true);
    ASSERT_TRUE(json.has_value());
    EXPECT_EQ(*json, (std::vector<std::string>{"bank", "river"}))
        << "a JSON entry that is neither a string nor a named word is skipped";

    EXPECT_FALSE(importLemmas(QByteArrayLiteral(R"({"word":"bank"})"), true).has_value())
        << "a JSON document that is not an array is refused";
}

TEST(WordsView, ImportRowsDropRepeatsAndStampThePassedMinute)
{
    const std::unordered_set<std::string> existing{"river"};
    const std::vector<std::string>        incoming{"bank", "bank", "river", "ghost"};

    const auto rows = importRows(incoming, existing, "2026-10-18 09:30");

    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.at(0).lemma, "bank");
    EXPECT_EQ(rows.at(0).minute, "2026-10-18 09:30");
    EXPECT_EQ(rows.at(0).verdict, "new");
    EXPECT_EQ(rows.at(1).lemma, "ghost");
}
