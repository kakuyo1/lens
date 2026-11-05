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
            verify(!byName(wizard, "nextButton").visible);
            compare(byName(wizard, "doneButton").visible, true);

            wizard.back();
            compare(wizard.step, 2);
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
        }

        function test_theDoneChipPressesThrough() {
            const wizard = opened();
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
