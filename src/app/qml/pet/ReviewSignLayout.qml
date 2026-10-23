import QtQuick
import "../util/Numbers.js" as Numbers

/**
 * The review sign's placement (UI.md 4.13): a wooden sign standing on the pet's feet line beside it,
 * the side it takes, the nudge that keeps it and its card on the screen, and where the board and the card sit. The desktop sign
 * window and the settings preview both place the sign with this, so they choose the same side. Lengths are in DIPs; the figure is
 * the pet window's top-left, and the pet's idle ink box is in the sprite's own canvas units.
 */
QtObject {
    id: layout

    /// The pet's magnification in DIPs per canvas unit: Pet.scale.
    required property real scale
    /// The pet window's top-left corner in DIPs.
    required property point figure
    /// The idle figure's ink box in canvas units, as {x, y, width, height}: the sign stands outside it and on its foot line.
    required property var ink
    /// The screen the sign and its card must stay on, as {left, top, right, bottom} in DIPs.
    required property var area
    /// The word on the sign, which sets the plank's width.
    required property string word
    /// The card's size in DIPs.
    required property real cardWidth
    required property real cardHeight
    /// Whether the card takes part in the choice and the fit. The desktop sign has one; the settings preview draws none.
    required property bool withCard

    readonly property SurfaceTokens tokens: SurfaceTokens {}
    /// The word's font, as ReviewSignBoard draws it: Pixelify Sans, or the mono family when it is not installed.
    readonly property string wordFamily: Qt.fontFamilies().indexOf("Pixelify Sans") >= 0 ? "Pixelify Sans" : tokens.monoFamily
    /// The plank at its design size: the word plus 14 px of wood each side and a 3 px border. The board takes this width, so the
    /// placement and the drawing agree to the pixel.
    readonly property TextMetrics wordMetrics: TextMetrics {
        font.family: layout.wordFamily
        font.pixelSize: 20
        text: layout.word
    }
    readonly property real designWidth: wordMetrics.width + 34
    readonly property real designHeight: 40
    readonly property real postWidth: 10
    readonly property real postHeight: 32
    readonly property real postOverlap: 2
    readonly property real designBase: designHeight + postHeight - postOverlap
    readonly property real shadowOffset: 4

    /// The sign's magnification: the pet's scale over the 3.01 the design is drawn at (K = scale / 3.01).
    readonly property real k: scale / 3.01
    readonly property real boardWidth: designWidth * k
    readonly property real boardHeight: designHeight * k
    readonly property real signWidth: (designWidth + shadowOffset) * k
    readonly property real signHeight: (designBase + shadowOffset) * k
    /// The pet's foot line, where the post's foot stands: the idle ink's bottom edge.
    readonly property real baseY: figure.y + (ink.y + ink.height) * scale
    /// The board's top-left y, the same on either side.
    readonly property real boardTop: baseY - designBase * k
    readonly property real inkLeft: figure.x + ink.x * scale
    readonly property real inkRight: figure.x + (ink.x + ink.width) * scale
    /// The margin kept clear of the screen's edges (PAD).
    readonly property int pad: 16
    /// The gap between the pet's ink and the sign.
    readonly property int gap: 8
    /// The gap between the sign and its card.
    readonly property int cardGap: 10
    /// The card's widest (CARD_W). The fit tests count the card at this width whatever its real width.
    readonly property int cardMax: 210

    /// The board's left in DIPs for a sign on `side`: `gap` clear of the pet's ink on that side.
    function boardX(side) {
        return side === "left" ? inkLeft - gap - boardWidth : inkRight + gap;
    }

    /// The card's left in DIPs beside a board at `left` on `on`. `width` is the card's width.
    function cardX(left, on, width) {
        return on === "left" ? left - cardGap - width : left + boardWidth + cardGap;
    }

    /// The card's top in DIPs, centred on the board but kept whole on the screen.
    function cardY() {
        const centred = boardTop + boardHeight / 2 - cardHeight / 2;
        const low = area.top + pad;
        const high = area.bottom - pad - cardHeight;
        return Numbers.clamp(centred, low, high);
    }

    /// The boxes of the sign and its card for a sign side and a card side, as [left, top, right, bottom] in DIPs. The sign's box
    /// runs from its board's top-left to its foot's shadow; the card counts at its widest.
    function boxesOf(side, on) {
        const left = boardX(side);
        const cardLeft = cardX(left, on, cardMax);
        const top = cardY();
        return [[left, boardTop, left + signWidth, boardTop + signHeight],
                [cardLeft, top, cardLeft + cardMax, top + cardHeight]];
    }

    /// The boxes that take part: the sign's, and the card's when there is a card.
    function partsOf(boxes) {
        return withCard ? boxes : [boxes[0]];
    }

    /// How far the boxes stick out of the screen's margin, summed over every edge.
    function overflowOf(boxes) {
        let over = 0;
        for (const box of partsOf(boxes))
            over += Math.max(0, area.left + pad - box[0]) + Math.max(0, box[2] - (area.right - pad))
                  + Math.max(0, area.top + pad - box[1]) + Math.max(0, box[3] - (area.bottom - pad));
        return over;
    }

    /**
     * The sign's side and the card's side: the pet's left first, the card outside before inside.
     * The first candidate with no overflow wins; when none fits, the one that overflows least, the earlier on a tie.
     * @return {side, on}.
     */
    function choose() {
        const candidates = [["left", "left"], ["left", "right"], ["right", "right"], ["right", "left"]];
        let best = null;
        for (const [side, on] of candidates) {
            const over = overflowOf(boxesOf(side, on));
            if (over === 0)
                return {side: side, on: on};
            if (!best || over < best.over)
                best = {side: side, on: on, over: over};
        }
        return best;
    }

    /// Moves one span [from, to] inside [low, high]: keeps its start edge when it is too wide, else pulls its far edge in.
    function fit(from, to, low, high) {
        if (to - from > high - low - 2 * pad || from < low + pad)
            return low + pad - from;
        if (to > high - pad)
            return high - pad - to;
        return 0;
    }

    /// The horizontal move that keeps the sign and its card on the screen for a choice. Zero whenever they already fit. The sign's foot stays on the pet's foot line; only x comes back.
    function nudgeFor(side, on) {
        const boxes = partsOf(boxesOf(side, on));
        const low = Math.min(...boxes.map(box => box[0]));
        const high = Math.max(...boxes.map(box => box[2]));
        return Qt.point(fit(low, high, area.left, area.right), 0);
    }

    // The helpers take points and boxes as plain values, which qmllint cannot type.
    // qmllint disable missing-property
    /// The board's top-left in DIPs for a sign on `side`, moved by `nudge`.
    function boardAt(side, nudge) {
        return Qt.point(boardX(side) + nudge.x, boardTop);
    }

    /// The card's top-left in DIPs beside a board at `board` on side `on`, at the card's real width.
    function cardAt(board, on) {
        return Qt.point(board.x + (on === "left" ? -cardGap - cardWidth : boardWidth + cardGap), cardY());
    }
    // qmllint enable missing-property
}
