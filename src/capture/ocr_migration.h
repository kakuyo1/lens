/** @file ocr_migration.h
 * @brief The one-time mapping from a pre-OCR-settings document to the local policy.
 */
#pragma once

#include "ocr_local.h"

namespace lens::capture {

/// @brief What the old document and the effective data directory say.
struct OcrMigrationInput {
    int  version         = 0; ///< 0 means the key is absent, which is the only signal to migrate.
    bool customTesseract = false;
    bool englishData     = false;
    bool chineseData     = false;
};

struct OcrMigrationDecision {
    bool              migrate  = false;
    int               version  = 1;
    LocalEngineChoice engine   = LocalEngineChoice::Auto;
    OcrLanguage       language = OcrLanguage::English;
};

/**
 * @brief Decide the local policy for a document written before ocrConfigVersion.
 *
 * A reader who installed their own Tesseract keeps that explicit choice, because switching them
 * to Auto would silently route around the engine they installed. A pair of traineddata files
 * becomes mixed recognition; Chinese alone stays Chinese and says so, rather than pretending the
 * missing English half is there. Online mode is never derived from the explanation service.
 */
OcrMigrationDecision decideOcrMigration(OcrMigrationInput const& input);

}
