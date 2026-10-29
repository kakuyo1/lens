pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window

/**
 * The online upload confirmation; the surface spec is UI-SPEC.md 4.11.
 *
 * It shows the frozen crop through the engine-owned image provider, names the exact destination,
 * and states both outbound legs before anything is sent. Escape, Cancel and Alt+F4 all mean no
 * upload; the first focused control is Cancel so a finished drag cannot confirm with Return.
 */
Window {
    id: consent
    objectName: "uploadConsent"

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: false

    readonly property int shadowMargin: 26
    readonly property int cardWidth: 420
    readonly property int cardHeight: 380
    readonly property SurfaceTokens tokens: SurfaceTokens {}

    /// The frozen request handed over by CaptureDuty; never a live screen reference.
    property var request: ({})
    property bool retrying: false
    property bool busy: false
    property string statusText: ""

    width: cardWidth + 2 * shadowMargin
    height: cardHeight + 2 * shadowMargin

    signal confirmed(double requestId)
    signal cancelled()

    function open(consentRequest) {
        request = consentRequest;
        retrying = false;
        busy = false;
        statusText = "";
        visible = true;
        x = Math.round(Math.max(0, (Screen.width - width) / 2));
        y = Math.round(Math.max(0, (Screen.height - height) / 2));
        raise();
        requestActivate();
        Qt.callLater(() => cancelButton.forceActiveFocus());
    }

    function showRetry(outcome) {
        retrying = true;
        busy = false;
        statusText = retryReason(outcome.status);
        visible = true;
        raise();
        requestActivate();
        Qt.callLater(() => cancelButton.forceActiveFocus());
    }

    function retryReason(status) {
        if (status === "timeout") return qsTranslate("UploadConsent", "The request timed out.");
        if (status === "transport-failed") return qsTranslate("UploadConsent", "The service could not be reached.");
        if (status === "busy") return qsTranslate("UploadConsent", "Another recognition is still running.");
        return qsTranslate("UploadConsent", "The upload did not finish.");
    }

    function close() {
        visible = false;
        request = ({});
        retrying = false;
        busy = false;
        statusText = "";
    }

    onClosing: (closeEvent) => {
        closeEvent.accepted = false;
        if (visible) {
            consent.cancelled();
            consent.close();
        }
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: consent.visible
        onActivated: {
            consent.cancelled();
            consent.close();
        }
    }

    ShadowCard {
        anchors.fill: parent
        radius: consent.tokens.radiusCard
        color: consent.tokens.panel

        Column {
            x: 20
            y: 18
            width: consent.cardWidth - 40
            spacing: 9

            Text {
                width: parent.width
                text: qsTranslate("UploadConsent", "Upload this selection?")
                color: consent.tokens.text
                font.pixelSize: 14
                font.weight: Font.Bold
            }

            Rectangle {
                width: parent.width
                height: 132
                radius: consent.tokens.radiusField
                color: consent.tokens.panel2
                border.width: 1
                border.color: consent.tokens.line

                Image {
                    anchors.fill: parent
                    anchors.margins: 6
                    source: consent.request.previewUrl !== undefined ? consent.request.previewUrl : ""
                    fillMode: Image.PreserveAspectFit
                    cache: false
                    asynchronous: false
                }
            }

            Text {
                width: parent.width
                text: qsTranslate("UploadConsent", "Destination: %1 (%2)")
                    .arg(consent.request.host !== undefined ? consent.request.host : "")
                    .arg(consent.request.provider !== undefined ? consent.request.provider : "")
                color: consent.tokens.text
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: qsTranslate("UploadConsent", "The selected pixels will be sent to %1. Sensitive content in the image may be visible to it.")
                    .arg(consent.request.provider !== undefined ? consent.request.provider : "")
                color: consent.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: qsTranslate("UploadConsent", "If recognition succeeds, the necessary text will then be sent to your explanation service.")
                color: consent.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: qsTranslate("UploadConsent", "The OCR service bills this request; it is not counted in Lens's model budget.")
                color: consent.tokens.faint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 8

                Chip {
                    id: cancelButton
                    objectName: "cancelUploadButton"
                    label: qsTranslate("UploadConsent", "Cancel")
                    onPicked: {
                        consent.cancelled();
                        consent.close();
                    }
                }

                Chip {
                    id: uploadButton
                    objectName: "confirmUploadButton"
                    visible: !consent.busy
                    selected: true
                    label: consent.retrying ? qsTranslate("UploadConsent", "Retry upload")
                                            : qsTranslate("UploadConsent", "Upload")
                    onPicked: {
                        consent.busy = true;
                        consent.statusText = qsTranslate("UploadConsent", "Uploading…");
                        consent.confirmed(consent.request.requestId);
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: consent.busy
                    text: qsTranslate("UploadConsent", "Uploading…")
                    color: consent.tokens.muted
                    font.pixelSize: 11
                }
            }

            Text {
                width: parent.width
                visible: consent.statusText !== ""
                text: consent.statusText
                color: consent.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Text {
                width: parent.width
                text: qsTranslate("UploadConsent", "Cancel stops this request; it cannot recall bytes the service has already received.")
                color: consent.tokens.faint
                font.pixelSize: 10
                wrapMode: Text.WordWrap
            }
        }
    }
}
