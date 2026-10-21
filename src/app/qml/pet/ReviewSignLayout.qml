import QtQuick

/**
 * The review sign's placement (UI.md 4.13, ui-prototypes/pet-review.html): the hand that holds the sign and the side of its card,
 * the nudge that keeps both on the screen, and where the board, its pole and the card sit for a grip. The desktop sign window
 * and the settings preview both place the sign with this, so they choose the same hand. Lengths are in DIPs; the figure is
 * one canvas unit per `scale` DIPs.
 */
QtObject {
    id: layout

    /// The pet's magnification in DIPs per canvas unit: Pet.scale.
    required property real scale
    /// The figure's top-left corner in DIPs.
    required property point figure
    /// The screen the sign and its card must stay on, as {left, top, right, bottom} in DIPs.
    required property var area
    /// The grips of each hand at the start of the ask action, and in the frame on show: {left, right} points in canvas units,
    /// with a hand the pet does not have left out.
    required property var restGrips
    required property var grips
    /// The word on the board, which sets the board's width.
    required property string word
    /// The card's size in DIPs.
    required property real cardWidth
    required property real cardHeight
    /// Whether the card takes part in the choice and the fit. The desktop sign has one; the settings preview draws none.
    required property bool withCard

    readonly property SurfaceTokens tokens: SurfaceTokens {}
    /// The word's font, as ReviewSignBoard draws it: Pixelify Sans, or the mono family when it is not installed.
    readonly property string wordFamily: Qt.fontFamilies().indexOf("Pixelify Sans") >= 0 ? "Pixelify Sans" : tokens.monoFamily
    /// The board at its design size, border included: the word's width plus 18 px padding each side and a 3 px border, and a 20 px
    /// line plus 14 px above and 12 below, and the border (.sign in pet-review.html). The board takes this width, so the placement
    /// and the drawing agree to the pixel.
    readonly property TextMetrics wordMetrics: TextMetrics {
        font.family: layout.wordFamily
        font.pixelSize: 20
        text: layout.word
    }
    readonly property real designWidth: wordMetrics.width + 36 + 6
    readonly property real designHeight: 20 + 26 + 6

    /// The board's magnification: the pet's scale over the 3.01 the design is drawn at (K in pet-review.html).
    readonly property real k: scale / 3.01
    readonly property real boardWidth: designWidth * k
    readonly property real boardHeight: designHeight * k
    readonly property real poleHeight: 26 * k
    /// The pole's centre sits 16 design px in from the board's edge nearest the pet.
    readonly property real inset: 16 * k
    /// The margin kept clear of the screen's edges (PAD).
    readonly property int pad: 16
    /// The gap between the board and its card (CARD_GAP).
    readonly property int gap: 10
    /// The card's widest (CARD_W). The fit tests count the card at this width whatever its real width.
    readonly property int cardMax: 210
    /// How far the ask animation's bob lifts the hand, 6 canvas units (BOB).
    readonly property real bob: 6 * scale

    // The helpers take points and boxes as plain values, which qmllint cannot type.
    // qmllint disable missing-property
    /// The grip of hand `hand` from `from` in DIPs, or null when `from` has no such hand.
    function gripIn(from, hand) {
        const grip = from[hand];
        return grip ? Qt.point(figure.x + grip.x * scale, figure.y + grip.y * scale) : null;
    }

    /// The board's top-left for a hand whose grip is `grip`: the pole's centre on the grip's column and its tail on its row.
    function boardOn(hand, grip) {
        return Qt.point(hand === "right" ? grip.x - inset : grip.x - boardWidth + inset, grip.y - boardHeight - poleHeight);
    }

    /// The card's top-left beside a board at `board`, on side `on`, centred on the board. `width` is the card's width.
    function cardOn(board, on, width) {
        return Qt.point(on === "right" ? board.x + boardWidth + gap : board.x - gap - width,
                        board.y + boardHeight / 2 - cardHeight / 2);
    }

    /// The boxes of the sign and its card for a hand and a card side, as [left, top, right, bottom] in DIPs. The sign's box ends
    /// at its tail on the grip, and both reach up by the bob. The card counts at its widest.
    function boxesOf(hand, on, grip) {
        const board = boardOn(hand, grip);
        const card = cardOn(board, on, cardMax);
        return [[board.x, board.y - bob, board.x + boardWidth, grip.y],
                [card.x, card.y - bob, card.x + cardMax, card.y + cardHeight]];
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
     * The hand and the card's side (chooseHand in pet-review.html): the left hand first, the card outside before inside. The first
     * candidate with no overflow wins; when none fits, the one that overflows least, the earlier on a tie.
     * @return {hand, on}, or null when the pet has no hand.
     */
    function choose() {
        const candidates = [["left", "left"], ["left", "right"], ["right", "right"], ["right", "left"]];
        let best = null;
        for (const [hand, on] of candidates) {
            const grip = gripIn(restGrips, hand);
            if (!grip)
                continue;
            const over = overflowOf(boxesOf(hand, on, grip));
            if (over === 0)
                return {hand: hand, on: on};
            if (!best || over < best.over)
                best = {hand: hand, on: on, over: over};
        }
        return best ? {hand: best.hand, on: best.on} : null;
    }

    /// Moves one span [from, to] inside [low, high]: keeps its start edge when it is too wide, else pulls its far edge in.
    function fit(from, to, low, high) {
        if (to - from > high - low - 2 * pad || from < low + pad)
            return low + pad - from;
        if (to > high - pad)
            return high - pad - to;
        return 0;
    }

    /**
     * The move that keeps the sign and its card on the screen for a choice (fitSign in pet-review.html). Zero whenever they already
     * fit. The pet never moves; the sign and its card do, and the sign may come off the hand when neither side fits.
     */
    function nudgeFor(hand, on) {
        const boxes = partsOf(boxesOf(hand, on, gripIn(restGrips, hand)));
        const low = i => Math.min(...boxes.map(box => box[i]));
        const high = i => Math.max(...boxes.map(box => box[i + 2]));
        return Qt.point(fit(low(0), high(0), area.left, area.right), fit(low(1), high(1), area.top, area.bottom));
    }

    /// The board's top-left in DIPs for hand `hand` in the frame on show, moved by `nudge`. The origin until the pet shows that hand.
    function boardAt(hand, nudge) {
        const grip = gripIn(grips, hand);
        if (!grip)
            return Qt.point(0, 0);
        const board = boardOn(hand, grip);
        return Qt.point(board.x + nudge.x, board.y + nudge.y);
    }

    /// The card's top-left in DIPs beside a board at `board`, on side `on`, at the card's real width.
    function cardAt(board, on) {
        return cardOn(board, on, cardWidth);
    }
    // qmllint enable missing-property
}
