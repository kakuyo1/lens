import QtQuick
import QtTest
import Lens
import "../"
import "../testutil.js" as Util

/**
 * The account and sync sub-page, one state at a time.
 *
 * Each case puts a stand-in for the sync duty in front of the page. The real duty's state depends on
 * the client id in data/sync.json, a release setting that must not decide what a test sees. Nothing
 * here presses a control that signs in or copies, so no request reaches GitHub and nothing reaches
 * the clipboard.
 */
Item {
    id: root
    width: 380
    height: 720

    Component {
        id: pageComponent
        SettingsSyncPage { x: 26; y: 20; width: 328; height: implicitHeight }
    }

    TokenHost { id: host; width: root.width; height: root.height }

    /// Stands in for the sync duty. The state is set per case; the other fields are what the controller
    /// would report once GitHub has issued a code, or once the WebDAV form has been filled in.
    Component {
        id: dutyStub
        QtObject {
            property string state: "unconfigured"
            property string provider: "github"
            property string userCode: "ABCD-1234"
            property string verificationUri: "https://github.com/login/device"
            property string login: ""
            property string lastSyncedAt: ""
            property bool hasPassphrase: false
            property string webdavServer: ""
            property string webdavUsername: ""
            property bool hasWebdavPassword: false
            property bool webdavConnected: false
            property string error: ""
            function startLogin() {}
            function cancelLogin() {}
            function useProvider(provider) {}
            function setWebdavServer(server) {}
            function setWebdavUsername(username) {}
            function setWebdavAppPassword(password) {}
        }
    }

    TestCase {
        name: "SyncSettings"
        when: windowShown

        function make() {
            const page = createTemporaryObject(pageComponent, host.contentItem);
            waitForRendering(host.contentItem);
            wait(50);
            return page;
        }

        function named(page, name) {
            return Util.findAll(page, function (object) {
                return object.objectName === name;
            })[0];
        }

        function test_unconfiguredShowsOnlyTheNote() {
            const page = make();
            page.sync = createTemporaryObject(dutyStub, root, { state: "unconfigured" });
            wait(50);

            compare(named(page, "unconfiguredNote").visible, true);
            compare(named(page, "signedOutBlock").visible, false, "no sign-in button without a client id");
            compare(named(page, "codeBlock").visible, false);
            compare(named(page, "linkedBlock").visible, false, "no sync or log-out button without a sign-in");
            compare(named(page, "errorText").visible, false);
        }

        /// The error lines are the page's own copy, one per kind the controller can report.
        function test_everyErrorKindHasItsLine() {
            const page = make();
            compare(page.errorText(""), "");
            compare(page.errorText("proxy"), qsTranslate("SettingsPopup", "Needs a proxy from mainland China"));
            compare(page.errorText("passphrase"), qsTranslate("SettingsPopup", "The passphrase does not open the cloud copy"));
            compare(page.errorText("generic"), qsTranslate("SettingsPopup", "Sync failed; try again"));
            compare(page.errorText("expired"), qsTranslate("SettingsPopup", "The sign-in code expired"));
            compare(page.errorText("denied"), qsTranslate("SettingsPopup", "Sign-in was refused"));
            compare(page.errorText("noGist"), qsTranslate("SettingsPopup", "No cloud copy to delete"));
        }

        /// The code and the address are read-only text the reader can select, with buttons beside them.
        /// The copy and open buttons are only checked to exist: pressing them would write to the real
        /// clipboard or open a browser on the machine the test runs on.
        function test_codePendingShowsASelectableCodeAndAddress() {
            const page = make();
            page.sync = createTemporaryObject(dutyStub, root, { state: "codePending" });
            wait(50);

            compare(named(page, "codeBlock").visible, true);
            const address = named(page, "verificationUri");
            const code = named(page, "userCode");
            compare(address.text, "https://github.com/login/device");
            compare(address.readOnly, true);
            compare(address.selectByMouse, true);
            compare(code.text, "ABCD-1234");
            compare(code.readOnly, true);
            compare(code.selectByMouse, true);
            verify(named(page, "copyAddressButton"), "a button copies the address");
            verify(named(page, "copyCodeButton"), "a button copies the code");
            verify(named(page, "openBrowserButton"), "a button opens the address");
        }

        /// The picker reads the stored provider; the note and the WebDAV form split the unconfigured state.
        function test_webdavUnconfiguredShowsTheForm() {
            const page = make();
            page.sync = createTemporaryObject(dutyStub, root, { state: "unconfigured", provider: "webdav" });
            wait(50);

            compare(named(page, "providerField").currentIndex, 1, "the picker shows the stored choice");
            compare(named(page, "unconfiguredNote").visible, false, "the GitHub note is not the WebDAV answer");
            compare(named(page, "webdavFormBlock").visible, true);
            verify(named(page, "webdavServerInput"), "a field for the server address");
            verify(named(page, "webdavUsernameInput"), "a field for the username");
            verify(named(page, "webdavPasswordInput"), "a field for the app password");
            compare(named(page, "testConnectionButton").opacity, 0.45, "a probe without credentials does nothing");
        }

        /// With all three credentials the form arms the probe; the GitHub blocks stay out of the way.
        function test_webdavFormArmsTheProbeOnceTheCredentialsAreThere() {
            const page = make();
            page.sync = createTemporaryObject(dutyStub, root, {
                state: "unconfigured",
                provider: "webdav",
                webdavServer: "https://dav.example.com/dav",
                webdavUsername: "reader",
                hasWebdavPassword: true
            });
            wait(50);

            compare(named(page, "testConnectionButton").opacity, 1, "three credentials are enough to probe");
            compare(named(page, "linkedBlock").visible, false, "the linked block is the real duty's to show");
        }

        /// Signed in on WebDAV, the form stays (a wrong password is fixed in place, not by logging
        /// out) and the probe's confirmation line shows when the verdict is in.
        function test_webdavLinkedShowsTheFormAndTheVerdict() {
            const page = make();
            page.sync = createTemporaryObject(dutyStub, root, {
                state: "signedIn",
                provider: "webdav",
                webdavServer: "https://dav.example.com/dav",
                webdavConnected: true
            });
            wait(50);

            compare(named(page, "linkedBlock").visible, true);
            compare(named(page, "codeBlock").visible, false, "WebDAV has no device code to wait for");
            compare(named(page, "webdavFormBlock").visible, true, "the credentials are editable without a log-out");
            verify(named(page, "webdavServerInput"), "the server address sits in the editable field");
            compare(named(page, "webdavConnectedNote").visible, true);
        }

        /// The two provider-flavored error kinds read differently per provider.
        function test_errorLinesFollowTheProvider() {
            const page = make();
            page.sync = createTemporaryObject(dutyStub, root, { provider: "webdav" });
            compare(page.errorText("proxy"), qsTranslate("SettingsPopup", "The server could not be reached; check the address"));
            compare(page.errorText("signInExpired"), qsTranslate("SettingsPopup", "The server refused the credentials; check the form"));

            page.sync = createTemporaryObject(dutyStub, root, { provider: "github" });
            compare(page.errorText("proxy"), qsTranslate("SettingsPopup", "Needs a proxy from mainland China"));
            compare(page.errorText("signInExpired"), qsTranslate("SettingsPopup", "The GitHub sign-in expired; sign in again"));
        }
    }
}
