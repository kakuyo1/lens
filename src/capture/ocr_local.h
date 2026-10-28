/** @file ocr_local.h
 * @brief Local engine policy: automatic selection, explicit choice, and resource inspection.
 */
#pragma once

#include <QString>

#include <memory>
#include <optional>
#include <string_view>

#include "ocr_engine.h"
#include "ocr_rapidocr.h"

namespace lens::capture {

enum class LocalEngineChoice : std::uint8_t {
    Auto,
    Windows,
    Tesseract,
    RapidOcr,
};

QString                          localEngineKey(LocalEngineChoice choice);
std::optional<LocalEngineChoice> parseLocalEngine(std::string_view key);

struct LocalOcrConfig {
    LocalEngineChoice engine   = LocalEngineChoice::Auto;
    OcrLanguage       language = OcrLanguage::English;
    TesseractConfig   tesseract;
    RapidOcrConfig    rapidOcr;
};

/// @brief What a configuration can do without starting an external program.
struct LocalOcrInspection {
    OcrStatus status = OcrStatus::RuntimeMissing;
    QString   engine;        ///< "windows", "tesseract" or "rapidocr": the engine the configuration would ask.
    QString   reason;        ///< The chosen engine's missing token, empty when ready.
    QString   windowsReason; ///< Why Windows was not chosen, for Auto; empty when it was.
};

/// @return Whether Tesseract's executable and the chosen language data are present, without running it.
OcrStatus tesseractResourceStatus(TesseractConfig const& config, QString* missing = nullptr);

/**
 * @return The engine policy's answer; ordinary enumeration only, never a child process.
 * @param verifyTrust Hash RapidOCR's executable and models for the explicit RapidOCR choice.
 */
LocalOcrInspection inspectLocalOcr(LocalOcrConfig const& config, bool verifyTrust = false);

/**
 * @brief Build the engine a configuration names.
 *
 * Auto chooses Windows only when its deployment and build conditions are both satisfied, and
 * otherwise Tesseract. An explicit choice is never rewritten: a missing Windows condition or a
 * missing Tesseract path is reported by that engine, not replaced by the other one.
 */
std::unique_ptr<OcrEngine> makeLocalOcr(LocalOcrConfig const& config);

}
