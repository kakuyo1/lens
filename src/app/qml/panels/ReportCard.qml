pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import QtQuick.Window

/**
 * This week's report card (UI-SPEC.md section 4.7c, docs/adr/0151): the figures, the hours they fall on, and the
 * interpretation the reader asked for, as one card centred on a scrim that covers the screen the reading was asked on.
 *
 * A window of its own rather than a card inside the profile panel, because the scrim has to cover the screen and the
 * panel is 280 px wide. Application-modal for as long as it is up: a press on the scrim reaches nothing behind it, and
 * only the card's own controls and Escape take it down. The card is never taller than the screen less a 24 px scrim
 * margin above and below; content that does not fit scrolls inside it.
 */
Window {
    id: report
    objectName: "report"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("report")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: report.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    /// Set by Close, Done and Escape: the card fades out in place, and the window goes when it has.
    property bool leaving: false
    /// Fixed for the window's life: changing modality while it is shown leaves Qt's modal list out of step (RemovalQuestion.qml).
    modality: Qt.ApplicationModal

    readonly property int shadowMargin: 26
    /// The card's own width: 640 px, narrower on a screen that cannot hold it with a 24 px scrim margin each side.
    readonly property int cardWidth: Math.max(320, Math.min(640, width - 48))
    /// The card's visible height: its content, or the screen less a 24 px scrim margin above and below when that is shorter.
    readonly property real cardHeight: Math.min(column.implicitHeight + 52, report.height - 48)

    /// Read each time the card comes up, so the figures are the week's as of the request.
    readonly property var facts: visible ? Controller.readingProfileDuty.facts : ({})
    readonly property var duty: Controller.readingProfileDuty
    readonly property var hours: facts.hours || []
    readonly property int peakCount: {
        let peak = 1;
        for (const count of hours)
            peak = Math.max(peak, count);
        return peak;
    }
    readonly property int peakHour: Math.max(0, hours.indexOf(peakCount))
    readonly property string peakLabel: String(peakHour).padStart(2, "0") + ":00"
    readonly property bool hasLookups: facts.pops > 0
    readonly property string rangeLine: facts.from
                                        ? qsTr("%1 – %2").arg(dayLabel(facts.from)).arg(dayLabel(facts.to))
                                        : ""

    function dayLabel(iso) {
        const parts = iso.split("-");
        return Qt.formatDate(new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2])), "MMM d");
    }

    /// Which page the card shows: the week's report, or the story written from it (UI-SPEC.md 4.7d).
    property string page: "report"
    /// The story and its failure, as the duty holds them for this week.
    readonly property string story: report.duty.story
    readonly property string storyFailure: report.duty.storyFailure

    function openStory() {
        report.duty.tellStory();
        report.page = "story";
    }

    function backToReport() {
        report.page = "report";
    }

    function retryStory() {
        report.duty.tellStory();
    }

    /// @return The figure a point rests on, read from this week's own counts; empty for a key the panel does not draw.
    function figureFor(fact) {
        if (fact === "freshShare")
            return qsTr("New %1 of %2").arg(report.facts.fresh || 0).arg(report.facts.words || 0);
        if (fact === "repeated")
            return qsTr("Repeated %1").arg(report.facts.repeated || 0);
        if (fact === "peakHour")
            return qsTr("Peak at %1").arg(report.peakLabel);
        if (fact === "topWords") {
            const top = report.facts.top || [];
            const pairs = [];
            for (let i = 0; i < top.length && i < 2; ++i)
                pairs.push(top[i].lemma + " " + top[i].lookups);
            return pairs.join(" · ");
        }
        if (fact === "partsOfSpeech") {
            const part = (report.facts.partsOfSpeech || [])[0];
            return part ? part.part + " " + part.count : "";
        }
        return "";
    }

    /// @return The words of one line of the story, in order; the empty runs between spaces are dropped.
    function wordsOf(line) {
        const words = [];
        const parts = line.split(" ");
        for (let i = 0; i < parts.length; ++i) {
            if (parts[i] !== "")
                words.push(parts[i]);
        }
        return words;
    }

    /// @return Whether a word, without the punctuation around it, is one of the words the story was written from (docs/adr/0152).
    function isMarked(token) {
        let start = 0;
        let end = token.length;
        while (start < end && token.charAt(start).toLowerCase() === token.charAt(start).toUpperCase())
            ++start;
        while (end > start && token.charAt(end - 1).toLowerCase() === token.charAt(end - 1).toUpperCase())
            --end;
        const bare = token.substring(start, end).toLowerCase();
        const listed = report.duty.storyWords;
        for (let i = 0; i < listed.length; ++i) {
            if (bare !== "" && listed[i].toLowerCase() === bare)
                return true;
        }
        return false;
    }

    /// Close, Done and Escape all come here. The window stays up for the fade, and ShadowCard takes it down after.
    function dismiss() {
        if (!report.visible || report.leaving)
            return;
        report.leaving = true;
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: report.visible && !report.leaving
        onActivated: report.dismiss()
    }

    /// The scrim fades with the card, so the screen darkens as the card lands rather than before it.
    Rectangle {
        anchors.fill: parent
        color: report.tokens.scrim
        opacity: card.opacity
    }

    Item {
        anchors.fill: parent

        TapHandler {
            acceptedButtons: Qt.BackButton
            enabled: report.page === "story"
            onTapped: report.backToReport()
        }
    }

    ShadowCard {
        id: card
        anchors.centerIn: parent
        width: report.cardWidth + 2 * report.shadowMargin
        height: report.cardHeight + 2 * report.shadowMargin
        color: report.tokens.glassMode === "" ? report.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: report.glass
            mode: report.tokens.glassMode
            radius: card.radius
        }

        Flickable {
            id: reportScroll
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: column.implicitHeight + 52
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds
            enabled: report.page !== "story"

            Column {
                id: column
                x: 28
                y: 26
                width: report.cardWidth - 56
                spacing: 0
                // The report gives way to the story: it fades and slides aside, and returns the same way.
                opacity: report.page === "story" ? 0 : 1
                Behavior on opacity {
                    NumberAnimation {
                        duration: report.tokens.motion.pop
                        easing.type: report.tokens.motion.easing
                    }
                }
                transform: Translate {
                    x: report.page === "story" ? -8 : 0
                    Behavior on x {
                        NumberAnimation {
                            duration: report.tokens.motion.pop
                            easing.type: report.tokens.motion.easing
                        }
                    }
                }

                Item {
                    width: column.width
                    height: titleText.height + subText.height + 4

                    Text {
                        id: titleText
                        text: qsTr("This week's reading report")
                        color: report.tokens.text
                        font.pixelSize: 20
                        font.weight: Font.Bold
                    }

                    Text {
                        id: subText
                        anchors.top: titleText.bottom
                        anchors.topMargin: 4
                        text: report.rangeLine
                        color: report.tokens.faint
                        font.pixelSize: 12
                    }

                    IconButton {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        source: "qrc:/icons/ui-close.svg"
                        color: report.tokens.faint
                        accessibleName: qsTr("Close")
                        onTapped: report.dismiss()
                    }
                }

                Item { width: 1; height: 18 }

                Row {
                    id: figures
                    width: column.width
                    spacing: 28

                    Repeater {
                        model: [
                            {label: qsTr("Lookups"), value: String(report.facts.pops || 0)},
                            {label: qsTr("Words"), value: String(report.facts.words || 0)},
                            {label: qsTr("New"), value: qsTr("%1%").arg(report.facts.freshPercent || 0)},
                            {label: qsTr("Repeated"), value: qsTr("%1%").arg(report.facts.repeatedPercent || 0)}
                        ]
                        Column {
                            id: figure
                            required property var modelData
                            Text {
                                text: figure.modelData.label
                                color: report.tokens.faint
                                font.pixelSize: 12
                            }
                            Text {
                                topPadding: 2
                                text: figure.modelData.value
                                color: report.tokens.text
                                font.family: report.tokens.monoFamily
                                font.pixelSize: 18
                                font.weight: Font.Bold
                            }
                        }
                    }
                }

                Item { width: 1; height: 18 }

                Item {
                    width: column.width
                    height: hourCaption.height

                    Text {
                        id: hourCaption
                        text: qsTr("Lookups by hour")
                        color: report.tokens.faint
                        font.pixelSize: 12
                    }

                    Text {
                        anchors.right: parent.right
                        visible: report.hasLookups
                        text: qsTr("Peak %1, %2 lookups").arg(report.peakLabel).arg(report.peakCount)
                        color: report.tokens.faint
                        font.pixelSize: 12
                    }
                }

                Item { width: 1; height: 10 }

                Row {
                    id: hourBars
                    width: column.width
                    height: 96

                    Repeater {
                        model: report.hours
                        Item {
                            id: hour
                            required property int modelData
                            required property int index
                            width: hourBars.width / 24
                            height: hourBars.height

                            Rectangle {
                                anchors.bottom: parent.bottom
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: hour.width - 4
                                height: hour.modelData > 0
                                        ? Math.max(2, Math.round(hourBars.height * hour.modelData / report.peakCount))
                                        : 0
                                radius: 2
                                color: hour.index === report.peakHour ? report.tokens.ink : report.tokens.muted
                            }
                        }
                    }
                }

                Item {
                    id: ticks
                    width: column.width
                    height: 16

                    Repeater {
                        model: [0, 6, 12, 18, 23]
                        Text {
                            id: tick
                            required property int modelData
                            x: (tick.modelData + 0.5) / 24 * ticks.width - implicitWidth / 2
                            text: tick.modelData
                            color: report.tokens.faint
                            font.pixelSize: 11
                        }
                    }
                }

                Item { width: 1; height: 20 }
                Rectangle { width: column.width; height: 1; color: report.tokens.line2 }

                // The observations: one row per line the interpretation gave. While it is on its way the rows are four bars,
                // and a failure is the one reason in the card's voice.
                Column {
                    id: observations
                    width: column.width

                    Column {
                        width: observations.width
                        visible: report.duty.interpreting
                        spacing: 14

                        Item { width: 1; height: 14 }
                        Repeater {
                            model: [0.92, 0.78, 0.86, 0.64]
                            Rectangle {
                                id: skeleton
                                required property real modelData
                                width: observations.width * skeleton.modelData
                                height: 14
                                radius: 4
                                color: report.tokens.panel2
                            }
                        }
                    }

                    Text {
                        visible: report.duty.interpretFailure !== ""
                        width: observations.width
                        topPadding: 18
                        text: report.duty.interpretFailure
                        textFormat: Text.PlainText
                        color: report.tokens.danger
                        font.pixelSize: 13
                        lineHeight: 1.6
                        lineHeightMode: Text.ProportionalHeight
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: report.duty.interpretation
                        Item {
                            id: point
                            required property var modelData
                            width: observations.width
                            height: pointText.height + 28

                            Text {
                                id: pointText
                                y: 14
                                width: point.width - (pill.visible ? pill.width + 18 : 0)
                                text: point.modelData.text
                                textFormat: Text.PlainText
                                color: report.tokens.text
                                font.pixelSize: 15
                                lineHeight: 1.6
                                lineHeightMode: Text.ProportionalHeight
                                wrapMode: Text.WordWrap
                            }

                            Rectangle {
                                id: pill
                                readonly property string caption: report.figureFor(point.modelData.fact)
                                visible: caption !== ""
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.topMargin: 16
                                width: figureText.implicitWidth + 20
                                height: figureText.implicitHeight + 8
                                radius: report.tokens.radiusPill
                                color: report.tokens.panel2

                                Text {
                                    id: figureText
                                    anchors.centerIn: parent
                                    text: pill.caption
                                    textFormat: Text.PlainText
                                    color: report.tokens.muted
                                    font.pixelSize: 11
                                }
                            }

                            Rectangle {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: 1
                                color: report.tokens.line2
                            }
                        }
                    }
                }

                Item { width: 1; height: 18 }

                Item {
                    width: column.width
                    height: 27

                    Text {
                        anchors.left: parent.left
                        anchors.right: buttons.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("AI observations, for reference only. Billed per request.")
                        color: report.tokens.faint
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        id: buttons
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8

                        Chip {
                            label: qsTr("Story")
                            enabled: report.duty.storyAvailable
                            opacity: enabled ? 1 : 0.45
                            onPicked: report.openStory()
                        }
                        Chip {
                            label: qsTr("Done")
                            selected: true
                            onPicked: report.dismiss()
                        }
                    }
                }
            }
        }

        // The story takes the report's place in the same card, at the card's height, so the card does not change size.
        Item {
            id: storyPage
            x: 28
            y: 26
            width: column.width
            height: report.cardHeight - 52
            enabled: report.page === "story"
            opacity: report.page === "story" ? 1 : 0
            Behavior on opacity {
                NumberAnimation {
                    duration: report.tokens.motion.pop
                    easing.type: report.tokens.motion.easing
                }
            }
            transform: Translate {
                x: report.page === "story" ? 0 : 8
                Behavior on x {
                    NumberAnimation {
                        duration: report.tokens.motion.pop
                        easing.type: report.tokens.motion.easing
                    }
                }
            }

            Row {
                id: storyTitle
                width: parent.width
                spacing: 6

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-back.svg"
                    color: report.tokens.faint
                    accessibleName: qsTr("Back")
                    onTapped: report.backToReport()
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Short story")
                    color: report.tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }
            }

            IconButton {
                anchors.right: parent.right
                anchors.top: parent.top
                source: "qrc:/icons/ui-close.svg"
                color: report.tokens.faint
                accessibleName: qsTr("Close")
                onTapped: report.dismiss()
            }

            Flickable {
                id: storyScroll
                anchors.top: storyTitle.bottom
                anchors.topMargin: 16
                anchors.bottom: storyStatus.top
                anchors.bottomMargin: 8
                width: parent.width
                clip: true
                contentWidth: width
                contentHeight: storyBody.height
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: storyBody
                    width: storyScroll.width
                    spacing: 12
                    Accessible.role: Accessible.StaticText
                    Accessible.name: report.story

                    Text {
                        visible: report.story === ""
                        width: storyBody.width
                        text: report.duty.storyWriting ? qsTr("Writing the story…") : ""
                        textFormat: Text.PlainText
                        color: report.tokens.text
                        font.pixelSize: 15
                    }

                    // One row of words per line of the story. Each word is an item 24 px tall, which is the 1.6 line pitch the
                    // spec asks for, and each dashed line hangs from its own word (docs/adr/0152).
                    Repeater {
                        model: report.story === "" ? [] : report.story.split("\n")

                        Flow {
                            id: paragraph
                            required property string modelData
                            width: storyBody.width

                            Repeater {
                                model: report.wordsOf(paragraph.modelData)

                                Item {
                                    id: word
                                    required property string modelData
                                    width: caption.implicitWidth + 4
                                    height: 24
                                    Accessible.ignored: true

                                    Text {
                                        id: caption
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: word.modelData
                                        textFormat: Text.PlainText
                                        color: report.tokens.text
                                        font.pixelSize: 15
                                    }

                                    Shape {
                                        visible: report.isMarked(word.modelData)
                                        x: 0
                                        y: caption.y + caption.height - 3
                                        width: caption.implicitWidth
                                        height: 2

                                        ShapePath {
                                            strokeColor: report.tokens.faint
                                            strokeWidth: 1
                                            strokeStyle: ShapePath.DashLine
                                            dashPattern: [2, 2]
                                            startX: 0
                                            startY: 1
                                            PathLine { x: caption.implicitWidth; y: 1 }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // The failure and its retry share one row held at one height, so the story page keeps the card's size.
            Item {
                id: storyStatus
                anchors.bottom: storyNote.top
                anchors.bottomMargin: 10
                width: parent.width
                height: 27

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - (retry.visible ? retry.width + 12 : 0)
                    visible: report.storyFailure !== "" && !report.duty.storyWriting
                    text: report.storyFailure
                    textFormat: Text.PlainText
                    color: report.tokens.danger
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }

                Chip {
                    id: retry
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: report.storyFailure !== "" && !report.duty.storyWriting
                    label: qsTr("Retry")
                    onPicked: report.retryStory()
                }
            }

            Text {
                id: storyNote
                anchors.bottom: parent.bottom
                width: parent.width
                text: qsTr("Written by AI from the words you looked up this week.")
                color: report.tokens.faint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }

    GlassHost {
        window: report
        sheet: report.glass
    }
}
