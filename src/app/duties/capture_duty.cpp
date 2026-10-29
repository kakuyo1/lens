/**
 * @file capture_duty.cpp
 * @brief Selection capture and local OCR acquisition, independent of explanation presentation.
 */

#include "capture_duty.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QRegularExpression>
#include <QScreen>
#include <QtGui/qscreen_platform.h>

#include <algorithm>
#include <chrono>
#include <iterator>

#include "capture/capture_policy.h"
#include "capture/foreground_window.h"
#include "capture/ocr_local.h"
#include "capture/ocr_migration.h"
#include "ocr/online_ocr.h"
#include "ocr/qt_http_transport.h"
#include "core/domain/filter_core.h"
#include "core/domain/settings_table.h"
#include "app/platform/mouse_selection_hook.h"
#include "app/settings_access.h"
#include "capture/ocr_rapidocr.h"
#include "llm/llm_pure.h"
#include "util/log.h"

namespace lens::app {
namespace {

/// @return The monitor behind a QScreen, in the same terms `capture::monitorAt` speaks.
///
/// A screen is looked up by the monitor it is rather than by the name it is called: Qt names a
/// screen by the monitor's friendly name and Win32 by its device name, and a comparison between
/// the two is always false -- which cost the screenshot path every capture and would have cost the
/// scanner every frame.
quintptr monitorOf(QScreen const* screen)
{
    if (auto* const native = screen->nativeInterface<QNativeInterface::QWindowsScreen>())
        return reinterpret_cast<quintptr>(native->handle());
    return 0;
}

constexpr int kMaxSelectionChars = 1000;

/// @return The document key that holds one credential slot, or empty for a slot this build has no
///         field for. Fixed strings: a slot name never chooses where a secret is written.
QString credentialSettingKey(QString const& slot)
{
    if (slot == QLatin1String("apiKey")) return QStringLiteral("OCR-API-KEY");
    if (slot == QLatin1String("secretKey")) return QStringLiteral("OCR-SECRET-KEY");
    return {};
}

/// @return The Tesseract the reader pointed at, or the bundled runtime when both are empty.
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

/// @return The diagnostic token for an OCR status, including the ready state.
QString statusToken(capture::OcrStatus const status)
{
    auto const refusal = refusalOf(status);
    return refusal ? QString::fromLatin1(refusalKey(*refusal)) : QStringLiteral("ready");
}

/// @return The token for a reason, as the QString the QML boundary reads.
QString tokenOf(CaptureRefusal refusal)
{
    return QString::fromLatin1(refusalKey(refusal));
}

/// @return A synthetic page for the explicit local test: no desktop pixels, no user content.
QImage localTestImage(capture::OcrLanguage const language)
{
    QImage image{560, 140, QImage::Format_RGB32};
    image.fill(Qt::white);
    QPainter painter{&image};
    QFont    font{QStringLiteral("Segoe UI")};
    font.setPixelSize(36);
    painter.setFont(font);
    painter.setPen(Qt::black);
    QString const text = language == capture::OcrLanguage::English             ? QStringLiteral("Lens OCR 12345")
                         : language == capture::OcrLanguage::SimplifiedChinese ? QStringLiteral("识别测试 12345")
                                                                               : QStringLiteral("Lens 识别 12345");
    painter.drawText(image.rect().adjusted(24, 24, -24, -24), Qt::AlignLeft | Qt::AlignVCenter, text);
    return image;
}

}

QString kindName(core::SelectionKind const kind)
{
    switch (kind) {
        case core::SelectionKind::Entity: return QStringLiteral("entity");
        case core::SelectionKind::Sentence: return QStringLiteral("sentence");
        case core::SelectionKind::Translation: return QStringLiteral("translation");
        case core::SelectionKind::Word: return QStringLiteral("word");
    }
    return QStringLiteral("word");
}

CaptureDuty::CaptureDuty(StorageDuty& storage, MouseSelectionHook& hook, QObject* parent)
    : CaptureDuty(storage, hook, nullptr, {}, parent)
{}

CaptureDuty::CaptureDuty(StorageDuty&                        storage,
                         MouseSelectionHook&                 hook,
                         std::unique_ptr<capture::OcrEngine> ocr,
                         QObject*                            parent)
    : CaptureDuty(storage, hook, std::move(ocr), {}, parent)
{}

CaptureDuty::CaptureDuty(StorageDuty&                        storage,
                         MouseSelectionHook&                 hook,
                         std::unique_ptr<capture::OcrEngine> ocr,
                         std::shared_ptr<ocr::HttpTransport> onlineTransport,
                         QObject*                            parent)
    : QObject(parent), storage_(storage), hook_(hook), grabber_(std::make_unique<SelectionTextGrabber>()),
      ocr_(std::move(ocr)), executor_(std::make_unique<capture::OcrExecutor>(this)),
      onlineTransport_(std::move(onlineTransport)), onlineTokens_(ocr::makeMemoryOcrTokenStore()),
      scan_(capture::makeScanPipeline())
{
    if (not onlineTransport_) onlineTransport_ = std::make_shared<ocr::QtHttpTransport>();
    connect(&hook_, &MouseSelectionHook::selectionReleased, this, &CaptureDuty::onSelectionReleased);
    connect(&hook_, &MouseSelectionHook::pointerPressed, this, &CaptureDuty::pointerPressed);
    connect(executor_.get(), &capture::OcrExecutor::finished, this, &CaptureDuty::onOcrFinished);
    consentTimer_.setSingleShot(true);
    uploadTimer_.setSingleShot(true);
    connect(&consentTimer_, &QTimer::timeout, this, &CaptureDuty::onConsentTimeout);
    connect(&uploadTimer_, &QTimer::timeout, this, &CaptureDuty::onUploadTimeout);

    auto const               names = settings().value("scanWhitelist").toString().split(';', Qt::SkipEmptyParts);
    std::vector<std::string> whitelist;
    for (auto const& name : names)
        whitelist.push_back(name.toStdString());
    scan_->setWhitelist(std::move(whitelist));
    scanTimer_.setInterval(1000);
    connect(&scanTimer_, &QTimer::timeout, this, &CaptureDuty::scanTick);
    scanTimer_.start();
    migrateOcrSettings();
    localInspection_ = capture::inspectLocalOcr(storedLocalConfig(storage_));
    if (ocr_)
        probeOcr();
    else
        rebuildOcr();
}

CaptureDuty::~CaptureDuty()
{
    scanTimer_.stop();
    consentTimer_.stop();
    uploadTimer_.stop();
    executor_->cancel();
    jobContext_.reset();
    upload_.reset();
}

QVariantMap CaptureDuty::settings() const
{
    auto const keys = storage_.section();
    // The capture rows, read through their own rules: a stored length the document did not mean
    // lands on the row's range the same way a written one would.
    const auto readOf = [&keys](std::string_view const key) {
        const auto* const row = core::settings::find(key);
        return settings::read(keys, *row);
    };
    auto const local    = storedLocalConfig(storage_);
    auto const resolved = capture::resolveTesseract(local.tesseract);
    auto const tokenFor = [](capture::OcrStatus const status) {
        auto const refusal = refusalOf(status);
        return refusal ? tokenOf(*refusal) : QStringLiteral("ready");
    };
    auto const        online      = settings::read(keys, *core::settings::find("ocrMode")).toString() == QLatin1String("online");
    auto const        onlineReady = onlineConfigured();
    auto const* const provider    = onlineProvider();
    QVariantList      providers;
    for (auto const& entry : ocr::onlineOcrProviders())
        providers.append(QVariantMap{{"value", entry.id}, {"label", entry.label}, {"group", entry.group}, {"note", ""}});
    QStringList languages;
    for (auto const* const value : {"eng", "auto"})
        if (provider == nullptr or provider->languages.contains(QLatin1String(value))) languages << QLatin1String(value);
    QVariantList credentials;
    if (provider != nullptr) {
        auto const document = storage_.section();
        for (auto const& slot : provider->credentials)
            credentials.append(QVariantMap{
                {"slot", slot},
                {"present", not document.value<std::string>(credentialSettingKey(slot).toStdString(), {}).empty()}});
    }
    return {{"ocrCapture", readOf("ocrCapture")},
            {"autoScan", readOf("autoScan")},
            {"ocrAvailable", ocrState_ == capture::OcrStatus::Ready},
            {"minimumWordLength", readOf("minimumWordLength")},
            {"ocrConfigVersion", readOf("ocrConfigVersion")},
            {"ocrMode", readOf("ocrMode")},
            {"ocrLocalEngine", readOf("ocrLocalEngine")},
            {"ocrLocalLanguage", readOf("ocrLocalLanguage")},
            {"ocrOnlineProvider", readOf("ocrOnlineProvider")},
            {"ocrOnlineProviderLabel", provider != nullptr ? provider->label : QString{}},
            {"ocrOnlineEndpoint", readOf("ocrOnlineEndpoint")},
            {"ocrOnlineEndpointDefault", provider != nullptr ? provider->endpoint.toString() : QString{}},
            {"ocrOnlineLanguage", readOf("ocrOnlineLanguage")},
            {"ocrOnlineProviders", providers},
            {"ocrOnlineLanguages", languages},
            {"ocrOnlineCredentials", credentials},
            {"onlineConfigured", onlineReady},
            {"screenshotAvailable", online ? onlineReady : ocrState_ == capture::OcrStatus::Ready},
            {"scanAvailable", ocrState_ == capture::OcrStatus::Ready},
            {"ocrOnlineHost", provider != nullptr ? onlineEndpoint(*provider).host() : QString{}},
            {"uploadPhase", uploadPhase_},
            {"uploadCanRetry", uploadCanRetry_},
            {"localAvailability", tokenFor(ocrState_)},
            {"screenshotAvailability", online ? (onlineReady ? QStringLiteral("ready") : QStringLiteral("online-unconfigured")) : tokenFor(ocrState_)},
            {"scanAvailability", tokenFor(ocrState_)},
            {"actualLocalEngine", localInspection_.engine},
            {"localEngineReason", localInspection_.reason},
            {"ocrTestState", ocrTestState_},
            {"ocrTestEngine", ocrTestEngine_},
            {"ocrTestLanguage", ocrTestLanguage_},
            {"ocrTestMs", ocrTestMs_},
            {"tesseractExecutable", readOf("tesseractExecutable")},
            {"tesseractDataDirectory", readOf("tesseractDataDirectory")},
            {"rapidOcrExecutable", readOf("rapidOcrExecutable")},
            {"rapidOcrModelsDirectory", readOf("rapidOcrModelsDirectory")},
            {"rapidOcrModelsAscii",
             readOf("rapidOcrModelsDirectory").toString().isEmpty() or capture::rapidOcrPathIsAscii(readOf("rapidOcrModelsDirectory").toString())},
            {"rapidOcrDetectState", rapidOcrDetectState_},
            {"rapidOcrDetectMs", rapidOcrDetectMs_},
            // What an empty setting resolves to, for the surface that shows the reader where OCR
            // will look. Resolved through the engine's own function so the answer on screen cannot
            // drift from the path the process is actually started with.
            {"resolvedTesseractExecutable", resolved.executable},
            {"resolvedTesseractDataDirectory", resolved.dataDirectory},
            {"scanWhitelist", readOf("scanWhitelist")}};
}

QString CaptureDuty::ocrStatus() const
{
    if (jobContext_ and jobContext_->probe) return tokenOf(CaptureRefusal::Checking);
    // Ready is the status that is not a reason a capture cannot start, so it is the one string
    // here that has no CaptureRefusal behind it: the settings page reads it as "fine".
    const auto refusal = refusalOf(ocrState_);
    return refusal ? tokenOf(*refusal) : QStringLiteral("ready");
}

void CaptureDuty::migrateOcrSettings()
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

void CaptureDuty::invalidateScan()
{
    ++generation_;
    scanContext_.clear();
    scan_->reset();
}

bool CaptureDuty::setSetting(std::string_view const key, QVariant const& value)
{
    // One entry per row the settings table marks Signal::Capture, so the facade's generic write has
    // somewhere to land and the wiring below stays the duty's own business.
    if (key == "ocrCapture") {
        setOcrCapture(value.toBool());
        return true;
    }
    if (key == "autoScan") {
        setAutoScan(value.toBool());
        return true;
    }
    if (key == "minimumWordLength") {
        setMinimumWordLength(value.toInt());
        return true;
    }
    if (key == "scanWhitelist")
        return setScanWhitelist(value.toString());
    if (key == "tesseractExecutable") {
        setTesseractExecutable(value.toString());
        return true;
    }
    if (key == "tesseractDataDirectory") {
        setTesseractDataDirectory(value.toString());
        return true;
    }
    if (key == "rapidOcrExecutable") {
        setRapidOcrExecutable(value.toString());
        return true;
    }
    if (key == "rapidOcrModelsDirectory") {
        setRapidOcrModelsDirectory(value.toString());
        return true;
    }
    if (key == "rapidOcrTrust") {
        if (not settings::write(storage_.section(), *core::settings::find("rapidOcrTrust"), value.toString())) return false;
        rapidOcrDetectState_ = QStringLiteral("idle");
        rapidOcrDetectMs_    = 0;
        rebuildOcr();
        return true;
    }
    if (key == "ocrLocalEngine") {
        if (not capture::parseLocalEngine(value.toString().toStdString())) return false;
        setOcrLocalEngine(value.toString());
        return true;
    }
    if (key == "ocrLocalLanguage") {
        if (not capture::parseOcrLanguage(value.toString().toStdString())) return false;
        setOcrLocalLanguage(value.toString());
        return true;
    }
    if (key == "ocrMode") {
        if (value.toString() != QLatin1String("local") and value.toString() != QLatin1String("online")) return false;
        setOcrMode(value.toString());
        return true;
    }
    if (key == "ocrOnlineProvider") {
        auto const text = value.toString().trimmed();
        if (not text.isEmpty() and ocr::findOcrProvider(text) == nullptr) return false;
        setOcrOnlineProvider(text);
        return true;
    }
    if (key == "ocrOnlineEndpoint") {
        auto const text = value.toString().trimmed();
        if (not text.isEmpty() and ocr::validateOcrEndpoint(QUrl{text})) return false;
        setOcrOnlineEndpoint(text);
        return true;
    }
    if (key == "ocrOnlineLanguage") {
        if (not onlineLanguageSupported(value.toString())) return false;
        setOcrOnlineLanguage(value.toString());
        return true;
    }
    if (key == "ocrConfigVersion") {
        if (not settings::write(storage_.section(), *core::settings::find("ocrConfigVersion"), value)) return false;
        emit settingsChanged();
        return true;
    }
    LENS_WARN("'{}' is not a capture setting; nothing was written", std::string{key});
    return false;
}

void CaptureDuty::setOcrCapture(bool const on)
{
    if (not settings::write(storage_.section(), *core::settings::find("ocrCapture"), on))
        return;
    ++screenshotGeneration_;
    emit settingsChanged();
}

void CaptureDuty::setAutoScan(bool const on)
{
    if (not settings::write(storage_.section(), *core::settings::find("autoScan"), on))
        return;
    invalidateScan();
    emit settingsChanged();
}

void CaptureDuty::setMinimumWordLength(int const length)
{
    if (not settings::write(storage_.section(), *core::settings::find("minimumWordLength"), length))
        return;
    invalidateScan();
    emit settingsChanged();
}

bool CaptureDuty::setScanWhitelist(QString processes)
{
    if (processes.size() > 4096) return false;
    auto const names = processes.split(QRegularExpression{QStringLiteral("[;,\\s]+")}, Qt::SkipEmptyParts);
    if (names.size() > 32) return false;
    static QRegularExpression const executable{QStringLiteral("^[A-Za-z0-9_.-]+\\.exe$"), QRegularExpression::CaseInsensitiveOption};
    QStringList                     normalized;
    std::vector<std::string>        whitelist;
    for (auto const& name : names) {
        if (name.size() > 128 or not executable.match(name).hasMatch()) return false;
        if (normalized.contains(name.toLower())) continue;
        normalized.append(name.toLower());
        whitelist.push_back(name.toLower().toStdString());
    }
    if (not settings::write(storage_.section(), *core::settings::find("scanWhitelist"), normalized.join(';')))
        return false;
    invalidateScan();
    scan_->setWhitelist(std::move(whitelist));
    emit settingsChanged();
    return true;
}

void CaptureDuty::setTesseractExecutable(QString executable)
{
    const QString value = executable.trimmed();
    auto          keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("tesseractExecutable")).toString()) return;
    settings::write(keys, *core::settings::find("tesseractExecutable"), value);
    rebuildOcr();
}

void CaptureDuty::setTesseractDataDirectory(QString directory)
{
    const QString value = directory.trimmed();
    auto          keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("tesseractDataDirectory")).toString()) return;
    settings::write(keys, *core::settings::find("tesseractDataDirectory"), value);
    rebuildOcr();
}

void CaptureDuty::setRapidOcrExecutable(QString executable)
{
    auto const value = executable.trimmed();
    auto       keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("rapidOcrExecutable")).toString()) return;
    if (not settings::write(keys, *core::settings::find("rapidOcrExecutable"), value)) return;
    settings::write(keys, *core::settings::find("rapidOcrTrust"), QString{});
    rapidOcrDetectState_ = QStringLiteral("idle");
    rapidOcrDetectMs_    = 0;
    rebuildOcr();
}

void CaptureDuty::setRapidOcrModelsDirectory(QString directory)
{
    auto const value = directory.trimmed();
    auto       keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("rapidOcrModelsDirectory")).toString()) return;
    if (not settings::write(keys, *core::settings::find("rapidOcrModelsDirectory"), value)) return;
    settings::write(keys, *core::settings::find("rapidOcrTrust"), QString{});
    rapidOcrDetectState_ = QStringLiteral("idle");
    rapidOcrDetectMs_    = 0;
    rebuildOcr();
}

void CaptureDuty::setOcrLocalEngine(QString engine)
{
    auto const value = engine.trimmed();
    if (not capture::parseLocalEngine(value.toStdString())) return;
    auto keys = storage_.section();
    if (value == settings::read(keys, *core::settings::find("ocrLocalEngine")).toString()) return;
    if (not settings::write(keys, *core::settings::find("ocrLocalEngine"), value)) return;
    rapidOcrDetectState_ = QStringLiteral("idle");
    rapidOcrDetectMs_    = 0;
    rebuildOcr();
}

void CaptureDuty::setOcrLocalLanguage(QString language)
{
    auto const value = language.trimmed();
    if (not capture::parseOcrLanguage(value.toStdString())) return;
    auto keys = storage_.section();
    if (value == settings::read(keys, *core::settings::find("ocrLocalLanguage")).toString()) return;
    if (not settings::write(keys, *core::settings::find("ocrLocalLanguage"), value)) return;
    if (settings::read(keys, *core::settings::find("ocrLocalEngine")).toString() == QLatin1String("rapidocr")) {
        settings::write(keys, *core::settings::find("rapidOcrTrust"), QString{});
        rapidOcrDetectState_ = QStringLiteral("idle");
        rapidOcrDetectMs_    = 0;
    }
    rebuildOcr();
}

void CaptureDuty::setOcrMode(QString mode)
{
    auto const value = mode.trimmed();
    if (value != QLatin1String("local") and value != QLatin1String("online")) return;
    auto keys = storage_.section();
    if (value == settings::read(keys, *core::settings::find("ocrMode")).toString()) return;
    if (not settings::write(keys, *core::settings::find("ocrMode"), value)) return;
    // A screenshot already in flight was frozen under the old mode and must not land in the new one.
    ++screenshotGeneration_;
    clearUpload(QStringLiteral("configuration-changed"), true);
    emit settingsChanged();
}

void CaptureDuty::setOcrOnlineProvider(QString provider)
{
    auto const value = provider.trimmed();
    if (not value.isEmpty() and ocr::findOcrProvider(value) == nullptr) return;
    auto       keys     = storage_.section();
    auto const previous = settings::read(keys, *core::settings::find("ocrOnlineProvider")).toString();
    if (value == previous) return;
    if (not settings::write(keys, *core::settings::find("ocrOnlineProvider"), value)) return;
    // The address and the credentials were chosen for the service that just went away; carrying
    // them over would point one service's key at another service's protocol.
    settings::write(keys, *core::settings::find("ocrOnlineEndpoint"), QString{});
    for (auto const* const slot : {"apiKey", "secretKey"})
        keys.set<std::string>(credentialSettingKey(QLatin1String(slot)).toStdString(), {});
    clearOnlineTokens();
    invalidateOnlineConfiguration();
    emit settingsChanged();
}

void CaptureDuty::setOcrOnlineEndpoint(QString endpoint)
{
    auto const value = endpoint.trimmed();
    if (not value.isEmpty() and ocr::validateOcrEndpoint(QUrl{value})) return;
    auto       keys     = storage_.section();
    auto const previous = settings::read(keys, *core::settings::find("ocrOnlineEndpoint")).toString();
    if (value == previous) return;
    if (not settings::write(keys, *core::settings::find("ocrOnlineEndpoint"), value)) return;
    // A credential entered for one address is never sent to another; the reader binds it again.
    // Clearing only the credentials is enough: the address stays visible for the re-entry.
    if (not previous.isEmpty() and value != previous) {
        for (auto const* const slot : {"apiKey", "secretKey"})
            keys.set<std::string>(credentialSettingKey(QLatin1String(slot)).toStdString(), {});
        clearOnlineTokens();
    }
    invalidateOnlineConfiguration();
    emit settingsChanged();
}

void CaptureDuty::setOcrOnlineLanguage(QString language)
{
    auto const value = language.trimmed();
    if (not onlineLanguageSupported(value)) return;
    auto keys = storage_.section();
    if (value == settings::read(keys, *core::settings::find("ocrOnlineLanguage")).toString()) return;
    if (not settings::write(keys, *core::settings::find("ocrOnlineLanguage"), value)) return;
    invalidateOnlineConfiguration();
    emit settingsChanged();
}

void CaptureDuty::setOcrCredential(QString slot, QString value)
{
    auto const key = credentialSettingKey(slot.trimmed());
    if (key.isEmpty()) return;
    auto       keys    = storage_.section();
    auto const trimmed = value.trimmed().left(4096);
    if (trimmed.toStdString() == keys.value<std::string>(key.toStdString(), {})) return;
    keys.set<std::string>(key.toStdString(), trimmed.toStdString());
    clearOnlineTokens();
    invalidateOnlineConfiguration();
    LENS_INFO("OCR credential '{}' {}", slot.toStdString(), trimmed.isEmpty() ? "cleared" : "updated");
    emit settingsChanged();
}

bool CaptureDuty::onlineConfigured() const
{
    auto const* const provider = onlineProvider();
    if (provider == nullptr) return false;
    if (ocr::validateOcrEndpoint(onlineEndpoint(*provider))) return false;
    auto const keys = storage_.section();
    if (not onlineLanguageSupported(settings::read(keys, *core::settings::find("ocrOnlineLanguage")).toString())) return false;
    for (auto const& slot : provider->credentials) {
        auto const key = credentialSettingKey(slot);
        if (key.isEmpty() or keys.value<std::string>(key.toStdString(), {}).empty()) return false;
    }
    return true;
}

bool CaptureDuty::onlineLanguageSupported(QString const& language) const
{
    auto const* const provider = onlineProvider();
    if (provider != nullptr) return provider->languages.contains(language);
    return language == QLatin1String("eng") or language == QLatin1String("auto");
}

ocr::OnlineOcrProvider const* CaptureDuty::onlineProvider() const
{
    auto const keys = storage_.section();
    return ocr::findOcrProvider(settings::read(keys, *core::settings::find("ocrOnlineProvider")).toString());
}

QUrl CaptureDuty::onlineEndpoint(ocr::OnlineOcrProvider const& provider) const
{
    auto const stored = settings::read(storage_.section(), *core::settings::find("ocrOnlineEndpoint")).toString().trimmed();
    return stored.isEmpty() ? provider.endpoint : QUrl{stored};
}

ocr::OnlineOcrConfig CaptureDuty::onlineConfig() const
{
    auto const* const provider = onlineProvider();
    if (provider == nullptr) return {};
    auto const keys = storage_.section();
    return {.provider  = *provider,
            .endpoint  = onlineEndpoint(*provider),
            .apiKey    = onlineKey(),
            .secretKey = onlineSecret(),
            .language  = settings::read(keys, *core::settings::find("ocrOnlineLanguage")).toString()};
}

QString CaptureDuty::onlineKey() const
{
    return QString::fromStdString(storage_.section().value<std::string>("OCR-API-KEY", {}));
}

QString CaptureDuty::onlineSecret() const
{
    return QString::fromStdString(storage_.section().value<std::string>("OCR-SECRET-KEY", {}));
}

void CaptureDuty::clearOnlineTokens()
{
    if (onlineTokens_) onlineTokens_->clear();
}

void CaptureDuty::invalidateOnlineConfiguration()
{
    if (jobContext_ and jobContext_->test) {
        executor_->cancel();
        jobContext_.reset();
        ocrTestState_ = QStringLiteral("idle");
    }
    clearUpload(QStringLiteral("configuration-changed"), true);
}

void CaptureDuty::rebuildOcr()
{
    // A probe or recognition in flight belongs to the engine that just went away, and its answer
    // would land on the new one's state. Cancel it before the old engine is released.
    executor_->cancel();
    jobContext_.reset();
    auto const local = storedLocalConfig(storage_);
    ocr_             = capture::makeLocalOcr(local);
    // Ordinary readiness is a file/resource inspection; no child process is started until the
    // reader asks for a real test or captures a region. RapidOCR's digest is verified here, at
    // startup and after a path or language change, rather than on every settings projection.
    localInspection_ = capture::inspectLocalOcr(local, true);
    ocrState_        = localInspection_.status;
    ocrTestState_    = QStringLiteral("idle");
    ocrTestEngine_.clear();
    ocrTestLanguage_.clear();
    ocrTestMs_ = 0;
    emit settingsChanged();
}

void CaptureDuty::restoreDefaults()
{
    // The rows the table marks Signal::Capture, in the table's order, through the same write path a
    // choice takes -- so a restore and a fresh install read one list rather than two.
    for (auto const& row : core::settings::kRows)
        if (row.signal == core::settings::Signal::Capture and row.restorable)
            setSetting(row.key, settings::defaultValue(row));
}

void CaptureDuty::setScanPaused(bool const paused)
{
    if (scanPaused_ == paused) return;
    scanPaused_ = paused;
    invalidateScan();
}

void CaptureDuty::setBudgetPaused(bool const paused)
{
    if (budgetPaused_ == paused) return;
    budgetPaused_ = paused;
    ++screenshotGeneration_;
    invalidateScan();
}

void CaptureDuty::probeOcr()
{
    if (jobContext_ or executor_->busy()) return;
    auto const id      = ++nextRequestId_;
    auto       engine  = ocr_;
    auto const started = executor_->start(id, [engine](std::shared_ptr<std::atomic_bool> cancel) {
        capture::OcrResult result;
        try {
            result.status = engine->probe(std::move(cancel));
        } catch (...) {
            result.status = capture::OcrStatus::Failed;
        }
        return result;
    });
    if (not started) return;
    jobContext_ = OcrJob{.requestId = id, .probe = true};
    emit settingsChanged();
}

void CaptureDuty::testLocalOcr()
{
    if (jobContext_ or executor_->busy()) return;
    auto const          local = storedLocalConfig(storage_);
    auto const          id    = ++nextRequestId_;
    capture::OcrRequest request{.requestId = id,
                                .source    = capture::OcrSource::LocalTest,
                                .image     = localTestImage(local.language),
                                .language  = local.language,
                                .deadline  = std::chrono::milliseconds{10000}};
    auto                engine  = ocr_;
    auto const          started = executor_->start(id, [engine, request = std::move(request)](std::shared_ptr<std::atomic_bool> cancel) {
        try {
            return engine->recognize(request, std::move(cancel));
        } catch (...) {
            return capture::OcrResult{.status = capture::OcrStatus::Failed, .requestId = request.requestId};
        }
    });
    if (not started) return;
    jobContext_   = OcrJob{.requestId = id, .test = true};
    ocrTestState_ = QStringLiteral("checking");
    emit settingsChanged();
}

void CaptureDuty::detectRapidOcr()
{
    if (jobContext_ or executor_->busy()) return;
    auto config           = storedRapidOcrConfig(storage_);
    config.requireTrust   = false;
    auto const inspection = capture::inspectRapidOcr(config);
    if (inspection.status != capture::OcrStatus::Ready) {
        rapidOcrDetectState_ = statusToken(inspection.status);
        rapidOcrDetectMs_    = 0;
        emit settingsChanged();
        return;
    }

    auto const id      = ++nextRequestId_;
    auto       engine  = std::shared_ptr<capture::OcrEngine>(capture::makeRapidOcr(config));
    auto const request = capture::OcrRequest{.requestId = id,
                                             .source    = capture::OcrSource::LocalTest,
                                             .image     = localTestImage(capture::OcrLanguage::English),
                                             .language  = config.language,
                                             .deadline  = std::chrono::milliseconds{10000}};
    auto const started = executor_->start(id, [engine, request](std::shared_ptr<std::atomic_bool> cancel) {
        try {
            return engine->recognize(request, std::move(cancel));
        } catch (...) {
            return capture::OcrResult{.status = capture::OcrStatus::Failed, .requestId = request.requestId};
        }
    });
    if (not started) return;
    jobContext_          = OcrJob{.requestId = id, .detectRapid = true};
    rapidOcrDetectState_ = QStringLiteral("checking");
    rapidOcrDetectMs_    = 0;
    emit settingsChanged();
}

void CaptureDuty::beginOnlineUpload(QImage image, QPoint const anchor)
{
    if (not onlineConfigured()) {
        emit screenshotFailed(CaptureRefusal::OnlineNotConfigured);
        return;
    }
    clearUpload(QStringLiteral("replaced"), false);
    // A confirmation is not a pause the scan may resume from: the frozen crop and the scan's
    // previous stable text must not revive each other.
    invalidateScan();

    PendingUpload pending;
    pending.requestId        = ++nextRequestId_;
    pending.image            = std::move(image);
    pending.anchor           = anchor;
    pending.config           = onlineConfig();
    pending.absoluteDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{120};
    upload_                  = std::move(pending);
    uploadPhase_             = QStringLiteral("awaiting-consent");
    uploadCanRetry_          = false;
    consentTimer_.start(60000);
    uploadTimer_.start(120000);
    emit uploadPreviewReady(static_cast<qulonglong>(upload_->requestId), upload_->image);
    emit uploadConsentRequested(QVariantMap{{"requestId", static_cast<qulonglong>(upload_->requestId)},
                                            {"host", upload_->config.endpoint.host()},
                                            {"provider", upload_->config.provider.label},
                                            {"language", upload_->config.language},
                                            {"expiresInMs", 60000},
                                            {"previewUrl", QStringLiteral("image://ocr-consent/%1").arg(upload_->requestId)}});
    emit settingsChanged();
}

void CaptureDuty::confirmOnlineUpload(qulonglong const requestId)
{
    if (not upload_ or upload_->requestId != requestId) return;
    auto const now = std::chrono::steady_clock::now();
    if (now >= upload_->absoluteDeadline) {
        clearUpload(QStringLiteral("expired"), true);
        return;
    }
    if (upload_->attempts >= 2) {
        clearUpload(QStringLiteral("retry-limit"), true);
        return;
    }
    std::shared_ptr<ocr::OnlineOcrClient> client{
        ocr::makeOnlineOcrClient(upload_->config, onlineTransport_, onlineTokens_)};
    if (not client) {
        clearUpload(QStringLiteral("configuration-changed"), true);
        return;
    }

    auto const          remaining = std::chrono::duration_cast<std::chrono::milliseconds>(upload_->absoluteDeadline - now);
    auto const          budget    = std::min(std::chrono::milliseconds{30000}, remaining);
    auto const          id        = upload_->requestId;
    capture::OcrRequest request{.requestId = id,
                                .source    = capture::OcrSource::Screenshot,
                                .image     = upload_->image,
                                .language  = std::nullopt,
                                .layout    = capture::OcrLayoutHint::Unknown,
                                .deadline  = budget};
    ++upload_->attempts;
    uploadPhase_    = QStringLiteral("uploading");
    uploadCanRetry_ = false;
    consentTimer_.stop();

    auto const started = executor_->start(id, [client = std::move(client), request = std::move(request)](std::shared_ptr<std::atomic_bool> cancel) mutable {
        try {
            return client->recognize(request, std::move(cancel));
        } catch (...) {
            return capture::OcrResult{.status = capture::OcrStatus::Failed, .requestId = request.requestId};
        }
    });
    if (not started) {
        uploadPhase_    = QStringLiteral("awaiting-retry");
        uploadCanRetry_ = true;
        consentTimer_.start(30000);
        emit uploadFinished(QVariantMap{{"status", QStringLiteral("busy")},
                                        {"requestId", static_cast<qulonglong>(id)},
                                        {"canRetry", true}});
        emit settingsChanged();
        return;
    }
    jobContext_ = OcrJob{.anchor = upload_->anchor, .generation = screenshotGeneration_, .requestId = id, .online = true};
    emit settingsChanged();
}

void CaptureDuty::cancelOnlineUpload()
{
    clearUpload(QStringLiteral("cancelled"), true);
}

void CaptureDuty::onConsentTimeout()
{
    if (not upload_) return;
    if (uploadPhase_ == QLatin1String("awaiting-consent"))
        clearUpload(QStringLiteral("consent-expired"), true);
    else if (uploadPhase_ == QLatin1String("awaiting-retry"))
        clearUpload(QStringLiteral("retry-expired"), true);
}

void CaptureDuty::onUploadTimeout()
{
    if (not upload_) return;
    if (jobContext_ and jobContext_->online) executor_->cancel();
    clearUpload(QStringLiteral("expired"), true);
}

void CaptureDuty::finishOnline(capture::OcrResult const& result)
{
    if (not upload_) return;
    auto const requestId = upload_->requestId;
    auto const anchor    = upload_->anchor;
    auto const attempts  = upload_->attempts;
    auto const absolute  = upload_->absoluteDeadline;
    auto const refusal   = refusalOf(result.status);
    auto const token     = refusal ? tokenOf(*refusal) : QStringLiteral("ready");

    if (result.status == capture::OcrStatus::Ready) {
        auto const text = result.text;
        clearUpload(QStringLiteral("ready"), false);
        emit uploadFinished(QVariantMap{{"status", token},
                                        {"requestId", static_cast<qulonglong>(requestId)},
                                        {"engine", result.actualEngine},
                                        {"canRetry", false}});
        classifyText(text, anchor, true);
        return;
    }
    if (result.status == capture::OcrStatus::Cancelled) {
        clearUpload(QStringLiteral("cancelled"), true);
        return;
    }
    bool const retryable = (result.status == capture::OcrStatus::TimedOut or
                            result.status == capture::OcrStatus::TransportFailed) and
                           attempts < 2 and std::chrono::steady_clock::now() < absolute and
                           result.uploadState != capture::OcrUploadState::NotUploaded;
    if (retryable) {
        uploadPhase_    = QStringLiteral("awaiting-retry");
        uploadCanRetry_ = true;
        consentTimer_.start(30000);
        emit uploadFinished(QVariantMap{{"status", token},
                                        {"requestId", static_cast<qulonglong>(requestId)},
                                        {"canRetry", true}});
        emit settingsChanged();
        return;
    }
    clearUpload(token, true);
}

void CaptureDuty::clearUpload(QString outcome, bool const notify)
{
    auto const requestId = upload_ ? upload_->requestId : 0;
    auto const hadUpload = upload_.has_value();
    consentTimer_.stop();
    uploadTimer_.stop();
    if (jobContext_ and jobContext_->online) {
        executor_->cancel();
        jobContext_.reset();
    }
    upload_.reset();
    uploadPhase_    = QStringLiteral("idle");
    uploadCanRetry_ = false;
    if (hadUpload) invalidateScan();
    if (notify and hadUpload)
        emit uploadFinished(QVariantMap{{"status", std::move(outcome)},
                                        {"requestId", static_cast<qulonglong>(requestId)},
                                        {"canRetry", false}});
    if (hadUpload) emit settingsChanged();
}

void CaptureDuty::testOnlineOcr()
{
    if (jobContext_ or upload_ or executor_->busy()) return;
    if (budgetPaused_) {
        ocrTestState_ = QStringLiteral("budget-paused");
        emit settingsChanged();
        return;
    }
    if (not onlineConfigured()) {
        ocrTestState_ = QStringLiteral("online-unconfigured");
        emit settingsChanged();
        return;
    }
    auto client = std::shared_ptr<ocr::OnlineOcrClient>{
        ocr::makeOnlineOcrClient(onlineConfig(), onlineTransport_, onlineTokens_)};
    if (not client) {
        ocrTestState_ = QStringLiteral("online-unconfigured");
        emit settingsChanged();
        return;
    }
    auto const          id = ++nextRequestId_;
    capture::OcrRequest request{.requestId = id,
                                .source    = capture::OcrSource::OnlineTest,
                                .image     = localTestImage(capture::OcrLanguage::English),
                                .deadline  = std::chrono::milliseconds{30000}};
    ocrTestState_      = QStringLiteral("checking");
    auto const started = executor_->start(id, [client = std::move(client), request = std::move(request)](std::shared_ptr<std::atomic_bool> cancel) mutable {
        try {
            return client->recognize(request, std::move(cancel));
        } catch (...) {
            return capture::OcrResult{.status = capture::OcrStatus::Failed, .requestId = request.requestId};
        }
    });
    if (not started) {
        ocrTestState_ = QStringLiteral("busy");
        emit settingsChanged();
        return;
    }
    jobContext_ = OcrJob{.requestId = id, .test = true};
    emit settingsChanged();
}

bool CaptureDuty::launchOcr(QImage image, QPoint const anchor, QString context)
{
    auto const          generation = context.isEmpty() ? screenshotGeneration_ : generation_;
    auto const          id         = ++nextRequestId_;
    capture::OcrRequest request{.requestId = id,
                                .source    = context.isEmpty() ? capture::OcrSource::Screenshot : capture::OcrSource::AutoScan,
                                .image     = std::move(image),
                                .deadline  = std::chrono::milliseconds{10000}};
    auto                engine  = ocr_;
    auto const          started = executor_->start(id, [engine, request = std::move(request)](std::shared_ptr<std::atomic_bool> cancel) {
        try {
            return engine->recognize(request, std::move(cancel));
        } catch (...) {
            return capture::OcrResult{.status = capture::OcrStatus::Failed, .requestId = request.requestId};
        }
    });
    if (not started) return false;
    jobContext_ = OcrJob{.anchor = anchor, .context = std::move(context), .generation = generation, .requestId = id};
    return true;
}

std::optional<CaptureRefusal> CaptureDuty::captureRefusalReason() const
{
    auto const online = settings::read(storage_.section(), *core::settings::find("ocrMode")).toString() == QLatin1String("online");
    return gateRefusal({.budgetPaused     = budgetPaused_,
                        .probing          = jobContext_ and jobContext_->probe,
                        .busy             = jobContext_.has_value(),
                        .screenshotOn     = settings().value("ocrCapture").toBool(),
                        .online           = online,
                        .onlineConfigured = online and onlineConfigured(),
                        .runtime          = ocrState_});
}

bool CaptureDuty::captureScreenshot(QImage image, QPoint const anchor)
{
    if (captureRefusalReason()) return false;
    auto const online = settings::read(storage_.section(), *core::settings::find("ocrMode")).toString() == QLatin1String("online");
    if (online) {
        beginOnlineUpload(std::move(image), anchor);
        return true;
    }
    return launchOcr(std::move(image), anchor, {});
}

std::optional<CaptureRefusal> CaptureDuty::captureRegion(QRect const region)
{
    if (const auto refusal = captureRefusalReason()) return refusal;
    auto* const screen = QGuiApplication::screenAt(region.center());
    if (screen == nullptr or region.isEmpty() or not screen->geometry().contains(region)) return CaptureRefusal::OutsideScreen;
    if (region.width() * screen->devicePixelRatio() > 8192 or region.height() * screen->devicePixelRatio() > 8192) return CaptureRefusal::RegionTooLarge;
    auto const nativeOrigin = capture::physicalScreenOrigin(monitorOf(screen));
    if (not nativeOrigin) return CaptureRefusal::OutsideScreen;
    auto const local  = region.translated(-screen->geometry().topLeft());
    auto const image  = screen->grabWindow(0, local.x(), local.y(), local.width(), local.height()).toImage();
    auto const anchor = *nativeOrigin + local.topLeft() * screen->devicePixelRatio();
    // The gate was read a moment ago; a job that started since is the only way this refuses now,
    // and it is the same answer the next press will get.
    return captureScreenshot(image, anchor) ? std::nullopt : std::optional<CaptureRefusal>{CaptureRefusal::Busy};
}

void CaptureDuty::scanTick()
{
    if (budgetPaused_ or scanPaused_ or not settings().value("autoScan").toBool() or ocrState_ != capture::OcrStatus::Ready) return;
    auto const foreground = capture::foregroundWindow();
    if (not foreground) {
        invalidateScan();
        return;
    }
    capture::ScanFrame frame{.context     = foreground->context.toStdString(),
                             .processName = foreground->processName.toStdString(),
                             .ownProcess  = foreground->ownProcess};
    if (not scan_->allows(frame)) {
        invalidateScan();
        return;
    }
    if (scanContext_ != foreground->context) {
        invalidateScan();
        scanContext_ = foreground->context;
    }
    if (jobContext_ or upload_) return;
    QScreen* screen = nullptr;
    for (auto* const candidate : QGuiApplication::screens())
        if (monitorOf(candidate) == foreground->monitor) {
            screen = candidate;
            break;
        }
    if (screen == nullptr) return;
    auto const image = screen->grabWindow(foreground->window).toImage();
    if (image.isNull()) {
        invalidateScan();
        return;
    }
    frame.pixels   = capture::pixelFingerprint(image);
    auto const now = std::chrono::steady_clock::now();
    scanAnchor_    = foreground->anchor;
    if (scan_->needsOcr(frame, now))
        launchOcr(image, scanAnchor_, scanContext_);
    else
        publishBatch(now, scanAnchor_);
}

void CaptureDuty::onOcrFinished(std::uint64_t const requestId, capture::OcrResult result)
{
    if (not jobContext_ or jobContext_->requestId != requestId) return;
    auto const context = *jobContext_;
    jobContext_.reset();
    if (context.online) {
        finishOnline(result);
        return;
    }
    if (context.detectRapid) {
        auto const trusted = result.status == capture::OcrStatus::Ready and
                             result.text.contains(QStringLiteral("Lens"), Qt::CaseInsensitive);
        auto keys = storage_.section();
        if (trusted) {
            auto const digest = capture::rapidOcrTrustDigest(storedRapidOcrConfig(storage_));
            if (not digest.isEmpty() and settings::write(keys, *core::settings::find("rapidOcrTrust"), digest)) {
                rapidOcrDetectState_ = QStringLiteral("ready");
                rapidOcrDetectMs_    = result.timing.totalMs;
                rebuildOcr();
                return;
            }
        }
        settings::write(keys, *core::settings::find("rapidOcrTrust"), QString{});
        rapidOcrDetectState_ = result.status == capture::OcrStatus::Ready ? QStringLiteral("failed")
                                                                          : statusToken(result.status);
        rapidOcrDetectMs_    = result.timing.totalMs;
        rebuildOcr();
        return;
    }
    if (context.test) {
        auto const refusal = refusalOf(result.status);
        ocrTestState_      = refusal ? tokenOf(*refusal) : QStringLiteral("ready");
        ocrTestEngine_     = result.actualEngine;
        ocrTestLanguage_   = result.languageUsed;
        ocrTestMs_         = result.timing.totalMs;
        emit settingsChanged();
        return;
    }
    if (context.probe) {
        ocrState_ = result.status;
        emit settingsChanged();
        if (result.status != capture::OcrStatus::Ready)
            LENS_WARN("OCR probe failed (status {})", ocrStatus().toStdString());
        return;
    }
    std::optional<QString> foregroundContext;
    if (not context.context.isEmpty()) {
        if (auto const foreground = capture::foregroundWindow()) foregroundContext = foreground->context;
    }
    auto const fate = capture::ocrResultFate({.jobContext           = context.context,
                                              .jobGeneration        = context.generation,
                                              .scanGeneration       = generation_,
                                              .screenshotGeneration = screenshotGeneration_,
                                              .foregroundContext    = std::move(foregroundContext),
                                              .budgetPaused         = budgetPaused_,
                                              .scanPaused           = scanPaused_,
                                              .autoScan             = settings().value("autoScan").toBool()});
    if (fate == capture::OcrResultFate::Superseded) return;
    if (fate == capture::OcrResultFate::Withdrawn) {
        invalidateScan();
        return;
    }
    if (result.status == capture::OcrStatus::Cancelled) return;
    if (result.status != capture::OcrStatus::Ready) {
        // The status this result carried, never ocrStatus(): that reads ocrState_, which the gate
        // requires to be Ready before a framed capture starts at all -- so ocrStatus() would report
        // the runtime as fine about a region that simply had no text in it.
        const auto refusal = refusalOf(result.status).value_or(CaptureRefusal::Failed);
        if (context.context.isEmpty()) {
            LENS_INFO("OCR of the framed region produced no text (status {})", refusalKey(refusal));
            emit screenshotFailed(refusal);
            return;
        }
        scan_->reset();
        if (capture::isRuntimeFault(result.status)) {
            ocrState_ = result.status;
            emit settingsChanged();
            LENS_WARN("OCR worker failed (status {})", ocrStatus().toStdString());
        }
        return;
    }
    if (context.context.isEmpty()) {
        // Only the screenshot path reaches here with no scan context: a selection brings its own
        // text rather than going through OCR at all.
        classifyText(result.text, context.anchor, true);
        return;
    }
    auto const unsupported = capture::unsupportedEntityCount(result.text.toStdString());
    if (unsupported > 0) {
        LENS_INFO("automatic scan retained {} unsupported entity candidate(s); entity channel is not enabled", unsupported);
        emit scanEntitiesUnsupported(static_cast<int>(unsupported));
    }
    auto const now = std::chrono::steady_clock::now();
    scan_->acceptText(result.text.toStdString(),
                      {.minimumLength = settings().value("minimumWordLength").toInt(),
                       .knownLemmas   = storage_.knownStore().known(),
                       .minimumRank   = minFreqRank()},
                      now);
    publishBatch(now, context.anchor);
}

void CaptureDuty::publishBatch(capture::ScanTime const now, QPoint const anchor)
{
    const auto   batch = scan_->takeBatch(now);
    QVariantList words;
    for (auto const& word : batch)
        words.append(QVariantMap{{"surface", QString::fromStdString(word.surface)},
                                 {"lemma", QString::fromStdString(word.lemma)}});
    if (not words.isEmpty()) emit scanCandidatesReady(words, anchor);
}

void CaptureDuty::onSelectionReleased(QPoint const anchor)
{
    if (budgetPaused_) return;
    if (not settings::read(storage_.section(), *core::settings::find("selectionCapture")).toBool()) {
        LENS_TRACE("selection at ({}, {}) ignored: selection capture is off", anchor.x(), anchor.y());
        return;
    }
    beginSelection(anchor);
}

void CaptureDuty::beginSelection(QPoint const anchor, std::optional<QString> const text)
{
    if (text) {
        classifyText(*text, anchor);
        return;
    }
    const auto policy = clipboardPolicyFromStored(
        settings::read(storage_.section(), *core::settings::find("clipboardPolicy")).toString().toStdString());
    const auto grabbed = grabber_->grab(policy);
    if (const auto* status = std::get_if<GrabStatus>(&grabbed)) {
        LENS_DEBUG("selection at ({}, {}) produced no text (status {})", anchor.x(), anchor.y(), static_cast<int>(*status));
        return;
    }
    classifyText(std::get<QString>(grabbed), anchor);
}

void CaptureDuty::classifyText(QString text, QPoint const anchor, bool const fromScreenshot)
{
    if (text.size() > kMaxSelectionChars) text = text.left(kMaxSelectionChars) + QStringLiteral("…");
    const core::Selection selection = core::classifySelection(text.toStdString(), storage_.knownStore().known(), minFreqRank());
    PendingSelection      pending{.anchor = anchor, .text = text, .kind = selection.kind};
    if (selection.kind == core::SelectionKind::Word) {
        // Only a word narrows to a candidate: the other kinds have no lemma to pick, and the whole
        // selection is what gets answered.
        const auto& candidates = selection.candidates;
        const auto  fresh      = std::find_if(candidates.begin(), candidates.end(), [](const core::Candidate& candidate) {
            return candidate.state == core::CandidateState::New;
        });
        if (candidates.empty()) return;
        const core::Candidate& word = fresh != candidates.end() ? *fresh : candidates.front();
        pending.surface             = QString::fromStdString(word.surface);
        pending.lemma               = QString::fromStdString(word.lemma);
    }
    emit selectionReady(pending);
    if (fromScreenshot) {
        // The reader framed this region themselves, so they have already said what they want done
        // with it: translate. The bar would ask again -- and OCR exists for text that cannot be
        // selected, which is exactly the text whose reader wants the meaning rather than a menu.
        emit selectionActionRequested(QStringLiteral("translate"), pending.text);
        return;
    }
    emit selectionBarRequested(QVariantMap{{"x", anchor.x()},
                                           {"y", anchor.y()},
                                           {"kind", kindName(pending.kind)},
                                           {"text", pending.text},
                                           {"letterCount", capture::selectionLetterCount(pending.text)},
                                           {"breakdownable", pending.kind == core::SelectionKind::Sentence and llm::breakdownApplies(pending.text)},
                                           {"spellable", llm::spellingApplies(pending.text) and pending.kind != core::SelectionKind::Translation},
                                           {"grammarable", pending.kind == core::SelectionKind::Sentence and llm::grammarApplies(pending.text)}});
}

std::size_t CaptureDuty::minFreqRank() const
{
    return core::rankForLevel(storage_.knownStore().level());
}

}
