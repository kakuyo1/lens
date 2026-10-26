#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
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
 * back the body a POST or a PATCH sent.
 */

namespace lens::test {

/// A reply that finishes on the next turn of the loop, so the client's own connections exist
/// before `finished` arrives. It carries a status, an optional `Location`, and a body.
/// `abort()` is what an answer that outgrew its ceiling does: a cancelled reply, which the client
/// has to read as offline.
class ScriptedReply final : public QNetworkReply {
public:
    ScriptedReply(QObject* parent, int status, QString location = {}, qint64 length = 0, bool answers = true, QByteArray body = {})
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
        if (not body_.isEmpty())
            emit readyRead();
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
        QString    url;
        QByteArray body; ///< What the request carried; empty for a GET or a DELETE.
    };

    explicit ScriptedManager(int status = 302, QString location = {}, bool answers = true, QByteArray body = {})
        : default_{status, std::move(location), answers, std::move(body)}
    {
    }

    /// @brief Give the next request its own answer, ahead of the default one.
    void queue(int status, QByteArray body = {}, QString location = {}, bool answers = true)
    {
        queued_.push_back(Answer{status, std::move(location), answers, std::move(body)});
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
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice* outgoing) override
    {
        ++requests;
        redirectPolicy = request.attribute(QNetworkRequest::RedirectPolicyAttribute);
        Sent record{request.url().toString(), {}};
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
        return new ScriptedReply(this, answer.status, answer.location, announceLength, answer.answers, std::move(answer.body));
    }

private:
    struct Answer {
        int     status;
        QString location;
        /// Whether the reply ever finishes. False leaves it pending, which is how a case drives a
        /// client whose answer it delivers itself rather than one the stub hands over.
        bool       answers;
        QByteArray body;
    };

    Answer             default_;
    std::deque<Answer> queued_;
};

}
