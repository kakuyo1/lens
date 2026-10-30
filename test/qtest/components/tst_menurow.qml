import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * One row of the tray menu: a glyph and a label, an optional value on the right, and the
 * danger colour the Quit row is drawn in. The menu itself cannot be photographed on this
 * machine (TEST.md section 5), so the row's own behaviour is all that can be checked at all.
 */
Item {
    id: root
    width: 260
    height: 120

    Component {
        id: rowComponent
        MenuRow { width: 220; label: "Settings" }
    }

    TokenHost { id: host }

    TestCase {
        name: "MenuRow"
        when: windowShown

        function make(properties) {
            const row = createTemporaryObject(rowComponent, host.contentItem, properties);
            waitForRendering(host.contentItem);
            verify(row);
            return row;
        }

        function test_theNoteIsShownOnlyWhenThereIsOne() {
            const bare = make({});
            const bareTexts = Util.textsUnder(bare);
            compare(bareTexts.length, 1);

            const noted = make({ note: "2" });
            const notedTexts = Util.textsUnder(noted);
            compare(notedTexts.length, 2);
            compare(notedTexts[1].visible, true);
            compare(notedTexts[1].text, "2");
        }

        function test_theDangerRowIsDrawnInTheDangerColour() {
            compare(Util.textsUnder(make({}))[0].color, Tokens.text);
            compare(Util.textsUnder(make({ danger: true }))[0].color, Tokens.danger);
        }

        function test_pickingTheRowReports() {
            const row = make({});
            let picks = 0;
            row.picked.connect(function () { picks++; });

            mouseClick(row, row.width / 2, row.height / 2);

            compare(picks, 1);
        }

        /// The tray menu is pointer-first by decision (ADR 0108). A row has no Tab stop and no
        /// keyboard handler; the pointer and the screen reader's press action are its paths.
        function test_theRowStaysPointerFirst() {
            const row = make({});
            let picks = 0;
            row.picked.connect(function () { picks++; });

            compare(row.activeFocusOnTab, false, "a tray row claimed a Tab stop");
            keyClick(Qt.Key_Return);
            keyClick(Qt.Key_Space);
            compare(picks, 0, "a tray row answered the keyboard");
        }

        /// The value on the right is part of the row's name, so a reader is told the day's
        /// tally and not just the word "Statistics".
        function test_theNameCarriesTheValueOnTheRight() {
            compare(make({}).Accessible.name, "Settings");
            compare(make({ note: "128 words" }).Accessible.name, "Settings, 128 words");
            compare(make({}).Accessible.role, Accessible.Button);
        }

        /// The note's runs share one baseline, so the CJK "次" sits on the line of the digits beside it. Top-aligned
        /// runs put the CJK glyphs lower.
        function test_theNoteRunsShareOneBaseline() {
            const row = make({ label: "Stats", note: "14 次 · ¥0.00" });
            const runs = Util.textsUnder(row).slice(1);
            verify(runs.length >= 2, "the note did not split into runs");
            const line = runs[0].y + runs[0].baselineOffset;
            for (let i = 1; i < runs.length; ++i)
                verify(Math.abs(runs[i].y + runs[i].baselineOffset - line) < 0.5,
                       "the run '" + runs[i].text + "' sits off the note's baseline");
        }

        /// The figures row in Spanish, with the label the catalogue ships for it: the label has to stand whole beside
        /// the figures, since a label cut short hides the word the reader is looking for.
        function test_theSpanishStatsLabelIsNotElided() {
            const row = make({ label: "Cifras", note: "14 consultas · €0.00" });
            const label = Util.textsUnder(row)[0];
            verify(!label.truncated, "the label was elided: it needs " + label.implicitWidth
                   + " px and has " + label.width + " px");
        }

        /// A label longer than the room the note leaves elides before the note, rather than running under it.
        function test_aLongLabelElidesBeforeTheNote() {
            const row = make({ label: "Estadísticas de uso diario", note: "128 consultas · €0.42" });
            const texts = Util.textsUnder(row);
            const label = texts[0];
            const noteStart = texts[1].mapToItem(row, 0, 0).x;
            compare(label.elide, Text.ElideRight);
            verify(label.mapToItem(row, 0, 0).x + label.width <= noteStart, "the label runs under the note");
        }
    }
}
