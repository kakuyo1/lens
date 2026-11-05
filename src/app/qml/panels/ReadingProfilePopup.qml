pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window

/**
 * This week's reading profile (UI-SPEC.md section 4.7b): the figures are read off the local history and
 * shown without a request. The interpretation is one button: its first press sends the summary and opens the
 * week's report card (ReportCard.qml), where the answer is shown. Once the week has an answer the same button
 * opens that card without a request. The summary is not shown here.
 *
 * Kept near the height of the cost panel: the three shares sit in one strip, the parts of speech and the
 * most looked-up words are one line each, and the hours are a short bar row.
 */
Window {
    id: profile
    objectName: "profile"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("profile")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: profile.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    /// Set by the panel that replaces this one: the card fades out in place, and the window goes when it has.
    property bool leaving: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 280
    readonly property int barHeight: 22

    /// Empty while the panel is shut, and read again each time it comes up, so a mark made while it was shut is in the counts.
    readonly property var facts: visible ? Controller.readingProfileDuty.facts : ({})
    readonly property var duty: Controller.readingProfileDuty
    readonly property bool hasLookups: facts.pops > 0
    readonly property int peakHour: {
        let peak = 1;
        for (const count of facts.hours || [])
            peak = Math.max(peak, count);
        return peak;
    }
    readonly property string partsLine: (facts.partsOfSpeech || []).map(p => p.part + " " + p.count).join(" · ")
    readonly property string topLine: (facts.top || []).map(w => w.lemma + " " + w.lookups).join(" · ")
    readonly property bool canInterpret: hasLookups && !duty.interpreting && !Controller.costDuty.stats.budgetExhausted
    /// This week's answer is in, so the button reads it again rather than asking twice.
    readonly property bool viewing: (duty.interpretation || []).length > 0
    readonly property bool canOpen: viewing || canInterpret

    function openReport() {
        if (!profile.canOpen)
            return;
        if (!profile.viewing)
            Controller.readingProfileDuty.interpret();
        profile.reportRequested();
    }

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    /// The reader asked for the panel this one was opened from.
    signal backRequested()
    /// The week's report card is wanted: the interpretation was asked for, or its answer is already there.
    signal reportRequested()

    /// Escape closes the panel, the way its cross does (the scope is the one the removal
    /// question's Shortcut explains).
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: profile.visible
        onActivated: profile.visible = false
    }

    Item {
        anchors.fill: parent

        TapHandler {
            acceptedButtons: Qt.BackButton
            onTapped: profile.backRequested()
        }
    }

    ShadowCard {
        id: card
        anchors.fill: parent
        movable: true
        color: profile.tokens.glassMode === "" ? profile.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: profile.glass
            mode: profile.tokens.glassMode
            radius: card.radius
        }

        Column {
            id: column
            x: 20
            y: 18
            width: profile.cardWidth - 40
            spacing: 0

            // The icons are anchored to the right edge rather than pushed by a spacer, as the cost panel does.
            Item {
                width: parent.width
                height: title.height

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Week profile")
                    color: profile.tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-back.svg"
                        color: profile.tokens.faint
                        accessibleName: qsTr("Back")
                        onTapped: profile.backRequested()
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-close.svg"
                        color: profile.tokens.faint
                        accessibleName: qsTr("Close")
                        onTapped: profile.visible = false
                    }
                }
            }

            // The big number and its caption share one line: the caption sits on the number's baseline.
            Item {
                width: column.width
                height: 40

                Text {
                    id: bigNumber
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    text: String(profile.facts.pops)
                    color: profile.tokens.text
                    font.family: profile.tokens.monoFamily
                    font.pixelSize: 32
                    font.weight: Font.Bold
                }

                Text {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    text: profile.hasLookups ? qsTr("Lookups this week") : qsTr("Nothing looked up yet this week.")
                    color: profile.tokens.faint
                    font.pixelSize: 12
                }
            }

            Item { width: 1; height: 10 }
            Rectangle { width: parent.width; height: 1; color: profile.tokens.line2; visible: profile.hasLookups }

            // Three shares in one strip. The counts behind them are in the summary the request sends.
            Row {
                id: strip
                width: column.width
                visible: profile.hasLookups

                Repeater {
                    model: [
                        {label: qsTr("Words"), value: String(profile.facts.words)},
                        {label: qsTr("New words"), value: qsTr("%1%").arg(String(profile.facts.freshPercent))},
                        {label: qsTr("Repeated"), value: qsTr("%1%").arg(String(profile.facts.repeatedPercent))}
                    ]
                    Column {
                        id: cell
                        required property var modelData
                        width: strip.width / 3
                        Item { width: 1; height: 10 }
                        Text {
                            text: cell.modelData.label
                            color: profile.tokens.faint
                            font.pixelSize: 11
                        }
                        Text {
                            topPadding: 2
                            text: cell.modelData.value
                            color: profile.tokens.text
                            font.family: profile.tokens.monoFamily
                            font.pixelSize: 15
                            font.weight: Font.Bold
                        }
                    }
                }
            }

            Item { width: 1; height: 10; visible: profile.hasLookups }
            Rectangle { width: parent.width; height: 1; color: profile.tokens.line2; visible: profile.hasLookups }

            Text {
                visible: profile.hasLookups
                topPadding: 10
                text: qsTr("Parts of speech")
                color: profile.tokens.faint
                font.pixelSize: 11
            }
            Text {
                visible: profile.partsLine !== ""
                width: column.width
                topPadding: 3
                text: profile.partsLine
                textFormat: Text.PlainText
                color: profile.tokens.text
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }
            Text {
                visible: profile.facts.uncached > 0
                width: column.width
                topPadding: 2
                text: qsTr("%1 words without one").arg(String(profile.facts.uncached))
                color: profile.tokens.faint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Text {
                visible: profile.hasLookups
                topPadding: 10
                text: qsTr("Lookups by hour")
                color: profile.tokens.faint
                font.pixelSize: 11
            }
            Item { width: 1; height: 4; visible: profile.hasLookups }
            Row {
                id: hourBars
                width: column.width
                height: profile.barHeight
                visible: profile.hasLookups
                Repeater {
                    model: profile.facts.hours || []
                    Item {
                        required property int modelData
                        width: hourBars.width / 24
                        height: profile.barHeight
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - 3
                            height: parent.modelData > 0 ? Math.max(2, Math.round(profile.barHeight * parent.modelData / profile.peakHour)) : 0
                            radius: 2
                            color: profile.tokens.muted
                        }
                    }
                }
            }
            Item {
                width: column.width
                height: 13
                visible: profile.hasLookups
                Text { x: 0; text: "0"; color: profile.tokens.faint; font.pixelSize: 11 }
                Text { x: column.width * 6 / 24; text: "6"; color: profile.tokens.faint; font.pixelSize: 11 }
                Text { x: column.width * 12 / 24; text: "12"; color: profile.tokens.faint; font.pixelSize: 11 }
                Text { x: column.width * 18 / 24; text: "18"; color: profile.tokens.faint; font.pixelSize: 11 }
            }

            Item { width: 1; height: 10; visible: profile.hasLookups }
            Rectangle { width: parent.width; height: 1; color: profile.tokens.line2; visible: profile.hasLookups }

            Text {
                visible: profile.topLine !== ""
                topPadding: 10
                text: qsTr("Most looked up")
                color: profile.tokens.faint
                font.pixelSize: 11
            }
            Text {
                visible: profile.topLine !== ""
                width: column.width
                topPadding: 3
                text: profile.topLine
                textFormat: Text.PlainText
                color: profile.tokens.text
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            Item { width: 1; height: 12 }
            Rectangle { width: parent.width; height: 1; color: profile.tokens.line2 }
            Item { width: 1; height: 12 }

            // The interpretation is the only part that leaves the machine. Its button is the press that sends it,
            // and the answer comes back to the report card it opens, not to this panel.
            Item {
                width: column.width
                height: 27

                Rectangle {
                    id: interpretButton
                    objectName: "interpretButton"
                    anchors.left: parent.left
                    anchors.top: parent.top
                    width: interpretLabel.width + 26
                    height: 27
                    radius: profile.tokens.radiusPill
                    color: profile.tokens.ink
                    opacity: profile.canOpen ? 1 : 0.45
                    scale: interpretTap.pressed && profile.canOpen ? 0.97 : 1.0

                    activeFocusOnTab: profile.canOpen
                    Accessible.role: Accessible.Button
                    Accessible.name: interpretLabel.text
                    Accessible.onPressAction: profile.openReport()
                    Keys.onPressed: (event) => {
                        if (profile.canOpen
                                && (event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Space)
                                && !event.isAutoRepeat) {
                            event.accepted = true;
                            profile.openReport();
                        }
                    }
                    FocusRing { radius: profile.tokens.radiusPill }

                    Text {
                        id: interpretLabel
                        anchors.centerIn: parent
                        text: profile.viewing ? qsTr("View this week's report") : qsTr("Interpret this week")
                        color: profile.tokens.on
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    TapHandler {
                        id: interpretTap
                        enabled: profile.canOpen
                        onTapped: profile.openReport()
                    }
                }
            }

            Text {
                visible: Controller.costDuty.stats.budgetExhausted && !profile.viewing
                topPadding: 6
                width: column.width
                text: qsTr("Today's budget is used up; the reading waits until tomorrow.")
                color: profile.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }

    GlassHost {
        window: profile
        sheet: profile.glass
        dragging: card.dragging
    }
}
