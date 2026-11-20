/**
 * @file sync_duty_test.cpp
 * @brief The duty as the adapter: a sync now drives one round through the real clients, and the
 *        surfaces it owns follow the round's hooks.
 *
 * The network is scripted, the document is a scratch file, and the client id is a fixture in a
 * temporary data directory, so nothing here reaches GitHub or the reader's own profile.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkReply>
#include <QObject>
#include <QString>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>

#include <gtest/gtest.h>

#include "app/duties/storage_duty.h"
#include "app/duties/sync_backend.h"
#include "app/duties/sync_duty.h"
#include "core/crypto/vault.h"
#include "core/document/known_store.h"
#include "core/domain/sync_partition.h"
#include "scripted_manager.h"

namespace {

namespace vault   = lens::core::vault;
namespace syncrun = lens::core::syncrun;
using lens::core::KnownStore;
using lens::core::sync::SyncPartition;

struct QtApplication {
    int              argc     = 1;
    char             name[24] = "sync_duty_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

vault::KdfParams quickParams()
{
    vault::KdfParams params = vault::newParams();
    params.iterations       = 1000;
    return params;
}

syncrun::StoredKey aKey(const char* passphrase, const vault::KdfParams& params)
{
    return {vault::deriveKey(passphrase, params), params};
}

bool spinUntil(const std::function<bool()>& done, int ms = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

/// @brief Abort every reply the manager is still holding, as a connection that never got a byte does.
void dropTheConnection(lens::test::ScriptedManager& manager)
{
    for (auto* reply : manager.findChildren<QNetworkReply*>())
        if (!reply->isFinished())
            reply->abort();
}

/// The device-code answer the sign-in cases start from: a one-second poll, so a case reaches the
/// pending state without waiting the production five.
const QByteArray kCodeBody = R"({"device_code":"d","user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device",)"
                             R"("expires_in":900,"interval":1})";

/// @brief The duty over a scratch document, with the client id fixture the backend reads and the
///        network scripted; what a case seeds it with is the only thing that differs.
struct Rig {
    /// @param name         The scratch directory under the system temp directory.
    /// @param withClientId Whether `sync.json` carries the OAuth app's id; false is a fresh install.
    /// @param webdav       Whether the stored provider is the reader's own server rather than GitHub.
    explicit Rig(std::string const& name, bool const withClientId = true, bool const webdav = false) // NOLINT(bugprone-easily-swappable-parameters): the two flags are named at every call site and mean different things; one enum would say less than the two names do
        : dir(std::filesystem::temp_directory_path() / name)
    {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        if (withClientId) {
            std::ofstream config(dir / "sync.json");
            config << R"({"clientId":"client-id"})";
        }
        store     = std::make_unique<KnownStore>(KnownStore::load(dir / "settings.json"));
        partition = std::make_unique<SyncPartition>(*store);
        if (webdav) {
            partition->setProvider("webdav");
            partition->setServer("https://dav.example.com/dav");
            partition->setUsername("reader");
            partition->setAppPassword("app-pass");
        }
        storage = std::make_unique<lens::app::StorageDuty>(*store);
        duty    = std::make_unique<lens::app::SyncDuty>(*storage, dir, &manager);
    }

    ~Rig()
    {
        // The duty holds references to the storage and the partition, so it goes first.
        duty.reset();
        storage.reset();
        partition.reset();
        store.reset();
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }

    std::filesystem::path                   dir;
    std::unique_ptr<KnownStore>             store;
    std::unique_ptr<SyncPartition>          partition;
    std::unique_ptr<lens::app::StorageDuty> storage;
    /// A connection that never answers unless the case queued an answer: the proxy case is what
    /// this default is for, and every other case queues the one reply it needs.
    lens::test::ScriptedManager          manager{302, {}, false};
    std::unique_ptr<lens::app::SyncDuty> duty;
};

}

/// The settings page's Sync now, end to end: the duty starts a round, the round lists and creates
/// through the real clients, and the remote apply reaches the listener the surfaces hang on.
TEST(SyncDuty, ASyncNowCarriesOneRoundThroughTheScriptedNetwork)
{
    QtApplication qt;
    const auto    dir = std::filesystem::temp_directory_path() / "lens-sync-duty-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream config(dir / "sync.json");
        config << R"({"clientId":"client-id"})";
    }
    const auto settingsPath = dir / "settings.json";
    std::filesystem::remove(settingsPath);

    KnownStore             store = KnownStore::load(settingsPath);
    lens::app::StorageDuty storage(store);
    // What a previous session would have left: the derived key and the token pair.
    SyncPartition seeded(store);
    seeded.storeKey(aKey("a fixture passphrase", quickParams()));
    seeded.storeTokens("access-1", "refresh-1");

    lens::test::ScriptedManager manager;
    lens::app::SyncDuty         duty(storage, dir, &manager);
    int                         applied = 0;
    bool                        synced  = false;
    bool                        manual  = false;
    QObject::connect(&duty, &lens::app::SyncDuty::remoteApplied, [&applied] { ++applied; });
    QObject::connect(&duty, &lens::app::SyncDuty::synced, [&](bool asked) {
        synced = true;
        manual = asked;
    });
    EXPECT_EQ(duty.state(), QStringLiteral("signedIn"));

    manager.queue(200, "[]");
    manager.queue(201, R"({"id":"gist-1","description":"lens-sync","updated_at":"2026-10-26T00:00:00Z",)"
                       R"("files":{"lens-sync.json":{"content":"{}"}}})");
    duty.syncNow();

    EXPECT_EQ(duty.state(), QStringLiteral("syncing"));
    ASSERT_TRUE(spinUntil([&] { return duty.state() != QStringLiteral("syncing"); }));

    EXPECT_EQ(manager.requests, 2);
    EXPECT_EQ(duty.state(), QStringLiteral("signedIn"));
    EXPECT_TRUE(duty.error().isEmpty());
    EXPECT_FALSE(duty.lastSyncedAt().isEmpty()) << "the page shows the minute the round succeeded";
    EXPECT_TRUE(duty.hasPassphrase());
    EXPECT_EQ(applied, 1) << "the surfaces are told to reload the document once";
    EXPECT_TRUE(synced) << "a successful sync tells the event channel it may run";
    EXPECT_TRUE(manual) << "Sync now is a reader-asked run";
    EXPECT_EQ(seeded.gistId(), "gist-1") << "the round wrote the new id into the same document";

    std::filesystem::remove_all(dir);
}

/// The provider key is device-local config: a fresh document means GitHub, and a stored value reads back.
TEST(SyncDuty, ProviderDefaultsToGithubAndReadsBack)
{
    QtApplication qt;
    const auto    dir = std::filesystem::temp_directory_path() / "lens-sync-provider-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto settingsPath = dir / "settings.json";
    std::filesystem::remove(settingsPath);

    KnownStore    store = KnownStore::load(settingsPath);
    SyncPartition partition(store);
    EXPECT_EQ(partition.provider(), "github");

    // The choice UI arrives with the second backend, so the document is seeded directly here.
    store.section("sync").set<std::string>("provider", "webdav");
    EXPECT_EQ(partition.provider(), "webdav");

    std::filesystem::remove_all(dir);
}

/// The tokens and the app password are the secrets the settings document holds at rest, so the
/// document carries the DPAPI seal (docs/adr/0177), and the plaintext only ever answers through
/// the partition's own reads.
TEST(SyncPartition, StoresTheSecretsSealedAndReadsThemBack)
{
    QtApplication qt;
    const auto    dir = std::filesystem::temp_directory_path() / "lens-sync-seal-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto settingsPath = dir / "settings.json";
    std::filesystem::remove(settingsPath);

    KnownStore    store = KnownStore::load(settingsPath);
    SyncPartition seeded(store);
    seeded.storeTokens("access-1", "refresh-1");
    seeded.setAppPassword("app-pass");

    const nlohmann::json& sync = store.document().at("sync");
    EXPECT_EQ(seeded.token(), "access-1");
    EXPECT_EQ(seeded.refreshToken(), "refresh-1");
    EXPECT_EQ(seeded.appPassword(), "app-pass");

    // The stored value carries the seal, not the plaintext.
    const std::string token       = sync.at("token").get<std::string>();
    const std::string refresh     = sync.at("refreshToken").get<std::string>();
    const std::string appPassword = sync.at("appPassword").get<std::string>();
    EXPECT_EQ(token.rfind("dpapi:", 0), 0);
    EXPECT_EQ(refresh.rfind("dpapi:", 0), 0);
    EXPECT_EQ(appPassword.rfind("dpapi:", 0), 0);
    EXPECT_EQ(token.find("access-1"), std::string::npos)
        << "the document never carries the plaintext";

    std::filesystem::remove_all(dir);
}

/// A build before the seal wrote the secrets in the clear, and an upgraded document still carries
/// them: the reads answer the plaintext until the next write seals it, so nothing breaks between.
TEST(SyncPartition, AnswersABareValueALegacyBuildWrote)
{
    QtApplication qt;
    const auto    dir = std::filesystem::temp_directory_path() / "lens-sync-bare-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto settingsPath = dir / "settings.json";
    std::filesystem::remove(settingsPath);

    KnownStore store = KnownStore::load(settingsPath);
    store.section("sync").set<std::string>("token", "bare-token");
    store.section("sync").set<std::string>("refreshToken", "bare-refresh");
    store.section("sync").set<std::string>("appPassword", "app-pass");

    SyncPartition partition(store);
    EXPECT_EQ(partition.token(), "bare-token");
    EXPECT_EQ(partition.refreshToken(), "bare-refresh");
    EXPECT_EQ(partition.appPassword(), "app-pass");
    EXPECT_TRUE(partition.signedIn());

    std::filesystem::remove_all(dir);
}

/// The startup pass seals the bare values once, keeps an absent secret absent, and passes over a
/// value already sealed -- a second seal would only nest one around another.
TEST(SyncPartition, SealsTheBareSecretsWhenStartupAsks)
{
    QtApplication qt;
    const auto    dir = std::filesystem::temp_directory_path() / "lens-sync-migrate-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto settingsPath = dir / "settings.json";
    std::filesystem::remove(settingsPath);

    KnownStore store = KnownStore::load(settingsPath);
    store.section("sync").set<std::string>("token", "bare-token");
    store.section("sync").set<std::string>("appPassword", "app-pass");

    SyncPartition partition(store);
    partition.sealBareSecrets();

    const nlohmann::json& sync        = store.document().at("sync");
    const std::string     token       = sync.at("token").get<std::string>();
    const std::string     appPassword = sync.at("appPassword").get<std::string>();
    EXPECT_EQ(partition.token(), "bare-token");
    EXPECT_EQ(partition.appPassword(), "app-pass");
    EXPECT_EQ(token.rfind("dpapi:", 0), 0);
    EXPECT_EQ(appPassword.rfind("dpapi:", 0), 0);
    EXPECT_FALSE(sync.contains("refreshToken")) << "an absent secret stays absent";

    partition.sealBareSecrets();
    EXPECT_EQ(partition.token(), "bare-token") << "the second pass did not seal the seal again";

    std::filesystem::remove_all(dir);
}

/// A seal another account wrote opens to nothing: the reads answer empty, so the page reads as
/// signed out -- the honest state under an account the seal does not cover.
TEST(SyncPartition, ASealThisAccountCannotOpenReadsAsEmpty)
{
    QtApplication qt;
    const auto    dir = std::filesystem::temp_directory_path() / "lens-sync-deadseal-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto settingsPath = dir / "settings.json";
    std::filesystem::remove(settingsPath);

    KnownStore         store = KnownStore::load(settingsPath);
    const vault::Bytes garbage{0x01, 0x02, 0x03};
    store.section("sync").set<std::string>("token", "dpapi:" + vault::encodeBase64(garbage));

    SyncPartition partition(store);
    EXPECT_EQ(partition.token(), "");
    EXPECT_FALSE(partition.signedIn());
    EXPECT_FALSE(partition.hasRefreshToken());

    std::filesystem::remove_all(dir);
}

/// A provider the factory does not know falls back to GitHub instead of leaving the duty without a backend.
TEST(SyncDuty, UnknownProviderFallsBackToGithub)
{
    QtApplication qt;
    const auto    dir = std::filesystem::temp_directory_path() / "lens-sync-provider-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto settingsPath = dir / "settings.json";
    std::filesystem::remove(settingsPath);

    KnownStore    store = KnownStore::load(settingsPath);
    SyncPartition partition(store);
    store.section("sync").set<std::string>("provider", "gitee");

    lens::test::ScriptedManager manager;
    auto                        backend = lens::app::makeSyncBackend(partition.provider(), dir, &manager, partition);
    EXPECT_EQ(backend->id(), QStringLiteral("github"));
    EXPECT_FALSE(backend->configured()) << "no sync.json in the directory: nothing is configured yet";

    std::filesystem::remove_all(dir);
}

/// Picking the other storage provider rebuilds the backend around the stored choice, and both
/// providers' credentials survive the switch on this device.
TEST(SyncDuty, SwitchingTheProviderRebuildsTheBackendAndKeepsBothCredentials)
{
    QtApplication qt;
    const auto    dir = std::filesystem::temp_directory_path() / "lens-sync-switch-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    {
        std::ofstream config(dir / "sync.json");
        config << R"({"clientId":"client-id"})";
    }
    const auto settingsPath = dir / "settings.json";
    std::filesystem::remove(settingsPath);

    KnownStore             store = KnownStore::load(settingsPath);
    lens::app::StorageDuty storage(store);
    SyncPartition          seeded(store);
    seeded.storeTokens("access-1", "refresh-1"); // GitHub's sign-in, still on this device
    seeded.setProvider("webdav");
    seeded.setServer("https://dav.example.com/dav");
    seeded.setUsername("reader");
    seeded.setAppPassword("app-pass");

    lens::test::ScriptedManager manager;
    lens::app::SyncDuty         duty(storage, dir, &manager);
    EXPECT_EQ(duty.provider(), QStringLiteral("webdav"));
    EXPECT_EQ(duty.state(), QStringLiteral("signedIn")) << "the WebDAV credentials are the sign-in";

    duty.useProvider(QStringLiteral("github"));
    EXPECT_EQ(duty.provider(), QStringLiteral("github"));
    EXPECT_EQ(duty.state(), QStringLiteral("signedIn")) << "GitHub's token pair is still on this device";
    EXPECT_TRUE(duty.error().isEmpty());

    duty.useProvider(QStringLiteral("webdav"));
    EXPECT_EQ(duty.provider(), QStringLiteral("webdav"));
    EXPECT_EQ(duty.state(), QStringLiteral("signedIn"));

    std::filesystem::remove_all(dir);
}

/// A paste that carries invisible whitespace cannot poison the credentials: every form field trims.
TEST(SyncDuty, WebdavFormFieldsTrimWhatTheyStore)
{
    QtApplication qt;
    const auto    dir = std::filesystem::temp_directory_path() / "lens-sync-trim-test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto settingsPath = dir / "settings.json";
    std::filesystem::remove(settingsPath);

    KnownStore             store = KnownStore::load(settingsPath);
    lens::app::StorageDuty storage(store);
    SyncPartition          seeded(store);

    lens::test::ScriptedManager manager;
    lens::app::SyncDuty         duty(storage, dir, &manager);
    duty.setWebdavAppPassword("\tapp-pass\t");
    duty.setWebdavUsername(" reader ");
    duty.setWebdavServer("  https://dav.example.com/dav ");

    EXPECT_EQ(seeded.appPassword(), "app-pass");
    EXPECT_EQ(seeded.username(), "reader");
    EXPECT_EQ(seeded.server(), "https://dav.example.com/dav");

    std::filesystem::remove_all(dir);
}

/// A build with no OAuth app yet is the page's "unconfigured", and every control is a no-op rather
/// than a request that cannot work.
TEST(SyncDuty, ADeviceWithNoClientIdReadsAsUnconfiguredAndAsksNothing)
{
    QtApplication qt;
    Rig           rig("lens-sync-unconfigured-test", /*withClientId=*/false);

    EXPECT_EQ(rig.duty->state(), QStringLiteral("unconfigured"));
    rig.duty->startLogin();
    rig.duty->syncNow();
    rig.duty->deleteCloudData();

    EXPECT_EQ(rig.manager.requests, 0) << "nothing is configured, so nothing is asked";
    EXPECT_TRUE(rig.duty->error().isEmpty());
}

/// A configured device nobody has signed in on reads as "signedOut": Sync now does nothing, and a
/// passphrase alone is not a sign-in, so it is refused where it stands.
TEST(SyncDuty, ASignedOutDeviceAsksForThePassphraseAndSendsNothing)
{
    QtApplication qt;
    Rig           rig("lens-sync-signedout-test");

    EXPECT_EQ(rig.duty->state(), QStringLiteral("signedOut"));
    rig.duty->syncNow();
    EXPECT_TRUE(rig.duty->error().isEmpty()) << "no sign-in, no failure to report";

    rig.duty->setPassphrase(QString{});
    EXPECT_EQ(rig.duty->error(), QStringLiteral("passphrase"));
    rig.duty->setPassphrase(QStringLiteral("a fixture passphrase"));
    EXPECT_EQ(rig.manager.requests, 0) << "a passphrase cannot stand in for a sign-in";
}

/// The device flow through the duty: the code is the page's "codePending" with the code and the
/// address to show, and calling it off takes both down without a report.
TEST(SyncDuty, ACodeTheReaderNeverEnteredHoldsThePageUntilItIsCalledOff)
{
    QtApplication qt;
    Rig           rig("lens-sync-codepending-test");
    rig.manager.queue(200, kCodeBody);

    rig.duty->startLogin();
    ASSERT_TRUE(spinUntil([&] { return !rig.duty->userCode().isEmpty(); }));
    EXPECT_EQ(rig.duty->state(), QStringLiteral("codePending"));
    EXPECT_EQ(rig.duty->userCode(), QStringLiteral("WDJB-MJHT"));
    EXPECT_EQ(rig.duty->verificationUri(), QStringLiteral("https://github.com/login/device"));

    rig.duty->cancelLogin();
    EXPECT_EQ(rig.duty->state(), QStringLiteral("signedOut"));
    EXPECT_TRUE(rig.duty->userCode().isEmpty());
    EXPECT_TRUE(rig.duty->error().isEmpty()) << "a cancel reports nothing";

    // Both timers stop with the code: the poll that would have gone out a second later does not.
    QElapsedTimer quiet;
    quiet.start();
    while (quiet.elapsed() < 1300)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_EQ(rig.manager.requests, 1) << "a cancelled sign-in polls no more";
}

/// A sign-in the reader declined reaches the page as its own error code, with the code taken down.
TEST(SyncDuty, ADeclinedSignInLandsOnThePagesError)
{
    QtApplication qt;
    Rig           rig("lens-sync-declined-test");
    rig.manager.queue(200, kCodeBody);
    rig.manager.queue(200, QByteArrayLiteral(R"({"error":"access_denied"})"));

    rig.duty->startLogin();
    ASSERT_TRUE(spinUntil([&] { return !rig.duty->error().isEmpty(); }, 8000));

    EXPECT_EQ(rig.duty->error(), QStringLiteral("denied"));
    EXPECT_EQ(rig.duty->state(), QStringLiteral("signedOut"));
    EXPECT_TRUE(rig.duty->userCode().isEmpty());
}

/// A sync whose connection never answered is the proxy code, and it is not a sign-out: the stored
/// token is left where it is for the retry.
TEST(SyncDuty, AConnectionThatNeverAnsweredLeavesTheProxyCodeAndKeepsTheSignIn)
{
    QtApplication qt;
    Rig           rig("lens-sync-proxy-test");
    rig.partition->storeTokens("access-1", "refresh-1");
    rig.partition->storeKey(aKey("a fixture passphrase", quickParams()));

    rig.duty->syncNow();
    ASSERT_TRUE(spinUntil([&] { return rig.manager.requests == 1; }));
    dropTheConnection(rig.manager);
    ASSERT_TRUE(spinUntil([&] { return !rig.duty->error().isEmpty(); }));

    EXPECT_EQ(rig.duty->error(), QStringLiteral("proxy"));
    EXPECT_EQ(rig.duty->state(), QStringLiteral("signedIn")) << "an unreachable server is not a sign-out";
    EXPECT_TRUE(rig.partition->signedIn());
    EXPECT_TRUE(rig.partition->hasKey());
}

/// A delete with no cloud copy behind it names the missing gist rather than asking; re-picking the
/// provider already in force is not a change, and a signed-out device has nothing to delete at all.
TEST(SyncDuty, ADeleteWithoutACloudCopyNamesTheMissingGistAndARepickChangesNothing)
{
    QtApplication qt;
    Rig           rig("lens-sync-nogist-test");
    rig.partition->storeTokens("access-1", "refresh-1"); // signed in, nothing uploaded yet

    rig.duty->deleteCloudData();
    EXPECT_EQ(rig.duty->error(), QStringLiteral("noGist"));
    EXPECT_EQ(rig.manager.requests, 0) << "there is no id to delete";

    rig.duty->useProvider(QStringLiteral("github"));
    EXPECT_EQ(rig.duty->error(), QStringLiteral("noGist")) << "the same provider is not a rebuilt backend";

    rig.duty->logOut();
    rig.duty->deleteCloudData();
    EXPECT_TRUE(rig.duty->error().isEmpty());
    EXPECT_EQ(rig.manager.requests, 0);
}

/// A deleted cloud copy wakes the event channel and not the surfaces' synced line, and the sign-in
/// that allowed the delete stays: deleting the copy is not signing out.
TEST(SyncDuty, ADeletedCloudCopyTakesItsSignalAndLeavesTheSignInAlone)
{
    QtApplication qt;
    Rig           rig("lens-sync-delete-test");
    rig.partition->storeTokens("access-1", "refresh-1");
    rig.partition->setGistId("gist-1");

    int deleted = 0;
    int synced  = 0;
    QObject::connect(rig.duty.get(), &lens::app::SyncDuty::cloudDeleted, [&deleted] { ++deleted; });
    QObject::connect(rig.duty.get(), &lens::app::SyncDuty::synced, [&synced](bool) { ++synced; });

    rig.manager.queue(204);
    rig.duty->deleteCloudData();
    ASSERT_TRUE(spinUntil([&] { return deleted > 0; }));

    EXPECT_EQ(synced, 0) << "a delete is the event channel's, not a sync of the reader's";
    EXPECT_TRUE(rig.partition->gistId().empty());
    EXPECT_TRUE(rig.partition->signedIn()) << "the token is not the cloud copy's";
    EXPECT_TRUE(rig.duty->error().isEmpty());
    ASSERT_EQ(rig.manager.sent.size(), 1u);
    EXPECT_EQ(rig.manager.sent[0].verb, "DELETE");
}

/// Signing out forgets the token, the key, the login name and the cloud copy's id, and asks nothing
/// of the network: the copy is left where it is.
TEST(SyncDuty, LoggingOutForgetsTheTokenTheKeyAndTheCloudCopysIdentity)
{
    QtApplication qt;
    Rig           rig("lens-sync-logout-test");
    rig.partition->storeTokens("access-1", "refresh-1");
    rig.partition->storeKey(aKey("a fixture passphrase", quickParams()));
    rig.partition->setGistId("gist-1");
    rig.partition->setLogin("reader");
    ASSERT_EQ(rig.duty->state(), QStringLiteral("signedIn"));

    rig.duty->logOut();

    EXPECT_EQ(rig.duty->state(), QStringLiteral("signedOut"));
    EXPECT_FALSE(rig.duty->hasPassphrase());
    EXPECT_TRUE(rig.duty->login().isEmpty());
    EXPECT_FALSE(rig.partition->signedIn());
    EXPECT_TRUE(rig.partition->gistId().empty());
    EXPECT_TRUE(rig.duty->error().isEmpty());
    EXPECT_EQ(rig.manager.requests, 0) << "a sign-out is local";
}

/// The WebDAV form's confirmation line answers the credentials that were probed: a refusal of the
/// new password takes the line down with the error the form can fix.
TEST(SyncDuty, AWebdavProbeRefusalTakesTheConnectedLineDownWithItsError)
{
    QtApplication qt;
    Rig           rig("lens-sync-webdav-probe-test", /*withClientId=*/true, /*webdav=*/true);

    rig.manager.queue(207);
    rig.duty->startLogin();
    ASSERT_TRUE(spinUntil([&] { return rig.duty->webdavConnected(); }));

    rig.manager.queue(401);
    rig.duty->startLogin();
    ASSERT_TRUE(spinUntil([&] { return !rig.duty->error().isEmpty(); }));
    EXPECT_EQ(rig.duty->error(), QStringLiteral("signInExpired"));
    EXPECT_FALSE(rig.duty->webdavConnected());
}
