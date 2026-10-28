/** @file ocr_engine.h
 * @brief The structured OCR contract shared by local engines and the online adapter.
 */
#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace lens::capture {

/// @brief The outcome of one recognition attempt.
///
/// `Ready` means non-empty valid text; `EmptyText` is its own answer so a blank region is not
/// reported as a runtime fault. Transport and protocol statuses belong to the online adapter but
/// live here because both halves deliver the same result type.
enum class OcrStatus : std::uint8_t {
    Ready,
    EmptyText,
    Cancelled,
    RuntimeMissing,
    LanguageMissing,
    PathUnsupported, ///< A local engine path is not one it can use, such as RapidOCR's non-ASCII model folder.
    Untrusted,       ///< The reader has not trusted this third-party engine, or its files changed.
    StartFailed,
    TimedOut,
    Failed,
    InvalidImage,
    InputTooLarge,
    ProtocolError,   ///< The engine answered, but not in the shape the adapter may consume.
    ResultTooLarge,  ///< Parsed output crossed a published limit.
    TransportFailed, ///< Online only: no complete response arrived.
    PartialResult,   ///< Online only: the service returned only part of the image's text.
};

/// @brief The recognition languages the first release offers.
enum class OcrLanguage : std::uint8_t {
    English,
    SimplifiedChinese,
    ChineseEnglish,
};

/// @return The stored spelling of @p language: `en`, `zh-Hans`, or `zh-Hans+en`.
QString ocrLanguageKey(OcrLanguage language);

/// @return The language @p key names, or nothing when the key is not one of the three.
std::optional<OcrLanguage> parseOcrLanguage(std::string_view key);

/// @brief What the caller knows about the image's shape, without guessing from its height.
enum class OcrLayoutHint : std::uint8_t {
    Unknown,
    Word,
    Line,
    Block,
    Sparse,
};

/// @brief Which path asked, so an online adapter can refuse anything but a consented request.
enum class OcrSource : std::uint8_t {
    Screenshot,
    AutoScan,
    LocalTest,
    OnlineTest,
};

/// @brief Why the automatic strategy left its primary engine, if it did.
enum class OcrFallbackReason : std::uint8_t {
    None,
    PrimaryUnavailable,
    PrimaryLanguageMissing,
    PrimaryInputTooLarge,
    PrimaryFailed,
    PrimaryTimedOut,
};

/// @brief How much geometry the result really carries; `has any box` is not enough to say `Word`.
enum class OcrGeometryLevel : std::uint8_t {
    None,
    Line,
    Word,
};

/// @brief Where an online request's bytes have gone; cancellation never moves it back to NotUploaded.
enum class OcrUploadState : std::uint8_t {
    NotApplicable,
    NotUploaded,
    UploadStarted,
    ServerResponded,
};

/// @brief An axis-aligned box in the original crop's physical pixels, top-left origin.
struct OcrBox {
    double x      = 0;
    double y      = 0;
    double width  = 0;
    double height = 0;
};

struct OcrWord {
    QString               text;
    std::optional<OcrBox> box; ///< Missing when the engine returned no real word rectangle.
};

struct OcrLine {
    QString               text;
    std::optional<OcrBox> box;
    std::vector<OcrWord>  words;
};

/// @brief A mean over the words the engine actually scored, and the coverage it represents.
struct OcrConfidence {
    double      average = 0;
    std::size_t words   = 0; ///< Words with a score.
    std::size_t total   = 0; ///< Words with and without a score.
};

struct OcrTiming {
    int schedulingMs    = 0;
    int preprocessingMs = 0;
    int recognitionMs   = 0;
    int totalMs         = 0;
};

/// @brief One local attempt, never carrying the engine's raw error text.
struct OcrAttempt {
    QString   engine;
    OcrStatus status    = OcrStatus::Failed;
    int       elapsedMs = 0;
};

/// @brief The unified result. `text` is reader content and must never reach a log.
struct OcrResult {
    OcrStatus                    status = OcrStatus::RuntimeMissing;
    QString                      text;
    std::vector<OcrLine>         lines;
    std::optional<OcrConfidence> confidence;
    QString                      actualEngine;
    QString                      languageUsed;
    OcrGeometryLevel             geometryLevel  = OcrGeometryLevel::None;
    OcrFallbackReason            fallbackReason = OcrFallbackReason::None;
    OcrTiming                    timing;
    std::vector<OcrAttempt>      attempts; ///< At most the two entries of the automatic strategy.
    OcrUploadState               uploadState = OcrUploadState::NotApplicable;
    std::uint64_t                requestId   = 0;
};

/// @brief One recognition task, frozen by the caller before it is scheduled.
struct OcrRequest {
    std::uint64_t requestId = 0;
    OcrSource     source    = OcrSource::Screenshot;
    QImage        image;
    /// Empty leaves the engine's own configured language policy in charge.
    std::optional<OcrLanguage> language;
    OcrLayoutHint              layout = OcrLayoutHint::Unknown;
    /// Covers scheduling, preprocessing and recognition. Reset by the automatic fallback.
    std::chrono::milliseconds deadline{10000};
};

struct TesseractConfig {
    QString executable;    ///< Empty: resolve only the bundled ocr/tesseract.exe.
    QString dataDirectory; ///< Empty: use tessdata next to the selected executable.
    /// Empty keeps the legacy directory-derived language choice; stage C always sets it.
    std::optional<OcrLanguage> language;
    int                        timeoutMs = 10000;
};

struct OcrEngine {
    virtual ~OcrEngine() = default;
    /// @return Runtime and language resource availability, including loader failures.
    virtual OcrStatus probe(std::shared_ptr<std::atomic_bool> cancel = {}) const = 0;
    /// @brief Blocking worker operation with a deadline and cooperative cancellation.
    virtual OcrResult recognize(OcrRequest const&                 request,
                                std::shared_ptr<std::atomic_bool> cancel = {}) const = 0;
};

/// @brief The Tesseract language spec a policy needs, and what is missing when it cannot run.
struct TesseractLanguageSelection {
    QString spec;
    QString missing; ///< Empty when @c spec is usable.
    bool    ok() const
    {
        return not spec.isEmpty() and missing.isEmpty();
    }
};

/**
 * @brief Map a recognition language onto the traineddata files @p dataDirectory holds.
 *
 * Mixed Chinese and English needs both files: naming `chi_sim+eng` with one missing fails the
 * whole run, and silently dropping the other language is exactly what the reader did not ask for.
 */
TesseractLanguageSelection tesseractLanguageFor(OcrLanguage language, QString const& dataDirectory);

/**
 * @brief The recognition languages @p dataDirectory can serve, in the order Tesseract takes them.
 *
 * Legacy behaviour for a configuration that has not chosen a language yet. New code should call
 * tesseractLanguageFor(); this stays because the migration reads what the directory actually has.
 *
 * @param dataDirectory A tessdata directory; may not exist.
 * @return A `-l` value such as `chi_sim+eng` or `eng`, or empty when neither file is readable.
 */
QString tesseractLanguages(QString const& dataDirectory);

/**
 * @brief Fill in what an empty TesseractConfig resolves to.
 *
 * @param config Executable and data directory as configured; either may be empty.
 * @return The same config with the bundled fallbacks filled in.
 */
TesseractConfig resolveTesseract(TesseractConfig config);

std::unique_ptr<OcrEngine> makeTesseractOcr(TesseractConfig const& config = {});
/// @return Cheap deterministic hash of grayscale pixels, excluding padding and image metadata.
std::uint64_t pixelFingerprint(QImage const& image);

/// @return Whether @p box is finite, non-negative and inside @p image after integer rounding.
bool boxFitsImage(OcrBox const& box, QSize image, double tolerance = 0.0);

} // namespace lens::capture
