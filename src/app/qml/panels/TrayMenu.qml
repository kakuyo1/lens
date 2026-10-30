pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import "../util/Format.js" as Format

/**
 * The tray menu (UI-SPEC.md section 4.2), drawn rather than handed to QMenu.
 *
 * A surface like the others: a frameless window with a card in it. The language list is a second
 * card unfolding to the left of its row, which is why the window is wider than the menu card: the
 * extra strip stays empty, and the window keeps one width whether the list is out or not.
 */
Window {
    id: menu
    objectName: "tray"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("tray")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: menu.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false
    /// Set by the panel that replaces this one: the card fades out in place, and the window goes when it has.
    property bool leaving: false

    readonly property int shadowMargin: 26
    readonly property int padding: 6
    readonly property int cardWidth: 240
    readonly property int listWidth: 104
    readonly property int gap: 8
    readonly property int languageRowOffset: 34 + 4

    /// Where the menu card's edges sit inside the window: the list takes the left strip.
    readonly property int cardLeft: shadowMargin + listWidth + gap
    readonly property int listLeft: cardLeft - gap - listWidth

    width: cardLeft + cardWidth + shadowMargin
    readonly property real bodyHeight: bodyLoader.implicitHeight
    /// The menu card's own height. The window is this while the language list is down, and the list's
    /// own bounds when it reaches past it: the list unfolds below its row, the catalog sets the rows.
    readonly property real cardHeight: bodyHeight + 2 * padding
    height: Math.max(cardHeight, languageList.y + languageList.height) + 2 * shadowMargin

    /// True while a selection is captured at all; the state line's dot and label read it.
    readonly property bool capturing: Controller.settings.selectionCapture
    /// Whether the day's budget has stopped lookups; the first row becomes the budget shortcut.
    readonly property bool budgetExhausted: Controller.costDuty.stats.budgetExhausted === true
    /// Whether a key is stored. Without one nothing can be looked up, so the first row says so (docs/adr/0144).
    readonly property bool hasApiKey: Controller.settings.hasApiKey === true
    /// Whether the first row is a button: while no key is stored, or the budget is spent, it opens the page that fixes it.
    readonly property bool stateActionable: !menu.hasApiKey || menu.budgetExhausted
    /// The label an in-flight explanation carries; empty when nothing is.
    readonly property string busyLabel: Controller.busyLabel

    /// The state line, straight off the controller: whether a key is stored, the day's budget gate, the label an
    /// in-flight explanation carries, and the capture switch above. stateLineFor() puts them in the tray icon's own
    /// priority order (tray_state.h), with the missing key ahead of all of them.
    readonly property var stateLine: stateLineFor(menu.hasApiKey, menu.budgetExhausted, menu.busyLabel, menu.capturing)

    /// @return Whether the product mark carries the busy corner dot. The tray icon draws the
    ///         same dot for the same state; an exhausted budget outranks it (tray_state.h).
    function busyDotVisible(exhausted, busyLabel) {
        return !exhausted && busyLabel !== "";
    }

    /// @return The state line's mark colour and text for one readout.
    ///
    /// The tray icon's four states (tray_state.h) drawn in words. A missing key outranks them all: nothing can
    /// be looked up, so the budget and the busy line say nothing the reader can act on. An exhausted budget
    /// outranks the rest -- capture gates pause with it, so the mode name would lie; an in-flight explanation
    /// outranks the mode name for the same reason; otherwise the line says whether capture is on and what mode
    /// it is in. A function of its inputs so a test can drive the mapping without spending money.
    function stateLineFor(hasKey, exhausted, busyLabel, capturing) {
        if (!hasKey)
            return { mark: menu.tokens.warn, text: qsTr("No API key yet") };
        if (exhausted)
            return { mark: menu.tokens.warn, text: qsTr("Today's budget is used up") };
        if (busyLabel !== "")
            return { mark: menu.tokens.ok, text: busyLabel };
        return { mark: capturing ? menu.tokens.ok : menu.tokens.faint,
                 text: capturing ? Controller.modeLabel : qsTr("Selection capture is off") };
    }

    /// The first row's press: the API page while no key is stored, the model service page (its budget block) while the budget is spent.
    function openStateSettings() {
        if (!menu.hasApiKey)
            menu.apiKeyRequested();
        else
            menu.budgetRequested();
    }

    /// The interface language, and the languages to offer. Both are the catalog's list, the same four
    /// the explanation language is chosen from, so an added language needs no second table in step.
    readonly property string language: Controller.settings.uiLanguage
    readonly property var languages: Controller.settings.languages || []
    readonly property string languageName: {
        for (let i = 0; i < languages.length; ++i)
            if (languages[i].value === language)
                return languages[i].label;
        return language;
    }

    property bool listVisible: false

    /// The menu rows are only needed after the tray menu is first opened.
    property bool contentActive: false

    /// The two panels the menu opens. Placement is SurfaceHost.qml's business, so the menu asks rather
    /// than calling into the file that instantiated it: its `root` id resolves at runtime, unchecked.
    signal statsRequested()
    signal settingsRequested()
    /// The first row was pressed with no key stored; the settings panel opens on the page the key is typed into.
    signal apiKeyRequested()
    /// The budget row was pressed; the settings panel opens on the model service page, where the budget is.
    signal budgetRequested()

    /// The day's tally for the statistics row, in the shape the tray tooltip uses. The count is
    /// pop events, so the unit is lookups (ADR 0104): "words" is the deduplicated vocabulary.
    readonly property string todayFigures: Controller.costDuty.stats.todayPops + " " + qsTr("lookups")
        + " · " + Format.formatMoney(Controller.costDuty.stats.currency, Controller.costDuty.stats.todayCost)

    /// What counts as a press on this surface, in screen coordinates: the card, plus the
    /// language list while it is out. main.qml's outside-press rule reads it.
    readonly property int hitLeft: listVisible ? menu.x + listLeft : menu.x + cardLeft
    readonly property int hitRight: menu.x + cardLeft + cardWidth
    readonly property rect hitRect: {
        const top = menu.y + shadowMargin;
        // The card's own bottom, read from the card rather than from the window: the window is
        // taller than the card while the language list hangs past it.
        const ownBottom = top + menu.cardHeight;
        const listBottom = menu.y + languageList.y + languageList.height - shadowMargin;
        const bottom = listVisible ? Math.max(ownBottom, listBottom) : ownBottom;
        return Qt.rect(hitLeft, top, hitRight - hitLeft, bottom - top);
    }

    ShadowCard {
        id: card
        x: menu.cardLeft - menu.shadowMargin
        y: 0
        width: menu.cardWidth + 2 * menu.shadowMargin
        height: menu.cardHeight + 2 * menu.shadowMargin
        radius: menu.tokens.radiusMenu
        color: menu.tokens.glassMode === "" ? menu.tokens.panel : "transparent"

        GlassSurface {
            anchors.fill: parent
            sheet: menu.glass
            mode: menu.tokens.glassMode
            radius: card.radius
        }

        Loader {
            id: bodyLoader
            x: menu.padding
            y: menu.padding
            width: menu.cardWidth - 2 * menu.padding
            active: menu.contentActive
            sourceComponent: Column {
                id: body
                width: menu.cardWidth - 2 * menu.padding
                spacing: 0

                // The state line: the product mark carries the state -- ok while the app is
                // working, warn when no key is stored or the day's budget is spent, faint when capture is off -- and
                // the text names what the app is doing, in the tray icon's priority order
                // (tray_state.h). The mark stands where UI-SPEC.md section 4.2 draws an anonymous
                // dot: it says the same thing, and whose app this is.
                //
                // Its inset and size are the rows' beneath it (11 px, 15 px), so the mark sits in
                // the same column as the statistics, language and settings glyphs instead of
                // starting a second one.
                Item {
                    id: stateRow
                    width: parent.width
                    height: 34

                    // While no key is stored, or the budget is spent, this row is the way to the page that fixes it;
                    // at rest it is a status line, not a button (UI-SPEC 4.2).
                    Accessible.role: menu.stateActionable ? Accessible.Button : Accessible.StaticText
                    Accessible.name: menu.stateLine.text
                    Accessible.onPressAction: if (menu.stateActionable) menu.openStateSettings()

                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 2
                        anchors.bottomMargin: 2
                        radius: 8
                        color: stateHover.hovered && menu.stateActionable ? menu.tokens.panel2 : "transparent"
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: menu.tokens.line2
                    }

                    Icon {
                        id: mark
                        anchors.left: parent.left
                        anchors.leftMargin: 11
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/lens.svg"
                        color: menu.stateLine.mark
                    }

                    // The busy tray icon's corner dot, drawn at the mark's own corner.
                    Rectangle {
                        x: mark.x + mark.width - 3
                        y: mark.y
                        width: 3
                        height: 3
                        radius: 1.5
                        color: menu.tokens.ok
                        visible: menu.busyDotVisible(menu.budgetExhausted, menu.busyLabel)
                    }

                    Text {
                        anchors.left: mark.right
                        anchors.leftMargin: 9
                        anchors.right: parent.right
                        anchors.rightMargin: 11
                        anchors.verticalCenter: parent.verticalCenter
                        text: menu.stateLine.text
                        color: menu.tokens.text
                        font.pixelSize: 12
                        font.weight: Font.Bold
                        elide: Text.ElideRight
                    }

                    HoverHandler {
                        id: stateHover
                        enabled: menu.stateActionable
                        cursorShape: Qt.PointingHandCursor
                    }
                    TapHandler {
                        enabled: menu.stateActionable
                        onTapped: menu.openStateSettings()
                    }
                }

                Item { width: 1; height: 4 }

                MenuRow {
                    id: statsRow
                    width: parent.width
                    source: "qrc:/icons/ui-stats.svg"
                    label: qsTr("Statistics")
                    note: menu.todayFigures
                    // Not hidden here: the statistics panel that opens hands the menu over, and the menu fades out.
                    onPicked: menu.statsRequested()
                }

                MenuRow {
                    id: languageRow
                    width: parent.width
                    source: "qrc:/icons/ui-language.svg"
                    label: qsTr("Language")
                    note: menu.languageName

                    // The list unfolds on hover and folds again a moment after the pointer leaves.
                    // The moment is what lets the pointer cross the gap between the two cards.
                    onHoveredChanged: {
                        if (hovered) {
                            listHide.stop();
                            menu.listVisible = true;
                        } else {
                            listHide.restart();
                        }
                    }
                    // A press on the row is another way to unfold the list; the menu itself stays
                    // pointer-first, so the row and its options carry screen-reader names only.
                    onPicked: menu.listVisible = !menu.listVisible
                }

                MenuRow {
                    width: parent.width
                    source: "qrc:/icons/ui-settings.svg"
                    label: qsTr("Settings")
                    // Not hidden here: the settings panel that opens hands the menu over, and the menu fades out.
                    onPicked: menu.settingsRequested()
                }

                Rectangle {
                    x: 2
                    width: parent.width - 4
                    height: 1
                    color: menu.tokens.line2
                }

                MenuRow {
                    width: parent.width
                    source: "qrc:/icons/ui-quit.svg"
                    label: qsTr("Quit")
                    danger: true
                    onPicked: Qt.quit()
                }
            }
        }
    }

    GlassHost {
        window: menu
        sheet: menu.glass
        dragging: card.dragging
    }

    Loader {
        id: languageList
        x: menu.listLeft - menu.shadowMargin
        y: menu.shadowMargin + menu.padding + menu.languageRowOffset - 6
        width: menu.listWidth + 2 * menu.shadowMargin
        height: menu.listVisible ? menu.languages.length * 28 + 10 + 2 * menu.shadowMargin : 0
        active: menu.listVisible
        visible: active
        sourceComponent: ShadowCard {
            id: languageCard
            width: menu.listWidth + 2 * menu.shadowMargin
            height: listColumn.implicitHeight + 10 + 2 * menu.shadowMargin
            radius: menu.tokens.radiusGroup
            color: menu.tokens.glassMode === "" ? menu.tokens.panel : "transparent"
            // The list is built by this loader only once it is meant to be out, so the window is
            // already visible and ShadowCard's own rule has nothing to animate. The loader's status
            // is the flip instead: Loading while the card is being built, Ready once it is done.
            shown: languageList.status === Loader.Ready && !menu.leaving

            GlassSurface {
                anchors.fill: parent
                sheet: menu.glass
                mode: menu.tokens.glassMode
                radius: languageCard.radius
            }

            Column {
                id: listColumn
                x: 5
                y: 5
                width: menu.listWidth - 10

                Repeater {
                    model: menu.languages

                    Rectangle {
                        id: option
                        required property var modelData

                        function choose() {
                            Controller.setSetting("uiLanguage", option.modelData.value);
                            menu.visible = false;
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: option.modelData.label
                        Accessible.onPressAction: option.choose()

                        width: listColumn.width
                        height: 28
                        radius: 8
                        color: modelData.value === menu.language ? menu.tokens.ink
                             : rowPress.pressed ? menu.tokens.line
                             : rowHover.hovered ? menu.tokens.panel2
                             : "transparent"

                        Text {
                            anchors.left: parent.left
                            anchors.leftMargin: 11
                            anchors.verticalCenter: parent.verticalCenter
                            text: option.modelData.label
                            color: option.modelData.value === menu.language ? menu.tokens.on : menu.tokens.text
                            font.pixelSize: 13
                        }

                        HoverHandler {
                            id: rowHover
                            cursorShape: Qt.PointingHandCursor
                            onHoveredChanged: if (hovered) listHide.stop()
                        }
                        TapHandler {
                            id: rowPress
                            onTapped: option.choose()
                        }
                    }
                }
            }
        }
    }

    Timer {
        id: listHide
        interval: 160
        onTriggered: menu.listVisible = false
    }

    /// Escape puts the menu down, the way a press outside it does. Application-wide rather than
    /// window-scoped, for the reason the removal question's Shortcut gives: a window that never
    /// becomes active cannot answer a window-scoped key.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: menu.visible
        onActivated: menu.visible = false
    }
}
