/**
 * @file webdav_client_test.cpp
 * @brief The WebDAV verbs over the scripted network: what each status means, and the collection
 *        a first write makes on the way (docs/adr/0157).
 *
 * The network is scripted, so nothing here reaches a server. A pending reply's abort() is what a
 * connection that never got a byte does, which is how the offline judgement is driven.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkReply>
#include <QObject>
#include <QString>

#include <functional>
#include <initializer_list>
#include <optional>

#include <gtest/gtest.h>

#include "scripted_manager.h"
#include "sync/webdav_client.h"

namespace {

namespace dav = lens::sync;
using lens::test::ScriptedManager;

struct QtApplication {
    int              argc     = 1;
    char             name[24] = "webdav_client_test";
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

}

/// 207 says the file is there; 404 and 409 say it is not, which is an answer rather than a failure.
/// Nutstore answers 409 for any path under a collection that does not exist yet, where
/// nginx-flavoured servers say 404 (docs/adr/0157).
TEST(WebDavClient, APropfindAnswersWhetherTheFileExists)
{
    QtApplication       qt;
    ScriptedManager     manager;
    dav::WebDavClient   client(&manager);
    std::optional<bool> found;
    QObject::connect(&client, &dav::WebDavClient::found, [&found](quint64, bool exists) { found = exists; });

    manager.queue(207);
    client.propfind("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return found.has_value(); }));
    EXPECT_TRUE(*found) << "207 multi-status: the collection and the file exist";
    EXPECT_EQ(manager.sent.size(), 1u);
    EXPECT_EQ(manager.sent[0].verb, "PROPFIND");
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://dav.example.com/dav/Lens/lens-sync.json");
    EXPECT_EQ(manager.sent[0].depth, "0");

    found.reset();
    manager.queue(404);
    client.propfind("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return found.has_value(); }));
    EXPECT_FALSE(*found) << "the first sync has nothing to read yet, and that is not an error";

    found.reset();
    manager.queue(409);
    client.propfind("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return found.has_value(); }));
    EXPECT_FALSE(*found) << "the collection is missing, so the file cannot exist";
}

/// A write into a collection the server does not have yet makes the collection once and tries again.
TEST(WebDavClient, APutThatConflictsMakesTheCollectionAndWritesAgain)
{
    QtApplication     qt;
    ScriptedManager   manager;
    dav::WebDavClient client(&manager);
    QString           done;
    QObject::connect(&client, &dav::WebDavClient::putDone, [&done](quint64, QString path) { done = std::move(path); });

    manager.queue(409); // the PUT: the Lens collection is missing
    manager.queue(201); // the MKCOL
    manager.queue(201); // the PUT again
    client.put("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json", "{\"format\":1}");
    ASSERT_TRUE(spinUntil([&] { return not done.isEmpty(); }));
    EXPECT_EQ(done.toStdString(), "Lens/lens-sync.json");
    ASSERT_EQ(manager.sent.size(), 3u);
    EXPECT_EQ(manager.sent[0].verb, "PUT");
    EXPECT_EQ(manager.sent[1].verb, "MKCOL");
    EXPECT_EQ(manager.sent[1].url.toStdString(), "https://dav.example.com/dav/Lens");
    EXPECT_EQ(manager.sent[2].verb, "PUT");
    EXPECT_EQ(manager.sent[2].body.toStdString(), "{\"format\":1}");
}

/// A second 409 after the collection was made is a real refusal: the write stops rather than
/// making the same collection again for as long as the server keeps saying no.
TEST(WebDavClient, ASecondConflictAfterTheCollectionFails)
{
    QtApplication                     qt;
    ScriptedManager                   manager;
    dav::WebDavClient                 client(&manager);
    std::optional<dav::WebDavFailure> why;
    QObject::connect(&client, &dav::WebDavClient::failed, [&why](quint64, dav::WebDavFailure failure) { why = failure; });

    manager.queue(409); // the PUT: the collection is missing
    manager.queue(201); // the MKCOL
    manager.queue(409); // the PUT again: a second refusal is real
    client.put("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json", "{}");
    ASSERT_TRUE(spinUntil([&] { return why.has_value(); }));
    EXPECT_EQ(*why, dav::WebDavFailure::Generic);
    EXPECT_EQ(manager.sent.size(), 3u) << "a second 409 must not make the collection again";
}

/// A server that refuses the credentials, and a connection that never got a byte, are different kinds.
TEST(WebDavClient, FailuresCarryTheKindAndNeverTheServer)
{
    QtApplication                     qt;
    ScriptedManager                   manager;
    dav::WebDavClient                 client(&manager);
    std::optional<dav::WebDavFailure> why;
    QObject::connect(&client, &dav::WebDavClient::failed, [&why](quint64, dav::WebDavFailure failure) { why = failure; });

    manager.queue(401);
    client.get("https://dav.example.com/dav", "reader", "wrong-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return why.has_value(); }));
    EXPECT_EQ(*why, dav::WebDavFailure::Unauthorised);

    // A pending reply that aborts is a connection that never answered: no status, no bytes.
    why.reset();
    manager.queue(302, {}, {}, false);
    client.get("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] {
        for (auto* reply : manager.findChildren<QNetworkReply*>())
            if (not reply->isFinished())
                reply->abort();
        return why.has_value();
    }));
    EXPECT_EQ(*why, dav::WebDavFailure::Proxy);
}

/// A reader who pastes a bare host meant https; the envelope path joins under the base. Deleting
/// what is already gone -- or whose collection is gone -- counts as deleted.
TEST(WebDavClient, AnAddressWithoutASchemeIsAskedOverHttps)
{
    QtApplication     qt;
    ScriptedManager   manager;
    dav::WebDavClient client(&manager);
    QString           done;
    QObject::connect(&client, &dav::WebDavClient::deleted, [&done](quint64, QString path) { done = std::move(path); });

    manager.queue(204);
    client.del("dav.example.com/dav/", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return not done.isEmpty(); }));
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://dav.example.com/dav/Lens/lens-sync.json");
    EXPECT_EQ(done.toStdString(), "Lens/lens-sync.json");

    done.clear();
    manager.queue(409);
    client.del("dav.example.com/dav/", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return not done.isEmpty(); }));
    EXPECT_EQ(done.toStdString(), "Lens/lens-sync.json") << "a delete of a missing collection is a delete";
}

/// Only http and https go out: a reader's own NAS or an Alist on the LAN is legitimately plain
/// http, but anything else -- a scheme typed wrong, an empty address -- is not asked at all.
TEST(WebDavClient, AnAddressThatIsNotHttpOrHttpsIsRefusedWithoutARequest)
{
    QtApplication                     qt;
    ScriptedManager                   manager;
    dav::WebDavClient                 client(&manager);
    std::optional<dav::WebDavFailure> why;
    QObject::connect(&client, &dav::WebDavClient::failed, [&why](quint64, dav::WebDavFailure failure) { why = failure; });

    client.propfind("ftp://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return why.has_value(); }));
    EXPECT_EQ(*why, dav::WebDavFailure::Generic);
    EXPECT_EQ(manager.requests, 0) << "the refusal happens before anything is sent";

    // Every verb checks, and so does an address that never became a URL at all.
    for (const QString& server : {QStringLiteral("ftp://dav.example.com/dav"), QStringLiteral("  ")}) {
        for (const auto& call : std::initializer_list<std::function<void()>>{
                 [&] { client.get(server, "reader", "app-pass", "Lens/lens-sync.json"); },
                 [&] { client.put(server, "reader", "app-pass", "Lens/lens-sync.json", "{}"); },
                 [&] { client.del(server, "reader", "app-pass", "Lens/lens-sync.json"); },
             }) {
            why.reset();
            call();
            ASSERT_TRUE(spinUntil([&] { return why.has_value(); }));
            EXPECT_EQ(*why, dav::WebDavFailure::Generic);
        }
    }
    EXPECT_EQ(manager.requests, 0);
}

/// A read that answers hands the file's text over; a probe that answers 200 says the server is
/// there, the way a sign-in's reachability check reads it.
TEST(WebDavClient, AReadHandsOverTheTextAndAProbeThatAnsweredIsFound)
{
    QtApplication       qt;
    ScriptedManager     manager;
    dav::WebDavClient   client(&manager);
    QString             file;
    std::optional<bool> found;
    QObject::connect(&client, &dav::WebDavClient::gotFile, [&file](quint64, QString content) { file = std::move(content); });
    QObject::connect(&client, &dav::WebDavClient::found, [&found](quint64, bool exists) { found = exists; });

    manager.queue(200, QByteArray("{\"format\":1}"));
    client.get("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return not file.isEmpty(); }));
    EXPECT_EQ(file.toStdString(), "{\"format\":1}");

    found.reset();
    manager.queue(200);
    client.propfind("https://dav.example.com/dav", "reader", "app-pass", "");
    ASSERT_TRUE(spinUntil([&] { return found.has_value(); }));
    EXPECT_TRUE(*found) << "the probe asked about the server itself, and it answered";
}

/// A file that is not there is NotFound, its own kind: the page offers re-entering the server
/// rather than a generic "something went wrong".
TEST(WebDavClient, AFileThatIsNotThereIsNotFound)
{
    QtApplication                     qt;
    ScriptedManager                   manager;
    dav::WebDavClient                 client(&manager);
    std::optional<dav::WebDavFailure> why;
    QObject::connect(&client, &dav::WebDavClient::failed, [&why](quint64, dav::WebDavFailure failure) { why = failure; });

    manager.queue(404);
    client.get("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return why.has_value(); }));
    EXPECT_EQ(*why, dav::WebDavFailure::NotFound);
}

/// A collection the server refuses to make, a write refused for any other reason, and a delete the
/// server says no to: all three stop with the server's kind, and never retry.
TEST(WebDavClient, RefusalsThatAreNotConflictsStopTheCall)
{
    QtApplication                     qt;
    ScriptedManager                   manager;
    dav::WebDavClient                 client(&manager);
    std::optional<dav::WebDavFailure> why;
    QObject::connect(&client, &dav::WebDavClient::failed, [&why](quint64, dav::WebDavFailure failure) { why = failure; });

    // The collection cannot be made: the MKCOL's own refusal ends it, before any retry.
    manager.queue(409); // the PUT
    manager.queue(500); // the MKCOL
    client.put("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json", "{}");
    ASSERT_TRUE(spinUntil([&] { return why.has_value(); }));
    EXPECT_EQ(*why, dav::WebDavFailure::Generic);
    EXPECT_EQ(manager.sent.size(), 2u);

    // A write refused without a 409 never reaches the collection request at all.
    why.reset();
    manager.queue(500);
    client.put("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json", "{}");
    ASSERT_TRUE(spinUntil([&] { return why.has_value(); }));
    EXPECT_EQ(*why, dav::WebDavFailure::Generic);
    EXPECT_EQ(manager.sent.size(), 3u);

    // A delete the server says no to is a failure, not a delete.
    why.reset();
    manager.queue(500);
    client.del("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return why.has_value(); }));
    EXPECT_EQ(*why, dav::WebDavFailure::Generic);
}

/// An answer past the ceiling is not the server speaking to this call: the body is dropped, and the
/// call reads an empty file rather than ten megabytes of something else.
TEST(WebDavClient, AnAnswerPastTheCeilingIsDropped)
{
    QtApplication     qt;
    ScriptedManager   manager;
    dav::WebDavClient client(&manager);
    QString           file;
    bool              answered = false;
    QObject::connect(&client, &dav::WebDavClient::gotFile, [&](quint64, QString content) {
        answered = true;
        file     = std::move(content);
    });

    manager.queue(200, QByteArray(10 * 1024 * 1024 + 1, 'x'));
    client.get("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] { return answered; }));
    EXPECT_TRUE(file.isEmpty());
}

/// A listing asks for one level and keeps only the collection's direct children: the collection
/// itself, a deeper path and a sibling collection are not files in this directory.
TEST(WebDavClient, AListingReadsTheDirectChildren)
{
    QtApplication              qt;
    ScriptedManager            manager;
    dav::WebDavClient          client(&manager);
    std::optional<QStringList> names;
    QObject::connect(&client, &dav::WebDavClient::listed, [&names](quint64, QStringList listed) { names = std::move(listed); });

    const QByteArray body = R"(<?xml version="1.0"?>
        <d:multistatus xmlns:d="DAV:">
          <d:response><d:href>/dav/Lens/</d:href></d:response>
          <d:response><d:href>/dav/Lens/lens-sync.json</d:href></d:response>
          <d:response><d:href>/dav/Lens/lens-events-abcd1234-2026-11-r0001-aaaaaaaa.json.enc</d:href></d:response>
          <d:response><d:href>/dav/Lens/with%20space.txt</d:href></d:response>
          <d:response><d:href>/dav/Lens/sub/file.txt</d:href></d:response>
          <d:response><d:href>/dav/Other/file.txt</d:href></d:response>
        </d:multistatus>)";
    manager.queue(207, body);
    client.list("https://dav.example.com/dav", "reader", "app-pass", "Lens");
    ASSERT_TRUE(spinUntil([&] { return names.has_value(); }));

    const QStringList expected{
        "lens-events-abcd1234-2026-11-r0001-aaaaaaaa.json.enc",
        "lens-sync.json",
        "with space.txt",
    };
    EXPECT_EQ(*names, expected);
    ASSERT_EQ(manager.sent.size(), 1u);
    EXPECT_EQ(manager.sent[0].verb, "PROPFIND");
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://dav.example.com/dav/Lens");
    EXPECT_EQ(manager.sent[0].depth, "1");
}

/// A collection that does not exist yet has no children; the first upload makes it.
TEST(WebDavClient, AMissingCollectionListsAsEmpty)
{
    QtApplication              qt;
    ScriptedManager            manager;
    dav::WebDavClient          client(&manager);
    std::optional<QStringList> names;
    QObject::connect(&client, &dav::WebDavClient::listed, [&names](quint64, QStringList listed) { names = std::move(listed); });

    manager.queue(404);
    client.list("https://dav.example.com/dav", "reader", "app-pass", "Lens");
    ASSERT_TRUE(spinUntil([&] { return names.has_value(); }));
    EXPECT_TRUE(names->isEmpty());
}

/// XML that is not a multistatus is a failed listing, not an empty collection: an empty answer
/// would let the uploader treat the cloud as clean and delete what it could not see.
TEST(WebDavClient, AMalformedListingFails)
{
    QtApplication                     qt;
    ScriptedManager                   manager;
    dav::WebDavClient                 client(&manager);
    std::optional<dav::WebDavFailure> why;
    QObject::connect(&client, &dav::WebDavClient::failed, [&why](quint64, dav::WebDavFailure failure) { why = failure; });

    manager.queue(207, "<not-a-multistatus>");
    client.list("https://dav.example.com/dav", "reader", "app-pass", "Lens");
    ASSERT_TRUE(spinUntil([&] { return why.has_value(); }));
    EXPECT_EQ(*why, dav::WebDavFailure::Generic);
}

/// A delete reads every "already gone" answer as the delete that was wanted, whichever shape the
/// server says it in, and a refused password as the credential kind the form is there to fix.
TEST(WebDavClient, ADeleteCountsEveryGoneAnswerAsDeleted)
{
    QtApplication                     qt;
    ScriptedManager                   manager{500};
    dav::WebDavClient                 client(&manager);
    QString                           deletedPath;
    std::optional<dav::WebDavFailure> why;
    QObject::connect(&client, &dav::WebDavClient::deleted, [&deletedPath](quint64, QString path) { deletedPath = std::move(path); });
    QObject::connect(&client, &dav::WebDavClient::failed, [&why](quint64, dav::WebDavFailure failure) { why = failure; });

    // 204 and 409 are covered beside the address normalisation; 200 and 404 are the other two
    // answers a delete of something already gone comes back with.
    for (const int status : {200, 404}) {
        deletedPath.clear();
        manager.queue(status);
        client.del("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
        ASSERT_TRUE(spinUntil([&] { return !deletedPath.isEmpty(); }));
        EXPECT_EQ(deletedPath.toStdString(), "Lens/lens-sync.json") << "status " << status;
    }

    for (const int status : {401, 403}) {
        why.reset();
        manager.queue(status);
        client.del("https://dav.example.com/dav", "reader", "wrong-pass", "Lens/lens-sync.json");
        ASSERT_TRUE(spinUntil([&why] { return why.has_value(); }));
        EXPECT_EQ(*why, dav::WebDavFailure::Unauthorised) << "status " << status;
    }
}

/// A delete whose connection never got a byte is the proxy kind, as it is for every other verb:
/// the copy is still there and the next sync will try again.
TEST(WebDavClient, ADeleteThatNeverAnsweredIsAProxyFailure)
{
    QtApplication                     qt;
    ScriptedManager                   manager;
    dav::WebDavClient                 client(&manager);
    std::optional<dav::WebDavFailure> why;
    QObject::connect(&client, &dav::WebDavClient::failed, [&why](quint64, dav::WebDavFailure failure) { why = failure; });

    manager.queue(302, {}, {}, false);
    client.del("https://dav.example.com/dav", "reader", "app-pass", "Lens/lens-sync.json");
    ASSERT_TRUE(spinUntil([&] {
        for (auto* reply : manager.findChildren<QNetworkReply*>())
            if (!reply->isFinished())
                reply->abort();
        return why.has_value();
    }));
    EXPECT_EQ(*why, dav::WebDavFailure::Proxy);
}
