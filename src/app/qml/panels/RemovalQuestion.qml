pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The question the words panel's cross asks, as a window of its own over that panel.
 *
 * A window rather than a face drawn inside the panel's card, for two reasons. The card's height is
 * not the panel's to change: SurfaceHost.placeBeside puts its bottom edge on the tray icon and
 * nothing re-places a panel that changes size afterwards, so a card that shrank for the question
 * would lift away from the icon it was opened from. And it is a window: docs/QML.md section 1 draws
 * the surface/component line there, so it is a file of its own.
 *
 * Modal to the window that raises it, so a press landing on the list behind cannot reach a row:
 * one word cannot be deleted while another is being asked about.
 *
 * Neither answer pill is filled; the sentence carries the decision -- PRODUCT.md asks a removal to
 * confirm before it happens, not to alarm the reader into it.
 *
 * Its strings are spelled in the words panel's translation context rather than this file's, the way
 * SettingsPopup's sub-pages are: the question belongs to that panel, and a context of its own would
 * read as a new string to every translator.
 */
Window {
    id: root
    objectName: "removal"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("removal")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: root.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    /// Modal while it is a question, and for the fade as well.
    ///
    /// Qt adds a modal window to its modal list when it is shown and removes it when it is hidden;
    /// changing the modality in between does neither. A window still in that list after it hides is
    /// read by QApplication as application-modal, which once stopped every surface taking presses
    /// after Cancel. The window stays WindowModal until ShadowCard hides it, which is also when Qt
    /// releases it.
    modality: Qt.WindowModal

    /// The lemma being asked about. The caller owns it -- this window only draws it.
    property string word: ""

    /// The lemma the sentence is drawn with, copied when the question is asked: the caller clears
    /// `word` as soon as it is answered, and the sentence must not lose its subject mid-fade.
    property string displayWord: ""

    /// ShadowCard fades it and hides the window when the fade is done; the answer itself is
    /// emitted at once, not after it.
    property bool leaving: false

    /// @p dontAsk is what the switch under the sentence says.
    signal answered(bool dontAsk)
    /// The question is on its way out, answered or not. The caller's idea of what is being asked
    /// ends here rather than when the fade does; this window keeps drawing `displayWord`.
    signal dismissed()

    readonly property int shadowMargin: 26
    readonly property int minCardWidth: 320
    readonly property int maxCardWidth: 460

    /// Whether this answer also settles the asking. On at the run's first question, so one removal
    /// a run is asked about; the reader turns it off to be asked about the ones after, and it stays
    /// where they left it.
    ///
    /// Not reset per question on purpose: `components/Switch.qml` writes its `checked` itself when
    /// tapped, so writing it from elsewhere leaves the switch and the decision disagreeing.
    property bool dontAsk: true

    /// As wide as the sentence needs and no wider, like the panel's widest row: 320 px floor, and
    /// past 460 a long word or translation wraps rather than widening the card off the screen.
    readonly property int cardWidth: Math.min(maxCardWidth,
                                              Math.max(minCardWidth, Math.ceil(questionText.implicitWidth) + 40))
    width: cardWidth + 2 * shadowMargin
    height: questionColumn.implicitHeight + 36 + 2 * shadowMargin

    /**
     * @brief Put the question up, centred on the window it is modal to.
     *
     * The centring waits a turn: the height is the laid-out height of what the card holds, and
     * that is not settled on the same turn the window is shown.
     */
    function ask() {
        root.displayWord = root.word;
        root.leaving = false;
        root.visible = true;
        root.raise();
        Qt.callLater(() => root.placeOverCard());
    }

    /// @brief Put the question down, fading it out before the window goes.
    ///
    /// Escape and Cancel report nothing: leaving a question unanswered is not an answer. Remove
    /// emits `answered` first, so the word is settled while the card is still on screen. Both
    /// ways out emit `dismissed` at once, because the caller's pending subject is the question's,
    /// not the fade's.
    function dismiss() {
        if (!root.visible || root.leaving)
            return;
        root.leaving = true;
        root.dismissed();
    }

    /// @brief Centre the question on the window it is modal to.
    function placeOverCard() {
        const host = root.transientParent;
        if (!host)
            return;
        root.x = Math.round(host.x + (host.width - root.width) / 2);
        root.y = Math.round(host.y + (host.height - root.height) / 2);
    }

    /// Escape means "not this word", and is offered exactly while the question is up.
    ///
    /// Application-wide rather than the default window scope: the offscreen platform the suite runs
    /// on never gives a window the active state, so a window-scoped Escape cannot be exercised
    /// there. The wider scope is not a wider net -- Qt consults a shortcut map only for keys some
    /// window of this application received, so `enabled` is the whole guard.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: root.visible && !root.leaving
        onActivated: root.dismiss()
    }

    ShadowCard {
        id: panel
        anchors.fill: parent
        color: root.tokens.glassMode === "" ? root.tokens.panel : "transparent"
        enabled: !root.leaving

        GlassSurface {
            anchors.fill: parent
            sheet: root.glass
            mode: root.tokens.glassMode
            radius: panel.radius
        }

        Column {
            id: questionColumn
            x: 20
            y: 18
            width: root.cardWidth - 40
            spacing: 12

            Text {
                width: parent.width
                text: qsTranslate("WordsPopup", "Remove word")
                color: root.tokens.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            // The word being removed is the sentence's own %1 rather than a field of its own:
            // where it sits in that sentence is the translator's to place.
            Text {
                id: questionText
                width: parent.width
                text: qsTranslate("WordsPopup", "Remove %1 from your word list and history?").arg(root.displayWord)
                color: root.tokens.text
                font.pixelSize: 13
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            // The only control here that is not an answer to the removal: it decides whether this
            // question is asked again, and it is read when the answer is Remove.
            SwitchRow {
                label: qsTranslate("WordsPopup", "Don't ask again")
                checked: root.dontAsk
                onToggled: (on) => root.dontAsk = on
            }

            Item {
                width: parent.width
                height: buttons.height

                Row {
                    id: buttons
                    anchors.right: parent.right
                    spacing: 8

                    Rectangle {
                        width: cancelLabel.width + 28
                        height: 27
                        radius: root.tokens.radiusPill
                        color: root.tokens.panel2
                        border.width: 1
                        border.color: root.tokens.line
                        scale: cancelTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: root.tokens.motion.press
                                easing.type: root.tokens.motion.easing
                            }
                        }

                        Text {
                            id: cancelLabel
                            anchors.centerIn: parent
                            text: qsTranslate("WordsPopup", "Cancel")
                            color: root.tokens.muted
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: cancelTap
                            onTapped: root.dismiss()
                        }
                    }

                    Rectangle {
                        width: removeLabel.width + 28
                        height: 27
                        radius: root.tokens.radiusPill
                        color: root.tokens.panel2
                        border.width: 1
                        border.color: root.tokens.line
                        scale: removeTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: root.tokens.motion.press
                                easing.type: root.tokens.motion.easing
                            }
                        }

                        Text {
                            id: removeLabel
                            anchors.centerIn: parent
                            text: qsTranslate("WordsPopup", "Remove")
                            color: root.tokens.text
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: removeTap
                            onTapped: {
                                root.answered(root.dontAsk);
                                root.dismiss();
                            }
                        }
                    }
                }
            }
        }
    }

    GlassHost {
        window: root
        sheet: root.glass
        dragging: panel.dragging
    }
}
