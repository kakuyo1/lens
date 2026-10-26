/**
 * @file sync_transport_test.cpp
 * @brief The sync round over the real gist and auth clients, with the network scripted.
 *
 * The requests are the ones GitHub would receive -- the list, the read, the write, and the token
 * endpoint -- and the answers are the bodies it would send. What this covers is the seam the round
 * does not own: the URL a call goes to, the token it carries, the file a write puts up, and the
 * mapping of a status back into the round's failure kinds.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QString>

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "app/duties/sync_transport.h"
#include "core/crypto/vault.h"
#include "core/document/known_store.h"
#include "core/document/stats_store.h"
#include "core/domain/sync_partition.h"
#include "core/domain/sync_round.h"
#include "scripted_manager.h"
#include "sync/gist_client.h"
#include "sync/github_auth.h"

namespace {

namespace vault   = lens::core::vault;
namespace syncrun = lens::core::syncrun;
using lens::core::KnownStore;
using lens::core::StatsStore;
using lens::core::sync::Hooks;
using lens::core::sync::SyncPartition;
using lens::core::sync::SyncRound;
using nlohmann::json;

/// The event loop the scripted answers arrive on.
struct QtApplication {
    int              argc     = 1;
    char             name[24] = "sync_transport_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

std::filesystem::path scratchPath(const char* name)
{
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

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

std::string sealedCopy(const syncrun::StoredKey& key, const json& document)
{
    return vault::seal(key.key, document.dump(), key.params).dump();
}

/// @return Whether @p done came true within @p ms, turning the loop so the answers can arrive.
bool spinUntil(const std::function<bool()>& done, int ms = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

/// What the hooks saw, enough for these cases.
struct Recorded {
    int                                      finished  = 0;
    bool                                     succeeded = false;
    std::string                              error;
    std::optional<std::chrono::milliseconds> retry;

    Hooks hooks()
    {
        Hooks out;
        out.error    = [this](std::string code) { error = std::move(code); };
        out.finished = [this](bool ok) { ++finished; succeeded = ok; };
        out.retry    = [this](std::chrono::milliseconds delay) { retry = delay; };
        return out;
    }
};

/// One round over the real clients, a scripted network and a scratch document.
struct Fixture {
    Fixture()
        : path(scratchPath("lens-sync-transport-test.json")),
          store(KnownStore::load(path)),
          stats(store.document()),
          partition(store, [] { return std::string("device-a"); }),
          gist(&manager),
          auth(&manager),
          transport(gist, auth, QStringLiteral("client-id")),
          round(transport,
                store,
                stats,
                partition,
                SyncRound::Clock{[] { return 5000LL; }, [] { return std::string("2026-10-26 12:00"); }},
                recorded.hooks())
    {
    }

    ~Fixture()
    {
        std::filesystem::remove(path);
    }

    void signedIn()
    {
        key = aKey("a fixture passphrase", quickParams());
        partition.storeKey(*key);
        partition.storeTokens("access-1", "refresh-1");
    }

    std::filesystem::path             path;
    KnownStore                        store;
    StatsStore                        stats;
    SyncPartition                     partition;
    lens::test::ScriptedManager       manager;
    lens::sync::GistClient            gist;
    lens::sync::GithubAuth            auth;
    lens::app::SyncTransport          transport;
    Recorded                          recorded;
    std::optional<syncrun::StoredKey> key;
    SyncRound                         round;
};

/// @return A gist body as the API sends it, with the cloud copy in the file.
json gistBody(const std::string& id, const std::string& envelope)
{
    return {{"id", id},
            {"description", "lens-sync"},
            {"updated_at", "2026-10-26T00:00:00Z"},
            {"files", {{"lens-sync.json", {{"content", envelope}}}}}};
}

} // namespace

/// A whole round over the real clients: the list finds the gist, the read brings back the cloud copy,
/// and the write puts the merged document up as the file GitHub receives.
TEST(SyncTransport, TheRoundRunsOverTheRealClientsAndUploadsTheMergedCopy)
{
    QtApplication qt;
    Fixture       fixture;
    fixture.signedIn();

    const json remote{{"format", 1},
                      {"known", {{"river", {{"learned", true}, {"at", 1}}}}},
                      {"deleted", json::object()},
                      {"history", json::array()},
                      {"daily", json::object()},
                      {"settings", json::object()}};
    const json cloud = gistBody("gist-1", sealedCopy(*fixture.key, remote));
    fixture.manager.queue(200, QByteArray::fromStdString(json::array({cloud}).dump()));
    fixture.manager.queue(200, QByteArray::fromStdString(cloud.dump()));
    fixture.manager.queue(200, QByteArray::fromStdString(cloud.dump()));

    fixture.round.begin({.manual = true});
    ASSERT_TRUE(spinUntil([&] { return fixture.recorded.finished > 0; }));

    ASSERT_EQ(fixture.manager.requests, 3);
    ASSERT_EQ(fixture.manager.sent.size(), 3u);
    EXPECT_TRUE(fixture.manager.sent[0].url.endsWith(QStringLiteral("/gists?per_page=100")));
    EXPECT_TRUE(fixture.manager.sent[1].url.endsWith(QStringLiteral("/gists/gist-1")));
    EXPECT_TRUE(fixture.manager.sent[2].url.endsWith(QStringLiteral("/gists/gist-1")));

    // The write's body is the sealed merged document, under the key that opened the cloud copy.
    const json        body   = json::parse(std::string(fixture.manager.sent[2].body.constData()));
    const std::string sealed = body.at("files").at("lens-sync.json").at("content").get<std::string>();
    const auto        opened = vault::open(fixture.key->key, json::parse(sealed));
    ASSERT_TRUE(opened.has_value());
    EXPECT_TRUE(json::parse(*opened).at("known").contains("river"));

    EXPECT_TRUE(fixture.store.isKnown("river"));
    EXPECT_TRUE(fixture.recorded.succeeded);
    EXPECT_EQ(fixture.partition.lastSyncedAt(), "2026-10-26 12:00");
}

/// A 401 on the listing is renewed through the auth client, and the run restarts with the new pair:
/// the token endpoint is asked for the app's refresh grant, and the listing then carries the new token.
TEST(SyncTransport, ARefusedListingIsRenewedThroughTheAuthClientAndTheRunCarriesOn)
{
    QtApplication qt;
    Fixture       fixture;
    fixture.signedIn();

    fixture.manager.queue(401, "{}");
    fixture.manager.queue(200, R"({"access_token":"access-2","token_type":"bearer","scope":"gist",)"
                               R"("expires_in":28800,"refresh_token":"refresh-2"})");
    fixture.manager.queue(200, "[]");
    fixture.manager.queue(201, QByteArray::fromStdString(gistBody("gist-3", "{}").dump()));

    fixture.round.begin({.manual = true});
    ASSERT_TRUE(spinUntil([&] { return fixture.recorded.finished > 0; }));

    ASSERT_EQ(fixture.manager.requests, 4);
    EXPECT_TRUE(fixture.manager.sent[1].url.endsWith(QStringLiteral("/login/oauth/access_token")));
    EXPECT_TRUE(fixture.manager.sent[1].body.contains("grant_type=refresh_token"));
    EXPECT_TRUE(fixture.manager.sent[1].body.contains("refresh_token=refresh-1"));
    EXPECT_TRUE(fixture.manager.sent[1].body.contains("client_id=client-id"));
    EXPECT_TRUE(fixture.manager.sent[2].url.endsWith(QStringLiteral("/gists?per_page=100")));
    EXPECT_TRUE(fixture.manager.sent[3].url.endsWith(QStringLiteral("/gists")));
    EXPECT_EQ(fixture.partition.token(), "access-2");
    EXPECT_EQ(fixture.partition.refreshToken(), "refresh-2");
    EXPECT_TRUE(fixture.recorded.succeeded);
}

/// A refresh GitHub refuses ends the round there: the promise to the reader is an error and a
/// finished run, not a request that never comes back (docs/adr/0120).
TEST(SyncTransport, ARefusedRefreshEndsTheRound)
{
    QtApplication qt;
    Fixture       fixture;
    fixture.signedIn();

    fixture.manager.queue(401, "{}");
    fixture.manager.queue(200, R"({"error":"bad_refresh_token","error_description":"The refresh token is invalid or expired."})");

    fixture.round.begin({.manual = false});
    ASSERT_TRUE(spinUntil([&] { return fixture.recorded.finished > 0; }));

    EXPECT_EQ(fixture.manager.requests, 2) << "the run asked for nothing after the refusal";
    EXPECT_EQ(fixture.recorded.error, "signInExpired");
    EXPECT_FALSE(fixture.recorded.succeeded);
    EXPECT_FALSE(fixture.recorded.retry.has_value());
    EXPECT_FALSE(fixture.round.busy());
    EXPECT_FALSE(fixture.partition.hasRefreshToken());
    EXPECT_TRUE(fixture.partition.token().empty());
}
