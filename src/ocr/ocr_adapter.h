/** @file ocr_adapter.h
 * @brief The frame every online provider adapter shares, written once instead of five times.
 *
 * An adapter owns its protocol: how it signs, the request it builds, and the payload shape it
 * parses. Everything around that protocol -- the preflight nothing may skip, the transport and
 * HTTP status readings, the shared caps, and how a parsed answer finishes into an OcrResult --
 * lives here, so a classification can no longer drift between providers (ADR 0136).
 */
#pragma once

#include <QString>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "capture/ocr_engine.h"
#include "http_transport.h"

namespace lens::ocr {

/// The caps every parsed answer obeys, whatever the service: lines, words, reader text in UTF-16
/// units, and the structured bytes one result may carry.
inline constexpr std::size_t kOcrMaxLines           = 2000;
inline constexpr std::size_t kOcrMaxWords           = 20000;
inline constexpr std::size_t kOcrMaxTextUnits       = 65536;
inline constexpr std::size_t kOcrMaxStructuredBytes = 4 * 1024 * 1024;

/// @brief What an HTTP status means when it means the same thing at every service.
struct OcrHttpStatus {
    capture::OcrStatus        status;
    capture::OcrFailureReason reason;
};

/// @return The shared reading of @p httpStatus, or nothing when this build has no shared answer
///         and the adapter decides -- a body-driven service reads its own error shape.
std::optional<OcrHttpStatus> classifyOcrHttpStatus(int httpStatus);

/// @brief The image bounds a service publishes, checked before anything is encoded.
struct OcrImageLimits {
    int maxEdge  = 0; ///< The longest side the service accepts; 0 leaves the edge unchecked.
    int minEdge  = 0; ///< The shortest side the service accepts; 0 sets no floor.
    int maxBytes = 0; ///< The in-memory image cap; 0 leaves size unchecked.
};

/**
 * @brief The refusal @p request earns before anything is encoded, or nothing when it may be sent.
 * @param configurationUsable The adapter's own verdict on its endpoints, credentials and language;
 *        the values differ per service, the outcome does not.
 *
 * The one preflight of the provider table: a consented source, a usable configuration, a readable
 * image inside the service's bounds, no cancellation, and a deadline still open.
 */
std::optional<capture::OcrResult> refuseOcrRequest(capture::OcrRequest const&        request,
                                                   QString const&                    engine,
                                                   OcrImageLimits const&             limits,
                                                   std::shared_ptr<std::atomic_bool> cancel,
                                                   bool                              configurationUsable);

/// @return The failure @p response's transport outcome stands for, or nothing when a complete
///         response arrived and the status may still decide.
std::optional<capture::OcrResult> refuseOcrTransport(HttpResponse const& response,
                                                     QString const&      engine,
                                                     std::uint64_t       requestId);

/**
 * @brief One response's aftermath in a single reading: transport outcome, then the shared HTTP
 *        classes, then the catch-all for a status no service classifies.
 * @return The refusal, or nothing when @p response carries a body the adapter may parse. Logs the
 *         answered request once; a refusal already said what went wrong.
 */
std::optional<capture::OcrResult> refuseOcrResponse(HttpResponse const& response,
                                                    QString const&      engine,
                                                    std::uint64_t       requestId);

/// @brief The result every answer from @p engine shares: no text, no lines, the stated failure.
capture::OcrResult ocrResultFor(QString const&            engine,
                                capture::OcrStatus        status,
                                std::uint64_t             requestId,
                                capture::OcrUploadState   upload,
                                capture::OcrFailureReason reason = capture::OcrFailureReason::None);

/**
 * @brief Finish a parsed answer the adapter left on @c OcrStatus::Ready.
 * @param result Text, lines, confidence and language used, as the adapter's parser gathered them.
 * @return The same result with the text trimmed and the geometry level read off the lines -- or
 *         the shared refusal for an empty text or an answer over the caps.
 *
 * The geometry level is derived from the boxes the lines actually carry, so an adapter cannot
 * claim word boxes it never had.
 */
capture::OcrResult finishOcrResult(capture::OcrResult result);

}
