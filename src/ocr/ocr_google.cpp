/** @file ocr_google.cpp
 * @brief Google Vision request construction and strict response mapping.
 */
#include "ocr_google.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QStringList>

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

QString const kEngine = QStringLiteral("google");

/// @return Whether the service's own answer would be refused in the shape it actually arrived.
bool usable(GoogleConfig const& config)
{
    return not validateOcrEndpoint(config.endpoint).has_value() and not config.apiKey.isEmpty() and
           (config.language.isEmpty() or config.language == QLatin1String("en"));
}

/// @return Whether a vertex coordinate is absent (which the protocol defines as 0) or a number.
bool coordinate(nlohmann::json const& vertex, char const* key, double& out)
{
    if (not vertex.is_object()) return false;
    auto const found = vertex.find(key);
    if (found == vertex.end()) {
        out = 0;
        return true;
    }
    if (not found->is_number()) return false;
    out = found->get<double>();
    return std::isfinite(out);
}

/// @brief Read one `boundingBox` / `boundingPoly` into a pixel box.
bool polygonBox(nlohmann::json const& holder, char const* key, OcrBox& out)
{
    auto const polygon = holder.find(key);
    if (polygon == holder.end() or not polygon->is_object()) return false;
    auto const vertices = polygon->find("vertices");
    if (vertices == polygon->end() or not vertices->is_array() or vertices->size() < 3) return false;

    double left   = std::numeric_limits<double>::max();
    double top    = std::numeric_limits<double>::max();
    double right  = std::numeric_limits<double>::lowest();
    double bottom = std::numeric_limits<double>::lowest();
    for (auto const& vertex : *vertices) {
        double x = 0, y = 0;
        if (not coordinate(vertex, "x", x) or not coordinate(vertex, "y", y)) return false;
        left   = std::min(left, x);
        top    = std::min(top, y);
        right  = std::max(right, x);
        bottom = std::max(bottom, y);
    }
    out = OcrBox{.x = left, .y = top, .width = right - left, .height = bottom - top};
    return out.width > 0 and out.height > 0;
}

OcrBox unionOf(OcrBox const& first, OcrBox const& second)
{
    double const left   = std::min(first.x, second.x);
    double const top    = std::min(first.y, second.y);
    double const right  = std::max(first.x + first.width, second.x + second.width);
    double const bottom = std::max(first.y + first.height, second.y + second.height);
    return {.x = left, .y = top, .width = right - left, .height = bottom - top};
}

/// @brief A word's text is its symbols concatenated, in order.
QString symbolText(nlohmann::json const& word)
{
    auto const symbols = word.find("symbols");
    if (symbols == word.end() or not symbols->is_array()) return {};
    QString text;
    for (auto const& symbol : *symbols) {
        QString piece;
        if (detail::stringField(symbol, "text", piece)) text += piece;
    }
    return text;
}

OcrResult refused(OcrStatus const status, std::uint64_t const requestId)
{
    return ocrResultFor(kEngine, status, requestId, OcrUploadState::ServerResponded);
}

OcrResult parseResponse(QByteArray const& body, QSize const image, std::uint64_t const requestId)
{
    if (body.size() > static_cast<qsizetype>(kOcrMaxStructuredBytes)) return refused(OcrStatus::ResultTooLarge, requestId);
    auto const document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() or not document.is_object()) return refused(OcrStatus::ProtocolError, requestId);

    auto const responses = document.find("responses");
    if (responses == document.end() or not responses->is_array() or responses->size() != 1)
        return refused(OcrStatus::ProtocolError, requestId);
    auto const& page = responses->front();
    if (not page.is_object()) return refused(OcrStatus::ProtocolError, requestId);

    if (auto const error = page.find("error"); error != page.end()) {
        int code = 0;
        if (error->is_object()) {
            auto const found = error->find("code");
            if (found != error->end() and detail::parseInt(*found, code)) {
                LENS_WARN("online OCR refused the image: google error code {}", code);
                // 3 is INVALID_ARGUMENT for the image itself; everything else is the account, the
                // key or the quota, and the page's own test is where that is chased.
                if (code == 3)
                    return ocrResultFor(kEngine, OcrStatus::InvalidImage, requestId, OcrUploadState::ServerResponded, lens::capture::OcrFailureReason::Image);
            }
        }
        return refused(OcrStatus::Failed, requestId);
    }

    QString    text;
    bool       hasText = false;
    auto const full    = page.find("fullTextAnnotation");
    if (full != page.end() and full->is_object()) {
        QString assembled;
        if (detail::stringField(*full, "text", assembled)) {
            text    = assembled.trimmed();
            hasText = not text.isEmpty();
        }
    }
    if (not hasText) {
        auto const annotations = page.find("textAnnotations");
        if (annotations != page.end() and annotations->is_array() and not annotations->empty()) {
            QString assembled;
            if (detail::stringField(annotations->front(), "description", assembled)) {
                text    = assembled.trimmed();
                hasText = not text.isEmpty();
            }
        }
    }
    if (not hasText) return refused(OcrStatus::EmptyText, requestId);

    OcrResult result  = ocrResultFor(kEngine, OcrStatus::Ready, requestId, OcrUploadState::ServerResponded);
    result.text       = text;
    std::size_t words = 0;

    if (full != page.end() and full->is_object()) {
        auto const pages = full->find("pages");
        if (pages != full->end() and pages->is_array()) {
            for (auto const& onePage : *pages) {
                auto const blocks = onePage.find("blocks");
                if (blocks == onePage.end() or not blocks->is_array()) continue;
                for (auto const& block : *blocks) {
                    auto const paragraphs = block.find("paragraphs");
                    if (paragraphs == block.end() or not paragraphs->is_array()) continue;
                    for (auto const& paragraph : *paragraphs) {
                        OcrLine line;
                        if (auto const found = paragraph.find("boundingBox"); found != paragraph.end()) {
                            OcrBox box;
                            if (polygonBox(paragraph, "boundingBox", box) and capture::boxFitsImage(box, image, 1.0))
                                line.box = box;
                        }
                        auto const paragraphWords = paragraph.find("words");
                        if (paragraphWords != paragraph.end() and paragraphWords->is_array()) {
                            QStringList           texts;
                            std::optional<OcrBox> unionBox;
                            for (auto const& word : *paragraphWords) {
                                QString const piece = symbolText(word);
                                if (piece.isEmpty()) continue;
                                OcrWord entry{.text = piece};
                                if (auto const found = word.find("boundingBox"); found != word.end()) {
                                    OcrBox box;
                                    if (polygonBox(word, "boundingBox", box)) {
                                        if (not capture::boxFitsImage(box, image, 1.0))
                                            return refused(OcrStatus::ProtocolError, requestId);
                                        entry.box = box;
                                        unionBox  = unionBox ? unionOf(*unionBox, box) : box;
                                    }
                                }
                                texts << piece;
                                line.words.push_back(std::move(entry));
                                ++words;
                                if (words > kOcrMaxWords) return refused(OcrStatus::ResultTooLarge, requestId);
                            }
                            line.text = texts.join(QLatin1Char(' '));
                            if (not line.box and unionBox) line.box = unionBox;
                        }
                        if (line.text.isEmpty()) continue;
                        result.lines.push_back(std::move(line));
                        if (result.lines.size() > kOcrMaxLines) return refused(OcrStatus::ResultTooLarge, requestId);
                    }
                }
            }
        }
    }
    return finishOcrResult(std::move(result));
}

struct GoogleClientImpl final : OnlineOcrClient {
    GoogleClientImpl(GoogleConfig config_, std::shared_ptr<HttpTransport> transport_)
        : config(std::move(config_)), transport(std::move(transport_))
    {}

    GoogleConfig                   config;
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
        if (auto const refused = refuseOcrRequest(request, kEngine, {}, cancel, usable(config))) return done(*refused);

        QByteArray png;
        QBuffer    buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not request.image.save(&buffer, "PNG"))
            return done(ocrResultFor(kEngine, OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        if (png.size() > kGoogleMaxImageBytes)
            return done(ocrResultFor(kEngine, OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));

        nlohmann::json document;
        auto&          one      = document["requests"][0];
        one["image"]["content"] = png.toBase64().toStdString();
        one["features"]         = nlohmann::json::array({{{"type", "TEXT_DETECTION"}}});
        if (config.language == QLatin1String("en")) one["imageContext"]["languageHints"] = nlohmann::json::array({"en"});
        auto const body = QByteArray::fromStdString(document.dump());
        if (body.size() > kGoogleMaxRequestBytes)
            return done(ocrResultFor(kEngine, OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));

        HttpRequest httpRequest;
        httpRequest.url       = config.endpoint;
        httpRequest.headers   = {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json; charset=utf-8")},
                                 {QByteArrayLiteral("X-Goog-Api-Key"), config.apiKey.toUtf8()}};
        httpRequest.body      = body;
        httpRequest.timeoutMs = std::min(30000, static_cast<int>(request.deadline.count()));

        auto const response = transport->send(httpRequest, cancel);
        if (auto const refused = refuseOcrTransport(response, kEngine, request.requestId)) return done(*refused);

        if (response.status == 400) {
            // The key, its restrictions or the enabled API; the body's reason is not reader
            // content, but the status is what support asks for first.
            LENS_WARN("online OCR refused the request: google HTTP {}", response.status);
            return done(ocrResultFor(kEngine, OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded, lens::capture::OcrFailureReason::Authentication));
        }
        if (auto const shared = classifyOcrHttpStatus(response.status))
            return done(ocrResultFor(kEngine, shared->status, request.requestId, OcrUploadState::ServerResponded, shared->reason));
        if (response.status < 200 or response.status >= 300)
            return done(ocrResultFor(kEngine, OcrStatus::TransportFailed, request.requestId, OcrUploadState::ServerResponded));

        LENS_INFO("online OCR answered HTTP {}, {} byte(s)", response.status, response.body.size());
        return done(parseResponse(response.body, request.image.size(), request.requestId));
    }
};

}

std::unique_ptr<OnlineOcrClient> makeGoogleClient(GoogleConfig config, std::shared_ptr<HttpTransport> transport)
{
    if (not transport) return {};
    return std::make_unique<GoogleClientImpl>(std::move(config), std::move(transport));
}

}
