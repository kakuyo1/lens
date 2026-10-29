/** @file ocr_aliyun.cpp
 * @brief Aliyun RecognizeGeneral request signing and its double-encoded response.
 */
#include "ocr_aliyun.h"

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimeZone>
#include <QUuid>
#include <QUrlQuery>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <map>

#include "ocr_crypto.h"
#include "ocr_json.h"
#include "util/log.h"

namespace lens::ocr {
namespace {

using lens::capture::OcrBox;
using lens::capture::OcrConfidence;
using lens::capture::OcrLine;
using lens::capture::OcrResult;
using lens::capture::OcrStatus;
using lens::capture::OcrUploadState;
using lens::capture::OcrWord;

constexpr std::size_t kMaxBlocks          = 2000;
constexpr std::size_t kMaxTextUnits       = 65536;
constexpr std::size_t kMaxStructuredBytes = 4 * 1024 * 1024;
constexpr char const* kAction             = "RecognizeGeneral";
constexpr char const* kVersion            = "2021-07-07";

OcrResult failed(OcrStatus const                 status,
                 std::uint64_t const             requestId,
                 OcrUploadState const            upload,
                 capture::OcrFailureReason const reason = capture::OcrFailureReason::None)
{
    return {.status        = status,
            .actualEngine  = QStringLiteral("aliyun"),
            .failureReason = reason,
            .uploadState   = upload,
            .requestId     = requestId};
}

/// @return What the reader could change about @p code, in the shared vocabulary.
capture::OcrFailureReason reasonForCode(QString const& code)
{
    if (code == QLatin1String("ocrServiceNotOpen")) return capture::OcrFailureReason::NotActivated;
    if (code.contains(QLatin1String("AccessKeyId")) or code == QLatin1String("MissingSignature") or
        code == QLatin1String("SignatureDoesNotMatch") or code == QLatin1String("illegalSignature") or
        code == QLatin1String("SignatureNonceUsed") or code == QLatin1String("noPermission") or
        code == QLatin1String("InvalidTimeStamp.Expired") or code == QLatin1String("InvalidSignatureVerssion"))
        return capture::OcrFailureReason::Authentication;
    if (code == QLatin1String("Throttling.User") or code == QLatin1String("OcrServiceExpired"))
        return capture::OcrFailureReason::Quota;
    if (code == QLatin1String("algorithmError") or code == QLatin1String("ServiceUnavailable") or
        code == QLatin1String("ServiceTimeout") or code == QLatin1String("LLMTimeout") or code == QLatin1String("systemError"))
        return capture::OcrFailureReason::Unavailable;
    if (code == QLatin1String("unsupportedImageFormat") or code == QLatin1String("illegalImageSize") or
        code == QLatin1String("exceededImageContent"))
        return capture::OcrFailureReason::Image;
    return capture::OcrFailureReason::Unknown;
}

/// @brief RFC 3986 encoding with Aliyun's exceptions: `~` stays literal, a space is `%20`, `*` is `%2A`.
QString percentEncode(QString const& value)
{
    QByteArray const bytes = value.toUtf8();
    QByteArray       out;
    out.reserve(bytes.size());
    for (char const byte : bytes) {
        auto const ch = static_cast<unsigned char>(byte);
        if ((ch >= 'A' and ch <= 'Z') or (ch >= 'a' and ch <= 'z') or (ch >= '0' and ch <= '9') or ch == '-' or ch == '_' or
            ch == '.' or ch == '~') {
            out += static_cast<char>(ch);
        } else {
            out += '%';
            out += QByteArray::number(ch, 16).toUpper().rightJustified(2, '0');
        }
    }
    return QString::fromLatin1(out);
}

OcrStatus statusForCode(QString const& code)
{
    if (code == QLatin1String("unsupportedImageFormat")) return OcrStatus::InvalidImage;
    if (code == QLatin1String("illegalImageSize") or code == QLatin1String("exceededImageContent") or
        code == QLatin1String("exceededImageUrlLength"))
        return OcrStatus::InputTooLarge;
    // Auth, permission, activation, quota and unknown codes share one answer; the code is logged.
    return OcrStatus::Failed;
}

/// @brief One `pos` quad as a pixel box; the service may rectify the image, so a box that does
///        not fit the original crop is dropped rather than allowed to fail the whole answer.
std::optional<OcrBox> quadBox(nlohmann::json const& block, QSize const image)
{
    auto const pos = block.find("pos");
    if (pos == block.end() or not pos->is_array() or pos->size() < 3) return std::nullopt;
    double left   = std::numeric_limits<double>::max();
    double top    = std::numeric_limits<double>::max();
    double right  = std::numeric_limits<double>::lowest();
    double bottom = std::numeric_limits<double>::lowest();
    for (auto const& vertex : *pos) {
        double x = 0, y = 0;
        if (not detail::numberField(vertex, "x", x) or not detail::numberField(vertex, "y", y)) return std::nullopt;
        left   = std::min(left, x);
        top    = std::min(top, y);
        right  = std::max(right, x);
        bottom = std::max(bottom, y);
    }
    OcrBox const box{.x = left, .y = top, .width = right - left, .height = bottom - top};
    if (not capture::boxFitsImage(box, image, 1.0)) return std::nullopt;
    return box;
}

OcrResult parseResponse(QByteArray const& body, QSize const image, std::uint64_t const requestId)
{
    if (body.size() > static_cast<qsizetype>(kMaxStructuredBytes))
        return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
    auto const document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() or not document.is_object())
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);

    if (auto const code = document.find("Code"); code != document.end()) {
        QString value;
        if (not detail::stringField(document, "Code", value))
            return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        LENS_WARN("online OCR refused the request: aliyun {}", value.toStdString());
        return failed(statusForCode(value), requestId, OcrUploadState::ServerResponded, reasonForCode(value));
    }

    // `Data` is a JSON document carried as a string, not an object.
    QString dataText;
    if (not detail::stringField(document, "Data", dataText))
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    auto const data = nlohmann::json::parse(dataText.toStdString(), nullptr, false);
    if (data.is_discarded() or not data.is_object())
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);

    QString text;
    if (not detail::stringField(data, "content", text))
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    text = text.trimmed();
    if (text.isEmpty()) return failed(OcrStatus::EmptyText, requestId, OcrUploadState::ServerResponded);

    OcrResult result            = failed(OcrStatus::Ready, requestId, OcrUploadState::ServerResponded);
    result.text                 = text;
    std::size_t structured      = text.toUtf8().size();
    bool        anyBox          = false;
    double      confidenceSum   = 0;
    std::size_t confidenceCount = 0;

    auto const blocks = data.find("prism_wordsInfo");
    if (blocks != data.end()) {
        if (not blocks->is_array()) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        if (blocks->size() > kMaxBlocks) return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
        for (auto const& block : *blocks) {
            if (not block.is_object()) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
            QString word;
            if (not detail::stringField(block, "word", word))
                return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
            if (word.isEmpty()) continue;
            // The service has no line array; a block is the finest text unit it documents, so a
            // block is one line holding one word rather than a word split out of a guessed line.
            OcrWord entry{.text = word};
            OcrLine line{.text = word};
            if (auto const box = quadBox(block, image)) {
                entry.box = box;
                line.box  = box;
                anyBox    = true;
            }
            line.words.push_back(std::move(entry));
            result.lines.push_back(std::move(line));
            structured += static_cast<std::size_t>(word.toUtf8().size()) + 64;
            double score = 0;
            if (detail::numberField(block, "prob", score)) {
                confidenceSum += std::clamp(score, 0.0, 100.0);
                ++confidenceCount;
            }
        }
    }

    if (text.size() > static_cast<qsizetype>(kMaxTextUnits) or structured > kMaxStructuredBytes)
        return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
    if (confidenceCount > 0)
        result.confidence = OcrConfidence{
            .average = confidenceSum / static_cast<double>(confidenceCount) / 100.0,
            .words   = confidenceCount,
            .total   = result.lines.size()};
    result.geometryLevel = anyBox ? capture::OcrGeometryLevel::Word : capture::OcrGeometryLevel::None;
    return result;
}

QString iso8601(qint64 const seconds)
{
    return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
}

struct AliyunClientImpl final : OnlineOcrClient {
    AliyunClientImpl(AliyunConfig config_, std::shared_ptr<HttpTransport> transport_)
        : config(std::move(config_)), transport(std::move(transport_))
    {}

    AliyunConfig                   config;
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
        if (request.source != capture::OcrSource::Screenshot and request.source != capture::OcrSource::OnlineTest)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        if (validateOcrEndpoint(config.endpoint) or config.accessKeyId.isEmpty() or config.accessKeySecret.isEmpty())
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        if (request.image.isNull()) return done(failed(OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        if (request.image.width() > 8192 or request.image.height() > 8192 or
            std::min(request.image.width(), request.image.height()) < 15)
            return done(failed(OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));
        if (cancel and cancel->load(std::memory_order_relaxed))
            return done(failed(OcrStatus::Cancelled, request.requestId, OcrUploadState::NotUploaded));
        if (request.deadline.count() <= 0) return done(failed(OcrStatus::TimedOut, request.requestId, OcrUploadState::NotUploaded));

        QByteArray png;
        QBuffer    buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not request.image.save(&buffer, "PNG"))
            return done(failed(OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        if (png.size() > kAliyunMaxImageBytes)
            return done(failed(OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));

        auto const  nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto const  stamp = iso8601(QDateTime::currentSecsSinceEpoch());
        HttpRequest httpRequest;
        httpRequest.url       = signedRecognizeUrl(config.endpoint, config.accessKeyId, config.accessKeySecret, nonce, stamp);
        httpRequest.headers   = {{QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/octet-stream")}};
        httpRequest.body      = png;
        httpRequest.timeoutMs = std::min(30000, static_cast<int>(request.deadline.count()));

        auto const response = transport->send(httpRequest, cancel);
        auto const upload   = response.status != 0 ? OcrUploadState::ServerResponded : OcrUploadState::UploadStarted;
        if (response.cancelled) return done(failed(OcrStatus::Cancelled, request.requestId, upload));
        if (response.timedOut) return done(failed(OcrStatus::TimedOut, request.requestId, upload));
        if (response.responseTooLarge) return done(failed(OcrStatus::ResultTooLarge, request.requestId, upload));
        if (not response.transportOk) return done(failed(OcrStatus::TransportFailed, request.requestId, upload));

        if (response.status >= 300 and response.status < 400)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded));
        if (response.status == 429)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded, capture::OcrFailureReason::Quota));
        if (response.status == 401 or response.status == 403) {
            // This service reports account states -- the service not being activated among them --
            // as 401/403 with the same `Code` body, so the body is read before the status decides.
            auto const refused = parseResponse(response.body, request.image.size(), request.requestId);
            if (refused.status != OcrStatus::ProtocolError) return done(refused);
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded, capture::OcrFailureReason::Authentication));
        }
        if (response.status >= 500)
            return done(failed(OcrStatus::TransportFailed, request.requestId, OcrUploadState::ServerResponded));
        if (response.status < 200 or response.status >= 300) {
            // 4xx carries the service's own Code, which the parser reads; a 400 without one is a
            // gateway or proxy answer and cannot be mapped.
            LENS_WARN("online OCR refused the request: aliyun HTTP {}", response.status);
            return done(parseResponse(response.body, request.image.size(), request.requestId));
        }

        LENS_INFO("online OCR answered HTTP {}, {} bytes", response.status, response.body.size());
        return done(parseResponse(response.body, request.image.size(), request.requestId));
    }
};

}

QUrl signedRecognizeUrl(QUrl const&    endpoint,
                        QString const& accessKeyId,
                        QString const& accessKeySecret,
                        QString const& nonce,
                        QString const& timestamp)
{
    std::map<QString, QString> parameters{
        {QStringLiteral("AccessKeyId"), accessKeyId},
        {QStringLiteral("Action"), QString::fromLatin1(kAction)},
        {QStringLiteral("Format"), QStringLiteral("JSON")},
        {QStringLiteral("SignatureMethod"), QStringLiteral("HMAC-SHA1")},
        {QStringLiteral("SignatureNonce"), nonce},
        {QStringLiteral("SignatureVersion"), QStringLiteral("1.0")},
        {QStringLiteral("Timestamp"), timestamp},
        {QStringLiteral("Version"), QString::fromLatin1(kVersion)},
    };

    QStringList pairs;
    for (auto const& [key, value] : parameters)
        pairs << percentEncode(key) + QLatin1Char('=') + percentEncode(value);
    auto const canonicalQuery = pairs.join(QLatin1Char('&'));

    auto const stringToSign = QStringLiteral("POST&%2F&") + percentEncode(canonicalQuery);
    auto const signature    = detail::hmacSha1((accessKeySecret + QLatin1Char('&')).toUtf8(), stringToSign.toUtf8()).toBase64();

    QUrl url = endpoint;
    url.setQuery(canonicalQuery + QStringLiteral("&Signature=") + percentEncode(QString::fromLatin1(signature)),
                 QUrl::StrictMode);
    return url;
}

std::unique_ptr<OnlineOcrClient> makeAliyunClient(AliyunConfig config, std::shared_ptr<HttpTransport> transport)
{
    if (not transport) return {};
    return std::make_unique<AliyunClientImpl>(std::move(config), std::move(transport));
}

}
