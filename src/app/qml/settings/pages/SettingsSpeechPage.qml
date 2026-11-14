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

    /// Four dropdowns can be open, and only one at a time: whichever opened last is the one that
    /// took the press. All are closed by the same call, and the panel asks for whichever is down.
    readonly property var openChildRect: engineField.openChildRect || fallbackField.openChildRect
                                         || onlineProviderField.openChildRect || localEngineField.openChildRect
    implicitHeight: content.implicitHeight

    function closeChild() {
        engineField.closeList();
        fallbackField.closeList();
        onlineProviderField.closeList();
        localEngineField.closeList();
    }

    // Pronunciation (UI-SPEC.md 4.4, docs/adr/0134): a page of its own, because these decide
    // what the card does rather than what it draws. No row here raises a preview: a card that
    // demonstrated one of them would have to synthesise a word on a hover, and a hover that
    // makes a sound is exactly the interruption auto-play is off by default to avoid.
    Column {
        id: content
        width: parent.width
        spacing: 16

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
                id: engineField
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
                id: fallbackField
                objectName: "speechFallbackField"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Fallback")
                options: root.settings.speechFallbackOptions || []
                currentValue: root.settings.speechFallback
                onPicked: (value) => Controller.setSetting("speechFallback", value)
            }
        }

        // Shown only when an online recording can be asked for at all. Only the word leaves the machine, and only
        // to the source picked here (docs/adr/0138).
        Column {
            width: parent.width
            spacing: 7
            visible: root.settings.speechEngine === "online" || root.settings.speechFallback === "online"
            Text {
                text: qsTranslate("SettingsPopup", "Online source")
                color: root.tokens.muted
                font.pixelSize: 12
            }
            DropdownField {
                id: onlineProviderField
                objectName: "speechOnlineProviderField"
                width: parent.width
                accessibleName: qsTranslate("SettingsPopup", "Online source")
                options: root.settings.speechOnlineProviderOptions || []
                currentValue: root.settings.speechOnlineProvider
                onPicked: (value) => Controller.setSetting("speechOnlineProvider", value)
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Only the word is sent, to the source you pick.")
                color: root.tokens.faint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
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
                id: localEngineField
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
}
