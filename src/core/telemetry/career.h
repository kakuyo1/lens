/**
 * @file career.h
 * @brief Lifetime counters over the whole behavior log: the career statistics page's numbers.
 *
 * Qt-free and stateless: one pass over the events, every registered name contributing to the
 * counters it owns. Unknown names are ignored, as the report's are. Counters are keyed by stable
 * ids the page maps to its own labels; durations and tokens are summed as recorded, in
 * milliseconds and tokens. Every id is present at zero, so a career with no events still lists
 * the whole table.
 */

#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/telemetry/event.h"
#include "core/telemetry/event_log.h"

namespace lens::core::telemetry {

/// @brief One career's counters by id, including `firstTs`, the oldest event's instant (0 when none).
using Career = std::map<std::string, long long>;

/// @return Every counter id at zero, `firstTs` included.
Career emptyCareer();

/// @brief Accumulate one pass over @p events into a fresh career.
Career buildCareer(std::vector<Event> const& events);

/// @brief Accumulate every month the log holds, one month at a time.
Career buildCareerFromLog(EventLog const& log);

/// @return The career as one JSON object of ids to numbers.
nlohmann::json careerJson(Career const& career);

}