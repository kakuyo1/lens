import QtQuick
import QtQuick.Window
import "../util/Format.js" as Format

/**
 * The card a new version raises.
 *
 * A passive surface: no part in the panels' mutual exclusion, and never the explanation notice
 * slot, so a standing card neither replaces a notice nor is replaced by one. It can be dragged out
 * of the way like the panels; a drag only moves the window and answers nothing. Its close path
 * clears only the card's own flag -- skipping a version is the Skip button's separate decision.
 *
 * The card also carries the download: its state, its progress, and the one thing a reader can do
 * about it. View opens the release page; Download fetches the installer and Install runs it, only
 * after the file has been checked against the digest the same release publishes. The texts here are
 * the duty's states drawn as words -- nothing a transfer said reaches this surface.
 */
Window {
    id: card
    objectName: "update"
    /// The desktop under this window, which its glass draws (docs/adr/0041).
    readonly property var glass: Controller.glassBackdrop.surface("update")
    readonly property SurfaceTokens tokens: SurfaceTokens {
        glassEnabled: true
        glassLightText: card.glass.lightText
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 340

    /// The version on offer, the release page to open, and the notes to read.
    property string version: ""
    property string pageUrl: ""
    property string notes: ""

    /// ShadowCard fades it and hides the window when the fade is done; the action itself is
    /// reported at once, not after it.
    property bool leaving: false

    /// The download's own state, read straight off the duty that owns it; the card only shows it.
    readonly property var download: Controller.updateDuty.download

    /// Open the release page in the browser.
    signal viewRequested()
    /// Stop asking about this exact version.
    signal skipRequested()
    /// Fetch the installer for this version.
    signal downloadRequested()
    /// Stop the transfer and delete what it wrote.
    signal cancelRequested()
    /// Run the installer that has been checked.
    signal installRequested()

    width: cardWidth + 2 * shadowMargin
    height: column.implicitHeight + 36 + 2 * shadowMargin

    /// What the line under the title says, for one download state.
    ///
    /// A function of the state, so the surface never carries a transport's own words: failure text
    /// is one of three fixed sentences picked by key. `idle` with nothing to show says nothing.
    function downloadText(state, failure) {
        if (state === "downloading")
            return qsTranslate("UpdateCard", "Downloading...");
        if (state === "verifying")
            return qsTranslate("UpdateCard", "Checking the download...");
        if (state === "ready")
            return qsTranslate("UpdateCard", "Checksum verified. The installer is ready to run.");
        if (state !== "failed")
            return "";
        if (failure === "proxy")
            return qsTranslate("UpdateCard", "Downloads from GitHub usually need a proxy in mainland China. Turn on your proxy and try again.");
        if (failure === "checksum")
            return qsTranslate("UpdateCard", "The download did not match the published checksum. It was deleted.");
        return qsTranslate("UpdateCard", "Download failed. Check your connection.");
    }

    /// What the primary button says in this state, and whether it is there at all.
    function primaryAction(state, available) {
        if (state === "downloading")
            return qsTranslate("UpdateCard", "Cancel");
        if (state === "ready")
            return qsTranslate("UpdateCard", "Install");
        if (state === "failed" || state === "idle")
            return available ? qsTranslate("UpdateCard", "Download") : "";
        return "";
    }

    /// @brief The transfer's byte counts: the total is left out when the server states none.
    function bytesText(received, total) {
        return total > 0 ? Format.formatBytes(received) + " / " + Format.formatBytes(total)
                         : Format.formatBytes(received);
    }

    /// @brief The time left, written as a clock so no unit word has to be translated.
    function etaText(seconds) {
        if (!(seconds > 0))
            return "";
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor((seconds % 3600) / 60);
        const rest = seconds % 60;
        const clock = hours > 0
                ? hours + ":" + (minutes < 10 ? "0" : "") + minutes + ":" + (rest < 10 ? "0" : "") + rest
                : minutes + ":" + (rest < 10 ? "0" : "") + rest;
        return qsTranslate("UpdateCard", "%1 left").arg(clock);
    }

    /// @brief The right end of the progress detail: the share done, then the time left when the
    ///        duty can estimate it.
    function paceText(percent, etaSeconds) {
        const share = Math.round(percent) + "%";
        const eta = etaText(etaSeconds);
        return eta === "" ? share : share + " · " + eta;
    }

    /// Put the card in the middle of the current screen and raise it, the way Notice.qml does.
    function show(payload) {
        leaving = false
        version = payload.version || ""
        pageUrl = payload.pageUrl || ""
        notes = payload.notes || ""
        x = Math.round(Screen.virtualX + (Screen.width - width) / 2)
        y = Math.round(Screen.virtualY + (Screen.height - height) / 2)
        visible = true
        raise()
    }

    /// @brief Take the card down, fading it out before the window goes.
    ///
    /// The controller only hears about the cross from closeCard(); a card the duty has already
    /// cleared arrives here through dismiss(), and must not be sent back to it.
    function dismiss() {
        if (visible)
            leaving = true
    }

    /// Take the card down and write nothing: the cross means "not now", Skip means "not this version".
    function closeCard() {
        dismiss()
        Controller.updateDuty.closeCard()
    }

    /// Escape is the cross. The card's window takes no focus, so the buttons below carry names
    /// for the screen reader but no tab stops or rings -- there is no focus for either to mark.
    /// The shortcut still answers, application-wide, whenever a window of this app has the keys.
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: card.visible && !card.leaving
        onActivated: card.closeCard()
    }

    ShadowCard {
        id: panel
        anchors.fill: parent
        movable: true
        color: card.tokens.glassMode === "" ? card.tokens.panel : "transparent"
        enabled: !card.leaving

        GlassSurface {
            anchors.fill: parent
            sheet: card.glass
            mode: card.tokens.glassMode
            radius: panel.radius
        }

        Column {
            id: column
            x: 20
            y: 18
            width: card.cardWidth - 40
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
                    text: qsTranslate("UpdateCard", "Lens %1 is available").arg(card.version)
                    color: card.tokens.text
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }

                IconButton {
                    id: close
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    source: "qrc:/icons/ui-close.svg"
                    color: card.tokens.faint
                    accessibleName: qsTranslate("UpdateCard", "Close")
                    onTapped: card.closeCard()
                }
            }

            // Empty rather than zero-height when there is nothing to read: a positioner skips an
            // invisible child whole, and an empty Text still opens a gap.
            Text {
                width: parent.width
                text: card.notes
                visible: text !== ""
                color: card.tokens.faint
                font.pixelSize: 13
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            // What the download is doing, or why it stopped. Empty rather than zero-height when
            // there is nothing to say, for the reason the notes line above does it.
            Text {
                objectName: "downloadLine"
                width: parent.width
                text: card.downloadText(card.download.state, card.download.failure)
                visible: text !== ""
                color: card.download.state === "failed" ? card.tokens.danger : card.tokens.muted
                font.pixelSize: 12
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
                wrapMode: Text.WordWrap
            }

            // How the transfer is going, under what it is doing: a bar for the share done and a
            // line for the bytes and the time left. The bar is left out when the server states no
            // length; the byte count never is.
            Column {
                width: parent.width
                spacing: 6
                visible: card.download.state === "downloading"

                Rectangle {
                    id: track
                    width: parent.width
                    height: 4
                    radius: 2
                    color: card.tokens.line
                    visible: card.download.total > 0

                    Rectangle {
                        width: parent.width * Math.min(1, card.download.percent / 100)
                        height: parent.height
                        radius: parent.radius
                        color: card.tokens.ink

                        Behavior on width {
                            NumberAnimation {
                                duration: card.tokens.motion.press
                                easing.type: card.tokens.motion.easing
                            }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: bytesLabel.implicitHeight

                    Text {
                        id: bytesLabel
                        anchors.left: parent.left
                        text: card.bytesText(card.download.received, card.download.total)
                        color: card.tokens.faint
                        font.family: card.tokens.monoFamily
                        font.pixelSize: 11
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.left: bytesLabel.right
                        anchors.leftMargin: 8
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideLeft
                        text: card.download.total > 0 ? card.paceText(card.download.percent, card.download.etaSeconds) : ""
                        color: card.tokens.faint
                        font.family: card.tokens.monoFamily
                        font.pixelSize: 11
                    }
                }
            }

            Item {
                width: parent.width
                height: row.height

                Row {
                    id: row
                    width: (primaryLabel.text !== "" ? primary.width + 12 : 0) + viewButton.width + 12 + skipButton.width
                    height: 26
                    spacing: 12

                    // The one button that changes meaning: Download while the file can be fetched,
                    // Cancel while it is, Install once it has been checked. Nothing when this release
                    // is not downloadable -- an empty pill is a button that does nothing. Only
                    // Download and Install wear ink; Cancel is the transfer's least-wanted act, so
                    // it takes the secondary recipe at full opacity rather than the loudest style.
                    Rectangle {
                        id: primary
                        objectName: "primaryButton"
                        readonly property bool secondary: card.download.state === "downloading"
                        visible: primaryLabel.text !== ""
                        width: primaryLabel.implicitWidth + 26
                        height: 26
                        radius: card.tokens.radiusPill
                        color: secondary ? card.tokens.panel2 : card.tokens.ink
                        border.width: secondary ? 1 : 0
                        border.color: card.tokens.line
                        scale: primaryTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: card.tokens.motion.press
                                easing.type: card.tokens.motion.easing
                            }
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: primaryLabel.text
                        Accessible.onPressAction: primaryTap.tapped()

                        Text {
                            id: primaryLabel
                            anchors.centerIn: parent
                            text: card.primaryAction(card.download.state, card.download.available)
                            color: primary.secondary ? card.tokens.text : card.tokens.on
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: primaryTap
                            onTapped: {
                                if (card.download.state === "downloading")
                                    card.cancelRequested();
                                else if (card.download.state === "ready")
                                    card.installRequested();
                                else
                                    card.downloadRequested();
                            }
                        }
                    }

                    Rectangle {
                        id: viewButton
                        objectName: "viewButton"
                        width: viewLabel.width + 26
                        height: 26
                        radius: card.tokens.radiusPill
                        color: card.tokens.panel2
                        border.width: 1
                        border.color: card.tokens.line
                        scale: viewTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: card.tokens.motion.press
                                easing.type: card.tokens.motion.easing
                            }
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: viewLabel.text
                        Accessible.onPressAction: viewTap.tapped()

                        Text {
                            id: viewLabel
                            anchors.centerIn: parent
                            text: qsTranslate("UpdateCard", "View")
                            color: card.tokens.text
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: viewTap
                            onTapped: {
                                card.dismiss()
                                card.viewRequested()
                            }
                        }
                    }

                    Rectangle {
                        id: skipButton
                        width: skipLabel.width + 26
                        height: 26
                        radius: card.tokens.radiusPill
                        color: "transparent"
                        border.width: 1
                        border.color: card.tokens.line
                        scale: skipTap.pressed ? 0.97 : 1.0
                        Behavior on scale {
                            NumberAnimation {
                                duration: card.tokens.motion.press
                                easing.type: card.tokens.motion.easing
                            }
                        }

                        Accessible.role: Accessible.Button
                        Accessible.name: skipLabel.text
                        Accessible.onPressAction: skipTap.tapped()

                        Text {
                            id: skipLabel
                            anchors.centerIn: parent
                            text: qsTranslate("UpdateCard", "Skip this version")
                            color: card.tokens.text
                            font.pixelSize: 12
                            font.weight: Font.Bold
                        }

                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            id: skipTap
                            onTapped: {
                                card.dismiss()
                                card.skipRequested()
                            }
                        }
                    }
                }
            }
        }
    }

    GlassHost {
        window: card
        sheet: card.glass
        dragging: panel.dragging
    }
}