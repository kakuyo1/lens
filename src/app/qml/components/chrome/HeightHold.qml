import QtQuick

/**
 * Holds a surface at the largest height a content transition passes through.
 *
 * The surface binds its height to max(@ref floor, content height) while a transition runs, so the
 * native window takes the largest size the content passes through and stays there until the
 * content arrives. Only a surface whose content grows away from its anchored edge can use this:
 * the bubble's card is pinned at the top, so its verdict row unfolds into space the window already
 * occupies. A bottom-anchored panel would have to move its card inside the window instead, which
 * flickers -- the two panels in docs/adr/0055 keep following their content frame by frame.
 *
 * A transition starts with begin() and ends when the content reaches the target; the target is
 * the caller's, because only it knows which rows the new state will show.
 */
QtObject {
    id: root

    /// The content's height right now, animated by the content itself.
    required property real contentHeight
    /// How long the content takes to get there, from the surface's motion table.
    property int duration: 0

    property bool active: false
    property real floor: 0
    property int checks: 0
    /// The target begin() started towards.
    property real lastTarget: contentHeight

    /// Start a transition from the current content height, or re-base one already running.
    ///
    /// @param targetHeight The content height the transition is heading for; read from
    ///        @ref contentHeight when omitted.
    function begin(targetHeight) {
        lastTarget = targetHeight === undefined ? root.contentHeight : targetHeight;
        // No animation: the content has already jumped, so a hold would only delay the window.
        if (root.duration <= 0) {
            active = false;
            return;
        }
        floor = Math.max(active ? floor : contentHeight, lastTarget);
        active = true;
        checks = 0;
        settle.interval = root.duration + 17;
        settle.restart();
    }

    property Timer settle: Timer {
        id: settle
        // One frame at 60 Hz past the animation, so the content has written its last height.
        interval: root.duration + 17
        onTriggered: {
            // A target the content never reaches would hold the window up forever; quick retries
            // and then the surface keeps whatever height the content did reach. The test is tight
            // because a leftover fraction of a pixel still rounds to a native resize.
            const arrived = Math.abs(root.contentHeight - root.lastTarget) <= 0.01;
            if (!arrived && ++root.checks < 4) {
                interval = 17;
                restart();
            } else {
                root.active = false;
            }
        }
    }
}
