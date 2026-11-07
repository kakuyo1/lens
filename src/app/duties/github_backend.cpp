/**
 * @file github_backend.cpp
 * @brief The GitHub sign-in flow and the transport's clients, for github_backend.h.
 */

#include "app/duties/github_backend.h"

namespace lens::app {

namespace {

QString errorForGist(lens::sync::GistFailure why)
{
    switch (why) {
        // 401 from api.github.com: the access token is no longer accepted. The device code that
        // bought it is long gone, so what expired is the sign-in, not the code -- a different thing
        // to tell the reader and a different thing to ask them to do about it.
        case lens::sync::GistFailure::Unauthorised: return QStringLiteral("signInExpired");
        case lens::sync::GistFailure::NotFound: return QStringLiteral("noGist");
        case lens::sync::GistFailure::Proxy: return QStringLiteral("proxy");
        case lens::sync::GistFailure::Generic: break;
    }
    return QStringLiteral("generic");
}

QString errorForAuth(lens::sync::AuthFailure why)
{
    switch (why) {
        case lens::sync::AuthFailure::Expired: return QStringLiteral("expired");
        case lens::sync::AuthFailure::Denied: return QStringLiteral("denied");
        case lens::sync::AuthFailure::Proxy: return QStringLiteral("proxy");
        // The refresh token GitHub was handed is itself refused: nothing is left to renew with.
        case lens::sync::AuthFailure::BadRefresh: return QStringLiteral("signInExpired");
        case lens::sync::AuthFailure::Generic: break;
    }
    return QStringLiteral("generic");
}

}

GithubBackend::GithubBackend(const std::filesystem::path& dataDir, QNetworkAccessManager* manager, core::sync::SyncPartition& partition, QObject* parent)
    : SyncBackend(parent),
      config_(lens::sync::loadSyncConfig(dataDir / "sync.json")),
      auth_(manager, this),
      gist_(manager, this),
      transport_(gist_, auth_, config_.clientId, this),
      partition_(partition)
{
    connect(&auth_, &lens::sync::GithubAuth::codeReady, this, [this](lens::sync::DeviceCode code) {
        if (phase_ != Phase::CodePending)
            return;
        userCode_        = code.userCode;
        verificationUri_ = code.verificationUri;
        emit changed();
    });
    connect(&auth_, &lens::sync::GithubAuth::granted, this, [this](lens::sync::Token token) {
        if (phase_ != Phase::CodePending)
            return;
        partition_.storeTokens(token.access.toStdString(), token.refresh.toStdString());
        userCode_.clear();
        verificationUri_.clear();
        phase_ = Phase::Naming;
        emit changed();
        gist_.getUser(token.access);
    });
    connect(&auth_, &lens::sync::GithubAuth::failed, this, [this](lens::sync::AuthFailure why) {
        // A refresh's failure is the round's port's to report; this handler is the sign-in's.
        if (phase_ != Phase::CodePending)
            return;
        userCode_.clear();
        verificationUri_.clear();
        phase_ = Phase::Idle;
        // A refused refresh token is proof the stored sign-in is dead: forget it, so the page offers
        // the sign-in button instead of a Sync now that cannot work.
        if (why == lens::sync::AuthFailure::BadRefresh)
            partition_.forgetTokens();
        emit failed(errorForAuth(why));
    });

    connect(&gist_, &lens::sync::GistClient::gotUser, this, [this](QString loginName) {
        if (phase_ == Phase::Naming)
            onNamed(loginName);
    });
    connect(&gist_, &lens::sync::GistClient::failed, this, [this](lens::sync::GistFailure why) {
        if (phase_ == Phase::Naming)
            onLoginGistFailed(why);
    });
}

QString GithubBackend::id() const
{
    return QStringLiteral("github");
}

bool GithubBackend::configured() const
{
    return !config_.clientId.isEmpty();
}

core::sync::Transport& GithubBackend::transport()
{
    return transport_;
}

SignInPhase GithubBackend::signInPhase() const
{
    switch (phase_) {
        case Phase::CodePending: return SignInPhase::Pending;
        case Phase::Naming: return SignInPhase::Finishing;
        case Phase::Idle: break;
    }
    return SignInPhase::Idle;
}

QString GithubBackend::userCode() const
{
    return userCode_;
}

QString GithubBackend::verificationUri() const
{
    return verificationUri_;
}

void GithubBackend::startLogin()
{
    if (!configured() || phase_ != Phase::Idle)
        return;
    phase_ = Phase::CodePending;
    emit changed();
    auth_.requestCode(config_.clientId);
}

void GithubBackend::cancelLogin()
{
    if (phase_ != Phase::CodePending)
        return;
    auth_.cancel();
    phase_ = Phase::Idle;
    userCode_.clear();
    verificationUri_.clear();
    emit changed();
}

void GithubBackend::abortSignIn()
{
    auth_.cancel();
    phase_ = Phase::Idle;
    userCode_.clear();
    verificationUri_.clear();
}

void GithubBackend::onNamed(const QString& loginName)
{
    partition_.setLogin(loginName.toStdString());
    phase_ = Phase::Idle;
    emit completed();
}

void GithubBackend::onLoginGistFailed(lens::sync::GistFailure why)
{
    phase_ = Phase::Idle;
    // The token a sign-in just bought is not even enough to read the login name: it is dead.
    if (why == lens::sync::GistFailure::Unauthorised)
        partition_.forgetTokens();
    emit failed(errorForGist(why));
}

}
