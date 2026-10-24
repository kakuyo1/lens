import QtQuick

/**
 * A lone glyph that acts as a button: the cross on a surface's header, the chevron that goes
 * back, and the marks above a list.
 *
 * Icon.qml draws the glyph; this adds what makes it a control -- the pointing cursor, the
 * press scale UI-SPEC.md's motion table gives every button, and the keyboard and screen-reader
 * reach: Enter and Space press it, the focus ring marks it, and the caller names it, because a
 * glyph names nothing.
 */
Icon {
    id: root

    /// What the button says about itself to the keyboard and the screen reader. The caller
    /// names it: the glyph is the whole button, so no label exists to read.
    property string accessibleName: ""

    signal tapped()

    scale: press.pressed ? 0.97 : 1.0
    Behavior on scale {
        NumberAnimation {
            duration: root.tokens.motion.press
            easing.type: root.tokens.motion.easing
        }
    }

    activeFocusOnTab: enabled
    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.onPressAction: root.tapped()
    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Space)
                && !event.isAutoRepeat) {
            event.accepted = true;
            root.tapped();
        }
    }

    FocusRing { radius: 8 }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler {
        id: press
        onTapped: root.tapped()
    }
}
