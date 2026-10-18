pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects

/**
 * One field of an answer, written out one character at a time (UI.md 4.3).
 *
 * The answer streams in and the model finishes it in a jump or two, so a field used to arrive
 * whole. This paces the field itself: one character every `characterMs`, whatever the transport
 * did -- an answer that arrived in one lump is written out at the same speed. The characters are
 * the model's; only the pace is the view's.
 *
 * The target only grows. A chunk arriving mid-reveal extends `text`, and the cursor carries on from
 * where it stood: starting the field over is the stutter this exists to prevent. The layout grows
 * with it: an extending chunk lays only the token it touched again, so the characters already on
 * the card are neither measured nor re-created by the next one.
 *
 * Each character appears soft and sharpens over `blurMs`, which is a fixed multiple of the pace
 * rather than a fixed number of milliseconds: the reader's speed setting moves `characterMs`, and
 * the blur follows it so the tail stays a handful of characters whatever they pick. Only the
 * characters still sharpening are drawn with a blur; everything settled is plain text. That is what
 * keeps the shader count at the tail of the text rather than at the length of the field.
 *
 * A character cannot be blurred inside a Text, so every one of them is an item, laid out here. A
 * word is measured whole and the breaks fall between words, so a line never breaks inside one; the
 * price is that a glyph pair is not kerned. `measure` lays the same text out as the one Text the
 * field used to be, and its size is what the field reserves -- the card is its final size as soon
 * as the answer arrives, and the characters fill it in rather than pushing it down.
 */
Item {
    id: root

    /// The whole field, as far as it has arrived. A text that does not continue the last one is a
    /// different answer and reveals from the start; anything else extends the reveal running.
    property string text: ""
    /// The field's type, from UI.md 4.3: the definitions 13 px, the translations 12 px.
    property int pixelSize: 13
    property int weight: Font.Normal
    property color color: "black"
    /// The family the field is drawn in, when it names one (UI.md 4.4). Empty is the application's
    /// own face, and it is left unsaid rather than written as an empty family: a Text told
    /// `font.family: ""` does not fall back to the application's font, it falls back to whatever
    /// the font database hands an unmatched request -- measured, Cascadia Code on this machine.
    /// The measurement follows the family: the size the field reserves and every letter's place
    /// are read off the font it ends up drawing with.
    property string family: ""
    /// The line-height multiple the field is set with; Text's own default is one line.
    property real lineHeight: 1.0
    /// Whether the field wraps to the width it is given. A pronunciation beside the word does not:
    /// it is measured, not wrapped.
    property int wrapMode: Text.Wrap
    property int textFormat: Text.PlainText
    property real topPadding: 0
    property real bottomPadding: 0
    /// Whether the reveal runs. A card raised whole out of the cache, and every card under reduced
    /// motion, arrives complete (UI.md's motion table).
    property bool animated: true

    /// How long a character waits for the next one, in real milliseconds. The card sets it from the
    /// reader's speed setting (UI.md 4.3); 35 is the pace the reveal shipped with.
    property int characterMs: 35
    /// How much longer a character stays soft than it takes to appear. A ratio rather than a fixed
    /// count so the shader count below does not grow as the reader slows the pace down. This is the
    /// 180 ms blur the reveal shipped with, taken at the 35 ms pace.
    readonly property real blurRatio: 180 / 35
    /// How long a character stays soft after it appears, at the pace in force.
    readonly property real blurMs: characterMs * blurRatio
    /// How many characters' worth of time that is -- the number of shaders the tail costs. Read off
    /// the ratio rather than the milliseconds so it is one answer for every pace.
    readonly property int softSpan: Math.max(1, Math.ceil(blurRatio))

    /// Characters written out so far, which is what watches the pace.
    readonly property int revealed: animated ? Math.min(Math.floor(cursor), text.length) : text.length
    /// True while the field is still moving: a character still to be written, or a character
    /// written and still sharpening. The card's countdown waits on it (Bubble.qml), and so does
    /// anything that has to read the field as the reader finally sees it. It is the reveal timer's
    /// own condition, so there is one answer to "is this field done".
    readonly property bool revealing: animated && text.length > 0 && cursor < text.length + softSpan

    implicitWidth: measure.implicitWidth
    implicitHeight: measure.implicitHeight
    readonly property real baselineOffset: measure.baselineOffset

    /// Where the reveal has reached, in characters, as a real number: the fraction is what lets the
    /// tail sharpen continuously instead of a character at a time. It runs `softSpan` past the end
    /// of the text so that the last characters clear as well.
    property real cursor: 0
    /// The wall clock the reveal started at. The pace is read off it rather than counted, so a late
    /// frame cannot make the reveal run slow.
    property real startedMs: 0
    /// The text last handed over, for telling an extension of it from a different answer.
    property string previous: ""

    /// Where every character of the text sits, by text index, for the blur tail below. Built as the
    /// text arrives rather than bound, because building it measures as it goes. An extending chunk
    /// lays only the token it touched, so the characters already drawn are left alone.
    property var positions: []
    /// Bumped whenever a layout changes in place, so the tail's binding re-reads positions.
    property int layoutRevision: 0
    /// The start of the last token laid out, and the pen it started at: where an extending chunk
    /// drops that token's pieces and lays them again over the longer text.
    property int tailStart: 0
    property real tailX: 0
    property real tailY: 0
    /// How many pieces that last token put at the end of the model.
    property int tailPieces: 0
    /// The advance of each run of characters measured so far, dropped when the font changes.
    property var advances: ({})

    /// @brief Put the whole field up at once: what a card raised from the cache gets, and every
    ///        card under reduced motion.
    function settle() {
        cursor = text.length + softSpan;
    }

    /// @return The advance of @p run, measuring it the first time it is asked for. The words and
    ///         the letters of a definition come back many times, and measuring is what a rebuild
    ///         costs.
    function measureWidth(run) {
        let width = root.advances[run];
        if (width === undefined) {
            metrics.text = run;
            width = metrics.advanceWidth;
            root.advances[run] = width;
        }
        return width;
    }

    /// @return The piece at @p index, or null where the text has none: a space, or off either end.
    function pieceAt(index) {
        return index >= 0 && index < positions.length ? (positions[index] || null) : null;
    }

    /// @return How soft the character at @p index still is: one when it has just appeared, zero
    ///         once it has had `blurMs`. The cursor runs in real time, so this keeps falling after
    ///         the last character and the tail clears even though nothing more arrives.
    function softness(index) {
        return Math.max(0, Math.min(1, 1 - (cursor - index - 1) / softSpan));
    }

    /// Every character, in reading order, as the model the settled Text items are drawn from. A
    /// ListModel and not an array: a chunk appends rows, and only those rows get delegates, where
    /// reassigning an array would reset the model and re-create every character on the card.
    ListModel {
        id: pieces
    }

    /// Lay the whole text out, breaking lines between words. A width, wrap or font change goes
    /// this way; an extending chunk goes through extend() instead.
    function rebuild() {
        pieces.clear();
        positions = [];
        tailStart = 0;
        tailX = 0;
        tailY = 0;
        tailPieces = 0;
        layFrom(0, 0, 0);
        layoutRevision++;
    }

    /// Whether a character belongs to a script that breaks between characters rather than at
    /// spaces. Latin words are measured whole; Chinese and Japanese runs have no spaces to break
    /// at, and laying them as one word is what put a Chinese definition in a single line past the
    /// card's edge.
    readonly property var characterBreaks: /[\u2E80-\u2FFF\u3000-\u303F\u3040-\u30FF\u3400-\u4DBF\u4E00-\u9FFF\uAC00-\uD7AF\uF900-\uFAFF\uFF00-\uFFEF]/

    /// Lay every token from @p from to the end of the text, the first one at pen (@p x, @p y), and
    /// leave tailStart/tailX/tailY on the last token for the next chunk.
    function layFrom(from, x, y) {
        const source = text;
        const step = lineBox.implicitHeight;
        const limit = wrapMode === Text.NoWrap ? Number.POSITIVE_INFINITY : Math.max(1, width);
        let index = from;
        while (index < source.length) {
            const start = index;
            const blank = source.charAt(index) === " " || source.charAt(index) === "\n";
            while (index < source.length
                   && (source.charAt(index) === " " || source.charAt(index) === "\n") === blank)
                ++index;
            const token = source.substring(start, index);

            // Remembered before the token is laid: an appended chunk drops this token and lays it
            // again from exactly here, which is why the rest of the field never moves.
            tailStart = start;
            tailX = x;
            tailY = y;
            tailPieces = 0;

            if (blank) {
                // An explicit break moves the line; the spaces between words only move along it.
                if (token.indexOf("\n") >= 0) {
                    y += step;
                    x = 0;
                } else {
                    x += measureWidth(token);
                }
                continue;
            }

            // A run with no spaces in it has no word boundary to break at. Chinese and Japanese
            // can break between any two characters, so those runs are laid a character at a time
            // and wrap before whichever character would cross the field.
            if (characterBreaks.test(token)) {
                for (let i = 0; i < token.length; ++i) {
                    const character = token.charAt(i);
                    const width = measureWidth(character);
                    if (x > 0 && x + width > limit) {
                        x = 0;
                        y += step;
                    }
                    const piece = { index: start + i, character: character, x: x, y: y };
                    pieces.append({ charIndex: piece.index, glyph: piece.character,
                                    px: piece.x, py: piece.y });
                    positions[piece.index] = piece;
                    ++tailPieces;
                    x += width;
                }
                continue;
            }
            // A word is measured whole, so the break below always falls between two of them.
            const tokenWidth = measureWidth(token);
            if (x > 0 && x + tokenWidth > limit) {
                x = 0;
                y += step;
            }
            // Each letter sits where the whole run up to it ends rather than where the letters
            // before it add up to: an advance is whole pixels, and fourteen of them rounded
            // separately put the word further along than the word itself is wide.
            for (let i = 0; i < token.length; ++i) {
                const piece = { index: start + i, character: token.charAt(i),
                                x: x + measureWidth(token.substring(0, i)), y: y };
                pieces.append({ charIndex: piece.index, glyph: piece.character,
                                px: piece.x, py: piece.y });
                positions[piece.index] = piece;
                ++tailPieces;
            }
            // The word's own width rather than the sum of its letters: a pair that is kerned is
            // narrower than the two apart, and the next word starts where this one ends.
            x += tokenWidth;
        }
    }

    /// Lay what an extending chunk added: drop the last token and lay it again from where it
    /// started. Only that token can have changed -- an append either grows it or follows it -- and
    /// a word grown past the line's width wraps here from its own start, as a full rebuild would.
    function extend() {
        if (tailPieces > 0)
            pieces.remove(pieces.count - tailPieces, tailPieces);
        if (positions.length > tailStart)
            positions.length = tailStart;
        tailPieces = 0;
        layFrom(tailStart, tailX, tailY);
        layoutRevision++;
    }

    onTextChanged: {
        const continued = previous.length > 0 && text.startsWith(previous);
        previous = text;
        if (continued) {
            extend();
        } else {
            startedMs = Date.now();
            cursor = 0;
            rebuild();
        }
        if (!animated)
            settle();
    }

    onWidthChanged: rebuild()
    onWrapModeChanged: rebuild()
    Component.onCompleted: rebuild()

    onAnimatedChanged: {
        if (animated) {
            startedMs = Date.now();
            cursor = 0;
        } else {
            settle();
        }
    }

    Timer {
        // Finer than the pace, so the tail sharpens smoothly rather than a character at a time. The
        // reveal reads the wall clock below, so a late tick cannot make it run slow, and it keeps
        // ticking past the end of the text so that the last characters clear too.
        interval: 16
        repeat: true
        running: root.animated && root.text.length > 0 && root.cursor < root.text.length + root.softSpan
        onTriggered: root.cursor = (Date.now() - root.startedMs) / root.characterMs
    }

    /// The whole text as the one Text the field used to be: what the field reserves its height by,
    /// and what its own baseline is read off. Never drawn -- the characters below are what the
    /// reader sees.
    Text {
        id: measure
        visible: false
        text: root.text
        textFormat: root.textFormat
        // A wrapping field is given its column's width; a single-line one is measured against
        // nothing, so its own width is what the field takes.
        width: root.wrapMode === Text.NoWrap ? 0 : root.width
        topPadding: root.topPadding
        bottomPadding: root.bottomPadding
        color: root.color
        font.pixelSize: root.pixelSize
        font.weight: root.weight
        lineHeight: root.lineHeight
        wrapMode: root.wrapMode
    }

    /// The family, written only when the field names one: see `family`. The three Texts below wear
    /// their own copy of this, since a binding is one property of one object.
    Binding {
        target: measure
        property: "font.family"
        value: root.family
        when: root.family !== ""
    }

    /// One line box of this font, which is the step the lines are laid out on. Measured rather than
    /// computed from the pixel size: a font's line is taller than the type.
    Text {
        id: lineBox
        visible: false
        text: "A"
        font.pixelSize: root.pixelSize
        font.weight: root.weight
        lineHeight: root.lineHeight
    }

    Binding {
        target: lineBox
        property: "font.family"
        value: root.family
        when: root.family !== ""
    }

    /// Measured, never read: see measureWidth(). Its font is the one the text above resolved to
    /// rather than the same two properties set again -- a font spelled one property at a time is
    /// not the font a Text ends up drawing with, and measuring by it lays the field out at the
    /// application's default size.
    TextMetrics {
        id: metrics
        font: measure.font

        onFontChanged: {
            root.advances = ({});
            root.rebuild();
        }
    }

    /// What has settled: plain Text items, with no blur and no shader of their own.
    Repeater {
        model: pieces

        delegate: Text {
            id: settled

            required property int charIndex
            required property string glyph
            required property real px
            required property real py
            x: px
            y: py + root.topPadding
            visible: root.softness(charIndex) <= 0
            text: glyph
            color: root.color
            font.pixelSize: root.pixelSize
            font.weight: root.weight
            lineHeight: root.lineHeight

            Binding {
                target: settled
                property: "font.family"
                value: root.family
                when: root.family !== ""
            }
        }
    }

    /// The tail still sharpening, one MultiEffect a character. A character cannot share a blur with
    /// one a line above it, so the newest `softSpan` of them each carry their own, and that is the
    /// whole shader count this component adds whatever length the field runs to. A slot holds
    /// whichever character is that many places from the cursor, so the items are built once and the
    /// text moves through them.
    Repeater {
        model: root.softSpan

        delegate: Item {
            id: tail
            required property int index
            // layoutRevision is read so that a chunk which re-lays the token this character sits
            // in re-reads its place: positions is edited in place, and a var property does not
            // announce that.
            readonly property var piece: {
                root.layoutRevision;
                return root.pieceAt(root.revealed - 1 - index);
            }

            x: piece ? piece.x : 0
            y: piece ? piece.y + root.topPadding : 0
            visible: piece !== null && root.softness(piece.index) > 0

            Text {
                id: glyph
                visible: false
                text: tail.piece ? tail.piece.character : ""
                color: root.color
                font.pixelSize: root.pixelSize
                font.weight: root.weight
                lineHeight: root.lineHeight
            }

            Binding {
                target: glyph
                property: "font.family"
                value: root.family
                when: root.family !== ""
            }

            MultiEffect {
                // Placed and sized by the effect from its source, so the blur is not cut off at the
                // character's own box -- see ShadowCard.qml.
                x: glyph.x
                y: glyph.y
                source: glyph
                blurEnabled: true
                blurMax: 6
                blur: tail.piece ? root.softness(tail.piece.index) : 0
            }
        }
    }
}
