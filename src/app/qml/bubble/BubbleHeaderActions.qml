import QtQuick

/**
 * The bubble's header controls (UI.md 4.3): the close cross and, to its left, the explanation
 * language shortcut.
 *
 * The shortcut lives where the reader notices they need it -- on the card they cannot read. It
 * unfolds the catalog's languages in a strip under the header; a pick changes the explanation
 * language and asks for the standing card again (Controller.reexplainIn), from the cache when the
 * new language has one. A translation has no second language to ask for, and a card the word list
 * owns goes away with the pointer, so neither offers the shortcut.
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

    readonly property string language: Controller.settings.explanationLang || "en"
    readonly property var languages: Controller.settings.languages || []
    readonly property var labels: {
        const result = [];
        for (let i = 0; i < languages.length; ++i)
            result.push(languages[i].label);
        return result;
    }
    readonly property int currentIndex: {
        for (let i = 0; i < languages.length; ++i)
            if (languages[i].value === language)
                return i;
        return 0;
    }

    /**
     * Whether the shortcut is offered. The translation channel answers in English whatever the
     * setting says, and a card without a countdown belongs to the pointer's row rather than to the
     * reader: both are left as they are.
     */
    readonly property bool switchable: !root.bubbleWindow.preview
                                       && root.bubbleWindow.type !== "translation"
                                       && root.bubbleWindow.countsDown

    /// Whether the language strip is out.
    property bool open: false
    onSwitchableChanged: if (!switchable) open = false

    Connections {
        target: root.bubbleWindow
        function onVisibleChanged() {
            if (!root.bubbleWindow.visible)
                root.open = false;
        }
    }

    // A press anywhere else on the card puts the strip away, the way the open list of a dropdown
    // answers a press outside it. It sits under the strip and under the two header controls.
    Item {
        anchors.fill: parent
        visible: root.open
        TapHandler { onTapped: root.open = false }
    }

    // The language strip, on the card rather than in a window of its own: the four labels fit one
    // row, so the whole picker is here and a separate surface would only have to be placed and
    // dismissed again.
    Rectangle {
        id: strip
        visible: root.open && root.languages.length > 0
        x: 12
        y: 36
        width: parent.width - 24
        height: 36
        radius: root.tokens.radiusGroup
        color: root.tokens.popup
        border.width: 1
        border.color: root.tokens.line
        opacity: root.open ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: root.tokens.motion.pop
                easing.type: root.tokens.motion.easing
            }
        }
        transform: Translate { y: (1 - strip.opacity) * 8 }

        Segment {
            anchors.fill: parent
            anchors.margins: 3
            labels: root.labels
            currentIndex: root.currentIndex
            onPicked: (index) => {
                const picked = root.languages[index].value;
                root.open = false;
                if (picked !== root.language) {
                    root.bubbleWindow.beginLanguageReplay();
                    Controller.reexplainIn(picked);
                }
            }
        }
    }

    // The way out for a reader who does not want to wait the countdown out. It takes the same two
    // steps the countdown takes on its own, so the controller hears about the dismissal the same
    // way either way.
    IconButton {
        id: closeButton
        visible: !root.bubbleWindow.preview
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.top: parent.top
        anchors.topMargin: 15
        source: "qrc:/icons/ui-close.svg"
        color: root.tokens.faint
        onTapped: {
            root.open = false;
            root.bubbleWindow.dismiss();
            Controller.explanationDuty.dismissBubble();
        }
    }

    Item {
        id: languageButton
        visible: root.switchable
        anchors.right: closeButton.left
        anchors.rightMargin: 6
        anchors.verticalCenter: closeButton.verticalCenter
        width: glyph.width + code.implicitWidth + 14
        height: 22

        Rectangle {
            anchors.fill: parent
            radius: root.tokens.radiusPill
            color: root.open || hover.hovered ? root.tokens.panel2 : "transparent"
            scale: press.pressed ? 0.97 : 1.0
            Behavior on scale {
                NumberAnimation {
                    duration: root.tokens.motion.press
                    easing.type: root.tokens.motion.easing
                }
            }
        }

        Row {
            anchors.centerIn: parent
            spacing: 3

            Icon {
                id: glyph
                width: 13
                height: 13
                source: "qrc:/icons/ui-language.svg"
                color: root.open || hover.hovered ? root.tokens.text : root.tokens.faint
            }

            Text {
                id: code
                text: root.language.toUpperCase()
                color: root.open || hover.hovered ? root.tokens.text : root.tokens.faint
                font.pixelSize: 10
                font.weight: Font.Bold
            }
        }

        HoverHandler {
            id: hover
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            id: press
            onTapped: root.open = !root.open
        }
    }
}
