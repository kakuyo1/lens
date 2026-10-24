import QtQuick
import QtQuick.Window

/**
 * The keyboard-focus ring DESIGN.md's button state asks for: a 2 px ink stroke standing 2 px
 * clear of the control it marks, drawn only while that control holds the focus.
 *
 * A child of the control it rings: it fills the parent, steps 4 px out (the 2 px offset plus the
 * stroke), and the caller sets the radius to the control's own corner plus the same 4 px so the
 * two curves stay parallel. A Rectangle caps its radius at half its size, so a pill stays a pill
 * with no case of its own.
 */
Rectangle {
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    anchors.fill: parent
    anchors.margins: -4
    color: "transparent"
    border.width: 2
    border.color: tokens.ink
    visible: parent.activeFocus
}
