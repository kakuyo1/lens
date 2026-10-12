import QtQuick

/**
 * The 34x20 pill switch from UI.md 4.4.
 *
 * A disabled switch is dimmed and does not respond. It carries no explanation on the surface:
 * the phase-1 placeholders are meant to read as unavailable, not as a promise.
 */
Item {
    id: root

    property bool checked: false
    property bool interactive: true
    /// The table the switch takes its colours from. The settings window passes its own.
    property TokenTable tokens: Tokens

    signal toggled(bool checked)

    implicitWidth: 34
    implicitHeight: 20
    opacity: interactive ? 1.0 : 0.45

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: root.checked ? root.tokens.ink : root.tokens.line
        Behavior on color {
            ColorAnimation {
                duration: root.tokens.motion.press
                easing.type: root.tokens.motion.easing
            }
        }

        Rectangle {
            id: knob
            width: 16
            height: 16
            radius: 8
            y: 2
            x: root.checked ? track.width - width - 2 : 2
            color: root.checked ? root.tokens.on : (root.tokens.dark ? root.tokens.faint : root.tokens.panel)
            Behavior on x {
                NumberAnimation {
                    duration: root.tokens.motion.press
                    easing.type: root.tokens.motion.easing
                }
            }
        }
    }

    HoverHandler { cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor }

    TapHandler {
        enabled: root.interactive
        onTapped: {
            root.checked = !root.checked;
            root.toggled(root.checked);
        }
    }
}
