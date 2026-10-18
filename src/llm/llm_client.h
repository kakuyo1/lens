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
struct Sense {
    QString en, zh, translation;
};

inline bool operator==(Sense const& a, Sense const& b)
{
    return a.en == b.en and a.zh == b.zh and a.translation == b.translation;
}

inline constexpr int kMaxSenses = 3;

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
    QString        etymology;
    QString        example;
    QVector<Sense> senses;
};

/// @brief Field-wise equality, so a stream's next preview can be told from the one on screen.
inline bool operator==(Explanation const& a, Explanation const& b)
{
    return a.title == b.title and a.ipa == b.ipa and a.en == b.en and a.zh == b.zh and
           a.translation == b.translation and a.etymology == b.etymology and a.example == b.example and
           a.senses == b.senses;
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
    /// @param parent  QObject parent.
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

signals:
    /// @brief Emitted with the validated explanations, in the order the words were asked for.
    /// @param usage Token counts from the same response, all zero when it carried none.
    void batchFinished(QVector<Explanation> results, Usage usage);

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

private:
    Config                 config_;
    QNetworkAccessManager* manager_;
};

}
