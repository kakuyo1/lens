#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include "llm_protocol.h" // Channel

class QNetworkAccessManager;

/**
 * @file llm_client.h
 * @brief Asynchronous client for the explanation model (DeepSeek, OpenAI-compatible, BYOK).
 *
 * The request body and the response check live in llm_pure.h so they can be tested without
 * a network. This header holds only the transport: a QObject around one HTTP round trip.
 */

namespace lens::llm {

/// @brief Connection settings, loaded from the reader's settings document.
struct Config {
    QUrl        baseUrl;          ///< OpenAI-format base, e.g. https://api.deepseek.com (no /anthropic).
    QString     apiKey;           ///< Bearer token. Never logged, never echoed in errors.
    QString     model;            ///< Model name, e.g. "deepseek-flash".
    QJsonObject requestOverrides; ///< Provider-specific wire options; null removes a default.
};

/**
 * @brief Everything one explanation call decides, so no setter carries state between calls.
 *
 * The preset is the selection's own ("default", "translate" or "explain"); presetFor() upgrades it
 * to "multiple" when @c multipleSenses is set. The language, the sense flag, the origin and the
 * example are all per-call values: a setting that moves while a request is away cannot retarget
 * that request, and the next call is not affected by the one before it.
 */
struct ExplainRequest {
    QStringList words;                          ///< Payload to explain, in request order.
    Channel     channel        = Channel::Word; ///< Which protocol to speak.
    QString     preset         = QStringLiteral("default");
    QString     language       = QStringLiteral("en");
    bool        multipleSenses = false;
    bool        etymology      = false;
    bool        example        = false;
};

/// @brief One frequency-ordered word sense; translation is in the requested output language.
/// @note `imageable` and `imageQuery` are the picture advice the word channel carries:
///       whether a photograph can make this sense clearer, and the two-to-five English words an
///       image search runs with. Advisory only; `imageable` false, the default and the reading of
///       an absent field, draws no picture entry. `hint` is the sense channel's one English word
///       for the sense, and stays empty for senses that came from the word channel.
struct Sense {
    QString en, zh, translation;
    QString hint;
    bool    imageable = false;
    QString imageQuery;
};

inline bool operator==(Sense const& a, Sense const& b)
{
    return a.en == b.en and a.zh == b.zh and a.translation == b.translation and a.hint == b.hint and
           a.imageable == b.imageable and a.imageQuery == b.imageQuery;
}

inline constexpr int kMaxSenses = 3;

/// @brief What a sentence part does: the S / V / C / A codes the breakdown prompt asks for.
enum class Role : std::uint8_t { Subject,
                                 Verb,
                                 Complement,
                                 Adverbial };

/// @brief One run of a sentence's words that plays one role; @p from and @p to are word indices,
///        both inclusive, counted from 0 in the order breakdownWords() gives them.
struct RoleSpan {
    Role role = Role::Subject;
    int  from = 0;
    int  to   = 0;
};

/// @brief One explanation as returned by the model.
/// @note `title` is `word` on the word channel and the echoed sentence or entity otherwise.
///       `ipa` is required only by the word schema; sentence and entity responses have three
///       fields (`title`, `en`, `zh`).
///       `etymology` is the word's origin in one sentence, in the explanation language. Optional
///       like `ipa`: it is asked for only when the reader's setting wants it, and a word whose
///       origin is unknown answers with none.
///       `example` is one English sentence using the word. Optional for the same reasons.
struct Explanation {
    QString        title, ipa, en, zh;
    QString        translation;
    QString        pos; ///< The most common sense's part of speech, one lowercase English word. Optional like `ipa`.
    QString        etymology;
    QString        example;
    QVector<Sense> senses;
    /// @note `signpost` and `collocations` feed the definition chain: a short label for the meaning, and up
    ///       to two collocations with the word written as "~". Both are optional; an absent one is not drawn.
    QString     signpost;
    QStringList collocations;
    /// @note The picture advice in its singular shape: a word answered without `senses` carries the
    ///       pair here, as it carries `en` / `zh`. The card reads the per-sense copy in `senses`;
    ///       this one is folded into `senses` on parse so nothing downstream sees two shapes.
    bool    imageable = false;
    QString imageQuery;
};

/// @brief Field-wise equality, so a stream's next preview can be told from the one on screen.
inline bool operator==(Explanation const& a, Explanation const& b)
{
    return a.title == b.title and a.ipa == b.ipa and a.en == b.en and a.zh == b.zh and
           a.translation == b.translation and a.pos == b.pos and a.etymology == b.etymology and
           a.example == b.example and a.senses == b.senses and a.signpost == b.signpost and
           a.collocations == b.collocations and a.imageable == b.imageable and
           a.imageQuery == b.imageQuery;
}

/// @brief Token counts the service reports for one call, for the cost surfaces.
///
/// Missing from the response is recorded as zero rather than failing the batch: losing one
/// line of a tally is survivable, throwing away an explanation that succeeded is not.
struct Usage {
    int     promptTokens     = 0;
    int     completionTokens = 0;
    QString model; ///< Model captured for this request, even if settings changed in flight.
};

/**
 * @brief Sends a batch of words and reports the validated explanations back.
 *
 * One HTTP request per call, streamed: the answer is read as the model writes it, and whatever
 * has completed so far is reported through partial() while it arrives. A preview is drawn and
 * nothing else -- the cache, the known store and the statistics only ever take what
 * batchFinished() carries, and that one has been through parseExplanations() whole.
 *
 * The model list is the one thing this client asks for on its own: a single unauthenticated GET
 * to the shared source, which answers for every configured provider.
 */
class LlmClient : public QObject {
    Q_OBJECT
public:
    /// @brief Build a client, optionally over a caller-supplied network manager.
    ///
    /// The manager is the seam that lets a test answer without a socket: pass a subclass whose
    /// createRequest() returns a stub reply, and the status-code, empty-body and transport-failure
    /// branches become reachable offline. It must outlive this object. Null (the default) makes
    /// one owned by this object, which is what every caller in the application passes.
    ///
    /// @param config  Connection settings.
    /// @param manager Network manager to post through, or null to make one.
    explicit LlmClient(Config config, QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    /// @brief Replace the model used by subsequent requests.
    void setModel(const QString& model);

    /// @brief Replace the OpenAI-compatible endpoint used by subsequent requests.
    void setBaseUrl(const QUrl& baseUrl);

    /// @brief Replace the bearer token used by subsequent requests without logging it.
    void setApiKey(const QString& apiKey);

    /// @brief Apply a catalog provider's base URL and wire options.
    /// @return False for an unknown provider; custom retains the current endpoint and model.
    bool setProvider(QString const& provider);

    /// @brief Ask the shared model source for the whole catalogue of model ids.
    /// @note No key, no auth header, no query: the source answers for every provider at once.
    ///       Empty, malformed, failed, or timed-out responses leave the current cache untouched.
    void fetchModels();

    /// @brief Check the endpoint, key and model with one minimal, non-streaming completion.
    ///
    /// A single `ping` with `max_tokens: 1` makes the service validate all three parts without
    /// producing an explanation; the cost is one tiny request. The result is a fixed sentence
    /// in probeFinished(), never a transport's own words, and never contains the key.
    void probeConnection();

    /// @return The model name as sent on the wire, which is the key the price list uses.
    QString model() const
    {
        return config_.model;
    }

    /// @return The endpoint used by subsequent requests, without exposing the bearer token.
    QUrl baseUrl() const
    {
        return config_.baseUrl;
    }

    /**
     * @brief Look up one request's payload in one HTTP request.
     * @param request The payload and every prompt decision that call makes. More than 20 entries
     *                are silently truncated to the first 20; the current caller sends one, so the
     *                cap is not reached.
     */
    void explainWords(ExplainRequest request);

    /// @brief Ask for one word's meaning in one language, one short line, one whole body.
    ///
    /// The peek is what the bubble's held globe asks for (docs/adr/0112): one word in the mother
    /// tongue is all the reader wants while they hold, so the request does not stream -- the line
    /// is drawn whole or not at all. It rides its own signal rather than batchFinished(), so its
    /// answer can never be mistaken for an explanation batch's, and it fails quietly: a gesture
    /// that bought nothing owes the reader no notice.
    /// @param lemma    The word, as the standing card names it.
    /// @param language The mother tongue's catalog code; picks the prompt's closing line.
    /// @param sense    The English definition of the sense the card is showing, when it is not the
    ///                 answer's own first one: the line then answers for that sense alone rather
    ///                 than for the word's most common one (UI-SPEC.md 4.3, the sense cycle).
    void peekWord(QString const& lemma, QString const& language, QString const& sense = {});

    /// @brief Ask for the next distinct common sense of a word whose senses are already known.
    ///
    /// The sense channel is what the bubble's Not-the-Sense? entry asks for: the word on the first
    /// line of @p input and the senses already shown under it, one sense back, or an empty answer
    /// when the listed senses are all the word has. Like the peek it does not stream and rides its
    /// own signal, and it fails quietly: the definition on the card is the reader's answer either
    /// way, and the entry is the one thing that says there was nothing more to buy.
    /// @param lemma    The word, as the standing card names it; echoed back unchanged.
    /// @param input    The wire payload: the lemma, then one already-known definition per line.
    /// @param language The explanation language's catalog code; picks the prompt's closing line.
    void nextSenseWord(QString const& lemma, QString const& input, QString const& language);

    /// @brief Ask which part of an English sentence each run of words plays (UI-SPEC.md 4.3).
    ///
    /// Not streamed, and it rides its own signal like the peek: the breakdown is drawn whole or not
    /// at all, and the answer never reaches the cache or the verdicts.
    /// @param sentence The selected text, echoed back so the answer can be matched to its card.
    /// @param words    The sentence split by breakdownWords(); the answer indexes into it.
    void breakdownSentence(QString const& sentence, QStringList const& words);

    /// @brief Ask for the corrected spelling of one word (UI-SPEC.md 4.10, the copy drawer).
    /// @param text The selected word, echoed back so the answer can be matched to its card.
    void correctSpelling(QString const& text);

    /// @brief Ask for the corrected grammar of one English sentence (UI-SPEC.md 4.10, the copy drawer).
    /// @param text The selected sentence, echoed back so the answer can be matched to its card.
    void correctGrammar(QString const& text);

signals:
    /// @brief Emitted with the validated explanations, in the order the words were asked for.
    /// @param usage Token counts from the same response, all zero when it carried none.
    void batchFinished(QVector<Explanation> results, Usage usage);

    /// @brief The peek's answer for @p lemma: @p line, empty when the request failed or was
    ///        refused. @p usage carries the call's token counts, all zero when it never went out.
    void peekAnswered(QString const& lemma, QString const& line, Usage usage);

    /// @brief The sense channel's answer for @p lemma. An @p answer whose `senses` is empty is the
    ///        exhaustion answer -- the word has no further distinct common sense -- and is also
    ///        what a failed or refused request reports, the peek's quietness carried over.
    ///        @p usage carries the call's token counts, all zero when it never went out.
    void senseAnswered(QString const& lemma, Explanation const& answer, Usage usage);

    /// @brief The breakdown's answer for @p sentence: @p spans, empty when the request failed or
    ///        the answer was refused, and then @p failure holds the reader's reason. @p usage carries the
    ///        call's token counts whenever it came back.
    void breakdownAnswered(QString const& sentence, QVector<RoleSpan> spans, Usage usage, QString failure);

    /// @brief The correction's answer for @p text: @p corrected, empty when the request failed or the
    ///        answer was refused, and then @p failure holds the reader's reason. @p channel says which
    ///        correction answered, so the duty files it right.
    void correctionAnswered(QString const& text, Channel channel, QString corrected, Usage usage, QString failure);

    /// @brief Emitted while the answer is still arriving, with whatever has been written so far.
    ///
    /// Only fields whose value has closed are set; the rest are empty and the card grows into
    /// them. This is a preview for the reader and nothing more: only batchFinished() carries a
    /// result the cache, the known store and the statistics may take. Emitted again only when a
    /// drawn field actually changes, so a chunk that completes nothing stays silent.
    void partial(Explanation explanation);

    /// @brief Emitted on transport, HTTP status, or response-validation failure.
    /// @param message Reader-facing reason, already routed through translation.
    ///                Never contains the API key.
    void failed(QString message);

    /// @brief The whole usable catalogue the shared source lists, in the order it gave them.
    void modelsFetched(QStringList models);

    /// @brief The connection probe's answer: whether the service accepted it, and why not.
    void probeFinished(bool ok, QString message);

private:
    /// Post a one-word or one-sentence correction on @p channel and report it through correctionAnswered().
    void postCorrection(Channel channel, QString const& text);

    Config                 config_;
    QNetworkAccessManager* manager_;
};

}
