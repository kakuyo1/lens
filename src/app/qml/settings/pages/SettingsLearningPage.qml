import QtQuick

Item {
    id: root
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property
    property var settings: Controller.settings

    /// The local voice the setting names, as the catalogue spells its id (docs/adr/0135).
    readonly property string localVoiceId: settings.speechLocalEngine === "ljspeech" ? "ljspeech" : "kokoro"
    /// That voice on this machine: `installed`, `downloading`, and `percent` while it downloads.
    readonly property var localVoiceState: Controller.speechDuty.voiceStates[localVoiceId] || ({installed: false, downloading: false, percent: 0})
    readonly property string localVoiceStatus: {
        if (localVoiceState.downloading)
            return qsTranslate("SettingsPopup", "Downloading %1%").arg(localVoiceState.percent);
        if (localVoiceId === "ljspeech" && settings.speechAccent === "uk")
            return qsTranslate("SettingsPopup", "LJSpeech has only an American voice, so this accent stays silent.");
        if (localVoiceState.installed)
            return qsTranslate("SettingsPopup", "Downloaded and verified.");
        return qsTranslate("SettingsPopup", "Not downloaded. The local engine stays silent.");
    }

    readonly property var openChildRect: motherTongueField.openChildRect || levelField.openChildRect || languageField.openChildRect || reviewField.openChildRect
    implicitHeight: content.implicitHeight

    /// The preview the pointer has called up, or "" for none: a row's title raises one on hover
    /// (UI-SPEC.md 4.4). The popup turns it into the window Main puts beside the card.
    signal previewRequested(string key)
    /// The language the explanation-language row under the pointer asks to preview. The popup
    /// draws the same card in that language; "" takes it down.
    signal languagePreviewRequested(string language)
    /// The language the mother-tongue row under the pointer asks to preview. The popup draws the
    /// English card with the peek line in that language; "" takes it down.
    signal motherTonguePreviewRequested(string language)

    function closeChild() {
        motherTongueField.closeList();
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

        // Above the explanation language, because the peek it feeds only exists under an English
        // explanation: the reader reads this as "the language I fall back to", then chooses what
        // everything is explained in (docs/adr/0112). Its rows raise no preview -- the peek is a
        // hold on the reader's own card, and the preview bubble draws no globe.
        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Mother tongue")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: motherTongueField
                objectName: "motherTongueField"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Mother tongue")
                options: root.settings.motherTongueOptions || []
                currentValue: root.settings.motherTongue
                onPicked: (value) => Controller.setSetting("motherTongue", value)
                // The peek line is the one thing this setting changes on the card, and the peek
                // only exists under an English explanation: the row's preview draws the English
                // card with the line in the hovered language (UI-SPEC.md 4.4).
                onOptionHovered: (option) => root.motherTonguePreviewRequested(String(option.value))
                onOptionUnhovered: root.motherTonguePreviewRequested("")
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

        // The seven card switches are one column, but not one undivided list: a quiet group
        // label names what the switches below it decide, without adding a card around them
        // (UI-SPEC 4.4).
        Column {
            width: parent.width
            spacing: 8

            Text {
                text: qsTranslate("SettingsPopup", "Card content")
                color: root.tokens.muted
                font.pixelSize: 11
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
        }

        // The rest of what a word's card can draw. These are display switches: the field comes
        // back with the definition either way, and the row only decides whether the card draws it.
        Column {
            width: parent.width
            spacing: 8

            Text {
                text: qsTranslate("SettingsPopup", "Word details")
                color: root.tokens.muted
                font.pixelSize: 11
            }

            SwitchRow {
                width: parent.width
                objectName: "ipaSwitch"
                label: qsTranslate("SettingsPopup", "IPA")
                checked: root.settings.ipa
                previewKey: "ipa"
                onToggled: (on) => Controller.setSetting("ipa", on)
                onPreviewChanged: (key) => root.previewRequested(key)
            }

            // Beside the pronunciation, where a dictionary prints it: the part of speech is the
            // other half of how a word is named, and its switch reads like the pronunciation's.
            SwitchRow {
                width: parent.width
                objectName: "posSwitch"
                label: qsTranslate("SettingsPopup", "Part of speech")
                checked: root.settings.pos
                previewKey: "pos"
                onToggled: (on) => Controller.setSetting("pos", on)
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
        }

        // Pronunciation (UI-SPEC.md 4.4, docs/adr/0134): a group of its own, because these decide
        // what the card does rather than what it draws. No row here raises a preview: a card that
        // demonstrated one of them would have to synthesise a word on a hover, and a hover that
        // makes a sound is exactly the interruption auto-play is off by default to avoid.
        Column {
            width: parent.width
            spacing: 8

            Text {
                text: qsTranslate("SettingsPopup", "Pronunciation")
                color: root.tokens.muted
                font.pixelSize: 11
            }

            SwitchRow {
                width: parent.width
                objectName: "speechAutoPlaySwitch"
                label: qsTranslate("SettingsPopup", "Auto-play")
                checked: root.settings.speechAutoPlay === true
                onToggled: (on) => Controller.setSetting("speechAutoPlay", on)
            }

            // Two ways of saying an English word, so a two-way switch rather than a list: the
            // reader is picking one of two, not searching a catalogue.
            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTranslate("SettingsPopup", "Accent")
                    color: root.tokens.muted
                    font.pixelSize: 12
                }
                Segment {
                    objectName: "speechAccentField"
                    width: parent.width
                    height: 31
                    labels: [qsTranslate("SettingsPopup", "American"), qsTranslate("SettingsPopup", "British")]
                    currentIndex: root.settings.speechAccent === "uk" ? 1 : 0
                    onPicked: (index) => Controller.setSetting("speechAccent", index === 1 ? "uk" : "us")
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTranslate("SettingsPopup", "Engine")
                    color: root.tokens.muted
                    font.pixelSize: 12
                }
                DropdownField {
                    objectName: "speechEngineField"
                    width: parent.width
                    accessibleName: qsTranslate("SettingsPopup", "Engine")
                    options: root.settings.speechEngineOptions || []
                    currentValue: root.settings.speechEngine
                    onPicked: (value) => Controller.setSetting("speechEngine", value)
                }
                // Whose voice the reader is hearing is part of the setting, not a detail: the system
                // synthesiser's quality is whatever their Windows language pack installed, so the
                // page names it -- and says so plainly when there is none (docs/adr/0134).
                Text {
                    width: parent.width
                    visible: root.settings.speechEngine === "system"
                    text: root.settings.speechSystemAvailable
                          ? qsTranslate("SettingsPopup", "Voice: %1").arg(root.settings.speechSystemVoice)
                          : qsTranslate("SettingsPopup", "No system voice is installed for this accent.")
                    color: root.settings.speechSystemAvailable ? root.tokens.muted : root.tokens.faint
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTranslate("SettingsPopup", "Fallback")
                    color: root.tokens.muted
                    font.pixelSize: 12
                }
                DropdownField {
                    objectName: "speechFallbackField"
                    width: parent.width
                    accessibleName: qsTranslate("SettingsPopup", "Fallback")
                    options: root.settings.speechFallbackOptions || []
                    currentValue: root.settings.speechFallback
                    onPicked: (value) => Controller.setSetting("speechFallback", value)
                }
            }

            // The local voice the reader named (docs/adr/0135). A voice is on this machine only once its
            // download has verified every file; until then the engine stays silent and says so here.
            Column {
                width: parent.width
                spacing: 7
                Text {
                    text: qsTranslate("SettingsPopup", "Local voice")
                    color: root.tokens.muted
                    font.pixelSize: 12
                }
                DropdownField {
                    objectName: "speechLocalEngineField"
                    width: parent.width
                    accessibleName: qsTranslate("SettingsPopup", "Local voice")
                    options: root.settings.speechLocalEngineOptions || []
                    currentValue: root.settings.speechLocalEngine
                    onPicked: (value) => Controller.setSetting("speechLocalEngine", value)
                }
                Text {
                    width: parent.width
                    objectName: "speechLocalVoiceState"
                    text: root.localVoiceStatus
                    color: root.localVoiceState.installed ? root.tokens.muted : root.tokens.faint
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
                Row {
                    spacing: 8
                    Chip {
                        objectName: "downloadVoiceButton"
                        visible: !root.localVoiceState.installed && !root.localVoiceState.downloading
                        label: qsTranslate("SettingsPopup", "Download")
                        onPicked: Controller.speechDuty.downloadVoice(root.localVoiceId)
                    }
                    Chip {
                        objectName: "cancelVoiceButton"
                        visible: root.localVoiceState.downloading
                        label: qsTranslate("SettingsPopup", "Cancel")
                        onPicked: Controller.speechDuty.cancelVoiceDownload()
                    }
                    Chip {
                        objectName: "removeVoiceButton"
                        visible: root.localVoiceState.installed && !root.localVoiceState.downloading
                        label: qsTranslate("SettingsPopup", "Remove")
                        onPicked: Controller.speechDuty.removeVoice(root.localVoiceId)
                    }
                }
                Text {
                    width: parent.width
                    text: qsTranslate("SettingsPopup", "Voice files stay on this computer and are not synced.")
                    color: root.tokens.faint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
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
            // What the interval is for: the cadence is the pet's, not a timer the reader sets
            // for themselves, and the dropdown's own rows only say how long between looks.
            Text {
                width: parent.width
                visible: Pet.enabled
                text: qsTranslate("SettingsPopup", "How often the pet raises its sign to ask a word you have looked up.")
                color: root.tokens.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
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
