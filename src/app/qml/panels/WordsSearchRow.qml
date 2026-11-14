import QtQuick

/**
 * The words popup's search row (UI-SPEC.md 4.8): a field under the segment that narrows the list
 * to the words holding its text. It writes no rows itself -- the model owns the filter, and the
 * popup binds Controller.wordsModel.query to the field -- and it asks for nothing while the
 * reader types: a request only leaves through the miss hint's pill, or Enter on the same miss
 * (docs/adr/0176).
 *
 * @p host is the WordsPopup window: the tokens and card width read from it, and host.searchMiss
 * says whether Enter would be asking for an explanation.
 */
Item {
    id: search

    required property var host

    /// The word the reader typed, untrimmed; the model trims before it matches.
    readonly property alias query: field.text
    /// Whether the field holds the keyboard focus: the popup's cue that Escape clears the
    /// query rather than closing the panel.
    readonly property bool fieldFocus: field.activeFocus

    /// @brief Give the field back its empty state; the model follows through the popup's binding.
    function clear() {
        field.text = "";
    }

    width: host.cardWidth - 40
    height: 30

    // DESIGN.md's input field: panel2 ground, line stroke, 9 px corners; focus turns the
    // stroke ink and adds no glow.
    Rectangle {
        anchors.fill: parent
        radius: 9
        color: search.host.tokens.panel2
        border.width: 1
        border.color: field.activeFocus ? search.host.tokens.ink : search.host.tokens.line
    }

    Icon {
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        source: "qrc:/icons/ui-search.svg"
        color: search.host.tokens.faint
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 30
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        visible: field.text === ""
        text: qsTr("Search words")
        color: search.host.tokens.faint
        font.pixelSize: 12
        elide: Text.ElideRight
    }

    TextInput {
        id: field
        objectName: "searchField"
        anchors.fill: parent
        anchors.leftMargin: 30
        anchors.rightMargin: clearCross.visible ? 26 : 10
        verticalAlignment: TextInput.AlignVCenter
        color: search.host.tokens.text
        font.pixelSize: 12
        clip: true
        selectByMouse: true
        activeFocusOnTab: true

        // Enter asks only on a miss: while the query still names rows, the row's own hover
        // shows their stored reading for free (docs/adr/0145), and a keystroke that paid again
        // for it would be a double charge. Escape clears the query and stands down; the panel's
        // own Escape closes, and answers only when this one has nothing to hold.
        Keys.onPressed: (event) => {
            if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return) && !event.isAutoRepeat
                && search.host.searchMiss) {
                event.accepted = true;
                search.host.lookUpTyped();
            } else if (event.key === Qt.Key_Escape) {
                event.accepted = true;
                search.clear();
            }
        }
    }

    IconButton {
        id: clearCross
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        visible: field.text !== ""
        source: "qrc:/icons/ui-close.svg"
        color: search.host.tokens.faint
        accessibleName: qsTr("Clear search")
        onTapped: {
            search.clear();
            field.forceActiveFocus();
        }
    }
}
