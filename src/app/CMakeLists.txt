# The app itself. lens_app is grouped by responsibility:
#
#   duties/    -- use cases the reader asks for: capture, explanation, review, sync, update.
#   platform/  -- Win32 and shell integration: the mouse hook, hotkey, tray and window helpers.
#   glass/     -- the desktop under the cards: capture, duplication and the sheet item.
#   pet/       -- the pet's Qt shell around the core state machine.
#   qml/       -- the surfaces, grouped by surface family, all one QML module.
#   the root   -- the facade (app_controller), the composition root (main.cpp), and the shared
#                 settings and projection headers.
#
#   lens      -- the executable: main.cpp and the .qm files.
#   icons/    -- the tray art, compiled into the binary as resources.
#
# The QML module hangs off lens_app and not off the executable, which is the reverse of what
# this file used to say. The reason is registration scope: qt_add_qml_module registers the C++
# types of its own backing target and no others, so a module attached to `lens` could not
# register classes that live in lens_app.

add_library(lens_app STATIC
  # Facade and composition root.
  app_controller.cpp
  paths.h
  paths.cpp
  settings_access.h
  settings_access.cpp
  settings_defaults.h
  words_view.h

  # Use cases: what the reader asks for, independent of a surface.
  duties/capture_duty.h
  duties/capture_duty.cpp
  duties/cost_duty.h
  duties/cost_duty.cpp
  duties/explanation_duty.h
  duties/explanation_duty.cpp
  duties/model_catalog_duty.h
  duties/model_catalog_duty.cpp
  duties/picture_duty.h
  duties/picture_duty.cpp
  duties/review_duty.h
  duties/review_duty.cpp
  duties/storage_duty.h
  duties/storage_duty.cpp
  duties/sync_duty.h
  duties/sync_duty.cpp
  duties/update_duty.h
  duties/update_duty.cpp

  # Platform: Win32 and shell integration.
  platform/autostart.cpp
  platform/global_hotkey.h
  platform/global_hotkey.cpp
  platform/mouse_selection_hook.h
  platform/mouse_selection_hook.cpp
  platform/selection_text_grabber.cpp
  platform/system_motion.cpp
  platform/tray.cpp
  platform/window_native.cpp

  # Glass: the desktop under the cards.
  glass/glass_backdrop.cpp
  glass/glass_capability.h
  glass/glass_capability.cpp
  glass/glass_capture.h
  glass/glass_capture.cpp
  glass/glass_desktop.h
  glass/glass_desktop.cpp
  glass/glass_duplication.h
  glass/glass_duplication.cpp
  glass/glass_math.cpp
  glass/glass_sheet_item.h
  glass/glass_sheet_item.cpp
  glass/glass_source.h
  glass/glass_source.cpp
  glass/glass_stats.h
  glass/glass_stats.cpp

  # Pet: the Qt shell around the core state machine.
  pet/pet_assets.cpp
  pet/pet_catalog.cpp
  pet/pet_controller.cpp
  pet/pet_flight.cpp
  pet/pet_package_fs.cpp
  pet/pet_throw_mood.cpp
  pet/pet_placement.cpp
  pet/pet_scenes.cpp
  pet/pet_wander.cpp
)

# src/ is exposed for self-contained "app/xxx.h" includes (same as lens_core and lens_llm).
target_include_directories(lens_app PUBLIC ${CMAKE_SOURCE_DIR}/src)

# The QML type registration includes each module header by its basename, so the directories that
# hold a registered header must be on the include path for that generated file to find it.
target_include_directories(lens_app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/pet
  ${CMAKE_CURRENT_SOURCE_DIR}/glass
  ${CMAKE_CURRENT_SOURCE_DIR}/platform
)

# Qt6::Gui is public because AppController's interface reaches into it: the clipboard and
# QPoint come from there. Widgets is public for the tray -- QMenu and QSystemTrayIcon.
# lens_llm is public for the same reason: the constructor takes an LlmClient and a Pricing.
# lens_update comes along with it: UpdateDuty is a member, and its header is in this one.
# Quick is public because the QML module below backs this library, and anything linking it is
# linking the module: that now includes lens_gtest_integration, which used to need no QML.
target_link_libraries(lens_app
  PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick lens_llm lens_update lens_sync lens_pictures
  PRIVATE lens_core lens_util lens_capture
)

# user32 for the hook, SendInput and the clipboard; gdi32 for the glass sheet's desktop read; dxgi and d3d11 for the
# glass duplication path, which builds a device on the adapter that owns each output (src/app/glass/glass_duplication.h);
# windowsapp and runtimeobject for Windows Graphics Capture, the source for screens duplication refuses
# (src/app/glass/glass_capture.h). Not guarded by if(WIN32): this module *is* the Win32 one, and the product has no other
# platform. OLE is deliberately absent -- ClipboardSnapshot records why its round trip is not usable here.
target_link_libraries(lens_app PRIVATE user32 gdi32 dxgi d3d11 windowsapp runtimeobject)

# The C++/WinRT projection headers ship beside the SDK's ABI headers, not on the include path vcvars sets. The
# documented build path runs vcvars first (scripts/build/build.bat), which names the SDK version; an IDE configure
# without that environment takes the newest SDK under the standard Windows Kits root instead.
if(DEFINED ENV{WindowsSdkDir} AND DEFINED ENV{WindowsSDKVersion})
  set(LENS_SDK_VERSION "$ENV{WindowsSDKVersion}")
  string(REGEX REPLACE "[\\\\/]+$" "" LENS_SDK_VERSION "${LENS_SDK_VERSION}")
  set(LENS_CPPWINRT "$ENV{WindowsSdkDir}Include/${LENS_SDK_VERSION}/cppwinrt")
else()
  set(_lens_pf86 "$ENV{ProgramFiles} (x86)")
  file(GLOB _lens_cppwinrt "${_lens_pf86}/Windows Kits/10/Include/*/cppwinrt/winrt/base.h")
  if(_lens_cppwinrt)
    list(SORT _lens_cppwinrt)
    list(GET _lens_cppwinrt -1 _lens_cppwinrt_base)
    get_filename_component(LENS_CPPWINRT "${_lens_cppwinrt_base}" DIRECTORY)
    get_filename_component(LENS_CPPWINRT "${LENS_CPPWINRT}" DIRECTORY)
  endif()
endif()
file(TO_CMAKE_PATH "${LENS_CPPWINRT}" LENS_CPPWINRT)
if(NOT LENS_CPPWINRT OR NOT EXISTS "${LENS_CPPWINRT}/winrt/base.h")
  message(FATAL_ERROR
    "C++/WinRT headers were not found (looked in '${LENS_CPPWINRT}'); check the Windows SDK installation, or"
    " configure through scripts/build/build.bat, which enters the MSVC environment.")
endif()
target_include_directories(lens_app PRIVATE "${LENS_CPPWINRT}")

# RC, so the resource script below compiles. The root project() lists CXX alone and the root
# CMakeLists is another branch's file, but enable_language is directory scoped: turning it on
# here covers this directory and everything under it, and this is the only directory with a
# resource script to compile.
enable_language(RC)

# main.cpp, the .qm files, and the icon the shell draws for lens.exe. lens.rc names lens.ico
# by a path relative to itself, so the resource compiler reads the art beside the script and
# no machine path is written into either file.
qt_add_executable(lens
  main.cpp
  ${CMAKE_SOURCE_DIR}/data/icons/lens.rc
)

# The developer command surface (docs/adr/0068), only when the build asks for it. It sits in the executable because
# main.cpp is its one caller and it reaches the windows through the Main window that caller hands it.
if(LENS_ENABLE_DEV_SURFACE)
  target_sources(lens PRIVATE dev/dev_surface.cpp dev/dev_surface.h dev/dev_pipe.cpp dev/dev_pipe.h)
  target_compile_definitions(lens PRIVATE LENS_DEV_SURFACE)
endif()

# The module's QML files reach the executable through lens_app's resources, but the C++ types
# it registers do not come with them: a static plugin is only imported when its init object --
# the one carrying Q_IMPORT_PLUGIN -- is linked in, and nothing links it on its own. Qt attaches
# that object to the plugin target, so naming the plugin is the whole fix. Without it the
# surfaces load and then every reference to Controller or Tray is a ReferenceError.
target_link_libraries(lens PRIVATE lens_appplugin)

# GUI subsystem, not console. Left at the default, a tray application carries a console window
# on screen for as long as it runs -- there is no main window to close it with. The property is
# also what pulls Qt6::EntryPoint in: Qt6::Core's interface links it through
# $<BOOL:$<TARGET_PROPERTY:WIN32_EXECUTABLE>>, so setting this one property is the whole change.
set_target_properties(lens PROPERTIES WIN32_EXECUTABLE ON)

# lens_core comes along for its include directories: main.cpp reads the settings document
# through KnownStore, which needs nlohmann/json.hpp from third_party/.
target_link_libraries(lens
  PRIVATE lens_app lens_core Qt6::Quick Qt6::Svg
)

# The two paths a build tree has and an installed copy does not. Neither is the only answer
# any more: main.cpp prefers data/ beside the executable, which is where installer/lens.iss
# puts it, and keeps the settings document in %APPDATA% -- these are the fallbacks that keep a
# build tree working, where nothing writes either one next to build-ninja/src/app/lens.exe.
#
# LENS_DATA_DIR is where the word list and the protocol files are read from when there is no
# data/ beside the executable. LENS_SETTINGS_PATH is the document a first run after the
# packaging change carries into the profile once, so an API key already configured keeps
# working; nothing writes it afterwards.
target_compile_definitions(lens PRIVATE
  LENS_DATA_DIR="${CMAKE_SOURCE_DIR}/data"
  LENS_SETTINGS_PATH="${CMAKE_SOURCE_DIR}/settings.local.json"
)

# Tokens holds the stored theme and the motion table, and is the one singleton of the colour tables. Each surface
# window owns a SurfaceTokens built from it (docs/adr/0041). The property has to be set before qt_add_qml_module looks at
# the file, not after: it decides there whether to write a singleton entry into the module's qmldir.
set_source_files_properties(qml/theme/Tokens.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# PreviewSample is the settings preview's payload builder: one table of sample content, asked by
# every window that could raise a preview, so it is a singleton for the same reason Tokens is.
set_source_files_properties(qml/components/preview/PreviewSample.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# GlassRule is the backdrop-or-not rule, read by the card that draws and the window that decides when to read the
# desktop (docs/adr/0069): a rule the two hold separately is a rule that drifts, so it lives in one singleton.
set_source_files_properties(qml/components/chrome/GlassRule.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# The URI is Lens, so generated QML tooling files belong below a Lens directory. Keeping the
# target path aligned makes CMake generate the qmllint response file in every build tree,
# including the dedicated profiler tree.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/Lens")

# The QML files are grouped by surface family. Main.qml is the module entry point and has to stay
# at the root; SurfaceHost.qml and ScreenPoints.qml sit beside it, holding the placement and panel
# rules and the native-to-DIP mapping. Each family owns its window, its private parts and its
# settings previews: bubble/, selection/ (with bodies/),
# settings/ (pages/, previews/), pet/, panels/, capture/. dev/ holds the profiling scenario, and
# components/ holds the cross-family pieces by role (chrome/, controls/, content/, preview/).
# theme/ owns every table, from
# TokenTable and the two live views to the five colour tables and the motion table. All of them
# are the same QML module, so no group names another in an import: Qt gives every file in the
# module an implicit import of its own types.
# The module hangs off lens_app and not off the executable because of where the types the
# surfaces talk to are declared. qt_add_qml_module registers the C++ types of the target it is
# called on and only that target's; a class living in a linked static library stays invisible to
# it. A module on `lens` therefore registered nothing, and every read off Controller or Tray
# came back to qmllint as unqualified access -- 53 warnings that no amount of renaming fixes.
# The executable still owns main.cpp, the icon resources and the .qm files, and picks the
# module up by linking lens_app.
qt_add_qml_module(lens_app
  URI Lens
  VERSION 1.0
  # GlassSheetItem derives from QQuickItem, so the module's type metadata has to name the module that declares it.
  IMPORTS QtQuick
  SOURCES
    app_controller.h
    glass/glass_sheet_item.h
    platform/tray.h
    platform/system_motion.h
    pet/pet_controller.h
  QML_FILES
    qml/Main.qml
    qml/SurfaceHost.qml
    qml/pet/PetWindow.qml
    qml/pet/ReviewSignWindow.qml
    qml/pet/ReviewSignBoard.qml
    qml/pet/ReviewCard.qml
    qml/pet/ReviewSignLayout.qml
    qml/dev/ProfileScenario.qml
    qml/ScreenPoints.qml
    qml/pet/PetScene.qml
    qml/pet/SceneEntryEffect.qml
    qml/settings/previews/PetStage.qml
    qml/pet/PetPackageDialog.qml
    qml/pet/SpriteLayer.qml
    qml/panels/TrayMenu.qml
    qml/selection/SelectionBar.qml
    qml/selection/bodies/SelectionBarClassicBody.qml
    qml/selection/bodies/SelectionBarTabBody.qml
    qml/selection/bodies/SelectionBarHandBody.qml
    qml/selection/bodies/SelectionBarTagBody.qml
    qml/selection/bodies/SelectionBarFanBody.qml
    qml/bubble/Bubble.qml
    qml/bubble/BubbleCard.qml
    qml/panels/Notice.qml
    qml/panels/UpdateCard.qml
    qml/capture/Mask.qml
    qml/settings/SettingsPopup.qml
    qml/settings/previews/ThemePreview.qml
    qml/settings/previews/VocabPreview.qml
    qml/settings/previews/NotePreview.qml
    qml/settings/previews/SelectionBarPreview.qml
    qml/settings/SettingsCategoryRow.qml
    qml/settings/SettingsLinkRow.qml
    qml/settings/SettingsCatalog.qml
    qml/settings/pages/SettingsAboutPage.qml
    qml/settings/pages/SettingsBudgetPage.qml
    qml/settings/pages/SettingsFontPage.qml
    qml/settings/pages/SettingsGeneralPage.qml
    qml/settings/pages/SettingsLearningPage.qml
    qml/settings/pages/SettingsCapturePage.qml
    qml/settings/pages/SettingsModelPage.qml
    qml/settings/pages/SettingsExtensionsPage.qml
    qml/settings/pages/SettingsClipboardPage.qml
    qml/settings/pages/SettingsConnectionPage.qml
    qml/settings/pages/SettingsSyncPage.qml
     qml/panels/StatsPopup.qml
     qml/panels/WordsPopup.qml
    qml/panels/WordsListRow.qml
     qml/panels/YearWordsPopup.qml
    qml/panels/YearPalette.qml
     qml/panels/RemovalQuestion.qml
    qml/panels/CostPopup.qml
    qml/theme/Tokens.qml
    qml/theme/TokenTable.qml
    qml/components/preview/PreviewSample.qml
    qml/theme/SurfaceTokens.qml
    qml/components/chrome/GlassHost.qml
    qml/components/chrome/GlassRule.qml
    qml/theme/Light.qml
    qml/theme/Dark.qml
    qml/theme/Motion.qml
    qml/theme/Frosted.qml
    qml/theme/Liquid.qml
    qml/theme/GlassOnDark.qml
    qml/components/controls/Icon.qml
    qml/components/controls/IconButton.qml
    qml/components/controls/FocusRing.qml
    qml/components/chrome/ShadowCard.qml
    qml/components/chrome/HeightHold.qml
    qml/components/chrome/SurfaceFade.qml
    qml/components/controls/Segment.qml
    qml/components/controls/Switch.qml
    qml/components/controls/Chip.qml
    qml/components/controls/StepSlider.qml
    qml/components/controls/SwitchRow.qml
    qml/panels/StatRow.qml
    qml/components/controls/DropdownField.qml
    qml/components/controls/DropdownRow.qml
    qml/panels/MenuRow.qml
    qml/components/controls/PathField.qml
    qml/components/content/MixedText.qml
    qml/components/content/RevealText.qml
    qml/components/content/SenseRows.qml
    qml/components/content/PictureStrip.qml
    qml/components/content/ChainText.qml
    qml/bubble/BubbleTitleRow.qml
    qml/bubble/BubbleChainPanel.qml
    qml/bubble/ChainEdges.qml
    qml/bubble/ChainSignpost.qml
    qml/bubble/ChainCollocations.qml
    qml/components/controls/HotkeyField.qml
    qml/components/chrome/GlassSurface.qml
    qml/components/controls/SliderRow.qml
    qml/components/preview/PreviewFrame.qml
    qml/util/Numbers.js
    qml/util/Format.js
    qml/bubble/BubbleVerdictRow.qml
    qml/bubble/BubbleHeaderActions.qml
)

# The icon art, addressed as :/icons/<name>.svg. BASE strips the source directory so the names
# stay as written on disk. The tray-<state>-<taskbar> files are the shell's bitmap states, each
# carrying its own two colours; the ui- files are the surfaces' outlines, stroked in white and
# tinted from QML, so one file serves every surface and both themes.
#
# lens.svg is the product mark itself -- the ring and the centre dot every other file here is a
# recolouring of. It is stroked in white like the ui- set, so the tray menu's first line draws it
# through Icon and it takes the theme's colour; the tray art and lens.ico are its geometry with
# the ink baked in, since a shell icon has no QML to tint it. The .ico and the .rc that carries
# it are the executable's rather than resources, so they are named in the executable above.
#
# It hangs off lens_app and not off the executable, for the same reason the QML module does: a
# resource only resolves in a binary that carries it, and the surfaces are the ones reading these
# -- every QTest target links lens_app, so on `lens` each of them drew a missing image and printed
# a warning per case. The executable still gets them, since it links lens_app.
set(LENS_ICONS
  ${CMAKE_SOURCE_DIR}/data/icons/lens.svg
  ${CMAKE_SOURCE_DIR}/data/icons/tray-auto-ondark.svg
  ${CMAKE_SOURCE_DIR}/data/icons/tray-auto-onlight.svg
  ${CMAKE_SOURCE_DIR}/data/icons/tray-busy-ondark.svg
  ${CMAKE_SOURCE_DIR}/data/icons/tray-busy-onlight.svg
  ${CMAKE_SOURCE_DIR}/data/icons/tray-off-ondark.svg
  ${CMAKE_SOURCE_DIR}/data/icons/tray-off-onlight.svg
  ${CMAKE_SOURCE_DIR}/data/icons/tray-budget-ondark.svg
  ${CMAKE_SOURCE_DIR}/data/icons/tray-budget-onlight.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-back.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-calendar.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-chevron-down.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-chevron-right.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-close.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-copy.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-explain.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-export.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-folder.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-github.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-language.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-pictures.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-quit.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-settings.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-stats.svg
  ${CMAKE_SOURCE_DIR}/data/icons/ui-translate.svg
)
qt_add_resources(lens_app "icons"
  PREFIX "/icons"
  BASE ${CMAKE_SOURCE_DIR}/data/icons
  FILES ${LENS_ICONS}
)

# The glass shaders, compiled by qsb into :/shaders/glass.frag.qsb and :/shaders/glass_hblur.frag.qsb. qsb comes from the
# same Qt install as the runtime, so the two cannot drift apart. They are resources of lens_app for the reason the icons
# are: a binary that links the surfaces draws them.
qt_add_shaders(lens_app "lens_shaders"
  PREFIX "/shaders"
  BASE ${CMAKE_CURRENT_SOURCE_DIR}/shaders
  FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/glass.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/glass_hblur.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fan.frag
)

# The .ts files, run through lrelease and embedded at :/i18n/. Rebuilding after a string
# change needs lupdate first -- see CLAUDE.md, which owns that command.
qt_add_translations(lens
  TS_FILES
    ${CMAKE_SOURCE_DIR}/i18n/lens_en_US.ts
    ${CMAKE_SOURCE_DIR}/i18n/lens_zh_CN.ts
    ${CMAKE_SOURCE_DIR}/i18n/lens_es_ES.ts
    ${CMAKE_SOURCE_DIR}/i18n/lens_ja_JP.ts
  RESOURCE_PREFIX "/i18n"
)

