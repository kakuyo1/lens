import QtQuick
import QtTest
import Lens
import "../testutil.js" as Util

Item {
    id: root
    width: 800
    height: 600

    Component {
        id: mainComponent
        Main {}
    }

    TestCase {
        name: "Phase2E"
        when: windowShown

        SignalSpy {
            id: opacityChanges
            signalName: "opacityChanged"
        }

        function makeMain() {
            const main = createTemporaryObject(mainComponent, root);
            verify(main);
            wait(25);
            return main;
        }

        /// The colour table the settings window owns (docs/adr/0041), with its glass on.
        function settingsTokens() {
            const panel = Util.ofType(makeMain(), "SettingsPopup");
            verify(panel);
            return panel.tokens;
        }

        function test_twoBuiltInThemesHaveIndependentTables() {
            compare(Tokens.themeName(0), "light");
            compare(Tokens.themeName(1), "dark");
            verify(Tokens.lightTheme.bg !== Tokens.darkTheme.bg);
            verify(Tokens.validateCustomTheme(Tokens.colorsForTheme("light")).valid);
            verify(Tokens.validateCustomTheme(Tokens.colorsForTheme("dark")).valid);
        }

        function test_glassThemesHaveTheirOwnIndexAndName() {
            compare(Tokens.themeIndex("frosted"), 3);
            compare(Tokens.themeIndex("liquid"), 4);
            compare(Tokens.themeName(3), "frosted");
            compare(Tokens.themeName(4), "liquid");
            const settings = settingsTokens();
            Tokens.theme = "frosted";
            compare(settings.radiusCard, 18);
            Tokens.theme = "liquid";
            compare(settings.radiusCard, 22);
            Tokens.theme = "light";
            compare(settings.radiusCard, 18);
        }

        function test_glassStaysOnTheSettingsTable() {
            const settings = settingsTokens();
            Tokens.theme = "frosted";
            compare(Tokens.glassMode, "");
            compare(Tokens.panel.a, 1);
            compare(Tokens.popup.a, 1);
            verify(Tokens.contrastRatio(Tokens.text, Tokens.panel) >= 4.5);
            verify(Tokens.contrastRatio(Tokens.muted, Tokens.panel) >= 4.5);
            verify(Tokens.contrastRatio(Tokens.faint, Tokens.panel) >= 4.5);
            compare(settings.glassMode, "frosted");
            verify(Math.abs(settings.panel.a - 0.4) < 0.01);

            Tokens.theme = "liquid";
            compare(Tokens.radiusCard, 18);
            verify(Qt.colorEqual(Tokens.text, Tokens.lightTheme.text));
            compare(settings.radiusCard, 22);
            Tokens.theme = "light";
        }

        /// Every object under `root` that `predicate` accepts, through `data` and `children` both: a
        /// Loader's page is a child item and is not in its `data`, so Util.findAll does not reach it.
        function findDeep(root, predicate) {
            const found = [];
            const visited = [];
            (function collect(node) {
                const kids = [].concat(node.data || [], node.children || []);
                for (let i = 0; i < kids.length; ++i) {
                    if (visited.indexOf(kids[i]) >= 0)
                        continue;
                    visited.push(kids[i]);
                    if (predicate(kids[i]))
                        found.push(kids[i]);
                    collect(kids[i]);
                }
            })(root);
            return found;
        }

        function glassBlurSlider(panel) {
            return findDeep(panel, function (o) {
                return o.toString().indexOf("StepSlider_QMLTYPE") === 0 && o.to === 100 && o.visible;
            })[0];
        }

        function openFrostedGeneral() {
            Controller.setSetting("theme", "frosted");
            const main = makeMain();
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel);
            Util.surfaceHost(main).showSettings();
            wait(25);
            panel.openCategory("general");
            wait(25);
            return {main: main, panel: panel};
        }

        function test_glassSliderDragSetsTheValueWithoutDraggingTheWindow() {
            const shown = openFrostedGeneral();
            const shell = findDeep(shown.panel, function (o) {
                return o.toString().indexOf("ShadowCard_QMLTYPE") === 0 && o.movable;
            })[0];
            const slider = glassBlurSlider(shown.panel);
            verify(shell && slider, "the frosted blur slider is not on the general page");

            mousePress(slider, slider.width * 0.2, slider.height / 2);
            for (let step = 1; step <= 6; ++step) {
                mouseMove(slider, slider.width * (0.2 + 0.1 * step), slider.height / 2);
                wait(20);
                verify(!shell.dragging, "the card's window drag took the press from the slider");
                if (step === 3)
                    compare(Controller.settings.frostedBlur, slider.stopAt(slider.width * 0.5), "the value did not follow the drag before the release");
            }
            compare(Controller.settings.frostedBlur, slider.stopAt(slider.width * 0.8), "the value did not follow the drag");
            mouseRelease(slider, slider.width * 0.8, slider.height / 2);

            Controller.setSetting("frostedBlur", 10);
            Controller.setSetting("theme", "light");
            Util.surfaceHost(shown.main).hidePanels(null);
        }

        function test_glassSliderDragLeavesTheSettingsWindowOpaque() {
            const shown = openFrostedGeneral();
            const slider = glassBlurSlider(shown.panel);
            verify(slider, "the frosted blur slider is not on the general page");
            // Opening the window takes its own backdrop, which dims it for 120 ms: let that finish first.
            wait(300);

            opacityChanges.target = shown.panel;
            opacityChanges.clear();
            mousePress(slider, slider.width * 0.2, slider.height / 2);
            for (let step = 1; step <= 6; ++step) {
                mouseMove(slider, slider.width * (0.2 + 0.1 * step), slider.height / 2);
                wait(20);
            }
            mouseRelease(slider, slider.width * 0.8, slider.height / 2);
            wait(300);
            compare(opacityChanges.count, 0, "the settings window dimmed during a slider drag");
            opacityChanges.target = null;

            Controller.setSetting("frostedBlur", 10);
            Controller.setSetting("theme", "light");
            Util.surfaceHost(shown.main).hidePanels(null);
        }

        function test_movingTheSettingsWindowRedrawsItsGlassOnlyWhenTheSheetNoLongerCoversIt() {
            const shown = openFrostedGeneral();
            // Opening takes its own sheet after the window settles: let that finish before counting.
            wait(700);

            opacityChanges.target = shown.panel;
            opacityChanges.clear();
            // Well inside the sheet's margin and on the screen: no sheet is taken, so the window is not dimmed. (The
            // screen's right edge leaves no room to the right in this environment, so the move goes left.)
            shown.panel.x = shown.panel.x - 20;
            wait(600);
            compare(opacityChanges.count, 0, "a move the sheet covers dimmed the window");

            // Past the margin a new sheet is needed. Excluded from captures that takes no dim; otherwise one dim is two
            // opacity changes, to 0 and back to 1. The exclusion lives on the panel's GlassHost, not on the window.
            shown.panel.x = shown.panel.x + 300;
            wait(600);
            const host = Util.ofType(shown.panel, "GlassHost");
            verify(host, "the settings panel has no GlassHost");
            compare(opacityChanges.count, host.excludedFromCapture ? 0 : 2, "a move past the sheet did not take the expected dim");
            opacityChanges.target = null;

            Controller.setSetting("frostedBlur", 10);
            Controller.setSetting("theme", "light");
            Util.surfaceHost(shown.main).hidePanels(null);
        }

        function test_glassThemeDrawsGlassOnTheSettingsCard() {
            const main = makeMain();
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel);
            Controller.setSetting("theme", "frosted");
            Util.surfaceHost(main).showSettings();
            wait(25);

            const frosted = Util.ofTypeInWindow(panel, "GlassSurface");
            verify(frosted, "the frosted theme drew no glass on the settings card");
            compare(frosted.mode, "frosted");

            Controller.setSetting("theme", "liquid");
            wait(25);
            compare(frosted.mode, "liquid");

            Controller.setSetting("theme", "light");
            wait(25);
            compare(frosted.visible, false);
        }

        function test_everyGlassSurfaceReadsItsOwnTableAndSheet() {
            const main = makeMain();
            const sheets = {
                SettingsPopup: "settings", TrayMenu: "tray", StatsPopup: "stats", WordsPopup: "words",
                YearWordsPopup: "yearWords", CostPopup: "cost", Bubble: "bubble", SelectionBar: "selectionBar",
                Notice: "notice", UpdateCard: "update", RemovalQuestion: "removal"
            };
            for (const type of Object.keys(sheets)) {
                const surface = Util.ofType(main, type);
                verify(surface, type + " is not in Main");
                verify(surface.tokens.glassEnabled, type + " does not read the glass table");
                compare(surface.glass.name, sheets[type], type + " draws from the wrong sheet");
            }
        }

        function test_glassSamplesTheDesktopBehindTheCardNotBehindTheWindow() {
            const shown = openFrostedGeneral();
            const glass = Util.ofTypeInWindow(shown.panel, "GlassSurface");
            verify(glass, "the frosted theme drew no glass on the settings card");
            // The card is inset by the shadow margin, so its corner is that far into the window on the desktop.
            compare(glass.origin.x, shown.panel.x + shown.panel.shadowMargin - glass.shownSheet.x);
            compare(glass.origin.y, shown.panel.y + shown.panel.shadowMargin - glass.shownSheet.y);
        }

        /// One live texture, replaced in place (docs/adr/0071). A blur move is a uniform, so it must not retake the
        /// sheet, and the sheet rectangle stays non-empty throughout -- the guarantee the old double slot bought with a
        /// second Image and ShaderEffectSource.
        function test_glassKeepsItsLiveTextureWhileTheBlurMoves() {
            const shown = openFrostedGeneral();
            const glass = Util.ofTypeInWindow(shown.panel, "GlassSurface");
            verify(glass, "the frosted theme drew no glass on the settings card");
            verify(glass.sheetItem, "the glass has no live sheet texture");
            // Opening the panel takes a sheet after it settles, and the read is off the GUI thread: wait for the first
            // picture rather than for a fixed time.
            tryVerify(function () { return glass.sheetItem.pictureSerial > 0; }, 3000,
                      "the live texture never took a picture");
            tryVerify(function () { return glass.shownSheet.width > 0; }, 3000,
                      "the sheet rectangle is empty");
            const serial = glass.sheetItem.pictureSerial;

            for (let step = 1; step <= 10; ++step) {
                Controller.setSetting("frostedBlur", 10 + step * 5);
                for (let poll = 0; poll < 4; ++poll) {
                    wait(5);
                    compare(glass.sheetItem.pictureSerial, serial, "a blur move retook the sheet");
                    verify(glass.shownSheet.width > 0, "the sheet went blank while the blur moved");
                }
            }

            Controller.setSetting("frostedBlur", 10);
            Controller.setSetting("theme", "light");
            Util.surfaceHost(shown.main).hidePanels(null);
        }

        /// Blur at 0 with nothing to bend (docs/adr/0069): the card draws no backdrop, asks for no picture, and does
        /// not dim itself to photograph a desktop it no longer shows. The live desktop comes from the window's own
        /// transparency, so this is the one setting where reading the desktop would be pure cost.
        function test_blurAtZeroLetsTheLiveDesktopThrough() {
            const shown = openFrostedGeneral();
            const glass = Util.ofTypeInWindow(shown.panel, "GlassSurface");
            verify(glass, "the frosted theme drew no glass on the settings card");
            wait(300);
            compare(glass.passthrough, false, "the card drew passthrough with its blur above 0");
            verify(glass.drawsBackdrop, "the card drew no backdrop while its blur was above 0");
            verify(glass.sheetItem.active, "the card held no live texture while its blur was above 0");

            Controller.setSetting("frostedBlur", 0);
            wait(30);
            compare(glass.passthrough, true, "blur at 0 left the card drawing a backdrop");
            compare(glass.drawsBackdrop, false, "blur at 0 still drew a backdrop");
            compare(glass.sheetItem.active, false, "blur at 0 left the live texture uploaded");
            compare(glass.mapUrl, "", "blur at 0 still asked for a displacement map");
            compare(shown.panel.glass.live, false, "blur at 0 left the desktop refreshing for a card that draws none");
            compare(shown.panel.opacity, 1, "the card dimmed itself to photograph the desktop it no longer draws");

            // And it comes back the moment the reader moves the blur up again, without a reload of the window.
            Controller.setSetting("frostedBlur", 10);
            wait(30);
            compare(glass.passthrough, false, "raising the blur left the card without a backdrop");
            verify(glass.drawsBackdrop, "raising the blur drew no backdrop");
            verify(glass.sheetItem.active, "raising the blur left the live texture released");

            Controller.setSetting("frostedBlur", 10);
            Controller.setSetting("theme", "light");
            Util.surfaceHost(shown.main).hidePanels(null);
        }

        /// The blur and the saturation are shader uniforms (docs/adr/0070): the picture no longer reloads when they
        /// move, so the binding from the setting to the effect's property is the whole path. The blur sits on both
        /// passes of the separable pair, the saturation only on the compositing one (docs/adr/0082).
        function test_blurAndSaturationReachTheShaderUniforms() {
            const shown = openFrostedGeneral();
            const glass = Util.ofTypeInWindow(shown.panel, "GlassSurface");
            verify(glass, "the frosted theme drew no glass on the settings card");
            const passes = findDeep(glass, function (o) {
                return o.fragmentShader !== undefined;
            });
            verify(passes.length >= 2, "the glass drew fewer than its two blur passes");
            const effect = passes.find(function (o) { return o.saturation !== undefined; });
            const xPass = passes.find(function (o) { return o.saturation === undefined; });
            verify(effect, "the glass drew no shader");
            compare(effect.blur, Controller.settings.frostedBlur);
            compare(effect.saturation, Controller.settings.frostedSaturation);
            // The sheet is published on the card's first frame, which lands after openCategory's
            // wait on a slow machine and before it on a fast one. Read before it, the serial is the
            // zero a sheet that was never taken has, and the comparison below would pass or fail on
            // the machine's pace rather than on what a write does.
            let frames = 0;
            while (glass.sheet.serial === 0 && frames++ < 50)
                wait(20);
            const picture = glass.sheet.serial;
            verify(picture > 0, "the card never took a sheet to compare against");

            Controller.setSetting("frostedBlur", 40);
            wait(30);
            compare(effect.blur, 40, "the blur uniform did not follow the setting");
            compare(xPass.blur, 40, "the horizontal pass did not follow the blur setting");
            compare(glass.sheet.serial, picture, "moving the blur retook the sheet instead of changing a uniform");
            Controller.setSetting("frostedSaturation", 60);
            wait(30);
            compare(effect.saturation, 60, "the saturation uniform did not follow the setting");

            Controller.setSetting("frostedBlur", 10);
            Controller.setSetting("frostedSaturation", 120);
            Controller.setSetting("theme", "light");
            Util.surfaceHost(shown.main).hidePanels(null);
        }

        function test_colorConversionKeepsAlphaForThemeValues() {
            const light = Tokens.colorsForTheme("light");
            verify(light.bubbleBg.length === 9, "light bubble alpha was dropped");

            const encoded = Tokens.customThemeValue(light);
            const decoded = Tokens.colorsForTheme(encoded);
            compare(decoded.bubbleBg, light.bubbleBg);
            compare(decoded.bubbleBorder, light.bubbleBorder);
        }

        function test_customTextRolesMustEachPassAa() {
            const colors = Tokens.colorsForTheme("light");
            colors.panel = "#ffffff";
            colors.text = "#ffffff";
            colors.muted = "#777777";
            colors.faint = "#999999";
            verify(!Tokens.validateCustomTheme(colors).valid);

            colors.panel = "#202020";
            colors.text = "#ffffff";
            colors.muted = "#d0d0d0";
            colors.faint = "#aaaaaa";
            const result = Tokens.validateCustomTheme(colors);
            verify(result.valid);
            verify(result.bodyRatio >= 4.5);
            verify(result.mutedRatio >= 4.5);
            verify(result.faintRatio >= 4.5);
        }

        function test_alphaTextIsValidatedAfterCompositing() {
            const colors = Tokens.colorsForTheme("light");
            colors.panel = "#202020";
            colors.text = "#20ffffff";
            colors.muted = "#20ffffff";
            colors.faint = "#20ffffff";
            verify(!Tokens.validateCustomTheme(colors).valid,
                   "transparent text must not pass AA using its uncomposited RGB values");
        }

        function test_customThemeRoundTripsThroughTheThemeSettingValue() {
            const colors = Tokens.colorsForTheme("dark");
            colors.panel = "#202020";
            colors.text = "#ffffff";
            colors.muted = "#d0d0d0";
            colors.faint = "#aaaaaa";
            const encoded = Tokens.customThemeValue(colors);
            const decoded = Tokens.colorsForTheme(encoded);

            compare(Tokens.themeIndex(encoded), 2);
            compare(decoded.panel, "#202020");
            compare(decoded.text, "#ffffff");
            compare(decoded.muted, "#d0d0d0");
            compare(decoded.faint, "#aaaaaa");
            compare(decoded.bubbleBg, colors.bubbleBg);
        }

        function test_customThemeIsReadBackAfterBeingPersisted() {
            makeMain();
            const previous = Controller.settings.theme;
            const colors = Tokens.colorsForTheme("dark");
            colors.panel = "#202020";
            colors.text = "#ffffff";
            colors.muted = "#d0d0d0";
            colors.faint = "#aaaaaa";
            const encoded = Tokens.customThemeValue(colors);

            Controller.setSetting("theme", encoded);
            wait(25);
            compare(Controller.settings.theme, encoded);
            compare(Tokens.theme, encoded);

            verify(makeMain());
            compare(Tokens.theme, Controller.settings.theme);
            Controller.setSetting("theme", previous);
        }

        function test_generalSettingsExposeBuiltInAndCustomChoices() {
            const main = makeMain();
            const panel = Util.ofType(main, "SettingsPopup");
            verify(panel);
            Util.surfaceHost(main).showSettings();
            panel.openCategory("general");
            wait(25);

            const page = Util.ofType(panel, "SettingsGeneralPage");
            verify(page);
            const themes = Util.findAll(page, function (o) {
                return o.objectName === "themeField";
            });
            verify(themes.length === 1, "the General page has no theme control");
            compare(themes[0].options.length, 5);
            compare(themes[0].options[2].label, "Frosted glass");
            compare(themes[0].options[2].value, 3);
            compare(themes[0].options[3].label, "Liquid glass");
            compare(themes[0].options[3].value, 4);
            compare(themes[0].options[4].label, "Custom");
            compare(themes[0].options[4].value, 2);
        }

        function test_transientPlacementReadsBubbleSizeAfterContentChanges() {
            const main = makeMain();
            const bubble = Util.ofType(main, "Bubble");
            verify(bubble);
            const anchor = Qt.point(500, 500);

            bubble.show({title: "short", type: "word", en: "short", zh: "短", x: 500, y: 500});
            wait(25);
            Util.surfaceHost(main).placeTransient(bubble, anchor, bubble.gap);
            const firstHeight = bubble.height;
            const firstY = bubble.y;

            bubble.show({title: "long", type: "word", senses: [
                {en: "a longer explanation that wraps", text: "第一条"},
                {en: "another explanation that wraps", text: "第二条"},
                {en: "a third explanation that wraps", text: "第三条"}
            ], x: 500, y: 500});
            wait(25);
            Util.surfaceHost(main).placeTransient(bubble, anchor, bubble.gap);

            verify(bubble.height > firstHeight);
            verify(bubble.y < firstY, "placement must use the current height, not the first height");
        }

        function test_narrowScreenUsesBoundsWithoutScalingContent() {
            const main = makeMain();
            const screen = {virtualX: -320, virtualY: 40, width: 320, height: 480};
            const layout = Util.surfaceHost(main).layoutFor(screen, 432, 532);

            verify(layout.scale === undefined, "narrow layout must not scale the content tree");
            verify(layout.width <= screen.width - 16);
            verify(layout.height <= screen.height - 16);

            const anchor = Qt.rect(-2, 90, 24, 24);
            const at = Util.surfaceHost(main).placeBeside(anchor, layout.width, layout.height, screen);
            verify(at.x >= screen.virtualX + 8);
            verify(at.y >= screen.virtualY + 8);
            verify(at.x + layout.width <= screen.virtualX + screen.width - 8);
            verify(at.y + layout.height <= screen.virtualY + screen.height - 8);
        }
    }
}
