/**
 * @file phase2_explanation_test.cpp
 * @brief Offline Phase 2 explanation contracts, persistence and provider round trips.
 */

#include <QCoreApplication>
#include <QDate>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>

#include "app/app_controller.h"
#include "app/duties/cost_duty.h"
#include "app/duties/explanation_duty.h"
#include "app/duties/model_catalog_duty.h"
#include "app/duties/storage_duty.h"
#include "core/document/known_store.h"
#include "llm/llm_pricing.h"
#include "llm/llm_pure.h"
#include "llm_support.h"

namespace {

using namespace lens;
using lens::test::LlmTest;

QByteArray envelope(QJsonArray const& results)
{
    auto const content = QString::fromUtf8(QJsonDocument{QJsonObject{{"results", results}}}.toJson());
    return QJsonDocument{QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"}, {"message", QJsonObject{{"content", content}}}}}},
                                     {"usage", QJsonObject{{"prompt_tokens", 12}, {"completion_tokens", 9}}}}}
        .toJson();
}

QJsonObject word(int const count = 3, QString const& translation = QStringLiteral("translation"))
{
    QJsonArray senses;
    for (int i = 0; i < count; ++i)
        senses.append(QJsonObject{{"en", QStringLiteral("sense %1").arg(i)}, {"zh", QStringLiteral("meaning %1").arg(i)}, {"translation", translation}});
    return {{"word", "bank"}, {"ipa", "/bank/"}, {"senses", senses}};
}

QJsonObject plainWord(QString const& lemma, QString const& etymology = {}, QString const& example = {})
{
    QJsonObject value{{"word", lemma}, {"ipa", "/x/"}, {"en", "English"}, {"zh", "中文"}};
    if (not etymology.isEmpty()) value["etymology"] = etymology;
    if (not example.isEmpty()) value["example"] = example;
    return value;
}

struct QtApplication {
    int              argc     = 1;
    char             name[32] = "phase2_explanation_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

struct Reply final : QNetworkReply {
    explicit Reply(QByteArray body_, QObject* parent)
        : QNetworkReply(parent), body(std::move(body_))
    {
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this] { setFinished(true); emit finished(); });
    }
    void abort() override
    {}
    qint64 readData(char* data, qint64 const size) override
    {
        if (body.isEmpty()) return -1;
        auto const count = std::min(size, static_cast<qint64>(body.size()));
        std::memcpy(data, body.constData(), static_cast<std::size_t>(count));
        body.remove(0, count);
        return count;
    }
    QByteArray body;
};

struct Manager final : QNetworkAccessManager {
    QByteArray     response = envelope({word()});
    QByteArray     posted;
    QUrl           endpoint;
    QUrl           listed;
    QByteArray     listedAuthorization;
    QByteArray     listedGoogleKey;
    QByteArray     modelResponseOverride;
    bool           usesTestToken = false;
    int            posts         = 0;
    int            gets          = 0;
    QNetworkReply* createRequest(Operation operation, QNetworkRequest const& request, QIODevice* device) override
    {
        // The model list is a GET and carries no body at all, so it is answered without a device:
        // the assertions here are about the POST, and reading a device this call does not have is
        // how a null dereference gets into a test fake.
        if (operation == GetOperation) {
            ++gets;
            listed              = request.url();
            listedAuthorization = request.rawHeader("Authorization");
            listedGoogleKey     = request.rawHeader("x-goog-api-key");
            // One source, answered by host. The body spans two vendors and carries the two kinds of
            // entry the reader must never see: a routing variant and a model that also writes image.
            const QByteArray ids = not modelResponseOverride.isEmpty()
                                       ? modelResponseOverride
                                   : request.url().host() == QLatin1String("openrouter.ai")
                                       ? QByteArrayLiteral(R"({"data":[
        {"id":"openai/gpt-4.1-mini","architecture":{"output_modalities":["text"]}},
        {"id":"openai/gpt-4.1-mini:free","architecture":{"output_modalities":["text"]}},
        {"id":"openai/image-preview","architecture":{"output_modalities":["text","image"]}},
        {"id":"deepseek/deepseek-chat","architecture":{"output_modalities":["text"]}},
        {"id":"google/gemini-2.5-flash","architecture":{"output_modalities":["text"]}}
    ]})")
                                       : QByteArrayLiteral(R"({"data":[]})");
            return new Reply{ids, this};
        }
        ++posts;
        endpoint      = request.url();
        usesTestToken = request.rawHeader("Authorization") == "Bearer fake-test-token";
        posted        = device->readAll();
        return new Reply{response, this};
    }
};

void run(app::ExplanationDuty& duty, llm::LlmClient& client, QString const& text = QStringLiteral("bank"))
{
    QEventLoop loop;
    QObject::connect(&client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QObject::connect(&client, &llm::LlmClient::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    duty.runSelectionAction("explain", text);
    loop.exec();
}

app::PendingSelection selectionFor(QString const& lemma)
{
    app::PendingSelection result;
    result.kind   = core::SelectionKind::Word;
    result.text   = lemma;
    result.lemma  = lemma;
    result.preset = "default";
    result.anchor = QPoint{200, 400};
    return result;
}

app::PendingSelection selection()
{
    return selectionFor("bank");
}

/// @brief The graph every explanation case needs: a store on a temp dir, the stub transport, and
///        the duty with its cost, in dependency order.
///
/// What a case varies it passes in -- the model, the cost duty's clock -- and the catalog cases add
/// their own catalog beside it. The test declares its own QtApplication first: one process holds
/// one, so a rig cannot own it.
struct Rig {
    QTemporaryDir        temporary;
    core::KnownStore     store;
    app::StorageDuty     storage;
    Manager              manager;
    llm::LlmClient       client;
    llm::Pricing const   pricing;
    app::CostDuty        cost;
    app::ExplanationDuty duty;

    explicit Rig(QString const& model = QStringLiteral("test-model"), app::CostDuty::DateProvider clock = {})
        : store{core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json")},
          storage{store},
          client{{QUrl{"https://example.invalid"}, "fake-test-token", model}, &manager},
          pricing{llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json")},
          cost{storage.statsStore(), client, pricing, clockOrDefault(std::move(clock))},
          duty{storage, client, cost}
    {
    }

    /// @return @p clock, or the duty's own today() when a case pinned none: an empty std::function
    ///         passed explicitly would replace the default argument rather than fall back to it.
    static app::CostDuty::DateProvider clockOrDefault(app::CostDuty::DateProvider clock)
    {
        return clock ? std::move(clock) : app::CostDuty::DateProvider{[] { return QDate::currentDate(); }};
    }

    /// @brief Ask for @p lemma, and wait only when a request actually went out: a word already
    ///        stored is answered out of the cache before this returns.
    void explain(QString const& lemma)
    {
        const int before = manager.posts;
        duty.setSelection(selectionFor(lemma));
        QEventLoop loop;
        QObject::connect(&client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
        QObject::connect(&client, &llm::LlmClient::failed, &loop, &QEventLoop::quit);
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        duty.runSelectionAction("explain", lemma);
        if (manager.posts > before) loop.exec();
    }
};

} // namespace

TEST_F(LlmTest, Phase2ChainFieldsAreReadWhenTheModelGivesThemAndAbsentOtherwise)
{
    auto withChain            = plainWord("bank");
    withChain["signpost"]     = QStringLiteral("a place for money");
    withChain["collocations"] = QJsonArray{QStringLiteral("~ account"), QStringLiteral("river ~"), QStringLiteral("central ~")};
    auto const parsed         = llm::parseExplanations(llm::Channel::Word, envelope({withChain}), {"bank"}, "en", false);
    ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(parsed));
    auto const& entry = std::get<QVector<llm::Explanation>>(parsed).front();
    EXPECT_EQ(entry.signpost, QStringLiteral("a place for money"));
    // Two collocations at most, whatever the model sends.
    EXPECT_EQ(entry.collocations, (QStringList{QStringLiteral("~ account"), QStringLiteral("river ~")}));

    // A word with no label still parses, and the card draws none.
    auto const bare = llm::parseExplanations(llm::Channel::Word, envelope({plainWord("bank")}), {"bank"}, "en", false);
    ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(bare));
    EXPECT_TRUE(std::get<QVector<llm::Explanation>>(bare).front().signpost.isEmpty());
    EXPECT_TRUE(std::get<QVector<llm::Explanation>>(bare).front().collocations.isEmpty());
}

TEST_F(LlmTest, Phase2AcceptsLegacyAndCapsOrderedSenses)
{
    auto const legacy = llm::parseExplanations(llm::Channel::Word,
                                               envelope({QJsonObject{{"word", "bank"}, {"en", "first"}, {"zh", "meaning"}}}),
                                               {"bank"});
    ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(legacy));
    EXPECT_EQ(std::get<QVector<llm::Explanation>>(legacy).front().senses.size(), 1);
    for (bool const multiple : {false, true}) {
        auto const parsed = llm::parseExplanations(llm::Channel::Word, envelope({word(5)}), {"bank"}, "en", multiple);
        ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(parsed));
        auto const result = std::get<QVector<llm::Explanation>>(parsed).front();
        EXPECT_EQ(result.senses.size(), multiple ? 3 : 1);
        EXPECT_EQ(result.en, "sense 0");
        EXPECT_EQ(result.ipa, "/bank/");
    }
}

/// The repeated word is deliberately not in this list any more: a model that names its word once per
/// sense is answering the question asked, and the repeat is folded into the first entry rather than
/// failing the batch (llm_pure_test.cpp, FoldsAWordTheModelAnsweredTwiceInsteadOfFailingTheBatch).
TEST_F(LlmTest, Phase2RejectsMalformedNestedSenses)
{
    for (auto const& senses : QJsonArray{QJsonArray{}, "wrong type", QJsonArray{7}, QJsonArray{QJsonObject{{"en", "valid"}}}, QJsonArray{QJsonObject{{"en", " "}, {"zh", "valid"}}}}) {
        auto value      = word();
        value["senses"] = senses;
        EXPECT_TRUE(std::holds_alternative<QString>(llm::parseExplanations(llm::Channel::Word, envelope({value}), {"bank"}, "en", true)));
    }
    auto invalidTail      = word(4);
    auto senses           = invalidTail["senses"].toArray();
    senses[3]             = QJsonObject{{"en", "tail"}};
    invalidTail["senses"] = senses;
    EXPECT_TRUE(std::holds_alternative<QString>(llm::parseExplanations(llm::Channel::Word, envelope({invalidTail}), {"bank"}, "en", true)));
}

TEST_F(LlmTest, Phase2AdditionalLanguagesRequireTextOnEveryChannel)
{
    for (auto const* language : {"es", "ja"}) {
        for (auto const channel : {llm::Channel::Word, llm::Channel::Entity, llm::Channel::Sentence}) {
            QJsonObject const value  = channel == llm::Channel::Word ? word() : QJsonObject{{"en", "English"}, {"zh", "Chinese"}, {"translation", "selected language"}};
            auto const        parsed = llm::parseExplanations(channel, envelope({value}), {"bank"}, language, true);
            ASSERT_TRUE(std::holds_alternative<QVector<llm::Explanation>>(parsed));
            EXPECT_FALSE(std::get<QVector<llm::Explanation>>(parsed).front().translation.isEmpty());
            auto missing = value;
            if (channel == llm::Channel::Word)
                missing = word(2, QString{});
            else
                missing.remove("translation");
            EXPECT_TRUE(std::holds_alternative<QString>(llm::parseExplanations(channel, envelope({missing}), {"bank"}, language, true)));
        }
    }
}

TEST_F(LlmTest, Phase2CachePreservesLegacyEntriesAndSeparatesAllContexts)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    auto const path = std::filesystem::path{temporary.path().toStdWString()} / "fixture.json";
    {
        std::ofstream file{path};
        file << R"({"untouched":"sentinel","cache":{"en":{"bank":{"ipa":"","en":"legacy","zh":"legacy meaning"},"broken":{"ipa":7,"en":"x","zh":"y"}}}})";
    }
    auto store = core::KnownStore::load(path);
    ASSERT_TRUE(store.cacheGet("bank", {"en", false}));
    EXPECT_FALSE(store.cacheGet("bank", {"en", true}));
    EXPECT_FALSE(store.cacheGet("broken"));
    for (auto const* language : {"en", "zh", "es", "ja"}) {
        for (bool const multiple : {false, true}) {
            if (std::string{language} == "en" and not multiple) continue;
            core::WordCache entry{.ipa = "/bank/", .en = language, .zh = "meaning", .translation = language, .etymology = "from " + std::string{language}};
            entry.senses = {{language, "meaning", language}, {"second", "meaning 2", language}};
            store.cachePut("bank", entry, {language, multiple});
        }
    }
    store.save();
    auto const restored = core::KnownStore::load(path);
    EXPECT_EQ(restored.document().at("untouched"), "sentinel");
    EXPECT_EQ(restored.cacheGet("bank", {"en", false})->en, "legacy");
    // A legacy entry has no origin, and one must not be invented for it.
    EXPECT_TRUE(restored.cacheGet("bank", {"en", false})->etymology.empty());
    for (auto const* language : {"en", "zh", "es", "ja"}) {
        for (bool const multiple : {false, true}) {
            auto const entry = restored.cacheGet("bank", {language, multiple});
            ASSERT_TRUE(entry);
            EXPECT_EQ(entry->senses.size(), std::string{language} == "en" and not multiple ? 0u : multiple ? 2u
                                                                                                           : 1u);
            // Outside the cache key, so it survives every context this loop covers.
            if (not(std::string{language} == "en" and not multiple))
                EXPECT_EQ(entry->etymology, "from " + std::string{language});
        }
    }
}

TEST_F(LlmTest, Phase2LanguagesRoundTripThroughDutyTransportAndCache)
{
    // One language a sense: the reader picked the language they want to read, and the response's
    // other half is not it (UI-SPEC.md 4.3).
    QtApplication qt;
    for (auto const* language : {"en", "es", "ja"}) {
        Rig rig;
        rig.store.setExplanationLang(language);
        rig.store.document()["multiSense"] = "true";
        rig.explain("bank");
        ASSERT_EQ(rig.duty.bubble().value("senses").toList().size(), 3);
        auto const first  = rig.duty.bubble().value("senses").toList().front().toMap();
        const bool asking = language == std::string{"en"};
        EXPECT_EQ(first.value("text").toString(), asking ? QString{} : QString{"translation"});
        EXPECT_EQ(first.value("en").toString(), asking ? QString{"sense 0"} : QString{});
        if (language != std::string{"en"})
            EXPECT_TRUE(rig.manager.posted.contains(language == std::string{"es"} ? "Spanish" : "Japanese"));
        ASSERT_TRUE(rig.store.cacheGet("bank", {language, true}));
        rig.duty.dismissBubble();
        rig.duty.runSelectionAction("explain", "bank");
        EXPECT_EQ(rig.manager.posts, 1);
        EXPECT_EQ(rig.duty.bubble().value("senses").toList().size(), 3);
        EXPECT_EQ(rig.store.cacheGet("bank", {language, true})->senses.size(), 3u);
    }
}

TEST_F(LlmTest, Phase2TheClickPutsTheCardUpBeforeTheAnswerIsWritten)
{
    QtApplication qt;
    Rig           rig;
    rig.manager.response = envelope({plainWord("bank")});

    rig.duty.setSelection(selection());
    QEventLoop loop;
    QObject::connect(&rig.client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QObject::connect(&rig.client, &llm::LlmClient::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    rig.duty.runSelectionAction("explain", "bank");

    // The click's own frame has a card on it: the word the reader pointed at, the badge the store
    // already gives it, and nothing where the answer will go. No clock runs under the wait and no
    // verdict is on offer -- a word nothing has explained is not yet a word being judged (ADR 0146).
    auto const waiting = rig.duty.bubble();
    EXPECT_TRUE(waiting.value("waiting").toBool());
    // The card also reserves the height its answer is expected to reach, and keeps reserving it through
    // every preview, so nothing on screen moves until the whole answer lands (docs/adr/0146).
    EXPECT_TRUE(waiting.value("hold").toBool());
    EXPECT_GT(waiting.value("waitingLines").toInt(), 2);
    EXPECT_EQ(waiting.value("title").toString(), QStringLiteral("bank"));
    EXPECT_EQ(waiting.value("status").toString(), QStringLiteral("new"));
    EXPECT_TRUE(waiting.value("en").toString().isEmpty());
    EXPECT_FALSE(waiting.value("countsDown").toBool());
    EXPECT_FALSE(waiting.value("verdictButtons").toBool());
    EXPECT_TRUE(rig.duty.notice().isEmpty());
    EXPECT_EQ(rig.manager.posts, 1);

    loop.exec();

    // The answer is written into that card, and the wait's own flags end with it.
    auto const answered = rig.duty.bubble();
    EXPECT_FALSE(answered.value("waiting").toBool());
    EXPECT_FALSE(answered.value("hold").toBool()) << "the settled answer left the card on its reservation";
    EXPECT_EQ(answered.value("en").toString(), QStringLiteral("English"));
    EXPECT_TRUE(answered.value("countsDown").toBool());
    EXPECT_TRUE(answered.value("verdictButtons").toBool());
}

TEST_F(LlmTest, Phase2AWaitingCardComesDownWithTheNoticeWhenNoAnswerArrives)
{
    QtApplication qt;
    Rig           rig;
    rig.manager.response = QByteArrayLiteral(R"({"choices":[{"finish_reason":"stop","message":{"content":"not an answer"}}]})");

    rig.duty.setSelection(selection());
    QEventLoop loop;
    QObject::connect(&rig.client, &llm::LlmClient::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    rig.duty.runSelectionAction("explain", "bank");
    ASSERT_TRUE(rig.duty.bubble().value("waiting").toBool());

    loop.exec();

    // A card nothing is coming to fill does not stay on screen breathing: the notice says why, and it
    // takes the card down as it comes up.
    EXPECT_TRUE(rig.duty.bubble().isEmpty());
    EXPECT_FALSE(rig.duty.notice().isEmpty());
}

TEST_F(LlmTest, Phase2AWaitingCardGoesDownWhenTheReaderMovesOnFromItsWord)
{
    QtApplication qt;
    Rig           rig;
    rig.manager.response = envelope({plainWord("bank")});

    rig.duty.setSelection(selection());
    QEventLoop loop;
    QObject::connect(&rig.client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    rig.duty.runSelectionAction("explain", "bank");
    ASSERT_TRUE(rig.duty.bubble().value("waiting").toBool());

    // The reader takes another selection without asking again. The answer is still worth storing -- the
    // request was paid for -- but it is no longer theirs to see, and the card waiting for it would
    // breathe for a word nobody is asking about (ADR 0146).
    rig.duty.setSelection(selectionFor("river"));
    loop.exec();

    EXPECT_TRUE(rig.duty.bubble().isEmpty());
    EXPECT_TRUE(rig.duty.notice().isEmpty()) << "an answer the reader had moved on from was reported as a failure";
    ASSERT_TRUE(rig.store.cacheGet("bank", {"en", false}));
    EXPECT_EQ(rig.store.cacheGet("bank", {"en", false})->en, "English");
}

TEST_F(LlmTest, Phase2AStoredWordNeedsNoWaitingCardBecauseItsAnswerIsAlreadyInHand)
{
    QtApplication qt;
    Rig           rig;
    rig.manager.response = envelope({plainWord("bank")});
    rig.explain("bank");
    ASSERT_FALSE(rig.duty.bubble().value("waiting").toBool());

    rig.duty.dismissBubble();
    rig.duty.runSelectionAction("explain", "bank");
    EXPECT_EQ(rig.manager.posts, 1) << "a stored word went to the wire again";
    EXPECT_FALSE(rig.duty.bubble().value("waiting").toBool());
    EXPECT_EQ(rig.duty.bubble().value("en").toString(), QStringLiteral("English"));
}

TEST_F(LlmTest, Phase2ThePeekAnswersTheStandingWordFromThePairTheAnswerCarried)
{
    QtApplication qt;
    Rig           rig;
    rig.manager.response = envelope({plainWord("bank")});
    rig.explain("bank");
    ASSERT_EQ(rig.duty.bubble().value("en").toString(), QStringLiteral("English"));

    QStringList lines;
    QObject::connect(&rig.duty, &app::ExplanationDuty::peekArrived, [&](QString line) { lines.push_back(std::move(line)); });

    // The word channel always buys the Chinese pair along with the explanation, so the held globe
    // under a Chinese mother tongue is answered by the entry the standing card came from.
    rig.storage.section().set<std::string>("motherTongue", "zh");
    rig.duty.requestPeek();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.front(), QStringLiteral("中文"));
    EXPECT_EQ(rig.manager.posts, 1) << "a pair the word channel had already paid for was bought again";

    // A peek is a word card's thing: nothing stands, nothing is asked, nothing answers.
    rig.duty.dismissBubble();
    rig.duty.requestPeek();
    EXPECT_EQ(lines.size(), 1) << "a peek without a standing card answered";
    EXPECT_EQ(rig.manager.posts, 1);
}

TEST_F(LlmTest, Phase2ThePeekAsksTheModelInTheMotherTongueAndKeepsTheLineForTheSession)
{
    QtApplication qt;
    Rig           rig;
    rig.explain("bank");

    QStringList lines;
    QObject::connect(&rig.duty, &app::ExplanationDuty::peekArrived, [&](QString line) { lines.push_back(std::move(line)); });

    // A mother tongue the word answer does not carry goes to the peek channel, in that language.
    rig.storage.section().set<std::string>("motherTongue", "es");
    rig.manager.response =
        envelope(QJsonArray{QJsonObject{{"line", QStringLiteral("la dicha de hallar algo bueno sin buscarlo")}}});
    QEventLoop loop;
    QObject::connect(&rig.duty, &app::ExplanationDuty::peekArrived, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    rig.duty.requestPeek();
    loop.exec();

    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.front(), QStringLiteral("la dicha de hallar algo bueno sin buscarlo"));
    EXPECT_EQ(rig.manager.posts, 2);
    const auto messages = QJsonDocument::fromJson(rig.manager.posted).object().value("messages").toArray();
    EXPECT_TRUE(messages.at(0).toObject().value("content").toString().contains(QStringLiteral("Write the line in Spanish.")))
        << "the peek did not go out under the mother tongue";
    EXPECT_EQ(messages.at(1).toObject().value("content").toString(), QStringLiteral("bank"));

    // The second hold on the same word is answered from the session, not from the model.
    rig.duty.requestPeek();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines.back(), lines.front());
    EXPECT_EQ(rig.manager.posts, 2) << "a line the session had already paid for was bought again";
}

/// A sentence card holds the globe as a word card does, but it has no lemma: the peek asks for the
/// text the card was made from, and a Chinese mother tongue is asked too, since a sentence answer's
/// Chinese pair is not kept.
TEST_F(LlmTest, Phase2ThePeekAnswersASentenceFromItsText)
{
    QtApplication qt;
    Rig           rig;
    const QString text   = QStringLiteral("The bank was closed on Sunday.");
    rig.manager.response = envelope({QJsonObject{{"en", "The bank did not open on Sunday."}, {"zh", "银行周日没开门。"}}});

    app::PendingSelection sentence;
    sentence.kind   = core::SelectionKind::Sentence;
    sentence.text   = text;
    sentence.anchor = QPoint{200, 400};
    rig.duty.setSelection(sentence);
    run(rig.duty, rig.client, text);
    ASSERT_EQ(rig.duty.bubble().value("type").toString(), QStringLiteral("sentence"));

    QStringList lines;
    QObject::connect(&rig.duty, &app::ExplanationDuty::peekArrived, [&](QString line) { lines.push_back(std::move(line)); });

    rig.storage.section().set<std::string>("motherTongue", "es");
    rig.manager.response = envelope(QJsonArray{QJsonObject{{"line", QStringLiteral("El banco no abrió el domingo.")}}});
    QEventLoop loop;
    QObject::connect(&rig.duty, &app::ExplanationDuty::peekArrived, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    rig.duty.requestPeek();
    loop.exec();

    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.front(), QStringLiteral("El banco no abrió el domingo."));
    const auto messages = QJsonDocument::fromJson(rig.manager.posted).object().value("messages").toArray();
    EXPECT_EQ(messages.at(1).toObject().value("content").toString(), text) << "the peek did not ask about the sentence's text";
    EXPECT_EQ(rig.manager.posts, 2);

    // The second hold on the same sentence is answered from the session.
    rig.duty.requestPeek();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(rig.manager.posts, 2) << "a line the session had already paid for was bought again";

    // The Chinese pair the sentence answer carried was not kept, so Chinese is asked for like any other.
    rig.storage.section().set<std::string>("motherTongue", "zh");
    rig.manager.response = envelope(QJsonArray{QJsonObject{{"line", QStringLiteral("银行周日关门了。")}}});
    QEventLoop chineseLoop;
    QObject::connect(&rig.duty, &app::ExplanationDuty::peekArrived, &chineseLoop, &QEventLoop::quit);
    QTimer::singleShot(2000, &chineseLoop, &QEventLoop::quit);
    rig.duty.requestPeek();
    chineseLoop.exec();
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines.back(), QStringLiteral("银行周日关门了。"));
    EXPECT_EQ(rig.manager.posts, 3);
}

/// The peek answers the sense the card is showing: a hold on a swapped sense asks for that
/// sense's line -- the wire names the sense, and a Chinese mother tongue is answered by the pair
/// the sense channel bought with the sense -- and the line comes back to the same sense on the
/// second hold without the model being asked twice.
TEST_F(LlmTest, Phase2ThePeekAnswersTheSenseTheCardIsShowing)
{
    QtApplication qt;
    Rig           rig;
    rig.explain("bank");

    QVariantMap swapped;
    QObject::connect(&rig.duty, &app::ExplanationDuty::senseSwapped, [&](QVariantMap map) { swapped = std::move(map); });
    rig.manager.response =
        envelope(QJsonArray{QJsonObject{{"word", "bank"}, {"senses", QJsonArray{QJsonObject{{"en", "the land beside a river"}, {"zh", "河岸"}, {"translation", "la orilla de un río"}, {"hint", "riverside"}}}}}});
    QEventLoop swapLoop;
    QObject::connect(&rig.duty, &app::ExplanationDuty::senseSwapped, &swapLoop, &QEventLoop::quit);
    QTimer::singleShot(2000, &swapLoop, &QEventLoop::quit);
    rig.duty.cycleSense();
    swapLoop.exec();
    ASSERT_EQ(swapped.value("index").toInt(), 1);

    // A Spanish mother tongue goes to the peek channel, and the wire names the sense standing.
    rig.storage.section().set<std::string>("motherTongue", "es");
    QStringList lines;
    QObject::connect(&rig.duty, &app::ExplanationDuty::peekArrived, [&](QString line) { lines.push_back(std::move(line)); });
    rig.manager.response =
        envelope(QJsonArray{QJsonObject{{"line", QStringLiteral("la orilla del río")}}});
    QEventLoop peekLoop;
    QObject::connect(&rig.duty, &app::ExplanationDuty::peekArrived, &peekLoop, &QEventLoop::quit);
    QTimer::singleShot(2000, &peekLoop, &QEventLoop::quit);
    rig.duty.requestPeek();
    peekLoop.exec();

    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines.front(), QStringLiteral("la orilla del río"));
    const auto messages = QJsonDocument::fromJson(rig.manager.posted).object().value("messages").toArray();
    EXPECT_EQ(messages.at(1).toObject().value("content").toString(),
              QStringLiteral("bank\nsense: the land beside a river"))
        << "the peek did not name the sense the card is showing";
    // The second hold on the same sense is answered from the session.
    rig.duty.requestPeek();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines.back(), lines.front());
    EXPECT_EQ(rig.manager.posts, 3) << "a line the session had already paid for was bought again";

    // A Chinese mother tongue is answered by the pair the sense channel bought with the sense:
    // the hold costs nothing, and the line is the swapped sense's own.
    rig.storage.section().set<std::string>("motherTongue", "zh");
    rig.duty.requestPeek();
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines.back(), QStringLiteral("河岸"));
    EXPECT_EQ(rig.manager.posts, 3) << "a Chinese peek was bought when the sense already carried the pair";
}

/// The review card's life is the pointer's: a dismissal the row asks for while the pointer is on
/// the card waits for it to leave, and the panel closing takes the card down held or not.
TEST_F(LlmTest, Phase2AReviewCardStaysWhileThePointerIsOnIt)
{
    QtApplication qt;
    Rig           rig;
    rig.explain("bank");

    rig.duty.reviewWord(QStringLiteral("bank"), {200, 400}, {60, 20});
    EXPECT_EQ(rig.duty.bubble().value("title").toString(), QStringLiteral("bank"));

    // The row's dismissal, with the pointer arrived on the card: it waits.
    rig.duty.holdReviewCard(true);
    rig.duty.dismissReview();
    EXPECT_EQ(rig.duty.bubble().value("title").toString(), QStringLiteral("bank"))
        << "the review card went while the pointer was on it";
    // A second arrival report is not a second hold; the waiting dismissal still waits.
    rig.duty.holdReviewCard(true);
    EXPECT_EQ(rig.duty.bubble().value("title").toString(), QStringLiteral("bank"));

    // The pointer leaving the card lands what waited.
    rig.duty.holdReviewCard(false);
    EXPECT_TRUE(rig.duty.bubble().isEmpty()) << "the dismissal outlived the pointer leaving the card";

    // The panel closing is not the pointer leaving: it takes the card down held or not.
    rig.duty.reviewWord(QStringLiteral("bank"), {200, 400}, {60, 20});
    rig.duty.holdReviewCard(true);
    rig.duty.dismissReviewNow();
    EXPECT_TRUE(rig.duty.bubble().isEmpty()) << "a held review card outlived the panel closing";

    // With the pointer never on it, the row's dismissal is the immediate one it always was.
    rig.duty.reviewWord(QStringLiteral("bank"), {200, 400}, {60, 20});
    rig.duty.dismissReview();
    EXPECT_TRUE(rig.duty.bubble().isEmpty());
}

/// A hover over a word with nothing stored gets a hint beside the word, not a notice, and the hover itself
/// asks nothing of the model. The hint carries the button that does; the row's dismissal takes the hint down.
TEST_F(LlmTest, Phase2ReviewMissRaisesAHintNotANotice)
{
    QtApplication qt;
    Rig           rig;

    rig.duty.reviewWord(QStringLiteral("gazebo"), {200, 400}, {60, 20});
    EXPECT_TRUE(rig.duty.notice().isEmpty()) << "a hover over an unstored word raised a notice";
    EXPECT_EQ(rig.duty.wordHint().value("lemma").toString(), QStringLiteral("gazebo"));
    EXPECT_EQ(rig.duty.wordHint().value("x").toInt(), 200);
    EXPECT_FALSE(rig.duty.wordHint().value("body").toString().isEmpty());
    EXPECT_EQ(rig.duty.wordHint().value("action").toString(), QStringLiteral("Explain now"));
    EXPECT_EQ(rig.manager.posts, 0) << "the hover asked the model";

    rig.duty.dismissReview();
    EXPECT_TRUE(rig.duty.wordHint().isEmpty()) << "the row's dismissal left the hint up";
}

/// While the pointer is on the hint, the row's dismissal waits for it to leave, so the button stays reachable.
TEST_F(LlmTest, Phase2WordHintStaysWhileThePointerIsOnIt)
{
    QtApplication qt;
    Rig           rig;

    rig.duty.reviewWord(QStringLiteral("gazebo"), {200, 400}, {60, 20});
    rig.duty.holdWordHint(true);
    rig.duty.dismissReview();
    EXPECT_FALSE(rig.duty.wordHint().isEmpty()) << "the hint went while the pointer was on it";

    rig.duty.holdWordHint(false);
    EXPECT_TRUE(rig.duty.wordHint().isEmpty()) << "the hint outlived the pointer leaving it";
}

/// The hint's button is the one path from a hover to a request: it asks the model once and takes the hint down.
TEST_F(LlmTest, Phase2WordHintButtonAsksTheModelOnce)
{
    QtApplication qt;
    Rig           rig;

    // The fake model answers for "bank", so the request is one it can echo back.
    rig.duty.reviewWord(QStringLiteral("bank"), {200, 400}, {60, 20});
    ASSERT_FALSE(rig.duty.wordHint().isEmpty());

    const int  before = rig.manager.posts;
    QEventLoop loop;
    QObject::connect(&rig.client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QObject::connect(&rig.client, &llm::LlmClient::failed, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    rig.duty.explainLemma(QStringLiteral("bank"));
    EXPECT_TRUE(rig.duty.wordHint().isEmpty()) << "the button left the hint up";
    if (rig.manager.posts > before) loop.exec();
    EXPECT_EQ(rig.manager.posts, before + 1) << "the button did not ask the model exactly once";
}

/// The Not-the-Sense? entry's cycle: the part of speech is a display switch over a stored field,
/// the click asks the sense channel for the next distinct common sense, and past the last one
/// the cycle turns back to the most common one without asking again.
TEST_F(LlmTest, Phase2TheNotTheSenseEntryCyclesTheSensesTheSessionBought)
{
    QtApplication qt;
    Rig           rig;

    auto answer          = plainWord("bank");
    answer["pos"]        = "noun";
    answer["signpost"]   = "a place for money";
    rig.manager.response = envelope({answer});
    // The switch is a display one, the way the pronunciation's is: what was bought stays stored,
    // and the card draws it or not as the setting stands when the card is drawn.
    rig.store.document()["pos"] = "false";
    rig.explain("bank");
    EXPECT_EQ(rig.manager.posts, 1);
    EXPECT_TRUE(rig.duty.bubble().value("pos").toString().isEmpty()) << "a part of speech was drawn with the switch off";

    rig.duty.dismissBubble();
    rig.store.document()["pos"] = "true";
    rig.explain("bank");
    EXPECT_EQ(rig.manager.posts, 1) << "the stored part of speech was bought a second time";
    EXPECT_EQ(rig.duty.bubble().value("pos").toString(), QStringLiteral("noun"));
    EXPECT_EQ(rig.duty.bubble().value("senseCycle").toBool(), true);
    EXPECT_EQ(rig.duty.bubble().value("senseIndex").toInt(), 0);
    EXPECT_TRUE(rig.duty.bubble().value("senseHint").toString().isEmpty())
        << "the entry offered a word no request had paid for";

    // The click asks for the next distinct common sense on the sense channel, with the senses
    // already shown listed under the word, and the answer swaps the row in place.
    QVariantMap swapped;
    QObject::connect(&rig.duty, &app::ExplanationDuty::senseSwapped, [&](QVariantMap map) { swapped = std::move(map); });
    rig.manager.response =
        envelope(QJsonArray{QJsonObject{{"word", "bank"}, {"senses", QJsonArray{QJsonObject{{"en", "the land beside a river"}, {"zh", "河岸"}, {"hint", "riverside"}, {"imageable", true}, {"imageQuery", "river bank"}}}}}});
    QEventLoop loop;
    QObject::connect(&rig.duty, &app::ExplanationDuty::senseSwapped, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    rig.duty.cycleSense();
    loop.exec();

    EXPECT_EQ(rig.manager.posts, 2);
    const auto messages = QJsonDocument::fromJson(rig.manager.posted).object().value("messages").toArray();
    EXPECT_TRUE(messages.at(0).toObject().value("content").toString().contains(QStringLiteral("\"hint\"")))
        << "the sense channel went out without asking for the one-word label";
    const auto user = messages.at(1).toObject().value("content").toString();
    EXPECT_TRUE(user.contains(QStringLiteral("bank")) && user.contains(QStringLiteral("- English")))
        << "the wire payload dropped the word or the sense already shown";
    ASSERT_FALSE(swapped.isEmpty());
    EXPECT_EQ(swapped.value("index").toInt(), 1);
    EXPECT_EQ(swapped.value("hint").toString(), QString()) << "the entry offered a sense that was not bought yet";
    EXPECT_EQ(swapped.value("row").toMap().value("en").toString(), QStringLiteral("the land beside a river"));
    EXPECT_EQ(swapped.value("signpost").toString(), QStringLiteral("riverside"))
        << "the swapped sense carried no label of its own";
    EXPECT_EQ(rig.duty.bubble().value("senses").toList().at(0).toMap().value("en").toString(),
              QStringLiteral("the land beside a river"))
        << "the payload stopped agreeing with the card";

    // The next click asks again, and a whole answer without a sense has said there is nothing
    // further: the entry offers the way back to the most common sense instead.
    rig.manager.response = envelope(QJsonArray{QJsonObject{{"word", "bank"}, {"senses", QJsonArray{}}}});
    QEventLoop exhaustedLoop;
    QObject::connect(&rig.duty, &app::ExplanationDuty::senseSwapped, &exhaustedLoop, &QEventLoop::quit);
    QTimer::singleShot(2000, &exhaustedLoop, &QEventLoop::quit);
    rig.duty.cycleSense();
    exhaustedLoop.exec();

    EXPECT_EQ(rig.manager.posts, 3);
    EXPECT_EQ(swapped.value("index").toInt(), 1) << "an exhausted cycle still moved onto a sense it did not have";
    EXPECT_EQ(swapped.value("exhausted").toBool(), true);
    EXPECT_EQ(swapped.value("hint").toString(), QStringLiteral("a place for money"))
        << "the way back to the first sense carried no label for it";

    // And the turn itself is the session's: no third request is paid for a rotation.
    rig.duty.cycleSense();
    EXPECT_EQ(rig.manager.posts, 3);
    EXPECT_EQ(swapped.value("index").toInt(), 0);
    EXPECT_EQ(swapped.value("row").toMap().value("en").toString(), QStringLiteral("English"));
    EXPECT_EQ(swapped.value("hint").toString(), QStringLiteral("riverside"));

    // With multiple senses on, the rows are the selector themselves: the entry has no cycle to move.
    rig.storage.section().set<std::string>("multiSense", "true");
    rig.duty.cycleSense();
    EXPECT_EQ(rig.manager.posts, 3) << "a cycle was moved while the rows were the selector";
    EXPECT_EQ(swapped.value("index").toInt(), 0);
}

TEST_F(LlmTest, Phase2TheEtymologySettingBringsTheOriginAlongAndKeepsItOutOfTheKey)
{
    QtApplication qt;
    Rig           rig;

    auto const askedForOrigin = [&] {
        // The prompt is a string inside the posted body, so its own quotes arrive escaped; the
        // question is what the model was told, which means reading the body.
        const auto messages = QJsonDocument::fromJson(rig.manager.posted).object().value("messages").toArray();
        if (messages.isEmpty()) return false;
        return messages.at(0).toObject().value("content").toString().contains(QStringLiteral("\"etymology\":\"...\""));
    };

    // Explained with the setting off, the word is bought without its origin and the card shows none.
    rig.manager.response = envelope({plainWord("bank")});
    rig.explain("bank");
    EXPECT_FALSE(askedForOrigin()) << "the origin was paid for though the setting was off";
    EXPECT_TRUE(rig.duty.bubble().value("etymology").toString().isEmpty());

    // The setting decides what is asked for, never what was already paid for: turning it on does
    // not go back and buy the origin of a word explained while it was off (docs/adr/0019).
    rig.duty.dismissBubble();
    rig.store.document()["etymology"] = "true";
    rig.explain("bank");
    EXPECT_EQ(rig.manager.posts, 1) << "the stored definition was bought a second time";
    EXPECT_TRUE(rig.duty.bubble().value("etymology").toString().isEmpty());

    // Asked for from the start, the origin arrives with the definition and is drawn under it.
    rig.duty.dismissBubble();
    rig.manager.response = envelope({plainWord("resilience", "Latin resilire, to leap back.")});
    rig.explain("resilience");
    ASSERT_TRUE(askedForOrigin());
    EXPECT_EQ(rig.duty.bubble().value("etymology").toString(), QStringLiteral("Latin resilire, to leap back."));

    // And it is stored, so putting it away and taking it out again costs nothing: the entry is
    // reused whole, and the setting only decides whether the card draws the part it holds.
    rig.duty.dismissBubble();
    rig.store.document()["etymology"] = "false";
    rig.explain("resilience");
    EXPECT_EQ(rig.manager.posts, 2);
    EXPECT_TRUE(rig.duty.bubble().value("etymology").toString().isEmpty()) << "a stored origin was drawn with the setting off";

    rig.duty.dismissBubble();
    rig.store.document()["etymology"] = "true";
    rig.explain("resilience");
    EXPECT_EQ(rig.manager.posts, 2) << "the same word was paid for twice";
    EXPECT_EQ(rig.duty.bubble().value("etymology").toString(), QStringLiteral("Latin resilire, to leap back."));
}

TEST_F(LlmTest, Phase2TheExampleSettingBuysASentenceOnceAndAStoredWordWithoutOneIsBoughtAgain)
{
    QtApplication qt;
    Rig           rig;

    auto const askedForExample = [&] {
        const auto messages = QJsonDocument::fromJson(rig.manager.posted).object().value("messages").toArray();
        if (messages.isEmpty()) return false;
        return messages.at(0).toObject().value("content").toString().contains(QStringLiteral("\"example\":\"...\""));
    };

    // Explained with the example off, the word is bought without a sentence, and the entry says so.
    rig.manager.response = envelope({plainWord("bank")});
    rig.explain("bank");
    EXPECT_FALSE(askedForExample());
    EXPECT_FALSE(rig.store.cacheGet("bank", {"en", false})->example.has_value()) << "an unasked-for example was stored";

    // Turning the example on does not reuse that entry: it was bought without a sentence, and the
    // reader is owed one now. The second request is paid for, and the answer is what the bubble shows.
    rig.duty.dismissBubble();
    rig.store.document()["example"] = "true";
    rig.manager.response            = envelope({plainWord("bank", {}, "The bank raised its interest rate.")});
    rig.explain("bank");
    EXPECT_EQ(rig.manager.posts, 2) << "a stored word without an example was served as if it had one";
    ASSERT_TRUE(askedForExample());
    EXPECT_EQ(rig.duty.bubble().value("example").toString(), QStringLiteral("The bank raised its interest rate."));
    EXPECT_EQ(rig.store.cacheGet("bank", {"en", false})->example.value_or("<absent>"), "The bank raised its interest rate.");

    // With the sentence stored, the next look is served from the cache, and no request is made.
    rig.duty.dismissBubble();
    rig.explain("bank");
    EXPECT_EQ(rig.manager.posts, 2) << "a stored example was bought a second time";
    EXPECT_EQ(rig.duty.bubble().value("example").toString(), QStringLiteral("The bank raised its interest rate."));

    // Turned off, the card leaves the sentence out but keeps it stored, so turning it back on costs nothing.
    rig.duty.dismissBubble();
    rig.store.document()["example"] = "false";
    rig.explain("bank");
    EXPECT_EQ(rig.manager.posts, 2);
    EXPECT_TRUE(rig.duty.bubble().value("example").toString().isEmpty()) << "a stored example was drawn with the setting off";
    EXPECT_TRUE(rig.store.cacheGet("bank", {"en", false})->example.has_value()) << "turning the setting off dropped the stored sentence";
}

TEST_F(LlmTest, Phase2TheCardContentSwitchesHideWhatTheCacheStillHolds)
{
    QtApplication qt;
    Rig           rig;

    auto answer            = plainWord("bank");
    answer["ipa"]          = "/bæŋk/";
    answer["signpost"]     = "a place for money";
    answer["collocations"] = QJsonArray{"~ account", "river ~"};
    answer["imageable"]    = true;
    answer["imageQuery"]   = "river bank";
    rig.manager.response   = envelope({answer});
    rig.explain("bank");

    auto const senseOf = [&] { return rig.duty.bubble().value("senses").toList().front().toMap(); };
    EXPECT_EQ(rig.duty.bubble().value("ipa").toString(), QStringLiteral("/bæŋk/"));
    EXPECT_EQ(rig.duty.bubble().value("signpost").toString(), QStringLiteral("a place for money"));
    EXPECT_EQ(rig.duty.bubble().value("collocations").toList().size(), 2);
    EXPECT_TRUE(senseOf().value("imageable").toBool());
    EXPECT_EQ(senseOf().value("imageQuery").toString(), QStringLiteral("river bank"));

    // Off, the same cached word draws none of them -- and is not bought again.
    rig.duty.dismissBubble();
    for (auto const key : {"ipa", "signpost", "collocations", "pictures"})
        rig.store.document()[key] = "false";
    rig.explain("bank");
    EXPECT_EQ(rig.manager.posts, 1) << "a display switch sent the word to the model again";
    EXPECT_TRUE(rig.duty.bubble().value("ipa").toString().isEmpty());
    EXPECT_TRUE(rig.duty.bubble().value("signpost").toString().isEmpty());
    EXPECT_TRUE(rig.duty.bubble().value("collocations").toList().isEmpty());
    EXPECT_FALSE(senseOf().value("imageable").toBool());
    EXPECT_TRUE(senseOf().value("imageQuery").toString().isEmpty());

    // The entry itself is untouched, which is what lets one switch come back without a purchase.
    auto const cached = rig.store.cacheGet("bank", {"en", false});
    ASSERT_TRUE(cached.has_value());
    EXPECT_EQ(cached->ipa, "/bæŋk/");
    EXPECT_EQ(cached->signpost, "a place for money");
    EXPECT_EQ(cached->imageQuery, "river bank");

    rig.duty.dismissBubble();
    rig.store.document()["ipa"] = "true";
    rig.explain("bank");
    EXPECT_EQ(rig.manager.posts, 1);
    EXPECT_EQ(rig.duty.bubble().value("ipa").toString(), QStringLiteral("/bæŋk/"));
    EXPECT_TRUE(rig.duty.bubble().value("signpost").toString().isEmpty()) << "another switch was turned on too";
}

TEST_F(LlmTest, Phase2AnExampleThatIsEmptyIsAnAnswerAndIsNotAskedForAgain)
{
    QtApplication qt;
    Rig           rig;

    // The model was asked and had no sentence to give: that is a settled answer, so the entry records
    // it as empty and the next look does not spend another request on the same question.
    rig.store.document()["example"] = "true";
    rig.manager.response            = envelope({plainWord("acronym")});
    rig.explain("acronym");
    EXPECT_EQ(rig.manager.posts, 1);
    ASSERT_TRUE(rig.store.cacheGet("acronym", {"en", false})->example.has_value()) << "an asked-for but empty example was dropped as a miss";
    EXPECT_TRUE(rig.store.cacheGet("acronym", {"en", false})->example->empty());

    rig.duty.dismissBubble();
    rig.explain("acronym");
    EXPECT_EQ(rig.manager.posts, 1) << "a settled empty example was asked for again";
}

TEST_F(LlmTest, Phase2ChangingSettingsDuringResponseKeepsTheOriginalCacheContext)
{
    QtApplication qt;
    Rig           rig;
    rig.store.document()["multiSense"] = "true";
    rig.duty.setSelection(selection());
    QEventLoop loop;
    QObject::connect(&rig.client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    rig.duty.runSelectionAction("explain", "bank");
    rig.store.setExplanationLang("ja");
    rig.store.document()["multiSense"] = "false";
    loop.exec();
    EXPECT_TRUE(rig.duty.bubble().isEmpty());
    EXPECT_TRUE(rig.store.cacheGet("bank", {"en", true}));
    EXPECT_FALSE(rig.store.cacheGet("bank", {"ja", false}));
}

TEST_F(LlmTest, Phase2AProviderChoiceMovesTheEndpointItsWireOptionsAndTheModel)
{
    // A model that belongs to neither service, so both trips through the loop prove the same
    // thing: the one standing is from somewhere else, and following the service replaces it.
    QtApplication         qt;
    Rig                   rig{QStringLiteral("gpt-4.1-nano")};
    app::ModelCatalogDuty catalog{rig.storage, rig.client};
    for (auto const* provider : {"DeepSeek", "openai"}) {
        ASSERT_TRUE(catalog.setProvider(provider));
        auto const entry = llm::serviceProvider(provider);
        // The endpoint, its wire quirks -- and the model, which follows the service: a name from
        // another one is the 400 whose message names nothing (docs/adr/0017).
        EXPECT_EQ(rig.client.model(), provider == std::string_view{"DeepSeek"} ? "deepseek-flash" : "gpt-4.1-mini");
        EXPECT_EQ(rig.store.document().at("URL"), entry.value("baseUrl").toString().toStdString());
        rig.duty.setSelection(selection());
        rig.duty.dismissBubble();
        rig.store.setExplanationLang("en");
        // A fresh lemma is not needed: clear the in-memory cache by using a different mode.
        rig.store.document()["multiSense"] = provider == std::string{"DeepSeek"} ? "false" : "true";
        rig.explain("bank");
        EXPECT_EQ(rig.manager.endpoint, QUrl{entry.value("baseUrl").toString() + "/chat/completions"});
        auto const body = QJsonDocument::fromJson(rig.manager.posted).object();
        EXPECT_EQ(body.contains("thinking"), provider == std::string{"DeepSeek"});
    }
    // One shared source, not one endpoint per provider, and it is asked with nothing the reader
    // owns. What came back is split per provider by the vendor prefix in the catalog, and each
    // slice is that provider's cache -- the seed is only for a machine that has never reached it.
    EXPECT_GE(rig.manager.gets, 1);
    EXPECT_EQ(rig.manager.listed, QUrl{"https://openrouter.ai/api/v1/models"});
    EXPECT_TRUE(rig.manager.listedAuthorization.isEmpty()) << "the shared source was sent the API key";
    EXPECT_TRUE(rig.manager.listedGoogleKey.isEmpty());
    // The routing variant and the image-capable entry are in the answer and in neither slice.
    EXPECT_EQ(rig.store.document().at("MODELS").at("DeepSeek"), nlohmann::json::array({"deepseek-chat"}));
    EXPECT_EQ(rig.store.document().at("MODELS").at("openai"), nlohmann::json::array({"gpt-4.1-mini"}));

    // The price shown is the typed model's, not the provider's.
    ASSERT_TRUE(catalog.setModel("gpt-4.1-mini"));
    EXPECT_GT(rig.pricing.cost(rig.client.model(), {1000, 1000}), 0.0);
    EXPECT_FALSE(catalog.settings().value("modelPrice").toString().isEmpty());
    ASSERT_TRUE(catalog.setModel("unlisted-model"));
    EXPECT_EQ(rig.pricing.cost("unlisted-custom", {1000, 1000}), 0.0);
    EXPECT_TRUE(catalog.settings().value("modelPrice").toString().contains("No listed price"));
    EXPECT_FALSE(catalog.setProvider("unknown"));
    EXPECT_TRUE(catalog.setProvider("custom"));
    EXPECT_EQ(rig.client.model(), "unlisted-model");
    auto const restored = core::KnownStore::load(std::filesystem::path{rig.temporary.path().toStdWString()} / "fixture.json");
    EXPECT_EQ(restored.document().at("PROVIDER"), "custom");
}

/// The contract of the shared source in one case: one GET, no key, and an answer that reaches
/// more than one provider's cache because it is classified per vendor rather than per endpoint.
TEST_F(LlmTest, OneSharedSourceIsAskedWithNoKeyAndItsAnswerReachesEveryProvider)
{
    QtApplication    qt;
    QTemporaryDir    temporary;
    auto             store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager          manager;
    // A client holding no key at all, which used to be exactly the case that fetched nothing.
    llm::LlmClient        client{{QUrl{"https://example.invalid"}, "", "deepseek-flash"}, &manager};
    app::ModelCatalogDuty catalog{storage, client};
    QStringList           fetched;
    QEventLoop            loop;
    QObject::connect(&client, &llm::LlmClient::modelsFetched, &loop, [&](QStringList models) {
        fetched = std::move(models);
        loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);

    catalog.refreshModels();
    loop.exec();

    EXPECT_EQ(manager.gets, 1);
    EXPECT_EQ(manager.listed, QUrl{"https://openrouter.ai/api/v1/models"});
    EXPECT_TRUE(manager.listed.query().isEmpty()) << "the shared source was asked a filtered question";
    EXPECT_TRUE(manager.listedAuthorization.isEmpty()) << "the API key went to the shared source";
    EXPECT_TRUE(manager.listedGoogleKey.isEmpty());

    // Whole catalogue, prefix-stripped per provider, and the last provider gets no entry at all.
    ASSERT_EQ(fetched.size(), 3);
    EXPECT_EQ(fetched.front(), QStringLiteral("openai/gpt-4.1-mini"));
    EXPECT_EQ(store.document().at("MODELS").at("DeepSeek"), nlohmann::json::array({"deepseek-chat"}));
    EXPECT_EQ(store.document().at("MODELS").at("openai"), nlohmann::json::array({"gpt-4.1-mini"}));
    EXPECT_EQ(store.document().at("MODELS").at("Google"), nlohmann::json::array({"gemini-2.5-flash"}));
    EXPECT_FALSE(store.document().at("MODELS").contains("custom")) << "custom declares no prefix";
}

/// With a key in hand the standing service is asked for its own shelf first: its answer is what
/// that key can call, so the ids are cached as they came -- no vendor prefix to strip, no other
/// provider's cache touched, and no second GET to the shared catalogue.
TEST_F(LlmTest, TheStandingServiceListsItsOwnModelsWhenItAnswers)
{
    QtApplication    qt;
    QTemporaryDir    temporary;
    auto             store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager          manager;
    // A first-party shelf: the entries carry no `architecture`, because the service states no
    // modality for a model its own endpoint takes.
    manager.modelResponseOverride = QByteArrayLiteral(R"({"data":[
        {"id":"deepseek-flash"},{"id":"deepseek-v4-pro"}]})");
    llm::LlmClient        client{{QUrl{"https://api.deepseek.com"}, "fake-test-token", "deepseek-flash"}, &manager};
    app::ModelCatalogDuty catalog{storage, client};
    QStringList           fetched;
    QEventLoop            loop;
    QObject::connect(&client, &llm::LlmClient::serviceModelsFetched, &loop, [&](QStringList models) {
        fetched = std::move(models);
        loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);

    catalog.refreshModels();
    loop.exec();

    ASSERT_EQ(fetched.size(), 2);
    EXPECT_EQ(manager.gets, 1) << "the shared catalogue was asked as well";
    EXPECT_EQ(manager.listed, QUrl{"https://api.deepseek.com/models"});
    EXPECT_EQ(manager.listedAuthorization, QByteArrayLiteral("Bearer fake-test-token"));
    EXPECT_EQ(store.document().at("MODELS").at("DeepSeek"),
              nlohmann::json::array({"deepseek-flash", "deepseek-v4-pro"}));
    EXPECT_EQ(catalog.models().size(), 2);
}

TEST_F(LlmTest, ModelListRefreshKeepsTheUserCacheWhenAResponseIsMalformed)
{
    QtApplication qt;
    QTemporaryDir temporary;
    auto          store                    = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    store.document()["MODELS"]["DeepSeek"] = nlohmann::json::array({"cached-model"});
    app::MouseSelectionHook hook;
    app::GlobalHotkey       hotkey;
    Manager                 manager;
    manager.modelResponseOverride = QByteArrayLiteral(R"({"data":"malformed"})");
    llm::LlmClient     client{{QUrl{"https://example.invalid"}, "fake-test-token", "deepseek-flash"}, &manager};
    auto const         pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::AppController controller{store, client, hook, hotkey, pricing, test::sourceDir() / "data"};

    ASSERT_EQ(controller.modelCatalogDuty()->models().size(), 1);
    EXPECT_EQ(controller.modelCatalogDuty()->models().front().toMap().value("value").toString(), "cached-model");
    QEventLoop loop;
    QObject::connect(&client, &llm::LlmClient::modelsFetched, &loop, &QEventLoop::quit);
    QTimer::singleShot(50, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(controller.modelCatalogDuty()->models().front().toMap().value("value").toString(), "cached-model");
    EXPECT_EQ(store.document().at("MODELS").at("DeepSeek").at(0), "cached-model");
}

/// A fetched list is the industry's, not the standing service's, so it does not get to say what
/// that service carries: the model in force is left exactly as it was. The correction lives where
/// it fired -- the reader changing the service -- and still happens there.
TEST_F(LlmTest, AFetchedListNeverRewritesTheModelInForceButAProviderChangeStillDoes)
{
    QtApplication    qt;
    QTemporaryDir    temporary;
    auto             store = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    app::StorageDuty storage{store};
    Manager          manager;
    // A model from another service, so setProvider writes MODEL to deepseek-flash and the fetch
    // then has a standing value it would have to overwrite to prove it does not.
    llm::LlmClient        client{{QUrl{"https://example.invalid"}, "fake-test-token", "gpt-4.1-nano"}, &manager};
    auto const            pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::ModelCatalogDuty catalog{storage, client};

    ASSERT_TRUE(catalog.setProvider("DeepSeek"));
    ASSERT_EQ(client.model(), "deepseek-flash");
    QEventLoop loop;
    QObject::connect(&client, &llm::LlmClient::modelsFetched, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    // The answer lists deepseek-chat, not the deepseek-flash that is in force: rewriting the
    // model here would move every DeepSeek reader onto a name the price list does not carry.
    ASSERT_EQ(store.document().at("MODELS").at("DeepSeek").at(0), "deepseek-chat");
    EXPECT_EQ(client.model(), "deepseek-flash");
    EXPECT_EQ(store.document().at("MODEL"), "deepseek-flash");
    EXPECT_FALSE(pricing.cost(client.model(), {1000, 1000}) <= 0.0) << "the shipped default lost its price";

    // Re-picking the provider already in force is a tap on the dropdown row, not a change, and the
    // dropdown passes the current value straight through. With one list shared by every service,
    // that tap would otherwise replace a valid model with whichever id the source lists first.
    ASSERT_TRUE(catalog.setProvider("DeepSeek"));
    EXPECT_EQ(client.model(), "deepseek-flash") << "re-picking the standing provider rewrote the model";
    EXPECT_EQ(store.document().at("MODEL"), "deepseek-flash");

    // Changing the service is the one place the model still follows: the standing one is by
    // construction from somewhere else, and the 400 that names nothing is what that produces.
    ASSERT_TRUE(catalog.setProvider("openai"));
    EXPECT_EQ(client.model(), "gpt-4.1-mini");
    EXPECT_EQ(store.document().at("MODEL"), "gpt-4.1-mini");
}

TEST_F(LlmTest, Phase2DefaultsToOneSenseAndQueuesTheLatestRequestedMode)
{
    QtApplication qt;
    Rig           rig;
    rig.explain("bank");
    EXPECT_EQ(rig.duty.bubble().value("senses").toList().size(), 1);
    EXPECT_TRUE(rig.duty.bubble().value("senses").toList().front().toMap().value("text").toString().isEmpty());
    EXPECT_FALSE(rig.manager.posted.contains("one to three"));

    rig.store.setExplanationLang("es");
    QEventLoop loop;
    int        responses = 0;
    QObject::connect(&rig.client, &llm::LlmClient::batchFinished, &loop, [&](auto, llm::Usage const usage) {
        EXPECT_EQ(usage.model, "test-model");
        if (++responses == 2) loop.quit();
    });
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    rig.duty.setSelection(selection());
    rig.duty.runSelectionAction("explain", "bank");
    rig.store.document()["multiSense"] = "true";
    rig.duty.setSelection(selection());
    rig.duty.runSelectionAction("explain", "bank");
    loop.exec();
    EXPECT_EQ(responses, 2);
    EXPECT_EQ(rig.manager.posts, 3);
    EXPECT_EQ(rig.duty.bubble().value("senses").toList().size(), 3);
    EXPECT_TRUE(rig.store.cacheGet("bank", {"es", false}));
    EXPECT_TRUE(rig.store.cacheGet("bank", {"es", true}));
}

TEST_F(LlmTest, Phase2BudgetStopsQueuedDispatchWithoutAnErrorNotice)
{
    QtApplication qt;
    const QDate   date = QDate::currentDate();
    Rig           rig{QStringLiteral("deepseek-flash"), [date] { return date; }};
    ASSERT_TRUE(rig.cost.setDailyBudget(0.000045));

    rig.duty.setSelection(selection());
    rig.duty.runSelectionAction("explain", "bank");
    rig.duty.setSelection(selectionFor("river"));
    rig.duty.runSelectionAction("explain", "river");

    QEventLoop loop;
    QObject::connect(&rig.client, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_EQ(rig.manager.posts, 1);
    EXPECT_TRUE(rig.cost.budgetStatus().exhausted);
    EXPECT_TRUE(rig.duty.notice().isEmpty());
}

TEST_F(LlmTest, Phase2ControllerAppliesChoicesAndRestoresPersistedWireSettings)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    auto const path                 = std::filesystem::path{temporary.path().toStdWString()} / "fixture.json";
    auto       store                = core::KnownStore::load(path);
    store.document()["API-KEY"]     = "fake-test-token";
    auto const              pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::MouseSelectionHook hook;
    app::GlobalHotkey       hotkey;
    Manager                 manager;
    llm::LlmClient          client{{QUrl{"https://example.invalid"}, "fake-test-token", "test-model"}, &manager};
    app::AppController      controller{store, client, hook, hotkey, pricing, test::sourceDir() / "data"};
    int                     settingsChanges = 0;
    QObject::connect(&controller, &app::AppController::settingsChanged, [&] { ++settingsChanges; });
    auto const initial   = controller.settings();
    const auto languages = initial.value("languages").toList();
    ASSERT_EQ(languages.size(), 5);
    bool hasTraditionalChinese = false;
    for (auto const& value : languages) {
        const auto language = value.toMap();
        if (language.value("value").toString() == "zh-Hant") {
            EXPECT_EQ(language.value("label").toString(), QStringLiteral("繁體中文"));
            hasTraditionalChinese = true;
        }
    }
    EXPECT_TRUE(hasTraditionalChinese) << "the catalog lost its Traditional Chinese entry";
    EXPECT_EQ(initial.value("providers").toList().size(), 9);
    EXPECT_EQ(initial.value("provider").toString(), "DeepSeek");
    // The provider carries no model, so the one the client was built with stands.
    EXPECT_EQ(initial.value("model").toString(), "test-model");
    EXPECT_FALSE(initial.contains("API-KEY"));
    EXPECT_FALSE(initial.contains("apiKey"));

    auto const request = [&](llm::LlmClient& active) {
        QEventLoop loop;
        QObject::connect(&active, &llm::LlmClient::batchFinished, &loop, &QEventLoop::quit);
        QObject::connect(&active, &llm::LlmClient::failed, &loop, &QEventLoop::quit);
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        active.explainWords(llm::ExplainRequest{.words = {"bank"}});
        loop.exec();
        EXPECT_TRUE(manager.usesTestToken);
        return QJsonDocument::fromJson(manager.posted).object();
    };
    EXPECT_TRUE(request(client).contains("thinking"));
    controller.modelCatalogDuty()->setProvider("custom");
    EXPECT_EQ(controller.settings().value("url").toString(), "https://api.deepseek.com");
    // The custom endpoint keeps the address it was on, and a model is always in force: which one
    // follows from the service, and the rule itself is pinned in the provider test above.
    EXPECT_FALSE(controller.settings().value("model").toString().isEmpty());
    EXPECT_EQ(store.document().at("URL"), "https://api.deepseek.com");
    EXPECT_FALSE(request(client).contains("thinking"));
    settingsChanges = 0;
    controller.modelCatalogDuty()->setProvider("openai");
    controller.setModel(" gpt-4.1-nano ");
    EXPECT_EQ(settingsChanges, 2);
    auto const chosen = controller.settings();
    EXPECT_EQ(chosen.value("model").toString(), "gpt-4.1-nano");
    EXPECT_EQ(chosen.value("url").toString(), "https://api.openai.com/v1");
    EXPECT_FALSE(chosen.value("modelPrice").toString().isEmpty());
    auto const body = request(client);
    EXPECT_EQ(body.value("model").toString(), "gpt-4.1-nano");
    EXPECT_FALSE(body.contains("thinking"));
    EXPECT_EQ(manager.endpoint, QUrl{"https://api.openai.com/v1/chat/completions"});

    controller.modelCatalogDuty()->setProvider("unknown");
    controller.setModel(" ");
    // Two: the provider and the model. A fetched model list is not one of them any more -- it
    // changes nothing in the settings map (docs/adr/0020) -- and neither rejected call counts.
    EXPECT_EQ(settingsChanges, 2);
    controller.setApiUrl("https://example.invalid/v2");
    auto               restored = core::KnownStore::load(path);
    llm::LlmClient     restarted{{QUrl{"https://wrong.invalid"}, "fake-test-token", "wrong-model"}, &manager};
    app::AppController restoredController{restored, restarted, hook, hotkey, pricing, test::sourceDir() / "data"};
    auto const         restoredBody = request(restarted);
    EXPECT_EQ(restoredBody.value("model").toString(), "gpt-4.1-nano");
    EXPECT_FALSE(restoredBody.contains("thinking"));
    EXPECT_EQ(manager.endpoint, QUrl{"https://example.invalid/v2/chat/completions"});
    EXPECT_EQ(restoredController.settings().value("provider").toString(), "openai");
    EXPECT_EQ(restored.document().at("API-KEY"), "fake-test-token");

    controller.modelCatalogDuty()->setProvider("custom");
    controller.setModel("custom-model");
    auto               customStore = core::KnownStore::load(path);
    llm::LlmClient     custom{{QUrl{"https://wrong.invalid"}, "fake-test-token", "wrong-model"}, &manager};
    app::AppController customController{customStore, custom, hook, hotkey, pricing, test::sourceDir() / "data"};
    auto const         customBody = request(custom);
    EXPECT_EQ(customBody.value("model").toString(), "custom-model");
    EXPECT_FALSE(customBody.contains("thinking"));
    EXPECT_EQ(manager.endpoint, QUrl{"https://example.invalid/v2/chat/completions"});
    EXPECT_EQ(customController.settings().value("provider").toString(), "custom");
}

TEST_F(LlmTest, Phase2ControllerRejectsAnInsecurePersistedServiceUrl)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    auto store                      = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    store.document()["PROVIDER"]    = "DeepSeek";
    store.document()["URL"]         = "http://untrusted.invalid/v1";
    store.document()["MODEL"]       = "deepseek-flash";
    auto const              pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::MouseSelectionHook hook;
    app::GlobalHotkey       hotkey;
    llm::LlmClient          client{{QUrl{"https://safe.invalid"}, "fake-test-token", "test-model"}};

    app::AppController controller{store, client, hook, hotkey, pricing, test::sourceDir() / "data"};

    EXPECT_EQ(client.baseUrl(), QUrl{"https://api.deepseek.com"});
    EXPECT_EQ(controller.settings().value("url").toString(), "https://api.deepseek.com");
}

TEST_F(LlmTest, Phase2RemovingAWordClearsItsOwnBubbleAndLeavesAnotherAlone)
{
    QtApplication qt;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    auto                    store   = core::KnownStore::load(std::filesystem::path{temporary.path().toStdWString()} / "fixture.json");
    auto const              pricing = llm::Pricing::load(test::sourceDir() / "data" / "llm" / "pricing.json");
    app::MouseSelectionHook hook;
    app::GlobalHotkey       hotkey;
    Manager                 manager;
    llm::LlmClient          client{{QUrl{"https://example.invalid"}, "fake-test-token", "test-model"}, &manager};
    app::AppController      controller{store, client, hook, hotkey, pricing, test::sourceDir() / "data"};
    controller.explanationDuty()->setSelection(selection());
    run(*controller.explanationDuty(), client);
    ASSERT_EQ(controller.explanationDuty()->bubble().value("title").toString(), "bank");

    controller.removeWord("resilience");
    EXPECT_EQ(controller.explanationDuty()->bubble().value("title").toString(), "bank")
        << "removing a word the bubble does not show took the bubble down";

    controller.removeWord("bank");
    EXPECT_TRUE(controller.explanationDuty()->bubble().isEmpty()) << "removing the bubble's own word left the bubble up";
}

TEST_F(LlmTest, Phase2AChineseSelectionIsTranslatedAndOnlyTheTranslationIsDrawn)
{
    QtApplication qt;
    Rig           rig;
    // A Chinese reader, which is the one the whole channel exists for: the answer is still English,
    // because Chinese is what they selected and the one thing they cannot be asking for.
    rig.store.setExplanationLang("zh");
    rig.manager.response = envelope({QJsonObject{{"en", "The weather is nice today."}}});

    app::PendingSelection chinese;
    chinese.kind   = core::SelectionKind::Translation;
    chinese.text   = QStringLiteral("今天天气不错。");
    chinese.anchor = QPoint{200, 400};
    rig.duty.setSelection(chinese);

    // Either button of the action bar lands here: with Chinese there is one decision, and the two
    // items are two ways into it (UI-SPEC.md section 4.10).
    run(rig.duty, rig.client, chinese.text);

    EXPECT_TRUE(rig.manager.posted.contains("Translate it into English")) << "the request did not go out on the translation channel";
    const auto bubble = rig.duty.bubble();
    EXPECT_EQ(bubble.value("type").toString(), "translation");
    EXPECT_EQ(bubble.value("en").toString(), "The weather is nice today.");
    EXPECT_TRUE(bubble.value("zh").toString().isEmpty()) << "a Chinese line was drawn beside the translation";
    EXPECT_TRUE(bubble.value("senses").toList().isEmpty());
    EXPECT_TRUE(bubble.value("status").toString().isEmpty()) << "a translation carries no learning verdict";
}
