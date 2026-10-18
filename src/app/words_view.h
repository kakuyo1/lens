/**
 * @file words_view.h
 * @brief The words surface's rows, the export vocabulary and the import rule, as pure functions.
 *
 * AppController used to answer these from the wall clock and three copies of the same scope
 * switch, so the day labels and the import timestamp could not be pinned. Here every answer takes
 * the date or hour it judges against; the invokable that QML reaches reads the clock and passes it
 * in. No file, network or clock is touched.
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
#include <string>
#include <unordered_set>
#include <vector>

#include "core/stats_store.h"

namespace lens::app {

/// @return The export scope @p scope names, or nothing when it names none.
inline std::optional<core::ExportScope> parseScope(QString const& scope)
{
    if (scope == QLatin1String("all")) return core::ExportScope::All;
    if (scope == QLatin1String("known")) return core::ExportScope::Known;
    if (scope == QLatin1String("new")) return core::ExportScope::New;
    return std::nullopt;
}

/// @return Whether @p path asks for the JSON export by its extension.
inline bool isJsonPath(QString const& path)
{
    return path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive);
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
 * @brief The words popup's rows: each lemma once, its newest entry deciding the labels and verdict.
 * @param today The day the labels are relative to; the caller reads the clock.
 * @param history The statistics history, newest first.
 * @return One map per distinct lemma: word, day, date, time, verdict and the total pop count.
 */
inline QVariantList wordRows(QDate const& today, std::vector<core::HistoryEntry> const& history)
{
    QSet<QString>       seen;
    QHash<QString, int> pops;
    for (const core::HistoryEntry& entry : history)
        ++pops[QString::fromStdString(entry.lemma)];

    QVariantList rows;
    rows.reserve(static_cast<qsizetype>(history.size()));
    for (const core::HistoryEntry& entry : history) {
        const QString shown = QString::fromStdString(entry.lemma);
        if (seen.contains(shown))
            continue;
        seen.insert(shown);

        const QDateTime moment = QDateTime::fromString(QString::fromStdString(entry.minute), QStringLiteral("yyyy-MM-dd HH:mm"));
        rows.append(QVariantMap{{"word", shown},
                                {"day", dayLabel(moment, today)},
                                {"date", moment.isValid() ? moment.toString(QStringLiteral("MM-dd")) : QString{}},
                                {"time", moment.isValid() ? moment.toString(QStringLiteral("HH:mm")) : QString{}},
                                {"verdict", QString::fromStdString(entry.verdict)},
                                {"pops", pops.value(shown)}});
    }
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

/// @brief One word an import adds, stamped with the import moment and the verdict it starts on.
struct ImportRow {
    std::string lemma;
    std::string minute;
    std::string verdict; ///< "new": an imported word has not been marked known.
};

/**
 * @brief The rows an import writes: no word the history already holds, and no repeats in the file.
 * @param incoming The file's words, in order, as importLemmas() returned them.
 * @param existing The lemmas the history already holds.
 * @param minute The local "yyyy-MM-dd HH:mm" the import is recorded at; the caller reads the clock.
 * @return One row per word to add, in file order.
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
        rows.push_back(ImportRow{lemma, minute, "new"});
    }
    return rows;
}

}
