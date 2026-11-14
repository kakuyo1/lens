import QtQuick

/**
 * The answer card of the review sign (UI-SPEC.md 4.13): two buttons that open beside the board while the pointer is on the sign or
 * on the card. A window of its own, so the card can open past the board's window without that window ever changing size
 * (ADR 0053). It never takes focus, and a screen capture does not see it.
 */
Window {
    id: card
    objectName: "reviewSignCard"

    /// A button was pressed: true for Remembered, false for Forgot.
    signal answered(bool remembered)
    /// Whether the pointer is on the card, including its buttons: the buttons sit above the card's hover item, so their own
    /// hover is counted too, otherwise the card would close the moment the pointer reached a button.
    readonly property bool hovered: hover.hovered || forgotHover.hovered || rememberedHover.hovered

    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("reviewSignCard")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: card.glass.lightText
    }
    readonly property int padding: 8

    // The padding and the 1 px border on each side, as .ask-pop in pet-review.html sizes it.
    width: buttons.implicitWidth + 2 * padding + 2
    height: buttons.height + 2 * padding + 2

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"

    Component.onCompleted: Pet.host.keepOutOfCapture(card)

    Rectangle {
        anchors.fill: parent
        radius: card.tokens.radiusCard
        // Under a glass theme the card is the glass: the GlassSurface below draws the fill and the
        // rim, and a filled rectangle here would hide the backdrop (DESIGN.md 3.1c and 3.1d).
        color: card.tokens.glassMode === "" ? card.tokens.panel : "transparent"
        border.width: 1
        border.color: card.tokens.glassMode === "" ? card.tokens.line : "transparent"
    }

    GlassSurface {
        anchors.fill: parent
        sheet: card.glass
        mode: card.tokens.glassMode
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
            scale: forgotTap.pressed ? 0.97 : 1.0
            Behavior on scale {
                NumberAnimation {
                    duration: card.tokens.motion.press
                    easing.type: card.tokens.motion.easing
                }
            }

            Text {
                id: forgotLabel

                anchors.centerIn: parent
                text: qsTr("Forgot")
                color: card.tokens.text
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            HoverHandler {
                id: forgotHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                id: forgotTap
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
            scale: rememberedTap.pressed ? 0.97 : 1.0
            Behavior on scale {
                NumberAnimation {
                    duration: card.tokens.motion.press
                    easing.type: card.tokens.motion.easing
                }
            }

            Text {
                id: rememberedLabel

                anchors.centerIn: parent
                text: qsTr("Remembered")
                color: card.tokens.on
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            HoverHandler {
                id: rememberedHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                id: rememberedTap
                onTapped: card.answered(true)
            }

            Accessible.role: Accessible.Button
            Accessible.name: rememberedLabel.text
        }
    }

    GlassHost {
        window: card
        sheet: card.glass
        // The card stays out of captures at every theme, like the sign it belongs to (ADR 0053);
        // without this the glass lifecycle would take the exclusion back when a glass theme is off.
        alwaysExcluded: true
    }
}
