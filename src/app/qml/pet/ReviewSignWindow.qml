import QtQuick
import "../util/Numbers.js" as Numbers

/**
 * The desktop pet's review sign (UI-SPEC.md 4.13): a wooden plank on a short post, standing on the pet's
 * foot line beside it, with the word on the plank. Its answer card opens beside the board while the pointer is on the sign, in a
 * window of its own (ReviewCard). The sign's side and the card's side are chosen once per question from the room on screen, and the
 * sign then follows the pet as it moves. A window of its own, kept out of screen captures by the pet's native bridge, so the scan
 * never reads the word back.
 */
Window {
    id: sign
    objectName: "reviewSign"

    /// The pet's window: the sign stands beside it on its foot line.
    required property var petWindow
    /// The work area of the screen a point is on, in DIPs: SurfaceHost.qml's workArea, so the sign and the answer stay on that screen.
    required property var workAreaAt

    /// The word on the board, whether a question is up, and whether its side has been chosen.
    property string word: ""
    property bool asking: false
    property bool placed: false
    /// The sign's side, and the card's side: outer when the card is on the sign's side, inner when it is not.
    property string side: "left"
    property string cardSide: "right"
    /// The move that keeps the sign and its card on the screen (fitSign). Zero unless neither side fits.
    property point nudge: Qt.point(0, 0)
    /// The work area the choice was made in, in DIPs.
    property var workArea: ({left: 0, top: 0, right: 0, bottom: 0})
    /// Whether the card is open. It opens while the pointer is on the sign or the card, and shuts 120 ms after both are left.
    property bool expanded: false
    /// The answer's bubble, once placed, so a screen change can put it back.
    property var answerCard: null
    /// The answer card's own window, which opens beside the board.
    readonly property alias card: answers

    /// The board's top-left in DIPs, beside the pet on the chosen side and moved by the nudge.
    readonly property point boardAt: layout.boardAt(side, nudge)
    /// The card's top-left in DIPs, beside the board on the card's side.
    readonly property point cardAt: layout.cardAt(boardAt, cardSide)

    // The sign stands beside the pet while a question is up, whichever action the pet happens to play: a package with no
    // ask pose still gets its sign.
    visible: asking && placed && Pet.enabled
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"

    width: paper.width
    height: paper.height
    // Windows hold whole pixels: rounded, not truncated, so the plank lands within half a pixel of its place.
    x: Math.round(boardAt.x)
    y: Math.round(boardAt.y)

    onVisibleChanged: {
        if (visible)
            raise();
        reportLook();
    }
    onExpandedChanged: reportLook()
    onSideChanged: reportLook()
    onCardSideChanged: reportLook()

    Component.onCompleted: Pet.keepOutOfCapture(sign)

    /// Tells the pet which side to look at: the board's while it stands, the open card's while the pointer holds it
    /// open. Only the direction travels, never the word (ADR 0097).
    function reportLook() {
        Pet.setReviewLook(visible ? (expanded ? cardSide : side) : "");
    }

    Connections {
        target: Controller

        function onReviewQuestionChanged() {
            sign.begin(Controller.reviewQuestion);
        }
    }

    // A pet that is resized changes its scale, so the sign takes a fresh choice of side.
    Connections {
        target: sign.petWindow

        function onWidthChanged() {
            sign.relayout();
        }
    }

    /// Answers the question. The bubble's anchor goes out in physical pixels, as every other surface's does.
    function answer(remembered) {
        const ratio = Screen.devicePixelRatio || 1;
        const centre = Qt.point(boardAt.x + paper.boardWidth / 2, boardAt.y + paper.boardHeight / 2);
        Controller.answerReview(remembered, Math.round(centre.x * ratio), Math.round(centre.y * ratio));
    }

    /**
     * Starts the question for `lemma`, or ends the one up when it is empty.
     * @param lemma The word to put on the board.
     */
    function begin(lemma) {
        if (lemma === "") {
            asking = false;
            placed = false;
            expanded = false;
            return;
        }
        word = lemma;
        asking = true;
        relayout();
    }

    /// The work area of the screen a point is on. workAreaAt is SurfaceHost.qml's function handed in, which qmllint cannot type.
    function areaAt(point) {
        // qmllint disable use-proper-function
        return workAreaAt(point);
        // qmllint enable use-proper-function
    }

    /// Chooses the sign's side and the card's side, then the nudge that keeps them on the screen. Runs when a question starts
    /// and when the pet is resized; the choice is then kept until the next question.
    function relayout() {
        if (!asking)
            return;
        workArea = areaAt(Qt.point(petWindow.x + petWindow.width / 2, petWindow.y + petWindow.height / 2));
        nudge = Qt.point(0, 0);
        const choice = layout.choose();
        if (!choice) {
            placed = false;
            return;
        }
        side = choice.side;
        cardSide = choice.on;
        nudge = layout.nudgeFor(side, cardSide);
        placed = true;
    }

    /// Places the answer's bubble on the sign's side of the pet (UI-SPEC.md 4.13). The side is kept, so repositionAnswerCard() puts
    /// the bubble back after a screen change.
    function placeAnswerCard(surface) {
        answerCard = surface;
        repositionAnswerCard();
    }

    /// The answer's bubble beside the pet on the sign's side, centred on the pet, and kept whole on the screen. It goes to the
    /// other side only when the sign's side has no room and the other side has.
    function repositionAnswerCard() {
        const surface = answerCard;
        if (!surface || !surface.visible || surface.dragging || surface.cardKind !== "reviewAnswer")
            return;
        const pet = Qt.rect(petWindow.x, petWindow.y, petWindow.width, petWindow.height);
        const area = areaAt(Qt.point(pet.x + pet.width / 2, pet.y + pet.height / 2));
        const left = area.left + layout.pad;
        const top = area.top + layout.pad;
        const right = area.right - layout.pad;
        const bottom = area.bottom - layout.pad;
        const shadow = surface.shadowMargin;
        const cardWidth = surface.width - 2 * shadow;
        const cardHeight = surface.height - 2 * shadow;
        const roomLeft = pet.x - surface.gap - left;
        const roomRight = right - (pet.x + pet.width + surface.gap);
        let goLeft = side === "left";
        if (goLeft ? roomLeft < cardWidth : roomRight < cardWidth) {
            if (goLeft ? roomRight >= cardWidth : roomLeft >= cardWidth)
                goLeft = !goLeft;
        }
        const cardX = goLeft ? pet.x - surface.gap - cardWidth : pet.x + pet.width + surface.gap;
        const cardY = Numbers.clamp(pet.y + (pet.height - cardHeight) / 2, top, bottom - cardHeight);
        surface.x = Math.round(Numbers.clamp(cardX, left, right - cardWidth) - shadow);
        surface.y = Math.round(cardY - shadow);
        surface.raise();
    }

    // The pointer on the sign opens the card; the card stays open while the pointer is on either, and shuts 120 ms after both are left.
    readonly property bool pointed: hover.hovered || answers.hovered

    onPointedChanged: {
        if (pointed) {
            collapse.stop();
            expanded = true;
        } else {
            collapse.restart();
        }
    }

    Timer {
        id: collapse

        interval: 120
        onTriggered: sign.expanded = false
    }

    Item {
        anchors.fill: parent

        HoverHandler {
            id: hover
        }
    }

    ReviewCard {
        id: answers

        visible: sign.expanded && sign.visible
        x: Math.round(sign.cardAt.x)
        y: Math.round(sign.cardAt.y)

        onAnswered: function (remembered) {
            sign.answer(remembered);
        }
    }

    /// The placement rules, shared with the settings preview. Declared before the board, which takes its width from them.
    ReviewSignLayout {
        id: layout

        scale: Pet.scale
        figure: Qt.point(sign.petWindow.x, sign.petWindow.y)
        ink: Pet.inkBox
        area: sign.workArea
        word: sign.word
        cardWidth: answers.width
        cardHeight: answers.height
        withCard: true
    }

    ReviewSignBoard {
        id: paper

        word: sign.word
        petScale: Pet.scale
        designWidth: layout.designWidth
    }
}
