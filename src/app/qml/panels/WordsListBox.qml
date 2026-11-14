pragma ComponentBehavior: Bound

import QtQuick

/**
 * The words popup's list, with everything that sizes and paces it (UI-SPEC.md 4.8): the viewport
 * capped at a whole number of rows, the fold transitions a filter switch rides, the empty states,
 * the search miss's hint, and the scrollbar. Split out of WordsPopup.qml when that file reached
 * the size cap (docs/QML.md section 1), the way RemovalQuestion and WordsListRow were before it.
 *
 * @p host is the WordsPopup window: the states the height keys on, the tokens and the row height
 * read from it, and the rows still hand their width and their removal asks to it. The view is
 * aliased as @p view for the card's wheel handler, which scrolls on the card's behalf.
 */
Item {
    id: box

    required property var host

    /// The view, exposed for the card's wheel handler and the removal fold -- both imperative,
    /// both write. Declarative reads take @p listEmpty instead: a property of this file notifies
    /// the popup, where a count read through the object alias does not.
    readonly property alias view: list
    /// Whether the view holds its height for a removal or a switch to land.
    readonly property bool holding: internalHolding
    /// Whether the view has no rows at all under the current filter and query.
    readonly property bool listEmpty: list.count === 0

    width: parent.width

    /// The popup's filter, watched so a segment change settles like one: the state the empty
    /// lines key on resets here, and a scrolled list re-reads from its first row (docs/adr/0122).
    readonly property int filter: host.filter
    onFilterChanged: {
        host.filterSettled = false;
        filterSettle.restart();
        // A scrolled list keeps the rows that stay at their old offsets and draws blank, so the
        // new list is read from its first row (docs/adr/0122).
        list.contentY = 0;
    }

    // UI-SPEC.md 4.8 gives the list a *maximum* of nine rows: a handful of words leaves a
    // panel sized to those words, not a panel with a hole under them; an empty list,
    // and a filter with no rows, keep one line of room for the line that fills it.
    //
    // The viewport is the model's row count times the row height, never the view's own
    // contentHeight -- a lazily-filled view estimates that from the rows it holds, and
    // a viewport bound to it feeds the revisions back. A decrease waits for the remove
    // transition below, so a short list does not cut the folding row out of its view.
    //
    // A query miss keeps the same one-line room, for the hint that fills it. Each waits for
    // the states it reads to settle, so a mid-fold switch draws neither line early.
    //
    // The cap is a whole number of rows: the spec's 306 px was nine 34 px rows, and a
    // raw cap against the 31 px row cuts the row that straddles it -- the row a mark
    // just appended at the list's end, every time (docs/adr/0161's list work).
    readonly property int maxViewport: Math.floor(306 / host.rowHeight) * host.rowHeight
    property int viewportRows: 0
    property bool internalHolding: false

    height: host.searchMiss ? searchMissHint.height + 6
          : host.showEmpty ? emptyText.implicitHeight + 6
          : host.filterEmpty ? filteredEmptyText.implicitHeight + 6
          : Math.min(maxViewport, viewportRows * host.rowHeight)

    Component.onCompleted: viewportRows = list.count

    ListView {
        id: list
        width: parent.width
        height: parent.height
        clip: true
        model: Controller.wordsModel
        boundsBehavior: Flickable.StopAtBounds
        // Every row stays instantiated, not for the viewport (that is the model's
        // count now) but for the width handshake: a row outside the cache has not
        // measured its word yet, and the card would widen under the reader's scroll
        // when it finally does (docs/adr/0122 carries the measurement).
        cacheBuffer: 6000
        // The viewport above is a number the view itself never computes, so nothing
        // in its own layout obliges it to build the rows: a view that first shows up
        // already sized waits for its first rendered frame to fill in, and a reader
        // (or a case) looking the instant the panel lands finds no rows. One
        // synchronous layout here builds them at birth instead.
        Component.onCompleted: forceLayout()

        // The model removes a row at once; these are what make the leave and the
        // arrival visible. A closing row shrinks to zero while the rows below move up
        // by the same amount on the same curve, so their edges meet at every frame; an
        // arriving row grows into the space the closing ones left. The model holds a
        // switch's arrivals for one token (wordsModel.swapMs), so the two never share
        // a line. The durations are the fold token, not second ones.
        //
        // Both directions ride their opacity too: a row clips its own content (the
        // cross's ring stays inside the list), so a closing height slices the word at a
        // hard edge that walks up the glyphs. Fading with the fold leaves that edge
        // nothing to cut, and the two land together -- a 180 ms fold has no second beat.
        //
        // None of it rides on the search query: a keystroke narrows in place, and a fold
        // per character would run the reader's typing through the motion token.
        add: Transition {
            enabled: box.host.searchQuery === ""
            NumberAnimation {
                property: "height"
                from: 0
                to: box.host.rowHeight
                duration: box.host.tokens.motion.pop
                easing.type: box.host.tokens.motion.easing
            }
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: box.host.tokens.motion.pop
                easing.type: box.host.tokens.motion.easing
            }
        }
        // No fold, no transition: a zero-length one is still one the view runs, and it
        // left the last row of each removal run drawn over the rows that stayed
        // (docs/adr/0122).
        remove: Transition {
            enabled: !box.host.tokens.motion.reduced && box.host.searchQuery === ""
            NumberAnimation {
                property: "height"
                to: 0
                duration: box.host.tokens.motion.pop
                easing.type: box.host.tokens.motion.easing
            }
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: box.host.tokens.motion.pop
                easing.type: box.host.tokens.motion.easing
            }
        }
        displaced: Transition {
            enabled: box.host.searchQuery === ""
            NumberAnimation {
                property: "y"
                duration: box.host.tokens.motion.pop
                easing.type: box.host.tokens.motion.easing
            }
        }

        onCountChanged: {
            // A switch holds its arrivals with no fold too, so the viewport holds.
            if (list.count > box.viewportRows
                || (box.host.tokens.motion.pop === 0 && !Controller.wordsModel.filterSwitching)) {
                box.viewportRows = list.count;
                box.internalHolding = false;
            } else {
                box.internalHolding = true;
                viewHold.restart();
            }
        }

        // A model removal shortens contentHeight without a gesture, and the view
        // does not always clamp a contentY the removals left behind -- a bulk
        // removal measured 1120 against a settled 558, and a filter 868 against 93.
        // The bottom margin is part of the scroll's range; clamping to it is what
        // keeps a fold at the end in view while the margin animation closes, and
        // settles the view after it. Checking on contentY itself catches the view's
        // own pinned position after a refill, which a contentHeight handler misses.
        // The negative half recalls a touchpad's momentum parked past the top, which
        // StopAtBounds stops from crossing but does not bring back after the gesture.
        onContentYChanged: if (!moving)
            clampScroll()
        onMovementEnded: clampScroll()
        onContentHeightChanged: clampScroll()
        onHeightChanged: clampScroll()
        onBottomMarginChanged: clampScroll()
        function clampScroll() {
            const lowest = Math.max(0, contentHeight + bottomMargin - height);
            if (contentY < 0)
                contentY = 0;
            else if (contentY > lowest)
                contentY = lowest;
        }

        delegate: WordsListRow {
            host: box.host
        }

    }

    /// Holds the viewport at the height it had before a removal, through the remove
    /// transition that closes the rows; then the count is the settled one.
    ///
    /// It outlasts the model's swap and defers while a switch runs at all, even one
    /// that started after it did: the count it would take there is the intersection's,
    /// which for two verdicts is no rows at all -- 183 px on the way to 276, a blink
    /// the reader saw.
    Timer {
        id: viewHold
        interval: Controller.wordsModel.swapMs + 20
        onTriggered: {
            if (Controller.wordsModel.filterSwitching) {
                restart();
                return;
            }
            box.viewportRows = list.count;
            box.internalHolding = false;
        }
    }

    Text {
        id: emptyText
        anchors.top: parent.top
        width: parent.width
        visible: box.host.showEmpty
        // The strings keep the context they were first extracted under: the popup is
        // where they were drawn, and one entry serves both files.
        text: qsTranslate("WordsPopup", "Words you look up will appear here.")
        color: box.host.tokens.faint
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    Text {
        id: filteredEmptyText
        objectName: "filteredEmptyText"
        anchors.top: parent.top
        width: parent.width
        visible: box.host.filterEmpty
        text: qsTranslate("WordsPopup", "No words in this filter.")
        color: box.host.tokens.faint
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }

    /// The search row's miss: the hint a hand-typed lookup leaves from (docs/adr/0176). It
    /// takes the rows' place, the way the two empty lines do, and its pill is the one press
    /// that turns a query into a request.
    WordsSearchHint {
        id: searchMissHint
        objectName: "searchMissHint"
        anchors.top: parent.top
        width: parent.width
        visible: box.host.searchMiss
        host: box.host
    }

    Timer {
        id: filterSettle
        interval: box.host.tokens.motion.pop + 20
        onTriggered: box.host.filterSettled = true
    }

    // The six-pixel scrollbar UI-SPEC.md 4.8 asks for, drawn rather than imported so the
    // module needs no widget styling. It sits in the card's right padding: a bar
    // against the edge would draw its rounded end over a verdict pill.
    Rectangle {
        visible: list.contentHeight > list.height
        width: 6
        radius: 3
        color: box.host.tokens.line
        x: parent.width + 6
        height: Math.max(24, list.height * list.height / list.contentHeight)
        // Held inside the track rather than mapped straight off contentY. A removal
        // shortens the content under a contentY the Flickable leaves where it was,
        // and the unclamped ratio then draws the thumb below the card.
        y: {
            if (list.contentHeight <= list.height)
                return 0;
            const at = list.contentY / (list.contentHeight - list.height);
            return (list.height - height) * Math.min(1, Math.max(0, at));
        }
    }
}
