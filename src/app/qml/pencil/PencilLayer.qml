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

            // The pencil line: dashes two pixels under the glyphs. A word the reader marked known
            // carries a firmer, inked line; a fresh one stays faint. Hover inks either.
            Shape {
                x: 0
                y: mark.height + 1
                width: mark.width
                height: 2
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    readonly property bool known: mark.modelData.verdict === "known"
                    strokeColor: mark.modelData.hovered || known ? tokens.ink : tokens.faint
                    strokeWidth: 1.4
                    strokeStyle: ShapePath.DashLine
                    dashPattern: known ? [3, 2] : [2, 2]
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

    /// The mark button a resting pointer raises over a dashed word (UI-SPEC.md 4.20). One window of
    /// its own, because the layer takes no input: the button must take clicks, so it cannot live on
    /// the passthrough layer. Kept out of captures like the layer is, without the passthrough -- the
    /// scan must not read its own affordance either (docs/adr/0160).
    Window {
        id: markButtonWindow

        readonly property var spec: Controller.pencil.markButton
        readonly property bool standing: spec.lemma !== undefined && spec.lemma !== ""

        /// Centered over the anchor word, clamped into its monitor; flush on the word's top edge,
        /// so the pointer crosses no dead ground between the line and the button.
        function placeX() {
            if (!standing) return 0;
            const s = Controller.pencil.screen;
            const raw = spec.x + spec.width / 2 - markPill.width / 2;
            return Math.max(s.x, Math.min(raw, s.x + s.width - markPill.width));
        }

        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
        visible: standing
        x: placeX()
        y: standing ? spec.y - height : 0
        width: markPill.width
        height: markPill.height

        Component.onCompleted: Controller.pencil.keepOutOfCapture(markButtonWindow, false)
        // The layer re-shows on every scan cycle and jumps back above this window in the topmost
        // band; each raise puts the button back on top of it (docs/adr/0160).
        onVisibleChanged: if (visible) markButtonWindow.raise()
        // The duty keeps the button alive on this window's own geometry: report every move and
        // resize, so the keep-alive rectangle is the one actually drawn.
        onXChanged: Controller.pencil.reportMarkButtonGeometry(Qt.point(x, y), Qt.size(width, height))
        onYChanged: Controller.pencil.reportMarkButtonGeometry(Qt.point(x, y), Qt.size(width, height))
        onWidthChanged: Controller.pencil.reportMarkButtonGeometry(Qt.point(x, y), Qt.size(width, height))
        onHeightChanged: Controller.pencil.reportMarkButtonGeometry(Qt.point(x, y), Qt.size(width, height))

        Rectangle {
            id: markPill

            width: markLabel.width + 26
            height: markLabel.height + 10
            radius: tokens.radiusPill
            color: markHover.hovered ? tokens.ink : tokens.panel2
            border.width: 1
            border.color: tokens.line

            Accessible.role: Accessible.Button
            Accessible.name: markLabel.text
            Accessible.onPressAction: markTap.tapped()

            Text {
                id: markLabel
                anchors.centerIn: parent
                text: qsTr("Mastered")
                color: markHover.hovered ? tokens.on : tokens.text
                font.pixelSize: 11
                font.weight: Font.Bold
            }
            HoverHandler { id: markHover; cursorShape: Qt.PointingHandCursor }
            TapHandler {
                id: markTap
                onTapped: Controller.explanationDuty.mark(markButtonWindow.spec.lemma, "mastered")
            }
        }
    }
}
