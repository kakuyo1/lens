#pragma once

#include <string_view>

#include <nlohmann/json.hpp>

/**
 * @file telemetry_sink.h
 * @brief The one method a duty calls to leave a behavior event.
 *
 * The sink is an interface rather than the telemetry duty itself so a duty test can capture events
 * without a file or a clock. The application binds TelemetryDuty; production code never checks
 * which implementation it holds.
 */

namespace lens::app {

class TelemetrySink {
public:
    virtual ~TelemetrySink() = default;

    /**
     * @brief Queue one behavior event.
     * @param name A registered event name; an unknown one is refused by the log.
     * @param data The event's payload. The sink may add the foreground application name to an
     *        event whose spec lists `app`.
     */
    virtual void record(std::string_view name, nlohmann::json data) = 0;
};

}
