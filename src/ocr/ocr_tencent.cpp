/** @file ocr_tencent.cpp
 * @brief Tencent Cloud OCR request signing and strict response mapping.
 */
#include "ocr_tencent.h"

#include <QBuffer>
#include <QDateTime>
#include <QElapsedTimer>
#include <QStringList>
#include <QTimeZone>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>

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

constexpr std::size_t kMaxLines           = 2000;
constexpr std::size_t kMaxTextUnits       = 65536;
constexpr std::size_t kMaxStructuredBytes = 4 * 1024 * 1024;
constexpr char const* kAction             = "GeneralBasicOCR";
constexpr char const* kVersion            = "2018-11-19";
constexpr char const* kContentType        = "application/json; charset=utf-8";

OcrResult failed(OcrStatus const                 status,
                 std::uint64_t const             requestId,
                 OcrUploadState const            upload,
                 capture::OcrFailureReason const reason = capture::OcrFailureReason::None)
{
    return {.status        = status,
            .actualEngine  = QStringLiteral("tencent"),
            .failureReason = reason,
            .uploadState   = upload,
            .requestId     = requestId};
}

/// @return What the reader could change about @p code, in the shared vocabulary.
capture::OcrFailureReason reasonForCode(QString const& code)
{
    if (code == QLatin1String("FailedOperation.UnOpenError")) return capture::OcrFailureReason::NotActivated;
    if (code.startsWith(QLatin1String("AuthFailure.")) or code == QLatin1String("FailedOperation.LanguageNotSupport"))
        return capture::OcrFailureReason::Authentication;
    if (code.startsWith(QLatin1String("RequestLimitExceeded")) or code == QLatin1String("LimitExceeded") or
        code == QLatin1String("ResourceUnavailable.InArrears") or code == QLatin1String("ResourcesSoldOut.ChargeStatusException"))
        return capture::OcrFailureReason::Quota;
    if (code == QLatin1String("InternalError") or code == QLatin1String("ServiceUnavailable") or
        code == QLatin1String("FailedOperation.EngineRecognizeTimeout") or code == QLatin1String("FailedOperation.OcrFailed") or
        code == QLatin1String("FailedOperation.UnKnowError"))
        return capture::OcrFailureReason::Unavailable;
    if (code == QLatin1String("LimitExceeded.TooLargeFileError")) return capture::OcrFailureReason::Image;
    return capture::OcrFailureReason::Unknown;
}

/// @return The status an API-level `Response.Error.Code` stands for.
OcrStatus statusForCode(QString const& code)
{
    if (code == QLatin1String("LimitExceeded.TooLargeFileError")) return OcrStatus::InputTooLarge;
    if (code == QLatin1String("FailedOperation.ImageDecodeFailed") or
        code == QLatin1String("FailedOperation.EmptyImageError"))
        return OcrStatus::InvalidImage;
    if (code == QLatin1String("FailedOperation.ImageNoText")) return OcrStatus::EmptyText;
    // Auth, signature, quota, account and unknown codes share one answer; the code is logged.
    return OcrStatus::Failed;
}

/// @brief A polygon's bounding box; the service promises four vertices in the original image.
std::optional<OcrBox> polygonBox(nlohmann::json const& detection, QSize const image)
{
    auto const polygon = detection.find("Polygon");
    if (polygon == detection.end() or not polygon->is_array() or polygon->size() < 3) return std::nullopt;
    double left   = std::numeric_limits<double>::max();
    double top    = std::numeric_limits<double>::max();
    double right  = std::numeric_limits<double>::lowest();
    double bottom = std::numeric_limits<double>::lowest();
    for (auto const& vertex : *polygon) {
        double x = 0, y = 0;
        if (not detail::numberField(vertex, "X", x) or not detail::numberField(vertex, "Y", y)) return std::nullopt;
        left   = std::min(left, x);
        top    = std::min(top, y);
        right  = std::max(right, x);
        bottom = std::max(bottom, y);
    }
    OcrBox const box{.x = left, .y = top, .width = right - left, .height = bottom - top};
    if (not capture::boxFitsImage(box, image, 1.0)) return std::nullopt;
    return box;
}

OcrResult parseResponse(QByteArray const& body, QSize const image, QString const& language, std::uint64_t const requestId)
{
    if (body.size() > static_cast<qsizetype>(kMaxStructuredBytes))
        return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
    auto const document = nlohmann::json::parse(body, nullptr, false);
    if (document.is_discarded() or not document.is_object())
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);

    auto const response = document.find("Response");
    if (response == document.end() or not response->is_object())
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);

    if (auto const error = response->find("Error"); error != response->end()) {
        QString code;
        if (not detail::stringField(*error, "Code", code))
            return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        LENS_WARN("online OCR refused the request: tencent {}", code.toStdString());
        return failed(statusForCode(code), requestId, OcrUploadState::ServerResponded, reasonForCode(code));
    }

    auto const detections = response->find("TextDetections");
    if (detections == response->end() or not detections->is_array())
        return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
    if (detections->size() > kMaxLines) return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);

    OcrResult result    = failed(OcrStatus::Ready, requestId, OcrUploadState::ServerResponded);
    result.languageUsed = language;
    QStringList texts;
    double      confidenceSum   = 0;
    std::size_t confidenceCount = 0;
    std::size_t structured      = 0;
    for (auto const& detection : *detections) {
        if (not detection.is_object())
            return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        QString text;
        if (not detail::stringField(detection, "DetectedText", text))
            return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        auto const box = polygonBox(detection, image);
        if (not box) return failed(OcrStatus::ProtocolError, requestId, OcrUploadState::ServerResponded);
        if (text.isEmpty()) continue;
        texts << text;
        result.lines.push_back(OcrLine{.text = text, .box = box});
        structured += static_cast<std::size_t>(text.toUtf8().size()) + 32;
        double score = 0;
        if (detail::numberField(detection, "Confidence", score)) {
            confidenceSum += std::clamp(score, 0.0, 100.0);
            ++confidenceCount;
        }
    }

    result.text = texts.join(QLatin1Char('\n')).trimmed();
    if (result.text.isEmpty()) return failed(OcrStatus::EmptyText, requestId, OcrUploadState::ServerResponded);
    if (result.text.size() > static_cast<qsizetype>(kMaxTextUnits) or structured > kMaxStructuredBytes)
        return failed(OcrStatus::ResultTooLarge, requestId, OcrUploadState::ServerResponded);
    if (confidenceCount > 0)
        result.confidence = OcrConfidence{
            .average = confidenceSum / static_cast<double>(confidenceCount) / 100.0,
            .words   = confidenceCount,
            .total   = result.lines.size()};
    result.geometryLevel = capture::OcrGeometryLevel::Line;
    return result;
}

struct TencentClientImpl final : OnlineOcrClient {
    TencentClientImpl(TencentConfig config_, std::shared_ptr<HttpTransport> transport_)
        : config(std::move(config_)), transport(std::move(transport_))
    {}

    TencentConfig                  config;
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
        if (validateOcrEndpoint(config.endpoint) or config.secretId.isEmpty() or config.secretKey.isEmpty())
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        if (config.language != QLatin1String("en") and config.language != QLatin1String("auto"))
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::NotUploaded));
        if (request.image.isNull()) return done(failed(OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        if (cancel and cancel->load(std::memory_order_relaxed))
            return done(failed(OcrStatus::Cancelled, request.requestId, OcrUploadState::NotUploaded));
        if (request.deadline.count() <= 0) return done(failed(OcrStatus::TimedOut, request.requestId, OcrUploadState::NotUploaded));

        QByteArray png;
        QBuffer    buffer{&png};
        if (not buffer.open(QIODevice::WriteOnly) or not request.image.save(&buffer, "PNG"))
            return done(failed(OcrStatus::InvalidImage, request.requestId, OcrUploadState::NotUploaded));
        auto const base64 = png.toBase64();
        if (base64.size() > kTencentMaxEncodedBytes)
            return done(failed(OcrStatus::InputTooLarge, request.requestId, OcrUploadState::NotUploaded));

        nlohmann::json document;
        document["ImageBase64"]  = base64.toStdString();
        document["LanguageType"] = config.language.toStdString();
        auto const body          = QByteArray::fromStdString(document.dump());

        auto const  timestamp = QDateTime::currentSecsSinceEpoch();
        HttpRequest httpRequest;
        httpRequest.url       = config.endpoint;
        httpRequest.headers   = {{QByteArrayLiteral("Content-Type"), QByteArray{kContentType}},
                                 {QByteArrayLiteral("Authorization"),
                                  tc3Authorization(config.endpoint, body, config.secretId, config.secretKey, timestamp).toUtf8()},
                                 {QByteArrayLiteral("X-TC-Action"), QByteArray{kAction}},
                                 {QByteArrayLiteral("X-TC-Version"), QByteArray{kVersion}},
                                 {QByteArrayLiteral("X-TC-Timestamp"), QByteArray::number(timestamp)}};
        httpRequest.body      = body;
        httpRequest.timeoutMs = std::min(30000, static_cast<int>(request.deadline.count()));

        auto const response = transport->send(httpRequest, cancel);
        auto const upload   = response.status != 0 ? OcrUploadState::ServerResponded : OcrUploadState::UploadStarted;
        if (response.cancelled) return done(failed(OcrStatus::Cancelled, request.requestId, upload));
        if (response.timedOut) return done(failed(OcrStatus::TimedOut, request.requestId, upload));
        if (response.responseTooLarge) return done(failed(OcrStatus::ResultTooLarge, request.requestId, upload));
        if (not response.transportOk) return done(failed(OcrStatus::TransportFailed, request.requestId, upload));

        if (response.status >= 300 and response.status < 400)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded));
        if (response.status == 401 or response.status == 403 or response.status == 429)
            return done(failed(OcrStatus::Failed, request.requestId, OcrUploadState::ServerResponded, response.status == 429 ? capture::OcrFailureReason::Quota : capture::OcrFailureReason::Authentication));
        if (response.status < 200 or response.status >= 300)
            return done(failed(OcrStatus::TransportFailed, request.requestId, OcrUploadState::ServerResponded));

        LENS_INFO("online OCR answered HTTP {}, {} byte(s)", response.status, response.body.size());
        return done(parseResponse(response.body, request.image.size(), config.language, request.requestId));
    }
};

}

QString tc3Authorization(QUrl const&       endpoint,
                         QByteArray const& body,
                         QString const&    secretId,
                         QString const&    secretKey,
                         qint64            timestampSeconds)
{
    auto const       date = QDateTime::fromSecsSinceEpoch(timestampSeconds, QTimeZone::UTC).toString(QStringLiteral("yyyy-MM-dd"));
    QByteArray const host = endpoint.host().toLower().toUtf8();
    // The canonical headers end with their own newline, so the assembled request holds the blank
    // line the published hashes require and most hand-written signers lose.
    QByteArray const canonicalHeaders = QByteArrayLiteral("content-type:") + QByteArray{kContentType} +
                                        QByteArrayLiteral("\nhost:") + host + '\n';
    QByteArray const signedHeaders = QByteArrayLiteral("content-type;host");
    QByteArray const canonicalRequest =
        QByteArrayLiteral("POST\n/\n\n") + canonicalHeaders + '\n' + signedHeaders + '\n' + detail::sha256Hex(body);
    QByteArray const stringToSign = QByteArrayLiteral("TC3-HMAC-SHA256\n") + QByteArray::number(timestampSeconds) + '\n' +
                                    date.toUtf8() + QByteArrayLiteral("/ocr/tc3_request\n") + detail::sha256Hex(canonicalRequest);

    auto const secretDate    = detail::hmacSha256(QByteArrayLiteral("TC3") + secretKey.toUtf8(), date.toUtf8());
    auto const secretService = detail::hmacSha256(secretDate, QByteArrayLiteral("ocr"));
    auto const secretSigning = detail::hmacSha256(secretService, QByteArrayLiteral("tc3_request"));
    auto const signature     = detail::hmacSha256(secretSigning, stringToSign).toHex();

    return QStringLiteral("TC3-HMAC-SHA256 Credential=%1/%2/ocr/tc3_request, SignedHeaders=content-type;host, Signature=%3")
        .arg(secretId, date, QString::fromLatin1(signature));
}

std::unique_ptr<OnlineOcrClient> makeTencentClient(TencentConfig config, std::shared_ptr<HttpTransport> transport)
{
    if (not transport) return {};
    return std::make_unique<TencentClientImpl>(std::move(config), std::move(transport));
}

}
