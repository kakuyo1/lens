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
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("yearWords")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: year.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

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
        hueIndex = index;
    }

    onVisibleChanged: if (!visible)
        closePalette()

    Shortcut {
        sequence: "Escape"
        onActivated: if (year.paletteOpen) year.closePalette()
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

                Icon {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-back.svg"
                    color: year.tokens.faint
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: year.backRequested() }
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
                    Icon {
                        source: "qrc:/icons/ui-close.svg"
                        color: year.tokens.faint
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: year.visible = false }
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

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 12
                        radius: width / 2
                        color: "transparent"
                        border.width: 1
                        border.color: year.tokens.line
                    }

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

                        Repeater {
                            model: year.hues.length
                            delegate: Rectangle {
                                id: hueCell
                                required property int index
                                readonly property real angle: index * Math.PI / 6
                                readonly property int hue: year.hues[index].hue
                                x: wheel.width / 2 - width / 2 + Math.sin(angle) * 51
                                y: wheel.height / 2 - height / 2 - Math.cos(angle) * 51
                                width: 20
                                height: 20
                                radius: 10
                                color: Qt.hsla(hue / 360, 0.72, 0.52, 1)
                                border.width: year.hueIndex === index ? 2 : 1
                                border.color: year.hueIndex === index ? year.tokens.text : year.tokens.panel
                                Accessible.role: Accessible.Button
                                Accessible.name: year.hues[index].name
                                TapHandler { onTapped: year.selectHue(hueCell.index) }
                            }
                        }
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 48
                        height: 48
                        radius: 24
                        color: year.tokens.panel2
                        border.width: 1
                        border.color: year.tokens.line
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 8
                            text: year.hues[year.hueIndex].name
                            color: year.tokens.muted
                            font.pixelSize: 8
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
