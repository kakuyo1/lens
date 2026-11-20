/**
 * @file main.cpp
 * @brief The composition root: own the long-lived objects and name the startup steps in order.
 *
 * There is no main window: the QML root is a window that is never shown, the surfaces are
 * windows of their own, and the tray icon is the only visible thing when nothing is happening.
 * Every step's body and ordering rationale lives in app/startup.cpp; the helpers it uses live in
 * their own modules (paths, translation, platform/surface_graphics), so this file holds only the
 * flow and the objects whose lifetime is the run's.
 */

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include <filesystem>

#include "app/platform/alt_key_hook.h"
#include "app/platform/global_hotkey.h"
#include "app/platform/mouse_selection_hook.h"
#include "app/platform/qt_voice_output.h"
#include "app/platform/surface_graphics.h"
#include "app/platform/tray.h"
#include "app/pet/pet_controller.h"
#include "app/startup.h"
#include "app/translation.h"
#include "app_controller.h"
#include "core/document/known_store.h"
#include "core/document/pet_store.h"
#include "core/domain/sync_partition.h"
#include "llm/llm_client.h"
#include "paths.h"
#include "util/log.h"

using lens::app::AppController;
using lens::app::GlobalHotkey;
using lens::app::AltKeyHook;
using lens::app::MouseSelectionHook;
using lens::app::Tray;
using lens::core::KnownStore;

int main(int argc, char* argv[]) // NOLINT(bugprone-exception-escape): the throw is on a failure path the program cannot recover from; swallowing it would hide the fault
{
    const std::filesystem::path logDir = lens::app::installCrashForensics();
    lens::app::selectMediaBackend();

    QApplication app(argc, argv);
    lens::app::configureApplication(app);
    lens::app::configureUiFont();
    lens::app::initLogging(logDir);
    lens::app::claimSingleInstance();

    const std::filesystem::path dataDir = lens::app::dataDirectory();
    if (!lens::app::loadPipelineData(dataDir))
        return 1;

    const std::filesystem::path settings = lens::app::resolveSettingsPath(app.arguments());
    if (settings.empty())
        return 1;
    KnownStore store = KnownStore::load(settings);

    // The secrets a build before the seal wrote in the clear are sealed to this account once,
    // here rather than inside the duty, so it happens before anything reads the document
    // concurrently (docs/adr/0177). A document already sealed writes nothing.
    lens::core::sync::SyncPartition{store}.sealBareSecrets();

    lens::app::importLegacyWordbook(store);
    const auto           pricing = lens::app::loadPricing(dataDir);
    lens::llm::LlmClient llm(lens::app::makeLlmConfig(store));

    MouseSelectionHook hook;
    AltKeyHook         altHook;
    GlobalHotkey       hotkey;
    // The machine's pronunciation devices -- the player, the Windows synthesiser and the local
    // voices (docs/adr/0183). Built here rather than in the controller because device code lives
    // in the executable: no test target links it, and the seam above it takes a silent output.
    lens::app::QtVoiceOutput speechOutput;

#ifdef _WIN32
    // Declared before the controller and the engine so the shared device outlives both: the glass idle release
    // trims it (docs/adr/0202), and the engine is gone before it.
    lens::app::SharedGraphics sharedGraphics;
#endif
    AppController controller(store, llm, hook, altHook, hotkey, speechOutput, pricing, dataDir);

    // A refused pet data directory leaves the pet off and says why; the rest of the program is unaffected. The reader's
    // packages sit beside the settings document, in %APPDATA%\Lens\pets (docs/adr/0043).
    lens::core::pet::PetStore     petStore(store);
    lens::app::pet::PetController petController(dataDir / "pet", settings.parent_path() / "pets", petStore);
    lens::app::wirePet(controller, petController);

    Tray tray([&controller] { return controller.trayReadout(); });
    lens::app::wireTray(controller, tray);
    if (!tray.show())
        return 1;

    lens::app::provideQmlSingletons(controller, tray);

    // Transparent frameless windows (the pet, the review sign, the answer card) need an alpha channel in the scene
    // graph's default surface format, otherwise their transparent pixels composite as black on Windows.
    QQuickWindow::setDefaultAlphaBuffer(true);

    QQmlApplicationEngine engine;
    lens::app::attachConsentPreview(engine, controller);

    lens::app::TranslationKeeper translations;
    lens::app::installTranslation(controller, engine, store, translations);

    if (!lens::app::loadSurfaces(engine))
        return 1;

#ifdef _WIN32
    lens::app::tuneSurfaceGraphics(engine, &sharedGraphics);
    // The glass idle release is where the capture sessions stop costing memory; the shared device that drew the
    // cards is trimmed at the same moment, wired here rather than reached for from the glass (docs/adr/0202).
    controller.glassBackdrop()->setIdleTrim([&sharedGraphics] { sharedGraphics.trim(); });
#else
    lens::app::tuneSurfaceGraphics(engine);
#endif

    if (!lens::app::installInputHooks(hook, altHook, controller))
        return 1;

    lens::app::runStartupDuties(controller, logDir);
    lens::app::startProfileScenario(engine);
    lens::app::startDevSurfaceIfEnabled(engine, controller, petController, app.arguments());
#ifdef _WIN32
    lens::app::watchForHangs(logDir, &sharedGraphics);
#else
    lens::app::watchForHangs(logDir);
#endif

    LENS_INFO("Lens is up");
    return app.exec();
}