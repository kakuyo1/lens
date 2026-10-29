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
#include <charconv>
#include <cmath>
#include <limits>

#include "util/log.h"

namespace lens::ocr {
namespace {

using lens::capture::OcrBox;
using lens::capture::OcrLine;
using lens::capture::OcrResult;
using lens::capture::OcrStatus;
using lens::capture::OcrUploadState;
using lens::capture::OcrWord;

constexpr std::size_t kMaxLines           = 2000;
constexpr std::size_t kMaxWords           = 20000;
constexpr std::size_t kMaxTextUnits       = 65536;
constexpr std::size_t kMaxStructuredBytes = 4 * 1024 * 1024;

bool parseInt(nlohmann::json const& value, int& out)
{
    if (value.is_number_integer()) {
        out = value.get<int>();
        return true;
    }
    if (not value.is_string()) return false;
    auto const& text   = value.get_ref<std::string const&>();
    auto const  parsed = std::from_chars(text.data(), text.data() + text.size(), out);
    return parsed.ec == std::errc{} and parsed.ptr == text.data() + text.size();
}

bool numberField(nlohmann::json const& object, char const* key, double& out)
{
    if (not object.is_object()) return false;
    auto const found = object.find(key);
    if (found == object.end() or not found->is_number()) return false;
    out = found->get<double>();
    return std::isfinite(out);
}

bool stringField(nlohmann::json const& object, char const* key, QString& out)
{
    if (not object.is_object()) return false;
    auto const found = object.find(key);
    if (found == object.end() or not found->is_string()) return false;
    out = QString::fromStdString(found->get<std::string>());
    return not out.contains(QChar(0));
}

OcrResult failed(OcrStatus const status, std::uint64_t const requestId, OcrUploadState const upload)
{
    return {.status = status, .actualEngine = QStringLiteral("ocrspace"), .uploadState = upload, .requestId = requestId};
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

bool parseLine(nlohmann::json const& line, QSize const image, OcrLine& out)
{
    if (not line.is_object()) return false;
    QString    lineText;
    bool const hasLineText = stringField(line, "LineText", lineText);
    auto const words       = line.find("Words");
    if (words == line.end()) {
        // No words means no geometry; the parsed text at the top level remains authoritative.
        if (hasLineText) out.text = lineText;
        return true;
    }
    if (not words->is_array()) return false;
    if (words->size() > 20000) return false;

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
        if (not numberField(word, "Left", wordLeft) or not numberField(word, "Top", wordTop) or
            not numberField(word, "Width", width) or not numberField(word, "Height", height))
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
    if (body.size() > static_cast<qsizetype>(kMaxStructuredBytes))
        return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
    auto const document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() or not document.is_object()) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);

    int        exitCode = 0;
    auto const exitIt   = document.find("OCRExitCode");
    if (exitIt == document.end() or not parseInt(*exitIt, exitCode)) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    auto const errored = document.find("IsErroredOnProcessing");
    if (errored == document.end() or not errored->is_boolean()) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    if (errored->get<bool>() or exitCode == 3 or exitCode == 4) return failed(OcrStatus::Failed, requestId, OcrUploadState::ServerResponded);
    if (exitCode == 2) return failed(OcrStatus::PartialResult, requestId, OcrUploadState::ServerResponded);
    if (exitCode != 1) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);

    auto const parsed = document.find("ParsedResults");
    if (parsed == document.end() or not parsed->is_array() or parsed->size() != 1)
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    auto const& page = parsed->front();
    if (not page.is_object()) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);

    int        fileExit   = 0;
    auto const fileExitIt = page.find("FileParseExitCode");
    if (fileExitIt == page.end() or not parseInt(*fileExitIt, fileExit))
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    if (fileExit != 1) return failed(OcrStatus::PartialResult, requestId, OcrUploadState::ServerResponded);

    QString text;
    if (not stringField(page, "ParsedText", text)) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    if (text.size() > static_cast<qsizetype>(kMaxTextUnits)) return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);

    OcrResult result    = failed(OcrStatus::Ready, requestId, OcrUploadState::ServerResponded);
    result.languageUsed = language;
    result.text         = text.trimmed();
    if (result.text.isEmpty()) return failed(OcrStatus::EmptyText, requestId, OcrUploadState::ServerResponded);

    auto const overlay = page.find("TextOverlay");
    if (overlay != page.end() and not overlay->is_null()) {
        if (not overlay->is_object()) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        auto const lines = overlay->find("Lines");
        if (lines != overlay->end()) {
            if (not lines->is_array()) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
            if (lines->size() > kMaxLines) return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
            std::size_t words = 0;
            for (auto const& line : *lines) {
                OcrLine parsedLine;
                if (not parseLine(line, image, parsedLine))
                    return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
                words += parsedLine.words.size();
                if (words > kMaxWords) return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
                if (parsedLine.text.isEmpty() and not parsedLine.box) continue;
                result.lines.push_back(std::move(parsedLine));
            }
        }
    }

    bool        anyWordBox = false;
    bool        anyLineBox = false;
    std::size_t structured = result.text.toUtf8().size();
    for (auto const& line : result.lines) {
        anyLineBox = anyLineBox or line.box.has_value();
        structured += line.text.toUtf8().size() + 32;
        for (auto const& word : line.words) {
            anyWordBox = anyWordBox or word.box.has_value();
            structured += word.text.toUtf8().size() + 64;
        }
    }
    if (structured > kMaxStructuredBytes) return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
    result.geometryLevel = anyWordBox   ? capture::OcrGeometryLevel::Word
                           : anyLineBox ? capture::OcrGeometryLevel::Line
                                        : capture::OcrGeometryLevel::None;
    return result;
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
        // The online half exists only for an explicit, consented manual capture. A scan source is
        // refused before anything is encoded, which is the second line of defence behind the
        // capture duty never calling here for one.
        if (request.source != capture::OcrSource::Screenshot and request.source != capture::OcrSource::OnlineTest)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        // validate... returns the reason when the value is unusable, so a present optional is the
        // refusal; the earlier `not optional` form refused every valid configuration.
        if (validateOcrEndpoint(config.endpoint) or config.apiKey.isEmpty())
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        if (validateOcrSpaceLanguage(config.language))
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        if (request.image.isNull()) return done(failed(OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        if (request.image.width() > 8192 or request.image.height() > 8192 or request.image.sizeInBytes() > 128 * 1024 * 1024)
            return done(failed(OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));
        if (cancel and cancel->load(std::memory_order_relaxed))
            return done(failed(OcrStatus::Cancelled, request.requestId, OcrUploadState::NotUploaded));
        if (request.deadline.count() <= 0) return done(failed(OcrStatus::TimedOut, request.requestId, OcrUploadState::NotUploaded));

        QByteArray png;
        QBuffer    buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not request.image.save(&buffer, "PNG"))
            return done(failed(OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        if (png.size() > kOcrSpaceMaxUploadBytes)
            return done(failed(OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));

        auto const  boundary = QByteArray{"----lens-" + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1()};
        HttpRequest httpRequest;
        httpRequest.url       = config.endpoint;
        httpRequest.headers   = {{QByteArrayLiteral("Content-Type"), "multipart/form-data; boundary=" + boundary},
                                 {QByteArrayLiteral("apikey"), config.apiKey.toUtf8()}};
        httpRequest.body      = multipartBody(png, config, boundary);
        httpRequest.timeoutMs = std::min(30000, static_cast<int>(request.deadline.count()));

        auto       response = transport->send(httpRequest, cancel);
        auto const upload   = response.status != 0 ? OcrUploadState::ServerResponded : OcrUploadState::UploadStarted;
        if (response.cancelled) return done(failed(OcrStatus::Cancelled, request.requestId, upload));
        if (response.timedOut) return done(failed(OcrStatus::TimedOut, request.requestId, upload));
        if (response.responseTooLarge) return done(failed(OcrStatus::ResultTooLarge, request.requestId, upload));
        if (not response.transportOk) return done(failed(OcrStatus::TransportFailed, request.requestId, upload));

        if (response.status >= 300 and response.status < 400)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded)); // never a followed redirect
        if (response.status == 401 or response.status == 403 or response.status == 429)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded));
        if (response.status < 200 or response.status >= 300)
            return done(failed(OcrStatus::TransportFailed, request.requestId, OcrUploadState::ServerResponded));

        LENS_INFO("online OCR answered HTTP {}, {} byte(s)", response.status, response.body.size());
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
