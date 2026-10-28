/**
 * @file ocr_space_test.cpp
 * @brief The online adapter's request shape, response mapping and zero-upload guarantees.
 *
 * Every case uses an injected transport: no test in this file opens a socket. The negative
 * assertions are the point -- refusal paths must leave the transport's call count at zero.
 */
#include <gtest/gtest.h>

#include <QImage>
#include <QUrl>

#include <atomic>
#include <chrono>
#include <memory>

#include <nlohmann/json.hpp>

#include "capture/ocr_engine.h"
#include "ocr/http_transport.h"
#include "ocr/ocr_space.h"

namespace {

using lens::capture::OcrRequest;
using lens::capture::OcrSource;
using lens::capture::OcrStatus;
using lens::capture::OcrUploadState;

struct FakeTransport final : lens::ocr::HttpTransport {
    lens::ocr::HttpResponse response;
    lens::ocr::HttpRequest  last;
    int                     calls = 0;

    lens::ocr::HttpResponse send(lens::ocr::HttpRequest const& request, std::shared_ptr<std::atomic_bool>) override
    {
        ++calls;
        last = request;
        return response;
    }
};

QImage image()
{
    QImage page{200, 100, QImage::Format_RGB32};
    page.fill(Qt::white);
    return page;
}

lens::ocr::OcrSpaceConfig config()
{
    return {.endpoint = QUrl{QStringLiteral("https://api.ocr.space/parse/image")},
            .apiKey   = QStringLiteral("test-key"),
            .language = QStringLiteral("eng")};
}

OcrRequest request(OcrSource const source = OcrSource::Screenshot)
{
    return {.requestId = 42, .source = source, .image = image(), .deadline = std::chrono::seconds{30}};
}

nlohmann::json successDocument()
{
    return nlohmann::json::parse(R"({
        "ParsedResults": [{
            "TextOverlay": {
                "Lines": [{
                    "LineText": "Hello world",
                    "Words": [
                        {"WordText": "Hello", "Left": 10, "Top": 10, "Width": 30, "Height": 15},
                        {"WordText": "world", "Left": 45, "Top": 10, "Width": 30, "Height": 15}
                    ]
                }]
            },
            "FileParseExitCode": 1,
            "ParsedText": "Hello world\n",
            "ErrorMessage": "",
            "ErrorDetails": ""
        }],
        "OCRExitCode": 1,
        "IsErroredOnProcessing": false,
        "ProcessingTimeInMilliseconds": "123"
    })");
}

lens::ocr::HttpResponse okResponse(nlohmann::json const& document)
{
    auto const text = document.dump();
    return {.status = 200, .body = QByteArray::fromStdString(text), .transportOk = true};
}

}

TEST(OcrSpace, ASuccessfulPageCarriesTextWordsAndNoConfidence)
{
    EXPECT_FALSE(lens::ocr::validateOcrSpaceEndpoint(config().endpoint));
    EXPECT_FALSE(lens::ocr::validateOcrSpaceLanguage(config().language));
    auto transport      = std::make_shared<FakeTransport>();
    transport->response = okResponse(successDocument());
    auto client         = lens::ocr::makeOcrSpaceClient(config(), transport);
    ASSERT_TRUE(client);

    auto const result = client->recognize(request());
    EXPECT_EQ(transport->calls, 1) << "upload state " << static_cast<int>(result.uploadState);
    ASSERT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("Hello world"));
    ASSERT_EQ(result.lines.size(), 1u);
    EXPECT_EQ(result.lines[0].text, QStringLiteral("Hello world"));
    EXPECT_EQ(result.lines[0].words.size(), 2u);
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::Word);
    EXPECT_FALSE(result.confidence);
    EXPECT_EQ(result.actualEngine, QStringLiteral("ocrspace"));
    EXPECT_EQ(result.languageUsed, QStringLiteral("eng"));
    EXPECT_EQ(result.requestId, 42u);
    EXPECT_EQ(result.uploadState, OcrUploadState::ServerResponded);
    EXPECT_EQ(transport->calls, 1);
}

TEST(OcrSpace, TheRequestKeepsTheKeyInAHeaderAndTheImageInMemory)
{
    auto transport      = std::make_shared<FakeTransport>();
    transport->response = okResponse(successDocument());
    auto client         = lens::ocr::makeOcrSpaceClient(config(), transport);
    ASSERT_TRUE(client);
    ASSERT_EQ(client->recognize(request()).status, OcrStatus::Ready);

    EXPECT_EQ(transport->last.url, QUrl{QStringLiteral("https://api.ocr.space/parse/image")});
    EXPECT_FALSE(transport->last.url.toString().contains(QStringLiteral("test-key")));
    bool keyHeader = false;
    for (auto const& [name, value] : transport->last.headers)
        keyHeader = keyHeader or (name.toLower() == "apikey" and value == "test-key");
    EXPECT_TRUE(keyHeader);
    EXPECT_TRUE(transport->last.body.contains("name=\"file\"; filename=\"capture.png\""));
    EXPECT_TRUE(transport->last.body.contains("name=\"OCREngine\""));
    EXPECT_TRUE(transport->last.body.contains("name=\"isOverlayRequired\""));
    EXPECT_TRUE(transport->last.body.contains("name=\"language\""));
    EXPECT_FALSE(transport->last.body.contains("data:image"));
}

TEST(OcrSpace, AScanSourceIsRefusedBeforeAnyByteIsSent)
{
    auto transport = std::make_shared<FakeTransport>();
    auto client    = lens::ocr::makeOcrSpaceClient(config(), transport);
    ASSERT_TRUE(client);

    auto const result = client->recognize(request(OcrSource::AutoScan));
    EXPECT_EQ(result.status, OcrStatus::Failed);
    EXPECT_EQ(result.uploadState, OcrUploadState::NotUploaded);
    EXPECT_EQ(transport->calls, 0);

    EXPECT_EQ(client->recognize(request(OcrSource::LocalTest)).status, OcrStatus::Failed);
    EXPECT_EQ(transport->calls, 0);
}

TEST(OcrSpace, OnlyAnExplicitOnlineTestIsTheOtherAcceptedSource)
{
    auto transport      = std::make_shared<FakeTransport>();
    transport->response = okResponse(successDocument());
    auto client         = lens::ocr::makeOcrSpaceClient(config(), transport);
    ASSERT_TRUE(client);

    EXPECT_EQ(client->recognize(request(OcrSource::OnlineTest)).status, OcrStatus::Ready);
    EXPECT_EQ(transport->calls, 1);
}

TEST(OcrSpace, AnInvalidEndpointOrMissingKeyIsRefusedBeforeAnyByteIsSent)
{
    auto transport       = std::make_shared<FakeTransport>();
    auto badEndpoint     = config();
    badEndpoint.endpoint = QUrl{QStringLiteral("http://api.ocr.space/parse/image")};
    auto client          = lens::ocr::makeOcrSpaceClient(badEndpoint, transport);
    EXPECT_EQ(client->recognize(request()).status, OcrStatus::Failed);
    EXPECT_EQ(transport->calls, 0);

    auto noKey = config();
    noKey.apiKey.clear();
    client = lens::ocr::makeOcrSpaceClient(noKey, transport);
    EXPECT_EQ(client->recognize(request()).status, OcrStatus::Failed);
    EXPECT_EQ(transport->calls, 0);
}

TEST(OcrSpace, AnOversizedCropIsRefusedBeforeAnyByteIsSent)
{
    auto   transport = std::make_shared<FakeTransport>();
    auto   client    = lens::ocr::makeOcrSpaceClient(config(), transport);
    QImage large{8193, 1, QImage::Format_RGB32};
    large.fill(Qt::white);
    auto const result = client->recognize({.source = OcrSource::Screenshot, .image = large, .deadline = std::chrono::seconds{30}});
    EXPECT_EQ(result.status, OcrStatus::InputTooLarge);
    EXPECT_EQ(transport->calls, 0);
}

TEST(OcrSpace, ATimeoutAfterSendingKeepsTheUploadStartedState)
{
    auto transport      = std::make_shared<FakeTransport>();
    transport->response = {.timedOut = true};
    auto       client   = lens::ocr::makeOcrSpaceClient(config(), transport);
    auto const result   = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::TimedOut);
    EXPECT_EQ(result.uploadState, OcrUploadState::UploadStarted);
    EXPECT_EQ(transport->calls, 1);
}

TEST(OcrSpace, ATransportFailureIsNotAProtocolFailure)
{
    auto transport      = std::make_shared<FakeTransport>();
    transport->response = {.status = 0, .transportOk = false};
    auto       client   = lens::ocr::makeOcrSpaceClient(config(), transport);
    auto const result   = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::TransportFailed);
    EXPECT_EQ(result.uploadState, OcrUploadState::UploadStarted);
}

TEST(OcrSpace, HttpAuthenticationAndRedirectsAreRefusedWithoutAFollowUp)
{
    auto transport = std::make_shared<FakeTransport>();
    auto client    = lens::ocr::makeOcrSpaceClient(config(), transport);

    transport->response = {.status = 401, .transportOk = true};
    EXPECT_EQ(client->recognize(request()).status, OcrStatus::Failed);
    transport->response = {.status = 302, .transportOk = true};
    EXPECT_EQ(client->recognize(request()).status, OcrStatus::Failed);
    transport->response = {.status = 500, .transportOk = true};
    EXPECT_EQ(client->recognize(request()).status, OcrStatus::TransportFailed);
    EXPECT_EQ(transport->calls, 3);
}

TEST(OcrSpace, PartialProcessingIsNotTranslatableText)
{
    auto transport          = std::make_shared<FakeTransport>();
    auto document           = successDocument();
    document["OCRExitCode"] = 2;
    transport->response     = okResponse(document);
    auto client             = lens::ocr::makeOcrSpaceClient(config(), transport);

    auto const result = client->recognize(request());
    EXPECT_EQ(result.status, OcrStatus::PartialResult);
    EXPECT_TRUE(result.text.isEmpty());
}

TEST(OcrSpace, MalformedJsonAndBadGeometryAreProtocolErrorsWithoutText)
{
    auto transport      = std::make_shared<FakeTransport>();
    transport->response = {.status = 200, .body = "{not json", .transportOk = true};
    auto       client   = lens::ocr::makeOcrSpaceClient(config(), transport);
    auto const badJson  = client->recognize(request());
    EXPECT_EQ(badJson.status, OcrStatus::ProtocolError);
    EXPECT_TRUE(badJson.text.isEmpty());

    auto document                                                                = successDocument();
    document["ParsedResults"][0]["TextOverlay"]["Lines"][0]["Words"][0]["Left"]  = 190;
    document["ParsedResults"][0]["TextOverlay"]["Lines"][0]["Words"][0]["Width"] = 30;
    transport->response                                                          = okResponse(document);
    auto const badBox                                                            = client->recognize(request());
    EXPECT_EQ(badBox.status, OcrStatus::ProtocolError);
    EXPECT_TRUE(badBox.text.isEmpty());
}

TEST(OcrSpace, MissingOverlayStillAllowsPlainTextSuccess)
{
    auto transport = std::make_shared<FakeTransport>();
    auto document  = successDocument();
    document["ParsedResults"][0].erase("TextOverlay");
    transport->response = okResponse(document);
    auto client         = lens::ocr::makeOcrSpaceClient(config(), transport);

    auto const result = client->recognize(request());
    EXPECT_EQ(transport->calls, 1) << "upload state " << static_cast<int>(result.uploadState);
    ASSERT_EQ(result.status, OcrStatus::Ready);
    EXPECT_EQ(result.text, QStringLiteral("Hello world"));
    EXPECT_EQ(result.geometryLevel, lens::capture::OcrGeometryLevel::None);
    EXPECT_TRUE(result.lines.empty());
}

TEST(OcrSpace, EmptyTextIsItsOwnAnswer)
{
    auto transport                             = std::make_shared<FakeTransport>();
    auto document                              = successDocument();
    document["ParsedResults"][0]["ParsedText"] = "";
    transport->response                        = okResponse(document);
    auto client                                = lens::ocr::makeOcrSpaceClient(config(), transport);
    EXPECT_EQ(client->recognize(request()).status, OcrStatus::EmptyText);
}

TEST(OcrSpace, EndpointsMustBeHttpsWithoutUserInfoFragmentOrCredentialsInTheQuery)
{
    EXPECT_FALSE(lens::ocr::validateOcrSpaceEndpoint(QUrl{QStringLiteral("https://api.ocr.space/parse/image")}));
    EXPECT_TRUE(lens::ocr::validateOcrSpaceEndpoint(QUrl{QStringLiteral("http://api.ocr.space/parse/image")}));
    EXPECT_TRUE(lens::ocr::validateOcrSpaceEndpoint(QUrl{QStringLiteral("https://user:pass@api.ocr.space/parse/image")}));
    EXPECT_TRUE(lens::ocr::validateOcrSpaceEndpoint(QUrl{QStringLiteral("https://api.ocr.space/parse/image#f")}));
    EXPECT_TRUE(lens::ocr::validateOcrSpaceEndpoint(QUrl{QStringLiteral("https://api.ocr.space/parse/image?apikey=x")}));
}
