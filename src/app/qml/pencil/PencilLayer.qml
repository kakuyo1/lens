pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Shapes

/**
 * The pencil layer (UI-SPEC.md 4.20): a dashed line under each word the page holds, and the word's signpost beside
 * it while Alt is held. It covers the monitor the page is on, and it takes no input: the pointer and the keyboard
 * go to the page underneath. Kept out of screen captures, so the scan never reads its own lines back.
 *
 * Its marks come from PencilDuty in device-independent pixels, relative to this window.
 */
Window {
    id: layer
    objectName: "pencilLayer"

    // Click-through is the Win32 style PencilDuty sets: Qt.WindowTransparentForInput painted this layer black.
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
           | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: Controller.pencil.visible

    x: Controller.pencil.screen.x
    y: Controller.pencil.screen.y
    width: Controller.pencil.screen.width
    height: Controller.pencil.screen.height

    /// The colour table, as the bubble and the settings previews take it.
    SurfaceTokens {
        id: tokens
    }

    Component.onCompleted: Controller.pencil.keepOutOfCapture(layer)

    Repeater {
        model: Controller.pencil.marks

        delegate: Item {
            id: mark
            required property var modelData

            x: mark.modelData.x
            y: mark.modelData.y
            width: mark.modelData.width
            height: mark.modelData.height

            // The pencil line: dashes two pixels under the glyphs, inked when the pointer is on the word.
            Shape {
                x: 0
                y: mark.height + 1
                width: mark.width
                height: 2
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: mark.modelData.hovered ? tokens.ink : tokens.faint
                    strokeWidth: 1.4
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [2, 2]
                    fillColor: "transparent"
                    startX: 0
                    startY: 1
                    PathLine { x: mark.width; y: 1 }
                }
            }

            // The signpost above the word's top-right corner, while Alt is held and one is stored.
            Text {
                visible: Controller.pencil.altHeld && mark.modelData.signpost !== ""
                x: mark.width
                y: -implicitHeight + 3
                text: mark.modelData.signpost
                color: tokens.okText
                font.pixelSize: 10
                font.weight: Font.Bold
            }
        }
    }
}
