pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import QtQuick.Window

/**
 * Every word the bubble has shown (UI-SPEC.md section 4.8), newest first, with a filter across the
 * top. The controller works out which day a word was last shown on, relative to today, and hands
 * over the raw date and time; the labels are drawn here, in the reader's language.
 *
 * A search row under the segment narrows the list to the words holding a hand-typed query, and a
 * query that matches nothing is the one doorway to asking the model about a word the reader typed
 * rather than pointed at (docs/adr/0176).
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
    /// lay out rather than being walked for it -- a JS loop over the model's rows reads a
    /// QVariantMap per row, a frozen window (docs/QML.md section 6). A running maximum, so the
    /// card does not narrow under the reader as rows change; the cap above bounds it.
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

    /// 0 = all, 1 = known, 2 = new. The model owns the filter, so the list only ever holds the
    /// rows this names; the segment and the export scope read it back (docs/adr/0122).
    readonly property int filter: Controller.wordsModel.filter
    /// One row's drawn height, the number the list's own sizing keys on (WordsListRow.qml). The
    /// viewport is a fact of the row count rather than of an animation's progress.
    readonly property int rowHeight: 31
    /// The filter's rows close over a motion token; until that settles, a list with no rows is
    /// still collapsing, not empty -- and a switch holds its arriving rows for a token more.
    /// The box watches the filter and settles it; this only states the result.
    property bool filterSettled: true
    readonly property bool filterEmpty: filter !== 0 && Controller.wordsModel.allCount > 0
                                        && filterSettled && !listBox.holding
                                        && !Controller.wordsModel.filterSwitching && listBox.listEmpty
                                        && searchQuery === ""
    /// The unfiltered list's line waits for the last row's remove transition too.
    readonly property bool showEmpty: Controller.wordsModel.allCount === 0 && !listBox.holding
                                      && searchQuery === ""
    /// The search row's word; the model narrows on it through the Binding below.
    readonly property string searchQuery: searchRow.query
    /// @brief The query matches no row: the miss the hint fills and Enter asks from
    ///        (docs/adr/0176). The query suspends the segment, so a miss is final the moment
    ///        the query is -- no settle or hold waits, which would blank the list while the
    ///        reader types toward a word that is not there.
    readonly property bool searchMiss: searchQuery !== "" && listBox.listEmpty
    /// With a word in the focused field, Escape clears it; the panel's close stands down rather
    /// than answering the same key ambiguously.
    readonly property bool searchHoldsEscape: searchRow.fieldFocus && searchQuery !== ""

    /// The word the question is asking about, "" when it is not up.
    property string pendingRemoval: ""

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
        // A fresh panel opens on the whole list, the way the segment does: a query left behind
        // would keep rows hidden under a list that reads as unfiltered.
        searchRow.clear();
        Controller.explanationDuty.dismissReviewNow();
    }

    /// A fresh panel opens on the whole list, the way it did when the filter lived here.
    Component.onCompleted: Controller.wordsModel.filter = 0

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

    /// @brief Take @p word out of the list, with the row closing.
    ///
    /// The model removes the row at once and the ListView's own remove transition closes it;
    /// there is no second timer and no delegate rebuild. Two geometry holds keep that
    /// transition visible: a short list's viewport waits for the fold before it shortens, and a
    /// removal at the scroll's end adds a row of bottom margin so the fold does not happen below
    /// the viewport (docs/adr/0122).
    function removeWordAnimated(word) {
        if (word === "")
            return;
        if (words.tokens.motion.pop === 0) {
            Controller.removeWord(word);
            return;
        }
        if (listBox.view.contentY + listBox.view.height >= listBox.view.contentHeight - 1)
            listBox.view.bottomMargin = words.rowHeight;
        Controller.removeWord(word);
        if (listBox.view.bottomMargin > 0)
            bottomMarginBack.restart();
    }

    /// The scroll room a removal at the end adds while its row folds. Without it the view clamps
    /// contentY to the shorter content at once, and the row closes below the viewport.
    NumberAnimation {
        id: bottomMarginBack
        target: listBox.view
        property: "bottomMargin"
        from: words.rowHeight
        to: 0
        duration: words.tokens.motion.pop
        easing.type: words.tokens.motion.easing
    }

    /// The model paces a filter's two phases by the same token the rows fold over, so whoever
    /// writes the filter -- the segment, a statistics drill-down -- gets the same move. The slack
    /// is the frame between the model's signal and the view starting the fold: without it the
    /// arriving rows could land while a leaving row still has a little closing to do.
    Binding {
        target: Controller.wordsModel
        property: "swapMs"
        value: words.tokens.motion.pop + 30
    }

    /// The model owns what the list shows; the search row's word is its query, and it narrows
    /// whatever the segment already chose.
    Binding {
        target: Controller.wordsModel
        property: "query"
        value: words.searchQuery
    }

    /// @brief The time label of a row, in the reader's language. Empty when the row has no day.
    function whenLabel(day, date, time) {
        if (day === "today")
            return qsTr("Today %1").arg(time);
        if (day === "yesterday")
            return qsTr("Yesterday %1").arg(time);
        if (day === "earlier")
            return date + " " + time;
        return "";
    }

    /// @brief Put the question down without answering it.
    function closeChild() {
        question.dismiss();
    }

    /// @brief Ask the model about the word in the search row, through the gate explainLemma()
    ///        keeps on the facade. The card anchors beside the row, in the native pixels the
    ///        other anchors speak (docs/QML.md section 4) -- not at the last review position.
    function lookUpTyped() {
        // Untrimmed: the duty trims what it receives, the way explainLemma() does.
        const word = searchRow.query;
        if (word === "")
            return;
        const ratio = Screen.devicePixelRatio || 1;
        const at = searchRow.mapToGlobal(0, 0);
        const nativeAt = SurfaceGeometry.nativeFromDipPoint(Qt.point(at.x, at.y),
                                                            Qt.point(Screen.virtualX, Screen.virtualY),
                                                            ratio);
        Controller.explainTypedWord(word, nativeAt.x, nativeAt.y,
                                    SurfaceGeometry.nativeFromDipLength(searchRow.width, ratio),
                                    SurfaceGeometry.nativeFromDipLength(searchRow.height, ratio));
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
        objectName: "wordsEscape"
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: words.visible && question.phase !== "asking" && !words.searchHoldsEscape
        onActivated: words.visible = false
    }

    /// The filter above, spelled the way exportWords() and saveWords() take it. The list and
    /// an export are the same rows seen twice, so an export follows whatever is on screen.
    readonly property var scopes: ["all", "known", "mastered", "new"]
    readonly property string scope: scopes[filter]

    /// The reader asked for the panel this one was opened from.
    signal backRequested()
    /// The title is the entry point to the annual word record.
    signal yearRequested()

    Item {
        anchors.fill: parent

        TapHandler {
            acceptedButtons: Qt.BackButton
            onTapped: words.backRequested()
        }
    }

    ShadowCard {
        id: card
        anchors.fill: parent
        color: words.tokens.glassMode === "" ? words.tokens.panel : "transparent"

        /// The list is not an answer to the question, so it takes no press while one is up -- the fade
        /// included, because the window is modal until it hides. This guard is what holds on the
        /// offscreen platform, which does not enforce modality: a press reaching the card would act
        /// on a verdict pill or a cross, deleting a second word out from under the first.
        enabled: question.phase === "hidden"

        GlassSurface {
            anchors.fill: parent
            sheet: words.glass
            mode: words.tokens.glassMode
            radius: card.radius
        }

        // The list is the only scroller, but the title, the filters and the card's right padding
        // are part of the same page: a wheel anywhere on the card scrolls the list. The handler
        // sits on the card, behind the view in delivery order, so a wheel over the list itself is
        // still the ListView's; this only answers where that view declined the event.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                if (listBox.view.contentHeight <= listBox.view.height)
                    return;
                let step = event.pixelDelta.y;
                if (step === 0)
                    step = -event.angleDelta.y / 120 * 72;
                const next = Math.max(0, Math.min(listBox.view.contentY + step,
                                                  listBox.view.contentHeight - listBox.view.height));
                if (next !== listBox.view.contentY) {
                    listBox.view.contentY = next;
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
                    text: qsTr("%1 words").arg(Controller.wordsModel.allCount)
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
                labels: [qsTr("All"), qsTr("Known"), qsTr("Mastered"), qsTr("New")]
                currentIndex: words.filter
                onPicked: (index) => Controller.wordsModel.filter = index
                // A query suspends the segment: the search spans the whole vocabulary, and the
                // reader's slice comes back when the query clears (docs/adr/0176).
                enabled: words.searchQuery === ""
                opacity: words.searchQuery === "" ? 1 : 0.45
            }

            Item { width: 1; height: 10 }
            WordsSearchRow {
                id: searchRow
                host: words
            }

            Item { width: 1; height: 13 }
            Rectangle { width: parent.width; height: 1; color: words.tokens.line2 }
            Item { width: 1; height: 6 }

            WordsListBox {
                id: listBox
                host: words
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
    WordsExportDialog {
        id: saver
        scope: words.scope
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
