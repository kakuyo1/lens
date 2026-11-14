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
#include <QObject>
#include <QString>

#include <filesystem>
#include <fstream>
#include <functional>

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
    QObject::connect(&duty, &lens::app::SyncDuty::remoteApplied, [&applied] { ++applied; });
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
