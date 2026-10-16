pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property int themeChoice: page.tokens.themeIndex(Controller.settings.theme)
    property var draftColors: ({})
    readonly property var contrast: page.tokens.validateCustomTheme(draftColors)
    readonly property var openChildRect: themeField.openChildRect
    implicitHeight: content.implicitHeight

    signal childRequested(string value)

    function closeChild() {
        themeField.closeList();
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
            Controller.setTheme(page.tokens.themeName(index));
    }

    function applyCustomTheme() {
        if (page.contrast.valid)
            Controller.setTheme(page.tokens.customThemeValue(page.draftColors));
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
                    {value: 0, label: qsTranslate("SettingsPopup", "Light"), group: "", note: ""},
                    {value: 1, label: qsTranslate("SettingsPopup", "Dark"), group: "", note: ""},
                    {value: 3, label: qsTranslate("SettingsPopup", "Frosted glass"), group: "", note: ""},
                    {value: 4, label: qsTranslate("SettingsPopup", "Liquid glass"), group: "", note: ""},
                    {value: 2, label: qsTranslate("SettingsPopup", "Custom"), group: "", note: ""}
                ]
                currentValue: page.themeChoice
                onPicked: (value) => page.chooseTheme(value)
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
                to: 100
                value: Controller.settings.frostedBlur
                onMoved: (stop) => Controller.setFrostedBlur(stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Saturation")
                suffix: "%"
                to: 200
                value: Controller.settings.frostedSaturation
                onMoved: (stop) => Controller.setFrostedSaturation(stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Rim highlight")
                suffix: "%"
                to: 100
                value: Controller.settings.frostedRim
                onMoved: (stop) => Controller.setFrostedRim(stop)
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
                to: 40
                value: Controller.settings.liquidRefraction
                onMoved: (stop) => Controller.setLiquidRefraction(stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Blur")
                suffix: "%"
                to: 100
                value: Controller.settings.liquidBlur
                onMoved: (stop) => Controller.setLiquidBlur(stop)
            }

            SliderRow {
                label: qsTranslate("SettingsPopup", "Rim highlight")
                suffix: "%"
                to: 100
                value: Controller.settings.liquidRim
                onMoved: (stop) => Controller.setLiquidRim(stop)
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
                        Controller.setLiquidTextColor(text);
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

                Text {
                    anchors.centerIn: parent
                    text: qsTranslate("SettingsPopup", "Apply custom colors")
                    color: page.contrast.valid ? page.tokens.on : page.tokens.faint
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                TapHandler {
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
            onToggled: (on) => Controller.setAnimationsEnabled(on)
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
            suffix: " / 5"
            from: 1
            to: 5
            value: Controller.settings.revealSpeed
            onMoved: (stop) => Controller.setRevealSpeed(stop)
        }

        SwitchRow {
            width: parent.width
            label: qsTranslate("SettingsPopup", "Check for updates at startup")
            checked: Controller.updateDuty.autoCheck
            onToggled: (on) => Controller.updateDuty.setAutoCheck(on)
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
