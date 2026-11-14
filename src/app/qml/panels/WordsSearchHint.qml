import QtQuick

/**
 * The words popup's search miss (UI-SPEC.md 4.8): one line in the rows' place -- the typed query
 * matches nothing -- carrying the pill that asks the model about the word (docs/adr/0176). It is
 * the same doorway the row hover's hint opens (docs/adr/0145): the hint itself never asks,
 * pressing is the reader's.
 *
 * @p host is the WordsPopup window: the tokens and the query read from it, and
 * host.lookUpTyped() is the one path out.
 */
Item {
    id: hint

    required property var host

    width: host.cardWidth - 40
    height: Math.max(line.implicitHeight, pill.height)

    Text {
        id: line
        anchors.left: parent.left
        anchors.right: pill.left
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("No record of %1").arg(hint.host.searchQuery)
        color: hint.host.tokens.faint
        font.pixelSize: 11
        elide: Text.ElideRight
    }

    Rectangle {
        id: pill
        objectName: "searchMissPill"
        readonly property bool lit: hover.hovered || pill.activeFocus
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: pillLabel.width + 20
        height: 22
        radius: hint.host.tokens.radiusPill
        color: pill.lit ? hint.host.tokens.ink : hint.host.tokens.panel2
        border.width: 1
        border.color: pill.lit ? hint.host.tokens.ink : hint.host.tokens.line
        scale: tap.pressed ? 0.97 : 1.0
        Behavior on scale {
            NumberAnimation {
                duration: hint.host.tokens.motion.press
                easing.type: hint.host.tokens.motion.easing
            }
        }

        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: pillLabel.text
        Accessible.onPressAction: tap.tapped()
        FocusRing { radius: hint.host.tokens.radiusPill }
        Keys.onPressed: (event) => {
            if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return || event.key === Qt.Key_Space)
                && !event.isAutoRepeat) {
                event.accepted = true;
                tap.tapped();
            }
        }

        Text {
            id: pillLabel
            anchors.centerIn: parent
            // The row hover's hint carries the same button: one string, one entry, one translation.
            text: qsTranslate("lens::app::AppController", "Explain now")
            color: pill.lit ? hint.host.tokens.on : hint.host.tokens.text
            font.pixelSize: 11
            font.weight: Font.Bold
        }

        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        TapHandler { id: tap; onTapped: hint.host.lookUpTyped() }
    }
}
