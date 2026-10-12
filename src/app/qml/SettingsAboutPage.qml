pragma ComponentBehavior: Bound

import QtQuick

/**
 * The about page: the repository, the version with its update check, and the build stamp.
 *
 * The update check moved here from the General page. Its startup switch stayed there, because the
 * switch is a setting and the check is an action.
 */
Item {
    id: page

    /**
     * What the update line says, for one state.
     *
     * A function of the state alone, so the line can never carry a transport's own words: the
     * five states below are the whole of what it knows. An unknown state -- and `idle`, which
     * has nothing to say -- reads as nothing rather than as a guess.
     */
    function updateStatusText(state) {
        if (state === "checking")
            return qsTranslate("SettingsPopup", "Checking...");
        if (state === "upToDate")
            return qsTranslate("SettingsPopup", "Up to date (version %1)").arg(Controller.updateDuty.update.current);
        if (state === "available")
            return qsTranslate("SettingsPopup", "Version %1 is available").arg(Controller.updateDuty.update.latest);
        if (state === "offline")
            return qsTranslate("SettingsPopup", "Cannot connect");
        return "";
    }

    /// Whether a check is running, which is the one thing that locks the button.
    function updateBusy(state) {
        return state === "checking";
    }

    readonly property bool updatesBusy: page.updateBusy(Controller.updateDuty.update.state)

    implicitHeight: content.implicitHeight

    Column {
        id: content
        width: parent.width
        spacing: 16

        Rectangle {
            id: repoRow
            width: parent.width
            height: 44
            radius: SettingsTokens.radiusField
            color: repoHover.hovered ? SettingsTokens.panel2 : "transparent"

            Icon {
                id: githubIcon
                anchors.left: parent.left
                anchors.leftMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                width: 15
                height: 15
                source: "qrc:/icons/ui-github.svg"
                color: SettingsTokens.muted
            }

            Text {
                anchors.left: githubIcon.right
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("SettingsPopup", "GitHub repository")
                color: SettingsTokens.text
                font.pixelSize: 13
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                text: "github.com/kakuyo1/lens"
                color: SettingsTokens.faint
                font.pixelSize: 11
            }

            HoverHandler {
                id: repoHover
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                onTapped: Qt.openUrlExternally("https://github.com/kakuyo1/lens")
            }
        }

        /// The version and the button that checks it. The status line sits under the version
        /// because the answer is about the version it names, and it is empty while idle.
        Rectangle {
            width: parent.width
            height: 56
            radius: SettingsTokens.radiusGroup
            color: SettingsTokens.panel2
            border.width: 1
            border.color: SettingsTokens.line

            // A fixed box rather than a Column: a Column drops a hidden line from its height, so the
            // version would move up when a check starts. The line keeps its place while it is empty.
            Item {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - checkButton.width - 36
                height: 39

                Text {
                    width: parent.width
                    text: qsTranslate("SettingsPopup", "Current version %1").arg(Controller.updateDuty.update.current)
                    color: SettingsTokens.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Text {
                    id: updateLine
                    objectName: "updateStatusLine"
                    y: 21
                    width: parent.width
                    text: page.updateStatusText(Controller.updateDuty.update.state)
                    visible: text !== ""
                    color: SettingsTokens.muted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                id: checkButton
                objectName: "checkForUpdatesButton"
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: checkLabel.width + 26
                height: 26
                radius: SettingsTokens.radiusPill
                color: SettingsTokens.ink
                opacity: page.updatesBusy ? 0.58 : 1
                scale: checkTap.pressed ? 0.97 : 1.0
                Behavior on scale {
                    NumberAnimation {
                        duration: SettingsTokens.motion.press
                        easing.type: SettingsTokens.motion.easing
                    }
                }

                Text {
                    id: checkLabel
                    anchors.centerIn: parent
                    text: qsTranslate("SettingsPopup", "Check for updates")
                    color: page.updatesBusy ? SettingsTokens.faint : SettingsTokens.on
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: checkTap
                    enabled: !page.updatesBusy
                    onTapped: Controller.updateDuty.checkForUpdates()
                }
            }
        }

        // The footer is centred text under the block. Column positions its children, so each line
        // takes the full width and centres its own text rather than anchoring to the column.
        // The 8 px inside the wrapper, on top of the column's 16, gives the 24 px the prototype sets.
        Item {
            width: parent.width
            height: footer.height + 8

            Column {
                id: footer
                y: 8
                width: parent.width
                spacing: 4

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: "Lens"
                    color: SettingsTokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTranslate("SettingsPopup", "Copyright (c) 2026 kakuyo1 · MIT License")
                    color: SettingsTokens.faint
                    font.pixelSize: 11
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTranslate("SettingsPopup", "Built %1").arg(Controller.buildDate)
                    color: SettingsTokens.faint
                    font.pixelSize: 11
                }
            }
        }
    }
}
