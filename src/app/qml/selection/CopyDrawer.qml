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
    /// Whether the drawer is on glass: its cards and the fan's blades are glass then, as the bar's own are.
    readonly property bool underGlass: root.tokens.glassMode !== ""
    /// The glass rim, as GlassSurface draws it: the theme's setting in percent of white.
    readonly property color rimColor: Qt.rgba(1, 1, 1, (root.tokens.glassMode === "liquid" ? Controller.settings.liquidRim : Controller.settings.frostedRim) / 100)
    readonly property color hoverTint: Qt.rgba(18 / 255, 24 / 255, 36 / 255, 0.12)
    /// The fan's hinge in the body's own coordinates: the outer corner of its right blade (SelectionBarFanBody). The
    /// drawer's lower blade starts at its bottom-left corner, so the drawer stands with that corner on the hinge.
    readonly property real fanHingeX: 250
    readonly property real fanHingeY: 87
    /// The blade under the pointer, 0 for the lower and 1 for the upper, or -1 off the blades.
    property int hoveredBlade: -1

    /// The blade's fill: a glass blade rests clear and the one under the pointer takes the hover tint; a solid one rests
    /// in the panel and lights to panel2, as the bar's own fan does (UI-SPEC.md 4.10).
    function bladeFill(index) {
        if (root.underGlass)
            return root.hoveredBlade === index ? root.hoverTint : "transparent";
        return root.hoveredBlade === index ? root.tokens.panel2 : root.tokens.panel;
    }
    /// The longest word of each tab label, unwrapped, to measure the drawer's width by: the drawer grows to fit a word
    /// rather than breaking it, and no further, so a long label wraps onto a second line instead of widening the card.
    /// Hidden, and not for a reader.
    Column {
        id: measured
        opacity: 0

        Repeater {
            model: root.form === "tab" ? root.items : []

            delegate: Text {
                required property var modelData

                Accessible.ignored: true
                text: modelData.label.split(" ").reduce(function (longest, word) { return word.length > longest.length ? word : longest; })
                font.pixelSize: 12
                font.weight: Font.Bold
            }
        }
    }
    /// The tab's drawer is the copy item's width, and grows to the longest word's width plus its icon and padding
    /// (15 + 7 + 18) when that is wider, as the prototype's min-width: max-content does for a word.
    readonly property real trayItemWidth: form === "tag" ? tagWidth - 39
                                        : form === "tab" ? Math.max(anchorW - 8, measured.width + 40)
                                        : anchorW - 8

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
        /// Beside the icon, the label takes the width the item leaves over and wraps there. A form whose item has a set
        /// width sets it; a bar item is as wide as its label, so it does not.
        property bool fitLabel: false
        /// The hand's card, counted from the left.
        property int order: 0
        /// A card's place as the drawer opens and shuts and as the pointer passes over it: the prototype's tray-hand values
        /// (UI-SPEC.md 4.10). Shut, a card sits 12 px low at 0.9. Open, the two cards fan out to -6 and 6 degrees; the one
        /// under the pointer straightens, lifts 6 px and grows 3.5%. A lone card stands straight.
        readonly property bool lifted: act.card && hover.hovered && root.open
        property real drop: !act.card ? 0 : !root.open ? 12 : act.lifted ? -6 : root.items.length === 1 ? 0 : 2
        property real turn: !act.card || !root.open || act.lifted || root.items.length === 1 ? 0 : act.order === 0 ? -6 : 6
        property real grow: !act.card ? 1 : !root.open ? 0.9 : act.lifted ? 1.035 : 1
        Behavior on drop {
            NumberAnimation {
                duration: root.tokens.motion.bubble
                easing.type: root.tokens.motion.easing
            }
        }
        Behavior on turn {
            NumberAnimation {
                duration: root.tokens.motion.bubble
                easing.type: root.tokens.motion.easing
            }
        }
        Behavior on grow {
            NumberAnimation {
                duration: root.tokens.motion.bubble
                easing.type: root.tokens.motion.easing
            }
        }
        transform: [
            Translate { y: act.drop },
            Rotation { origin.x: act.width / 2; origin.y: act.height / 2; angle: act.turn },
            Scale { origin.x: act.width / 2; origin.y: act.height / 2; xScale: act.grow; yScale: act.grow }
        ]

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
            color: act.card ? (root.underGlass ? "transparent" : hover.hovered ? root.tokens.panel : root.tokens.panel2)
                 : hover.hovered ? root.tokens.panel2 : "transparent"
            border.width: act.card && !root.underGlass ? 1 : 0
            border.color: root.tokens.line

            // A card under glass is glass, as the bar's own hand is: its rim is the only edge.
            Loader {
                anchors.fill: parent
                active: act.card && root.underGlass
                sourceComponent: GlassSurface {
                    objectName: "drawerCardGlass"
                    anchors.fill: parent
                    sheet: root.glassSheet
                    mode: root.tokens.glassMode
                    radius: root.tokens.radiusGroup
                }
            }
        }

        Row {
            id: labelRow
            visible: !act.vertical
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 9
            spacing: 7

            Icon {
                objectName: "drawerIcon"
                anchors.verticalCenter: parent.verticalCenter
                width: 15
                height: 15
                source: act.modelData.source
                color: root.tokens.muted
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: act.fitLabel ? act.width - 40 : implicitWidth
                text: act.modelData.label
                textFormat: Text.PlainText
                color: root.tokens.text
                font.pixelSize: act.labelPixels
                font.weight: Font.Bold
                wrapMode: Text.Wrap
            }
        }

        Column {
            id: column
            visible: act.vertical
            anchors.centerIn: parent
            width: parent.width - act.labelInset
            spacing: 5

            Icon {
                objectName: "drawerIcon"
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
         : form === "tab" ? trayItemWidth + 8
         : form === "tag" ? tagWidth
         : trayColumn.width + 8
    height: form === "hand" ? 64
          : form === "fan" ? 140
          : form === "tab" ? trayColumn.height + 8
          : form === "tag" ? trayColumn.height + 12
          : trayColumn.height + 8

    x: form === "bar" ? anchorX + anchorW - width
     : form === "hand" ? anchorX + (anchorW - width) / 2
     : form === "fan" ? anchorX + fanHingeX
     : anchorX
    /// The two places a vertical drawer can take, above the copy item and below it. The fan has one, hinged in place.
    readonly property real yUp: form === "fan" ? anchorY + fanHingeY - height : anchorY - height - cardGap
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

    /// The fan's drawer unfolds from its hinge, the bottom-left corner, as it opens.
    transformOrigin: root.form === "fan" ? Item.BottomLeft : Item.Center
    scale: root.form === "fan" && !root.open ? 0.9 : 1
    Behavior on scale {
        NumberAnimation {
            duration: root.tokens.motion.bubble
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
        onPointChanged: root.hoveredBlade = root.form === "fan" ? root.bladeUnder(pointer.point.position) : -1
        onHoveredChanged: {
            if (!pointer.hovered)
                root.hoveredBlade = -1;
        }
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

    // The items of the bar, the tab and the tag stand one above another, each label beside its icon, as the prototype
    // draws them. The bar's items are as wide as the longest label; the tab's and the tag's are the width the form gives,
    // and a label too wide for that wraps beside its icon.
    Column {
        id: trayColumn
        visible: root.form === "bar" || root.form === "tab" || root.form === "tag"
        x: root.form === "tag" ? 31 : 4
        y: root.form === "tag" ? 6 : 4
        spacing: 2

        Repeater {
            model: trayColumn.visible ? root.items : []

            delegate: Act {
                required property int index

                labelPixels: root.form === "bar" ? 13 : 12
                fitLabel: root.form === "tab" || root.form === "tag"
                width: root.form === "bar" ? implicitWidth : root.trayItemWidth
                onPressed: (action) => root.picked(action)
            }
        }
    }

    // The hand's cards overlap by 4 px at each side, as the prototype's tray-hand does, and the hand is centred in the drawer.
    Row {
        id: handRow
        visible: root.form === "hand"
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: -8

        Repeater {
            model: handRow.visible ? root.items : []

            delegate: Act {
                required property int index

                card: true
                vertical: true
                labelPixels: 12
                order: index
                onPressed: (action) => root.picked(action)
            }
        }
    }

    // The fan's blades: the two triangles from the hinge to the outer edge, split by the one diagonal, as the
    // prototype draws them. The first item takes the lower blade and the second the upper one; a lone item
    // takes the lower blade. Each blade's action stands at the blade's centroid.
    readonly property var bladePoints: [[[0, 140], [140, 140], [99, 41]], [[0, 140], [99, 41], [0, 0]]]
    readonly property var bladeCentres: [Qt.point(78, 115), Qt.point(36, 54)]

    /// @return The blade whose triangle holds @p point, 0 for the lower and 1 for the upper, or -1 off the fan.
    function bladeUnder(point) {
        for (let i = 0; i < root.bladePoints.length; ++i) {
            const t = root.bladePoints[i];
            const side = (a, b) => (b[0] - a[0]) * (point.y - a[1]) - (b[1] - a[1]) * (point.x - a[0]);
            const sides = [side(t[0], t[1]), side(t[1], t[2]), side(t[2], t[0])];
            if (sides.every(s => s >= 0) || sides.every(s => s <= 0))
                return i;
        }
        return -1;
    }

    // Under glass the drawn blades are one glass fan, cut from a GlassSurface as the bar's own fan is (fan_drawer.frag).
    // The cut takes the blades the items draw: both for two items, the lower one alone for one.
    Item {
        id: bladeLayer
        anchors.fill: parent
        visible: root.form === "fan" && root.underGlass

        GlassSurface {
            id: bladeGlass
            objectName: "drawerBladeGlass"
            anchors.fill: parent
            sheet: root.glassSheet
            mode: root.tokens.glassMode
            radius: 0
            rim: 0
        }

        ShaderEffectSource {
            id: bladeGlassSource
            anchors.fill: bladeGlass
            sourceItem: bladeGlass
            hideSource: true
            visible: false
            live: true
        }

        ShaderEffect {
            objectName: "drawerBladeCut"
            anchors.fill: parent
            fragmentShader: "qrc:/shaders/fan_drawer.frag.qsb"
            property variant glass: bladeGlassSource
            property vector2d size: Qt.vector2d(width, height)
            property real solo: root.items.length === 1 ? 1 : 0
        }
    }

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
                    fillColor: root.bladeFill(blade.index)
                    strokeColor: root.underGlass ? root.rimColor : root.tokens.line
                    strokeWidth: root.underGlass ? 2 : 1
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
