import QtQuick

/**
 * One pill choice in the pet settings: an action, an accessory, or a button in the pet-package dialog. The ink fill marks the one in use,
 * the way the segment control marks its selected item.
 */
Rectangle {
    id: chip

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property string label: ""
    property bool selected: false

    signal picked()

    implicitWidth: caption.implicitWidth + 20
    implicitHeight: 27
    radius: chip.tokens.radiusPill
    color: selected ? chip.tokens.ink : chip.tokens.panel2
    Behavior on color {
        ColorAnimation {
            duration: chip.tokens.motion.press
            easing.type: chip.tokens.motion.easing
        }
    }
    border.width: selected ? 0 : 1
    border.color: chip.tokens.line
    scale: press.pressed ? 0.97 : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: chip.tokens.motion.press
            easing.type: chip.tokens.motion.easing
        }
    }

    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: caption.text
    Accessible.onPressAction: chip.picked()
    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Space)
                && !event.isAutoRepeat) {
            event.accepted = true;
            chip.picked();
        }
    }

    Text {
        id: caption
        anchors.centerIn: parent
        text: chip.label
        // An action name comes from the pet's data, so it reads as plain text (docs/pet-package-format.md, section 7).
        textFormat: Text.PlainText
        color: chip.selected ? chip.tokens.on : chip.tokens.text
        Behavior on color {
            ColorAnimation {
                duration: chip.tokens.motion.press
                easing.type: chip.tokens.motion.easing
            }
        }
        font.pixelSize: 12
        font.weight: chip.selected ? Font.Bold : Font.Normal
    }

    FocusRing { radius: chip.radius + 4 }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler {
        id: press
        onTapped: chip.picked()
    }
}
