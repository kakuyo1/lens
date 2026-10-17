import QtQuick

/**
 * Debug-only automation for a profiling run: main.cpp calls Main.profileScenario(step) with each step of the scenario named by
 * LENS_QML_PROFILE_SCENARIO, and each step opens or closes one surface. It is not product behaviour, so it lives here rather
 * than in Main.qml, which stays under the size limit (docs/QML.md section 1).
 */
QtObject {
    /// Main.qml, whose panel functions the steps call.
    required property var host
    /// The surfaces the steps show and hide.
    required property var trayMenu
    required property var bubble
    required property var bar

    function run(step) {
        if (step === 0) {
            host.showTrayMenu();
        } else if (step === 1) {
            trayMenu.listVisible = true;
        } else if (step === 2) {
            trayMenu.visible = false;
            host.showStats();
        } else if (step === 3) {
            host.showCost();
        } else if (step === 4) {
            host.showWords();
        } else if (step === 5) {
            host.showSettings();
        } else if (step === 6) {
            bubble.show({title: "profile", type: "word", ipa: "/ˈproʊfaɪl/", en: "profile run", zh: "profile run", status: "new", x: 500, y: 500});
        } else if (step === 7) {
            host.hidePanels(null);
            bubble.dismiss();
            bar.dismiss();
            Qt.quit();
        }
    }
}
