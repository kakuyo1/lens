/**
 * @file telemetry_sync_test.cpp
 * @brief The event cloud channel offline: file names, state, the upload/delete plan, the daily cap,
 *        the state rebuilt from a remote list, and the download selection.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#include <system_error>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "app/duties/storage_duty.h"
#include "app/duties/sync_duty.h"
#include "app/duties/telemetry_duty.h"
#include "app/duties/telemetry_sync_duty.h"
#include "core/crypto/hash.h"
#include "core/crypto/vault.h"
#include "core/document/known_store.h"
#include "core/domain/sync_partition.h"
#include "core/telemetry/event.h"
#include "core/telemetry/sync_plan.h"
#include "scripted_manager.h"

namespace {

using lens::core::telemetry::EventFileRef;
using lens::core::telemetry::LocalMonth;
using lens::core::telemetry::Plan;
using lens::core::telemetry::PlanInput;
using lens::core::telemetry::SyncState;
using lens::core::telemetry::UploadAction;
using lens::core::telemetry::UploadedMonth;
using lens::core::telemetry::commitDelete;
using lens::core::telemetry::commitUpload;
using lens::core::telemetry::eventFileName;
using lens::core::telemetry::latestEventFiles;
using lens::core::telemetry::parseEventFileName;
using lens::core::telemetry::planEventSync;
using lens::core::telemetry::syncStateFromJson;
using lens::core::telemetry::writeSyncState;

constexpr char kDevice[] = "abcd1234";
constexpr char kOther[]  = "99999999";
constexpr char kHashA[]  = "aaaaaaaa";
constexpr char kHashB[]  = "bbbbbbbb";

std::string fileOf(std::string const& month, int const revision, std::string const& hash, std::string const& device = kDevice)
{
    return eventFileName(device, month, revision, hash);
}

UploadedMonth recordOf(std::string const& month, int const revision, std::string const& hash, std::string const& day, std::string const& device = kDevice)
{
    return UploadedMonth{fileOf(month, revision, hash, device), hash, revision, day};
}

PlanInput inputOf(std::vector<LocalMonth> local, std::vector<std::string> remote, SyncState state, bool const manual = false, std::string const& today = "2026-11-15")
{
    return PlanInput{.device       = kDevice,
                     .currentMonth = "2026-11",
                     .today        = today,
                     .manual       = manual,
                     .local        = std::move(local),
                     .remote       = std::move(remote),
                     .state        = std::move(state)};
}

TEST(TelemetrySyncPlan, NamesRoundTrip)
{
    const std::string name = fileOf("2026-11", 2, "0123abcd");
    EXPECT_EQ(name, "lens-events-abcd1234-2026-11-r0002-0123abcd.json.enc");

    const auto ref = parseEventFileName(name);
    ASSERT_TRUE(ref);
    EXPECT_EQ(ref->device, kDevice);
    EXPECT_EQ(ref->month, "2026-11");
    EXPECT_EQ(ref->revision, 2);
    EXPECT_EQ(ref->hash, "0123abcd");

    EXPECT_FALSE(parseEventFileName("lens-sync.json"));
    EXPECT_FALSE(parseEventFileName("lens-events-abcd1234-2026-11-r0000-0123abcd.json.enc"));
    EXPECT_FALSE(parseEventFileName("lens-events-abcd1234-2026-13-r0001-0123abcd.json.enc"));
    EXPECT_FALSE(parseEventFileName("lens-events-abcd1234-2026-11-r0001-0123ABCD.json.enc"));
    EXPECT_TRUE(eventFileName("ABCD1234", "2026-11", 1, "0123abcd").empty());
    EXPECT_TRUE(eventFileName(kDevice, "2026-11", 10000, "0123abcd").empty());
}

TEST(TelemetrySyncPlan, UploadsAClosedMonthOnce)
{
    Plan first = planEventSync(inputOf({{"2026-10", kHashA}}, {}, {}));
    ASSERT_EQ(first.uploads.size(), 1u);
    EXPECT_EQ(first.uploads[0].file, fileOf("2026-10", 1, kHashA));
    EXPECT_TRUE(first.uploads[0].retire.empty());
    EXPECT_TRUE(first.deletes.empty());

    SyncState state = first.state;
    commitUpload(state, first.uploads[0]);
    Plan second = planEventSync(inputOf({{"2026-10", kHashA}}, {first.uploads[0].file}, state));
    EXPECT_TRUE(second.uploads.empty());
    EXPECT_TRUE(second.deletes.empty());
    EXPECT_EQ(second.state.uploaded.at("2026-10").file, fileOf("2026-10", 1, kHashA));
}

TEST(TelemetrySyncPlan, ALateEventAddsARevisionAndRetiresTheOldOne)
{
    SyncState state;
    state.uploaded["2026-10"] = recordOf("2026-10", 1, kHashA, "2026-10-31");

    Plan plan = planEventSync(inputOf({{"2026-10", kHashB}}, {fileOf("2026-10", 1, kHashA)}, state));
    ASSERT_EQ(plan.uploads.size(), 1u);
    EXPECT_EQ(plan.uploads[0].revision, 2);
    EXPECT_EQ(plan.uploads[0].file, fileOf("2026-10", 2, kHashB));
    ASSERT_EQ(plan.uploads[0].retire.size(), 1u);
    EXPECT_EQ(plan.uploads[0].retire[0], fileOf("2026-10", 1, kHashA));
    EXPECT_TRUE(plan.deletes.empty()) << "the only cloud copy must wait for the replacement";

    commitUpload(plan.state, plan.uploads[0]);
    EXPECT_EQ(plan.state.uploaded.at("2026-10").file, fileOf("2026-10", 2, kHashB));
    EXPECT_TRUE(plan.state.pendingDeletes.contains(fileOf("2026-10", 1, kHashA)));
    commitDelete(plan.state, fileOf("2026-10", 1, kHashA));
    EXPECT_TRUE(plan.state.pendingDeletes.empty());
}

TEST(TelemetrySyncPlan, TheCurrentMonthUploadsOncePerDayAndManualBypasses)
{
    SyncState state;
    state.uploaded["2026-11"] = recordOf("2026-11", 1, kHashA, "2026-11-15");
    const std::vector<std::string> remote{fileOf("2026-11", 1, kHashA)};

    Plan automatic = planEventSync(inputOf({{"2026-11", kHashB}}, remote, state));
    EXPECT_TRUE(automatic.uploads.empty());
    EXPECT_TRUE(automatic.deletes.empty()) << "an old copy is not deleted while the new one waits a day";

    Plan manual = planEventSync(inputOf({{"2026-11", kHashB}}, remote, state, true));
    ASSERT_EQ(manual.uploads.size(), 1u);
    EXPECT_EQ(manual.uploads[0].revision, 2);

    Plan tomorrow = planEventSync(inputOf({{"2026-11", kHashB}}, remote, state, false, "2026-11-16"));
    ASSERT_EQ(tomorrow.uploads.size(), 1u);
    EXPECT_EQ(tomorrow.uploads[0].revision, 2);
}

TEST(TelemetrySyncPlan, RebuildsTheStateFromTheRemoteCopy)
{
    const std::vector<std::string> remote{
        fileOf("2026-10", 1, kHashB),
        fileOf("2026-10", 3, kHashA),
        fileOf("2026-10", 2, kHashB),
    };

    Plan plan = planEventSync(inputOf({{"2026-10", kHashA}}, remote, {}));
    EXPECT_TRUE(plan.uploads.empty()) << "the matching remote copy is already the upload";
    EXPECT_EQ(plan.state.uploaded.at("2026-10").file, fileOf("2026-10", 3, kHashA));
    EXPECT_EQ(plan.state.uploaded.at("2026-10").day, "");
    EXPECT_EQ(plan.deletes.size(), 2u);
    EXPECT_EQ(plan.deletes[0], fileOf("2026-10", 1, kHashB));
    EXPECT_EQ(plan.deletes[1], fileOf("2026-10", 2, kHashB));
}

TEST(TelemetrySyncPlan, AdoptsANewerMatchingCopyOverAnOlderStateRecord)
{
    SyncState state;
    state.uploaded["2026-11"] = recordOf("2026-11", 1, kHashA, "2026-11-14");
    const std::vector<std::string> remote{fileOf("2026-11", 1, kHashA), fileOf("2026-11", 2, kHashA)};

    Plan plan = planEventSync(inputOf({{"2026-11", kHashA}}, remote, state));
    EXPECT_TRUE(plan.uploads.empty());
    EXPECT_EQ(plan.state.uploaded.at("2026-11").revision, 2);
    ASSERT_EQ(plan.deletes.size(), 1u);
    EXPECT_EQ(plan.deletes[0], fileOf("2026-11", 1, kHashA));
}

TEST(TelemetrySyncPlan, DeletesAStaleMonthAndLeavesOtherDevicesAlone)
{
    SyncState state;
    state.uploaded["2026-08"] = recordOf("2026-08", 2, kHashA, "");
    state.uploaded["2026-11"] = recordOf("2026-11", 1, kHashA, "2026-11-15");
    const std::vector<std::string> remote{
        fileOf("2026-08", 1, kHashA),
        fileOf("2026-08", 2, kHashA),
        fileOf("2026-08", 9, kHashA, kOther),
        fileOf("2025-11", 1, kHashB),
        fileOf("2026-11", 1, kHashA),
    };

    Plan plan = planEventSync(inputOf({{"2026-11", kHashA}}, remote, state));
    EXPECT_TRUE(plan.uploads.empty());
    ASSERT_EQ(plan.deletes.size(), 3u);
    ASSERT_EQ(plan.state.uploaded.size(), 1u);
    EXPECT_EQ(plan.state.uploaded.begin()->first, "2026-11");
    EXPECT_FALSE(plan.state.pendingDeletes.contains(fileOf("2026-08", 9, kHashA, kOther)));
}

TEST(TelemetrySyncPlan, KeepsADeferredDeleteUntilTheReplacementLands)
{
    SyncState state;
    state.uploaded["2026-10"] = recordOf("2026-10", 1, kHashA, "2026-10-31");
    state.pendingDeletes.insert(fileOf("2026-10", 1, kHashA));

    Plan plan = planEventSync(inputOf({{"2026-10", kHashB}}, {fileOf("2026-10", 1, kHashA)}, state));
    ASSERT_EQ(plan.uploads.size(), 1u);
    EXPECT_TRUE(plan.deletes.empty());
    EXPECT_TRUE(plan.state.pendingDeletes.contains(fileOf("2026-10", 1, kHashA)));

    SyncState cappedState;
    cappedState.uploaded["2026-11"] = recordOf("2026-11", 2, kHashA, "2026-11-15");
    cappedState.pendingDeletes.insert(fileOf("2026-11", 1, kHashB));
    Plan const capped = planEventSync(
        inputOf({{"2026-11", kHashB}}, {fileOf("2026-11", 1, kHashB), fileOf("2026-11", 2, kHashA)}, cappedState));
    EXPECT_TRUE(capped.uploads.empty());
    EXPECT_TRUE(capped.deletes.empty());
    EXPECT_TRUE(capped.state.pendingDeletes.contains(fileOf("2026-11", 1, kHashB)));
}

TEST(TelemetrySyncPlan, DeletesEveryCopyWhenTheLocalMonthIsGone)
{
    SyncState state;
    state.uploaded["2026-10"] = recordOf("2026-10", 2, kHashA, "");
    const std::vector<std::string> remote{fileOf("2026-10", 1, kHashA), fileOf("2026-10", 2, kHashA)};

    Plan plan = planEventSync(inputOf({}, remote, state));
    ASSERT_EQ(plan.deletes.size(), 2u);
    EXPECT_TRUE(plan.state.uploaded.empty());
    EXPECT_EQ(plan.state.pendingDeletes.size(), 2u);
}

TEST(TelemetrySyncPlan, DropsAQueuedDeleteForACopyThatIsStillCurrent)
{
    SyncState state;
    state.uploaded["2026-11"] = recordOf("2026-11", 1, kHashA, "2026-11-15");
    state.pendingDeletes.insert(fileOf("2026-11", 1, kHashA));

    Plan plan = planEventSync(inputOf({{"2026-11", kHashA}}, {fileOf("2026-11", 1, kHashA)}, state));
    EXPECT_TRUE(plan.uploads.empty());
    EXPECT_TRUE(plan.deletes.empty());
    EXPECT_TRUE(plan.state.pendingDeletes.empty()) << "the current copy is not deleted";
}

TEST(TelemetrySyncPlan, RoundTripsTheStateAndKeepsTheOtherKeys)
{
    SyncState state;
    state.uploaded["2026-10"] = recordOf("2026-10", 2, kHashA, "2026-10-31");
    state.pendingDeletes.insert(fileOf("2026-10", 1, kHashB));
    state.deleteAll = true;

    nlohmann::json document = {{"v", 1}, {"seq", 42}};
    writeSyncState(document, state);
    EXPECT_EQ(document.at("seq"), 42);
    const SyncState read = syncStateFromJson(document);
    EXPECT_EQ(read.uploaded.at("2026-10").file, fileOf("2026-10", 2, kHashA));
    EXPECT_EQ(read.uploaded.at("2026-10").day, "2026-10-31");
    EXPECT_TRUE(read.pendingDeletes.contains(fileOf("2026-10", 1, kHashB)));
    EXPECT_TRUE(read.deleteAll);

    nlohmann::json  broken  = {{"uploaded", {{"2026-10", {{"file", "not-an-event-file"}}}, {"2026-11", 7}}}, {"pendingDeletes", {1, "x"}}};
    const SyncState dropped = syncStateFromJson(broken);
    EXPECT_TRUE(dropped.uploaded.empty());
    EXPECT_EQ(dropped.pendingDeletes.size(), 1u);
}

TEST(TelemetrySyncPlan, LatestFilesPicksOnePerDeviceAndMonth)
{
    const std::vector<std::string> remote{
        fileOf("2026-10", 1, kHashA, "11111111"),
        fileOf("2026-10", 3, kHashA, "11111111"),
        fileOf("2026-10", 2, kHashB, "22222222"),
        fileOf("2026-11", 4, kHashA, "33333333"),
        "lens-sync.json",
        "lens-events-nope-2026-10-r0001-aaaaaaaa.json.enc",
    };

    const std::vector<EventFileRef> latest = latestEventFiles(remote, "2026-10");
    ASSERT_EQ(latest.size(), 2u);
    EXPECT_EQ(latest[0].device, "11111111");
    EXPECT_EQ(latest[0].revision, 3);
    EXPECT_EQ(latest[1].device, "22222222");
    EXPECT_EQ(latest[1].revision, 2);
    EXPECT_TRUE(latestEventFiles(remote, "2026-12").empty());
}

TEST(TelemetrySyncPlan, DeletesARemoteMonthPastRetention)
{
    SyncState state;
    state.uploaded["2025-11"] = recordOf("2025-11", 1, kHashA, "2025-11-30");
    const std::vector<std::string> remote{fileOf("2025-11", 1, kHashA)};

    Plan plan = planEventSync(inputOf({{"2025-11", kHashA}}, remote, state));
    EXPECT_TRUE(plan.uploads.empty()) << "a month past retention is not uploaded";
    ASSERT_EQ(plan.deletes.size(), 1u);
    EXPECT_EQ(plan.deletes[0], fileOf("2025-11", 1, kHashA));
    EXPECT_TRUE(plan.state.uploaded.empty());
}

TEST(TelemetrySyncPlan, DeletesEveryEventFileWhenTheCloudCopyWasDeleted)
{
    SyncState state;
    state.deleteAll           = true;
    state.uploaded["2026-11"] = recordOf("2026-11", 1, kHashA, "2026-11-15");
    state.pendingDeletes.insert(fileOf("2026-10", 1, kHashB));
    const std::vector<std::string> remote{
        fileOf("2026-11", 1, kHashA),
        fileOf("2026-11", 1, kHashB, kOther),
        "lens-sync.json",
    };

    Plan plan = planEventSync(inputOf({{"2026-11", kHashA}}, remote, state));
    EXPECT_TRUE(plan.uploads.empty());
    ASSERT_EQ(plan.deletes.size(), 3u);
    EXPECT_TRUE(plan.state.deleteAll) << "the duty clears the flag only after the last delete";
    EXPECT_TRUE(plan.state.uploaded.empty());
    EXPECT_EQ(plan.state.pendingDeletes.size(), 3u);
}

struct QtApplication {
    int              argc     = 1;
    char             name[28] = "telemetry_sync_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

lens::core::vault::KdfParams quickParams()
{
    lens::core::vault::KdfParams params = lens::core::vault::newParams();
    params.iterations                   = 1000;
    return params;
}

lens::core::syncrun::StoredKey aKey(char const* passphrase, lens::core::vault::KdfParams const& params)
{
    return {lens::core::vault::deriveKey(passphrase, params), params};
}

bool spinUntil(std::function<bool()> const& done, int ms = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (not done() and clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

std::string shortDevice(std::string const& id)
{
    return lens::core::crypto::sha1Hex(id).substr(0, 8);
}

QByteArray gistListing(std::vector<std::string> const& eventFiles)
{
    QJsonObject files;
    files.insert(QStringLiteral("lens-sync.json"),
                 QJsonObject{{"content", "{}"}, {"truncated", false}, {"raw_url", "https://gist.githubusercontent.com/x/raw/sync"}});
    for (std::string const& name : eventFiles)
        files.insert(QString::fromStdString(name),
                     QJsonObject{{"content", ""},
                                 {"truncated", true},
                                 {"raw_url", QString::fromStdString("https://gist.githubusercontent.com/x/raw/" + name)}});
    const QJsonObject root{{"id", "gist-1"},
                           {"description", "lens-sync"},
                           {"updated_at", "2026-11-15T00:00:00Z"},
                           {"files", files}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray webdavListing(std::vector<std::string> const& names)
{
    QString xml = QStringLiteral("<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\">");
    for (std::string const& name : names)
        xml += QStringLiteral("<d:response><d:href>/dav/Lens/%1</d:href></d:response>").arg(QString::fromStdString(name));
    xml += QStringLiteral("</d:multistatus>");
    return xml.toUtf8();
}

struct TelemetrySyncRig {
    explicit TelemetrySyncRig(bool const webdav = false)
    {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        {
            std::ofstream config(dir / "sync.json");
            config << R"({"clientId":"client-id"})";
        }

        store = std::make_unique<lens::core::KnownStore>(lens::core::KnownStore::load(dir / "settings.json"));
        lens::core::sync::SyncPartition seeded(*store);
        key = aKey("a fixture passphrase", quickParams());
        seeded.storeKey(key);
        if (webdav) {
            seeded.setProvider("webdav");
            seeded.setServer("https://dav.example.com/dav");
            seeded.setUsername("reader");
            seeded.setAppPassword("app-pass");
            seeded.setGistId("Lens/lens-sync.json");
        } else {
            seeded.storeTokens("access-1", "refresh-1");
            seeded.setGistId("gist-1");
        }

        storage   = std::make_unique<lens::app::StorageDuty>(*store);
        sync      = std::make_unique<lens::app::SyncDuty>(*storage, dir, &manager);
        telemetry = std::make_unique<lens::app::TelemetryDuty>(dir / "events", "device-a", [] { return true; }, [] { return std::string{"chrome.exe"}; }, [] { return lens::app::TelemetryDuty::Now{1750000000000LL, "2026-11"}; }, [](long long) { return lens::core::telemetry::LocalMoment{"2026-11-17", 10}; });
        events    = std::make_unique<lens::app::TelemetrySyncDuty>(*telemetry,
                                                                *sync,
                                                                &manager,
                                                                [] { return lens::app::TelemetrySyncDuty::Now{"2026-11", "2026-11-15"}; });
        dev       = shortDevice("device-a");
    }

    ~TelemetrySyncRig()
    {
        events.reset();
        telemetry.reset();
        sync.reset();
        storage.reset();
        store.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    void seedEvent(std::string lemma) const
    {
        telemetry->record("lookup.pop", {{"src", "scan"}, {"lemma", std::move(lemma)}, {"verdict", "new"}});
        telemetry->flush();
    }

    std::string uploadedFile() const
    {
        const SyncState state = syncStateFromJson(telemetry->log().state());
        const auto      found = state.uploaded.find("2026-11");
        return found == state.uploaded.end() ? std::string{} : found->second.file;
    }

    QtApplication                                 qt;
    std::filesystem::path                         dir = std::filesystem::temp_directory_path() / "lens-telemetry-sync-duty-test";
    lens::test::ScriptedManager                   manager{500};
    std::unique_ptr<lens::core::KnownStore>       store;
    std::unique_ptr<lens::app::StorageDuty>       storage;
    std::unique_ptr<lens::app::SyncDuty>          sync;
    std::unique_ptr<lens::app::TelemetryDuty>     telemetry;
    std::unique_ptr<lens::app::TelemetrySyncDuty> events;
    lens::core::syncrun::StoredKey                key;
    std::string                                   dev;
};

/// A successful main sync is the only timer the event channel has: the upload starts from its
/// success, not from a timer of its own.
TEST(TelemetrySyncDuty, StartsFromASuccessfulMainSync)
{
    TelemetrySyncRig rig;
    rig.seedEvent("apple");

    rig.manager.queue(200, "[]"); // the main sync's gist list
    rig.manager.queue(201, R"({"id":"gist-1","description":"lens-sync","files":{"lens-sync.json":{"content":"{}"}}})");
    rig.manager.queue(200, gistListing({}));                 // the event channel's listing
    rig.manager.queue(200, R"({"id":"gist-1","files":{}})"); // the event upload

    rig.sync->syncNow();
    ASSERT_TRUE(spinUntil([&] { return not rig.uploadedFile().empty(); }));

    ASSERT_EQ(rig.manager.sent.size(), 4u);
    EXPECT_EQ(rig.manager.sent[0].verb, "GET");
    EXPECT_EQ(rig.manager.sent[1].verb, "POST");
    EXPECT_EQ(rig.manager.sent[2].verb, "GET");
    EXPECT_EQ(rig.manager.sent[3].verb, "PATCH");
    EXPECT_FALSE(QJsonDocument::fromJson(rig.manager.sent[3].body).object().value("files").toObject().contains("lens-sync.json"));
}

TEST(TelemetrySyncDuty, UploadsAChangedMonthAndDeletesTheOldVersion)
{
    TelemetrySyncRig rig;
    rig.seedEvent("apple");
    const std::string oldFile = fileOf("2026-11", 1, "11111111", rig.dev);

    rig.manager.queue(200, gistListing({oldFile}));
    rig.manager.queue(200, R"({"id":"gist-1","files":{}})");
    rig.manager.queue(200, R"({"id":"gist-1","files":{}})");

    bool finished = false;
    bool ok       = false;
    QObject::connect(rig.events.get(), &lens::app::TelemetrySyncDuty::finished, [&](bool success) {
        finished = true;
        ok       = success;
    });
    rig.events->run(true);
    ASSERT_TRUE(spinUntil([&] { return finished; }));
    EXPECT_TRUE(ok);

    ASSERT_EQ(rig.manager.sent.size(), 3u);
    EXPECT_EQ(rig.manager.sent[0].verb, "GET");
    EXPECT_EQ(rig.manager.sent[1].verb, "PATCH");
    EXPECT_EQ(rig.manager.sent[2].verb, "PATCH");

    const QJsonObject uploadedFiles = QJsonDocument::fromJson(rig.manager.sent[1].body).object().value("files").toObject();
    ASSERT_EQ(uploadedFiles.size(), 1);
    const QString uploadedName = uploadedFiles.begin().key();
    EXPECT_FALSE(uploadedFiles.contains("lens-sync.json")) << "the main document is not part of an event upload";
    EXPECT_FALSE(uploadedFiles.value(uploadedName).toObject().value("content").toString().isEmpty());

    const std::string sealed = uploadedFiles.value(uploadedName).toObject().value("content").toString().toStdString();
    const auto        opened = lens::core::vault::open(rig.key.key, nlohmann::json::parse(sealed));
    ASSERT_TRUE(opened);
    EXPECT_EQ(*opened, rig.telemetry->log().readMonthText("2026-11"));

    const auto ref = parseEventFileName(uploadedName.toStdString());
    ASSERT_TRUE(ref);
    EXPECT_EQ(ref->revision, 2);
    EXPECT_EQ(ref->hash, lens::core::crypto::sha1Hex(*opened).substr(0, 8));
    EXPECT_EQ(rig.uploadedFile(), uploadedName.toStdString());

    const QJsonObject deletedFiles = QJsonDocument::fromJson(rig.manager.sent[2].body).object().value("files").toObject();
    EXPECT_TRUE(deletedFiles.value(QString::fromStdString(oldFile)).isNull());

    rig.manager.queue(200, gistListing({uploadedName.toStdString()}));
    finished = false;
    rig.events->run(false);
    ASSERT_TRUE(spinUntil([&] { return finished; }));
    EXPECT_TRUE(ok);
    EXPECT_EQ(rig.manager.sent.size(), 4u) << "an unchanged month uploads nothing on the next sync";
}

TEST(TelemetrySyncDuty, WebdavUploadsAndDeletesTheOldVersion)
{
    TelemetrySyncRig rig(true);
    rig.seedEvent("apple");
    rig.manager.queue(207, webdavListing({}));
    rig.manager.queue(201);

    bool finished = false;
    bool ok       = false;
    QObject::connect(rig.events.get(), &lens::app::TelemetrySyncDuty::finished, [&](bool success) {
        finished = true;
        ok       = success;
    });
    rig.events->run(true);
    ASSERT_TRUE(spinUntil([&] { return finished; }));
    EXPECT_TRUE(ok);

    ASSERT_EQ(rig.manager.sent.size(), 2u);
    EXPECT_EQ(rig.manager.sent[0].verb, "PROPFIND");
    EXPECT_EQ(rig.manager.sent[0].depth, "1");
    EXPECT_EQ(rig.manager.sent[1].verb, "PUT");
    EXPECT_NE(rig.manager.sent[1].url.indexOf("/Lens/lens-events-"), -1);
    EXPECT_FALSE(rig.manager.sent[1].body.isEmpty());

    const std::string oldFile = rig.uploadedFile();
    ASSERT_FALSE(oldFile.empty());
    rig.seedEvent("pear");
    rig.manager.queue(207, webdavListing({oldFile}));
    rig.manager.queue(201);
    rig.manager.queue(204);

    finished = false;
    rig.events->run(true);
    ASSERT_TRUE(spinUntil([&] { return finished; }));
    EXPECT_TRUE(ok);

    ASSERT_EQ(rig.manager.sent.size(), 5u);
    EXPECT_EQ(rig.manager.sent[2].verb, "PROPFIND");
    EXPECT_EQ(rig.manager.sent[3].verb, "PUT");
    EXPECT_NE(rig.manager.sent[3].url.indexOf("/Lens/lens-events-"), -1);
    EXPECT_EQ(rig.manager.sent[4].verb, "DELETE");
    EXPECT_NE(rig.manager.sent[4].url.indexOf(QString::fromStdString("/Lens/" + oldFile)), -1);
}

TEST(TelemetrySyncDuty, DownloadsTheNewestCopyOfAMonth)
{
    TelemetrySyncRig rig;
    rig.seedEvent("apple");
    const std::string plain  = rig.telemetry->log().readMonthText("2026-11");
    const std::string hash   = lens::core::crypto::sha1Hex(plain).substr(0, 8);
    const std::string file   = eventFileName(rig.dev, "2026-11", 1, hash);
    const std::string sealed = lens::core::vault::seal(rig.key.key, plain, rig.key.params).dump();

    rig.manager.queue(200, gistListing({file}));
    rig.manager.queue(200, QByteArray::fromStdString(sealed));

    bool                                      done = false;
    std::vector<lens::core::telemetry::Event> events;
    rig.events->downloadMonth("2026-11", [&](std::string const& month, std::vector<lens::core::telemetry::Event> received) {
        EXPECT_EQ(month, "2026-11");
        events = std::move(received);
        done   = true;
    });
    ASSERT_TRUE(spinUntil([&] { return done; }));
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "lookup.pop");
    EXPECT_EQ(events[0].data.value("lemma", ""), "apple");
    ASSERT_EQ(rig.manager.sent.size(), 2u);
    EXPECT_EQ(rig.manager.sent[0].verb, "GET");
    EXPECT_EQ(rig.manager.sent[1].verb, "GET");
}

TEST(TelemetrySyncDuty, WebdavCloudDeletionRemovesTheEventFiles)
{
    TelemetrySyncRig rig(true);
    rig.seedEvent("apple");
    rig.manager.queue(207, webdavListing({}));
    rig.manager.queue(201);

    bool finished = false;
    QObject::connect(rig.events.get(), &lens::app::TelemetrySyncDuty::finished, [&](bool) { finished = true; });
    rig.events->run(true);
    ASSERT_TRUE(spinUntil([&] { return finished; }));
    const std::string uploaded = rig.uploadedFile();
    ASSERT_FALSE(uploaded.empty());

    rig.manager.queue(204); // the main sync deletes Lens/lens-sync.json
    rig.manager.queue(207, webdavListing({uploaded}));
    rig.manager.queue(204); // the event channel deletes the month file
    rig.sync->deleteCloudData();
    ASSERT_TRUE(spinUntil([&] {
        const SyncState state = syncStateFromJson(rig.telemetry->log().state());
        return state.uploaded.empty() and state.pendingDeletes.empty() and not state.deleteAll;
    }));

    ASSERT_EQ(rig.manager.sent.size(), 5u);
    EXPECT_EQ(rig.manager.sent[2].verb, "DELETE");
    EXPECT_NE(rig.manager.sent[2].url.indexOf("/Lens/lens-sync.json"), -1);
    EXPECT_EQ(rig.manager.sent[3].verb, "PROPFIND");
    EXPECT_EQ(rig.manager.sent[4].verb, "DELETE");
    EXPECT_NE(rig.manager.sent[4].url.indexOf(QString::fromStdString("/Lens/" + uploaded)), -1);
}

TEST(TelemetrySyncDuty, DeletingTheCloudCopyForgetsTheUploadedFiles)
{
    TelemetrySyncRig rig;
    rig.seedEvent("apple");
    rig.manager.queue(200, gistListing({}));
    rig.manager.queue(200, R"({"id":"gist-1","files":{}})");

    bool finished = false;
    QObject::connect(rig.events.get(), &lens::app::TelemetrySyncDuty::finished, [&](bool) { finished = true; });
    rig.events->run(false);
    ASSERT_TRUE(spinUntil([&] { return finished; }));
    ASSERT_FALSE(rig.uploadedFile().empty());

    rig.manager.queue(204); // the main sync's gist delete
    rig.sync->deleteCloudData();
    ASSERT_TRUE(spinUntil([&] { return syncStateFromJson(rig.telemetry->log().state()).uploaded.empty(); }));
}

}
