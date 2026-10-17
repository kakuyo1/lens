import QtQuick

Item {
    id: root
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property
    property var settings: Controller.settings

    readonly property var openChildRect: levelField.openChildRect || languageField.openChildRect || reviewField.openChildRect
    implicitHeight: content.implicitHeight

    /// The preview the pointer has called up, or "" for none: a row's title raises one on hover
    /// (UI.md 4.4). The popup turns it into the window Main puts beside the card.
    signal previewRequested(string key)

    function closeChild() {
        levelField.closeList();
        languageField.closeList();
        reviewField.closeList();
    }

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Vocabulary level")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: levelField
                width: parent.width
                options: root.settings.levels
                currentValue: root.settings.level
                onPicked: (value) => Controller.setLevel(value)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            // The explanation language is the one row here whose label is its own item rather than
            // part of a control component, so its preview is raised from here (UI.md 4.4).
            Text {
                text: qsTranslate("SettingsPopup", "Explanation language")
                color: root.tokens.muted
                font.pixelSize: 12
                HoverHandler {
                    onHoveredChanged: root.previewRequested(hovered ? "language" : "")
                }
            }
            DropdownField {
                id: languageField
                objectName: "languageField"
                width: parent.width
                options: root.settings.languages || []
                currentValue: root.settings.explanationLang
                onPicked: (value) => Controller.setExplanationLang(value)
            }
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Multiple senses")
            checked: root.settings.multiSense
            previewKey: "multisense"
            onToggled: (on) => Controller.setMultiSense(on)
            onPreviewChanged: (key) => root.previewRequested(key)
        }

        // Beside "multiple senses" rather than in a category of its own: both decide what a word's
        // explanation is made of, and one is read as a richer version of the other.
        SwitchRow {
            width: parent.width
            objectName: "etymologySwitch"
            label: qsTranslate("SettingsPopup", "Etymology")
            checked: root.settings.etymology
            previewKey: "etymology"
            onToggled: (on) => Controller.setEtymology(on)
            onPreviewChanged: (key) => root.previewRequested(key)
        }

        SwitchRow {
            width: parent.width
            objectName: "exampleSwitch"
            label: qsTranslate("SettingsPopup", "Example sentence")
            checked: root.settings.example
            previewKey: "example"
            onToggled: (on) => Controller.setExample(on)
            onPreviewChanged: (key) => root.previewRequested(key)
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Word review")
                color: Pet.enabled ? root.tokens.muted : root.tokens.faint
                font.pixelSize: 12
            }
            DropdownField {
                id: reviewField

                objectName: "reviewField"
                width: parent.width
                enabled: Pet.enabled
                opacity: Pet.enabled ? 1.0 : 0.45
                options: root.settings.reviewFrequencyOptions
                currentValue: root.settings.reviewFrequency
                onPicked: (value) => Controller.setReviewFrequency(value)
            }
        }
    }
}
