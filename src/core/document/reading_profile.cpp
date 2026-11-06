/**
 * @file reading_profile.cpp
 * @brief Counting one window of the history into a reading profile, and the summary text, for reading_profile.h.
 */

#include "core/document/reading_profile.h"

#include <algorithm>
#include <map>
#include <unordered_map>

#include "util/text.h"

namespace lens::core {
namespace {

/// @return The local hour of a "YYYY-MM-DD HH:MM" minute, or -1 when the minute does not carry one.
int hourOf(const std::string& minute)
{
    if (minute.size() < 13 || minute[10] != ' ')
        return -1;
    const auto digit = [](char c) { return c >= '0' && c <= '9'; };
    if (!digit(minute[11]) || !digit(minute[12]))
        return -1;
    const int hour = (minute[11] - '0') * 10 + (minute[12] - '0');
    return hour < 24 ? hour : -1;
}

}

ReadingProfile buildReadingProfile(const std::vector<HistoryEntry>&                      history,
                                   const std::unordered_set<std::string>&                known,
                                   const std::function<std::string(const std::string&)>& partOfSpeech,
                                   const std::string&                                    fromDate,
                                   const std::string&                                    toDate,
                                   const std::map<std::string, DailyUsage>&              daily)
{
    ReadingProfile                       profile;
    std::unordered_map<std::string, int> lookups;
    for (const HistoryEntry& entry : history) {
        const std::string date = util::datePart(entry.minute);
        if (date < fromDate || date > toDate)
            continue;
        ++profile.pops;
        if (!entry.language.empty()) {
            ++profile.explainedPops;
            if (entry.language != "en")
                ++profile.motherTonguePops;
        }
        if (const int hour = hourOf(entry.minute); hour >= 0)
            ++profile.hours[static_cast<std::size_t>(hour)];
        ++lookups[entry.lemma];
    }
    for (const auto& [date, day] : daily)
        if (date >= fromDate && date <= toDate)
            profile.peeks += day.peeks;

    std::map<std::string, int> parts;
    for (const auto& [lemma, count] : lookups) {
        ++profile.words;
        if (!known.contains(lemma))
            ++profile.fresh;
        if (count >= 2)
            ++profile.repeated;
        const std::string part = partOfSpeech(lemma);
        if (part.empty())
            ++profile.uncached;
        else
            ++parts[part];
        profile.top.push_back(ProfileWord{lemma, count});
    }

    profile.partsOfSpeech.assign(parts.begin(), parts.end());
    std::stable_sort(profile.partsOfSpeech.begin(), profile.partsOfSpeech.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    // Most looked up first, and a tie reads alphabetically so the same history always names the same five.
    std::sort(profile.top.begin(), profile.top.end(), [](const ProfileWord& a, const ProfileWord& b) {
        return a.lookups != b.lookups ? a.lookups > b.lookups : a.lemma < b.lemma;
    });
    for (const ProfileWord& word : profile.top)
        profile.lemmas.push_back(word.lemma);
    if (profile.top.size() > kProfileTopWords)
        profile.top.resize(kProfileTopWords);
    return profile;
}

std::string readingProfileSummary(const ReadingProfile& profile, const std::string& fromDate, const std::string& toDate)
{
    // One paragraph, not a line per fact: the request carries it as one item, and the panel shows the same text before it goes.
    std::string text = "Week " + fromDate + " to " + toDate + ". " + std::to_string(profile.pops) + " lookups, " +
                       std::to_string(profile.words) + " distinct words: " + std::to_string(profile.fresh) + " new (" +
                       std::to_string(sharePercent(profile.fresh, profile.words)) + "%), " + std::to_string(profile.repeated) +
                       " repeated (" + std::to_string(sharePercent(profile.repeated, profile.words)) + "%). ";

    std::string parts;
    for (const auto& [part, count] : profile.partsOfSpeech)
        parts += (parts.empty() ? "" : ", ") + part + " " + std::to_string(count);
    text += "Parts of speech: " + (parts.empty() ? std::string("none cached") : parts) + ", " + std::to_string(profile.uncached) +
            " without one. ";

    std::string hours;
    for (std::size_t hour = 0; hour < profile.hours.size(); ++hour) {
        if (profile.hours[hour] == 0)
            continue;
        const std::string label = hour < 10 ? "0" + std::to_string(hour) : std::to_string(hour);
        hours += (hours.empty() ? "" : ", ") + label + ":00 " + std::to_string(profile.hours[hour]);
    }
    text += "Lookups by hour: " + (hours.empty() ? std::string("none") : hours) + ". ";

    std::string top;
    for (const ProfileWord& word : profile.top)
        top += (top.empty() ? "" : ", ") + word.lemma + " " + std::to_string(word.lookups);
    text += "Most looked up: " + (top.empty() ? std::string("none") : top) + ".";
    return text;
}

}
