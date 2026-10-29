pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The dropdown's explanation preview (UI-SPEC.md 4.4): the note of the row under the pointer, in a card
 * beside the open list, for the fields whose note is a sentence rather than a word.
 *
 * A window of its own because the list it belongs to is one, and the settings panel is exactly as
 * tall as its contents. It carries no dashed frame: that mark says a card is a demo of something
 * (PreviewFrame), and this one is the explanation itself.
 */
Window {
    id: root

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
    color: "transparent"
    visible: false

    /// The dropdown option under the pointer: {value, label, note}.
    property var option: null

    readonly property string title: root.option ? String(root.option.label) : ""
    readonly property string body: root.option ? String(root.option.note || "") : ""
    /// A row with nothing to say keeps the window down rather than raising an empty card.
    readonly property bool hasContent: root.body.length > 0

    /// @brief Point this window at @p value, the option whose note it is to show.
    function showOption(value) {
        root.option = value;
    }

    /// The margin the card's shadow needs: DropdownField keeps this free to hang over the screen's edge.
    readonly property int shadowMargin: 26
    readonly property int cardWidth: 244
    /// Room the text leaves inside the card, on every side.
    readonly property int cardPadding: 13
    /// A fixed height, as the other previews keep: the same window whichever row is hovered, so the
    /// card neither jumps as the pointer crosses the list nor changes size after its first show -- a
    /// window resized before it has ever been shown loses the translucent presentation and its shadow
    /// margin comes up black. High enough for the longest note in any language, five lines of body.
    readonly property int cardHeight: 116

    width: root.cardWidth + 2 * root.shadowMargin
    height: root.cardHeight + 2 * root.shadowMargin

    /// The desktop under this window, for the glass themes only.
    readonly property var glass: Controller.glassBackdrop.surface("notePreview")
    /// The stored theme's table, glass included: this card is raised beside the panel the reader
    /// is choosing in, and a flat card beside a glass one reads as another surface's note.
    readonly property TokenTable tokens: TokenTable {
        theme: Tokens.theme
        glassEnabled: true
        glassLightText: root.glass ? root.glass.lightText : false
        motion: Tokens.motion
    }

    GlassHost {
        window: root
        sheet: root.glass
    }

    ShadowCard {
        anchors.fill: parent
        radius: root.tokens.radiusCard
        color: root.tokens.glassMode === "" ? root.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: root.glass
            mode: root.tokens.glassMode
            radius: root.tokens.radiusCard
        }

        Column {
            id: column
            anchors.left: parent.left
            anchors.leftMargin: root.cardPadding
            anchors.verticalCenter: parent.verticalCenter
            width: root.cardWidth - 2 * root.cardPadding
            spacing: 5

            Text {
                visible: root.title.length > 0
                width: parent.width
                text: root.title
                textFormat: Text.PlainText
                color: root.tokens.text
                font.pixelSize: 12
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                text: root.body
                textFormat: Text.PlainText
                wrapMode: Text.WordWrap
                color: root.tokens.muted
                font.pixelSize: 11
                lineHeight: 1.25
            }
        }
    }
}
