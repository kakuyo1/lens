/**
 * @file github_auth_test.cpp
 * @brief Offline tests for the sync module's pure parsers: the device-flow answers, the gist body,
 *        the gist list lookup, and the client id config -- and the flow itself, over the scripted
 *        network: the code request, the polling loop with its slow-down and its expiry, the cancel,
 *        and the refresh's own failures.
 *
 * Every case hands the object the body GitHub would send, written out inline. No socket is opened
 * and no token is read from anywhere. The gist client's own requests live in gist_client_test.cpp.
 */

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <utility>

#include "scripted_manager.h"
#include "sync/github_auth.h"
#include "sync/gist_client.h"
#include "sync/sync_config.h"

using lens::sync::PollState;
using lens::sync::findGistId;
using lens::sync::loadSyncConfig;
using lens::sync::parseDeviceCode;
using lens::sync::parseGist;
using lens::sync::parseTokenPoll;

TEST(DeviceCode, ReadsTheCodesAndTheIntervalGitHubSends)
{
    const auto code = parseDeviceCode(R"({"device_code":"3584d83530557fdd1f46af8289938c8ef79f9dc5",)"
                                      R"("user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device",)"
                                      R"("expires_in":900,"interval":5})");
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(code->deviceCode, "3584d83530557fdd1f46af8289938c8ef79f9dc5");
    EXPECT_EQ(code->userCode, "WDJB-MJHT");
    EXPECT_EQ(code->verificationUri, "https://github.com/login/device");
    EXPECT_EQ(code->expiresIn, 900);
    EXPECT_EQ(code->interval, 5);
}

TEST(DeviceCode, DefaultsTheIntervalWhenGitHubOmitsIt)
{
    const auto code = parseDeviceCode(R"({"device_code":"d","user_code":"U-1","verification_uri":"https://github.com/login/device","expires_in":900})");
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(code->interval, 5);
}

TEST(DeviceCode, ResetsAnIntervalThatIsNotASecondsCount)
{
    // A zero or a negative one is GitHub answering a field it did not mean to send; the default
    // five seconds is what a poll runs at until GitHub names a real one.
    const auto code = parseDeviceCode(R"({"device_code":"d","user_code":"U-1","verification_uri":"https://github.com/login/device","expires_in":900,"interval":0})");
    ASSERT_TRUE(code.has_value());
    EXPECT_EQ(code->interval, 5);
}

TEST(DeviceCode, RefusesAnAnswerMissingItsCodesOrItsLifetime)
{
    EXPECT_FALSE(parseDeviceCode(R"({"user_code":"U","verification_uri":"https://github.com/login/device","expires_in":900})").has_value());
    EXPECT_FALSE(parseDeviceCode(R"({"device_code":"d","verification_uri":"https://github.com/login/device","expires_in":900})").has_value());
    EXPECT_FALSE(parseDeviceCode(R"({"device_code":"d","user_code":"U","verification_uri":"https://github.com/login/device"})").has_value());
    EXPECT_FALSE(parseDeviceCode("not json").has_value());
}

TEST(DeviceCode, RefusesAVerificationAddressThatIsNotSecure)
{
    // The address is typed into by the reader; a plain-http one is not shown.
    EXPECT_FALSE(parseDeviceCode(R"({"device_code":"d","user_code":"U","verification_uri":"http://github.com/login/device","expires_in":900})").has_value());
}

TEST(TokenPoll, ReadsEachStateGitHubReportsWhileWaiting)
{
    EXPECT_EQ(parseTokenPoll(R"({"error":"authorization_pending"})").state, PollState::Pending);
    EXPECT_EQ(parseTokenPoll(R"({"error":"slow_down","interval":10})").state, PollState::SlowDown);
    EXPECT_EQ(parseTokenPoll(R"({"error":"expired_token"})").state, PollState::Expired);
    EXPECT_EQ(parseTokenPoll(R"({"error":"access_denied"})").state, PollState::Denied);
}

TEST(TokenPoll, GrantsWithTheTokenItCarries)
{
    const auto result = parseTokenPoll(R"({"access_token":"gho_test","token_type":"bearer","scope":"gist"})");
    EXPECT_EQ(result.state, PollState::Granted);
    EXPECT_EQ(result.token.access, "gho_test");
    // An app whose user tokens do not expire sends neither of these, and nothing may then look
    // renewable: an empty refresh token is what says "sign in again" rather than "renew".
    EXPECT_TRUE(result.token.refresh.isEmpty());
    EXPECT_EQ(result.token.expiresIn, 0);
}

TEST(TokenPoll, ReadsTheRefreshTokenAnExpiringAppSends)
{
    // What GitHub answers for an app with expiring user tokens: the access token lives eight hours,
    // and a refresh token, valid six months unused, comes with it. Both numbers are the body's own.
    const auto result = parseTokenPoll(R"({"access_token":"gho_new","token_type":"bearer","scope":"gist",)"
                                       R"("expires_in":28800,"refresh_token":"ghr_next","refresh_token_expires_in":15552000})");
    EXPECT_EQ(result.state, PollState::Granted);
    EXPECT_EQ(result.token.access, "gho_new");
    EXPECT_EQ(result.token.refresh, "ghr_next");
    EXPECT_EQ(result.token.expiresIn, 28800);
    EXPECT_EQ(result.token.refreshExpiresIn, 15552000);
}

TEST(TokenPoll, KnowsARefusedRefreshTokenFromAnyOtherFailure)
{
    // The answers that end a sign-in for good: nothing is left to renew with, so this is the reader
    // being sent back through the flow rather than a failure worth retrying.
    EXPECT_EQ(parseTokenPoll(R"({"error":"bad_refresh_token","error_description":"The refresh token is invalid or expired."})").state,
              PollState::BadRefresh);
    // The app's own credentials, which an OAuth App needs before it may spend a refresh token at all.
    // A desktop application has nowhere to keep a client secret, so every refresh answers this and
    // the sign-in is as dead as a spent token. It read as "failed" until 2026-10-26, which left the
    // page offering a Sync now that could never work.
    const auto refused = parseTokenPoll(R"({"error":"incorrect_client_credentials","error_description":"The client_id and/or client_secret passed are incorrect."})");
    EXPECT_EQ(refused.state, PollState::BadRefresh);
}

TEST(TokenPoll, TreatsAnyOtherAnswerAsFailed)
{
    EXPECT_EQ(parseTokenPoll(R"({"error":"unsupported_grant_type"})").state, PollState::Failed);
    EXPECT_EQ(parseTokenPoll("<html>gateway</html>").state, PollState::Failed);
    EXPECT_EQ(parseTokenPoll("").state, PollState::Failed);
}

TEST(GistParse, ReadsTheFilesAndTheirTruncationFlag)
{
    const auto gist = parseGist(R"({"id":"abc123","description":"lens-sync","updated_at":"2026-10-14T10:00:00Z",)"
                                R"("files":{"lens-sync.json":{"content":"{}","truncated":false,"raw_url":"https://gist.githubusercontent.com/x/raw"},)"
                                R"("big.json":{"content":"{\"a\":","truncated":true,"raw_url":"https://gist.githubusercontent.com/x/raw/big"}}})");
    ASSERT_TRUE(gist.has_value());
    EXPECT_EQ(gist->id, "abc123");
    EXPECT_EQ(gist->description, "lens-sync");
    EXPECT_EQ(gist->updatedAt, "2026-10-14T10:00:00Z");
    ASSERT_EQ(gist->files.size(), 2);
    EXPECT_FALSE(gist->files.value("lens-sync.json").truncated);
    EXPECT_EQ(gist->files.value("lens-sync.json").content, "{}");
    EXPECT_TRUE(gist->files.value("big.json").truncated);
    EXPECT_EQ(gist->files.value("big.json").rawUrl.host(), "gist.githubusercontent.com");
}

TEST(GistParse, RefusesABodyWithoutAnId)
{
    EXPECT_FALSE(parseGist(R"({"description":"lens-sync","files":{}})").has_value());
    EXPECT_FALSE(parseGist("[]").has_value());
}

TEST(FindGistId, PicksTheNewestGistWithTheExactDescription)
{
    const QByteArray list = R"([
        {"id":"old","description":"lens-sync","updated_at":"2026-10-01T00:00:00Z"},
        {"id":"other","description":"notes","updated_at":"2026-10-20T00:00:00Z"},
        {"id":"new","description":"lens-sync","updated_at":"2026-10-14T00:00:00Z"},
        {"id":"near","description":"lens-sync-old","updated_at":"2026-10-30T00:00:00Z"}
    ])";
    const auto       id   = findGistId(list, "lens-sync");
    ASSERT_TRUE(id.has_value());
    EXPECT_EQ(*id, "new");
}

TEST(FindGistId, FindsNothingWhenNoGistCarriesTheDescription)
{
    EXPECT_FALSE(findGistId(R"([{"id":"x","description":"notes","updated_at":"2026-10-01T00:00:00Z"}])", "lens-sync").has_value());
    EXPECT_FALSE(findGistId("[]", "lens-sync").has_value());
    EXPECT_FALSE(findGistId("not a list", "lens-sync").has_value());
}

TEST(SyncConfig, ReadsTheClientIdAndTrimsIt)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("sync.json");
    QFile         file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(R"({"clientId":"  Iv1.abc123  "})");
    file.close();

    EXPECT_EQ(loadSyncConfig(path.toStdString()).clientId, "Iv1.abc123");
}

TEST(SyncConfig, ReadsAsNotConfiguredWhenTheIdIsEmptyOrMissing)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("sync.json");
    QFile         file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(R"({"_comment":"no app yet","clientId":""})");
    file.close();

    EXPECT_TRUE(loadSyncConfig(path.toStdString()).clientId.isEmpty());
    EXPECT_TRUE(loadSyncConfig((dir.path() + "/absent.json").toStdString()).clientId.isEmpty());
}

namespace {

using lens::sync::AuthFailure;
using lens::sync::DeviceCode;
using lens::sync::GithubAuth;
using lens::sync::Token;
using lens::test::ScriptedManager;

/// One QCoreApplication for a case's duration: the auth object's network objects need one, and Qt
/// allows one at a time, so each case that touches them makes its own.
struct QtApplication {
    QtApplication()
        : application(argc, argv)
    {
    }

    int              argc     = 1;
    char             name[20] = "github_auth_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application;
};

/// What the object reported. Plain connections rather than a QSignalSpy: this target links Qt Core
/// and Network, not Qt Test.
struct Reports {
    int         codeReady     = 0;
    int         granted       = 0;
    int         refreshed     = 0;
    int         failed        = 0;
    int         refreshFailed = 0;
    DeviceCode  code;
    Token       token;
    quint64     refreshCallId = 0;
    AuthFailure why           = AuthFailure::Generic;
};

void record(GithubAuth& auth, Reports& seen)
{
    QObject::connect(&auth, &GithubAuth::codeReady, [&seen](DeviceCode code) {
        ++seen.codeReady;
        seen.code = code;
    });
    QObject::connect(&auth, &GithubAuth::granted, [&seen](Token token) {
        ++seen.granted;
        seen.token = token;
    });
    QObject::connect(&auth, &GithubAuth::refreshed, [&seen](quint64 callId, Token token) {
        ++seen.refreshed;
        seen.refreshCallId = callId;
        seen.token         = token;
    });
    QObject::connect(&auth, &GithubAuth::refreshFailed, [&seen](quint64 callId, AuthFailure why) {
        ++seen.refreshFailed;
        seen.refreshCallId = callId;
        seen.why           = why;
    });
    QObject::connect(&auth, &GithubAuth::failed, [&seen](AuthFailure why) {
        ++seen.failed;
        seen.why = why;
    });
}

/// @return Whether @p done came true within @p ms, turning the loop so a reply can arrive.
bool spinUntil(const std::function<bool()>& done, int ms = 2000)
{
    QElapsedTimer clock;
    clock.start();
    while (not done() and clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

/// @brief Abort every reply the manager is still holding, as a connection that never got a byte does.
void dropTheConnection(ScriptedManager& manager)
{
    for (auto* reply : manager.findChildren<QNetworkReply*>())
        if (not reply->isFinished())
            reply->abort();
}

/// The device-code answer the flow starts from: a one-second poll, so a case runs its waits in
/// seconds rather than the production five.
const QByteArray kCodeBody = R"({"device_code":"d","user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device",)"
                             R"("expires_in":900,"interval":1})";

/// A code whose lifetime GitHub has already cut to one second: the expiry timer is what ends it.
const QByteArray kDyingCodeBody = R"({"device_code":"d","user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device",)"
                                  R"("expires_in":1,"interval":900})";

const QByteArray kPending = QByteArrayLiteral(R"({"error":"authorization_pending"})");
const QByteArray kGranted = QByteArrayLiteral(R"({"access_token":"gho_ok","token_type":"bearer","scope":"gist"})");

} // namespace

TEST(GithubFlow, ASignInAsksForTheCodeAndShowsIt)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, kCodeBody};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    ASSERT_TRUE(spinUntil([&seen] { return seen.codeReady > 0; }));

    ASSERT_EQ(seen.codeReady, 1);
    EXPECT_EQ(seen.code.deviceCode, "d");
    EXPECT_EQ(seen.code.userCode, "WDJB-MJHT");
    EXPECT_EQ(seen.code.verificationUri, "https://github.com/login/device");
    // The one request went to the device endpoint with the app's id and the one scope it needs.
    ASSERT_EQ(manager.sent.size(), 1u);
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://github.com/login/device/code");
    EXPECT_TRUE(manager.sent[0].body.contains("client_id=Iv1.test"));
    EXPECT_TRUE(manager.sent[0].body.contains("scope=gist"));
    EXPECT_EQ(seen.failed, 0);
}

TEST(GithubFlow, ASignInWithoutAClientIdFailsWhereItStands)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, kCodeBody};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.requestCode(QString{});
    ASSERT_TRUE(spinUntil([&seen] { return seen.failed > 0; }));

    EXPECT_EQ(seen.why, AuthFailure::Generic);
    EXPECT_EQ(manager.requests, 0) << "nothing was asked of the network";
    EXPECT_EQ(seen.codeReady, 0);
}

TEST(GithubFlow, ARefusedCodeRequestEndsTheSignIn)
{
    QtApplication   qt;
    ScriptedManager manager{500, {}, true, QByteArray("server says no")};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    ASSERT_TRUE(spinUntil([&seen] { return seen.failed > 0; }));

    ASSERT_EQ(seen.failed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Generic);
    EXPECT_EQ(seen.codeReady, 0);
}

TEST(GithubFlow, ACodeRequestThatNeverAnsweredIsAProxyFailure)
{
    // No answer at all is the connection, which is what a blocked route looks like; any answer,
    // even an error, would be the server's own.
    QtApplication   qt;
    ScriptedManager manager{500, {}, false};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    dropTheConnection(manager);
    ASSERT_TRUE(spinUntil([&seen] { return seen.failed > 0; }));

    ASSERT_EQ(seen.failed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Proxy);
}

TEST(GithubFlow, AnUnusableCodeAnswerEndsTheSignIn)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArray("not json")};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    ASSERT_TRUE(spinUntil([&seen] { return seen.failed > 0; }));

    ASSERT_EQ(seen.failed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Generic);
}

TEST(GithubFlow, ThePollRunsUntilTheReaderApproves)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    manager.queue(200, kCodeBody); // the code request
    manager.queue(200, kPending);  // the first poll: the reader is still typing
    manager.queue(200, kPending);  // the second poll: still typing
    manager.queue(200, kGranted);  // the third: approved
    GithubAuth auth{&manager};
    Reports    seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    ASSERT_TRUE(spinUntil([&seen] { return seen.granted > 0; }, 8000));

    ASSERT_EQ(seen.granted, 1);
    EXPECT_EQ(seen.token.access, "gho_ok");
    // One code request, two pending polls and the granted one -- and nothing further: the timers
    // stopped when the token arrived.
    ASSERT_EQ(manager.sent.size(), 4u);
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://github.com/login/device/code");
    EXPECT_EQ(manager.sent[1].url.toStdString(), "https://github.com/login/oauth/access_token");
    EXPECT_TRUE(manager.sent[1].body.contains("device_code=d"));
    EXPECT_TRUE(manager.sent[1].body.contains("client_id=Iv1.test"));
    EXPECT_TRUE(manager.sent[1].body.contains("grant_type"));
    QElapsedTimer quiet;
    quiet.start();
    while (quiet.elapsed() < 1500)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_EQ(manager.sent.size(), 4u) << "a granted flow polls no more";
    EXPECT_EQ(seen.failed, 0);
}

TEST(GithubFlow, GitHubSlowingThePollDownDoesNotStopIt)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    manager.queue(200, kCodeBody);                                     // the code request
    manager.queue(200, kPending);                                      // poll one
    manager.queue(200, QByteArrayLiteral(R"({"error":"slow_down"})")); // poll two: back off
    manager.queue(200, kGranted);                                      // poll three, after the wait
    GithubAuth auth{&manager};
    Reports    seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    // The slow-down adds five seconds to the one-second interval, so the granted poll is a few
    // seconds out: this case carries the flow's real pacing, and it is the unit suite's slowest.
    ASSERT_TRUE(spinUntil([&seen] { return seen.granted > 0; }, 12000));

    ASSERT_EQ(seen.granted, 1);
    EXPECT_EQ(manager.sent.size(), 4u);
}

TEST(GithubFlow, TheReaderDecliningEndsTheSignIn)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    manager.queue(200, kCodeBody);
    manager.queue(200, QByteArrayLiteral(R"({"error":"access_denied"})"));
    GithubAuth auth{&manager};
    Reports    seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    ASSERT_TRUE(spinUntil([&seen] { return seen.failed > 0; }, 8000));

    ASSERT_EQ(seen.failed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Denied);
    EXPECT_EQ(seen.granted, 0);
}

TEST(GithubFlow, AnExpiredCodeEndsTheSignIn)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    manager.queue(200, kCodeBody);
    manager.queue(200, QByteArrayLiteral(R"({"error":"expired_token"})"));
    GithubAuth auth{&manager};
    Reports    seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    ASSERT_TRUE(spinUntil([&seen] { return seen.failed > 0; }, 8000));

    ASSERT_EQ(seen.failed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Expired);
}

TEST(GithubFlow, TheCodeRunningOutEndsTheSignInToo)
{
    // The code's lifetime is GitHub's, not ours: when the timer runs out, sign-in is over whatever
    // the last poll said. The interval is far longer than the lifetime here, so the timer is what
    // fires, and no poll is ever sent.
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, kDyingCodeBody};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    ASSERT_TRUE(spinUntil([&seen] { return seen.failed > 0; }, 6000));

    ASSERT_EQ(seen.failed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Expired);
    EXPECT_EQ(manager.requests, 1) << "the expiry is the timer's own, not a poll's";
}

TEST(GithubFlow, AnAnswerThePollDoesNotRecogniseFails)
{
    QtApplication   qt;
    ScriptedManager manager{500};
    manager.queue(200, kCodeBody);
    manager.queue(200, QByteArrayLiteral(R"({"error":"unsupported_grant_type"})"));
    GithubAuth auth{&manager};
    Reports    seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    ASSERT_TRUE(spinUntil([&seen] { return seen.failed > 0; }, 8000));

    ASSERT_EQ(seen.failed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Generic);
}

TEST(GithubFlow, APollThatNeverAnsweredIsAProxyFailure)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, kCodeBody}; // the code request answers
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    ASSERT_TRUE(spinUntil([&seen] { return seen.codeReady > 0; }));
    // The poll goes out on its own timer a second later; its answer is queued now, ahead of it.
    manager.queue(302, {}, {}, false); // a connection that never speaks
    ASSERT_TRUE(spinUntil([&manager] { return manager.requests == 2; }));
    dropTheConnection(manager);
    ASSERT_TRUE(spinUntil([&seen] { return seen.failed > 0; }));

    ASSERT_EQ(seen.failed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Proxy);
}

TEST(GithubFlow, ACancelStopsTheFlowQuietly)
{
    // The reply never answers, and the reader backs out: stop() drops the connection before it
    // aborts it, so the abandoned poll reports nothing, and neither does the object.
    QtApplication   qt;
    ScriptedManager manager{500, {}, false};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.requestCode(QStringLiteral("Iv1.test"));
    auth.cancel();

    QElapsedTimer quiet;
    quiet.start();
    while (quiet.elapsed() < 800)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    EXPECT_EQ(seen.failed + seen.granted + seen.codeReady, 0) << "a cancel reports nothing";
    EXPECT_EQ(manager.sent.size(), 1u);
}

TEST(GithubRefresh, SpendsTheStoredRefreshTokenAndReportsTheRotatedPair)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArrayLiteral(R"({"access_token":"gho_next","token_type":"bearer","scope":"gist","expires_in":28800,)"
                                                             R"("refresh_token":"ghr_after","refresh_token_expires_in":15552000})")};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    const quint64 callId = auth.refresh("Ov23li0i58xw4shNcF7I", "ghr_before");
    ASSERT_TRUE(spinUntil([&seen] { return seen.refreshed + seen.refreshFailed > 0; }));

    ASSERT_EQ(seen.refreshed, 1);
    EXPECT_EQ(seen.refreshCallId, callId);
    EXPECT_EQ(seen.token.access, "gho_next");
    // Both tokens come back new: GitHub rotates the pair, and the spent one stops working.
    EXPECT_EQ(seen.token.refresh, "ghr_after");
    // The request is the one the endpoint documents: the same address the device flow polls, with
    // the refresh grant rather than the device grant.
    ASSERT_EQ(manager.sent.size(), 1u);
    EXPECT_EQ(manager.sent[0].url.toStdString(), "https://github.com/login/oauth/access_token");
    EXPECT_TRUE(manager.sent[0].body.contains("grant_type=refresh_token"));
    EXPECT_TRUE(manager.sent[0].body.contains("refresh_token=ghr_before"));
    EXPECT_TRUE(manager.sent[0].body.contains("client_id=Ov23li0i58xw4shNcF7I"));
}

TEST(GithubRefresh, ARefusedRefreshTokenEndsTheSignInRatherThanRetrying)
{
    // GitHub answers this one with HTTP 200 and an error body, the way it answers a poll.
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArrayLiteral(R"({"error":"bad_refresh_token","error_description":"The refresh token is invalid or expired."})")};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.refresh("Ov23li0i58xw4shNcF7I", "ghr_dead");
    ASSERT_TRUE(spinUntil([&seen] { return seen.refreshed + seen.refreshFailed > 0; }));

    EXPECT_EQ(seen.refreshed, 0);
    ASSERT_EQ(seen.refreshFailed, 1);
    // Its own kind, because it is the only answer that says "sign in again" rather than "try again".
    EXPECT_EQ(seen.why, AuthFailure::BadRefresh);
}

/// The other refusal that ends a sign-in: an OAuth App cannot spend a refresh token without its
/// client secret, and a desktop application has nowhere to keep one. GitHub names the app's
/// credentials rather than the token, and the answer has to land in the same place -- a reader sent
/// back through the flow, not a Sync now that fails the same way every time.
TEST(GithubRefresh, ACredentialRefusalEndsTheSignInToo)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, QByteArrayLiteral(R"({"error":"incorrect_client_credentials","error_description":"The client_id and/or client_secret passed are incorrect."})")};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.refresh("Ov23li0i58xw4shNcF7I", "ghr_old");
    ASSERT_TRUE(spinUntil([&seen] { return seen.refreshed + seen.refreshFailed > 0; }));

    EXPECT_EQ(seen.refreshed, 0);
    ASSERT_EQ(seen.refreshFailed, 1);
    EXPECT_EQ(seen.why, AuthFailure::BadRefresh);
}

TEST(GithubRefresh, ARefreshWithoutItsIdsFailsWhereItStands)
{
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, kGranted};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    const quint64 callId = auth.refresh(QString{}, QString{});
    ASSERT_TRUE(spinUntil([&seen] { return seen.refreshFailed > 0; }));

    ASSERT_EQ(seen.refreshFailed, 1);
    EXPECT_EQ(seen.refreshCallId, callId);
    EXPECT_EQ(seen.why, AuthFailure::Generic);
    EXPECT_EQ(manager.requests, 0) << "the ids were checked before anything was asked";
}

TEST(GithubRefresh, ARefreshThatNeverAnsweredIsAProxyFailure)
{
    QtApplication   qt;
    ScriptedManager manager{500, {}, false};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.refresh("Ov23li0i58xw4shNcF7I", "ghr_before");
    dropTheConnection(manager);
    ASSERT_TRUE(spinUntil([&seen] { return seen.refreshed + seen.refreshFailed > 0; }));

    ASSERT_EQ(seen.refreshFailed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Proxy);
}

TEST(GithubRefresh, AnAnswerTheRefreshDoesNotRecogniseFails)
{
    // Pending, slow_down and the device-code states do not belong to this endpoint; the answer is
    // read as the refusal to retry it is, with the kind the caller acts on.
    QtApplication   qt;
    ScriptedManager manager{200, {}, true, kPending};
    GithubAuth      auth{&manager};
    Reports         seen;
    record(auth, seen);

    auth.refresh("Ov23li0i58xw4shNcF7I", "ghr_before");
    ASSERT_TRUE(spinUntil([&seen] { return seen.refreshed + seen.refreshFailed > 0; }));

    ASSERT_EQ(seen.refreshFailed, 1);
    EXPECT_EQ(seen.why, AuthFailure::Generic);
    EXPECT_EQ(seen.refreshed, 0);
}
