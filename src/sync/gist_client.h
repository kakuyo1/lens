#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @file gist_client.h
 * @brief The gist calls sync makes: find the reader's sync gist, read it, create, update, delete.
 *
 * Every call carries the reader's token as a bearer header and nothing else the reader owns. Gist
 * content is read as UTF-8 text; the encryption that happens before a write and after a read is the
 * sync module's, not this one's. Failures are a kind, never the server's text, and a body is never
 * logged, because a gist holds the reader's data (docs/adr/0042).
 */

namespace lens::sync {

/// @brief One file of a gist, as read back.
struct GistFile {
    QString name;
    QString content;           ///< UTF-8 text; empty when the file is binary or was not returned.
    bool    truncated = false; ///< The API cut the content; the full text is at rawUrl.
    QUrl    rawUrl;
};

/// @brief One gist, as read back.
struct Gist {
    QString                 id;
    QString                 description;
    QString                 updatedAt; ///< ISO 8601, as GitHub writes it; compared as text, which sorts correctly.
    QMap<QString, GistFile> files;
};

/// @return The gist, or std::nullopt when the body is not a gist.
std::optional<Gist> parseGist(const QByteArray& body);

/// @return The id of the newest gist whose description is exactly @p description, or std::nullopt.
std::optional<QString> findGistId(const QByteArray& listBody, const QString& description);

/// @brief Why a gist call stopped.
enum class GistFailure {
    Unauthorised, ///< The token is refused: revoked, or it lost the gist scope.
    NotFound,     ///< The gist does not exist, or the token cannot see it.
    Generic,      ///< Anything else the server said, or a body that was not what was asked for.
    Proxy,        ///< No answer at all: the connection never got a byte.
};

/**
 * @brief The gist requests, each reporting one result or one failure.
 *
 * Calls are not queued. Each one is its own request, and its reply is handed to the signal that
 * matches it; nothing here keeps state between calls.
 */
class GistClient : public QObject {
    Q_OBJECT
public:
    /**
     * @param manager Network manager to post through, or null to make one.
     */
    explicit GistClient(QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @brief List the reader's gists, following up to five pages.
    /// @return The call's id; every signal it produces carries it.
    quint64 listGists(const QString& token);

    /// @brief Read one gist, with any truncated file read in full from its raw address.
    /// @return The call's id; every signal it produces carries it.
    quint64 getGist(const QString& token, const QString& id);

    /// @brief Create a secret gist with @p files, mapping file name to its UTF-8 content.
    /// @return The call's id; every signal it produces carries it.
    quint64 createGist(const QString& token, const QString& description, const QMap<QString, QString>& files);

    /// @brief Replace the content of the named files in an existing gist.
    /// @return The call's id; every signal it produces carries it.
    quint64 updateGist(const QString& token, const QString& id, const QMap<QString, QString>& files);

    /// @brief Delete a gist.
    /// @return The call's id; every signal it produces carries it.
    quint64 deleteGist(const QString& token, const QString& id);

    /// @brief Read the login name the token belongs to.
    /// @return The call's id; every signal it produces carries it.
    quint64 getUser(const QString& token);

signals:
    /// @brief The gist list, every page's array joined into one JSON array for findGistId() to read.
    void listed(quint64 callId, QByteArray body);
    void gotGist(quint64 callId, lens::sync::Gist gist);
    void created(quint64 callId, lens::sync::Gist gist);
    void updated(quint64 callId, lens::sync::Gist gist);
    void deleted(quint64 callId, QString id);
    void gotUser(quint64 callId, QString login);

    /// @brief A call stopped without its answer. Carries only the kind, never the server's text.
    void failed(quint64 callId, lens::sync::GistFailure why);

private:
    /// Sends one request and hands its reply to @p onDone: the status, the body, and the Link header.
    void send(quint64 callId, const QByteArray& verb, const QUrl& url, const QString& token, const QByteArray& body, std::function<void(int status, QByteArray body, QByteArray link)> onDone);
    /// Reads one page of the list, and asks for the next when Link names one, up to @p pagesLeft pages.
    void listPage(quint64 callId, const QString& token, const QUrl& url, int pagesLeft, QJsonArray merged);
    /// Reads a truncated file's full content from its raw address.
    void               fillTruncated(quint64 callId, const QString& token, Gist gist, QStringList pending);
    static GistFailure failureFor(int status, const QByteArray& body);
    /// @return The files as the API wants them on write: { name: { content } }.
    static QJsonObject filesObject(const QMap<QString, QString>& files);

    QNetworkAccessManager* manager_;
    QString                userAgent_;
    /// The id the next call takes; every answer carries it back, so one client's calls stay apart.
    quint64 nextCallId_ = 1;
};

}

Q_DECLARE_METATYPE(lens::sync::Gist)
Q_DECLARE_METATYPE(lens::sync::GistFailure)
