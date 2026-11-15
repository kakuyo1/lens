/**
 * @file report.cpp
 * @brief Local calendar grouping, session splitting and the rankings behind core/telemetry/report.h.
 */

#include "core/telemetry/report.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace lens::core::telemetry {
namespace {

constexpr long long kDayMs = 24LL * 60 * 60 * 1000;

struct CivilDate {
    int year  = 0;
    int month = 0;
    int day   = 0;
};

bool isLeapYear(int const year)
{
    return (year % 4 == 0 and year % 100 != 0) or year % 400 == 0;
}

int daysInMonth(int const year, int const month) // NOLINT(bugprone-easily-swappable-parameters): a month is only meaningful with its year, and the two are never passed in the other order
{
    constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 and isLeapYear(year))
        return 29;
    return month >= 1 and month <= 12 ? kDays[month - 1] : 0;
}

std::optional<CivilDate> parseDate(std::string_view const text)
{
    if (text.size() != 10 or text[4] != '-' or text[7] != '-')
        return std::nullopt;

    auto const digit = [&text](std::size_t const at) { return text[at] >= '0' and text[at] <= '9'; };
    for (std::size_t const at : {0, 1, 2, 3, 5, 6, 8, 9})
        if (not digit(at))
            return std::nullopt;

    const int year  = (text[0] - '0') * 1000 + (text[1] - '0') * 100 + (text[2] - '0') * 10 + (text[3] - '0');
    const int month = (text[5] - '0') * 10 + (text[6] - '0');
    const int day   = (text[8] - '0') * 10 + (text[9] - '0');
    if (year < 1 or month < 1 or month > 12 or day < 1 or day > daysInMonth(year, month))
        return std::nullopt;
    return CivilDate{year, month, day};
}

std::string twoDigits(int const value)
{
    return std::string{value < 10 ? "0" : ""} + std::to_string(value);
}

std::string dateText(CivilDate const& date)
{
    return std::to_string(date.year) + "-" + twoDigits(date.month) + "-" + twoDigits(date.day);
}

// Howard Hinnant's civil-calendar algorithms: days since 1970-01-01 and back.
int daysFromCivil(CivilDate const& date)
{
    int const      year      = date.year - (date.month <= 2 ? 1 : 0);
    int const      era       = (year >= 0 ? year : year - 399) / 400;
    unsigned const yearOfEra = static_cast<unsigned>(year - era * 400);
    unsigned const dayOfYear = (153U * static_cast<unsigned>(date.month + (date.month > 2 ? -3 : 9)) + 2U) / 5U + static_cast<unsigned>(date.day) - 1U;
    unsigned const dayOfEra  = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
    return era * 146097 + static_cast<int>(dayOfEra) - 719468;
}

CivilDate civilFromDays(int days)
{
    days += 719468;
    int const      era       = (days >= 0 ? days : days - 146096) / 146097;
    unsigned const dayOfEra  = static_cast<unsigned>(days - era * 146097);
    unsigned const yearOfEra = (dayOfEra - dayOfEra / 1460U + dayOfEra / 36524U - dayOfEra / 146096U) / 365U;
    int const      year      = static_cast<int>(yearOfEra) + era * 400;
    unsigned const dayOfYear = dayOfEra - (365U * yearOfEra + yearOfEra / 4U - yearOfEra / 100U);
    unsigned const monthPart = (5U * dayOfYear + 2U) / 153U;
    unsigned const day       = dayOfYear - (153U * monthPart + 2U) / 5U + 1U;
    unsigned const month     = monthPart < 10U ? monthPart + 3U : monthPart - 9U;
    return CivilDate{year + (month <= 2U ? 1 : 0), static_cast<int>(month), static_cast<int>(day)};
}

/// @return The Monday of @p date's week, as a date string.
std::string weekStart(CivilDate const& date)
{
    const int days = daysFromCivil(date);
    // 1970-01-01 was a Thursday; Monday is the week's first day.
    const int weekday = ((days % 7) + 7 + 3) % 7;
    return dateText(civilFromDays(days - weekday));
}

LocalMoment utcMoment(long long const tsMs)
{
    long long days   = tsMs / kDayMs;
    long long within = tsMs % kDayMs;
    if (within < 0) {
        within += kDayMs;
        --days;
    }
    return LocalMoment{dateText(civilFromDays(static_cast<int>(days))), static_cast<int>(within / (60LL * 60 * 1000))};
}

bool isActivity(std::string_view const name)
{
    return not name.starts_with("app.");
}

void addEvent(std::map<std::string, PeriodTotal>& rows, std::string const& key)
{
    PeriodTotal& row = rows[key];
    row.key          = key;
    ++row.events;
}

void addSession(std::map<std::string, PeriodTotal>& rows, std::string const& key, long long const durationMs)
{
    PeriodTotal& row = rows[key];
    row.key          = key;
    ++row.sessions;
    row.activeMs += durationMs;
}

void sortNamed(std::vector<NamedCount>& rows)
{
    std::sort(rows.begin(), rows.end(), [](NamedCount const& a, NamedCount const& b) {
        return a.count != b.count ? a.count > b.count : a.name < b.name;
    });
}

nlohmann::json periodsJson(std::vector<PeriodTotal> const& rows, char const* keyName)
{
    nlohmann::json array = nlohmann::json::array();
    for (PeriodTotal const& row : rows) {
        nlohmann::json json = nlohmann::json::object();
        json[keyName]       = row.key;
        json["events"]      = row.events;
        json["sessions"]    = row.sessions;
        json["activeMs"]    = row.activeMs;
        array.push_back(std::move(json));
    }
    return array;
}

nlohmann::json namedJson(std::vector<NamedCount> const& rows)
{
    nlohmann::json array = nlohmann::json::array();
    for (NamedCount const& row : rows)
        array.push_back({{"name", row.name}, {"count", row.count}});
    return array;
}

nlohmann::json wordsJson(std::vector<WordCount> const& rows)
{
    nlohmann::json array = nlohmann::json::array();
    for (WordCount const& row : rows)
        array.push_back({{"lemma", row.lemma}, {"lookups", row.lookups}});
    return array;
}

} // namespace

Report buildReport(std::vector<Event> events, Localize const& localize, ReportOptions const& options)
{
    Report         report;
    Localize const zone = localize ? localize : Localize{utcMoment};

    std::sort(events.begin(), events.end(), [](Event const& a, Event const& b) {
        if (a.tsMs != b.tsMs)
            return a.tsMs < b.tsMs;
        if (a.device != b.device)
            return a.device < b.device;
        return a.seq < b.seq;
    });

    std::set<std::pair<std::string, unsigned long long>> seen;
    std::map<std::string, PeriodTotal>                   days;
    std::map<std::string, PeriodTotal>                   weeks;
    std::map<std::string, PeriodTotal>                   months;
    std::map<std::string, long long>                     words;
    std::map<std::string, long long>                     features;
    std::map<std::string, long long>                     apps;

    bool        sessionOpen  = false;
    long long   sessionStart = 0;
    long long   sessionLast  = 0;
    std::string sessionDate;
    std::string sessionWeek;
    std::string sessionMonth;

    auto const closeSession = [&] {
        if (not sessionOpen)
            return;
        const long long duration = std::min(sessionLast - sessionStart, options.sessionMaxMs);
        ++report.sessions;
        report.activeMs += duration;
        addSession(days, sessionDate, duration);
        addSession(weeks, sessionWeek, duration);
        addSession(months, sessionMonth, duration);
        sessionOpen = false;
    };

    for (Event const& event : events) {
        if (not seen.insert({event.device, event.seq}).second)
            continue;
        if (findEventSpec(event.name) == nullptr) {
            ++report.ignored;
            continue;
        }

        LocalMoment const              moment = zone(event.tsMs);
        std::optional<CivilDate> const date   = parseDate(moment.date);
        if (not date or moment.hour < 0 or moment.hour >= 24) {
            ++report.ignored;
            continue;
        }

        ++report.events;
        if (report.events == 1)
            report.firstTsMs = event.tsMs;
        report.lastTsMs = event.tsMs;

        if (auto const app = event.data.find("app"); app != event.data.end() and app->is_string()) {
            auto const& text = app->get_ref<std::string const&>();
            if (not text.empty())
                ++apps[text];
        }
        if (event.name == "lookup.pop") {
            if (auto const lemma = event.data.find("lemma"); lemma != event.data.end() and lemma->is_string()) {
                auto const& text = lemma->get_ref<std::string const&>();
                if (not text.empty())
                    ++words[text];
            }
        }

        const std::string dateKey  = dateText(*date);
        const std::string weekKey  = weekStart(*date);
        const std::string monthKey = dateKey.substr(0, 7);
        addEvent(days, dateKey);
        addEvent(weeks, weekKey);
        addEvent(months, monthKey);

        if (not isActivity(event.name))
            continue;

        ++report.hours[static_cast<std::size_t>(moment.hour)];
        ++features[event.name];

        if (sessionOpen and event.tsMs - sessionLast > options.sessionIdleMs)
            closeSession();
        if (not sessionOpen) {
            sessionOpen  = true;
            sessionStart = event.tsMs;
            sessionDate  = dateKey;
            sessionWeek  = weekKey;
            sessionMonth = monthKey;
        }
        sessionLast = event.tsMs;
    }
    closeSession();

    for (auto const& [key, row] : days)
        report.days.push_back(row);
    for (auto const& [key, row] : weeks)
        report.weeks.push_back(row);
    for (auto const& [key, row] : months)
        report.months.push_back(row);

    for (auto const& [name, count] : features)
        report.features.push_back(NamedCount{name, count});
    sortNamed(report.features);

    for (auto const& [name, count] : apps)
        report.apps.push_back(NamedCount{name, count});
    sortNamed(report.apps);

    for (auto const& [lemma, count] : words)
        report.topWords.push_back(WordCount{lemma, count});
    std::sort(report.topWords.begin(), report.topWords.end(), [](WordCount const& a, WordCount const& b) {
        return a.lookups != b.lookups ? a.lookups > b.lookups : a.lemma < b.lemma;
    });
    if (report.topWords.size() > options.topWords)
        report.topWords.resize(options.topWords);
    return report;
}

Report buildReportFromLog(EventLog const& log, Localize const& localize, ReportOptions const& options)
{
    std::vector<Event> events;
    for (std::string const& month : log.months()) {
        auto monthEvents = log.readMonth(month);
        for (Event& event : monthEvents)
            events.push_back(std::move(event));
    }
    return buildReport(std::move(events), localize, options);
}

nlohmann::json reportJson(Report const& report)
{
    nlohmann::json hours = nlohmann::json::array();
    for (long long const count : report.hours)
        hours.push_back(count);

    return {
        {"firstTs", report.firstTsMs},
        {"lastTs", report.lastTsMs},
        {"events", report.events},
        {"ignored", report.ignored},
        {"sessions", report.sessions},
        {"activeMs", report.activeMs},
        {"hours", std::move(hours)},
        {"days", periodsJson(report.days, "date")},
        {"weeks", periodsJson(report.weeks, "start")},
        {"months", periodsJson(report.months, "month")},
        {"topWords", wordsJson(report.topWords)},
        {"features", namedJson(report.features)},
        {"apps", namedJson(report.apps)},
    };
}

}
