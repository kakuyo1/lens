pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The question a surface asks before an irreversible action: clear a cache, remove a download,
 * delete data.
 *
 * A window rather than a face drawn inside the asking surface's card, for the reason the words
 * panel's removal question gives: a placed card's height is not the panel's to change, and a card
 * that grew for the question would lift away from the anchor it was opened from. It is a window,
 * so it is a file of its own (docs/QML.md section 1).
 *
 * Modal to the window that raises it, so a press landing on the surface behind cannot reach a row
 * or a button: the action being asked about cannot be started twice.
 *
 * The confirm key is filled with `danger` and its label in `on`, so the irreversible answer is the
 * one the eye finds first; the fill keeps its contrast in every theme because `on` inverts with
 * the table (UI-SPEC.md 4.4 and 4.8). Cancel stays the secondary pill.
 *
 * The component carries no strings of its own: the caller spells every one in its own translation
 * context, the way SettingsPopup's sub-pages are.
 */
Window {
    id: root

    /// The surface table's name: this window's objectName and its image://glass key.
    required property string surfaceName
    objectName: root.surfaceName

    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface(root.surfaceName)
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

    /// The question's words. The caller sets them before ask().
    property string heading: ""
    property string message: ""
    property string confirmLabel: ""
    property string cancelLabel: ""

    /// A row under the message that silences this question, or "" when it carries none.
    property string switchLabel: ""
    /// What that switch says. Not reset per question on purpose: `components/Switch.qml` writes its
    /// `checked` itself when tapped, so writing it from elsewhere leaves the switch and the decision
    /// disagreeing.
    property bool switchOn: false

    /// ShadowCard fades it and hides the window when the fade is done; the answer itself is
    /// emitted at once, not after it.
    property bool leaving: false

    /// The question's own state: `hidden`, `asking` (up and waiting for an answer), or `closing`
    /// (answered or put down, fading before the window goes).
    readonly property string phase: !visible ? "hidden" : leaving ? "closing" : "asking"

    /// The reader took the irreversible answer; @p switchOn is what the switch says, and is
    /// always false when the question carries no switch.
    signal confirmed(bool switchOn)
    /// The question is on its way out, answered or not. The caller's idea of what is being asked
    /// ends here rather than when the fade does.
    signal dismissed()

    readonly property int shadowMargin: 26
    property int minCardWidth: 320
    /// The ceiling the caller may lower: a dialog centred on a narrow host card stays over that
    /// card rather than hanging past its sides.
    property int maxCardWidth: 460

    /// As wide as the wider of the heading and the sentence needs and no wider: 320 px floor, and
    /// past 460 a long sentence or heading wraps rather than widening the card off the screen.
    readonly property int cardWidth: Math.min(maxCardWidth,
                                              Math.max(minCardWidth,
                                                       Math.ceil(Math.max(headingText.implicitWidth,
                                                                          messageText.implicitWidth)) + 40))
    width: cardWidth + 2 * shadowMargin
    height: questionColumn.implicitHeight + 36 + 2 * shadowMargin

    /**
     * @brief Put the question up, centred on the window it is modal to.
     *
     * The centring waits a turn: the height is the laid-out height of what the card holds, and
     * that is not settled on the same turn the window is shown.
     */
    function ask() {
        root.leaving = false;
        root.visible = true;
        root.raise();
        Qt.callLater(() => root.placeOverCard());
    }

    /// @brief Put the question down, fading it out before the window goes.
    ///
    /// Escape and Cancel report nothing: leaving a question unanswered is not an answer. Confirm
    /// emits `confirmed` first, so the action runs while the card is still on screen. Both ways
    /// out emit `dismissed` at once, because the caller's pending subject is the question's, not
    /// the fade's.
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

    /// Escape means "not this", and is offered exactly while the question is up.
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
                id: headingText
                width: parent.width
                text: root.heading
                color: root.tokens.text
                font.pixelSize: 14
                font.weight: Font.Bold
                wrapMode: Text.WordWrap
            }

            Text {
                id: messageText
                width: parent.width
                text: root.message
                color: root.tokens.text
                font.pixelSize: 13
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            SwitchRow {
                visible: root.switchLabel !== ""
                label: root.switchLabel
                checked: root.switchOn
                onToggled: (on) => root.switchOn = on
            }

            Item {
                width: parent.width
                height: buttons.height

                Row {
                    id: buttons
                    anchors.right: parent.right
                    spacing: 8

                    Rectangle {
                        objectName: "cancelButton"
                        width: cancelText.width + 28
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

                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: cancelText.text
                        Accessible.onPressAction: cancelTap.tapped()
                        Keys.onPressed: (event) => {
                            if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return
                                 || event.key === Qt.Key_Space) && !event.isAutoRepeat) {
                                event.accepted = true;
                                cancelTap.tapped();
                            }
                        }
                        FocusRing { radius: root.tokens.radiusPill }

                        Text {
                            id: cancelText
                            anchors.centerIn: parent
                            text: root.cancelLabel
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
                        objectName: "confirmButton"
                        width: confirmText.width + 28
                        height: 27
                        radius: root.tokens.radiusPill
                        color: root.tokens.danger
                        scale: confirmTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: root.tokens.motion.press
                                easing.type: root.tokens.motion.easing
                            }
                        }

                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: confirmText.text
                        Accessible.onPressAction: confirmTap.tapped()
                        Keys.onPressed: (event) => {
                            if ((event.key === Qt.Key_Enter || event.key === Qt.Key_Return
                                 || event.key === Qt.Key_Space) && !event.isAutoRepeat) {
                                event.accepted = true;
                                confirmTap.tapped();
                            }
                        }
                        FocusRing { radius: root.tokens.radiusPill }

                        Text {
                            id: confirmText
                            anchors.centerIn: parent
                            text: root.confirmLabel
                            color: root.tokens.on
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: confirmTap
                            onTapped: {
                                root.confirmed(root.switchOn);
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
