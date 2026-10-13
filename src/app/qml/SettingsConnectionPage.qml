import QtQuick

Item {
    id: page
    property var settings: Controller.settings

    property var draftOwner
    readonly property string apiDraft: draftOwner ? draftOwner.apiDraft : ""
    readonly property var openChildRect: null
    signal apiEdited(string value)
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
                color: SettingsTokens.muted
                font.pixelSize: 12
            }
            Rectangle {
                width: parent.width
                height: 35
                radius: SettingsTokens.radiusField
                color: SettingsTokens.panel2
                border.width: 1
                border.color: SettingsTokens.line

                TextInput {
                    id: apiInput
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: TextInput.AlignVCenter
                    text: page.apiDraft
                    echoMode: TextInput.Password
                    color: SettingsTokens.text
                    font.pixelSize: 13
                    selectByMouse: true
                    onTextEdited: page.apiEdited(text)
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
                    color: SettingsTokens.faint
                    font: apiInput.font
                    elide: Text.ElideRight
                }
            }
            Text {
                text: qsTranslate("SettingsPopup", "The key is stored on this device only.")
                color: SettingsTokens.muted
                font.pixelSize: 11
            }
        }

        Column {
            width: parent.width
            spacing: 7
            Text {
                text: qsTranslate("SettingsPopup", "Service address")
                color: SettingsTokens.muted
                font.pixelSize: 12
            }
            Rectangle {
                width: parent.width
                height: 35
                radius: SettingsTokens.radiusField
                color: SettingsTokens.panel2
                border.width: 1
                border.color: SettingsTokens.line

                TextInput {
                    anchors.fill: parent
                    anchors.leftMargin: 11
                    anchors.rightMargin: 11
                    verticalAlignment: TextInput.AlignVCenter
                    text: page.settings.url
                    color: SettingsTokens.text
                    font.pixelSize: 12
                    selectByMouse: true
                    onEditingFinished: Controller.setApiUrl(text)
                }
            }
            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "This address is a common one for the provider and can go stale; check it against their documentation.")
                color: SettingsTokens.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
}
