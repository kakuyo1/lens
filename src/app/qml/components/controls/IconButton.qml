import QtQuick

/**
 * A lone glyph that acts as a button: the cross on a surface's header, the chevron that goes
 * back, and the marks above a list.
 *
 * Icon.qml draws the glyph; this adds what makes it a control -- the pointing cursor and the
 * press scale UI-SPEC.md's motion table gives every button.
 */
Icon {
    id: root

    signal tapped()

    scale: press.pressed ? 0.97 : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: root.tokens.motion.press
            easing.type: root.tokens.motion.easing
        }
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler {
        id: press
        onTapped: root.tapped()
    }
}
