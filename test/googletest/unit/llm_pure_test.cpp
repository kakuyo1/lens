/**
 * @file llm_pure_test.cpp
 * @brief The network-free LlmClient helpers: mask, build the request, check the response.
 *
 * The request cases pin what data/llm/request.word.json must keep saying, so a data edit
 * that breaks the DeepSeek contract fails here rather than in a paid round trip. The
 * response cases treat the payload as untrusted: anything missing, extra, misspelled, or
 * truncated has to fail the whole batch.
 */

#include <string>
#include <variant>
#include <vector>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <gtest/gtest.h>

#include "llm/llm_pure.h"
#include "llm_support.h"

namespace {

using lens::llm::Channel;
using lens::llm::Config;
using lens::llm::Usage;
using lens::llm::Explanation;
using lens::llm::breakdownApplies;
using lens::llm::breakdownWords;
using lens::llm::buildRequestBody;
using lens::llm::failureLineFor;
using lens::llm::grammarApplies;
using lens::llm::httpErrorFor;
using lens::llm::maskSensitive;
using lens::llm::parseBreakdown;
using lens::llm::parseExplanations;
using lens::llm::parsePartialExplanation;
using lens::llm::parseStreamDelta;
using lens::llm::parseUsage;
using lens::llm::StreamAssembler;
using lens::llm::takeSsePayloads;
using lens::llm::CorrectionRejected;
using lens::llm::Role;
using lens::llm::RoleSpan;
using LlmResult       = std::variant<QVector<Explanation>, QString>;
using BreakdownResult = std::variant<QVector<RoleSpan>, QString>;

// TEST_F pastes the fixture name into a class definition, so it has to be unqualified.
using lens::test::LlmTest;

/// @return The failure message when @p result is an error, or an empty string on success.
QString errorOf(const LlmResult& result)
{
    const auto* message = std::get_if<QString>(&result);
    return message ? *message : QString();
}

bool accepted(const LlmResult& result)
{
    return std::holds_alternative<QVector<Explanation>>(result);
}

/// @brief Wrap @p content the way the chat-completions envelope does.
QByteArray envelope(const QString& content, const QString& finishReason = QStringLiteral("stop"))
{
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", finishReason}, {"message", QJsonObject{{"content", content}}}}}}})
        .toJson(QJsonDocument::Compact);
}

/// @return A word-channel content payload carrying @p items as its `results` array.
QString results(const QString& items)
{
    return QStringLiteral("{\"results\":[%1]}").arg(items);
}

const QStringList kAskedFor{"ubiquitous"};
const QString     kGoodResult =
    QStringLiteral(R"({"word":"ubiquitous","ipa":"/juːˈbɪkwɪtəs/","en":"existing everywhere","zh":"无处不在的"})");

/// @brief One OpenRouter-shaped entry: an id plus the modality the source states for it.
QJsonObject listed(QString const& id, QString const& modality = QStringLiteral("text"))
{
    return {{"id", id}, {"architecture", QJsonObject{{"output_modalities", modality.isEmpty() ? QJsonArray{} : QJsonArray{modality}}}}};
}

} // namespace

/// The source's own list, in the order it gave them, and nothing invented for a body that is not
/// that list: a partial list would hide the one the reader wanted.
TEST(ParseModelIds, ReadsTheIdOrderTheSourceGaveAndDedupes)
{
    const auto ids = lens::llm::parseModelIds(
        QByteArrayLiteral(R"({"data":[{"id":"deepseek/deepseek-chat","architecture":{"output_modalities":["text"]}},)"
                          R"({"id":"openai/gpt-4.1-mini","architecture":{"output_modalities":["text"]}},)"
                          R"({"id":"deepseek/deepseek-chat","architecture":{"output_modalities":["text"]}}]})"));

    ASSERT_EQ(ids.size(), 2);
    EXPECT_EQ(ids.at(0), QStringLiteral("deepseek/deepseek-chat"));
    EXPECT_EQ(ids.at(1), QStringLiteral("openai/gpt-4.1-mini"));
}

/// An id carrying `:` is OpenRouter's own routing or pricing mode, not a model name. Both sides of
/// the rule are here: the suffix goes, and the plain neighbour stays.
TEST(ParseModelIds, DropsRoutingVariantsAndKeepsThePlainNeighbour)
{
    const auto ids = lens::llm::parseModelIds(
        QByteArrayLiteral(R"({"data":[{"id":"openai/gpt-4.1-mini:batch","architecture":{"output_modalities":["text"]}},)"
                          R"({"id":"openai/gpt-4.1-mini:free","architecture":{"output_modalities":["text"]}},)"
                          R"({"id":"openai/gpt-4.1-mini","architecture":{"output_modalities":["text"]}}]})"));

    ASSERT_EQ(ids.size(), 1);
    EXPECT_EQ(ids.front(), QStringLiteral("openai/gpt-4.1-mini"));
}

/// The source states the output modality itself, so the app states it too: only an entry that is
/// text and nothing else survives. An entry that also writes image or audio is dropped, and one
/// that says nothing at all is dropped rather than guessed at from its name.
TEST(ParseModelIds, KeepsOnlyEntriesWhoseOnlyOutputModalityIsText)
{
    QJsonArray data;
    for (const auto& modality : {QStringLiteral("text"), QStringLiteral("image"), QStringLiteral("audio"), QString{}})
        data.append(listed("acme/mixed", modality));
    data.append(listed("acme/quiet"));
    data.append(QJsonObject{{"id", "acme/silent"}});
    const auto body = QJsonDocument(QJsonObject{{"data", data}}).toJson(QJsonDocument::Compact);

    const auto ids = lens::llm::parseModelIds(body);

    ASSERT_EQ(ids.size(), 2);
    EXPECT_EQ(ids.at(0), QStringLiteral("acme/mixed")); // the "text"-only entry
    EXPECT_EQ(ids.at(1), QStringLiteral("acme/quiet"));
}

/// A malformed answer is no answer. Each of these has to come back empty rather than as a shorter
/// list, because a shorter list still reads to the reader as "these are the models".
TEST(ParseModelIds, AnswersEmptyForAnythingThatIsNotThatList)
{
    QJsonArray oversized;
    for (int i = 0; i < 2001; ++i)
        oversized.append(listed(QStringLiteral("acme/model-%1").arg(i)));

    const struct {
        const char* what;
        QByteArray  body;
    } cases[] = {
        {"not JSON at all", QByteArrayLiteral("not json at all")},
        {"not an object", QByteArrayLiteral("[1,2,3]")},
        {"data is not an array", QByteArrayLiteral(R"({"data":"nope"})")},
        {"an entry has no string id", QByteArrayLiteral(R"({"data":[{"id":"acme/ok","architecture":{"output_modalities":["text"]}},7]})")},
        {"the id is not a string", QByteArrayLiteral(R"({"data":[{"id":7,"architecture":{"output_modalities":["text"]}}]})")},
        {"the id is blank", QByteArrayLiteral(R"({"data":[{"id":"  ","architecture":{"output_modalities":["text"]}}]})")},
        {"more entries than the cap", QJsonDocument(QJsonObject{{"data", oversized}}).toJson(QJsonDocument::Compact)},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.what);
        EXPECT_TRUE(lens::llm::parseModelIds(item.body).isEmpty()) << item.what;
    }
    // The cap itself is not a failure: the entry just below it reads normally.
    oversized.removeLast();
    EXPECT_FALSE(lens::llm::parseModelIds(QJsonDocument(QJsonObject{{"data", oversized}}).toJson(QJsonDocument::Compact)).isEmpty());
}

/// An empty prefix is OpenRouter's own case: its API takes `vendor/slug`, so the ids must reach
/// the field whole.
TEST(IdsForPrefix, AnEmptyPrefixIsTheWholeCatalogueUntouched)
{
    const QStringList ids{"deepseek/deepseek-chat", "openai/gpt-4.1-mini"};

    EXPECT_EQ(lens::llm::idsForPrefix(ids, QString{}), ids);
}

/// A vendor prefix matches on the slash, so a vendor that merely starts the same way is not it.
TEST(IdsForPrefix, KeepsThePrefixedIdsAndStripsThePrefix)
{
    const QStringList ids{"deepseek/deepseek-chat", "deepseek/deepseek-reasoner", "openai/gpt-4.1-mini", "deepseek-x/gpt-4.1-mini", "deepseek"};

    const auto matched = lens::llm::idsForPrefix(ids, QStringLiteral("deepseek"));

    ASSERT_EQ(matched.size(), 2);
    EXPECT_EQ(matched.at(0), QStringLiteral("deepseek-chat"));
    EXPECT_EQ(matched.at(1), QStringLiteral("deepseek-reasoner"));
    EXPECT_TRUE(lens::llm::idsForPrefix(ids, QStringLiteral("absent-vendor")).isEmpty());
}

TEST(LlmPureMask, CollapsesEmailUrlAndLongDigits)
{
    const struct {
        const char* input;
        const char* want;
    } cases[] = {
        {"mail a.b+tag@example.com now", "mail <email> now"},
        {"see https://example.com/p?q=1#f end", "see <url> end"},
        {"call 13800138000 now", "call <num> now"},
        {"ubiquitous", "ubiquitous"},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.input);
        EXPECT_EQ(maskSensitive(QString::fromUtf8(item.input)), QString::fromUtf8(item.want));
    }
}

TEST(LlmPureHttpError, NamesEachStatusTheModelServiceCanAnswerWith)
{
    // Each entry pins a phrase only that arm of the switch carries, and the status number, so a
    // wrong arm -- or one falling through to the default -- fails here rather than in a paid
    // round trip. 402 in particular is DeepSeek's out-of-credit code, which is the one failure a
    // reader has to act on rather than retry.
    const struct {
        int         status;
        const char* phrase;
    } cases[] = {
        {400, "malformed"},
        {401, "API key"},
        {402, "credit"},
        {422, "parameters"},
        {429, "rate-limiting"},
        {500, "failed"},
        {503, "overloaded"},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.status);
        const QString message = httpErrorFor(item.status);
        EXPECT_TRUE(message.contains(item.phrase)) << message.toStdString();
        EXPECT_TRUE(message.contains(QString::number(item.status))) << message.toStdString();
    }

    // A status with no arm of its own still names the number rather than collapsing to a
    // generic line, so the reader can search for it.
    EXPECT_TRUE(httpErrorFor(418).contains("418"));
    EXPECT_TRUE(httpErrorFor(0).contains("0"));
}

TEST_F(LlmTest, RequestBodyKeepsTheDeepSeekContract)
{
    const Config      config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const QStringList words{"ubiquitous", "resilience"};

    for (const auto lang : {"en", "zh"}) {
        SCOPED_TRACE(lang);
        const auto doc = QJsonDocument::fromJson(buildRequestBody(config, Channel::Word, words, lang));
        ASSERT_TRUE(doc.isObject());
        const auto root = doc.object();

        EXPECT_EQ(root.value("model").toString(), config.model);
        // The answer is streamed: the card is drawn as the model writes it (docs/API.md section 3).
        EXPECT_TRUE(root.value("stream").toBool());
        EXPECT_TRUE(root.value("stream_options").toObject().value("include_usage").toBool())
            << "a streaming endpoint omits usage unless it is asked for";
        EXPECT_EQ(root.value("response_format").toObject().value("type").toString(), "json_object");
        // DeepSeek turns thinking on by default; it costs reasoning tokens on every call.
        EXPECT_EQ(root.value("thinking").toObject().value("type").toString(), "disabled");
        EXPECT_GT(root.value("max_tokens").toInt(), 0) << "an unset cap can truncate the JSON";

        const auto messages = root.value("messages").toArray();
        ASSERT_EQ(messages.size(), 2);
        const auto system = messages.at(0).toObject().value("content").toString();
        const auto user   = messages.at(1).toObject().value("content").toString();

        EXPECT_EQ(messages.at(0).toObject().value("role").toString(), "system");
        EXPECT_EQ(messages.at(1).toObject().value("role").toString(), "user");
        // response_format json_object is only accepted when the prompt mentions JSON.
        EXPECT_TRUE(system.contains("json", Qt::CaseInsensitive));
        EXPECT_TRUE(system.contains("\"results\"")) << "the prompt shows no format example";
        // The model copies the example's field names, so every required field has to be in
        // it -- the schema's required list is not in the prompt.
        EXPECT_TRUE(system.contains("\"ipa\"")) << "the format example leaves out the IPA field";
        EXPECT_TRUE(user.contains("ubiquitous") && user.contains("resilience"))
            << "the user message dropped a requested word";
        // The shared body never names a language, so its presence tracks the switch.
        EXPECT_EQ(system.contains(QStringLiteral("Chinese")),
                  QString(lang) == QStringLiteral("zh"))
            << "the explanation language does not switch the prompt's closing line";
    }
}

/// Traditional Chinese is a fifth catalog language but the same response shape as Spanish and
/// Japanese: the model writes it into `translation` while `zh` stays the Simplified field. The
/// prompt has to ask for it by name, or the reader gets Simplified Chinese with a new label.
TEST_F(LlmTest, TraditionalChineseHasItsOwnPromptAndTheTranslationField)
{
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const auto   body = QJsonDocument::fromJson(
        buildRequestBody(config, Channel::Word, {"ubiquitous"}, "zh-Hant", "default"));
    const auto system = body.object().value("messages").toArray().at(0).toObject().value("content").toString();
    EXPECT_TRUE(system.contains("Traditional Chinese")) << system.toStdString();
    EXPECT_TRUE(system.contains("\"translation\"")) << system.toStdString();

    const QString content =
        QStringLiteral(R"({"results":[{"word":"ubiquitous","ipa":"/x/","en":"existing everywhere","zh":"无处不在的","translation":"無處不在的"}]})");
    const auto parsed = parseExplanations(Channel::Word, envelope(content), {"ubiquitous"}, "zh-Hant", false);
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    const auto& explanation = std::get<QVector<Explanation>>(parsed).at(0);
    EXPECT_EQ(explanation.translation, QStringLiteral("無處不在的"));
    EXPECT_EQ(explanation.zh, QStringLiteral("无处不在的")) << "the Simplified field was overwritten";
}

TEST_F(LlmTest, AsksForTheEtymologyOnlyWhenAskedAndOnlyOnTheWordChannel)
{
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const auto   prompt = [&](Channel channel, const QString& preset, bool etymology) {
        const auto body =
            QJsonDocument::fromJson(buildRequestBody(config, channel, {"ubiquitous"}, "zh", preset, etymology));
        return body.object().value("messages").toArray().at(0).toObject().value("content").toString();
    };

    // Both word presets, because the field is asked for on either: a reader who turned on multiple
    // senses has not thereby given up the origin.
    for (const auto* preset : {"default", "multiple"}) {
        SCOPED_TRACE(preset);
        const auto off = prompt(Channel::Word, preset, false);
        const auto on  = prompt(Channel::Word, preset, true);
        // Off, the prompt reads as it did before the field existed: nothing is paid for that the
        // reader's setting did not ask for.
        EXPECT_FALSE(off.contains("etymology")) << "the field is named even though it was not asked for";
        // On, the sentence and the example travel together. A shape that does not name the field
        // is a field the model leaves out, and a missing optional value is not a failure any
        // validator can report -- the card would simply have nothing to draw.
        EXPECT_TRUE(on.contains(QStringLiteral("\"etymology\":\"...\""))) << "the format example omits the field";
        EXPECT_TRUE(on.contains("origin")) << "nothing in the prompt asks for the word's origin";
        EXPECT_GT(on.size(), off.size());
    }

    // The entity and sentence templates have no place for the origin, so the flag changes nothing
    // there -- an entity is not a word with a history.
    EXPECT_EQ(prompt(Channel::Entity, "default", false), prompt(Channel::Entity, "default", true));
}

TEST_F(LlmTest, AsksForAnExampleOnlyWhenAskedAndOnlyOnTheWordChannel)
{
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const auto   prompt = [&](Channel channel, const QString& preset, bool example) {
        const auto body =
            QJsonDocument::fromJson(buildRequestBody(config, channel, {"ubiquitous"}, "zh", preset, false, example));
        return body.object().value("messages").toArray().at(0).toObject().value("content").toString();
    };

    for (const auto* preset : {"default", "multiple"}) {
        SCOPED_TRACE(preset);
        const auto off = prompt(Channel::Word, preset, false);
        const auto on  = prompt(Channel::Word, preset, true);
        // Off, the prompt is the one a reader without the setting gets: nothing is paid for that the
        // setting did not ask for.
        EXPECT_FALSE(off.contains("example")) << "the example is named even though it was not asked for";
        EXPECT_TRUE(on.contains(QStringLiteral("\"example\":\"...\""))) << "the format example omits the field";
        EXPECT_TRUE(on.contains("English")) << "nothing in the prompt says the sentence is English";
        EXPECT_GT(on.size(), off.size());
    }

    // Asked alongside the origin, both sentences and both fields arrive together, and neither
    // placeholder is left in the text the model reads.
    const auto both = prompt(Channel::Word, "default", true);
    EXPECT_FALSE(both.contains("{exampleNote}"));
    EXPECT_FALSE(both.contains("{exampleField}"));

    // The entity and sentence templates have no place for an example, so the flag changes nothing.
    EXPECT_EQ(prompt(Channel::Entity, "default", false), prompt(Channel::Entity, "default", true));
}

TEST_F(LlmTest, KeepsAnExampleWhenTheResponseCarriesOneAndShrugsWithoutIt)
{
    const QString withExample =
        QStringLiteral(R"({"word":"ubiquitous","ipa":"/juːˈbɪkwɪtəs/","en":"existing everywhere","zh":"无处不在的",)"
                       R"("example":"Smartphones are ubiquitous in modern cities."})");

    const auto parsed = parseExplanations(Channel::Word, envelope(results(withExample)), kAskedFor);
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(parsed).front().example,
              QStringLiteral("Smartphones are ubiquitous in modern cities."));

    // Absent is the ordinary case: a model with no sentence to give leaves the field out, and rejecting
    // the batch over it would throw away the definition that did arrive.
    const auto bare = parseExplanations(Channel::Word, envelope(results(kGoodResult)), kAskedFor);
    ASSERT_TRUE(accepted(bare));
    EXPECT_TRUE(std::get<QVector<Explanation>>(bare).front().example.isEmpty());
}

TEST_F(LlmTest, KeepsAWordOriginWhenTheResponseCarriesOneAndShrugsWithoutIt)
{
    const QString withOrigin =
        QStringLiteral(R"({"word":"ubiquitous","ipa":"/juːˈbɪkwɪtəs/","en":"existing everywhere","zh":"无处不在的",)"
                       R"("etymology":"Latin, from ubique: everywhere."})");

    const auto parsed = parseExplanations(Channel::Word, envelope(results(withOrigin)), kAskedFor);
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(parsed).front().etymology,
              QStringLiteral("Latin, from ubique: everywhere."));

    // Absent is the ordinary case, not a fault: the setting asks for the field, and a word with no
    // recorded origin answers with none. Rejecting the batch over it would throw away the
    // definitions that did arrive.
    const auto bare = parseExplanations(Channel::Word, envelope(results(kGoodResult)), kAskedFor);
    ASSERT_TRUE(accepted(bare));
    EXPECT_TRUE(std::get<QVector<Explanation>>(bare).front().etymology.isEmpty());
}

TEST_F(LlmTest, AcceptsAWellFormedResponse)
{
    const auto result = parseExplanations(Channel::Word, envelope(results(kGoodResult)), kAskedFor);

    ASSERT_TRUE(accepted(result)) << errorOf(result).toStdString();
    const auto& explanations = std::get<QVector<Explanation>>(result);
    ASSERT_EQ(explanations.size(), 1);
    EXPECT_EQ(explanations.at(0).title, QStringLiteral("ubiquitous"));
    EXPECT_EQ(explanations.at(0).ipa, QString::fromUtf8("/juːˈbɪkwɪtəs/")) << "the IPA was dropped on the way out";
    EXPECT_EQ(explanations.at(0).zh, QString::fromUtf8("无处不在的"));
}

TEST_F(LlmTest, TheSenseFlagChoosesTheMultiplePresetOverTheSelectionsOwn)
{
    lens::llm::ExplainRequest request;
    request.preset = QStringLiteral("translate");
    EXPECT_EQ(lens::llm::presetFor(request), QStringLiteral("translate"));

    request.multipleSenses = true;
    EXPECT_EQ(lens::llm::presetFor(request), QStringLiteral("multiple"));
}

TEST_F(LlmTest, KeepsAtMostTheContractSenses)
{
    const QString content = QStringLiteral(
        R"({"results":[{"word":"bank","ipa":"/bæŋk/","senses":[{"en":"a financial house","zh":"银行"},{"en":"the land beside a river","zh":"河岸"},{"en":"to rely on","zh":"依靠"},{"en":"a row of similar objects","zh":"一排"}]}]})");

    const auto multiple = parseExplanations(Channel::Word, envelope(content), {"bank"}, "en", true);
    ASSERT_TRUE(accepted(multiple)) << errorOf(multiple).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(multiple).at(0).senses.size(), lens::llm::kMaxSenses);

    const auto single = parseExplanations(Channel::Word, envelope(content), {"bank"}, "en", false);
    ASSERT_TRUE(accepted(single)) << errorOf(single).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(single).at(0).senses.size(), 1);
}

/// The picture advice, read for both shapes the word channel answers in: the pair sits at the top
/// level when a word carries no `senses`, and inside each sense when it does. A sense is imageable
/// only when the model both said so and named the words an image search runs with, so a half-answer
/// -- true with a blank query -- is not a picture to fetch.
TEST_F(LlmTest, ReadsThePictureAdviceFromEitherShape)
{
    const QString singular =
        QStringLiteral(R"({"results":[{"word":"trowel","ipa":"/ˈtraʊəl/","en":"a small hand tool","zh":"抹刀","imageable":true,"imageQuery":"garden trowel"}]})");
    const auto one = parseExplanations(Channel::Word, envelope(singular), {"trowel"}, "en", false);
    ASSERT_TRUE(accepted(one)) << errorOf(one).toStdString();
    const auto single = std::get<QVector<Explanation>>(one).at(0);
    EXPECT_TRUE(single.imageable);
    EXPECT_EQ(single.imageQuery, QStringLiteral("garden trowel"));
    ASSERT_EQ(single.senses.size(), 1);
    EXPECT_TRUE(single.senses.front().imageable) << "the singular pair was not folded into the one sense";
    EXPECT_EQ(single.senses.front().imageQuery, QStringLiteral("garden trowel"));

    const QString multiple =
        QStringLiteral(R"({"results":[{"word":"crane","ipa":"/kreɪn/","senses":[{"en":"a lifting machine","zh":"起重机","imageable":true,"imageQuery":"construction crane"},{"en":"a wading bird","zh":"鹤","imageable":true,"imageQuery":"   "}]}]})");
    const auto many = parseExplanations(Channel::Word, envelope(multiple), {"crane"}, "en", true);
    ASSERT_TRUE(accepted(many)) << errorOf(many).toStdString();
    const auto pair = std::get<QVector<Explanation>>(many).at(0);
    ASSERT_EQ(pair.senses.size(), 2);
    EXPECT_TRUE(pair.senses.at(0).imageable);
    EXPECT_EQ(pair.senses.at(0).imageQuery, QStringLiteral("construction crane"));
    EXPECT_FALSE(pair.senses.at(1).imageable) << "a blank query is not a picture to fetch";
    EXPECT_TRUE(pair.senses.at(1).imageQuery.isEmpty()) << "the query was not trimmed";
    EXPECT_TRUE(pair.imageable) << "the top-level pair does not mirror the most frequent sense";
}

/// A word whose advice is the wrong type is still a word: it is read as not imageable and the
/// definition is kept, because losing an explanation that arrived intact is the worse trade.
/// Both fields are declared in the schema without a type for exactly this reason.
TEST_F(LlmTest, ReadsAMistypedPictureFlagAsNotImageableRatherThanFailing)
{
    const QString content =
        QStringLiteral(R"({"results":[{"word":"trowel","ipa":"","en":"a small hand tool","zh":"抹刀","imageable":"true","imageQuery":42}]})");
    const auto result = parseExplanations(Channel::Word, envelope(content), {"trowel"}, "en", false);
    ASSERT_TRUE(accepted(result)) << errorOf(result).toStdString();
    EXPECT_FALSE(std::get<QVector<Explanation>>(result).at(0).imageable);
}

/// Both word presets ask for the picture advice, and no other channel does: the fields are not
/// gated on a setting, so a prompt that stopped asking would leave every card without its entry.
TEST_F(LlmTest, AsksForThePictureAdviceOnBothWordPresetsOnly)
{
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const auto   prompt = [&](Channel channel, const QString& preset) {
        const auto body = QJsonDocument::fromJson(buildRequestBody(config, channel, {"ubiquitous"}, "en", preset));
        return body.object().value("messages").toArray().at(0).toObject().value("content").toString();
    };

    for (const auto* preset : {"default", "multiple"}) {
        SCOPED_TRACE(preset);
        const auto system = prompt(Channel::Word, preset);
        EXPECT_TRUE(system.contains(QStringLiteral("\"imageable\""))) << "the format example omits the flag";
        EXPECT_TRUE(system.contains(QStringLiteral("\"imageQuery\""))) << "the format example omits the query";
        EXPECT_TRUE(system.contains(QStringLiteral("photograph"))) << "nothing asks whether a picture would help";
    }

    EXPECT_FALSE(prompt(Channel::Sentence, "translate").contains(QStringLiteral("imageable")))
        << "a sentence is not something with a picture to fetch";
    EXPECT_FALSE(prompt(Channel::Entity, "default").contains(QStringLiteral("imageable")));
}

TEST_F(LlmTest, UsesSentencePresetsAndTheSharedEntityPreset)
{
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};

    const auto sentencePrompt = [&](const QString& lang, const QString& preset) {
        const auto body = QJsonDocument::fromJson(
            buildRequestBody(config, Channel::Sentence, {"New York is busy."}, lang, preset));
        return body.object().value("messages").toArray().at(0).toObject().value("content").toString();
    };
    const auto entity       = QJsonDocument::fromJson(buildRequestBody(config, Channel::Entity, {"New York"}, "en"));
    const auto entityPrompt = entity.object().value("messages").toArray().at(0).toObject().value("content").toString();

    // In English, translate restates the sentence in simpler English; explain says what it means.
    EXPECT_NE(sentencePrompt("en", "translate"), sentencePrompt("en", "explain"));
    EXPECT_TRUE(sentencePrompt("en", "translate").contains("simpler English"));
    EXPECT_TRUE(sentencePrompt("en", "explain").contains("plain English"));

    // With Chinese, translate is literal and explain is plain-language.
    EXPECT_TRUE(sentencePrompt("zh", "translate").contains("literally"));
    EXPECT_TRUE(sentencePrompt("zh", "explain").contains("everyday Chinese"));
    EXPECT_NE(sentencePrompt("zh", "translate"), sentencePrompt("zh", "explain"));

    EXPECT_TRUE(entityPrompt.contains("named entity"));
}

TEST_F(LlmTest, StampsTheRequestedTextOntoEntityAndSentenceResults)
{
    // Entity and sentence responses carry only the two explanations; the app supplies the
    // title, so a model that drifts on a long echo cannot fail the batch.
    const auto entity = parseExplanations(
        Channel::Entity,
        envelope(QStringLiteral(R"({"results":[{"en":"a city","zh":"一座城市"}]})")),
        {"New York"});
    ASSERT_TRUE(accepted(entity)) << errorOf(entity).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(entity).at(0).title, "New York");

    const auto sentence = parseExplanations(
        Channel::Sentence,
        envelope(QStringLiteral(R"({"results":[{"en":"The city is busy.","zh":"纽约很忙。"}]})")),
        {"New York is busy."});
    ASSERT_TRUE(accepted(sentence)) << errorOf(sentence).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(sentence).at(0).title, "New York is busy.");

    // A missing title no longer matters for entity / sentence, but a missing language does.
    const auto missing = parseExplanations(
        Channel::Sentence, envelope(QStringLiteral(R"({"results":[{"zh":"只有中文"}]})")), {"text"});
    EXPECT_FALSE(accepted(missing));
}

TEST_F(LlmTest, AcceptsATranslationThatCarriesEnglishAlone)
{
    // The one channel whose result holds a single language: the input is Chinese, so the reader's
    // own explanation language is the one answer that cannot be right, and a missing `zh` is the
    // contract here rather than the fault it is on every other channel.
    const auto translated = parseExplanations(
        Channel::Translation,
        envelope(QStringLiteral(R"({"results":[{"en":"The weather is nice today."}]})")),
        {"今天天气不错。"});
    ASSERT_TRUE(accepted(translated)) << errorOf(translated).toStdString();
    const auto& result = std::get<QVector<Explanation>>(translated).at(0);
    EXPECT_EQ(result.en, QStringLiteral("The weather is nice today."));
    EXPECT_EQ(result.title, QStringLiteral("今天天气不错。")) << "the app stamps the selected text on";
    EXPECT_TRUE(result.zh.isEmpty());

    // An empty translation is the one emptiness this channel still refuses: it is the whole card.
    const auto empty = parseExplanations(
        Channel::Translation, envelope(QStringLiteral(R"({"results":[{"en":" "}]})")), {"今天"});
    EXPECT_FALSE(accepted(empty));

    // The prompt does not move with the reader's language: whatever the explanation language is,
    // the model is asked for English, which is also what the loader demands of this channel.
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    for (const char* lang : {"en", "zh", "zh-Hant", "es", "ja"}) {
        SCOPED_TRACE(lang);
        const auto body = QJsonDocument::fromJson(
            buildRequestBody(config, Channel::Translation, {"今天天气不错。"}, lang, "default"));
        const auto prompt = body.object().value("messages").toArray().at(0).toObject().value("content").toString();
        EXPECT_TRUE(prompt.contains(QLatin1String("Translate it into English")));
        EXPECT_TRUE(prompt.contains(QLatin1String("English translation")));
    }
}

TEST_F(LlmTest, PeeksOneLineInTheMotherTongue)
{
    // The peek is the second one-language channel: one English word, phrase or sentence in, one `line` out, stamped
    // with the asked-for word like the translation's selected text is.
    const auto peeked = parseExplanations(
        Channel::Peek,
        envelope(QStringLiteral(R"({"results":[{"line":"意外的幸运"}]})")),
        {"serendipity"});
    ASSERT_TRUE(accepted(peeked)) << errorOf(peeked).toStdString();
    const auto& result = std::get<QVector<Explanation>>(peeked).at(0);
    EXPECT_EQ(result.en, QStringLiteral("意外的幸运"));
    EXPECT_EQ(result.title, QStringLiteral("serendipity")) << "the app stamps the asked-for word on";
    EXPECT_TRUE(result.zh.isEmpty());

    // An empty line is the one emptiness this channel refuses: it is the whole answer.
    EXPECT_FALSE(accepted(parseExplanations(
        Channel::Peek, envelope(QStringLiteral(R"({"results":[{"line":" "}]})")), {"serendipity"})));
    EXPECT_FALSE(accepted(parseExplanations(
        Channel::Peek, envelope(QStringLiteral(R"({"results":[{}]})")), {"serendipity"})));
}

TEST_F(LlmTest, ThePeekPromptAsksInTheMotherTongueAndNeverStreams)
{
    // The closing line follows the mother tongue the request was made in, not the explanation
    // language; the answer is one whole body, because a held gesture draws nothing half-written.
    const Config      config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const QStringList expectations{
        "Write the line in English.", "Write the line in Simplified Chinese.", "Write the line in Traditional Chinese.", "Write the line in Spanish.", "Write the line in Japanese."};
    const QStringList codes{"en", "zh", "zh-Hant", "es", "ja"};
    for (int i = 0; i < codes.size(); ++i) {
        SCOPED_TRACE(codes.at(i).toStdString());
        const auto body = QJsonDocument::fromJson(
            buildRequestBody(config, Channel::Peek, {"serendipity"}, codes.at(i), "default"));
        const auto prompt = body.object().value("messages").toArray().at(0).toObject().value("content").toString();
        EXPECT_TRUE(prompt.contains(expectations.at(i)));
        // The prompt knows the sense line a hold on a swapped sense names: without the sentence,
        // the line would read as part of the word and the answer would answer anything.
        EXPECT_TRUE(prompt.contains("sense:")) << "the prompt does not know the sense line";
        const auto user = body.object().value("messages").toArray().at(1).toObject().value("content").toString();
        EXPECT_EQ(user, QStringLiteral("serendipity"));
        EXPECT_FALSE(body.object().value("stream").toBool());
    }
}

/// The part of speech rides the word answer the way the pronunciation does: read when it is
/// there, absent without failing the batch, and previewed while it is still arriving.
TEST_F(LlmTest, ReadsThePartOfSpeechAndShrugsWithoutIt)
{
    const QString withPos =
        QStringLiteral(R"({"word":"spring","ipa":"/sprɪŋ/","pos":"noun","en":"the season","zh":"春季"})");
    const auto parsed = parseExplanations(Channel::Word, envelope(results(withPos)), {"spring"});
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(parsed).at(0).pos, QStringLiteral("noun"));

    const auto bare = parseExplanations(Channel::Word, envelope(results(kGoodResult)), kAskedFor);
    ASSERT_TRUE(accepted(bare)) << errorOf(bare).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(bare).at(0).pos, QString());

    // A stream carries it like any other string field: whole once its quotes have closed.
    const QString content = results(withPos);
    const auto    partial = parsePartialExplanation(Channel::Word, content.left(content.indexOf(QStringLiteral("\"pos\""))));
    EXPECT_TRUE(partial.pos.isEmpty());
    const auto arrived = parsePartialExplanation(Channel::Word, content);
    EXPECT_EQ(arrived.pos, QStringLiteral("noun"));
}

/// The sense channel answers the Not-the-Sense? entry: one more distinct common sense with its
/// one-word hint, stamped with the asked-for word like the peek's line is.
TEST_F(LlmTest, TheSenseChannelAcceptsOneSenseWithItsHint)
{
    const QString content =
        QStringLiteral(R"({"results":[{"word":"crane","senses":[{"en":"a large bird with long legs","zh":"鹤","hint":"bird","imageable":true,"imageQuery":"crane bird"}]}]})");
    const auto parsed = parseExplanations(Channel::Sense, envelope(content), {"crane"}, "en", false);
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    const auto& explanation = std::get<QVector<Explanation>>(parsed).at(0);
    ASSERT_EQ(explanation.senses.size(), 1);
    EXPECT_EQ(explanation.senses.front().en, QStringLiteral("a large bird with long legs"));
    EXPECT_EQ(explanation.senses.front().hint, QStringLiteral("bird"));
    EXPECT_EQ(explanation.senses.front().imageQuery, QStringLiteral("crane bird"));
    EXPECT_EQ(explanation.title, QStringLiteral("crane")) << "the app stamps the asked-for word on";
}

/// Past the last sense the model answers with the same shape and nothing in `senses`. That is
/// the exhaustion answer the duty retires the entry on -- not a failure the batch dies of.
TEST_F(LlmTest, TheSenseChannelReadsAnEmptySensesArrayAsTheExhaustionAnswer)
{
    const auto parsed = parseExplanations(
        Channel::Sense, envelope(QStringLiteral(R"({"results":[{"word":"crane","senses":[]}]})")), {"crane"});
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    EXPECT_TRUE(std::get<QVector<Explanation>>(parsed).at(0).senses.isEmpty());

    // A sense without its definitions is still refused: the entry has nothing to swap in.
    EXPECT_FALSE(accepted(parseExplanations(
        Channel::Sense, envelope(QStringLiteral(R"({"results":[{"word":"crane","senses":[{"en":"a bird"}]}]})")), {"crane"})));
}

TEST_F(LlmTest, TheSensePromptAsksForOneMoreSenseWithAHintAndNeverStreams)
{
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const auto   body = QJsonDocument::fromJson(
        buildRequestBody(config, Channel::Sense, {QStringLiteral("crane\n- a tall machine with a long arm")}, "en"));
    const auto& root = body.object();
    EXPECT_FALSE(root.value("stream").toBool()) << "the entry's answer is drawn whole or not at all";
    const auto system = root.value("messages").toArray().at(0).toObject().value("content").toString();
    const auto user   = root.value("messages").toArray().at(1).toObject().value("content").toString();
    EXPECT_TRUE(system.contains("\"hint\"")) << "the prompt never asks for the one-word label";
    EXPECT_TRUE(system.contains("\"senses\"")) << "the prompt shows no format example";
    EXPECT_TRUE(user.contains(QStringLiteral("crane")) && user.contains(QStringLiteral("- a tall machine")))
        << "the user message dropped the word or the senses already shown";
}

TEST_F(LlmTest, TheSpellingCorrectionAppliesOnlyToASingleToken)
{
    EXPECT_TRUE(lens::llm::spellingApplies(QStringLiteral("recieve")));
    EXPECT_TRUE(lens::llm::spellingApplies(QStringLiteral("  recieve  ")));
    EXPECT_FALSE(lens::llm::spellingApplies(QStringLiteral("recieve it")));
    EXPECT_FALSE(lens::llm::spellingApplies(QStringLiteral("   ")));
}

TEST_F(LlmTest, TheSpellingRequestSendsOneWordWithoutStreaming)
{
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const auto   body = QJsonDocument::fromJson(buildRequestBody(config, Channel::Spell, {"recieve"}, "en"));
    EXPECT_FALSE(body.object().value("stream").toBool());
    EXPECT_EQ(body.object().value("messages").toArray().at(1).toObject().value("content").toString(), QStringLiteral("recieve"));
}

TEST_F(LlmTest, TheSpellingAnswerIsOneTokenAndAnyOtherShapeIsRefused)
{
    const auto accepted = lens::llm::parseCorrection(Channel::Spell, envelope(QStringLiteral(R"({"results":[{"corrected":"receive"}]})")));
    ASSERT_TRUE(std::holds_alternative<QString>(accepted));
    EXPECT_EQ(std::get<QString>(accepted), QStringLiteral("receive"));

    for (auto const* content : {R"({"results":[{"corrected":"receive it"}]})", R"({"results":[{"corrected":""}]})"}) {
        const auto refused = lens::llm::parseCorrection(Channel::Spell, envelope(QString::fromUtf8(content)));
        EXPECT_TRUE(std::holds_alternative<CorrectionRejected>(refused)) << content;
    }
}

TEST_F(LlmTest, TheGrammarAnswerIsTheWholeSentenceAndMayEqualTheOriginal)
{
    const auto parsed = lens::llm::parseCorrection(
        Channel::Grammar, envelope(QStringLiteral(R"({"results":[{"corrected":"She goes to school every day."}]})")));
    ASSERT_TRUE(std::holds_alternative<QString>(parsed));
    EXPECT_EQ(std::get<QString>(parsed), QStringLiteral("She goes to school every day."));
}

TEST_F(LlmTest, TheCorrectionMarksOnlyTheWordsThatChanged)
{
    const auto runs = lens::llm::correctionRuns(QStringLiteral("She go to school"), QStringLiteral("She goes to school"));
    QString    rebuilt;
    for (auto const& run : runs)
        rebuilt += run.text;
    EXPECT_EQ(rebuilt, QStringLiteral("She goes to school")) << "the runs must read as the corrected sentence";

    QStringList changed;
    for (auto const& run : runs)
        if (run.changed) changed << run.text;
    EXPECT_EQ(changed, (QStringList{"goes"})) << "only the word that moved is underlined";
}

TEST_F(LlmTest, ADeletedWordDoesNotMarkTheWordsAfterIt)
{
    // The model dropped "very": the words after it did not change, so none of them may be underlined.
    const auto runs = lens::llm::correctionRuns(QStringLiteral("it is very good now"), QStringLiteral("it is good now"));
    for (auto const& run : runs)
        EXPECT_FALSE(run.changed) << run.text.toStdString();
}

TEST_F(LlmTest, TheCorrectionOfAnUnchangedWordMarksNothing)
{
    const auto runs = lens::llm::correctionRuns(QStringLiteral("receive"), QStringLiteral("receive"));
    for (auto const& run : runs)
        EXPECT_FALSE(run.changed);
}

bool breakdownAccepted(const BreakdownResult& result)
{
    return std::holds_alternative<QVector<RoleSpan>>(result);
}

TEST_F(LlmTest, TheBreakdownRequestSendsOneWordPerLineWithoutStreaming)
{
    const Config config{QUrl("https://api.deepseek.com"), "sk-not-a-real-key", "deepseek-flash"};
    const auto   body = QJsonDocument::fromJson(buildRequestBody(config, Channel::Breakdown, {"The", "cat", "sleeps."}, "en"));
    const auto&  root = body.object();
    EXPECT_FALSE(root.value("stream").toBool()) << "the breakdown is drawn whole or not at all";
    EXPECT_FALSE(root.contains("stream_options")) << "a whole body asks for no usage stream";
    EXPECT_EQ(root.value("messages").toArray().at(1).toObject().value("content").toString(), QStringLiteral("The\ncat\nsleeps."))
        << "the indices the model answers with are line numbers, so one word must stay one line";
}

TEST_F(LlmTest, TheBreakdownSplitsOnWhitespaceAndKeepsPunctuationWithItsWord)
{
    EXPECT_EQ(breakdownWords(QStringLiteral("  The cat,\tsleeps.\n")), (QStringList{"The", "cat,", "sleeps."}));
}

TEST_F(LlmTest, TheBreakdownAppliesToEnglishSentencesOfEightyWordsOrFewer)
{
    EXPECT_TRUE(breakdownApplies(QStringLiteral("The cat sleeps.")));
    EXPECT_TRUE(breakdownApplies(QStringLiteral("It’s fine.")));
    EXPECT_TRUE(breakdownApplies(QStringList(80, QStringLiteral("word")).join(QLatin1Char(' '))));
    EXPECT_FALSE(breakdownApplies(QStringList(81, QStringLiteral("word")).join(QLatin1Char(' '))));
    EXPECT_FALSE(breakdownApplies(QStringLiteral("   ")));
    EXPECT_FALSE(breakdownApplies(QStringLiteral("Привет мир")));
    EXPECT_FALSE(breakdownApplies(QStringLiteral("这句话 is English?")));
}

TEST_F(LlmTest, TheGrammarCorrectionTakesTheSameSentencesAsTheBreakdown)
{
    EXPECT_TRUE(grammarApplies(QStringList(80, QStringLiteral("word")).join(QLatin1Char(' '))));
    EXPECT_FALSE(grammarApplies(QStringList(81, QStringLiteral("word")).join(QLatin1Char(' '))));
}

TEST_F(LlmTest, AFailedBreakdownOrCorrectionSaysWhyInOneReaderLine)
{
    EXPECT_EQ(failureLineFor(0), QStringLiteral("No connection or timeout."));
    EXPECT_EQ(failureLineFor(503), QStringLiteral("Service error (HTTP 503)."));
    EXPECT_EQ(failureLineFor(200), QStringLiteral("The answer could not be read."));
    EXPECT_EQ(failureLineFor(-1), QStringLiteral("The service is not set up."));
}

TEST_F(LlmTest, TheBreakdownRunsCoverTheSentenceAndColourOnlyTheMarkedWords)
{
    const QString           sentence = QStringLiteral("Although it was raining, the children played outside.");
    const QVector<RoleSpan> spans{{Role::Adverbial, 0, 3}, {Role::Subject, 4, 5}, {Role::Verb, 6, 6}, {Role::Adverbial, 7, 7}};
    const auto              runs = lens::llm::breakdownRuns(sentence, spans);

    QString rebuilt;
    for (auto const& run : runs)
        rebuilt += run.text;
    EXPECT_EQ(rebuilt, sentence) << "the runs must give the sentence back exactly";

    ASSERT_EQ(runs.size(), 7);
    EXPECT_EQ(runs.at(0).text, QStringLiteral("Although it was raining,"));
    EXPECT_EQ(runs.at(0).role, QStringLiteral("A")) << "whitespace inside a run keeps its colour";
    EXPECT_EQ(runs.at(1).text, QStringLiteral(" "));
    EXPECT_TRUE(runs.at(1).role.isEmpty()) << "the gap between two different roles is plain";
    EXPECT_EQ(runs.at(2).text, QStringLiteral("the children"));
    EXPECT_EQ(runs.at(2).role, QStringLiteral("S"));
    EXPECT_EQ(runs.at(4).text, QStringLiteral("played"));
    EXPECT_EQ(runs.at(4).role, QStringLiteral("V"));
    EXPECT_EQ(runs.at(6).text, QStringLiteral("outside."));
    EXPECT_EQ(runs.at(6).role, QStringLiteral("A"));
}

TEST_F(LlmTest, TheBreakdownLegendListsTheUsedRolesInFixedOrder)
{
    const QVector<RoleSpan> spans{{Role::Adverbial, 0, 0}, {Role::Subject, 1, 1}};
    EXPECT_EQ(lens::llm::breakdownRoles(spans), (QStringList{"S", "A"}));
}

TEST_F(LlmTest, TheBreakdownAcceptsSpansThatTakeEachWordAtMostOnce)
{
    const auto parsed = parseBreakdown(
        envelope(QStringLiteral(R"({"results":[{"spans":[{"role":"S","from":0,"to":1},{"role":"V","from":2,"to":2}]}]})")), 4);
    ASSERT_TRUE(breakdownAccepted(parsed));
    const auto& spans = std::get<QVector<RoleSpan>>(parsed);
    ASSERT_EQ(spans.size(), 2);
    EXPECT_EQ(spans.at(0).role, Role::Subject);
    EXPECT_EQ(spans.at(0).from, 0);
    EXPECT_EQ(spans.at(0).to, 1);
    EXPECT_EQ(spans.at(1).role, Role::Verb);
}

TEST_F(LlmTest, TheBreakdownRefusesTheWholeAnswerOnAnyBadSpan)
{
    const QStringList refused{
        QStringLiteral(R"({"results":[{"spans":[{"role":"S","from":0,"to":4}]}]})"),
        QStringLiteral(R"({"results":[{"spans":[{"role":"S","from":2,"to":1}]}]})"),
        QStringLiteral(R"({"results":[{"spans":[{"role":"S","from":0,"to":2},{"role":"V","from":2,"to":3}]}]})"),
        QStringLiteral(R"({"results":[{"spans":[{"role":"X","from":0,"to":1}]}]})"),
        QStringLiteral(R"({"results":[{"spans":[{"role":"S","from":0.5,"to":1}]}]})"),
        QStringLiteral(R"({"results":[{"spans":[{"role":"S","from":-1,"to":1}]}]})"),
        QStringLiteral(R"({"results":[{"spans":[]}]})"),
        QStringLiteral(R"({"results":[{"spans":[{"role":"S","from":0,"to":1}]},{"spans":[{"role":"V","from":0,"to":0}]}]})"),
    };
    for (auto const& content : refused)
        EXPECT_FALSE(breakdownAccepted(parseBreakdown(envelope(content), 4))) << content.toStdString();

    const auto valid = QStringLiteral(R"({"results":[{"spans":[{"role":"S","from":0,"to":1}]}]})");
    EXPECT_FALSE(breakdownAccepted(parseBreakdown(envelope(valid, QStringLiteral("length")), 4)))
        << "a cut-off answer is never complete";
}

TEST_F(LlmTest, FoldsSeveralEntityOrSentenceResultsIntoTheOneRequested)
{
    // A model may split a selected block (a numbered list, several sentences) into more entries
    // than the single item that was sent; fold them instead of failing the whole batch.
    const auto split = parseExplanations(
        Channel::Sentence,
        envelope(QStringLiteral(
            R"({"results":[{"en":"First line.","zh":"第一行。"},{"en":"Second line.","zh":"第二行。"}]})")),
        {"First line.\nSecond line."});
    ASSERT_TRUE(accepted(split)) << errorOf(split).toStdString();
    const auto& result = std::get<QVector<Explanation>>(split).at(0);
    EXPECT_EQ(result.title, "First line.\nSecond line.");
    EXPECT_EQ(result.en, "First line.\nSecond line.");
    EXPECT_EQ(result.zh, "第一行。\n第二行。");
}

TEST_F(LlmTest, ToleratesAMarkdownFencedContent)
{
    // A model often wraps its JSON in a ```json fence even when asked not to; that is not a
    // reason to throw the whole explanation away.
    const QString fenced = QStringLiteral(
        "```json\n{\"results\":[{\"word\":\"ubiquitous\",\"en\":\"x\",\"zh\":\"y\"}]}\n```");
    const auto parsed = parseExplanations(Channel::Word, envelope(fenced), {"ubiquitous"});
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(parsed).at(0).title, "ubiquitous");
}

TEST_F(LlmTest, MatchesAWordEchoCaseInsensitively)
{
    // The request sends a lower-cased lemma; a model may capitalize an acronym on echo. The
    // requested spelling wins, so the cache key and the bubble title stay canonical.
    const auto parsed = parseExplanations(
        Channel::Word,
        envelope(results(QStringLiteral(R"({"word":"QML","en":"a markup language","zh":"一种标记语言"})"))),
        {"qml"});
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(parsed).at(0).title, "qml");
}

TEST_F(LlmTest, RejectsTheWholeBatchOnAnyMalformedResponse)
{
    const struct {
        const char* what;
        QByteArray  body;
    } cases[] = {
        {"finish_reason=length, the JSON was cut off", envelope(kGoodResult, QStringLiteral("length"))},
        {"content is not JSON", envelope(QStringLiteral("not json at all"))},
        {"no results key", envelope(QStringLiteral("{}"))},
        {"missing field (no zh)", envelope(results(QStringLiteral(R"({"word":"ubiquitous","ipa":"/x/","en":"x"})")))},
        {"empty field (no en)",
         envelope(results(QStringLiteral(R"({"word":"ubiquitous","ipa":"/x/","en":"","zh":"y"})")))},
        {"misspelled echo",
         envelope(results(QStringLiteral(R"({"word":"ubiquitos","ipa":"/x/","en":"x","zh":"y"})")))},
        {"missing echo", envelope(results(QString()))},
        {"extra echo",
         envelope(results(QStringLiteral(
             R"({"word":"ubiquitous","ipa":"/x/","en":"x","zh":"y"},{"word":"extra","ipa":"/x/","en":"x","zh":"y"})")))},
    };

    for (const auto& item : cases) {
        SCOPED_TRACE(item.what);
        EXPECT_FALSE(accepted(parseExplanations(Channel::Word, item.body, kAskedFor)));
    }
}

/// A model sometimes writes one result per sense instead of one result with a `senses` array, and so
/// names its word once per sense. That is an answer to the question asked rather than a broken one:
/// the repeat is folded into the entry already held rather than failing the batch, because throwing
/// away definitions that had already arrived is the worse trade (API.md section 4).
TEST_F(LlmTest, FoldsAWordTheModelAnsweredTwiceInsteadOfFailingTheBatch)
{
    const QString twice =
        QStringLiteral(R"({"results":[{"word":"crane","ipa":"/kreɪn/","en":"a lifting machine","zh":"起重机","imageable":true,"imageQuery":"construction crane"},{"word":"Crane","ipa":"/kreɪn/","en":"a wading bird","zh":"鹤"}]})");

    const auto many = parseExplanations(Channel::Word, envelope(twice), {"crane"}, "en", true);
    ASSERT_TRUE(accepted(many)) << errorOf(many).toStdString();
    const auto folded = std::get<QVector<Explanation>>(many).at(0);
    EXPECT_EQ(folded.title, QStringLiteral("crane")) << "the requested spelling is the one kept";
    ASSERT_EQ(folded.senses.size(), 2) << "the repeat's definition was lost";
    EXPECT_EQ(folded.senses.at(0).en, QStringLiteral("a lifting machine"));
    EXPECT_EQ(folded.senses.at(1).en, QStringLiteral("a wading bird"));
    EXPECT_TRUE(folded.imageable) << "the first entry keeps its picture advice";

    // Single-sense mode keeps one: the fold respects the cap the parse already applied.
    const auto single = parseExplanations(Channel::Word, envelope(twice), {"crane"}, "en", false);
    ASSERT_TRUE(accepted(single)) << errorOf(single).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(single).at(0).senses.size(), 1);

    // The same definition twice is one sense: what the entry already holds is not appended again.
    const QString repeated = QStringLiteral(
        R"({"results":[{"word":"trowel","ipa":"/x/","en":"a small hand tool","zh":"抹刀"},{"word":"trowel","ipa":"/x/","en":"a small hand tool","zh":"抹刀"}]})");
    const auto same = parseExplanations(Channel::Word, envelope(repeated), {"trowel"}, "en", true);
    ASSERT_TRUE(accepted(same)) << errorOf(same).toStdString();
    EXPECT_EQ(std::get<QVector<Explanation>>(same).at(0).senses.size(), 1);
}

/// An answer that names a word nobody asked for is still a failure: folding a repeat is not the same
/// as accepting an entry the request never made.
TEST_F(LlmTest, StillRejectsAnAnswerThatNamesAWordNobodyAskedFor)
{
    const QString extra =
        QStringLiteral(R"({"results":[{"word":"crane","ipa":"/x/","en":"x","zh":"y"},{"word":"heron","ipa":"/x/","en":"x","zh":"y"}]})");
    EXPECT_FALSE(accepted(parseExplanations(Channel::Word, envelope(extra), {"crane"}, "en", true)));
}

TEST_F(LlmTest, AcceptsAWordResponseWithoutIPAForAnAcronym)
{
    // IPA is optional even on the word channel: a lone acronym has no pronunciation, and a
    // lone all-caps selection is routed there. The bubble simply draws no pronunciation line.
    const auto parsed = parseExplanations(
        Channel::Word,
        envelope(results(QStringLiteral(R"({"word":"qml","en":"a UI markup language","zh":"一种界面标记语言"})"))),
        {"qml"});
    ASSERT_TRUE(accepted(parsed)) << errorOf(parsed).toStdString();
    const auto& result = std::get<QVector<Explanation>>(parsed).at(0);
    EXPECT_EQ(result.title, "qml");
    EXPECT_TRUE(result.ipa.isEmpty());
}

TEST_F(LlmTest, ReadsTokenUsageOffTheEnvelope)
{
    const QJsonObject usage{{"prompt_tokens", 1200}, {"completion_tokens", 340}, {"total_tokens", 1540}};
    const QByteArray  body = QJsonDocument(QJsonObject{{"usage", usage}}).toJson(QJsonDocument::Compact);

    const Usage counts = parseUsage(body);
    EXPECT_EQ(counts.promptTokens, 1200);
    EXPECT_EQ(counts.completionTokens, 340);
}

TEST_F(LlmTest, TreatsMissingUsageAsZeroRatherThanAFailure)
{
    // A response with no usage is still a good explanation; only the tally loses a line.
    const QByteArray body = envelope(results(kGoodResult));
    EXPECT_TRUE(accepted(parseExplanations(Channel::Word, body, kAskedFor)));

    const Usage counts = parseUsage(body);
    EXPECT_EQ(counts.promptTokens, 0);
    EXPECT_EQ(counts.completionTokens, 0);
}

TEST_F(LlmTest, TreatsNonNumericUsageAsZero)
{
    const QJsonObject usage{{"prompt_tokens", QStringLiteral("many")}, {"completion_tokens", QJsonValue::Null}};
    const QByteArray  body = QJsonDocument(QJsonObject{{"usage", usage}}).toJson(QJsonDocument::Compact);

    const Usage counts = parseUsage(body);
    EXPECT_EQ(counts.promptTokens, 0);
    EXPECT_EQ(counts.completionTokens, 0);
}

/// A `data:` line is held back until its event's blank-line terminator has arrived: half an event
/// parsed is half an answer drawn, and a chunk boundary can fall anywhere.
TEST(SseFraming, HoldsAnEventUntilItsTerminatorArrives)
{
    QByteArray buffer = QByteArrayLiteral("data: {\"a\"");
    EXPECT_TRUE(takeSsePayloads(buffer).isEmpty());
    EXPECT_EQ(buffer, QByteArrayLiteral("data: {\"a\"")) << "a partial event must be kept whole";

    buffer += QByteArrayLiteral(":1}\n\n");
    const auto payloads = takeSsePayloads(buffer);
    ASSERT_EQ(payloads.size(), 1);
    EXPECT_EQ(payloads.front(), QByteArrayLiteral("{\"a\":1}"));
    EXPECT_TRUE(buffer.isEmpty()) << "a consumed event must leave nothing behind";
}

/// One read can carry several events, and the payloads come out in arrival order.
TEST(SseFraming, SplitsSeveralEventsOutOfOneRead)
{
    QByteArray buffer = QByteArrayLiteral("data: one\n\ndata: two\n\n");

    const auto payloads = takeSsePayloads(buffer);

    ASSERT_EQ(payloads.size(), 2);
    EXPECT_EQ(payloads.at(0), QByteArrayLiteral("one"));
    EXPECT_EQ(payloads.at(1), QByteArrayLiteral("two"));
}

/// An event that carries no data -- a keep-alive comment, an `event:` header -- contributes
/// nothing; only `[DONE]`, which does arrive as data, is handed back.
TEST(SseFraming, KeepsOnlyEventsThatCarryData)
{
    QByteArray buffer = QByteArrayLiteral(": keep-alive\n\nevent: ping\n\ndata: [DONE]\n\n");

    const auto payloads = takeSsePayloads(buffer);

    ASSERT_EQ(payloads.size(), 1);
    EXPECT_EQ(payloads.front(), QByteArrayLiteral("[DONE]"));
}

/// The terminator a provider writes is not this side's choice.
TEST(SseFraming, AcceptsTheCrlfTerminator)
{
    QByteArray buffer = QByteArrayLiteral("data: one\r\n\r\ndata: two\r\n\r\n");

    const auto payloads = takeSsePayloads(buffer);

    ASSERT_EQ(payloads.size(), 2);
    EXPECT_EQ(payloads.at(0), QByteArrayLiteral("one"));
    EXPECT_EQ(payloads.at(1), QByteArrayLiteral("two"));
}

/// A multi-byte character split across two reads is never decoded half-formed: the event is only
/// read once its terminator has arrived, so the character is whole by the time anything looks.
TEST(SseFraming, ReassemblesAUtf8CharacterSplitAcrossReads)
{
    const QByteArray frame = QByteArrayLiteral("data: ") + QStringLiteral("{\"word\":\"无限\"}").toUtf8() +
                             QByteArrayLiteral("\n\n");
    const int at = frame.indexOf(QStringLiteral("无").toUtf8());
    ASSERT_GE(at, 0);

    QByteArray buffer = frame.left(at + 2); // two bytes of a three-byte character
    EXPECT_TRUE(takeSsePayloads(buffer).isEmpty());

    buffer += frame.mid(at + 2);
    const auto payloads = takeSsePayloads(buffer);
    ASSERT_EQ(payloads.size(), 1);
    EXPECT_EQ(QJsonDocument::fromJson(payloads.front()).object().value("word").toString(),
              QStringLiteral("无限"))
        << "a character split across two reads arrived mangled";
}

/// The text, the reason the model stopped and the token counts all ride the stream's chunks.
TEST(StreamDelta, ReadsTheTextTheReasonAndTheUsage)
{
    const auto text = parseStreamDelta(
        QByteArrayLiteral(R"({"choices":[{"delta":{"content":"hello"},"finish_reason":null}]})"));
    ASSERT_TRUE(text);
    EXPECT_EQ(text->content, QStringLiteral("hello"));
    EXPECT_TRUE(text->finishReason.isEmpty()) << "the model has not said it stopped yet";
    EXPECT_TRUE(text->usage.isEmpty()) << "usage arrives only on the last chunk, when asked for";

    const auto last = parseStreamDelta(QByteArrayLiteral(
        R"({"choices":[{"delta":{},"finish_reason":"stop"}],"usage":{"prompt_tokens":12,"completion_tokens":9}})"));
    ASSERT_TRUE(last);
    EXPECT_TRUE(last->content.isEmpty());
    EXPECT_EQ(last->finishReason, QStringLiteral("stop"));
    EXPECT_EQ(last->usage.value("prompt_tokens").toInt(), 12);
    EXPECT_EQ(last->usage.value("completion_tokens").toInt(), 9);
}

/// `[DONE]`, an empty payload and a keep-alive comment are not chunks; none of them is a failure.
TEST(StreamDelta, AnswersNothingForDoneOrAKeepAlive)
{
    EXPECT_FALSE(parseStreamDelta(QByteArrayLiteral("[DONE]")));
    EXPECT_FALSE(parseStreamDelta(QByteArray()));
    EXPECT_FALSE(parseStreamDelta(QByteArrayLiteral(": keep-alive")));
}

/// The card grows a field at a time: a value is drawn only once its closing quote has arrived,
/// and one whose boundary has not is simply absent -- the same as a finished answer that omitted it.
TEST(PartialExplanation, DrawsOnlyTheValuesThatHaveClosed)
{
    const QString answer = QStringLiteral(
        R"({"results":[{"word":"ubiquitous","ipa":"/juːˈbɪkwɪtəs/","en":"existing everywhere","zh":"无处不在的"}]})");

    const auto midValue = parsePartialExplanation(Channel::Word, answer.left(answer.indexOf(QStringLiteral("ju")) + 2));
    EXPECT_EQ(midValue.title, QStringLiteral("ubiquitous"));
    EXPECT_TRUE(midValue.ipa.isEmpty()) << "a value that has not closed must be absent";
    EXPECT_TRUE(midValue.en.isEmpty()) << "nothing after the open value has been read yet";

    const auto whole = parsePartialExplanation(Channel::Word, answer);
    EXPECT_EQ(whole.ipa, QString::fromUtf8("/juːˈbɪkwɪtəs/"));
    EXPECT_EQ(whole.en, QStringLiteral("existing everywhere"));
    EXPECT_EQ(whole.zh, QString::fromUtf8("无处不在的"));
}

/// A chunk boundary can fall inside a JSON escape. Nothing is named until the escape and the
/// closing quote are both there -- and when they are, the quote the escape stands for is the value.
TEST(PartialExplanation, WaitsOutAnEscapeSplitByAChunkBoundary)
{
    const QString head = QStringLiteral("{\"results\":[{\"word\":\"a\\");
    const QString tail = QStringLiteral("\"b\",\"en\":\"present\"}]}");

    EXPECT_TRUE(parsePartialExplanation(Channel::Word, head).title.isEmpty())
        << "an unfinished escape must not name anything";

    const auto joined = parsePartialExplanation(Channel::Word, head + tail);
    EXPECT_EQ(joined.title, QStringLiteral("a\"b"));
    EXPECT_EQ(joined.en, QStringLiteral("present"));
}

/// With multiple senses asked for, the array grows the way the top-level fields do: a sense shows
/// up once its first value has closed, and its remaining values follow.
TEST(PartialExplanation, GrowsTheSenseArrayOneValueAtATime)
{
    const auto partial = parsePartialExplanation(
        Channel::Word,
        QStringLiteral(R"({"results":[{"word":"bank","ipa":"/bank/","senses":[{"en":"a financial house","zh":"银行"},{"en":"a river)"));

    EXPECT_EQ(partial.title, QStringLiteral("bank"));
    ASSERT_EQ(partial.senses.size(), 1);
    EXPECT_EQ(partial.senses.front().en, QStringLiteral("a financial house"));
    EXPECT_EQ(partial.senses.front().zh, QString::fromUtf8("银行"));
}

/// A boolean in the answer does not hold back what is written after it. The preview draws no picture
/// -- the entry arrives only once the whole answer has passed the schema -- but stopping the read on
/// `imageable` would keep the row behind it, and any second sense, from appearing as it is written.
TEST(PartialExplanation, ReadsPastABooleanToTheFieldsBehindIt)
{
    const auto behind = parsePartialExplanation(
        Channel::Word,
        QStringLiteral(R"({"results":[{"word":"trowel","en":"a small hand tool","imageable":true,"imageQuery":"garden trowel","zh":"抹刀"})"));

    EXPECT_EQ(behind.en, QStringLiteral("a small hand tool"));
    EXPECT_EQ(behind.zh, QString::fromUtf8("抹刀")) << "the read stopped at the boolean";
    EXPECT_FALSE(behind.imageable) << "the preview does not offer a picture before the answer is whole";

    const auto secondSense = parsePartialExplanation(
        Channel::Word,
        QStringLiteral(R"({"results":[{"word":"crane","senses":[{"en":"a lifting machine","zh":"起重机","imageable":true},{"en":"a wading bird","zh":"鹤"}]})"));

    ASSERT_EQ(secondSense.senses.size(), 2) << "the sense behind the boolean was lost";
    EXPECT_EQ(secondSense.senses.at(1).en, QStringLiteral("a wading bird"));
}

/// The entity and sentence channels echo no title: it is the reader's own selection, stamped on
/// further up, so this reads the definition lines alone.
TEST(PartialExplanation, LeavesTheTitleToTheCallerOnTheNonWordChannels)
{
    const auto partial = parsePartialExplanation(
        Channel::Entity, QStringLiteral(R"({"results":[{"en":"A major city.","zh":"一座大城市")"));

    EXPECT_TRUE(partial.title.isEmpty());
    EXPECT_EQ(partial.en, QStringLiteral("A major city."));
    EXPECT_EQ(partial.zh, QString::fromUtf8("一座大城市"));
}

/// A fenced answer is common enough that the finished one is accepted through the fence; the
/// preview reads through it the same way rather than showing the fence as content.
TEST(PartialExplanation, ReadsThroughAMarkdownFence)
{
    const auto partial = parsePartialExplanation(
        Channel::Word, QStringLiteral("```json\n{\"results\":[{\"word\":\"qml\",\"en\":\"a UI markup language\""));

    EXPECT_EQ(partial.title, QStringLiteral("qml"));
    EXPECT_EQ(partial.en, QStringLiteral("a UI markup language"));
}

/// The card may draw a half-written answer; the store never does. The very text that yields a
/// preview here is rejected by the whole-response check the cache sits behind.
TEST(PartialExplanation, NeverStandsInForTheWholeResponseCheck)
{
    const QString half = QStringLiteral(
        R"({"results":[{"word":"ubiquitous","ipa":"/juːˈbɪkwɪtəs/","en":"existing everywhere")");

    EXPECT_EQ(parsePartialExplanation(Channel::Word, half).en, QStringLiteral("existing everywhere"))
        << "this is what the card would draw";
    EXPECT_FALSE(accepted(parseExplanations(Channel::Word, envelope(half), kAskedFor)))
        << "an answer that is still being written must not pass the check";
}

namespace {

QByteArray sseEvent(QString const& content, QString const& finish = {}, QJsonObject const& usage = {})
{
    QJsonObject choice{{"delta", QJsonObject{{"content", content}}}};
    choice.insert(QStringLiteral("finish_reason"), finish.isEmpty() ? QJsonValue() : QJsonValue(finish));
    QJsonObject root{{"choices", QJsonArray{choice}}};
    if (not usage.isEmpty())
        root.insert(QStringLiteral("usage"), usage);
    return QByteArrayLiteral("data: ") + QJsonDocument(root).toJson(QJsonDocument::Compact) + QByteArrayLiteral("\n\n");
}

} // namespace

/// The sniff waits for enough bytes, a chunk that completes no drawn field is silent, and the
/// finished answer goes through the same check as a whole body without a rebuilt envelope.
TEST(StreamAssembly, SniffsAcrossReadsDeduplicatesPreviewsAndFinishes)
{
    const QString head = QStringLiteral(R"({"results":[{"word":"ubiquitous")");
    const QString mid  = QStringLiteral(R"(,"ipa":"/juːˈbɪkwɪtəs/","en":"existing everywhere")");
    const QString tail = QStringLiteral(R"(,"zh":"无处不在的"}]})");

    const QByteArray first = sseEvent(head);
    StreamAssembler  assembler(Channel::Word, kAskedFor, QStringLiteral("zh"), false);

    // Four bytes are not enough to tell a stream from a body; the event is held, not guessed at.
    EXPECT_FALSE(assembler.feed(first.left(4), true).has_value());
    auto title = assembler.feed(first.mid(4), true);
    ASSERT_TRUE(title.has_value());
    EXPECT_EQ(title->title, QStringLiteral("ubiquitous"));
    EXPECT_TRUE(title->en.isEmpty()) << "the first preview carries only what had closed";

    auto fields = assembler.feed(sseEvent(mid), false); // an error status draws nothing
    EXPECT_FALSE(fields.has_value());
    fields = assembler.feed(QByteArray{}, true); // the same bytes, now with the answer status
    ASSERT_TRUE(fields.has_value());
    EXPECT_EQ(fields->en, QStringLiteral("existing everywhere"));

    // A chunk that closes no new field produces the same preview, which is not worth emitting.
    EXPECT_FALSE(assembler.feed(sseEvent(QStringLiteral(" ")), true).has_value());

    auto zh = assembler.feed(sseEvent(tail, QStringLiteral("stop"), QJsonObject{{"prompt_tokens", 12}, {"completion_tokens", 9}}), true);
    ASSERT_TRUE(zh.has_value());
    EXPECT_EQ(zh->zh, QString::fromUtf8("无处不在的"));

    const auto result = assembler.finish(QByteArray{});
    ASSERT_TRUE(accepted(result)) << errorOf(result).toStdString();
    EXPECT_TRUE(assembler.streamed());
    const auto& explanations = std::get<QVector<Explanation>>(result);
    ASSERT_EQ(explanations.size(), 1);
    EXPECT_EQ(explanations.front().en, QStringLiteral("existing everywhere"));
    EXPECT_EQ(assembler.usage().promptTokens, 12);
    EXPECT_EQ(assembler.usage().completionTokens, 9);
}

/// A reply whose bytes are one JSON envelope takes the whole-body path, previews and all.
TEST(StreamAssembly, FallsBackToAWholeBody)
{
    StreamAssembler assembler(Channel::Word, kAskedFor, QStringLiteral("en"), false);

    EXPECT_FALSE(assembler.feed(envelope(results(kGoodResult)), true).has_value());
    const auto result = assembler.finish(QByteArray{});
    ASSERT_TRUE(accepted(result)) << errorOf(result).toStdString();
    EXPECT_FALSE(assembler.streamed());
    EXPECT_EQ(std::get<QVector<Explanation>>(result).front().title, QStringLiteral("ubiquitous"));
}

/// A stream that ends without the model saying it stopped is today's failure, never a short answer.
TEST(StreamAssembly, RejectsAStreamThatNeverSaysStop)
{
    StreamAssembler assembler(Channel::Word, kAskedFor, QStringLiteral("en"), false);
    assembler.feed(sseEvent(QStringLiteral(R"({"results":[{"word":"ubiquitous","en":"x","zh":"y"}]})")), true);

    const auto  result  = assembler.finish(QByteArray{});
    const auto* message = std::get_if<QString>(&result);
    ASSERT_NE(message, nullptr);
    EXPECT_TRUE(message->contains("stopped before finishing")) << message->toStdString();
}
