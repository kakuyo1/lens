/**
 * @file words_model.cpp
 * @brief The words item model's diff and filter (see words_model.h).
 */

#include "words_model.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QSet>

namespace lens::app {
namespace {

/// @return Whether a QGuiApplication (or QApplication) is alive: the font database needs one, and
///         instance()/qGuiApp cast QCoreApplication::instance() without checking the type.
bool hasGuiApplication()
{
    auto const* app = QCoreApplication::instance();
    return app != nullptr && app->inherits("QGuiApplication");
}

/// @return The width MixedText draws @p text at @p pixelSize: CJK runs (U+3400..U+9FFF) keep the UI
///         face, every other run takes the numeric face, laid end to end (MixedText.qml splitRuns).
qreal mixedWidth(QString const& text, int pixelSize, QFont const& uiFont, QFont const& monoFont)
{
    QFont ui   = uiFont;
    QFont mono = monoFont;
    ui.setPixelSize(pixelSize);
    mono.setPixelSize(pixelSize);
    QFontMetrics const uiFm(ui);
    QFontMetrics const monoFm(mono);

    qreal   width = 0;
    QString run;
    bool    runCjk = false;
    auto    flush  = [&] {
        if (run.isEmpty())
            return;
        width += (runCjk ? uiFm : monoFm).horizontalAdvance(run);
        run.clear();
    };
    for (QChar const character : text) {
        bool const cjk = character.unicode() >= 0x3400 && character.unicode() <= 0x9fff;
        if (not run.isEmpty() and cjk != runCjk)
            flush();
        runCjk = cjk;
        run += character;
    }
    flush();
    return width;
}

} // namespace

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

    auto next = filtered(all_, filter_, query_);
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

void WordsModel::setQuery(QString const& value)
{
    const QString next = value.trimmed();
    if (next == query_)
        return;
    if (hasPending_)
        finishSwitch(); // a keystroke before a switch landed: land that one now
    query_ = next;
    emit queryChanged();
    // A keystroke lands in place rather than through the switch's two phases: the rows the
    // word drops close at once, the rows it brings back open at once.
    diffVisible(filtered(all_, filter_, query_));
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

void WordsModel::update(QDate const&                           today,
                        std::vector<core::HistoryEntry> const& history,
                        std::unordered_set<std::string> const& knownLemmas,
                        std::unordered_set<std::string> const& masteredLemmas)
{
    if (hasPending_)
        finishSwitch(); // a row cannot arrive under a statistics change meant for the set before it
    std::vector<WordRow> nextAll     = wordRowList(today, history, knownLemmas, masteredLemmas);
    std::vector<WordRow> nextVisible = filtered(nextAll, filter_, query_);
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

    recomputeWidestRow();

    if (allCount() != oldCount)
        emit allCountChanged();
}

void WordsModel::setEnglishFont(QString const& value)
{
    if (value == englishFont_)
        return;
    englishFont_ = value;
    emit englishFontChanged();
    recomputeWidestRow();
}

void WordsModel::setMonoFamily(QString const& value)
{
    if (value == monoFamily_)
        return;
    monoFamily_ = value;
    emit monoFamilyChanged();
    recomputeWidestRow();
}

qreal WordsModel::rowNaturalWidth(WordRow const& row) const
{
    // Mirrors WordsListRow.qml: the word in the bubble's face at 13 px, then the right cluster --
    // metadata and verdict pills -- whose widths the card needs to size itself without building rows.
    // A unit test drives the model under QCoreApplication alone; the default font stands in there.
    QFont const uiFont = hasGuiApplication() ? QGuiApplication::font() : QFont{};
    QFont const monoFont(monoFamily_.isEmpty() ? QStringLiteral("Cascadia Code") : monoFamily_);
    QFont       wordFont(englishFont_.isEmpty() ? uiFont.family() : englishFont_);
    wordFont.setPixelSize(13);
    qreal const wordWidth = QFontMetrics(wordFont).horizontalAdvance(row.word);

    QString when;
    if (row.day == QLatin1String("today"))
        when = QCoreApplication::translate("WordsPopup", "Today %1").arg(row.time);
    else if (row.day == QLatin1String("yesterday"))
        when = QCoreApplication::translate("WordsPopup", "Yesterday %1").arg(row.time);
    else if (row.day == QLatin1String("earlier"))
        when = row.date + QLatin1Char(' ') + row.time;
    qreal const whenWidth = mixedWidth(when, 11, uiFont, monoFont);
    qreal const popsWidth = mixedWidth(QCoreApplication::translate("WordsListRow", "%1×").arg(row.pops), 11, uiFont, monoFont);
    qreal const metaWidth = whenWidth + 6 + popsWidth;

    QFont pillFont = uiFont;
    pillFont.setPixelSize(11);
    pillFont.setWeight(QFont::Bold);
    QFontMetrics const pillFm(pillFont);
    auto               pillWidth  = [&](QString const& label) { return pillFm.horizontalAdvance(label) + 13; };
    qreal const        pillsWidth = pillWidth(QCoreApplication::translate("WordsListRow", "Known")) + 6 + pillWidth(QCoreApplication::translate("WordsListRow", "Mastered")) + 6 + pillWidth(QCoreApplication::translate("WordsListRow", "New"));

    qreal const sideWidth = metaWidth + 8 + pillsWidth + 8 + 14;
    return 48 + wordWidth + 9 + sideWidth;
}

void WordsModel::recomputeWidestRow()
{
    // The width needs the font database, which only a QGuiApplication carries; a unit test drives
    // the model under QCoreApplication alone and never reads widestRow. instance() and qGuiApp both
    // cast QCoreApplication::instance() without checking the type, so ask the metaobject.
    if (not hasGuiApplication())
        return;
    qreal widest = widestRow_;
    for (WordRow const& row : all_)
        widest = qMax(widest, rowNaturalWidth(row));
    if (widest != widestRow_) {
        widestRow_ = widest;
        emit widestRowChanged();
    }
}

bool WordsModel::matches(WordRow const& row, int filter)
{
    // The popup's four segments are the export's four scopes, and the rule is spelt once in
    // words_view.h. The values are the same order as core::ExportScope's.
    static_assert(All == static_cast<int>(core::ExportScope::All));
    static_assert(Known == static_cast<int>(core::ExportScope::Known));
    static_assert(Mastered == static_cast<int>(core::ExportScope::Mastered));
    static_assert(New == static_cast<int>(core::ExportScope::New));
    return rowInScope(row, static_cast<core::ExportScope>(filter));
}

std::vector<WordRow> WordsModel::filtered(std::vector<WordRow> const& rows, int filter, QString const& query)
{
    std::vector<WordRow> visible;
    visible.reserve(rows.size());
    for (const WordRow& row : rows)
        // The query searches the whole vocabulary and suspends the segment while it stands:
        // a word outside the reader's current slice must stay findable, or the miss hint
        // would invite a paid re-explain of a word the list already holds (docs/adr/0176).
        if (query.isEmpty() ? matches(row, filter)
                            : row.word.contains(query, Qt::CaseInsensitive))
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
