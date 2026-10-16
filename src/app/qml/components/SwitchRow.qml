import QtQuick

/**
 * One labelled switch inside the capture group (UI.md 4.4). A non-interactive row keeps its
 * label in the faint colour and its switch dimmed, with nothing said about why.
 */
Row {
    id: root

    property string label: ""
    property bool checked: false
    property bool interactive: true
    /// The preview this row raises when the pointer rests on its label (UI.md 4.4), empty for the
    /// rows that raise none. The owner sets it and answers previewChanged().
    property string previewKey: ""
    /// The key the preview should show, or "" for none: raised while the pointer is on the label.
    signal previewChanged(string key)
    /// The table the row takes its colours from: the one of the window it sits in.
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    signal toggled(bool checked)

    width: parent ? parent.width : implicitWidth
    height: 33
    spacing: 8

    Text {
        id: labelText
        anchors.verticalCenter: parent.verticalCenter
        text: root.label
        color: root.interactive ? root.tokens.text : root.tokens.faint
        font.pixelSize: 13
        // The label is what raises the preview, so it takes the row's full height as its own box:
        // the text is centred in it exactly as before, and the band the pointer has to find is the
        // row's height rather than the 17 px of type. The switch keeps its own box -- a reader on
        // their way to press it is not asking what the setting does (UI.md 4.4).
        height: root.height
        verticalAlignment: Text.AlignVCenter
        HoverHandler {
            enabled: root.previewKey !== ""
            onHoveredChanged: root.previewChanged(hovered ? root.previewKey : "")
        }
    }

    Item {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(0, root.width - labelText.width - switcher.width - 2 * root.spacing)
        height: 1
    }

    Switch {
        id: switcher
        anchors.verticalCenter: parent.verticalCenter
        checked: root.checked
        interactive: root.interactive
        onToggled: (on) => root.toggled(on)
    }
}
