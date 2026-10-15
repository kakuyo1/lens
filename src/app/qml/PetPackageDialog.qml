pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window

/**
 * The question asked the first time the reader picks an installed pet package (UI.md 4.12, docs/adr/0043).
 *
 * A modal popup over the settings panel, so the pet does not switch until Enable is pressed. A press on the backdrop
 * does not close it: only Enable, Cancel and Escape do, and Cancel is the default, so Return backs out too. The package's
 * name is the one string the package supplies, and it is shown as plain text, in quotes.
 */
Popup {
    id: root

    /// The package being asked about. The page sets both before it opens the popup.
    property string petId: ""
    property string petName: ""

    /// Enable was pressed. The page confirms the package then.
    signal enableRequested()

    width: 320
    height: body.implicitHeight + 40
    anchors.centerIn: Overlay.overlay
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: 0

    /// Escape is Cancel, and is offered exactly while the question is up. Application-wide, not the popup's own key
    /// handling: a popup takes keys only once its window is active, which the offscreen test platform never reports
    /// (RemovalQuestion.qml has the same reasoning).
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: root.opened
        onActivated: root.close()
    }

    // Built on open for the same reason as the body below.
    background: Loader {
        active: root.opened

        sourceComponent: Rectangle {
            // qmllint disable missing-property
            readonly property var tokens: Window.window.tokens
            // qmllint enable missing-property

            anchors.fill: parent
            radius: tokens.radiusCard
            color: tokens.panel
            border.width: 1
            border.color: tokens.line
        }
    }

    // The body is built on open, when the popup has its window: a Chip reads the window's tokens as it is built, and
    // built any earlier it would read nothing.
    Loader {
        id: body

        x: 20
        y: 20
        width: root.width - 40
        active: root.opened

        sourceComponent: Column {
            id: question

            // qmllint disable missing-property
            readonly property var tokens: Window.window.tokens
            // qmllint enable missing-property

            width: root.width - 40
            spacing: 12
            focus: true

            // Cancel is the default answer, so Return is the same as Cancel.
            Keys.onReturnPressed: root.close()
            Keys.onEnterPressed: root.close()

            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Third-party pet package")
                color: question.tokens.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            Text {
                objectName: "petPackageQuestion"
                width: parent.width
                text: qsTranslate("SettingsPopup", "\"%1\" is a third-party package. Lens has not reviewed it. Enable it only if you trust where it came from.").arg(root.petName)
                textFormat: Text.PlainText
                color: question.tokens.text
                font.pixelSize: 13
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            Item {
                width: parent.width
                height: 27

                Row {
                    anchors.right: parent.right
                    spacing: 8

                    Chip {
                        objectName: "cancelPetPackageButton"
                        label: qsTranslate("SettingsPopup", "Cancel")
                        onPicked: root.close()
                    }

                    Chip {
                        objectName: "enablePetPackageButton"
                        label: qsTranslate("SettingsPopup", "Enable")
                        selected: true
                        onPicked: {
                            root.enableRequested();
                            root.close();
                        }
                    }
                }
            }
        }
    }
}
