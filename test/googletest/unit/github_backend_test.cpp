/**
 * @file github_backend_test.cpp
 * @brief The GitHub backend's sign-in state machine, over the scripted network: the code the page
 *        shows, the tokens the grant stores, the login that ends the flow, and the page each
 *        failure sends back to.
 *
 * The flow itself is GithubAuth's (github_auth_test.cpp); what this drives is the wiring around it
 * -- the phases, the texts the page's error codes carry, and the stored tokens each ending leaves
 * behind.
 */

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include "app/duties/github_backend.h"
#include "core/document/known_store.h"
#include "core/domain/sync_partition.h"
#include "scripted_manager.h"

namespace {

using lens::app::GithubBackend;
using lens::app::SignInPhase;
using lens::core::KnownStore;
using lens::core::sync::SyncPartition;
using lens::test::ScriptedManager;

struct QtApplication {
    int              argc     = 1;
    char             name[24] = "github_backend_test";
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

/// The device-code answer, with a one-second poll so the waits stay in seconds.
const QByteArray kCodeBody = R"({"device_code":"d","user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device",)"
                             R"("expires_in":900,"interval":1})";
const QByteArray kPending  = QByteArrayLiteral(R"({"error":"authorization_pending"})");
const QByteArray kGranted  = QByteArrayLiteral(R"({"access_token":"gho_ok","token_type":"bearer","scope":"gist",)"
                                               R"("refresh_token":"ghr_ok"})");

/// What the page was told. Plain connections rather than a QSignalSpy: this target links Qt Core
/// and Network, not Qt Test.
struct Page {
    int     changed   = 0;
    int     completed = 0;
    int     failed    = 0;
    QString error;
};

void record(GithubBackend& backend, Page& page)
{
    QObject::connect(&backend, &GithubBackend::changed, [&page] { ++page.changed; });
    QObject::connect(&backend, &GithubBackend::completed, [&page] { ++page.completed; });
    QObject::connect(&backend, &GithubBackend::failed, [&page](QString const& code) {
        ++page.failed;
        page.error = code;
    });
}

/// A scratch settings document, the client id config beside it, and the backend over both.
struct Fixture {
    QTemporaryDir                  dir;
    KnownStore                     store{KnownStore::load(std::filesystem::path{dir.path().toStdWString()} / L"settings.json")};
    SyncPartition                  partition{store};
    ScriptedManager                manager;
    std::unique_ptr<GithubBackend> backend;

    explicit Fixture(bool withClientId = true)
    {
        if (withClientId) {
            QFile file{dir.filePath(QStringLiteral("sync.json"))};
            file.open(QIODevice::WriteOnly);
            file.write(R"({"clientId":"Iv1.test"})");
        }
        backend = std::make_unique<GithubBackend>(std::filesystem::path{dir.path().toStdWString()}, &manager, partition);
    }
};

} // namespace

TEST(GithubBackend, TheSignInShowsTheCodeAndEndsAtTheLoginName)
{
    QtApplication qt;
    Fixture       fixture;
    Page          page;
    record(*fixture.backend, page);

    fixture.manager.queue(200, kCodeBody);
    fixture.manager.queue(200, kPending);
    fixture.manager.queue(200, kGranted);
    fixture.manager.queue(200, QByteArrayLiteral(R"({"login":"reader"})"));

    fixture.backend->startLogin();
    // Pending is set before the code request goes out; the code itself arrives one loop turn later.
    ASSERT_TRUE(spinUntil([&fixture] { return not fixture.backend->userCode().isEmpty(); }));
    EXPECT_EQ(fixture.backend->userCode(), "WDJB-MJHT");
    EXPECT_EQ(fixture.backend->verificationUri(), "https://github.com/login/device");
    EXPECT_TRUE(page.changed > 0);

    // The reader approves on github.com; the grant stores the pair, the login read names it, and
    // the flow ends. The Finishing phase is one loop turn wide, so the wait is on the end itself.
    ASSERT_TRUE(spinUntil([&page] { return page.completed > 0; }, 8000));
    EXPECT_EQ(fixture.partition.token(), "gho_ok");
    EXPECT_EQ(fixture.partition.refreshToken(), "ghr_ok");
    EXPECT_EQ(fixture.partition.login(), "reader");
    EXPECT_EQ(fixture.backend->signInPhase(), SignInPhase::Idle);
    EXPECT_EQ(fixture.backend->userCode(), "") << "the code's job is done once the tokens arrive";
    EXPECT_EQ(fixture.backend->id(), QStringLiteral("github"));

    // The login read went out with the fresh token as the bearer.
    ASSERT_EQ(fixture.manager.sent.size(), 4u);
    EXPECT_EQ(fixture.manager.sent[3].url.toStdString(), "https://api.github.com/user");
}

TEST(GithubBackend, ABackendWithoutAClientIdDoesNothing)
{
    QtApplication qt;
    Fixture       fixture(false);
    Page          page;
    record(*fixture.backend, page);

    EXPECT_FALSE(fixture.backend->configured());
    fixture.backend->startLogin();

    EXPECT_EQ(fixture.backend->signInPhase(), SignInPhase::Idle);
    EXPECT_EQ(fixture.manager.requests, 0) << "nothing was asked of the network";
    EXPECT_EQ(page.changed + page.failed, 0);
}

TEST(GithubBackend, ACodeRequestThatNeverAnsweredSendsThePageBack)
{
    QtApplication qt;
    Fixture       fixture;
    Page          page;
    record(*fixture.backend, page);

    fixture.manager.queue(302, {}, {}, false);
    fixture.backend->startLogin();
    ASSERT_TRUE(spinUntil([&fixture] { return fixture.backend->signInPhase() == SignInPhase::Pending; }));
    spinUntil([&] {
        for (auto* reply : fixture.manager.findChildren<QNetworkReply*>())
            if (not reply->isFinished())
                reply->abort();
        return page.failed > 0;
    });

    ASSERT_EQ(page.failed, 1);
    EXPECT_EQ(page.error, "proxy");
    EXPECT_EQ(fixture.backend->signInPhase(), SignInPhase::Idle);
    EXPECT_EQ(fixture.backend->userCode(), "");
}

TEST(GithubBackend, TheReaderDecliningSendsThePageBackWithItsWord)
{
    QtApplication qt;
    Fixture       fixture;
    Page          page;
    record(*fixture.backend, page);

    fixture.manager.queue(200, kCodeBody);
    fixture.manager.queue(200, kPending);
    fixture.manager.queue(200, QByteArrayLiteral(R"({"error":"access_denied"})"));
    fixture.backend->startLogin();
    ASSERT_TRUE(spinUntil([&page] { return page.failed > 0; }, 8000));

    ASSERT_EQ(page.failed, 1);
    EXPECT_EQ(page.error, "denied");
    EXPECT_EQ(fixture.backend->signInPhase(), SignInPhase::Idle);
    EXPECT_EQ(fixture.partition.token(), "") << "a declined sign-in stores no tokens";
}

TEST(GithubBackend, AnExpiredCodeEndsTheSignInWhereItStands)
{
    QtApplication qt;
    Fixture       fixture;
    Page          page;
    record(*fixture.backend, page);

    fixture.manager.queue(200, kCodeBody);
    fixture.manager.queue(200, QByteArrayLiteral(R"({"error":"expired_token"})"));
    fixture.backend->startLogin();
    ASSERT_TRUE(spinUntil([&page] { return page.failed > 0; }, 8000));

    ASSERT_EQ(page.failed, 1);
    EXPECT_EQ(page.error, "expired");
    EXPECT_EQ(fixture.backend->signInPhase(), SignInPhase::Idle);
}

TEST(GithubBackend, ALoginTheFreshTokenCannotReadForgetsTheTokens)
{
    QtApplication qt;
    Fixture       fixture;
    Page          page;
    record(*fixture.backend, page);

    // The grant stores the pair and asks for the login; a token that cannot even read that is
    // dead on arrival, so the stored sign-in goes with it. The read goes out in the same turn the
    // grant lands, so its refusal is queued before startLogin with the rest of the flow.
    fixture.manager.queue(200, kCodeBody);
    fixture.manager.queue(200, kPending);
    fixture.manager.queue(200, kGranted);
    fixture.manager.queue(401);
    fixture.backend->startLogin();
    ASSERT_TRUE(spinUntil([&page] { return page.failed > 0; }, 8000));

    ASSERT_EQ(page.failed, 1);
    EXPECT_EQ(page.error, "signInExpired");
    EXPECT_EQ(fixture.partition.token(), "") << "the tokens the flow just bought are forgotten";
    EXPECT_EQ(fixture.backend->signInPhase(), SignInPhase::Idle);
}

TEST(GithubBackend, TheLoginReadFailingOtherWaysCarriesItsOwnKind)
{
    QtApplication qt;
    Fixture       fixture;
    Page          page;
    record(*fixture.backend, page);

    fixture.manager.queue(200, kCodeBody);
    fixture.manager.queue(200, kPending);
    fixture.manager.queue(200, kGranted);
    fixture.manager.queue(404);
    fixture.backend->startLogin();
    ASSERT_TRUE(spinUntil([&page] { return page.failed > 0; }, 8000));

    ASSERT_EQ(page.failed, 1);
    EXPECT_EQ(page.error, "noGist");
    EXPECT_EQ(fixture.backend->signInPhase(), SignInPhase::Idle);
}

TEST(GithubBackend, ACancelPutsThePageBackWithoutAReport)
{
    QtApplication qt;
    Fixture       fixture;
    Page          page;
    record(*fixture.backend, page);

    fixture.manager.queue(302, {}, {}, false);
    fixture.backend->startLogin();
    ASSERT_TRUE(spinUntil([&fixture] { return fixture.backend->signInPhase() == SignInPhase::Pending; }));

    fixture.backend->cancelLogin();
    EXPECT_EQ(fixture.backend->signInPhase(), SignInPhase::Idle);
    EXPECT_EQ(fixture.backend->userCode(), "");
    EXPECT_EQ(fixture.backend->verificationUri(), "");
    EXPECT_EQ(page.failed + page.completed, 0) << "a cancel reports nothing";

    // And the whole sign-in abandoned outside the page's knowledge ends the same way.
    fixture.backend->abortSignIn();
    EXPECT_EQ(fixture.backend->signInPhase(), SignInPhase::Idle);
}
