import QtQuick
import QtTest
import Lens
import "../"

/**
 * The first pick of an installed pet package asks once (UI.md 4.12, docs/adr/0043): the question holds the pet back until
 * Enable, Cancel or Escape answers it, and a press on the backdrop does not close it.
 */
Item {
    id: root
    width: 800
    height: 600

    Component {
        id: dialogComponent

        PetPackageDialog {
            petId: "lounge"
            petName: "Lounge"
        }
    }

    TokenHost { id: host }

    TestCase {
        id: testCase
        name: "PetPackage"
        when: windowShown

        /// The dialog as the page builds it, opened the way the page opens it.
        function openDialog() {
            const dialog = createTemporaryObject(dialogComponent, host.contentItem);
            dialog.open();
            waitForRendering(host.contentItem);
            return dialog;
        }

        function test_theNameIsShownAsPlainTextInTheSentence() {
            const dialog = openDialog();
            const sentence = findChild(dialog, "petPackageQuestion");
            verify(sentence, "the question is not on screen");
            verify(sentence.text.indexOf("Lounge") >= 0, "the question does not name the package");
            compare(sentence.textFormat, Text.PlainText);
        }

        function test_enableConfirmsOnceAndCloses() {
            const dialog = openDialog();
            let enabled = 0;
            dialog.enableRequested.connect(function () { enabled += 1; });

            mouseClick(findChild(dialog, "enablePetPackageButton"), 10, 10);

            compare(enabled, 1);
            compare(dialog.opened, false);
        }

        function test_cancelClosesWithoutConfirming() {
            const dialog = openDialog();
            let enabled = 0;
            dialog.enableRequested.connect(function () { enabled += 1; });

            mouseClick(findChild(dialog, "cancelPetPackageButton"), 10, 10);

            compare(enabled, 0);
            compare(dialog.opened, false);
        }

        function test_aPressOnTheBackdropDoesNotClose() {
            const dialog = openDialog();

            mouseClick(host.contentItem, 2, 2);

            compare(dialog.opened, true);
            dialog.close();
        }

        function test_escapeCancels() {
            const dialog = openDialog();
            let enabled = 0;
            dialog.enableRequested.connect(function () { enabled += 1; });

            keyClick(Qt.Key_Escape);

            compare(enabled, 0);
            compare(dialog.opened, false);
        }
    }
}
