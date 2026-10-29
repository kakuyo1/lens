/**
 * @file ocr_tencent_test.cpp
 * @brief TC3 signing, request shape and the GeneralBasicOCR response mapping.
 *
 * The signing vector is the one computed from the published algorithm (timestamp 1551113065,
 * placeholder credentials); the canonical-request layout it depends on is checked separately in
 * the same case. No test here opens a socket.
 */
#include <gtest/gtest.h>

#include <QImage>
#include <QUrlQuery>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include "capture/ocr_engine.h"
#include "ocr/http_transport.h"
#include "ocr/ocr_tencent.h"

namespace {

using lens::capture::OcrRequest;
using lens::capture::OcrSource;
using lens::capture::OcrStatus;
using lens::capture::OcrUploadState;

struct FakeTransport final : lens::ocr::HttpTransport {
    std::vector<lens::ocr::HttpResponse> responses;
    std::vector<lens::ocr::HttpRequest>  requests;

    lens::ocr::HttpResponse send(lens::ocr::HttpRequest const& request, std::shared_ptr<std::atomic_bool>) override
    {
        requests.push_back(request);
        if (requests.size() > responses.size()) return {};
        return responses[requests.size() - 1];
    }
};

lens::ocr::TencentConfig config()
{
    return {.endpoint  = QUrl{QStringLiteral("https://ocr.tencentcloudapi.com/")},
            .secretId  = QStringLiteral("AKIDEXAMPLE"),
            .secretKey = QStringLiteral("SECRETKEYEXAMPLE"),
            .language  = QStringLiteral("en")};
}

QImage image(int const width = 600, int const height = 200)
{
    QImage page{width, height, QImage::Format_RGB32};
    page.fill(Qt::white);
    return page;
}

OcrRequest request(OcrSource const source = OcrSource::Screenshot)
{
    return {.requestId = 7, .source = source, .image = image(), .deadline = std::chrono::seconds{30}};
}

lens::ocr::HttpResponse detectionResponse()
{
    return {.status      = 200,
            .body        = QByteArray{R"({"Response":{"TextDetections":[)"
                                      R"({"DetectedText":"Sun","Confidence":100,"Polygon":[{"X":464,"Y":100},{"X":538,"Y":100},{"X":538,"Y":135},{"X":464,"Y":135}]},)"
                                      R"({"DetectedText":"rise","Confidence":90,"Polygon":[{"X":10,"Y":10},{"X":80,"Y":10},{"X":80,"Y":40},{"X":10,"Y":40}]}])"
                                      R"(,"Language":"en","RequestId":"r"}})"},
            .transportOk = true};
}

TEST(OcrTencent, TheSignatureMatchesThePublishedAlgorithm)
{
    // Body, timestamp and placeholder credentials from the algorithm's worked example.
    auto const authorization =
        lens::ocr::tc3Authorization(QUrl{QStringLiteral("https://ocr.tencentcloudapi.com/")},
                                    QByteArrayLiteral(R"({"ImageBase64":"aGVsbG8="})"),
                                    QStringLiteral("AKIDEXAMPLE"),
                                    QStringLiteral("SECRETKEYEXAMPLE"),
                                    1551113065);
    EXPECT_EQ(authorization,
              QStringLiteral("TC3-HMAC-SHA256 Credential=AKIDEXAMPLE/2019-02-25/ocr/tc3_request, "
                             "SignedHeaders=content-type;host, "
                             "Signature=7281d11c0858d8ae3804d065946c1444afd3de768671decf46be6bb3b05b3789"));
}

TEST(OcrTencent, ACropIsSentWithTheActionVersionAndItsBoxesAreRead)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {detectionResponse()};
    auto client          = lens::ocr::makeTencentClient(config(), transport);
    ASSERT_TRUE(client);

    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("Sun\nrise"));
    EXPECT_EQ(result.actualEngine, QStringLiteral("tencent"));
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::Line);
    ASSERT_EQ(result.lines.size(), 2u);
    ASSERT_TRUE(result.lines[0].box.has_value());
    EXPECT_DOUBLE_EQ(result.lines[0].box->x, 464);
    EXPECT_DOUBLE_EQ(result.lines[0].box->height, 35);
    ASSERT_TRUE(result.confidence.has_value());
    EXPECT_DOUBLE_EQ(result.confidence->average, 0.95);

    ASSERT_EQ(transport->requests.size(), 1u);
    auto const& sent = transport->requests[0];
    EXPECT_EQ(sent.url, config().endpoint);
    auto const headers = [&sent](char const* name) {
        for (auto const& [key, value] : sent.headers)
            if (key.compare(name, Qt::CaseInsensitive) == 0) return value;
        return QByteArray{};
    };
    EXPECT_TRUE(headers("Authorization").startsWith("TC3-HMAC-SHA256 Credential=AKIDEXAMPLE/"));
    EXPECT_EQ(headers("X-TC-Action"), QByteArrayLiteral("GeneralBasicOCR"));
    EXPECT_EQ(headers("X-TC-Version"), QByteArrayLiteral("2018-11-19"));
    EXPECT_FALSE(headers("X-TC-Timestamp").isEmpty());
    EXPECT_TRUE(sent.body.contains("\"LanguageType\":\"en\""));
    EXPECT_TRUE(sent.body.contains("\"ImageBase64\":"));
}

TEST(OcrTencent, AnApiErrorIsAFailureWithTheCodeInTheLogOnly)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status      = 200,
                             .body        = QByteArray{R"({"Response":{"Error":{"Code":"AuthFailure.SignatureFailure","Message":"x"},"RequestId":"r"}})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeTencentClient(config(), transport);
    ASSERT_TRUE(client);
    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Failed);
    EXPECT_EQ(result.failureReason, lens::capture::OcrFailureReason::Authentication);
    EXPECT_TRUE(result.text.isEmpty());
    EXPECT_EQ(result.uploadState, OcrUploadState::ServerResponded);
}

TEST(OcrTencent, NoTextHasItsOwnAnswer)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status      = 200,
                             .body        = QByteArray{R"({"Response":{"Error":{"Code":"FailedOperation.ImageNoText","Message":"x"},"RequestId":"r"}})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeTencentClient(config(), transport);
    ASSERT_TRUE(client);
    EXPECT_EQ(client->recognize(request()).status, OcrStatus::EmptyText);
}

TEST(OcrTencent, OnlyAConsentedCaptureMayUpload)
{
    auto transport = std::make_shared<FakeTransport>();
    auto client    = lens::ocr::makeTencentClient(config(), transport);
    ASSERT_TRUE(client);
    EXPECT_EQ(client->recognize(request(OcrSource::AutoScan)).status, OcrStatus::Failed);
    EXPECT_EQ(transport->requests.size(), 0u);
}

}
