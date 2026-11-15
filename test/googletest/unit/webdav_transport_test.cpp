/**
 * @file webdav_transport_test.cpp
 * @brief The port over the WebDAV client, driven request by request over the scripted network: the
 *        verbs the round asks for land on the right paths, and every answer keeps the shape the
 *        round reads.
 *
 * The backend's own reachability probe shares this client; the queue's call ids are what keep the
 * two apart (docs/adr/0170), and what this file drives is the mapping itself rather than a round.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkReply>
#include <QTemporaryDir>

#include <filesystem>
#include <functional>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "app/duties/webdav_transport.h"
#include "core/document/known_store.h"
#include "core/domain/sync_partition.h"
#include "core/domain/sync_round.h"
#include "scripted_manager.h"
#include "sync/webdav_client.h"

namespace {

namespace sync = lens::core::sync;
using lens::app::WebdavTransport;
using lens::core::KnownStore;
using lens::core::sync::SyncPartition;
using lens::sync::WebDavClient;
using lens::test::ScriptedManager;

struct QtApplication {
    int              argc     = 1;
    char             name[26] = "webdav_transport_test";
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

/// A scratch settings document with the three WebDAV credentials on it, and the transport over it.
struct Fixture {
    QTemporaryDir   dir;
    KnownStore      store{KnownStore::load(std::filesystem::path{dir.path().toStdWString()} / L"settings.json")};
    SyncPartition   partition{store};
    ScriptedManager manager;
    WebDavClient    dav{&manager};
    WebdavTransport transport{dav, partition};

    Fixture()
    {
        partition.setServer("https://dav.example.com/dav");
        partition.setUsername("reader");
        partition.setAppPassword("app-pass");
    }
};

/// @brief Carry one request and wait for its answer, whatever it turns out to be.
sync::Answer asked(Fixture& fixture, sync::Request request)
{
    std::optional<sync::Answer> got;
    fixture.transport.send(std::move(request), [&got](sync::Answer const& answer) { got = answer; });
    EXPECT_TRUE(spinUntil([&got] { return got.has_value(); }));
    return got.value_or(sync::Answer{});
}

} // namespace

TEST(WebdavTransport, TheListProbeNamesTheEnvelopeWhenItIsThere)
{
    QtApplication qt;
    Fixture       fixture;

    fixture.manager.queue(207);
    sync::Request request;
    request.kind              = sync::Request::Kind::List;
    const sync::Answer answer = asked(fixture, request);

    // The probe went to the envelope's own path, and finding it names that path as the cloud copy.
    ASSERT_EQ(fixture.manager.sent.size(), 1u);
    EXPECT_EQ(fixture.manager.sent[0].verb, "PROPFIND");
    EXPECT_EQ(fixture.manager.sent[0].url.toStdString(), "https://dav.example.com/dav/Lens/lens-sync.json");
    EXPECT_EQ(answer.gistId, "Lens/lens-sync.json");
    EXPECT_EQ(answer.failure, sync::Answer::Failure::None);
}

TEST(WebdavTransport, AListProbeThatFindsNothingNamesNoCopy)
{
    QtApplication qt;
    Fixture       fixture;

    fixture.manager.queue(404);
    sync::Request request;
    request.kind              = sync::Request::Kind::List;
    const sync::Answer answer = asked(fixture, request);

    // The first sync has nothing to read yet; an empty id is that answer, not a failure.
    EXPECT_EQ(answer.gistId, "");
    EXPECT_EQ(answer.failure, sync::Answer::Failure::None);
}

TEST(WebdavTransport, AReadHandsTheFileBackUnderItsOwnId)
{
    QtApplication qt;
    Fixture       fixture;

    fixture.manager.queue(200, QByteArray("{\"format\":1}"));
    sync::Request request;
    request.kind              = sync::Request::Kind::Read;
    request.gistId            = "Lens/lens-sync.json";
    const sync::Answer answer = asked(fixture, request);

    EXPECT_EQ(answer.gistId, "Lens/lens-sync.json");
    EXPECT_EQ(answer.content, "{\"format\":1}");
    EXPECT_EQ(fixture.manager.sent[0].verb, "GET");
}

TEST(WebdavTransport, ACreateMakesTheCollectionAndStoresThePathItWrote)
{
    QtApplication qt;
    Fixture       fixture;

    // The Lens collection is missing: the write makes it once, and the path that landed is what the
    // round stores as the cloud copy's id.
    fixture.manager.queue(409); // the PUT: the collection is missing
    fixture.manager.queue(201); // the MKCOL
    fixture.manager.queue(201); // the PUT again
    sync::Request request;
    request.kind              = sync::Request::Kind::Create;
    request.fileName          = "lens-sync.json";
    request.content           = "{\"format\":1}";
    const sync::Answer answer = asked(fixture, request);

    ASSERT_EQ(fixture.manager.sent.size(), 3u);
    EXPECT_EQ(fixture.manager.sent[0].verb, "PUT");
    EXPECT_EQ(fixture.manager.sent[0].url.toStdString(), "https://dav.example.com/dav/Lens/lens-sync.json");
    EXPECT_EQ(fixture.manager.sent[1].verb, "MKCOL");
    EXPECT_EQ(fixture.manager.sent[2].body.toStdString(), "{\"format\":1}");
    EXPECT_EQ(answer.gistId, "Lens/lens-sync.json");
    EXPECT_EQ(answer.failure, sync::Answer::Failure::None);
}

TEST(WebdavTransport, AnUpdateWritesOverThePathItIsGiven)
{
    QtApplication qt;
    Fixture       fixture;

    fixture.manager.queue(200);
    sync::Request request;
    request.kind              = sync::Request::Kind::Update;
    request.gistId            = "Lens/lens-sync.json";
    request.content           = "{\"format\":2}";
    const sync::Answer answer = asked(fixture, request);

    EXPECT_EQ(fixture.manager.sent[0].verb, "PUT");
    EXPECT_EQ(fixture.manager.sent[0].url.toStdString(), "https://dav.example.com/dav/Lens/lens-sync.json");
    // An update names no new id: the copy it overwrote is the one it was given.
    EXPECT_EQ(answer.gistId, "");
    EXPECT_EQ(answer.failure, sync::Answer::Failure::None);
}

TEST(WebdavTransport, ADeleteAnswersWithThePathItRemoved)
{
    QtApplication qt;
    Fixture       fixture;

    fixture.manager.queue(204);
    sync::Request request;
    request.kind              = sync::Request::Kind::Delete;
    request.gistId            = "Lens/lens-sync.json";
    const sync::Answer answer = asked(fixture, request);

    EXPECT_EQ(fixture.manager.sent[0].verb, "DELETE");
    EXPECT_EQ(answer.gistId, "Lens/lens-sync.json");
    EXPECT_EQ(answer.failure, sync::Answer::Failure::None);
}

TEST(WebdavTransport, ARefusalCarriesTheKindTheRoundReads)
{
    QtApplication qt;
    Fixture       fixture;

    // The server's own kinds, one answer each.
    for (const auto [status, expected] : std::initializer_list<std::pair<int, sync::Answer::Failure>>{
             {401, sync::Answer::Failure::Unauthorised},
             {404, sync::Answer::Failure::NotFound},
             {500, sync::Answer::Failure::Generic},
         }) {
        fixture.manager.queue(status);
        sync::Request request;
        request.kind              = sync::Request::Kind::Read;
        request.gistId            = "Lens/lens-sync.json";
        const sync::Answer answer = asked(fixture, request);
        EXPECT_EQ(answer.failure, expected) << "status " << status;
        EXPECT_EQ(answer.content, "") << "a refusal carries no text from the server";
    }

    // And a connection that never got a byte is the proxy kind.
    fixture.manager.queue(302, {}, {}, false);
    sync::Request request;
    request.kind   = sync::Request::Kind::Read;
    request.gistId = "Lens/lens-sync.json";
    std::optional<sync::Answer> got;
    fixture.transport.send(std::move(request), [&got](sync::Answer const& answer) { got = answer; });
    ASSERT_TRUE(spinUntil([&] {
        for (auto* reply : fixture.manager.findChildren<QNetworkReply*>())
            if (not reply->isFinished())
                reply->abort();
        return got.has_value();
    }));
    EXPECT_EQ(got->failure, sync::Answer::Failure::Proxy);
}

TEST(WebdavTransport, ARefreshIsRefusedAtOnceWithoutARequest)
{
    QtApplication qt;
    Fixture       fixture;

    sync::Request request;
    request.kind              = sync::Request::Kind::Refresh;
    request.token             = "refresh-1";
    const sync::Answer answer = asked(fixture, request);

    // Basic auth has nothing to renew; the round is told that where it stands.
    EXPECT_EQ(answer.failure, sync::Answer::Failure::RefusedRefresh);
    EXPECT_EQ(fixture.manager.requests, 0);
}
