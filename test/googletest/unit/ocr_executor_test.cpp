/**
 * @file ocr_executor_test.cpp
 * @brief The one owned worker, offline: one task at a time, the id carried back, and the worker
 *        free again once the task's work has returned.
 */

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include <gtest/gtest.h>

#include "capture/ocr_executor.h"

namespace {

using lens::capture::OcrExecutor;
using lens::capture::OcrResult;
using lens::capture::OcrStatus;

struct QtApplication {
    int              argc     = 1;
    char             name[20] = "ocr_executor_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

/// @return Whether @p done came true within @p ms, turning the loop so the queued answer lands.
bool spinUntil(const std::function<bool()>& done, int ms = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (not done() and clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

OcrResult readyResult()
{
    OcrResult result;
    result.status = OcrStatus::Ready;
    return result;
}

}

TEST(OcrExecutor, OneTaskAtATimeAndTheWorkerComesFreeWhenTheWorkReturns)
{
    QtApplication qt;
    OcrExecutor   executor;

    std::uint64_t finishedId    = 0;
    int           finishedCount = 0;
    QObject::connect(&executor, &OcrExecutor::finished, [&](std::uint64_t id, OcrResult) {
        ++finishedCount;
        finishedId = id;
    });

    std::atomic<int> runs{0};
    const auto       work = [&runs](std::shared_ptr<std::atomic_bool>) {
        ++runs;
        return readyResult();
    };

    ASSERT_TRUE(executor.start(7, work));
    EXPECT_TRUE(executor.busy());
    EXPECT_FALSE(executor.start(8, work)) << "a second task is refused while one is queued or running";

    ASSERT_TRUE(spinUntil([&finishedCount] { return finishedCount == 1; }));
    EXPECT_EQ(finishedId, 7u) << "the answer carries the id the task was started with";
    EXPECT_FALSE(executor.busy()) << "the worker is free once the task's work has returned";
    EXPECT_EQ(runs.load(), 1);

    ASSERT_TRUE(executor.start(9, work)) << "and it takes the next task";
    ASSERT_TRUE(spinUntil([&finishedCount] { return finishedCount == 2; }));
    EXPECT_EQ(finishedId, 9u);
}
