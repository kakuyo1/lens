/**
 * @file sync_round_test.cpp
 * @brief One sync round offline: the order of its calls, the key it keeps when it stops, the
 *        fingerprint it takes after the apply, the 401's renewal, the backoff and the run a change
 *        during this one asks for (docs/adr/0042, 0057).
 *
 * The transport is a script that answers when the case says so, so a run's order is a list and no
 * event loop is needed. Every passphrase, key and envelope here is a fixture.
 */

#include <chrono>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/crypto/vault.h"
#include "core/document/known_store.h"
#include "core/document/stats_store.h"
#include "core/domain/sync_partition.h"
#include "core/domain/sync_round.h"

namespace {

namespace vault   = lens::core::vault;
namespace syncrun = lens::core::syncrun;
using lens::core::KnownStore;
using lens::core::StatsStore;
using lens::core::sync::Answer;
using lens::core::sync::Hooks;
using lens::core::sync::Request;
using lens::core::sync::SyncPartition;
using lens::core::sync::SyncRound;
using lens::core::sync::Transport;
using nlohmann::json;

std::filesystem::path scratchPath(const char* name)
{
    auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    return path;
}

/// A salt and an iteration count small enough that a case runs in milliseconds. The envelope carries
/// them, so a run that reopens it derives under these and not under the production figure.
vault::KdfParams quickParams()
{
    vault::KdfParams params = vault::newParams();
    params.iterations       = 1000;
    return params;
}

/// @return The key a passphrase derives under a small iteration count, for fixtures.
syncrun::StoredKey aKey(const char* passphrase, const vault::KdfParams& params)
{
    return {vault::deriveKey(passphrase, params), params};
}

/// @return A sealed cloud copy holding @p document, as the text the gist file carries.
std::string sealedCopy(const syncrun::StoredKey& key, const json& document)
{
    return vault::seal(key.key, document.dump(), key.params).dump();
}

/// A transport that keeps every request until the case answers it, so a run's order is a list.
class ScriptedTransport final : public Transport {
public:
    struct Sent {
        Request request;
        Reply   reply;
    };

    void send(const Request& request, Reply reply) override
    {
        waiting.push_back({request, std::move(reply)});
    }

    [[nodiscard]] bool waitingFor(Request::Kind kind) const
    {
        return !waiting.empty() && waiting.front().request.kind == kind;
    }

    [[nodiscard]] const Request& front() const
    {
        return waiting.front().request;
    }

    void deliver(Answer answer = {})
    {
        Sent sent = std::move(waiting.front());
        waiting.pop_front();
        sent.reply(answer);
    }

    /// @brief Answer the call in flight with a gist: its id, and the file's text for a read.
    void gist(std::string id, std::string content = {})
    {
        Answer answer;
        answer.gistId  = std::move(id);
        answer.content = std::move(content);
        deliver(std::move(answer));
    }

    void fail(Answer::Failure why)
    {
        Answer answer;
        answer.failure = why;
        deliver(std::move(answer));
    }

    std::deque<Sent> waiting;
};

/// What the hooks saw, in the order they were called.
struct Recorded {
    int                                      changed   = 0;
    int                                      applied   = 0;
    int                                      finished  = 0;
    int                                      rerun     = 0;
    bool                                     succeeded = false;
    std::string                              error;
    std::optional<std::chrono::milliseconds> retry;
    std::vector<std::string>                 order;

    void reset()
    {
        *this = Recorded{};
    }

    Hooks hooks()
    {
        Hooks out;
        out.changed  = [this] { ++changed; order.push_back("changed"); };
        out.error    = [this](std::string code) { error = std::move(code); order.push_back("error"); };
        out.finished = [this](bool ok) { ++finished; succeeded = ok; order.push_back("finished"); };
        out.retry    = [this](std::chrono::milliseconds delay) { retry = delay; order.push_back("retry"); };
        out.rerun    = [this] { ++rerun; order.push_back("rerun"); };
        out.applied  = [this] { ++applied; order.push_back("applied"); };
        return out;
    }
};

/// One round over a scratch document, a scripted transport and a pinned clock.
struct Fixture {
    Fixture()
        : path(scratchPath("lens-sync-round-test.json")),
          store(KnownStore::load(path)),
          stats(store.document()),
          partition(store, [] { return std::string("device-a"); }),
          round(transport,
                store,
                stats,
                partition,
                SyncRound::Clock{[this] { return nowMs; }, [this] { return nowMinute; }},
                recorded.hooks())
    {
    }

    ~Fixture()
    {
        std::filesystem::remove(path);
    }

    /// @brief Store a key derived cheaply and a token pair, so a run needs no passphrase.
    void signedIn()
    {
        key = aKey("a fixture passphrase", quickParams());
        partition.storeKey(*key);
        partition.storeTokens("access-1", "refresh-1");
    }

    /// @brief Start a run, dropping the previous run's report so this one's is read alone.
    void start(bool manual, std::string passphrase = {})
    {
        recorded.reset();
        round.begin({.passphrase = std::move(passphrase), .manual = manual});
    }

    std::filesystem::path             path;
    KnownStore                        store;
    StatsStore                        stats;
    SyncPartition                     partition;
    ScriptedTransport                 transport;
    Recorded                          recorded;
    std::optional<syncrun::StoredKey> key;
    long long                         nowMs     = 5000;
    std::string                       nowMinute = "2026-10-26 12:00";
    SyncRound                         round;
};

} // namespace

/// The first run on an account with no gist: the listing finds none, the merged document goes up as
/// a new gist, and the time of the success is stamped for the settings page.
TEST(SyncRound, AFirstRunCreatesTheCloudCopyAndStampsTheTime)
{
    Fixture fixture;
    fixture.signedIn();

    fixture.start(true);
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::List));
    EXPECT_EQ(fixture.transport.front().token, "access-1");
    EXPECT_EQ(fixture.transport.front().description, "lens-sync");
    fixture.transport.gist("");

    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Create));
    const std::string sealed = fixture.transport.front().content;
    EXPECT_EQ(fixture.transport.front().fileName, "lens-sync.json");
    fixture.transport.gist("gist-1");

    EXPECT_FALSE(fixture.round.busy());
    EXPECT_EQ(fixture.partition.gistId(), "gist-1");
    EXPECT_EQ(fixture.partition.lastSyncedAt(), "2026-10-26 12:00");
    EXPECT_TRUE(fixture.recorded.succeeded);
    EXPECT_EQ(fixture.recorded.applied, 1);
    EXPECT_EQ(fixture.recorded.finished, 1);
    EXPECT_FALSE(fixture.round.carriesNewContent()) << "what went up is what this device holds";

    // The file is the document sealed under the key in force, which another device opens with the
    // salt the envelope carries.
    const json envelope  = json::parse(sealed);
    const auto plaintext = vault::open(fixture.key->key, envelope);
    ASSERT_TRUE(plaintext.has_value());
    const json document = json::parse(*plaintext);
    EXPECT_EQ(document.at("format").get<int>(), 1);
    EXPECT_TRUE(document.contains("known"));
}

/// The key that opened the cloud copy is this device's even when the run then fails: it was bought
/// with the passphrase, and the next run must not have to ask for it again.
TEST(SyncRound, TheKeyThatOpenedTheCloudCopyIsStoredEvenWhenTheRunFails)
{
    Fixture fixture;
    fixture.partition.storeTokens("access-1", "refresh-1");
    const auto        cloud    = aKey("the passphrase", quickParams());
    const std::string envelope = vault::seal(cloud.key, "[]", cloud.params).dump(); // opens, is not a document

    fixture.start(true, "the passphrase");
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::List));
    fixture.transport.gist("gist-1");
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Read));
    fixture.transport.gist("gist-1", envelope);

    EXPECT_FALSE(fixture.round.busy());
    EXPECT_EQ(fixture.recorded.error, "generic");
    EXPECT_EQ(fixture.recorded.finished, 1);
    EXPECT_FALSE(fixture.recorded.succeeded);
    ASSERT_TRUE(fixture.partition.hasKey());
    EXPECT_EQ(fixture.partition.storedKey()->key, cloud.key);
}

/// The fingerprint is taken after the merged document was written: a mark that arrived from the
/// cloud is part of what went up, so the run leaves nothing new to send.
TEST(SyncRound, TheFingerprintIsTakenAfterTheMergedDocumentIsApplied)
{
    Fixture fixture;
    fixture.signedIn();
    const json        remote{{"format", 1},
                             {"known", {{"river", {{"learned", true}, {"at", 1}}}}},
                             {"deleted", json::object()},
                             {"history", json::array()},
                             {"daily", json::object()},
                             {"settings", json::object()}};
    const std::string envelope = sealedCopy(*fixture.key, remote);

    fixture.start(false);
    fixture.transport.gist("gist-1");
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Read));
    fixture.transport.gist("gist-1", envelope);
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Update));
    const std::string sealed = fixture.transport.front().content;
    fixture.transport.gist("gist-1");

    EXPECT_TRUE(fixture.store.isKnown("river"));
    EXPECT_EQ(fixture.recorded.applied, 1);
    EXPECT_FALSE(fixture.round.carriesNewContent()) << "the merged mark is in the fingerprint the upload took";
    const auto plaintext = vault::open(fixture.key->key, json::parse(sealed));
    ASSERT_TRUE(plaintext.has_value());
    EXPECT_TRUE(json::parse(*plaintext).at("known").contains("river"));

    // A mark made afterwards is new again, which is what schedules the next run.
    fixture.store.mark("lemon", true);
    EXPECT_TRUE(fixture.round.carriesNewContent());
}

/// An access token GitHub stopped accepting is renewed once, and the run carries on from the
/// listing with the new pair stored.
TEST(SyncRound, A401IsRenewedOnceAndTheRunCarriesOnFromTheListing)
{
    Fixture fixture;
    fixture.signedIn();

    fixture.start(true);
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::List));
    fixture.transport.fail(Answer::Failure::Unauthorised);

    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Refresh));
    EXPECT_EQ(fixture.transport.front().token, "refresh-1") << "a refresh spends the stored refresh token";
    Answer renewed;
    renewed.access  = "access-2";
    renewed.refresh = "refresh-2";
    fixture.transport.deliver(renewed);

    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::List));
    EXPECT_EQ(fixture.transport.front().token, "access-2") << "the listing restarts under the new token";
    EXPECT_EQ(fixture.partition.token(), "access-2");
    EXPECT_EQ(fixture.partition.refreshToken(), "refresh-2");

    fixture.transport.gist("");
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Create));
    EXPECT_EQ(fixture.transport.front().token, "access-2");
    fixture.transport.gist("gist-2");
    EXPECT_TRUE(fixture.recorded.succeeded);
}

/// A refresh that is itself refused ends the run instead of leaving it half open: the tokens are
/// forgotten, the passphrase is dropped with the run, and a change made during it still books the
/// next one. This is the path that used to leave the run behind (docs/adr/0120).
TEST(SyncRound, AFailedRefreshEndsTheRunAndHonoursTheChangeThatArrivedDuringIt)
{
    Fixture fixture;
    fixture.signedIn();

    fixture.start(false);
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::List));
    fixture.round.requestRun(); // a local change arrives while the listing is out
    fixture.transport.fail(Answer::Failure::Unauthorised);
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Refresh));
    fixture.transport.fail(Answer::Failure::RefusedRefresh);

    EXPECT_FALSE(fixture.round.busy());
    EXPECT_EQ(fixture.recorded.error, "signInExpired");
    EXPECT_EQ(fixture.recorded.finished, 1);
    EXPECT_FALSE(fixture.recorded.succeeded);
    EXPECT_EQ(fixture.recorded.rerun, 1) << "the change during the run still asks for the next";
    EXPECT_FALSE(fixture.recorded.retry.has_value()) << "a dead refresh token cannot clear by retrying";
    EXPECT_FALSE(fixture.partition.hasRefreshToken());
    EXPECT_TRUE(fixture.transport.waiting.empty()) << "the run asked for nothing after the refusal";
}

/// A proxy failure an automatic run can clear by itself is retried with a doubling backoff, and a
/// success resets the count. A run the reader asked for is reported once instead.
TEST(SyncRound, AFailedAutomaticRunAsksForABackoffAndAManualOneDoesNot)
{
    Fixture fixture;
    fixture.signedIn();

    fixture.start(false);
    fixture.transport.fail(Answer::Failure::Proxy);
    ASSERT_TRUE(fixture.recorded.retry.has_value());
    EXPECT_EQ(fixture.recorded.retry->count(), 30 * 1000);

    fixture.start(false);
    fixture.transport.fail(Answer::Failure::Proxy);
    ASSERT_TRUE(fixture.recorded.retry.has_value());
    EXPECT_EQ(fixture.recorded.retry->count(), 60 * 1000);

    fixture.start(false);
    fixture.transport.fail(Answer::Failure::Generic);
    ASSERT_TRUE(fixture.recorded.retry.has_value());
    EXPECT_EQ(fixture.recorded.retry->count(), 2 * 60 * 1000);

    // A success clears the count: the next transient failure starts from 30 s again.
    fixture.start(false);
    fixture.transport.gist("");
    fixture.transport.gist("gist-1");
    ASSERT_TRUE(fixture.recorded.succeeded);
    fixture.start(false);
    fixture.transport.fail(Answer::Failure::Proxy);
    ASSERT_TRUE(fixture.recorded.retry.has_value());
    EXPECT_EQ(fixture.recorded.retry->count(), 30 * 1000);

    fixture.start(true);
    fixture.transport.fail(Answer::Failure::Proxy);
    EXPECT_FALSE(fixture.recorded.retry.has_value()) << "the reader asked for this one; it is reported, not retried";
}

/// The ten-minute poll: it pulls and merges like any other pass, and on a device where nothing has
/// changed it has nothing to send -- no upload, and the run is over.
TEST(SyncRound, AQuietTimerPassEndsWithoutAnUpload)
{
    Fixture fixture;
    fixture.signedIn();

    fixture.start(false);
    fixture.transport.gist("");
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Create));
    const std::string sealed = fixture.transport.front().content;
    fixture.transport.gist("gist-1");
    ASSERT_TRUE(fixture.recorded.succeeded);

    fixture.start(false);
    fixture.transport.gist("gist-1");
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Read));
    fixture.nowMinute = "2026-10-26 12:10";
    fixture.transport.gist("gist-1", sealed);

    EXPECT_FALSE(fixture.round.busy());
    EXPECT_TRUE(fixture.recorded.succeeded);
    EXPECT_TRUE(fixture.transport.waiting.empty()) << "nothing was uploaded";
    EXPECT_EQ(fixture.partition.lastSyncedAt(), "2026-10-26 12:10") << "a pass that worked stamps its time";
}

/// Deleting the cloud copy forgets its id, its key and its time, and clears what the last upload
/// described, so the next run starts the copy over. The sign-in itself stays.
TEST(SyncRound, DeletingTheCloudCopyForgetsItsIdentityAndKeepsTheSignIn)
{
    Fixture fixture;
    fixture.signedIn();
    fixture.partition.setGistId("gist-1");
    fixture.partition.setLastSyncedAt("2026-10-25 09:00");

    fixture.round.removeCloud();
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::Delete));
    EXPECT_EQ(fixture.transport.front().gistId, "gist-1");
    fixture.transport.gist("gist-1");

    EXPECT_FALSE(fixture.round.busy());
    EXPECT_TRUE(fixture.recorded.succeeded);
    EXPECT_TRUE(fixture.partition.gistId().empty());
    EXPECT_FALSE(fixture.partition.hasKey());
    EXPECT_TRUE(fixture.partition.lastSyncedAt().empty());
    EXPECT_TRUE(fixture.partition.signedIn()) << "the token is not the cloud copy's";
    EXPECT_TRUE(fixture.round.carriesNewContent()) << "there is no copy any more, so the next run uploads";
}

/// An answer to a run that was abandoned is not the next run's: the generation the request was made
/// under no longer matches, so nothing is asked for and nothing is reported.
TEST(SyncRound, AnAnswerToAForgottenRunIsIgnored)
{
    Fixture fixture;
    fixture.signedIn();

    fixture.start(true);
    ASSERT_TRUE(fixture.transport.waitingFor(Request::Kind::List));
    fixture.round.forget();
    fixture.transport.gist("gist-1");

    EXPECT_FALSE(fixture.round.busy());
    EXPECT_TRUE(fixture.transport.waiting.empty());
    EXPECT_EQ(fixture.recorded.finished, 0);
    EXPECT_TRUE(fixture.partition.gistId().empty());
}

/// With no passphrase and no stored key there is nothing to run under, and the page is told to ask
/// the reader for one rather than reporting a network failure.
TEST(SyncRound, NoKeyAndNoPassphraseAsksForThePassphrase)
{
    Fixture fixture;
    fixture.partition.storeTokens("access-1", "refresh-1");

    fixture.start(true);

    EXPECT_EQ(fixture.recorded.error, "passphrase");
    EXPECT_FALSE(fixture.round.busy());
    EXPECT_TRUE(fixture.transport.waiting.empty());
}
