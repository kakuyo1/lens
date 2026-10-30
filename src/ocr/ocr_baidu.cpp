/** @file ocr_baidu.cpp
 * @brief Baidu request construction, token caching and strict response mapping.
 */
#include "ocr_baidu.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QStringList>
#include <QUrlQuery>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>

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

QString const kEngine = QStringLiteral("baidu");

constexpr std::size_t kMaxTokenBodyBytes = 64 * 1024;
constexpr int         kTokenTimeoutMs    = 10000;
constexpr int         kMaxImageEdge      = 4096; ///< The service's longest accepted side.
constexpr int         kMinImageEdge      = 15;   ///< The service's shortest accepted side.

/// @return Whether the service's own answer would be refused in the shape it actually arrived.
bool usable(BaiduConfig const& config)
{
    return not validateOcrEndpoint(config.endpoint).has_value() and
           not validateOcrEndpoint(config.tokenEndpoint).has_value() and not config.apiKey.isEmpty() and
           not config.secretKey.isEmpty() and
           (config.language == QLatin1String("ENG") or config.language == QLatin1String("CHN_ENG"));
}

/// @return @p fields as one application/x-www-form-urlencoded body.
QByteArray formBody(std::initializer_list<std::pair<char const*, QByteArray>> const fields)
{
    QByteArray body;
    for (auto const& [name, value] : fields) {
        if (not body.isEmpty()) body += '&';
        body += QUrl::toPercentEncoding(QString::fromLatin1(name));
        body += '=';
        body += QUrl::toPercentEncoding(QString::fromUtf8(value));
    }
    return body;
}

/// @brief A token exchange's answer: the token, or why there is none.
struct TokenOutcome {
    QString                               token;
    OcrStatus                             status = OcrStatus::Failed;
    std::chrono::steady_clock::time_point expiresAt;
};

TokenOutcome exchangeToken(BaiduConfig const&                config,
                           HttpTransport&                    transport,
                           std::shared_ptr<std::atomic_bool> cancel,
                           std::chrono::milliseconds const   remaining)
{
    HttpRequest request;
    request.url     = config.tokenEndpoint;
    request.headers = {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/x-www-form-urlencoded")}};
    request.body    = formBody({{"grant_type", QByteArrayLiteral("client_credentials")},
                                {"client_id", config.apiKey.toUtf8()},
                                {"client_secret", config.secretKey.toUtf8()}});
    request.timeoutMs =
        static_cast<int>(std::max<std::int64_t>(1000, std::min<std::int64_t>(kTokenTimeoutMs, remaining.count())));

    auto const response = transport.send(request, cancel);
    if (response.cancelled) return {.status = OcrStatus::Cancelled};
    if (response.timedOut) return {.status = OcrStatus::TimedOut};
    if (response.responseTooLarge) return {.status = OcrStatus::ResultTooLarge};
    if (not response.transportOk) return {.status = OcrStatus::TransportFailed};
    if (response.status != 200) return {.status = OcrStatus::Failed};

    if (response.body.size() > static_cast<qsizetype>(kMaxTokenBodyBytes)) return {.status = OcrStatus::ResultTooLarge};
    auto const document = nlohmann::json::parse(response.body, nullptr, false);
    if (document.is_discarded() or not document.is_object()) return {.status = OcrStatus::ProtocolError};

    // A refused key pair is an HTTP 200 carrying `error` and `error_description`; only the token
    // itself is worth reading, and neither field is ever logged.
    QString token;
    if (not detail::stringField(document, "access_token", token) or token.isEmpty()) return {.status = OcrStatus::Failed};

    int seconds = 0;
    if (auto const lifetime = document.find("expires_in"); lifetime != document.end()) detail::parseInt(*lifetime, seconds);
    // Stop using it before the service does, and keep a short-lived token for less than its life.
    auto const usable = std::chrono::seconds{seconds > 120 ? seconds - 60 : seconds / 2};
    return {.token = token, .status = OcrStatus::Ready, .expiresAt = std::chrono::steady_clock::now() + usable};
}

QString tokenKey(BaiduConfig const& config)
{
    return config.tokenEndpoint.toString() + QLatin1Char('|') + config.endpoint.toString();
}

OcrResult refused(OcrStatus const status, std::uint64_t const requestId)
{
    return ocrResultFor(kEngine, status, requestId, OcrUploadState::ServerResponded);
}

OcrResult parseResponse(QByteArray const&   body,
                        QSize const         image,
                        QString const&      language,
                        std::uint64_t const requestId,
                        bool&               tokenRejected,
                        int&                errorCode)
{
    tokenRejected = false;
    errorCode     = 0;
    if (body.size() > static_cast<qsizetype>(kOcrMaxStructuredBytes)) return refused(OcrStatus::ResultTooLarge, requestId);
    auto const document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() or not document.is_object()) return refused(OcrStatus::ProtocolError, requestId);

    if (auto const error = document.find("error_code"); error != document.end()) {
        int code = 0;
        if (not detail::parseInt(*error, code)) return refused(OcrStatus::ProtocolError, requestId);
        if (code == 216201) return ocrResultFor(kEngine, OcrStatus::InvalidImage, requestId, OcrUploadState::ServerResponded, lens::capture::OcrFailureReason::Image);
        if (code == 216202 or code == 216203 or code == 216205)
            return ocrResultFor(kEngine, OcrStatus::InputTooLarge, requestId, OcrUploadState::ServerResponded, lens::capture::OcrFailureReason::Image);
        // 110 and 111 are the token itself; the caller drops it so the next explicit attempt
        // exchanges a new one instead of reusing a token the service has already refused.
        tokenRejected = code == 110 or code == 111;
        errorCode     = code;
        // Quota, throttling, a rejected token and anything unknown share one answer: the service
        // refused the request, and the settings page's own test is where the reason is chased.
        auto reason = lens::capture::OcrFailureReason::Unknown;
        if (code == 17 or code == 18 or code == 19)
            reason = lens::capture::OcrFailureReason::Quota;
        else if (code == 110 or code == 111 or code == 6)
            reason = lens::capture::OcrFailureReason::Authentication;
        else if (code == 282000)
            reason = lens::capture::OcrFailureReason::Unavailable;
        return ocrResultFor(kEngine, OcrStatus::Failed, requestId, OcrUploadState::ServerResponded, reason);
    }

    auto const lines = document.find("words_result");
    if (lines == document.end() or not lines->is_array()) return refused(OcrStatus::ProtocolError, requestId);
    if (lines->size() > kOcrMaxLines) return refused(OcrStatus::ResultTooLarge, requestId);
    if (auto const counted = document.find("words_result_num"); counted != document.end()) {
        int number = 0;
        if (not detail::parseInt(*counted, number) or number != static_cast<int>(lines->size()))
            return refused(OcrStatus::ProtocolError, requestId);
    }

    OcrResult result    = ocrResultFor(kEngine, OcrStatus::Ready, requestId, OcrUploadState::ServerResponded);
    result.languageUsed = language;
    QStringList texts;
    for (auto const& entry : *lines) {
        if (not entry.is_object()) return refused(OcrStatus::ProtocolError, requestId);
        QString lineText;
        if (not detail::stringField(entry, "words", lineText)) return refused(OcrStatus::ProtocolError, requestId);
        if (lineText.isEmpty()) continue;
        OcrLine line{.text = lineText};
        // The service omits `location` for some answers (a single line, or a plan that does not
        // carry geometry). Text without a rectangle is still text; a rectangle that is present
        // but malformed is not, and stays a protocol error.
        if (auto const location = entry.find("location"); location != entry.end() and not location->is_null()) {
            if (not location->is_object()) return refused(OcrStatus::ProtocolError, requestId);
            double left = 0, top = 0, width = 0, height = 0;
            if (not detail::numberField(*location, "left", left) or not detail::numberField(*location, "top", top) or
                not detail::numberField(*location, "width", width) or not detail::numberField(*location, "height", height))
                return refused(OcrStatus::ProtocolError, requestId);
            OcrBox const box{.x = left, .y = top, .width = width, .height = height};
            if (not capture::boxFitsImage(box, image, 1.0)) return refused(OcrStatus::ProtocolError, requestId);
            line.box = box;
        }
        texts << lineText;
        result.lines.push_back(std::move(line));
    }

    result.text = texts.join(QLatin1Char('\n'));
    return finishOcrResult(std::move(result));
}

struct BaiduClientImpl final : OnlineOcrClient {
    BaiduClientImpl(BaiduConfig config_, std::shared_ptr<HttpTransport> transport_, std::shared_ptr<OcrTokenStore> tokens_)
        : config(std::move(config_)), transport(std::move(transport_)), tokens(std::move(tokens_))
    {}

    BaiduConfig                    config;
    std::shared_ptr<HttpTransport> transport;
    std::shared_ptr<OcrTokenStore> tokens;

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
                refuseOcrRequest(request, kEngine, {.maxEdge = kMaxImageEdge, .minEdge = kMinImageEdge}, cancel, usable(config)))
            return done(*refused);

        QByteArray png;
        QBuffer    buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not request.image.save(&buffer, "PNG"))
            return done(ocrResultFor(kEngine, OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        auto const base64 = png.toBase64();
        if (base64.size() > kBaiduMaxEncodedBytes)
            return done(ocrResultFor(kEngine, OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));

        auto const key   = tokenKey(config);
        auto       token = tokens->find(key);
        if (not token) {
            auto const outcome = exchangeToken(config, *transport, cancel, request.deadline);
            if (outcome.token.isEmpty()) {
                LENS_WARN("online OCR token exchange failed before any image was sent");
                return done(ocrResultFor(kEngine, outcome.status, request.requestId, OcrUploadState::NotUploaded));
            }
            tokens->put(key, OcrToken{.accessToken = outcome.token, .expiresAt = outcome.expiresAt});
            token = OcrToken{.accessToken = outcome.token, .expiresAt = outcome.expiresAt};
        }
        if (cancel and cancel->load(std::memory_order_relaxed))
            return done(ocrResultFor(kEngine, OcrStatus::Cancelled, request.requestId, OcrUploadState::NotUploaded));

        QUrl      url = config.endpoint;
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("access_token"), token->accessToken);
        url.setQuery(query);

        HttpRequest httpRequest;
        httpRequest.url       = url;
        httpRequest.headers   = {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/x-www-form-urlencoded")}};
        httpRequest.body      = formBody({{"image", base64}, {"language_type", config.language.toUtf8()}});
        httpRequest.timeoutMs = std::min(30000, static_cast<int>(request.deadline.count()));

        auto const response = transport->send(httpRequest, cancel);
        if (auto const refused = refuseOcrResponse(response, kEngine, request.requestId)) {
            // The token was rejected after all; the next attempt exchanges a new one.
            if (refused->failureReason == lens::capture::OcrFailureReason::Authentication) tokens->erase(key);
            return done(*refused);
        }

        bool       tokenRejected = false;
        int        errorCode     = 0;
        auto const result        = parseResponse(response.body, request.image.size(), config.language, request.requestId, tokenRejected, errorCode);
        if (tokenRejected) tokens->erase(key);
        // The service's own message is not repeated; its code is the part that says which limit,
        // permission or parameter the reader (or this adapter) has to fix.
        if (errorCode != 0) LENS_WARN("online OCR refused the request: error_code {}", errorCode);
        return done(result);
    }
};

}

std::unique_ptr<OnlineOcrClient> makeBaiduClient(BaiduConfig                    config,
                                                 std::shared_ptr<HttpTransport> transport,
                                                 std::shared_ptr<OcrTokenStore> tokens)
{
    if (not transport or not tokens) return {};
    return std::make_unique<BaiduClientImpl>(std::move(config), std::move(transport), std::move(tokens));
}

}
