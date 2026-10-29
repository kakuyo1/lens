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
#include <charconv>
#include <cmath>

#include "util/log.h"

namespace lens::ocr {
namespace {

using lens::capture::OcrBox;
using lens::capture::OcrLine;
using lens::capture::OcrResult;
using lens::capture::OcrStatus;
using lens::capture::OcrUploadState;

constexpr std::size_t kMaxLines           = 2000;
constexpr std::size_t kMaxTextUnits       = 65536;
constexpr std::size_t kMaxStructuredBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxTokenBodyBytes  = 64 * 1024;
constexpr int         kTokenTimeoutMs     = 10000;
constexpr int         kMinImageEdge       = 15;
constexpr int         kMaxImageEdge       = 4096;

OcrResult failed(OcrStatus const                 status,
                 std::uint64_t const             requestId,
                 OcrUploadState const            upload,
                 capture::OcrFailureReason const reason = capture::OcrFailureReason::None)
{
    return {.status        = status,
            .actualEngine  = QStringLiteral("baidu"),
            .failureReason = reason,
            .uploadState   = upload,
            .requestId     = requestId};
}

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
    if (not stringField(document, "access_token", token) or token.isEmpty()) return {.status = OcrStatus::Failed};

    int seconds = 0;
    if (auto const lifetime = document.find("expires_in"); lifetime != document.end()) parseInt(*lifetime, seconds);
    // Stop using it before the service does, and keep a short-lived token for less than its life.
    auto const usable = std::chrono::seconds{seconds > 120 ? seconds - 60 : seconds / 2};
    return {.token = token, .status = OcrStatus::Ready, .expiresAt = std::chrono::steady_clock::now() + usable};
}

QString tokenKey(BaiduConfig const& config)
{
    return config.tokenEndpoint.toString() + QLatin1Char('|') + config.endpoint.toString();
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
    if (body.size() > static_cast<qsizetype>(kMaxStructuredBytes))
        return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
    auto const document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() or not document.is_object())
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);

    if (auto const error = document.find("error_code"); error != document.end()) {
        int code = 0;
        if (not parseInt(*error, code)) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        if (code == 216201) return failed(OcrStatus::InvalidImage, requestId, OcrUploadState::ServerResponded, capture::OcrFailureReason::Image);
        if (code == 216202 or code == 216203 or code == 216205)
            return failed(OcrStatus::InputTooLarge, requestId, OcrUploadState::ServerResponded, capture::OcrFailureReason::Image);
        // 110 and 111 are the token itself; the caller drops it so the next explicit attempt
        // exchanges a new one instead of reusing a token the service has already refused.
        tokenRejected = code == 110 or code == 111;
        errorCode     = code;
        // Quota, throttling, a rejected token and anything unknown share one answer: the service
        // refused the request, and the settings page's own test is where the reason is chased.
        auto reason = capture::OcrFailureReason::Unknown;
        if (code == 17 or code == 18 or code == 19)
            reason = capture::OcrFailureReason::Quota;
        else if (code == 110 or code == 111 or code == 6)
            reason = capture::OcrFailureReason::Authentication;
        else if (code == 282000)
            reason = capture::OcrFailureReason::Unavailable;
        return failed(OcrStatus::Failed, requestId, OcrUploadState::ServerResponded, reason);
    }

    auto const lines = document.find("words_result");
    if (lines == document.end() or not lines->is_array())
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    if (lines->size() > kMaxLines) return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
    if (auto const counted = document.find("words_result_num"); counted != document.end()) {
        int number = 0;
        if (not parseInt(*counted, number) or number != static_cast<int>(lines->size()))
            return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    }

    OcrResult result    = failed(OcrStatus::Ready, requestId, OcrUploadState::ServerResponded);
    result.languageUsed = language;
    QStringList texts;
    std::size_t structured = 0;
    bool        anyBox     = false;
    for (auto const& entry : *lines) {
        if (not entry.is_object()) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        QString lineText;
        if (not stringField(entry, "words", lineText))
            return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        if (lineText.isEmpty()) continue;
        OcrLine line{.text = lineText};
        // The service omits `location` for some answers (a single line, or a plan that does not
        // carry geometry). Text without a rectangle is still text; a rectangle that is present
        // but malformed is not, and stays a protocol error.
        if (auto const location = entry.find("location"); location != entry.end() and not location->is_null()) {
            if (not location->is_object()) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
            double left = 0, top = 0, width = 0, height = 0;
            if (not numberField(*location, "left", left) or not numberField(*location, "top", top) or
                not numberField(*location, "width", width) or not numberField(*location, "height", height))
                return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
            OcrBox const box{.x = left, .y = top, .width = width, .height = height};
            if (not capture::boxFitsImage(box, image, 1.0))
                return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
            line.box = box;
            anyBox   = true;
        }
        texts << lineText;
        result.lines.push_back(std::move(line));
        structured += static_cast<std::size_t>(lineText.toUtf8().size()) + 32;
    }

    result.text = texts.join(QLatin1Char('\n')).trimmed();
    if (result.text.isEmpty()) return failed(OcrStatus::EmptyText, requestId, OcrUploadState::ServerResponded);
    if (result.text.size() > static_cast<qsizetype>(kMaxTextUnits) or structured > kMaxStructuredBytes)
        return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
    result.geometryLevel = anyBox ? capture::OcrGeometryLevel::Line : capture::OcrGeometryLevel::None;
    return result;
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
        // Only a consented manual capture and the settings page's own sample may reach here; a scan
        // source is refused before anything is encoded.
        if (request.source != capture::OcrSource::Screenshot and request.source != capture::OcrSource::OnlineTest)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        if (validateOcrEndpoint(config.endpoint) or validateOcrEndpoint(config.tokenEndpoint) or
            config.apiKey.isEmpty() or config.secretKey.isEmpty())
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        if (config.language != QLatin1String("ENG") and config.language != QLatin1String("CHN_ENG"))
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        if (request.image.isNull()) return done(failed(OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        if (request.image.width() > kMaxImageEdge or request.image.height() > kMaxImageEdge or
            std::min(request.image.width(), request.image.height()) < kMinImageEdge)
            return done(failed(OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));
        if (cancel and cancel->load(std::memory_order_relaxed))
            return done(failed(OcrStatus::Cancelled, request.requestId, OcrUploadState::NotUploaded));
        if (request.deadline.count() <= 0) return done(failed(OcrStatus::TimedOut, request.requestId, OcrUploadState::NotUploaded));

        QByteArray png;
        QBuffer    buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not request.image.save(&buffer, "PNG"))
            return done(failed(OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        auto const base64 = png.toBase64();
        if (base64.size() > kBaiduMaxEncodedBytes)
            return done(failed(OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));

        auto const key   = tokenKey(config);
        auto       token = tokens->find(key);
        if (not token) {
            auto const outcome = exchangeToken(config, *transport, cancel, request.deadline);
            if (outcome.token.isEmpty()) {
                LENS_WARN("online OCR token exchange failed before any image was sent");
                return done(failed(outcome.status, request.requestId, OcrUploadState::NotUploaded));
            }
            tokens->put(key, OcrToken{.accessToken = outcome.token, .expiresAt = outcome.expiresAt});
            token = OcrToken{.accessToken = outcome.token, .expiresAt = outcome.expiresAt};
        }
        if (cancel and cancel->load(std::memory_order_relaxed))
            return done(failed(OcrStatus::Cancelled, request.requestId, OcrUploadState::NotUploaded));

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
        auto const upload   = response.status != 0 ? OcrUploadState::ServerResponded : OcrUploadState::UploadStarted;
        if (response.cancelled) return done(failed(OcrStatus::Cancelled, request.requestId, upload));
        if (response.timedOut) return done(failed(OcrStatus::TimedOut, request.requestId, upload));
        if (response.responseTooLarge) return done(failed(OcrStatus::ResultTooLarge, request.requestId, upload));
        if (not response.transportOk) return done(failed(OcrStatus::TransportFailed, request.requestId, upload));

        if (response.status >= 300 and response.status < 400)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded));
        if (response.status == 401 or response.status == 403) {
            // The token was rejected after all; the next attempt exchanges a new one.
            tokens->erase(key);
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded));
        }
        if (response.status == 429) return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded));
        if (response.status < 200 or response.status >= 300)
            return done(failed(OcrStatus::TransportFailed, request.requestId, OcrUploadState::ServerResponded));

        LENS_INFO("online OCR answered HTTP {}, {} byte(s)", response.status, response.body.size());
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
