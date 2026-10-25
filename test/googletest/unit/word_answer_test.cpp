/**
 * @file word_answer_test.cpp
 * @brief One word answer across the wire, the store and the card: the field table's contract.
 *
 * The schema test fails when a field is added to the wire contract and not to the table; the
 * round-trip test fails when a table row has no home in the stored entry or the card; the stream
 * test fails when the preview and the whole-answer check stop reading the same rows.
 */

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <variant>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <gtest/gtest.h>

#include "app/duties/word_answer.h"
#include "core/document/known_store.h"
#include "llm/llm_pure.h"
#include "llm_support.h"
#include "support.h"
#include "util/word_fields.h"

namespace {

using namespace lens;

// TEST_F pastes the fixture name into a class definition, so it has to be unqualified.
using lens::test::LlmTest;

namespace wa   = lens::app::word_answer;
namespace word = lens::word;

/// @return The chat-completions envelope the client's parser expects.
QByteArray envelope(QString const& content)
{
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", QStringLiteral("stop")}, {"message", QJsonObject{{"content", content}}}}}}})
        .toJson(QJsonDocument::Compact);
}

llm::Sense oneSense()
{
    return llm::Sense{QStringLiteral("a small hand tool"),
                      QStringLiteral("抹刀"),
                      QStringLiteral("una pala pequeña"),
                      QString{},
                      true,
                      QStringLiteral("garden trowel")};
}

/// @return An answer carrying a distinct value in every row of the table.
llm::Explanation fullAnswer()
{
    llm::Explanation answer;
    answer.title        = QStringLiteral("trowel");
    answer.ipa          = QStringLiteral("/ˈtraʊəl/");
    answer.pos          = QStringLiteral("noun");
    answer.en           = QStringLiteral("a small hand tool");
    answer.zh           = QStringLiteral("抹刀");
    answer.translation  = QStringLiteral("una pala pequeña");
    answer.etymology    = QStringLiteral("From Old French truelle.");
    answer.example      = QStringLiteral("He dug with a trowel.");
    answer.signpost     = QStringLiteral("small hand tool");
    answer.collocations = {QStringLiteral("garden ~"), QStringLiteral("~ blade")};
    answer.imageable    = true;
    answer.imageQuery   = QStringLiteral("garden trowel");
    answer.senses       = {oneSense()};
    return answer;
}

QString textOf(llm::Explanation const& answer, word::Slot const slot)
{
    switch (slot) {
        case word::Slot::Ipa:
            return answer.ipa;
        case word::Slot::Pos:
            return answer.pos;
        case word::Slot::En:
            return answer.en;
        case word::Slot::Zh:
            return answer.zh;
        case word::Slot::Translation:
            return answer.translation;
        case word::Slot::Etymology:
            return answer.etymology;
        case word::Slot::Example:
            return answer.example;
        case word::Slot::Signpost:
            return answer.signpost;
        case word::Slot::ImageQuery:
            return answer.imageQuery;
        default:
            return {};
    }
}

QStringList listOf(llm::Explanation const& answer, word::Slot const slot)
{
    return slot == word::Slot::Collocations ? answer.collocations : QStringList{};
}

bool flagOf(llm::Explanation const& answer, word::Slot const slot)
{
    return slot == word::Slot::Imageable ? answer.imageable : false;
}

QStringList collocationsOf(QVariantMap const& payload)
{
    QStringList collocations;
    for (auto const& value : payload.value(QStringLiteral("collocations")).toList())
        collocations << value.toString();
    return collocations;
}

struct WordAnswerTest : ::testing::Test {
protected:
    void SetUp() override
    {
        test::requireFilterTablesLoaded();
        path = std::filesystem::temp_directory_path() / "lens_word_answer_test.json";
        std::filesystem::remove(path);
    }

    void TearDown() override
    {
        std::filesystem::remove(path);
    }

    std::filesystem::path path;
};

/// The schema and the table have to name the same fields. A row added to one and not the other
/// is exactly the hand copy this module exists to remove, so it is a failure here.
TEST(WordAnswerFields, NamesEveryFieldTheSchemaDoes)
{
    std::ifstream in(test::sourceDir() / "data" / "llm" / "response.word.schema.json");
    ASSERT_TRUE(in) << "cannot open the word response schema";
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto        root   = QJsonDocument::fromJson(QByteArray::fromStdString(text)).object();
    const auto        result = root.value("properties").toObject().value("results").toObject().value("items").toObject();

    QStringList wire = result.value("properties").toObject().keys();
    wire.removeAll(QStringLiteral("word"));
    wire.removeAll(QStringLiteral("senses"));
    QStringList table;
    for (word::Field const& field : word::kFields)
        if (field.scope == word::Scope::Answer)
            table << QString::fromStdString(std::string{field.key});
    wire.sort();
    table.sort();
    EXPECT_EQ(wire, table);

    QStringList senseWire = result.value("properties").toObject().value("senses").toObject().value("items").toObject().value("properties").toObject().keys();
    QStringList senseTable;
    for (word::Field const& field : word::kFields)
        if (field.scope == word::Scope::Sense)
            senseTable << QString::fromStdString(std::string{field.key});
    senseWire.sort();
    senseTable.sort();
    EXPECT_EQ(senseWire, senseTable);
}

/// Every row crosses both conversions and reaches the payload. A row the module forgot shows up
/// as its default here rather than as a field the card silently never draws.
TEST_F(WordAnswerTest, EveryFieldSurvivesTheStoreAndTheCard)
{
    const auto answer = fullAnswer();
    const auto cached = wa::toCache(answer, true);

    EXPECT_EQ(cached.ipa, answer.ipa.toStdString());
    EXPECT_EQ(cached.pos, answer.pos.toStdString());
    EXPECT_EQ(cached.en, answer.en.toStdString());
    EXPECT_EQ(cached.zh, answer.zh.toStdString());
    EXPECT_EQ(cached.translation, answer.translation.toStdString());
    EXPECT_EQ(cached.etymology, answer.etymology.toStdString());
    EXPECT_EQ(cached.signpost, answer.signpost.toStdString());
    EXPECT_EQ(cached.collocations, (std::vector<std::string>{"garden ~", "~ blade"}));
    EXPECT_TRUE(cached.imageable);
    EXPECT_EQ(cached.imageQuery, answer.imageQuery.toStdString());
    ASSERT_TRUE(cached.example.has_value());
    EXPECT_EQ(*cached.example, answer.example.toStdString());
    ASSERT_EQ(cached.senses.size(), 1);
    EXPECT_EQ(cached.senses.front().imageQuery, answer.senses.front().imageQuery.toStdString());
    EXPECT_EQ(wa::fromCache(cached, answer.title), answer);

    auto store = core::KnownStore::load(path);
    store.cachePut(answer.title.toStdString(), cached, {.language = "en"});
    store.save();
    const auto hit = core::KnownStore::load(path).cacheGet(answer.title.toStdString(), {.language = "en"});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(wa::fromCache(*hit, answer.title), answer);

    wa::Switches switches;
    switches.etymology = true;
    switches.example   = true;
    const auto payload = wa::cardFields(answer, QStringLiteral("en"), switches, 2);
    EXPECT_EQ(payload.value("ipa").toString(), answer.ipa);
    EXPECT_EQ(payload.value("pos").toString(), answer.pos);
    EXPECT_EQ(payload.value("signpost").toString(), answer.signpost);
    EXPECT_EQ(collocationsOf(payload), answer.collocations);
    EXPECT_EQ(payload.value("etymology").toString(), answer.etymology);
    EXPECT_EQ(payload.value("example").toString(), answer.example);
    EXPECT_TRUE(payload.contains(QStringLiteral("spans")));
}

/// The example is the one row whose absence and emptiness are different answers (docs/adr/0033).
TEST_F(WordAnswerTest, TheExampleKeepsItsThreeStates)
{
    auto answer = fullAnswer();
    EXPECT_FALSE(wa::toCache(answer, false).example.has_value()) << "an example was stored though it was not bought";

    answer.example.clear();
    const auto asked = wa::toCache(answer, true);
    ASSERT_TRUE(asked.example.has_value());
    EXPECT_TRUE(asked.example->empty()) << "a settled 'none to give' became 'not bought'";

    auto store = core::KnownStore::load(path);
    store.cachePut(answer.title.toStdString(), asked, {.language = "en"});
    store.save();
    const auto hit = core::KnownStore::load(path).cacheGet(answer.title.toStdString(), {.language = "en"});
    ASSERT_TRUE(hit.has_value());
    ASSERT_TRUE(hit->example.has_value()) << "the empty example was dropped on the way through the store";
    EXPECT_TRUE(hit->example->empty());
}

/// A switch clears its own block and leaves every other one alone; the picture pair is one switch.
TEST_F(WordAnswerTest, EachSwitchClearsOnlyItsOwnBlock)
{
    const auto   answer = fullAnswer();
    wa::Switches all;
    all.etymology = true;
    all.example   = true;

    const auto on = wa::cardFields(answer, QStringLiteral("en"), all, 2);
    EXPECT_EQ(on.value("ipa").toString(), answer.ipa);
    EXPECT_EQ(on.value("pos").toString(), answer.pos);
    EXPECT_EQ(on.value("signpost").toString(), answer.signpost);
    EXPECT_EQ(collocationsOf(on), answer.collocations);
    EXPECT_EQ(on.value("etymology").toString(), answer.etymology);
    EXPECT_EQ(on.value("example").toString(), answer.example);

    auto off     = all;
    off.ipa      = false;
    auto payload = wa::cardFields(answer, QStringLiteral("en"), off, 2);
    EXPECT_TRUE(payload.value("ipa").toString().isEmpty());
    EXPECT_EQ(payload.value("pos").toString(), answer.pos) << "another block was cleared with the pronunciation";

    off     = all;
    off.pos = false;
    payload = wa::cardFields(answer, QStringLiteral("en"), off, 2);
    EXPECT_TRUE(payload.value("pos").toString().isEmpty());
    EXPECT_EQ(payload.value("ipa").toString(), answer.ipa);

    off          = all;
    off.signpost = false;
    payload      = wa::cardFields(answer, QStringLiteral("en"), off, 2);
    EXPECT_TRUE(payload.value("signpost").toString().isEmpty());
    EXPECT_EQ(collocationsOf(payload), answer.collocations);

    off              = all;
    off.collocations = false;
    payload          = wa::cardFields(answer, QStringLiteral("en"), off, 2);
    EXPECT_TRUE(collocationsOf(payload).isEmpty());
    EXPECT_EQ(payload.value("signpost").toString(), answer.signpost);

    off           = all;
    off.etymology = false;
    payload       = wa::cardFields(answer, QStringLiteral("en"), off, 2);
    EXPECT_FALSE(payload.contains(QStringLiteral("etymology"))) << "a bought setting was turned into an empty block";
    EXPECT_EQ(payload.value("example").toString(), answer.example);

    off         = all;
    off.example = false;
    payload     = wa::cardFields(answer, QStringLiteral("en"), off, 2);
    EXPECT_FALSE(payload.contains(QStringLiteral("example")));
    EXPECT_EQ(payload.value("etymology").toString(), answer.etymology);

    const auto& sense = answer.senses.front();
    const auto  shown = wa::senseRow(sense, QStringLiteral("en"), true, 2);
    EXPECT_TRUE(shown.value("imageable").toBool());
    EXPECT_EQ(shown.value("imageQuery").toString(), sense.imageQuery);
    const auto hidden = wa::senseRow(sense, QStringLiteral("en"), false, 2);
    EXPECT_FALSE(hidden.value("imageable").toBool());
    EXPECT_TRUE(hidden.value("imageQuery").toString().isEmpty());
}

/// A row carries one language, chosen by the explanation language the answer was bought in.
TEST_F(WordAnswerTest, TheCardDrawsTheLanguageTheAnswerWasBoughtIn)
{
    const auto answer = fullAnswer();
    EXPECT_EQ(wa::answerText(answer, QStringLiteral("en")), answer.en);
    EXPECT_EQ(wa::answerText(answer, QStringLiteral("zh")), answer.zh);
    EXPECT_EQ(wa::answerText(answer, QStringLiteral("es")), answer.translation);

    const auto& sense = answer.senses.front();
    const auto  en    = wa::senseRow(sense, QStringLiteral("en"), true, 2);
    EXPECT_EQ(en.value("en").toString(), sense.en);
    EXPECT_TRUE(en.value("text").toString().isEmpty());
    const auto zh = wa::senseRow(sense, QStringLiteral("zh"), true, 2);
    EXPECT_TRUE(zh.value("en").toString().isEmpty());
    EXPECT_EQ(zh.value("text").toString(), sense.zh);
    const auto es = wa::senseRow(sense, QStringLiteral("es"), true, 2);
    EXPECT_EQ(es.value("text").toString(), sense.translation);

    wa::Switches switches;
    const auto   entry = wa::chainEntry(answer, QStringLiteral("en"), switches, 2);
    EXPECT_EQ(entry.value("title").toString(), answer.title);
    EXPECT_EQ(entry.value("ipa").toString(), answer.ipa);
    EXPECT_EQ(entry.value("text").toString(), answer.en);
    EXPECT_EQ(entry.value("signpost").toString(), answer.signpost);
    EXPECT_EQ(collocationsOf(entry), answer.collocations);
    EXPECT_TRUE(entry.contains(QStringLiteral("spans")));
}

/// The store keeps the rules the wire parser applied: a non-en/zh entry without its translation
/// is not readable, and the picture pair follows the query rather than a bare flag.
TEST_F(WordAnswerTest, AStoredEntryKeepsTheReadersRules)
{
    auto answer = fullAnswer();
    auto cached = wa::toCache(answer, true);

    auto store = core::KnownStore::load(path);
    store.cachePut(answer.title.toStdString(), cached, {.language = "es"});
    store.save();
    auto hit = core::KnownStore::load(path).cacheGet(answer.title.toStdString(), {.language = "es"});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->translation, answer.translation.toStdString());

    auto missing = cached;
    missing.translation.clear();
    store.cachePut(answer.title.toStdString(), missing, {.language = "es"});
    store.save();
    EXPECT_FALSE(core::KnownStore::load(path).cacheGet(answer.title.toStdString(), {.language = "es"}).has_value())
        << "an entry with no line in the reader's language came back as usable";

    auto noPicture      = cached;
    noPicture.imageable = true;
    noPicture.imageQuery.clear();
    store.cachePut(answer.title.toStdString(), noPicture, {.language = "en"});
    store.save();
    hit = core::KnownStore::load(path).cacheGet(answer.title.toStdString(), {.language = "en"});
    ASSERT_TRUE(hit.has_value());
    EXPECT_FALSE(hit->imageable) << "a bare flag was stored as advice the duty could act on";
    EXPECT_TRUE(hit->imageQuery.empty());
}

/// The preview and the finished answer read the same table: every row a preview may draw agrees
/// with the whole answer, and the picture advice waits for the answer to pass its checks.
TEST_F(LlmTest, TheStreamAndTheWholeAnswerReadTheSameFields)
{
    const QString content = QStringLiteral(
        R"({"results":[{"word":"trowel","ipa":"/ˈtraʊəl/","pos":"noun","en":"a small hand tool","zh":"抹刀",)"
        R"("translation":"una pala pequeña","etymology":"From Old French truelle.","example":"He dug with a trowel.",)"
        R"("signpost":"small hand tool","collocations":["garden ~","~ blade"],"imageable":true,"imageQuery":"garden trowel"}]})");

    const auto whole = llm::parseExplanations(llm::Channel::Word, envelope(content), {QStringLiteral("trowel")});
    ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(whole));
    const auto& answer  = std::get<QVector<llm::Explanation>>(whole).front();
    const auto  preview = llm::parsePartialExplanation(llm::Channel::Word, content);

    for (word::Field const& field : word::kFields) {
        if (field.scope != word::Scope::Answer)
            continue;
        SCOPED_TRACE(std::string{field.key});
        if (not field.preview) {
            if (field.slot == word::Slot::Imageable)
                EXPECT_FALSE(preview.imageable);
            if (field.slot == word::Slot::ImageQuery)
                EXPECT_TRUE(preview.imageQuery.isEmpty());
            continue;
        }
        switch (field.kind) {
            case word::ValueKind::Text:
                EXPECT_EQ(textOf(preview, field.slot), textOf(answer, field.slot));
                break;
            case word::ValueKind::Flag:
                EXPECT_EQ(flagOf(preview, field.slot), flagOf(answer, field.slot));
                break;
            case word::ValueKind::TextList:
                EXPECT_EQ(listOf(preview, field.slot), listOf(answer, field.slot));
                break;
        }
    }

    // A sense grows its lines as they close, with the same rule for the picture advice.
    const QString senses = QStringLiteral(
        R"({"results":[{"word":"crane","senses":[{"en":"a lifting machine","zh":"起重机","translation":"grúa","imageable":true,"imageQuery":"construction crane"},)"
        R"({"en":"a wading bird","zh":"鹤","translation":"grulla","imageable":true,"imageQuery":"crane bird"}]}]})");
    const auto many = llm::parseExplanations(llm::Channel::Word, envelope(senses), {QStringLiteral("crane")}, QStringLiteral("en"), true);
    ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(many));
    const auto kept     = std::get<QVector<llm::Explanation>>(many).front();
    const auto streamed = llm::parsePartialExplanation(llm::Channel::Word, senses);
    ASSERT_EQ(streamed.senses.size(), kept.senses.size());
    for (qsizetype i = 0; i < kept.senses.size(); ++i) {
        EXPECT_EQ(streamed.senses.at(i).en, kept.senses.at(i).en);
        EXPECT_EQ(streamed.senses.at(i).zh, kept.senses.at(i).zh);
        EXPECT_EQ(streamed.senses.at(i).translation, kept.senses.at(i).translation);
        EXPECT_FALSE(streamed.senses.at(i).imageable) << "a picture was offered before the answer was whole";
        EXPECT_TRUE(streamed.senses.at(i).imageQuery.isEmpty());
    }
}

}
