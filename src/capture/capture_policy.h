/** @file capture_policy.h
 * @brief Offline capture policy shared by the desktop hook and OCR pipeline.
 */
#pragma once

#include <cstdint>
#include <algorithm>
#include <optional>

#include <QChar>
#include <QList>
#include <QString>

#include "ocr_engine.h"

namespace lens::capture {

/// @brief What becomes of an OCR result once it is back.
enum class OcrResultFate {
    Current,    ///< It still answers the request that started it.
    Superseded, ///< A newer request or a settings change moved the generation on; drop it.
    Withdrawn,  ///< A scan whose window, pause or switch changed while it ran; drop it and restart the scan.
};

/// @brief The request an OCR result belongs to, beside the duty's state as it is now.
struct OcrResultContext {
    QString                jobContext; ///< The scan context the job started in; empty for a framed screenshot.
    std::uint64_t          jobGeneration        = 0;
    std::uint64_t          scanGeneration       = 0; ///< The scan path's generation now.
    std::uint64_t          screenshotGeneration = 0; ///< The screenshot path's generation now.
    std::optional<QString> foregroundContext;        ///< The foreground window's context now; nothing when there is none.
    bool                   budgetPaused = false;
    bool                   scanPaused   = false;
    bool                   autoScan     = false;
};

/**
 * @brief Whether an OCR result still applies to the request that started it.
 * @param result The request and the state it is judged against.
 * @return Superseded when the generation of the job's path moved on, which outranks the rest;
 *         Withdrawn when a scan's window, a pause or the scan switch changed; Current otherwise.
 */
inline OcrResultFate ocrResultFate(OcrResultContext const& result)
{
    bool const scan = not result.jobContext.isEmpty();
    if (result.jobGeneration != (scan ? result.scanGeneration : result.screenshotGeneration)) return OcrResultFate::Superseded;
    if (not scan) return OcrResultFate::Current;
    if (result.budgetPaused or result.scanPaused or not result.foregroundContext or
        *result.foregroundContext != result.jobContext or not result.autoScan)
        return OcrResultFate::Withdrawn;
    return OcrResultFate::Current;
}

/// @return Whether a recognition status speaks of the runtime rather than of the image or answer it
///         was given: an empty or unreadable region says nothing about whether OCR works.
constexpr bool isRuntimeFault(OcrStatus const status)
{
    switch (status) {
        case OcrStatus::Ready:
        case OcrStatus::EmptyText:
        case OcrStatus::InvalidImage:
        case OcrStatus::Cancelled:
        case OcrStatus::ProtocolError:
        case OcrStatus::InputTooLarge:
        case OcrStatus::ResultTooLarge:
        case OcrStatus::TransportFailed:
        case OcrStatus::PartialResult: return false;
        case OcrStatus::RuntimeMissing:
        case OcrStatus::LanguageMissing:
        case OcrStatus::PathUnsupported:
        case OcrStatus::Untrusted:
        case OcrStatus::StartFailed:
        case OcrStatus::TimedOut:
        case OcrStatus::Failed: return true;
    }
    return true;
}

/// @return Whether either axis reaches the threshold, including negative screen origins.
constexpr bool exceedsDragThreshold(std::int64_t const dx, std::int64_t const dy, int const threshold)
{
    return threshold > 0 and
           (dx >= threshold or dx <= -threshold or dy >= threshold or dy <= -threshold);
}

/// @return A valid prose length; malformed persisted values fall back to the default.
constexpr int minimumWordLength(int const value)
{
    return value >= 2 and value <= 5 ? value : 3;
}

/// @return The number of Unicode letter code points in an explicit selection.
inline int selectionLetterCount(QString const& text)
{
    const auto codepoints = text.toUcs4();
    return static_cast<int>(std::count_if(codepoints.cbegin(), codepoints.cend(), [](uint codepoint) {
        return QChar::isLetter(codepoint);
    }));
}

}
