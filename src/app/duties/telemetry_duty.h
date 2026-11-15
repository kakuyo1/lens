#pragma once

#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/telemetry/event_log.h"
#include "core/telemetry/report.h"
#include "telemetry_sink.h"

/**
 * @file telemetry_duty.h
 * @brief The behavior-event sink: a queue written to the local month log in small batches.
 *
 * The duty owns what only the application knows -- which device this is, whether the reader lets a
 * foreground app name ride along, and what the clock and the local month are. The log and the
 * event schema stay in core, so the file format is testable without Qt.
 *
 * @note record() is safe from any thread; flush() must run on the duty's own thread, where the
 *       timer and the application's exit hook call it.
 */

namespace lens::app {

class TelemetryDuty final : public QObject, public TelemetrySink {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.telemetryDuty")
    Q_PROPERTY(qint64 eventBytes READ eventBytes NOTIFY eventBytesChanged)
public:
    /// @brief One clock reading: the event's time and the local month whose file takes it.
    struct Now {
        long long   tsMs = 0;
        std::string month;
    };

    /// @return The foreground process name, or empty when there is none.
    using ForegroundApp = std::function<std::string()>;
    using Clock         = std::function<Now()>;
    using Localize      = core::telemetry::Localize;

    /// @brief How often the queue is written out, in milliseconds.
    static constexpr int kFlushIntervalMs = 2000;
    /// @brief How many queued events ask for a write before the timer's turn.
    static constexpr std::size_t kFlushCount = 64;

    /**
     * @brief Bind the duty to its log and the facts only the application can supply.
     *
     * @param root          Directory the month files and state.json live in.
     * @param deviceId      The sync device id; its first eight SHA-1 digits go on every line.
     * @param recordAppName Whether a foreground app name may ride along.
     * @param foregroundApp The foreground process name. The default reads the Win32 foreground.
     * @param clock         Now and the local month. The default reads the system clock.
     * @param localize      An instant as the reader's local calendar. The default reads the system zone.
     */
    TelemetryDuty(std::filesystem::path root,
                  std::string_view      deviceId,
                  std::function<bool()> recordAppName,
                  ForegroundApp         foregroundApp = {},
                  Clock                 clock         = {},
                  Localize              localize      = {},
                  QObject*              parent        = nullptr);
    ~TelemetryDuty() override;

    /// @brief Validate the name, capture the foreground app the event may carry, and queue it.
    /// @note The reader's switch is read at flush, on the duty's own thread, so a record from
    ///       another thread never touches the settings document.
    void record(std::string_view name, nlohmann::json data) override;

    /// @brief Write the queue and the state now.
    /// @note Never throws: a failed write is logged and the next batch tries again.
    void flush() noexcept;

    /// @return Counts by event name over every month file, for the developer surface and reports.
    nlohmann::json summary() const;

    /// @return The month files aggregated into a report, with this duty's local calendar.
    /// @note Reads what the log holds; flush() first to include events still in the queue.
    nlohmann::json report() const;

    /// @return The bytes the month files hold, for the About page's storage line.
    qint64 eventBytes() const;

    /// @brief Queue the uploaded event files for cloud deletion and remove the local month files.
    /// @note The sequence continues, so a later event never reuses a number this device wrote before.
    Q_INVOKABLE void clear();

    /// @return The local log, read-only.
    const core::telemetry::EventLog& log() const
    {
        return log_;
    }

    /// @return The same log, for the event sync's state bookkeeping. The application calls this on
    ///         the duty's own thread; the log is not thread-safe.
    core::telemetry::EventLog& log()
    {
        return log_;
    }

signals:
    /// @brief The queue reached kFlushCount; the duty's thread writes it out.
    void flushRequested();
    /// @brief The local event bytes changed, by a flush or a clear.
    void eventBytesChanged();
    /// @brief clear() removed the local months and queued their cloud files; the event sync may run.
    void cleared();

private:
    struct Pending {
        std::string    name;
        long long      tsMs = 0;
        std::string    month;
        nlohmann::json data;
        /// The foreground app read when the event happened; the switch decides at flush.
        std::string app;
    };

    /// @return Whether @p spec lists an `app` field for the sink to fill.
    static bool acceptsApp(const core::telemetry::EventSpec& spec);

    std::function<bool()>     recordAppName_;
    ForegroundApp             foregroundApp_;
    Clock                     clock_;
    Localize                  localize_;
    core::telemetry::EventLog log_;
    mutable std::mutex        queueMutex_;
    std::vector<Pending>      queue_;
    QTimer                    flushTimer_;
    std::string               lastMonth_;
};

}
