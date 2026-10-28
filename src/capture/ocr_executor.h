/** @file ocr_executor.h
 * @brief One owned worker thread for local recognition, replacing a detached thread per request.
 */
#pragma once

#include <QObject>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "ocr_engine.h"

namespace lens::capture {

/**
 * @brief Runs at most one recognition task at a time on a thread this object owns.
 *
 * The task is a closure rather than a bare request because a probe and a recognition share the
 * same scheduling, cancellation and delivery rules. The result is delivered on the thread that
 * owns this object; a cancelled task still finishes, so the caller can tell "cancelled" from
 * "not started" without guessing.
 */
class OcrExecutor final : public QObject {
    Q_OBJECT
public:
    using Work = std::function<OcrResult(std::shared_ptr<std::atomic_bool>)>;

    explicit OcrExecutor(QObject* parent = nullptr);
    ~OcrExecutor() override;

    /// @brief Queue @p work; false when a task is already queued or running.
    bool start(std::uint64_t id, Work work);
    /// @brief Ask the running task to stop and drop one that has not started yet.
    void cancel();
    /// @return Whether a task is queued or running.
    bool busy() const;

signals:
    void finished(std::uint64_t id, lens::capture::OcrResult result);

private:
    struct Job {
        std::uint64_t id = 0;
        Work          work;
    };

    void run();

    mutable std::mutex                mutex_;
    std::condition_variable           wake_;
    std::optional<Job>                pending_;
    std::shared_ptr<std::atomic_bool> cancelToken_;
    bool                              stopping_ = false;
    std::atomic_bool                  running_{false};
    std::thread                       worker_;
};

}
