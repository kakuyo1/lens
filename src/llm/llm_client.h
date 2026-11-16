#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <functional>
#include <variant>

#include "llm_protocol.h" // Channel

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @file llm_client.h
 * @brief Asynchronous client for the explanation model (DeepSeek, OpenAI-compatible, BYOK).
 *
 * The request body and the response check live in llm_pure.h so they can be tested without
 * a network. This header holds only the transport: a QObject around one HTTP round trip.
 *
 * Every feature -- the explanation batch, the peek, the sense channel, the breakdown, the two
 * corrections, the reading profile and the story -- enters through request() with its own typed
 * request and answers on answered() under the handle that call returned; the request's body,
 * timeout, response check and failure line are this seam's knowledge, not each caller's
 * (docs/adr/0182). A handle is what cancels: the reply is aborted and nothing is reported.
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

/// @brief One request to the peek channel: one word's line in the mother tongue (docs/adr/0112).
struct PeekRequest {
    QString lemma;    ///< The word, as the standing card names it; echoed back with the answer.
    QString language; ///< The mother tongue's catalog code; picks the prompt's closing line.
    QString sense;    ///< The English definition of the sense the card shows, when it is not the
                      ///< answer's own first one; empty asks for the word's most common sense.
};

/// @brief One request to the sense channel: the next distinct common sense of a known word.
struct SenseRequest {
    QString lemma;    ///< The word, as the standing card names it; echoed back unchanged.
    QString input;    ///< The wire payload: the lemma, then one already-known definition per line.
    QString language; ///< The explanation language's catalog code; picks the prompt's closing line.
};

/// @brief One request to the breakdown channel: which part each run of words plays (UI-SPEC.md 4.3).
struct BreakdownRequest {
    QString     sentence; ///< The selected text, echoed back so the answer can be matched to its card.
    QStringList words;    ///< The sentence split by breakdownWords(); the answer indexes into it.
};

/// @brief One request to a correction channel (UI-SPEC.md 4.10, the copy drawer).
struct CorrectionRequest {
    QString text;                     ///< The selected word or sentence, echoed back.
    Channel channel = Channel::Spell; ///< Spell or Grammar: which correction to ask for.
};

/// @brief One request to the profile channel: the interpretation of one week (UI-SPEC.md 4.7b).
struct ProfileRequest {
    QString summary;  ///< The profile as readingProfileSummary() writes it: the only text the request carries.
    QString language; ///< The interface language's catalog code; picks the prompt's closing line.
};

/// @brief One request to the story channel: one short English story from the week's words (UI-SPEC.md 4.7d).
struct StoryRequest {
    QStringList words; ///< The words the story is written from, in dictionary form; each is masked
                       ///< before it leaves the machine, and the answer keeps only the ones listed here.
};

/// @brief Any feature request; which alternative it holds is which channel is spoken.
using Request = std::variant<ExplainRequest, PeekRequest, SenseRequest, BreakdownRequest, CorrectionRequest, ProfileRequest, StoryRequest>;

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

/// @brief One line of the weekly reading profile's interpretation, and the key of the figure it rests on.
/// @note `fact` is one of freshShare, repeated, peakHour, topWords or partsOfSpeech, as the prompt names them. It is
///       empty when the model named none, and an unknown key is kept as written: the panel draws no figure for it.
struct ProfileLine {
    QString text;
    QString fact;
};

/// @brief The story the model wrote from the week's words, and the listed words it used (docs/adr/0152).
struct Story {
    QString     text;
    QStringList words;
};

/// @brief The explanation batch's answer: the validated explanations, in the order asked for.
/// @note An empty @c results with an empty @c failure is a batch that answered nothing to explain;
///       a non-empty @c failure is a request that never produced an answer (refused before sending,
///       unreachable, or a response the schema rejected).
struct ExplainAnswer {
    QVector<Explanation> results;
    Usage                usage;
    QString              failure; ///< Reader-facing reason; empty on success.
};

/// @brief The peek's answer for @c lemma: @c line, empty when the request failed or was refused.
struct PeekAnswer {
    QString lemma;
    QString line;
    Usage   usage; ///< All zero when the call never went out.
};

/// @brief The sense channel's answer for @c lemma.
/// @note An @c answer whose `senses` is empty is the exhaustion answer -- the word has no further
///       distinct common sense -- and is also what a failed or refused request reports, the peek's
///       quietness carried over.
struct SenseAnswer {
    QString     lemma;
    Explanation answer;
    Usage       usage;
};

/// @brief The breakdown's answer for @c sentence: @c spans, empty when the request failed or the
///        answer was refused, and then @c failure holds the reader's reason.
struct BreakdownAnswer {
    QString           sentence;
    QVector<RoleSpan> spans;
    Usage             usage;   ///< Carried whenever the call came back, even a refused answer.
    QString           failure; ///< Reader-facing reason; empty on success.
};

/// @brief The correction's answer for @c text: @c corrected, empty when the request failed or the
///        answer was refused, and then @c failure holds the reader's reason.
struct CorrectionAnswer {
    QString text;
    Channel channel = Channel::Spell; ///< Which correction answered, so the duty files it right.
    QString corrected;
    Usage   usage;
    QString failure;
};

/// @brief The interpretation's answer: @c points, empty when the request failed or the answer was
///        refused, and then @c failure holds the reader's reason.
struct ProfileAnswer {
    QVector<ProfileLine> points;
    Usage                usage;
    QString              failure;
};

/// @brief The story's answer: @c story, empty when the request failed or the answer was refused,
///        and then @c failure holds the reader's reason.
struct StoryAnswer {
    Story   story;
    Usage   usage;
    QString failure;
};

/// @brief Any feature answer; which alternative it holds says which request it answers.
using Answer = std::variant<ExplainAnswer, PeekAnswer, SenseAnswer, BreakdownAnswer, CorrectionAnswer, ProfileAnswer, StoryAnswer>;

/// @brief How long each kind of call may take before it is cut off.
/// @note A test shortens these to prove the policy reaches the wire; the values must stay
///       large enough for a real service, and small calls (a peek, a probe) answer a waiting
///       reader, so their ceilings are tighter than the batch's.
struct Timeouts {
    int explainMs   = 30000; ///< The explanation batch, and the calls that ride its shape.
    int peekMs      = 15000;
    int senseMs     = 15000;
    int probeMs     = 15000;
    int modelListMs = 10000;
};

/// @brief The handle one request returns: its identity while in flight, and what cancels it.
struct Handle {
    quint64 id = 0;

    bool operator==(Handle const&) const = default;
};

/**
 * @brief Sends one feature request and reports its validated answer back under the same handle.
 *
 * One HTTP request per call, exactly one answered() per request. The explanation batch streams:
 * whatever has completed so far is reported through partial() while it arrives, a preview and
 * nothing more -- the cache, the known store and the statistics only ever take what
 * answered() carries, and that one has been through parseExplanations() whole.
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
    /// @param config   Connection settings.
    /// @param manager  Network manager to post through, or null to make one.
    /// @param timeouts How long each kind of call may take; a test shortens one to see it applied.
    explicit LlmClient(Config config, QNetworkAccessManager* manager = nullptr, Timeouts timeouts = {}, QObject* parent = nullptr);

    /// @brief Replace the model used by subsequent requests.
    void setModel(const QString& model);

    /// @brief Replace the OpenAI-compatible endpoint used by subsequent requests.
    void setBaseUrl(const QUrl& baseUrl);

    /// @brief Replace the bearer token used by subsequent requests without logging it.
    void setApiKey(const QString& apiKey);

    /// @brief Apply a catalog provider's base URL and wire options.
    /// @return False for an unknown provider; custom retains the current endpoint and model.
    bool setProvider(QString const& provider);

    /// @brief Ask for the models the reader's service carries.
    ///
    /// With a key in hand the standing service is asked for its own shelf, because the shared
    /// catalogue is the industry's list and carries names the vendor never opened at this endpoint
    /// (docs/adr/0137). Without a key, or when the service answers nothing usable, the shared
    /// source is asked instead.
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
     * @brief Send one feature request.
     *
     * Exactly one answered() follows -- from inside this call when the request is refused before
     * it goes out (no key, no endpoint, nothing to ask), otherwise when the reply finishes.
     * @param request The payload and every decision that call makes.
     * @return The handle its answer will carry, and the token cancel() takes.
     */
    Handle request(Request request);

    /// @brief Abandon the request @p handle names: its reply is aborted and nothing is reported.
    /// @note A handle whose answer already arrived names nothing in flight; cancelling it does nothing.
    void cancel(Handle handle);

signals:
    /// @brief The answer to one request, under the handle request() returned.
    void answered(Handle handle, Answer answer);

    /// @brief Emitted while an explanation batch is still arriving, with whatever has been written so far.
    ///
    /// Only fields whose value has closed are set; the rest are empty and the card grows into
    /// them. This is a preview for the reader and nothing more: only answered() carries a
    /// result the cache, the known store and the statistics may take. Emitted again only when a
    /// drawn field actually changes, so a chunk that completes nothing stays silent.
    void partial(Handle handle, Explanation explanation);

    /// @brief The whole usable catalogue the shared source lists, in the order it gave them.
    void modelsFetched(QStringList models);

    /// @brief The models the standing service itself serves, in the order it gave them and as its
    ///        endpoint takes them: no vendor prefix to strip, no other provider's names.
    void serviceModelsFetched(QStringList models);

    /// @brief The connection probe's answer: whether the service accepted it, and why not.
    void probeFinished(bool ok, QString message);

private:
    /// @brief Ask the standing service, with the reader's key, for the models it serves.
    /// @note Falls back to askSharedSourceModels() when the answer is empty or refused.
    void askServiceModels();

    /// @brief Ask the shared source for the whole catalogue: no key, no auth header, no query.
    /// @note Empty, malformed, failed, or timed-out responses leave the current cache untouched.
    void askSharedSourceModels();

    /// @brief Build and send one request per alternative; each answers @p handle exactly once.
    void askExplain(Handle handle, ExplainRequest request);
    void askPeek(Handle handle, PeekRequest const& request);
    void askSense(Handle handle, SenseRequest const& request);
    void askBreakdown(Handle handle, BreakdownRequest const& request);
    void askCorrection(Handle handle, CorrectionRequest const& request);
    void askProfile(Handle handle, ProfileRequest const& request);
    void askStory(Handle handle, StoryRequest const& request);

    /// @return Whether this client can post at all: a key, and an endpoint it will speak to.
    /// @param requireHttps True for the calls that stand alone (a held gesture, a drawer); the
    ///        explanation batch keeps the looser check it always had.
    bool configured(bool requireHttps) const;

    /// @brief POST @p body on the chat endpoint and run @p onRead / @p onFinished with its reply.
    ///
    /// The reply's whole life is this function's: the id is registered for cancel(), a download
    /// that grows past the cap is aborted, and a cancelled request's finish is dropped.
    /// @param onRead     Called with each chunk; empty for the calls that do not stream.
    /// @param onFinished Called once when the reply ends, unless the request was cancelled.
    void post(Handle handle, QByteArray const& body, int timeoutMs, std::function<void(QNetworkReply&)> onRead, std::function<void(QNetworkReply&)> onFinished);

    /// @brief Hand one answer to the surface that asked for it.
    void answer(Handle handle, Answer answer)
    {
        emit answered(handle, std::move(answer));
    }

    Config                 config_;
    QNetworkAccessManager* manager_;
    Timeouts               timeouts_;
    /// The id the next request takes; answers carry it back, so one client's calls stay apart.
    quint64 nextId_ = 1;
    /// The replies still waiting, by handle; a cancelled one leaves before its finish runs.
    QHash<quint64, QPointer<QNetworkReply>> replies_;
};

}
