pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/// The last 365 local days as a contribution graph, with a hue for its four active levels.
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
    // The weekday column is as wide as its 11 px labels; with the grid gap the 53 columns of
    // 4 px cells then fill the card's content width exactly.
    readonly property int weekdayWidth: 22
    readonly property int gridGap: 2
    readonly property int cellGap: 2
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

    /// @brief Select the day at @p index in the year, clamped to the data.
    function selectDayAt(index) {
        if (year.days.length === 0)
            return;
        const bounded = Math.max(0, Math.min(year.days.length - 1, index));
        year.selectedDate = year.days[bounded].date;
    }

    /// @brief Move the selected day by @p delta days, starting from today when none is selected.
    function moveSelectedDay(delta) {
        let index = year.days.length - 1;
        if (year.selectedDate !== "") {
            const found = year.days.findIndex(function (day) { return day.date === year.selectedDate; });
            if (found >= 0)
                index = found;
        }
        year.selectDayAt(index + delta);
    }

    /// @brief Give the grid a selected day when it takes focus, so the first arrow has an origin.
    function beginGridKeyboard() {
        if (year.selectedDate === "")
            year.selectDayAt(year.days.length - 1);
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
                    // The 365-day span is the graph's own caption and rides in the default
                    // label; the day readout replaces it once a cell is selected.
                    text: year.selectedDay
                          ? qsTr("%1: %2 lookups").arg(year.selectedDay.date).arg(year.selectedDay.pops)
                          : qsTr("Lookups · 365 days")
                    color: year.tokens.muted
                    font.pixelSize: 11
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    // Both denominators side by side (ADR 0104): lookups and the deduplicated
                    // vocabulary. Mixed CJK units take the app face (UI-SPEC.md 4.7).
                    text: qsTr("%1 lookups · %2 words")
                          .arg(year.days.reduce(function (sum, day) { return sum + day.pops; }, 0))
                          .arg(Controller.words.length)
                    color: year.tokens.faint
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
                    height: 14

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
                            font.pixelSize: 11
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
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Grid {
                        id: cells
                        objectName: "yearGrid"
                        columns: year.weekCount
                        rows: 7
                        flow: Grid.TopToBottom
                        width: year.cellSize * year.weekCount + year.cellGap * (year.weekCount - 1)
                        height: year.graphHeight
                        columnSpacing: year.cellGap
                        rowSpacing: year.cellGap

                        // One tab stop for the year, not 365: the arrows move the selected day.
                        // The cells keep their names for the screen reader's review pass.
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Table
                        Accessible.name: {
                            const day = year.selectedDay;
                            return day
                                ? qsTr("Year activity. %1, %2 lookups. Use the arrow keys to choose a day.")
                                  .arg(day.date).arg(day.pops)
                                : qsTr("Year activity. Use the arrow keys to choose a day.");
                        }
                        Keys.onPressed: (event) => {
                            let handled = true;
                            if (event.key === Qt.Key_Left)
                                year.moveSelectedDay(-1);
                            else if (event.key === Qt.Key_Right)
                                year.moveSelectedDay(1);
                            else if (event.key === Qt.Key_Up)
                                year.moveSelectedDay(-7);
                            else if (event.key === Qt.Key_Down)
                                year.moveSelectedDay(7);
                            else if (event.key === Qt.Key_PageUp)
                                year.moveSelectedDay(-7);
                            else if (event.key === Qt.Key_PageDown)
                                year.moveSelectedDay(7);
                            else if (event.key === Qt.Key_Home)
                                year.selectDayAt(0);
                            else if (event.key === Qt.Key_End)
                                year.selectDayAt(year.days.length - 1);
                            else
                                handled = false;
                            event.accepted = handled;
                        }
                        onActiveFocusChanged: if (activeFocus) year.beginGridKeyboard()

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
                                Accessible.role: Accessible.Cell
                                // The count is pop events, so the spoken unit is lookups, the same
                                // one the summary row writes (ADR 0104).
                                Accessible.name: visible ? qsTr("%1, %2 lookups").arg(day.date).arg(day.pops) : ""

                                // The focus mark stands outside the chosen cell's 1 px selection
                                // border, so a ring and a selection remain two marks.
                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: -2
                                    visible: parent.visible && cells.activeFocus && year.selectedDate === parent.day.date
                                    radius: 3
                                    color: "transparent"
                                    border.width: 2
                                    border.color: year.tokens.ink
                                }

                                TapHandler {
                                    enabled: parent.visible
                                    onTapped: {
                                        cells.forceActiveFocus();
                                        year.selectedDate = parent.day.date;
                                    }
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

                    Text { text: qsTr("Less"); color: year.tokens.faint; font.pixelSize: 11 }
                    Repeater {
                        model: 5
                        delegate: Item {
                            id: legendCell
                            objectName: "yearLegendCell"
                            required property int index
                            width: 16
                            height: 16

                            activeFocusOnTab: true
                            Accessible.role: Accessible.Button
                            Accessible.name: legendCell.index === 0 ? qsTr("No lookups, choose color") : qsTr("Level %1, choose color").arg(legendCell.index)
                            Accessible.onPressAction: year.toggleLevel(legendCell.index)
                            Keys.onPressed: (event) => {
                                if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Space)
                                        && !event.isAutoRepeat) {
                                    event.accepted = true;
                                    year.toggleLevel(legendCell.index);
                                }
                            }
                            FocusRing { radius: 5 }

                            Rectangle {
                                anchors.centerIn: parent
                                width: 9
                                height: 9
                                radius: 1
                                color: year.cellColor(legendCell.index)
                                border.width: year.selectedLevel === legendCell.index ? 1 : 0
                                border.color: year.tokens.text
                            }
                            TapHandler { onTapped: year.toggleLevel(legendCell.index) }
                        }
                    }
                    Text { text: qsTr("More"); color: year.tokens.faint; font.pixelSize: 11 }
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
                                    // An annulus, so the centre discs read as the card's glass.
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
                                    // The beads' soft rest shadow, painted here so it turns with the ring.
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
                                    objectName: "yearHueCell"
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
                                    activeFocusOnTab: true
                                    Accessible.role: Accessible.Button
                                    Accessible.name: year.hues[index].name
                                    Accessible.onPressAction: year.selectHue(hueCell.index)
                                    Keys.onPressed: (event) => {
                                        if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Space)
                                                && !event.isAutoRepeat) {
                                            event.accepted = true;
                                            year.selectHue(hueCell.index);
                                        }
                                    }
                                    FocusRing { radius: 10.5 }

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
                            font.pixelSize: 11
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
                                font.pixelSize: 11
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }
            }
        }
    }
}
