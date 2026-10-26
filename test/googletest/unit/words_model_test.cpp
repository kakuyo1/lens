/**
 * @file words_model_test.cpp
 * @brief The words item model's diff and filter, without a view.
 */

#include <QDate>
#include <QList>
#include <QModelIndex>
#include <QString>
#include <QVariantMap>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "app/words_model.h"

namespace {

using lens::app::WordsModel;
using lens::core::ExportScope;
using lens::core::HistoryEntry;

const QDate kToday(2026, 10, 20);

HistoryEntry entry(std::string lemma, std::string minute, std::string verdict = {})
{
    return HistoryEntry{std::move(lemma), std::move(minute), std::move(verdict)};
}

QString wordAt(WordsModel const& model, int row)
{
    return model.data(model.index(row), WordsModel::WordRole).toString();
}

} // namespace

TEST(WordsModel, UpdateDeduplicatesAndKeepsTheNewestEntryPerLemma)
{
    WordsModel model;
    model.update(kToday,
                 {entry("bank", "2026-10-20 09:00", "known"),
                  entry("river", "2026-10-19 10:00", "new"),
                  entry("bank", "2026-10-18 08:00")});

    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.allCount(), 2);
    EXPECT_EQ(wordAt(model, 0), QStringLiteral("bank"));
    EXPECT_EQ(model.data(model.index(0), WordsModel::VerdictRole).toString(), QStringLiteral("known"));
    EXPECT_EQ(model.data(model.index(0), WordsModel::PopsRole).toInt(), 2);
    EXPECT_EQ(model.data(model.index(0), WordsModel::DayRole).toString(), QStringLiteral("today"));
    EXPECT_EQ(wordAt(model, 1), QStringLiteral("river"));
}

TEST(WordsModel, TheFilterShowsOneVerdictAndLeavesTheAllCountAlone)
{
    WordsModel model;
    model.update(kToday,
                 {entry("bank", "2026-10-20 09:00", "known"),
                  entry("river", "2026-10-19 10:00", "new"),
                  entry("ghost", "2026-10-18 08:00")});

    model.setFilter(static_cast<int>(ExportScope::Known));
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(wordAt(model, 0), QStringLiteral("bank"));
    EXPECT_EQ(model.allCount(), 3);
    EXPECT_EQ(model.filter(), static_cast<int>(ExportScope::Known));

    model.setFilter(static_cast<int>(ExportScope::New));
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(wordAt(model, 0), QStringLiteral("river"));

    model.setFilter(static_cast<int>(ExportScope::All));
    ASSERT_EQ(model.rowCount(), 3);

    // An index no segment can send is ignored rather than read as a fourth scope.
    model.setFilter(9);
    EXPECT_EQ(model.filter(), static_cast<int>(ExportScope::All));
    EXPECT_EQ(model.rowCount(), 3);
}

TEST(WordsModel, ARemovalEmitsOneRemovalRatherThanAReset)
{
    WordsModel model;
    model.update(kToday,
                 {entry("bank", "2026-10-20 09:00", "known"),
                  entry("river", "2026-10-19 10:00", "new"),
                  entry("ghost", "2026-10-18 08:00")});

    int removed = 0;
    int reset   = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&](QModelIndex const&, int, int) { ++removed; });
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++reset; });

    model.update(kToday,
                 {entry("bank", "2026-10-20 09:00", "known"),
                  entry("ghost", "2026-10-18 08:00")});

    EXPECT_EQ(removed, 1);
    EXPECT_EQ(reset, 0);
    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(wordAt(model, 0), QStringLiteral("bank"));
    EXPECT_EQ(wordAt(model, 1), QStringLiteral("ghost"));
}

TEST(WordsModel, AVerdictChangeIsADataChangeNotARemoval)
{
    WordsModel model;
    model.update(kToday,
                 {entry("bank", "2026-10-20 09:00"),
                  entry("river", "2026-10-19 10:00", "new")});

    int changed = 0;
    int removed = 0;
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&](QModelIndex const&, QModelIndex const&, QList<int> const&) { ++changed; });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&](QModelIndex const&, int, int) { ++removed; });

    model.update(kToday,
                 {entry("bank", "2026-10-20 09:00", "known"),
                  entry("river", "2026-10-19 10:00", "new")});

    EXPECT_GT(changed, 0);
    EXPECT_EQ(removed, 0);
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.data(model.index(0), WordsModel::VerdictRole).toString(), QStringLiteral("known"));
}

TEST(WordsModel, ANewWordIsInsertedAtTheFront)
{
    WordsModel model;
    model.update(kToday, {entry("bank", "2026-10-19 09:00")});

    int inserted = 0;
    int at       = -1;
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&](QModelIndex const&, int first, int) { ++inserted; at = first; });

    model.update(kToday,
                 {entry("river", "2026-10-20 10:00"), entry("bank", "2026-10-19 09:00")});

    EXPECT_EQ(inserted, 1);
    EXPECT_EQ(at, 0);
    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(wordAt(model, 0), QStringLiteral("river"));
    EXPECT_EQ(wordAt(model, 1), QStringLiteral("bank"));
}

TEST(WordsModel, AFilterChangeDiffsTheVisibleRows)
{
    WordsModel model;
    model.update(kToday,
                 {entry("bank", "2026-10-20 09:00", "known"),
                  entry("river", "2026-10-19 10:00", "new"),
                  entry("ghost", "2026-10-18 08:00", "known")});

    int removed = 0;
    int reset   = 0;
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&](QModelIndex const&, int, int) { ++removed; });
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++reset; });

    model.setFilter(static_cast<int>(ExportScope::Known));
    EXPECT_EQ(removed, 1);
    EXPECT_EQ(reset, 0);
    ASSERT_EQ(model.rowCount(), 2);

    model.setFilter(static_cast<int>(ExportScope::All));
    EXPECT_EQ(model.rowCount(), 3);
    EXPECT_EQ(reset, 0);
}

TEST(WordsModel, AReorderIsTheDocumentedReset)
{
    WordsModel model;
    model.update(kToday,
                 {entry("bank", "2026-10-19 09:00"), entry("river", "2026-10-18 10:00")});

    int reset = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++reset; });

    // river is looked up again, so the newest entry moves it in front of bank.
    model.update(kToday,
                 {entry("river", "2026-10-20 10:00"),
                  entry("bank", "2026-10-19 09:00"),
                  entry("river", "2026-10-18 10:00")});

    EXPECT_EQ(reset, 1);
    ASSERT_EQ(model.rowCount(), 2);
    EXPECT_EQ(wordAt(model, 0), QStringLiteral("river"));
}

TEST(WordsModel, AllRowsCarriesTheRawFieldsAndIgnoresTheFilter)
{
    WordsModel model;
    model.update(kToday, {entry("bank", "2026-10-20 09:00", "known"), entry("river", "2026-10-19 10:00", "new")});
    model.setFilter(static_cast<int>(ExportScope::Known));

    const QVariantList rows = model.allRows();
    ASSERT_EQ(rows.size(), 2);
    const QVariantMap bank = rows.at(0).toMap();
    EXPECT_EQ(bank.value("word").toString(), QStringLiteral("bank"));
    EXPECT_EQ(bank.value("verdict").toString(), QStringLiteral("known"));
    EXPECT_EQ(bank.value("pops").toInt(), 1);
}
