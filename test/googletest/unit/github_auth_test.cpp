/**
 * @file github_auth_test.cpp
 * @brief Offline tests for the sync module's pure parsers: the device-flow answers, the gist body,
 *        the gist list lookup, and the client id config.
 *
 * Every case hands a parser the body GitHub would send, written out inline. No socket is opened and
 * no token is read from anywhere. The flow itself (timers, polling, failures) runs over a network
 * manager and is covered by the client's own tests, not here.
 */

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

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
    // The one answer that ends a sign-in for good: nothing is left to renew with, so this is the
    // reader being sent back through the flow rather than a failure worth retrying.
    EXPECT_EQ(parseTokenPoll(R"({"error":"bad_refresh_token","error_description":"The refresh token is invalid or expired."})").state,
              PollState::BadRefresh);
}

TEST(TokenPoll, TreatsAnyOtherAnswerAsFailed)
{
    EXPECT_EQ(parseTokenPoll(R"({"error":"incorrect_client_credentials"})").state, PollState::Failed);
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
using lens::sync::GithubAuth;
using lens::sync::Token;

/// One QCoreApplication for a case's duration: the client's network objects need one, and Qt allows
/// one at a time, so each case that touches them makes its own.
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

/// What the auth object reported. Plain connections rather than a QSignalSpy: this target links Qt
/// Core and Network, not Qt Test.
struct Reports {
    int         granted   = 0;
    int         refreshed = 0;
    int         failed    = 0;
    Token       token;
    AuthFailure why = AuthFailure::Generic;
};

void record(GithubAuth& auth, Reports& seen)
{
    QObject::connect(&auth, &GithubAuth::granted, [&seen](Token token) {
        ++seen.granted;
        seen.token = token;
    });
    QObject::connect(&auth, &GithubAuth::refreshed, [&seen](Token token) {
        ++seen.refreshed;
        seen.token = token;
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

/// One canned answer, finishing on the next turn of the loop so the client's own connections exist
/// before `finished` arrives. `delivered_` keeps it from finishing twice.
class CannedReply final : public QNetworkReply {
public:
    CannedReply(QObject* parent, int status, QByteArray body)
        : QNetworkReply(parent),
          body_(std::move(body))
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] { deliver(); });
    }

    void abort() override
    {
        deliver();
    }

    qint64 readData(char* data, qint64 max) override
    {
        if (body_.isEmpty())
            return -1;
        const qint64 n = std::min<qint64>(max, body_.size());
        std::memcpy(data, body_.constData(), static_cast<std::size_t>(n));
        body_.remove(0, static_cast<int>(n));
        return n;
    }

    /// Finish once, whichever of the two paths arrives first.
    void deliver()
    {
        if (delivered_)
            return;
        delivered_ = true;
        setFinished(true);
        emit finished();
    }

private:
    QByteArray body_;
    bool       delivered_ = false;
};

/// The seam GithubAuth takes, keeping what the last request was so a case can read it back.
class CannedManager final : public QNetworkAccessManager {
public:
    CannedManager(int status, QByteArray body)
        : status_(status),
          body_(std::move(body))
    {
    }

    QString    url;    ///< The address the last request went to.
    QByteArray posted; ///< The form body of the last POST.

protected:
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice* outgoing) override
    {
        url = request.url().toString();
        if (outgoing != nullptr) {
            if (not outgoing->isOpen())
                outgoing->open(QIODevice::ReadOnly);
            posted = outgoing->readAll();
        }
        return new CannedReply(this, status_, body_);
    }

private:
    int        status_;
    QByteArray body_;
};

} // namespace

TEST(GithubRefresh, SpendsTheStoredRefreshTokenAndReportsTheRotatedPair)
{
    QtApplication qt;
    CannedManager manager{200, R"({"access_token":"gho_next","token_type":"bearer","scope":"gist","expires_in":28800,)"
                               R"("refresh_token":"ghr_after","refresh_token_expires_in":15552000})"};
    GithubAuth    auth{&manager};
    Reports       seen;
    record(auth, seen);

    auth.refresh("Ov23li0i58xw4shNcF7I", "ghr_before");
    ASSERT_TRUE(spinUntil([&seen] { return seen.refreshed + seen.failed > 0; }));

    ASSERT_EQ(seen.refreshed, 1);
    EXPECT_EQ(seen.token.access, "gho_next");
    // Both tokens come back new: GitHub rotates the pair, and the spent one stops working.
    EXPECT_EQ(seen.token.refresh, "ghr_after");
    // The request is the one the endpoint documents: the same address the device flow polls, with
    // the refresh grant rather than the device grant.
    EXPECT_EQ(manager.url, QStringLiteral("https://github.com/login/oauth/access_token"));
    EXPECT_TRUE(manager.posted.contains("grant_type=refresh_token"));
    EXPECT_TRUE(manager.posted.contains("refresh_token=ghr_before"));
    EXPECT_TRUE(manager.posted.contains("client_id=Ov23li0i58xw4shNcF7I"));
}

TEST(GithubRefresh, ARefusedRefreshTokenEndsTheSignInRatherThanRetrying)
{
    // GitHub answers this one with HTTP 200 and an error body, the way it answers a poll.
    QtApplication qt;
    CannedManager manager{200, R"({"error":"bad_refresh_token","error_description":"The refresh token is invalid or expired."})"};
    GithubAuth    auth{&manager};
    Reports       seen;
    record(auth, seen);

    auth.refresh("Ov23li0i58xw4shNcF7I", "ghr_dead");
    ASSERT_TRUE(spinUntil([&seen] { return seen.refreshed + seen.failed > 0; }));

    EXPECT_EQ(seen.refreshed, 0);
    ASSERT_EQ(seen.failed, 1);
    // Its own kind, because it is the only answer that says "sign in again" rather than "try again".
    EXPECT_EQ(seen.why, AuthFailure::BadRefresh);
}
