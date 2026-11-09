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
    QObject::connect(&client, &dav::WebDavClient::putDone, [&done](quint64, QString path) { done = path; });

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
    QObject::connect(&client, &dav::WebDavClient::deleted, [&done](quint64, QString path) { done = path; });

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
