pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The explanation bubble (UI-SPEC.md section 4.3), the surface the whole channel exists for.
 *
 * Lands on the side the reader chose in the bubble settings, above the selection by default, taking
 * the other side only when the chosen one has no room; the action bar follows the same setting, so
 * both surfaces answer a finished selection from the same side. The countdown lives here, not in the
 * controller: it is the view's behaviour, and hovering pauses it. The window never takes focus, so
 * clicking Known or New does not change which application the next injected copy keystroke goes to.
 */
Window {
    id: bubble
    /// Whether this window is the settings preview (UI-SPEC.md 4.4): the same card without the close
    /// button, verdict row, drag or hover report, tagged "Preview" and framed by PreviewFrame.qml.
    /// Its own glass sheet, since one sheet holds one backdrop.
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

    /// Whether the card is meant to be on screen. The fade and the delayed window hide follow
    /// it (SurfaceFade.qml), so every path that takes the bubble down goes through dismiss().
    property bool surfaceShown: false

    readonly property int shadowMargin: 26
    readonly property int gap: 10
    /// How long an uninterrupted bubble stays up, from the bubble settings. show() reads it each time.
    readonly property int dismissAfterMs: Controller.settings.bubbleDismissSeconds * 1000

    width: content.implicitWidth + 2 * shadowMargin
    height: SurfaceGeometry.windowHeight(content.implicitHeight, rowHold.active ? rowHold.floor : 0, shadowMargin)
    onVisibleChanged: {
        if (!visible) {
            rowHold.active = false;
            // The picture strip belongs to the card that asked, so it goes with it.
            content.collapseStrip();
            // So does the peek line: the next card has not asked for one.
            peekLine = "";
        }
    }
    HeightHold {
        id: rowHold
        contentHeight: content.implicitHeight
        duration: bubble.tokens.motion.bubble
    }

    SurfaceFade {
        id: fade
        tokens: bubble.tokens
        window: bubble
        shown: bubble.surfaceShown
    }

    /// The word, entity, or sentence title and its explanation, as the controller hands them over.
    property string title: ""
    property string type: "" ///< "word", "entity", or "sentence".
    /// The card's identity, settled by the duty's card session: "selection", "review" (the word
    /// list's hover replay) or "reviewAnswer" (the pet's answer). Placement reads this; the flags
    /// settled beside it (countsDown, verdictButtons) are what the rows read.
    property string cardKind: ""
    property string ipa: "" ///< Pronunciation in slashes, beside the word (app_controller.h).
    /// The most common sense's part of speech, drawn after the pronunciation (UI-SPEC.md 4.3).
    /// Empty whenever the answer had none or the display switch is off.
    property string pos: ""
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
    property var englishSpans: []
    property string signpost: ""
    property var collocations: []
    readonly property bool atRoot: chainSteps.atRoot
    property alias chainSteps: content.chainSteps
    property string status: "" ///< "new", "known", or empty when there is no word verdict (entity / sentence).
    /// The sentence breakdown's state (UI-SPEC.md 4.3): empty for any other card, else "pending",
    /// "ready" or "failed". `segments` is the sentence in order, each `{text, role}`; `roles` the roles present.
    property string breakdown: ""
    property var segments: []
    property var roles: []
    /// The correction's kind, "spell" or "grammar", for a card that shows a correction of the selection; empty
    /// otherwise. It rides the same state field as the breakdown: `breakdown` is its pending, ready or failed.
    property string correction: ""
    /// The reader's reason on a failed breakdown or correction, already translated; empty otherwise.
    property string failure: ""
    /// A card that marks up the sentence rather than defines a word: a breakdown or a correction. Such a card
    /// has no title, no verdict, no peek globe and no definition.
    readonly property bool annotated: breakdown !== "" || correction !== ""

    property bool hovering: false

    property bool dragging: false

    /// Milliseconds left before the countdown takes the bubble down. The display's number comes
    /// from here rather than from the timer's interval: hovering and dragging stop the clock, and
    /// the number has to stand still with it. `show()` refills it from `dismissAfterMs`.
    property int remainingMs: 0

    /// A bubble the word list raises belongs to the row under the pointer and closes when the
    /// pointer leaves it; no clock runs under it.
    property bool countsDown: true

    /// The mother-tongue line the held globe asked for (UI-SPEC.md 4.3), for the word standing
    /// when it arrived. Main.qml sets it from the duty's peekArrived; the card draws it while the
    /// hold lasts, and a different card starts over without it.
    property string peekLine: ""

    /// The Not-the-Sense? entry's state (UI-SPEC.md 4.3), as the duty's cycle holds it: whether
    /// the entry is offered at all, and the word it offers next -- the way back past the last
    /// sense. The duty's senseSwapped carries the changes, and a card drawn fresh starts at the
    /// answer's own sense.
    property bool   senseCycle: false
    property string senseHint: ""
    /// Whether a swapped sense has been drawn. From then on the card's fields draw whole: the
    /// reader has already read this card once, and writing a switched sense out a character at
    /// a time would stand between the click and the sense it asked for.
    property bool senseSwappedOnce: false

    /// Whether the Known and New buttons are drawn. The card a review raises is the reader's to
    /// read, not to mark (UI-SPEC.md 4.8 / 4.13), and the duty settles this once for both review
    /// kinds; the status tag above the title still says what the word is.
    property bool verdictButtons: true

    /// Whether the answer is written out a character at a time (RevealText). A card raised whole out
    /// of the cache is not: the word list puts one up for every row the pointer crosses.
    property bool reveal: true
    /// Whether the fields reveal at all, which is the payload's flag and the reader's motion setting.
    readonly property bool revealAnimation: reveal && !bubble.tokens.motion.reduced
    /// The pace the fields are written at, from the reader's speed setting (UI-SPEC.md 4.3). Hoisted like
    /// `revealAnimation`, so the nine fields read one value rather than the setting each.
    readonly property int revealCharacterMs: Controller.settings.revealCharacterMs

    /// The face the English fields are drawn in, from the reader's font setting (UI-SPEC.md 4.4). Hoisted
    /// like the pace: one value for every English run on the card -- word, pronunciation, definition,
    /// example. The reader's own language, in the definition or the translation, keeps the installed face.
    /// The font dropdown's row's face; null follows the setting.
    property var previewFont: null
    readonly property string englishFont: previewFont !== null ? previewFont : Controller.settings.font

    /// The size the answer is read at, from the reader's font-size setting (UI-SPEC.md 4.4): one answer
    /// for every run on the card, so the word stays above the definitions at every step. The
    /// setting names the body size, and the rest is that size against the 13 px the card is drawn
    /// at by default; the labels, the verdict row and the countdown are the card's chrome and keep
    /// their own. The size dropdown's row's size; null follows the setting.
    property var previewFontSize: null
    readonly property int bodyFontSize: previewFontSize !== null ? previewFontSize : Controller.settings.bubbleFontSize
    readonly property real fontScale: bodyFontSize / 13
    /// @param base The size a run is drawn at under the default setting.
    /// @return That size under the size in force, in whole pixels.
    function sizeAt(base) {
        return Math.round(base * fontScale);
    }

    /// True while any field is still being written out: what holds the countdown.
    readonly property bool revealing: content.revealing

    /// Whether the word on this card is the one coming out of the speaker now (UI-SPEC.md 4.3).
    /// The word is compared as well as the flag: a card that replaces another one while a recording
    /// is still playing must not wear the playing colour for a word it does not show.
    readonly property bool speaking: Controller.speechDuty.playing && Controller.speechDuty.playingWord === bubble.title

    /// Whether a card that appears reads its word by itself (docs/adr/0134). Off by default: sound
    /// interrupts reading, and an automatic scan pops card after card.
    readonly property bool autoPlay: Controller.settings.speechAutoPlay === true

    /// The word this card has already read by itself. One word, one reading: a card arrives a field
    /// at a time and show() runs once per field, so without this the word would be spoken several
    /// times over. It is cleared when a different card takes this one's place.
    property string autoPlayedWord: ""

    /// @brief Read this card's headword once, because the reader pressed its speaker or its
    ///        pronunciation. A card with no word -- a sentence, an entity, a breakdown -- has
    ///        nothing to read and says nothing.
    function speak() {
        if (type === "word" && title !== "")
            Controller.speechDuty.speak(title);
    }

    property var pendingPayload: null
    property bool pendingWasVisible: false
    property int showGeneration: 0
    property int pendingGeneration: 0

    property point lastHoverCursor: Qt.point(0, 0)
    property rect lastHoverCard: Qt.rect(0, 0, 0, 0)

    /// @brief Asks the root to put the card beside @p payload, before the bubble is shown.
    ///
    /// Only the root knows the screens; the card must stay inside the anchor screen's work area.
    /// SurfaceHost.qml's placeTransient() answers, on the chosen side or the other when that side
    /// has no room, and the same call is made again once the text has settled.
    signal placementRequested(var payload)

    function show(payload) {
        const wasVisible = visible;
        const generation = ++showGeneration;
        // A different card has not asked for a peek line: the one on the card was answered for
        // the word that stood before it. The payload itself may carry one -- the mother-tongue
        // row the settings preview draws under -- and that line wins. A different card has also
        // not had a sense swapped onto it.
        if (payload.sameCard !== true) {
            peekLine = payload.peekLine || "";
            senseSwappedOnce = false;
        }
        pendingPayload = payload;
        pendingWasVisible = wasVisible;
        pendingGeneration = generation;
        // A fresh bubble is never mid-drag; a refresh may arrive while the reader is holding it.
        if (!wasVisible)
            dragging = false;
        // A preview starts its fields over every time; a real bubble refreshing under the pointer
        // must keep the writing it has already done.
        if (preview && !wasVisible) {
            ipa = "";
            english = "";
            chinese = "";
            translation = "";
            etymology = "";
            example = "";
            senses = [];
        }
        chainSteps.refresh(payload);
        englishSpans = payload.spans || [];
        signpost = payload.signpost || "";
        collocations = payload.collocations || [];
        title = payload.title || "";
        type = payload.type || "";
        cardKind = payload.cardKind || "";
        ipa = payload.ipa || "";
        pos = payload.pos || "";
        english = payload.en || "";
        chinese = payload.zh || "";
        translation = payload.translation || "";
        etymology = payload.etymology || "";
        example = payload.example || "";
        senses = payload.senses || [];
        status = payload.status || "";
        breakdown = payload.breakdown || "";
        correction = payload.correction || "";
        failure = payload.failure || "";
        segments = payload.segments || [];
        roles = payload.roles || [];
        senseCycle = payload.senseCycle === true;
        senseHint = payload.senseHint || "";
        countsDown = payload.countsDown !== false;
        reveal = payload.reveal !== false;
        verdictButtons = payload.verdictButtons !== false;
        remainingMs = dismissAfterMs;

        // Auto-play (docs/adr/0134, PRODUCT.md's pronunciation table). Only the card a selection
        // pops reads by itself: the settings preview is a by-product of changing a setting, and the
        // word list's hover replay and the pet's answer are surfaces the reader did not ask to hear
        // -- one reads a row the pointer crossed, the other answers the pet's question for them.
        // "selection" is the card kind that means the reader pointed at a word, so it is the whole
        // of the test along with the word and the setting.
        if (payload.sameCard !== true) {
            autoPlayedWord = "";
            // A different card is a different word: whatever the last one was reading stops here,
            // whether or not this one reads at all.
            Controller.speechDuty.stop();
        }
        if (autoPlay && cardKind === "selection" && !preview && type === "word" && title !== "" && title !== autoPlayedWord) {
            autoPlayedWord = title;
            Controller.speechDuty.speak(title);
        }

        // A new window must not paint its first frame before Text has settled the card height, or it
        // shows at the old height and moves on the next event-loop turn. An already visible bubble
        // stays up through a refresh, so replacing one explanation is not a hide-then-show flash. A
        // card that was fading out starts again from the bottom, since its window may already be gone.
        if (!wasVisible) {
            visible = false;
            surfaceShown = false;
        }

        // Growing content re-asks for placement; a dragged card keeps its frame.
        if (!dragging)
            placement.restart();
    }

    /// @brief Take the card down, fading it out before the window goes.
    ///
    /// The semantic half is the caller's: the dismissal reaches the controller whether or not the
    /// card is still fading. Only the appearance waits, so a caller clearing the duty need not
    /// coordinate with it.
    function dismiss() {
        countdown.stop();
        // The recording belongs to the card that asked for it, so it goes with it: a word still
        // being read out of a card the reader has closed is a voice with nothing on screen to
        // explain it.
        Controller.speechDuty.stop();
        surfaceShown = false;
    }

    /// @brief Draw what the duty's sense cycle now shows, in place.
    ///
    /// The Not-the-Sense? entry's answer: the row's texts and pictures swap where they stand and
    /// the window is not re-placed -- the card is where the reader left it, and pulling it back
    /// to its anchor would take it away from them. The entry's own fields follow the map either
    /// way, so a hint that lands without a switch still reaches the card.
    function applySense(swap) {
        senseCycle = swap.cycle !== false;
        senseHint = swap.hint || "";
        const row = swap.row;
        if (row === undefined)
            return;
        senseSwappedOnce = true;
        senses = [row];
        signpost = swap.signpost || "";
        // The peek line on the card answered the sense before the swap; the next hold asks for
        // the swapped sense's own line, and the stale one does not stay to be read as it.
        peekLine = "";
        // The strip belongs to the sense: open, it follows the row's own query, and a sense with
        // none takes it down.
        content.followSense(row.imageQuery || "");
    }

    Timer {
        id: placement
        interval: 0
        onTriggered: {
            if (bubble.pendingGeneration !== bubble.showGeneration)
                return;
            bubble.placementRequested(bubble.pendingPayload);
            if (!bubble.pendingWasVisible)
                bubble.visible = true;
            // The fade starts only once the window is up (SurfaceFade.qml): a card faded in
            // while hidden would be half-way through its arrival on the first painted frame.
            // A visible bubble refreshing its contents is already at 1, so it does not move.
            bubble.surfaceShown = true;
            // Showing a surface is two steps: see SurfaceHost.qml placePanel() for why `visible` on its
            // own can leave a topmost window under the taskbar.
            bubble.raise();
            bubble.updateCountdown();
        }
    }

    /// @brief Give the reader a fresh reading window after a screen-reader press.
    ///
    /// The countdown knows the pointer and a drag; it cannot see the AT review pass. A press
    /// arriving through an `Accessible.onPressAction` refills the clock, the same way arriving
    /// with the pointer holds it, so a card read aloud does not disappear mid-sentence.
    function readerActed() {
        if (!countsDown)
            return;
        remainingMs = dismissAfterMs;
        updateCountdown();
    }

    onHoveringChanged: {
        updateCountdown();
        // The row's availability is BubbleCard's one reading of it, so this hold and the row itself
        // cannot disagree about whether a verdict is on offer.
        if (content.verdictsAvailable)
            rowHold.begin(content.implicitHeight + (hovering ? content.verdictRowHeight : -content.verdictRowHeight));
    }

    onDraggingChanged: updateCountdown()

    /// The clock starts when the reveal ends, not while the answer is still being written.
    onRevealingChanged: updateCountdown()

    /// The countdown stands still while the bubble is hovered or dragged and picks up the time that
    /// is left on release. The reveal holds it too: the time shown is the reader's time with the
    /// answer, readable only once it is all on the card.
    function updateCountdown() {
        if (hovering || dragging || !countsDown || revealing || breakdown === "pending" || !surfaceShown)
            countdown.stop();
        else
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
                bubble.dismiss();
                Controller.explanationDuty.dismissBubble();
            }
        }
    }

    /// The card the answer is drawn on: its shape, its glass and every row (BubbleCard.qml).
    BubbleCard {
        id: content
        bubble: bubble
        x: bubble.shadowMargin
        y: bubble.shadowMargin
        opacity: fade.progress
        // A window that is still on screen while it fades out is not there to be pressed, and
        // the card that used to be under the pointer must not act for it.
        enabled: bubble.surfaceShown
        transform: Translate { y: fade.lift }
    }

    GlassHost {
        window: bubble
        sheet: bubble.glass
        dragging: bubble.dragging
    }
}
