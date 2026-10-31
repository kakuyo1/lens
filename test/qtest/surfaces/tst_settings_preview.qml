import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

/**
 * The settings preview (UI-SPEC.md 4.4): resting the pointer on a setting's title raises the explanation
 * bubble's card beside the panel, and taking the pointer away puts the card down again.
 *
 * The card is the bubble itself, so these cases are about the wiring that turns a hover into one --
 * the key a row raises, the side the card goes to, which part of the sample each row forces in, and
 * the marks that keep it from reading as an answer the reader called up. The sample's own prose is
 * read back out of `Controller.settingPreview` rather than written here: what is asserted is that
 * the language setting picks the entry, not what the entry says.
 */
Item {
    id: root
    width: 480
    height: 720

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        id: testCase
        name: "SettingsPreview"
        when: windowShown

        function make() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(50);
            return main;
        }

        /// Every engine in the run shares one Controller and one settings document, so a case that
        /// writes one puts it back rather than leaving the next suite reading it.
        function cleanup() {
            Controller.setSetting("theme", "dark"); ///< the fixture's, which the snapshot loop above changes
            Controller.setSetting("explanationLang", "zh");
            Controller.setSetting("multiSense", false);
            Controller.setSetting("etymology", false);
            Controller.setSetting("example", false);
            Controller.setSetting("animationsEnabled", true);
        }

        function settingsPanel(main) {
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel, "Main did not build a SettingsPopup");
            return panel;
        }

        /// @return The settings preview's window. The search is scoped to the panel: the onboarding
        ///         wizard hosts a preview Bubble of its own (docs/adr/0148), so Main no longer has
        ///         exactly one to hand.
        function previewOf(main) {
            const previews = Util.findAll(settingsPanel(main), function (o) {
                return o.toString().indexOf("Bubble_QMLTYPE") === 0 && o.preview === true;
            });
            verify(previews.length === 1, "the settings panel does not own exactly one preview bubble");
            return previews[0];
        }

        /// @return The panel's card, without the transparent margin its shadow is drawn in.
        function panelRect(panel) {
            return Qt.rect(panel.x + panel.shadowMargin, panel.y + panel.shadowMargin,
                           panel.cardWidth, panel.cardHeight);
        }

        function categoryRow(panel, title) {
            const rows = Util.findAll(panel, function (o) {
                return o.toString().indexOf("SettingsCategoryRow_QMLTYPE") === 0;
            });
            for (const row of rows) {
                if (row.title === title)
                    return row;
            }
            return null;
        }

        function openCategory(panel, title) {
            const row = categoryRow(panel, title);
            verify(row, "the settings catalog has no " + title + " category");
            // The catalog is longer than the body, so the last categories start below the fold: a
            // press on a row that is not on screen goes past the panel. Bring it up first.
            const body = scrollerOf(row);
            if (body && body.contentHeight > body.height) {
                const bottom = row.mapToItem(body, 0, row.height).y;
                if (bottom > body.height)
                    body.contentY = Math.min(bottom - body.height, body.contentHeight - body.height);
                wait(20);
            }
            mouseClick(row, row.width / 2, row.height / 2);
            wait(20);
        }

        function scrollerOf(item) {
            for (let it = item.parent; it; it = it.parent) {
                if (it.contentY !== undefined && it.contentHeight !== undefined)
                    return it;
            }
            return null;
        }

        /// The label of the row carrying `text`, which is what the preview is raised by. Only the
        /// panel's own window is read: the preview card is a Bubble and carries labels of its own --
        /// "Etymology" among them -- so the panel's texts and the card's have to be told apart.
        function labelOf(panel, text) {
            const own = Util.ownWindow(panel).filter(function (o) { return typeof o.text === "string"; });
            const label = Util.textWith(own, [text]);
            verify(label, "the settings page has no '" + text + "' row");
            return label;
        }

        /// Scroll the settings body until a label is inside it: the word details sit below the fold of the
        /// learning page, and a pointer on a row the body clips reaches nothing.
        function revealLabel(label) {
            const body = scrollerOf(label);
            if (!body)
                return;
            const bottom = label.mapToItem(body, 0, label.height).y;
            if (bottom > body.height) {
                body.contentY = Math.min(body.contentY + bottom - body.height, body.contentHeight - body.height);
                wait(20);
            }
        }

        /// Rest the pointer on a row's title, which is the whole gesture.
        function hoverTitle(main, panel, text) {
            const label = labelOf(panel, text);
            revealLabel(label);
            mouseMove(label, label.width / 2, label.height / 2);
            return previewOf(main);
        }

        /// @return The explanation-language dropdown, with its list up.
        function languageField(panel) {
            const field = Util.findAll(panel, function (o) {
                return o.objectName === "languageField";
            })[0];
            verify(field, "the learning page has no language field");
            if (field.openChildRect === null) {
                field.openList();
                wait(50);
            }
            return field;
        }

        /// Rest the pointer on a language row, which is the whole gesture for that preview.
        function hoverLanguageRow(main, panel, label) {
            const field = languageField(panel);
            const row = Util.textWith(Util.textsInNestedWindow(field), [label]);
            verify(row, "the language list has no '" + label + "' row");
            mouseMove(row, row.width / 2, row.height / 2);
            return previewOf(main);
        }

        /// @return The mother-tongue dropdown, with its list up.
        function motherTongueField(panel) {
            const field = Util.findAll(panel, function (o) {
                return o.objectName === "motherTongueField";
            })[0];
            verify(field, "the learning page has no mother-tongue field");
            if (field.openChildRect === null) {
                field.openList();
                wait(50);
            }
            return field;
        }

        /// Rest the pointer on a mother-tongue row, which is the whole gesture for that preview.
        function hoverMotherTongueRow(main, panel, label) {
            const field = motherTongueField(panel);
            const row = Util.textWith(Util.textsInNestedWindow(field), [label]);
            verify(row, "the mother-tongue list has no '" + label + "' row");
            mouseMove(row, row.width / 2, row.height / 2);
            return previewOf(main);
        }

        /// Take the pointer off every title: a place in the panel that is not a label.
        function leaveTitle(main, panel) {
            const preview = previewOf(main);
            const away = Util.textWith(Util.textsUnder(panel), ["Settings"]);
            verify(away, "the settings panel has no header to move the pointer to");
            mouseMove(away, away.width / 2, away.height / 2);
            // Leaving is held a beat before the card goes (Main.qml's previewSettle).
            tryCompare(preview, "visible", false);
        }

        /// @return The card's left edge in screen coordinates: the preview window less the
        ///         transparent margin its shadow is drawn in.
        function cardLeftOf(preview) {
            return preview.x + preview.shadowMargin;
        }

        /// @return The height of the row that hides `item` by clipping it to nothing -- how a card
        ///         that is not hovered keeps its verdict buttons out of the picture.
        function rowClipping(item) {
            for (let it = item.parent; it; it = it.parent) {
                if (it.clip === true)
                    return it.height;
            }
            return -1;
        }

        /// Keyboard focus asks the same question as a pointer on the label: a reader who reaches
        /// a switch with Tab gets the preview the mouse would have raised.
        function test_focusingASwitchRaisesItsPreview() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Reading & learning");

            const switcher = Util.findAll(panel, function (o) {
                return o.toString().indexOf("Switch_QMLTYPE") === 0 && o.accessibleName === "Etymology";
            })[0];
            verify(switcher, "the learning page has no etymology switch");
            switcher.forceActiveFocus();
            const preview = previewOf(main);
            tryCompare(preview, "visible", true);
            compare(preview.title, Controller.settingPreview.word, "focus raised the wrong preview");
            panel.visible = false;
        }

        function test_aHoverRaisesTheCardBesideThePanelAndLeavingPutsItDown() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const preview = hoverTitle(main, panel, "Typing speed");
            tryCompare(preview, "visible", true);
            compare(preview.countsDown, false, "a preview has no countdown and must not draw one");
            compare(preview.type, "word");
            compare(preview.title, Controller.settingPreview.word, "the card is not the sample word");

            // The card is placed beside the panel's own card, on the side that has room.
            const card = panelRect(panel);
            verify(cardLeftOf(preview) + (preview.width - 2 * preview.shadowMargin) <= card.x,
                   "the preview did not land clear of the panel's left edge");

            // And it says what it is: the tag a real bubble spends on a learning verdict reads
            // "Preview" here, and the two affordances only a real bubble has are not drawn.
            const texts = Util.textsUnder(preview).filter(function (t) {
                return t.visible && t.text.length > 0;
            }).map(function (t) { return t.text; });
            verify(texts.indexOf("Preview") >= 0, "the preview card carries no preview tag");
            // The verdict buttons are drawn on every card and hidden by a row that clips to nothing
            // when the card is not hovered, which a preview never is (UI-SPEC.md 4.3, 4.4).
            compare(preview.hovering, false, "a preview must not take the pointer");
            const verdicts = Util.textsUnder(preview).filter(function (t) {
                return t.text === "New" || t.text === "Known";
            });
            verify(verdicts.length > 0, "the preview card has no verdict row to keep shut");
            for (const verdict of verdicts)
                verify(rowClipping(verdict) === 0, "the preview card opened its verdict row");
            verify(texts.indexOf("Disappears in 5s") < 0, "the preview card counts down");
            const closes = Util.findAll(preview, function (o) {
                return o.toString().indexOf("Icon_QMLTYPE") === 0 && o.source.indexOf("ui-close.svg") >= 0;
            });
            for (const close of closes)
                compare(close.visible, false, "a preview card must not carry a dismiss button");

            leaveTitle(main, panel);
        }

        /// Which side the card takes. The choice is exercised through the placement's own function:
        /// the offscreen platform has no move(), so a case cannot take hold of the panel and put it
        /// somewhere else on the screen the way a reader can.
        function test_theCardTakesTheSideOfTheScreenThatHasRoom() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const preview = previewOf(main);
            const area = Util.surfaceHost(main).workArea(Util.surfaceHost(main).screenFor(Qt.point(panel.x, panel.y)));
            const margin = panel.previewMargin;
            const shadow = preview.shadowMargin;
            const width = preview.width;
            const size = {width: panel.cardWidth, height: panel.cardHeight};

            // The panel at the screen's left end: the room is on its right.
            const atLeft = {x: area.left + shadow, y: area.top + shadow, width: size.width, height: size.height};
            const x = panel.previewX(atLeft, area, shadow, width);
            compare(x, atLeft.x + size.width + margin - shadow,
                    "the card did not take the room beside the panel's right");
            verify(x + width - shadow <= area.right - margin, "the card hangs past the screen's right");

            // The panel at its right end, which is where the tray puts it: the room is on the left,
            // and that is where the hover in the case above leaves the card.
            const atRight = {x: area.right - shadow - size.width, y: area.top + shadow, width: size.width, height: size.height};
            const left = panel.previewX(atRight, area, shadow, width);
            compare(left, atRight.x - margin - width + shadow,
                    "the card did not take the room beside the panel's left");

            // And a screen too narrow for either side clamps instead of going off it. The card is
            // what has to stay on the screen: the window's own transparent margin may hang over
            // the edge, since nothing is drawn in it but the shadow.
            const narrow = {left: area.left, top: area.top, right: area.left + size.width + 40, bottom: area.bottom};
            const clamped = panel.previewX(atLeft, narrow, shadow, width);
            verify(clamped + shadow >= narrow.left + margin
                   && clamped + width - shadow <= narrow.right - margin,
                   "a card with no room at all went off the screen");
        }

        function test_eachRowForcesInThePartItIsAbout() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Reading & learning");

            // Multiple senses: three of them, whether or not the switch is on, and one language a
            // sense -- the reader's own, which the fixture stores as Chinese (UI-SPEC.md 4.3).
            compare(Controller.settings.multiSense, false);
            let preview = hoverTitle(main, panel, "Multiple senses");
            tryCompare(preview, "visible", true);
            compare(preview.senses.length, 3, "the senses preview does not draw three senses");
            compare(preview.senses[0].en, "");
            compare(preview.senses[0].text, Controller.settingPreview.senses[0].zh);
            leaveTitle(main, panel);

            // The other way round for an English reader: the English definition and no Chinese.
            Controller.setSetting("explanationLang", "en");
            wait(20);
            preview = hoverTitle(main, panel, "Multiple senses");
            tryCompare(preview, "visible", true);
            compare(preview.senses.length, 3);
            compare(preview.senses[0].en, Controller.settingPreview.senses[0].en);
            compare(preview.senses[0].text, "");
            leaveTitle(main, panel);

            // Etymology: the block is drawn even though the switch is off.
            compare(Controller.settings.etymology, false);
            preview = hoverTitle(main, panel, "Etymology");
            tryCompare(preview, "visible", true);
            const etymology = Util.findAll(preview, function (o) {
                return o.objectName === "etymologyBlock";
            });
            verify(etymology.length === 1, "the preview card has no etymology block");
            compare(etymology[0].visible, true, "the etymology preview drew no etymology");
            leaveTitle(main, panel);

            // Example: same again, for the sentence.
            compare(Controller.settings.example, false);
            preview = hoverTitle(main, panel, "Example sentence");
            tryCompare(preview, "visible", true);
            const example = Util.findAll(preview, function (o) {
                return o.objectName === "exampleBlock";
            });
            verify(example.length === 1, "the preview card has no example block");
            compare(example[0].visible, true, "the example preview drew no example");
            const definition = Util.findAll(preview, function (o) {
                return o.objectName === "exampleText";
            });
            verify(definition.length === 1);
            compare(definition[0].text, Controller.settingPreview.example);
            leaveTitle(main, panel);
        }

        /// A switch's card draws the one block that switch adds, so the four word-detail cards differ even
        /// with every one of them on (UI-SPEC.md 4.4): each shows its own block and none of the others.
        function test_aWordDetailPreviewDrawsOnlyItsOwnBlock() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Reading & learning");

            const keys = {"IPA": "ipa", "Part of speech": "pos", "Signpost": "signpost", "Collocations": "collocations"};
            const previous = {};
            try {
                for (const label in keys) {
                    previous[label] = Controller.settings[keys[label]];
                    Controller.setSetting(keys[label], true);
                }
                wait(20);
                for (const label in keys) {
                    const preview = hoverTitle(main, panel, label);
                    tryCompare(preview, "visible", true);
                    for (const other in keys)
                        compare(preview[keys[other]].length > 0, other === label, label + " card drew " + keys[other]);
                    leaveTitle(main, panel);
                }
            } finally {
                for (const label in previous)
                    Controller.setSetting(keys[label], previous[label]);
            }
        }

        /// One reading per field the controller fills an explanation language into: English and
        /// Simplified Chinese in their own, Traditional Chinese, Spanish and Japanese in the
        /// translation (UI-SPEC.md 4.3). The card is raised by the list's rows, and each row draws
        /// its own language whichever one is stored -- the title is not a trigger.
        function test_theDefinitionFollowsTheHoveredLanguageRow() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Reading & learning");

            const sample = Controller.settingPreview.definition;
            compare(Controller.settings.explanationLang, "zh");

            // A rest on the title is no longer a gesture.
            const title = Util.textWith(Util.ownWindow(panel), ["Explanation language"]);
            verify(title, "the learning page has no language title");
            mouseMove(title, title.width / 2, title.height / 2);
            wait(200);
            compare(previewOf(main).visible, false, "the language title still raises the preview");

            // The fixture stores Chinese, but the row under the pointer decides what is drawn.
            let preview = hoverLanguageRow(main, panel, "中文");
            tryCompare(preview, "visible", true);
            compare(preview.chinese, sample.zh);
            compare(preview.english, "", "a Chinese explanation draws no English definition");

            preview = hoverLanguageRow(main, panel, "English");
            tryCompare(preview, "english", sample.en);
            compare(preview.chinese, "", "an English explanation draws no Chinese definition");
            compare(preview.visible, true, "moving to the next row took the card down");

            preview = hoverLanguageRow(main, panel, "Español");
            tryCompare(preview, "translation", sample.es);

            preview = hoverLanguageRow(main, panel, "繁體中文");
            tryCompare(preview, "translation", sample["zh-Hant"]);
            compare(preview.chinese, "", "a Traditional Chinese explanation reused the Simplified field");
            compare(Controller.settings.explanationLang, "zh", "a hover wrote the stored language");

            languageField(panel).closeList();
            tryCompare(preview, "visible", false);
        }

        /// The mother-tongue rows preview the one thing that setting changes: the peek line under
        /// an English explanation. The card is drawn in English whatever is stored, because the
        /// peek exists only there, and each row draws its own language's line without writing it.
        function test_theMotherTongueRowPreviewsThePeekLine() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "Reading & learning");

            const sample = Controller.settingPreview.definition;
            const previous = Controller.settings.explanationLang;
            try {
                Controller.setSetting("explanationLang", "es");
                let preview = hoverMotherTongueRow(main, panel, "中文");
                tryCompare(preview, "visible", true);
                tryCompare(preview, "english", sample.en);
                compare(preview.chinese, "", "an English explanation draws no Chinese definition");
                compare(preview.peekLine, sample.zh);

                preview = hoverMotherTongueRow(main, panel, "繁體中文");
                tryCompare(preview, "peekLine", sample["zh-Hant"]);
                preview = hoverMotherTongueRow(main, panel, "日本語");
                tryCompare(preview, "peekLine", sample.ja);
                compare(Controller.settings.explanationLang, "es", "a hover wrote the stored language");

                motherTongueField(panel).closeList();
                tryCompare(preview, "visible", false);
            } finally {
                Controller.setSetting("explanationLang", previous);
            }
        }

        /// The card draws glass out of a sheet of its own: it is a second Bubble, and two windows
        /// on one sheet would take each other's backdrop and flip the live refresh between them
        /// (glass_backdrop.cpp's kSurfaces, docs/adr/0041).
        ///
        /// The theme is changed with the card already up rather than before the hover. Offscreen has
        /// no real window, so a glass theme's capture dance leaves the panel unphotographable in a way
        /// that also swallows the pointer: the hover that raises a card under a glass theme is the one
        /// case this target cannot drive, and the reader's own machine is where that half is seen.
        function test_theCardTakesItsOwnGlassSheet() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const preview = hoverTitle(main, panel, "Typing speed");
            tryCompare(preview, "visible", true);
            verify(preview.glass, "the preview has no sheet of its own");
            const bubbles = Util.findAll(main, function (o) {
                return o.toString().indexOf("Bubble_QMLTYPE") === 0;
            });
            const explanation = bubbles.find(function (o) { return o.preview !== true; });
            verify(explanation, "Main did not build the explanation bubble");
            verify(preview.glass !== explanation.glass,
                   "the preview and the explanation bubble share one sheet");

            for (const theme of ["frosted", "liquid"]) {
                Controller.setSetting("theme", theme);
                wait(20);
                compare(preview.tokens.glassMode, theme);
                verify(preview.glass, "the preview lost its sheet under " + theme);
            }
            leaveTitle(main, panel);
        }

        /// The card as it renders, one shot per theme: the snapshot path of TEST.md section 5, which
        /// writes nothing unless LENS_QA_SNAPSHOT_DIR is set. Offscreen does not rasterise MultiEffect,
        /// so the shadow is missing from these -- the pill, the frame and the type are not.
        function test_capturePreviewCard() {
            if (!lensQaSnapshotDir)
                skip("Set LENS_QA_SNAPSHOT_DIR (scripts/qa/qml-snapshot.ps1) to save the offscreen snapshot");

            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const preview = hoverTitle(main, panel, "Typing speed");
            tryCompare(preview, "visible", true);
            // The waiting is the reveal: a card photographed mid-write is a card with half a word on it.
            tryVerify(function () { return !preview.revealing; }, 8000);
            for (const theme of ["light", "dark"]) {
                Controller.setSetting("theme", theme);
                wait(20);
                verify(Util.saveSnapshot(testCase, preview.contentItem, lensQaSnapshotDir,
                                         "settings-preview-" + theme));
            }
            leaveTitle(main, panel);

            // And the row that draws several senses, read in English: the two lines under each
            // sense are the English definition and the Chinese the request asks for beside it,
            // the pair the duty hands over (explanation_duty.cpp) -- the duplicate-sentence bug
            // this shot guards against.
            Controller.setSetting("theme", "light");
            Controller.setSetting("explanationLang", "en");
            wait(20);
            panel.navigateBack();
            openCategory(panel, "Reading & learning");
            const senses = hoverTitle(main, panel, "Multiple senses");
            tryCompare(senses, "visible", true);
            tryVerify(function () { return !senses.revealing; }, 8000);
            verify(Util.saveSnapshot(testCase, senses.contentItem, lensQaSnapshotDir,
                                     "settings-preview-senses-en"));
            leaveTitle(main, panel);
        }

        function test_aPanelThatGoesAwayTakesItsCardWithIt() {
            const main = make();
            Util.surfaceHost(main).showSettings();
            const panel = settingsPanel(main);
            openCategory(panel, "General");

            const preview = hoverTitle(main, panel, "Typing speed");
            tryCompare(preview, "visible", true);

            panel.visible = false;
            tryCompare(preview, "visible", false);
        }
    }
}
