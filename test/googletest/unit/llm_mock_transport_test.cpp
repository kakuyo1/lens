/**
 * @file llm_mock_transport_test.cpp
 * @brief Offline HTTP round trips for the channel-aware LlmClient.
 *
 * QTcpServer stays on loopback, so this exercises request preparation, channel selection,
 * response parsing, and the asynchronous signal without a real service or API key.
 */

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <utility>

#include "llm/llm_client.h"
#include "llm_support.h"

namespace {

using lens::llm::Channel;
using lens::llm::Config;
using lens::llm::Explanation;
using lens::llm::LlmClient;
using lens::llm::Usage;
using lens::test::LlmTest;

QByteArray entityResponse()
{
    const QString content = QStringLiteral(
        R"({"results":[{"en":"A major city in the United States.","zh":"美国的一座大城市。"}]})");
    return QJsonDocument(QJsonObject{
                             {"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"}, {"message", QJsonObject{{"content", content}}}}}},
                             {"usage", QJsonObject{{"prompt_tokens", 12}, {"completion_tokens", 9}}},
                         })
        .toJson(QJsonDocument::Compact);
}

/// @brief The event-loop plumbing every client case needs: one QCoreApplication for the test's
///        duration, because Qt allows one at a time and the stub reply fires from a timer.
struct QtApplication {
    QtApplication()
        : application(argc, argv)
    {}

    int              argc     = 1;
    char             name[20] = "lens_gtest_unit";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application;
};

} // namespace

TEST_F(LlmTest, CompletesEntityAgainstAnOfflineHttpServer)
{
    QtApplication qt;

    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));

    QByteArray requestBytes;
    QObject::connect(&server, &QTcpServer::newConnection, [&] {
        QTcpSocket* socket = server.nextPendingConnection();
        ASSERT_NE(socket, nullptr);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &requestBytes] {
            requestBytes += socket->readAll();
            const QByteArray body    = entityResponse();
            const QByteArray headers = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                       QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
            socket->write(headers + body);
            socket->disconnectFromHost();
        });
    });

    LlmClient client(Config{QUrl(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort())),
                            QStringLiteral("test-key"),
                            QStringLiteral("deepseek-flash")});
    client.setChannel(Channel::Entity);
    client.setPreset(QStringLiteral("default"));

    QEventLoop           loop;
    QVector<Explanation> results;
    Usage                usage;
    QString              failure;
    QTimer               timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(2000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&client, &LlmClient::batchFinished, &loop, [&](QVector<Explanation> received, Usage receivedUsage) {
        results = std::move(received);
        usage   = receivedUsage;
        loop.quit();
    });
    QObject::connect(&client, &LlmClient::failed, &loop, [&](QString message) {
        failure = std::move(message);
        loop.quit();
    });

    client.explainWords({QStringLiteral("New York")});
    timeout.start();
    loop.exec();

    EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results.front().title, QStringLiteral("New York"));
    EXPECT_TRUE(results.front().ipa.isEmpty());
    EXPECT_EQ(usage.promptTokens, 12);
    EXPECT_EQ(usage.completionTokens, 9);
    EXPECT_TRUE(requestBytes.contains("New York"));
    EXPECT_TRUE(requestBytes.contains("zh"));
}

namespace {

/// How long a stub case waits for the client to report back before it gives up.
constexpr int kStubTimeoutMs = 2000;

/// @brief A reply that answers from memory, so the client's status, empty-body and
///        transport-failure branches are reachable without a socket.
class StubReply : public QNetworkReply {
public:
    StubReply(int status, QByteArray body, QNetworkReply::NetworkError error)
        : body_(std::move(body))
    {
        // No status attribute at all is how "the transfer never completed" looks to the client:
        // attribute() comes back invalid and toInt() is zero.
        if (status != 0) setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        setError(error, error == QNetworkReply::NoError ? QString() : QStringLiteral("stub failure"));
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] {
            setFinished(true);
            emit finished();
        });
    }

    void abort() override
    {}

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        if (body_.isEmpty()) return -1; // end of the stub body
        const qint64 count = std::min(maxSize, static_cast<qint64>(body_.size()));
        std::memcpy(data, body_.constData(), static_cast<std::size_t>(count));
        body_.remove(0, count);
        return count;
    }

private:
    QByteArray body_;
};

/// @brief A manager whose every request is answered by a StubReply with the given status.
///
/// The posted body is kept so a case can assert on it: the request bytes are written to the
/// device createRequest() is handed.
class StubManager : public QNetworkAccessManager {
public:
    StubManager(int status, QByteArray body, QNetworkReply::NetworkError error = QNetworkReply::NoError)
        : status_(status), body_(std::move(body)), error_(error)
    {}

    QByteArray postedBody;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* device) override
    {
        Q_UNUSED(operation)
        Q_UNUSED(request)
        if (device != nullptr) postedBody = device->readAll();
        auto* reply = new StubReply(status_, body_, error_);
        reply->setParent(this);
        return reply;
    }

private:
    int                         status_;
    QByteArray                  body_;
    QNetworkReply::NetworkError error_;
};

Config stubConfig(const QString& key = QStringLiteral("test-key"))
{
    return Config{QUrl(QStringLiteral("http://example.invalid")), key, QStringLiteral("deepseek-flash")};
}

/// @brief Run one explainWords() to completion and return the failure it reported, if any.
QString runToFailure(LlmClient& client, const QStringList& words)
{
    QString    failure;
    QEventLoop loop;
    QObject::connect(&client, &LlmClient::failed, &loop, [&](QString message) {
        failure = std::move(message);
        loop.quit();
    });
    QTimer::singleShot(kStubTimeoutMs, &loop, &QEventLoop::quit);
    client.explainWords(words);
    loop.exec();
    return failure;
}

} // namespace

/// A status the service answered with is turned into its own reader-facing message, not the
/// generic transport one. These are the branches the httpErrorFor table exists for.
TEST_F(LlmTest, ReportsANonSuccessStatusWithItsOwnMessage)
{
    QtApplication qt;
    StubManager   manager(402, QByteArray());
    LlmClient     client(stubConfig(), &manager);

    const QString failure = runToFailure(client, {QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("credit")) << failure.toStdString();
}

/// A transfer that never reached the service has no status code at all; that is a different
/// failure from a service that answered with an error, and the message says so.
TEST_F(LlmTest, ReportsATransferThatNeverReachedTheService)
{
    QtApplication qt;
    StubManager   manager(0, QByteArray(), QNetworkReply::ConnectionRefusedError);
    LlmClient     client(stubConfig(), &manager);

    const QString failure = runToFailure(client, {QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("could not reach the service")) << failure.toStdString();
}

/// A 200 with an empty body still has to fail the batch: an empty response is not an answer.
TEST_F(LlmTest, RejectsAnEmptyBodyFromASuccessStatus)
{
    QtApplication qt;
    StubManager   manager(200, QByteArray());
    LlmClient     client(stubConfig(), &manager);

    const QString failure = runToFailure(client, {QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("JSON object")) << failure.toStdString();
}

/// A transport error can arrive after Qt has already recorded a successful HTTP status. It must
/// not be misreported as a malformed model response.
TEST_F(LlmTest, ReportsATransportFailureEvenWhenTheReplyHasA200Status)
{
    QtApplication qt;
    StubManager   manager(200, QByteArray(), QNetworkReply::UnknownNetworkError);
    LlmClient     client(stubConfig(), &manager);

    const QString failure = runToFailure(client, {QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("could not reach the service")) << failure.toStdString();
    EXPECT_FALSE(failure.contains("JSON object")) << failure.toStdString();
}

/// More than the contract's twenty entries are truncated before the request is built, so the
/// model never sees the extra ones.
TEST_F(LlmTest, TruncatesAnOverlongBatchToTheContractCap)
{
    QtApplication qt;
    StubManager   manager(402, QByteArray());
    LlmClient     client(stubConfig(), &manager);

    QStringList words;
    for (int i = 0; i < 21; ++i)
        words << QStringLiteral("word%1").arg(i, 2, 10, QLatin1Char('0'));

    runToFailure(client, words);

    EXPECT_TRUE(manager.postedBody.contains("word19")) << "the twentieth word was dropped too";
    EXPECT_FALSE(manager.postedBody.contains("word20")) << "the twenty-first word was sent";
}

/// An empty batch is refused before the network is touched -- there is nothing to ask about.
TEST_F(LlmTest, RefusesAnEmptyBatchWithoutPosting)
{
    QtApplication qt;
    StubManager   manager(200, QByteArray());
    LlmClient     client(stubConfig(), &manager);

    QString failure;
    QObject::connect(&client, &LlmClient::failed, [&](QString message) { failure = message; });
    client.explainWords({});

    EXPECT_TRUE(failure.contains("nothing to look up")) << failure.toStdString();
    EXPECT_TRUE(manager.postedBody.isEmpty());
}

/// No key is the one failure the reader has to fix in settings, and it is caught before the
/// request goes out rather than spending a round trip to be told 401.
TEST_F(LlmTest, RefusesToPostWithoutAnApiKey)
{
    QtApplication qt;
    StubManager   manager(200, QByteArray());
    LlmClient     client(stubConfig(QString()), &manager);

    QString failure;
    QObject::connect(&client, &LlmClient::failed, [&](QString message) { failure = message; });
    client.explainWords({QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("API key is missing")) << failure.toStdString();
    EXPECT_TRUE(manager.postedBody.isEmpty());
}

namespace {

/// @brief A reply that hands its answer over the way a streaming endpoint does: one `readyRead`
///        per chunk, then finished.
///
/// The chunks are split wherever the case says, so a boundary inside an event, or inside a
/// multi-byte character, is the case's choice rather than luck.
class StreamingReply : public QNetworkReply {
public:
    StreamingReply(QVector<QByteArray> chunks, QObject* parent)
        : QNetworkReply(parent), chunks_(std::move(chunks))
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, &StreamingReply::pump);
    }

    void abort() override
    {}

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        const qint64 count = std::min(maxSize, static_cast<qint64>(available_.size()));
        std::memcpy(data, available_.constData(), static_cast<std::size_t>(count));
        available_.remove(0, count);
        return count;
    }

private:
    void pump()
    {
        if (index_ >= chunks_.size()) {
            setFinished(true);
            emit finished();
            return;
        }
        available_ = chunks_.at(index_++);
        emit readyRead();
        QTimer::singleShot(0, this, &StreamingReply::pump);
    }

    QVector<QByteArray> chunks_;
    QByteArray          available_;
    int                 index_ = 0;
};

/// @brief The streaming half of the transport seam: every POST is answered by @p chunks.
class StreamingManager : public QNetworkAccessManager {
public:
    explicit StreamingManager(QVector<QByteArray> chunks)
        : chunks_(std::move(chunks))
    {}

    QByteArray postedBody;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest&, QIODevice* device) override
    {
        Q_UNUSED(operation)
        if (device != nullptr) postedBody = device->readAll();
        auto* reply = new StreamingReply(chunks_, this);
        reply->setParent(this);
        return reply;
    }

private:
    QVector<QByteArray> chunks_;
};

/// @return One SSE event carrying @p content, a finish reason and optionally a usage object, as
///         an OpenAI-compatible endpoint writes it.
QByteArray sseEvent(QString const& content, QString const& finishReason = {}, QJsonObject const& usage = {})
{
    QJsonObject choice{{"delta", QJsonObject{{"content", content}}}};
    choice.insert(QStringLiteral("finish_reason"), finishReason.isEmpty() ? QJsonValue() : QJsonValue(finishReason));
    QJsonObject root{{"choices", QJsonArray{choice}}};
    if (not usage.isEmpty())
        root.insert(QStringLiteral("usage"), usage);
    return QByteArrayLiteral("data: ") + QJsonDocument(root).toJson(QJsonDocument::Compact) + QByteArrayLiteral("\n\n");
}

} // namespace

/// The answer is drawn as it arrives: one `partial()` per field that completes, then a single
/// `batchFinished()` with the whole validated result. The first event is split across two reads,
/// so the buffer has to hold half an event without losing it.
TEST_F(LlmTest, StreamsTheAnswerAsItArrivesAndFinishesWithTheValidatedOne)
{
    QtApplication qt;

    const QString head = QStringLiteral(R"({"results":[{"word":"ubiquitous")");
    const QString mid  = QStringLiteral(R"(,"ipa":"/juːˈbɪkwɪtəs/","en":"existing everywhere")");
    const QString tail = QStringLiteral(R"(,"zh":"无处不在的"}]})");

    QVector<QByteArray> first;
    first << sseEvent(head);
    QVector<QByteArray> chunks{first.front().left(10), first.front().mid(10), sseEvent(mid), sseEvent(tail, QStringLiteral("stop"), QJsonObject{{"prompt_tokens", 12}, {"completion_tokens", 9}}), QByteArrayLiteral("data: [DONE]\n\n")};
    StreamingManager    manager(std::move(chunks));
    LlmClient           client(stubConfig(), &manager);

    QVector<Explanation> previews;
    QVector<Explanation> finished;
    Usage                usage;
    qsizetype            previewsAtFinish = -1;
    QObject::connect(&client, &LlmClient::partial, [&](Explanation preview) { previews.push_back(std::move(preview)); });
    QObject::connect(&client, &LlmClient::batchFinished, &manager, [&](QVector<Explanation> results, Usage received) {
        previewsAtFinish = previews.size();
        finished         = std::move(results);
        usage            = received;
    });

    QEventLoop loop;
    QObject::connect(&client, &LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QObject::connect(&client, &LlmClient::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(kStubTimeoutMs, &loop, &QEventLoop::quit);
    client.explainWords({QStringLiteral("ubiquitous")});
    loop.exec();

    ASSERT_GE(previewsAtFinish, 3) << "the answer arrived in more chunks than were reported";
    EXPECT_EQ(previewsAtFinish, previews.size()) << "a preview arrived after the answer was complete";
    EXPECT_EQ(previews.front().title, QStringLiteral("ubiquitous"));
    EXPECT_TRUE(previews.front().en.isEmpty()) << "the first preview carries only what had closed";
    EXPECT_EQ(previews.back().ipa, QString::fromUtf8("/juːˈbɪkwɪtəs/"));
    EXPECT_EQ(previews.back().zh, QString::fromUtf8("无处不在的"));

    ASSERT_EQ(finished.size(), 1);
    EXPECT_EQ(finished.front().en, QStringLiteral("existing everywhere"));
    EXPECT_EQ(usage.promptTokens, 12) << "the streamed usage was dropped";
    EXPECT_EQ(usage.completionTokens, 9);
    EXPECT_TRUE(manager.postedBody.contains("\"stream\":true"));
    EXPECT_TRUE(manager.postedBody.contains("\"include_usage\":true"));
}

/// A stream that ends without the model saying it is done is today's failure, not a short answer:
/// the whole-response check rejects it and nothing reaches the store.
TEST_F(LlmTest, EndsAStreamThatStoppedShortInTheFailureNotice)
{
    QtApplication qt;

    const QString body = QStringLiteral(
        R"({"results":[{"word":"ubiquitous","ipa":"/juːˈbɪkwɪtəs/","en":"existing everywhere","zh":"无处不在的"}]})");
    // "length" is the model running out of output tokens: the JSON was cut off mid-way.
    StreamingManager manager({sseEvent(body, QStringLiteral("length"))});
    LlmClient        client(stubConfig(), &manager);

    int batchFinished = 0;
    QObject::connect(&client, &LlmClient::batchFinished, [&](QVector<Explanation>, Usage) { ++batchFinished; });

    const QString failure = runToFailure(client, {QStringLiteral("ubiquitous")});

    EXPECT_TRUE(failure.contains("stopped before finishing")) << failure.toStdString();
    EXPECT_EQ(batchFinished, 0) << "a cut-off answer must not reach the store";
}
