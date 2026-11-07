#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

/**
 * @file webdav_client.h
 * @brief The WebDAV verbs sync needs: find the envelope file, read it, write it, delete it.
 *
 * One file at a fixed relative path under the server address the reader entered; credentials are
 * Basic auth with a username and an app password. The envelope crosses this client as opaque text --
 * the encryption that happens before a write and after a read is the sync module's, not this one's.
 * Failures are a kind, never the server's text, and a body is never logged (docs/adr/0157).
 */

namespace lens::sync {

/// @brief Why a WebDAV call stopped.
enum class WebDavFailure {
    Unauthorised, ///< The server refused the credentials, or the call was throttled.
    NotFound,     ///< The asked-about path does not exist.
    Generic,      ///< Anything else the server said.
    Proxy,        ///< No answer at all: the connection never got a byte.
};

/**
 * @brief The WebDAV requests, each reporting one result or one failure.
 *
 * Calls are not queued. Each one is its own request, and its reply is handed to the signal that
 * matches it; nothing here keeps state between calls. A write whose collection is missing makes the
 * collection once and retries, so the caller never sequences MKCOL itself.
 */
class WebDavClient : public QObject {
    Q_OBJECT
public:
    /**
     * @param manager Network manager to post through, or null to make one.
     */
    explicit WebDavClient(QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @brief Ask whether @p relativePath exists under the server; an empty path probes the server
    ///        itself, which is the reachability check a sign-in runs.
    void propfind(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath);

    /// @brief Read the file at @p relativePath as UTF-8 text.
    void get(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath);

    /// @brief Write @p content to @p relativePath as UTF-8 text.
    void put(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath, const QString& content);

    /// @brief Delete @p relativePath; a path already gone counts as deleted.
    void del(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath);

signals:
    /// @brief The asked-about path exists; for a server probe, the probe simply answered.
    void found(bool exists);
    void gotFile(QString content);
    void putDone(QString path);
    void deleted(QString path);

    /// @brief A call stopped without its answer. Carries only the kind, never the server's text.
    void failed(lens::sync::WebDavFailure why);

private:
    /// Sends one request and hands its reply to @p onDone: the status and the body.
    void send(const QByteArray& verb, const QUrl& url, const QString& username, const QString& appPassword, const QByteArray& body, const QByteArray& contentType, std::function<void(int status, QByteArray body)> onDone);
    /// @brief Write @p content once the collection of @p relativePath exists, making it when it does not.
    void putOnce(const QString& server, const QString& username, const QString& appPassword, const QString& relativePath, const QString& content);
    /// @return The server's address for @p relativePath: a scheme is supplied when missing, the path
    ///         is joined under it; invalid when the address is not one this client may ask.
    static QUrl          addressFor(const QString& server, const QString& relativePath);
    static WebDavFailure failureFor(int status, const QByteArray& body);

    QNetworkAccessManager* manager_;
    QString                userAgent_;
};

}
