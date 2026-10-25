import QtQuick

/**
 * A slider over whole-number stops, drawn in the switch's colours. Dragging or tapping snaps to the nearest stop:
 * every setting it drives is a whole number, a percentage or a count of pixels. A non-interactive slider
 * is dimmed and does not respond. The owner binds `value` and takes the new stop from `moved`.
 */
Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property int from: 1
    property int to: 4
    property int value: from
    property bool interactive: true
    /// What the slider says about itself: the row's label plus the value in force.
    property string accessibleName: ""

    readonly property real knobSize: 16
    readonly property real travel: width - knobSize
    readonly property real ratio: to > from ? (value - from) / (to - from) : 0

    signal moved(int value)

    implicitWidth: 160
    implicitHeight: 20
    opacity: interactive ? 1.0 : 0.45

    activeFocusOnTab: interactive
    Accessible.role: Accessible.Slider
    Accessible.name: {
        const named = root.accessibleName !== "" ? root.accessibleName : qsTr("Slider");
        return named + ", " + root.value;
    }
    Accessible.onPressAction: root.focus = true

    /// @brief Whether @p key is one this slider steps on.
    function handlesKey(key) {
        return [Qt.Key_Left, Qt.Key_Down, Qt.Key_Right, Qt.Key_Up,
                Qt.Key_PageDown, Qt.Key_PageUp, Qt.Key_Home, Qt.Key_End].indexOf(key) >= 0;
    }

    /// @brief The stop @p key moves @p current to, clamped to the row's range.
    function nextValue(key, current) {
        let next = current;
        if (key === Qt.Key_Left || key === Qt.Key_Down)
            next -= 1;
        else if (key === Qt.Key_Right || key === Qt.Key_Up)
            next += 1;
        else if (key === Qt.Key_PageDown)
            next -= Math.max(1, Math.round((root.to - root.from) / 10));
        else if (key === Qt.Key_PageUp)
            next += Math.max(1, Math.round((root.to - root.from) / 10));
        else if (key === Qt.Key_Home)
            next = root.from;
        else if (key === Qt.Key_End)
            next = root.to;
        return Math.max(root.from, Math.min(root.to, next));
    }

    Keys.onPressed: (event) => {
        if (!root.interactive || !root.handlesKey(event.key)) {
            event.accepted = false;
            return;
        }
        event.accepted = true;
        const next = root.nextValue(event.key, root.value);
        if (next !== root.value)
            root.moved(next);
    }

    FocusRing { radius: 6 }

    function stopAt(x) {
        const clamped = Math.max(0, Math.min(1, (x - knobSize / 2) / travel));
        return from + Math.round(clamped * (to - from));
    }

    Rectangle {
        x: root.knobSize / 2
        width: root.travel
        height: 4
        anchors.verticalCenter: parent.verticalCenter
        radius: 2
        color: root.tokens.line
    }

    Rectangle {
        x: root.knobSize / 2
        width: root.travel * root.ratio
        height: 4
        anchors.verticalCenter: parent.verticalCenter
        radius: 2
        color: root.tokens.ink
    }

    Rectangle {
        width: root.knobSize
        height: root.knobSize
        radius: root.knobSize / 2
        y: (root.height - height) / 2
        x: root.travel * root.ratio
        color: root.tokens.ink
        border.width: 2
        border.color: root.tokens.panel
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.interactive
        cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
        // Keeps the press here: the settings card drags its window from an ancestor handler, and that
        // handler would otherwise take the grab from the slider mid-drag.
        preventStealing: true

        onPressed: (mouse) => root.moveTo(mouse.x)
        onPositionChanged: (mouse) => {
            if (pressed)
                root.moveTo(mouse.x);
        }
    }

    function moveTo(x) {
        const stop = stopAt(x);
        if (stop !== value)
            moved(stop);
    }
}
