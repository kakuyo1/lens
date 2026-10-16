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
 * where it stood: starting the field over is the stutter this exists to prevent.
 *
 * Each character appears soft and sharpens over `blurMs`, so the tail of the text is blurred and
 * the newest character visibly clears. Only the characters still sharpening are drawn with a blur;
 * everything settled is plain text. That is what keeps the shader count at the tail of the text
 * rather than at the length of the field.
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

    /// How long a character waits for the next one.
    readonly property int characterMs: 35
    /// How long a character stays soft after it appears.
    readonly property int blurMs: 180
    /// How many characters' worth of time that is. These and only these carry a blur.
    readonly property int softSpan: Math.max(1, Math.ceil(blurMs / characterMs))

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

    /// Where every character of the text sits, in reading order, and the same pieces by text index
    /// for the tail below. Rebuilt when the text or the width changes rather than bound, because
    /// building it measures as it goes.
    property var pieces: []
    property var positions: []
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

    /// Lay the whole text out, breaking lines between words.
    ///
    /// The two lists are handed over whole, once they are filled: a Repeater reads its model when
    /// the property changes, so an array assigned empty and then pushed into would leave the field
    /// drawn out of nothing at all.
    function rebuild() {
        const found = [];
        const at = [];
        const source = text;
        if (source.length > 0) {
            const step = lineBox.implicitHeight;
            const limit = wrapMode === Text.NoWrap ? Number.POSITIVE_INFINITY : Math.max(1, width);
            let x = 0;
            let y = 0;
            let index = 0;
            while (index < source.length) {
                const start = index;
                const blank = source.charAt(index) === " " || source.charAt(index) === "\n";
                while (index < source.length
                       && (source.charAt(index) === " " || source.charAt(index) === "\n") === blank)
                    ++index;
                const token = source.substring(start, index);

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
                    found.push(piece);
                    at[start + i] = piece;
                }
                // The word's own width rather than the sum of its letters: a pair that is kerned is
                // narrower than the two apart, and the next word starts where this one ends.
                x += tokenWidth;
            }
        }
        pieces = found;
        positions = at;
    }

    onTextChanged: {
        const continued = previous.length > 0 && text.startsWith(previous);
        previous = text;
        if (!continued) {
            startedMs = Date.now();
            cursor = 0;
        }
        if (!animated)
            settle();
        rebuild();
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
        model: root.pieces

        delegate: Text {
            required property var modelData
            x: modelData.x
            y: modelData.y + root.topPadding
            visible: root.softness(modelData.index) <= 0
            text: modelData.character
            color: root.color
            font.pixelSize: root.pixelSize
            font.weight: root.weight
            lineHeight: root.lineHeight
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
            readonly property var piece: root.pieceAt(root.revealed - 1 - index)

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
