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
    /// (UI-SPEC.md 4.4). The popup turns it into the window Main puts beside the card.
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
                accessibleName: qsTranslate("SettingsPopup", "Vocabulary level")
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
                accessibleName: qsTranslate("SettingsPopup", "Explanation language")
                options: root.settings.languages || []
                currentValue: root.settings.explanationLang
                onPicked: (value) => Controller.setSetting("explanationLang", value)
                // The card is the panel's own preview bubble; a row just tells it which language
                // to write the sample in. The title is not a trigger (UI-SPEC.md 4.4).
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

        // The rest of what a word's card can draw. These are display switches: the field comes
        // back with the definition either way, and the row only decides whether the card draws it.
        SwitchRow {
            width: parent.width
            objectName: "ipaSwitch"
            label: qsTranslate("SettingsPopup", "IPA")
            checked: root.settings.ipa
            previewKey: "ipa"
            onToggled: (on) => Controller.setSetting("ipa", on)
            onPreviewChanged: (key) => root.previewRequested(key)
        }

        SwitchRow {
            width: parent.width
            objectName: "signpostSwitch"
            label: qsTranslate("SettingsPopup", "Signpost")
            checked: root.settings.signpost
            previewKey: "signpost"
            onToggled: (on) => Controller.setSetting("signpost", on)
            onPreviewChanged: (key) => root.previewRequested(key)
        }

        SwitchRow {
            width: parent.width
            objectName: "collocationsSwitch"
            label: qsTranslate("SettingsPopup", "Collocations")
            checked: root.settings.collocations
            previewKey: "collocations"
            onToggled: (on) => Controller.setSetting("collocations", on)
            onPreviewChanged: (key) => root.previewRequested(key)
        }

        // No preview: what this one restores is a picture strip that would have to run a real
        // image search to say anything, and a hover is not the place to start one.
        SwitchRow {
            width: parent.width
            objectName: "picturesSwitch"
            label: qsTranslate("SettingsPopup", "Pictures")
            checked: root.settings.pictures
            onToggled: (on) => Controller.setSetting("pictures", on)
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
                accessibleName: qsTranslate("SettingsPopup", "Word review")
                enabled: Pet.enabled
                opacity: Pet.enabled ? 1.0 : 0.45
                options: root.settings.reviewFrequencyOptions
                currentValue: root.settings.reviewFrequency
                onPicked: (value) => Controller.setReviewFrequency(value)
            }
            // The reason the row is dimmed, in the OCR trigger group's manner: the switch that
            // gates it lives on another page, so the cause has to be said here.
            Text {
                width: parent.width
                visible: !Pet.enabled
                text: qsTranslate("SettingsPopup", "Unavailable while the pet is off.")
                color: root.tokens.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }
}
