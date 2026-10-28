/** @file ocr_local.cpp
 * @brief Local routing and file-level resource inspection.
 */
#include "ocr_local.h"

#include <QFileInfo>
#include <QStringList>

namespace lens::capture {
namespace {

TesseractConfig withLanguage(TesseractConfig config, OcrLanguage const language)
{
    config.language = language;
    return config;
}

}

QString localEngineKey(LocalEngineChoice const choice)
{
    switch (choice) {
        case LocalEngineChoice::Auto: return QStringLiteral("auto");
        case LocalEngineChoice::Tesseract: return QStringLiteral("tesseract");
        case LocalEngineChoice::RapidOcr: return QStringLiteral("rapidocr");
    }
    return QStringLiteral("auto");
}

std::optional<LocalEngineChoice> parseLocalEngine(std::string_view const key)
{
    if (key == "auto") return LocalEngineChoice::Auto;
    if (key == "tesseract") return LocalEngineChoice::Tesseract;
    if (key == "rapidocr") return LocalEngineChoice::RapidOcr;
    return std::nullopt;
}

OcrStatus tesseractResourceStatus(TesseractConfig const& config, QString* missing)
{
    auto const resolved = resolveTesseract(config);
    if (resolved.executable.isEmpty() or not QFileInfo{resolved.executable}.isFile()) {
        if (missing) *missing = QStringLiteral("runtime-missing");
        return OcrStatus::RuntimeMissing;
    }
    auto const selection = resolved.language ? tesseractLanguageFor(*resolved.language, resolved.dataDirectory)
                                             : TesseractLanguageSelection{.spec = tesseractLanguages(resolved.dataDirectory)};
    if (not selection.ok()) {
        if (missing) *missing = selection.missing;
        return OcrStatus::LanguageMissing;
    }
    if (missing) missing->clear();
    return OcrStatus::Ready;
}

LocalOcrInspection inspectLocalOcr(LocalOcrConfig const& config, bool const verifyTrust)
{
    if (config.engine == LocalEngineChoice::RapidOcr) {
        auto rapidOcrConfig     = config.rapidOcr;
        rapidOcrConfig.language = config.language;
        auto const inspection   = inspectRapidOcr(rapidOcrConfig, verifyTrust);
        return {.status = inspection.status, .engine = QStringLiteral("rapidocr"), .reason = inspection.reason};
    }

    QString    missing;
    auto const status = tesseractResourceStatus(withLanguage(config.tesseract, config.language), &missing);
    return {.status = status, .engine = QStringLiteral("tesseract"), .reason = missing};
}

std::unique_ptr<OcrEngine> makeLocalOcr(LocalOcrConfig const& config)
{
    if (config.engine == LocalEngineChoice::RapidOcr) {
        auto rapidOcrConfig     = config.rapidOcr;
        rapidOcrConfig.language = config.language;
        return makeRapidOcr(rapidOcrConfig);
    }
    return makeTesseractOcr(withLanguage(config.tesseract, config.language));
}

}
