import QtQuick

Item {
    id: root

    signal categoryRequested(string category)
    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 2

        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "General")
            summary: qsTranslate("SettingsPopup", "Theme, animations, launch at startup")
            onPicked: root.categoryRequested("general")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Reading & learning")
            summary: qsTranslate("SettingsPopup", "Vocabulary level, explanation language, multiple senses")
            onPicked: root.categoryRequested("learning")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Capture & popups")
            summary: qsTranslate("SettingsPopup", "Trigger channels")
            onPicked: root.categoryRequested("capture")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Model service")
            summary: qsTranslate("SettingsPopup", "Provider, model, API configuration")
            onPicked: root.categoryRequested("model")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Extensions")
            summary: qsTranslate("SettingsPopup", "Desktop companion")
            onPicked: root.categoryRequested("extensions")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "Account & sync")
            summary: qsTranslate("SettingsPopup", "GitHub sign-in, encrypted cloud copy")
            onPicked: root.categoryRequested("sync")
        }
        SettingsCategoryRow {
            title: qsTranslate("SettingsPopup", "About")
            summary: qsTranslate("SettingsPopup", "Version, updates, repository")
            onPicked: root.categoryRequested("about")
        }
    }
}
