/**
 * @file gist_client.cpp
 * @brief The gist requests, and the parsers that read a gist and find the sync gist in a list.
 */

#include "gist_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

#include "util/log.h"

namespace lens::sync {
namespace {

/// The API the calls go to. Fixed here: no path a caller supplies is ever a host.
constexpr auto kApi       = "https://api.github.com";
constexpr int  kTimeoutMs = 15000;
/// How many pages of the gist list one search reads. Enough for any one reader's gists.
constexpr int kMaxPages = 5;
/// The most any single answer may be. A sync document is far smaller; this is a ceiling, not a plan.
constexpr int kMaxBodyBytes = 10 * 1024 * 1024;

/// @return The address of the first `rel="next"` link in a `Link` header, if there is one.
QUrl nextPage(const QByteArray& link)
{
    static const QRegularExpression next(QStringLiteral("<([^>]+)>;\\s*rel=\"next\""));
    const QRegularExpressionMatch   match = next.match(QString::fromUtf8(link));
    return match.hasMatch() ? QUrl{match.captured(1)} : QUrl{};
}

/// @return Whether a URL is one the gist calls may be sent to: https, to the GitHub API or its raw host.
bool allowedHost(const QUrl& url)
{
    return url.scheme() == QLatin1String("https") and (url.host() == QLatin1String("api.github.com") or url.host() == QLatin1String("gist.githubusercontent.com"));
}

} // namespace

std::optional<Gist> parseGist(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (not document.isObject())
        return std::nullopt;

    const QJsonObject root = document.object();
    Gist              gist;
    gist.id          = root.value("id").toString();
    gist.description = root.value("description").toString();
    gist.updatedAt   = root.value("updated_at").toString();
    if (gist.id.isEmpty())
        return std::nullopt;

    const QJsonObject files = root.value("files").toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject file = it.value().toObject();
        GistFile          out;
        out.name      = it.key();
        out.content   = file.value("content").toString();
        out.truncated = file.value("truncated").toBool();
        out.rawUrl    = QUrl{file.value("raw_url").toString()};
        gist.files.insert(out.name, out);
    }
    return gist;
}

std::optional<QString> findGistId(const QByteArray& listBody, const QString& description)
{
    const QJsonDocument document = QJsonDocument::fromJson(listBody);
    if (not document.isArray())
        return std::nullopt;

    // ISO 8601 timestamps sort as text, so the newest is the greatest string among the matches.
    std::optional<QString> newestId;
    QString                newestAt;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject item = value.toObject();
        if (item.value("description").toString() != description)
            continue;
        const QString updated = item.value("updated_at").toString();
        if (not newestId or updated > newestAt) {
            newestId = item.value("id").toString();
            newestAt = updated;
        }
    }
    return newestId;
}

GistClient::GistClient(QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      userAgent_(QStringLiteral("Lens/%1").arg(QString::fromUtf8(LENS_VERSION)))
{
}

void GistClient::listGists(const QString& token)
{
    listPage(token, QUrl{QStringLiteral("%1/gists?per_page=100").arg(QLatin1String(kApi))}, kMaxPages, QJsonArray{});
}

void GistClient::getGist(const QString& token, const QString& id)
{
    send("GET", QUrl{QStringLiteral("%1/gists/%2").arg(QLatin1String(kApi), id)}, token, {}, [this, token](int status, QByteArray body, QByteArray) {
        if (status != 200) {
            emit failed(failureFor(status, body));
            return;
        }
        const std::optional<Gist> gist = parseGist(body);
        if (not gist) {
            emit failed(GistFailure::Generic);
            return;
        }
        QStringList pending;
        for (const GistFile& file : gist->files)
            if (file.truncated)
                pending.append(file.name);
        if (pending.isEmpty())
            emit gotGist(*gist);
        else
            fillTruncated(token, *gist, pending);
    });
}

void GistClient::fillTruncated(const QString& token, Gist gist, QStringList pending)
{
    if (pending.isEmpty()) {
        emit gotGist(gist);
        return;
    }
    const QString name = pending.takeFirst();
    const QUrl    raw  = gist.files.value(name).rawUrl;
    // The raw address is on the gist host; anything else is not asked for, and the file stays short.
    if (not allowedHost(raw)) {
        LENS_WARN("a truncated gist file's address is not one this call may ask for");
        emit failed(GistFailure::Generic);
        return;
    }
    send("GET", raw, token, {}, [this, token, gist, pending, name](int status, QByteArray body, QByteArray) mutable {
        if (status != 200) {
            emit failed(failureFor(status, body));
            return;
        }
        GistFile file    = gist.files.value(name);
        file.content     = QString::fromUtf8(body);
        file.truncated   = false;
        gist.files[name] = file;
        fillTruncated(token, gist, pending);
    });
}

void GistClient::createGist(const QString& token, const QString& description, const QMap<QString, QString>& files)
{
    // One POST, no retry. A create whose answer is lost leaves an orphan gist with the same description;
    // findGistId() reads the newest of those, so the orphan is never mistaken for a different document.
    const QJsonObject body{{"description", description}, {"public", false}, {"files", filesObject(files)}};
    send("POST", QUrl{QStringLiteral("%1/gists").arg(QLatin1String(kApi))}, token, QJsonDocument(body).toJson(QJsonDocument::Compact), [this](int status, QByteArray answer, QByteArray) {
        if (status != 201) {
            emit failed(failureFor(status, answer));
            return;
        }
        const std::optional<Gist> gist = parseGist(answer);
        if (gist)
            emit created(*gist);
        else
            emit failed(GistFailure::Generic);
    });
}

void GistClient::updateGist(const QString& token, const QString& id, const QMap<QString, QString>& files)
{
    const QJsonObject body{{"files", filesObject(files)}};
    send("PATCH", QUrl{QStringLiteral("%1/gists/%2").arg(QLatin1String(kApi), id)}, token, QJsonDocument(body).toJson(QJsonDocument::Compact), [this](int status, QByteArray answer, QByteArray) {
        if (status != 200) {
            emit failed(failureFor(status, answer));
            return;
        }
        const std::optional<Gist> gist = parseGist(answer);
        if (gist)
            emit updated(*gist);
        else
            emit failed(GistFailure::Generic);
    });
}

void GistClient::deleteGist(const QString& token, const QString& id)
{
    send("DELETE", QUrl{QStringLiteral("%1/gists/%2").arg(QLatin1String(kApi), id)}, token, {}, [this, id](int status, QByteArray answer, QByteArray) {
        // 204 is the answer; a 404 means it is already gone, which is what the caller wanted.
        if (status == 204 or status == 404)
            emit deleted(id);
        else
            emit failed(failureFor(status, answer));
    });
}

void GistClient::getUser(const QString& token)
{
    send("GET", QUrl{QStringLiteral("%1/user").arg(QLatin1String(kApi))}, token, {}, [this](int status, QByteArray body, QByteArray) {
        if (status != 200) {
            emit failed(failureFor(status, body));
            return;
        }
        const QString login = QJsonDocument::fromJson(body).object().value("login").toString();
        if (login.isEmpty())
            emit failed(GistFailure::Generic);
        else
            emit gotUser(login);
    });
}

void GistClient::send(const QByteArray& verb, const QUrl& url, const QString& token, const QByteArray& body, std::function<void(int, QByteArray, QByteArray)> onDone)
{
    if (not allowedHost(url)) {
        LENS_WARN("a gist call's address is not one this client may ask for");
        emit failed(GistFailure::Generic);
        return;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(kTimeoutMs);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent_);
    if (not body.isEmpty())
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // A redirect here would be a host this client did not choose; it is not followed.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    LENS_TRACE("{} {}", verb.toStdString(), url.path().toStdString());

    QNetworkReply* reply = nullptr;
    if (verb == "GET")
        reply = manager_->get(request);
    else if (verb == "DELETE")
        reply = manager_->deleteResource(request);
    else
        reply = manager_->sendCustomRequest(request, verb, body);

    connect(reply, &QNetworkReply::finished, this, [reply, onDone = std::move(onDone)] {
        const int        status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray link   = reply->rawHeader("Link");
        // The body is read whole and capped: an answer past the ceiling is not the server speaking
        // to this call, so it is dropped rather than parsed.
        QByteArray body = reply->readAll();
        if (body.size() > kMaxBodyBytes) {
            LENS_WARN("a gist answer was over the ceiling and was dropped");
            body.clear();
        }
        reply->deleteLater();
        onDone(status, body, link);
    });
}

void GistClient::listPage(const QString& token, const QUrl& url, int pagesLeft, QJsonArray merged)
{
    send("GET", url, token, {}, [this, token, pagesLeft, merged](int status, QByteArray body, QByteArray link) mutable {
        if (status != 200) {
            emit failed(failureFor(status, body));
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (not document.isArray()) {
            emit failed(GistFailure::Generic);
            return;
        }
        for (const QJsonValue& item : document.array())
            merged.append(item);

        // A missing or exhausted link, or the page cap, ends the list with what has been read.
        const QUrl next = nextPage(link);
        if (next.isValid() and pagesLeft > 1 and allowedHost(next)) {
            listPage(token, next, pagesLeft - 1, merged);
            return;
        }
        emit listed(QJsonDocument(merged).toJson(QJsonDocument::Compact));
    });
}

GistFailure GistClient::failureFor(int status, const QByteArray& body)
{
    if (status == 0 and body.isEmpty())
        return GistFailure::Proxy;
    if (status == 401 or status == 403)
        return GistFailure::Unauthorised;
    if (status == 404)
        return GistFailure::NotFound;
    return GistFailure::Generic;
}

QJsonObject GistClient::filesObject(const QMap<QString, QString>& files)
{
    QJsonObject out;
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        out.insert(it.key(), QJsonObject{{"content", it.value()}});
    return out;
}

}
