pragma ComponentBehavior: Bound

import QtQuick

/**
 * The senses of a word, one two-line row each: the English line and the reader's own language
 * under it, with a hairline between rows (UI-SPEC.md 4.3).
 *
 * A sense the model judged a photograph could make clearer also carries a `Pictures` entry under its
 * lines, and the strip that entry opens (IMAGES.md 2.1). One strip is open at a time, and the row
 * itself is the sense selector, so there is no separate "not this sense?" control.
 *
 * A component rather than part of `Bubble.qml` because that file sits at the repository's QML size
 * cap (docs/QML.md section 1), not because this is shared: it is one surface's block lifted out
 * whole. The root is a Repeater so the rows are laid out in the column that holds this.
 *
 * `revealing` is the block's own aggregate, so the card can hold its countdown while any line of
 * any row is still being written out.
 */
Repeater {
    id: root

    /// The senses to draw, in order; each is an object with an `en` and the reader's own `text`.
    required property var senses
    /// Whether the lines are written out a character at a time (RevealText), or arrive whole.
    required property bool animated
    /// The pace the lines are written at, in real milliseconds (RevealText).
    property int characterMs: 35
    required property color englishColor
    required property color translationColor
    required property color separatorColor
    /// The family the English line is drawn in (UI-SPEC.md 4.4); the reader's own language keeps the
    /// application's face, so the translation line is not given it.
    property string family: ""
    /// The multiple the lines are drawn at, from the reader's font-size setting (UI-SPEC.md 4.4). The
    /// card decides the scale and the block follows it: the senses are part of one answer, not a
    /// surface with a size of its own.
    property real scale: 1
    /// The tint of a tappable word's line, and the tint a hovered word takes (ChainText).
    property color markColor: "gray"
    property color hoverColor: "transparent"

    /// The table the picture entry takes its colours from: the one of the window it sits in.
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    /// Which sense's pictures are open, or -1 for none. One at a time: opening another's entry closes
    /// the first, and the row itself is the sense selector -- there is no "not this sense?" control
    /// (IMAGES.md 2.1).
    property int expandedIndex: -1

    /// The reader tapped a word of a sense's English line: the card's definition chain takes it.
    signal wordTapped(string lemma)

    /// @brief Take the picture strip down, if one is up, and stop drawing whatever was arriving.
    function collapse() {
        expandedIndex = -1;
        Controller.pictureDuty.clear();
    }

    /// True while any line of any row is still being written out.
    readonly property bool revealing: {
        // qmllint disable missing-property
        // A row's own `revealing`; the repeater hands its delegates back as QQuickItem.
        for (let i = 0; i < root.count; ++i) {
            const row = root.itemAt(i);
            if (row && row.revealing)
                return true;
        }
        // qmllint enable missing-property
        return false;
    }

    model: root.senses

    // A new answer is a new set of senses: whatever strip was open belonged to the answer before it,
    // so it comes down with the answer (IMAGES.md 2.2).
    onSensesChanged: root.collapse()

    delegate: Column {
        id: senseRow
        required property int index
        required property var modelData
        // The column this block sits in lays the rows out, and each row is as wide as it is.
        width: parent ? parent.width : 0
        spacing: 0

        /// What this row contributes to the block's reveal.
        readonly property bool revealing: senseEnglishText.revealing
                                           || senseTranslationText.revealing

        Rectangle {
            objectName: "senseSeparator"
            visible: senseRow.index > 0
            width: parent.width
            height: 1
            color: root.separatorColor
        }

        ChainText {
            id: senseEnglishText
            objectName: "senseEnglish"
            visible: text !== ""
            animated: root.animated
            characterMs: root.characterMs
            width: parent.width
            topPadding: 6
            bottomPadding: senseRow.modelData.text === "" ? 6 : 0
            text: senseRow.modelData.en || ""
            color: root.englishColor
            family: root.family
            pixelSize: Math.round(13 * root.scale)
            lineHeight: 1.55
            spans: senseRow.modelData.spans || []
            markColor: root.markColor
            hoverColor: root.hoverColor
            onWordTapped: lemma => root.wordTapped(lemma)
        }

        RevealText {
            id: senseTranslationText
            objectName: "senseTranslation"
            visible: text !== ""
            animated: root.animated
            characterMs: root.characterMs
            width: parent.width
            topPadding: 5
            bottomPadding: 6
            text: senseRow.modelData.text || ""
            color: root.translationColor
            pixelSize: Math.round(12 * root.scale)
            lineHeight: 1.5
        }

        // The picture entry: only for a sense the model judged a photograph could make clearer
        // (IMAGES.md 2.1). It is exactly as tall as its pill, so the gaps above and below come from
        // the rows around it rather than from dead space of its own.
        Item {
            id: pictureEntry
            objectName: "sensePicturesEntry"
            visible: senseRow.modelData.imageable === true
            width: parent.width
            height: 22

            /// Whether this row is the one whose strip is open.
            readonly property bool open: root.expandedIndex === senseRow.index

            /// @brief Open this sense's pictures, or close them when they are the ones open.
            function toggle() {
                if (open) {
                    root.collapse();
                    return;
                }
                // One strip at a time: the row that owns it moves first, then the duty is asked, so
                // the strip that appears is this sense's and no other's (IMAGES.md 2.1).
                root.expandedIndex = senseRow.index;
                Controller.pictureDuty.request(senseRow.modelData.imageQuery || "");
            }

            Item {
                id: entry
                // The pill hangs its own left padding off the card's content edge, which puts the
                // glyph -- the only part drawn until the pointer arrives -- on the same vertical
                // baseline as the sense above it, and the sense above *that* (UI-SPEC.md 4.3). Its right
                // padding hangs off the same way, so the label stays symmetric inside it.
                x: -entry.sidePadding
                y: 0
                width: entry.sidePadding * 2 + entryGlyph.width + entry.spacing + entryLabel.implicitWidth
                height: 22
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                // The label carries the state, so a screen reader hears "Hide pictures" once it is
                // open rather than a picture button with no state at all (IMAGES.md 2.4).
                Accessible.name: entryLabel.text
                Accessible.onPressAction: pictureEntry.toggle()

                /// The pill's own padding and the gap between its glyph and its label.
                readonly property int sidePadding: 7
                readonly property int spacing: 4

                Rectangle {
                    id: entryPill
                    objectName: "picturesEntryPill"
                    anchors.fill: parent
                    radius: root.tokens.radiusPill
                    // Open, hovered or keyboard-focused: a button needs a focus state (DESIGN.md §6).
                    color: pictureEntry.open || entryHover.hovered || entry.activeFocus ? root.tokens.panel2 : "transparent"
                    scale: entryPress.pressed ? 0.97 : 1.0
                    Behavior on scale {
                        NumberAnimation {
                            duration: root.tokens.motion.press
                            easing.type: root.tokens.motion.easing
                        }
                    }
                }

                // The glyph and the label are each centered on the pill's own middle, rather than
                // stacked in a Row where both would sit at the top. 13 px inside 22 px cannot land on
                // a whole pixel -- the half-pixel nudge is what puts it on the grid -- and the glyph's
                // own ink sits half a pixel above its box, so the nudge cancels that too.
                Icon {
                    id: entryGlyph
                    objectName: "sensePicturesGlyph"
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: 0.5
                    x: entry.sidePadding
                    width: 13
                    height: 13
                    source: "qrc:/icons/ui-pictures.svg"
                    color: pictureEntry.open || entryHover.hovered ? root.tokens.text : root.tokens.faint
                }

                Text {
                    id: entryLabel
                    anchors.left: entryGlyph.right
                    anchors.leftMargin: entry.spacing
                    // The label is the pill's full height with its line centred inside it, so the
                    // text's own line box does not decide where the row sits; the half-pixel nudge up
                    // meets the glyph's ink, which sits above its box centre while a
                    // descender-shaped line box sits below its own.
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.verticalCenterOffset: -0.5
                    height: entry.height
                    verticalAlignment: Text.AlignVCenter
                    text: pictureEntry.open ? qsTr("Hide pictures") : qsTr("Pictures")
                    color: pictureEntry.open || entryHover.hovered ? root.tokens.text : root.tokens.faint
                    font.pixelSize: 11
                }

                HoverHandler {
                    id: entryHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    id: entryPress
                    onTapped: pictureEntry.toggle()
                }
                // The entry is a button: Enter and Space work it, and the focus stays on it rather
                // than moving into the pictures (IMAGES.md 2.4).
                Keys.onSpacePressed: pictureEntry.toggle()
                Keys.onReturnPressed: pictureEntry.toggle()
            }
        }

        PictureStrip {
            objectName: "sensePictures"
            open: root.expandedIndex === senseRow.index
            query: senseRow.modelData.imageQuery || ""
            scale: root.scale
        }
    }
}
