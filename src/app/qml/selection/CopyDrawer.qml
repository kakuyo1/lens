pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes

/**
 * The copy text's drawer (UI-SPEC.md 4.10): the actions this selection can take, out while the pointer
 * is on the copy text or on the drawer, and closed 200 ms after both are left. It draws inside the bar's
 * own window, so the body places it by the copy item's box (`anchorX`..`anchorH`) and `side` picks the
 * edge it opens from. `form` picks the shape. Its size comes from its labels, and `overhangIn()` says how
 * much window the bar must keep for it.
 */
Item {
    id: root

    /// "bar", "tab", "hand", "tag" or "fan": the bar's form, which picks the shape.
    required property string form
    /// The actions that apply, as the bar lists them. Empty hides the drawer.
    required property var items
    /// "up" or "down" for the vertical forms, "right" for the fan; empty until the bar is placed.
    required property string side
    /// Whether the pointer is on the copy text.
    required property bool copyHovered
    /// Whether the bar is on screen; a drawer that was out closes with it.
    required property bool shown
    required property var tokens
    required property var glassSheet

    /// The copy item's box in this item's parent coordinates. Declared inside the copy item, the
    /// defaults are its own box.
    property real anchorX: 0
    property real anchorY: 0
    property real anchorW: parent.width
    property real anchorH: parent.height

    /// Whether the drawer is out: the hover opens it after 120 ms, a screen reader's press at once.
    property bool open: false

    signal picked(string action)

    readonly property bool applies: items.length > 0 && side !== ""
    readonly property real cardGap: form === "tag" ? 16 : 6
    /// The tag's card is the prototype's 168 px; its items sit right of the grommet column.
    readonly property int tagWidth: 168
    readonly property real trayItemWidth: form === "tag" ? tagWidth - 39 : anchorW - 8

    /// @brief Open at once, for a screen reader's press on the copy text.
    function expand() {
        root.open = true;
    }

    /// Counts the moves of the copy text and of every item above it. Their positions are set by layouts and by
    /// other items' bindings, which mapToItem() does not report; each move ticks this, and overhangIn() reads it.
    property int geomTick: 0

    /// The rotation, in degrees, of an item above the copy text that the drawer hangs under. mapToItem() carries it
    /// into the drawer's place, but nothing signals a rotation as a move, so the body passes it in and overhangIn()
    /// reads it. A body whose drawer is not under a turning item leaves it at 0.
    property real tilt: 0

    /// The copy text and each item above it, up to the window's content.
    readonly property var ancestors: {
        const chain = [];
        for (let item = root.parent; item; item = item.parent)
            chain.push(item);
        return chain;
    }

    Instantiator {
        model: root.ancestors
        delegate: Connections {
            required property var modelData
            target: modelData
            function onXChanged() { root.geomTick++; }
            function onYChanged() { root.geomTick++; }
        }
    }

    /// @return How far the drawer reaches past each edge of @p frame, the body it is drawn beside, in px. Both sides
    ///         a vertical drawer can open to count, so the bar sizes its window before it picks the side; a zero
    ///         means the drawer lies inside that edge.
    function overhangIn(frame) {
        // mapToItem() is not a binding dependency, so the moves and the tilt it depends on are read here.
        root.geomTick;
        root.tilt;
        const origin = root.parent.mapToItem(frame, 0, 0);
        const left = origin.x + root.x;
        const top = origin.y + Math.min(root.yUp, root.yDown);
        const bottom = origin.y + Math.max(root.yUp, root.yDown) + root.height;
        return {
            top: Math.max(0, -top),
            right: Math.max(0, left + root.width - frame.width),
            bottom: Math.max(0, bottom - frame.height),
            left: Math.max(0, -left)
        };
    }

    onShownChanged: {
        if (!shown) {
            openTimer.stop();
            closeTimer.stop();
            open = false;
        }
    }

    /// One action of the drawer: its icon and label, with the press and the reader's name. The label sits beside
    /// the icon in a row, or under it in a column when `vertical`; a `card` is the hand's own card, 84 by 64.
    component Act: Item {
        id: act

        required property var modelData
        property bool vertical: false
        property bool card: false
        /// Whether the action lights up under the pointer. The fan's blades light up as one shape instead.
        property bool hoverable: true
        property int labelPixels: 13
        /// The width the label column gives up to the item's edges, in total; the prototype's padding for the form.
        property int labelInset: 10

        signal pressed(string action)

        implicitWidth: act.card ? 84 : labelRow.implicitWidth + 18
        implicitHeight: act.card ? 64 : act.vertical ? column.implicitHeight + (act.hoverable ? 14 : 0)
                        : labelRow.implicitHeight + 14

        Accessible.role: Accessible.Button
        Accessible.name: act.modelData.label
        Accessible.onPressAction: act.pressed(act.modelData.action)

        Rectangle {
            anchors.fill: parent
            visible: act.card || act.hoverable
            radius: act.card ? root.tokens.radiusGroup : root.tokens.radiusField
            color: act.card ? (root.tokens.glassMode !== "" ? "transparent" : hover.hovered ? root.tokens.panel : root.tokens.panel2)
                 : hover.hovered ? root.tokens.panel2 : "transparent"
            border.width: act.card ? 1 : 0
            border.color: root.tokens.line
        }

        Row {
            id: labelRow
            visible: !act.vertical
            anchors.centerIn: parent
            spacing: 7

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                width: 15
                height: 15
                source: act.modelData.source
                color: root.tokens.muted
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: act.modelData.label
                textFormat: Text.PlainText
                color: root.tokens.text
                font.pixelSize: act.labelPixels
                font.weight: Font.Bold
            }
        }

        Column {
            id: column
            visible: act.vertical
            anchors.centerIn: parent
            width: parent.width - act.labelInset
            spacing: 5

            Icon {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 15
                height: 15
                source: act.modelData.source
                color: root.tokens.muted
            }
            Text {
                width: parent.width
                text: act.modelData.label
                textFormat: Text.PlainText
                color: root.tokens.text
                font.pixelSize: act.labelPixels
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
                // Breaks at a space, or inside a word only when one word is wider than the card: Japanese has no spaces.
                wrapMode: Text.Wrap
                // A fixed 1.2 line: a proportional lineHeight multiplies the font's own line, which is taller.
                lineHeightMode: Text.FixedHeight
                lineHeight: Math.round(act.labelPixels * 1.2)
            }
        }

        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            onTapped: act.pressed(act.modelData.action)
        }
    }

    width: form === "hand" ? items.length * 84
         : form === "fan" ? 140
         : form === "tab" ? anchorW
         : form === "tag" ? tagWidth
         : trayRow.width + 8
    height: form === "hand" ? 64
          : form === "fan" ? 140
          : form === "tab" ? trayColumn.height + 8
          : form === "tag" ? trayColumn.height + 12
          : trayRow.height + 8

    x: form === "bar" ? anchorX + anchorW - width
     : form === "hand" ? anchorX + (anchorW - width) / 2
     : form === "fan" ? anchorX + anchorW - 10
     : anchorX
    /// The two places a vertical drawer can take, above the copy item and below it. The fan has one.
    readonly property real yUp: form === "fan" ? anchorY + anchorH - 83 - height : anchorY - height - cardGap
    readonly property real yDown: form === "fan" ? yUp : anchorY + anchorH + cardGap
    y: side === "up" ? yUp : yDown

    visible: applies
    enabled: open
    opacity: open ? 1 : 0
    Behavior on opacity {
        NumberAnimation {
            duration: root.tokens.motion.pop
            easing.type: root.tokens.motion.easing
        }
    }

    /// The pointer is on the copy text or on the drawer. Leaving both closes it after 200 ms; a
    /// gap between the two does not, because the drawer takes the pointer again before that.
    readonly property bool pointerOn: root.copyHovered || pointer.hovered
    onPointerOnChanged: {
        if (pointerOn) {
            closeTimer.stop();
            if (!open)
                openTimer.restart();
        } else {
            openTimer.stop();
            closeTimer.restart();
        }
    }

    Timer {
        id: openTimer
        interval: 120
        onTriggered: root.open = true
    }

    Timer {
        id: closeTimer
        interval: 200
        onTriggered: root.open = false
    }

    HoverHandler {
        id: pointer
    }

    Rectangle {
        id: card
        visible: root.form !== "hand" && root.form !== "fan"
        anchors.fill: parent
        radius: root.form === "bar" ? root.tokens.radiusGroup : root.tokens.radiusMenu
        color: root.tokens.glassMode === "" ? root.tokens.panel : "transparent"
        border.width: 1
        // Under glass the rim is the only edge (DESIGN.md 3.1c).
        border.color: root.tokens.glassMode === "" ? root.tokens.line : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: root.glassSheet
            mode: root.tokens.glassMode
            radius: card.radius
        }
    }

    // The tag's string runs from the card to the copy text, and its grommet sits where the string
    // comes in.
    Rectangle {
        visible: root.form === "tag"
        x: 14.25
        y: root.side === "up" ? card.height : -16
        width: 1.5
        height: 16
        color: root.tokens.muted
    }

    Rectangle {
        visible: root.form === "tag"
        x: 8
        y: root.side === "up" ? card.height - 18 : 4
        width: 14
        height: 14
        radius: 7
        color: root.tokens.panel
        border.width: 2
        border.color: root.tokens.line

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            radius: 4
            color: root.tokens.panel2
        }
    }

    Row {
        id: trayRow
        visible: root.form === "bar"
        x: 4
        y: 4
        spacing: 2

        Repeater {
            model: trayRow.visible ? root.items : []

            delegate: Act {
                required property int index

                onPressed: (action) => root.picked(action)
            }
        }
    }

    // The tab's and the tag's items stand one above another, each label wrapped to the width the form gives it.
    Column {
        id: trayColumn
        visible: root.form === "tab" || root.form === "tag"
        x: root.form === "tag" ? 31 : 4
        y: root.form === "tag" ? 6 : 4
        spacing: 2

        Repeater {
            model: trayColumn.visible ? root.items : []

            delegate: Act {
                required property int index

                vertical: true
                labelPixels: 12
                labelInset: root.form === "tag" ? 18 : 10
                width: root.trayItemWidth
                onPressed: (action) => root.picked(action)
            }
        }
    }

    Row {
        id: handRow
        visible: root.form === "hand"

        Repeater {
            model: handRow.visible ? root.items : []

            delegate: Act {
                required property int index

                card: true
                vertical: true
                labelPixels: 12
                onPressed: (action) => root.picked(action)
            }
        }
    }

    // The fan's blades: the two triangles from the hinge to the outer edge, split by the one diagonal, as the
    // prototype draws them. The first item takes the lower blade and the second the upper one; a lone item
    // takes the lower blade. Each blade's action stands at the blade's centroid.
    readonly property var bladePoints: [[[0, 140], [140, 140], [99, 41]], [[0, 140], [99, 41], [0, 0]]]
    readonly property var bladeCentres: [Qt.point(78, 115), Qt.point(36, 54)]

    Repeater {
        model: root.form === "fan" ? root.items : []

        delegate: Item {
            id: blade
            required property var modelData
            required property int index

            readonly property var points: root.bladePoints[blade.index]
            readonly property point centre: root.bladeCentres[blade.index]
            width: 140
            height: 140

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    fillColor: root.tokens.glassMode === "" ? root.tokens.panel2 : "transparent"
                    strokeColor: root.tokens.line
                    strokeWidth: 1
                    startX: blade.points[0][0]
                    startY: blade.points[0][1]

                    PathLine { x: blade.points[1][0]; y: blade.points[1][1] }
                    PathLine { x: blade.points[2][0]; y: blade.points[2][1] }
                    PathLine { x: blade.points[0][0]; y: blade.points[0][1] }
                }
            }

            Act {
                modelData: blade.modelData
                width: 68
                x: blade.centre.x - width / 2
                y: blade.centre.y - height / 2
                vertical: true
                hoverable: false
                labelPixels: 11
                labelInset: 0
                onPressed: (action) => root.picked(action)
            }
        }
    }
}
