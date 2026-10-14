/**
 * @file sync_apply_test.cpp
 * @brief The sync's local side, offline: the document a device sends, and what a merged document does
 *        to the stores once it comes back.
 *
 * Each case builds its stores over a scratch file, so a save writes somewhere disposable and the
 * document is the real settings document. Nothing here touches the network: the requests belong to
 * SyncDuty and are not run.
 */

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "app/sync_state.h"
#include "core/known_store.h"
#include "core/stats_store.h"

namespace {

using lens::app::syncstate::applyDaily;
using lens::app::syncstate::applyHistory;
using lens::app::syncstate::applyMarks;
using lens::app::syncstate::applySettings;
using lens::app::syncstate::buildLocalDocument;
using lens::app::syncstate::snapshotOf;
using lens::core::KnownStore;
using lens::core::StatsStore;
using nlohmann::json;

std::filesystem::path scratchPath(const char* name)
{
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

}

TEST(SyncApply, AKnownMarkRoundTripsThroughBuildAndApply)
{
    const auto from   = scratchPath("lens-sync-apply-from.json");
    const auto to     = scratchPath("lens-sync-apply-to.json");
    KnownStore source = KnownStore::load(from);
    source.mark("panel", true);
    StatsStore sourceStats{source.document()};

    const json local = buildLocalDocument(source, sourceStats, json::object(), "device-a", 1000);

    KnownStore target = KnownStore::load(to);
    applyMarks(target, local);
    EXPECT_TRUE(target.isKnown("panel"));

    std::filesystem::remove(from);
    std::filesystem::remove(to);
}

TEST(SyncApply, ADeletedLemmaStaysDeletedAfterApply)
{
    const auto path  = scratchPath("lens-sync-apply-deleted.json");
    KnownStore store = KnownStore::load(path);
    store.mark("panel", true);

    json deleted      = json::object();
    deleted["panel"]  = 9000000000000LL;
    json merged       = json::object();
    merged["known"]   = json::object();
    merged["deleted"] = deleted;
    applyMarks(store, merged);

    EXPECT_FALSE(store.isKnown("panel"));
    EXPECT_TRUE(store.syncMarks().at("deleted").contains("panel")) << "the tombstone is what the next upload carries";

    std::filesystem::remove(path);
}

TEST(SyncApply, ARemoteHistoryRowAppearsNewestFirstAndAMalformedRowIsSkipped)
{
    const auto path  = scratchPath("lens-sync-apply-history.json");
    KnownStore store = KnownStore::load(path);
    StatsStore stats{store.document()};

    json merged       = json::object();
    merged["history"] = json::array({
        json{{"word", "lemon"}, {"minute", "2026-10-13 09:00"}, {"verdict", "new"}},
        json{{"word", "river"}, {"minute", "2026-10-14 08:30"}, {"verdict", ""}},
        json{{"word", 5}, {"minute", "2026-10-14 08:31"}},
    });
    applyHistory(stats, merged);

    ASSERT_EQ(stats.history().size(), 2u) << "a row whose word is not a string is dropped, not kept half-read";
    EXPECT_EQ(stats.history().front().lemma, "river");
    EXPECT_EQ(stats.history().back().verdict, "new");

    std::filesystem::remove(path);
}

TEST(SyncApply, ASettingChangedSinceTheSnapshotIsStampedWithTheCurrentTime)
{
    const auto path  = scratchPath("lens-sync-apply-stamp-new.json");
    KnownStore store = KnownStore::load(path);
    StatsStore stats{store.document()};
    store.document()["bubbleDismissSeconds"] = 9;

    json snapshot                    = json::object();
    snapshot["bubbleDismissSeconds"] = json{{"value", 5}, {"at", 100}};
    const json local                 = buildLocalDocument(store, stats, snapshot, "device-a", 5000);

    const json& entry = local.at("settings").at("bubbleDismissSeconds");
    EXPECT_EQ(entry.at("value").get<int>(), 9);
    EXPECT_EQ(entry.at("at").get<long long>(), 5000LL);

    std::filesystem::remove(path);
}

TEST(SyncApply, AnUnchangedSettingKeepsTheTimeOfTheLastSync)
{
    const auto path  = scratchPath("lens-sync-apply-stamp-kept.json");
    KnownStore store = KnownStore::load(path);
    StatsStore stats{store.document()};
    store.document()["bubbleDismissSeconds"] = 9;

    json snapshot                    = json::object();
    snapshot["bubbleDismissSeconds"] = json{{"value", 9}, {"at", 100}};
    const json local                 = buildLocalDocument(store, stats, snapshot, "device-a", 5000);

    EXPECT_EQ(local.at("settings").at("bubbleDismissSeconds").at("at").get<long long>(), 100LL)
        << "a value the snapshot already has keeps the time it was last changed, not the time of this sync";

    std::filesystem::remove(path);
}

TEST(SyncApply, ARemoteSettingIsWrittenAndAnOutOfRangeLevelIsRefused)
{
    const auto path  = scratchPath("lens-sync-apply-settings.json");
    KnownStore store = KnownStore::load(path);
    const int  level = store.level();

    json settings      = json::object();
    settings["theme"]  = json{{"value", "dark"}, {"at", 1}};
    settings["level"]  = json{{"value", 99}, {"at", 1}};
    json merged        = json::object();
    merged["settings"] = settings;
    applySettings(store, store.section(), merged);

    EXPECT_EQ(store.document().at("theme").get<std::string>(), "dark");
    EXPECT_EQ(store.level(), level) << "a level outside 0..7 is refused rather than clamped";

    std::filesystem::remove(path);
}

TEST(SyncApply, ARemoteValueKeepsTheEncodingTheKeyAlreadyHas)
{
    const auto path                = scratchPath("lens-sync-apply-encoding.json");
    KnownStore store               = KnownStore::load(path);
    store.document()["multiSense"] = "false"; // a key an older build wrote as text

    json settings          = json::object();
    settings["multiSense"] = json{{"value", true}, {"at", 1}};
    json merged            = json::object();
    merged["settings"]     = settings;
    applySettings(store, store.section(), merged);

    EXPECT_EQ(store.document().at("multiSense"), "true")
        << "a text key stays text, so a build that reads text still reads it (docs/adr/0024)";

    std::filesystem::remove(path);
}

TEST(SyncApply, OnlyWhitelistedSettingsGoIntoTheSnapshot)
{
    json settings       = json::object();
    settings["theme"]   = json{{"value", "dark"}, {"at", 1}};
    settings["API-KEY"] = json{{"value", "secret"}, {"at", 1}};

    const json snapshot = snapshotOf(settings);
    EXPECT_TRUE(snapshot.contains("theme"));
    EXPECT_FALSE(snapshot.contains("API-KEY")) << "the API key is never part of the sync, so it never reaches the snapshot";
}

TEST(SyncApply, OtherDevicesTalliesAreKeptAndThisDeviceIsLeftAlone)
{
    const auto path  = scratchPath("lens-sync-apply-daily.json");
    KnownStore store = KnownStore::load(path);
    StatsStore stats{store.document()};

    json other          = json::object();
    other["2026-10-13"] = json{{"pops", 4}, {"learned", 1}, {"fresh", 3}, {"promptTokens", 0}, {"completionTokens", 0}, {"models", json::object()}};
    json daily          = json::object();
    daily["device-b"]   = other;
    daily["device-a"]   = json::object(); // this device's own bucket, which applyDaily must not touch
    json merged         = json::object();
    merged["daily"]     = daily;
    applyDaily(stats, merged, "device-a");

    ASSERT_TRUE(stats.daily().count("2026-10-13") == 1) << "another device's day shows up in the merged view";
    EXPECT_EQ(stats.daily().at("2026-10-13").pops, 4);
    EXPECT_TRUE(stats.ownDaily().empty()) << "this device's own tallies are not replaced by a copy from the cloud";

    std::filesystem::remove(path);
}

TEST(SyncFingerprint, AChangeToSyncedSettingChangesIt)
{
    const auto        path  = scratchPath("lens-sync-fingerprint-setting.json");
    KnownStore        store = KnownStore::load(path);
    StatsStore        stats{store.document()};
    const std::string before = lens::app::syncstate::contentFingerprint(store, stats);

    store.document()["theme"] = "dark";

    EXPECT_NE(lens::app::syncstate::contentFingerprint(store, stats), before);
    std::filesystem::remove(path);
}

TEST(SyncFingerprint, AChangeOutsideTheWhitelistLeavesItAlone)
{
    const auto        path  = scratchPath("lens-sync-fingerprint-outside.json");
    KnownStore        store = KnownStore::load(path);
    StatsStore        stats{store.document()};
    const std::string before = lens::app::syncstate::contentFingerprint(store, stats);

    // An API key, a hotkey and a dismiss-by-hover flag are on this device only: none is in the cloud copy.
    store.document()["API-KEY"]   = "sk-changed";
    store.document()["ocrHotkey"] = "Ctrl+Alt+Q";
    store.document()["autoScan"]  = true;

    EXPECT_EQ(lens::app::syncstate::contentFingerprint(store, stats), before)
        << "a change a sync never sends must not wake the timer, or it would upload for nothing";
    std::filesystem::remove(path);
}

TEST(SyncFingerprint, TheTimeAMarkWasSetDoesNotChangeIt)
{
    const auto path  = scratchPath("lens-sync-fingerprint-time.json");
    KnownStore store = KnownStore::load(path);
    StatsStore stats{store.document()};

    store.mark("panel", true);
    const std::string first = lens::app::syncstate::contentFingerprint(store, stats);

    // Setting the same verdict again stamps it with a later time. The value is unchanged, so the
    // fingerprint must be too, or every re-mark would look like a change worth uploading.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    store.mark("panel", true);

    EXPECT_EQ(lens::app::syncstate::contentFingerprint(store, stats), first)
        << "a re-mark with the same verdict is not a change a sync needs to send";
    std::filesystem::remove(path);
}
