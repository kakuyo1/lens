#pragma once

#include <optional>

#include "capture/ocr_engine.h"

/**
 * @file capture_refusal.h
 * @brief Why a capture cannot start, and the token each reason is named by.
 *
 * The reasons are a closed set rather than a bare `QString` with a last-arm-catches-all sentence:
 * every runtime status is named here, so none of them reports as the wrong sentence. The tokens
 * are the bytes that crossed before the reason had a type, so the QML side and the translation
 * catalogue do not move. The sentence is still made where `lupdate` can see it: at a call site
 * whose every arm spells a literal `tr()`.
 */

namespace lens::app {

/// @brief Why a capture cannot start, or why one that did produced nothing.
///
/// The first five are this duty's own gates -- the budget, the probe, a job in flight, the
/// reader's switch, and an unconfigured online mode. The rest are what the runtime, the engine's
/// answer, or the framed region said. Every value is told apart here even though several share
/// one sentence: the sentence is the facade's decision, and this is the vocabulary (docs/adr/0025).
enum class CaptureRefusal {
    BudgetPaused,          ///< Today's spend reached the cap.
    Checking,              ///< The runtime is still being probed.
    Busy,                  ///< A recognition is in flight.
    ScreenshotOff,         ///< The reader has the OCR capture switch off.
    OnlineNotConfigured,   ///< The manual mode is online, but no online service is configured.
    OutsideScreen,         ///< The framed region is not wholly inside one screen.
    RegionTooLarge,        ///< The framed region is larger than the grabber or backend takes.
    EmptyText,             ///< OCR ran and found nothing in the region.
    InvalidImage,          ///< OCR could not read the image at all.
    Cancelled,             ///< The request was cancelled or superseded before an answer.
    LanguageMissing,       ///< The traineddata or model files the chosen language needs are not there.
    RuntimeMissing,        ///< There is no Tesseract or RapidOCR runtime to run.
    PathUnsupported,       ///< A local engine path is unusable, such as RapidOCR's non-ASCII model folder.
    Untrusted,             ///< The reader has not trusted this third-party engine, or its files changed.
    DeploymentUnsupported, ///< The engine's packaging or platform condition is not satisfied.
    StartFailed,           ///< The runtime is there and refused to start.
    TimedOut,              ///< The runtime started and did not finish in time.
    ProtocolError,         ///< The engine answered in a shape the adapter may not consume.
    TransportFailed,       ///< Online only: no complete response arrived.
    PartialResult,         ///< Online only: the service returned only part of the image's text.
    Failed,                ///< The runtime started and fell over.
};

/**
 * @brief The token a reason is named by.
 * @return One of "budget-paused", "checking", "busy", "screenshot-off", "online-unconfigured",
 *         "outside-screen", "too-large", "empty-text", "invalid-image", "cancelled",
 *         "language-missing", "runtime-missing", "path-unsupported", "untrusted",
 *         "deployment-unsupported", "start-failed", "timeout", "protocol-error",
 *         "transport-failed", "partial-result", "failed". Every one is asserted in the unit suite.
 */
inline const char* refusalKey(CaptureRefusal refusal)
{
    switch (refusal) {
        case CaptureRefusal::BudgetPaused: return "budget-paused";
        case CaptureRefusal::Checking: return "checking";
        case CaptureRefusal::Busy: return "busy";
        case CaptureRefusal::ScreenshotOff: return "screenshot-off";
        case CaptureRefusal::OnlineNotConfigured: return "online-unconfigured";
        case CaptureRefusal::OutsideScreen: return "outside-screen";
        case CaptureRefusal::RegionTooLarge: return "too-large";
        case CaptureRefusal::EmptyText: return "empty-text";
        case CaptureRefusal::InvalidImage: return "invalid-image";
        case CaptureRefusal::Cancelled: return "cancelled";
        case CaptureRefusal::LanguageMissing: return "language-missing";
        case CaptureRefusal::RuntimeMissing: return "runtime-missing";
        case CaptureRefusal::PathUnsupported: return "path-unsupported";
        case CaptureRefusal::Untrusted: return "untrusted";
        case CaptureRefusal::DeploymentUnsupported: return "deployment-unsupported";
        case CaptureRefusal::StartFailed: return "start-failed";
        case CaptureRefusal::TimedOut: return "timeout";
        case CaptureRefusal::ProtocolError: return "protocol-error";
        case CaptureRefusal::TransportFailed: return "transport-failed";
        case CaptureRefusal::PartialResult: return "partial-result";
        case CaptureRefusal::Failed: break;
    }
    return "failed";
}

/**
 * @brief What the OCR runtime's own state means for a capture.
 * @return The reason, or nothing when the runtime is fine -- `Ready` is not a reason a capture
 *         cannot start.
 * @note Every `capture::OcrStatus` is named above and the switch has no default arm, so the whole
 *       mapping is visible at once. A status added later and left unclassified falls through to
 *       @c Failed, a runtime that fell over: the gate stays shut rather than opening on a status
 *       nobody has looked at. Falling through to "nothing" instead would open the gate and report
 *       the runtime as fine.
 * @note Nothing *names* an unclassified status at compile time. C4061 is off by default and this
 *       tree sets no warning level; turning it on was measured at 458 warnings from one
 *       `switch (Qt::Key)`, so the honesty here is the shape of the switch rather than a
 *       diagnostic. docs/adr/0025 records that half of the trade.
 */
inline std::optional<CaptureRefusal> refusalOf(capture::OcrStatus status)
{
    switch (status) {
        case capture::OcrStatus::Ready: return std::nullopt;
        case capture::OcrStatus::RuntimeMissing: return CaptureRefusal::RuntimeMissing;
        case capture::OcrStatus::LanguageMissing: return CaptureRefusal::LanguageMissing;
        case capture::OcrStatus::PathUnsupported: return CaptureRefusal::PathUnsupported;
        case capture::OcrStatus::Untrusted: return CaptureRefusal::Untrusted;
        case capture::OcrStatus::DeploymentUnsupported: return CaptureRefusal::DeploymentUnsupported;
        case capture::OcrStatus::StartFailed: return CaptureRefusal::StartFailed;
        case capture::OcrStatus::TimedOut: return CaptureRefusal::TimedOut;
        case capture::OcrStatus::Failed: return CaptureRefusal::Failed;
        case capture::OcrStatus::InvalidImage: return CaptureRefusal::InvalidImage;
        case capture::OcrStatus::EmptyText: return CaptureRefusal::EmptyText;
        case capture::OcrStatus::Cancelled: return CaptureRefusal::Cancelled;
        case capture::OcrStatus::InputTooLarge: return CaptureRefusal::RegionTooLarge;
        case capture::OcrStatus::ProtocolError: return CaptureRefusal::ProtocolError;
        case capture::OcrStatus::ResultTooLarge: return CaptureRefusal::ProtocolError;
        case capture::OcrStatus::TransportFailed: return CaptureRefusal::TransportFailed;
        case capture::OcrStatus::PartialResult: return CaptureRefusal::PartialResult;
    }
    return CaptureRefusal::Failed;
}

/// @brief What the capture gate reads at the moment a capture is asked for.
struct CaptureGate {
    bool               budgetPaused     = false;
    bool               probing          = false; ///< The runtime probe is in flight.
    bool               busy             = false; ///< An OCR job is in flight, the probe included.
    bool               screenshotOn     = false; ///< The reader's OCR capture switch.
    bool               online           = false; ///< The manual mode is online.
    bool               onlineConfigured = false; ///< The online endpoint and key are complete.
    capture::OcrStatus runtime          = capture::OcrStatus::RuntimeMissing;
};

/**
 * @brief Why a capture cannot start, in the order the reasons outrank one another.
 * @param gate The duty's flags and the runtime's last known state.
 * @return The first reason that applies: the budget, then the probe, then a job in flight, then
 *         the reader's switch, then an unconfigured online mode, then the local runtime. A
 *         configured online mode does not consult the local runtime at all.
 */
inline std::optional<CaptureRefusal> gateRefusal(CaptureGate const& gate)
{
    if (gate.budgetPaused) return CaptureRefusal::BudgetPaused;
    if (gate.probing) return CaptureRefusal::Checking;
    if (gate.busy) return CaptureRefusal::Busy;
    if (not gate.screenshotOn) return CaptureRefusal::ScreenshotOff;
    if (gate.online) return gate.onlineConfigured ? std::nullopt : std::optional<CaptureRefusal>{CaptureRefusal::OnlineNotConfigured};
    return refusalOf(gate.runtime);
}

}
