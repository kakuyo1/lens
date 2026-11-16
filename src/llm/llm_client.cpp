/**
 * @file llm_client.cpp
 * @brief Request builders and the round trips behind llm_client.h.
 */

#include "llm_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>

#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

#include "llm_pure.h"
#include "util/log.h"

namespace lens::llm {
namespace {

constexpr int    kMaxWords          = 20; ///< Contract limit.
constexpr qint64 kMaxModelListBytes = static_cast<const qint64>(2 * 1024) * 1024;
/// A feature answer larger than this is not one this client asked for: a stuck or hostile reply
/// is cut off rather than buffered without bound.
constexpr qint64 kMaxResponseBytes = static_cast<qint64>(4 * 1024) * 1024;

} // namespace

LlmClient::LlmClient(Config config, QNetworkAccessManager* manager, Timeouts timeouts, QObject* parent)
    : QObject(parent),
      config_(std::move(config)),
      manager_(manager != nullptr ? manager : new QNetworkAccessManager(this)),
      timeouts_(timeouts)
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
    // A key in hand is the reader saying this service answers for them, so the service is asked
    // what it carries: the shared catalogue is the industry's list, and it keeps names the vendor
    // never opened at this endpoint. The catalogue stays the fallback for a service that lists
    // nothing, and for the reader who has not pasted a key yet (docs/adr/0137).
    if (not config_.apiKey.isEmpty() and config_.baseUrl.isValid() and config_.baseUrl.scheme() == QLatin1String("https") and not config_.baseUrl.host().isEmpty()) {
        askServiceModels();
        return;
    }
    askSharedSourceModels();
}

void LlmClient::askServiceModels()
{
    QUrl    url  = config_.baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/models"));

    QNetworkRequest request(url);
    request.setTransferTimeout(timeouts_.modelListMs);
    request.setRawHeader("Authorization", "Bearer " + config_.apiKey.toUtf8());
    LENS_TRACE("GET {} (key hidden)", url.toString().toStdString());

    QNetworkReply* reply = manager_->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxModelListBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int         status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QStringList ids    = (reply->error() == QNetworkReply::NoError and status == 200)
                                       ? parseServiceModelIds(reply->readAll())
                                       : QStringList{};
        if (ids.isEmpty()) {
            askSharedSourceModels();
            return;
        }
        emit serviceModelsFetched(ids);
    });
}

void LlmClient::askSharedSourceModels()
{
    // One source, declared in the data, asked for with nothing the reader owns: no key, no auth
    // header, no query. Whatever it answers is classified per provider further up (docs/adr/0020).
    const auto url = QUrl{serviceCatalog().value("modelSource").toObject().value("url").toString()};
    if (not url.isValid() or url.scheme() != QLatin1String("https") or url.host().isEmpty()) {
        LENS_WARN("the model source URL in the catalog is not usable; the lists stay as they are");
        return;
    }

    QNetworkRequest request(url);
    request.setTransferTimeout(timeouts_.modelListMs);
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
            LENS_WARN("the model list was not fetched: HTTP {} ({})", status, reply->errorString().toStdString()); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
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
    request.setTransferTimeout(timeouts_.probeMs);

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
            LENS_WARN("the connection probe could not reach the service"); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            emit probeFinished(false, tr("Could not reach the service. Check the address and your connection."));
            return;
        }
        if (status != 200) {
            LENS_WARN("the connection probe answered HTTP {}", status); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            // A 400 at a completion endpoint is the model's name far more often than anything
            // else, and the service's own words say only "malformed" without naming it: the
            // sentence has to point at the one part the reader can change here.
            emit probeFinished(false, status == 400 ? tr("The service refused '%1'. Check the model name against the list it serves.").arg(config_.model) : httpErrorFor(status));
            return;
        }
        // A proxy that answers 200 with a login page is still not a model: the probe accepts
        // only the shape every compatible service answers a completion with.
        const QJsonDocument answer = QJsonDocument::fromJson(reply->readAll());
        if (not answer.isObject() or not answer.object().value("choices").isArray()) {
            LENS_WARN("the connection probe answered 200 without a model-shaped body"); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            emit probeFinished(false, tr("The service answered, but not like a model. Check the address."));
            return;
        }
        LENS_INFO("the model service accepted the connection probe"); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
        emit probeFinished(true, tr("Connected. The key and model are accepted."));
    });
}

Handle LlmClient::request(Request request)
{
    const Handle handle{nextId_++};
    std::visit(
        [this, handle](auto&& typed) {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, ExplainRequest>)
                askExplain(handle, std::move(typed));
            else if constexpr (std::is_same_v<T, PeekRequest>)
                askPeek(handle, std::move(typed));
            else if constexpr (std::is_same_v<T, SenseRequest>)
                askSense(handle, std::move(typed));
            else if constexpr (std::is_same_v<T, BreakdownRequest>)
                askBreakdown(handle, std::move(typed));
            else if constexpr (std::is_same_v<T, CorrectionRequest>)
                askCorrection(handle, std::move(typed));
            else if constexpr (std::is_same_v<T, ProfileRequest>)
                askProfile(handle, std::move(typed));
            else
                askStory(handle, std::move(typed));
        },
        std::move(request));
    return handle;
}

void LlmClient::cancel(Handle const handle)
{
    if (QPointer<QNetworkReply> reply = replies_.take(handle.id))
        reply->abort();
}

bool LlmClient::configured(bool const requireHttps) const
{
    if (config_.apiKey.isEmpty())
        return false;
    if (not requireHttps)
        return true;
    return config_.baseUrl.isValid() and config_.baseUrl.scheme() == QLatin1String("https") and not config_.baseUrl.host().isEmpty();
}

void LlmClient::post(Handle const handle, QByteArray const& body, int const timeoutMs, std::function<void(QNetworkReply&)> onRead, std::function<void(QNetworkReply&)> onFinished)
{
    QUrl    url  = config_.baseUrl;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/chat/completions"));

    QNetworkRequest httpRequest(url);
    httpRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    httpRequest.setRawHeader("Authorization", "Bearer " + config_.apiKey.toUtf8());
    httpRequest.setTransferTimeout(timeoutMs);
    LENS_TRACE("POST {} (timeout {} ms, key hidden)", url.toString().toStdString(), timeoutMs);

    QNetworkReply* reply = manager_->post(httpRequest, body);
    replies_.insert(handle.id, reply);

    connect(reply, &QNetworkReply::downloadProgress, this, [reply](qint64 received, qint64) {
        if (received > kMaxResponseBytes)
            reply->abort();
    });
    if (onRead) {
        connect(reply, &QNetworkReply::readyRead, this, [reply, onRead = std::move(onRead)] {
            onRead(*reply);
        });
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, handle, onFinished = std::move(onFinished)] {
        reply->deleteLater();
        // A cancelled request left the register before its finish ran; nobody waits for this one.
        if (replies_.remove(handle.id) == 0)
            return;
        onFinished(*reply);
    });
}

void LlmClient::askExplain(Handle const handle, ExplainRequest request)
{
    QStringList words = std::move(request.words);
    if (words.isEmpty()) {
        LENS_ERROR("LlmClient::request: called with nothing to look up");
        answer(handle, ExplainAnswer{.failure = tr("There is nothing to look up.")});
        return;
    }
    if (words.size() > kMaxWords) {
        LENS_WARN("LlmClient::request: {} entries requested, keeping the first {}", words.size(), kMaxWords);
        words = words.mid(0, kMaxWords);
    }
    if (not configured(/*requireHttps=*/false)) {
        answer(handle, ExplainAnswer{.failure = tr("The API key is missing.")});
        return;
    }

    const Channel channel        = request.channel;
    const QString preset         = presetFor(request);
    const bool    multipleSenses = request.multipleSenses;
    auto const    language       = request.language;
    auto const    model          = config_.model;
    LENS_INFO("explaining {} item(s) on channel '{}' preset '{}' with '{}'", words.size(), channelKey(channel), preset.toStdString(), config_.model.toStdString());

    // The whole stream lives in the pure assembler: the handlers below only feed it bytes and
    // forward what it says is ready. A reply that answers with one body rather than a stream --
    // which is what every offline seam and a buffering proxy do -- falls back inside it.
    auto assembler = std::make_shared<StreamAssembler>(channel, words, language, multipleSenses);

    post(handle, buildRequestBody(config_, channel, words, language, preset, request.etymology, request.example), timeouts_.explainMs, [this, handle, assembler](QNetworkReply& reply) {
             const bool httpOk = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
             if (auto preview = assembler->feed(reply.readAll(), httpOk))
                 emit partial(handle, *preview); }, [this, handle, assembler, model](QNetworkReply& reply) {
             const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
             if (reply.error() != QNetworkReply::NoError && (status == 0 || status == 200)) {
                 // Qt can retain a successful status after the stream fails. In that case the body is
                 // incomplete and must not be passed to the JSON/schema validator.
                 LENS_ERROR("request failed before a response: {}", reply.errorString().toStdString()); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
                 answer(handle, ExplainAnswer{.failure = tr("The request could not reach the service: %1").arg(reply.errorString())});
                 return;
             }
             if (status == 0) {                                                                          // no status code at all: the transfer itself never completed
                 LENS_ERROR("request failed before a response: {}", reply.errorString().toStdString()); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
                 answer(handle, ExplainAnswer{.failure = tr("The request could not reach the service: %1").arg(reply.errorString())});
                 return;
             }
             if (status != 200) {
                 LENS_ERROR("request failed with HTTP {}", status); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
                 answer(handle, ExplainAnswer{.failure = httpErrorFor(status)});
                 return;
             }

             const auto parsed = assembler->finish(reply.readAll());
             if (const auto* message = std::get_if<QString>(&parsed)) {
                 answer(handle, ExplainAnswer{.failure = *message});
                 return;
             }
             const auto& explanations = std::get<QVector<Explanation>>(parsed);
             LENS_INFO("received {} explanation(s)", explanations.size()); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
             auto usage  = assembler->usage();
             usage.model = model;
             answer(handle, ExplainAnswer{.results = explanations, .usage = usage}); });
}

void LlmClient::askPeek(Handle const handle, PeekRequest request)
{
    if (request.lemma.isEmpty() or not configured(/*requireHttps=*/true)) {
        // Quietly: the peek is a gesture on a card the reader is already reading, and a missing
        // key or address is the settings page's to explain, not the globe's.
        LENS_DEBUG("the peek for '{}' went nowhere: the client is not configured", request.lemma.toStdString());
        answer(handle, PeekAnswer{.lemma = request.lemma});
        return;
    }

    // The wire payload: the word alone, or the word and a line naming the sense the card is
    // showing -- a hold on a swapped sense asks for that sense's line, not the word's most
    // common one.
    const QString content = request.sense.isEmpty()
                                ? request.lemma
                                : request.lemma + QLatin1Char('\n') + QStringLiteral("sense: ") + request.sense;
    LENS_INFO("peeking '{}' in '{}' with '{}'", request.lemma.toStdString(), request.language.toStdString(), config_.model.toStdString());
    const QString lemma = request.lemma;
    const auto    model = config_.model;

    post(handle, buildRequestBody(config_, Channel::Peek, {content}, request.language), timeouts_.peekMs, {}, [this, handle, lemma, model](QNetworkReply& reply) {
        const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // A 200 that failed mid-transfer carries an incomplete body, which parseExplanations()
        // rejects like any other malformed answer -- the same quiet failure either way.
        if (status != 200) {
            LENS_WARN("the peek for '{}' came back with HTTP {}", lemma.toStdString(), status); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            answer(handle, PeekAnswer{.lemma = lemma});
            return;
        }
        const QByteArray body   = reply.readAll();
        const auto       parsed = parseExplanations(Channel::Peek, body, {lemma});
        if (const auto* message = std::get_if<QString>(&parsed)) {
            LENS_WARN("the peek for '{}' was refused: {}", lemma.toStdString(), message->toStdString()); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            answer(handle, PeekAnswer{.lemma = lemma});
            return;
        }
        const auto& results = std::get<QVector<Explanation>>(parsed);
        auto        usage   = parseUsage(body);
        usage.model         = model;
        answer(handle, PeekAnswer{.lemma = lemma, .line = results.isEmpty() ? QString{} : results.front().en, .usage = usage});
    });
}

void LlmClient::askSense(Handle const handle, SenseRequest request)
{
    if (request.lemma.isEmpty() or request.input.isEmpty() or not configured(/*requireHttps=*/true)) {
        // Quietly, like the peek: the entry lives on a card the reader is already reading, and a
        // missing key or address is the settings page's to explain, not the entry's.
        LENS_DEBUG("the next sense for '{}' went nowhere: the client is not configured", request.lemma.toStdString());
        answer(handle, SenseAnswer{.lemma = request.lemma});
        return;
    }

    LENS_INFO("asking for the next sense of '{}' in '{}' with '{}'", request.lemma.toStdString(), request.language.toStdString(), config_.model.toStdString());
    const QString lemma = request.lemma;
    const auto    model = config_.model;

    post(handle, buildRequestBody(config_, Channel::Sense, {request.input}, request.language), timeouts_.senseMs, {}, [this, handle, lemma, model](QNetworkReply& reply) {
        const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            LENS_WARN("the next sense for '{}' came back with HTTP {}", lemma.toStdString(), status); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            answer(handle, SenseAnswer{.lemma = lemma});
            return;
        }
        // The body rides the same parseExplanations() checks every channel's answer does; the
        // sense channel's schema lets the senses array be empty, which is the exhaustion answer.
        const QByteArray body   = reply.readAll();
        const auto       parsed = parseExplanations(Channel::Sense, body, {lemma});
        if (const auto* message = std::get_if<QString>(&parsed)) {
            LENS_WARN("the next sense for '{}' was refused: {}", lemma.toStdString(), message->toStdString()); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            answer(handle, SenseAnswer{.lemma = lemma});
            return;
        }
        const auto& results = std::get<QVector<Explanation>>(parsed);
        auto        usage   = parseUsage(body);
        usage.model         = model;
        answer(handle, SenseAnswer{.lemma = lemma, .answer = results.isEmpty() ? Explanation{} : results.front(), .usage = usage});
    });
}

void LlmClient::askBreakdown(Handle const handle, BreakdownRequest request)
{
    if (request.words.isEmpty() or not configured(/*requireHttps=*/true)) {
        LENS_DEBUG("the breakdown went nowhere: the client is not configured");
        answer(handle, BreakdownAnswer{.sentence = request.sentence, .failure = failureLineFor(-1)});
        return;
    }

    LENS_INFO("breaking down a {}-word sentence with '{}'", request.words.size(), config_.model.toStdString());
    const QString sentence = request.sentence;
    const int     count    = request.words.size();
    const auto    model    = config_.model;

    post(handle, buildRequestBody(config_, Channel::Breakdown, breakdownLines(request.words), QStringLiteral("en")), timeouts_.explainMs, {}, [this, handle, sentence, count, model](QNetworkReply& reply) {
        const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            LENS_WARN("the breakdown came back with HTTP {}", status); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            answer(handle, BreakdownAnswer{.sentence = sentence, .failure = failureLineFor(status)});
            return;
        }
        // A refused answer was still paid for, so its usage goes out with the empty result.
        const QByteArray body  = reply.readAll();
        auto             usage = parseUsage(body);
        usage.model            = model;
        const auto parsed      = parseBreakdown(body, count);
        if (const auto* message = std::get_if<QString>(&parsed)) {
            LENS_WARN("the breakdown was refused: {}", message->toStdString()); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            answer(handle, BreakdownAnswer{.sentence = sentence, .usage = usage, .failure = failureLineFor(status)});
            return;
        }
        answer(handle, BreakdownAnswer{.sentence = sentence, .spans = std::get<QVector<RoleSpan>>(parsed), .usage = usage});
    });
}

void LlmClient::askCorrection(Handle const handle, CorrectionRequest request)
{
    const Channel channel = request.channel;
    if (request.text.isEmpty() or not configured(/*requireHttps=*/true)) {
        LENS_DEBUG("the {} correction went nowhere: the client is not configured", channelKey(channel));
        answer(handle, CorrectionAnswer{.text = request.text, .channel = channel, .failure = failureLineFor(-1)});
        return;
    }

    LENS_INFO("correcting a {} with '{}'", channelKey(channel), config_.model.toStdString());
    const QString text  = request.text;
    const auto    model = config_.model;

    post(handle, buildRequestBody(config_, channel, {text}, QStringLiteral("en")), timeouts_.explainMs, {}, [this, handle, text, channel, model](QNetworkReply& reply) {
        const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            LENS_WARN("the {} correction came back with HTTP {}", channelKey(channel), status); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            answer(handle, CorrectionAnswer{.text = text, .channel = channel, .failure = failureLineFor(status)});
            return;
        }
        // A refused answer was still paid for, so its usage goes out with the empty correction.
        const QByteArray body  = reply.readAll();
        auto             usage = parseUsage(body);
        usage.model            = model;
        const auto parsed      = parseCorrection(channel, body);
        if (const auto* corrected = std::get_if<QString>(&parsed)) {
            answer(handle, CorrectionAnswer{.text = text, .channel = channel, .corrected = *corrected, .usage = usage});
            return;
        }
        answer(handle, CorrectionAnswer{.text = text, .channel = channel, .usage = usage, .failure = failureLineFor(status)});
    });
}

void LlmClient::askProfile(Handle const handle, ProfileRequest request)
{
    if (request.summary.isEmpty() or not configured(/*requireHttps=*/true)) {
        LENS_DEBUG("the reading profile went nowhere: the client is not configured");
        answer(handle, ProfileAnswer{.failure = failureLineFor(-1)});
        return;
    }

    LENS_INFO("interpreting a reading profile with '{}'", config_.model.toStdString());
    const auto model = config_.model;

    post(handle, buildRequestBody(config_, Channel::Profile, {request.summary}, request.language), timeouts_.explainMs, {}, [this, handle, model](QNetworkReply& reply) {
        const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            LENS_WARN("the reading profile came back with HTTP {}", status); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            answer(handle, ProfileAnswer{.failure = failureLineFor(status)});
            return;
        }
        // A refused answer was still paid for, so its usage goes out with the empty result.
        const QByteArray body  = reply.readAll();
        auto             usage = parseUsage(body);
        usage.model            = model;
        const auto parsed      = parseProfile(body);
        if (std::holds_alternative<QString>(parsed)) {
            answer(handle, ProfileAnswer{.usage = usage, .failure = failureLineFor(status)});
            return;
        }
        answer(handle, ProfileAnswer{.points = std::get<QVector<ProfileLine>>(parsed), .usage = usage});
    });
}

void LlmClient::askStory(Handle const handle, StoryRequest request)
{
    if (request.words.isEmpty() or not configured(/*requireHttps=*/true)) {
        LENS_DEBUG("the story went nowhere: the client is not configured");
        answer(handle, StoryAnswer{.failure = failureLineFor(-1)});
        return;
    }

    LENS_INFO("writing a story from {} word(s) with '{}'", request.words.size(), config_.model.toStdString());
    const auto model = config_.model;

    post(handle, buildRequestBody(config_, Channel::Story, request.words, QStringLiteral("en")), timeouts_.explainMs, {}, [this, handle, model, words = request.words](QNetworkReply& reply) {
        const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200) {
            LENS_WARN("the story came back with HTTP {}", status); // NOLINT(bugprone-lambda-function-name): the log macro names the enclosing function; inside this lambda that is operator(), which is the function the line belongs to
            answer(handle, StoryAnswer{.failure = failureLineFor(status)});
            return;
        }
        // A refused answer was still paid for, so its usage goes out with the empty result.
        const QByteArray body  = reply.readAll();
        auto             usage = parseUsage(body);
        usage.model            = model;
        const auto parsed      = parseStory(body, words);
        if (std::holds_alternative<QString>(parsed)) {
            answer(handle, StoryAnswer{.usage = usage, .failure = failureLineFor(status)});
            return;
        }
        answer(handle, StoryAnswer{.story = std::get<Story>(parsed), .usage = usage});
    });
}

}
