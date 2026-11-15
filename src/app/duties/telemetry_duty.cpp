/**
 * @file telemetry_duty.cpp
 * @brief The queue, the foreground app name, the clock and calendar defaults, and the summary and
 *        report behind telemetry_duty.h.
 */

#include "telemetry_duty.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimeZone>

#include <exception>
#include <string>
#include <utility>

#include "capture/foreground_window.h"
#include "core/telemetry/sync_plan.h"
#include "util/log.h"

namespace lens::app {

TelemetryDuty::TelemetryDuty(std::filesystem::path root,
                             std::string_view      deviceId,
                             std::function<bool()> recordAppName,
                             ForegroundApp         foregroundApp,
                             Clock                 clock,
                             Localize              localize,
                             QObject*              parent)
    : QObject(parent),
      recordAppName_(std::move(recordAppName)),
      foregroundApp_(std::move(foregroundApp)),
      clock_(std::move(clock)),
      localize_(std::move(localize)),
      log_(std::move(root), deviceId)
{
    if (!foregroundApp_) {
        foregroundApp_ = [] {
            const auto window = capture::foregroundWindow();
            return window ? window->processName.toStdString() : std::string{};
        };
    }
    if (!clock_) {
        clock_ = [] {
            const QDateTime now = QDateTime::currentDateTime();
            return Now{now.toMSecsSinceEpoch(), now.date().toString(QStringLiteral("yyyy-MM")).toStdString()};
        };
    }
    if (!localize_) {
        localize_ = [](long long tsMs) {
            QDateTime local = QDateTime::fromMSecsSinceEpoch(tsMs, QTimeZone::LocalTime);
            if (!local.isValid())
                local = QDateTime::fromMSecsSinceEpoch(tsMs, QTimeZone::UTC);
            if (!local.isValid())
                return core::telemetry::LocalMoment{};
            return core::telemetry::LocalMoment{local.date().toString(Qt::ISODate).toStdString(), local.time().hour()};
        };
    }

    lastMonth_ = clock_().month;
    if (!lastMonth_.empty())
        log_.prune(lastMonth_);

    connect(this, &TelemetryDuty::flushRequested, this, &TelemetryDuty::flush);
    // A unit test builds the duty without an event loop; the timer would warn and never fire there.
    if (QCoreApplication::instance() != nullptr) {
        flushTimer_.setInterval(kFlushIntervalMs);
        connect(&flushTimer_, &QTimer::timeout, this, &TelemetryDuty::flush);
        flushTimer_.start();
    }
}

TelemetryDuty::~TelemetryDuty()
{
    flush();
}

bool TelemetryDuty::acceptsApp(core::telemetry::EventSpec const& spec)
{
    for (core::telemetry::FieldSpec const& field : spec.fields)
        if (field.name == "app")
            return true;
    return false;
}

void TelemetryDuty::record(std::string_view name, nlohmann::json data)
{
    const core::telemetry::EventSpec* const spec = core::telemetry::findEventSpec(name);
    if (spec == nullptr) {
        LENS_WARN("telemetry: refusing unregistered event '{}'", name);
        return;
    }

    const Now now = clock_ ? clock_() : Now{};
    Pending   pending{std::string(name), now.tsMs, now.month, std::move(data), {}};
    // The foreground read is a Win32 query and safe wherever record() runs; whether the name is
    // written is the reader's switch, read at flush on the duty's thread.
    if (acceptsApp(*spec) && pending.data.is_object() && !pending.data.contains("app") && foregroundApp_)
        pending.app = foregroundApp_();

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        queue_.push_back(std::move(pending));
        if (queue_.size() < kFlushCount)
            return;
    }
    emit flushRequested();
}

void TelemetryDuty::flush() noexcept
{
    try {
        std::vector<Pending> batch;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            batch.swap(queue_);
        }
        if (batch.empty())
            return;

        const bool recordApp = recordAppName_ && recordAppName_();
        for (Pending& event : batch) {
            nlohmann::json                          data = std::move(event.data);
            const core::telemetry::EventSpec* const spec = core::telemetry::findEventSpec(event.name);
            if (spec != nullptr && acceptsApp(*spec) && data.is_object()) {
                if (!recordApp) {
                    data.erase("app");
                } else if (!data.contains("app") && !event.app.empty()) {
                    data["app"] = event.app;
                }
            }
            log_.append(event.name, event.tsMs, event.month, std::move(data));
        }
        log_.flush();

        const Now current = clock_();
        if (!current.month.empty() && current.month != lastMonth_) {
            lastMonth_ = current.month;
            log_.prune(lastMonth_);
        }
        emit eventBytesChanged();
    } catch (std::exception const& error) {
        // A desktop app does not die because its own history file could not be written; the next
        // batch tries again and the log says why this one did not.
        LENS_ERROR("telemetry: cannot write the event log: {}", error.what());
    } catch (...) {
        LENS_ERROR("telemetry: the event log failed for an unknown reason");
    }
}

nlohmann::json TelemetryDuty::summary() const
{
    const std::vector<std::string> months = log_.months();
    nlohmann::json                 events = nlohmann::json::object();
    long long                      total  = 0;
    for (const std::string& month : months) {
        for (const auto& event : log_.readMonth(month)) {
            events[event.name] = events.value(event.name, 0) + 1;
            ++total;
        }
    }
    return {{"ok", true}, {"months", months}, {"events", std::move(events)}, {"total", total}};
}

nlohmann::json TelemetryDuty::report() const
{
    nlohmann::json result = core::telemetry::reportJson(core::telemetry::buildReportFromLog(log_, localize_));
    result["ok"]          = true;
    return result;
}

qint64 TelemetryDuty::eventBytes() const
{
    return static_cast<qint64>(log_.totalBytes());
}

void TelemetryDuty::clear()
{
    flush();

    core::telemetry::SyncState sync = core::telemetry::syncStateFromJson(log_.state());
    for (auto const& [month, record] : sync.uploaded)
        sync.pendingDeletes.insert(record.file);
    sync.uploaded.clear();
    log_.clear();
    try {
        core::telemetry::writeSyncState(log_.state(), sync);
        log_.saveState();
    } catch (std::exception const& error) {
        LENS_ERROR("telemetry: cannot save the clear's cloud deletion queue: {}", error.what());
    }

    emit eventBytesChanged();
    emit cleared();
}

}
