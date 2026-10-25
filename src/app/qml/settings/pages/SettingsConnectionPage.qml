import QtQuick

Item {
    id: page
    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property
    property var settings: Controller.settings

    property var draftOwner
    readonly property string apiDraft: draftOwner ? draftOwner.apiDraft : ""
    readonly property var openChildRect: null
    signal apiEdited(string value)
    /// The reader left the key field: the owner writes the draft and clears it.
    signal apiCommitted()
    implicitHeight: content.implicitHeight

    function closeChild() {}

    Column {
        id: content
        width: parent.width
        spacing: 16

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "API key")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            Rectangle {
                width: parent.width
                height: 35
                radius: page.tokens.radiusField
                color: page.tokens.panel2
                border.width: 1
                border.color: page.tokens.line

                TextInput {
                    id: apiInput
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: TextInput.AlignVCenter
                    text: page.apiDraft
                    echoMode: TextInput.Password
                    color: page.tokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    activeFocusOnTab: true
                    Accessible.name: qsTranslate("SettingsPopup", "API key")
                    onTextEdited: page.apiEdited(text)
                    onEditingFinished: {
                        page.apiCommitted();
                        // The first keystroke destroys the text binding; put it back so the field
                        // goes to the masked placeholder once the owner has cleared the draft.
                        text = Qt.binding(() => page.apiDraft);
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                }

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: Text.AlignVCenter
                    visible: apiInput.text.length === 0
                    text: page.settings.hasApiKey
                          ? "sk-" + "•".repeat(28)
                          : "sk-********************************"
                    color: page.tokens.faint
                    font: apiInput.font
                    elide: Text.ElideRight
                }
            }
            Text {
                text: qsTranslate("SettingsPopup", "The key is stored on this device only.")
                color: page.tokens.muted
                font.pixelSize: 11
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Service address")
                color: page.tokens.muted
                font.pixelSize: 12
            }
            Rectangle {
                width: parent.width
                height: 35
                radius: page.tokens.radiusField
                color: page.tokens.panel2
                border.width: 1
                border.color: page.tokens.line

                TextInput {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: TextInput.AlignVCenter
                    text: page.settings.url
                    color: page.tokens.text
                    font.pixelSize: 12
                    selectByMouse: true
                    activeFocusOnTab: true
                    Accessible.name: qsTranslate("SettingsPopup", "Service address")
                    onEditingFinished: Controller.setApiUrl(text)
                }
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "This address is a common one for the provider and can go stale; check it against their documentation.")
                color: page.tokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
}
