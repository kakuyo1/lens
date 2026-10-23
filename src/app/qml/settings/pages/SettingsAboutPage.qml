pragma ComponentBehavior: Bound

import QtQuick
import "../../util/Format.js" as Format

/**
 * The about page: the repository, the version with its update check, and the build stamp.
 *
 * The update check moved here from the General page. Its startup switch stayed there, because the
 * switch is a setting and the check is an action.
 */
Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

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

    /// The blocks, the 24 px the prototype leaves under them, and the footer. This is the whole
    /// page when it stands alone, as a case builds it; the panel stretches it to the body it loads
    /// it into, and the footer -- anchored to the page's bottom -- drops to the body's own bottom.
    implicitHeight: content.implicitHeight + 24 + footer.height

    Column {
        id: content
        width: parent.width
        spacing: 16

        Rectangle {
            id: repoRow
            width: parent.width
            height: 44
            radius: page.tokens.radiusField
            color: repoHover.hovered ? page.tokens.panel2 : "transparent"

            Icon {
                id: githubIcon
                anchors.left: parent.left
                anchors.leftMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                width: 15
                height: 15
                source: "qrc:/icons/ui-github.svg"
                color: page.tokens.muted
            }

            Text {
                anchors.left: githubIcon.right
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("SettingsPopup", "GitHub repository")
                color: page.tokens.text
                font.pixelSize: 13
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                text: "github.com/kakuyo1/lens"
                color: page.tokens.faint
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
            radius: page.tokens.radiusGroup
            color: page.tokens.panel2
            border.width: 1
            border.color: page.tokens.line

            // The block is as tall as what it shows and centred in the row, so the version sits in the
            // middle while the line is empty. A line that shows adds its height beneath the version, and
            // the block re-centres: the version rises to make room for it.
            Item {
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - checkButton.width - 36
                height: versionText.height + (updateLine.visible ? updateLine.height + 4 : 0)

                Text {
                    id: versionText
                    width: parent.width
                    text: qsTranslate("SettingsPopup", "Current version %1").arg(Controller.updateDuty.update.current)
                    color: page.tokens.text
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }

                Text {
                    id: updateLine
                    objectName: "updateStatusLine"
                    y: versionText.height + 4
                    width: parent.width
                    text: page.updateStatusText(Controller.updateDuty.update.state)
                    visible: text !== ""
                    color: page.tokens.muted
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
                radius: page.tokens.radiusPill
                color: page.tokens.ink
                opacity: page.updatesBusy ? 0.58 : 1
                scale: checkTap.pressed ? 0.97 : 1.0
                Behavior on scale {
                    NumberAnimation {
                        duration: page.tokens.motion.press
                        easing.type: page.tokens.motion.easing
                    }
                }

                Text {
                    id: checkLabel
                    anchors.centerIn: parent
                    text: qsTranslate("SettingsPopup", "Check for updates")
                    color: page.updatesBusy ? page.tokens.faint : page.tokens.on
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

        /// Pictures are downloaded again on the next open, so this is the one cache the page may
        /// drop. The explanation cache is paid data and the review's source; it is not offered here.
        Item {
            id: cacheRow
            objectName: "pictureCacheRow"
            width: parent.width
            height: 44

            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("SettingsPopup", "Picture cache")
                color: page.tokens.text
                font.pixelSize: 13
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8

                Text {
                    objectName: "pictureCacheSize"
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    text: Format.formatBytes(Controller.pictureDuty.cacheBytes)
                    color: page.tokens.faint
                    font.pixelSize: 11
                }

                Text {
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    text: "·"
                    color: page.tokens.faint
                    font.pixelSize: 11
                }

                Chip {
                    objectName: "clearPictureCacheButton"
                    label: qsTranslate("SettingsPopup", "Clear")
                    onPicked: Controller.pictureDuty.clearCache()
                }
            }
        }
    }

    // The footer is the page's last line, centred, and it hangs off the page's own bottom edge:
    // the panel loads the page into the body and stretches it to fill, so the stamp reads as the
    // foot of the panel rather than as one more block under the version. Column positions its
    // children, so each line takes the full width and centres its own text.
    Column {
        id: footer
        objectName: "aboutFooter"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 4

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: "Lens"
            color: page.tokens.text
            font.pixelSize: 14
            font.weight: Font.Bold
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTranslate("SettingsPopup", "Copyright (c) 2026 kakuyo1 · MIT License")
            color: page.tokens.faint
            font.pixelSize: 11
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTranslate("SettingsPopup", "Built %1").arg(Controller.buildDate)
            color: page.tokens.faint
            font.pixelSize: 11
        }
    }
}
