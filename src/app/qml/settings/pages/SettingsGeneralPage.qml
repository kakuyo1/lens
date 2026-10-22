pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    readonly property var glass: Window.window.glass
    // qmllint enable missing-property

    /// The backdrop refresh tiers, in the order GlassQuality numbers them (docs/adr/0073, docs/adr/0074).
    readonly property var qualityOptions: [
        {value: 0, label: qsTranslate("SettingsPopup", "Automatic"), group: "",
         note: qsTranslate("SettingsPopup", "Follow this screen up to about 60 times a second when it can be read live; otherwise read about 15 times a second.")},
        {value: 1, label: qsTranslate("SettingsPopup", "Battery saver"), group: "",
         note: qsTranslate("SettingsPopup", "Keep the picture still; retake it only when the card appears or moves.")},
        {value: 2, label: qsTranslate("SettingsPopup", "Standard"), group: "",
         note: qsTranslate("SettingsPopup", "About 15 reads a second on the CPU.")},
        {value: 3, label: qsTranslate("SettingsPopup", "Smooth"), group: "",
         note: qsTranslate("SettingsPopup", "Follow this screen's refresh rate when it can be read live.")}
    ]

    /// What the chosen tier is doing on this screen: the option's own note, or why Smooth could not be reached.
    function qualityNote() {
        let note = "";
        for (let i = 0; i < page.qualityOptions.length; ++i)
            if (page.qualityOptions[i].value === Controller.settings.glassQuality)
                note = page.qualityOptions[i].note;
        if ((Controller.settings.glassQuality === 0 || Controller.settings.glassQuality === 3)
                && page.glass && page.glass.duplicationRefused)
            note = qsTranslate("SettingsPopup", "This screen cannot be read live, so the backdrop refreshes about 15 times a second.");
        return note;
    }

    property int themeChoice: page.tokens.themeIndex(Controller.settings.theme)
    property var draftColors: ({})
    readonly property var contrast: page.tokens.validateCustomTheme(draftColors)
    readonly property var openChildRect: themeField.openChildRect || qualityField.openChildRect
    implicitHeight: content.implicitHeight

    signal childRequested(string value)

    /// The preview the pointer has called up, or "" for none: a row's title raises one on hover
    /// (UI.md 4.4). The popup turns it into the window Main puts beside the card.
    signal previewRequested(string key)

    function closeChild() {
        themeField.closeList();
        qualityField.closeList();
    }

    function loadDraft() {
        page.draftColors = page.tokens.colorsForTheme(Controller.settings.theme);
    }

    function updateDraft(role, value) {
        const next = {};
        for (const key in page.draftColors)
            next[key] = page.draftColors[key];
        next[role] = value;
        page.draftColors = next;
    }

    function chooseTheme(index) {
        page.themeChoice = index;
        if (index === 2)
            page.loadDraft();
        else
            Controller.setSetting("theme", page.tokens.themeName(index));
    }

    function applyCustomTheme() {
        if (page.contrast.valid)
            Controller.setSetting("theme", page.tokens.customThemeValue(page.draftColors));
    }

    Component.onCompleted: page.loadDraft()

    Connections {
        target: Controller
        function onSettingsChanged() {
            page.themeChoice = page.tokens.themeIndex(Controller.settings.theme);
            if (page.themeChoice === 2)
                page.loadDraft();
        }
    }

    /// The card a hovered theme row raises beside the list. Every option carries the theme it
    /// stands for, so the preview needs nothing from this page but the option itself.
    Component {
        id: themePreviewComponent
        ThemePreview {}
    }

    /// The card a hovered refresh row raises: the row's own note, which the rows have no room for.
    Component {
        id: notePreviewComponent
        NotePreview {}
    }

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Theme")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            // A dropdown rather than a segment: the Custom path is a mode with an editor of its
            // own, so the row reads better as one closed choice than as four equal cells.
            DropdownField {
                id: themeField
                objectName: "themeField"
                width: parent.width
                options: [
                    {value: 0, label: qsTranslate("SettingsPopup", "Light"), group: "", note: "",
                     previewTheme: "light"},
                    {value: 1, label: qsTranslate("SettingsPopup", "Dark"), group: "", note: "",
                     previewTheme: "dark"},
                    {value: 3, label: qsTranslate("SettingsPopup", "Frosted glass"), group: "", note: "",
                     previewTheme: "frosted"},
                    {value: 4, label: qsTranslate("SettingsPopup", "Liquid glass"), group: "", note: "",
                     previewTheme: "liquid"},
                    // Custom previews the palette the editor below would apply, which is the
                    // theme this row stands for even when it is not the one in force.
                    {value: 2, label: qsTranslate("SettingsPopup", "Custom"), group: "", note: "",
                     previewTheme: page.tokens.customThemeValue(page.draftColors)}
                ]
                currentValue: page.themeChoice
                optionPreview: themePreviewComponent
                onPicked: (value) => page.chooseTheme(value)
            }
        }

        Column {
            id: refreshSection
            width: parent.width
            spacing: 10
            visible: page.themeChoice === 3 || page.themeChoice === 4
            height: visible ? implicitHeight : 0
            // The list is a window of its own, so hiding the section does not put it down by itself.
            onVisibleChanged: {
                if (!visible)
                    qualityField.closeList();
            }

            Text {
                text: qsTranslate("SettingsPopup", "Backdrop refresh")
                color: page.tokens.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            DropdownField {
                id: qualityField
                objectName: "glassQualityField"
                width: parent.width
                options: page.qualityOptions
                currentValue: Controller.settings.glassQuality
                // Each tier's note is a sentence about what it costs on this screen, which no row
                // has room for: it goes into the card the pointer raises beside the list.
                notesInRows: false
                optionPreview: notePreviewComponent
                onPicked: (value) => Controller.setSetting("glassQuality", value)
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: page.tokens.muted
                font.pixelSize: 12
                text: page.qualityNote()
            }
        }

        Column {
            width: parent.width
            spacing: 10
            visible: page.themeChoice === 3
            height: visible ? implicitHeight : 0

            Text {
                text: qsTranslate("SettingsPopup", "Frosted glass parameters")
                color: page.tokens.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Blur")
                suffix: "%"
                to: Controller.settingRange("frostedBlur").high
                value: Controller.settings.frostedBlur
                onMoved: (stop) => Controller.setSetting("frostedBlur", stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Saturation")
                suffix: "%"
                to: Controller.settingRange("frostedSaturation").high
                value: Controller.settings.frostedSaturation
                onMoved: (stop) => Controller.setSetting("frostedSaturation", stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Rim highlight")
                suffix: "%"
                to: Controller.settingRange("frostedRim").high
                value: Controller.settings.frostedRim
                onMoved: (stop) => Controller.setSetting("frostedRim", stop)
            }
        }

        Column {
            width: parent.width
            spacing: 10
            visible: page.themeChoice === 4
            height: visible ? implicitHeight : 0

            Text {
                text: qsTranslate("SettingsPopup", "Liquid glass parameters")
                color: page.tokens.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Refraction")
                suffix: " px"
                to: Controller.settingRange("liquidRefraction").high
                value: Controller.settings.liquidRefraction
                onMoved: (stop) => Controller.setSetting("liquidRefraction", stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Blur")
                suffix: "%"
                to: Controller.settingRange("liquidBlur").high
                value: Controller.settings.liquidBlur
                onMoved: (stop) => Controller.setSetting("liquidBlur", stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Rim highlight")
                suffix: "%"
                to: Controller.settingRange("liquidRim").high
                value: Controller.settings.liquidRim
                onMoved: (stop) => Controller.setSetting("liquidRim", stop)
            }

            Text {
                text: qsTranslate("SettingsPopup", "Text color")
                color: page.tokens.muted
                font.pixelSize: 12
            }

            Rectangle {
                width: parent.width
                height: 35
                radius: page.tokens.radiusField
                color: page.tokens.panel2
                border.width: textColorInput.activeFocus ? 2 : 1
                border.color: textColorInput.activeFocus ? page.tokens.ok : page.tokens.line

                TextInput {
                    id: textColorInput
                    anchors.left: parent.left
                    anchors.leftMargin: 11
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: Controller.settings.liquidTextColor
                    color: page.tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    activeFocusOnTab: true
                    onEditingFinished: {
                        Controller.setSetting("liquidTextColor", text);
                        // Back to the stored colour, so a refused value shows what is in force.
                        text = Qt.binding(() => Controller.settings.liquidTextColor);
                    }
                }
            }
        }

        Column {
            width: parent.width
            spacing: 10
            visible: page.themeChoice === 2
            height: visible ? implicitHeight : 0

            Text {
                text: qsTranslate("SettingsPopup", "Custom semantic colors")
                color: page.tokens.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTranslate("SettingsPopup", "Use six-digit hex colors. Body, muted, and faint text must each pass WCAG AA on the panel.")
                color: page.tokens.muted
                font.pixelSize: 11
            }

            Repeater {
                model: [
                    {role: "bg", label: qsTranslate("SettingsPopup", "Background")},
                    {role: "panel", label: qsTranslate("SettingsPopup", "Panel")},
                    {role: "text", label: qsTranslate("SettingsPopup", "Body text")},
                    {role: "muted", label: qsTranslate("SettingsPopup", "Muted text")},
                    {role: "faint", label: qsTranslate("SettingsPopup", "Faint text")},
                    {role: "ok", label: qsTranslate("SettingsPopup", "Accent")}
                ]

                delegate: Column {
                    id: colorField
                    required property var modelData
                    width: content.width
                    spacing: 5

                    Text {
                        text: colorField.modelData.label
                        color: page.tokens.muted
                        font.pixelSize: 12
                    }

                    Rectangle {
                        width: parent.width
                        height: 35
                        radius: page.tokens.radiusField
                        color: page.tokens.panel2
                        border.width: colorInput.activeFocus ? 2 : 1
                        border.color: colorInput.activeFocus ? page.tokens.ok : page.tokens.line

                        Rectangle {
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: 19
                            height: 19
                            radius: 5
                            color: page.tokens.validHex(page.draftColors[colorField.modelData.role])
                                   ? page.draftColors[colorField.modelData.role] : page.tokens.panel
                            border.width: 1
                            border.color: page.tokens.line
                        }

                        TextInput {
                            id: colorInput
                            anchors.left: parent.left
                            anchors.leftMargin: 37
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.draftColors[colorField.modelData.role] || ""
                            color: page.tokens.text
                            font.pixelSize: 13
                            selectByMouse: true
                            activeFocusOnTab: true
                            onTextEdited: page.updateDraft(colorField.modelData.role, text)
                        }
                    }
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: page.contrast.valid
                      ? qsTranslate("SettingsPopup", "AA contrast passes: body %1, muted %2, faint %3")
                        .arg(page.contrast.bodyRatio.toFixed(2))
                        .arg(page.contrast.mutedRatio.toFixed(2))
                        .arg(page.contrast.faintRatio.toFixed(2))
                      : qsTranslate("SettingsPopup", "AA contrast requires valid colors and a 4.5:1 ratio for body, muted, and faint text.")
                color: page.contrast.valid ? page.tokens.ok : page.tokens.danger
                font.pixelSize: 11
            }

            Rectangle {
                width: parent.width
                height: 35
                radius: page.tokens.radiusPill
                color: page.contrast.valid ? page.tokens.ink : page.tokens.panel2
                opacity: page.contrast.valid ? 1 : 0.58
                scale: applyPress.pressed ? 0.97 : 1.0
                Behavior on scale {
                    NumberAnimation {
                        duration: page.tokens.motion.press
                        easing.type: page.tokens.motion.easing
                    }
                }

                Text {
                    anchors.centerIn: parent
                    text: qsTranslate("SettingsPopup", "Apply custom colors")
                    color: page.contrast.valid ? page.tokens.on : page.tokens.faint
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                TapHandler {
                    id: applyPress
                    enabled: page.contrast.valid
                    onTapped: page.applyCustomTheme()
                }
            }
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Launch at startup")
            checked: Controller.settings.autostart
            onToggled: (on) => Controller.setAutostart(on)
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Animations")
            checked: Controller.settings.animationsEnabled
            onToggled: (on) => Controller.setSetting("animationsEnabled", on)
        }

        // The typing pace the answer is written out at. It only means anything while animations are
        // on -- a reduced-motion card arrives whole and has no pace to set -- so it is shown only
        // while the switch above it is on, and hidden with it rather than left as a control that
        // does nothing. The blur follows this level by a ratio, so a slow speed never widens the
        // tail of shaders (RevealText.qml).
        SliderRow {
            visible: Controller.settings.animationsEnabled
            height: visible ? implicitHeight : 0
            label: qsTranslate("SettingsPopup", "Typing speed")
            suffix: " / " + Controller.settingRange("revealSpeed").high
            from: Controller.settingRange("revealSpeed").low
            to: Controller.settingRange("revealSpeed").high
            value: Controller.settings.revealSpeed
            previewKey: "speed"
            onMoved: (stop) => Controller.setSetting("revealSpeed", stop)
            onPreviewChanged: (key) => page.previewRequested(key)
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Check for updates at startup")
            checked: Controller.updateDuty.autoCheck
            onToggled: (on) => Controller.updateDuty.setAutoCheck(on)
        }

        // Above the account row: the two are the page's only doors, and one is about this
        // machine's own copy while the other is about the copy that leaves it.
        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "Font")
            onPicked: page.childRequested("font")
        }

        SettingsLinkRow {
            label: qsTranslate("SettingsPopup", "Account & sync")
            onPicked: page.childRequested("sync")
        }

        Text {
            width: parent.width
            visible: SystemMotion.reduced
            text: qsTranslate("SettingsPopup", "Animations are disabled by Windows accessibility settings.")
            color: page.tokens.muted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
    }
}
