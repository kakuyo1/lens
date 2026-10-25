/**
 * @file main.cpp
 * @brief The application: load the data the pipeline needs, wire the pieces, put the tray up.
 *
 * There is no main window: the QML root is a window that is never shown, the surfaces are
 * windows of their own, and the tray icon is the only visible thing when nothing is happening.
 */

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QVariant>
#include <QTranslator>
#include <QUrl>
#include <QVector>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <system_error>

#include "app/pet/pet_controller.h"
#include "core/document/pet_store.h"
#include "app_controller.h"
#include "app/platform/autostart.h"
#include "core/domain/filter_core.h"
#include "core/document/known_store.h"
#if defined(LENS_DEV_SURFACE)
#include "core/dev/dev_command.h"
#include "app/dev/dev_surface.h"
#endif
#include "app/platform/global_hotkey.h"
#include "llm/llm_client.h"
#include "llm/llm_pricing.h"
#include "llm/llm_protocol.h"
#include "app/platform/mouse_selection_hook.h"
#include "notice.h"
#include "paths.h"
#include "settings_defaults.h"
#include "app/platform/tray.h"
#include "util/log.h"
#include "util/qt_log.h"

using lens::app::AppController;
using lens::app::GlobalHotkey;
using lens::app::MouseSelectionHook;
using lens::app::Tray;
using lens::app::narrow;
using lens::app::settingsPath;
using lens::app::toPath;
using lens::app::widen;
using lens::core::KnownStore;

namespace {

/// The data directory configure baked in: where a build tree reads from. A shipped build has
/// data/ beside the executable instead, and that one wins. See dataDirectory().
constexpr const char* kBuildDataDir = LENS_DATA_DIR;

/// The settings document at the repository root, which a first run copies into the reader's
/// profile once. See settingsPath().
constexpr const char* kDevSettingsPath = LENS_SETTINGS_PATH;

/**
 * @brief Where the word list and the protocol files are read from.
 *
 * The installer puts them beside the executable -- `<install>\data` -- which keeps the package
 * relocatable and compiles nothing about the machine into it. A build tree has no `data/` next
 * to `build-ninja/src/app/lens.exe` (nothing writes one), so it reads the path configure baked in.
 *
 * @return `<applicationDirPath>/data` when that is a directory, else the baked-in path.
 */
std::filesystem::path dataDirectory()
{
    const std::filesystem::path installed = toPath(QCoreApplication::applicationDirPath()) / "data";
    std::error_code             ec;
    if (std::filesystem::is_directory(installed, ec))
        return installed;
    return toPath(QString::fromUtf8(kBuildDataDir));
}

/// @brief The setting's language code, and the locale whose .qm carries it.
///
/// Not the same string: the setting holds "zh", the file is named for the locale, lens_zh_CN.qm;
/// "zh-Hant" is the script-tagged code the catalog offers, and its file is lens_zh_TW.qm. A name
/// built straight from the setting would look for lens_zh.qm or lens_zh-Hant.qm, neither of which
/// exists.
///
/// One code per .ts under i18n/, also the list `data/llm/catalog.json` offers as interface
/// languages: a language the catalog names and this function does not silently leaves the
/// interface in English.
const char* translationFileFor(const QString& language)
{
    if (language == QLatin1String("zh"))
        return "zh_CN";
    if (language == QLatin1String("zh-Hant"))
        return "zh_TW";
    if (language == QLatin1String("en"))
        return "en_US";
    if (language == QLatin1String("es"))
        return "es_ES";
    if (language == QLatin1String("ja"))
        return "ja_JP";
    return nullptr;
}

/// @return A translator for one of the .qm files built into the binary, or a null one.
std::unique_ptr<QTranslator> loadTranslation(const QString& language)
{
    const char* file = translationFileFor(language);
    if (file == nullptr) {
        LENS_WARN("'{}' names no translation file; the interface stays in English",
                  language.toStdString());
        return {};
    }

    auto translator = std::make_unique<QTranslator>();
    if (!translator->load(QStringLiteral(":/i18n/lens_%1.qm").arg(QLatin1String(file)))) {
        LENS_WARN("lens_{}.qm did not load; the interface stays in English", file);
        return {};
    }
    return translator;
}

/// @brief Keep exactly one interface translation installed.
class TranslationKeeper {
public:
    void select(const QString& language)
    {
        if (current_ == language)
            return;
        if (translator_)
            QCoreApplication::removeTranslator(translator_.get());
        translator_ = loadTranslation(language);
        if (translator_)
            QCoreApplication::installTranslator(translator_.get());
        current_ = language;
        LENS_INFO("interface language is now '{}'", language.toStdString());
    }

private:
    std::unique_ptr<QTranslator> translator_;
    QString                      current_;
};

QString settingsValue(KnownStore& store, const char* key)
{
    return QString::fromStdString(store.section().value<std::string>(key, {}));
}

/// How long a surface stays hidden before its graphics resources are released. Panels hide and
/// re-show while they are being repositioned, and rebuilding the scene graph on every such hop
/// costs more than the memory it saves, so a window that is back within this window is left alone.
constexpr int kReleaseDelayMs = 500;

/**
 * @brief Hand a hidden surface's graphics resources back to the driver.
 *
 * QQuickWindow keeps its graphics resources -- the device, the swapchain, the buffers and the
 * textures, and with them the driver's worker threads -- until the last window is deleted, and
 * setPersistentGraphics() defaults to true, so a hidden window keeps the lot. Measured with
 * .workbuddy-ai/tools/qwindow-probe on this machine: showing a second window costs about 75
 * driver threads and 1,200 handles, hiding returns none of them under Qt's defaults, and
 * releaseResources() with both persistence hints off returns all of them in one call.
 *
 * The hints only take effect before a window's scene graph is initialised, so this runs straight
 * after the QML is loaded and before the event loop. Every surface but the root window is hidden
 * at that point; the root is never shown and the pet is up from the start, and neither of those
 * should be released anyway.
 */
void releaseHiddenSurfaces(QObject* root)
{
    const auto tune = [](QQuickWindow* window) {
        window->setPersistentGraphics(false);
        window->setPersistentSceneGraph(false);
        QObject::connect(window, &QWindow::visibleChanged, window, [window] {
            if (window->isVisible())
                return;
            QTimer::singleShot(kReleaseDelayMs, window, [window] {
                if (not window->isVisible())
                    window->releaseResources();
            });
        });
    };

    if (auto* self = qobject_cast<QQuickWindow*>(root))
        tune(self);
    for (auto* window : root->findChildren<QQuickWindow*>())
        tune(window);
}

}

int main(int argc, char* argv[])
{
    // Widgets, not just Gui: the tray menu is a QMenu, which the contract chose over QML's
    // experimental platform menu types.
    QApplication app(argc, argv);
    // A tray application has no window to keep open, so the default rule would quit as soon
    // as the first surface closed.
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName(QStringLiteral("Lens"));

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

    lens::log::init();
    lens::log::installQtMessageHandler();

    // The Run entry names a path, and this launch is the truth about where the executable is.
    lens::app::refreshAutostart();

    const std::filesystem::path dataDir = dataDirectory();
    // The review sign's word face (UI-SPEC.md 4.13). Without the file the sign falls back to the monospace family it names.
    if (QFontDatabase::addApplicationFont(widen(dataDir / "fonts" / "PixelifySans-Regular.ttf")) < 0)
        LENS_WARN("the review sign's font is missing; the sign uses the monospace family instead");
    try {
        lens::core::loadWordlist(dataDir / "wordlist.txt");
        lens::core::loadIrregulars(dataDir / "irregulars.tsv");
        const std::filesystem::path protocolDir = dataDir / "llm";
        lens::llm::loadLlmProtocol(lens::llm::Channel::Word, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Entity, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Sentence, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Translation, protocolDir);
        lens::llm::loadLlmProtocol(lens::llm::Channel::Peek, protocolDir);
    } catch (const std::exception& e) {
        // Without the word list there is no pipeline at all, and without the protocol there is
        // nothing to send. Neither failure is recoverable at runtime.
        LENS_CRITICAL("startup failed: {}", e.what());
        return 1;
    }

#if defined(LENS_DEV_SURFACE)
    // docs/adr/0068: a run given --settings-file keeps its document there, so it never writes the reader's profile.
    const auto devSettings = lens::app::devSettingsFile(app.arguments());
    if (devSettings and devSettings->empty()) {
        LENS_CRITICAL("--settings-file takes a path");
        return 1;
    }
    const std::filesystem::path settings = devSettings ? *devSettings : settingsPath(toPath(QString::fromUtf8(kDevSettingsPath)));
#else
    const std::filesystem::path settings = settingsPath(toPath(QString::fromUtf8(kDevSettingsPath)));
#endif
    LENS_INFO("settings document: {}", narrow(settings));
    KnownStore store = KnownStore::load(settings);

    const auto pricing = [&] {
        try {
            return lens::llm::Pricing::load(dataDir / "llm" / "pricing.json");
        } catch (const std::exception& e) {
            // A missing price list costs the cost surfaces their figures and nothing else.
            LENS_ERROR("costs will read as zero: {}", e.what());
            return lens::llm::Pricing{};
        }
    }();

    const QString apiKey  = settingsValue(store, "API-KEY");
    const QString baseUrl = settingsValue(store, "URL");
    const QString model   = settingsValue(store, "MODEL");
    if (apiKey.isEmpty())
        LENS_WARN("no API key in the settings; explanations will be refused until one is set");

    lens::llm::LlmClient llm(lens::llm::Config{
        QUrl(baseUrl.isEmpty() ? lens::app::defaults::defaultBaseUrl() : baseUrl),
        apiKey,
        model.isEmpty() ? lens::app::defaults::defaultModelName() : model});

    MouseSelectionHook hook;
    GlobalHotkey       hotkey;

    AppController controller(store, llm, hook, hotkey, pricing, dataDir);

    // A refused pet data directory leaves the pet off and says why; the rest of the program is unaffected. The reader's
    // packages sit beside the settings document, in %APPDATA%\Lens\pets (docs/adr/0043).
    lens::core::pet::PetStore     petStore(store);
    lens::app::pet::PetController petController(dataDir / "pet", settings.parent_path() / "pets", petStore);
    lens::app::pet::PetController::provide(&petController);
    QObject::connect(&controller, &AppController::petEvent, &petController, &lens::app::pet::PetController::handle);
    controller.bindPet(&petController);
    QObject::connect(&petController, &lens::app::pet::PetController::fellBack, &controller, [&controller](QString title, QString body) {
        controller.explanationDuty()->showNotice(title, body, QString::fromLatin1(lens::app::kNoticeInfo));
    });

    Tray tray([&controller] { return controller.trayReadout(); });
    QObject::connect(&controller, &AppController::settingsChanged, &tray, &Tray::refresh);
    QObject::connect(&controller, &AppController::statsChanged, &tray, &Tray::refresh);
    QObject::connect(&controller, &AppController::busyChanged, &tray, &Tray::refresh);
    QObject::connect(&controller, &AppController::uiLanguageChanged, &tray, &Tray::refresh);
    if (!tray.show())
        return 1;

    // The surfaces reach both objects as QML singletons -- Controller and Tray -- declared on
    // the classes themselves with QML_SINGLETON. Context properties would be the simpler wiring,
    // but no linter can see a name that exists only in the engine's context: every read off
    // controller or tray came back as unqualified access, 53 warnings across the surfaces, and
    // a mistyped property name would read as undefined rather than fail. Registering the types
    // gives qmllint the property names to check against.
    AppController::provide(&controller);
    Tray::provide(&tray);

    // Transparent frameless windows (the pet, the review sign, the answer card) need an alpha channel in the scene
    // graph's default surface format, otherwise their transparent pixels composite as black on Windows.
    QQuickWindow::setDefaultAlphaBuffer(true);

    QQmlApplicationEngine engine;

    TranslationKeeper translations;
    const QString     uiLanguage = settingsValue(store, "uiLanguage");
    translations.select(uiLanguage.isEmpty() ? QStringLiteral("zh") : uiLanguage);
    // Installing a translator does not make QML's qsTr bindings re-evaluate -- the tray menu
    // follows along only because Tray::retranslate() listens for this signal, and the surfaces
    // had no equivalent. engine.retranslate() is the hook Qt 6.2 added for exactly this, and it
    // is why the engine is declared above the connect rather than below it.
    QObject::connect(&controller, &AppController::uiLanguageChanged, &app, [&translations, &engine](const QString& language) {
        translations.select(language);
        engine.retranslate();
    });

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { LENS_CRITICAL("the QML surfaces failed to load"); QCoreApplication::exit(1); }, Qt::QueuedConnection);

    engine.loadFromModule("Lens", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    // Surfaces are windows that are hidden and re-shown, not windows that are created and
    // destroyed, so nothing else ever asks the scene graph to let go. LENS_KEEP_GRAPHICS=1 keeps
    // Qt's defaults, for measuring against them in one binary.
    if (qEnvironmentVariableIsEmpty("LENS_KEEP_GRAPHICS"))
        releaseHiddenSurfaces(engine.rootObjects().constFirst());

    // Installed here, and not one line earlier. A WH_MOUSE_LL hook is called on the thread that
    // installed it, so from install() until that thread is back in a message loop the mouse
    // waits on it and then waits out the shell's 300 ms hook timeout -- measured on the real
    // machine, three events at 312 ms apiece while the QML above was compiled and its nine
    // windows built, which is what a reader feels as the pointer being yanked away as the app
    // comes up. Everything above this line blocks this thread and none of it needs the hook:
    // the controller has been connected to it since it was constructed, and nothing the
    // surfaces read comes from it.
    if (!hook.install())
        return 1;

    // After the surfaces, and for a different reason than the hook's: a global key is the machine's
    // while it is held, and the reader does not need it in the first second of startup. A
    // combination another program already owns leaves the trigger missing and says so in the
    // settings -- the program carries on either way.
    controller.installHotkey();

    // After the settings are loaded and the surfaces are up, and after the two things that
    // take the machine -- the mouse hook and the trigger key -- have had their moment. Once a
    // local date at most: the duty writes the date when the check starts, so a machine that
    // cannot reach the source does not retry on every launch.
    controller.updateDuty()->checkAtStartup();
    controller.syncDuty()->startAtLaunch();

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
            QTimer::singleShot(1500 + static_cast<int>(index) * 900, &app, [root, step] {
                const bool invoked = QMetaObject::invokeMethod(root,
                                                               "profileScenario",
                                                               Q_ARG(QVariant, QVariant::fromValue(step)));
                if (!invoked)
                    LENS_WARN("QML profile scenario step {} could not be invoked", step);
            });
        }
    }
#endif

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
    lens::app::startDevSurface(engine.rootObjects().constFirst(), &controller, &petController, app.arguments(), lens::core::dev::Build{.config = config, .qmlDebug = qmlDebug});
#endif

    LENS_INFO("Lens is up");
    return app.exec();
}
