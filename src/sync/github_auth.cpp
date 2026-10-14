/**
 * @file github_auth.cpp
 * @brief The device flow: the code request, the polling loop, and the parsers that read its answers.
 */

#include "github_auth.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <initializer_list>
#include <utility>

#include "util/log.h"

namespace lens::sync {
namespace {

constexpr auto kDeviceCodeUrl   = "https://github.com/login/device/code";
constexpr auto kTokenUrl        = "https://github.com/login/oauth/access_token";
constexpr auto kGrantType       = "urn:ietf:params:oauth:grant-type:device_code";
constexpr auto kScope           = "gist";
constexpr int  kTimeoutMs       = 15000;
constexpr int  kDefaultInterval = 5;
/// GitHub's instruction when it asks for a slower poll: five seconds on top of the interval.
constexpr int kSlowDownStepMs = 5000;

/// @return A form body with every field percent-encoded, as the device endpoints read it.
QByteArray formBody(std::initializer_list<std::pair<QString, QString>> fields)
{
    QUrlQuery query;
    for (const auto& [key, value] : fields)
        query.addQueryItem(key, value);
    return query.toString(QUrl::FullyEncoded).toUtf8();
}

/// @return A request for one of the device endpoints, asking for JSON rather than the form encoding.
QNetworkRequest formRequest(const QUrl& url, const QString& userAgent)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    // GitHub answers these endpoints directly; a redirect would be unexpected, so it is not followed.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    return request;
}

/// @return Whether a failed request had no HTTP answer and no body: the connection itself, which is
///         what a blocked route looks like. Any answer, even an error, is the server's own.
AuthFailure failureFor(int status, const QByteArray& body)
{
    return status == 0 and body.isEmpty() ? AuthFailure::Proxy : AuthFailure::Generic;
}

} // namespace

std::optional<DeviceCode> parseDeviceCode(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (not document.isObject())
        return std::nullopt;

    const QJsonObject root = document.object();
    DeviceCode        code;
    code.deviceCode      = root.value("device_code").toString();
    code.userCode        = root.value("user_code").toString();
    code.verificationUri = root.value("verification_uri").toString();
    code.expiresIn       = root.value("expires_in").toInt();
    code.interval        = root.value("interval").toInt(kDefaultInterval);

    if (code.deviceCode.isEmpty() or code.userCode.isEmpty() or code.expiresIn <= 0)
        return std::nullopt;
    // The address is shown to the reader and typed into; only a secure one is worth showing.
    if (QUrl{code.verificationUri}.scheme() != QLatin1String("https"))
        return std::nullopt;
    if (code.interval <= 0)
        code.interval = kDefaultInterval;
    return code;
}

PollResult parseTokenPoll(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (not document.isObject())
        return {};

    const QJsonObject root = document.object();
    // A token wins over any error text: GitHub never sends both, and a token is the thing wanted.
    const QString token = root.value("access_token").toString();
    if (not token.isEmpty())
        return {PollState::Granted, token};

    const QString error = root.value("error").toString();
    if (error == QLatin1String("authorization_pending"))
        return {PollState::Pending, {}};
    if (error == QLatin1String("slow_down"))
        return {PollState::SlowDown, {}};
    if (error == QLatin1String("expired_token"))
        return {PollState::Expired, {}};
    if (error == QLatin1String("access_denied"))
        return {PollState::Denied, {}};
    return {};
}

GithubAuth::GithubAuth(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      userAgent_(QStringLiteral("Lens/%1").arg(QString::fromUtf8(LENS_VERSION))),
      pollTimer_(new QTimer(this)),
      expiryTimer_(new QTimer(this))
{
    pollTimer_->setSingleShot(true);
    expiryTimer_->setSingleShot(true);
    connect(pollTimer_, &QTimer::timeout, this, &GithubAuth::poll);
    // The code's lifetime is GitHub's, not ours: when it runs out, sign-in is over whatever the
    // last poll said.
    connect(expiryTimer_, &QTimer::timeout, this, [this] {
        LENS_INFO("the device code expired before the reader approved");
        fail(AuthFailure::Expired);
    });
}

void GithubAuth::requestCode(const QString& clientId)
{
    cancel();
    if (clientId.isEmpty()) {
        LENS_WARN("sign-in was asked for without a client id; nothing was requested");
        emit failed(AuthFailure::Generic);
        return;
    }

    clientId_            = clientId;
    QNetworkReply* reply = manager_->post(formRequest(QUrl{QString::fromLatin1(kDeviceCodeUrl)}, userAgent_),
                                          formBody({{QStringLiteral("client_id"), clientId}, {QStringLiteral("scope"), QString::fromLatin1(kScope)}}));
    reply_               = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onCodeFinished(reply); });
    LENS_INFO("requesting a GitHub device code");
}

void GithubAuth::cancel()
{
    stop();
}

void GithubAuth::onCodeFinished(QNetworkReply* reply)
{
    const QByteArray body   = reply->readAll();
    const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    reply_ = nullptr;

    if (status != 200) {
        LENS_WARN("the device code request answered HTTP {}", status);
        fail(failureFor(status, body));
        return;
    }
    const std::optional<DeviceCode> code = parseDeviceCode(body);
    if (not code) {
        LENS_WARN("the device code answer was not usable");
        fail(AuthFailure::Generic);
        return;
    }

    LENS_INFO("device code issued; valid for {} s", code->expiresIn);
    code_       = *code;
    intervalMs_ = code->interval * 1000;
    expiryTimer_->start(code->expiresIn * 1000);
    pollTimer_->start(intervalMs_);
    emit codeReady(*code);
}

void GithubAuth::poll()
{
    QNetworkReply* reply = manager_->post(formRequest(QUrl{QString::fromLatin1(kTokenUrl)}, userAgent_),
                                          formBody({{QStringLiteral("client_id"), clientId_},
                                                    {QStringLiteral("device_code"), code_.deviceCode},
                                                    {QStringLiteral("grant_type"), QString::fromLatin1(kGrantType)}}));
    reply_               = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onPollFinished(reply); });
}

void GithubAuth::onPollFinished(QNetworkReply* reply)
{
    const QByteArray body   = reply->readAll();
    const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    reply_ = nullptr;

    // Only a poll that got no answer at all is a transport failure. GitHub answers pending, slow and
    // denied states as JSON, and those are read below rather than treated as broken.
    if (status == 0 and body.isEmpty()) {
        LENS_WARN("the token poll got no answer");
        fail(AuthFailure::Proxy);
        return;
    }

    const PollResult result = parseTokenPoll(body);
    switch (result.state) {
        case PollState::Pending:
            pollTimer_->start(intervalMs_);
            return;
        case PollState::SlowDown:
            intervalMs_ += kSlowDownStepMs;
            LENS_INFO("GitHub asked for a slower poll; now every {} ms", intervalMs_);
            pollTimer_->start(intervalMs_);
            return;
        case PollState::Granted: {
            // The token leaves this object only through the signal; nothing about it is logged.
            const QString token = result.accessToken;
            stop();
            LENS_INFO("device flow granted");
            emit granted(token);
            return;
        }
        case PollState::Expired:
            LENS_INFO("the device code expired before the reader approved");
            fail(AuthFailure::Expired);
            return;
        case PollState::Denied:
            LENS_INFO("the reader declined the sign-in");
            fail(AuthFailure::Denied);
            return;
        case PollState::Failed:
            LENS_WARN("the token poll answer was not recognised");
            fail(AuthFailure::Generic);
            return;
    }
}

void GithubAuth::stop()
{
    pollTimer_->stop();
    expiryTimer_->stop();
    code_ = DeviceCode{};
    if (reply_ != nullptr) {
        QNetworkReply* reply = reply_;
        reply_               = nullptr;
        // Disconnected before the abort: the finished handler belongs to a poll that has been given up.
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

void GithubAuth::fail(AuthFailure why)
{
    stop();
    emit failed(why);
}

}
