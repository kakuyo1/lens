import QtQuick

/**
 * The fade and lift a surface that draws its own card plays as it arrives and leaves.
 *
 * ShadowCard.qml owns the appearance of every surface built on it. Bubble and SelectionBar draw their own card -- each
 * needs a shadow cast from a shape-only copy, which ShadowCard's layout cannot give them -- so the appearance is written
 * once here: shown, the card fades in while lifting into place; leaving, it fades out and sinks, and the window is
 * hidden when the fade is done.
 *
 * The surface's content binds `opacity` to `progress` and `y`'s offset to `lift`, and disables itself while `shown` is
 * false: a window still on screen and transparent must not take the press that lands where its controls used to be.
 *
 * `progress` is one value driving both halves for the reason ShadowCard.qml gives -- reduced motion sets every duration
 * to zero, so the fade and the movement have to collapse together.
 */
QtObject {
    id: root

    /// The surface's colour and motion table, from its window root.
    required property var tokens
    /// The window to hide once a fade-out has run its course.
    required property var window
    /// Whether the surface is meant to be on screen. Every show and hide goes through this.
    property bool shown: false

    /// Away duration is the pop every surface shares. The bar that stands beside the pointer
    /// leaves on the shorter press token, so it is out of the way before the next selection.
    readonly property int enterDuration: root.tokens.motion.pop
    property int leaveDuration: root.tokens.motion.pop

    /// 0 while the surface is away, 1 while it is up.
    property real progress: root.shown ? 1 : 0
    /// The 8 px the card travels: down while leaving, up while arriving, none when settled.
    readonly property real lift: (1 - root.progress) * 8

    Behavior on progress {
        NumberAnimation {
            duration: root.shown ? root.enterDuration : root.leaveDuration
            easing.type: root.tokens.motion.easing
        }
    }

    // Timed rather than started from the animation's finish: a surface withdrawn before its
    // fade-in ended has no finish to wait for, and its window would stay up, transparent and
    // taking input. A surface brought back before the timer runs stops it.
    readonly property Timer hideTimer: Timer {
        interval: root.leaveDuration
        running: !root.shown
        onTriggered: {
            if (!root.shown)
                root.window.visible = false;
        }
    }
}
