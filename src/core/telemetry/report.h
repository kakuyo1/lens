#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/telemetry/event.h"
#include "core/telemetry/event_log.h"

/**
 * @file report.h
 * @brief Aggregating logged behavior events into a report: local days, weeks and months, sessions,
 *        hours, words, features and applications.
 *
 * Qt-free: the caller supplies the function that resolves an instant into the reader's local
 * calendar, so the grouping is the same on every run and a test can pin the zone. Unknown event
 * names are ignored; the caller's parser has already skipped malformed lines.
 */

namespace lens::core::telemetry {

/// @brief The quiet gap that ends an activity session.
inline constexpr long long kSessionIdleMs = 5LL * 60 * 1000;

/// @brief The longest one session's duration is counted as.
inline constexpr long long kSessionMaxMs = 8LL * 60 * 60 * 1000;

/// @brief How many lemmas a report names as most looked up by default.
inline constexpr std::size_t kReportTopWords = 10;

/// @brief One instant as the reader's local calendar shows it.
struct LocalMoment {
    std::string date;     ///< The local date, "YYYY-MM-DD"; empty when the instant has none.
    int         hour = 0; ///< The local hour, 0 through 23.
};

/// @brief Resolve an epoch-millisecond instant into the reader's local calendar.
using Localize = std::function<LocalMoment(long long tsMs)>;

/// @brief One period's totals. A period's sessions and active time are the sessions that started in it.
struct PeriodTotal {
    std::string key; ///< The date for a day, the Monday for a week, or "YYYY-MM" for a month.
    long long   events   = 0;
    long long   sessions = 0;
    long long   activeMs = 0;
};

/// @brief One named count in a ranking.
struct NamedCount {
    std::string name;
    long long   count = 0;
};

/// @brief One lemma and how many lookup pops named it.
struct WordCount {
    std::string lemma;
    long long   lookups = 0;
};

/// @brief The thresholds and list lengths a report is built with.
struct ReportOptions {
    long long   sessionIdleMs = kSessionIdleMs;
    long long   sessionMaxMs  = kSessionMaxMs;
    std::size_t topWords      = kReportTopWords;
};

/**
 * @brief What the behavior log holds, aggregated for a report.
 *
 * `events` and every period's event total count every registered event, including the
 * application's start and exit. The hour histogram and the feature ranking count activity events
 * only: an application lifecycle event is not a reading session and would otherwise put a spike on
 * the launch and quit hours. Sessions split on a gap longer than ReportOptions::sessionIdleMs and
 * count from the first activity event to the last, capped at ReportOptions::sessionMaxMs; a session
 * belongs to the day, week and month of its start. Top words are lookup pops, the event that says the
 * reader looked a word up.
 */
struct Report {
    long long                 firstTsMs = 0;
    long long                 lastTsMs  = 0;
    long long                 events    = 0; ///< Registered events counted.
    long long                 ignored   = 0; ///< Events dropped: unknown names, or no local moment.
    long long                 sessions  = 0;
    long long                 activeMs  = 0;
    std::array<long long, 24> hours{};  ///< Activity events per local hour, 0 through 23.
    std::vector<PeriodTotal>  days;     ///< Non-empty local days, oldest first.
    std::vector<PeriodTotal>  weeks;    ///< Non-empty weeks, oldest first.
    std::vector<PeriodTotal>  months;   ///< Non-empty months, oldest first.
    std::vector<WordCount>    topWords; ///< Most looked-up lemmas, ties alphabetical.
    std::vector<NamedCount>   features; ///< Activity events by name, most first, ties alphabetical.
    std::vector<NamedCount>   apps;     ///< Events carrying a foreground app, most first, ties alphabetical.
};

/**
 * @brief Aggregate @p events into a report.
 *
 * @param events   Events from any number of month files and devices.
 * @param localize Resolves an instant into the reader's calendar. Empty means UTC.
 * @param options  Thresholds and list lengths; the defaults are the report's own.
 * @return The report. Duplicate `(device, seq)` events count once, so the same event downloaded
 *         twice adds nothing.
 */
Report buildReport(std::vector<Event>   events,
                   Localize const&      localize,
                   ReportOptions const& options = {});

/**
 * @brief Aggregate every month file @p log holds.
 *
 * The log skips a malformed line, so one truncated write from a crash costs that line and not the
 * report around it.
 */
Report buildReportFromLog(EventLog const&      log,
                          Localize const&      localize,
                          ReportOptions const& options = {});

/// @return The report as the JSON a developer surface or a report view reads.
nlohmann::json reportJson(Report const& report);

}
