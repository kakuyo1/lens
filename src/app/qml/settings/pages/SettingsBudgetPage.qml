import QtQuick
import "../../util/Format.js" as Format

Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property var settings: Controller.settings
    property real dailyBudget: Number(settings.dailyBudget || 0)
    signal budgetChanged(real amount)

    /// The symbol the cost surfaces spend in, so the cap is read in the same money as the tally
    /// it caps rather than as a bare number.
    readonly property string currency: Controller.costDuty.stats.currency
    /// What today has cost so far, and whether the cap has been reached. Both come off the
    /// duty's own readout; this page does not re-price or re-bucket anything.
    readonly property real spentToday: Controller.costDuty.stats.todayCost
    readonly property bool exhausted: Controller.costDuty.stats.budgetExhausted === true
    readonly property string spendLine: dailyBudget > 0
        ? qsTranslate("SettingsPopup", "Today %1 · %2 cap")
              .arg(Format.formatMoney(currency, spentToday))
              .arg(Format.formatMoney(currency, dailyBudget))
        : qsTranslate("SettingsPopup", "Today %1 · no cap").arg(Format.formatMoney(currency, spentToday))
    readonly property string capLine: exhausted
        ? qsTranslate("SettingsPopup", "The cap is used up; lookups and requests pause until tomorrow.")
        : dailyBudget > 0
            ? qsTranslate("SettingsPopup", "At the cap, lookups and requests pause until tomorrow.")
            : qsTranslate("SettingsPopup", "No cap; set an amount to pause at the limit.")

    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function restoreText() {
        amountInput.text = root.dailyBudget > 0 ? root.dailyBudget.toFixed(2) : "0"
    }

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7

            Text {
                text: qsTranslate("SettingsPopup", "Daily budget")
                color: root.tokens.muted
                font.pixelSize: 12
            }

            Rectangle {
                width: parent.width
                height: 35
                radius: root.tokens.radiusField
                color: root.tokens.panel2
                border.width: 1
                border.color: amountInput.activeFocus ? root.tokens.ink : root.tokens.line

                Text {
                    id: currencyMark
                    anchors.left: parent.left
                    anchors.leftMargin: 11
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.currency
                    color: root.tokens.muted
                    font.pixelSize: 13
                }

                TextInput {
                    id: amountInput
                    anchors.fill: parent
                    anchors.leftMargin: 11 + currencyMark.width + 4
                    anchors.rightMargin: 11
                    verticalAlignment: TextInput.AlignVCenter
                    text: root.dailyBudget > 0 ? root.dailyBudget.toFixed(2) : "0"
                    color: root.tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    activeFocusOnTab: true
                    Accessible.name: qsTranslate("SettingsPopup", "Daily budget")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    validator: DoubleValidator { bottom: 0; top: 1000000000; decimals: 2 }

                    onEditingFinished: {
                        const amount = Number(text)
                        if (!isFinite(amount) || amount < 0) {
                            root.restoreText()
                            return
                        }
                        root.budgetChanged(amount)
                    }
                }
            }

            Text {
                width: parent.width
                text: root.spendLine
                color: root.exhausted ? root.tokens.warn : root.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                objectName: "budgetSpend"
            }

            Text {
                width: parent.width
                text: root.capLine
                color: root.tokens.faint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                objectName: "budgetCap"
            }
        }
    }
}
