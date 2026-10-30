import QtQuick

/**
 * The host's placement and panel routing: which rule puts a surface where, which panel replaces
 * which, and what a press outside closes. Main.qml owns the surfaces; this gathers the facts a
 * rule needs and writes the answer, so the root object stays under the QML size cap (docs/QML.md
 * section 1).
 *
 * The arithmetic is SurfaceGeometry's (src/app/geometry/surface_geometry.h): this file reads the
 * windows, the screens and their work areas, hands those facts over, and writes back the top-left
 * it gets. Coordinates are DIPs; ScreenPoints maps the mouse hook's native pixels to DIPs before
 * anything here is called.
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

    /// The clear band every surface keeps off the work area's edges, from the one geometry module.
    readonly property int screenMargin: SurfaceGeometry.margin
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
            // The origin is not scaled, as SurfaceGeometry.dipFromNativePoint() assumes: only the size is in DIPs.
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

    /// @return @p target's work area as the rectangle SurfaceGeometry takes: its far edges are `right`
    ///         and `bottom`, which is how QRectF reads them.
    function areaOf(target) {
        const area = workArea(target);
        return Qt.rect(area.left, area.top, area.right - area.left, area.bottom - area.top);
    }

    /// @return A native-size surface bounded to the work area without changing its content scale.
    function layoutFor(target, width, height) {
        const size = SurfaceGeometry.boundedSize(Qt.size(width, height), areaOf(target), screenMargin);
        return {
            width: size.width,
            height: size.height
        };
    }

    /// @return Where a window of this size goes to sit beside the icon, whole when possible.
    function placeBeside(anchor, width, height, target, shadowMargin) {
        target = target || screenFor(anchorPoint(anchor));
        shadowMargin = shadowMargin === undefined ? 26 : shadowMargin;
        return SurfaceGeometry.besideAnchor(Qt.rect(anchor.x, anchor.y, anchor.width, anchor.height),
                                            Qt.size(width, height), areaOf(target), shadowMargin, screenMargin);
    }

    /// @brief Where a panel sits beside the pet's rectangle rather than beside the tray icon.
    /// @param rectangle The pet window's rectangle as it stands. The pet can be dragged or grown
    ///        over an edge, so it is read against the work area first: the on-screen part is what
    ///        the card is put beside.
    /// @return The panel's top-left corner, in DIPs.
    function placeBesidePet(rectangle, width, height, gap) {
        const target = screenFor(Qt.point(rectangle.x + rectangle.width / 2,
                                          rectangle.y + rectangle.height / 2));
        return SurfaceGeometry.besidePet(Qt.rect(rectangle.x, rectangle.y, rectangle.width, rectangle.height),
                                         Qt.size(width, height), areaOf(target), gap, screenMargin);
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

    // qmllint disable missing-property
    function placeTransient(surface, anchor, gap) {
        rememberPlacement(surface, anchor, "transient");
        const target = screenFor(anchorPoint(anchor));
        // Read the dimensions at placement time. Bubble's implicit height changes when its
        // senses, translation, or hover actions settle; caching its first size breaks that path.
        const layout = layoutFor(target, surface.width, surface.height);
        const at = SurfaceGeometry.transient(Qt.point(anchor.x, anchor.y), Qt.size(layout.width, layout.height),
                                             areaOf(target), gap, surface.shadowMargin,
                                             Controller.settings.bubblePlacement === "below", screenMargin);
        surface.x = at.x;
        surface.y = at.y;
        surface.raise();
    }

    /// @brief Where a list's review card sits against the word it belongs to (UI-SPEC.md 4.8).
    ///
    /// The card's bottom edge keeps a fixed gap above the word and its left edge lines up with
    /// the word's left edge. The height is read at placement time and the card is put back whenever
    /// it changes, because a card that grew after placement would otherwise creep down over the
    /// word and every word would end up with a different gap. Near the top of the screen it flips
    /// below the word; near either side edge it is clamped, so the gap survives the edge and only
    /// the horizontal alignment gives way.
    ///
    /// The gap is measured on the card, not the window: the window's 26 px shadow margin dips over
    /// the word instead of hanging clear of it, so the visible gap is the plain `gap` and the
    /// pointer's road from the word onto the card is that short. The margin still takes mouse
    /// input, so the band over the word steals its hover -- reviewHold answers that hold with
    /// geometry, or the theft would close the card under a pointer that never left the reading.
    ///
    /// @param surface The bubble, already sized to its content.
    /// @param payload The anchor rectangle in DIPs: the word's x, y, anchorWidth and anchorHeight.
    function placeReviewCard(surface, payload) {
        if (!surface || surface.dragging)
            return;
        placeAgainstWord(surface, payload);
        reviewHold.restart();
    }

    /// @brief Put @p surface against the word @p payload names, by the rule a review card follows.
    ///
    /// A word's hint is placed here too, so a line and a card with the same gap sit the same way. A hint
    /// takes no hold, since no pointer is on it to keep.
    /// @param payload The anchor rectangle in DIPs: the word's x, y, anchorWidth and anchorHeight. Nothing when null.
    function placeAgainstWord(surface, payload) {
        if (!surface || !payload)
            return;
        const target = screenFor(Qt.point(payload.x, payload.y));
        const at = SurfaceGeometry.reviewCard(
                    Qt.rect(payload.x, payload.y, payload.anchorWidth || 0, payload.anchorHeight || 0),
                    Qt.size(surface.width, surface.height), areaOf(target),
                    surface.gap, surface.shadowMargin, screenMargin);
        surface.x = at.x;
        surface.y = at.y;
        surface.raise();
    }

    /// The hold a review card's life runs on: the pointer is on the reading exactly while it is
    /// inside the card's window, the shadow band that dips over the word included -- the word
    /// under that band is still where the reading started. Geometry, not hover events: the band
    /// takes the word's hover away the frame the window lands on it, and a hold that waited for
    /// a hover the theft suppressed would let the card close under a pointer that never moved.
    readonly property Timer reviewHold: Timer {
        interval: 80
        repeat: true

        onTriggered: {
            const placement = host.placements.find(function (candidate) {
                return candidate.kind === "review" && candidate.surface.visible;
            });
            if (!placement) {
                stop();
                Controller.explanationDuty.holdReviewCard(false);
                return;
            }
            const at = Controller.cursorPos();
            const s = placement.surface;
            Controller.explanationDuty.holdReviewCard(
                        at.x >= s.x && at.x <= s.x + s.width
                        && at.y >= s.y && at.y <= s.y + s.height);
        }
    }

    /// @brief Re-place a bubble after its height changed.
    ///
    /// A review card is pinned by its bottom edge, so growing content must move the top, not the
    /// gap to the word. Every other bubble keeps the top it was placed at and is only pulled back
    /// on screen, which is the old behaviour.
    function bubbleGeometryChanged(surface) {
        const placement = placements.find(function (candidate) { return candidate.surface === surface; });
        if (placement && placement.kind === "review" && surface.surfaceShown && !surface.dragging)
            placeReviewCard(surface, placement.anchor);
        else
            keepOnScreen(surface);
    }

    /// @brief Put the action bar up in the form the reader chose (UI-SPEC.md 4.10).
    ///
    /// The form's own placement lives on SelectionBar (placeAt), next to the geometry it is
    /// placing; this only finds the screen and hands it the work area.
    function placeSelectionBar(surface, anchor) {
        rememberPlacement(surface, anchor, "transient");
        surface.placeAt(anchor, workArea(screenFor(anchor)), screenMargin);
    }

    /// A review card belongs to the word list and is placed against its word's rectangle; the
    /// answer beside the pet's sign is placed by reviewSign; every other card hangs from its anchor.
    /// The card's own kind is what tells them apart, not flags each caller would have to combine.
    function placeBubble(surface, payload) {
        if (payload.cardKind === "review") {
            rememberPlacement(surface, payload, "review");
            placeReviewCard(surface, payload);
        } else if (payload.cardKind === "reviewAnswer") {
            // The sign keeps its own record of where this answer goes; marking the kind here keeps
            // a later height change from reading the review placement that came before it.
            rememberPlacement(surface, payload, "reviewSign");
            reviewSign.placeAnswerCard(surface);
        } else {
            placeTransient(surface, payload, surface.gap);
        }
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
        const at = SurfaceGeometry.keptOnScreen(
                    Qt.point(surface.x, surface.y), Qt.size(surface.width, surface.height),
                    areaOf(screenFor(Qt.point(surface.x + surface.width / 2, surface.y + surface.height / 2))),
                    screenMargin);
        surface.x = at.x;
        surface.y = at.y;
    }

    function repositionVisible() {
        for (const placement of placements) {
            // A review's answer is placed by reviewSign, from its own record.
            if (!placement.surface.visible || placement.kind === "reviewSign")
                continue;
            if (placement.kind === "panel")
                placePanel(placement.surface, placement.anchor);
            else if (placement.kind === "pet")
                placePetPanel(placement.surface);
            else if (placement.kind === "review")
                placeReviewCard(placement.surface, placement.anchor);
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
    ///        (the year card's back arrow) the list opens on all words. The model owns the filter
    ///        (docs/adr/0122); the panel only reads it back.
    function showWords(from, filter) {
        const anchor = anchorFrom(from);
        hidePanels(wordsPopup);
        Controller.wordsModel.filter = filter === undefined ? 0 : filter;
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
