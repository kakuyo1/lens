/**
 * @file webdav_backend.cpp
 * @brief The WebDAV credentials and the reachability check, for webdav_backend.h.
 */

#include "app/duties/webdav_backend.h"

namespace lens::app {

namespace {

QString errorForDav(lens::sync::WebDavFailure why)
{
    switch (why) {
        // The server saw the request and refused the credentials; the form is what to fix.
        case lens::sync::WebDavFailure::Unauthorised: return QStringLiteral("signInExpired");
        case lens::sync::WebDavFailure::Proxy: return QStringLiteral("proxy");
        case lens::sync::WebDavFailure::NotFound: break;
        case lens::sync::WebDavFailure::Generic: break;
    }
    return QStringLiteral("generic");
}

}

WebdavBackend::WebdavBackend(core::sync::SyncPartition& partition, QNetworkAccessManager* manager, QObject* parent)
    : SyncBackend(parent),
      dav_(manager, this),
      transport_(dav_, partition, this),
      partition_(partition)
{
    connect(&dav_, &lens::sync::WebDavClient::found, this, [this](bool exists) {
        // A round's List probe is the transport's to answer; only this backend's own verify reacts.
        if (phase_ != Phase::Verifying)
            return;
        onProbed(exists);
    });
    connect(&dav_, &lens::sync::WebDavClient::failed, this, [this](lens::sync::WebDavFailure why) {
        if (phase_ != Phase::Verifying)
            return;
        onVerifyFailed(why);
    });
}

QString WebdavBackend::id() const
{
    return QStringLiteral("webdav");
}

bool WebdavBackend::configured() const
{
    return signedIn();
}

bool WebdavBackend::signedIn() const
{
    // Saving the three credentials is the sign-in; there is no token to come back with.
    return not partition_.server().empty() and not partition_.username().empty() and not partition_.appPassword().empty();
}

core::sync::Transport& WebdavBackend::transport()
{
    return transport_;
}

SignInPhase WebdavBackend::signInPhase() const
{
    return phase_ == Phase::Verifying ? SignInPhase::Finishing : SignInPhase::Idle;
}

QString WebdavBackend::userCode() const
{
    return QString();
}

QString WebdavBackend::verificationUri() const
{
    return QString();
}

void WebdavBackend::startLogin()
{
    if (not configured() or phase_ != Phase::Idle)
        return;
    phase_ = Phase::Verifying;
    emit changed();
    dav_.propfind(QString::fromStdString(partition_.server()), QString::fromStdString(partition_.username()), QString::fromStdString(partition_.appPassword()), QString());
}

void WebdavBackend::cancelLogin()
{
    // Nothing waits long: a probe either answers within its timeout or has already ended.
}

void WebdavBackend::abortSignIn()
{
    phase_ = Phase::Idle;
}

void WebdavBackend::onProbed(bool reached)
{
    phase_ = Phase::Idle;
    if (reached)
        emit completed();
    else
        // The server answered, but not with the root collection: the address is the thing to fix.
        emit failed(QStringLiteral("generic"));
}

void WebdavBackend::onVerifyFailed(lens::sync::WebDavFailure why)
{
    phase_ = Phase::Idle;
    emit failed(errorForDav(why));
}

}
