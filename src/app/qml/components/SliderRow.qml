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

    signal moved(int value)

    width: parent.width
    spacing: 6

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
    }

    StepSlider {
        width: parent.width
        from: root.from
        to: root.to
        value: root.value
        onMoved: (stop) => root.moved(stop)
    }
}
