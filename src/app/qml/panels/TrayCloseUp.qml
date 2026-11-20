import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * @file TrayCloseUp.qml
 * @brief The onboarding's close-up of the tray corner and its menu (UI-SPEC.md 4.19, ADR 0185).
 *
 * A drawing, not a surface: the tray icon belongs to the shell, cannot be scaled, and may sit in
 * Windows 11's overflow, so the lesson lives inside the card. The card itself is the desktop;
 * only the taskbar strip and the drawn menu stand on it.
 */
Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    /// Whether the drawn menu is out. It stays out once the tour ends.
    property bool open: false
    /// The row the tour points at: "state", "settings", or empty.
    property string highlight: ""

    height: 144
    Accessible.ignored: true

    readonly property bool hasKey: Controller.settings.hasApiKey === true
    readonly property bool budgetExhausted: Controller.costDuty.stats.budgetExhausted === true
    readonly property string busyLabel: Controller.busyLabel
    readonly property bool capturing: Controller.settings.selectionCapture
    /// The live state line in the tray menu's own priority order (TrayMenu.qml,
    /// tray_state.h): a missing key outranks the budget, which outranks the busy line.
    readonly property color stateMark: !hasKey || budgetExhausted ? root.tokens.warn
        : (busyLabel !== "" || capturing) ? root.tokens.ok : root.tokens.faint
    readonly property string stateLabel: !hasKey
        ? qsTranslate("TrayMenu", "No API key yet")
        : budgetExhausted ? qsTranslate("TrayMenu", "Today's budget is used up")
        : busyLabel !== "" ? busyLabel
        : capturing ? Controller.modeLabel
        : qsTranslate("TrayMenu", "Selection capture is off")
    readonly property string languageName: {
        const languages = Controller.settings.languages || [];
        for (let i = 0; i < languages.length; ++i)
            if (languages[i].value === Controller.settings.uiLanguage)
                return languages[i].label;
        return Controller.settings.uiLanguage;
    }
    /// A card inside a card: the menu's translucent popup tone under glass, its
    /// own panel tone otherwise (DESIGN.md 3.1c / 3.1d).
    readonly property color menuFill: root.tokens.glassMode === "" ? root.tokens.panel
                                                                     : root.tokens.popup
    /// The taskbar strip is a step past the card's own fill, so the drawing still
    /// reads as a taskbar corner without a frame around it.
    readonly property color barFill: root.tokens.glassMode === "" ? root.tokens.panel2
                                                                    : root.tokens.popup

    /// @brief Start the one-shot tour: the icon's cue, the menu, then the two rows.
    function play() {
        run.restart();
    }
    /// @brief Stop the tour and fold the menu away.
    function reset() {
        stop();
        open = false;
    }
    /// @brief Stop the tour where it is; the menu stays as it is drawn.
    function stop() {
        run.stop();
        cueAnim.stop();
        fillTimer.stop();
        trayFill.opacity = 0;
        trayIcon.scale = 1;
        trayRing.scale = 0.55;
        trayRing.opacity = 0;
        highlight = "";
    }
    function playCue() {
        trayFill.opacity = 1;
        fillTimer.restart();
        cueAnim.restart();
    }

    Item {
        anchors.fill: parent
        clip: true

        // The menu is drawn where the tray menu would unfold: above the corner,
        // inset from the panel's edges. It starts a few pixels low and rises with
        // the pop token, as the real menu does (UI-SPEC.md 4.17).
        Item {
            id: miniMenu
            x: parent.width - 10 - width
            y: parent.height - 30 - 4 - height + (root.open ? 0 : 6)
            width: 192
            height: 106
            opacity: root.open ? 1 : 0

            Behavior on opacity {
                NumberAnimation {
                    duration: root.tokens.motion.pop
                    easing.type: root.tokens.motion.easing
                }
            }
            Behavior on y {
                NumberAnimation {
                    duration: root.tokens.motion.pop
                    easing.type: root.tokens.motion.easing
                }
            }

            Rectangle {
                id: menuShadowShape
                anchors.fill: menuBackground
                radius: 9
                color: root.menuFill
                visible: false
            }

            MultiEffect {
                source: menuShadowShape
                shadowEnabled: true
                shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, root.tokens.dark ? 0.6 : 0.18)
                shadowBlur: 0.6
                shadowVerticalOffset: 3
                blurMax: 16
            }

            Rectangle {
                id: menuBackground
                anchors.fill: parent
                radius: 9
                color: root.menuFill
                border.width: 1
                border.color: root.tokens.line
            }

            Column {
                x: 4
                y: 4
                width: parent.width - 8

                // The state line: the product mark carries the live state, and
                // the label is the tray menu's own wording, translated once there.
                Item {
                    width: parent.width
                    height: 26

                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 1
                        anchors.bottomMargin: 1
                        radius: 6
                        color: root.highlight === "state" ? root.tokens.panel2 : "transparent"
                    }

                    Icon {
                        id: stateMarkIcon
                        x: 7
                        anchors.verticalCenter: parent.verticalCenter
                        width: 11
                        height: 11
                        source: "qrc:/icons/lens.svg"
                        color: root.stateMark
                    }

                    Text {
                        anchors.left: stateMarkIcon.right
                        anchors.leftMargin: 6
                        anchors.right: parent.right
                        anchors.rightMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.stateLabel
                        color: root.tokens.text
                        font.pixelSize: 11
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: root.tokens.line2
                    }
                }

                Item {
                    width: parent.width
                    height: 24

                    Icon {
                        id: statsIcon
                        x: 7
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        source: "qrc:/icons/ui-stats.svg"
                        color: root.tokens.muted
                    }

                    Text {
                        anchors.left: statsIcon.right
                        anchors.leftMargin: 6
                        anchors.right: parent.right
                        anchors.rightMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTranslate("TrayMenu", "Statistics")
                        color: root.tokens.text
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }

                Item {
                    width: parent.width
                    height: 24

                    Icon {
                        id: languageIcon
                        x: 7
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        source: "qrc:/icons/ui-language.svg"
                        color: root.tokens.muted
                    }

                    Text {
                        anchors.left: languageIcon.right
                        anchors.leftMargin: 6
                        anchors.right: languageNote.left
                        anchors.rightMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTranslate("TrayMenu", "Language")
                        color: root.tokens.text
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }

                    MixedText {
                        id: languageNote
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        value: root.languageName
                        color: root.tokens.faint
                        pixelSize: 11
                    }
                }

                Item {
                    width: parent.width
                    height: 24

                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 1
                        anchors.bottomMargin: 1
                        radius: 6
                        color: root.highlight === "settings" ? root.tokens.panel2 : "transparent"
                    }

                    Icon {
                        id: settingsIcon
                        x: 7
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        source: "qrc:/icons/ui-settings.svg"
                        color: root.tokens.muted
                    }

                    Text {
                        anchors.left: settingsIcon.right
                        anchors.leftMargin: 6
                        anchors.right: parent.right
                        anchors.rightMargin: 7
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTranslate("TrayMenu", "Settings")
                        color: root.tokens.text
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 30
            color: root.barFill

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: root.tokens.line2
            }

            Text {
                id: mockClock
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: "09:41"
                color: root.tokens.muted
                font.pixelSize: 11
            }

            Item {
                id: miniTray
                anchors.right: mockClock.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28

                Rectangle {
                    id: trayFill
                    anchors.fill: parent
                    radius: 6
                    color: root.tokens.panel2
                    opacity: 0
                }

                Rectangle {
                    id: trayRing
                    anchors.centerIn: parent
                    width: 24
                    height: 24
                    radius: 7
                    color: "transparent"
                    border.width: 1.5
                    border.color: root.tokens.ok
                    opacity: 0
                    scale: 0.55
                    antialiasing: true
                }

                Icon {
                    id: trayIcon
                    anchors.centerIn: parent
                    width: 13
                    height: 13
                    source: "qrc:/icons/lens.svg"
                    color: root.tokens.ok
                }
            }

            Icon {
                anchors.right: miniTray.left
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
                source: "qrc:/icons/ui-chevron-down.svg"
                color: root.tokens.muted
                rotation: 180
            }
        }
    }

    Timer {
        id: fillTimer
        interval: root.tokens.motion.bubble
        onTriggered: trayFill.opacity = 0
    }

    ParallelAnimation {
        id: cueAnim
        SequentialAnimation {
            NumberAnimation {
                target: trayIcon
                property: "scale"
                from: 1
                to: 1.16
                duration: root.tokens.motion.pop / 2
                easing.type: root.tokens.motion.easing
            }
            NumberAnimation {
                target: trayIcon
                property: "scale"
                from: 1.16
                to: 1
                duration: root.tokens.motion.pop / 2
                easing.type: root.tokens.motion.easing
            }
        }
        NumberAnimation {
            target: trayRing
            property: "scale"
            from: 0.55
            to: 1.65
            duration: root.tokens.motion.bubble
            easing.type: root.tokens.motion.easing
        }
        NumberAnimation {
            target: trayRing
            property: "opacity"
            from: 0.9
            to: 0
            duration: root.tokens.motion.bubble
            easing.type: root.tokens.motion.easing
        }
    }

    // The tour: a beat to let the reader see the corner, the icon's catch, the
    // menu rising, then the two rows the note names -- the state line and Settings.
    // Every beat is a multiple of a motion token (UI-SPEC.md 4.17).
    SequentialAnimation {
        id: run
        PauseAnimation { duration: root.tokens.motion.pop * 3 }
        ScriptAction { script: root.playCue() }
        PauseAnimation { duration: root.tokens.motion.bubble }
        ScriptAction { script: root.open = true }
        PauseAnimation { duration: root.tokens.motion.pop * 2 }
        ScriptAction { script: root.highlight = "state" }
        PauseAnimation { duration: root.tokens.motion.bubble * 2 }
        ScriptAction { script: root.highlight = "" }
        PauseAnimation { duration: root.tokens.motion.press }
        ScriptAction { script: root.highlight = "settings" }
        PauseAnimation { duration: root.tokens.motion.bubble * 2 }
        ScriptAction { script: root.highlight = "" }
    }
}

