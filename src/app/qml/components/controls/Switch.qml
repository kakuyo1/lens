import QtQuick

/**
 * The 34x20 pill switch from UI-SPEC.md 4.4.
 *
 * A toggle stretches the knob through the travel and settles it back (UI-SPEC.md 4.17). A
 * disabled switch is dimmed and does not respond. It carries no explanation on the surface:
 * the phase-1 placeholders are meant to read as unavailable, not as a promise.
 */
Item {
    id: root

    property bool checked: false
    property bool interactive: true
    /// What the switch says about itself to the keyboard and the screen reader. The row that
    /// owns it names it: the switch alone is a shape with no words.
    property string accessibleName: ""
    /// The table the switch takes its colours from: the one of the window it sits in.
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    signal toggled(bool checked)

    /// @brief Flip the switch the way a press, a key and a screen reader all do.
    function flip() {
        if (!root.interactive)
            return;
        root.checked = !root.checked;
        root.toggled(root.checked);
    }

    implicitWidth: 34
    implicitHeight: 20
    opacity: interactive ? 1.0 : 0.45
    scale: press.pressed ? 0.97 : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: root.tokens.motion.press
            easing.type: root.tokens.motion.easing
        }
    }

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
            transform: Scale {
                id: knobStretch
                origin.x: knob.width / 2
                origin.y: knob.height / 2
                xScale: 1
            }
            Behavior on x {
                NumberAnimation {
                    duration: root.tokens.motion.press
                    easing.type: root.tokens.motion.easing
                    onRunningChanged: if (running) stretch.restart()
                }
            }

            /// @brief The travel reads as weight: the knob pulls out to 1.4x toward the side it is
            ///        heading for and settles back as it lands, all inside the press token
            ///        (UI-SPEC.md 4.17).
            SequentialAnimation {
                id: stretch
                NumberAnimation {
                    target: knobStretch
                    property: "xScale"
                    to: 1.4
                    duration: root.tokens.motion.press * 0.45
                    easing.type: root.tokens.motion.easing
                }
                NumberAnimation {
                    target: knobStretch
                    property: "xScale"
                    to: 1.0
                    duration: root.tokens.motion.press * 0.55
                    easing.type: root.tokens.motion.easing
                }
            }
        }
    }

    activeFocusOnTab: interactive
    Accessible.role: Accessible.CheckBox
    Accessible.name: root.accessibleName
    Accessible.checked: root.checked
    Accessible.onToggleAction: root.flip()
    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Space || event.key === Qt.Key_Enter || event.key === Qt.Key_Return)
                && !event.isAutoRepeat) {
            event.accepted = true;
            root.flip();
        }
    }

    FocusRing { radius: root.height / 2 + 4 }

    HoverHandler { cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor }

    TapHandler {
        id: press
        enabled: root.interactive
        onTapped: root.flip()
    }
}
