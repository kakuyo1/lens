/** @file ocr_engine.h
 * @brief Injectable OCR boundary shared by screenshot and automatic scanning.
 */
#pragma once

#include <QImage>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>

namespace lens::capture {

enum class OcrStatus { Ready,
                       RuntimeMissing,
                       LanguageMissing, ///< Neither an English nor a Chinese traineddata is there.
                       StartFailed,
                       TimedOut,
                       Failed,
                       InvalidImage,
                       EmptyText };

struct OcrResult {
    OcrStatus status = OcrStatus::RuntimeMissing;
    QString   text; ///< Local OCR output; never log this or use it as a scan request body.
};

struct TesseractConfig {
    QString executable;    ///< Empty: resolve only the bundled ocr/tesseract.exe.
    QString dataDirectory; ///< Empty: use tessdata next to the selected executable.
    int     timeoutMs = 10000;
};

struct OcrEngine {
    virtual ~OcrEngine() = default;
    /// @return Runtime and language resource availability, including loader failures.
    virtual OcrStatus probe(std::shared_ptr<std::atomic_bool> cancel = {}) const = 0;
    /// @brief Blocking worker operation with a deadline and cooperative cancellation.
    virtual OcrResult recognize(QImage const& image, std::shared_ptr<std::atomic_bool> cancel = {}) const = 0;
};

/**
 * @brief The recognition languages @p dataDirectory can serve, in the order Tesseract takes them.
 *
 * The reader supplies the data directory, so whether Chinese can be read is a property of that
 * directory rather than a setting: Chinese is added to the list when its traineddata is there, and
 * left out when it is not, which is what keeps one install reading English and the next reading
 * both. Chinese comes first of the pair -- the reader who installed it did so to read Chinese.
 *
 * @param dataDirectory A tessdata directory; may not exist.
 * @return A `-l` value such as `chi_sim+eng` or `eng`, or empty when neither file is readable.
 */
QString tesseractLanguages(QString const& dataDirectory);

/**
 * @brief Fill in what an empty TesseractConfig resolves to.
 *
 * The engine does this to its own copy when it is built. It is public so that a surface can show
 * the reader the paths that will actually be used: an empty setting means the bundled runtime,
 * and the reader has to be able to see which runtime that is before deciding to point elsewhere.
 *
 * @param config Executable and data directory as configured; either may be empty.
 * @return The same config with the bundled fallbacks filled in. Both may still be empty when there
 *         is no bundled runtime to resolve to.
 */
TesseractConfig resolveTesseract(TesseractConfig config);

std::unique_ptr<OcrEngine> makeTesseractOcr(TesseractConfig const& config = {});
/// @return Cheap deterministic hash of grayscale pixels, excluding padding and image metadata.
std::uint64_t pixelFingerprint(QImage const& image);

}
