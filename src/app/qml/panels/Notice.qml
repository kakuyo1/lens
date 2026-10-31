import QtQuick
import QtQuick.Window

/**
 * A reason card for work that could not produce an explanation.
 *
 * A notice has no selection anchor: it is a separate surface and centers itself on the current
 * screen. Its close path only clears notice state, so a stale bubble or action cannot dismiss a
 * newer surface.
 */
Window {
    id: notice
    objectName: "notice"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("notice")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: notice.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 320

    property string noticeTitle: ""
    property string noticeBody: ""
    property string noticeKind: "info"

    /// ShadowCard fades it and hides the window when the fade is done; the dismissal itself
    /// is reported at once, not after it.
    property bool leaving: false

    /// The one thing the reader can do about this notice, empty when there is nothing to do. Most
    /// notices are a failure with nothing left to press; an action explains noticeLemma.
    property string noticeAction: ""
    /// The word that action acts on. The surface does not know what the action means; the one that
    /// raised the notice does, and it reads this back off the controller.
    property string noticeLemma: ""

    signal actionTriggered()

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 36 + 2 * shadowMargin

    function show(payload) {
        leaving = false
        noticeTitle = payload.title || ""
        noticeBody = payload.body || ""
        noticeKind = payload.kind || "info"
        noticeAction = payload.action || ""
        noticeLemma = payload.lemma || ""
        x = Math.round(Screen.virtualX + (Screen.width - width) / 2)
        y = Math.round(Screen.virtualY + (Screen.height - height) / 2)
        visible = true
        raise()
    }

    /// @brief Take the notice down, fading it out before the window goes.
    ///
    /// The controller only hears about the dismissal from closeNotice(); a notice the duty has
    /// already cleared arrives here through dismiss(), and must not be sent back to it.
    function dismiss() {
        if (visible)
            leaving = true
    }

    function closeNotice() {
        dismiss()
        Controller.explanationDuty.dismissNotice()
    }

    ShadowCard {
        id: panel
        anchors.fill: parent
        color: notice.tokens.glassMode === "" ? notice.tokens.panel : "transparent"
        enabled: !notice.leaving

        GlassSurface {
            anchors.fill: parent
            sheet: notice.glass
            mode: notice.tokens.glassMode
            radius: panel.radius
        }

        Column {
            id: column
            x: 20
            y: 18
            width: notice.cardWidth - 40
            spacing: 12

            Item {
                width: parent.width
                height: title.implicitHeight

                Text {
                    id: title
                    anchors.left: parent.left
                    anchors.right: close.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: notice.noticeTitle
                    color: notice.tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                IconButton {
                    id: close
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-close.svg"
                    color: notice.tokens.faint
                    accessibleName: qsTr("Close")
                    onTapped: notice.closeNotice()
                }
            }

            Text {
                width: parent.width
                text: notice.noticeBody
                color: notice.noticeKind === "error" ? notice.tokens.danger : notice.tokens.text
                font.pixelSize: 13
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            // Invisible rather than zero-height when there is nothing to press: a positioner skips an
            // invisible child whole, spacing included, and a zero-height one still opens a gap.
            Item {
                visible: notice.noticeAction !== ""
                width: parent.width
                height: visible ? action.height : 0

                Rectangle {
                    id: action
                    width: actionLabel.width + 26
                    height: actionLabel.height + 12
                    radius: notice.tokens.radiusPill
                    color: notice.tokens.ink
                    scale: actionTap.pressed ? 0.97 : 1.0
                    Behavior on scale {
                        NumberAnimation {
                            duration: notice.tokens.motion.press
                            easing.type: notice.tokens.motion.easing
                        }
                    }

                    // This window takes no focus, so the pill carries a name and a press for the
                    // screen reader but no tab stop or ring.
                    Accessible.role: Accessible.Button
                    Accessible.name: actionLabel.text
                    Accessible.onPressAction: actionTap.tapped()

                    Text {
                        id: actionLabel
                        anchors.centerIn: parent
                        text: notice.noticeAction
                        color: notice.tokens.on
                        font.pixelSize: 12
                        font.weight: Font.Bold
                    }

                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        id: actionTap
                        onTapped: notice.actionTriggered()
                    }
                }
            }
        }
    }

    GlassHost {
        window: notice
        sheet: notice.glass
        dragging: panel.dragging
    }
}
