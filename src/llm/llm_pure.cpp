/**
 * @file llm_pure.cpp
 * @brief Prompt rendering, response checks, and usage parsing behind llm_pure.h, with no network.
 */

#include "llm_pure.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

#include "util/log.h"

namespace lens::llm {
namespace {

// Reader-facing messages go through QCoreApplication::translate with both the context and the
// text written out as literals at every site. lupdate extracts literal arguments only, so a
// helper taking the context as a parameter would keep the strings out of the .ts files.

/// @brief Render a channel's system prompt with the output-language line filled in.
QString renderSystemPrompt(Channel channel, const QString& explanationLang, const QString& preset, bool const etymology, bool const example)
{
    const PromptTemplate& tmpl = promptTemplate(channel, preset);

    QString line = tmpl.outputLanguageLine.value(explanationLang);
    if (line.isEmpty()) {
        LENS_WARN("unknown explanation language '{}'; using 'en'", explanationLang.toStdString());
        line = tmpl.outputLanguageLine.value(QStringLiteral("en"));
    }

    QString prompt = tmpl.systemPromptTemplate;
    prompt.replace(QLatin1String(kOutputLanguagePlaceholder), line);
    // Off clears both placeholders together: the sentence and the field it names travel, and what
    // is left is the prompt as it read before the setting existed. A template that never carried
    // them is untouched, which is what keeps entity and sentence out of this.
    prompt.replace(QLatin1String(kEtymologyNotePlaceholder), etymology ? tmpl.etymologyNote : QString{});
    prompt.replace(QLatin1String(kEtymologyFieldPlaceholder), etymology ? tmpl.etymologyField : QString{});
    prompt.replace(QLatin1String(kExampleNotePlaceholder), example ? tmpl.exampleNote : QString{});
    prompt.replace(QLatin1String(kExampleFieldPlaceholder), example ? tmpl.exampleField : QString{});
    return prompt;
}

/// @return The JSON object bytes inside a model's content, tolerating a surrounding markdown
/// fence or a line of prose: the model is asked for bare JSON, but wrapped answers are common.
QByteArray extractJsonObject(const QString& content)
{
    QString text = content.trimmed();
    if (text.startsWith(QLatin1String("```"))) {
        const int newline = text.indexOf(QLatin1Char('\n'));
        if (newline >= 0)
            text = text.mid(newline + 1);
        const int fence = text.lastIndexOf(QLatin1String("```"));
        if (fence >= 0)
            text = text.left(fence);
        text = text.trimmed();
    }
    if (!text.startsWith(QLatin1Char('{'))) {
        const int open  = text.indexOf(QLatin1Char('{'));
        const int close = text.lastIndexOf(QLatin1Char('}'));
        if (open >= 0 && close > open)
            text = text.mid(open, close - open + 1);
    }
    return text.toUtf8();
}

/// @brief Validate the schema vocabulary used by the shipped response contracts.
bool matchesSchema(QJsonValue const& value, QJsonObject const& schema)
{
    auto const alternatives = schema.value("anyOf").toArray();
    if (not alternatives.isEmpty()) {
        bool matched = false;
        for (auto const& branch : alternatives)
            matched = matched or matchesSchema(value, branch.toObject());
        if (not matched) return false;
    }
    auto const type = schema.value("type").toString();
    if (type == QLatin1String("string"))
        return value.isString() and value.toString().trimmed().size() >= schema.value("minLength").toInt();
    if (type == QLatin1String("array")) {
        if (not value.isArray() or value.toArray().size() < schema.value("minItems").toInt()) return false;
        for (auto const& item : value.toArray())
            if (not matchesSchema(item, schema.value("items").toObject())) return false;
    }
    if (type == QLatin1String("object") or schema.contains("required")) {
        if (not value.isObject()) return false;
        auto const object = value.toObject();
        for (auto const& field : schema.value("required").toArray())
            if (not object.contains(field.toString())) return false;
        auto const properties = schema.value("properties").toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it)
            if (object.contains(it.key()) and not matchesSchema(object.value(it.key()), it.value().toObject()))
                return false;
    }
    return true;
}

/// @return The index of the quote that closes the JSON string whose opening quote sits at
///         @p quote, or -1 while the string has not closed. A backslash escapes exactly the one
///         character after it -- `\uXXXX` carries no quote among its hex digits -- so a buffer
///         that ends inside an escape reads as unclosed rather than as a false close.
int endOfJsonString(QString const& text, int quote)
{
    for (int i = quote + 1; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\\')) {
            ++i;
            continue;
        }
        if (text.at(i) == QLatin1Char('"'))
            return i;
    }
    return -1;
}

/// @return A JSON string literal's value, quotes included, with its escapes resolved, or empty
///         for a literal no JSON reader accepts -- a value the model has not finished writing.
QString jsonStringValue(QString const& literal)
{
    const auto array = QJsonDocument::fromJson(QStringLiteral("[%1]").arg(literal).toUtf8()).array();
    return array.isEmpty() ? QString{} : array.at(0).toString();
}

int scanSenseArray(QString const& text, int arrayStart, QVector<Sense>& senses);
int scanJsonObject(QString const& text, int objectStart, QHash<QString, QString>& fields, QVector<Sense>& senses);

/// @brief Read the sense objects of a `senses` array as far as they have arrived.
/// @return The index just past the array's `]`, or the end of the text while it has not closed.
int scanSenseArray(QString const& text, int arrayStart, QVector<Sense>& senses)
{
    int i = arrayStart + 1;
    while (true) {
        while (i < text.size() and (text.at(i).isSpace() or text.at(i) == QLatin1Char(',')))
            ++i;
        if (i >= text.size())
            return text.size();
        if (text.at(i) == QLatin1Char(']'))
            return i + 1;
        if (text.at(i) != QLatin1Char('{'))
            return i;

        QHash<QString, QString> entry;
        QVector<Sense>          ignored;
        i = scanJsonObject(text, i, entry, ignored);
        // A sense that has named nothing yet contributes no line to the card; one whose first
        // field has closed does, and grows as the rest arrive.
        if (not entry.isEmpty())
            senses.push_back({entry.value(QStringLiteral("en")),
                              entry.value(QStringLiteral("zh")),
                              entry.value(QStringLiteral("translation"))});
    }
}

/// @brief Read the string fields of the object starting at @p objectStart, as far as they have
///        arrived, folding a `senses` array into @p senses.
/// @return The index just past the object's `}`, or the end of the text while it has not closed.
/// @note A value that is neither a string nor the `senses` array is skipped when it is a scalar --
///       the word channel's `imageable` is a boolean, and stopping on it would hold back every
///       field after it. An array or object under any other key is not a shape the shipped results
///       hold, and still stops the read.
int scanJsonObject(QString const& text, int objectStart, QHash<QString, QString>& fields, QVector<Sense>& senses)
{
    int i = objectStart + 1;
    while (true) {
        while (i < text.size() and (text.at(i).isSpace() or text.at(i) == QLatin1Char(',')))
            ++i;
        if (i >= text.size())
            return text.size();
        if (text.at(i) == QLatin1Char('}'))
            return i + 1;
        if (text.at(i) != QLatin1Char('"'))
            return i;

        const int keyEnd = endOfJsonString(text, i);
        if (keyEnd < 0)
            return text.size();
        const QString key = text.mid(i + 1, keyEnd - i - 1);
        i                 = keyEnd + 1;
        while (i < text.size() and text.at(i).isSpace())
            ++i;
        if (i >= text.size() or text.at(i) != QLatin1Char(':'))
            return i;
        ++i;
        while (i < text.size() and text.at(i).isSpace())
            ++i;
        if (i >= text.size())
            return text.size();

        if (text.at(i) == QLatin1Char('"')) {
            const int valueEnd = endOfJsonString(text, i);
            if (valueEnd < 0)
                return text.size(); // the value has not closed; everything after it waits with it
            fields.insert(key, jsonStringValue(text.mid(i, valueEnd - i + 1)));
            i = valueEnd + 1;
            continue;
        }
        if (text.at(i) == QLatin1Char('[') and key == QLatin1String("senses")) {
            i = scanSenseArray(text, i, senses);
            continue;
        }
        // A scalar the card does not draw -- `true`, a number, `null`. Skipping to the next separator
        // keeps the read going, so a field written after it still arrives mid-stream; a scalar carries
        // no delimiter of its own, so this cannot cut into the value.
        const QChar token = text.at(i);
        if (token.isDigit() or token == QLatin1Char('-') or token == QLatin1Char('t') or
            token == QLatin1Char('f') or token == QLatin1Char('n')) {
            while (i < text.size() and text.at(i) != QLatin1Char(',') and text.at(i) != QLatin1Char('}'))
                ++i;
            continue;
        }
        return i;
    }
}

} // namespace

QString httpErrorFor(int status)
{
    switch (status) {
        case 400:
            return QCoreApplication::translate("lens::llm",
                                               "The request was rejected as malformed (400).");
        case 401:
            return QCoreApplication::translate("lens::llm",
                                               "The API key is missing or not accepted (401).");
        case 402:
            return QCoreApplication::translate("lens::llm",
                                               "The account is out of credit (402).");
        case 422:
            return QCoreApplication::translate("lens::llm",
                                               "The request parameters were rejected (422).");
        case 429:
            return QCoreApplication::translate(
                "lens::llm", "Too many requests; the service is rate-limiting (429).");
        case 500:
            return QCoreApplication::translate("lens::llm",
                                               "The explanation service failed (500).");
        case 503:
            return QCoreApplication::translate("lens::llm",
                                               "The explanation service is overloaded (503).");
        default:
            return QCoreApplication::translate("lens::llm", "Unexpected HTTP status %1.")
                .arg(status);
    }
}

QString maskSensitive(const QString& text)
{
    static const QRegularExpression url(R"(\bhttps?://\S+)");
    static const QRegularExpression email(R"([\w.%+-]+@[\w-]+(?:\.[\w-]+)+)");
    static const QRegularExpression digits(R"(\d{6,})");

    QString masked = text;
    masked.replace(url, QStringLiteral("<url>"));
    masked.replace(email, QStringLiteral("<email>"));
    masked.replace(digits, QStringLiteral("<num>"));

    // Only that something was masked is recorded. The masked text itself never reaches a log.
    if (masked != text)
        LENS_TRACE("maskSensitive: replaced sensitive content in a {}-char input", text.size());
    return masked;
}

QString presetFor(ExplainRequest const& request)
{
    return request.multipleSenses ? QStringLiteral("multiple") : request.preset;
}

QByteArray buildRequestBody(const Config&      config,
                            Channel            channel,
                            const QStringList& inputs,
                            const QString&     explanationLang,
                            const QString&     preset,
                            bool const         etymology,
                            bool const         example)
{
    const RequestTemplate& tmpl = requestTemplate(channel);

    QStringList masked;
    masked.reserve(inputs.size());
    for (const auto& input : inputs)
        masked << maskSensitive(input);

    QJsonObject body{
        {"model", config.model},
        {"messages",
         QJsonArray{
             QJsonObject{{"role", "system"},
                         {"content", renderSystemPrompt(channel, explanationLang, preset, etymology, example)}},
             QJsonObject{{"role", "user"}, {"content", masked.join(QLatin1Char('\n'))}},
         }},
        {"response_format", tmpl.responseFormat},
        {"thinking", tmpl.thinking},
        {"max_tokens", tmpl.maxTokens},
        {"stream", tmpl.stream},
    };
    // An empty block adds nothing: a channel that streams without asking for extra wire options
    // sends the body it always did.
    if (not tmpl.streamOptions.isEmpty())
        body.insert(QStringLiteral("stream_options"), tmpl.streamOptions);
    auto overrides = config.requestOverrides;
    if (overrides.isEmpty()) {
        for (auto const& value : serviceCatalog().value("providers").toArray()) {
            auto const provider = value.toObject();
            if (provider.value("baseUrl").toString() == config.baseUrl.toString())
                overrides = provider.value("requestOverrides").toObject();
        }
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        if (it.value().isNull())
            body.remove(it.key());
        else
            body.insert(it.key(), it.value());
    }

    LENS_TRACE("buildRequestBody: {} item(s) for channel '{}', preset '{}', model='{}', lang='{}'", inputs.size(), channelKey(channel), preset.toStdString(), config.model.toStdString(), explanationLang.toStdString());
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

namespace {

/// @return The reader-facing failure for a finish reason other than "stop", or an empty string.
QString finishReasonError(QString const& finish)
{
    // Only "stop" means the model finished on its own. "length" means the JSON was cut off
    // mid-way, which no amount of parsing can repair.
    if (finish == QLatin1String("stop")) return {};
    return QCoreApplication::translate("lens::llm",
                                       "The model stopped before finishing "
                                       "(reason: %1).")
        .arg(finish.isEmpty() ? QCoreApplication::translate("lens::llm", "absent") : finish);
}

/// @brief Validate the `results` payload out of one answer's content.
std::variant<QVector<Explanation>, QString> parseExplanationContent(Channel            channel,
                                                                    QString const&     content,
                                                                    const QStringList& expectedInputs,
                                                                    QString const&     explanationLang,
                                                                    bool const         multipleSenses)
{
    using Result = std::variant<QVector<Explanation>, QString>;

    /// Reject the whole batch, naming the reason once for both the caller and the log.
    const auto reject = [](const QString& reason) -> Result {
        LENS_ERROR("parseExplanations rejected the response: {}", reason.toStdString());
        return reason;
    };

    const auto payload = QJsonDocument::fromJson(extractJsonObject(content));
    if (!payload.isObject())
        return reject(QCoreApplication::translate("lens::llm",
                                                  "The model's answer is not valid JSON."));

    const auto resultsValue = payload.object().value("results");
    if (!resultsValue.isArray())
        return reject(QCoreApplication::translate(
            "lens::llm", "The model's answer has no results array."));

    // Presence is gated on the response schema, so adding a required field is a data edit.
    const bool    isWord        = channel == Channel::Word;
    const bool    isTranslation = channel == Channel::Translation;
    const QString titleField    = isWord ? QStringLiteral("word") : QStringLiteral("title");

    QVector<Explanation> parsed;
    for (const auto& item : resultsValue.toArray()) {
        if (!item.isObject())
            return reject(QCoreApplication::translate(
                "lens::llm", "One of the results entries is not an object."));
        const auto obj = item.toObject();

        if (not matchesSchema(item, resultSchema(channel)))
            return reject(QCoreApplication::translate("lens::llm", "A results entry does not match the response schema."));

        Explanation e{obj.value(titleField).toString(),
                      obj.value(QStringLiteral("ipa")).toString(),
                      obj.value(QStringLiteral("en")).toString(),
                      obj.value(QStringLiteral("zh")).toString()};
        e.translation = obj.value("translation").toString();
        // Read and never demanded: the origin is asked for only when the setting is on, an acronym
        // has none, and the model is free to answer "unknown". Failing a whole batch over it would
        // throw away the definitions that did arrive, the same trade `ipa` already makes.
        e.etymology = obj.value(QStringLiteral("etymology")).toString();
        // ponytail: English whatever the explanation language, so es and ja readers get an English sentence.
        // Upgrade: ask for it in the explanation language, as the origin does, once readers ask for that.
        e.example = obj.value(QStringLiteral("example")).toString();
        // Read for the definition chain and never demanded: a word with no short label still has its
        // definition, and the card draws no label for it. Two collocations at most, whatever arrives.
        e.signpost = obj.value(QStringLiteral("signpost")).toString();
        for (auto const& value : obj.value(QStringLiteral("collocations")).toArray()) {
            if (e.collocations.size() < 2)
                e.collocations.push_back(value.toString());
        }
        // The picture advice, read the same way and never demanded: a sense counts as
        // imageable only when the model both said so and named the words an image search is run with,
        // so the card's entry and the duty's request agree on what there is to fetch. Blank and absent
        // therefore read alike, and a mistyped value reads as false rather than costing the definition.
        const auto readImageAdvice = [](QJsonObject const& source, bool& imageable, QString& query) {
            query     = source.value(QStringLiteral("imageQuery")).toString().trimmed();
            imageable = source.value(QStringLiteral("imageable")).toBool() and not query.isEmpty();
        };
        readImageAdvice(obj, e.imageable, e.imageQuery);
        if (isWord and obj.contains("senses")) {
            auto const senses = obj.value("senses").toArray();
            for (auto const& value : senses) {
                auto const sense = value.toObject();
                if (explanationLang != QLatin1String("en") and explanationLang != QLatin1String("zh") and sense.value("translation").toString().trimmed().isEmpty())
                    return reject(QCoreApplication::translate("lens::llm", "The requested explanation language is missing."));
                Sense parsedSense{sense.value("en").toString(), sense.value("zh").toString(), sense.value("translation").toString()};
                readImageAdvice(sense, parsedSense.imageable, parsedSense.imageQuery);
                if (e.senses.size() < (multipleSenses ? kMaxSenses : 1))
                    e.senses.push_back(parsedSense);
            }
            e.en          = e.senses.front().en;
            e.zh          = e.senses.front().zh;
            e.translation = e.senses.front().translation;
            // The top-level pair mirrors the most frequent sense, so an answer read through either shape
            // says the same thing about its picture.
            e.imageable  = e.senses.front().imageable;
            e.imageQuery = e.senses.front().imageQuery;
        } else if (isWord) {
            e.senses.push_back({e.en, e.zh, e.translation, e.imageable, e.imageQuery});
        }
        if (not isTranslation and explanationLang != QLatin1String("en") and explanationLang != QLatin1String("zh") and e.translation.trimmed().isEmpty())
            return reject(QCoreApplication::translate("lens::llm", "The requested explanation language is missing."));
        // Presence is the schema's business; emptiness is not. Both language definitions are
        // required by every channel but the translation one, which has no explanation language to
        // follow and so no second line to be empty. A word must also name itself (its IPA stays
        // optional: an acronym has none); the other channels carry no echoed title.
        if (e.en.trimmed().isEmpty() || (not isTranslation and e.zh.trimmed().isEmpty()) || (isWord && e.title.trimmed().isEmpty()))
            return reject(QCoreApplication::translate("lens::llm",
                                                      "A results entry has an empty field "
                                                      "(title=%1).")
                              .arg(e.title));
        parsed.push_back(e);
    }

    if (!isWord) {
        // Entity, sentence and translation carry no echoed title on the wire: the app already knows
        // the selected text, and making a model reproduce a long, punctuation-heavy string invites
        // drift. A model may also split a selected block (a numbered list, several sentences) into
        // more entries than asked for; those fold into one result rather than losing the batch.
        QVector<Explanation> ordered;
        if (parsed.size() == expectedInputs.size()) {
            for (int i = 0; i < expectedInputs.size(); ++i) {
                Explanation e = parsed.at(i);
                e.title       = expectedInputs.at(i);
                ordered.push_back(e);
            }
        } else if (expectedInputs.size() == 1 && !parsed.isEmpty()) {
            Explanation e;
            for (const auto& part : parsed) {
                if (!e.en.isEmpty()) e.en += QLatin1Char('\n');
                e.en += part.en;
                if (!e.zh.isEmpty()) e.zh += QLatin1Char('\n');
                e.zh += part.zh;
                if (not e.translation.isEmpty()) e.translation += QLatin1Char('\n');
                e.translation += part.translation;
            }
            e.title = expectedInputs.front();
            ordered.push_back(e);
        } else {
            return reject(QCoreApplication::translate("lens::llm",
                                                      "The model echoed %1 result(s) for the %2 that "
                                                      "were asked for.")
                              .arg(parsed.size())
                              .arg(expectedInputs.size()));
        }
        LENS_TRACE("parseExplanations: accepted {} explanation(s) for channel '{}'", ordered.size(), channelKey(channel));
        return ordered;
    }

    // A word keeps the echo contract: the model must name the word requested, which catches an
    // explanation attached to the wrong entry before it reaches the bubble. Matching is
    // case-insensitive (a model may capitalize an acronym) and the requested spelling wins, so
    // the cache key and the bubble title stay canonical.
    //
    // The same word twice is not a failure: a model that writes one result per sense instead of
    // one result with a `senses` array names its word once per sense, and failing the batch over
    // it would discard definitions that had arrived -- the trade this file refuses everywhere
    // else. The repeat folds into the entry already held, as the other channels fold a model's
    // several entries for one selected block. A word the model never named at all is still a
    // failure: it leaves the reader with nothing.
    QHash<QString, Explanation> byTitle;
    for (const auto& e : parsed) {
        const QString key  = e.title.toLower();
        auto          held = byTitle.find(key);
        if (held == byTitle.end()) {
            byTitle.insert(key, e);
            continue;
        }
        Explanation& kept = held.value();
        const int    cap  = multipleSenses ? kMaxSenses : 1;
        for (auto const& sense : e.senses) {
            if (kept.senses.size() >= cap)
                break;
            const bool known = std::any_of(kept.senses.cbegin(), kept.senses.cend(), [&sense](Sense const& existing) {
                return existing.en == sense.en and existing.zh == sense.zh;
            });
            if (not known)
                kept.senses.push_back(sense);
        }
        LENS_WARN("the model echoed '{}' more than once; the repeat was folded into the first entry", e.title.toStdString());
    }

    if (byTitle.size() != expectedInputs.size())
        return reject(QCoreApplication::translate("lens::llm",
                                                  "The model echoed %1 result(s) for the %2 that "
                                                  "were asked for.")
                          .arg(byTitle.size())
                          .arg(expectedInputs.size()));

    QVector<Explanation> ordered;
    ordered.reserve(expectedInputs.size());
    for (const auto& input : expectedInputs) {
        const auto it = byTitle.constFind(input.toLower());
        if (it == byTitle.cend())
            return reject(QCoreApplication::translate(
                              "lens::llm", "The model never echoed \"%1\".")
                              .arg(input));
        Explanation e = *it;
        e.title       = input;
        ordered.push_back(e);
    }

    LENS_TRACE("parseExplanations: accepted {} explanation(s) for channel '{}'", ordered.size(), channelKey(channel));
    return ordered;
}

} // namespace

std::variant<QVector<Explanation>, QString> parseExplanations(Channel            channel,
                                                              const QByteArray&  responseBody,
                                                              const QStringList& expectedInputs,
                                                              QString const&     explanationLang,
                                                              bool const         multipleSenses)
{
    using Result = std::variant<QVector<Explanation>, QString>;

    const auto reject = [](const QString& reason) -> Result {
        LENS_ERROR("parseExplanations rejected the response: {}", reason.toStdString());
        return reason;
    };

    const auto envelope = QJsonDocument::fromJson(responseBody);
    if (!envelope.isObject())
        return reject(QCoreApplication::translate("lens::llm",
                                                  "The model answered with something that is not "
                                                  "a JSON object."));

    const auto choices = envelope.object().value("choices").toArray();
    if (choices.isEmpty())
        return reject(QCoreApplication::translate("lens::llm",
                                                  "The model's answer carries no choices."));

    const auto    choice = choices.at(0).toObject();
    const QString finish = choice.value("finish_reason").toString();
    if (const QString reason = finishReasonError(finish); not reason.isEmpty())
        return reject(reason);

    const QString content = choice.value("message").toObject().value("content").toString();
    return parseExplanationContent(channel, content, expectedInputs, explanationLang, multipleSenses);
}

namespace {

/// Bytes needed to tell an SSE stream from one whole body: exactly the length of "data:".
constexpr int kSseSniffBytes = 5;

/// @return Whether a preview has anything the card would draw. The first chunks are `{` and
///         `"resul` and the like; an empty card must not appear before any text has arrived.
bool hasAnythingToDraw(Explanation const& partial)
{
    return not(partial.title.isEmpty() and partial.ipa.isEmpty() and partial.en.isEmpty() and
               partial.zh.isEmpty() and partial.translation.isEmpty() and partial.etymology.isEmpty() and
               partial.example.isEmpty() and partial.senses.isEmpty());
}

/// @brief Read token counts out of a `usage` object, with the one warning both paths share.
Usage usageFromObject(QJsonObject const& usage)
{
    if (usage.isEmpty()) {
        LENS_WARN("the response carries no usage object; the cost tally loses this call");
        return {};
    }
    const Usage counts{usage.value("prompt_tokens").toInt(), usage.value("completion_tokens").toInt()};
    LENS_TRACE("usage: {} prompt / {} completion token(s)", counts.promptTokens, counts.completionTokens);
    return counts;
}

} // namespace

StreamAssembler::StreamAssembler(Channel channel, QStringList expectedInputs, QString language, bool const multipleSenses)
    : channel_(channel), expectedInputs_(std::move(expectedInputs)), language_(std::move(language)), multipleSenses_(multipleSenses)
{}

bool StreamAssembler::consume(QByteArray& buffer)
{
    bool grew = false;
    for (const auto& payload : takeSsePayloads(buffer)) {
        const auto chunk = parseStreamDelta(payload);
        if (not chunk)
            continue;
        content_ += chunk->content;
        grew = grew or not chunk->content.isEmpty();
        if (not chunk->finishReason.isEmpty())
            finishReason_ = chunk->finishReason;
        if (not chunk->usage.isEmpty())
            usageObject_ = chunk->usage;
    }
    return grew;
}

std::optional<Explanation> StreamAssembler::feed(QByteArray const& bytes, bool const httpOk)
{
    buffer_ += bytes;
    // Decided once there are enough bytes to tell the two shapes apart: a first read of "da" must
    // not settle it, or a stream split across reads would be judged a whole body and fail.
    if (not decided_ and buffer_.size() >= kSseSniffBytes) {
        decided_ = true;
        sse_     = buffer_.startsWith("data:");
    }
    // An error body is not a stream, and a stream is the reader's only while it is arriving:
    // anything else is left to the finished handler, which owns today's failure notices.
    if (not httpOk or not sse_ or not consume(buffer_))
        return std::nullopt;

    auto preview = parsePartialExplanation(channel_, content_);
    if (not hasAnythingToDraw(preview) or preview == last_)
        return std::nullopt;
    last_ = preview;
    return preview;
}

std::variant<QVector<Explanation>, QString> StreamAssembler::finish(QByteArray const& tail)
{
    buffer_ += tail;
    // A reply whose whole body arrived in one read: too short to be a stream, it is a body.
    if (not decided_) {
        decided_ = true;
        sse_     = buffer_.size() >= kSseSniffBytes and buffer_.startsWith("data:");
    }

    if (not sse_)
        return parseExplanations(channel_, buffer_, expectedInputs_, language_, multipleSenses_);

    consume(buffer_); // the last event may have arrived without its terminator
    if (const QString reason = finishReasonError(finishReason_); not reason.isEmpty()) {
        LENS_ERROR("parseExplanations rejected the response: {}", reason.toStdString());
        return reason;
    }
    return parseExplanationContent(channel_, content_, expectedInputs_, language_, multipleSenses_);
}

Usage StreamAssembler::usage() const
{
    if (not sse_)
        return parseUsage(buffer_);
    return usageFromObject(usageObject_);
}

QStringList parseModelIds(QByteArray const& body)
{
    const auto document = QJsonDocument::fromJson(body);
    if (not document.isObject()) {
        LENS_WARN("the model list was not valid JSON; keeping the one we have");
        return {};
    }
    const auto data = document.object().value("data");
    if (not data.isArray() or data.toArray().size() > 2000) {
        LENS_WARN("the model list did not arrive in the shape this reads; keeping the one we have");
        return {};
    }
    QStringList ids;
    for (const auto& entry : data.toArray()) {
        if (not entry.isObject()) return {};
        const auto object = entry.toObject();
        const auto rawId  = object.value("id");
        if (not rawId.isString()) return {};
        const auto id = rawId.toString().trimmed();
        if (id.isEmpty() or id.size() > 256) return {};
        // A `:` suffix is OpenRouter's own routing and pricing mode (`:batch`, `:free`), never a
        // model name at a first-party endpoint.
        if (id.contains(QLatin1Char(':'))) continue;
        // The source states the modality itself, so the app states it too rather than guessing from
        // the name. An entry that does not say "text" and nothing else is one a completion cannot
        // be sent to: it drops that entry, never the whole answer.
        const auto modalities = object.value("architecture").toObject().value("output_modalities");
        if (not modalities.isArray() or modalities.toArray().size() != 1 or modalities.toArray().at(0) != QLatin1String("text"))
            continue;
        if (ids.contains(id)) continue;
        ids.append(id);
    }
    LENS_INFO("the model source lists {} model(s) this app can use", ids.size());
    return ids;
}

QStringList idsForPrefix(QStringList const& ids, QString const& prefix)
{
    if (prefix.isEmpty()) return ids;
    const auto  marker = prefix + QLatin1Char('/');
    QStringList matched;
    for (const auto& id : ids) {
        if (not id.startsWith(marker)) continue;
        matched.append(id.mid(marker.size()));
    }
    return matched;
}

QVector<QByteArray> takeSsePayloads(QByteArray& buffer)
{
    QVector<QByteArray> payloads;
    while (true) {
        const int lf         = buffer.indexOf("\n\n");
        const int crlf       = buffer.indexOf("\r\n\r\n");
        int       end        = -1;
        int       terminator = 0;
        if (crlf >= 0 and (lf < 0 or crlf < lf)) {
            end        = crlf;
            terminator = 4;
        } else if (lf >= 0) {
            end        = lf;
            terminator = 2;
        } else {
            break; // the terminator has not arrived; the event waits whole in the buffer
        }

        QByteArray data;
        bool       found = false;
        for (auto line : buffer.mid(0, end).split('\n')) {
            if (line.endsWith('\r'))
                line.chop(1);
            if (not line.startsWith("data:"))
                continue; // "event:", "id:", a ":comment" keep-alive
            auto value = line.mid(5);
            if (value.startsWith(' '))
                value.remove(0, 1);
            if (found)
                data += '\n'; // SSE joins an event's several data lines with a newline
            data += value;
            found = true;
        }
        buffer.remove(0, end + terminator);
        if (found)
            payloads.append(data);
    }
    return payloads;
}

std::optional<StreamDelta> parseStreamDelta(const QByteArray& payload)
{
    const auto trimmed = payload.trimmed();
    if (trimmed.isEmpty() or trimmed == "[DONE]")
        return std::nullopt;
    const auto document = QJsonDocument::fromJson(trimmed);
    if (not document.isObject())
        return std::nullopt;

    const auto  root = document.object();
    StreamDelta delta;
    const auto  choices = root.value("choices").toArray();
    if (not choices.isEmpty()) {
        const auto choice = choices.at(0).toObject();
        delta.content     = choice.value("delta").toObject().value("content").toString();
        const auto reason = choice.value("finish_reason");
        if (reason.isString())
            delta.finishReason = reason.toString();
    }
    // Null on every chunk but the last, and only present at all when include_usage was asked for.
    const auto usage = root.value("usage");
    if (usage.isObject())
        delta.usage = usage.toObject();
    return delta;
}

Explanation parsePartialExplanation(Channel const channel, QString const& content)
{
    Explanation partial;
    const int   payloadStart = content.indexOf(QLatin1Char('{'));
    if (payloadStart < 0)
        return partial;
    const int resultsKey = content.indexOf(QStringLiteral("\"results\""), payloadStart);
    if (resultsKey < 0)
        return partial;
    const int arrayStart = content.indexOf(QLatin1Char('['), resultsKey);
    if (arrayStart < 0)
        return partial;

    int cursor = arrayStart + 1;
    while (cursor < content.size() and (content.at(cursor).isSpace() or content.at(cursor) == QLatin1Char(',')))
        ++cursor;
    if (cursor >= content.size() or content.at(cursor) != QLatin1Char('{'))
        return partial;

    QHash<QString, QString> fields;
    scanJsonObject(content, cursor, fields, partial.senses);

    // The title field follows the same rule the finished answer's does: the word channel echoes
    // the word it explained, the others carry no title and have it stamped on further up.
    partial.title       = fields.value(channel == Channel::Word ? QStringLiteral("word") : QStringLiteral("title"));
    partial.ipa         = fields.value(QStringLiteral("ipa"));
    partial.en          = fields.value(QStringLiteral("en"));
    partial.zh          = fields.value(QStringLiteral("zh"));
    partial.translation = fields.value(QStringLiteral("translation"));
    partial.etymology   = fields.value(QStringLiteral("etymology"));
    partial.example     = fields.value(QStringLiteral("example"));
    return partial;
}

Usage parseUsage(const QByteArray& responseBody)
{
    const auto envelope = QJsonDocument::fromJson(responseBody).object();
    return usageFromObject(envelope.value("usage").toObject());
}

}
