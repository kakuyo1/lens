import QtQuick
import QtTest
import Lens

/**
 * The pencil layer's duty (UI-SPEC.md 4.20): the mark button rises after the dwell, rides out the
 * frames that lose its word for the grace's length, re-anchors when the word returns, and goes
 * down at once on a clear. The press-hold freeze is the pill's own QML behaviour and is verified
 * by hand -- a TestCase cannot press another window's pill (docs/adr/0161).
 */
Item {
    width: 300
    height: 300

    TestCase {
        name: "Pencil"

        readonly property rect screenRect: Qt.rect(0, 0, 1920, 1080)
        readonly property point origin: Qt.point(0, 0)

        function init() {
            // A pin far from every box this file places: no word sits under the pointer between cases.
            Controller.pencil.pinCursorForTest(Qt.point(-10000, -10000))
            Controller.pencil.clear()
        }

        function test_buttonRisesAfterTheDwellAndRidesOutAMissedFrame() {
            const word = {lemma: "crumple", surface: "crumpled", verdict: "new", x: 500, y: 300, width: 80, height: 24}
            Controller.pencil.pinCursorForTest(Qt.point(540, 312))
            Controller.pencil.placeWords([word], screenRect, origin, 1.0)
            wait(120)
            verify(Controller.pencil.markButton.lemma === undefined) // the dwell is still running
            wait(600)
            compare(Controller.pencil.markButton.lemma, "crumple")

            // A frame that lost the word: inside the grace the button stands where it was.
            Controller.pencil.placeWords([], screenRect, origin, 1.0)
            wait(120)
            compare(Controller.pencil.markButton.lemma, "crumple")

            // The word back, under the pointer: the button re-anchors on the fresh box without
            // waiting the dwell again.
            Controller.pencil.pinCursorForTest(Qt.point(740, 312))
            const moved = Object.assign({}, word, {x: 700})
            Controller.pencil.placeWords([moved], screenRect, origin, 1.0)
            wait(150)
            compare(Controller.pencil.markButton.lemma, "crumple")
            verify(Math.abs(Controller.pencil.markButton.x - 700) < 0.5)

            // Past the grace with the word still gone, the next frame takes the button down.
            Controller.pencil.placeWords([], screenRect, origin, 1.0)
            wait(500)
            Controller.pencil.placeWords([], screenRect, origin, 1.0)
            wait(60)
            verify(Controller.pencil.markButton.lemma === undefined)
        }

        function test_clearTakesTheButtonDownAtOnce() {
            const word = {lemma: "serenity", surface: "serenity", verdict: "new", x: 500, y: 300, width: 90, height: 24}
            Controller.pencil.pinCursorForTest(Qt.point(545, 312))
            Controller.pencil.placeWords([word], screenRect, origin, 1.0)
            wait(650)
            compare(Controller.pencil.markButton.lemma, "serenity")
            // A scroll or a switch takes the page away: no grace there (UI-SPEC.md 4.20).
            Controller.pencil.clear()
            verify(Controller.pencil.markButton.lemma === undefined)
        }
    }
}
