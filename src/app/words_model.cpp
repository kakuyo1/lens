/**
 * @file words_model.cpp
 * @brief The words item model's diff and filter (see words_model.h).
 */

#include "words_model.h"

#include <QSet>

namespace lens::app {

WordsModel::WordsModel(QObject* parent)
    : QAbstractListModel(parent)
{
    swap_.setSingleShot(true);
    connect(&swap_, &QTimer::timeout, this, &WordsModel::finishSwitch);
}

int WordsModel::rowCount(QModelIndex const& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(visible_.size());
}

QVariant WordsModel::data(QModelIndex const& index, int role) const
{
    if (not index.isValid() or index.row() < 0 or index.row() >= static_cast<int>(visible_.size()))
        return {};
    const WordRow& row = visible_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
        case WordRole:
            return row.word;
        case DayRole:
            return row.day;
        case DateRole:
            return row.date;
        case TimeRole:
            return row.time;
        case VerdictRole:
            return row.verdict;
        case PopsRole:
            return row.pops;
        default:
            return {};
    }
}

QHash<int, QByteArray> WordsModel::roleNames() const
{
    return {{WordRole, "word"},
            {DayRole, "day"},
            {DateRole, "date"},
            {TimeRole, "time"},
            {VerdictRole, "verdict"},
            {PopsRole, "pops"}};
}

QVariantList WordsModel::allRows() const
{
    QVariantList rows;
    rows.reserve(static_cast<qsizetype>(all_.size()));
    for (const WordRow& row : all_)
        rows.append(QVariantMap{{"word", row.word},
                                {"day", row.day},
                                {"date", row.date},
                                {"time", row.time},
                                {"verdict", row.verdict},
                                {"pops", row.pops}});
    return rows;
}

void WordsModel::setSwapMs(int value)
{
    if (value == swapMs_)
        return;
    swapMs_ = qMax(0, value);
    emit swapMsChanged();
}

void WordsModel::setFilter(int value)
{
    if (value < All or value > New or value == filter_)
        return;
    if (hasPending_)
        finishSwitch(); // a second segment before the first landed: land that one now
    filter_ = value;
    emit filterChanged();

    auto next = filtered(all_, filter_);
    if (swapMs_ <= 0) {
        diffVisible(std::move(next));
        return;
    }

    // The rows the new filter hides close first. Their space is what the arriving rows take, so
    // the two sets are never drawn on one line -- the reader sees one list become the other.
    switching_ = true;
    emit filterSwitchingChanged();
    diffVisible(next, /*insertRows=*/false);
    pending_    = std::move(next);
    hasPending_ = true;
    swap_.start(swapMs_);
}

void WordsModel::finishSwitch()
{
    if (not hasPending_)
        return;
    hasPending_ = false;
    diffVisible(pending_);
    pending_.clear();
    switching_ = false;
    emit filterSwitchingChanged();
}

void WordsModel::update(QDate const& today, std::vector<core::HistoryEntry> const& history)
{
    if (hasPending_)
        finishSwitch(); // a row cannot arrive under a statistics change meant for the set before it
    std::vector<WordRow> nextAll     = wordRowList(today, history);
    std::vector<WordRow> nextVisible = filtered(nextAll, filter_);
    const int            oldCount    = allCount();

    // A row that is in both lists keeps its place in the difference below. When the common
    // rows themselves change order -- a word re-popped and moved to the front -- the index
    // bookkeeping would be all that the diff is, and a reset is what this list had before.
    QSet<QString> inOld;
    QSet<QString> inNext;
    for (const WordRow& row : visible_)
        inOld.insert(row.word);
    for (const WordRow& row : nextVisible)
        inNext.insert(row.word);
    std::vector<QString> oldCommon;
    std::vector<QString> nextCommon;
    for (const WordRow& row : visible_)
        if (inNext.contains(row.word))
            oldCommon.push_back(row.word);
    for (const WordRow& row : nextVisible)
        if (inOld.contains(row.word))
            nextCommon.push_back(row.word);

    if (oldCommon != nextCommon) {
        beginResetModel();
        visible_ = std::move(nextVisible);
        all_     = std::move(nextAll);
        endResetModel();
    } else {
        diffVisible(std::move(nextVisible));
        all_ = std::move(nextAll);
    }

    if (allCount() != oldCount)
        emit allCountChanged();
}

bool WordsModel::matches(WordRow const& row, int filter)
{
    // The popup's three segments are the export's three scopes, and the rule is spelt once in
    // words_view.h. The values are the same order as core::ExportScope's.
    static_assert(All == static_cast<int>(core::ExportScope::All));
    static_assert(Known == static_cast<int>(core::ExportScope::Known));
    static_assert(New == static_cast<int>(core::ExportScope::New));
    return rowInScope(row, static_cast<core::ExportScope>(filter));
}

std::vector<WordRow> WordsModel::filtered(std::vector<WordRow> const& rows, int filter)
{
    std::vector<WordRow> visible;
    visible.reserve(rows.size());
    for (const WordRow& row : rows)
        if (matches(row, filter))
            visible.push_back(row);
    return visible;
}

void WordsModel::diffVisible(std::vector<WordRow> next, bool insertRows)
{
    QHash<QString, int> oldAt;
    QHash<QString, int> nextAt;
    for (int i = 0; i < static_cast<int>(visible_.size()); ++i)
        oldAt.insert(visible_[static_cast<std::size_t>(i)].word, i);
    for (int i = 0; i < static_cast<int>(next.size()); ++i)
        nextAt.insert(next[static_cast<std::size_t>(i)].word, i);

    // Removals first, from the end, one run at a time: a filter that drops many rows is one
    // removal per contiguous stretch rather than one signal per row.
    for (int end = static_cast<int>(visible_.size()) - 1; end >= 0;) {
        if (nextAt.contains(visible_[static_cast<std::size_t>(end)].word)) {
            --end;
            continue;
        }
        int start = end;
        while (start > 0 and not nextAt.contains(visible_[static_cast<std::size_t>(start - 1)].word))
            --start;
        beginRemoveRows({}, start, end);
        visible_.erase(visible_.begin() + start, visible_.begin() + end + 1);
        endRemoveRows();
        end = start - 1;
    }

    if (not insertRows)
        return;

    // Then the new rows, in their final order. The common rows are already in that order,
    // which update() checked, so the insertion point is the number of rows placed so far.
    int at = 0;
    for (const WordRow& row : next) {
        if (oldAt.contains(row.word)) {
            Q_ASSERT(at < static_cast<int>(visible_.size()) and visible_[static_cast<std::size_t>(at)].word == row.word);
            ++at;
            continue;
        }
        beginInsertRows({}, at, at);
        visible_.insert(visible_.begin() + at, row);
        endInsertRows();
        ++at;
    }

    // The common rows now stand in their final places; only their values can differ.
    for (int i = 0; i < static_cast<int>(visible_.size()); ++i) {
        WordRow& row = visible_[static_cast<std::size_t>(i)];
        if (row == next[static_cast<std::size_t>(i)])
            continue;
        row = next[static_cast<std::size_t>(i)];
        emit dataChanged(index(i), index(i), {WordRole, DayRole, DateRole, TimeRole, VerdictRole, PopsRole});
    }
}

}
