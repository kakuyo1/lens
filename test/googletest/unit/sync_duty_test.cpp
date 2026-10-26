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
