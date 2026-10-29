/**
 * @file ocr_aliyun_test.cpp
 * @brief RPC signing, the binary request and the double-encoded RecognizeGeneral answer.
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
#include "ocr/ocr_aliyun.h"

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

lens::ocr::AliyunConfig config()
{
    return {.endpoint        = QUrl{QStringLiteral("https://ocr-api.cn-hangzhou.aliyuncs.com/")},
            .accessKeyId     = QStringLiteral("testid"),
            .accessKeySecret = QStringLiteral("testsecret")};
}

QImage image(int const width = 805, int const height = 655)
{
    QImage page{width, height, QImage::Format_RGB32};
    page.fill(Qt::white);
    return page;
}

OcrRequest request(OcrSource const source = OcrSource::Screenshot)
{
    return {.requestId = 9, .source = source, .image = image(), .deadline = std::chrono::seconds{30}};
}

QByteArray dataDocument(bool const insideImage = true)
{
    auto const vertex = [](int x, int y) { return nlohmann::json{{"x", x}, {"y", y}}; };

    nlohmann::json first;
    first["word"] = "iPhone 12";
    first["prob"] = 99;
    first["pos"]  = nlohmann::json::array(
        {vertex(insideImage ? 351 : 900, 45), vertex(461, 46), vertex(461, 67), vertex(351, 66)});

    nlohmann::json second;
    second["word"] = "升维";
    second["prob"] = 98;
    second["pos"]  = nlohmann::json::array({vertex(10, 10), vertex(60, 10), vertex(60, 40), vertex(10, 40)});

    nlohmann::json inner;
    inner["content"]         = "iPhone 12 升维";
    inner["width"]           = 805;
    inner["height"]          = 655;
    inner["prism_wnum"]      = 2;
    inner["prism_wordsInfo"] = nlohmann::json::array({first, second});

    nlohmann::json outer;
    outer["RequestId"] = "r";
    outer["Data"]      = inner.dump();
    return QByteArray::fromStdString(outer.dump());
}

TEST(OcrAliyun, TheSignatureMatchesTheAlgorithmWorkedExample)
{
    auto const      url = lens::ocr::signedRecognizeUrl(config().endpoint,
                                                   QStringLiteral("testid"),
                                                   QStringLiteral("testsecret"),
                                                   QStringLiteral("edb2b34af0af9a6d14deaf7c1a5315eb"),
                                                   QStringLiteral("2023-03-13T08:34:30Z"));
    QUrlQuery const query{url};
    EXPECT_EQ(query.queryItemValue(QStringLiteral("Action"), QUrl::FullyDecoded), QStringLiteral("RecognizeGeneral"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("Version"), QUrl::FullyDecoded), QStringLiteral("2021-07-07"));
    EXPECT_EQ(query.queryItemValue(QStringLiteral("Signature"), QUrl::FullyDecoded), QStringLiteral("oMyjZrzCiwNqrWrNDL8q0GQ0tMU="));
    // The signed string must be the string on the wire: Qt may not re-encode it on the way out.
    auto const encoded = url.toString(QUrl::FullyEncoded);
    EXPECT_TRUE(encoded.contains(QStringLiteral("Timestamp=2023-03-13T08%3A34%3A30Z"))) << encoded.toStdString();
    EXPECT_TRUE(encoded.endsWith(QStringLiteral("Signature=oMyjZrzCiwNqrWrNDL8q0GQ0tMU%3D"))) << encoded.toStdString();
}

TEST(OcrAliyun, TheImageIsTheBodyAndTheAnswerArrivesTwiceEncoded)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status = 200, .body = dataDocument(), .transportOk = true}};
    auto client          = lens::ocr::makeAliyunClient(config(), transport);
    ASSERT_TRUE(client);

    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("iPhone 12 升维"));
    EXPECT_EQ(result.actualEngine, QStringLiteral("aliyun"));
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::Word);
    ASSERT_EQ(result.lines.size(), 2u);
    ASSERT_TRUE(result.lines[0].box.has_value());
    EXPECT_DOUBLE_EQ(result.lines[0].box->x, 351);
    ASSERT_TRUE(result.confidence.has_value());
    EXPECT_DOUBLE_EQ(result.confidence->average, 0.985);

    ASSERT_EQ(transport->requests.size(), 1u);
    auto const& sent = transport->requests[0];
    EXPECT_EQ(sent.headers.front().first, QByteArrayLiteral("Content-Type"));
    EXPECT_EQ(sent.headers.front().second, QByteArrayLiteral("application/octet-stream"));
    EXPECT_TRUE(sent.body.startsWith("\x89PNG"));
    QUrlQuery const query{sent.url};
    EXPECT_EQ(query.queryItemValue(QStringLiteral("AccessKeyId"), QUrl::FullyDecoded), QStringLiteral("testid"));
    EXPECT_FALSE(query.queryItemValue(QStringLiteral("Signature")).isEmpty());
}

TEST(OcrAliyun, AServiceErrorCodeIsMappedFromTheFlatErrorBody)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status      = 400,
                             .body        = QByteArray{R"({"RequestId":"r","Code":"SignatureDoesNotMatch","Message":"x","HostId":"h"})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeAliyunClient(config(), transport);
    ASSERT_TRUE(client);
    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Failed);
    EXPECT_EQ(result.failureReason, lens::capture::OcrFailureReason::Authentication);
}

/// A 401 from this service can mean the service was never activated, and the body says so.
TEST(OcrAliyun, AnUnopenedServiceIsItsOwnReason)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status      = 401,
                             .body        = QByteArray{R"({"RequestId":"r","Code":"ocrServiceNotOpen","Message":"x"})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeAliyunClient(config(), transport);
    ASSERT_TRUE(client);
    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Failed);
    EXPECT_EQ(result.failureReason, lens::capture::OcrFailureReason::NotActivated);
}

TEST(OcrAliyun, ImageErrorsHaveTheirOwnAnswers)
{
    for (auto const& [code, expected] : std::initializer_list<std::pair<char const*, OcrStatus>>{
             {"unsupportedImageFormat", OcrStatus::InvalidImage}, {"illegalImageSize", OcrStatus::InputTooLarge}}) {
        auto transport       = std::make_shared<FakeTransport>();
        transport->responses = {{.status      = 400,
                                 .body        = QByteArray{"{\"RequestId\":\"r\",\"Code\":\"" + QByteArray{code} + "\",\"Message\":\"x\"}"},
                                 .transportOk = true}};
        auto client          = lens::ocr::makeAliyunClient(config(), transport);
        ASSERT_TRUE(client);
        auto const result = client->recognize(request());
        EXPECT_EQ(result.status, expected) << code;
        EXPECT_EQ(result.failureReason, lens::capture::OcrFailureReason::Image) << code;
    }
}

/// The service rectifies the image before reading it, so a block's quad can fall outside the
/// original crop; the text is still the answer and only the box is dropped.
TEST(OcrAliyun, ABoxOutsideTheCropIsDroppedRatherThanFailingTheText)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status = 200, .body = dataDocument(false), .transportOk = true}};
    auto client          = lens::ocr::makeAliyunClient(config(), transport);
    ASSERT_TRUE(client);

    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Ready);
    ASSERT_EQ(result.lines.size(), 2u);
    EXPECT_FALSE(result.lines[0].box.has_value());
    EXPECT_TRUE(result.lines[1].box.has_value());
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::Word);
}

TEST(OcrAliyun, OnlyAConsentedCaptureMayUpload)
{
    auto transport = std::make_shared<FakeTransport>();
    auto client    = lens::ocr::makeAliyunClient(config(), transport);
    ASSERT_TRUE(client);
    EXPECT_EQ(client->recognize(request(OcrSource::AutoScan)).status, OcrStatus::Failed);
    EXPECT_EQ(transport->requests.size(), 0u);
}

}
