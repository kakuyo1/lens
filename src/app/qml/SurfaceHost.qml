import QtQuick
import "util/Numbers.js" as Numbers

/**
 * The host's placement and panel routing: where a surface goes, which panel replaces which, and
 * what a press outside closes. Main.qml owns the surfaces; this owns the rules that place them,
 * so the root object stays under the QML size cap (docs/QML.md section 1).
 *
 * Coordinates are DIPs. ScreenPoints maps the mouse hook's native pixels to DIPs before anything
 * here is called.
 */
QtObject {
    id: host

    /// Main's window, the screen a point between monitors falls back to.
    required property var screen
    /// The surfaces the rules act on.
    required property var bar
    required property var petWindow
    required property var reviewSign
    required property var trayMenu
    required property var settingsPopup
    required property var statsPopup
    required property var wordsPopup
    required property var yearWordsPopup
    required property var costPopup

    readonly property int screenMargin: 8
    property var placements: []
    property string screenLayoutSignature: ""

    /// @return The tray icon's rectangle, for anchoring the menu and the panels. Empty only
    ///         until the icon has been seen once: the shell reports nothing while the taskbar
    ///         is down, and an auto-hidden taskbar is down whenever a panel is opened from the
    ///         menu. Tray::geometry() remembers the last place it did report.
    function trayAnchor() {
        const g = Tray.geometry;
        if (g.width > 0)
            return g;
        return Qt.rect(screen.virtualX + screen.width - 56,
                       screen.virtualY + screen.height - 48, 56, 48);
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
        const area = workArea(target);
        const availableWidth = Math.max(1, area.right - area.left - 2 * screenMargin);
        const availableHeight = Math.max(1, area.bottom - area.top - 2 * screenMargin);
        return {
            width: Math.min(Math.max(1, width), availableWidth),
            height: Math.min(Math.max(1, height), availableHeight)
        };
    }

    /// @return Where a window of this size goes to sit beside the icon, whole when possible.
    function placeBeside(anchor, width, height, target, shadowMargin) {
        target = target || screenFor(anchorPoint(anchor));
        shadowMargin = shadowMargin === undefined ? 26 : shadowMargin;
        const margin = screenMargin;
        const area = workArea(target);
        const left = area.left + margin;
        const top = area.top + margin;
        const right = area.right - margin;
        const bottom = area.bottom - margin;

        const x = Numbers.clamp(anchor.x + anchor.width / 2 - width + shadowMargin,
                                left, right - width);
        let y = anchor.y + anchor.height / 2 - height + shadowMargin;
        if (y < top)
            y = anchor.y + anchor.height + 8;
        return Qt.point(Math.round(x), Math.round(Numbers.clamp(y, top, bottom - height)));
    }

    /// @brief Where a panel sits beside the pet's rectangle rather than beside the tray icon.
    ///
    /// The candidates are the pet's four sides in the order right, left, below, above, each a card
    /// @p gap clear of that side and centred across its length. Each is scored by how much of the
    /// pet the clamped card covers (0 for a side the card clears); lowest wins, ties by that order.
    /// The score rather than a fit test keeps the card off the pet: a work area too short clamps the
    /// card back over it, and the side clamped least far onto the pet survives. The horizontal sides
    /// come first because the card is 532 px tall and needs that much work-area height to clear the
    /// pet vertically, which the corner the pet is usually left in does not have.
    /// @param rectangle The pet window's rectangle as it stands. The pet can be dragged or grown
    ///        over an edge, so it is read against the work area first: the on-screen part is what
    ///        the card is put beside.
    /// @return The panel's top-left corner, in DIPs.
    function placeBesidePet(rectangle, width, height, gap) {
        const area = workArea(screenFor(Qt.point(rectangle.x + rectangle.width / 2,
                                                           rectangle.y + rectangle.height / 2)));
        const left = area.left + screenMargin;
        const top = area.top + screenMargin;
        const right = area.right - screenMargin;
        const bottom = area.bottom - screenMargin;
        // Each clamp is monotone, so a pet dragged wholly past an edge collapses onto that edge
        // rather than turning its rectangle inside out.
        const petLeft = Numbers.clamp(rectangle.x, left, right);
        const petRight = Numbers.clamp(rectangle.x + rectangle.width, petLeft, right);
        const petTop = Numbers.clamp(rectangle.y, top, bottom);
        const petBottom = Numbers.clamp(rectangle.y + rectangle.height, petTop, bottom);
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
            return Qt.rect(Numbers.clamp(at.x, left, right - width), Numbers.clamp(at.y, top, bottom - height),
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
        rememberPlacement(panel, null, "pet");
        const pet = Qt.rect(petWindow.x, petWindow.y, petWindow.width, petWindow.height);
        const at = placeBesidePet(pet, panel.width, panel.height, screenMargin);
        panel.x = at.x;
        panel.y = at.y;
        panel.visible = true;
        panel.raise();
    }

    /// @brief Put a surface up beside its anchor, on whichever screen that is.
    function placePanel(panel, anchor) {
        const placement = rememberPlacement(panel, anchor, "panel");
        const target = screenFor(anchorPoint(anchor));
        const layout = layoutFor(target, panel.width, panel.height);
        const at = placeBeside(anchor, layout.width, layout.height, target, panel.shadowMargin);
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
        rememberPlacement(surface, anchor, "transient");
        const target = screenFor(anchorPoint(anchor));
        // Read the dimensions at placement time. Bubble's implicit height changes when its
        // senses, translation, or hover actions settle; caching its first size breaks that path.
        const layout = layoutFor(target, surface.width, surface.height);
        const area = workArea(target);
        const left = area.left + screenMargin;
        const top = area.top + screenMargin;
        const right = area.right - screenMargin;
        const bottom = area.bottom - screenMargin;
        const shadow = surface.shadowMargin;
        const x = Numbers.clamp(anchor.x - shadow, left, right - layout.width);
        const preferBelow = Controller.settings.bubblePlacement === "below";
        const y = Numbers.clamp(transientTop(anchor.y, gap, layout.height, shadow, top, bottom, preferBelow), top, bottom - layout.height);
        surface.x = Math.round(x);
        surface.y = Math.round(y);
        surface.raise();
    }

    /// @brief Put the action bar up in the form the reader chose (UI-SPEC.md 4.10).
    ///
    /// The form's own placement lives on SelectionBar (placeAt), next to the geometry it is
    /// placing; this only finds the screen and hands it the work area.
    function placeSelectionBar(surface, anchor) {
        rememberPlacement(surface, anchor, "transient");
        surface.placeAt(anchor, workArea(screenFor(anchor)), screenMargin);
    }

    /// The answer to a review carries no verdict buttons (Controller.explanationDuty.bubble); every other card is placed
    /// beside its anchor as before.
    function placeBubble(surface, payload) {
        if (payload.verdictButtons === false)
            reviewSign.placeAnswerCard(surface);
        else
            placeTransient(surface, payload, surface.gap);
    }
    // qmllint enable missing-property

    /// @return The anchor a panel that replaces `from` is put up at: the corner `from` hangs from, so
    ///         the next card lands where the one it replaces was. Without a `from`, the tray icon.
    function anchorFrom(from) {
        if (!from)
            return trayAnchor();
        const corner = Qt.point(from.x + from.width - from.shadowMargin, from.y + from.height - from.shadowMargin);
        return Qt.rect(corner.x, corner.y, 0, 0);
    }

    /// A stack panel grows from the corner it hangs from, its bottom-right, so it grows up and to the
    /// left rather than down past the tray icon. The placement remembers the size it was last placed
    /// at. A drag moves the window without changing that size, so the corner is read from where the
    /// window is now, and a dragged panel keeps the corner the reader left it at.
    function holdCorner(panel) {
        const placement = placements.find(function (candidate) { return candidate.surface === panel; });
        if (placement && panel.visible) {
            panel.x = panel.x + placement.width - panel.width;
            panel.y = panel.y + placement.height - panel.height;
            placement.width = panel.width;
            placement.height = panel.height;
        }
        keepOnScreen(panel);
    }

    /// A surface that grows after it was placed -- a row that opens, a list that loads -- keeps its
    /// top-left corner, so it runs off the bottom or right edge by however much it grew. Pull it back
    /// by that much. Placement is only ever computed from the anchor, so nothing else would. The stack
    /// uses holdCorner() instead, which keeps the corner rather than pulling it back.
    function keepOnScreen(surface) {
        if (!surface.visible)
            return;
        const area = workArea(screenFor(Qt.point(surface.x + surface.width / 2, surface.y + surface.height / 2)));
        surface.x = Numbers.clamp(surface.x, area.left + screenMargin, area.right - screenMargin - surface.width);
        surface.y = Numbers.clamp(surface.y, area.top + screenMargin, area.bottom - screenMargin - surface.height);
    }

    function repositionVisible() {
        for (const placement of placements) {
            // A review's answer (a bubble with no verdict buttons) is placed by reviewSign, from its own record.
            if (!placement.surface.visible || placement.surface.verdictButtons === false)
                continue;
            if (placement.kind === "panel")
                placePanel(placement.surface, placement.anchor);
            else if (placement.kind === "pet")
                placePetPanel(placement.surface);
            else if (placement.surface === bar)
                placeSelectionBar(bar, placement.anchor);
            else if (!placement.surface.dragging)
                placeTransient(placement.surface, placement.anchor, placement.surface.gap);
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
        const signature = currentScreenLayoutSignature();
        if (signature !== screenLayoutSignature) {
            screenLayoutSignature = signature;
            repositionVisible();
        }
    }

    /**
     * Take down every panel but the one being opened: the up panel fades out where it stands while
     * the opening one fades in beside it (ShadowCard) -- a cross-fade, or an immediate take-down
     * with the animations off. The one being opened is taken back from a fade if it was leaving.
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
        const anchor = anchorFrom(from);
        hidePanels(statsPopup);
        placePanel(statsPopup, anchor);
    }

    /// @param fromPet The pet's own gesture asked for the panel, and it is the only caller that wants it
    ///        beside the pet: the tray menu's entry names a panel rather than a place, and keeps the icon.
    function showSettings(fromPet) {
        hidePanels(settingsPopup);
        if (fromPet)
            placePetPanel(settingsPopup);
        else
            placePanel(settingsPopup, trayAnchor());
    }

    function showCost(from) {
        const anchor = anchorFrom(from);
        hidePanels(costPopup);
        placePanel(costPopup, anchor);
    }

    /// @param filter The words panel's verdict filter: 0 all, 1 known, 2 new. Called without it
    ///        (the year card's back arrow) the list opens on all words.
    function showWords(from, filter) {
        const anchor = anchorFrom(from);
        hidePanels(wordsPopup);
        wordsPopup.filter = filter === undefined ? 0 : filter;
        placePanel(wordsPopup, anchor);
    }

    function showYearWords(from) {
        const anchor = anchorFrom(from);
        hidePanels(yearWordsPopup);
        placePanel(yearWordsPopup, anchor);
    }

    function showTrayMenu() {
        hidePanels(trayMenu);
        trayMenu.contentActive = true;
        trayMenu.listVisible = false;
        placePanel(trayMenu, trayAnchor());
        // A tray menu is opened by the reader, not by a selection they are still reading: it takes
        // the keyboard while it is up. The focus call waits for the body loader to build the rows.
        trayMenu.requestActivate();
        Qt.callLater(() => {
            if (!trayMenu.visible)
                return;
            trayMenu.requestActivate();
            if (!trayMenu.focusFirstRow())
                Qt.callLater(() => { if (trayMenu.visible) trayMenu.focusFirstRow(); });
        });
    }

    // qmllint disable missing-property
    function contains(rect, point) {
        return point.x >= rect.x && point.x <= rect.x + rect.width
            && point.y >= rect.y && point.y <= rect.y + rect.height;
    }
    // qmllint enable missing-property

    function dismissOutside(point) {
        // A panel with a surface of its own out claims the presses that land on that surface. A
        // non-modal one, the settings dropdown, is put down by a press anywhere else, and the press
        // is then read again below, where a press outside closes the panel too. A modal one, the
        // removal question, takes no answer from a press elsewhere: nothing here is read past it.
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
}
