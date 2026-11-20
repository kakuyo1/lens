/**
 * @file startup.cpp
 * @brief The startup steps main() names: see startup.h for the order and the ownership rule.
 */

#include "startup.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVector>

#include "app/platform/autostart.h"
#include "app/platform/crash_dump.h"
#include "app/platform/hang_watchdog.h"
#include "app/platform/running_instance.h"
#include "app/platform/surface_graphics.h"
#include "app/settings_access.h"
#include "app/settings_defaults.h"
#include "app/translation.h"
#include "app_controller.h"
#include "consent_preview.h"
#include "core/domain/filter_core.h"
#include "core/domain/sync_partition.h"
#include "core/document/known_store.h"
#include "llm/llm_protocol.h"
#include "notice.h"
#include "paths.h"
#include "pet/pet_controller.h"
#include "util/log.h"
#include "util/qt_log.h"

#if defined(LENS_DEV_SURFACE)
#include "app/dev/dev_surface.h"
#include "core/dev/dev_command.h"
#endif

namespace lens::app {
namespace {

/// The settings document at the repository root, which a first run copies into the reader's
/// profile once. See settingsPath().
constexpr const char* kDevSettingsPath = LENS_SETTINGS_PATH;

/// The build switch for the first-run notice (docs/adr/0144): on, the notice goes up at every launch and the
/// document is left alone; off, it goes up once to a reader who has no key.
#if defined(LENS_FORCE_ONBOARDING)
constexpr bool kForceOnboarding = true;
#else
constexpr bool kForceOnboarding = false;
#endif

QString settingsValue(core::KnownStore& store, const char* key)
{
    return QString::fromStdString(store.section().value<std::string>(key, {}));
}

} // namespace

std::filesystem::path installCrashForensics()
{
    // Crash forensics before anything that could crash the process. The log directory is
    // resolved here, before QStandardPaths has an application object to ask (app/paths.h): an
    // installed launch starts in the install folder, and a relative name would send the log and
    // these artifacts into Program Files. From this line on every later death -- QApplication
    // included -- leaves a dump, a summary and a marker in logs/dumps (docs/adr/0174).
    const std::filesystem::path logDir = logDirectory(installedBuild());
    installCrashDumpHandler(logDir);
    return logDir;
}

void selectMediaBackend()
{
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND"))
        qputenv("QT_MEDIA_BACKEND", "windows");
}

void configureApplication(QApplication& app)
{
    // A tray application has no window to keep open, so the default rule would quit as soon
    // as the first surface closed.
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("Lens"));
}

void configureUiFont()
{
    // The UI font, set here and not in QML because QML cannot express it: a Text's font value
    // type has no families list, and naming a family there replaces the one it would inherit.
    //
    // One face for Latin and CJK when the Windows installation provides it. Mixing a Latin
    // face with a Chinese fallback at the same nominal weight makes the Chinese glyphs visibly
    // darker, because the two faces differ in stroke density. Mixed numeric values split
    // their runs in MixedText.qml, so digits can still use the crisp monospace face.
    QFont uiFont;
    if (QFontDatabase::hasFamily(QStringLiteral("Noto Sans SC")))
        uiFont.setFamilies({QStringLiteral("Noto Sans SC"), QStringLiteral("Segoe UI Variable")});
    else
        uiFont.setFamilies({QStringLiteral("Segoe UI Variable"), QStringLiteral("Microsoft YaHei UI Light")});
    QGuiApplication::setFont(uiFont);
}

void initLogging(const std::filesystem::path& logDir)
{
    lens::log::init(lens::log::kDefaultLevel, logDir);
    lens::log::installQtMessageHandler();
}

void claimSingleInstance()
{
    // The Run entry names a path, and this launch is the truth about where the executable is.
    refreshAutostart();
    holdRunningInstance();
}

bool loadPipelineData(const std::filesystem::path& dataDir)
{
    // The review sign's word face (UI-SPEC.md 4.13). Without the file the sign falls back to the monospace family it names.
    if (QFontDatabase::addApplicationFont(widen(dataDir / "fonts" / "PixelifySans-Regular.ttf")) < 0)
        LENS_WARN("the review sign's font is missing; the sign uses the monospace family instead");
    try {
        lens::core::loadWordlist(dataDir / "wordlist.txt");
        lens::core::loadIrregulars(dataDir / "irregulars.tsv");
        lens::core::loadWordbooks(dataDir / "wordbooks");
        lens::core::loadInformalSpellings(dataDir / "informal-spellings.txt");
        lens::core::loadScanWords(dataDir / "scan-words.txt");
        const std::filesystem::path protocolDir = dataDir / "llm";
        lens::llm::loadLlmProtocol(lens::llm::Channel::Word, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Entity, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Sentence, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Translation, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Peek, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Sense, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Breakdown, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Spell, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Grammar, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Profile, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Story, protocolDir);
    } catch (const std::exception& e) {
        // Without the word list there is no pipeline at all, and without the protocol there is
        // nothing to send. Neither failure is recoverable at runtime.
        LENS_CRITICAL("startup failed: {}", e.what());
        return false;
    }
    return true;
}

std::filesystem::path resolveSettingsPath(const QStringList& arguments)
{
#if defined(LENS_DEV_SURFACE)
    // docs/adr/0068: a run given --settings-file keeps its document there, so it never writes the reader's profile.
    const auto devSettings = devSettingsFile(arguments);
    if (devSettings and devSettings->empty()) {
        LENS_CRITICAL("--settings-file takes a path");
        return {};
    }
    const std::filesystem::path settings = devSettings ? *devSettings : settingsPath(toPath(QString::fromUtf8(kDevSettingsPath)));
#else
    const std::filesystem::path settings = settingsPath(toPath(QString::fromUtf8(kDevSettingsPath)));
#endif
    LENS_INFO("settings document: {}", narrow(settings));
    return settings;
}

void importLegacyWordbook(core::KnownStore& store)
{
    // The custom wordbook (docs/adr/0158) retired into a one-time import (docs/adr/0159): a path
    // left over from an earlier build is read once, its words are written into the verdict store
    // as mastered, and the key is dropped -- the file is never watched afterwards, so a lost one
    // retires nothing.
    if (store.document().contains("customWordlist")) {
        const auto held = store.document().at("customWordlist");
        if (held.is_string()) {
            const auto customPath = held.get<std::string>();
            if (!customPath.empty()) {
                auto const lemmas = lens::core::parseWordlistFile(customPath);
                if (!lemmas.empty()) {
                    for (auto const& lemma : lemmas)
                        store.mark(lemma, lens::core::WordVerdict::Mastered);
                    LENS_INFO("the old custom wordbook was imported once: {} mastered verdict(s) from '{}'", lemmas.size(), customPath);
                } else {
                    LENS_WARN("the old custom wordbook '{}' could not be read; nothing was imported", customPath);
                }
            }
        }
        store.document().erase("customWordlist");
        store.save();
    }
}

llm::Pricing loadPricing(const std::filesystem::path& dataDir)
{
    try {
        return lens::llm::Pricing::load(dataDir / "llm" / "pricing.json");
    } catch (const std::exception& e) {
        // A missing price list costs the cost surfaces their figures and nothing else.
        LENS_ERROR("costs will read as zero: {}", e.what());
        return lens::llm::Pricing{};
    }
}

llm::Config makeLlmConfig(core::KnownStore& store)
{
    const QString apiKey  = settingsValue(store, "API-KEY");
    const QString baseUrl = settingsValue(store, "URL");
    const QString model   = settingsValue(store, "MODEL");
    if (apiKey.isEmpty())
        LENS_WARN("no API key in the settings; explanations will be refused until one is set");

    return lens::llm::Config{
        QUrl(baseUrl.isEmpty() ? defaults::defaultBaseUrl() : baseUrl),
        apiKey,
        model.isEmpty() ? defaults::defaultModelName() : model};
}

void wirePet(AppController& controller, pet::PetController& petController)
{
    pet::PetController::provide(&petController);
    QObject::connect(&controller, &AppController::petEvent, &petController, &pet::PetController::handle);
    controller.bindPet(&petController);
    QObject::connect(&petController, &pet::PetController::fellBack, &controller, [&controller](QString const& title, QString const& body) {
        controller.explanationDuty()->showNotice(title, body, QString::fromLatin1(kNoticeInfo));
    });
}

void wireTray(AppController& controller, Tray& tray)
{
    QObject::connect(&controller, &AppController::settingsChanged, &tray, &Tray::refresh);
    QObject::connect(&controller, &AppController::statsChanged, &tray, &Tray::refresh);
    QObject::connect(&controller, &AppController::busyChanged, &tray, &Tray::refresh);
    QObject::connect(&controller, &AppController::uiLanguageChanged, &tray, &Tray::refresh);
}

void provideQmlSingletons(AppController& controller, Tray& tray)
{
    AppController::provide(&controller);
    Tray::provide(&tray);
}

void attachConsentPreview(QQmlApplicationEngine& engine, AppController& controller)
{
    // The engine owns the provider; the controller only holds a non-owning pointer to fill it
    // from the capture duty's frozen crop. The image never reaches a file or a QML property.
    auto* consentPreview = new ConsentPreview;
    engine.addImageProvider(QStringLiteral("ocr-consent"), consentPreview);
    controller.bindConsentPreview(consentPreview);
}

void installTranslation(AppController& controller, QQmlApplicationEngine& engine, core::KnownStore& store, TranslationKeeper& translations)
{
    // Through the row: uiLanguage is a table row, and its "not stored yet" answer is the row's
    // own default, not an empty language code the caller has to patch over.
    const QString uiLanguage = settings::read(store.section(), *lens::core::settings::find("uiLanguage")).toString();
    translations.select(uiLanguage);
    // Installing a translator does not make QML's qsTr bindings re-evaluate -- the tray menu
    // follows along only because Tray::retranslate() listens for this signal, and the surfaces
    // had no equivalent. engine.retranslate() is the hook Qt 6.2 added for exactly this, and it
    // is why the engine is declared above the connect rather than below it.
    QObject::connect(&controller, &AppController::uiLanguageChanged, qApp, [&translations, &engine](const QString& language) {
        translations.select(language);
        engine.retranslate();
    });
}

bool loadSurfaces(QQmlApplicationEngine& engine)
{
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, qApp, [] { LENS_CRITICAL("the QML surfaces failed to load"); QCoreApplication::exit(1); }, Qt::QueuedConnection); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to

    engine.loadFromModule("Lens", "Main");
    return not engine.rootObjects().isEmpty();
}

void tuneSurfaceGraphics(QQmlApplicationEngine& engine, [[maybe_unused]] SharedGraphics* shared)
{
    // Surfaces are windows that are hidden and re-shown, not windows that are created and
    // destroyed, so nothing else ever asks the scene graph to let go. LENS_KEEP_GRAPHICS=1 keeps
    // Qt's defaults, for measuring against them in one binary.
    if (qEnvironmentVariableIsEmpty("LENS_KEEP_GRAPHICS")) {
#ifdef _WIN32
        bool const sharedUp = shared != nullptr and shared->create();
        releaseHiddenSurfaces(engine.rootObjects().constFirst(), sharedUp ? shared : nullptr);
#else
        releaseHiddenSurfaces(engine.rootObjects().constFirst());
#endif
    }
}

bool installInputHooks(MouseSelectionHook& hook, AltKeyHook& altHook, AppController& controller)
{
    // Installed here, and not one line earlier. A WH_MOUSE_LL hook is called on the thread that
    // installed it, so from install() until that thread is back in a message loop the mouse
    // waits on it and then waits out the shell's 300 ms hook timeout -- measured on the real
    // machine, three events at 312 ms apiece while the QML above was compiled and its nine
    // windows built, which is what a reader feels as the pointer being yanked away as the app
    // comes up. Everything above this line blocks this thread and none of it needs the hook:
    // the controller has been connected to it since it was constructed, and nothing the
    // surfaces read comes from it.
    if (!hook.install())
        return false;

    // The pencil layer's Alt key. Without it the dashed lines still show; only the signposts and the bubble
    // by Alt+pointer are lost, and the hook says why in the log.
    altHook.install();

    // After the surfaces, and for a different reason than the hook's: a global key is the machine's
    // while it is held, and the reader does not need it in the first second of startup. A
    // combination another program already owns leaves the trigger missing and says so in the
    // settings -- the program carries on either way.
    controller.installHotkey();
    return true;
}

void runStartupDuties(AppController& controller, const std::filesystem::path& logDir)
{
    // After the settings are loaded and the surfaces are up, and after the two things that
    // take the machine -- the mouse hook and the trigger key -- have had their moment. Once a
    // local date at most: the duty writes the date when the check starts, so a machine that
    // cannot reach the source does not retry on every launch.
    // The crash notice goes first among the news: it is about the run that just ended, and the
    // translators are up, so the card reads in the UI language.
    controller.reportStartupCrash(logDir);
    controller.updateDuty()->checkAtStartup();
    controller.syncDuty()->startAtLaunch();
    // The first-run wizard is a surface too, so it waits for the same moment as the update check.
    controller.onboardingDuty()->startAtLaunch(kForceOnboarding);
}

void watchForHangs(const std::filesystem::path& logDir, [[maybe_unused]] SharedGraphics* shared)
{
    // The shared frame lock is the glass graph's one cross-thread wait, and the frame that pays a
    // long wait is the frame the GUI thread waits on when a hidden window comes back (docs/adr/0191):
    // the watchdog's dump and its log line name that frame, so the next freeze reads as a stack plus
    // a window instead of a silence.
#ifdef _WIN32
    startHangWatchdog(logDir, [shared] { return shared != nullptr ? shared->frameLockHolder() : std::string{}; });
#else
    startHangWatchdog(logDir);
#endif
}

void startProfileScenario([[maybe_unused]] QQmlApplicationEngine& engine)
{
#if defined(QT_QML_DEBUG)
    const QByteArray profileScenario = qgetenv("LENS_QML_PROFILE_SCENARIO");
    if (not profileScenario.isEmpty()) {
        QObject* const root = engine.rootObjects().constFirst();
        QVector<int>   steps;
        if (profileScenario == "startup")
            steps = {7};
        else if (profileScenario == "tray")
            steps = {0, 1, 7};
        else if (profileScenario == "navigation")
            steps = {2, 3, 4, 2, 7};
        else if (profileScenario == "settings")
            steps = {5, 7};
        else if (profileScenario == "bubble")
            steps = {6, 7};
        else
            steps = {0, 1, 2, 3, 4, 5, 6, 7};

        for (qsizetype index = 0; index < steps.size(); ++index) {
            const int step = steps.at(index);
            QTimer::singleShot(1500 + static_cast<int>(index) * 900, qApp, [root, step] {
                const bool invoked = QMetaObject::invokeMethod(root,
                                                               "profileScenario",
                                                               Q_ARG(QVariant, QVariant::fromValue(step)));
                if (!invoked)
                    LENS_WARN("QML profile scenario step {} could not be invoked", step);
            });
        }
    }
#endif
}

void startDevSurfaceIfEnabled([[maybe_unused]] QQmlApplicationEngine& engine, [[maybe_unused]] AppController& controller, [[maybe_unused]] pet::PetController& petController, [[maybe_unused]] const QStringList& arguments)
{
#if defined(LENS_DEV_SURFACE)
    // Starts only with --dev-script, so a run without the flag is the same run as a build without the surface. The build
    // block is read here because only this target sets QT_QML_DEBUG (docs/adr/0068).
#if defined(NDEBUG)
    auto const config = std::string{"optimized"};
#else
    auto const config = std::string{"debug"};
#endif
#if defined(QT_QML_DEBUG)
    constexpr bool qmlDebug = true;
#else
    constexpr bool qmlDebug = false;
#endif
    startDevSurface(engine.rootObjects().constFirst(), &controller, &petController, arguments, lens::core::dev::Build{.config = config, .qmlDebug = qmlDebug});
#endif
}

} // namespace lens::app