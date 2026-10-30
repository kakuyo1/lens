import QtQuick
import QtQuick.Window

/**
 * The word list's answer for a word with nothing stored in the current explanation language
 * (UI-SPEC.md 4.8): a line set beside the word, in the bubble's own look, with the one button that asks the model.
 *
 * The pointer has to be able to reach the button, so this window takes input. Whether the pointer is on it is
 * reported to the duty from inside this window, the shadow band included, so the row's dismissal waits for the
 * pointer to leave the hint as well as the word. SurfaceHost places it against the word `wordAt` names.
 */
Window {
    id: hint
    objectName: "wordHint"

    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("notice")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: hint.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    /// The gap the card keeps from its word, the bubble's own, so a hint and a review card sit alike.
    readonly property int gap: 10
    readonly property int cardWidth: 240
    readonly property int padding: 12

    /// The word the hint is about: x, y, anchorWidth and anchorHeight in DIPs, as ScreenPoints hands them over.
    property var wordAt: null
    /// The word the button explains.
    property string lemma: ""
    /// What the button says. Empty when there is nothing to press.
    property string action: ""

    /// Asks SurfaceHost to place the hint again: when it is shown, and when its text changes its height.
    signal placementRequested()
    /// The reader pressed the button: the word is to be explained.
    signal actionTriggered()

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 2 * padding + 2 * shadowMargin

    /// @brief Show the hint for @p at, the controller's payload already in DIPs.
    function show(at) {
        wordAt = at;
        lemma = at.lemma || "";
        body.text = at.body || "";
        action = at.action || "";
        visible = true;
        placementRequested();
    }

    /// @brief Take the hint down. It does not fade: the reader has already left the word.
    function dismiss() {
        visible = false;
        wordAt = null;
        lemma = "";
        action = "";
    }

    onHeightChanged: {
        if (visible)
            placementRequested();
    }

    /// The pointer is on the hint while it is inside this window, shadow band included. Reported by geometry, not by
    /// hover, because the band takes the word's hover away the moment the window lands on it (SurfaceHost's reviewHold
    /// makes the same argument).
    Timer {
        interval: 80
        repeat: true
        running: hint.visible
        onTriggered: {
            const at = Controller.cursorPos();
            Controller.explanationDuty.holdWordHint(at.x >= hint.x && at.x <= hint.x + hint.width
                                                     && at.y >= hint.y && at.y <= hint.y + hint.height);
        }
    }

    ShadowCard {
        id: panel
        anchors.fill: parent
        color: hint.tokens.glassMode === "" ? hint.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: hint.glass
            mode: hint.tokens.glassMode
            radius: panel.radius
        }

        Column {
            id: column
            x: hint.padding
            y: hint.padding
            width: hint.cardWidth - 2 * hint.padding
            spacing: 10

            Text {
                id: body
                width: parent.width
                color: hint.tokens.text
                font.pixelSize: 13
                wrapMode: Text.WordWrap
            }

            // Invisible rather than zero-height when there is nothing to press: a positioner skips an
            // invisible child whole, spacing included, and a zero-height one still opens a gap.
            Item {
                visible: hint.action !== ""
                width: parent.width
                height: visible ? explainNow.height : 0

                Rectangle {
                    id: explainNow
                    objectName: "explainNowButton"
                    width: actionLabel.width + 26
                    height: actionLabel.height + 12
                    radius: hint.tokens.radiusPill
                    color: hint.tokens.ink
                    scale: explainTap.pressed ? 0.97 : 1.0
                    Behavior on scale {
                        NumberAnimation {
                            duration: hint.tokens.motion.press
                            easing.type: hint.tokens.motion.easing
                        }
                    }

                    // Same pill as the notice's action: a name and a press for the screen reader, no tab stop.
                    Accessible.role: Accessible.Button
                    Accessible.name: actionLabel.text
                    Accessible.onPressAction: explainTap.tapped()

                    Text {
                        id: actionLabel
                        anchors.centerIn: parent
                        text: hint.action
                        color: hint.tokens.on
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        id: explainTap
                        onTapped: hint.actionTriggered()
                    }
                }
            }
        }
    }

    GlassHost {
        window: hint
        sheet: hint.glass
    }
}
