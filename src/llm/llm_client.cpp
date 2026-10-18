/**
 * @file llm_client.cpp
 * @brief Request setup and the model-list and explanation round trips for llm_client.h.
 */

#include "llm_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <memory>
#include <utility>
#include <variant>

#include "llm_pure.h"
#include "util/log.h"

namespace lens::llm {
namespace {

constexpr int    kMaxWords           = 20; ///< Contract limit.
constexpr int    kTimeoutMs          = 30000;
constexpr int    kModelListTimeoutMs = 10000;
constexpr qint64 kMaxModelListBytes  = 2 * 1024 * 1024;
/// Bytes needed to tell an SSE stream from one whole body: exactly the length of "data:".
constexpr int kSseSniffBytes = 5;

/// @brief Rebuild the single envelope parseExplanations() checks, out of what a stream delivered.
///
/// That envelope never exists on the wire; it is what lets the finished-answer contract -- and
/// with it the cache, the known store and the statistics -- run unchanged over a stream. A
/// stream that ended without a `finish_reason` leaves it empty, which is the failure the
/// whole-body path already reports for a truncated answer.
QByteArray streamEnvelope(QString const& content, QString const& finishReason, QJsonObject const& usage)
{
    QJsonObject envelope{{"choices", QJsonArray{QJsonObject{{"finish_reason", finishReason}, {"message", QJsonObject{{"content", content}}}}}}};
    if (not usage.isEmpty())
        envelope.insert(QStringLiteral("usage"), usage);
    return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

/// @return Whether a preview has anything the card would draw. The first chunks of an answer are
///         `{` and `"resul` and the like, and an empty card appearing before any text is not the
///         point of a stream.
bool hasAnythingToDraw(Explanation const& partial)
{
    return not(partial.title.isEmpty() and partial.ipa.isEmpty() and partial.en.isEmpty() and
               partial.zh.isEmpty() and partial.translation.isEmpty() and partial.etymology.isEmpty() and
               partial.example.isEmpty() and partial.senses.isEmpty());
}

} // namespace

LlmClient::LlmClient(Config config, QNetworkAccessManager* manager, QObject* parent)
    : QObject(parent),
      config_(std::move(config)),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this))
{
    LENS_TRACE("LlmClient created: model='{}' base='{}'", config_.model.toStdString(), config_.baseUrl.toString().toStdString());
}

void LlmClient::setModel(const QString& model)
{
    LENS_TRACE("LlmClient::setModel: '{}' -> '{}'", config_.model.toStdString(), model.toStdString());
    config_.model = model;
}

void LlmClient::setBaseUrl(const QUrl& baseUrl)
{
    LENS_TRACE("LlmClient::setBaseUrl: '{}' -> '{}'", config_.baseUrl.toString().toStdString(), baseUrl.toString().toStdString());
    config_.baseUrl = baseUrl;
}

void LlmClient::setApiKey(const QString& apiKey)
{
    config_.apiKey = apiKey;
    LENS_TRACE("LlmClient::setApiKey: key replaced (value hidden)");
}

bool LlmClient::setProvider(QString const& provider)
{
    auto const entry = serviceProvider(provider);
    if (entry.isEmpty()) return false;
    config_.requestOverrides = entry.value("requestOverrides").toObject();
    // The provider duty reconciles the current model with this service's cached or seeded list.
    if (provider != QLatin1String("custom"))
        setBaseUrl(QUrl{entry.value("baseUrl").toString()});
    return true;
}

void LlmClient::fetchModels()
{
    // One source, declared in the data, asked for with nothing the reader owns: no key, no auth
    // header, no query. Whatever it answers is classified per provider further up (docs/adr/0020).
    const auto url = QUrl{serviceCatalog().value("modelSource").toObject().value("url").toString()};
    if (not url.isValid() or url.scheme() != QLatin1String("https") or url.host().isEmpty()) {
        LENS_WARN("the model source URL in the catalog is not usable; the lists stay as they are");
        return;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(kModelListTimeoutMs);
    LENS_TRACE("GET {}", url.toString().toStdString());

    QNetworkReply* reply = manager_->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxModelListBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError or (status != 0 and status != 200)) {
            // Not a failure the reader has to act on: the list is a convenience, and the one
            // already known still works.
            LENS_WARN("the model list was not fetched: HTTP {} ({})", status, reply->errorString().toStdString());
            return;
        }
        const QStringList ids = parseModelIds(reply->readAll());
        if (ids.isEmpty()) return;
        emit modelsFetched(ids);
    });
}

void LlmClient::explainWords(ExplainRequest request)
{
    QStringList words = std::move(request.words);
    if (words.isEmpty()) {
        LENS_ERROR("LlmClient::explainWords called with nothing to look up");
        emit failed(tr("There is nothing to look up."));
        return;
    }
    if (words.size() > kMaxWords) {
        LENS_WARN("LlmClient::explainWords: {} entries requested, keeping the first {}", words.size(), kMaxWords);
        words = words.mid(0, kMaxWords);
    }
    if (config_.apiKey.isEmpty()) {
        emit failed(tr("The API key is missing."));
        return;
    }

    QUrl    url  = config_.baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/chat/completions"));

    QNetworkRequest httpRequest(url);
    httpRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    httpRequest.setRawHeader("Authorization", "Bearer " + config_.apiKey.toUtf8());
    httpRequest.setTransferTimeout(kTimeoutMs);

    const Channel channel        = request.channel;
    const QString preset         = presetFor(request);
    const bool    multipleSenses = request.multipleSenses;
    auto const    language       = request.language;
    auto const    model          = config_.model;
    LENS_INFO("explaining {} item(s) on channel '{}' preset '{}' with '{}'", words.size(), channelKey(channel), preset.toStdString(), config_.model.toStdString());
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), kTimeoutMs);

    QNetworkReply* reply =
        manager_->post(httpRequest, buildRequestBody(config_, channel, words, language, preset, request.etymology, request.example));

    // One stream's state, shared by the two handlers below. A reply that answers with a whole body
    // rather than a stream -- which is what every offline seam and a buffering proxy do -- leaves
    // `sse` false and takes the original single-body path at the end.
    struct Stream {
        QByteArray  buffer;  ///< Bytes that have not framed into a complete event yet.
        QString     content; ///< The model's answer as it has arrived.
        QString     finishReason;
        QJsonObject usage;
        Explanation last; ///< The preview last emitted, so a chunk that changes none of it is silent.
        bool        sse     = false;
        bool        decided = false;
    };
    auto stream = std::make_shared<Stream>();

    /// Fold the framed events out of @p buffer into the answer so far.
    /// @return Whether any text arrived, which is when the preview is worth looking at again.
    const auto consume = [stream](QByteArray& buffer) {
        bool grew = false;
        for (const auto& payload : takeSsePayloads(buffer)) {
            const auto chunk = parseStreamDelta(payload);
            if (not chunk)
                continue;
            stream->content += chunk->content;
            grew = grew or not chunk->content.isEmpty();
            if (not chunk->finishReason.isEmpty())
                stream->finishReason = chunk->finishReason;
            if (not chunk->usage.isEmpty())
                stream->usage = chunk->usage;
        }
        return grew;
    };

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, stream, channel, consume] {
        stream->buffer += reply->readAll();
        // Decided once there are enough bytes to tell the two shapes apart: a first read of "da" must
        // not settle it, or a stream split across reads would be judged a whole body and fail.
        if (not stream->decided and stream->buffer.size() >= kSseSniffBytes) {
            stream->decided = true;
            stream->sse     = stream->buffer.startsWith("data:");
        }
        // An error body is not a stream, and a stream is the reader's only while it is arriving:
        // anything else is left to the finished handler, which owns today's failure notices.
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200 or not stream->sse)
            return;
        if (not consume(stream->buffer))
            return;
        auto preview = parsePartialExplanation(channel, stream->content);
        if (not hasAnythingToDraw(preview) or preview == stream->last)
            return;
        stream->last = preview;
        emit partial(preview);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, stream, words, channel, language, multipleSenses, model, consume] {
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && (status == 0 || status == 200)) {
            // Qt can retain a successful status after the stream fails. In that case the body is
            // incomplete and must not be passed to the JSON/schema validator.
            LENS_ERROR("request failed before a response: {}", reply->errorString().toStdString());
            emit failed(tr("The request could not reach the service: %1").arg(reply->errorString()));
            return;
        }
        if (status == 0) { // no status code at all: the transfer itself never completed
            LENS_ERROR("request failed before a response: {}", reply->errorString().toStdString());
            emit failed(tr("The request could not reach the service: %1").arg(reply->errorString()));
            return;
        }
        if (status != 200) {
            LENS_ERROR("request failed with HTTP {}", status);
            emit failed(httpErrorFor(status));
            return;
        }

        stream->buffer += reply->readAll();
        // A reply whose whole body arrived in one read: too short to be a stream, it is a body.
        if (not stream->decided)
            stream->sse = stream->buffer.size() >= kSseSniffBytes and stream->buffer.startsWith("data:");

        QByteArray body;
        if (stream->sse) {
            consume(stream->buffer); // the last event may have arrived without its terminator
            body = streamEnvelope(stream->content, stream->finishReason, stream->usage);
        } else {
            body = stream->buffer;
        }

        const auto parsed = parseExplanations(channel, body, words, language, multipleSenses);
        if (const auto* message = std::get_if<QString>(&parsed)) {
            emit failed(*message);
            return;
        }
        LENS_INFO("received {} explanation(s)", words.size());
        auto usage  = parseUsage(body);
        usage.model = model;
        emit batchFinished(std::get<QVector<Explanation>>(parsed), usage);
    });
}

}
