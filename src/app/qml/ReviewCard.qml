import QtQuick

/**
 * The answer card of the review sign (UI.md 4.13): two buttons that open beside the board while the pointer is on the sign or
 * on the card. A window of its own, so the card can open past the board's window without that window ever changing size
 * (ADR 0053). It never takes focus, and a screen capture does not see it.
 */
Window {
    id: card

    /// A button was pressed: true for Remembered, false for Forgot.
    signal answered(bool remembered)
    /// Whether the pointer is on the card.
    readonly property bool hovered: hover.hovered

    readonly property SurfaceTokens tokens: SurfaceTokens {}
    readonly property int padding: 8

    // The padding and the 1 px border on each side, as .ask-pop in pet-review.html sizes it.
    width: buttons.implicitWidth + 2 * padding + 2
    height: buttons.height + 2 * padding + 2

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"

    Component.onCompleted: Pet.keepOutOfCapture(card)

    Rectangle {
        anchors.fill: parent
        radius: card.tokens.radiusCard
        color: card.tokens.panel
        border.width: 1
        border.color: card.tokens.line
    }

    // The pointer on any part of the window, the buttons included, counts as on the card.
    Item {
        anchors.fill: parent

        HoverHandler {
            id: hover
        }
    }

    Row {
        id: buttons

        anchors.centerIn: parent
        spacing: 8

        Rectangle {
            id: forgot

            width: forgotLabel.implicitWidth + 28
            height: 34
            radius: card.tokens.radiusPill
            color: card.tokens.panel2
            border.width: 1
            border.color: card.tokens.line

            Text {
                id: forgotLabel

                anchors.centerIn: parent
                text: qsTr("Forgot")
                color: card.tokens.text
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: card.answered(false)
            }

            Accessible.role: Accessible.Button
            Accessible.name: forgotLabel.text
        }

        Rectangle {
            id: remembered

            width: rememberedLabel.implicitWidth + 28
            height: 34
            radius: card.tokens.radiusPill
            color: card.tokens.ink

            Text {
                id: rememberedLabel

                anchors.centerIn: parent
                text: qsTr("Remembered")
                color: card.tokens.on
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: card.answered(true)
            }

            Accessible.role: Accessible.Button
            Accessible.name: rememberedLabel.text
        }
    }
}
