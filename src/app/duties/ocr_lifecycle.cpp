/**
 * @file ocr_lifecycle.cpp
 * @brief The engines' life, moved verbatim from the capture duty's engine block (docs/adr/0165).
 */

#include "ocr_lifecycle.h"

#include <QDir>
#include <QFileInfo>

#include "capture/ocr_migration.h"
#include "core/domain/settings_table.h"
#include "app/settings_access.h"
#include "util/log.h"

namespace lens::app {

capture::TesseractConfig storedTesseractConfig(StorageDuty& storage)
{
    auto const keys = storage.section();
    auto const language =
        capture::parseOcrLanguage(settings::read(keys, *core::settings::find("ocrLocalLanguage")).toString().toStdString());
    return {.executable    = settings::read(keys, *core::settings::find("tesseractExecutable")).toString(),
            .dataDirectory = settings::read(keys, *core::settings::find("tesseractDataDirectory")).toString(),
            .language      = language.value_or(capture::OcrLanguage::English)};
}

capture::RapidOcrConfig storedRapidOcrConfig(StorageDuty& storage)
{
    auto const keys = storage.section();
    auto const language =
        capture::parseOcrLanguage(settings::read(keys, *core::settings::find("ocrLocalLanguage")).toString().toStdString());
    return {.executable      = settings::read(keys, *core::settings::find("rapidOcrExecutable")).toString(),
            .modelsDirectory = settings::read(keys, *core::settings::find("rapidOcrModelsDirectory")).toString(),
            .trustDigest     = settings::read(keys, *core::settings::find("rapidOcrTrust")).toString(),
            .language        = language.value_or(capture::OcrLanguage::English)};
}

capture::LocalOcrConfig storedLocalConfig(StorageDuty& storage)
{
    auto const keys        = storage.section();
    auto const engineKey   = settings::read(keys, *core::settings::find("ocrLocalEngine")).toString();
    auto const languageKey = settings::read(keys, *core::settings::find("ocrLocalLanguage")).toString();
    return {.engine    = capture::parseLocalEngine(engineKey.toStdString()).value_or(capture::LocalEngineChoice::Auto),
            .language  = capture::parseOcrLanguage(languageKey.toStdString()).value_or(capture::OcrLanguage::English),
            .tesseract = storedTesseractConfig(storage),
            .rapidOcr  = storedRapidOcrConfig(storage)};
}

QString statusToken(capture::OcrStatus const status)
{
    auto const refusal = refusalOf(status);
    return refusal ? QString::fromLatin1(refusalKey(*refusal)) : QStringLiteral("ready");
}

QString tokenOf(CaptureRefusal const refusal)
{
    return QString::fromLatin1(refusalKey(refusal));
}

namespace {

/// @return The token for why a service refused, which the settings page explains in one sentence.
QString failureReasonToken(capture::OcrFailureReason const reason)
{
    switch (reason) {
        case capture::OcrFailureReason::None: return QStringLiteral("none");
        case capture::OcrFailureReason::Authentication: return QStringLiteral("authentication");
        case capture::OcrFailureReason::NotActivated: return QStringLiteral("not-activated");
        case capture::OcrFailureReason::Quota: return QStringLiteral("quota");
        case capture::OcrFailureReason::Image: return QStringLiteral("image");
        case capture::OcrFailureReason::Unavailable: return QStringLiteral("unavailable");
        case capture::OcrFailureReason::Unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

}

OcrLifecycle::OcrLifecycle(StorageDuty& storage, std::function<void()> cancelJobs)
    : storage_(storage), cancelJobs_(std::move(cancelJobs))
{}

void OcrLifecycle::adopt(std::unique_ptr<capture::OcrEngine> engine, capture::LocalOcrConfig const& local)
{
    ocr_ = std::move(engine);
    inspect(local);
}

void OcrLifecycle::rebuild()
{
    // A probe or recognition in flight belongs to the engine that just went away, and its answer
    // would land on the new one's state. Cancel it before the old engine is released.
    cancelJobs_();
    auto const local = storedLocalConfig(storage_);
    ocr_             = capture::makeLocalOcr(local);
    inspect(local);
    ocrTestState_  = QStringLiteral("idle");
    ocrTestReason_ = QStringLiteral("none");
    ocrTestEngine_.clear();
    ocrTestLanguage_.clear();
    ocrTestMs_ = 0;
}

void OcrLifecycle::inspect(capture::LocalOcrConfig const& local)
{
    // Ordinary readiness is a file/resource inspection; no child process is started until the
    // reader asks for a real test or captures a region. RapidOCR's digest is verified here, at
    // startup and after a path or language change, rather than on every settings projection.
    localInspection_ = capture::inspectLocalOcr(local, true);
    ocrState_        = localInspection_.status;
}

void OcrLifecycle::migrate()
{
    auto keys = storage_.section();
    if (keys.value<std::string>("ocrLocalEngine", std::string{}) == "windows") {
        settings::write(keys, *core::settings::find("ocrLocalEngine"), QStringLiteral("auto"));
        LENS_INFO("OCR engine 'windows' removed; using auto");
    }
    // The first release selected its only protocol by naming its address. The provider is its own
    // field now, and a profile that still carries that address -- or the key typed for that one
    // protocol -- would otherwise be stranded as unconfigured under a service nobody chose.
    if (keys.value<std::string>("ocrOnlineProvider", std::string{}).empty() and
        (not keys.value<std::string>("ocrOnlineEndpoint", std::string{}).empty() or
         not keys.value<std::string>("OCR-API-KEY", std::string{}).empty())) {
        settings::write(keys, *core::settings::find("ocrOnlineProvider"), QStringLiteral("ocrspace"));
        LENS_INFO("online OCR provider restored from the first release's configuration");
    }
    if (keys.value<int>("ocrConfigVersion", 0) >= 1) return;

    auto const tesseract = storedTesseractConfig(storage_);
    auto const resolved  = capture::resolveTesseract(tesseract);
    auto const usable    = [&](char const* code) {
        QFileInfo const file{QDir{resolved.dataDirectory}.filePath(QString::fromLatin1(code) + QStringLiteral(".traineddata"))};
        return file.isReadable() and file.size() > 0;
    };
    auto const decision = capture::decideOcrMigration(
        {.version         = 0,
         .customTesseract = not tesseract.executable.trimmed().isEmpty() or not tesseract.dataDirectory.trimmed().isEmpty(),
         .englishData     = usable("eng"),
         .chineseData     = usable("chi_sim")});
    if (not decision.migrate) return;

    // Version last: a run interrupted between these writes starts the same migration again.
    settings::write(keys, *core::settings::find("ocrLocalEngine"), capture::localEngineKey(decision.engine));
    settings::write(keys, *core::settings::find("ocrLocalLanguage"), capture::ocrLanguageKey(decision.language));
    settings::write(keys, *core::settings::find("ocrMode"), QStringLiteral("local"));
    settings::write(keys, *core::settings::find("ocrConfigVersion"), decision.version);
    LENS_INFO("OCR settings migrated to engine '{}', language '{}'",
              capture::localEngineKey(decision.engine).toStdString(),
              capture::ocrLanguageKey(decision.language).toStdString());
}

void OcrLifecycle::setStatus(capture::OcrStatus const status)
{
    ocrState_ = status;
}

void OcrLifecycle::testStarted()
{
    ocrTestState_  = QStringLiteral("checking");
    ocrTestReason_ = QStringLiteral("none");
}

void OcrLifecycle::testRefused(QString token)
{
    ocrTestState_ = std::move(token);
}

void OcrLifecycle::testFinished(capture::OcrResult const& result)
{
    auto const refusal = refusalOf(result.status);
    ocrTestState_      = refusal ? tokenOf(*refusal) : QStringLiteral("ready");
    ocrTestReason_     = failureReasonToken(result.failureReason);
    ocrTestEngine_     = result.actualEngine;
    ocrTestLanguage_   = result.languageUsed;
    ocrTestMs_         = result.timing.totalMs;
}

void OcrLifecycle::testCancelled()
{
    ocrTestState_  = QStringLiteral("idle");
    ocrTestReason_ = QStringLiteral("none");
}

void OcrLifecycle::detectIdle()
{
    rapidOcrDetectState_ = QStringLiteral("idle");
    rapidOcrDetectMs_    = 0;
}

void OcrLifecycle::detectStarted()
{
    rapidOcrDetectState_ = QStringLiteral("checking");
    rapidOcrDetectMs_    = 0;
}

void OcrLifecycle::detectRefused(capture::OcrStatus const status)
{
    rapidOcrDetectState_ = statusToken(status);
    rapidOcrDetectMs_    = 0;
}

void OcrLifecycle::detectDone(QString state, int const ms)
{
    rapidOcrDetectState_ = std::move(state);
    rapidOcrDetectMs_    = ms;
}

}
