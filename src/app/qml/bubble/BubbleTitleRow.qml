pragma ComponentBehavior: Bound

import QtQuick

/**
 * The word and its pronunciation on one line (UI-SPEC.md 4.3).
 *
 * A component rather than part of `Bubble.qml` because that file sits at the repository's QML size
 * cap (QML.md section 1), not because this is shared: it is one surface's block lifted out whole,
 * the same move `SenseRows` took. The pronunciation is set beside the word, the way a dictionary
 * prints it. It is placed by hand rather than by a RowLayout: a layout with no item left to grow
 * does not keep the slack at the end -- measured, a 30 px word and its pronunciation 36 px apart in
 * a 238 px row -- and a gap that wide reads as two unrelated things. The prototype's 7 px is what
 * the pair is set with. A word too long to leave room for the pair beside it takes the line whole
 * and the metadata goes under it, because an ellipsis hides the one thing the card was asked for.
 *
 * A word carries a speaker after the pronunciation, and the pronunciation itself is a press target:
 * either one reads the headword, once (docs/adr/0134). The glyph is the whole affordance -- there is
 * no tooltip, no ring and no state text -- and its colour is the only thing a press or a playing
 * recording changes. Nothing here moves when the sound starts: the card is anchored to the reader's
 * word and a card that grew a pixel would pull itself off it.
 *
 * A sentence or translation bubble draws no title, and the caller says so by hiding this row: the
 * selected sentence is usually long and overflows the card while the reader still has it
 * highlighted on screen, and the Chinese a translation came from is long while the reader asked for
 * the English of it rather than for the original back. Word and entity titles are short and stay.
 */
Item {
    id: root

    required property string title
    required property string ipa
    /// The most common sense's part of speech, one lowercase English word, drawn in small
    /// capitals after the pronunciation (UI-SPEC.md 4.3). Empty whenever the answer had none or
    /// the display switch is off.
    property string pos: ""
    /// The family the word is drawn in (UI-SPEC.md 4.4); the caller's own face is an empty name.
    property string family: ""
    /// The multiple the pair is drawn at, from the reader's font-size setting (UI-SPEC.md 4.4): the
    /// card decides the scale and this block follows it, as every block on it does.
    property real scale: 1
    required property bool animated
    required property int characterMs
    required property var tokens
    /// The width the header controls take at the card's right edge (BubbleHeaderActions.qml). A bare
    /// title, with no pronunciation or part of speech beside it, gives that room up; a line that has
    /// them keeps its width, as it always has.
    property real headerReserve: 0
    /// Whether this card's word may be read aloud. The word card and the chain's own entries say
    /// yes; a sentence, a translation, a breakdown, an entity and the settings preview say no, and
    /// a card that is not drawn says nothing at all. The speaker is tied to the pronunciation: the
    /// IPA switch off means no glyph either (UI-SPEC.md 4.3).
    property bool speakable: false
    /// Whether this word is the one coming out of the speaker now, which is the one thing that
    /// turns the glyph from faint to the answer's own colour.
    property bool speaking: false

    /// A press on the glyph or on the pronunciation: read this headword once.
    signal speakRequested()

    /// True while the pronunciation is still being written out, which is what holds the card's
    /// countdown: a card whose fields are still writing itself has nothing to count down yet.
    readonly property bool revealing: ipaText.revealing

    /// Whether the speaker is drawn at all: it follows the pronunciation it stands beside.
    readonly property bool speakerVisible: root.speakable && root.ipa !== ""

    // A column leaves its children at zero width; the title needs the card's, to run in.
    width: parent ? parent.width : 0
    /// What the pronunciation, the speaker and the part of speech would take beside the word.
    readonly property real beside: (ipaText.visible ? ipaText.implicitWidth + 7 : 0)
                                 + (root.speakerVisible ? speaker.width + 5 : 0)
                                 + (posText.visible ? posText.implicitWidth + 7 : 0)
    /// Whether the metadata is set on a line under the word. The word gives up its room to the pair
    /// on one line, and a word that only fits that way ends in an ellipsis while the reader is still
    /// looking for it: below the line it has the card's width, and the word reads whole.
    readonly property bool stacked: beside > 0 && wordText.implicitWidth > root.width - beside
    /// Where the metadata line ends, whichever of its two items reaches lower.
    readonly property real metadataBottom: Math.max(ipaText.visible ? ipaText.y + ipaText.implicitHeight : 0,
                                                    posText.visible ? posText.y + posText.implicitHeight : 0)
    /// A row that is not drawn holds no height either, since the column it sits in lays out its
    /// children whether they are visible or not.
    height: root.visible ? (stacked ? metadataBottom : wordText.implicitHeight) : 0

    /// Where a label after the pronunciation starts: past the speaker when it is drawn, the
    /// pronunciation's own end when only that is, the word's otherwise. A stacked line whose label
    /// has nothing before it starts on the left edge.
    readonly property real labelX: speakerVisible ? speaker.x + speaker.width : (ipaText.visible ? ipaText.x + ipaText.implicitWidth : (stacked ? 0 : wordText.width))
    /// The gap a label keeps from whatever stands before it, and none when it opens the line.
    readonly property real labelGap: stacked && !speakerVisible && !ipaText.visible ? 0 : 7

    Text {
        id: wordText
        // What is left once the pronunciation, the speaker and the part of speech have their room --
        // and when that room would leave the word nothing but an ellipsis, the word takes the line
        // and the pair goes below it. Only a word too long for the whole row still ends in one.
        width: Math.min(implicitWidth,
                        root.width - (root.stacked ? root.headerReserve
                                                   : root.beside + ((ipaText.visible || posText.visible) ? 0 : root.headerReserve)))
        topPadding: 8
        text: root.title
        color: root.tokens.text
        font.pixelSize: Math.round(20 * root.scale)
        font.weight: Font.Bold
        font.family: root.family
        elide: Text.ElideRight
    }

    RevealText {
        id: ipaText
        // On the word's baseline: each item's own baseline offset is the distance from its top to
        // that baseline, padding included.
        x: root.stacked ? 0 : wordText.width + 7
        y: root.stacked ? wordText.height : wordText.baselineOffset - baselineOffset
        visible: root.ipa !== ""
        animated: root.animated
        characterMs: root.characterMs
        // Measured, not wrapped, and whole from the first frame: the title gives up its room to
        // the full pronunciation, not to what has been written so far.
        wrapMode: Text.NoWrap
        text: root.ipa
        color: root.tokens.faint
        family: root.family
        pixelSize: Math.round(13 * root.scale) // the prototype's 12.5 px, as the definition line rounds it
    }

    // The pronunciation is a press target in its own right (UI-SPEC.md 4.3): a reader who has just
    // read the IPA is already looking at it, and asking them to find a 12 px glyph beside it is a
    // second aim for one action. It keeps the faint colour it is drawn in -- turning it the body
    // colour on hover would read as a link, which is not what it is -- and says so with the cursor.
    MouseArea {
        anchors.fill: ipaText
        enabled: root.speakerVisible
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.speakRequested()
    }

    Item {
        id: speaker
        // Directly after the pronunciation, centred on it: the glyph is 12 px and the pronunciation
        // is 13, so the pair sits on one line rather than the speaker floating beside it.
        x: ipaText.x + ipaText.implicitWidth + 5
        y: ipaText.y + (ipaText.implicitHeight - height) / 2
        width: Math.round(12 * root.scale)
        height: width
        visible: root.speakerVisible
        // The press is the only thing that scales: a glyph that shrank on hover would read as the
        // whole row moving, and the row is the card's anchor line.
        scale: press.pressed ? 0.97 : 1
        transformOrigin: Item.Center

        Icon {
            anchors.fill: parent
            source: "qrc:/icons/ui-speaker.svg"
            // Playing is the answer's own colour; at rest the glyph is as quiet as the
            // pronunciation beside it, and the pointer is what says it can be pressed.
            color: root.speaking ? root.tokens.ok : (press.containsMouse ? root.tokens.text : root.tokens.faint)
        }

        MouseArea {
            id: press
            // Two pixels of slack around a 12 px glyph: the aim a reader gets is the 16 px box a
            // pointer can actually find, while the row still measures the glyph.
            anchors.fill: parent
            anchors.margins: -2
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.speakRequested()
        }
    }

    Text {
        id: posText
        // After the pronunciation, on the same baseline, the way the prototype sets the pair:
        // small mono capitals, the metadata step down from the word like the pronunciation is.
        x: root.labelX + root.labelGap
        y: root.stacked ? ipaText.y + ipaText.baselineOffset - baselineOffset : wordText.baselineOffset - baselineOffset
        visible: root.pos !== ""
        wrapMode: Text.NoWrap
        text: root.pos
        color: root.tokens.faint
        font.pixelSize: Math.round(9.5 * root.scale) // the prototype's 9.5 px
        font.family: root.tokens.monoFamily
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }

    // One name and one action for the pronunciation and the glyph together: a screen reader has no
    // pointer to put on a 12 px speaker, and the two are one control to the reader (UI-SPEC.md 4.3).
    // A row with no speaker is not a button, so nothing announces an action that is not there.
    Accessible.role: root.speakerVisible ? Accessible.Button : Accessible.StaticText
    Accessible.name: root.speakable ? qsTr("Pronounce %1").arg(root.title) : ""
    Accessible.ignored: !root.speakerVisible
    Accessible.onPressAction: root.speakRequested()
}
