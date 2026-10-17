import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The pet's review sign and the answer card and bubble that follow it (UI.md 4.13, ADR 0052, 0053).
 *
 * The sign hangs from the pet's hand: the cases read which hand was chosen, where the pole's tail sits on the grip, and where
 * the card window opens. The pet is a stand-in the size of the figure on screen; the grips come from the real pet, shown in its
 * ask action with the preview. The snapshots are the visual check and skip without LENS_QA_SNAPSHOT_DIR.
 */
Item {
    id: root

    /// The figure the sign hangs from: as big as the pet on screen, and moved by each case.
    Item {
        id: fakePet

        width: Math.round(Pet.canvas * Pet.scale)
        height: width
        y: 300
    }

    Component {
        id: signComponent

        ReviewSignWindow {
            petWindow: fakePet
            workAreaAt: function (point) { return {left: 0, top: 0, right: Screen.desktopAvailableWidth, bottom: Screen.desktopAvailableHeight}; }
        }
    }

    Component {
        id: bubbleComponent

        Bubble {}
    }

    /// The answer's card as the sign places it: a window with the geometry Bubble gives it, and no verdict buttons.
    Component {
        id: answerComponent

        Window {
            property int gap: 10
            property int shadowMargin: 0
            property bool verdictButtons: false
            property bool dragging: false

            width: 200
            height: 60
            visible: true
        }
    }

    TestCase {
        id: testCase

        name: "ReviewSign"
        when: windowShown

        /// The sign on screen, and the bubble of an answer, as each case leaves them.
        property var sign: null
        property var bubble: null

        function init() {
            Pet.setEnabled(true);
            Pet.preview("ask");
            Pet.setPreviewing(true);
        }

        function cleanup() {
            if (sign) {
                sign.destroy();
                sign = null;
            }
            Pet.setPreviewing(false);
            Pet.setEnabled(false);
        }

        /// The grip of hand `hand` on screen, in the frame the pet shows (pet-review.html's gripX and gripY).
        function gripOf(hand) {
            const grip = Pet.handGrips[hand];
            return Qt.point(fakePet.x + grip.x * Pet.scale, fakePet.y + grip.y * Pet.scale);
        }

        /// Puts a question up on a sign hung from a pet at @p petX, and waits for the hand to be placed and the sign to show.
        /// The sign must land on the screen, which is checked here so a misplaced sign fails with its geometry.
        function askFromPetAt(petX) {
            fakePet.x = petX;
            sign = signComponent.createObject(null);
            verify(sign);
            sign.begin("serendipity");
            tryVerify(function () { return sign.visible; }, 2000);
            verify(sign.x >= 0 && sign.x + sign.width <= Screen.desktopAvailableWidth && sign.y >= 0 && sign.y + sign.height <= Screen.desktopAvailableHeight,
                   "the sign is on the screen: " + sign.x + "," + sign.y + " " + sign.width + "x" + sign.height + " on " + Screen.desktopAvailableWidth + "x" + Screen.desktopAvailableHeight);
            wait(50);
            return sign;
        }

        /// Moves the pointer onto the board, which opens the card, and waits for the card to be drawn.
        function openCard() {
            mouseMove(sign.contentItem, 20, 20);
            tryCompare(sign, "expanded", true);
            wait(50);
        }

        /// A pet with room on its left takes the left hand, and its outer card opens on the left.
        function test_handIsTheLeftOneWhenThereIsRoom() {
            askFromPetAt(Math.round(Screen.desktopAvailableWidth / 2));
            compare(sign.side, "left");
            compare(sign.cardSide, "left");
            compare(Pet.handSide, "left");
        }

        /// At the left edge the left hand has no room, so the right hand takes the sign and its card opens to the right.
        function test_handSwitchesToTheRightAtTheLeftEdge() {
            askFromPetAt(8);
            compare(sign.side, "right");
            compare(sign.cardSide, "right");
            compare(Pet.handSide, "right");
        }

        /// The pole stands on the grip: its tail sits on the hand's grip pixel and its centre on the grip's column.
        function test_poleStandsOnTheGrip() {
            askFromPetAt(Math.round(Screen.desktopAvailableWidth / 2));
            const grip = gripOf(sign.side);
            // The window holds whole pixels, so the board's own geometry is read rather than the window's.
            const board = Util.findAll(sign.contentItem, function (o) { return o.toString().indexOf("ReviewSignBoard_QMLTYPE") === 0; })[0];
            verify(board, "the sign has no board");
            const inset = 16 * board.boardScale;
            const pole = sign.side === "right" ? sign.x + inset : sign.x + board.boardWidth - inset;
            const tail = sign.y + board.boardHeight + board.poleHeight;
            verify(Math.abs(tail - grip.y) < 1, "the pole tail is at " + tail + ", not the grip at " + grip.y);
            verify(Math.abs(pole - grip.x) < 1, "the pole is at " + pole + ", not the grip column at " + grip.x);
        }

        /// The hand moves through its frames, so the sign follows it frame by frame.
        function test_signBobsWithTheHandFrameByFrame() {
            askFromPetAt(Math.round(Screen.desktopAvailableWidth / 2));
            const before = sign.y;
            tryVerify(function () { return sign.y !== before; }, 2000, "the sign did not follow the hand's frames");
        }

        /// The board keeps its place on the pet, so a pet the reader drags takes the sign along.
        function test_signFollowsThePetWhenItMoves() {
            askFromPetAt(100);
            const gapX = sign.x - fakePet.x;
            const gapY = sign.y - fakePet.y;
            fakePet.x = 300;
            fakePet.y = 120;
            compare(sign.x - fakePet.x, gapX, "the board did not follow the pet across the screen");
            compare(sign.y - fakePet.y, gapY, "the board did not follow the pet down the screen");
        }

        /// The card opens in its own window beside the board, on the hand's side, and the sign stays where it is.
        function test_cardOpensBesideTheBoardWithoutMovingTheSign() {
            askFromPetAt(Math.round(Screen.desktopAvailableWidth / 2));
            const x = sign.x;
            openCard();
            compare(sign.card.visible, true);
            compare(sign.x, x, "opening the card moved the sign");
            verify(Math.abs(sign.card.x + sign.card.width + 10 - sign.x) < 1, "the card is not 10 px left of the board: " + sign.card.x);
        }

        /// The pointer leaving the sign closes the card again, 120 ms later.
        function test_leavingTheSignClosesTheCard() {
            askFromPetAt(100);
            openCard();
            mouseMove(sign.contentItem, -10, -10);
            tryCompare(sign, "expanded", false);
        }

        /// The answer's card goes on the hand's side and is centred on the pet, while the work area has room there.
        function test_answerCardGoesOnTheHandsSideCentredOnThePet() {
            askFromPetAt(Math.round(Screen.desktopAvailableWidth / 2 - fakePet.width / 2));
            const card = createTemporaryObject(answerComponent, root);
            verify(card);
            sign.side = "left";
            sign.placeAnswerCard(card);
            compare(card.x, fakePet.x - 10 - card.width, "the card is not on the left of the pet");
            compare(card.y, Math.round(fakePet.y + (fakePet.height - card.height) / 2), "the card is not centred on the pet");
            sign.side = "right";
            sign.placeAnswerCard(card);
            compare(card.x, fakePet.x + fakePet.width + 10, "the card is not on the right of the pet");
        }

        /// With no room on the hand's side, the card goes to the other side of the pet.
        function test_answerCardFlipsWhenItHasNoRoom() {
            askFromPetAt(8);
            const card = createTemporaryObject(answerComponent, root);
            verify(card);
            sign.side = "left";
            sign.placeAnswerCard(card);
            verify(card.x >= fakePet.x + fakePet.width, "the card did not flip to the right of the pet: " + card.x);
        }

        /// The card an answer raises: the stored explanation, counting down, with no Known or New buttons.
        function showAnswerBubble() {
            bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            bubble.show({
                title: "serendipity",
                type: "word",
                ipa: "/ˌserənˈdɪpəti/",
                en: "the occurrence of events by chance in a happy way",
                zh: "机缘巧合",
                status: "new",
                countsDown: true,
                reveal: false,
                verdictButtons: false,
                x: 200,
                y: 240
            });
            return bubble;
        }

        function test_answerBubbleOffersNoVerdictButtons() {
            compare(showAnswerBubble().verdictButtons, false);
        }

        /// The whole sign scales by K = scale / 3.01 from its top-left, as pet-review.html scales it: a 52 px board, the 26 px
        /// pole under it and the 5 px shadow, all at the default size.
        function test_signScalesWithThePetByK() {
            const chosen = Pet.size;
            try {
                for (const size of [0, 67, 100]) {
                    Pet.setSize(size);
                    askFromPetAt(Math.round(Screen.desktopAvailableWidth / 2));
                    const k = Pet.scale / 3.01;
                    verify(Math.abs(sign.height - (52 + 26 + 5) * k) < 1.5, "the sign is " + sign.height + " tall at scale " + Pet.scale);
                    sign.destroy();
                    sign = null;
                }
            } finally {
                Pet.setSize(chosen);
            }
        }

        function test_captureTheClosedSign() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            askFromPetAt(100);
            wait(100);
            verify(Util.saveSnapshot(testCase, sign.contentItem, lensQaSnapshotDir, "review-sign-closed"));
        }

        /// The smallest pet, the worst case for the board's size against the figure.
        function test_captureTheClosedSignAtTheSmallestPet() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const chosen = Pet.size;
            try {
                Pet.setSize(0);
                askFromPetAt(100);
                wait(100);
                verify(Util.saveSnapshot(testCase, sign.contentItem, lensQaSnapshotDir, "review-sign-closed-size-0"));
            } finally {
                Pet.setSize(chosen);
            }
        }

        function test_captureTheCardOpen() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            askFromPetAt(Math.round(Screen.desktopAvailableWidth / 2));
            openCard();
            wait(100);
            verify(Util.saveSnapshot(testCase, sign.card.contentItem, lensQaSnapshotDir, "review-card-open"));
        }

        function test_captureTheAnswerBubbleWithoutVerdictButtons() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            showAnswerBubble();
            wait(100);
            verify(Util.saveSnapshot(testCase, bubble.contentItem, lensQaSnapshotDir, "review-answer-bubble"));
        }
    }
}
