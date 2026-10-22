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
    /// The language the explanation-language row under the pointer asks to preview. The popup
    /// draws the same card in that language; "" takes it down.
    signal languagePreviewRequested(string language)

    function closeChild() {
        levelField.closeList();
        languageField.closeList();
        reviewField.closeList();
    }

    /// The word cloud a hovered vocabulary level raises beside the list. The option carries its
    /// own level, so the preview needs nothing from this page but the row.
    Component {
        id: vocabPreviewComponent
        VocabPreview {}
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
                objectName: "levelField"
                width: parent.width
                options: root.settings.levels
                currentValue: root.settings.level
                optionPreview: vocabPreviewComponent
                onPicked: (value) => Controller.setSetting("level", value)
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Explanation language")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: languageField
                objectName: "languageField"
                width: parent.width
                options: root.settings.languages || []
                currentValue: root.settings.explanationLang
                onPicked: (value) => Controller.setSetting("explanationLang", value)
                // The card is the panel's own preview bubble; a row just tells it which language
                // to write the sample in. The title is not a trigger (UI.md 4.4).
                onOptionHovered: (option) => root.languagePreviewRequested(String(option.value))
                onOptionUnhovered: root.languagePreviewRequested("")
            }
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Multiple senses")
            checked: root.settings.multiSense
            previewKey: "multisense"
            onToggled: (on) => Controller.setSetting("multiSense", on)
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
            onToggled: (on) => Controller.setSetting("etymology", on)
            onPreviewChanged: (key) => root.previewRequested(key)
        }

        SwitchRow {
            width: parent.width
            objectName: "exampleSwitch"
            label: qsTranslate("SettingsPopup", "Example sentence")
            checked: root.settings.example
            previewKey: "example"
            onToggled: (on) => Controller.setSetting("example", on)
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
