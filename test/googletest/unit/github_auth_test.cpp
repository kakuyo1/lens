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
#include <QFile>
#include <QString>
#include <QTemporaryDir>

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
    EXPECT_EQ(result.accessToken, "gho_test");
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
