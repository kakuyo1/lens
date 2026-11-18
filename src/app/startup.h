#pragma once

#include <QStringList>

#include <filesystem>

/**
 * @file startup.h
 * @brief The startup steps main() names, in the order it calls them.
 *
 * The composition root owns the long-lived objects and threads them through these steps; each step
 * carries one phase of bringing the application up, with the ordering rationale beside its body in
 * startup.cpp. They sit on the executable beside main.cpp -- no test target links them -- for the
 * same reason platform/qt_voice_output.cpp does.
 */

class QApplication;
class QQmlApplicationEngine;

namespace lens::core {
class KnownStore;
}

namespace lens::llm {
struct Config;
class Pricing;
}

namespace lens::app {

class AppController;
class Tray;
class TranslationKeeper;
class SharedGraphics;
class MouseSelectionHook;
class AltKeyHook;

namespace pet {
class PetController;
}

/// @brief Install the crash handler before anything that could crash, and return the log directory it writes to.
std::filesystem::path installCrashForensics();

/// @brief Pin the Windows media backend, before the application object chooses one.
void selectMediaBackend();

/// @brief Set the tray application's quit policy and name on @p app.
void configureApplication(QApplication& app);

/// @brief Set the UI font QML cannot express: one face for Latin and CJK.
void configureUiFont();

/// @brief Start the rotating log and forward Qt's messages into it.
void initLogging(const std::filesystem::path& logDir);

/// @brief Refresh the autostart entry and claim the single running instance.
void claimSingleInstance();

/// @brief Load the sign font, the word lists and every channel's protocol.
/// @return False on a failure the program cannot recover from; the caller exits.
bool loadPipelineData(const std::filesystem::path& dataDir);

/// @brief Resolve the settings document's path, honoring --settings-file on a dev-surface build.
/// @return The path to load, or an empty one when --settings-file was given without a path -- a fatal
///         misuse the caller exits on.
std::filesystem::path resolveSettingsPath(const QStringList& arguments);

/// @brief Import a leftover custom wordbook once, as mastered verdicts, and drop the key (docs/adr/0159).
void importLegacyWordbook(core::KnownStore& store);

/// @brief Load the price list; a missing one costs the cost surfaces their figures and nothing else.
llm::Pricing loadPricing(const std::filesystem::path& dataDir);

/// @brief Build the LLM configuration from the settings, falling back to the shipped defaults.
llm::Config makeLlmConfig(core::KnownStore& store);

/// @brief Provide the pet singleton and wire its events, fallback notice and binding to the facade.
void wirePet(AppController& controller, pet::PetController& petController);

/// @brief Refresh the tray whenever the facade's readout changes.
void wireTray(AppController& controller, Tray& tray);

/// @brief Provide the Controller and Tray QML singletons the surfaces read.
void provideQmlSingletons(AppController& controller, Tray& tray);

/// @brief Give the engine the OCR consent image provider and bind it to the capture duty.
void attachConsentPreview(QQmlApplicationEngine& engine, AppController& controller);

/// @brief Select the stored interface language and retranslate the surfaces when it changes.
void installTranslation(AppController& controller, QQmlApplicationEngine& engine, core::KnownStore& store, TranslationKeeper& translations);

/// @brief Load the QML module's Main surface.
/// @return False when the surfaces failed to load; the caller exits.
bool loadSurfaces(QQmlApplicationEngine& engine);

/// @brief Adopt the shared device and release hidden surfaces' resources, unless LENS_KEEP_GRAPHICS keeps Qt's defaults.
void tuneSurfaceGraphics(QQmlApplicationEngine& engine, SharedGraphics* shared = nullptr);

/// @brief Install the mouse hook, the Alt key and the global trigger, in the order startup needs.
/// @return False when the mouse hook refused; the caller exits.
bool installInputHooks(MouseSelectionHook& hook, AltKeyHook& altHook, AppController& controller);

/// @brief Report the last crash, then start the update, sync and onboarding duties.
void runStartupDuties(AppController& controller, const std::filesystem::path& logDir);

/// @brief Drive the QML profile scenario named by LENS_QML_PROFILE_SCENARIO, on a QML-debug build.
void startProfileScenario(QQmlApplicationEngine& engine);

/// @brief Start the developer command surface on a dev-surface build given --dev-script.
void startDevSurfaceIfEnabled(QQmlApplicationEngine& engine, AppController& controller, pet::PetController& petController, const QStringList& arguments);

} // namespace lens::app