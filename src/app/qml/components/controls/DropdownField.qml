pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The collapsed dropdown from UI.md 4.4, and the grouped list it opens.
 *
 * The list is a window of its own rather than a child item: the settings panel is exactly as
 * tall as its contents, so a list drawn inside it would be clipped the moment it opened.
 *
 * Two shapes share the one component because they share the list. A field the reader only picks
 * from shows the current option's label, and the whole field opens the list -- and puts it down
 * again on a second press. An `editable` one
 * shows what was typed instead, and there only the chevron opens the list -- the rest of the
 * field is the reader's cursor, which is what makes a model name the service never listed
 * reachable by hand (UI.md 4.4, docs/adr/0017).
 *
 * Typing in the editable shape is also how the list is searched: a keystroke narrows the rows to
 * the options whose `value` contains what was typed, and brings the list up by itself, so the
 * reader never presses the chevron to look. It comes down on three things -- the text becoming one
 * of the ids in the list, a row being picked, and a field the reader has emptied -- and it never
 * comes up at all for a search that matches nothing, because an empty card is not a dropdown. The
 * text is never rewritten: a private deployment name the catalogue has never heard of is the reason
 * this field is typeable at all, so an exact match closes the list and changes nothing else.
 *
 * That second path up the list deliberately does not emit `listOpened`, which means "the reader
 * asked for the list" and is wired to a refresh. One request per keystroke is not what that
 * signal says.
 *
 * A chevron with no list behind it is dimmed and takes no press, the way a switch that cannot be
 * moved is: a service that never answered has no models to offer, and the field is still the
 * reader's to type in. It is greyed rather than taken away because the shape of this control is
 * the thing the reader reads as "the setting took" -- one that comes and goes is worse than one
 * that says nothing is there (docs/adr/0017).
 */
Item {
    id: root

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    /// The controller's level list: {value, label, group, note}.
    property var options: []
    property var currentValue: 0

    /// Whether the value can be typed as well as picked.
    property bool editable: false

    /// What the reader has typed, trimmed, and is searching the list by. Blank is no filter.
    property string filterText: ""

    /// A window component that previews an option under the pointer, or null for none. The
    /// window must expose `showOption(option)`, and a `shadowMargin` it keeps its card inset by;
    /// DropdownField creates it on the first hover, puts it beside the list on the side with
    /// room, and keeps it level with the hovered row.
    property Component optionPreview: null

    /// Whether a row draws its option's `note` at its right end. Off where the note is a sentence
    /// rather than a word: it would be elided to noise beside the label, and an explanation the
    /// reader cannot read is not a note. Such a field hands the note to `optionPreview` instead.
    property bool notesInRows: true

    /// The row under the pointer, by its index in `shown`, or -1 for none.
    property int hoveredIndex: -1
    /// The window an option hovers into, created on the first hover and owned by this field.
    property var optionPreviewWindow: null

    signal picked(var value)
    signal listOpened()
    /// A row the pointer has entered, with its option. A page whose preview is not a window of
    /// its own -- the font field raises the settings panel's bubble -- answers this instead of
    /// setting `optionPreview`.
    signal optionHovered(var option)
    /// The pointer has left the row it was on, or the list has gone.
    signal optionUnhovered()
    /// A value was typed and the reader left the field. Only ever emitted when `editable`.
    signal edited(string text)

    implicitHeight: 35

    readonly property var current: {
        for (let i = 0; i < options.length; ++i)
            if (options[i].value === currentValue)
                return options[i];
        return null;
    }

    /// The rows the list is drawn over: the whole `options` when nothing is being searched for,
    /// otherwise the ones whose value contains the search.
    ///
    /// A substring test, not a subsequence one. A subsequence match would offer ids that share no
    /// run of characters with what was typed, which reads as a filter that is broken rather than
    /// as one that found nothing. Matching on `value` rather than on `label` because in this list
    /// the two are the same string, and `value` is the one the field takes.
    readonly property var shown: {
        const needle = filterText.trim().toLowerCase();
        if (needle.length === 0)
            return options;
        const matched = [];
        for (let i = 0; i < options.length; ++i)
            if (String(options[i].value).trim().toLowerCase().indexOf(needle) >= 0)
                matched.push(options[i]);
        return matched;
    }

    readonly property int rowHeight: 29

    /// Whether there is a list to open at all.
    readonly property bool hasOptions: options.length > 0

    /// The open list's card, and the field it hangs from, as one band in screen coordinates -- null
    /// when the list is down. main.qml's outside-press rule reads it: a press on the list belongs to
    /// the list, and so does one on its field. Claimed by nobody, the rule puts the list down a beat
    /// before the field's own tap reads whether it is up, and the second press opens it again instead
    /// of closing it. The two rectangles share their width and meet at the field's bottom edge, so
    /// their union covers nothing but this control.
    readonly property var openChildRect: {
        if (!list.visible)
            return null;
        const card = Qt.rect(list.x + list.shadowMargin, list.y + list.shadowMargin,
                             list.width - 2 * list.shadowMargin, list.height - 2 * list.shadowMargin);
        const at = button.mapToItem(null, 0, 0);
        const originX = Window.window ? Window.window.x : 0;
        const originY = Window.window ? Window.window.y : 0;
        const field = Qt.rect(originX + at.x, originY + at.y, button.width, button.height);
        const left = Math.min(card.x, field.x);
        const top = Math.min(card.y, field.y);
        return Qt.rect(left, top,
                       Math.max(card.x + card.width, field.x + field.width) - left,
                       Math.max(card.y + card.height, field.y + field.height) - top);
    }

    /// @brief Take the list down. The panel calls this when it hides itself, since the list is
    ///        a window of its own and would otherwise outlive its owner.
    ///
    /// The search goes with it: whatever the reader had typed was a search, and a chevron pressed
    /// afterwards has to open the whole list again rather than the fragment they were reading.
    function closeList() {
        list.visible = false;
        root.filterText = "";
        root.hideOptionPreview();
    }

    /// Put the list's window under the field, in screen coordinates.
    ///
    /// A page that scrolls, or a panel that is dragged, moves the field without moving the list's window, so this is run
    /// again while the list is up (see the timer below). Where the list opens and where it is kept are the same place.
    function placeList() {
        // Where the field's bottom edge is, in screen coordinates.
        const point = button.mapToItem(null, 0, button.height + 6);
        const originX = Window.window ? Window.window.x : 0;
        const originY = Window.window ? Window.window.y : 0;
        // The list's window is shadowMargin bigger than its card on every side, so the window
        // has to be pulled back by that much for the card to land under the field. Placed
        // window-edge to field-edge it sat 26px right and 26px low.
        list.x = originX + point.x - list.shadowMargin;
        list.y = originY + point.y - list.shadowMargin;
    }

    // ponytail: polls every 16 ms while the list is up, instead of wiring each page's scroll position through to here. A
    // scroll signal is exact and costs nothing, but every page with a dropdown would have to forward it, which is how the
    // pet page missed its list. Upgrade to a scroll signal if this shows up in frame times.
    Timer {
        interval: 16
        repeat: true
        running: list.visible
        onTriggered: {
            root.placeList();
            // The row can move under the list's own scrolling while the pointer stays on it, so
            // the preview follows the row on the same beat the list follows its field.
            if (root.optionPreviewWindow !== null && root.optionPreviewWindow.visible)
                root.placeOptionPreview();
        }
    }

    function openList() {
        // The search goes, because this is the reader asking for the list outright rather than
        // narrowing it. Leaving the filter on would answer a deliberate press with whatever the
        // search had left -- and with nothing at all, if it had matched nothing, which is the one
        // thing the search path never shows.
        root.filterText = "";
        root.placeList();
        list.visible = true;
        listOpened();
    }

    /// @brief A press on the field's own button: the list comes up, or goes down if it is up.
    function toggleList() {
        if (list.visible)
            root.closeList();
        else
            root.openList();
    }

    /// @brief Report a row the pointer has entered or left, which is the whole preview gesture.
    /// @param index The row's index in `shown`.
    /// @param inside Whether the pointer is now on it.
    function rowHovered(index, inside) {
        if (inside) {
            root.hoveredIndex = index;
            previewSettle.stop();
            if (root.optionPreview !== null)
                root.showOptionPreview();
            root.optionHovered(root.shown[index]);
        } else if (root.hoveredIndex === index) {
            // Crossing from one row to the next enters the next before the last reports its
            // exit; the beat lets the new row take the window over instead of dropping it.
            previewSettle.restart();
        }
    }

    /// @brief Raise the option preview on the hovered row, creating its window on first use.
    function showOptionPreview() {
        const option = root.shown[root.hoveredIndex];
        if (!option)
            return;
        if (root.optionPreviewWindow === null) {
            root.optionPreviewWindow = root.optionPreview.createObject(null);
            if (root.optionPreviewWindow === null)
                return;
        }
        root.optionPreviewWindow.showOption(option);
        // A preview whose data could not be read says so and stays down, rather than standing
        // empty beside the list (the vocabulary card when data/preview.json has no words).
        if (root.optionPreviewWindow.hasContent === false) {
            root.optionPreviewWindow.visible = false;
            return;
        }
        root.placeOptionPreview();
        // The row can be gone by the time placement looks for it, which put the window down.
        if (root.hoveredIndex === -1)
            return;
        // One raise for the window's whole life, not one per row: it is topmost already, and a
        // raise per hover is a native call the list does not need.
        if (!root.optionPreviewWindow.visible) {
            root.optionPreviewWindow.visible = true;
            root.optionPreviewWindow.raise();
        }
    }

    /// @brief Put the preview beside the list's card, on the side with room, level with the row.
    ///
    /// The card clears the list by 8 px and the screen by the same margin every surface keeps; the
    /// window's own transparent shadow margin is subtracted from both so that it is the card, not
    /// the window, the reader sees beside the list. Right is tried first, then left, then the clamp
    /// -- the same order the settings preview takes (SettingsPopup.qml's previewX).
    function placeOptionPreview() {
        const preview = root.optionPreviewWindow;
        if (preview === null)
            return;
        const row = listColumn.itemAtIndex(root.hoveredIndex);
        if (row === null) {
            // The row scrolled out from under the pointer, so the window has nothing to point at.
            root.hideOptionPreview();
            return;
        }

        const shadow = preview.shadowMargin === undefined ? 0 : preview.shadowMargin;
        const width = preview.width;
        const height = preview.height;
        const gap = 8;
        const margin = 8;
        const area = root.listWorkArea();

        const cardX = list.x + list.shadowMargin;
        const cardWidth = list.width - 2 * list.shadowMargin;
        const right = cardX + cardWidth + gap - shadow;
        const left = cardX - gap - width + shadow;
        const x = right + width - shadow <= area.right - margin ? right : left;

        const at = row.mapToItem(null, 0, 0);
        const center = list.y + at.y + row.height / 2;
        preview.x = Math.round(root.clamp(x, area.left + margin - shadow,
                                          area.right - margin - width + shadow));
        preview.y = Math.round(root.clamp(center - height / 2,
                                          area.top + margin - shadow,
                                          area.bottom - margin - height + shadow));
    }

    /// @brief Put the option preview down, and forget the row it pointed at.
    function hideOptionPreview() {
        previewSettle.stop();
        const hadRow = root.hoveredIndex !== -1;
        root.hoveredIndex = -1;
        if (root.optionPreviewWindow !== null)
            root.optionPreviewWindow.visible = false;
        if (hadRow)
            root.optionUnhovered();
    }

    /// @return The work area of the screen the list is on, in virtual DIPs: asked of Qt by device
    ///         name, and the screen's own geometry where it has none.
    function listWorkArea() {
        // qmllint disable missing-property
        const screen = list.screen;
        const area = Controller.screenWorkArea(screen.name || "");
        if (area.width > 0)
            return {left: area.x, top: area.y, right: area.x + area.width, bottom: area.y + area.height};
        return {left: screen.virtualX, top: screen.virtualY,
                right: screen.virtualX + screen.width, bottom: screen.virtualY + screen.height};
        // qmllint enable missing-property
    }

    /// @return @p value held inside [@p low, @p high]; an empty range collapses to its low end.
    function clamp(value, low, high) {
        return Math.max(low, Math.min(value, Math.max(low, high)));
    }

    /// @brief Narrow the list to the text the reader has just typed, and show or hide it to match.
    ///
    /// Called from `textEdited` and never from `textChanged`: the latter also fires when
    /// `onEditingFinished` puts the binding back on `currentValue`, which writes the owner's answer
    /// into the input rather than the reader typing it, and filtering there would bring the list
    /// back up as they leave the field.
    ///
    /// The text arrives as an argument rather than being read off the input, so the whole decision
    /// is reachable from a case without a keystroke -- this QtTest cannot deliver one into a
    /// TextInput, which is why the typing cases call this directly. The one line that connection
    /// leaves unpinned is `onTextEdited` below.
    ///
    /// The text is read, never written. Rewriting it to the catalogue's spelling would move the
    /// caret mid-word and would throw away the private deployment name this field exists for.
    function filterTyped(text) {
        root.filterText = text.trim();
        const typed = root.filterText;
        // A field the reader has emptied is searching for nothing, so there is nothing to show:
        // the whole list is the chevron's business, not an empty query's. It also keeps the rule
        // even -- every keystroke is judged on its own, backspaces included.
        if (typed.length === 0) {
            list.visible = false;
            return;
        }
        if (root.shown.length === 0) {
            // Nothing matches: no list. An empty card is not a dropdown, and one that stays up
            // after the reader deletes their way out of it is worse than never having opened.
            list.visible = false;
            return;
        }
        // The text is an id in the list already, so there is nothing left to choose from it. The
        // case is here rather than at the end of typing because the reader can get there by
        // deleting as well as by typing.
        const needle = typed.toLowerCase();
        for (let i = 0; i < root.shown.length; ++i) {
            if (String(root.shown[i].value).trim().toLowerCase() === needle) {
                list.visible = false;
                return;
            }
        }
        root.placeList();
        list.visible = true;
    }

    Rectangle {
        id: button
        anchors.fill: parent
        radius: root.tokens.radiusField
        color: root.tokens.panel2
        border.width: 1
        border.color: root.editable && input.activeFocus ? root.tokens.ink : root.tokens.line
        scale: fieldPress.pressed || chevronPress.pressed ? 0.97 : 1.0
        Behavior on scale {
            NumberAnimation {
                duration: root.tokens.motion.press
                easing.type: root.tokens.motion.easing
            }
        }

        Text {
            id: label
            visible: !root.editable
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.right: chevron.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.current ? root.current.label : ""
            // Labels can come from a package, which must read as plain text (docs/pet-package-format.md, section 7).
            textFormat: Text.PlainText
            color: root.tokens.text
            font.pixelSize: 13
            elide: Text.ElideRight
        }

        // The typed value. Its binding is destroyed by the first keystroke, as TextInput always
        // does, and put back once the reader leaves the field so the controller's answer -- which
        // trims -- is what the field goes on showing.
        TextInput {
            id: input
            objectName: "fieldInput"
            visible: root.editable
            anchors.left: parent.left
            anchors.leftMargin: 11
            anchors.right: chevron.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            verticalAlignment: TextInput.AlignVCenter
            text: root.currentValue
            color: root.tokens.text
            font.pixelSize: 13
            selectByMouse: true
            onTextEdited: root.filterTyped(input.text)
            onEditingFinished: {
                if (text !== root.currentValue)
                    root.edited(text);
                text = Qt.binding(() => root.currentValue);
                root.filterText = "";
            }
        }

        Icon {
            id: chevron
            anchors.right: parent.right
            anchors.rightMargin: 11
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 14
            source: "qrc:/icons/ui-chevron-down.svg"
            color: root.tokens.faint
            // Dimmed on the same rule Switch dims by, and for the same reason: there is nothing
            // behind it to open.
            opacity: root.hasOptions ? 1.0 : 0.45
        }

        // Only the chevron is a target once the field is editable; the rest of it is where the
        // reader types. Wide enough to be hit without aiming: a 14px glyph is not.
        Item {
            visible: root.editable
            anchors.right: chevron.right
            anchors.rightMargin: -8
            anchors.verticalCenter: parent.verticalCenter
            width: 30
            height: parent.height - 4

            HoverHandler { cursorShape: root.hasOptions ? Qt.PointingHandCursor : Qt.ArrowCursor }
            TapHandler {
                id: chevronPress
                enabled: root.hasOptions
                onTapped: root.toggleList()
            }
        }

        HoverHandler {
            enabled: !root.editable
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            id: fieldPress
            enabled: !root.editable
            onTapped: root.toggleList()
        }
    }

    /// Leaving a row is held a beat before the preview goes: crossing from one option to the next
    /// can have the next one arrive before the last has reported its exit, and dropping the
    /// window in between reads as a flicker (Bubble.qml keeps the same guard over its own hover).
    Timer {
        id: previewSettle
        interval: 120
        onTriggered: root.hideOptionPreview()
    }

    Component.onDestruction: {
        root.closeList();
        if (root.optionPreviewWindow !== null)
            root.optionPreviewWindow.destroy();
    }

    Window {
        id: list

        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
        visible: false
        onVisibleChanged: {
            // The preview belongs to the open list: a window left standing over the desktop
            // would be a preview of a choice the reader can no longer see.
            if (!visible)
                root.hideOptionPreview();
        }
        /// The list is its own window, so it takes the colour table of the window the field sits in.
        readonly property var tokens: root.tokens

        readonly property int shadowMargin: 26
        /// Room the rows leave inside the card, on every side.
        readonly property int listPadding: 10
        /// How tall the rows may get before they scroll. Ten of them: a service that resells
        /// other people's models answers with several hundred ids, and the window has to stay on
        /// the screen with room for the shadow around it.
        readonly property int maximumListHeight: 290

        width: root.width + 2 * shadowMargin
        height: listColumn.height + 2 * listPadding + 2 * shadowMargin

        ShadowCard {
            anchors.fill: parent
            radius: list.tokens.radiusGroup
            color: list.tokens.popup

            // A view rather than a column: the delegates are built as they come into sight, so a
            // list of several hundred costs the ten that are drawn and no more.
            //
            // Placed by hand rather than by anchors: `ShadowCard` hands its children to the card
            // itself, so this item's origin is the card -- not the window, which is a shadow
            // margin wider on every side -- and the insets below are measured from the card.
            // Counting the margin in as well put every row 26px right and 26px low of the field
            // it belongs to.
            ListView {
                id: listColumn
                x: list.listPadding
                y: list.listPadding
                width: list.width - 2 * (list.shadowMargin + list.listPadding)
                height: Math.min(contentHeight, list.maximumListHeight)
                clip: true
                model: root.shown
                boundsBehavior: Flickable.StopAtBounds

                delegate: Column {
                    id: group
                    required property int index
                    required property var modelData

                    width: listColumn.width

                    // A rule between groups, never before the first one. Read against
                    // `shown`, the list the view is built over, not `options`: while a search is
                    // narrowing the list the row above this one on screen is not options[index-1],
                    // and a rule drawn from that would sit against a row nobody can see.
                    Rectangle {
                        visible: group.index > 0
                                 && group.modelData.group !== root.shown[group.index - 1].group
                        width: parent.width
                        height: 1
                        color: root.tokens.line2
                        anchors.margins: 5
                    }

                    Rectangle {
                        width: parent.width
                        height: root.rowHeight
                        radius: 8
                        color: group.modelData.value === root.currentValue ? root.tokens.ink
                             : rowPress.pressed ? root.tokens.line
                             : rowHover.hovered ? root.tokens.panel2
                             : "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 11
                            anchors.verticalCenter: parent.verticalCenter
                            text: group.modelData.label
                            textFormat: Text.PlainText
                            color: group.modelData.value === root.currentValue ? root.tokens.on : root.tokens.text
                            font.pixelSize: 13
                        }

                        Text {
                            visible: root.notesInRows && (group.modelData.note || "") !== ""
                            anchors.right: parent.right
                            anchors.rightMargin: 11
                            anchors.verticalCenter: parent.verticalCenter
                            text: group.modelData.note || ""
                            textFormat: Text.PlainText
                            color: group.modelData.value === root.currentValue ? Qt.rgba(1, 1, 1, 0.62) : root.tokens.faint
                            font.pixelSize: 11
                        }

                        HoverHandler {
                            id: rowHover
                            cursorShape: Qt.PointingHandCursor
                            onHoveredChanged: root.rowHovered(group.index, rowHover.hovered)
                        }
                        TapHandler {
                            id: rowPress
                            onTapped: {
                                root.picked(group.modelData.value);
                                // Through closeList(), so a search that was narrowing the list
                                // is dropped with it: the next chevron opens the whole list
                                // again rather than the fragment the reader just picked from.
                                root.closeList();
                            }
                        }
                    }
                }
            }

            // The scrollbar, drawn rather than imported so the module needs no widget styling of
            // its own -- the same six pixels UI.md 4.8 asks of the word list. Without it a list of
            // three hundred looks like a list of ten.
            Rectangle {
                visible: listColumn.contentHeight > listColumn.height
                width: 6
                radius: 3
                color: list.tokens.line
                x: listColumn.x + listColumn.width + 2
                height: Math.max(24, listColumn.height * listColumn.height / listColumn.contentHeight)
                // Held inside the track rather than mapped straight off contentY, as the word list's
                // bar is: a search shortens the list under a contentY the Flickable leaves where it
                // was, and the unclamped ratio draws the thumb below the card.
                y: {
                    if (listColumn.contentHeight <= listColumn.height)
                        return listColumn.y;
                    const at = listColumn.contentY / (listColumn.contentHeight - listColumn.height);
                    return listColumn.y + (listColumn.height - height) * Math.min(1, Math.max(0, at));
                }
            }
        }

        // The window is a shadow margin taller than its card, and the top of that band lies across
        // the field that opened the list. Windows hit-tests the rectangle rather than the pixels, so
        // a press on the field's lower half lands on this window with nothing here to press it, and
        // is dropped. It closes the list, which is what the same press on the field means.
        Item {
            x: list.shadowMargin
            width: list.width - 2 * list.shadowMargin
            height: list.shadowMargin

            TapHandler { onTapped: root.closeList() }
        }
    }
}
