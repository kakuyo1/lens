pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The last 365 local days of word lookups, shown as a compact contribution graph.
 *
 * The controller owns the date projection. This surface only maps it to seven rows and lets the
 * reader choose the hue used by the four non-empty levels.
 */
Window {
    id: year
    objectName: "yearWords"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("yearWords")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: year.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    /// Set by the panel that replaces this one: the card fades out in place, and the window goes when it has.
    property bool leaving: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 380
    readonly property int weekCount: 53
    readonly property int weekdayWidth: 15
    readonly property int gridGap: 4
    readonly property int cellGap: 1
    readonly property int cellSize: Math.max(3, Math.floor((cardWidth - 40 - weekdayWidth - gridGap - (weekCount - 1) * cellGap) / weekCount))
    readonly property int graphHeight: cellSize * 7 + cellGap * 6
    readonly property var days: Controller.yearDays
    readonly property int dataCellCount: days.length
    readonly property int gridCellCount: weekCount * 7
    readonly property var hues: [
        {name: qsTr("Red"), hue: 0}, {name: qsTr("Orange"), hue: 30},
        {name: qsTr("Yellow"), hue: 60}, {name: qsTr("Yellow green"), hue: 90},
        {name: qsTr("Green"), hue: 120}, {name: qsTr("Teal"), hue: 150},
        {name: qsTr("Cyan"), hue: 180}, {name: qsTr("Blue"), hue: 210},
        {name: qsTr("Indigo"), hue: 240}, {name: qsTr("Violet"), hue: 270},
        {name: qsTr("Magenta"), hue: 300}, {name: qsTr("Rose"), hue: 330}
    ]

    property bool paletteOpen: false
    property int hueIndex: 4
    /// The ring's turn, in degrees: the selected hue is brought to 90° clockwise (3 o'clock). A click
    /// takes the shortest way from wherever the last one left the ring.
    property real wheelRotation: 90 - 30 * hueIndex
    property int selectedLevel: 0
    property string selectedDate: ""
    readonly property var selectedDay: year.days.find(function (day) { return day.date === year.selectedDate; }) || null
    readonly property bool paletteReady: paletteLoader.status === Loader.Ready

    /// How far the palette has unrolled, 0 shut and 1 open. The card grows and shrinks with it, and the
    /// graph above rides along with the top edge, so opening the palette glides rather than jumping.
    property real paletteProgress: paletteOpen ? 1 : 0
    Behavior on paletteProgress {
        NumberAnimation {
            duration: year.tokens.motion.pop
            easing.type: year.tokens.motion.easing
        }
    }

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    signal backRequested()

    function dayAt(index) {
        return index >= 0 && index < days.length ? days[index] : null;
    }

    function leadingDays() {
        const first = dayAt(0);
        if (!first)
            return 0;
        return (new Date(first.date + "T00:00:00").getDay() + 6) % 7;
    }

    function dayLevel(pops) {
        return pops === 0 ? 0 : pops === 1 ? 1 : pops <= 3 ? 2 : pops <= 5 ? 3 : 4;
    }

    function cellColor(level) {
        if (level === 0)
            return year.tokens.panel2;
        const lightness = year.tokens.dark ? [0.29, 0.40, 0.51, 0.63][level - 1]
                                     : [0.84, 0.64, 0.46, 0.30][level - 1];
        return Qt.hsla(hues[hueIndex].hue / 360, 0.62, lightness, 1);
    }

    function monthLabel(column) {
        const start = leadingDays();
        for (let row = 0; row < 7; ++row) {
            const day = dayAt(column * 7 + row - start);
            if (day && day.date.slice(8, 10) === "01")
                return day.date.slice(5, 7);
        }
        return "";
    }

    function openPalette(level) {
        selectedLevel = level;
        paletteOpen = true;
    }

    /// A legend cell opens the palette at its level, and the level that is already open shuts it again:
    /// the palette has no close button of its own, the card's title row has the one close.
    function toggleLevel(level) {
        if (paletteOpen && selectedLevel === level)
            closePalette();
        else
            openPalette(level);
    }

    function closePalette() {
        paletteOpen = false;
    }

    function selectHue(index) {
        // Computed before hueIndex moves: wheelRotation's initial binding reads it.
        const target = 90 - 30 * index;
        wheelRotation += ((target - wheelRotation) % 360 + 540) % 360 - 180;
        hueIndex = index;
    }

    onVisibleChanged: if (!visible)
        closePalette()

    /// Escape answers the panel as a whole: the palette goes first, the year card after it. The
    /// application-wide scope is the one the removal question's Shortcut explains.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: year.visible
        onActivated: year.paletteOpen ? year.closePalette() : year.visible = false
    }

    ShadowCard {
        id: card
        anchors.fill: parent
        movable: true
        color: year.tokens.glassMode === "" ? year.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: year.glass
            mode: year.tokens.glassMode
            radius: card.radius
        }

        Column {
            id: column
            x: 20
            y: 18
            width: year.cardWidth - 40
            spacing: 0

            Item {
                width: parent.width
                height: 22

                IconButton {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-back.svg"
                    color: year.tokens.faint
                    accessibleName: qsTr("Back")
                    onTapped: year.backRequested()
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Year in words")
                    color: year.tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 4

                    Text {
                        text: qsTr("365 days")
                        color: year.tokens.faint
                        font.family: year.tokens.monoFamily
                        font.pixelSize: 10
                    }
                    IconButton {
                        source: "qrc:/icons/ui-close.svg"
                        color: year.tokens.faint
                        accessibleName: qsTr("Close")
                        onTapped: year.visible = false
                    }
                }
            }

            Item { width: 1; height: 10 }

            Item {
                width: parent.width
                height: 20

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: year.selectedDay
                          ? qsTr("%1: %2 lookups").arg(year.selectedDay.date).arg(year.selectedDay.pops)
                          : qsTr("Words explained")
                    color: year.tokens.muted
                    font.pixelSize: 11
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%1 words").arg(year.days.reduce(function (sum, day) { return sum + day.pops; }, 0))
                    color: year.tokens.faint
                    font.family: year.tokens.monoFamily
                    font.pixelSize: 11
                }
            }

            Item { width: 1; height: 8 }

            Item {
                id: graph
                width: parent.width
                height: monthLabels.height + gridRow.height + legend.height + 7

                Item {
                    id: monthLabels
                    width: parent.width
                    height: 12

                    Repeater {
                        model: year.weekCount
                        delegate: Text {
                            objectName: label.length > 0 ? "yearMonthLabel" : ""
                            required property int index
                            readonly property string label: year.monthLabel(index)
                            x: year.weekdayWidth + year.gridGap + index * (year.cellSize + year.cellGap)
                            width: 18
                            visible: label.length > 0
                            text: label
                            color: year.tokens.faint
                            font.family: year.tokens.monoFamily
                            font.pixelSize: 8
                        }
                    }
                }

                Row {
                    id: gridRow
                    y: monthLabels.height
                    spacing: year.gridGap

                    Item {
                        width: year.weekdayWidth
                        height: year.graphHeight

                        Repeater {
                            model: 7
                            delegate: Text {
                                required property int index
                                y: index * (year.cellSize + year.cellGap) - 1
                                width: year.weekdayWidth
                                height: year.cellSize + 2
                                visible: index === 1 || index === 3 || index === 5
                                text: index === 1 ? qsTr("Mon") : index === 3 ? qsTr("Wed") : qsTr("Fri")
                                color: year.tokens.faint
                                font.pixelSize: 8
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Grid {
                        id: cells
                        columns: year.weekCount
                        rows: 7
                        flow: Grid.TopToBottom
                        width: year.cellSize * year.weekCount + year.cellGap * (year.weekCount - 1)
                        height: year.graphHeight
                        columnSpacing: year.cellGap
                        rowSpacing: year.cellGap

                        Repeater {
                            model: year.weekCount * 7
                            delegate: Rectangle {
                                required property int index
                                readonly property int dayIndex: index - year.leadingDays()
                                readonly property var day: year.dayAt(dayIndex)
                                width: year.cellSize
                                height: year.cellSize
                                radius: 1
                                visible: day !== null
                                color: visible ? year.cellColor(year.dayLevel(day.pops)) : "transparent"
                                border.width: visible && year.selectedDate === day.date ? 1 : 0
                                border.color: year.tokens.text
                                Accessible.ignored: !visible
                                Accessible.name: visible ? qsTr("%1, %2 words").arg(day.date).arg(day.pops) : ""

                                TapHandler {
                                    enabled: parent.visible
                                    onTapped: year.selectedDate = parent.day.date
                                }
                            }
                        }
                    }
                }

                Row {
                    id: legend
                    anchors.right: parent.right
                    y: gridRow.y + gridRow.height + 7
                    spacing: 3

                    Text { text: qsTr("Less"); color: year.tokens.faint; font.pixelSize: 8 }
                    Repeater {
                        model: 5
                        delegate: Rectangle {
                            id: legendCell
                            required property int index
                            width: 9
                            height: 9
                            radius: 1
                            color: year.cellColor(index)
                            border.width: year.selectedLevel === index ? 1 : 0
                            border.color: year.tokens.text
                            Accessible.role: Accessible.Button
                            Accessible.name: legendCell.index === 0 ? qsTr("No words, choose color") : qsTr("Level %1, choose color").arg(legendCell.index)
                            TapHandler { onTapped: year.toggleLevel(legendCell.index) }
                        }
                    }
                    Text { text: qsTr("More"); color: year.tokens.faint; font.pixelSize: 8 }
                }
            }

            Loader {
                id: paletteLoader
                width: parent.width
                // Live while it unrolls and while it rolls back up, so the close animates as well as the open.
                active: year.paletteOpen || year.paletteProgress > 0
                height: year.paletteProgress * implicitHeight
                clip: true
                sourceComponent: paletteComponent
            }
        }
    }

    GlassHost {
        window: year
        sheet: year.glass
        dragging: card.dragging
    }

    Component {
        id: paletteComponent

        Item {
            implicitHeight: paletteBody.y + paletteBody.height
            width: year.cardWidth - 40

            // The rule and the title sit inside the palette, not in the column, so they unroll with it.
            Rectangle {
                y: 10
                width: parent.width
                height: 1
                color: year.tokens.line2
            }

            Text {
                objectName: "yearPaletteTitle"
                y: 20
                text: qsTr("Cell colors")
                color: year.tokens.text
                font.pixelSize: 11
                font.weight: Font.Bold
            }

            Item {
                id: paletteBody
                y: 40
                width: parent.width
                height: wheel.height

                Item {
                    id: wheel
                    objectName: "yearHueWheel"
                    width: 124
                    height: 124

                    Item {
                        id: orbit
                        anchors.fill: parent

                        RotationAnimation {
                            target: orbit
                            property: "rotation"
                            from: -20
                            to: 0
                            duration: year.tokens.motion.pop
                            easing.type: year.tokens.motion.easing
                            running: year.tokens.motion.pop > 0
                        }

                        // The ring and its dots turn together; the disc at the centre stays put, so the
                        // name needs no counter-rotation.
                        Item {
                            id: spin
                            anchors.fill: parent
                            rotation: year.wheelRotation
                            Behavior on rotation {
                                NumberAnimation {
                                    duration: year.tokens.motion.pop
                                    easing.type: year.tokens.motion.easing
                                }
                            }

                            // The colour disc is inset 12 px, as the prototype's is; the beads below straddle
                            // its rim, which is what makes a flower of them rather than a pie chart.
                            Canvas {
                                anchors.fill: parent
                                onPaint: {
                                    const ctx = getContext("2d");
                                    const cx = spin.width / 2;
                                    const cy = spin.height / 2;
                                    const radius = spin.width / 2 - 12;
                                    const colors = ["#ef4444", "#f97316", "#eab308", "#84cc16", "#22c55e", "#14b8a6",
                                                    "#06b6d4", "#3b82f6", "#6366f1", "#8b5cf6", "#d946ef", "#ec4899"];
                                    const step = Math.PI / 6;
                                    // A hair of overlap hides the anti-aliasing seam between neighbouring wedges.
                                    const overlap = 0.008;
                                    // An annulus, not a disc: nothing is painted under the middle, so the centre
                                    // discs over it read as the card's glass rather than as a tinted colour.
                                    const inner = 27;
                                    for (let index = 0; index < colors.length; ++index) {
                                        const from = -Math.PI / 2 + index * step - overlap;
                                        ctx.beginPath();
                                        ctx.arc(cx, cy, radius, from, from + step + 2 * overlap);
                                        ctx.arc(cx, cy, inner, from + step + 2 * overlap, from, true);
                                        ctx.closePath();
                                        ctx.fillStyle = colors[index];
                                        ctx.fill();
                                    }
                                    // The beads' rest shadow, one pixel down and soft, as the prototype's
                                    // box-shadow draws it. Painted here because it turns with their ring.
                                    for (let index = 0; index < colors.length; ++index) {
                                        const bx = cx + Math.sin(index * step) * 51;
                                        const by = cy - Math.cos(index * step) * 51 + 1;
                                        const shadow = ctx.createRadialGradient(bx, by, 8, bx, by, 14);
                                        shadow.addColorStop(0, "rgba(20,20,26,0.24)");
                                        shadow.addColorStop(1, "rgba(20,20,26,0)");
                                        ctx.beginPath();
                                        ctx.arc(bx, by, 14, 0, 2 * Math.PI);
                                        ctx.fillStyle = shadow;
                                        ctx.fill();
                                    }
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 12
                                radius: width / 2
                                color: "transparent"
                                border.width: 1
                                border.color: Qt.rgba(1, 1, 1, 0.24)
                            }

                            Repeater {
                                model: year.hues.length
                                delegate: Rectangle {
                                    id: hueCell
                                    required property int index
                                    readonly property real angle: index * Math.PI / 6
                                    readonly property int hue: year.hues[index].hue
                                    readonly property bool active: year.hueIndex === index || hueHover.hovered
                                    x: wheel.width / 2 - width / 2 + Math.sin(angle) * 51
                                    y: wheel.height / 2 - height / 2 - Math.cos(angle) * 51
                                    width: 21
                                    height: 21
                                    radius: 10.5
                                    color: Qt.hsla(hue / 360, 0.70, 0.52, 1)
                                    border.width: 2
                                    border.color: year.tokens.panel
                                    scale: active ? 1.12 : 1
                                    Accessible.role: Accessible.Button
                                    Accessible.name: year.hues[index].name

                                    Behavior on scale {
                                        NumberAnimation {
                                            duration: year.tokens.motion.press
                                            easing.type: year.tokens.motion.easing
                                        }
                                    }
                                    HoverHandler {
                                        id: hueHover
                                        cursorShape: Qt.PointingHandCursor
                                    }
                                    TapHandler { onTapped: year.selectHue(hueCell.index) }

                                    // The hover and selected mark: a ring outside the dot, as the prototype draws it.
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: parent.width + 4
                                        height: parent.height + 4
                                        radius: width / 2
                                        color: "transparent"
                                        border.width: 2
                                        border.color: year.tokens.text
                                        opacity: hueCell.active ? 1 : 0
                                        Accessible.ignored: true
                                        Behavior on opacity {
                                            NumberAnimation {
                                                duration: year.tokens.motion.press
                                                easing.type: year.tokens.motion.easing
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 56
                        height: 56
                        radius: 28
                        color: year.tokens.panel
                        border.width: 1
                        border.color: year.tokens.line
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 48
                        height: 48
                        radius: 24
                        color: year.tokens.panel2
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 8
                            text: year.hues[year.hueIndex].name
                            color: year.tokens.muted
                            font.pixelSize: 9
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                    }
                }

                Row {
                    id: shades
                    objectName: "yearShadeColumn"
                    x: wheel.width + 12
                    width: paletteBody.width - x
                    height: paletteBody.height
                    spacing: 4

                    Repeater {
                        model: 4
                        delegate: Item {
                            id: shadeCell
                            required property int index
                            width: (shades.width - 3 * shades.spacing) / 4
                            height: paletteBody.height

                            // The swatch is centred on the wheel's horizontal midline; its caption hangs below it.
                            Rectangle {
                                id: swatch
                                objectName: "yearShadeSwatch"
                                y: (shadeCell.height - height) / 2
                                width: parent.width
                                height: 18
                                radius: 3
                                color: year.cellColor(shadeCell.index + 1)
                                border.width: year.selectedLevel === shadeCell.index + 1 ? 1 : 0
                                border.color: year.tokens.text
                            }
                            Text {
                                y: swatch.y + swatch.height + 4
                                width: parent.width
                                text: shadeCell.index === 0 ? qsTr("Light") : shadeCell.index === 3 ? qsTr("Dark") : String(shadeCell.index + 1)
                                color: year.tokens.faint
                                font.pixelSize: 8
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }
        }
    }
}
