pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The settings surface from UI.md section 4.4 and ui-prototypes/settings.html.
 *
 * The window is fixed-size. Navigation changes the loaded page inside the card rather than
 * changing the surface geometry, so placement beside the tray remains stable.
 */
Window {
    id: settings

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    /// Set by the panel that replaces this one: the card fades out in place, and the window goes when it has.
    property bool leaving: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 380
    readonly property int cardHeight: 480
    property string category: ""
    property string route: ""
    /// Which setting's preview the pointer is asking for, by key, or "" for none (UI.md 4.4). Main
    /// reads the key, puts the demo card up beside this window's card, and takes it down again.
    property string previewKey: ""
    property string apiDraft: ""
    property string saveStatus: ""
    readonly property bool hasUnsavedChanges: apiDraft.length > 0
    // qmllint disable missing-property
    readonly property var openChildRect: viewLoader.status === Loader.Ready && viewLoader.item
        ? viewLoader.item.openChildRect
        : null
    // qmllint enable missing-property

    /// The open child here is a dropdown, so a press anywhere else puts it down. The words panel's
    /// removal question is the other kind and says so itself.
    readonly property bool openChildIsModal: false

    /// The desktop under this window, and the colours it reads: glass on, the text tone judged over its own sheet.
    readonly property var glass: Controller.glassBackdrop.surface("settings")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: settings.glass.lightText
    }

    width: cardWidth + 2 * shadowMargin
    height: cardHeight + 2 * shadowMargin

    function categoryTitle(value) {
        if (value === "general") return qsTranslate("SettingsPopup", "General");
        if (value === "learning") return qsTranslate("SettingsPopup", "Reading & learning");
        if (value === "capture") return qsTranslate("SettingsPopup", "Capture & popups");
        if (value === "model") return qsTranslate("SettingsPopup", "Model service");
        if (value === "budget") return qsTranslate("SettingsPopup", "Daily budget");
        if (value === "extensions") return qsTranslate("SettingsPopup", "Extensions");
        if (value === "about") return qsTranslate("SettingsPopup", "About");
        return "";
    }

    function routeTitle(value) {
        if (value === "font") return qsTranslate("SettingsPopup", "Font");
        if (value === "clipboard") return qsTranslate("SettingsPopup", "Popup & clipboard");
        if (value === "connection") return qsTranslate("SettingsPopup", "API configuration");
        if (value === "sync") return qsTranslate("SettingsPopup", "Account & sync");
        return categoryTitle(category);
    }

    function closeChild() {
        // qmllint disable missing-property
        if (viewLoader.status === Loader.Ready && viewLoader.item && viewLoader.item.closeChild)
            viewLoader.item.closeChild();
        // qmllint enable missing-property
    }

    function openCategory(value) {
        closeChild();
        category = value;
        route = "";
        body.contentY = 0;
        // The page that raised a preview is being swapped for another one, and an item that goes
        // away with its page never reports the pointer leaving it: the key would outlive the row.
        previewKey = "";
    }

    function openRoute(value) {
        closeChild();
        route = value;
        body.contentY = 0;
        previewKey = "";
    }

    /// @return Whether the page had a question up, which this closed. Backing out waits for it, so one Escape answers it
    ///         and the next one backs out.
    /// How far the preview card keeps clear of this panel and of the screen's edges: the 8 px every
    /// surface keeps (Main.qml's screenMargin).
    readonly property int previewMargin: 8

    /// @return This panel's own card, without the transparent margin its shadow is drawn in.
    function previewPanelRect() {
        return Qt.rect(settings.x + settings.shadowMargin, settings.y + settings.shadowMargin,
                       settings.cardWidth, settings.cardHeight);
    }

    /// @return The work area of the screen this panel is on, in virtual DIPs: asked of Qt by device
    ///         name, and its own geometry where the screen has none (Main.qml's workArea() for the
    ///         surfaces it places).
    function panelWorkArea() {
        // qmllint disable missing-property
        const area = Controller.screenWorkArea(settings.screen.name || "");
        if (area.width > 0)
            return {left: area.x, top: area.y, right: area.x + area.width, bottom: area.y + area.height};
        const screen = settings.screen;
        return {left: screen.virtualX, top: screen.virtualY,
                right: screen.virtualX + screen.width, bottom: screen.virtualY + screen.height};
        // qmllint enable missing-property
    }

    /// @return The x the preview's window takes for its card to sit previewMargin clear of @p panel,
    ///         in @p area: the panel's right first, its left when the right has no room, and the
    ///         clamp's answer -- over the panel -- when neither has.
    ///
    /// The panel is put up beside the tray icon, whose centre is where its right edge lands, so what
    /// is to the right of it is the tray's own end of the screen and usually has room for nothing: on
    /// a stock layout the left is the answer.
    function previewX(panel, area, shadow, width) {
        const right = panel.x + panel.width + settings.previewMargin - shadow;
        const left = panel.x - settings.previewMargin - width + shadow;
        // qmllint disable missing-property
        const x = right + width - shadow <= area.right - settings.previewMargin ? right : left;
        return Math.round(settings.clamp(x, area.left + settings.previewMargin - shadow,
                                         area.right - settings.previewMargin - width + shadow));
        // qmllint enable missing-property
    }

    /// @brief Put the preview beside the panel's card, centred on it vertically so the two read as
    ///        one gesture.
    function placePreviewCard() {
        const panel = settings.previewPanelRect();
        const area = settings.panelWorkArea();
        const shadow = previewCard.shadowMargin;
        const height = previewCard.height;
        previewCard.x = settings.previewX(panel, area, shadow, previewCard.width);
        // qmllint disable missing-property
        previewCard.y = Math.round(settings.clamp(panel.y + panel.height / 2 - height / 2,
                                                  area.top + settings.previewMargin - shadow,
                                                  area.bottom - settings.previewMargin - height + shadow));
        // qmllint enable missing-property
        previewCard.raise();
    }

    /// @return @p value held inside [@p low, @p high]; a range that is empty collapses to its low end.
    function clamp(value, low, high) {
        return Math.max(low, Math.min(value, Math.max(low, high)));
    }

    /// @brief Raise the preview card for a key, or put it down for "".
    ///
    /// The card is the explanation bubble itself (Bubble.qml's `preview`), so the demo follows the
    /// reader's settings: the definition in their explanation language, at their typing pace. A
    /// machine whose `data/preview.json` could not be read has no payload, and no preview.
    function updatePreview() {
        if (previewKey === "") {
            previewSettle.restart();
            return;
        }
        previewSettle.stop();
        const payload = PreviewSample.payload(previewKey);
        if (!payload) {
            previewCard.visible = false;
            return;
        }
        previewCard.show(payload);
    }

    onPreviewKeyChanged: settings.updatePreview()

    function closeQuestion() {
        // qmllint disable missing-property
        if (viewLoader.status === Loader.Ready && viewLoader.item && viewLoader.item.questionOpen) {
            viewLoader.item.closeQuestion();
            return true;
        }
        // qmllint enable missing-property
        return false;
    }

    function navigateBack() {
        if (closeQuestion())
            return;
        if (route !== "") {
            closeChild();
            route = "";
        } else if (category !== "") {
            category = "";
        } else {
            visible = false;
        }
        body.contentY = 0;
        previewKey = "";
    }

    function saveSettings() {
        if (apiDraft.length > 0)
            Controller.setApiKey(apiDraft);
        apiDraft = "";
        saveStatus = qsTranslate("SettingsPopup", "Saved");
    }

    function restoreDefaults() {
        Controller.setLevel(2);
        Controller.setExplanationLang("en");
        Controller.setMultiSense(false);
        Controller.setTheme("light");
        Controller.setFont("");
        Controller.setAnimationsEnabled(true);
        Controller.setRevealSpeed(3);
        Controller.setSelectionCapture(true);
        Controller.setClipboardPolicy("topmost");
        Controller.setBubbleDismissSeconds(5);
        Controller.setBubblePlacement("above");
        Controller.setFrostedBlur(10);
        Controller.setFrostedSaturation(120);
        Controller.setFrostedRim(20);
        Controller.setLiquidRefraction(20);
        Controller.setLiquidBlur(10);
        Controller.setLiquidRim(10);
        Controller.setLiquidTextColor("#101319");
        Controller.modelCatalogDuty.setProvider("DeepSeek");
        Controller.setModel("deepseek-flash");
        Controller.restoreCaptureDefaults();
        // The interface language is not here on purpose: it belongs to the tray menu, and a
        // panel is not the place that changes a setting the panel does not show.
        saveStatus = qsTranslate("SettingsPopup", "Defaults restored");
    }

    onVisibleChanged: {
        if (visible) {
            category = "";
            route = "";
            apiDraft = "";
            saveStatus = "";
            body.contentY = 0;
        } else {
            closeChild();
        }
        // The preview goes with the panel, at once rather than a beat later: a card left standing
        // over the desktop is a card belonging to a panel that is no longer under it.
        previewKey = "";
        previewSettle.stop();
        previewCard.visible = false;
    }

    GlassHost {
        window: settings
        sheet: settings.glass
        dragging: shell.dragging
    }

    Shortcut {
        sequence: "Escape"
        onActivated: settings.navigateBack()
    }

    ShadowCard {
        id: shell
        anchors.fill: parent
        radius: settings.tokens.radiusCard
        // The glass draws the card itself; under the solid themes the fill stays as it was.
        color: settings.tokens.glassMode === "" ? settings.tokens.panel : "transparent"
        movable: true
        onDraggingChanged: {
            if (dragging) {
                settings.closeChild();
                // A press that turns into a drag of the card is not a reader reading a title, and
                // the preview would be left standing at the place the card has moved away from.
                settings.previewKey = "";
            }
        }

        // The glass is the card's own background. Its picture is held in memory only (docs/adr/0034).
        GlassSurface {
            anchors.fill: parent
            sheet: settings.glass
            mode: settings.tokens.glassMode
        }

        Item {
            id: header
            x: 0
            y: 0
            width: settings.cardWidth
            height: 58

            Item {
                visible: settings.category !== ""
                x: 14
                y: 16
                width: 26
                height: 26

                Icon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: "qrc:/icons/ui-back.svg"
                    color: settings.tokens.muted
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: settings.navigateBack() }
            }

            // The breadcrumb is chained off the title rather than pinned to fixed offsets: the
            // title is 28 px of Chinese and 54 px of English at this size, so a separator at a
            // hardcoded x leaves a hole behind one of them.
            Text {
                id: titleText
                x: settings.category !== "" ? 54 : 20
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("SettingsPopup", "Settings")
                color: settings.tokens.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            Text {
                id: separator
                visible: settings.category !== ""
                anchors.left: titleText.right
                anchors.leftMargin: 9
                anchors.verticalCenter: parent.verticalCenter
                text: "/"
                color: settings.tokens.faint
                font.pixelSize: 12
            }

            Text {
                visible: settings.category !== ""
                anchors.left: separator.right
                anchors.leftMargin: 9
                anchors.right: closeButton.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: settings.route !== "" ? settings.routeTitle(settings.route) : settings.categoryTitle(settings.category)
                color: settings.tokens.muted
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Item {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: 26

                Icon {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: "qrc:/icons/ui-close.svg"
                    color: settings.tokens.faint
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: settings.visible = false }
            }
        }

        Flickable {
            id: body
            x: 0
            y: header.height
            width: settings.cardWidth
            height: settings.cardHeight - header.height - footer.height
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, viewLoader.y + viewLoader.height + 18)

            Loader {
                id: viewLoader
                x: 20
                y: 12
                width: settings.cardWidth - 40
                // A page is as tall as its own blocks, and at least as tall as the body it is
                // loaded into, less the 18 px the body keeps under a page that scrolls. A page
                // that ends early therefore still fills, and its last block has a bottom of its
                // own to sit on: the About page's footer does.
                // qmllint disable missing-property
                height: status === Loader.Ready && item
                        ? Math.max(item.implicitHeight, body.height - viewLoader.y - 18)
                        : 0
                // qmllint enable missing-property
                sourceComponent: settings.route === "clipboard" ? clipboardComponent
                                 : settings.route === "connection" ? connectionComponent
                                 : settings.route === "font" ? fontComponent
                                 : settings.route === "sync" ? syncComponent
                                 : settings.category === "" ? catalogComponent
                                  : settings.category === "general" ? generalComponent
                                  : settings.category === "learning" ? learningComponent
                                  : settings.category === "capture" ? captureComponent
                                  : settings.category === "model" ? modelComponent
                                  : settings.category === "budget" ? budgetComponent
                                  : settings.category === "about" ? aboutComponent
                                  : extensionsComponent
            }
        }

        Item {
            id: footer
            x: 0
            y: settings.cardHeight - height
            width: settings.cardWidth
            height: 56

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: settings.tokens.line2
            }

            Rectangle {
                x: 20
                y: 15
                width: defaultsLabel.width + 28
                height: 27
                radius: settings.tokens.radiusPill
                color: settings.tokens.panel2
                border.width: 1
                border.color: settings.tokens.line

                Text {
                    id: defaultsLabel
                    anchors.centerIn: parent
                    text: qsTranslate("SettingsPopup", "Defaults")
                    color: settings.tokens.muted
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: settings.restoreDefaults() }
            }

            Text {
                anchors.left: defaultsLabel.parent.right
                anchors.leftMargin: 8
                anchors.right: saveButton.left
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: settings.hasUnsavedChanges
                      ? qsTranslate("SettingsPopup", "Unsaved changes")
                      : settings.saveStatus
                color: settings.tokens.faint
                font.pixelSize: 11
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            Rectangle {
                id: saveButton
                anchors.right: parent.right
                anchors.rightMargin: 20
                y: 15
                width: saveLabel.width + 28
                height: 27
                radius: settings.tokens.radiusPill
                color: settings.tokens.ink
                opacity: settings.hasUnsavedChanges ? 1 : 0.45

                Text {
                    id: saveLabel
                    anchors.centerIn: parent
                    text: qsTranslate("SettingsPopup", "Save")
                    color: settings.tokens.on
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
                HoverHandler {
                    cursorShape: settings.hasUnsavedChanges ? Qt.PointingHandCursor : Qt.ArrowCursor
                }
                TapHandler {
                    enabled: settings.hasUnsavedChanges
                    onTapped: settings.saveSettings()
                }
            }
        }
    }

    /// The settings preview (UI.md 4.4): the card a hovered setting's title raises beside the panel.
    /// An explanation bubble with `preview` on, and it lives here rather than in Main because the
    /// panel is the one thing it is placed against -- its own screen is the screen the card goes on.
    Bubble {
        id: previewCard
        preview: true
        onPlacementRequested: settings.placePreviewCard()
    }

    /// Leaving a title is held a beat before the card goes: a pointer crossing from one label to the
    /// next can have the new one arrive before the old one has reported its exit, and dropping the
    /// card in between reads as a flicker (Bubble.qml keeps the same guard over its own hover).
    Timer {
        id: previewSettle
        interval: 120
        onTriggered: previewCard.visible = false
    }

    Component {
        id: catalogComponent
        SettingsCatalog {
            onCategoryRequested: (value) => settings.openCategory(value)
        }
    }
    Component {
        id: generalComponent
        SettingsGeneralPage {
            onChildRequested: (value) => settings.openRoute(value)
            onPreviewRequested: (key) => settings.previewKey = key
        }
    }
    Component { id: aboutComponent; SettingsAboutPage {} }
    Component {
        id: learningComponent
        SettingsLearningPage {
            onPreviewRequested: (key) => settings.previewKey = key
        }
    }
    Component {
        id: captureComponent
        SettingsCapturePage {
            onChildRequested: (value) => settings.openRoute(value)
        }
    }
    Component {
        id: modelComponent
        SettingsModelPage {
            onChildRequested: (value) => settings.openRoute(value)
        }
    }
    Component {
        id: budgetComponent
        SettingsBudgetPage {
            onBudgetChanged: (amount) => Controller.setDailyBudget(amount)
        }
    }
    Component {
        id: fontComponent
        SettingsFontPage {
            onPreviewRequested: (key) => settings.previewKey = key
        }
    }
    Component { id: extensionsComponent; SettingsExtensionsPage {} }
    Component { id: clipboardComponent; SettingsClipboardPage {} }
    Component { id: syncComponent; SettingsSyncPage {} }
    Component {
        id: connectionComponent
        SettingsConnectionPage {
            draftOwner: settings
            onApiEdited: (value) => settings.apiDraft = value
        }
    }
}
