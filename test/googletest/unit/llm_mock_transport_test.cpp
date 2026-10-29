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
using lens::llm::ExplainRequest;
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

    client.explainWords(ExplainRequest{.words = {QStringLiteral("New York")}, .channel = Channel::Entity});
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
        if (body_.isEmpty()) return -1;
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

/// The probe is only offered against an https endpoint, the one every real configuration uses.
Config probeConfig(const QString& key   = QStringLiteral("test-key"),
                   const QString& model = QStringLiteral("deepseek-flash"))
{
    return Config{QUrl(QStringLiteral("https://example.invalid")), key, model};
}

struct ProbeResult {
    bool    ok = false;
    QString message;
};

/// @brief Run one probe to its answer. A missing configuration answers synchronously, before
///        the loop would start, so the wait is only entered when nothing has answered yet.
ProbeResult runProbe(LlmClient& client)
{
    ProbeResult result;
    bool        answered = false;
    QEventLoop  loop;
    QObject::connect(&client, &LlmClient::probeFinished, &loop, [&](bool ok, QString message) {
        result   = {ok, std::move(message)};
        answered = true;
        loop.quit();
    });
    QTimer::singleShot(kStubTimeoutMs, &loop, &QEventLoop::quit);
    client.probeConnection();
    if (not answered)
        loop.exec();
    return result;
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
    client.explainWords(ExplainRequest{.words = words});
    loop.exec();
    return failure;
}

} // namespace

/// The probe asks the smallest thing that still makes the service check all three parts, and
/// its success sentence never carries the key.
TEST_F(LlmTest, ProbeAcceptsAnAnswerFromTheModelAndSendsNoExplanation)
{
    QtApplication qt;
    StubManager   manager(200, QByteArray(R"({"choices":[]})"));
    LlmClient     client(probeConfig(), &manager);

    const ProbeResult result = runProbe(client);

    EXPECT_TRUE(result.ok) << result.message.toStdString();
    EXPECT_TRUE(result.message.contains("Connected")) << result.message.toStdString();
    EXPECT_FALSE(result.message.contains("test-key")) << "the probe echoed the key";
    EXPECT_TRUE(manager.postedBody.contains("\"model\":\"deepseek-flash\"")) << manager.postedBody.toStdString();
    EXPECT_TRUE(manager.postedBody.contains("\"max_tokens\":1")) << manager.postedBody.toStdString();
    EXPECT_TRUE(manager.postedBody.contains("\"stream\":false")) << manager.postedBody.toStdString();
    EXPECT_FALSE(manager.postedBody.contains("test-key")) << "the key reached the request body";
}

/// An HTTP answer is the service speaking: a rejected key has its own sentence rather than the
/// reach failure, and a 200 without a completion shape is not accepted as a model.
TEST_F(LlmTest, ProbeReportsARejectedKeyAndANonModelAnswer)
{
    QtApplication qt;
    {
        StubManager       manager(401, QByteArray());
        LlmClient         client(probeConfig(), &manager);
        const ProbeResult result = runProbe(client);
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.message.contains("API key")) << result.message.toStdString();
    }
    {
        StubManager       manager(200, QByteArrayLiteral("<html>sign in</html>"));
        LlmClient         client(probeConfig(), &manager);
        const ProbeResult result = runProbe(client);
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.message.contains("not like a model")) << result.message.toStdString();
    }
}

/// A 400 at a completion endpoint is the model's name more often than anything else, and the
/// service's own words say only "malformed": the sentence names the model and never the key.
TEST_F(LlmTest, ProbeNamesTheModelWhenTheServiceRefusesTheRequest)
{
    QtApplication qt;
    StubManager   manager(400, QByteArray());
    LlmClient     client(probeConfig(QStringLiteral("test-key"), QStringLiteral("deepseek-v4.1-flash")), &manager);

    const ProbeResult result = runProbe(client);

    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.message.contains("deepseek-v4.1-flash")) << result.message.toStdString();
    EXPECT_TRUE(result.message.contains("model")) << result.message.toStdString();
    EXPECT_FALSE(result.message.contains("test-key")) << "the probe echoed the key";
}

/// Missing parts answer without a socket, so a reader who presses too early gets a sentence
/// rather than a spinning button.
TEST_F(LlmTest, ProbeNamesTheMissingPartBeforeSending)
{
    QtApplication qt;
    StubManager   manager(200, QByteArray(R"({"choices":[]})"));

    {
        LlmClient         client(probeConfig(QString()), &manager);
        const ProbeResult result = runProbe(client);
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.message.contains("API key")) << result.message.toStdString();
    }
    {
        LlmClient         client(probeConfig(QStringLiteral("test-key"), QString()), &manager);
        const ProbeResult result = runProbe(client);
        EXPECT_FALSE(result.ok);
        EXPECT_TRUE(result.message.contains("model")) << result.message.toStdString();
    }
    EXPECT_TRUE(manager.postedBody.isEmpty()) << "a probe with missing parts still sent a request";
}

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

/// An answer carrying more senses than the contract keeps is capped at kMaxSenses, and the
/// multiple prompt is the one the record's sense flag selects, not whatever preset it names.
TEST_F(LlmTest, SendsTheMultipleSenseRequestAndCapsItsSenses)
{
    QtApplication qt;

    const QString content = QStringLiteral(
        R"({"results":[{"word":"bank","ipa":"/bæŋk/","senses":[{"en":"a financial house","zh":"银行"},{"en":"the land beside a river","zh":"河岸"},{"en":"to rely on","zh":"依靠"},{"en":"a row of similar objects","zh":"一排"}]}]})");
    const QByteArray body = QJsonDocument(QJsonObject{
                                              {"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"}, {"message", QJsonObject{{"content", content}}}}}},
                                              {"usage", QJsonObject{{"prompt_tokens", 12}, {"completion_tokens", 9}}},
                                          })
                                .toJson(QJsonDocument::Compact);

    StubManager manager(200, body);
    LlmClient   client(stubConfig(), &manager);

    QVector<Explanation> results;
    QString              failure;
    QEventLoop           loop;
    QObject::connect(&client, &LlmClient::batchFinished, &loop, [&](QVector<Explanation> received, Usage) {
        results = std::move(received);
        loop.quit();
    });
    QObject::connect(&client, &LlmClient::failed, &loop, [&](QString message) {
        failure = std::move(message);
        loop.quit();
    });
    QTimer::singleShot(kStubTimeoutMs, &loop, &QEventLoop::quit);
    client.explainWords(ExplainRequest{.words          = {QStringLiteral("bank")},
                                       .preset         = QStringLiteral("translate"),
                                       .language       = QStringLiteral("zh"),
                                       .multipleSenses = true,
                                       .etymology      = true,
                                       .example        = true});
    loop.exec();

    EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results.front().senses.size(), lens::llm::kMaxSenses);
    EXPECT_TRUE(manager.postedBody.contains("one to three distinct common senses"))
        << "the sense flag did not select the multiple prompt";
    EXPECT_TRUE(manager.postedBody.contains("origin")) << "the call's etymology flag did not reach the prompt";
    EXPECT_TRUE(manager.postedBody.contains("example")) << "the call's example flag did not reach the prompt";
}

/// The record's sense flag, not the preset string, decides the response cap: a preset named
/// "multiple" with the flag off keeps the one sense a single-sense request asked for.
TEST_F(LlmTest, KeepsOneSenseWhenTheSenseFlagIsOff)
{
    QtApplication qt;

    const QString content = QStringLiteral(
        R"({"results":[{"word":"bank","ipa":"/bæŋk/","senses":[{"en":"a financial house","zh":"银行"},{"en":"the land beside a river","zh":"河岸"},{"en":"to rely on","zh":"依靠"},{"en":"a row of similar objects","zh":"一排"}]}]})");
    const QByteArray body = QJsonDocument(QJsonObject{
                                              {"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"}, {"message", QJsonObject{{"content", content}}}}}},
                                          })
                                .toJson(QJsonDocument::Compact);

    StubManager manager(200, body);
    LlmClient   client(stubConfig(), &manager);

    QVector<Explanation> results;
    QEventLoop           loop;
    QObject::connect(&client, &LlmClient::batchFinished, &loop, [&](QVector<Explanation> received, Usage) {
        results = std::move(received);
        loop.quit();
    });
    QObject::connect(&client, &LlmClient::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(kStubTimeoutMs, &loop, &QEventLoop::quit);
    client.explainWords(ExplainRequest{.words = {QStringLiteral("bank")}, .preset = QStringLiteral("multiple")});
    loop.exec();

    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results.front().senses.size(), 1) << "the preset string decided the cap instead of the sense flag";
}

/// An empty batch is refused before the network is touched -- there is nothing to ask about.
TEST_F(LlmTest, RefusesAnEmptyBatchWithoutPosting)
{
    QtApplication qt;
    StubManager   manager(200, QByteArray());
    LlmClient     client(stubConfig(), &manager);

    QString failure;
    QObject::connect(&client, &LlmClient::failed, [&](QString message) { failure = message; });
    client.explainWords(ExplainRequest{});

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
    client.explainWords(ExplainRequest{.words = {QStringLiteral("ubiquitous")}});

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
    client.explainWords(ExplainRequest{.words = {QStringLiteral("ubiquitous")}});
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
