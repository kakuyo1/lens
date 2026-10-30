/**
 * @file ocr_adapter_test.cpp
 * @brief The shared adapter frame: the preflight ladder, the HTTP classes, the finished result.
 *
 * ADR 0136 promises that a refusal's classification travels with the result. The cases here lock
 * the readings every provider borrows, so the quota answer can only ever change in one place.
 */
#include <gtest/gtest.h>

#include <QImage>
#include <QString>

#include <atomic>
#include <chrono>
#include <memory>

#include "capture/ocr_engine.h"
#include "ocr/ocr_adapter.h"

namespace {

using lens::capture::OcrBox;
using lens::capture::OcrFailureReason;
using lens::capture::OcrGeometryLevel;
using lens::capture::OcrRequest;
using lens::capture::OcrSource;
using lens::capture::OcrStatus;
using lens::capture::OcrUploadState;
using lens::capture::OcrWord;

QImage image(int const width = 200, int const height = 100)
{
    QImage page{width, height, QImage::Format_RGB32};
    page.fill(Qt::white);
    return page;
}

OcrRequest request()
{
    return {.requestId = 7, .source = OcrSource::Screenshot, .image = image(), .deadline = std::chrono::seconds{30}};
}

lens::capture::OcrResult readyAnswer()
{
    return lens::ocr::ocrResultFor(QStringLiteral("test"), OcrStatus::Ready, 7, OcrUploadState::ServerResponded);
}

TEST(OcrAdapter, AnHttpRefusalMeansTheSameThingAtEveryService)
{
    auto const quota = lens::ocr::classifyOcrHttpStatus(429);
    ASSERT_TRUE(quota);
    EXPECT_EQ(quota->status, OcrStatus::Failed);
    EXPECT_EQ(quota->reason, OcrFailureReason::Quota);

    auto const auth = lens::ocr::classifyOcrHttpStatus(401);
    ASSERT_TRUE(auth);
    EXPECT_EQ(auth->status, OcrStatus::Failed);
    EXPECT_EQ(auth->reason, OcrFailureReason::Authentication);

    auto const redirect = lens::ocr::classifyOcrHttpStatus(302);
    ASSERT_TRUE(redirect);
    EXPECT_EQ(redirect->status, OcrStatus::Failed);
    EXPECT_EQ(redirect->reason, OcrFailureReason::None) << "a redirect is never a followed one";

    EXPECT_FALSE(lens::ocr::classifyOcrHttpStatus(200));
    EXPECT_FALSE(lens::ocr::classifyOcrHttpStatus(400)) << "a body-driven service reads its own error shape";
    EXPECT_FALSE(lens::ocr::classifyOcrHttpStatus(500));
}

TEST(OcrAdapter, ARefusedResponseCarriesTheClassificationWithTheResult)
{
    lens::ocr::HttpResponse const response{.status = 429, .transportOk = true};
    auto const                    refused = lens::ocr::refuseOcrResponse(response, QStringLiteral("test"), 7);
    ASSERT_TRUE(refused);
    EXPECT_EQ(refused->status, OcrStatus::Failed);
    EXPECT_EQ(refused->failureReason, OcrFailureReason::Quota);
    EXPECT_EQ(refused->uploadState, OcrUploadState::ServerResponded);
    EXPECT_EQ(refused->requestId, 7u);
    EXPECT_EQ(refused->actualEngine, QStringLiteral("test"));
}

TEST(OcrAdapter, AnAnsweredResponseYieldsNoRefusal)
{
    lens::ocr::HttpResponse const answered{.status = 200, .body = QByteArrayLiteral("{}"), .transportOk = true};
    EXPECT_FALSE(lens::ocr::refuseOcrResponse(answered, QStringLiteral("test"), 7));
}

TEST(OcrAdapter, ATransportOutcomeKeepsItsStatusAndSaysWhereTheBytesWent)
{
    lens::ocr::HttpResponse const dropped{.transportOk = false};
    auto const                    failed = lens::ocr::refuseOcrTransport(dropped, QStringLiteral("test"), 7);
    ASSERT_TRUE(failed);
    EXPECT_EQ(failed->status, OcrStatus::TransportFailed);
    EXPECT_EQ(failed->uploadState, OcrUploadState::UploadStarted);

    lens::ocr::HttpResponse const answered{.status = 200, .transportOk = true};
    auto const                    cancelled =
        lens::ocr::refuseOcrTransport({.status = 200, .transportOk = true, .cancelled = true}, QStringLiteral("test"), 7);
    ASSERT_TRUE(cancelled);
    EXPECT_EQ(cancelled->status, OcrStatus::Cancelled);
    EXPECT_EQ(cancelled->uploadState, OcrUploadState::ServerResponded);
    EXPECT_EQ(lens::ocr::refuseOcrTransport(answered, QStringLiteral("test"), 7), std::nullopt);
}

TEST(OcrAdapter, ThePreflightRefusesBeforeAnythingIsEncoded)
{
    auto const refusal = [](OcrRequest const& request, lens::ocr::OcrImageLimits const& limits, bool const usable) {
        return lens::ocr::refuseOcrRequest(request, QStringLiteral("test"), limits, nullptr, usable);
    };

    auto scan    = request();
    scan.source  = OcrSource::AutoScan;
    auto refused = refusal(scan, {}, true);
    ASSERT_TRUE(refused);
    EXPECT_EQ(refused->status, OcrStatus::Failed);
    EXPECT_EQ(refused->uploadState, OcrUploadState::NotUploaded) << "a scan source never uploads";

    refused = refusal(request(), {}, false);
    ASSERT_TRUE(refused);
    EXPECT_EQ(refused->status, OcrStatus::Failed);

    auto blank  = request();
    blank.image = QImage{};
    EXPECT_EQ(refusal(blank, {}, true)->status, OcrStatus::InvalidImage);

    auto wide  = request();
    wide.image = image(5000, 100);
    EXPECT_EQ(refusal(wide, {.maxEdge = 4096}, true)->status, OcrStatus::InputTooLarge);
    auto thin  = request();
    thin.image = image(200, 10);
    EXPECT_EQ(refusal(thin, {.minEdge = 15}, true)->status, OcrStatus::InputTooLarge);
    EXPECT_EQ(refusal(wide, {.maxBytes = 1000}, true)->status, OcrStatus::InputTooLarge);
    EXPECT_FALSE(refusal(wide, {}, true)) << "a service without the bound does not refuse for it";

    auto flag = std::make_shared<std::atomic_bool>(true);
    EXPECT_EQ(lens::ocr::refuseOcrRequest(request(), QStringLiteral("test"), {}, flag, true)->status, OcrStatus::Cancelled);

    auto overdue     = request();
    overdue.deadline = std::chrono::milliseconds{0};
    EXPECT_EQ(refusal(overdue, {}, true)->status, OcrStatus::TimedOut);

    EXPECT_FALSE(refusal(request(), {.maxEdge = 8192, .minEdge = 15}, true));
}

TEST(OcrAdapter, AFinishedAnswerCarriesTheGeometryItsLinesReallyHave)
{
    auto answer  = readyAnswer();
    answer.text  = QStringLiteral("  harbour clock  ");
    answer.lines = {{.text = QStringLiteral("harbour clock"), .box = OcrBox{.x = 1, .y = 2, .width = 3, .height = 4}}};

    auto finished = lens::ocr::finishOcrResult(std::move(answer));
    EXPECT_EQ(finished.status, OcrStatus::Ready);
    EXPECT_EQ(finished.text, QStringLiteral("harbour clock"));
    EXPECT_EQ(finished.geometryLevel, OcrGeometryLevel::Line);
    EXPECT_EQ(finished.actualEngine, QStringLiteral("test"));
    EXPECT_EQ(finished.requestId, 7u);
    EXPECT_EQ(finished.uploadState, OcrUploadState::ServerResponded);
}

TEST(OcrAdapter, WordBoxesOutrankLineBoxesInTheFinishedGeometry)
{
    auto answer  = readyAnswer();
    answer.text  = QStringLiteral("harbour clock");
    answer.lines = {{.text  = QStringLiteral("harbour clock"),
                     .box   = OcrBox{.x = 1, .y = 2, .width = 3, .height = 4},
                     .words = {OcrWord{.text = QStringLiteral("harbour"), .box = OcrBox{.x = 1, .y = 2, .width = 1, .height = 4}}}}};

    EXPECT_EQ(lens::ocr::finishOcrResult(std::move(answer)).geometryLevel, OcrGeometryLevel::Word);
}

TEST(OcrAdapter, AnAnswerWithoutBoxesIsTextOnly)
{
    auto answer  = readyAnswer();
    answer.text  = QStringLiteral("harbour clock");
    answer.lines = {{.text = QStringLiteral("harbour clock")}};

    EXPECT_EQ(lens::ocr::finishOcrResult(std::move(answer)).geometryLevel, OcrGeometryLevel::None);
}

TEST(OcrAdapter, ABlankAnswerIsEmptyTextRatherThanReady)
{
    auto answer = readyAnswer();
    answer.text = QStringLiteral("   ");

    auto const finished = lens::ocr::finishOcrResult(std::move(answer));
    EXPECT_EQ(finished.status, OcrStatus::EmptyText);
    EXPECT_TRUE(finished.text.isEmpty());
    EXPECT_TRUE(finished.lines.empty());
}

TEST(OcrAdapter, AnAnswerOverTheCapsIsRefusedRatherThanCarried)
{
    auto wordy = readyAnswer();
    wordy.text = QString(static_cast<int>(lens::ocr::kOcrMaxTextUnits) + 1, QLatin1Char('x'));
    EXPECT_EQ(lens::ocr::finishOcrResult(std::move(wordy)).status, OcrStatus::ResultTooLarge);

    auto structured = readyAnswer();
    structured.text = QStringLiteral("harbour clock");
    for (int i = 0; i < 1024; ++i) {
        // 1024 lines of 4 KiB each clear the 4 MiB structured budget.
        structured.lines.push_back({.text = QString(4096, QLatin1Char('a')), .box = OcrBox{}});
    }
    EXPECT_EQ(lens::ocr::finishOcrResult(std::move(structured)).status, OcrStatus::ResultTooLarge);
}

}
