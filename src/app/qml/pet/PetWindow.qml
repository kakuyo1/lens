import QtQuick

/**
 * The desktop pet's own window: frameless, on top, never focused, kept out of screen capture. A left press that does
 * not move is a click; one that moves is a pickup whose release samples the pointer so a throw can fly and a still
 * release can drop the pet where it landed; and a right press asks for the settings panel.
 *
 * Hover is deliberately not a MouseArea here: the window stays the size of the sprite, and PetController polls the
 * cursor to move the eyes and play an idle reaction (UI.md 4.14, ADR 0066).
 */
Window {
    id: petWindow
    readonly property SurfaceTokens tokens: SurfaceTokens {}

    readonly property real cursorSlop: 4
    property point grabCursor
    property point grabAt
    property bool moved: false
    /// The pointer positions of the drag so far, with the time each was taken, for the release velocity. Old
    /// samples are dropped as they go out of the 120 ms window, and the whole list starts again at each pickup.
    property var releaseSamples: []
    /// The reader's motion answer, Windows' and the application's together. The ceremony lives in C++, so the window
    /// hands it the same token every surface animates by.
    readonly property bool reducedMotion: tokens.motion.reduced

    /// The reader's right-click on the pet. Where the panel goes is SurfaceHost.qml's business -- beside the pet, rather
    /// than at the tray icon the tray menu's own entry uses -- so this asks for it rather than placing anything itself.
    signal settingsRequested()

    width: Math.round(Pet.canvas * Pet.scale)
    height: Math.round(Pet.canvas * Pet.scale)
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // Not Pet.enabled: a pet being turned off keeps its window up until the goodbye walks it out.
    visible: Pet.shown

    onVisibleChanged: Pet.setRunning(visible)
    onReducedMotionChanged: Pet.setMotionReduced(reducedMotion)

    // A resize grows the window right and down from its top-left, which can take it off the screen: nudge it back,
    // and save where it ended up. Both handlers run; the second finds nothing left to move when the first covered it.
    onWidthChanged: Pet.fitOnScreen()
    onHeightChanged: Pet.fitOnScreen()

    Component.onCompleted: {
        Pet.setMotionReduced(reducedMotion);
        Pet.attachWindow(petWindow);
        place();
    }

    /// Opens at the saved spot, or the corner when that no longer fits.
    function place() {
        const at = Pet.initialPosition(Qt.size(width, height));
        x = at.x;
        y = at.y;
    }

    PetScene {
        anchors.fill: parent
    }

    // The right button's own area, so the left button's press/drag/pickup/click is untouched: a right press is never a
    // click and never starts a pickup. It accepts the right button and nothing else.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton

        onPressed: petWindow.settingsRequested()
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton

        onPressed: {
            Pet.stopWandering();
            petWindow.grabCursor = Pet.cursorPos();
            petWindow.grabAt = Qt.point(petWindow.x, petWindow.y);
            petWindow.moved = false;
            petWindow.releaseSamples = [];
        }

        onPositionChanged: {
            if (!pressed)
                return;
            const cursor = Pet.cursorPos();
            const dx = cursor.x - petWindow.grabCursor.x;
            const dy = cursor.y - petWindow.grabCursor.y;
            if (!petWindow.moved) {
                if (Math.abs(dx) + Math.abs(dy) < petWindow.cursorSlop)
                    return;
                petWindow.moved = true;
                Pet.dragStart();
                // The pet may have been flying or walking while the pointer went down: the grab starts from where it
                // is now, not from the press, so catching it does not snap it back. The sample list starts here too,
                // since the press itself was not a throw.
                petWindow.grabCursor = cursor;
                petWindow.grabAt = Qt.point(petWindow.x, petWindow.y);
                petWindow.releaseSamples = [];
                petWindow.addReleaseSample(cursor);
                return;
            }
            petWindow.x = petWindow.grabAt.x + dx;
            petWindow.y = petWindow.grabAt.y + dy;
            petWindow.addReleaseSample(cursor);
        }

        onReleased: {
            if (petWindow.moved) {
                petWindow.drop(petWindow.releaseVelocity());
            } else {
                Pet.click();
            }
        }

        // Another item taking the grab mid-drag ends the drag where the window is, so the state machine is not left
        // holding the pickup.
        onCanceled: {
            if (petWindow.moved)
                petWindow.drop(Qt.point(0, 0));
        }
    }

    /// @param at The pointer position in the same coordinates as the window.
    function addReleaseSample(at) {
        const now = Date.now();
        petWindow.releaseSamples.push({t: now, x: at.x, y: at.y});
        while (petWindow.releaseSamples.length > 1 && now - petWindow.releaseSamples[0].t > 160)
            petWindow.releaseSamples.shift();
    }

    /// @return The pointer's velocity over the last ~100 ms, in DIPs per second. A pointer that has been still for
    ///         a moment releases at zero: stopping before letting go is a drop, not a throw.
    function releaseVelocity() {
        const now = Date.now();
        const cursor = Pet.cursorPos();
        const samples = petWindow.releaseSamples;
        if (samples.length === 0 || now - samples[samples.length - 1].t > 60)
            return Qt.point(0, 0);

        let first = samples[samples.length - 1];
        for (let i = samples.length - 1; i >= 0; --i) {
            if (now - samples[i].t > 120)
                break;
            first = samples[i];
        }
        const span = now - first.t;
        if (span < 16)
            return Qt.point(0, 0);
        return Qt.point((cursor.x - first.x) * 1000 / span, (cursor.y - first.y) * 1000 / span);
    }

    /// Ends the drag. A release the controller takes as a throw is flown from here; otherwise the pet is simply put
    /// down where it is and that spot becomes home.
    function drop(velocity) {
        if (!Pet.release(velocity)) {
            Pet.dragEnd();
            Pet.savePosition(Qt.point(petWindow.x, petWindow.y));
        }
        petWindow.moved = false;
    }
}
