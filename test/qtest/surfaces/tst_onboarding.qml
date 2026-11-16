import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The first-run wizard (UI-SPEC.md 4.19, docs/adr/0148).
 *
 * The controller owns the guidance record; this case exercises the surface contract over it: the
 * steps write their settings as they are touched, the sample raises the demo card without a
 * request, and finishing or skipping the wizard both end in a recorded document and a closed
 * window. The record has no unset path, so the finish case runs first, while the document is
 * still unrecorded, and the later cases only re-check the window.
 */
Item {
    id: root
    width: 520
    height: 420

    Component {
        id: wizardComponent
        OnboardingWizard {}
    }

    TestCase {
        id: testCase
        name: "Onboarding"
        when: windowShown

        /// @return The wizard under test, raised and settled into its first step.
        function opened() {
            const wizard = createTemporaryObject(wizardComponent, root);
            verify(!!wizard, "Component exists");
            wizard.show();
            wait(50);
            compare(wizard.visible, true);
            compare(wizard.step, 1);
            return wizard;
        }

        function byName(wizard, name) {
            const found = Util.findAll(wizard, function (object) {
                return object.objectName === name;
            });
            verify(found.length === 1, "the wizard owns exactly one " + name);
            return found[0];
        }

        /// The finish fade hides the window at the end of the motion token (ShadowCard.qml).
        readonly property int settle: Tokens.motion.pop + 80

        function test_finishingWritesTheRecordAndTakesTheWindowDown() {
            const wizard = opened();
            verify(!Controller.onboardingDuty.isDone(), "the fixture starts unrecorded");

            // The skip path is the same call the close icon and Escape make: record, then fade.
            wizard.finish();
            compare(Controller.onboardingDuty.isDone(), true);
            tryCompare(wizard, "visible", false, testCase.settle);
        }

        function test_stepsWriteTheirSettingsAsTheyAreTouched() {
            const wizard = opened();

            // The level the reader picks is a setting, not wizard state: it is written at once.
            byName(wizard, "levelField").picked(5);
            compare(Controller.settings.level, 5);

            wizard.forward();
            compare(wizard.step, 2);
            compare(byName(wizard, "backButton").visible, true);
            verify(!Controller.settings.hasApiKey, "the fixture starts without a key");

            // Typing fills the draft (the field's onTextEdited copies it here); the commit is
            // what this surface owns: leaving the step writes it, as the settings panel's own
            // key field does. The draft stands in for the typing a headless case cannot send
            // to a second window.
            compare(byName(wizard, "keyInput").text, "", "the field starts on the masked placeholder");
            wizard.apiDraft = "sk-test-key-123";
            wizard.forward();
            compare(Controller.settings.hasApiKey, true);
            compare(wizard.apiDraft, "");

            compare(wizard.step, 3);
            compare(byName(wizard, "nextButton").visible, true);
            compare(byName(wizard, "doneButton").visible, false);

            wizard.forward();
            compare(wizard.step, 4);
            verify(!byName(wizard, "nextButton").visible);
            compare(byName(wizard, "doneButton").visible, true);

            wizard.back();
            compare(wizard.step, 3);
        }

        function test_theSampleSelectionRaisesTheDemoCard() {
            const wizard = opened();
            wizard.forward();
            wizard.forward();
            const sample = byName(wizard, "sampleText");

            // "quiet" sits at characters 17..22 of the sample's first line; the quotes around it
            // are what the surface strips, so the selection matches with or without them.
            sample.select(17, 22);
            const bubbles = Util.findAll(wizard, function (object) {
                return object.toString().indexOf("Bubble_QMLTYPE") === 0 && object.preview === true;
            });
            verify(bubbles.length === 1, "the wizard hosts exactly one demo bubble");
            tryCompare(bubbles[0], "visible", true);
            compare(bubbles[0].title, "quiet");

            // A selection that is not the word raises nothing further.
            sample.select(0, 3);
            compare(bubbles[0].visible, true);

            // Leaving the try-it step takes the demo card with it.
            wizard.forward();
            compare(wizard.step, 4);
            compare(bubbles[0].visible, false);
        }

        /// The fixed card keeps a long step body inside it: the body is a clipped scroll area, and
        /// the second step -- the tallest one -- scrolls when its content exceeds the viewport.
        function test_theStepBodyScrollsInsideTheFixedCard() {
            const wizard = opened();
            const body = byName(wizard, "onboardingBody");
            compare(body.clip, true, "the step body does not clip its content");

            wizard.forward();
            wait(20);
            compare(wizard.step, 2);
            verify(body.contentHeight >= body.height);
            compare(byName(wizard, "onboardingScrollThumb").visible,
                    body.contentHeight > body.height,
                    "the scroll thumb disagrees with the body it measures");

            if (body.contentHeight > body.height) {
                body.contentY = 0;
                mouseWheel(body, body.width / 2, body.height / 2, 0, -120);
                tryVerify(() => body.contentY > 0, 1000, "the wheel did not scroll the step body");
            }
        }

        /// The fourth step's tray close-up (UI-SPEC.md 4.19): a drawing that opens its menu once,
        /// names the live tray state in its first row, and leaves the menu open after the tour.
        function test_theTrayCloseUpOpensTheMenuOnce() {
            const wizard = opened();
            wizard.forward();
            wizard.forward();
            wizard.forward();
            const demo = byName(wizard, "trayDemo");

            tryCompare(demo, "open", true, 5000);
            compare(demo.highlight, "");
            verify(Util.textWith(Util.textsUnder(demo), [demo.stateLabel]),
                   "the close-up's first row is not the live tray state");
            verify(Util.textWith(Util.textsUnder(demo), [qsTranslate("TrayMenu", "Settings")]),
                   "the close-up has no Settings row");

            if (!wizard.tokens.motion.reduced) {
                // The tour visits the state row and Settings before it settles.
                tryCompare(demo, "highlight", "settings", 3000);
                tryCompare(demo, "highlight", "", 2000);
            }

            // Going back and forward again keeps the diagram open instead of replaying it.
            wizard.back();
            wizard.forward();
            compare(wizard.trayDemoPlayed, true);
            compare(demo.open, true);
        }

        /// With animation switched off the close-up is the still diagram: no cue, no tour.
        function test_theTrayCloseUpIsStillWhenMotionIsReduced() {
            const wizard = opened();
            wizard.tokens.motion.reduced = true;
            wizard.forward();
            wizard.forward();
            wizard.forward();
            const demo = byName(wizard, "trayDemo");
            compare(demo.open, true);
            compare(demo.highlight, "");
        }

        /// The source strings fit both steps: the try-it keeps its room, and the tray close-up gets
        /// a step of its own.
        function test_theTryAndTrayStepsFitTheCard() {
            const wizard = opened();
            const body = byName(wizard, "onboardingBody");
            wizard.forward();
            wizard.forward();
            compare(body.contentHeight <= body.height, true,
                    "step 3 overflows the card: " + body.contentHeight + " > " + body.height);
            wizard.forward();
            compare(wizard.step, 4);
            compare(body.contentHeight <= body.height, true,
                    "step 4 overflows the card: " + body.contentHeight + " > " + body.height);
        }

        function test_theDoneChipPressesThrough() {
            const wizard = opened();
            wizard.forward();
            wizard.forward();
            wizard.forward();

            const done = Util.textWith(Util.textsUnder(wizard), ["Done"]);
            verify(done, "the Done chip was not drawn");
            mouseClick(done.parent, done.parent.width / 2, done.parent.height / 2);
            tryCompare(wizard, "visible", false, testCase.settle);
            compare(Controller.onboardingDuty.isDone(), true);
        }
    }
}
