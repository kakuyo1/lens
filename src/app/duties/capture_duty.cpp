/**
 * @file capture_duty.cpp
 * @brief Selection capture and local OCR acquisition, independent of explanation presentation.
 */

#include "capture_duty.h"

#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QGuiApplication>
#include <QPainter>
#include <QRunnable>
#include <QThreadPool>
#include <QRegularExpression>
#include <QScreen>
#include <QtGui/qscreen_platform.h>

#include <algorithm>
#include <chrono>
#include <iterator>

#include "capture/capture_policy.h"
#include "capture/foreground_window.h"
#include "capture/ocr_local.h"
#include "ocr/online_ocr.h"
#include "ocr/qt_http_transport.h"
#include "core/domain/filter_core.h"
#include "core/domain/settings_table.h"
#include "app/platform/mouse_selection_hook.h"
#include "app/settings_access.h"
#include "verdict_view.h"
#include "capture/ocr_rapidocr.h"
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

/// @brief The production track: a PageTrack whose frames are the scan session's frames (docs/adr/0164).
class PageTrackScanTrack final : public ScanTrack {
public:
    PageTrackScanTrack(quintptr const window, QRect band, TrackSink deliver)
        : track_{std::make_unique<PageTrack>(window, band, std::move(deliver))}
    {}

    void setBand(QRect const band) override
    {
        track_->setBand(band);
    }

private:
    std::unique_ptr<PageTrack> track_;
};

/// @brief The monitor placement of a Chrome window, resolved the way the scan tick always has;
///        nothing when its display is unknown to Qt or its band falls off every screen.
std::optional<ScanPlacement> scanPlacementOf(capture::ForegroundWindow const& foreground)
{
    QScreen* screen = nullptr;
    for (auto* const candidate : QGuiApplication::screens())
        if (monitorOf(candidate) == foreground.monitor) {
            screen = candidate;
            break;
        }
    if (screen == nullptr) return std::nullopt;
    // The window is cut to the display it sits on: the words off that display are not on screen to be lined.
    auto const origin = capture::physicalScreenOrigin(foreground.monitor);
    if (not origin) return std::nullopt;
    auto const  display = screen->geometry();
    auto const  scale   = screen->devicePixelRatio();
    QRect const monitor{*origin, QSize{static_cast<int>(display.width() * scale), static_cast<int>(display.height() * scale)}};
    QRect const band = QRect{foreground.anchor, foreground.size}.intersected(monitor);
    if (band.isEmpty()) return std::nullopt;
    return ScanPlacement{.band = band, .screen = display, .origin = *origin, .scale = scale};
}

/// @return The payload the uploadFinished signal carries, with the engine spelled only when named.
QVariantMap uploadOutcomeMap(UploadOutcome const& outcome)
{
    QVariantMap map{{QStringLiteral("status"), outcome.status},
                    {QStringLiteral("requestId"), static_cast<qulonglong>(outcome.requestId)},
                    {QStringLiteral("canRetry"), outcome.canRetry}};
    if (not outcome.engine.isEmpty()) map.insert(QStringLiteral("engine"), outcome.engine);
    return map;
}

/// @return The document key that holds one credential slot, or empty for a slot this build has no
///         field for. Fixed strings: a slot name never chooses where a secret is written.
QString credentialSettingKey(QString const& slot)
{
    if (slot == QLatin1String("apiKey")) return QStringLiteral("OCR-API-KEY");
    if (slot == QLatin1String("secretKey")) return QStringLiteral("OCR-SECRET-KEY");
    return {};
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
    : QObject(parent), storage_(storage), hook_(hook), executor_(std::make_unique<capture::OcrExecutor>(this)),
      onlineTransport_(std::move(onlineTransport)), onlineTokens_(ocr::makeMemoryOcrTokenStore()),
      upload_(onlineTransport_, onlineTokens_),
      lifecycle_{storage_, [this] { executor_->cancel(); jobContext_.reset(); }},
      session_{storage_.knownStore(),
               [this](std::function<void()> work) { QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection); },
               [](quintptr window, QRect band, TrackSink deliver) { return std::make_unique<PageTrackScanTrack>(window, band, std::move(deliver)); },
               [this](ScanStep const& step) { routeScanStep(step); }},
      selection_{storage_.knownStore(), storage_, std::make_unique<SelectionTextGrabber>()}
{
    if (not onlineTransport_) onlineTransport_ = std::make_shared<ocr::QtHttpTransport>();
    connect(&hook_, &MouseSelectionHook::selectionReleased, this, &CaptureDuty::onSelectionReleased);
    connect(&hook_, &MouseSelectionHook::pointerPressed, this, &CaptureDuty::pointerPressed);
    connect(executor_.get(), &capture::OcrExecutor::finished, this, &CaptureDuty::onOcrFinished);
    connect(&bundleDownloader_, &capture::BundleDownloader::progressed, this, &CaptureDuty::onBundleProgressed);
    connect(&bundleDownloader_, &capture::BundleDownloader::finished, this, &CaptureDuty::onBundleFinished);
    consentTimer_.setSingleShot(true);
    uploadTimer_.setSingleShot(true);
    connect(&consentTimer_, &QTimer::timeout, this, &CaptureDuty::onConsentTimeout);
    connect(&uploadTimer_, &QTimer::timeout, this, &CaptureDuty::onUploadTimeout);
    scanPlaceFlush_.setSingleShot(true);
    connect(&scanPlaceFlush_, &QTimer::timeout, this, &CaptureDuty::onScanPlaceFlush);

    scanTimer_.setInterval(250);
    connect(&scanTimer_, &QTimer::timeout, this, &CaptureDuty::scanTick);
    scanTimer_.start();
    hoverTimer_.setInterval(100);
    connect(&hoverTimer_, &QTimer::timeout, this, &CaptureDuty::onHoverTick);
    lifecycle_.migrate();
    if (ocr) {
        lifecycle_.adopt(std::move(ocr), storedLocalConfig(storage_));
        probeOcr();
    } else {
        lifecycle_.rebuild();
    }
    lifecycle_.rebuildScan();
}

CaptureDuty::~CaptureDuty()
{
    // The track's worker delivers into this object, so it stops before anything else goes.
    session_.stopTrack();
    scanTimer_.stop();
    hoverTimer_.stop();
    consentTimer_.stop();
    uploadTimer_.stop();
    executor_->cancel();
    jobContext_.reset();
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
        for (auto const& field : provider->credentials)
            credentials.append(QVariantMap{
                {"slot", field.slot},
                {"label", field.label},
                {"present", not document.value<std::string>(credentialSettingKey(field.slot).toStdString(), {}).empty()}});
    }
    return {{"ocrCapture", readOf("ocrCapture")},
            {"autoScan", readOf("autoScan")},
            {"ocrAvailable", lifecycle_.status() == capture::OcrStatus::Ready},
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
            {"screenshotAvailable", online ? onlineReady : lifecycle_.status() == capture::OcrStatus::Ready},
            {"scanAvailable", lifecycle_.status() == capture::OcrStatus::Ready},
            {"ocrOnlineHost", provider != nullptr ? onlineEndpoint(*provider).host() : QString{}},
            {"uploadPhase", upload_.phase()},
            {"uploadCanRetry", upload_.canRetry()},
            {"localAvailability", tokenFor(lifecycle_.status())},
            {"screenshotAvailability", online ? (onlineReady ? QStringLiteral("ready") : QStringLiteral("online-unconfigured")) : tokenFor(lifecycle_.status())},
            {"scanAvailability", tokenFor(lifecycle_.status())},
            {"actualLocalEngine", lifecycle_.inspection().engine},
            {"localEngineReason", lifecycle_.inspection().reason},
            {"ocrTestState", lifecycle_.testState()},
            {"ocrTestReason", lifecycle_.testReason()},
            {"ocrTestEngine", lifecycle_.testEngine()},
            {"ocrTestLanguage", lifecycle_.testLanguage()},
            {"ocrTestMs", lifecycle_.testMs()},
            {"tesseractExecutable", readOf("tesseractExecutable")},
            {"tesseractDataDirectory", readOf("tesseractDataDirectory")},
            {"rapidOcrExecutable", readOf("rapidOcrExecutable")},
            {"rapidOcrModelsDirectory", readOf("rapidOcrModelsDirectory")},
            {"rapidOcrModelsAscii",
             readOf("rapidOcrModelsDirectory").toString().isEmpty() or capture::rapidOcrPathIsAscii(readOf("rapidOcrModelsDirectory").toString())},
            {"rapidOcrDetectState", lifecycle_.detectState()},
            {"rapidOcrDetectMs", lifecycle_.detectMs()},
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
    const auto refusal = refusalOf(lifecycle_.status());
    return refusal ? tokenOf(*refusal) : QStringLiteral("ready");
}

void CaptureDuty::invalidateScan()
{
    // The session moves its generation and drops its track and caches; the duty announces it, as it always has.
    session_.invalidate();
    clearScanLines();
}

void CaptureDuty::difficultyChanged()
{
    invalidateScan();
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
        lifecycle_.detectIdle();
        lifecycle_.rebuild();
        emit settingsChanged();
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
    for (auto const& name : names) {
        if (name.size() > 128 or not executable.match(name).hasMatch()) return false;
        if (normalized.contains(name.toLower())) continue;
        normalized.append(name.toLower());
    }
    if (not settings::write(storage_.section(), *core::settings::find("scanWhitelist"), normalized.join(';')))
        return false;
    invalidateScan();
    emit settingsChanged();
    return true;
}

void CaptureDuty::setTesseractExecutable(QString executable)
{
    const QString value = executable.trimmed();
    auto          keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("tesseractExecutable")).toString()) return;
    settings::write(keys, *core::settings::find("tesseractExecutable"), value);
    lifecycle_.rebuild();
    emit settingsChanged();
}

void CaptureDuty::setTesseractDataDirectory(QString directory)
{
    const QString value = directory.trimmed();
    auto          keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("tesseractDataDirectory")).toString()) return;
    settings::write(keys, *core::settings::find("tesseractDataDirectory"), value);
    lifecycle_.rebuild();
    emit settingsChanged();
}

void CaptureDuty::setRapidOcrExecutable(QString executable)
{
    auto const value = executable.trimmed();
    auto       keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("rapidOcrExecutable")).toString()) return;
    if (not settings::write(keys, *core::settings::find("rapidOcrExecutable"), value)) return;
    settings::write(keys, *core::settings::find("rapidOcrTrust"), QString{});
    lifecycle_.detectIdle();
    lifecycle_.rebuild();
    emit settingsChanged();
}

void CaptureDuty::setRapidOcrModelsDirectory(QString directory)
{
    auto const value = directory.trimmed();
    auto       keys  = storage_.section();
    if (value == settings::read(keys, *core::settings::find("rapidOcrModelsDirectory")).toString()) return;
    if (not settings::write(keys, *core::settings::find("rapidOcrModelsDirectory"), value)) return;
    settings::write(keys, *core::settings::find("rapidOcrTrust"), QString{});
    lifecycle_.detectIdle();
    lifecycle_.rebuild();
    emit settingsChanged();
}

QVariantMap CaptureDuty::rapidOcrBundleState() const
{
    return QVariantMap{{"installed", capture::bundleInstalled(capture::bundleRoot())},
                       {"downloading", bundleDownloader_.busy()},
                       {"percent", bundleDownloader_.busy() ? rapidOcrBundlePercent_ : 0},
                       {"failure", rapidOcrBundleFailure_},
                       {"ascii", capture::rapidOcrPathIsAscii(QString::fromStdWString(capture::bundleRoot().wstring()))},
                       {"archive", QString::fromStdWString(capture::bundleArchivePath().wstring())}};
}

void CaptureDuty::downloadRapidOcrBundle()
{
    // A non-ASCII profile root is the one place the unpacked models cannot live (docs/adr/0132),
    // so the download is refused before any bytes move, and the page explains it.
    if (bundleDownloader_.busy() or not capture::rapidOcrPathIsAscii(QString::fromStdWString(capture::bundleRoot().wstring())))
        return;
    rapidOcrBundlePercent_ = 0;
    rapidOcrBundleFailure_ = QString{};
    emit rapidOcrBundleChanged();
    bundleDownloader_.start();
}

void CaptureDuty::cancelRapidOcrBundleDownload()
{
    bundleDownloader_.cancel();
}

void CaptureDuty::removeRapidOcrBundle()
{
    if (bundleDownloader_.busy())
        return;
    const QString root = QString::fromStdWString(capture::bundleRoot().wstring());
    QDir{root}.removeRecursively();
    auto keys = storage_.section();
    if (settings::read(keys, *core::settings::find("rapidOcrExecutable")).toString().startsWith(root) or settings::read(keys, *core::settings::find("rapidOcrModelsDirectory")).toString().startsWith(root)) {
        // Both writes clear the trust record and rebuild the engine, so nothing keeps pointing at
        // the folder that just went away.
        setRapidOcrExecutable(QString{});
        setRapidOcrModelsDirectory(QString{});
    }
    rapidOcrBundleFailure_ = QString{};
    emit rapidOcrBundleChanged();
}

void CaptureDuty::onBundleProgressed(qint64 received, qint64 total)
{
    if (total <= 0)
        return;
    const int percent = static_cast<int>(received * 100 / total);
    if (percent == rapidOcrBundlePercent_)
        return;
    rapidOcrBundlePercent_ = percent;
    emit rapidOcrBundleChanged();
}

void CaptureDuty::onBundleFinished(bool ok, QString failure)
{
    if (ok) {
        rapidOcrBundleFailure_ = QString{};
        setRapidOcrExecutable(QString::fromStdWString(capture::bundleExecutablePath().wstring()));
        setRapidOcrModelsDirectory(QString::fromStdWString(capture::bundleModelsDirectoryPath().wstring()));
        // The writes cleared the trust record; the detection that follows is what writes it again,
        // as it always has -- a download never grants trust by itself (docs/adr/0132).
        detectRapidOcr();
    } else {
        rapidOcrBundleFailure_ = failure;
    }
    emit rapidOcrBundleChanged();
}

void CaptureDuty::setOcrLocalEngine(QString engine)
{
    auto const value = engine.trimmed();
    if (not capture::parseLocalEngine(value.toStdString())) return;
    auto keys = storage_.section();
    if (value == settings::read(keys, *core::settings::find("ocrLocalEngine")).toString()) return;
    if (not settings::write(keys, *core::settings::find("ocrLocalEngine"), value)) return;
    lifecycle_.detectIdle();
    lifecycle_.rebuild();
    emit settingsChanged();
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
        lifecycle_.detectIdle();
    }
    lifecycle_.rebuild();
    emit settingsChanged();
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
    routeUpload(upload_.clear(QStringLiteral("configuration-changed"), true));
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
    // Each service keeps its own address and credentials. The outgoing service's values are put
    // away first and the incoming service's own are restored, so no key is ever carried across.
    saveOnlineService(previous);
    restoreOnlineService(value);
    // A language one service lists and the next does not would leave the profile configured as
    // if it could speak it; the first language the new service lists takes over.
    if (auto const* const chosen = ocr::findOcrProvider(value); chosen != nullptr) {
        auto const language = settings::read(keys, *core::settings::find("ocrOnlineLanguage")).toString();
        if (not chosen->languages.contains(language)) {
            for (auto const* const candidate : {"eng", "auto"})
                if (chosen->languages.contains(QLatin1String(candidate))) {
                    settings::write(keys, *core::settings::find("ocrOnlineLanguage"), QLatin1String(candidate));
                    break;
                }
        }
    }
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
    for (auto const& field : provider->credentials) {
        auto const key = credentialSettingKey(field.slot);
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

void CaptureDuty::saveOnlineService(QString const& provider)
{
    if (provider.isEmpty()) return;
    auto const keys  = storage_.section();
    auto       entry = storage_.section("OCR-SERVICES").object(provider.toStdString());
    entry.set<std::string>("endpoint",
                           settings::read(keys, *core::settings::find("ocrOnlineEndpoint")).toString().toStdString());
    entry.set<std::string>("apiKey", keys.value<std::string>("OCR-API-KEY", {}));
    entry.set<std::string>("secretKey", keys.value<std::string>("OCR-SECRET-KEY", {}));
}

void CaptureDuty::restoreOnlineService(QString const& provider)
{
    auto const entry = storage_.section("OCR-SERVICES").object(provider.toStdString());
    auto       keys  = storage_.section();
    settings::write(keys, *core::settings::find("ocrOnlineEndpoint"), QString::fromStdString(entry.value<std::string>("endpoint", {})));
    keys.set<std::string>("OCR-API-KEY", entry.value<std::string>("apiKey", {}));
    keys.set<std::string>("OCR-SECRET-KEY", entry.value<std::string>("secretKey", {}));
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
        lifecycle_.testCancelled();
    }
    routeUpload(upload_.clear(QStringLiteral("configuration-changed"), true));
}

void CaptureDuty::restoreDefaults()
{
    // The rows the table marks Signal::Capture, in the table's order, through the same write path a
    // choice takes -- so a restore and a fresh install read one list rather than two.
    for (auto const& row : core::settings::kRows)
        if (row.signal == core::settings::Signal::Capture and row.restorable)
            setSetting(row.key, settings::defaultValue(row));
}

void CaptureDuty::setMarkButtonHeld(bool const held)
{
    session_.setMarkButtonHeld(held);
}

void CaptureDuty::setScanPaused(bool const paused)
{
    // The session invalidates its scan on a change; the duty announces it, as it always has.
    if (session_.scanPaused() == paused) return;
    session_.setScanPaused(paused);
    clearScanLines();
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
    auto       engine  = lifecycle_.engine();
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
    auto                engine  = lifecycle_.engine();
    auto const          started = executor_->start(id, [engine, request = std::move(request)](std::shared_ptr<std::atomic_bool> cancel) {
        try {
            return engine->recognize(request, std::move(cancel));
        } catch (...) {
            return capture::OcrResult{.status = capture::OcrStatus::Failed, .requestId = request.requestId};
        }
    });
    if (not started) return;
    jobContext_ = OcrJob{.requestId = id, .test = true};
    lifecycle_.testStarted();
    emit settingsChanged();
}

void CaptureDuty::detectRapidOcr()
{
    if (jobContext_ or executor_->busy()) return;
    auto config           = storedRapidOcrConfig(storage_);
    config.requireTrust   = false;
    auto const inspection = capture::inspectRapidOcr(config);
    if (inspection.status != capture::OcrStatus::Ready) {
        lifecycle_.detectRefused(inspection.status);
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
    jobContext_ = OcrJob{.requestId = id, .detectRapid = true};
    lifecycle_.detectStarted();
    emit settingsChanged();
}

void CaptureDuty::beginOnlineUpload(QImage image, QPoint const anchor)
{
    if (not onlineConfigured()) {
        emit screenshotFailed(CaptureRefusal::OnlineNotConfigured);
        return;
    }
    routeUpload(upload_.begin(std::move(image), anchor, onlineConfig(), ++nextRequestId_, std::chrono::steady_clock::now()));
}

void CaptureDuty::confirmOnlineUpload(qulonglong const requestId)
{
    routeUpload(upload_.confirm(requestId, screenshotGeneration_, std::chrono::steady_clock::now()));
}

void CaptureDuty::cancelOnlineUpload()
{
    routeUpload(upload_.cancel());
}

void CaptureDuty::onConsentTimeout()
{
    routeUpload(upload_.consentTimedOut());
}

void CaptureDuty::onUploadTimeout()
{
    if (jobContext_ and jobContext_->online) executor_->cancel();
    routeUpload(upload_.absoluteTimedOut());
}

void CaptureDuty::routeUpload(UploadStep const& step)
{
    // The job slot and the scan are the duty's; the transaction names when they move.
    auto const cancelOnlineJob = [this] {
        if (jobContext_ and jobContext_->online) {
            executor_->cancel();
            jobContext_.reset();
        }
    };
    switch (step.kind) {
        case UploadStep::Kind::None:
            return;
        case UploadStep::Kind::Consent:
            // A confirmation is not a pause the scan may resume from: the frozen crop and the scan's
            // previous stable text must not revive each other.
            cancelOnlineJob();
            invalidateScan();
            consentTimer_.start(static_cast<int>(step.card.expiresInMs));
            uploadTimer_.start(static_cast<int>(UploadTransaction::kAbsoluteWindow.count()));
            emit uploadPreviewReady(static_cast<qulonglong>(step.card.requestId), step.image);
            emit uploadConsentRequested(QVariantMap{{"requestId", static_cast<qulonglong>(step.card.requestId)},
                                                    {"host", step.card.host},
                                                    {"provider", step.card.provider},
                                                    {"language", step.card.language},
                                                    {"expiresInMs", step.card.expiresInMs},
                                                    {"previewUrl", QStringLiteral("image://ocr-consent/%1").arg(static_cast<qulonglong>(step.card.requestId))}});
            if (step.changed) emit settingsChanged();
            return;
        case UploadStep::Kind::Submit: {
            consentTimer_.stop();
            auto const& request = step.request;
            auto const  client  = step.client;
            auto const  started = executor_->start(request.requestId, [client, request](std::shared_ptr<std::atomic_bool> cancel) mutable {
                try {
                    return client->recognize(request, std::move(cancel));
                } catch (...) {
                    return capture::OcrResult{.status = capture::OcrStatus::Failed, .requestId = request.requestId};
                }
            });
            if (not started) {
                routeUpload(upload_.submitRefused());
                return;
            }
            jobContext_ = OcrJob{.anchor = step.anchor, .generation = step.generation, .requestId = request.requestId, .online = true};
            if (step.changed) emit settingsChanged();
            return;
        }
        case UploadStep::Kind::Finished:
            consentTimer_.stop();
            uploadTimer_.stop();
            if (step.retryWindow.count() > 0) consentTimer_.start(static_cast<int>(step.retryWindow.count()));
            if (step.cleared) {
                cancelOnlineJob();
                invalidateScan();
            }
            if (step.announced) emit uploadFinished(uploadOutcomeMap(step.outcome));
            if (step.changed) emit settingsChanged();
            return;
        case UploadStep::Kind::Completed:
            consentTimer_.stop();
            uploadTimer_.stop();
            if (step.cleared) {
                cancelOnlineJob();
                invalidateScan();
            }
            if (step.announced) emit uploadFinished(uploadOutcomeMap(step.outcome));
            classifyText(step.text, step.anchor, true);
            if (step.changed) emit settingsChanged();
            return;
    }
}

void CaptureDuty::testOnlineOcr()
{
    if (jobContext_ or upload_.active() or executor_->busy()) return;
    if (budgetPaused_) {
        lifecycle_.testRefused(QStringLiteral("budget-paused"));
        emit settingsChanged();
        return;
    }
    if (not onlineConfigured()) {
        lifecycle_.testRefused(QStringLiteral("online-unconfigured"));
        emit settingsChanged();
        return;
    }
    auto client = std::shared_ptr<ocr::OnlineOcrClient>{
        ocr::makeOnlineOcrClient(onlineConfig(), onlineTransport_, onlineTokens_)};
    if (not client) {
        lifecycle_.testRefused(QStringLiteral("online-unconfigured"));
        emit settingsChanged();
        return;
    }
    auto const          id = ++nextRequestId_;
    capture::OcrRequest request{.requestId = id,
                                .source    = capture::OcrSource::OnlineTest,
                                .image     = localTestImage(capture::OcrLanguage::English),
                                .deadline  = std::chrono::milliseconds{30000}};
    lifecycle_.testStarted();
    auto const started = executor_->start(id, [client = std::move(client), request = std::move(request)](std::shared_ptr<std::atomic_bool> cancel) mutable {
        try {
            return client->recognize(request, std::move(cancel));
        } catch (...) {
            return capture::OcrResult{.status = capture::OcrStatus::Failed, .requestId = request.requestId};
        }
    });
    if (not started) {
        lifecycle_.testRefused(QStringLiteral("busy"));
        emit settingsChanged();
        return;
    }
    jobContext_ = OcrJob{.requestId = id, .test = true};
    emit settingsChanged();
}

bool CaptureDuty::launchOcr(QImage image, QPoint const anchor, QString context)
{
    auto const          generation = context.isEmpty() ? screenshotGeneration_ : session_.generation();
    auto const          id         = ++nextRequestId_;
    capture::OcrRequest request{.requestId = id,
                                .source    = context.isEmpty() ? capture::OcrSource::Screenshot : capture::OcrSource::AutoScan,
                                .image     = std::move(image),
                                .deadline  = std::chrono::milliseconds{10000}};
    auto                engine  = context.isEmpty() ? lifecycle_.engine() : lifecycle_.scanEngine();
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
                        .runtime          = lifecycle_.status()});
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
    ScanFacts facts;
    facts.budgetPaused        = budgetPaused_;
    facts.autoScan            = settings().value("autoScan").toBool();
    facts.scanReady           = lifecycle_.scanReady();
    facts.jobOrUploadInFlight = jobContext_.has_value() or upload_.active();
    facts.minimumWordLength   = settings().value("minimumWordLength").toInt();
    if (auto const foreground = capture::foregroundWindow()) {
        auto& view = facts.foreground.emplace();
        // The page text comes through UI Automation, which exposes a browser's page; no other window has text here.
        if (foreground->ownProcess)
            view.kind = ScanForeground::Kind::OwnProcess;
        else if (foreground->processName.compare(QStringLiteral("chrome.exe"), Qt::CaseInsensitive) == 0)
            view.kind = ScanForeground::Kind::Chrome;
        else
            view.kind = ScanForeground::Kind::Other;
        view.window  = foreground->window;
        view.context = foreground->context;
        if (view.kind == ScanForeground::Kind::Chrome) view.placement = scanPlacementOf(*foreground);
    }

    auto const step = session_.tick(facts);
    routeScanStep(step);
}

void CaptureDuty::routeScanStep(ScanStep const& step)
{
    if (step.cleared) clearScanLines();
    if (step.warm) {
        // The first query for a browser window makes it build its page tree, so the query runs ahead of the first read.
        QThreadPool::globalInstance()->start(QRunnable::create([window = *step.warm] { warmPageTreeOf(window); }));
    }
    if (step.placed) {
        // The layer rebuilds its whole model for every payload, so the gate drops the ones that
        // describe the page it already draws and holds the rest to a cadence, with the newest
        // kept for the flush (docs/adr/0167).
        auto const now    = std::chrono::steady_clock::now();
        auto const answer = scanGate_.admit(*step.placed, now);
        if (answer.emitNow) {
            emitPlaced(*step.placed);
        } else if (answer.hold and not scanPlaceFlush_.isActive()) {
            auto const dueMs = std::chrono::duration_cast<std::chrono::milliseconds>(answer.dueAt - now).count();
            scanPlaceFlush_.start(static_cast<int>(std::max<decltype(dueMs)>(0, dueMs)));
        }
    }
}

void CaptureDuty::emitPlaced(ScanStep::Placed const& placed)
{
    QVariantList words;
    for (auto const& word : placed.words)
        words.append(QVariantMap{{QStringLiteral("lemma"), word.lemma},
                                 {QStringLiteral("surface"), word.surface},
                                 {QStringLiteral("verdict"), verdictToken(word.state)},
                                 {QStringLiteral("x"), word.x},
                                 {QStringLiteral("y"), word.y},
                                 {QStringLiteral("width"), word.width},
                                 {QStringLiteral("height"), word.height}});
    emit scanPlaced(words, placed.screen, placed.origin, placed.scale);
}

void CaptureDuty::clearScanLines()
{
    scanGate_.reset();
    scanPlaceFlush_.stop();
    emit scanCleared();
}

void CaptureDuty::onScanPlaceFlush()
{
    if (auto const held = scanGate_.takeHeld(std::chrono::steady_clock::now())) emitPlaced(*held);
}

CaptureDuty::ScanDebugState CaptureDuty::debugScanState() const
{
    auto const session = session_.debugState();
    return {.autoScan   = settings().value("autoScan").toBool(),
            .tracking   = session.tracking,
            .generation = session.generation,
            .candidates = session.candidates};
}

void CaptureDuty::debugScanArm(QRect const screen)
{
    session_.debugArm({.band = screen, .screen = screen, .origin = screen.topLeft(), .scale = 1.0});
}

void CaptureDuty::debugScanFrame(capture::TrackFrame frame)
{
    routeScanStep(session_.onFrame(session_.generation(), std::move(frame)));
}

void CaptureDuty::debugScanClear()
{
    invalidateScan();
}

void CaptureDuty::onOcrFinished(std::uint64_t const requestId, capture::OcrResult result)
{
    if (not jobContext_ or jobContext_->requestId != requestId) return;
    auto const context = *jobContext_;
    jobContext_.reset();
    if (context.online) {
        routeUpload(upload_.finish(result, std::chrono::steady_clock::now()));
        return;
    }
    if (context.detectRapid) {
        auto const trusted = result.status == capture::OcrStatus::Ready and
                             result.text.contains(QStringLiteral("Lens"), Qt::CaseInsensitive);
        auto keys = storage_.section();
        if (trusted) {
            auto const digest = capture::rapidOcrTrustDigest(storedRapidOcrConfig(storage_));
            if (not digest.isEmpty() and settings::write(keys, *core::settings::find("rapidOcrTrust"), digest)) {
                lifecycle_.detectDone(QStringLiteral("ready"), result.timing.totalMs);
                lifecycle_.rebuild();
                emit settingsChanged();
                return;
            }
        }
        settings::write(keys, *core::settings::find("rapidOcrTrust"), QString{});
        lifecycle_.detectDone(result.status == capture::OcrStatus::Ready ? QStringLiteral("failed") : statusToken(result.status),
                              result.timing.totalMs);
        lifecycle_.rebuild();
        emit settingsChanged();
        return;
    }
    if (context.test) {
        lifecycle_.testFinished(result);
        emit settingsChanged();
        return;
    }
    if (context.probe) {
        lifecycle_.setStatus(result.status);
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
                                              .scanGeneration       = session_.generation(),
                                              .screenshotGeneration = screenshotGeneration_,
                                              .foregroundContext    = std::move(foregroundContext),
                                              .budgetPaused         = budgetPaused_,
                                              .scanPaused           = session_.scanPaused(),
                                              .autoScan             = settings().value("autoScan").toBool()});
    if (not context.context.isEmpty())
        LENS_INFO("scan result: status={} chars={} took {} ms, fate={}", statusToken(result.status).toStdString(), result.text.size(), result.timing.totalMs, fate == capture::OcrResultFate::Current ? "current" : fate == capture::OcrResultFate::Superseded ? "superseded"
                                                                                                                                                                                                                                                               : "withdrawn");
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
        // The scan reads with Tesseract, so its failure says nothing about the chosen local engine's state.
        if (capture::isRuntimeFault(result.status))
            LENS_WARN("scan OCR failed (status {})", statusToken(result.status).toStdString());
        else
            LENS_TRACE("scan: no words in this frame (status {})", statusToken(result.status).toStdString());
        return;
    }
    if (context.context.isEmpty()) {
        // Only the screenshot path reaches here with no scan context: a selection brings its own
        // text rather than going through OCR at all.
        classifyText(result.text, context.anchor, true);
        return;
    }
}

bool CaptureDuty::chromeScanActive()
{
    return settings().value("autoScan").toBool() and session_.trackingPage();
}

void CaptureDuty::lookUpWord(QString const surface, QPoint const anchor)
{
    classifyText(surface, anchor, true);
}

void CaptureDuty::verdictChanged(QString const lemma)
{
    session_.onVerdictChanged(lemma);
}

void CaptureDuty::onSelectionReleased(QPoint const anchor)
{
    routeSelection(selection_.released(anchor, {.budgetPaused = budgetPaused_, .hoverMode = hover_.mode()}));
}

void CaptureDuty::toggleHoverMode()
{
    bool const on = hover_.toggle();
    if (on)
        hoverTimer_.start();
    else
        hoverTimer_.stop();
    emit hoverModeChanged(on);
}

void CaptureDuty::onHoverTick()
{
    auto const       nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    HoverFlow::Facts facts;
    facts.budgetPaused = budgetPaused_;
    facts.pointer      = hoverCursorPos();
    for (auto const& name : settings().value("scanWhitelist").toString().split(';', Qt::SkipEmptyParts))
        facts.whitelist.push_back(name.trimmed().toLower().toStdString());
    auto const step = hover_.tick(facts, nowMs);
    if (not step.read) return;

    // The read is a few round trips into the window under the pointer, so it runs off the main thread.
    QThreadPool::globalInstance()->start(QRunnable::create([this, at = step.at, whitelist = std::move(step.whitelist)] {
        auto word = hoverWordAt(at, whitelist);
        QMetaObject::invokeMethod(
            this, [this, word = std::move(word)]() mutable { onHoverWordRead(std::move(word)); }, Qt::QueuedConnection);
    }));
}

void CaptureDuty::onHoverWordRead(std::optional<HoverWord> word)
{
    auto const step = hover_.wordRead(std::move(word), hoverCursorPos());
    if (step.asked) classifyText(step.text, step.anchor, true);
}

void CaptureDuty::beginSelection(QPoint const anchor, std::optional<QString> const text)
{
    routeSelection(selection_.begin(anchor, text));
}

void CaptureDuty::routeSelection(std::optional<SelectionCapture::Answer> const& answer)
{
    if (not answer) return;
    emit selectionReady(answer->selection);
    if (answer->translateNow) {
        // The reader framed this region themselves, so they have already said what they want done
        // with it: translate. The bar would ask again -- and OCR exists for text that cannot be
        // selected, which is exactly the text whose reader wants the meaning rather than a menu.
        emit selectionActionRequested(QStringLiteral("translate"), answer->selection.text);
        return;
    }
    emit selectionBarRequested(answer->bar);
}

void CaptureDuty::classifyText(QString const text, QPoint const anchor, bool const translateNow)
{
    routeSelection(selection_.classify(text, anchor, translateNow));
}

}
