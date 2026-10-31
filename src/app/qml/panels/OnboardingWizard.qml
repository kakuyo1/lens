pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import "../util/Numbers.js" as Numbers

/**
 * The first-run wizard from UI-SPEC.md section 4.19 and ui-prototypes/first-run-onboarding.html
 * (docs/adr/0148).
 *
 * Three steps in the settings panel's shell: the level to read at, the service and its key, and one
 * sample lookup. Every step takes effect as it is touched, like the settings panel's own rows; the
 * guidance record is the one write that waits for the end, and a skip writes it too. The window is
 * fixed-size, like the panel whose shell it borrows.
 */
Window {
    id: wizard
    objectName: "onboarding"

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false
    /// ShadowCard fades it and hides the window when the fade is done; the record is written before
    /// the fade starts, not after it.
    property bool leaving: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 380
    readonly property int cardHeight: 480
    /// The step the card shows, 1..stepCount.
    property int step: 1
    readonly property int stepCount: 3
    /// The key typed but not yet written, committed when the reader leaves the step or the panel.
    property string apiDraft: ""
    /// The word the sample text asks the reader to drag across, and the demo card answers.
    readonly property string demoWord: "quiet"

    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("onboarding")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: wizard.glass.lightText
    }

    /// How far the demo card keeps clear of this window and of the screen's edges: the 8 px every
    /// surface keeps (SurfaceHost.qml's screenMargin).
    readonly property int previewMargin: 8

    width: cardWidth + 2 * shadowMargin
    height: cardHeight + 2 * shadowMargin

    function show() {
        leaving = false
        step = 1
        apiDraft = ""
        // Typing broke the field's binding to the draft, so a second showing (a forced build) has
        // to clear it by hand: the masked placeholder reads back only on an empty field.
        keyInput.text = ""
        x = Math.round(Screen.virtualX + (Screen.width - width) / 2)
        y = Math.round(Screen.virtualY + (Screen.height - height) / 2)
        visible = true
        raise()
    }

    /// @brief Write the API key the reader has typed, if any, and clear the draft.
    ///
    /// The settings panel's commitApiDraft() rule: the key is the one thing that is typed, so it is
    /// written when the field's step is left and again when the panel goes.
    function commitApiDraft() {
        if (apiDraft.length === 0)
            return;
        Controller.setApiKey(apiDraft);
        apiDraft = "";
    }

    /// @brief The reader is done with the wizard, one way or the other.
    ///
    /// Done and every skip path end here: the draft is written, the guidance is recorded, the card
    /// fades. The record goes first, so a window that is already gone still left a recorded document.
    function finish() {
        commitApiDraft();
        Controller.onboardingDuty.finish();
        dismiss();
    }

    /// @brief Take the wizard down, fading it out before the window goes.
    function dismiss() {
        if (visible)
            leaving = true
    }

    /// @brief Move one step in @p direction, writing the key draft on the way out of its step.
    function goTo(next) {
        commitApiDraft();
        step = Math.max(1, Math.min(stepCount, next));
    }

    function back() {
        goTo(step - 1)
    }

    function forward() {
        goTo(step + 1)
    }

    /// @brief The selection in the sample is the demo word: raise the demo card beside the window.
    ///
    /// The reading here is the reader's own drag inside a Lens window, which the selection hook
    /// never sees -- the card is answered locally, out of the sample data, and no request is made.
    function checkSample() {
        const selected = sampleText.selectedText.replace(/[^A-Za-z']/g, "").toLowerCase();
        if (selected !== demoWord)
            return;
        const payload = PreviewSample.payload("onboarding");
        if (!payload)
            return;
        previewBubble.show(payload);
    }

    /// @return This window's card, without the transparent margin its shadow is drawn in.
    function wizardCardRect() {
        return Qt.rect(wizard.x + wizard.shadowMargin, wizard.y + wizard.shadowMargin,
                       wizard.cardWidth, wizard.cardHeight);
    }

    /// @return The work area of the screen this window is on, in virtual DIPs, as the settings
    ///         panel's panelWorkArea() reads it.
    function cardWorkArea() {
        // qmllint disable missing-property
        const area = Controller.screenWorkArea(wizard.screen.name || "");
        if (area.width > 0)
            return {left: area.x, top: area.y, right: area.x + area.width, bottom: area.y + area.height};
        const screen = wizard.screen;
        return {left: screen.virtualX, top: screen.virtualY,
                right: screen.virtualX + screen.width, bottom: screen.virtualY + screen.height};
        // qmllint enable missing-property
    }

    /// @return The x the demo card takes for its card to sit previewMargin clear of this window:
    ///         the right first, the left when the right has no room, the clamp's answer when neither has.
    function previewX(area, shadow, width) {
        const rect = wizardCardRect();
        const right = rect.x + rect.width + wizard.previewMargin - shadow;
        const left  = rect.x - wizard.previewMargin - width + shadow;
        const x = right + width - shadow <= area.right - wizard.previewMargin ? right : left;
        return Math.round(Numbers.clamp(x, area.left + wizard.previewMargin - shadow,
                                        area.right - wizard.previewMargin - width + shadow));
    }

    /// @brief Put the demo card beside the window's card, centred on it vertically so the two read
    ///        as one gesture -- the settings panel's placePreviewCard() rule.
    function placePreviewCard() {
        const rect = wizardCardRect();
        const area = cardWorkArea();
        const shadow = previewBubble.shadowMargin;
        previewBubble.x = wizard.previewX(area, shadow, previewBubble.width);
        // qmllint disable missing-property
        previewBubble.y = Math.round(Numbers.clamp(rect.y + rect.height / 2 - previewBubble.height / 2,
                                                   area.top + wizard.previewMargin - shadow,
                                                   area.bottom - wizard.previewMargin - previewBubble.height + shadow));
        // qmllint enable missing-property
        previewBubble.raise();
    }

    onVisibleChanged: {
        if (visible)
            return;
        // Leaving the panel writes the typed key, and the demo card goes with the wizard at once
        // rather than a beat later: a card left standing over the desktop belongs to no window.
        commitApiDraft();
        previewBubble.visible = false;
    }

    GlassHost {
        window: wizard
        sheet: wizard.glass
        dragging: shell.dragging
    }

    Shortcut {
        sequence: "Escape"
        onActivated: wizard.step > 1 ? wizard.back() : wizard.finish()
    }

    /// The vocabulary preview: the word cloud a hovered level's row raises beside the list, the
    /// component the learning page's own dropdown uses (settings/previews/VocabPreview.qml). Its
    /// glass sheet is its own ("vocabPreview"), so nothing here has to host it.
    Component {
        id: vocabPreviewComponent
        VocabPreview {}
    }

    ShadowCard {
        id: shell
        anchors.fill: parent
        radius: wizard.tokens.radiusCard
        color: wizard.tokens.glassMode === "" ? wizard.tokens.panel : "transparent"
        movable: true
        enabled: !wizard.leaving

        GlassSurface {
            anchors.fill: parent
            sheet: wizard.glass
            mode: wizard.tokens.glassMode
        }

        Item {
            id: header
            x: 0
            y: 0
            width: wizard.cardWidth
            height: 58

            Text {
                x: 20
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("OnboardingWizard", "Welcome to Lens")
                color: wizard.tokens.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            Text {
                id: stepLabel
                objectName: "stepLabel"
                anchors.right: closeButton.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("OnboardingWizard", "Step %1 of %2").arg(wizard.step).arg(wizard.stepCount)
                color: wizard.tokens.muted
                font.pixelSize: 12
            }

            Item {
                id: closeButton
                anchors.right: parent.right
                anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: 26

                IconButton {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    source: "qrc:/icons/ui-close.svg"
                    color: wizard.tokens.faint
                    accessibleName: qsTranslate("OnboardingWizard", "Skip the setup")
                    onTapped: wizard.finish()
                }
            }
        }

        Item {
            id: body
            x: 0
            y: header.height
            width: wizard.cardWidth
            height: wizard.cardHeight - header.height - footer.height

            Column {
                id: stepLevel
                visible: wizard.step === 1
                x: 20
                y: 16
                width: wizard.cardWidth - 40
                spacing: 16

                Text {
                    width: parent.width
                    text: qsTranslate("OnboardingWizard", "Choose how much of what you read should be explained. The level decides which words Lens explains and which it leaves alone; you can change it later under Reading & learning.")
                    color: wizard.tokens.text
                    font.pixelSize: 13
                    lineHeight: 1.35
                    lineHeightMode: Text.ProportionalHeight
                    wrapMode: Text.WordWrap
                }

                Column {
                    width: parent.width
                    spacing: 7

                    Text {
                        text: qsTranslate("OnboardingWizard", "Vocabulary level")
                        color: wizard.tokens.muted
                        font.pixelSize: 12
                    }

                    DropdownField {
                        id: levelField
                        objectName: "levelField"
                        width: parent.width
                        accessibleName: qsTranslate("OnboardingWizard", "Vocabulary level")
                        options: Controller.settings.levels
                        currentValue: Controller.settings.level
                        onPicked: (value) => Controller.setSetting("level", value)
                        // The word cloud a hovered level raises beside the list -- the same preview
                        // the learning page's own dropdown carries; the option carries its level.
                        optionPreview: vocabPreviewComponent
                    }
                }
            }

            Column {
                id: stepService
                visible: wizard.step === 2
                x: 20
                y: 16
                width: wizard.cardWidth - 40
                spacing: 12

                Text {
                    width: parent.width
                    text: qsTranslate("OnboardingWizard", "Pick the service that writes the explanations, and paste the key it gave you. DeepSeek is the default and takes Chinese payment; any provider below works the same.")
                    color: wizard.tokens.text
                    font.pixelSize: 13
                    lineHeight: 1.35
                    lineHeightMode: Text.ProportionalHeight
                    wrapMode: Text.WordWrap
                }

                Column {
                    width: parent.width
                    spacing: 7

                    Text {
                        text: qsTranslate("OnboardingWizard", "Provider")
                        color: wizard.tokens.muted
                        font.pixelSize: 12
                    }

                    DropdownField {
                        id: providerField
                        objectName: "providerField"
                        width: parent.width
                        accessibleName: qsTranslate("OnboardingWizard", "Provider")
                        options: Controller.settings.providers
                        currentValue: Controller.settings.provider
                        onPicked: (value) => Controller.modelCatalogDuty.setProvider(value)
                    }
                }

                Column {
                    width: parent.width
                    spacing: 7

                    Text {
                        text: qsTranslate("OnboardingWizard", "Service address")
                        color: wizard.tokens.muted
                        font.pixelSize: 12
                    }

                    // The address the provider's catalog entry carries, the connection page's own
                    // field: switching the provider rewrites it to that provider's default, and the
                    // reader overrides it when the default moved or the provider is a custom one.
                    // Non-HTTPS answers are refused by the controller, as on the connection page.
                    Rectangle {
                        width: parent.width
                        height: 35
                        radius: wizard.tokens.radiusField
                        color: wizard.tokens.panel2
                        border.width: 1
                        border.color: wizard.tokens.line

                        TextInput {
                            anchors.fill: parent
                            anchors.leftMargin: 11
                            anchors.rightMargin: 11
                            clip: true
                            verticalAlignment: TextInput.AlignVCenter
                            text: Controller.settings.url
                            color: wizard.tokens.text
                            font.pixelSize: 12
                            selectByMouse: true
                            activeFocusOnTab: true
                            Accessible.name: qsTranslate("OnboardingWizard", "Service address")
                            onEditingFinished: Controller.setApiUrl(text)
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }
                    }

                    Text {
                        width: parent.width
                        text: qsTranslate("OnboardingWizard", "The address is the provider's default; override it when it moves or when you use a custom service.")
                        color: wizard.tokens.muted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }

                Column {
                    width: parent.width
                    spacing: 7

                    Text {
                        text: qsTranslate("OnboardingWizard", "API key")
                        color: wizard.tokens.muted
                        font.pixelSize: 12
                    }

                    Rectangle {
                        width: parent.width
                        height: 35
                        radius: wizard.tokens.radiusField
                        color: wizard.tokens.panel2
                        border.width: 1
                        border.color: wizard.tokens.line

                        TextInput {
                            id: keyInput
                            objectName: "keyInput"
                            anchors.fill: parent
                            anchors.leftMargin: 11
                            anchors.rightMargin: 11
                            clip: true
                            verticalAlignment: TextInput.AlignVCenter
                            text: wizard.apiDraft
                            echoMode: TextInput.Password
                            color: wizard.tokens.text
                            font.pixelSize: 13
                            selectByMouse: true
                            activeFocusOnTab: true
                            Accessible.name: qsTranslate("OnboardingWizard", "API key")
                            onTextEdited: wizard.apiDraft = text
                            onEditingFinished: {
                                wizard.commitApiDraft();
                                // The first keystroke destroys the text binding; put it back so the
                                // field goes to the masked placeholder once the draft has cleared.
                                text = Qt.binding(() => wizard.apiDraft);
                            }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 11
                            anchors.rightMargin: 11
                            verticalAlignment: Text.AlignVCenter
                            visible: keyInput.text.length === 0
                            text: Controller.settings.hasApiKey
                                  ? "sk-" + "•".repeat(28)
                                  : "sk-********************************"
                            color: wizard.tokens.faint
                            font: keyInput.font
                            elide: Text.ElideRight
                        }
                    }

                    Text {
                        width: parent.width
                        text: qsTranslate("OnboardingWizard", "Create a key in your provider's dashboard; it is stored on this device only.")
                        color: wizard.tokens.muted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        text: qsTranslate("OnboardingWizard", "A lookup is a small request that costs a fraction of a cent; the cost panel keeps the running total.")
                        color: wizard.tokens.muted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Column {
                id: stepTry
                visible: wizard.step === 3
                x: 20
                y: 16
                width: wizard.cardWidth - 40
                spacing: 16

                Text {
                    width: parent.width
                    text: qsTranslate("OnboardingWizard", "This is the whole loop: select English text anywhere, and the explanation appears beside it. Try it here -- drag across \"quiet\" in the line below, and the card appears beside this window. Nothing is sent; the card is a sample.")
                    color: wizard.tokens.text
                    font.pixelSize: 13
                    lineHeight: 1.35
                    lineHeightMode: Text.ProportionalHeight
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    width: parent.width
                    height: sampleText.height + 24
                    radius: wizard.tokens.radiusField
                    color: wizard.tokens.panel2
                    border.width: 1
                    border.color: wizard.tokens.line

                    TextEdit {
                        id: sampleText
                        objectName: "sampleText"
                        x: 12
                        y: 12
                        width: parent.width - 24
                        text: qsTranslate("OnboardingWizard", "The evening was \"quiet\", and the page felt close.\nOne small word at a time, the reading opens.")
                        readOnly: true
                        selectByMouse: true
                        activeFocusOnTab: true
                        color: wizard.tokens.text
                        font.pixelSize: 13
                        wrapMode: TextEdit.WordWrap
                        Accessible.name: text
                        onSelectedTextChanged: wizard.checkSample()
                        HoverHandler { cursorShape: Qt.IBeamCursor }
                    }
                }

                Text {
                    width: parent.width
                    text: qsTranslate("OnboardingWizard", "Lens lives in the tray. The tray menu's settings entry opens this panel again, and its first row speaks up until a key is set.")
                    color: wizard.tokens.muted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }
        }

        Item {
            id: footer
            x: 0
            y: wizard.cardHeight - height
            width: wizard.cardWidth
            height: 56

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: wizard.tokens.line2
            }

            Row {
                x: 20
                y: 15
                spacing: 8

                Chip {
                    id: backButton
                    objectName: "backButton"
                    visible: wizard.step > 1
                    label: qsTranslate("OnboardingWizard", "Back")
                    onPicked: wizard.back()
                }

                Chip {
                    id: nextButton
                    objectName: "nextButton"
                    visible: wizard.step < wizard.stepCount
                    label: qsTranslate("OnboardingWizard", "Next")
                    onPicked: wizard.forward()
                }

                Chip {
                    id: doneButton
                    objectName: "doneButton"
                    visible: wizard.step === wizard.stepCount
                    label: qsTranslate("OnboardingWizard", "Done")
                    selected: true
                    onPicked: wizard.finish()
                }
            }
        }
    }

    /// The demo card of the third step: an explanation bubble with `preview` on, hosted here rather
    /// than in Main because this window is the one thing it is placed against (the settings panel
    /// hosts its own the same way). Its sheet is its own, so its glass draws under its own window.
    Bubble {
        id: previewBubble
        preview: true
        glassKey: "onboardingPreview"
        onPlacementRequested: wizard.placePreviewCard()
    }
}
