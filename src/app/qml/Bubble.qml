pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Effects

/**
 * The explanation bubble (UI.md section 4.3), the surface the whole channel exists for.
 *
 * It lands on the side the reader chose in the bubble settings, above the selection by default, and
 * takes the other side only when the chosen one has no room. The action bar follows the same setting,
 * so the two surfaces answer the same gesture -- a finished selection -- from the same side. The
 * countdown lives here rather than in the controller: it is the view's behaviour, and hovering is
 * what pauses it.
 * It never takes focus, so clicking Known or New does not change which application the next
 * injected copy keystroke would go to.
 */
Window {
    id: bubble
    /// Whether this window is the settings preview rather than an explanation the reader called up
    /// (UI.md 4.4). The same card, with the close button, the verdict row, the drag and the hover
    /// report taken away -- each of them either dismisses an explanation or writes a word mark -- and
    /// two marks of its own in their place: the tag reads "Preview", and PreviewFrame.qml draws a
    /// dashed frame around the card. Its own glass sheet, since one sheet holds one backdrop.
    property bool preview: false
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface(bubble.preview ? "preview" : "bubble")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: bubble.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int gap: 10
    /// How long an uninterrupted bubble stays up, from the bubble settings. show() reads it each time.
    readonly property int dismissAfterMs: Controller.settings.bubbleDismissSeconds * 1000

    width: content.implicitWidth + 2 * shadowMargin
    height: content.implicitHeight + 2 * shadowMargin

    /// The word, entity, or sentence title and its explanation, as the controller hands them over.
    property string title: ""
    property string type: "" ///< "word", "entity", or "sentence".
    property string ipa: "" ///< Pronunciation in slashes, beside the word (app_controller.h).
    property string english: ""
    property string chinese: ""
    property string translation: ""
    /// Where the word came from, in one sentence, in the same language as the definition. Empty
    /// whenever the etymology setting is off, or when the model had no origin to give the word.
    property string etymology: ""
    /// One English sentence using the word. Empty whenever the example setting is off, or when the
    /// model had none to give.
    property string example: ""
    property var senses: []
    property string status: "" ///< "new", "known", or empty when there is no word verdict (entity / sentence).

    property bool hovering: false

    /// True while the reader is moving the card with the pointer.
    property bool dragging: false

    /// Milliseconds left before the countdown takes the bubble down. The display's number comes
    /// from here rather than from the timer's interval: hovering and dragging stop the clock, and
    /// the number has to stand still with it. `show()` refills it from `dismissAfterMs`.
    property int remainingMs: 0

    /// Whether the countdown runs at all. A bubble the word list raises belongs to the row under
    /// the pointer and is taken down when the pointer leaves it, so a clock counting under it would
    /// be a second, disagreeing answer to the same question -- and nothing to draw, since there is
    /// no count for the reader to read.
    property bool countsDown: true

    /// Whether the answer is written out a character at a time (RevealText). A card raised whole out
    /// of the cache is not: the word list puts one up for every row the pointer crosses.
    property bool reveal: true
    /// Whether the fields reveal at all, which is the payload's flag and the reader's motion setting.
    readonly property bool revealAnimation: reveal && !bubble.tokens.motion.reduced
    /// The pace the fields are written at, from the reader's speed setting (UI.md 4.3). Hoisted like
    /// `revealAnimation`, so the nine fields read one value rather than the setting each.
    readonly property int revealCharacterMs: Controller.settings.revealCharacterMs

    /// The face the English fields are drawn in, from the reader's font setting (UI.md 4.4). Hoisted
    /// like the pace, and for one more reason: it is the same answer for every English run on the
    /// card -- the word, its pronunciation, the definition and the example. The reader's own
    /// language, in the definition or the translation, keeps the face the application installed,
    /// which is what the setting promises.
    readonly property string englishFont: Controller.settings.font

    /// True while any field is still being written out: what holds the countdown.
    readonly property bool revealing: definitionText.revealing || chineseText.revealing
                                      || translationText.revealing || ipaText.revealing
                                      || etymologyText.revealing || exampleText.revealing
                                      || senseRows.revealing

    property var pendingPayload: null
    property bool pendingWasVisible: false
    property int showGeneration: 0
    property int pendingGeneration: 0

    /// Where the pointer and the card were the last time the hover state changed, in screen
    /// coordinates. Nothing in the application reads them: they are what TODO.md's hover jitter
    /// needs before it can be told a real exit from something else moving `visible`, and there is
    /// no QML-to-log path in this tree that is not a probe -- so the sighting is read off these
    /// two under a debugger instead. See TODO.md, the bubble-hover jitter item.
    property point lastHoverCursor: Qt.point(0, 0)
    property rect lastHoverCard: Qt.rect(0, 0, 0, 0)

    /// @brief Asks the root to put the card beside @p payload, before the bubble is shown.
    ///
    /// The root does the placing because only it knows the screens: the card has to stay inside
    /// the work area of the screen the anchor is on, and this window cannot tell which that is.
    /// Main.qml's placeTransient() answers it, on the side the reader chose or the other one when
    /// the chosen side has no room, and the same call is made again once the text has settled.
    signal placementRequested(var payload)

    function show(payload) {
        const wasVisible = visible;
        const generation = ++showGeneration;
        pendingPayload = payload;
        pendingWasVisible = wasVisible;
        pendingGeneration = generation;
        // A fresh bubble is never mid-drag; a refresh may arrive while the reader is holding it.
        if (!wasVisible)
            dragging = false;
        // The preview writes itself out again every time it comes up, even when it shows the same
        // card: RevealText reads a text that continues the last one as the same answer arriving in
        // another chunk, so clearing first is what makes the fields start over. A real bubble
        // refreshing under the pointer must not lose the writing it has already done.
        if (preview && !wasVisible) {
            ipa = "";
            english = "";
            chinese = "";
            translation = "";
            etymology = "";
            example = "";
            senses = [];
        }
        title = payload.title || "";
        type = payload.type || "";
        ipa = payload.ipa || "";
        english = payload.en || "";
        chinese = payload.zh || "";
        translation = payload.translation || "";
        etymology = payload.etymology || "";
        example = payload.example || "";
        senses = payload.senses ? payload.senses.slice(0, 3) : [];
        status = payload.status || "";
        countsDown = payload.countsDown !== false;
        reveal = payload.reveal !== false;
        remainingMs = dismissAfterMs;

        // A new window must not paint its first frame before Text has settled the card height:
        // the old path showed it at the old height, then moved it on the next event-loop turn.
        // Keep an already visible bubble up while its contents are refreshed, so that replacing
        // one explanation does not turn the fix into a hide-then-show flash.
        if (!wasVisible)
            visible = false;

        // Growing content re-asks for placement; a card the reader is dragging is theirs.
        if (!dragging)
            placement.restart();
    }

    Timer {
        id: placement
        interval: 0
        onTriggered: {
            if (bubble.pendingGeneration !== bubble.showGeneration)
                return;
            bubble.placementRequested(bubble.pendingPayload);
            if (!bubble.pendingWasVisible) {
                bubble.visible = true;
            }
            // Showing a surface is two steps: see Main.qml placePanel() for why `visible` on its
            // own can leave a topmost window under the taskbar.
            bubble.raise();
            bubble.updateCountdown();
        }
    }

    onHoveringChanged: {
        Controller.bubbleHoverChanged(hovering);
        updateCountdown();
    }

    onDraggingChanged: updateCountdown()

    /// The clock starts when the reveal ends, not while the answer is still being written.
    onRevealingChanged: updateCountdown()

    /// The countdown stands still while the reader is holding the bubble, under the pointer or in
    /// the middle of a drag, and picks up the time that is left when they let go of it. The reveal
    /// holds it too: the display time is the reader's time with the answer, which is only readable
    /// once it is all on the card.
    function updateCountdown() {
        if (hovering || dragging || !countsDown || revealing)
            countdown.stop();
        else if (visible)
            countdown.start();
    }

    Timer {
        id: countdown
        // It ticks finer than the second it displays and subtracts, rather than being the whole
        // dismissal in one shot: stopping and starting a timer begins a fresh interval, so a tick
        // as long as the display would swallow up to a second of the clock on every hover, and
        // the hover this bubble sees arrives in bursts (TODO.md, the bubble-hover jitter item).
        interval: 100
        repeat: true
        onTriggered: {
            bubble.remainingMs -= interval;
            if (bubble.remainingMs <= 0) {
                stop();
                bubble.visible = false;
                Controller.explanationDuty.dismissBubble();
            }
        }
    }

    Item {
        id: content
        x: bubble.shadowMargin
        y: bubble.shadowMargin
        implicitWidth: 270
        implicitHeight: card.height

        // The shadow is cast from a shape-only copy of the card, never from the card itself:
        // MultiEffect draws its source through an offscreen texture, and at this monitor's
        // 125% scale that texture is resampled. See SelectionBar.qml for the measurement.
        Rectangle {
            id: shadowShape
            anchors.fill: card
            radius: card.radius
            color: bubble.tokens.glassMode === "" ? bubble.tokens.bubbleBg : "transparent"
            visible: false
        }

        MultiEffect {
            // Placed and sized by the effect, from its source. Anchoring it to the window
            // stretched the card's shape across the whole surface -- see ShadowCard.qml.
            x: card.x
            y: card.y
            source: shadowShape
            shadowEnabled: true
            shadowColor: Qt.rgba(20 / 255, 20 / 255, 26 / 255, bubble.tokens.dark ? 0.75 : 0.26)
            shadowBlur: 0.9
            shadowVerticalOffset: 8
            blurMax: 44
        }

        // The mark that says this card is a demo (UI.md 4.4).
        PreviewFrame {
            visible: bubble.preview
            card: card
            cardRadius: bubble.tokens.radiusCard
            strokeColor: bubble.tokens.muted
        }

        Rectangle {
            id: card
            width: parent.implicitWidth
            height: column.height + 26
            radius: bubble.tokens.radiusCard
            color: bubble.tokens.glassMode === "" ? bubble.tokens.bubbleBg : "transparent"
            border.width: 1
            border.color: bubble.tokens.bubbleBorder

            GlassSurface {
                anchors.fill: parent
                sheet: bubble.glass
                mode: bubble.tokens.glassMode
                radius: card.radius
            }

            // The card is the handle, through the system's own move loop. The verdict buttons
            // keep the pointer where they are, so a drag that begins on one of them is a press
            // on the button rather than a move of the window.
            // The handle a drag moves the window by; the timer below does the moving, and the
            // two reasons it is not the handler's own signal are in ShadowCard.qml. The flag
            // brackets the drag on both edges: setting it only on the way in left the countdown
            // stopped for good, and the bubble never went away again.
            DragHandler {
                id: mover
                target: null
                // A preview belongs to the row under the pointer; a real card is the reader's to move.
                enabled: !bubble.preview

                property point grabCursor: Qt.point(0, 0)
                property point grabWindow: Qt.point(0, 0)

                function place() {
                    const at = Controller.cursorPos();
                    bubble.x = Math.round(grabWindow.x + at.x - grabCursor.x);
                    bubble.y = Math.round(grabWindow.y + at.y - grabCursor.y);
                }

                onActiveChanged: {
                    bubble.dragging = active;
                    if (active) {
                        grabCursor = Controller.cursorPos();
                        grabWindow = Qt.point(bubble.x, bubble.y);
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

            // Leaving takes effect only if it lasts: a pointer sitting on the edge of the
            // card can read as an exit for a frame or two while the verdict row animates open,
            // and each false reading restarts the countdown and collapses the row again.
            // Entering stays immediate, so the row still opens the moment the pointer arrives.
            // ponytail: a guard, not a diagnosis -- a controlled run with the pointer parked on
            // the card showed no flapping, so the cause of the reported one is still open. If
            // it outlasts this, raise the interval.
            HoverHandler {
                // A preview does not report hover: the pointer is on a settings row, not holding an answer.
                enabled: !bubble.preview
                onHoveredChanged: {
                    // Both readings, every flip: a real exit and something else moving `visible`
                    // look the same in the log the controller keeps, and these two are what tells
                    // them apart (TODO.md, the bubble-hover jitter item).
                    bubble.lastHoverCursor = Controller.cursorPos();
                    const at = card.mapToGlobal(0, 0);
                    bubble.lastHoverCard = Qt.rect(at.x, at.y, card.width, card.height);
                    if (hovered) {
                        hoverSettle.stop();
                        bubble.hovering = true;
                    } else {
                        hoverSettle.restart();
                    }
                }
            }

            Timer {
                id: hoverSettle
                interval: 140
                onTriggered: bubble.hovering = false
            }

            // The way out for a reader who does not want to wait the countdown out. It takes
            // the same two steps the countdown takes on its own, so the controller hears about
            // the dismissal the same way either way. A preview has no countdown to cut short.
            Icon {
                visible: !bubble.preview
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.top: parent.top
                anchors.topMargin: 15
                source: "qrc:/icons/ui-close.svg"
                color: bubble.tokens.faint
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        countdown.stop();
                        bubble.visible = false;
                        Controller.explanationDuty.dismissBubble();
                    }
                }
            }

            Column {
                id: column
                x: 16
                y: 14
                width: parent.width - 32
                spacing: 0

                // A word bubble carries the learning verdict; a sentence bubble carries its type
                // label. An entity bubble carries neither: a named thing is not a learning item,
                // so it is drawn as title plus encyclopaedic definition with no badge.
                Rectangle {
                    // A preview wears this row in place of a learning verdict (UI.md 4.4).
                    visible: bubble.preview || bubble.type === "word" || bubble.type === "sentence"
                    width: tag.width + 16
                    height: 17
                    radius: bubble.tokens.radiusPill
                    color: bubble.preview ? bubble.tokens.panel2
                         : bubble.type === "word" ? (bubble.status === "known" ? bubble.tokens.panel2 : bubble.tokens.okBg)
                         : "transparent"
                    border.width: bubble.preview || bubble.type === "word" ? 0 : 1
                    border.color: bubble.tokens.line

                    Text {
                        id: tag
                        anchors.centerIn: parent
                        text: bubble.preview ? qsTr("Preview")
                             : bubble.type === "sentence" ? qsTr("Sentence")
                             : bubble.type === "word" ? (bubble.status === "known" ? qsTr("Known") : qsTr("New"))
                             : ""
                        color: bubble.preview ? bubble.tokens.muted
                             : bubble.type === "sentence" ? bubble.tokens.muted
                             : bubble.status === "known" ? bubble.tokens.muted
                             : bubble.tokens.okText
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
                }

                // The title and its pronunciation share one line for word bubbles: IPA is set
                // beside the word, the way a dictionary prints it. Read only --
                // it is not a control, and nothing about it invites a press. The pair is placed by
                // hand rather than by a RowLayout: a layout with no item left to grow does not
                // keep the slack at the end -- measured, a 30 px word and its pronunciation 36 px
                // apart in a 238 px row -- and a gap that wide reads as two unrelated things.
                // The prototype's 7 px is what the pair is set with.
                // A sentence bubble draws no title: the selected sentence is usually long and
                // overflows the card, and the reader still has it highlighted on screen. Word and
                // entity titles are short and stay. A translation bubble draws none either, and for
                // the same reason twice over: the Chinese it came from is long, and the reader asked
                // for the English of it rather than for the original back (UI.md section 4.3).
                Item {
                    id: wordRow
                    visible: bubble.type !== "sentence" && bubble.type !== "translation"
                    width: parent.width
                    height: bubble.type === "sentence" || bubble.type === "translation" ? 0 : wordText.implicitHeight

                    Text {
                        id: wordText
                        // What is left once the pronunciation has its room: a word with none left
                        // ends in an ellipsis rather than running under its own IPA.
                        width: Math.min(implicitWidth, wordRow.width - (ipaText.visible ? ipaText.implicitWidth + 7 : 0))
                        topPadding: 8
                        text: bubble.title
                        color: bubble.tokens.text
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        font.family: bubble.englishFont
                        elide: Text.ElideRight
                    }

                    RevealText {
                        id: ipaText
                        // On the word's baseline: each item's own baseline offset is the distance
                        // from its top to that baseline, padding included.
                        x: wordText.width + 7
                        y: wordText.baselineOffset - baselineOffset
                        visible: bubble.type === "word" && bubble.ipa !== ""
                        animated: bubble.revealAnimation
                        characterMs: bubble.revealCharacterMs
                        // Measured, not wrapped, and whole from the first frame: the title gives up
                        // its room to the full pronunciation, not to what has been written so far.
                        wrapMode: Text.NoWrap
                        text: bubble.ipa
                        color: bubble.tokens.faint
                        family: bubble.englishFont
                        pixelSize: 13 // the prototype's 12.5 px, as the definition line rounds it
                    }
                }

                SenseRows {
                    id: senseRows
                    senses: bubble.type === "word" ? bubble.senses : []
                    animated: bubble.revealAnimation
                    characterMs: bubble.revealCharacterMs
                    englishColor: bubble.tokens.muted
                    translationColor: bubble.tokens.text
                    separatorColor: bubble.tokens.line2
                    family: bubble.englishFont
                }

                // The definition line, and for a translation bubble the whole card: a translation
                // has no title and no badge above it, so drawing it in the definition's secondary
                // colour would leave the answer looking like a footnote to nothing (UI.md 4.3).
                RevealText {
                    id: definitionText
                    visible: bubble.senses.length === 0 && bubble.english !== ""
                    animated: bubble.revealAnimation
                    characterMs: bubble.revealCharacterMs
                    width: parent.width
                    topPadding: 6
                    text: bubble.english
                    color: bubble.type === "translation" ? bubble.tokens.text : bubble.tokens.muted
                    family: bubble.englishFont
                    pixelSize: 13
                    lineHeight: 1.55
                }

                RevealText {
                    id: chineseText
                    visible: bubble.senses.length === 0 && bubble.chinese !== ""
                    animated: bubble.revealAnimation
                    characterMs: bubble.revealCharacterMs
                    width: parent.width
                    topPadding: 5
                    text: bubble.chinese
                    color: bubble.tokens.text
                    pixelSize: 12
                    lineHeight: 1.5
                }

                RevealText {
                    id: translationText
                    visible: bubble.senses.length === 0 && bubble.translation !== ""
                    animated: bubble.revealAnimation
                    characterMs: bubble.revealCharacterMs
                    width: parent.width
                    topPadding: 5
                    text: bubble.translation
                    color: bubble.tokens.text
                    pixelSize: 12
                    lineHeight: 1.5
                }

                // The word's origin, under the explanation it belongs to. A hairline and a small
                // label are what keep it from reading as one more line of the definition: the
                // reader asked for two different things and has to see two different things
                // (UI.md 4.3). The label is the card's label colour and weight; the sentence under
                // it sits one step down from the definition's colour, because an origin is context
                // for the meaning rather than another meaning.
                // Nothing at all is drawn when there is none: an empty labelled slot would read as
                // a model that failed rather than as a word with no origin to give.
                // The gap above the rule is the 6 px a sense break uses, not the 12 this block
                // was drawn with first: measured on the rendered card, that put 24 px of blank
                // between the definition and the hairline -- the widest gap on the card, in the
                // one place that is only introducing an aside.
                Column {
                    id: etymologyBlock
                    objectName: "etymologyBlock"
                    visible: bubble.etymology !== ""
                    width: parent.width
                    topPadding: 6
                    spacing: 5

                    Rectangle {
                        objectName: "etymologySeparator"
                        width: parent.width
                        height: 1
                        color: bubble.tokens.line2
                    }

                    Text {
                        objectName: "etymologyLabel"
                        text: qsTr("Etymology")
                        color: bubble.tokens.muted
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }

                    RevealText {
                        id: etymologyText
                        objectName: "etymologyText"
                        animated: bubble.revealAnimation
                        characterMs: bubble.revealCharacterMs
                        width: parent.width
                        text: bubble.etymology
                        color: bubble.tokens.muted
                        family: bubble.englishFont
                        pixelSize: 12
                        lineHeight: 1.5
                    }
                }

                // The example sentence sits under the origin in the same set-off form (UI.md 4.3). Drawn
                // only when there is one: an empty block would read as a model that failed to give one.
                Column {
                    id: exampleBlock
                    objectName: "exampleBlock"
                    visible: bubble.example !== ""
                    width: parent.width
                    topPadding: 6
                    spacing: 5

                    Rectangle {
                        objectName: "exampleSeparator"
                        width: parent.width
                        height: 1
                        color: bubble.tokens.line2
                    }

                    Text {
                        objectName: "exampleLabel"
                        text: qsTr("Example")
                        color: bubble.tokens.muted
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }

                    RevealText {
                        id: exampleText
                        objectName: "exampleText"
                        animated: bubble.revealAnimation
                        characterMs: bubble.revealCharacterMs
                        width: parent.width
                        text: bubble.example
                        color: bubble.tokens.muted
                        family: bubble.englishFont
                        pixelSize: 12
                        lineHeight: 1.5
                    }
                }

                // The verdict buttons, drawn shut until the pointer is on the card (UI.md 4.3).
                BubbleVerdictRow {
                    tokens: bubble.tokens
                    lemma: bubble.title
                    hovering: bubble.hovering
                    status: bubble.status
                }

                Text {
                    // A bubble with no countdown has no seconds to count, and the line's absence is
                    // what says it will wait.
                    visible: bubble.countsDown
                    width: parent.width
                    topPadding: 9
                    // The remaining seconds, not the setting that seeded them: the count stands
                    // still while the pointer holds the bubble and steps down again when it goes.
                    text: qsTr("Disappears in %1s").arg(Math.ceil(bubble.remainingMs / 1000))
                    color: bubble.tokens.faint
                    font.pixelSize: 11
                }

            }
        }
    }

    GlassHost {
        window: bubble
        sheet: bubble.glass
        dragging: bubble.dragging
    }
}
