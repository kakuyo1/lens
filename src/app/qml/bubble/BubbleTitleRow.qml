pragma ComponentBehavior: Bound

import QtQuick

/**
 * The word and its pronunciation on one line (UI-SPEC.md 4.3).
 *
 * A component rather than part of `Bubble.qml` because that file sits at the repository's QML size
 * cap (QML.md section 1), not because this is shared: it is one surface's block lifted out whole,
 * the same move `SenseRows` took. The pronunciation is set beside the word, the way a dictionary
 * prints it. The pair is read only -- it is not a control, and nothing about it invites a press --
 * and it is placed by hand rather than by a RowLayout: a layout with no item left to grow does not
 * keep the slack at the end -- measured, a 30 px word and its pronunciation 36 px apart in a 238 px
 * row -- and a gap that wide reads as two unrelated things. The prototype's 7 px is what the pair is
 * set with.
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

    /// True while the pronunciation is still being written out, which is what holds the card's
    /// countdown: a card whose fields are still writing itself has nothing to count down yet.
    readonly property bool revealing: ipaText.revealing

    // A column leaves its children at zero width; the title needs the card's, to run in.
    width: parent ? parent.width : 0
    /// A row that is not drawn holds no height either, since the column it sits in lays out its
    /// children whether they are visible or not.
    height: root.visible ? wordText.implicitHeight : 0

    /// Where a label after the pronunciation starts: the pronunciation's own end when it is
    /// drawn, the word's otherwise.
    readonly property real labelX: ipaText.visible ? ipaText.x + ipaText.implicitWidth : wordText.width

    Text {
        id: wordText
        // What is left once the pronunciation and the part of speech have their room: a word
        // with none left ends in an ellipsis rather than running under its own IPA.
        width: Math.min(implicitWidth, root.width - (ipaText.visible ? ipaText.implicitWidth + 7 : 0)
                                                    - (posText.visible ? posText.implicitWidth + 7 : 0)
                                                    - ((ipaText.visible || posText.visible) ? 0 : root.headerReserve))
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
        x: wordText.width + 7
        y: wordText.baselineOffset - baselineOffset
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

    Text {
        id: posText
        // After the pronunciation, on the same baseline, the way the prototype sets the pair:
        // small mono capitals, the metadata step down from the word like the pronunciation is.
        x: root.labelX + 7
        y: wordText.baselineOffset - baselineOffset
        visible: root.pos !== ""
        wrapMode: Text.NoWrap
        text: root.pos
        color: root.tokens.faint
        font.pixelSize: Math.round(9.5 * root.scale) // the prototype's 9.5 px
        font.family: root.tokens.monoFamily
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 0.8
    }
}
