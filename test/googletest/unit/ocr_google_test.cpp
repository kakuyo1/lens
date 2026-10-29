/**
 * @file ocr_google_test.cpp
 * @brief Vision request shape, paragraph/word mapping and the HTTP-200 per-image error.
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
#include "ocr/ocr_google.h"

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

lens::ocr::GoogleConfig config()
{
    return {.endpoint = QUrl{QStringLiteral("https://vision.googleapis.com/v1/images:annotate")},
            .apiKey   = QStringLiteral("google-key"),
            .language = QStringLiteral("en")};
}

QImage image(int const width = 200, int const height = 100)
{
    QImage page{width, height, QImage::Format_RGB32};
    page.fill(Qt::white);
    return page;
}

OcrRequest request(OcrSource const source = OcrSource::Screenshot)
{
    return {.requestId = 11, .source = source, .image = image(), .deadline = std::chrono::seconds{30}};
}

QByteArray structuredResponse()
{
    nlohmann::json wordA;
    wordA["boundingBox"]["vertices"] = nlohmann::json::array(
        {nlohmann::json{{"x", 10}, {"y", 20}}, nlohmann::json{{"x", 50}, {"y", 20}}, nlohmann::json{{"x", 50}, {"y", 60}}, nlohmann::json{{"x", 10}, {"y", 60}}});
    wordA["symbols"] = nlohmann::json::array({{{"text", "H"}}, {{"text", "i"}}});
    nlohmann::json wordB;
    wordB["boundingBox"]["vertices"] = nlohmann::json::array(
        {nlohmann::json{{"x", 60}, {"y", 20}}, nlohmann::json{{"x", 90}, {"y", 20}}, nlohmann::json{{"x", 90}, {"y", 60}}, nlohmann::json{{"x", 60}, {"y", 60}}});
    wordB["symbols"] = nlohmann::json::array({{{"text", "t"}}, {{"text", "here"}}});

    nlohmann::json paragraph;
    paragraph["words"] = nlohmann::json::array({wordA, wordB});

    nlohmann::json block;
    block["paragraphs"] = nlohmann::json::array({paragraph});
    nlohmann::json page;
    page["blocks"] = nlohmann::json::array({block});

    nlohmann::json response;
    response["fullTextAnnotation"]["text"]  = "Hi there\n";
    response["fullTextAnnotation"]["pages"] = nlohmann::json::array({page});

    nlohmann::json document;
    document["responses"] = nlohmann::json::array({response});
    return QByteArray::fromStdString(document.dump());
}

TEST(OcrGoogle, TheKeyTravelsInAHeaderAndTheBodyAsksForTextDetection)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status = 200, .body = structuredResponse(), .transportOk = true}};
    auto client          = lens::ocr::makeGoogleClient(config(), transport);
    ASSERT_TRUE(client);

    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("Hi there"));
    EXPECT_EQ(result.actualEngine, QStringLiteral("google"));
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::Word);
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_EQ(result.lines[0].text, QStringLiteral("Hi there"));
    ASSERT_EQ(result.lines[0].words.size(), 2u);
    EXPECT_EQ(result.lines[0].words[1].text, QStringLiteral("there"));
    ASSERT_TRUE(result.lines[0].box.has_value());
    EXPECT_DOUBLE_EQ(result.lines[0].box->x, 10);
    EXPECT_DOUBLE_EQ(result.lines[0].box->width, 80);

    ASSERT_EQ(transport->requests.size(), 1u);
    auto const& sent = transport->requests[0];
    EXPECT_FALSE(sent.url.hasQuery()) << "the key is never in the URL";
    bool keyed = false;
    for (auto const& [name, value] : sent.headers)
        if (name == QByteArrayLiteral("X-Goog-Api-Key")) keyed = value == QByteArrayLiteral("google-key");
    EXPECT_TRUE(keyed);
    auto const body = nlohmann::json::parse(sent.body.toStdString());
    ASSERT_TRUE(body["requests"].is_array());
    EXPECT_EQ(body["requests"][0]["features"][0]["type"], "TEXT_DETECTION");
    EXPECT_EQ(body["requests"][0]["imageContext"]["languageHints"][0], "en");
    EXPECT_FALSE(body["requests"][0]["image"]["content"].get<std::string>().empty());
}

TEST(OcrGoogle, ATextOnlyAnswerFallsBackToTheFlatAnnotations)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status      = 200,
                             .body        = QByteArray{R"({"responses":[{"textAnnotations":[{"locale":"en","description":"Hi there\n"}]}]})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeGoogleClient(config(), transport);
    ASSERT_TRUE(client);

    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("Hi there"));
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::None);
    EXPECT_TRUE(result.lines.empty());
}

TEST(OcrGoogle, APerImageErrorIsNotAProtocolError)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status      = 200,
                             .body        = QByteArray{R"({"responses":[{"error":{"code":3,"message":"Bad image data."}}]})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeGoogleClient(config(), transport);
    ASSERT_TRUE(client);
    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::InvalidImage);
    EXPECT_EQ(result.failureReason, lens::capture::OcrFailureReason::Image);
}

TEST(OcrGoogle, AnInvalidKeyFailsWithoutText)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status      = 400,
                             .body        = QByteArray{R"({"error":{"code":400,"message":"API key not valid.","status":"INVALID_ARGUMENT"}})"},
                             .transportOk = true}};
    auto client          = lens::ocr::makeGoogleClient(config(), transport);
    ASSERT_TRUE(client);
    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::Failed);
    EXPECT_EQ(result.failureReason, lens::capture::OcrFailureReason::Authentication);
    EXPECT_TRUE(result.text.isEmpty());
    EXPECT_EQ(result.uploadState, OcrUploadState::ServerResponded);
}

TEST(OcrGoogle, AnAnswerWithoutTextIsEmptyText)
{
    auto transport       = std::make_shared<FakeTransport>();
    transport->responses = {{.status = 200, .body = QByteArray{R"({"responses":[{}]})"}, .transportOk = true}};
    auto client          = lens::ocr::makeGoogleClient(config(), transport);
    ASSERT_TRUE(client);
    EXPECT_EQ(client->recognize(request()).status, OcrStatus::EmptyText);
}

TEST(OcrGoogle, OnlyAConsentedCaptureMayUpload)
{
    auto transport = std::make_shared<FakeTransport>();
    auto client    = lens::ocr::makeGoogleClient(config(), transport);
    ASSERT_TRUE(client);
    EXPECT_EQ(client->recognize(request(OcrSource::AutoScan)).status, OcrStatus::Failed);
    EXPECT_EQ(transport->requests.size(), 0u);
}

}
