/** @file ocr_executor.cpp
 * @brief The worker loop behind OcrExecutor.
 */
#include "ocr_executor.h"

#include <QMetaObject>

#include <utility>

namespace lens::capture {

OcrExecutor::OcrExecutor(QObject* parent)
    : QObject(parent), worker_([this] { run(); })
{}

OcrExecutor::~OcrExecutor()
{
    {
        std::lock_guard lock{mutex_};
        stopping_ = true;
        if (cancelToken_) cancelToken_->store(true, std::memory_order_relaxed);
        pending_.reset();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

bool OcrExecutor::start(std::uint64_t const id, Work work)
{
    {
        std::lock_guard lock{mutex_};
        if (stopping_ or pending_ or running_.load(std::memory_order_relaxed)) return false;
        pending_ = Job{.id = id, .work = std::move(work)};
    }
    wake_.notify_one();
    return true;
}

void OcrExecutor::cancel()
{
    {
        std::lock_guard lock{mutex_};
        if (cancelToken_) cancelToken_->store(true, std::memory_order_relaxed);
        pending_.reset();
    }
}

bool OcrExecutor::busy() const
{
    std::lock_guard lock{mutex_};
    return pending_.has_value() or running_.load(std::memory_order_relaxed);
}

void OcrExecutor::run()
{
    while (true) {
        Job                               job;
        std::shared_ptr<std::atomic_bool> cancel;
        {
            std::unique_lock lock{mutex_};
            wake_.wait(lock, [this] { return stopping_ or pending_.has_value(); });
            if (stopping_ and not pending_) return;
            job          = std::move(*pending_);
            cancel       = std::make_shared<std::atomic_bool>(false);
            cancelToken_ = cancel;
            running_.store(true, std::memory_order_relaxed);
            pending_.reset();
        }

        OcrResult result;
        try {
            result = job.work(cancel);
        } catch (...) {
            result.status = OcrStatus::Failed;
        }

        {
            std::lock_guard lock{mutex_};
            cancelToken_.reset();
        }
        running_.store(false, std::memory_order_relaxed);

        // Delivered on the owner's thread; the context object drops the event if this executor
        // is already gone, so a late answer can never land on a destroyed duty.
        QMetaObject::invokeMethod(
            this,
            [this, id = job.id, result = std::move(result)]() mutable { emit finished(id, std::move(result)); },
            Qt::QueuedConnection);
    }
}

}
