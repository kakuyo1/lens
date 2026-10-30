/** @file ocr_adapter.cpp
 * @brief The shared adapter frame, implemented once for the provider table.
 */
#include "ocr_adapter.h"

#include <algorithm>

#include "util/log.h"

namespace lens::ocr {
namespace {

using lens::capture::OcrFailureReason;
using lens::capture::OcrGeometryLevel;
using lens::capture::OcrLine;
using lens::capture::OcrResult;
using lens::capture::OcrStatus;
using lens::capture::OcrUploadState;
using lens::capture::OcrWord;

/// @return Where the request's bytes are once @p response arrived -- or did not.
OcrUploadState uploadStateFor(HttpResponse const& response)
{
    return response.status != 0 ? OcrUploadState::ServerResponded : OcrUploadState::UploadStarted;
}

/// @return The geometry the @p lines' boxes really support; `has any box` is not enough to say Word.
OcrGeometryLevel geometryOf(std::vector<OcrLine> const& lines)
{
    auto const anyWordBox = std::any_of(lines.begin(), lines.end(), [](OcrLine const& line) {
        return std::any_of(line.words.begin(), line.words.end(), [](OcrWord const& word) { return word.box.has_value(); });
    });
    if (anyWordBox) return OcrGeometryLevel::Word;
    auto const anyLineBox =
        std::any_of(lines.begin(), lines.end(), [](OcrLine const& line) { return line.box.has_value(); });
    return anyLineBox ? OcrGeometryLevel::Line : OcrGeometryLevel::None;
}

}

std::optional<OcrHttpStatus> classifyOcrHttpStatus(int const httpStatus)
{
    if (httpStatus >= 300 and httpStatus < 400) return {{OcrStatus::Failed, OcrFailureReason::None}}; // never a followed redirect
    if (httpStatus == 401 or httpStatus == 403) return {{OcrStatus::Failed, OcrFailureReason::Authentication}};
    if (httpStatus == 429) return {{OcrStatus::Failed, OcrFailureReason::Quota}};
    return std::nullopt;
}

std::optional<OcrResult> refuseOcrRequest(capture::OcrRequest const&        request,
                                          QString const&                    engine,
                                          OcrImageLimits const&             limits,
                                          std::shared_ptr<std::atomic_bool> cancel,
                                          bool const                        configurationUsable)
{
    auto const refusal = [&](OcrStatus const status) {
        return ocrResultFor(engine, status, request.requestId, OcrUploadState::NotUploaded);
    };
    // The online half exists only for an explicit, consented manual capture; a scan source is
    // refused before anything is encoded, the second line of defence behind the capture duty.
    if (request.source != capture::OcrSource::Screenshot and request.source != capture::OcrSource::OnlineTest)
        return refusal(OcrStatus::Failed);
    if (not configurationUsable) return refusal(OcrStatus::Failed);
    if (request.image.isNull()) return refusal(OcrStatus::InvalidImage);
    if ((limits.maxEdge != 0 and (request.image.width() > limits.maxEdge or request.image.height() > limits.maxEdge)) or
        (limits.minEdge != 0 and std::min(request.image.width(), request.image.height()) < limits.minEdge) or
        (limits.maxBytes != 0 and request.image.sizeInBytes() > limits.maxBytes))
        return refusal(OcrStatus::InputTooLarge);
    if (cancel and cancel->load(std::memory_order_relaxed)) return refusal(OcrStatus::Cancelled);
    if (request.deadline.count() <= 0) return refusal(OcrStatus::TimedOut);
    return std::nullopt;
}

std::optional<OcrResult> refuseOcrTransport(HttpResponse const& response, QString const& engine, std::uint64_t const requestId)
{
    auto const upload = uploadStateFor(response);
    if (response.cancelled) return ocrResultFor(engine, OcrStatus::Cancelled, requestId, upload);
    if (response.timedOut) return ocrResultFor(engine, OcrStatus::TimedOut, requestId, upload);
    if (response.responseTooLarge) return ocrResultFor(engine, OcrStatus::ResultTooLarge, requestId, upload);
    if (not response.transportOk) return ocrResultFor(engine, OcrStatus::TransportFailed, requestId, upload);
    return std::nullopt;
}

std::optional<OcrResult> refuseOcrResponse(HttpResponse const& response, QString const& engine, std::uint64_t const requestId)
{
    if (auto const refused = refuseOcrTransport(response, engine, requestId)) return refused;
    auto const upload = uploadStateFor(response);
    if (auto const shared = classifyOcrHttpStatus(response.status))
        return ocrResultFor(engine, shared->status, requestId, upload, shared->reason);
    if (response.status < 200 or response.status >= 300)
        return ocrResultFor(engine, OcrStatus::TransportFailed, requestId, upload);
    LENS_INFO("online OCR answered HTTP {}, {} byte(s)", response.status, response.body.size());
    return std::nullopt;
}

OcrResult ocrResultFor(QString const&         engine,
                       OcrStatus const        status,
                       std::uint64_t const    requestId,
                       OcrUploadState const   upload,
                       OcrFailureReason const reason)
{
    return {.status        = status,
            .actualEngine  = engine,
            .failureReason = reason,
            .uploadState   = upload,
            .requestId     = requestId};
}

OcrResult finishOcrResult(OcrResult result)
{
    auto const refusal = [&](OcrStatus const status) {
        return ocrResultFor(result.actualEngine, status, result.requestId, result.uploadState);
    };
    result.text = result.text.trimmed();
    if (result.text.isEmpty()) return refusal(OcrStatus::EmptyText);
    if (result.text.size() > static_cast<qsizetype>(kOcrMaxTextUnits)) return refusal(OcrStatus::ResultTooLarge);

    std::size_t structured = static_cast<std::size_t>(result.text.toUtf8().size());
    for (auto const& line : result.lines) {
        structured += static_cast<std::size_t>(line.text.toUtf8().size()) + 32;
        for (auto const& word : line.words)
            structured += static_cast<std::size_t>(word.text.toUtf8().size()) + 64;
    }
    if (structured > kOcrMaxStructuredBytes) return refusal(OcrStatus::ResultTooLarge);

    result.geometryLevel = geometryOf(result.lines);
    return result;
}

}
