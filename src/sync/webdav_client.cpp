/**
 * @file webdav_client.cpp
 * @brief The WebDAV requests, for webdav_client.h.
 */

#include "sync/webdav_client.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "util/log.h"

namespace lens::sync {
namespace {

constexpr int kTimeoutMs = 15000;
/// The most any single answer may be. A sync document is far smaller; this is a ceiling, not a plan.
constexpr int kMaxBodyBytes = 10 * 1024 * 1024;

/// @return Whether a URL is one the WebDAV calls may be sent to: http or https, since a reader's own
///        NAS or an Alist on the LAN is legitimately plain http.
bool allowedHost(const QUrl& url)
{
    return (url.scheme() == QLatin1String("https") or url.scheme() == QLatin1String("http")) and not url.host().isEmpty();
}

} // namespace

WebDavClient::WebDavClient(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      userAgent_(QStringLiteral("Lens/%1").arg(QString::fromUtf8(LENS_VERSION)))
{
}

void WebDavClient::propfind(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath)
{
    const QUrl url = addressFor(server, relativePath);
    if (not allowedHost(url)) {
        LENS_WARN("a WebDAV call's address is not one this client may ask for");
        emit failed(WebDavFailure::Generic);
        return;
    }
    send("PROPFIND", url, username, appPassword, {}, {}, [this](int status, QByteArray body) {
        // A missing file is an answer, not a failure: the first sync creates the envelope. Nutstore
        // answers 409 while the collection above the file is missing; nginx-flavoured servers say 404.
        if (status == 404 or status == 409) {
            emit found(false);
            return;
        }
        if (status != 207 and status != 200) {
            emit failed(failureFor(status, body));
            return;
        }
        emit found(true);
    });
}

void WebDavClient::get(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath)
{
    const QUrl url = addressFor(server, relativePath);
    if (not allowedHost(url)) {
        LENS_WARN("a WebDAV call's address is not one this client may ask for");
        emit failed(WebDavFailure::Generic);
        return;
    }
    send("GET", url, username, appPassword, {}, {}, [this](int status, QByteArray body) {
        if (status == 404 or status == 409) {
            // The path is gone, its parent collection with it (see the propfind note).
            emit failed(WebDavFailure::NotFound);
            return;
        }
        if (status != 200) {
            emit failed(failureFor(status, body));
            return;
        }
        emit gotFile(QString::fromUtf8(body));
    });
}

void WebDavClient::put(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath, const QString& content)
{
    putOnce(server, username, appPassword, relativePath, content, false);
}

void WebDavClient::putOnce(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath, const QString& content, bool collectionMade)
{
    const QUrl url = addressFor(server, relativePath);
    if (not allowedHost(url)) {
        LENS_WARN("a WebDAV call's address is not one this client may ask for");
        emit failed(WebDavFailure::Generic);
        return;
    }
    send("PUT", url, username, appPassword, content.toUtf8(), "application/octet-stream", [this, server, username, appPassword, relativePath, content, collectionMade](int status, QByteArray body) {
        if (status == 200 or status == 201 or status == 204) {
            emit putDone(relativePath);
            return;
        }
        // The collection is missing: make it once and write again. A second refusal is real.
        if (status == 409 and collectionMade) {
            emit failed(failureFor(status, body));
            return;
        }
        if (status == 409) {
            const int     slash      = relativePath.lastIndexOf('/');
            const QString collection = slash > 0 ? relativePath.left(slash) : QString();
            send("MKCOL", addressFor(server, collection), username, appPassword, {}, {}, [this, server, username, appPassword, relativePath, content](int mkcolStatus, QByteArray mkcolBody) {
                // 201 made it; 405 says it was already there. Anything else is not recoverable.
                if (mkcolStatus != 201 and mkcolStatus != 405) {
                    emit failed(failureFor(mkcolStatus, mkcolBody));
                    return;
                }
                putOnce(server, username, appPassword, relativePath, content, true);
            });
            return;
        }
        emit failed(failureFor(status, body));
    });
}

void WebDavClient::del(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath)
{
    const QUrl url = addressFor(server, relativePath);
    if (not allowedHost(url)) {
        LENS_WARN("a WebDAV call's address is not one this client may ask for");
        emit failed(WebDavFailure::Generic);
        return;
    }
    send("DELETE", url, username, appPassword, {}, {}, [this, relativePath](int status, QByteArray body) {
        // 204 is the answer; a 404 or a 409 means it is already gone -- or its collection is --
        // which is what the caller wanted. Nutstore answers 204 to both, but not every server does.
        if (status == 200 or status == 204 or status == 404 or status == 409)
            emit deleted(relativePath);
        else
            emit failed(failureFor(status, body));
    });
}

void WebDavClient::send(const QByteArray& verb, const QUrl& url, const QString& username, const QString& appPassword, const QByteArray& body, const QByteArray& contentType, std::function<void(int, QByteArray)> onDone)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setRawHeader("Authorization", QByteArray("Basic ") + QByteArray(username.toUtf8() + ":" + appPassword.toUtf8()).toBase64());
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    if (not body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    if (verb == "PROPFIND")
        request.setRawHeader("Depth", "0");
    // A redirect here would be a host this client did not choose; it is not followed.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // The verb rides along for the scripted test manager to record; sendCustomRequest is the only
    // place Qt lets a custom verb through, and it does not carry it anywhere else.
    request.setAttribute(QNetworkRequest::CustomVerbAttribute, verb);
    LENS_TRACE("{} {}", verb.toStdString(), url.path().toStdString());

    QNetworkReply* reply = nullptr;
    if (verb == "GET")
        reply = manager_->get(request);
    else if (verb == "DELETE")
        reply = manager_->deleteResource(request);
    else
        reply = manager_->sendCustomRequest(request, verb, body);

    connect(reply, &QNetworkReply::finished, this, [reply, onDone = std::move(onDone)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // The body is read whole and capped: an answer past the ceiling is not the server speaking
        // to this call, so it is dropped rather than parsed.
        QByteArray answer = reply->readAll();
        if (answer.size() > kMaxBodyBytes) {
            LENS_WARN("a WebDAV answer was over the ceiling and was dropped");
            answer.clear();
        }
        reply->deleteLater();
        onDone(status, answer);
    });
}

QUrl WebDavClient::addressFor(const QString& server, const QString& relativePath)
{
    QString base = server.trimmed();
    if (base.isEmpty())
        return QUrl{};
    // A reader who pastes a host without a scheme meant https; a LAN server is written with its http://.
    if (not base.contains(QLatin1String("://")))
        base.prepend(QStringLiteral("https://"));
    while (base.endsWith(u'/'))
        base.chop(1);
    const QString joined = relativePath.isEmpty() ? base : base + QStringLiteral("/") + relativePath;
    const QUrl    url(joined);
    return url.isValid() and not url.host().isEmpty() ? url : QUrl{};
}

WebDavFailure WebDavClient::failureFor(int status, const QByteArray& body)
{
    if (status == 0 and body.isEmpty())
        return WebDavFailure::Proxy;
    if (status == 401 or status == 403)
        return WebDavFailure::Unauthorised;
    if (status == 404)
        return WebDavFailure::NotFound;
    return WebDavFailure::Generic;
}

}
