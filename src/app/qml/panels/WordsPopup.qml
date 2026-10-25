pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Window

/**
 * Every word the bubble has shown (UI-SPEC.md section 4.8), newest first, with a filter across the
 * top. The controller works out which day a word was last shown on, relative to today, and hands
 * over the raw date and time; the labels are drawn here, in the reader's language.
 *
 * Also where a word is settled: each row carries the bubble's two verdict pills, so a word the
 * reader did not mark while the bubble was up (or the countdown took away) is marked here.
 *
 * The cross at the end of a row raises the removal question of the same section, a window of its
 * own over this card rather than a face drawn in it -- see the Window below.
 */
Window {
    id: words
    objectName: "words"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("words")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: words.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    /// Set by the panel that replaces this one: the card fades out in place, and the window goes when it has.
    property bool leaving: false

    readonly property int shadowMargin: 26
    /// The face the words themselves are drawn in, from the reader's font setting (UI-SPEC.md 4.4): the
    /// rows are the same English the bubble draws. Dates, counts and pills keep the application's face.
    readonly property string englishFont: Controller.settings.font
    readonly property int minCardWidth: 320
    readonly property int maxCardWidth: 560

    /// As wide as its widest row, so no word is ever shown elided. Rows report their width as they
    /// lay out rather than being walked for it -- a JS loop over Controller.words reads a QVariantMap
    /// per row, ~170 ms each on this machine, a frozen window (docs/QML.md section 6). A running
    /// maximum, so the card does not narrow under the reader as rows change; the cap above bounds it.
    property real widestRow: 0

    /// @brief Note one row's natural width. Called by each row as it lays out.
    function noteRowWidth(width) {
        if (width > widestRow)
            widestRow = width;
    }

    readonly property int cardWidth: Math.min(maxCardWidth, Math.max(minCardWidth, Math.ceil(widestRow)))

    width: cardWidth + 2 * shadowMargin

    /// As tall as its list, and it keeps that height while the question is up. It is not re-sized to
    /// the question: `SurfaceHost.placeBeside` puts the card's bottom edge on the tray icon and
    /// nothing re-places a panel that changes size afterwards, so a card that shrank would leave the
    /// icon it was opened from. The question is centred in the height the list left instead.
    height: column.implicitHeight + 34 + 2 * shadowMargin

    /// 0 = all, 1 = known, 2 = new.
    property int filter: 0
    /// One row's drawn height, the number the list's own sizing keys on (WordsListRow.qml). The
    /// rows' fold animation runs this between it and zero; stating it here keeps the viewport a
    /// fact of the row count rather than of the animation's progress.
    readonly property int rowHeight: 31
    /// The filter folds rows shut over a motion token; until that settles, a list with no rows is
    /// still collapsing, not empty. The empty line waits for it.
    property bool filterSettled: true
    readonly property bool filterEmpty: filter !== 0 && Controller.words.length > 0
                                        && filterSettled && list.contentHeight === 0
    onFilterChanged: {
        filterSettled = false;
        filterSettle.restart();
    }

    /// The word the question is asking about, "" when it is not up.
    property string pendingRemoval: ""

    /// Rows folding away before the model lets them go, and the words waiting on that fold.
    property var removingWords: ({})
    property var removalQueue: []
    /// The one line an import leaves behind, in the count row so the card does not change height.
    property string importStatus: ""
    property bool importFailed: false

    /// Whether a removal is put to the reader before it happens.
    ///
    /// Per run, not a setting: the reader asked for the question to be silenceable without the answer
    /// outliving the session, so this is a property of this surface and nothing is written to the
    /// settings document.
    ///
    /// Asked about every time, with "do not ask again" off, until the reader turns it on. Cancel
    /// removes nothing and asks nothing about asking, so the next deletion is asked about again.
    property bool askBeforeRemoving: true

    /// A question left half-answered by a press elsewhere is not a state to come back to, and
    /// neither is a review bubble: the pointer left the panel, whatever it is doing now. Forced,
    /// not graceful: the card may be held by a pointer standing on it, and the panel going means
    /// the reading goes too, held or not.
    onVisibleChanged: if (!visible) {
        closeChild();
        Controller.explanationDuty.dismissReviewNow();
    }

    /**
     * @brief Settle a word the row's cross asked to delete.
     * @param word Lemma that row was showing.
     *
     * With the question switched off this is the whole removal, which is what the reader chose.
     */
    function requestRemoval(word) {
        if (!askBeforeRemoving) {
            words.removeWordAnimated(word);
            return;
        }
        pendingRemoval = word;
        question.ask();
    }

    /// @brief Fold @p word's row shut, then take the word out of the list.
    ///
    /// The model change rebuilds every delegate, so removing the word first would take the row
    /// away in one frame instead of letting it close. The fold is the list's own row animation,
    /// and the controller call waits for it so the list and the window both settle by animation.
    function removeWordAnimated(word) {
        if (word === "" || words.removingWords[word])
            return;
        if (words.tokens.motion.pop === 0) {
            Controller.removeWord(word);
            return;
        }
        words.removingWords = Object.assign({}, words.removingWords, { [word]: true });
        words.removalQueue.push(word);
        removalTimer.restart();
    }

    /// @brief Take every folded word out at once, after the last fold has landed.
    function flushRemovals() {
        const queue = words.removalQueue;
        words.removalQueue = [];
        words.removingWords = ({});
        for (const word of queue)
            Controller.removeWord(word);
    }

    /// Restarted per cross, so every queued row gets a whole fold; the flush takes them together.
    Timer {
        id: removalTimer
        interval: words.tokens.motion.pop
        onTriggered: words.flushRemovals()
    }

    /// @brief The time label of a row, in the reader's language. Empty when the row has no day.
    function whenLabel(row) {
        if (row.day === "today")
            return qsTr("Today %1").arg(row.time);
        if (row.day === "yesterday")
            return qsTr("Yesterday %1").arg(row.time);
        if (row.day === "earlier")
            return row.date + " " + row.time;
        return "";
    }

    /// @brief Put the question down without answering it.
    function closeChild() {
        question.dismiss();
    }

    /// The question's card in screen coordinates, or null when it is not up. SurfaceHost.qml.s
    /// outside-press rule reads it the way it reads the settings panel's open dropdown.
    readonly property var openChildRect: question.visible
        ? Qt.rect(question.x + question.shadowMargin, question.y + question.shadowMargin,
                  question.width - 2 * question.shadowMargin, question.height - 2 * question.shadowMargin)
        : null

    /// It is a question, not a dropdown: a press that lands elsewhere is not an answer to it, so
    /// it neither puts the question down nor closes the panel under it. Cancel, Remove and Escape
    /// are the ways out.
    readonly property bool openChildIsModal: true

    /// Escape closes the panel, the way its cross does. The removal question answers it first:
    /// its own shortcut stands while the question stands, and two application-wide shortcuts on
    /// one key would be ambiguous. Once the question is on its way out this one takes over, so
    /// a second Escape during its fade still closes the panel.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: words.visible && (!question.visible || question.leaving)
        onActivated: words.visible = false
    }

    /// The filter above, spelled the way exportWords() and saveWords() take it. The list and
    /// an export are the same rows seen twice, so an export follows whatever is on screen.
    readonly property var scopes: ["all", "known", "new"]
    readonly property string scope: scopes[filter]

    /// The reader asked for the panel this one was opened from.
    signal backRequested()
    /// The title is the entry point to the annual word record.
    signal yearRequested()

    ShadowCard {
        id: card
        anchors.fill: parent
        color: words.tokens.glassMode === "" ? words.tokens.panel : "transparent"

        /// The list is not an answer to the question, so it takes no press while one is up -- the fade
        /// included, because the window is modal until it hides. This guard is what holds on the
        /// offscreen platform, which does not enforce modality: a press reaching the card would act
        /// on a verdict pill or a cross, deleting a second word out from under the first.
        enabled: !question.visible

        GlassSurface {
            anchors.fill: parent
            sheet: words.glass
            mode: words.tokens.glassMode
            radius: card.radius
        }

        // The list is the only scroller, but the title, the filters and the card's right padding
        // are part of the same page: a wheel anywhere on the card scrolls the list. The handler
        // sits on the card, behind the view in delivery order, so a wheel over the list itself is
        // still the ListView's (and keeps its acceleration); this only answers where that view
        // declined the event.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                if (list.contentHeight <= list.height)
                    return;
                let step = event.pixelDelta.y;
                if (step === 0)
                    step = -event.angleDelta.y / 120 * 72;
                const next = Math.max(0, Math.min(list.contentY + step,
                                                  list.contentHeight - list.height));
                if (next !== list.contentY) {
                    list.contentY = next;
                    event.accepted = true;
                }
            }
        }

        Column {
            id: column
            x: 20
            y: 18
            width: words.cardWidth - 40
            spacing: 0

            // The icons are anchored to the right edge rather than pushed by a spacer: one sized around
            // the English title lands the glyphs past the card at every other translation's width.
            Item {
                width: parent.width
                height: title.height

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Words")
                    color: words.tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    font.underline: titleHover.hovered

                    HoverHandler {
                        id: titleHover
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        onTapped: words.yearRequested()
                    }
                }

                // The title row is the handle rather than the card, because the card holds the list:
                // a drag inside a list is a scroll, and a handler covering it would compete with the
                // flick for the same gesture. The timer below does the moving, and the two reasons it
                // is not the handler's own signal are in ShadowCard.qml.
                DragHandler {
                    id: mover
                    target: null

                    property point grabCursor: Qt.point(0, 0)
                    property point grabWindow: Qt.point(0, 0)

                    function place() {
                        const at = Controller.cursorPos();
                        words.x = Math.round(grabWindow.x + at.x - grabCursor.x);
                        words.y = Math.round(grabWindow.y + at.y - grabCursor.y);
                    }

                    onActiveChanged: {
                        if (active) {
                            grabCursor = Controller.cursorPos();
                            grabWindow = Qt.point(words.x, words.y);
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

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6

                    // The annual record has a glyph of its own rather than living only behind the
                    // title: the title's press stays as a shortcut, but the calendar is the entry a
                    // reader can see, reach with Tab and name to a screen reader.
                    IconButton {
                        objectName: "yearButton"
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-calendar.svg"
                        color: words.tokens.faint
                        accessibleName: qsTranslate("WordsPopup", "Year in words")
                        onTapped: words.yearRequested()
                    }

                    // The export sits with the glyphs the reader already reads as "things this window
                    // does", not on a line of its own under the total: it acts on the list, and the
                    // list is what the card is.
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-export.svg"
                        color: words.tokens.faint
                        accessibleName: qsTranslate("WordsPopup", "Export words")
                        onTapped: saver.open()
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-folder.svg"
                        color: words.tokens.faint
                        accessibleName: qsTranslate("WordsPopup", "Import words")
                        onTapped: importer.open()
                    }

                    // Back to the statistics panel, which this one opens from. The panels replace
                    // rather than stack, so the way up has to be visible.
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-back.svg"
                        color: words.tokens.faint
                        accessibleName: qsTr("Back")
                        onTapped: words.backRequested()
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/ui-close.svg"
                        color: words.tokens.faint
                        accessibleName: qsTr("Close")
                        onTapped: words.visible = false
                    }
                }
            }

            // The list's own line: how many words the rows below are.
            Item {
                width: parent.width
                height: count.implicitHeight

                Text {
                    id: count
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    // The list's own count, not the all-time tally the statistics panel shows: the
                    // rows are deduplicated, so a word explained twice counts once here and twice
                    // there. A number that disagrees with the list under it reads as a bug either way.
                    text: qsTr("%1 words").arg(Controller.words.length)
                    color: words.tokens.faint
                    font.pixelSize: 12
                }

                Text {
                    anchors.left: count.right
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: words.importStatus !== ""
                    text: words.importStatus
                    color: words.importFailed ? words.tokens.danger : words.tokens.muted
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                }
            }

            Item { width: 1; height: 12 }
            Segment {
                width: parent.width
                height: 28
                labels: [qsTr("All"), qsTr("Known"), qsTr("New")]
                currentIndex: words.filter
                onPicked: (index) => words.filter = index
            }

            Item { width: 1; height: 13 }
            Rectangle { width: parent.width; height: 1; color: words.tokens.line2 }
            Item { width: 1; height: 6 }

            Item {
                width: parent.width
                // UI-SPEC.md 4.8 gives the list a *maximum* of 306px: a handful of words leaves a
                // panel sized to those words, not a panel with a hole under them. An empty list
                // keeps one line of room for the line that says how it fills; a filter with no
                // rows gets the same room rather than a card with a hole in it.
                //
                // Unfiltered, the viewport is the row count times the row height, never the
                // view's own contentHeight: with most rows not instantiated, that number is an
                // estimate the view revises as rows fold and instantiate, and a viewport bound
                // to it feeds the revisions back -- the list wobbles, and a fold mid-scroll
                // leaves the reader looking at blank while both ends of the scroll chase each
                // other (measured: contentHeight ringing between 279 and 2666 for seconds, the
                // viewport collapsing with it). The count is a fact; the estimate is not. A
                // filter keeps the tight fit: its few rows are all instantiated, so its
                // contentHeight is exact.
                height: Controller.words.length === 0 ? emptyText.implicitHeight + 6
                      : words.filterEmpty ? filteredEmptyText.implicitHeight + 6
                      : words.filter === 0 ? Math.min(306, list.count * words.rowHeight)
                      : Math.min(306, list.contentHeight)

                ListView {
                    id: list
                    width: parent.width
                    height: parent.height
                    clip: true
                    model: Controller.words
                    boundsBehavior: Flickable.StopAtBounds
                    // Every row stays instantiated: a lazily-filled view reports a contentHeight
                    // it estimates from the rows it holds, and while the fold animation runs that
                    // estimate swings wildly (measured: 691 one moment, 2666 the next, the scroll
                    // position dragged around with it and the tail of the list reading as blank).
                    // A vocabulary list is a few hundred simple rows; holding them all costs
                    // nothing next to a scroll that lies about what it holds.
                    cacheBuffer: 6000
                    // The viewport above is the count times the row height, a number the view
                    // itself never computes, so nothing in the view's own layout obliges it to
                    // build the rows: a view that first shows up already sized waits for its
                    // first rendered frame to fill in, and a reader (or a case) looking the
                    // instant the panel lands finds no rows. One synchronous layout here builds
                    // them at birth instead.
                    Component.onCompleted: forceLayout()

                    // A touchpad's momentum can leave the view past its top: contentY below
                    // zero, the first row pushed down and blank above it, and nothing recalls
                    // it -- the bounds stop a drag from crossing, but they do not bring back a
                    // view a gesture already parked across. The first word owes the reader the
                    // top, so wherever the scroll settles, that is where it returns.
                    onContentYChanged: if (!moving && contentY < 0)
                        contentY = 0
                    onMovementEnded: if (contentY < 0)
                        contentY = 0

                    delegate: WordsListRow {
                        host: words
                    }

                }

                Text {
                    id: emptyText
                    anchors.top: parent.top
                    width: parent.width
                    visible: Controller.words.length === 0
                    text: qsTr("Words you look up will appear here.")
                    color: words.tokens.faint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Text {
                    id: filteredEmptyText
                    objectName: "filteredEmptyText"
                    anchors.top: parent.top
                    width: parent.width
                    visible: words.filterEmpty
                    text: qsTr("No words in this filter.")
                    color: words.tokens.faint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Timer {
                    id: filterSettle
                    interval: words.tokens.motion.pop + 20
                    onTriggered: words.filterSettled = true
                }

                // The six-pixel scrollbar UI-SPEC.md 4.8 asks for, drawn rather than imported so the
                // module needs no widget styling. It sits in the card's right padding: a bar
                // against the edge would draw its rounded end over a verdict pill.
                Rectangle {
                    visible: list.contentHeight > list.height
                    width: 6
                    radius: 3
                    color: words.tokens.line
                    x: parent.width + 6
                    height: Math.max(24, list.height * list.height / list.contentHeight)
                    // Held inside the track rather than mapped straight off contentY. A filter
                    // change or a removal shortens the list under a contentY the Flickable leaves
                    // where it was, and the unclamped ratio then draws the thumb below the card.
                    y: {
                        if (list.contentHeight <= list.height)
                            return 0;
                        const at = list.contentY / (list.contentHeight - list.height);
                        return (list.height - height) * Math.min(1, Math.max(0, at));
                    }
                }
            }
        }

    }

    GlassHost {
        window: words
        sheet: words.glass
        dragging: mover.active
    }

    // The question this panel raises. It is a Window, so it is a surface of its own under
    // docs/QML.md section 1 rather than a block in this file -- and it is the one place the
    // pending lemma is drawn, while WordsPopup keeps owning it.
    RemovalQuestion {
        id: question
        transientParent: words
        word: words.pendingRemoval
        onAnswered: (dontAsk) => {
            if (dontAsk)
                words.askBeforeRemoving = false;
            words.removeWordAnimated(words.pendingRemoval);
        }
        // The question is the pending subject's owner, so the list lets go of it the moment the
        // question is put down -- not when its fade ends. The sentence being drawn is displayWord,
        // which the question copied when it was asked.
        onDismissed: words.pendingRemoval = ""
    }

    // Declared here, not in Main.qml: this surface shows the list, so it chooses where the list goes.
    // QtQuick.Dialogs names a file but cannot write one -- Controller.saveWords() takes the answer and
    // logs a failed write. Native on purpose: choosing a file is the system's business, unlike
    // confirming a removal.
    FileDialog {
        id: saver
        title: qsTr("Export words")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        // The scope is in the name because the scope is what the export is: the reader who
        // exports twice from two filters gets two files rather than one overwriting the other.
        currentFile: "words-" + words.scope + ".txt"
        onAccepted: Controller.saveWords(saver.selectedFile, words.scope)
    }

    FileDialog {
        id: importer
        title: qsTr("Import words")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Text files (*.txt)"), qsTr("JSON files (*.json)"), qsTr("All files (*)")]
        onAccepted: {
            const imported = Controller.importWords(importer.selectedFile);
            words.importFailed = !imported;
            words.importStatus = imported ? qsTr("Words imported") : qsTr("Import failed");
            importStatusTimer.restart();
        }
    }

    Timer {
        id: importStatusTimer
        interval: 2600
        onTriggered: words.importStatus = ""
    }
}
