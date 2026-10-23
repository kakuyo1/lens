import QtQuick

/**
 * One glass parameter: its name, its value, and a slider over whole-number stops.
 *
 * The owner binds `value` and takes the new stop from `moved`, so the setting is only ever written by
 * the reader's gesture and never by the slider itself.
 */
Column {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    property string label: ""
    property string suffix: ""
    property int from: 0
    property int to: 100
    property int value: from
    /// The preview this row raises when the pointer rests on its label (UI-SPEC.md 4.4), empty for the
    /// rows that raise none. The owner sets it and answers previewChanged().
    property string previewKey: ""
    /// The key the preview should show, or "" for none: raised while the pointer is on the label.
    signal previewChanged(string key)

    signal moved(int value)

    width: parent.width
    spacing: 6

    // The label line, which is also what raises the preview: the whole line, the value readout on
    // its right included, since the slider below is the one thing the pointer has to leave to reach
    // a control (UI-SPEC.md 4.4).
    Item {
        width: parent.width
        height: 16

        Text {
            anchors.left: parent.left
            text: root.label
            color: root.tokens.muted
            font.pixelSize: 12
        }

        Text {
            anchors.right: parent.right
            text: root.value + root.suffix
            color: root.tokens.text
            font.pixelSize: 12
        }

        HoverHandler {
            enabled: root.previewKey !== ""
            onHoveredChanged: root.previewChanged(hovered ? root.previewKey : "")
        }
    }

    StepSlider {
        width: parent.width
        from: root.from
        to: root.to
        value: root.value
        onMoved: (stop) => root.moved(stop)
    }
}
