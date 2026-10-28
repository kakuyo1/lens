/** @file ocr_migration.cpp
 * @brief The pure migration rule behind ocr_migration.h.
 */
#include "ocr_migration.h"

namespace lens::capture {

OcrMigrationDecision decideOcrMigration(OcrMigrationInput const& input)
{
    if (input.version >= 1) return {.migrate = false, .version = input.version};

    OcrMigrationDecision decision{.migrate = true, .version = 1};
    if (not input.customTesseract) {
        decision.engine = LocalEngineChoice::Auto;
        // A reader may have added chi_sim beside the bundled English without pointing at another
        // engine; the directory's models are still the reader's intent, so they survive Auto.
        if (input.chineseData and input.englishData)
            decision.language = OcrLanguage::ChineseEnglish;
        else if (input.chineseData)
            decision.language = OcrLanguage::SimplifiedChinese;
        else
            decision.language = OcrLanguage::English;
        return decision;
    }

    decision.engine = LocalEngineChoice::Tesseract;
    if (input.chineseData and input.englishData)
        decision.language = OcrLanguage::ChineseEnglish;
    else if (input.chineseData)
        decision.language = OcrLanguage::SimplifiedChinese;
    else
        decision.language = OcrLanguage::English;
    return decision;
}

}
