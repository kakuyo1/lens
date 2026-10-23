import QtQuick

/**
 * The dashed frame that says a card is a demo rather than an explanation the reader called up
 * (UI-SPEC.md 4.4), drawn `6` pixels clear of the card it is told about.
 *
 * Outside the card rather than on its own border: the card stays the same pixels an explanation is,
 * so what tells the two apart is a mark around it and not an edge that quietly changed. A Canvas
 * because a dashed rounded rectangle is all this is and QML's Rectangle has no dash; it paints once
 * and keeps the picture, so every property it draws from repaints it.
 *
 * A component rather than part of Bubble.qml because that file sits at the repository's QML size cap
 * (docs/QML.md section 1), the way SenseRows is.
 */
Canvas {
    id: frame

    /// The card the frame is drawn around, in this item's parent's coordinates.
    required property var card
    /// The card's corner radius: the frame follows it, five pixels wider.
    required property real cardRadius
    required property color strokeColor
    /// How far clear of the card the frame is drawn.
    property real gap: 6

    x: card.x - gap
    y: card.y - gap
    width: card.width + 2 * gap
    height: card.height + 2 * gap

    onPaint: {
        const ctx = getContext("2d");
        const right = frame.width - 1;
        const bottom = frame.height - 1;
        const radius = frame.cardRadius + gap - 1;
        ctx.clearRect(0, 0, frame.width, frame.height);
        ctx.strokeStyle = frame.strokeColor;
        ctx.lineWidth = 2;
        ctx.setLineDash([5, 4]);
        ctx.beginPath();
        ctx.moveTo(1 + radius, 1);
        ctx.lineTo(right - radius, 1);
        ctx.arcTo(right, 1, right, 1 + radius, radius);
        ctx.lineTo(right, bottom - radius);
        ctx.arcTo(right, bottom, right - radius, bottom, radius);
        ctx.lineTo(1 + radius, bottom);
        ctx.arcTo(1, bottom, 1, bottom - radius, radius);
        ctx.lineTo(1, 1 + radius);
        ctx.arcTo(1, 1, 1 + radius, 1, radius);
        ctx.closePath();
        ctx.stroke();
    }

    onVisibleChanged: {
        if (visible)
            requestPaint();
    }
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onStrokeColorChanged: requestPaint()
}
