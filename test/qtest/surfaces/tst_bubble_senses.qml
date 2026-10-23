import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 640
    height: 600

    Component { id: bubbleComponent; Bubble {} }

    TestCase {
        id: testCase
        name: "BubbleSenses"
        when: windowShown

        function opened(count) {
            const bubble = createTemporaryObject(bubbleComponent, root);
            verify(bubble);
            const senses = [
                {en: "a business that keeps and lends money", text: "一家接受存款并提供贷款的金融机构"},
                {en: "the land beside a river", text: "河流两侧的岸边"},
                {en: "a supply kept for future use", text: "储备起来供以后使用的资源"}
            ];
            bubble.show({title: "bank", type: "word", ipa: "/bæŋk/", senses: senses.slice(0, count), status: "new", x: 200, y: 500});
            tryCompare(bubble, "visible", true);
            bubble.dragging = true;
            return bubble;
        }

        function named(bubble, name) {
            return Util.findAll(bubble, function(item) { return item.objectName === name; });
        }

        function test_orderSeparatorsAndFirstIpa() {
            const bubble = opened(3);
            const english = named(bubble, "senseEnglish");
            compare(english.length, 3);
            compare(english[0].text, "a business that keeps and lends money");
            compare(english[1].text, "the land beside a river");
            const separators = named(bubble, "senseSeparator").filter(function(item) { return item.visible; });
            compare(separators.length, 2);
            for (let i = 0; i < separators.length; ++i) {
                compare(separators[i].height, 1);
                compare(separators[i].color, Tokens.line2);
            }
            const ipa = Util.textsUnder(bubble).filter(function(item) { return item.visible && item.text === "/bæŋk/"; });
            compare(ipa.length, 1);
        }

        function test_moreSensesGrowHeightAndKeepWidth() {
            const one = opened(1);
            const three = opened(3);
            compare(one.width, three.width);
            verify(three.height > one.height);
            compare(named(one, "senseSeparator").filter(function(item) { return item.visible; }).length, 0);
        }

        function test_refreshClearsSensesForASentence() {
            const bubble = opened(3);
            bubble.show({title: "a sentence", type: "sentence", translation: "Una explicación clara.", x: 200, y: 500});
            compare(bubble.senses.length, 0);
            compare(bubble.translation, "Una explicación clara.");
            compare(bubble.ipa, "");
        }

        function test_snapshotBothThemes() {
            if (!lensQaSnapshotDir) skip("Set LENS_QA_SNAPSHOT_DIR to save snapshots");
            const theme = Tokens.theme;
            try {
                for (const mode of ["light", "dark"]) {
                    Tokens.theme = mode;
                    const bubble = opened(3);
                    // The card writes its answer out a character at a time: photograph it settled.
                    tryVerify(function () { return !bubble.revealing; }, 8000);
                    waitForRendering(bubble.contentItem);
                    compare(Tokens.dark, mode === "dark");
                    verify(Util.saveSnapshot(testCase, bubble.contentItem, lensQaSnapshotDir, "bubble-senses-" + mode));
                    compare(named(bubble, "senseEnglish").length, 3);
                }
            } finally {
                Tokens.theme = theme;
            }
        }

        /// The picture entry, in both themes (IMAGES.md 2.1). Snapshotted because what it has to get
        /// right is optical: a 13 px glyph, an 11 px label and the sense's own text sharing one
        /// baseline, which is exactly what a number in a test cannot check.
        function test_snapshotPictureEntry() {
            if (!lensQaSnapshotDir) skip("Set LENS_QA_SNAPSHOT_DIR to save snapshots");
            const theme = Tokens.theme;
            try {
                for (const mode of ["light", "dark"]) {
                    Tokens.theme = mode;
                    const bubble = createTemporaryObject(bubbleComponent, root);
                    verify(bubble);
                    bubble.show({title: "crane",
                                 type: "word",
                                 ipa: "/kreɪn/",
                                 status: "new",
                                 x: 200,
                                 y: 500,
                                 senses: [{en: "a tall machine with a long arm, used for lifting heavy things",
                                           text: "起重机；吊车",
                                           imageable: true,
                                           imageQuery: "construction crane"},
                                          {en: "a large bird with long legs and a long neck",
                                           text: "鹤",
                                           imageable: true,
                                           imageQuery: "crane bird"}]});
                    tryCompare(bubble, "visible", true);
                    // Hovering a card stops its countdown; the drag flag is the harness's way to say
                    // the card is being held, so it is not taken down mid-snapshot.
                    bubble.dragging = true;
                    tryVerify(function () { return !bubble.revealing; }, 8000);
                    waitForRendering(bubble.contentItem);
                    verify(Util.saveSnapshot(testCase, bubble.contentItem, lensQaSnapshotDir, "bubble-pictures-" + mode));

                    // What the eye judges, as numbers. The glyph is not drawn on the offscreen
                    // platform -- it is an Icon, and Icon colorizes through a MultiEffect, which the
                    // software backend cannot run -- so its placement is asserted instead of
                    // photographed (IMAGES.md 2.1).
                    const entries = named(bubble, "sensePicturesEntry").filter(function(item) { return item.visible; });
                    compare(entries.length, 2, "one entry per imageable sense");
                    const glyphs = named(bubble, "sensePicturesGlyph");
                    compare(glyphs.length, 2);
                    const labels = Util.textsUnder(entries[0]).filter(function(text) { return text.text === "Pictures"; });
                    compare(labels.length, 1);

                    const content = bubble.contentItem;
                    const glyphAt = glyphs[0].mapToItem(content, 0, 0);
                    const englishAt = named(bubble, "senseEnglish")[0].mapToItem(content, 0, 0);
                    fuzzyCompare(glyphAt.x, englishAt.x, 0.6, "the glyph does not stand on the card's text baseline");

                    const labelAt = labels[0].mapToItem(content, 0, 0);
                    fuzzyCompare(glyphAt.y + glyphs[0].height / 2,
                                 labelAt.y + labels[0].height / 2,
                                 0.6,
                                 "the glyph and the label do not share a centre line");

                    // The block is the pill and nothing more: the space around the entry comes from
                    // the sense above it and the strip below it, not from dead space of its own.
                    compare(entries[0].height, 22, "the entry grew past its pill");
                }
            } finally {
                Tokens.theme = theme;
            }
        }
        /// The strip's failure state, seen when the source cannot be reached or answers with
        /// nothing usable. It draws no picture, so the whole block is photographable offscreen --
        /// the thumbnails are not -- and it is where the fallback pill has to stand on the same
        /// left baseline, in the same style, as the entry that opened it (IMAGES.md 2.4, 4.5).
        function test_snapshotPictureStripFailure() {
            if (!lensQaSnapshotDir) skip("Set LENS_QA_SNAPSHOT_DIR to save snapshots");
            const theme = Tokens.theme;
            try {
                for (const mode of ["light", "dark"]) {
                    Tokens.theme = mode;
                    const bubble = createTemporaryObject(bubbleComponent, root);
                    verify(bubble);
                    // A query with nothing behind it: the source either answers with no usable picture
                    // or cannot be reached at all, and both are the same one line.
                    bubble.show({title: "crane",
                                 type: "word",
                                 ipa: "/kreɪn/",
                                 status: "new",
                                 x: 200,
                                 y: 500,
                                 senses: [{en: "a tall machine with a long arm",
                                           text: "起重机",
                                           imageable: true,
                                           imageQuery: "zzqqxx no such photograph"}]});
                    tryCompare(bubble, "visible", true);
                    bubble.dragging = true;

                    const entry = named(bubble, "sensePicturesEntry").filter(function(item) { return item.visible; })[0];
                    verify(!!entry, "the entry is not drawn");

                    // The fallback is the entry's own pill in another state, so the two are compared
                    // property by property rather than against numbers copied out of the design. The
                    // style comparison comes first, while the entry is still closed: both controls are
                    // then idle, so their fills and label colours are comparable -- an open entry is
                    // filled on purpose, and the fallback has no open state to match it with.
                    const idleEntryPill = named(bubble, "picturesEntryPill")[0];
                    const idleFallbackPill = named(bubble, "picturesFallbackPill")[0];
                    const idleEntryLabel = Util.textsUnder(entry).filter(function(text) { return text.text === "Pictures"; })[0];
                    const idleFallbackLabel = Util.textsUnder(named(bubble, "picturesFallback")[0])[0];
                    verify(!!idleEntryPill && !!idleFallbackPill, "a pill is not drawn");
                    verify(!!idleEntryLabel && !!idleFallbackLabel, "a label is not drawn");
                    compare(idleFallbackPill.height, idleEntryPill.height, "the fallback pill is not the entry's height");
                    compare(idleFallbackPill.radius, idleEntryPill.radius, "the fallback pill is not the entry's shape");
                    compare(idleFallbackPill.border.width, idleEntryPill.border.width, "the fallback pill has a border the entry has not got");
                    compare(String(idleFallbackPill.color), String(idleEntryPill.color), "the fallback pill is filled differently from the entry");
                    compare(idleFallbackLabel.font.pixelSize, idleEntryLabel.font.pixelSize, "the fallback label is not the entry label's size");
                    compare(String(idleFallbackLabel.color), String(idleEntryLabel.color), "the fallback label is not the entry label's colour");

                    entry.toggle();
                    tryVerify(function () { return Controller.pictureDuty.pictures.state === "failed"; }, 15000);
                    waitForRendering(bubble.contentItem);
                    verify(Util.saveSnapshot(testCase, bubble.contentItem, lensQaSnapshotDir, "bubble-pictures-failed-" + mode));

                    // Both stand on the card's text baseline, as the sense above them does: the strip's
                    // own padding hangs off that edge exactly as the entry's does.
                    const content = bubble.contentItem;
                    const baseline = named(bubble, "senseEnglish")[0].mapToItem(content, 0, 0).x;
                    const fallbackLabel = Util.textsUnder(named(bubble, "picturesFallback")[0])[0];
                    fuzzyCompare(fallbackLabel.mapToItem(content, 0, 0).x, baseline, 0.6, "the fallback label is not on the card's text baseline");
                    const line = Util.textsUnder(bubble).filter(function(text) { return text.text === "Pictures unavailable"; })[0];
                    verify(!!line, "the failure line is not drawn");
                    fuzzyCompare(line.mapToItem(content, 0, 0).x, baseline, 0.6, "the failure line is not on the card's text baseline");
                }
            } finally {
                Tokens.theme = theme;
                Controller.pictureDuty.clear();
            }
        }
    }
}
