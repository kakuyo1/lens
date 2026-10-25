import QtQuick

Item {
    id: page

    // qmllint disable missing-property
    readonly property var tokens: Window.window.tokens
    // qmllint enable missing-property

    /// The duty the page reads. A test puts a stub here, so the sign-in states can be shown
    /// without signing in.
    property var sync: Controller.syncDuty
    readonly property bool linked: sync.state === "signedIn" || sync.state === "syncing"
    readonly property bool canSync: sync.state === "signedIn" && sync.hasPassphrase
    property bool confirmingDelete: false
    readonly property var openChildRect: null
    implicitHeight: content.implicitHeight

    function closeChild() {}

    /// @return The line for one sync error kind, or empty when the kind is empty or unknown.
    function errorText(kind) {
        if (kind === "proxy") return qsTranslate("SettingsPopup", "Needs a proxy from mainland China");
        if (kind === "passphrase") return qsTranslate("SettingsPopup", "The passphrase does not open the cloud copy");
        if (kind === "generic") return qsTranslate("SettingsPopup", "Sync failed; try again");
        if (kind === "expired") return qsTranslate("SettingsPopup", "The sign-in code expired");
        // Not the same thing as the line above: that one is the device code running out before the
        // reader approved it, this one is a stored token GitHub no longer accepts.
        if (kind === "signInExpired") return qsTranslate("SettingsPopup", "The GitHub sign-in expired; sign in again");
        if (kind === "denied") return qsTranslate("SettingsPopup", "Sign-in was refused");
        if (kind === "noGist") return qsTranslate("SettingsPopup", "No cloud copy to delete");
        return "";
    }

    Column {
        id: content
        width: parent.width
        spacing: 16

        Text {
            objectName: "unconfiguredNote"
            width: parent.width
            visible: page.sync.state === "unconfigured"
            text: qsTranslate("SettingsPopup", "Sync is not available in this build yet. The project has not registered its GitHub sign-in.")
            color: page.tokens.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }

        Column {
            id: signedOutBlock
            objectName: "signedOutBlock"
            width: parent.width
            spacing: 10
            visible: page.sync.state === "signedOut"
            height: visible ? implicitHeight : 0

            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Sync your settings and learning data with your GitHub account. The cloud copy is encrypted with your passphrase.")
                color: page.tokens.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Chip {
                objectName: "signInButton"
                label: qsTranslate("SettingsPopup", "Sign in with GitHub")
                selected: true
                onPicked: page.sync.startLogin()
            }
        }

        Column {
            id: codeBlock
            objectName: "codeBlock"
            width: parent.width
            spacing: 10
            visible: page.sync.state === "codePending"
            height: visible ? implicitHeight : 0

            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Open this address in a browser, then enter the code:")
                color: page.tokens.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            // Read-only TextEdits rather than Text: a Text cannot be selected, so the address and the
            // code could not be copied by hand. Copy buttons sit beside each one for the same reason.
            TextEdit {
                id: addressField
                objectName: "verificationUri"
                width: parent.width
                text: page.sync.verificationUri
                readOnly: true
                selectByMouse: true
                color: page.tokens.text
                font.pixelSize: 13
                wrapMode: TextEdit.WrapAnywhere
            }

            Row {
                spacing: 8

                Chip {
                    objectName: "copyAddressButton"
                    label: qsTranslate("SettingsPopup", "Copy address")
                    onPicked: {
                        addressField.selectAll();
                        addressField.copy();
                    }
                }

                Chip {
                    objectName: "openBrowserButton"
                    label: qsTranslate("SettingsPopup", "Open in browser")
                    onPicked: Qt.openUrlExternally(page.sync.verificationUri)
                }
            }

            TextEdit {
                id: codeField
                objectName: "userCode"
                text: page.sync.userCode
                readOnly: true
                selectByMouse: true
                color: page.tokens.text
                font.family: page.tokens.monoFamily
                font.pixelSize: 24
                font.weight: Font.Bold
            }

            Chip {
                objectName: "copyCodeButton"
                label: qsTranslate("SettingsPopup", "Copy code")
                onPicked: {
                    codeField.selectAll();
                    codeField.copy();
                }
            }

            Text {
                text: qsTranslate("SettingsPopup", "Waiting for GitHub to confirm")
                color: page.tokens.faint
                font.pixelSize: 11
            }

            Chip {
                objectName: "cancelLoginButton"
                label: qsTranslate("SettingsPopup", "Cancel")
                onPicked: page.sync.cancelLogin()
            }
        }

        Column {
            id: linkedBlock
            objectName: "linkedBlock"
            width: parent.width
            spacing: 10
            visible: page.linked
            height: visible ? implicitHeight : 0

            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Signed in as %1").arg(page.sync.login)
                color: page.tokens.text
                font.pixelSize: 13
                font.weight: Font.Bold
            }

            Text {
                width: parent.width
                text: page.sync.lastSyncedAt !== ""
                      ? qsTranslate("SettingsPopup", "Last synced %1").arg(page.sync.lastSyncedAt)
                      : qsTranslate("SettingsPopup", "Not synced yet")
                color: page.tokens.muted
                font.pixelSize: 12
            }

            Column {
                id: passphraseBlock
                objectName: "passphraseBlock"
                width: parent.width
                spacing: 7
                visible: !page.sync.hasPassphrase
                height: visible ? implicitHeight : 0

                Text {
                    text: qsTranslate("SettingsPopup", "Passphrase")
                    color: page.tokens.muted
                    font.pixelSize: 12
                }

                Rectangle {
                    width: parent.width
                    height: 35
                    radius: page.tokens.radiusField
                    color: page.tokens.panel2
                    border.width: 1
                    border.color: passphraseInput.activeFocus ? page.tokens.ink : page.tokens.line

                    TextInput {
                        id: passphraseInput
                        objectName: "passphraseInput"
                        anchors.fill: parent
                        anchors.leftMargin: 11
                        anchors.rightMargin: 11
                        verticalAlignment: TextInput.AlignVCenter
                        echoMode: TextInput.Password
                        color: page.tokens.text
                        font.pixelSize: 13
                        selectByMouse: true
                        activeFocusOnTab: true
                        Accessible.name: qsTranslate("SettingsPopup", "Passphrase")
                    }
                }

                Text {
                    width: parent.width
                    text: qsTranslate("SettingsPopup", "Encrypts the cloud copy. The passphrase is never stored; only a derived key stays on this device.")
                    color: page.tokens.faint
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                Chip {
                    objectName: "setPassphraseButton"
                    label: qsTranslate("SettingsPopup", "Set passphrase")
                    selected: true
                    onPicked: {
                        page.sync.setPassphrase(passphraseInput.text);
                        passphraseInput.text = "";
                    }
                }
            }

            Row {
                spacing: 8

                Chip {
                    objectName: "syncButton"
                    label: qsTranslate("SettingsPopup", "Sync now")
                    selected: true
                    opacity: page.canSync ? 1 : 0.45
                    onPicked: {
                        if (page.canSync)
                            page.sync.syncNow();
                    }
                }

                Chip {
                    objectName: "logOutButton"
                    label: qsTranslate("SettingsPopup", "Log out")
                    onPicked: page.sync.logOut()
                }
            }

            Text {
                width: parent.width
                text: qsTranslate("SettingsPopup", "Log out keeps this device's data. Delete cloud data removes the copy in your GitHub account.")
                color: page.tokens.faint
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }

            Row {
                spacing: 8
                visible: !page.confirmingDelete

                Chip {
                    objectName: "deleteButton"
                    label: qsTranslate("SettingsPopup", "Delete cloud data")
                    onPicked: page.confirmingDelete = true
                }
            }

            Row {
                spacing: 8
                visible: page.confirmingDelete

                Chip {
                    objectName: "confirmDeleteButton"
                    label: qsTranslate("SettingsPopup", "Confirm delete")
                    selected: true
                    onPicked: {
                        page.confirmingDelete = false;
                        page.sync.deleteCloudData();
                    }
                }

                Chip {
                    objectName: "cancelDeleteButton"
                    label: qsTranslate("SettingsPopup", "Cancel")
                    onPicked: page.confirmingDelete = false
                }
            }
        }

        Text {
            objectName: "errorText"
            width: parent.width
            visible: page.sync.error !== ""
            text: page.errorText(page.sync.error)
            color: page.tokens.danger
            font.pixelSize: 12
            wrapMode: Text.WordWrap
        }
    }
}
