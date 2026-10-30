import QtQuick

/**
 * One sprite sheet, drawn as a single cell. The frame index picks the cell; every layer reads the same index, so
 * the layers never fall out of step. Nearest-neighbour keeps the pixel edges sharp.
 */
Image {
    required property string sheet
    required property int frame
    required property point at
    /// The sprite's own canvas in pixels, which is the size of one cell as well as the figure's square.
    required property int canvas

    x: at.x
    y: at.y
    width: canvas
    height: canvas
    source: sheet
    sourceClipRect: Qt.rect(frame * canvas, 0, canvas, canvas)
    smooth: false
    visible: sheet !== ""
}
