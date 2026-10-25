pragma ComponentBehavior: Bound

import QtQuick

/**
 * One row of the dropdown list: the label, the optional note, the pointer targets and the screen
 * reader name. Its own file keeps DropdownField under the QML size cap.
 */
Rectangle {
    id: row

    required property var field
    required property var option
    required property int rowIndex

    width: parent ? parent.width : 0
    height: row.field.rowHeight
    radius: 8

    // The list window takes no focus, so the row is pointer-driven; the name still stands for the
    // screen reader's review pass.
    Accessible.role: Accessible.Button
    Accessible.name: row.option.label
    Accessible.onPressAction: {
        row.field.picked(row.option.value);
        row.field.closeList();
    }
    color: row.option.value === row.field.currentValue ? row.field.tokens.ink
         : rowPress.pressed || row.rowIndex === row.field.highlightIndex ? row.field.tokens.line
         : rowHover.hovered ? row.field.tokens.panel2
         : "transparent"

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 11
        anchors.verticalCenter: parent.verticalCenter
        text: row.option.label
        textFormat: Text.PlainText
        color: row.option.value === row.field.currentValue ? row.field.tokens.on : row.field.tokens.text
        font.pixelSize: 13
    }

    Text {
        visible: row.field.notesInRows && (row.option.note || "") !== ""
        anchors.right: parent.right
        anchors.rightMargin: 11
        anchors.verticalCenter: parent.verticalCenter
        text: row.option.note || ""
        textFormat: Text.PlainText
        color: row.option.value === row.field.currentValue ? Qt.rgba(1, 1, 1, 0.62) : row.field.tokens.faint
        font.pixelSize: 11
    }

    HoverHandler {
        id: rowHover
        cursorShape: Qt.PointingHandCursor
        onHoveredChanged: row.field.rowHovered(row.rowIndex, rowHover.hovered)
    }
    TapHandler {
        id: rowPress
        onTapped: {
            row.field.picked(row.option.value);
            // Through closeList(), so a search that was narrowing the list is dropped with it:
            // the next chevron opens the whole list again rather than the fragment just picked.
            row.field.closeList();
        }
    }
}
