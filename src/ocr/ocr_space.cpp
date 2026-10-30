/** @file ocr_space.cpp
 * @brief OCR.space request construction and strict response mapping.
 */
#include "ocr_space.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QStringList>
#include <QUuid>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>

#include "ocr_adapter.h"
#include "ocr_json.h"
#include "util/log.h"

namespace lens::ocr {
namespace {

using lens::capture::OcrBox;
using lens::capture::OcrLine;
using lens::capture::OcrResult;
using lens::capture::OcrStatus;
using lens::capture::OcrUploadState;
using lens::capture::OcrWord;

QString const kEngine = QStringLiteral("ocrspace");

/// @return Whether the service's own answer would be refused in the shape it actually arrived.
bool usable(OcrSpaceConfig const& config)
{
    return not validateOcrEndpoint(config.endpoint).has_value() and not config.apiKey.isEmpty() and
           not validateOcrSpaceLanguage(config.language).has_value();
}

QByteArray multipartBody(QByteArray const& png, OcrSpaceConfig const& config, QByteArray const& boundary)
{
    QByteArray body;
    auto       field = [&](char const* name, QByteArray const& value) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"" + QByteArray{name} + "\"\r\n\r\n";
        body += value + "\r\n";
    };
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"capture.png\"\r\n";
    body += "Content-Type: image/png\r\n\r\n";
    body += png + "\r\n";
    field("OCREngine", QByteArrayLiteral("2"));
    field("isOverlayRequired", QByteArrayLiteral("true"));
    field("language", config.language.toUtf8());
    body += "--" + boundary + "--\r\n";
    return body;
}

OcrResult refused(OcrStatus const status, std::uint64_t const requestId)
{
    return ocrResultFor(kEngine, status, requestId, OcrUploadState::ServerResponded);
}

bool parseLine(nlohmann::json const& line, QSize const image, OcrLine& out)
{
    if (not line.is_object()) return false;
    QString    lineText;
    bool const hasLineText = detail::stringField(line, "LineText", lineText);
    auto const words       = line.find("Words");
    if (words == line.end()) {
        // No words means no geometry; the parsed text at the top level remains authoritative.
        if (hasLineText) out.text = lineText;
        return true;
    }
    if (not words->is_array()) return false;
    if (words->size() > kOcrMaxWords) return false;

    double      left   = std::numeric_limits<double>::max();
    double      top    = std::numeric_limits<double>::max();
    double      right  = 0;
    double      bottom = 0;
    bool        any    = false;
    QStringList texts;
    for (auto const& word : *words) {
        auto const wordText = word.find("WordText");
        if (wordText == word.end() or not wordText->is_string()) return false;
        QString text = QString::fromStdString(wordText->get<std::string>());
        if (text.contains(QChar(0))) return false;
        double wordLeft = 0, wordTop = 0, width = 0, height = 0;
        if (not detail::numberField(word, "Left", wordLeft) or not detail::numberField(word, "Top", wordTop) or
            not detail::numberField(word, "Width", width) or not detail::numberField(word, "Height", height))
            return false;
        OcrBox const box{.x = wordLeft, .y = wordTop, .width = width, .height = height};
        if (not capture::boxFitsImage(box, image)) return false;
        if (text.isEmpty()) continue;
        out.words.push_back({.text = std::move(text), .box = box});
        texts << out.words.back().text;
        left   = std::min(left, wordLeft);
        top    = std::min(top, wordTop);
        right  = std::max(right, wordLeft + width);
        bottom = std::max(bottom, wordTop + height);
        any    = true;
    }
    if (any) out.box = OcrBox{.x = left, .y = top, .width = right - left, .height = bottom - top};
    out.text = hasLineText ? lineText : texts.join(QLatin1Char(' '));
    return true;
}

OcrResult parseResponse(QByteArray const& body, QSize const image, QString const& language, std::uint64_t const requestId)
{
    if (body.size() > static_cast<qsizetype>(kOcrMaxStructuredBytes)) return refused(OcrStatus::ResultTooLarge, requestId);
    auto const document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() or not document.is_object()) return refused(OcrStatus::ProtocolError, requestId);

    int        exitCode = 0;
    auto const exitIt   = document.find("OCRExitCode");
    if (exitIt == document.end() or not detail::parseInt(*exitIt, exitCode))
        return refused(OcrStatus::ProtocolError, requestId);
    auto const errored = document.find("IsErroredOnProcessing");
    if (errored == document.end() or not errored->is_boolean()) return refused(OcrStatus::ProtocolError, requestId);
    if (errored->get<bool>() or exitCode == 3 or exitCode == 4) return refused(OcrStatus::Failed, requestId);
    if (exitCode == 2) return refused(OcrStatus::PartialResult, requestId);
    if (exitCode != 1) return refused(OcrStatus::ProtocolError, requestId);

    auto const parsed = document.find("ParsedResults");
    if (parsed == document.end() or not parsed->is_array() or parsed->size() != 1)
        return refused(OcrStatus::ProtocolError, requestId);
    auto const& page = parsed->front();
    if (not page.is_object()) return refused(OcrStatus::ProtocolError, requestId);

    int        fileExit   = 0;
    auto const fileExitIt = page.find("FileParseExitCode");
    if (fileExitIt == page.end() or not detail::parseInt(*fileExitIt, fileExit))
        return refused(OcrStatus::ProtocolError, requestId);
    if (fileExit != 1) return refused(OcrStatus::PartialResult, requestId);

    QString text;
    if (not detail::stringField(page, "ParsedText", text)) return refused(OcrStatus::ProtocolError, requestId);

    OcrResult result    = ocrResultFor(kEngine, OcrStatus::Ready, requestId, OcrUploadState::ServerResponded);
    result.text         = text;
    result.languageUsed = language;

    auto const overlay = page.find("TextOverlay");
    if (overlay != page.end() and not overlay->is_null()) {
        if (not overlay->is_object()) return refused(OcrStatus::ProtocolError, requestId);
        auto const lines = overlay->find("Lines");
        if (lines != overlay->end()) {
            if (not lines->is_array()) return refused(OcrStatus::ProtocolError, requestId);
            if (lines->size() > kOcrMaxLines) return refused(OcrStatus::ResultTooLarge, requestId);
            std::size_t words = 0;
            for (auto const& line : *lines) {
                OcrLine parsedLine;
                if (not parseLine(line, image, parsedLine)) return refused(OcrStatus::ProtocolError, requestId);
                words += parsedLine.words.size();
                if (words > kOcrMaxWords) return refused(OcrStatus::ResultTooLarge, requestId);
                if (parsedLine.text.isEmpty() and not parsedLine.box) continue;
                result.lines.push_back(std::move(parsedLine));
            }
        }
    }
    return finishOcrResult(std::move(result));
}

struct OcrSpaceClientImpl final : OnlineOcrClient {
    OcrSpaceClientImpl(OcrSpaceConfig config_, std::shared_ptr<HttpTransport> transport_)
        : config(std::move(config_)), transport(std::move(transport_))
    {}

    OcrSpaceConfig                 config;
    std::shared_ptr<HttpTransport> transport;

    capture::OcrResult recognize(capture::OcrRequest const&        request,
                                 std::shared_ptr<std::atomic_bool> cancel = {}) override
    {
        QElapsedTimer timer;
        timer.start();
        auto const done = [&](OcrResult result) {
            result.timing.totalMs = static_cast<int>(timer.elapsed());
            return result;
        };
        if (auto const refused =
                refuseOcrRequest(request, kEngine, {.maxEdge = 8192, .maxBytes = 128 * 1024 * 1024}, cancel, usable(config)))
            return done(*refused);

        QByteArray png;
        QBuffer    buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not request.image.save(&buffer, "PNG"))
            return done(ocrResultFor(kEngine, OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        if (png.size() > kOcrSpaceMaxUploadBytes)
            return done(ocrResultFor(kEngine, OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));

        auto const  boundary = QByteArray{"----lens-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1()};
        HttpRequest httpRequest;
        httpRequest.url       = config.endpoint;
        httpRequest.headers   = {{QByteArrayLiteral("Content-Type"), "multipart/form-data; boundary=" + boundary},
                                 {QByteArrayLiteral("apikey"), config.apiKey.toUtf8()}};
        httpRequest.body      = multipartBody(png, config, boundary);
        httpRequest.timeoutMs = std::min(30000, static_cast<int>(request.deadline.count()));

        auto const response = transport->send(httpRequest, cancel);
        if (auto const refused = refuseOcrResponse(response, kEngine, request.requestId)) return done(*refused);
        return done(parseResponse(response.body, request.image.size(), config.language, request.requestId));
    }
};

}

std::optional<QString> validateOcrSpaceLanguage(QString const& language)
{
    if (language == QLatin1String("eng") or language == QLatin1String("auto")) return std::nullopt;
    return QStringLiteral("unsupported-language");
}

std::unique_ptr<OnlineOcrClient> makeOcrSpaceClient(OcrSpaceConfig config, std::shared_ptr<HttpTransport> transport)
{
    if (not transport) return {};
    return std::make_unique<OcrSpaceClientImpl>(std::move(config), std::move(transport));
}

}
