#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <utility>
#include <vector>

/**
 * @file scripted_manager.h
 * @brief The network seam the generated clients take: canned answers, counted requests.
 *
 * A reply finishes on the next turn of the loop, so the client's own connections exist before
 * `finished` arrives. One answer is the default; queue() gives the next request its own, which is
 * what a round of more than one call needs. What each request carried is kept, so a case can read
 * back the body a POST or a PATCH sent. An answer may carry the headers a client reads from the
 * reply -- a Location, a Content-Type, a Link -- and announces its body's size as download
 * progress, which is what a ceiling cut-off reads.
 */

namespace lens::test {

/// A reply that finishes on the next turn of the loop, so the client's own connections exist
/// before `finished` arrives. It carries a status, an optional `Location`, and a body.
/// `abort()` is what an answer that outgrew its ceiling does: a cancelled reply, which the client
/// has to read as offline.
class ScriptedReply final : public QNetworkReply {
public:
    ScriptedReply(QObject* parent, int status, QString const& location = {}, qint64 length = 0, bool answers = true, QByteArray body = {}, QByteArray const& contentType = {}, QByteArray const& link = {}) // NOLINT(bugprone-easily-swappable-parameters): ScriptedReply takes its 2 QByteArray arguments in a fixed positional order; the same-typed group cannot be reordered without changing the call's meaning
        : QNetworkReply(parent), status_(status), body_(std::move(body))
    {
        // A reply that never answers has no status: a real connection that times out before its
        // headers arrive reports none, and the proxy judgement depends on that difference.
        if (answers)
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status_);
        if (not location.isEmpty())
            setHeader(QNetworkRequest::LocationHeader, location);
        if (length > 0)
            setHeader(QNetworkRequest::ContentLengthHeader, length);
        if (not contentType.isEmpty())
            setHeader(QNetworkRequest::ContentTypeHeader, contentType);
        // The pagination header some clients read from the answer rather than the body.
        if (not link.isEmpty())
            setRawHeader("Link", link);
        open(QIODevice::ReadOnly);
        if (answers)
            QTimer::singleShot(0, this, [this] { deliver(); });
    }

    void abort() override
    {
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("the answer outgrew its ceiling"));
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

    /// Finish once, whichever of the two paths arrives first: a reply that finishes twice is
    /// not a shape Qt produces, and the counts below are only meaningful if it does not.
    void deliver()
    {
        if (delivered_)
            return;
        delivered_ = true;
        // A real transfer announces its bytes before it finishes, and the client reads them then.
        // Progress carries the same count: a client that cuts an answer off at its ceiling reads
        // the size from here, since a stub never streams.
        const qint64 announced = static_cast<qint64>(body_.size());
        if (not body_.isEmpty())
            emit readyRead();
        if (announced > 0)
            emit downloadProgress(announced, announced);
        setFinished(true);
        emit finished();
    }

private:
    int        status_;
    QByteArray body_;
    bool       delivered_ = false;
};

/// The seam the network clients take, counting what they asked for.
class ScriptedManager final : public QNetworkAccessManager {
public:
    /// One request, as the case reads it back.
    struct Sent {
        QByteArray verb; ///< GET, PUT, PROPFIND, ...; a custom verb rides the request's attribute.
        QString    url;
        QByteArray body;  ///< What the request carried; empty for a GET or a DELETE.
        QByteArray depth; ///< The PROPFIND Depth header; empty for the other verbs.
    };

    explicit ScriptedManager(int status = 302, QString location = {}, bool answers = true, QByteArray body = {}, QByteArray contentType = {}, QByteArray link = {})
        : default_{status, std::move(location), answers, std::move(body), std::move(contentType), std::move(link)}
    {
    }

    /// @brief Give the next request its own answer, ahead of the default one.
    void queue(int status, QByteArray body = {}, QString location = {}, bool answers = true, QByteArray contentType = {}, QByteArray link = {})
    {
        queued_.push_back(Answer{status, std::move(location), answers, std::move(body), std::move(contentType), std::move(link)});
    }

    int requests = 0;
    /// What the answer says it is, when a case wants the server to announce a size.
    qint64 announceLength = 0;
    /// What the request asked about redirects, kept so a case can read it back: a stub manager
    /// never follows one whatever the policy says, so the count alone would prove nothing.
    QVariant redirectPolicy;
    /// Every request in order, with the body it carried.
    std::vector<Sent> sent;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* outgoing) override
    {
        ++requests;
        redirectPolicy = request.attribute(QNetworkRequest::RedirectPolicyAttribute);
        QByteArray verb;
        switch (operation) {
            case QNetworkAccessManager::GetOperation: verb = "GET"; break;
            case QNetworkAccessManager::PostOperation: verb = "POST"; break;
            case QNetworkAccessManager::PutOperation: verb = "PUT"; break;
            case QNetworkAccessManager::DeleteOperation: verb = "DELETE"; break;
            default: verb = request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray(); break;
        }
        Sent record{std::move(verb), request.url().toString(), {}, request.rawHeader("Depth")};
        if (outgoing != nullptr) {
            if (not outgoing->isOpen())
                outgoing->open(QIODevice::ReadOnly);
            record.body = outgoing->readAll();
        }
        sent.push_back(std::move(record));

        Answer answer = default_;
        if (not queued_.empty()) {
            answer = std::move(queued_.front());
            queued_.pop_front();
        }
        return new ScriptedReply(this, answer.status, answer.location, announceLength, answer.answers, answer.body, answer.contentType, answer.link);
    }

private:
    struct Answer {
        int     status;
        QString location;
        /// Whether the reply ever finishes. False leaves it pending, which is how a case drives a
        /// client whose answer it delivers itself rather than one the stub hands over.
        bool       answers;
        QByteArray body;
        QByteArray contentType;
        QByteArray link;
    };

    Answer             default_;
    std::deque<Answer> queued_;
};

}
