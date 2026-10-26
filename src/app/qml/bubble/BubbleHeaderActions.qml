import QtQuick
import QtQuick.Shapes

/**
 * The bubble's header controls (UI-SPEC.md 4.3): the close cross and, to its left, the
 * mother-tongue peek globe, on the card the reader cannot read. The globe belongs to the
 * English-in-English read (docs/adr/0112, 0124): it stands only on a word, sentence or entity card
 * with a countdown, at the root, under an English explanation, and holding it down asks the duty for
 * the mother tongue's meaning of that text, which the card draws below the definition while the hold
 * lasts. The permanent language choice is the settings row and nothing else.
 *
 * A component rather than part of Bubble.qml because that file sits at the repository's QML size
 * cap (docs/QML.md section 1), the way BubbleVerdictRow and SenseRows are.
 */
Item {
    id: root

    /// The bubble window this belongs to, whose type, countdown and dismissal it reads.
    required property var bubbleWindow

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    /// How long a hold must last before it counts as asking, and the ring's one full sweep.
    readonly property int holdMs: 500

    readonly property string language: Controller.settings.explanationLang || "en"

    /**
     * Whether the globe is offered. The peek answers a word, sentence or entity the reader is reading
     * in English, on the card itself rather than a step down the chain, with a countdown to hold
     * against. A translation's input already is the mother tongue, a preview belongs to a settings
     * row, and the card the word list raises goes away with the pointer: none of them is it for.
     */
    readonly property bool peekable: !root.bubbleWindow.preview
                                     && ["word", "sentence", "entity"].includes(root.bubbleWindow.type)
                                     && root.bubbleWindow.countsDown
                                     && root.bubbleWindow.atRoot
                                     && root.language === "en"

    /// Whether the pointer is holding the globe down.
    property bool holding: false
    /// Whether a screen-reader press latched the peek on: the press has no release to end it
    /// with, so the line it asked for stays until the card does (docs/adr/0107).
    property bool latched: false

    /// Whether the mother-tongue line is on the card: a hold or a latched press, over a line the
    /// duty has answered for the standing word.
    readonly property bool peekVisible: (root.holding || root.latched)
                                        && root.bubbleWindow.peekLine !== ""

    onPeekableChanged: {
        if (peekable)
            return;
        endHold();
        latched = false;
    }

    Connections {
        target: root.bubbleWindow
        function onVisibleChanged() {
            if (!root.bubbleWindow.visible) {
                root.endHold();
                root.latched = false;
            }
        }
    }

    // The way out before the countdown ends. It takes the same two steps the countdown takes, so the
    // controller hears about the dismissal the same way either way. The bubble's window never takes
    // focus (UI-SPEC.md 4.3), so its controls carry names for the screen reader's review pass but
    // no tab stops or rings: there is no focus for either to mark.
    IconButton {
        id: closeButton
        visible: !root.bubbleWindow.preview
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 15
        source: "qrc:/icons/ui-close.svg"
        color: root.tokens.faint
        accessibleName: qsTr("Close")
        onTapped: {
            root.bubbleWindow.dismiss();
            Controller.explanationDuty.dismissBubble();
        }
    }

    Item {
        id: peekButton
        visible: root.peekable
        anchors.right: closeButton.left
        anchors.rightMargin: 6
        anchors.verticalCenter: closeButton.verticalCenter
        width: 22
        height: 22

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Mother tongue translation")
        Accessible.onPressAction: {
            root.bubbleWindow.readerActed();
            root.latched = true;
            Controller.explanationDuty.requestPeek();
        }

        Rectangle {
            anchors.fill: parent
            radius: root.tokens.radiusPill
            color: hover.hovered ? root.tokens.panel2 : "transparent"
            scale: press.pressed ? 0.97 : 1.0
            Behavior on scale {
                NumberAnimation {
                    duration: root.tokens.motion.press
                    easing.type: root.tokens.motion.easing
                }
            }
        }

        // The hold's progress, one full clockwise turn from the top: the sweep and the hold timer
        // run the same length, so the turn completing is the request leaving.
        Shape {
            anchors.fill: parent
            visible: root.holding && !root.tokens.motion.reduced

            ShapePath {
                strokeColor: root.tokens.ok
                strokeWidth: 1.5
                fillColor: "transparent"

                PathAngleArc {
                    id: ringArc
                    centerX: 11
                    centerY: 11
                    radiusX: 9
                    radiusY: 9
                    startAngle: -90
                    sweepAngle: 0
                }
            }
        }

        Icon {
            anchors.centerIn: parent
            width: 13
            height: 13
            source: "qrc:/icons/ui-language.svg"
            color: hover.hovered ? root.tokens.text : root.tokens.faint
        }

        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            id: press
            objectName: "peekTap"
            onPressedChanged: {
                if (pressed) {
                    root.holding = true;
                    holdTimer.restart();
                    if (!root.tokens.motion.reduced)
                        ringSweep.restart();
                } else {
                    root.endHold();
                }
            }
            onCanceled: root.endHold()
        }

        Timer {
            id: holdTimer
            interval: root.holdMs
            onTriggered: {
                root.bubbleWindow.readerActed();
                Controller.explanationDuty.requestPeek();
            }
        }
    }

    NumberAnimation {
        id: ringSweep
        target: ringArc
        property: "sweepAngle"
        from: 0
        to: 360
        duration: root.holdMs
        easing.type: Easing.Linear
    }

    /// @brief End a hold: the line the card is showing goes with it, and nothing was asked
    ///        unless the hold lasted the half second.
    function endHold() {
        holding = false;
        holdTimer.stop();
        ringSweep.stop();
        ringArc.sweepAngle = 0;
    }
}
