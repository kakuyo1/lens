import QtQuick
import QtQuick.Window

/**
 * Today's statistics (UI.md section 4.6).
 *
 * Two rows drill down: the cost row opens the cost popup and the all-time row opens the
 * words popup. The numbers come straight off the controller, which does the date arithmetic.
 */
Window {
    id: stats
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("stats")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: stats.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 280

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 34 + 2 * shadowMargin

    signal costRequested()
    signal wordsRequested()

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

            // The close icon is anchored to the right edge rather than pushed there by a
            // spacer: a spacer sized around the English title lands the glyph past the card
            // the moment the title is a different width, which is every translation of it.
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

                Icon {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-close.svg"
                    color: stats.tokens.faint
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: stats.visible = false }
                }
            }

            Text {
                topPadding: 16
                text: String(Controller.stats.todayPops)
                color: stats.tokens.text
                font.family: stats.tokens.monoFamily
                font.pixelSize: 32
                font.weight: Font.Bold
            }

            Text {
                topPadding: 4
                text: qsTr("Words explained today")
                color: stats.tokens.faint
                font.pixelSize: 12
            }

            Item { width: 1; height: 14 }
            Rectangle { width: parent.width; height: 1; color: stats.tokens.line2 }
            Item { width: 1; height: 6 }

            StatRow {
                width: parent.width
                label: qsTr("Known")
                value: String(Controller.stats.todayLearned)
            }
            StatRow {
                width: parent.width
                label: qsTr("New")
                value: String(Controller.stats.todayFresh)
            }
            StatRow {
                width: parent.width
                label: qsTr("Cost")
                value: Controller.stats.currency + Controller.stats.todayCost.toFixed(2)
                clickable: true
                onTapped: stats.costRequested()
            }
            StatRow {
                width: parent.width
                label: qsTr("All time")
                // The same count the words panel shows, from the same source: the rows are
                // deduplicated, so a tally of pops labelled "words" reads as a bug beside a
                // panel that lists fewer of them.
                value: qsTr("%1 words").arg(Controller.words.length)
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
