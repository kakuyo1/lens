pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import "../util/Numbers.js" as Numbers

/**
 * The bar that comes up when the reader finishes selecting text (UI-SPEC.md 4.10).
 *
 * Its form is the reader's setting (`selectionBarStyle`): a page tab beside the selection, a
 * hand of three cards, a hanging tag, or a fan. All four offer the same three actions and the
 * same contract -- it never takes focus, so clicking one of its items leaves the reader's
 * application in front and the next injected copy keystroke still lands in the right place.
 */
Window {
    id: bar
    objectName: "selectionBar"

    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("selectionBar")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: bar.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    /// Whether the bar is meant to be on screen. Every show and hide goes through open() and
    /// dismiss(), which move this and leave the window to SurfaceFade.qml.
    property bool surfaceShown: false

    readonly property int shadowMargin: 26
    /// Clearance between the bar and the selection: the tag needs the length of its string.
    readonly property int gap: bar.barStyle === "tag" ? 16 : 8

    /// The form in force. The original flat bar is the default; an unknown stored value falls
    /// back to it rather than to a bar with no body at all.
    readonly property string barStyle: {
        const value = Controller.settings.selectionBarStyle;
        return ["bar", "tab", "hand", "tag", "fan"].indexOf(value) >= 0 ? value : "bar";
    }

    /// The body in force, whichever form that is: the surface-level tests reach the press
    /// gestures and the geometry through it.
    readonly property alias bodyItem: bodyLoader.item

    /// The tab's side and the tag's string edge, chosen by placeAt() before the bar is shown.
    property bool sideLeft: false
    property bool tagAbove: false

    /// The selection the actions apply to, set by Main.qml before it places and shows the bar.
    property string selectionText: ""
    property string selectionKind: "word"
    /// Whether the selection is a sentence the breakdown can take (English, 40 words or fewer).
    property bool breakdownable: false
    /// Whether the selection is one token the spelling correction can take, and whether it is an English
    /// sentence of 40 words or fewer the grammar correction can take. Set from the selection's payload.
    property bool spellable: false
    property bool grammarable: false

    /// The shapes this run has already introduced. The first arrival of a shape is the one that
    /// teaches it; later arrivals halve the entrance (UI-SPEC 4.17, ADR 0109).
    property var introducedStyles: ({})
    /// Read by the fan and the tag when they start their entrance; set by open().
    property real entranceFactor: 1

    /// The three actions, in every form. The `qsTr` calls live here, on the surface, so the
    /// translations keep their SelectionBar context whichever body draws them.
    readonly property var actions: [
        {action: "translate", label: qsTr("Translate"), source: "qrc:/icons/ui-translate.svg"},
        {action: "explain", label: qsTr("Explain"), source: "qrc:/icons/ui-explain.svg"},
        {action: "copy", label: qsTr("Copy text"), source: "qrc:/icons/ui-copy.svg"}
    ]

    /// What the copy text's drawer offers for this selection (UI-SPEC.md 4.10), in the order the spec lists
    /// them: the two corrections, then the breakdown.
    readonly property var drawerItems: {
        const items = [];
        if (bar.spellable)
            items.push({action: "spell", label: qsTr("Spell correction"), source: "qrc:/icons/ui-spell.svg"});
        if (bar.grammarable)
            items.push({action: "grammar", label: qsTr("Grammar correction"), source: "qrc:/icons/ui-grammar.svg"});
        if (bar.selectionKind === "sentence" && bar.breakdownable)
            items.push({action: "breakdown", label: qsTr("Sentence breakdown"), source: "qrc:/icons/ui-breakdown.svg"});
        return items;
    }

    /// How far the drawer reaches past the body on each side, in px (UI-SPEC.md 4.10). The window keeps that room
    /// while the drawer applies, so the drawer draws inside the window and the body keeps its place. It is
    /// measured from the drawer's own labels, so a longer label or a second item widens the window with it.
    // qmllint disable missing-property
    readonly property var reach: drawerItems.length > 0 && bar.bodyItem && bar.bodyItem.drawer
                                 ? bar.bodyItem.drawer.overhangIn(bar.bodyItem)
                                 : ({top: 0, right: 0, bottom: 0, left: 0})
    // qmllint enable missing-property
    /// Where the body's top-left corner sits inside the window.
    readonly property real originX: shadowMargin + reach.left
    readonly property real originY: shadowMargin + reach.top

    /// The card's place on screen is held while the room around it changes: when the origin moves, the window
    /// moves the other way by the same amount, so the window grows toward the drawer and the card stays put.
    /// The window's position is whole pixels, so the hold works on the rounded origin; a fraction taken from
    /// each step would otherwise be lost, and the card would drift by a pixel a step.
    property int heldOriginX: 0
    property int heldOriginY: 0
    onOriginXChanged: {
        const now = Math.round(bar.originX);
        bar.x += bar.heldOriginX - now;
        bar.heldOriginX = now;
    }
    onOriginYChanged: {
        const now = Math.round(bar.originY);
        bar.y += bar.heldOriginY - now;
        bar.heldOriginY = now;
    }
    Component.onCompleted: {
        bar.heldOriginX = Math.round(bar.originX);
        bar.heldOriginY = Math.round(bar.originY);
    }

    /// Which edge the drawer opens from: "up" or "down" for the vertical forms, "right" for the fan.
    /// Set by placeAt() once the bar is placed; empty while it is not.
    property string drawerSide: ""

    width: bodyLoader.width + 2 * shadowMargin + reach.left + reach.right
    height: bodyLoader.height + 2 * shadowMargin + reach.top + reach.bottom

    SurfaceFade {
        id: fade
        tokens: bar.tokens
        window: bar
        shown: bar.surfaceShown
        // The bar stands beside the pointer, so its exit is the shorter press token: it is out
        // of the way before the next selection arrives.
        leaveDuration: bar.tokens.motion.press
    }

    /// @brief Put the bar up. The caller places the window first; this starts the arrival.
    function open() {
        const seen = introducedStyles[bar.barStyle] === true;
        entranceFactor = seen ? 0.5 : 1;
        introducedStyles[bar.barStyle] = true;
        surfaceShown = true;
        visible = true;
        raise();
    }

    /// @brief Take the bar down. The action, if any, has already happened; only the window waits.
    function dismiss() {
        surfaceShown = false;
    }

    /// @brief Run the action an item was pressed for. The body has already played its gesture -- it
    ///        emits `picked` from the same tap that started it, so no body member is reached here.
    function pick(action) {
        Controller.explanationDuty.runSelectionAction(action, bar.selectionText);
        bar.dismiss();
    }

    /// @brief Place this window at @p anchor, the point the drag ended at (UI-SPEC.md 4.10).
    ///
    /// @param area The work area of the anchor's screen, in virtual DIPs.
    /// @param margin The 8 px every surface keeps clear of the work area's edges.
    ///
    /// The five forms do not share an edge: the classic bar's left edge sits at the anchor; the
    /// tab stands beside it; the hand and the fan centre on it; the tag hangs from a grommet 15 px
    /// in from its card. Each rule also decides which way it flips when the preferred side has no
    /// room, and the result is clamped into the work area. `sideLeft` and `tagAbove` tell the
    /// body which way it ended up leaning, and `drawerSide` which way the drawer opens.
    function placeAt(anchor: point, area: var, margin: int) {
        const left = area.left + margin;
        const top = area.top + margin;
        const right = area.right - margin;
        const bottom = area.bottom - margin;
        // Whole pixels, as the window's place is: the same origin the hold above keeps the card at.
        const ox = Math.round(bar.originX);
        const oy = Math.round(bar.originY);
        const width = bar.width;
        const height = bar.height;
        const bodyWidth = bodyLoader.width;
        const bodyHeight = bodyLoader.height;
        const preferBelow = Controller.settings.bubblePlacement === "below";
        let x = 0;
        let y = 0;

        if (bar.barStyle === "bar") {
            // The original flat bar: its card's left edge sits at the release point, and it
            // takes the bubble's preferred side with the old flip-then-clamp rule.
            x = anchor.x - ox;
            const below = anchor.y + bar.gap - oy;
            const above = anchor.y - bar.gap - bodyHeight - oy;
            y = preferBelow ? (below + height <= bottom ? below : above)
                            : (above >= top ? above : below);
        } else if (bar.barStyle === "tab") {
            // The right of the selection first, the left when the right has no room; the two
            // outer candidates are ten pixels clear of the release point.
            const rightX = anchor.x + 10 - ox;
            const leftX = anchor.x - 10 - bodyWidth - ox;
            bar.sideLeft = rightX + width > right;
            x = bar.sideLeft ? leftX : rightX;
            y = anchor.y - bodyHeight / 2 - oy;
        } else if (bar.barStyle === "tag") {
            // The grommet is 15 px from the card's left edge; the string end touches the anchor.
            x = anchor.x - 15 - ox;
            const below = anchor.y + bar.gap - oy;
            const above = anchor.y - bar.gap - bodyHeight - oy;
            let aboveFirst = !preferBelow;
            if (aboveFirst && above < top)
                aboveFirst = false;
            else if (!aboveFirst && below + height > bottom)
                aboveFirst = true;
            bar.tagAbove = aboveFirst;
            y = aboveFirst ? above : below;
        } else {
            // The hand and the fan centre on the anchor and share the bubble's preferred side.
            x = anchor.x - bodyWidth / 2 - ox;
            const below = anchor.y + bar.gap - oy;
            const above = anchor.y - bar.gap - bodyHeight - oy;
            y = preferBelow ? (below + height <= bottom ? below : above)
                            : (above >= top ? above : below);
        }

        bar.x = Math.round(Numbers.clamp(x, left, right - width));
        bar.y = Math.round(Numbers.clamp(y, top, bottom - height));
        bar.raise();

        // The drawer opens away from the selection where it can (the classic bar), up for the
        // hand, down for the tab and the tag, and to the right for the fan; it flips when its side
        // has no room. The reserved room above is what the window already holds for it.
        const bodyTop = bar.y + oy;
        const roomAbove = bodyTop - top >= bar.reach.top;
        const roomBelow = bottom - (bodyTop + bodyHeight) >= bar.reach.bottom;
        let up = bar.barStyle === "bar" ? bodyTop < anchor.y : bar.barStyle === "hand";
        if (up && !roomAbove)
            up = false;
        else if (!up && !roomBelow)
            up = true;
        bar.drawerSide = bar.drawerItems.length === 0 ? ""
                       : bar.barStyle === "fan" ? "right"
                       : up ? "up" : "down";
    }

    Item {
        id: content
        x: bar.originX
        y: bar.originY
        width: bodyLoader.width
        height: bodyLoader.height
        opacity: fade.progress
        // A bar that is still on screen while it fades out must not take the press that was
        // meant for the text underneath it.
        enabled: bar.surfaceShown
        transform: Translate { y: fade.lift }

        // The whole bar is the handle: pressing an item taps it, pressing the padding and moving drags
        // the window. The two reasons this is a timer, not the handler's own signal, are in
        // ShadowCard.qml.
        DragHandler {
            id: mover
            target: null

            property point grabCursor: Qt.point(0, 0)
            property point grabWindow: Qt.point(0, 0)

            function place() {
                const at = Controller.cursorPos();
                bar.x = Math.round(grabWindow.x + at.x - grabCursor.x);
                bar.y = Math.round(grabWindow.y + at.y - grabCursor.y);
            }

            onActiveChanged: {
                if (active) {
                    grabCursor = Controller.cursorPos();
                    grabWindow = Qt.point(bar.x, bar.y);
                } else {
                    place();
                }
            }
        }

        Timer {
            interval: 16
            repeat: true
            running: mover.active
            onTriggered: mover.place()
        }

        Loader {
            id: bodyLoader
            sourceComponent: bar.barStyle === "bar" ? classicComponent
                             : bar.barStyle === "tab" ? tabComponent
                             : bar.barStyle === "hand" ? handComponent
                             : bar.barStyle === "tag" ? tagComponent
                             : fanComponent
        }
    }

    Component {
        id: classicComponent
        SelectionBarClassicBody {
            actions: bar.actions
            drawerItems: bar.drawerItems
            drawerSide: bar.drawerSide
            shown: bar.surfaceShown
            glassSheet: bar.glass
            onPicked: (action) => bar.pick(action)
        }
    }

    Component {
        id: tabComponent
        SelectionBarTabBody {
            actions: bar.actions
            drawerItems: bar.drawerItems
            drawerSide: bar.drawerSide
            shown: bar.surfaceShown
            sideLeft: bar.sideLeft
            glassSheet: bar.glass
            onPicked: (action) => bar.pick(action)
        }
    }

    Component {
        id: handComponent
        SelectionBarHandBody {
            actions: bar.actions
            drawerItems: bar.drawerItems
            drawerSide: bar.drawerSide
            shown: bar.surfaceShown
            glassSheet: bar.glass
            onPicked: (action) => bar.pick(action)
        }
    }

    Component {
        id: tagComponent
        SelectionBarTagBody {
            actions: bar.actions
            drawerItems: bar.drawerItems
            drawerSide: bar.drawerSide
            shown: bar.surfaceShown
            above: bar.tagAbove
            entranceFactor: bar.entranceFactor
            glassSheet: bar.glass
            onPicked: (action) => bar.pick(action)
        }
    }

    Component {
        id: fanComponent
        SelectionBarFanBody {
            actions: bar.actions
            drawerItems: bar.drawerItems
            drawerSide: bar.drawerSide
            shown: bar.surfaceShown
            entranceFactor: bar.entranceFactor
            glassSheet: bar.glass
            onPicked: (action) => bar.pick(action)
        }
    }

    GlassHost {
        window: bar
        sheet: bar.glass
        dragging: mover.active
    }
}
