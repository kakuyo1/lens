/**
 * @file words_view.h
 * @brief The words surface's rows, the export vocabulary and the import rule, as pure functions.
 *
 * Every answer takes the date or hour it judges against; the invokable that QML reaches reads the
 * clock and passes it in, so the day labels and the import timestamp can be pinned by a test.
 * No file, network or clock is touched.
 */

#pragma once

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <optional>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/document/stats_store.h"
#include "util/text.h"

namespace lens::app {

/// @return The export scope @p scope names, or nothing when it names none.
inline std::optional<core::ExportScope> parseScope(QString const& scope)
{
    if (scope == QLatin1String("all")) return core::ExportScope::All;
    if (scope == QLatin1String("known")) return core::ExportScope::Known;
    if (scope == QLatin1String("mastered")) return core::ExportScope::Mastered;
    if (scope == QLatin1String("new")) return core::ExportScope::New;
    return std::nullopt;
}

/// @return Whether @p path asks for the JSON export by its extension.
inline bool isJsonPath(QString const& path)
{
    return path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive);
}

/// @return The card format @p path asks for by its extension; nothing for the plain list and the JSON one.
inline std::optional<core::CardFormat> cardFormatOf(QString const& path)
{
    if (path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)) return core::CardFormat::Csv;
    if (path.endsWith(QLatin1String(".tsv"), Qt::CaseInsensitive)) return core::CardFormat::Anki;
    return std::nullopt;
}

/// @return The words list's day bucket for @p moment against @p today; empty for an unreadable time.
inline QString dayLabel(QDateTime const& moment, QDate const& today)
{
    if (not moment.isValid()) return {};
    const qint64 daysAgo = moment.date().daysTo(today);
    if (daysAgo == 0) return QStringLiteral("today");
    if (daysAgo == 1) return QStringLiteral("yesterday");
    return QStringLiteral("earlier");
}

/**
 * @brief One row of the words popup, before QML sees it.
 *
 * The model, its filter and the export range all judge this typed row rather than a map, so
 * the row's fields are spelt once (docs/adr/0122).
 */
struct WordRow {
    QString word;
    QString day;  ///< "today", "yesterday", "earlier", or empty for an unreadable time.
    QString date; ///< "MM-dd", empty for an unreadable time.
    QString time; ///< "HH:mm", empty for an unreadable time.
    /// Derived from the verdict sets, not stored: "mastered" when the lemma is marked mastered,
    /// "known" when it is marked known, "new" otherwise -- the words the bubble badge already
    /// speaks (docs/adr/0159).
    QString verdict;
    int     pops = 0;

    bool operator==(WordRow const&) const = default;
};

/**
 * @brief The words popup's rows: each lemma once, the verdict sets deciding the verdict.
 *
 * The rows are the history's words first, newest entry first; the verdict store's own words -- one
 * marked without ever being popped, by the pencil layer's mark button, a wordlist import or another
 * device's sync -- follow in lemma order, with no time and a zero pop count (docs/adr/0159).
 *
 * @param today The day the labels are relative to; the caller reads the clock.
 * @param history The statistics history, newest first.
 * @param knownLemmas The lemmas currently marked known; the known() view KnownStore derives.
 * @param masteredLemmas The lemmas currently marked mastered; the mastered() view derives (docs/adr/0159).
 * @return One row per distinct lemma.
 */
inline std::vector<WordRow> wordRowList(QDate const&                           today,
                                        std::vector<core::HistoryEntry> const& history,
                                        std::unordered_set<std::string> const& knownLemmas,
                                        std::unordered_set<std::string> const& masteredLemmas)
{
    QSet<QString>       seen;
    QHash<QString, int> pops;
    for (const core::HistoryEntry& entry : history)
        ++pops[QString::fromStdString(entry.lemma)];

    std::vector<WordRow> rows;
    rows.reserve(history.size());
    for (const core::HistoryEntry& entry : history) {
        const QString shown = QString::fromStdString(entry.lemma);
        if (seen.contains(shown))
            continue;
        seen.insert(shown);

        const QDateTime moment = QDateTime::fromString(QString::fromStdString(entry.minute), QString::fromLatin1(util::kMinuteFormat));
        rows.push_back(WordRow{shown,
                               dayLabel(moment, today),
                               moment.isValid() ? moment.toString(QStringLiteral("MM-dd")) : QString{},
                               moment.isValid() ? moment.toString(QStringLiteral("HH:mm")) : QString{},
                               masteredLemmas.count(entry.lemma) > 0 ? QStringLiteral("mastered")
                               : knownLemmas.count(entry.lemma) > 0  ? QStringLiteral("known")
                                                                     : QStringLiteral("new"),
                               pops.value(shown)});
    }

    // The verdict store's own words, the history never saw. Lemma order keeps the rows stable --
    // the sets behind them are unordered hashes, and the list must not shuffle between updates.
    std::vector<std::string> extra;
    for (const std::string& lemma : masteredLemmas)
        if (not seen.contains(QString::fromStdString(lemma)))
            extra.push_back(lemma);
    for (const std::string& lemma : knownLemmas)
        if (not seen.contains(QString::fromStdString(lemma)))
            extra.push_back(lemma);
    std::sort(extra.begin(), extra.end());
    for (const std::string& lemma : extra) {
        const QString shown = QString::fromStdString(lemma);
        seen.insert(shown);
        rows.push_back(WordRow{shown,
                               QString{},
                               QString{},
                               QString{},
                               masteredLemmas.count(lemma) > 0 ? QStringLiteral("mastered") : QStringLiteral("known"),
                               0});
    }
    return rows;
}

/// @return Whether @p row belongs under @p scope, the rule the filter and an export share.
inline bool rowInScope(WordRow const& row, core::ExportScope const scope)
{
    switch (scope) {
        case core::ExportScope::All:
            return true;
        case core::ExportScope::Known:
            return row.verdict == QLatin1String("known");
        case core::ExportScope::Mastered:
            return row.verdict == QLatin1String("mastered");
        case core::ExportScope::New:
            return row.verdict == QLatin1String("new");
    }
    return false;
}

/**
 * @brief The words popup's rows as the plain values QML used to read.
 * @param today The day the labels are relative to; the caller reads the clock.
 * @param history The statistics history, newest first.
 * @param knownLemmas The lemmas currently marked known; the known() view KnownStore derives.
 * @return One map per distinct lemma: word, day, date, time, verdict and the total pop count.
 */
inline QVariantList wordRows(QDate const&                           today,
                             std::vector<core::HistoryEntry> const& history,
                             std::unordered_set<std::string> const& knownLemmas,
                             std::unordered_set<std::string> const& masteredLemmas)
{
    QVariantList rows;
    const auto   typed = wordRowList(today, history, knownLemmas, masteredLemmas);
    rows.reserve(static_cast<qsizetype>(typed.size()));
    for (const WordRow& row : typed)
        rows.append(QVariantMap{{"word", row.word},
                                {"day", row.day},
                                {"date", row.date},
                                {"time", row.time},
                                {"verdict", row.verdict},
                                {"pops", row.pops}});
    return rows;
}

/**
 * @brief The words one import file names, in file order and before de-duplication.
 * @param data The file's bytes.
 * @param json Whether the file is the JSON export; a JSON document that is not an array is refused.
 * @return The trimmed non-empty words, or nothing when a JSON file is not an array.
 */
inline std::optional<std::vector<std::string>> importLemmas(QByteArray const& data, bool const json)
{
    std::vector<std::string> incoming;
    if (json) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        if (not doc.isArray())
            return std::nullopt;
        for (const QJsonValue& value : doc.array()) {
            if (value.isString()) {
                incoming.push_back(value.toString().trimmed().toStdString());
            } else if (value.isObject()) {
                const QString word = value.toObject().value(QStringLiteral("word")).toString();
                if (not word.trimmed().isEmpty())
                    incoming.push_back(word.trimmed().toStdString());
            }
        }
    } else {
        const QString text = QString::fromUtf8(data);
        for (const QString& line : text.split(QLatin1Char('\n'))) {
            const QString word = line.trimmed();
            if (not word.isEmpty())
                incoming.push_back(word.toStdString());
        }
    }
    return incoming;
}

/// @brief One word an import adds, stamped with the import moment.
struct ImportRow {
    std::string lemma;
    std::string minute;
};

/**
 * @brief The rows an import writes: no word the history already holds, and no repeats in the file.
 * @param incoming The file's words, in order, as importLemmas() returned them.
 * @param existing The lemmas the history already holds.
 * @param minute The local minute the import is recorded at, in util::kMinuteFormat; the caller reads the clock.
 * @return One row per word to add, in file order. The pop each row records starts the word on
 *         "new" (recordPop stamps it), unless the lemma already carries a known mark.
 */
inline std::vector<ImportRow> importRows(std::vector<std::string> const&        incoming,
                                         std::unordered_set<std::string> const& existing,
                                         std::string const&                     minute)
{
    std::unordered_set<std::string> seen;
    std::vector<ImportRow>          rows;
    for (const std::string& lemma : incoming) {
        if (lemma.empty() or seen.count(lemma) or existing.count(lemma))
            continue;
        seen.insert(lemma);
        rows.push_back(ImportRow{lemma, minute});
    }
    return rows;
}

}
