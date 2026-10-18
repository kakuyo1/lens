import QtQuick
import QtQuick.Window

/**
 * The root, and the only place the surfaces are tied to the controller.
 *
 * There is no main window, and this one is not it either: it is a 1x1 transparent window kept
 * off every screen, and its only job is to own the surfaces and be the engine's root object.
 * It has to be *shown*, though -- a window declared inside another one becomes its transient
 * child, and Windows does not put a transient child on screen while its parent is hidden.
 * Every surface is a window of its own, which is what UI.md's "each surface is independent,
 * sharing no window" asks for.
 */
Window {
    id: root

    width: 1
    height: 1
    x: 0
    y: 0
    opacity: 0
    visible: true
    color: "transparent"
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput

    readonly property int screenMargin: 8
    property var placements: []
    property string screenLayoutSignature: ""
    property int selectionBarGeneration: 0

    /// The colour table follows the stored theme, whichever surface is showing.
    Binding {
        target: Tokens
        property: "theme"
        value: Controller.settings.theme
    }

    /// The liquid theme's text colour, read from the same document as the theme.
    Binding {
        target: Tokens
        property: "liquidTextColor"
        value: Controller.settings.liquidTextColor
    }

    /// @return The tray icon's rectangle, for anchoring the menu and the panels. Empty only
    ///         until the icon has been seen once: the shell reports nothing while the taskbar
    ///         is down, and an auto-hidden taskbar is down whenever a panel is opened from the
    ///         menu. Tray::geometry() remembers the last place it did report.
    function trayAnchor() {
        const g = Tray.geometry;
        if (g.width > 0)
            return g;
        return Qt.rect(Screen.virtualX + Screen.width - 56,
                       Screen.virtualY + Screen.height - 48, 56, 48);
    }

    /// @return The screen a DIP point falls on, or the nearest screen when it falls between monitors.
    function screenFor(point) {
        let nearest = screen;
        let nearestDistance = Number.MAX_VALUE;
        // qmllint disable missing-property
        for (const s of Qt.application.screens) {
            if (point.x >= s.virtualX && point.x < s.virtualX + s.width
                && point.y >= s.virtualY && point.y < s.virtualY + s.height)
                return s;
            const dx = point.x < s.virtualX ? s.virtualX - point.x
                                             : Math.max(0, point.x - (s.virtualX + s.width));
            const dy = point.y < s.virtualY ? s.virtualY - point.y
                                             : Math.max(0, point.y - (s.virtualY + s.height));
            const distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearest = s;
                nearestDistance = distance;
            }
        }
        // qmllint enable missing-property
        return nearest;
    }

    /// @return The screen containing a native-pixel point, using each screen's own scale factor.
    function screenForPhysical(point) {
        let nearest = screen;
        let nearestDistance = Number.MAX_VALUE;
        // qmllint disable missing-property
        for (const s of Qt.application.screens) {
            const ratio = s.devicePixelRatio || 1;
            // The origin is not scaled, as ScreenPoints.dipOn assumes: only the size is in DIPs.
            const left = s.virtualX;
            const top = s.virtualY;
            const right = left + s.width * ratio;
            const bottom = top + s.height * ratio;
            if (point.x >= left && point.x < right && point.y >= top && point.y < bottom)
                return s;
            const dx = point.x < left ? left - point.x : Math.max(0, point.x - right);
            const dy = point.y < top ? top - point.y : Math.max(0, point.y - bottom);
            const distance = dx * dx + dy * dy;
            if (distance < nearestDistance) {
                nearest = s;
                nearestDistance = distance;
            }
        }
        // qmllint enable missing-property
        return nearest;
    }

    function anchorPoint(anchor) {
        return anchor.width === undefined
                ? anchor
                : Qt.point(anchor.x + anchor.width / 2, anchor.y + anchor.height / 2);
    }

    function rememberPlacement(surface, anchor, kind) {
        for (const placement of placements) {
            if (placement.surface === surface) {
                placement.anchor = anchor;
                placement.kind = kind;
                return placement;
            }
        }
        const placement = {surface: surface, anchor: anchor, kind: kind};
        placements.push(placement);
        return placement;
    }

    /// @return The edges of a screen's work area in virtual DIPs: the screen less any taskbar. Asked of
    ///         Qt by device name: Screen.desktopAvailable* gave the primary's size (1536x864) on the
    ///         portrait screen, whose own size is 1152x2048. A screen without a name uses its geometry.
    // qmllint disable missing-property
    function workArea(target) {
        const area = Controller.screenWorkArea(target.name || "");
        if (area.width > 0)
            return {left: area.x, top: area.y, right: area.x + area.width, bottom: area.y + area.height};
        return {left: target.virtualX, top: target.virtualY, right: target.virtualX + target.width, bottom: target.virtualY + target.height};
    }
    // qmllint enable missing-property

    /// @return A native-size surface bounded to the work area without changing its content scale.
    function layoutFor(target, width, height) {
        const area = root.workArea(target);
        const availableWidth = Math.max(1, area.right - area.left - 2 * screenMargin);
        const availableHeight = Math.max(1, area.bottom - area.top - 2 * screenMargin);
        return {
            width: Math.min(Math.max(1, width), availableWidth),
            height: Math.min(Math.max(1, height), availableHeight)
        };
    }

    function clamp(value, low, high) {
        const upper = Math.max(low, high);
        return Math.max(low, Math.min(value, upper));
    }

    /// @return Where a window of this size goes to sit beside the icon, whole when possible.
    function placeBeside(anchor, width, height, target, shadowMargin) {
        target = target || root.screenFor(root.anchorPoint(anchor));
        shadowMargin = shadowMargin === undefined ? 26 : shadowMargin;
        const margin = root.screenMargin;
        const area = root.workArea(target);
        const left = area.left + margin;
        const top = area.top + margin;
        const right = area.right - margin;
        const bottom = area.bottom - margin;

        const x = root.clamp(anchor.x + anchor.width / 2 - width + shadowMargin,
                             left, right - width);
        let y = anchor.y + anchor.height / 2 - height + shadowMargin;
        if (y < top)
            y = anchor.y + anchor.height + 8;
        return Qt.point(Math.round(x), Math.round(root.clamp(y, top, bottom - height)));
    }

    /// @brief Where a panel goes to sit beside the pet's rectangle rather than beside the tray icon.
    ///
    /// The candidates are the pet's four sides in the order right, left, below, above, each one a card
    /// @p gap clear of that side and centred across its length. The sides are scored by how much of the
    /// pet the card covers once it is clamped into the work area, which is 0 for a side the card clears;
    /// the lowest score wins and the order above breaks a tie. The score rather than a fit test is what
    /// keeps the card off the pet: a work area too short to hold the card beside the pet clamps the card
    /// back over it, and the side clamped least far onto the pet is the one that survives. The horizontal
    /// sides come first because the card is 532 px tall and needs that much of the work area's height to
    /// clear the pet vertically, which the corner the pet is usually left in does not have.
    /// @param rectangle The pet window's rectangle as it stands. The pet can be dragged or grown over an
    ///        edge, so the rectangle is read against the work area first: the part of the pet that is on
    ///        screen is the part the card is put beside.
    /// @return The panel's top-left corner, in DIPs.
    function placeBesidePet(rectangle, width, height, gap) {
        const area = root.workArea(root.screenFor(Qt.point(rectangle.x + rectangle.width / 2,
                                                           rectangle.y + rectangle.height / 2)));
        const left = area.left + screenMargin;
        const top = area.top + screenMargin;
        const right = area.right - screenMargin;
        const bottom = area.bottom - screenMargin;
        // Each clamp is monotone, so a pet dragged wholly past an edge collapses onto that edge
        // rather than turning its rectangle inside out.
        const petLeft = root.clamp(rectangle.x, left, right);
        const petRight = root.clamp(rectangle.x + rectangle.width, petLeft, right);
        const petTop = root.clamp(rectangle.y, top, bottom);
        const petBottom = root.clamp(rectangle.y + rectangle.height, petTop, bottom);
        const centreX = (petLeft + petRight) / 2;
        const centreY = (petTop + petBottom) / 2;

        const candidates = [
            Qt.point(petRight + gap, centreY - height / 2),
            Qt.point(petLeft - gap - width, centreY - height / 2),
            Qt.point(centreX - width / 2, petBottom + gap),
            Qt.point(centreX - width / 2, petTop - gap - height)
        ];
        /// The candidate's rectangle, pulled back inside the work area by however much of it hangs out.
        const onScreen = function (at) {
            return Qt.rect(root.clamp(at.x, left, right - width), root.clamp(at.y, top, bottom - height),
                           width, height);
        };
        /// How much of the pet that rectangle covers.
        const covered = function (at) {
            return Math.max(0, Math.min(at.x + at.width, petRight) - Math.max(at.x, petLeft))
                 * Math.max(0, Math.min(at.y + at.height, petBottom) - Math.max(at.y, petTop));
        };

        let chosen = onScreen(candidates[0]);
        for (const candidate of candidates) {
            const at = onScreen(candidate);
            if (covered(at) < covered(chosen))
                chosen = at;
        }
        return Qt.point(Math.round(chosen.x), Math.round(chosen.y));
    }

    /// @brief Put a panel up beside the pet, and remember that it went there: a placement is recomputed
    ///        from where the pet is now on every click, and from the pet again when the screens change.
    function placePetPanel(panel) {
        root.rememberPlacement(panel, null, "pet");
        const pet = Qt.rect(petWindow.x, petWindow.y, petWindow.width, petWindow.height);
        const at = root.placeBesidePet(pet, panel.width, panel.height, screenMargin);
        panel.x = at.x;
        panel.y = at.y;
        panel.visible = true;
        panel.raise();
    }

    /// @brief Put a surface up beside its anchor, on whichever screen that is.
    function placePanel(panel, anchor) {
        const placement = root.rememberPlacement(panel, anchor, "panel");
        const target = root.screenFor(root.anchorPoint(anchor));
        const layout = root.layoutFor(target, panel.width, panel.height);
        const at = root.placeBeside(anchor, layout.width, layout.height, target, panel.shadowMargin);
        panel.x = at.x;
        panel.y = at.y;
        placement.width = panel.width;
        placement.height = panel.height;
        panel.visible = true;
        // `visible` alone is not enough. Every surface carries WindowStaysOnTopHint, but a
        // click on the tray icon activates the taskbar -- which is topmost too and re-raises
        // itself -- so a panel shown into that moment can land under it.
        panel.raise();
    }

    /// @brief Where a surface that hangs from an anchor sits, before it is clamped into the work area.
    ///
    /// The side the reader chose (bubblePlacement) is tried first. The other side is taken when the
    /// chosen one has no room, and that is also the answer when neither side has any: the clamp in
    /// placeTransient() then keeps the surface on screen.
    /// @return The window's top edge, in DIPs.
    function transientTop(anchorY, gap, height, shadow, top, bottom, preferBelow) {
        const above = anchorY - gap - height + shadow;
        const below = anchorY + gap - shadow;
        if (preferBelow)
            return below + height <= bottom ? below : above;
        return above >= top ? above : below;
    }

    // qmllint disable missing-property
    function placeTransient(surface, anchor, gap) {
        root.rememberPlacement(surface, anchor, "transient");
        const target = root.screenFor(root.anchorPoint(anchor));
        // Read the dimensions at placement time. Bubble's implicit height changes when its
        // senses, translation, or hover actions settle; caching its first size breaks that path.
        const layout = root.layoutFor(target, surface.width, surface.height);
        const area = root.workArea(target);
        const left = area.left + screenMargin;
        const top = area.top + screenMargin;
        const right = area.right - screenMargin;
        const bottom = area.bottom - screenMargin;
        const shadow = surface.shadowMargin;
        const x = root.clamp(anchor.x - shadow, left, right - layout.width);
        const preferBelow = Controller.settings.bubblePlacement === "below";
        const y = root.clamp(root.transientTop(anchor.y, gap, layout.height, shadow, top, bottom, preferBelow), top, bottom - layout.height);
        surface.x = Math.round(x);
        surface.y = Math.round(y);
        surface.raise();
    }

    /// @brief Put the action bar up in the form the reader chose (UI.md 4.10).
    ///
    /// The form's own placement lives on SelectionBar (placeAt), next to the geometry it is
    /// placing; this only finds the screen and hands it the work area.
    function placeSelectionBar(surface, anchor) {
        root.rememberPlacement(surface, anchor, "transient");
        surface.placeAt(anchor, root.workArea(root.screenFor(anchor)), root.screenMargin);
    }

    /// The answer to a review carries no verdict buttons (Controller.explanationDuty.bubble); every other card is placed
    /// beside its anchor as before.
    function placeBubble(surface, payload) {
        if (payload.verdictButtons === false)
            reviewSign.placeAnswerCard(surface);
        else
            root.placeTransient(surface, payload, surface.gap);
    }
    // qmllint enable missing-property

    /// @return The anchor a panel that replaces `from` is put up at: the corner `from` hangs from, so
    ///         the next card lands where the one it replaces was. Without a `from`, the tray icon.
    function anchorFrom(from) {
        if (!from)
            return root.trayAnchor();
        const corner = Qt.point(from.x + from.width - from.shadowMargin, from.y + from.height - from.shadowMargin);
        return Qt.rect(corner.x, corner.y, 0, 0);
    }

    /// A stack panel grows from the corner it hangs from, its bottom-right, so it grows up and to the
    /// left rather than down past the tray icon. The placement remembers the size it was last placed
    /// at. A drag moves the window without changing that size, so the corner is read from where the
    /// window is now, and a dragged panel keeps the corner the reader left it at.
    function holdCorner(panel) {
        const placement = root.placements.find(function (candidate) { return candidate.surface === panel; });
        if (placement && panel.visible) {
            panel.x = panel.x + placement.width - panel.width;
            panel.y = panel.y + placement.height - panel.height;
            placement.width = panel.width;
            placement.height = panel.height;
        }
        root.keepOnScreen(panel);
    }

    /// A surface that grows after it was placed -- a row that opens, a list that loads -- keeps its
    /// top-left corner, so it runs off the bottom or right edge by however much it grew. Pull it back
    /// by that much. Placement is only ever computed from the anchor, so nothing else would. The stack
    /// uses holdCorner() instead, which keeps the corner rather than pulling it back.
    function keepOnScreen(surface) {
        if (!surface.visible)
            return;
        const area = root.workArea(root.screenFor(Qt.point(surface.x + surface.width / 2, surface.y + surface.height / 2)));
        surface.x = root.clamp(surface.x, area.left + screenMargin, area.right - screenMargin - surface.width);
        surface.y = root.clamp(surface.y, area.top + screenMargin, area.bottom - screenMargin - surface.height);
    }

    function repositionVisible() {
        for (const placement of placements) {
            // A review's answer (a bubble with no verdict buttons) is placed by reviewSign, from its own record.
            if (!placement.surface.visible || placement.surface.verdictButtons === false)
                continue;
            if (placement.kind === "panel")
                root.placePanel(placement.surface, placement.anchor);
            else if (placement.kind === "pet")
                root.placePetPanel(placement.surface);
            else if (placement.surface === bar)
                root.placeSelectionBar(bar, placement.anchor);
            else if (!placement.surface.dragging)
                root.placeTransient(placement.surface, placement.anchor, placement.surface.gap);
        }
        reviewSign.repositionAnswerCard();
    }

    function currentScreenLayoutSignature() {
        let signature = "";
        // qmllint disable missing-property
        for (const s of Qt.application.screens)
            signature += `${s.virtualX},${s.virtualY},${s.width},${s.height},${s.devicePixelRatio || 1};`;
        // qmllint enable missing-property
        return signature;
    }

    function checkScreenLayout() {
        const signature = root.currentScreenLayoutSignature();
        if (signature !== root.screenLayoutSignature) {
            root.screenLayoutSignature = signature;
            root.repositionVisible();
        }
    }

    /**
     * Take down every panel but the one being opened. A panel that is up fades out where it stands while the one
     * opening fades in beside it (ShadowCard), so the hand-over is a cross-fade. With the animations off there is
     * nothing to fade, so the panel goes at once. The one being opened is taken back from a fade if it was leaving.
     */
    function hidePanels(except) {
        const panels = [trayMenu, settingsPopup, statsPopup, wordsPopup, yearWordsPopup, costPopup];
        for (const panel of panels) {
            if (panel === except)
                panel.leaving = false;
            else if (!panel.visible)
                continue;
            else if (Tokens.motion.pop > 0)
                panel.leaving = true;
            else
                panel.visible = false;
        }
    }

    /// The stack's panels take the place of one another, so each one opens from the panel it replaces.
    function showStats(from) {
        const anchor = root.anchorFrom(from);
        root.hidePanels(statsPopup);
        root.placePanel(statsPopup, anchor);
    }

    /// @param fromPet The pet's own gesture asked for the panel, and it is the only caller that wants it
    ///        beside the pet: the tray menu's entry names a panel rather than a place, and keeps the icon.
    function showSettings(fromPet) {
        root.hidePanels(settingsPopup);
        if (fromPet)
            root.placePetPanel(settingsPopup);
        else
            root.placePanel(settingsPopup, root.trayAnchor());
    }

    function showCost(from) {
        const anchor = root.anchorFrom(from);
        root.hidePanels(costPopup);
        root.placePanel(costPopup, anchor);
    }

    function showWords(from) {
        const anchor = root.anchorFrom(from);
        root.hidePanels(wordsPopup);
        root.placePanel(wordsPopup, anchor);
    }

    function showYearWords(from) {
        const anchor = root.anchorFrom(from);
        root.hidePanels(yearWordsPopup);
        root.placePanel(yearWordsPopup, anchor);
    }

    function showTrayMenu() {
        root.hidePanels(trayMenu);
        trayMenu.contentActive = true;
        trayMenu.listVisible = false;
        root.placePanel(trayMenu, root.trayAnchor());
    }

    Timer {
        interval: 250
        repeat: true
        running: true
        onTriggered: root.checkScreenLayout()
    }

    // Debug-only automation called by main.cpp when QT_QML_DEBUG and the profiling scenario
    // environment flag are both enabled. The steps are in ProfileScenario.qml.
    function profileScenario(step) {
        profile.run(step);
    }

    // qmllint disable missing-property
    function contains(rect, point) {
        return point.x >= rect.x && point.x <= rect.x + rect.width
            && point.y >= rect.y && point.y <= rect.y + rect.height;
    }
    // qmllint enable missing-property

    function dismissOutside(point) {
        // A panel with a surface of its own out claims the presses that land on that surface. One
        // that is not modal -- the settings dropdown -- is put down by a press anywhere else, and
        // the press is then read again below, where a press outside the panel closes the panel
        // too. A modal one, the removal question, is a question: a press elsewhere is not an
        // answer to it, and nothing here is read past it.
        for (const panel of [settingsPopup, wordsPopup]) {
            if (!panel.visible || !panel.openChildRect)
                continue;
            if (contains(panel.openChildRect, point) || panel.openChildIsModal)
                return;
            panel.closeChild();
        }

        const surfaces = [bar, settingsPopup, statsPopup, wordsPopup, yearWordsPopup, costPopup, trayMenu];
        for (const surface of surfaces) {
            if (!surface.visible)
                continue;
            const margin = surface.shadowMargin;
            const card = surface.hitRect !== undefined
                       ? surface.hitRect
                       : Qt.rect(surface.x + margin, surface.y + margin,
                                 surface.width - 2 * margin, surface.height - 2 * margin);
            if (!contains(card, point)) {
                // The bar draws the fade itself; a `visible = false` here would cut it off.
                if (surface === bar)
                    bar.dismiss();
                else
                    surface.visible = false;
            }
        }
    }

    SelectionBar { id: bar }
    Bubble {
        id: bubble
        onPlacementRequested: (payload) => root.placeBubble(bubble, payload)
        onHeightChanged: root.keepOnScreen(bubble)
    }
    /// The pet's right-click opens the panel on the page that carries the pet's block, beside the pet: the side is
    /// chosen from where the pet is at that click, so a pet the reader has moved gets the card on its other side. The
    /// tray's entry opens the same panel without choosing a page, and at the tray icon.
    PetWindow {
        id: petWindow
        onSettingsRequested: {
            root.showSettings(true);
            // After, not before: the panel clears its own category every time it comes up, so it opens on the list.
            settingsPopup.openCategory("extensions");
        }
    }

    /// The pet's review sign (UI.md 4.13) and the card that answers it. Passive like the pet it hangs from.
    ReviewSignWindow {
        id: reviewSign
        petWindow: petWindow
        workAreaAt: point => root.workArea(root.screenFor(point))
    }

    /// The debug profiling steps, driven by main.cpp through profileScenario() (ProfileScenario.qml).
    ProfileScenario {
        id: profile

        host: root
        trayMenu: trayMenu
        bubble: bubble
        bar: bar
    }

    /// The mouse hook's native-pixel points, mapped to the DIPs the surfaces are placed in (ScreenPoints.qml).
    ScreenPoints {
        id: points

        host: root
    }

    Notice {
        id: notice
        // The one action a notice can carry: the word it is about is the controller's, and so is
        // what asking for it costs.
        onActionTriggered: Controller.explainLemma(notice.noticeLemma)
    }

    /// A new version's card. Passive: not in the panels' mutual exclusion above, and not on
    /// the explanation notice slot, so it neither takes a notice's place nor loses one to it.
    UpdateCard {
        id: updateCard
        onViewRequested: Controller.updateDuty.openReleasePage()
        onSkipRequested: Controller.updateDuty.skipUpdate()
        onDownloadRequested: Controller.updateDuty.downloadUpdate()
        onCancelRequested: Controller.updateDuty.cancelDownload()
        onInstallRequested: Controller.updateDuty.installUpdate()
    }

    /// The capture mask, raised by the global trigger key and by nothing else. What it gives back is
    /// a rectangle for the capture duty; that it is also a surface is why it is here and not in the
    /// settings panel that owns the key.
    Mask {
        id: mask
        // The one place the mask and the capture duty meet: the rectangle comes back in desktop
        // coordinates and goes straight to the only thing that can take a shot of it. Nothing else
        // in the mask knows a controller exists, and the controller never learns a mask does --
        // which is why this wire had no test over it, and was missing until the reader pressed the
        // key and framed a region and nothing whatsoever happened.
        onRegionChosen: (region) => Controller.captureRegion(region)
    }

    SettingsPopup {
        id: settingsPopup
        onWidthChanged: root.keepOnScreen(settingsPopup)
        onHeightChanged: root.keepOnScreen(settingsPopup)
    }

    StatsPopup {
        id: statsPopup
        onCostRequested: root.showCost(statsPopup)
        onWordsRequested: root.showWords(statsPopup)
        onWidthChanged: root.holdCorner(statsPopup)
        onHeightChanged: root.holdCorner(statsPopup)
    }

    CostPopup {
        id: costPopup
        onBackRequested: root.showStats(costPopup)
        onWidthChanged: root.holdCorner(costPopup)
        onHeightChanged: root.holdCorner(costPopup)
    }

    WordsPopup {
        id: wordsPopup
        onYearRequested: root.showYearWords(wordsPopup)
        onBackRequested: root.showStats(wordsPopup)
        onWidthChanged: root.holdCorner(wordsPopup)
        onHeightChanged: root.holdCorner(wordsPopup)
    }

    YearWordsPopup {
        id: yearWordsPopup
        onBackRequested: root.showWords(yearWordsPopup)
        onWidthChanged: root.holdCorner(yearWordsPopup)
        onHeightChanged: root.holdCorner(yearWordsPopup)
    }

    TrayMenu {
        id: trayMenu
        onStatsRequested: root.showStats(trayMenu)
        onSettingsRequested: root.showSettings()
        onWidthChanged: root.keepOnScreen(trayMenu)
        onHeightChanged: root.keepOnScreen(trayMenu)
    }

    Connections {
        target: Controller

        function onSelectionBarRequested(payload) {
            const generation = ++root.selectionBarGeneration;
            // The minimum is a word length, which is a count of letters: it keeps an accidental
            // drag over two letters of English from raising the bar. Chinese is counted in
            // characters rather than letters, and one Han character is a word on its own, so the
            // rule has no floor to set here -- a phrase of two, which is the common size of a
            // Chinese word, would otherwise never reach the bar at all.
            if (payload.kind !== "translation" && payload.letterCount < Controller.settings.minimumWordLength) {
                bar.dismiss();
                return;
            }

            const at = points.toDip(payload);
            bar.selectionText = payload.text;
            bar.selectionKind = payload.kind;
            // A bar that is away is kept down until it has been placed, rather than shown where
            // it was last left and moved a line later -- a frame of the bar in the wrong place,
            // the corner of the screen, the first time. One that is already up is moved in the
            // same turn, so a second selection does not flash it out and in. open() is two
            // steps for the reason placePanel() gives: `visible` alone can leave a topmost
            // window under the taskbar.
            if (!bar.surfaceShown)
                bar.visible = false;
            Qt.callLater(() => {
                if (generation !== root.selectionBarGeneration)
                    return;
                root.placeSelectionBar(bar, at);
                bar.open();
            });
        }

        function onCaptureMaskRequested() {
            mask.open();
        }

        function onPointerPressed(at) {
            ++root.selectionBarGeneration;
            root.dismissOutside(points.toDip(at));
        }
    }

    Connections {
        target: Controller.explanationDuty

        function onBubbleChanged() {
            const payload = Controller.explanationDuty.bubble;
            if (payload && Object.keys(payload).length > 0) {
                const at = points.toDip(payload);
                bubble.show(at);
                // An answer now arrives a field at a time, so this runs several times for one card: a card the
                // reader is holding is theirs, and the stream must not pull it back to the anchor (repositionVisible
                // keeps the same rule for a screen change).
                Qt.callLater(() => {
                    // A language switch replays the answer in the frame the card already has:
                    // asking for placement here would move it as the new text changes height.
                    if (!bubble.dragging && !bubble.languageReplay)
                        root.placeBubble(bubble, at);
                });
            } else {
                bubble.dismiss();
            }
        }

        function onNoticeChanged() {
            const payload = Controller.explanationDuty.notice;
            if (payload && Object.keys(payload).length > 0)
                notice.show(payload);
            else
                notice.dismiss();
        }
    }

    Connections {
        target: Controller.updateDuty

        /// The card is raised and taken down from one flag the duty holds, rather than from a
        /// transition through states the reader would see as a flicker. `show()` centres the
        /// window, so it only runs when there is something new to show: the same signal also
        /// carries the settings switch and the state the card does not draw, and re-centring
        /// on those would move the card out from under a reader who is looking at it.
        function onUpdateChanged() {
            if (!Controller.updateDuty.cardVisible) {
                updateCard.dismiss();
                return;
            }
            const payload = Controller.updateDuty.update;
            if (updateCard.visible && updateCard.version === payload.latest)
                return;
            updateCard.show({version: payload.latest, pageUrl: payload.pageUrl, notes: payload.notes});
        }
    }

    Connections {
        target: Tray
        function onMenuRequested() {
            root.showTrayMenu();
        }
    }
}
