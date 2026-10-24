import QtQuick
import QtQuick.Window
import "../util/Format.js" as Format

/**
 * What the explanations have cost (UI-SPEC.md section 4.8): the month in the hero, four figures
 * underneath. Same shape as the statistics panel, a different dimension.
 *
 * The three rows the controller buckets carry the tokens their amount was priced from as well as
 * the amount: the count in StatRow's quieter `note` slot, the amount in `value`, so the four
 * amounts keep one right edge. The daily average is a derived figure rather than a bucket, so
 * the controller prices no token count for it and the row carries the amount alone.
 */
Window {
    id: cost
    objectName: "cost"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("cost")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: cost.glass.lightText
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

    /// The reader asked for the panel this one was opened from.
    signal backRequested()

    /// Escape closes the panel, the way its cross does (the scope is the one the removal
    /// question's Shortcut explains).
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: cost.visible
        onActivated: cost.visible = false
    }

    ShadowCard {
        id: card
        anchors.fill: parent
        movable: true
        color: cost.tokens.glassMode === "" ? cost.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: cost.glass
            mode: cost.tokens.glassMode
            radius: card.radius
        }

        Column {
            id: column
            x: 20
            y: 18
            width: cost.cardWidth - 40
            spacing: 0

            // The icons are anchored to the right edge rather than pushed by a spacer: one sized around
            // the English title lands the glyphs past the card at every other translation's width.
            Item {
                width: parent.width
                height: title.height

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Cost")
                    color: cost.tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    // Back to the statistics panel, which this one opens from. The panels replace
                    // rather than stack, so the way up has to be visible.
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-back.svg"
                        color: cost.tokens.faint
                        accessibleName: qsTr("Back")
                        onTapped: cost.backRequested()
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-close.svg"
                        color: cost.tokens.faint
                        accessibleName: qsTr("Close")
                        onTapped: cost.visible = false
                    }
                }
            }

            Text {
                topPadding: 16
                text: Format.formatMoney(Controller.costDuty.cost.currency, Controller.costDuty.cost.month)
                color: cost.tokens.text
                font.family: cost.tokens.monoFamily
                font.pixelSize: 32
                font.weight: Font.Bold
            }

            Text {
                topPadding: 4
                text: qsTr("Spent this month")
                color: cost.tokens.faint
                font.pixelSize: 12
            }

            Item { width: 1; height: 14 }
            Rectangle { width: parent.width; height: 1; color: cost.tokens.line2 }
            Item { width: 1; height: 6 }

            // The token count goes in the note slot rather than in front of the amount in `value`:
            // the amount keeps the bold right-aligned face, the four rows keep one right edge, and
            // the count is quiet enough there at 11px. String(), not the raw number, is the house
            // way of spelling an integer.
            StatRow {
                width: parent.width
                label: qsTr("Today")
                note: qsTr("%1 tokens").arg(String(Controller.costDuty.cost.todayTokens))
                value: Format.formatMoney(Controller.costDuty.cost.currency, Controller.costDuty.cost.today)
            }
            StatRow {
                width: parent.width
                label: qsTr("Yesterday")
                note: qsTr("%1 tokens").arg(String(Controller.costDuty.cost.yesterdayTokens))
                value: Format.formatMoney(Controller.costDuty.cost.currency, Controller.costDuty.cost.yesterday)
            }
            StatRow {
                width: parent.width
                label: qsTr("This week")
                note: qsTr("%1 tokens").arg(String(Controller.costDuty.cost.weekTokens))
                value: Format.formatMoney(Controller.costDuty.cost.currency, Controller.costDuty.cost.week)
            }
            StatRow {
                width: parent.width
                label: qsTr("Daily average")
                // A derived average, not a bucket: cost() prices no token count for it, so the
                // row carries the amount alone rather than a figure invented here.
                value: Format.formatMoney(Controller.costDuty.cost.currency, Controller.costDuty.cost.dailyAverage)
            }
        }
    }

    GlassHost {
        window: cost
        sheet: cost.glass
        dragging: card.dragging
    }
}
