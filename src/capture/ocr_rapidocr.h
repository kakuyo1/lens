/** @file ocr_rapidocr.h
 * @brief The user-installed RapidOCR-json v0.2.0 adapter and its trust boundary.
 */
#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>

#include "ocr_engine.h"

namespace lens::capture {

/// @brief The one distribution the adapter accepts, and the paths the reader chose for it.
struct RapidOcrConfig {
    QString     executable;
    QString     modelsDirectory;
    QString     trustDigest; ///< SHA-256 written by an explicit detection, empty until trusted.
    OcrLanguage language  = OcrLanguage::English;
    int         timeoutMs = 10000;
    /// False only for the detection run: it must be able to read a distribution before trust exists.
    bool requireTrust = true;
};

struct RapidOcrInspection {
    OcrStatus status = OcrStatus::RuntimeMissing;
    QString   reason;
};

/// @return The model file names the selected language uses, in hash and trust order.
QStringList rapidOcrModelFiles(OcrLanguage language);

/// @return Whether @p path contains only ASCII characters; an empty path is vacuously ASCII.
bool rapidOcrPathIsAscii(QString const& path);

/**
 * @brief Hash the executable and the selected model files.
 * @return Lowercase SHA-256, or empty when a file is missing, unreadable or over the size cap.
 */
QString rapidOcrTrustDigest(RapidOcrConfig const& config);

/**
 * @brief Inspect paths, model files and optionally the trust digest without starting a process.
 * @param verifyTrust Hash the files and compare them with @c config.trustDigest; false is the cheap
 *                    check used by the settings projection.
 */
RapidOcrInspection inspectRapidOcr(RapidOcrConfig const& config, bool verifyTrust = false);

/// @brief Parse one response line; exposed separately so the protocol has offline tests.
OcrResult parseRapidOcrResponse(QByteArray const& response,
                                QSize             imageSize,
                                std::uint64_t     requestId,
                                QString const&    language,
                                QString const&    engine,
                                int               elapsedMs);

/**
 * @brief Whether the Chinese pass left a Latin-only line whose words look glued together.
 *
 * The Chinese recogniser writes Latin runs without spaces; a line of several English words then
 * arrives as one long token. A single short word is left alone, so the second process is only
 * paid for when there is something to gain.
 */
bool rapidOcrNeedsLatinPass(OcrResult const& result);

/**
 * @brief Give each Latin-only line the English pass's text, keeping the Chinese pass's boxes.
 *
 * Both passes share one detector, so their line boxes are the same; a line holding Chinese keeps
 * the Chinese pass's text because the English recogniser drops those characters outright.
 *
 * @param totalMs Wall time of the whole two-pass read, recorded as the merged timing.
 */
OcrResult mergeRapidOcrLatinPass(OcrResult primary, OcrResult const& english, int totalMs);

/// @brief The adapter: one process per task, killed after its answer, never a persistent worker.
std::unique_ptr<OcrEngine> makeRapidOcr(RapidOcrConfig const& config);

}
