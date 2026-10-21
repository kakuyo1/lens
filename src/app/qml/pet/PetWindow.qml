import QtQuick

/**
 * The desktop pet's own window: frameless, on top, never focused, kept out of screen capture. A left press that does
 * not move is a click, one that moves is a pickup followed by a drop that remembers where it landed, and a right
 * press asks for the settings panel.
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

    /// The reader's right-click on the pet. Where the panel goes is SurfaceHost.qml's business -- beside the pet, rather
    /// than at the tray icon the tray menu's own entry uses -- so this asks for it rather than placing anything itself.
    signal settingsRequested()

    width: Math.round(Pet.canvas * Pet.scale)
    height: Math.round(Pet.canvas * Pet.scale)
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: Pet.enabled

    onVisibleChanged: Pet.setRunning(visible)

    // A resize grows the window right and down from its top-left, which can take it off the screen: nudge it back,
    // and save where it ended up. Both handlers run; the second finds nothing left to move when the first covered it.
    onWidthChanged: Pet.fitOnScreen()
    onHeightChanged: Pet.fitOnScreen()

    Component.onCompleted: {
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
            }
            petWindow.x = petWindow.grabAt.x + dx;
            petWindow.y = petWindow.grabAt.y + dy;
        }

        onReleased: {
            if (petWindow.moved) {
                petWindow.drop();
            } else {
                Pet.click();
            }
        }

        // Another item taking the grab mid-drag ends the drag where the window is, so the state machine is not left
        // holding the pickup.
        onCanceled: {
            if (petWindow.moved)
                petWindow.drop();
        }
    }

    function drop() {
        Pet.dragEnd();
        Pet.savePosition(Qt.point(petWindow.x, petWindow.y));
        petWindow.moved = false;
    }
}
