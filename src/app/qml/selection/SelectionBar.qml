pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import "../util/Numbers.js" as Numbers

/**
 * The bar that comes up when the reader finishes selecting text (UI.md 4.10).
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

    /// The tab's side and the tag's string edge, chosen by SurfaceHost.qml before the bar is placed.
    property bool sideLeft: false
    property bool tagAbove: false

    /// The selection the actions apply to, set by SurfaceHost.qml before it places and shows the bar.
    property string selectionText: ""
    property string selectionKind: "word"

    /// The three actions, in every form. The `qsTr` calls live here, on the surface, so the
    /// translations keep their SelectionBar context whichever body draws them.
    readonly property var actions: [
        {action: "translate", label: qsTr("Translate"), source: "qrc:/icons/ui-translate.svg"},
        {action: "explain", label: qsTr("Explain"), source: "qrc:/icons/ui-explain.svg"},
        {action: "copy", label: qsTr("Copy text"), source: "qrc:/icons/ui-copy.svg"}
    ]

    width: bodyLoader.width + 2 * shadowMargin
    height: bodyLoader.height + 2 * shadowMargin

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
        surfaceShown = true;
        visible = true;
        raise();
    }

    /// @brief Take the bar down. The action, if any, has already happened; only the window waits.
    function dismiss() {
        surfaceShown = false;
    }

    /// @brief Run the action an item was pressed for. The body has already played its gesture:
    ///        it emits `picked` from the same tap that started the gesture, so no member of the
    ///        loaded body has to be reached from here.
    function pick(action) {
        Controller.explanationDuty.runSelectionAction(action, bar.selectionText);
        bar.dismiss();
    }

    /// @brief Place this window at @p anchor, the point the drag ended at (UI.md 4.10).
    ///
    /// @param area The work area of the anchor's screen, in virtual DIPs.
    /// @param margin The 8 px every surface keeps clear of the work area's edges.
    ///
    /// The five forms do not share an edge: the classic bar's left edge sits at the anchor; the
    /// tab stands beside it; the hand and the fan centre on it; the tag hangs from a grommet 15 px
    /// in from its card. Each rule also decides which way it flips when the preferred side has no
    /// room, and the result is clamped into the work area. `sideLeft` and `tagAbove` tell the
    /// body which way it ended up leaning.
    function placeAt(anchor: point, area: var, margin: int) {
        const left = area.left + margin;
        const top = area.top + margin;
        const right = area.right - margin;
        const bottom = area.bottom - margin;
        const shadow = bar.shadowMargin;
        const width = bar.width;
        const height = bar.height;
        const preferBelow = Controller.settings.bubblePlacement === "below";
        let x = 0;
        let y = 0;

        if (bar.barStyle === "bar") {
            // The original flat bar: its card's left edge sits at the release point, and it
            // takes the bubble's preferred side with the old flip-then-clamp rule.
            x = anchor.x - shadow;
            const below = anchor.y + bar.gap - shadow;
            const above = anchor.y - bar.gap - height + shadow;
            y = preferBelow ? (below + height <= bottom ? below : above)
                            : (above >= top ? above : below);
        } else if (bar.barStyle === "tab") {
            // The right of the selection first, the left when the right has no room; the two
            // outer candidates are ten pixels clear of the release point.
            const rightX = anchor.x + 10 - shadow;
            const leftX = anchor.x - 10 - width + shadow;
            bar.sideLeft = rightX + width > right;
            x = bar.sideLeft ? leftX : rightX;
            y = anchor.y - height / 2;
        } else if (bar.barStyle === "tag") {
            // The grommet is 15 px from the card's left edge; the string end touches the anchor.
            x = anchor.x - 15 - shadow;
            const below = anchor.y + bar.gap - shadow;
            const above = anchor.y - bar.gap - height + shadow;
            let aboveFirst = !preferBelow;
            if (aboveFirst && above < top)
                aboveFirst = false;
            else if (!aboveFirst && below + height > bottom)
                aboveFirst = true;
            bar.tagAbove = aboveFirst;
            y = aboveFirst ? above : below;
        } else {
            // The hand and the fan centre on the anchor and share the bubble's preferred side.
            x = anchor.x - width / 2;
            const below = anchor.y + bar.gap - shadow;
            const above = anchor.y - bar.gap - height + shadow;
            y = preferBelow ? (below + height <= bottom ? below : above)
                            : (above >= top ? above : below);
        }

        bar.x = Math.round(Numbers.clamp(x, left, right - width));
        bar.y = Math.round(Numbers.clamp(y, top, bottom - height));
        bar.raise();
    }

    Item {
        id: content
        x: bar.shadowMargin
        y: bar.shadowMargin
        width: bodyLoader.width
        height: bodyLoader.height
        opacity: fade.progress
        // A bar that is still on screen while it fades out must not take the press that was
        // meant for the text underneath it.
        enabled: bar.surfaceShown
        transform: Translate { y: fade.lift }

        // The whole bar is the handle: pressing an item taps it, pressing the padding and
        // moving drags the window. The two reasons this is a timer rather than the handler's
        // own signal are in ShadowCard.qml.
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
            shown: bar.surfaceShown
            glassSheet: bar.glass
            onPicked: (action) => bar.pick(action)
        }
    }

    Component {
        id: tabComponent
        SelectionBarTabBody {
            actions: bar.actions
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
            shown: bar.surfaceShown
            glassSheet: bar.glass
            onPicked: (action) => bar.pick(action)
        }
    }

    Component {
        id: tagComponent
        SelectionBarTagBody {
            actions: bar.actions
            shown: bar.surfaceShown
            above: bar.tagAbove
            glassSheet: bar.glass
            onPicked: (action) => bar.pick(action)
        }
    }

    Component {
        id: fanComponent
        SelectionBarFanBody {
            actions: bar.actions
            shown: bar.surfaceShown
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
