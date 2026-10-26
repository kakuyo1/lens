import QtQuick
import QtQuick.Window
import "../util/Format.js" as Format

/**
 * Today's statistics (UI-SPEC.md section 4.6).
 *
 * Four rows drill down: Known and New open the word list already narrowed to that verdict,
 * Cost opens the cost popup, and All time opens the unfiltered words popup. The numbers come
 * straight off the controller, which does the date arithmetic.
 */
Window {
    id: stats
    objectName: "stats"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("stats")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: stats.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    /// Set by the panel that replaces this one: the card fades out in place, and the window goes when it has.
    property bool leaving: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 280

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    signal costRequested()
    signal wordsRequested()
    signal knownRequested()
    signal newRequested()

    /// Escape closes the panel, the way its cross does (the scope is the one the removal
    /// question's Shortcut explains).
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: stats.visible
        onActivated: stats.visible = false
    }

    ShadowCard {
        id: card
        anchors.fill: parent
        movable: true
        color: stats.tokens.glassMode === "" ? stats.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: stats.glass
            mode: stats.tokens.glassMode
            radius: card.radius
        }

        Column {
            id: column
            x: 20
            y: 18
            width: stats.cardWidth - 40
            spacing: 0

            // The close icon is anchored to the right edge rather than pushed by a spacer: one sized
            // around the English title lands the glyph past the card at every other translation's width.
            Item {
                width: parent.width
                height: title.height

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Today")
                    color: stats.tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                IconButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-close.svg"
                    color: stats.tokens.faint
                    accessibleName: qsTr("Close")
                    onTapped: stats.visible = false
                }
            }

            Text {
                topPadding: 16
                text: String(Controller.costDuty.stats.todayPops)
                color: stats.tokens.text
                font.family: stats.tokens.monoFamily
                font.pixelSize: 32
                font.weight: Font.Bold
            }

            Text {
                topPadding: 4
                text: qsTr("Lookups today")
                color: stats.tokens.faint
                font.pixelSize: 12
            }

            Item { width: 1; height: 14 }
            Rectangle { width: parent.width; height: 1; color: stats.tokens.line2 }
            Item { width: 1; height: 6 }

            StatRow {
                width: parent.width
                label: qsTr("Known")
                value: String(Controller.costDuty.stats.todayLearned)
                clickable: true
                onTapped: stats.knownRequested()
            }
            StatRow {
                width: parent.width
                label: qsTr("New")
                value: String(Controller.costDuty.stats.todayFresh)
                clickable: true
                onTapped: stats.newRequested()
            }
            StatRow {
                width: parent.width
                label: qsTr("Cost")
                value: Format.formatMoney(Controller.costDuty.stats.currency, Controller.costDuty.stats.todayCost)
                clickable: true
                onTapped: stats.costRequested()
            }
            StatRow {
                width: parent.width
                label: qsTr("All time")
                // The deduplicated vocabulary, the same count the words popup titles (ADR 0104):
                // the tallies that count events say "lookups", never "words".
                value: qsTr("%1 words").arg(Controller.wordsModel.allCount)
                clickable: true
                onTapped: stats.wordsRequested()
            }
        }
    }

    GlassHost {
        window: stats
        sheet: stats.glass
        dragging: card.dragging
    }
}
