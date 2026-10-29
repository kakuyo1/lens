/**
 * @file ocr_baidu_test.cpp
 * @brief Baidu's token exchange, request shape, response mapping and zero-upload guarantees.
 *
 * Every case uses an injected transport: no test in this file opens a socket. The refusal paths
 * assert that the transport was never called at all.
 */
#include <gtest/gtest.h>

#include <QImage>
#include <QUrlQuery>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

#include "capture/ocr_engine.h"
#include "ocr/http_transport.h"
#include "ocr/ocr_baidu.h"
#include "ocr/online_ocr.h"

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

QImage image(int const width = 200, int const height = 100)
{
    QImage page{width, height, QImage::Format_RGB32};
    page.fill(Qt::white);
    return page;
}

lens::ocr::BaiduConfig config()
{
    return {.endpoint      = QUrl{QStringLiteral("https://aip.baidubce.com/rest/2.0/ocr/v1/general_basic")},
            .tokenEndpoint = QUrl{QStringLiteral("https://aip.baidubce.com/oauth/2.0/token")},
            .apiKey        = QStringLiteral("api-key"),
            .secretKey     = QStringLiteral("secret-key"),
            .language      = QStringLiteral("ENG")};
}

lens::ocr::HttpResponse tokenResponse()
{
    return {.status      = 200,
            .body        = QByteArray{R"({"access_token":"token-123","expires_in":2592000})"},
            .transportOk = true};
}

lens::ocr::HttpResponse linesResponse()
{
    return {.status      = 200,
            .body        = QByteArray{R"({"words_result":[)"
                                      R"({"words":"harbour clock","location":{"left":10,"top":20,"width":100,"height":20}},)"
                                      R"({"words":"second line","location":{"left":10,"top":50,"width":80,"height":20}})"
                                      R"(],"words_result_num":2,"log_id":1})"},
            .transportOk = true};
}

OcrRequest request(OcrSource const source = OcrSource::Screenshot)
{
    return {.requestId = 42, .source = source, .image = image(), .deadline = std::chrono::seconds{30}};
}

TEST(OcrBaidu, ARecognizedCropCarriesLineBoxesAndTheServicesText)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {tokenResponse(), linesResponse()};
    auto tokens          = lens::ocr::makeMemoryOcrTokenStore();
    auto client          = lens::ocr::makeBaiduClient(config(), transport, tokens);
    ASSERT_TRUE(client);

    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("harbour clock\nsecond line"));
    EXPECT_EQ(result.actualEngine, QStringLiteral("baidu"));
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::Line);
    EXPECT_EQ(result.uploadState, OcrUploadState::ServerResponded);
    ASSERT_EQ(result.lines.size(), 2u);
    ASSERT_TRUE(result.lines[0].box.has_value());
    EXPECT_DOUBLE_EQ(result.lines[0].box->x, 10);
    EXPECT_DOUBLE_EQ(result.lines[0].box->width, 100);
    EXPECT_EQ(result.lines[0].text, QStringLiteral("harbour clock"));

    ASSERT_EQ(transport->requests.size(), 2u);
    EXPECT_EQ(transport->requests[0].url, config().tokenEndpoint);
    EXPECT_TRUE(transport->requests[0].body.contains("grant_type=client_credentials"));
    EXPECT_TRUE(transport->requests[0].body.contains("client_id=api-key"));
    EXPECT_TRUE(transport->requests[0].body.contains("client_secret=secret-key"));
    QUrlQuery const query{transport->requests[1].url};
    EXPECT_EQ(query.queryItemValue(QStringLiteral("access_token")), QStringLiteral("token-123"));
    EXPECT_TRUE(transport->requests[1].body.startsWith("image="));
    EXPECT_TRUE(transport->requests[1].body.contains("language_type=ENG"));
}

TEST(OcrBaidu, ATokenIsExchangedOnceAndReused)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {tokenResponse(), linesResponse(), linesResponse()};
    auto tokens          = lens::ocr::makeMemoryOcrTokenStore();
    auto client          = lens::ocr::makeBaiduClient(config(), transport, tokens);
    ASSERT_TRUE(client);

    ASSERT_EQ(client->recognize(request()).status, OcrStatus::Ready);
    ASSERT_EQ(client->recognize(request()).status, OcrStatus::Ready);
    ASSERT_EQ(transport->requests.size(), 3u);
    EXPECT_NE(transport->requests[2].url, config().tokenEndpoint)
        << "the third request is the second recognition, not another token exchange";
}

/// The service omits `location` for some answers -- a single line came back without one in the
/// live check on 2026-10-29 -- and text without a rectangle is still a successful read.
TEST(OcrBaidu, ATextOnlyAnswerWithoutGeometryIsStillASuccess)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {tokenResponse(),
                            {.status      = 200,
                             .body        = QByteArray{R"({"words_result":[{"words":"harbour clock"}],"words_result_num":1})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeBaiduClient(config(), transport, lens::ocr::makeMemoryOcrTokenStore());
    ASSERT_TRUE(client);

    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("harbour clock"));
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::None);
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_FALSE(result.lines[0].box.has_value());
}

TEST(OcrBaidu, ARefusedKeyPairSendsNoPixels)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status      = 200,
                             .body        = QByteArray{R"({"error":"invalid_client","error_description":"unknown client id"})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeBaiduClient(config(), transport, lens::ocr::makeMemoryOcrTokenStore());
    ASSERT_TRUE(client);

    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Failed);
    EXPECT_EQ(result.uploadState, OcrUploadState::NotUploaded);
    EXPECT_EQ(transport->requests.size(), 1u) << "the token exchange is the only request";
}

TEST(OcrBaidu, ServiceErrorCodesMapToTheStatusThePageCanExplain)
{
    struct Case {
        int                             code;
        OcrStatus                       status;
        lens::capture::OcrFailureReason reason;
    };
    for (auto const& expected : {Case{17, OcrStatus::Failed, lens::capture::OcrFailureReason::Quota},
                                 Case{110, OcrStatus::Failed, lens::capture::OcrFailureReason::Authentication},
                                 Case{216201, OcrStatus::InvalidImage, lens::capture::OcrFailureReason::Image},
                                 Case{216202, OcrStatus::InputTooLarge, lens::capture::OcrFailureReason::Image}}) {
        auto transport       = std::make_shared<FakeTransport>();
        transport->responses = {tokenResponse(),
                                {.status      = 200,
                                 .body        = QByteArray{"{\"error_code\":" + QByteArray::number(expected.code) +
                                                    ",\"error_msg\":\"refused\"}"},
                                 .transportOk = true}};
        auto client          = lens::ocr::makeBaiduClient(config(), transport, lens::ocr::makeMemoryOcrTokenStore());
        ASSERT_TRUE(client);
        auto const result = client->recognize(request());
        EXPECT_EQ(result.status, expected.status) << "error_code " << expected.code;
        EXPECT_EQ(result.failureReason, expected.reason) << "error_code " << expected.code;
        EXPECT_TRUE(result.text.isEmpty()) << "an error body is never reader text";
    }
}

TEST(OcrBaidu, ARejectedTokenIsDroppedSoTheNextAttemptExchangesAgain)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {tokenResponse(), {.status = 401, .transportOk = true}, tokenResponse(), linesResponse()};
    auto tokens          = lens::ocr::makeMemoryOcrTokenStore();
    auto client          = lens::ocr::makeBaiduClient(config(), transport, tokens);
    ASSERT_TRUE(client);

    EXPECT_EQ(client->recognize(request()).status, OcrStatus::Failed);
    ASSERT_EQ(client->recognize(request()).status, OcrStatus::Ready);
    ASSERT_EQ(transport->requests.size(), 4u);
    EXPECT_EQ(transport->requests[2].url, config().tokenEndpoint);
}

TEST(OcrBaidu, ARejectedTokenCodeDropsTheCachedTokenToo)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {tokenResponse(),
                            {.status      = 200,
                             .body        = QByteArray{R"({"error_code":111,"error_msg":"access token expired"})"},
                             .transportOk = true},
                            tokenResponse(),
                            linesResponse()};
    auto tokens          = lens::ocr::makeMemoryOcrTokenStore();
    auto client          = lens::ocr::makeBaiduClient(config(), transport, tokens);
    ASSERT_TRUE(client);

    EXPECT_EQ(client->recognize(request()).status, OcrStatus::Failed);
    ASSERT_EQ(client->recognize(request()).status, OcrStatus::Ready);
    EXPECT_EQ(transport->requests.size(), 4u);
    EXPECT_EQ(transport->requests[2].url, config().tokenEndpoint);
}

TEST(OcrBaidu, AnOversizedOrTooSmallCropNeverReachesTheWire)
{
    auto transport = std::make_shared<FakeTransport>();
    auto client    = lens::ocr::makeBaiduClient(config(), transport, lens::ocr::makeMemoryOcrTokenStore());
    ASSERT_TRUE(client);

    auto wide  = request();
    wide.image = image(5000, 100);
    EXPECT_EQ(client->recognize(wide).status, OcrStatus::InputTooLarge);
    auto thin  = request();
    thin.image = image(200, 10);
    EXPECT_EQ(client->recognize(thin).status, OcrStatus::InputTooLarge);
    EXPECT_EQ(transport->requests.size(), 0u);
}

TEST(OcrBaidu, OnlyAConsentedCaptureMayUpload)
{
    auto transport = std::make_shared<FakeTransport>();
    auto client    = lens::ocr::makeBaiduClient(config(), transport, lens::ocr::makeMemoryOcrTokenStore());
    ASSERT_TRUE(client);

    EXPECT_EQ(client->recognize(request(OcrSource::AutoScan)).status, OcrStatus::Failed);
    EXPECT_EQ(transport->requests.size(), 0u);
}

TEST(OcrBaidu, AMalformedLocationIsAProtocolErrorRatherThanAForgedBox)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {tokenResponse(),
                            {.status      = 200,
                             .body        = QByteArray{R"({"words_result":[{"words":"x","location":{"left":10,"top":20,"width":5000,"height":20}}],"words_result_num":1})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeBaiduClient(config(), transport, lens::ocr::makeMemoryOcrTokenStore());
    ASSERT_TRUE(client);

    EXPECT_EQ(client->recognize(request()).status, OcrStatus::ProtocolError);
}

TEST(OcrBaidu, ACountThatDisagreesWithTheArrayIsAProtocolError)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {tokenResponse(),
                            {.status      = 200,
                             .body        = QByteArray{R"({"words_result":[],"words_result_num":3})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeBaiduClient(config(), transport, lens::ocr::makeMemoryOcrTokenStore());
    ASSERT_TRUE(client);

    EXPECT_EQ(client->recognize(request()).status, OcrStatus::ProtocolError);
}

}
