/**
 * @file gist_client_test.cpp
 * @brief The gist requests over the scripted network: what each status means, how a truncated file
 *        is refilled from its raw address, how the list follows its Link header, and the ceiling
 *        that keeps a hostile answer from being read at all.
 *
 * Only the parsers were tested before (github_auth_test.cpp); every request path here was dark.
 * The network is scripted, so nothing reaches GitHub, and a pending reply's abort() is what a
 * connection that never got a byte does.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QNetworkReply>
#include <QString>

#include <functional>
#include <optional>

#include <gtest/gtest.h>

#include "scripted_manager.h"
#include "sync/gist_client.h"

namespace {

namespace sync = lens::sync;
using lens::test::ScriptedManager;

struct QtApplication {
    int              argc     = 1;
    char             name[22] = "gist_client_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

bool spinUntil(const std::function<bool()>& done, int ms = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done() && clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

/// The gist GitHub answers a read with: one file, whole.
const QByteArray kGistBody = R"({"id":"abc123","description":"lens-sync","updated_at":"2026-10-14T10:00:00Z",)"
                             R"("files":{"lens-sync.json":{"content":"{}","truncated":false,)"
                             R"("raw_url":"https://gist.githubusercontent.com/x/raw"}}})";

/// The same gist with its one file truncated: the content is a fragment, and the full text sits at
/// the raw address the body names.
const QByteArray kTruncatedBody = R"({"id":"abc123","description":"lens-sync","updated_at":"2026-10-14T10:00:00Z",)"
                                  R"("files":{"lens-sync.json":{"content":"{\"a\":","truncated":true,)"
                                  R"("raw_url":"https://gist.githubusercontent.com/x/raw/big"}}})";

const QByteArray kFullContent = R"({"a":1,"b":2})";

/// What one call did. Every signal carries the call's id, and a call answers once.
struct Answer {
    quint64                          callId = 0;
    int                              calls  = 0;
    std::optional<sync::Gist>        gist;
    QByteArray                       listed;
    QString                          text;
    std::optional<sync::GistFailure> why;
};

void record(sync::GistClient& client, Answer& seen)
{
    QObject::connect(&client, &sync::GistClient::gotGist, [&seen](quint64 callId, sync::Gist gist) {
        ++seen.calls;
        seen.callId = callId;
        seen.gist   = std::move(gist);
    });
    QObject::connect(&client, &sync::GistClient::created, [&seen](quint64 callId, sync::Gist gist) {
        ++seen.calls;
        seen.callId = callId;
        seen.gist   = std::move(gist);
    });
    QObject::connect(&client, &sync::GistClient::updated, [&seen](quint64 callId, sync::Gist gist) {
        ++seen.calls;
        seen.callId = callId;
        seen.gist   = std::move(gist);
    });
    QObject::connect(&client, &sync::GistClient::deleted, [&seen](quint64 callId, QString id) {
        ++seen.calls;
        seen.callId = callId;
        seen.text   = std::move(id);
    });
    QObject::connect(&client, &sync::GistClient::gotUser, [&seen](quint64 callId, QString login) {
        ++seen.calls;
        seen.callId = callId;
        seen.text   = std::move(login);
    });
    QObject::connect(&client, &sync::GistClient::listed, [&seen](quint64 callId, QByteArray body) {
        ++seen.calls;
        seen.callId = callId;
        seen.listed = std::move(body);
    });
    QObject::connect(&client, &sync::GistClient::failed, [&seen](quint64 callId, sync::GistFailure failure) {
        ++seen.calls;
        seen.callId = callId;
        seen.why    = failure;
    });
}

} // namespace

TEST(GistClient, AReadReturnsTheGistWithItsFiles)
{
    QtApplication    qt;
    ScriptedManager  manager{200, {}, true, kGistBody};
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    const quint64 callId = client.getGist("token-1", "abc123");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.callId, callId);
    ASSERT_TRUE(seen.gist.has_value());
    EXPECT_EQ(seen.gist->id, "abc123");
    ASSERT_EQ(seen.gist->files.size(), 1);
    EXPECT_EQ(seen.gist->files.value("lens-sync.json").content, "{}");
    // The call went to the API with the token as a bearer header, for no host but GitHub's.
    ASSERT_EQ(manager.sent.size(), 1u);
    EXPECT_EQ(manager.sent[0].verb, "GET");
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://api.github.com/gists/abc123");
}

TEST(GistClient, ATruncatedFileIsRefilledFromItsRawAddress)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    manager.queue(200, kTruncatedBody); // the read: one truncated file
    manager.queue(200, kFullContent);   // the raw refill
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    client.getGist("token-1", "abc123");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    ASSERT_TRUE(seen.gist.has_value());
    ASSERT_EQ(seen.gist->files.size(), 1);
    const sync::GistFile file = seen.gist->files.value("lens-sync.json");
    EXPECT_FALSE(file.truncated);
    EXPECT_EQ(file.content, kFullContent);
    // Two requests: the read, then the raw address the body named.
    ASSERT_EQ(manager.sent.size(), 2u);
    EXPECT_EQ(manager.sent[1].url.toStdString(), "https://gist.githubusercontent.com/x/raw/big");
}

TEST(GistClient, ARawAddressOffTheGistHostsIsRefused)
{
    // The raw address is on the gist host; a body naming somewhere else is not asked for, and the
    // file stays short -- the call fails rather than fetching from a stranger.
    QtApplication   qt;
    ScriptedManager manager{500};
    QByteArray      evil = kTruncatedBody;
    evil.replace("gist.githubusercontent.com", "evil.example.com");
    manager.queue(200, evil);
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    client.getGist("token-1", "abc123");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.why, sync::GistFailure::Generic);
    EXPECT_EQ(manager.requests, 1) << "the raw address was never asked";
}

TEST(GistClient, ARefusedReadCarriesTheKindAndNeverTheServer)
{
    QtApplication    qt;
    ScriptedManager  manager{500};
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    // Revoked, or the scope lost: the sign-in is over, and the kind says so.
    manager.queue(401);
    client.getGist("token-1", "abc123");
    ASSERT_TRUE(spinUntil([&] { return seen.why.has_value(); }));
    EXPECT_EQ(seen.why, sync::GistFailure::Unauthorised);

    // Throttled reads the same way -- the token is fine, the call is not.
    seen.why.reset();
    manager.queue(403);
    client.getGist("token-1", "abc123");
    ASSERT_TRUE(spinUntil([&] { return seen.why.has_value(); }));
    EXPECT_EQ(seen.why, sync::GistFailure::Unauthorised);

    // A gist the token cannot see.
    seen.why.reset();
    manager.queue(404);
    client.getGist("token-1", "gone");
    ASSERT_TRUE(spinUntil([&] { return seen.why.has_value(); }));
    EXPECT_EQ(seen.why, sync::GistFailure::NotFound);

    seen.why.reset();
    manager.queue(500, QByteArray("oops"));
    client.getGist("token-1", "abc123");
    ASSERT_TRUE(spinUntil([&] { return seen.why.has_value(); }));
    EXPECT_EQ(seen.why, sync::GistFailure::Generic);

    // No answer at all: the connection, which is what a blocked route looks like.
    seen.why.reset();
    manager.queue(302, {}, {}, false);
    client.getGist("token-1", "abc123");
    ASSERT_TRUE(spinUntil([&] {
        for (auto* reply : manager.findChildren<QNetworkReply*>())
            if (not reply->isFinished())
                reply->abort();
        return seen.why.has_value();
    }));
    EXPECT_EQ(seen.why, sync::GistFailure::Proxy);
}

TEST(GistClient, ACreateCarriesTheFilesAndAnswersWithTheGist)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    manager.queue(201, kGistBody);
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    QMap<QString, QString> files;
    files.insert("lens-sync.json", "{}");
    const quint64 callId = client.createGist("token-1", "lens-sync", files);
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.callId, callId);
    ASSERT_TRUE(seen.gist.has_value());
    EXPECT_EQ(seen.gist->id, "abc123");
    // One POST, no retry: the body is the description and the files as the API wants them.
    ASSERT_EQ(manager.sent.size(), 1u);
    EXPECT_EQ(manager.sent[0].verb, "POST");
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://api.github.com/gists");
    const QJsonObject body = QJsonDocument::fromJson(manager.sent[0].body).object();
    EXPECT_EQ(body.value("description"), "lens-sync");
    EXPECT_FALSE(body.value("public").toBool());
    EXPECT_EQ(body.value("files").toObject().value("lens-sync.json").toObject().value("content"), "{}");
}

TEST(GistClient, ARefusedCreateFailsRatherThanRetrying)
{
    // A create whose answer is lost leaves an orphan gist with the same description; retrying would
    // make another. One attempt, and the failure is the caller's to read.
    QtApplication    qt;
    ScriptedManager  manager{500, {}, true, QByteArray("error")};
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    client.createGist("token-1", "lens-sync", QMap<QString, QString>{});
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));
    ASSERT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.why, sync::GistFailure::Generic);
    EXPECT_EQ(manager.requests, 1);
}

TEST(GistClient, AnUpdatePatchesTheNamedFiles)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    manager.queue(200, kGistBody);
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    QMap<QString, QString> files;
    files.insert("lens-sync.json", R"({"format":2})");
    client.updateGist("token-1", "abc123", files);
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    ASSERT_TRUE(seen.gist.has_value());
    ASSERT_EQ(manager.sent.size(), 1u);
    EXPECT_EQ(manager.sent[0].verb, "PATCH");
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://api.github.com/gists/abc123");
    const QJsonObject body = QJsonDocument::fromJson(manager.sent[0].body).object();
    EXPECT_EQ(body.value("files").toObject().value("lens-sync.json").toObject().value("content"), R"({"format":2})");
}

TEST(GistClient, ADeleteCountsAGoneGistAsDeleted)
{
    QtApplication    qt;
    ScriptedManager  manager{500};
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    // 204 is the answer; a 404 means it is already gone, which is what the caller wanted. Anything
    // else is the server saying no.
    for (const int status : {204, 404}) {
        seen.calls = 0;
        seen.text.clear();
        manager.queue(status);
        client.deleteGist("token-1", "abc123");
        ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));
        ASSERT_EQ(seen.calls, 1);
        EXPECT_EQ(seen.text, "abc123");
        EXPECT_FALSE(seen.why.has_value());
    }

    seen.calls = 0;
    seen.why.reset();
    manager.queue(500);
    client.deleteGist("token-1", "abc123");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));
    EXPECT_EQ(seen.why, sync::GistFailure::Generic);
}

TEST(GistClient, TheLoginNameComesBackFromTheUserCall)
{
    QtApplication    qt;
    ScriptedManager  manager{500};
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    manager.queue(200, QByteArray(R"({"login":"reader"})"));
    client.getUser("token-1");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));
    ASSERT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.text, "reader");
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://api.github.com/user");

    // A token that cannot name a login is a refusal, not an empty name.
    seen.calls = 0;
    seen.why.reset();
    manager.queue(200, QByteArray(R"({"login":""})"));
    client.getUser("token-1");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));
    EXPECT_EQ(seen.why, sync::GistFailure::Generic);
}

TEST(GistClient, TheListFollowsItsNextLink)
{
    QtApplication    qt;
    ScriptedManager  manager{500};
    const QByteArray page = R"([{"id":"g1","description":"lens-sync","updated_at":"2026-10-01T00:00:00Z"}])";
    // The first answer names the next page with its Link header; the second is the last.
    manager.queue(200, page, {}, true, {}, QByteArrayLiteral("<https://api.github.com/gists?per_page=100&page=2>; rel=\"next\""));
    manager.queue(200, QByteArray(page).replace("g1", "g2"));
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    client.listGists("token-1");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    // Both pages, joined into one array for findGistId() to read as the newest-first list it is.
    const QJsonDocument merged = QJsonDocument::fromJson(seen.listed);
    ASSERT_TRUE(merged.isArray());
    ASSERT_EQ(merged.array().size(), 2);
    EXPECT_EQ(merged.array().at(0).toObject().value("id"), "g1");
    EXPECT_EQ(merged.array().at(1).toObject().value("id"), "g2");
    ASSERT_EQ(manager.sent.size(), 2u);
    EXPECT_EQ(manager.sent[1].url.toStdString(), "https://api.github.com/gists?per_page=100&page=2");
}

TEST(GistClient, TheListStopsAtFivePagesWhateverTheLinksSay)
{
    QtApplication    qt;
    ScriptedManager  manager{200, {}, true, QByteArray(R"([{"id":"g"}])"), {}, QByteArrayLiteral("<https://api.github.com/gists?per_page=100&page=next>; rel=\"next\"")};
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    client.listGists("token-1");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));

    // Every answer names a next page; the cap is what ends the list, after five requests.
    ASSERT_EQ(seen.calls, 1);
    EXPECT_EQ(manager.requests, 5);
}

TEST(GistClient, TheListIgnoresANextLinkOffTheApiHost)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    manager.queue(200, QByteArray(R"([{"id":"g1"}])"), {}, true, {}, QByteArrayLiteral("<https://evil.example.com/next>; rel=\"next\""));
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    client.listGists("token-1");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));

    ASSERT_EQ(seen.calls, 1);
    EXPECT_EQ(manager.requests, 1) << "the next address was never asked";
    EXPECT_EQ(QJsonDocument::fromJson(seen.listed).array().size(), 1);
}

TEST(GistClient, AListAnswerThatIsNotAnArrayFails)
{
    QtApplication    qt;
    ScriptedManager  manager{200, {}, true, QByteArray(R"({"not":"a list"})")};
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    client.listGists("token-1");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));
    ASSERT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.why, sync::GistFailure::Generic);
}

TEST(GistClient, AnAnswerPastTheCeilingIsDropped)
{
    // A sync document is far smaller than the ceiling; a body past it is not the server speaking to
    // this call, so it is dropped and the call fails rather than parsing ten megabytes.
    QtApplication    qt;
    ScriptedManager  manager{200, {}, true, QByteArray(10 * 1024 * 1024 + 1, 'x')};
    sync::GistClient client(&manager);
    Answer           seen;
    record(client, seen);

    client.getGist("token-1", "abc123");
    ASSERT_TRUE(spinUntil([&] { return seen.calls > 0; }));
    ASSERT_EQ(seen.calls, 1);
    EXPECT_EQ(seen.why, sync::GistFailure::Generic);
}
