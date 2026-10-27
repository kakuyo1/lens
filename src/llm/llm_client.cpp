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
/// A probe should answer while the reader is still looking at the button.
constexpr int kProbeTimeoutMs = 15000;
/// A peek answers a reader who is holding a button: it is small and it should not outlast the
/// patience a held gesture has.
constexpr int kPeekTimeoutMs = 15000;
/// The sense channel answers a click on the card it belongs to: small like the peek, and the
/// same ceiling keeps a slow service from outlasting the reader's attention.
constexpr int kSenseTimeoutMs = 15000;

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

void LlmClient::probeConnection()
{
    if (config_.apiKey.isEmpty()) {
        emit probeFinished(false, tr("Enter an API key first."));
        return;
    }
    if (config_.model.isEmpty()) {
        emit probeFinished(false, tr("Choose a model first."));
        return;
    }
    if (not config_.baseUrl.isValid() or config_.baseUrl.scheme() != QLatin1String("https") or config_.baseUrl.host().isEmpty()) {
        emit probeFinished(false, tr("Enter a service address that starts with https://."));
        return;
    }

    QUrl    url  = config_.baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/chat/completions"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + config_.apiKey.toUtf8());
    request.setTransferTimeout(kProbeTimeoutMs);

    // No prompt, no schema, no stream: this is the smallest request that still makes the
    // service look at the endpoint, the key and the model together.
    QJsonObject body{
        {"model", config_.model},
        {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", "ping"}}}},
        {"max_tokens", 1},
        {"stream", false},
    };
    auto overrides = config_.requestOverrides;
    if (overrides.isEmpty()) {
        for (auto const& value : serviceCatalog().value("providers").toArray()) {
            auto const provider = value.toObject();
            if (provider.value("baseUrl").toString() == config_.baseUrl.toString())
                overrides = provider.value("requestOverrides").toObject();
        }
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (it.value().isNull())
            body.remove(it.key());
        else
            body.insert(it.key(), it.value());
    }

    LENS_INFO("testing the model service connection with '{}'", config_.model.toStdString());
    LENS_TRACE("connection probe: POST to the configured endpoint (key hidden)");

    QNetworkReply* reply = manager_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // An HTTP answer is the service speaking, even when Qt also marks the reply failed;
        // only no status at all, or a 200 that failed mid-transfer, is a reach failure.
        if (status == 0 or (reply->error() != QNetworkReply::NoError and status == 200)) {
            LENS_WARN("the connection probe could not reach the service");
            emit probeFinished(false, tr("Could not reach the service. Check the address and your connection."));
            return;
        }
        if (status != 200) {
            LENS_WARN("the connection probe answered HTTP {}", status);
            emit probeFinished(false, httpErrorFor(status));
            return;
        }
        // A proxy that answers 200 with a login page is still not a model: the probe accepts
        // only the shape every compatible service answers a completion with.
        const QJsonDocument answer = QJsonDocument::fromJson(reply->readAll());
        if (not answer.isObject() or not answer.object().value("choices").isArray()) {
            LENS_WARN("the connection probe answered 200 without a model-shaped body");
            emit probeFinished(false, tr("The service answered, but not like a model. Check the address."));
            return;
        }
        LENS_INFO("the model service accepted the connection probe");
        emit probeFinished(true, tr("Connected. The key and model are accepted."));
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

    // The whole stream lives in the pure assembler: the handlers below only feed it bytes and
    // forward what it says is ready. A reply that answers with one body rather than a stream --
    // which is what every offline seam and a buffering proxy do -- falls back inside it.
    auto assembler = std::make_shared<StreamAssembler>(channel, words, language, multipleSenses);

    connect(reply, &QNetworkReply::readyRead, this, [this, reply, assembler] {
        const bool httpOk = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        if (auto preview = assembler->feed(reply->readAll(), httpOk))
            emit partial(*preview);
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, assembler, model] {
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

        const auto parsed = assembler->finish(reply->readAll());
        if (const auto* message = std::get_if<QString>(&parsed)) {
            emit failed(*message);
            return;
        }
        const auto& explanations = std::get<QVector<Explanation>>(parsed);
        LENS_INFO("received {} explanation(s)", explanations.size());
        auto usage  = assembler->usage();
        usage.model = model;
        emit batchFinished(explanations, usage);
    });
}

void LlmClient::peekWord(QString const& lemma, QString const& language, QString const& sense)
{
    if (lemma.isEmpty() or config_.apiKey.isEmpty() or not config_.baseUrl.isValid() or
        config_.baseUrl.scheme() != QLatin1String("https") or config_.baseUrl.host().isEmpty()) {
        // Quietly: the peek is a gesture on a card the reader is already reading, and a missing
        // key or address is the settings page's to explain, not the globe's.
        LENS_DEBUG("the peek for '{}' went nowhere: the client is not configured", lemma.toStdString());
        emit peekAnswered(lemma, {}, {});
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
    httpRequest.setTransferTimeout(kPeekTimeoutMs);

    const auto model = config_.model;
    // The wire payload: the word alone, or the word and a line naming the sense the card is
    // showing -- a hold on a swapped sense asks for that sense's line, not the word's most
    // common one.
    const QString content = sense.isEmpty()
                                ? lemma
                                : lemma + QLatin1Char('\n') + QStringLiteral("sense: ") + sense;
    LENS_INFO("peeking '{}' in '{}' with '{}'", lemma.toStdString(), language.toStdString(), config_.model.toStdString());
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), kPeekTimeoutMs);

    QNetworkReply* reply =
        manager_->post(httpRequest, buildRequestBody(config_, Channel::Peek, {content}, language));

    connect(reply, &QNetworkReply::finished, this, [this, reply, lemma, model] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // A 200 that failed mid-transfer carries an incomplete body, which parseExplanations()
        // rejects like any other malformed answer -- the same quiet failure either way.
        if (status != 200) {
            LENS_WARN("the peek for '{}' came back with HTTP {}", lemma.toStdString(), status);
            emit peekAnswered(lemma, {}, {});
            return;
        }
        const QByteArray body   = reply->readAll();
        const auto       parsed = parseExplanations(Channel::Peek, body, {lemma});
        if (const auto* message = std::get_if<QString>(&parsed)) {
            LENS_WARN("the peek for '{}' was refused: {}", lemma.toStdString(), message->toStdString());
            emit peekAnswered(lemma, {}, {});
            return;
        }
        const auto& results = std::get<QVector<Explanation>>(parsed);
        auto        usage   = parseUsage(body);
        usage.model         = model;
        emit peekAnswered(lemma, results.isEmpty() ? QString{} : results.front().en, usage);
    });
}

void LlmClient::nextSenseWord(QString const& lemma, QString const& input, QString const& language)
{
    if (lemma.isEmpty() or input.isEmpty() or config_.apiKey.isEmpty() or not config_.baseUrl.isValid() or
        config_.baseUrl.scheme() != QLatin1String("https") or config_.baseUrl.host().isEmpty()) {
        // Quietly, like the peek: the entry lives on a card the reader is already reading, and a
        // missing key or address is the settings page's to explain, not the entry's.
        LENS_DEBUG("the next sense for '{}' went nowhere: the client is not configured", lemma.toStdString());
        emit senseAnswered(lemma, {}, {});
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
    httpRequest.setTransferTimeout(kSenseTimeoutMs);

    const auto model = config_.model;
    LENS_INFO("asking for the next sense of '{}' in '{}' with '{}'", lemma.toStdString(), language.toStdString(), config_.model.toStdString());
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), kSenseTimeoutMs);

    QNetworkReply* reply =
        manager_->post(httpRequest, buildRequestBody(config_, Channel::Sense, {input}, language));

    connect(reply, &QNetworkReply::finished, this, [this, reply, lemma, model] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            LENS_WARN("the next sense for '{}' came back with HTTP {}", lemma.toStdString(), status);
            emit senseAnswered(lemma, {}, {});
            return;
        }
        // The body rides the same parseExplanations() checks every channel's answer does; the
        // sense channel's schema lets the senses array be empty, which is the exhaustion answer.
        const QByteArray body   = reply->readAll();
        const auto       parsed = parseExplanations(Channel::Sense, body, {lemma});
        if (const auto* message = std::get_if<QString>(&parsed)) {
            LENS_WARN("the next sense for '{}' was refused: {}", lemma.toStdString(), message->toStdString());
            emit senseAnswered(lemma, {}, {});
            return;
        }
        const auto& results = std::get<QVector<Explanation>>(parsed);
        auto        usage   = parseUsage(body);
        usage.model         = model;
        emit senseAnswered(lemma, results.isEmpty() ? Explanation{} : results.front(), usage);
    });
}

void LlmClient::correctSpelling(QString const& text)
{
    postCorrection(Channel::Spell, text);
}

void LlmClient::correctGrammar(QString const& text)
{
    postCorrection(Channel::Grammar, text);
}

void LlmClient::postCorrection(Channel const channel, QString const& text)
{
    if (text.isEmpty() or config_.apiKey.isEmpty() or not config_.baseUrl.isValid() or
        config_.baseUrl.scheme() != QLatin1String("https") or config_.baseUrl.host().isEmpty()) {
        LENS_DEBUG("the {} correction went nowhere: the client is not configured", channelKey(channel));
        emit correctionAnswered(text, channel, {}, {}, failureLineFor(-1));
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

    const auto model = config_.model;
    LENS_INFO("correcting a {} with '{}'", channelKey(channel), config_.model.toStdString());
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), kTimeoutMs);

    QNetworkReply* reply = manager_->post(httpRequest, buildRequestBody(config_, channel, {text}, QStringLiteral("en")));

    connect(reply, &QNetworkReply::finished, this, [this, reply, text, channel, model] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            LENS_WARN("the {} correction came back with HTTP {}", channelKey(channel), status);
            emit correctionAnswered(text, channel, {}, {}, failureLineFor(status));
            return;
        }
        // A refused answer was still paid for, so its usage goes out with the empty correction.
        const QByteArray body  = reply->readAll();
        auto             usage = parseUsage(body);
        usage.model            = model;
        const auto parsed      = parseCorrection(channel, body);
        if (const auto* corrected = std::get_if<QString>(&parsed)) {
            emit correctionAnswered(text, channel, *corrected, usage, {});
            return;
        }
        emit correctionAnswered(text, channel, {}, usage, failureLineFor(status));
    });
}

void LlmClient::breakdownSentence(QString const& sentence, QStringList const& words)
{
    if (words.isEmpty() or config_.apiKey.isEmpty() or not config_.baseUrl.isValid() or
        config_.baseUrl.scheme() != QLatin1String("https") or config_.baseUrl.host().isEmpty()) {
        LENS_DEBUG("the breakdown went nowhere: the client is not configured");
        emit breakdownAnswered(sentence, {}, {}, failureLineFor(-1));
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

    const auto model = config_.model;
    LENS_INFO("breaking down a {}-word sentence with '{}'", words.size(), config_.model.toStdString());
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), kTimeoutMs);

    QNetworkReply* reply = manager_->post(httpRequest, buildRequestBody(config_, Channel::Breakdown, words, QStringLiteral("en")));

    connect(reply, &QNetworkReply::finished, this, [this, reply, sentence, count = words.size(), model] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            LENS_WARN("the breakdown came back with HTTP {}", status);
            emit breakdownAnswered(sentence, {}, {}, failureLineFor(status));
            return;
        }
        // A refused answer was still paid for, so its usage goes out with the empty result.
        const QByteArray body  = reply->readAll();
        auto             usage = parseUsage(body);
        usage.model            = model;
        const auto parsed      = parseBreakdown(body, static_cast<int>(count));
        if (const auto* message = std::get_if<QString>(&parsed)) {
            LENS_WARN("the breakdown was refused: {}", message->toStdString());
            emit breakdownAnswered(sentence, {}, usage, failureLineFor(status));
            return;
        }
        emit breakdownAnswered(sentence, std::get<QVector<RoleSpan>>(parsed), usage, {});
    });
}

}
