/**
 * @file webdav_backend_test.cpp
 * @brief The WebDAV backend behind the provider seam: what being signed in means, the reachability
 *        check, and one round carried through the scripted network (docs/adr/0157).
 *
 * The network is scripted, the document is a scratch file, and the credentials are fixtures, so
 * nothing here reaches a server.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QString>

#include <filesystem>
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

namespace vault = lens::core::vault;
using lens::core::KnownStore;
using lens::core::sync::SyncPartition;

struct QtApplication {
    int              argc     = 1;
    char             name[24] = "webdav_backend_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

vault::KdfParams quickParams()
{
    vault::KdfParams params = vault::newParams();
    params.iterations       = 1000;
    return params;
}

bool spinUntil(const std::function<bool()>& done, int ms = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

/// A scratch settings document with the WebDAV credentials and a derived key already on it.
struct Fixture {
    std::filesystem::path          dir;
    std::unique_ptr<KnownStore>    store;
    std::unique_ptr<SyncPartition> partition;

    explicit Fixture(const std::string& name)
        : dir(std::filesystem::temp_directory_path() / name)
    {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        store     = std::make_unique<KnownStore>(KnownStore::load(dir / "settings.json"));
        partition = std::make_unique<SyncPartition>(*store);
        partition->setProvider("webdav");
        partition->setServer("https://dav.example.com/dav");
        partition->setUsername("reader");
        partition->setAppPassword("app-pass");
        vault::KdfParams params = vault::newParams();
        params.iterations       = 1000;
        partition->storeKey({vault::deriveKey("a fixture passphrase", params), params});
    }

    ~Fixture()
    {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
};

}

/// The factory hands the stored provider's backend back; the webdav branch is its own class.
TEST(WebdavBackend, TheFactoryBuildsItForTheStoredProvider)
{
    QtApplication qt;
    Fixture       fixture("lens-webdav-factory-test");

    lens::test::ScriptedManager manager;
    auto                        backend = lens::app::makeSyncBackend(fixture.partition->provider(), fixture.dir, &manager, *fixture.partition);
    EXPECT_EQ(backend->id(), QStringLiteral("webdav"));
    EXPECT_TRUE(backend->configured());
    EXPECT_TRUE(backend->signedIn()) << "the three credentials together are the sign-in";
}

/// One credential missing, or a sign-out, takes the page back to the form.
TEST(WebdavBackend, SignedInMeansAllThreeCredentialsStored)
{
    QtApplication qt;
    Fixture       fixture("lens-webdav-signedin-test");

    lens::test::ScriptedManager manager;
    auto                        backend = lens::app::makeSyncBackend(fixture.partition->provider(), fixture.dir, &manager, *fixture.partition);
    ASSERT_TRUE(backend->signedIn());

    fixture.partition->setAppPassword("");
    EXPECT_FALSE(backend->signedIn());
    EXPECT_FALSE(backend->configured());
    fixture.partition->setAppPassword("app-pass");
    fixture.partition->forgetAll();
    EXPECT_FALSE(backend->signedIn()) << "a sign-out forgets the credentials too";
}

/// Test connection is one probe: the server answering is the completed sign-in, a refusal maps.
TEST(WebdavBackend, TestConnectionProbesTheServerOnce)
{
    QtApplication qt;
    Fixture       fixture("lens-webdav-probe-test");

    lens::test::ScriptedManager manager;
    auto                        backend = lens::app::makeSyncBackend(fixture.partition->provider(), fixture.dir, &manager, *fixture.partition);

    QString error;
    int     completed = 0;
    QObject::connect(backend.get(), &lens::app::SyncBackend::completed, [&completed] { ++completed; });
    QObject::connect(backend.get(), &lens::app::SyncBackend::failed, [&error](const QString& code) { error = code; });

    manager.queue(207);
    backend->startLogin();
    ASSERT_TRUE(spinUntil([&] { return completed == 1; }));
    EXPECT_EQ(manager.requests, 1);
    EXPECT_EQ(manager.sent[0].verb, "PROPFIND");
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://dav.example.com/dav") << "the probe asks the server root";
    EXPECT_TRUE(error.isEmpty());

    manager.queue(401);
    backend->startLogin();
    ASSERT_TRUE(spinUntil([&] { return not error.isEmpty(); }));
    EXPECT_EQ(error, QStringLiteral("signInExpired")) << "the page reads the refusal as credentials to fix";
}

/// The probe's verdict surfaces as the form's confirmation line, and a changed field takes it down.
TEST(WebdavBackend, TheProbeVerdictLivesOnTheDutyUntilAFieldChanges)
{
    QtApplication          qt;
    Fixture                fixture("lens-webdav-verdict-test");
    lens::app::StorageDuty storage(*fixture.store);

    lens::test::ScriptedManager manager;
    lens::app::SyncDuty         duty(storage, fixture.dir, &manager);
    EXPECT_FALSE(duty.webdavConnected());

    manager.queue(207);
    duty.startLogin();
    ASSERT_TRUE(spinUntil([&] { return duty.webdavConnected(); }));

    duty.setWebdavUsername("other-reader");
    EXPECT_FALSE(duty.webdavConnected()) << "the verdict answered the old credentials";
}

/// The full round the settings page's Sync now runs: the listing finds nothing, the write makes the
/// file, and the id lands in the partition for the next round to read.
TEST(WebdavBackend, ASyncNowCarriesOneRoundThroughTheScriptedNetwork)
{
    QtApplication          qt;
    Fixture                fixture("lens-webdav-round-test");
    lens::app::StorageDuty storage(*fixture.store);

    lens::test::ScriptedManager manager;
    lens::app::SyncDuty         duty(storage, fixture.dir, &manager);
    ASSERT_EQ(duty.provider(), QStringLiteral("webdav"));
    ASSERT_EQ(duty.state(), QStringLiteral("signedIn"));

    manager.queue(404); // the PROPFIND: no envelope yet, so the round creates one
    manager.queue(201); // the PUT
    duty.syncNow();
    EXPECT_EQ(duty.state(), QStringLiteral("syncing"));
    ASSERT_TRUE(spinUntil([&] { return duty.state() != QStringLiteral("syncing"); }));

    EXPECT_EQ(manager.requests, 2);
    ASSERT_EQ(manager.sent.size(), 2u);
    EXPECT_EQ(manager.sent[0].verb, "PROPFIND");
    EXPECT_EQ(manager.sent[1].verb, "PUT");
    EXPECT_GT(manager.sent[1].body.size(), 0u) << "the write carries the encrypted envelope";
    EXPECT_EQ(duty.state(), QStringLiteral("signedIn"));
    EXPECT_TRUE(duty.error().isEmpty());
    EXPECT_FALSE(duty.lastSyncedAt().isEmpty());
    EXPECT_EQ(fixture.partition->gistId(), "Lens/lens-sync.json") << "the path is the cloud copy's id";
}

/// A server that refuses the credentials ends the run: no refresh exists to try, so the page shows
/// the sign-in error the form can fix.
TEST(WebdavBackend, ARefusedCredentialEndsTheRunWithTheSignInError)
{
    QtApplication          qt;
    Fixture                fixture("lens-webdav-refused-test");
    lens::app::StorageDuty storage(*fixture.store);

    lens::test::ScriptedManager manager;
    lens::app::SyncDuty         duty(storage, fixture.dir, &manager);

    manager.queue(207); // the PROPFIND: the envelope is there
    manager.queue(401); // the GET: the app password was changed on the server
    duty.syncNow();
    ASSERT_TRUE(spinUntil([&] { return duty.state() != QStringLiteral("syncing"); }));

    EXPECT_EQ(manager.requests, 2) << "Basic auth has nothing to renew, so no third call goes out";
    EXPECT_EQ(duty.error(), QStringLiteral("signInExpired"));
}
