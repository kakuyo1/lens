#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <variant>

#include "llm_client.h"   // Config / Explanation
#include "llm_protocol.h" // Channel

/**
 * @file llm_pure.h
 * @brief The network-free core of LlmClient: mask, build the request, check the response.
 *
 * The prompt, the envelope defaults, and the response schema all come from `data/llm/`
 * (see llm_protocol.h); what lives here is the assembly and the checks. That keeps this
 * unit-testable from the lens_gtest_unit target, with no socket and no API key.
 */

namespace lens::llm {

/**
 * @brief Mask anything sensitive that slipped through before a request leaves the machine.
 *
 * Email addresses, URLs, and runs of 6+ digits each collapse to a placeholder. Words have
 * already been hard-filtered by FilterCore, so this is a backstop rather than the main
 * defence.
 *
 * @param text Text about to be sent.
 * @return The text with matches replaced by `<email>`, `<url>`, or `<num>`.
 */
QString maskSensitive(const QString& text);

/**
 * @brief Map an HTTP status onto something the reader can act on.
 *
 * Lives here, like the request body and the response check, so it is testable without a network.
 * The message never carries the API key or the response body, which may echo the request.
 *
 * @param status HTTP status code the service answered with.
 * @return A reader-facing reason, already routed through translation.
 */
QString httpErrorFor(int status);

/**
 * @brief The prompt preset a request names.
 *
 * The sense flag's "multiple" wins over the selection's own preset: a reader who turned multiple
 * senses on is asking for the multiple-sense shape whatever button the sentence came from, and a
 * request that did not ask for them keeps the selection's prompt.
 *
 * @param request The call's record.
 * @return "multiple", or the record's own preset.
 */
QString presetFor(ExplainRequest const& request);

/**
 * @brief Build the /chat/completions request body for one channel.
 *
 * Channel decides which template is used, so the caller must know whether it is sending a
 * word, an entity, or a sentence before it gets here. The envelope defaults
 * (`response_format`, `thinking`, `max_tokens`, `stream`) come from that channel's
 * `request.<channel>.json`, as does the system prompt.
 *
 * @param config          Supplies the model name.
 * @param channel         Which protocol to speak.
 * @param inputs          Payload to explain; each entry is masked before it is embedded.
 * @param explanationLang A catalog language code; picks the closing line of the system prompt.
 *                        "en" and "zh" fill their own fields, other codes the translation field.
 *                        An unknown value falls back to "en" and logs a warning.
 * @param preset          Prompt preset, such as sentence's "translate" or "explain".
 * @param etymology       Ask for the word's origin as well. The word channel's templates carry the
 *                        placeholders for it; a channel without them ignores the flag. Costs output
 *                        tokens, so it is the reader's setting and never this function's default.
 * @param example         Ask for one English example sentence as well. Same rules as @p etymology.
 * @return A compact JSON body, ready to POST.
 * @throws std::logic_error If that channel's protocol has not been loaded.
 */
QByteArray buildRequestBody(const Config&      config,
                            Channel            channel,
                            const QStringList& inputs,
                            const QString&     explanationLang,
                            const QString&     preset    = QStringLiteral("default"),
                            bool               etymology = false,
                            bool               example   = false);

/**
 * @brief Validate a response that arrives from the network and must not be trusted.
 *
 * Checks, in order: the HTTP envelope is JSON with a `choices` array; finish_reason is
 * `stop`; the message content is JSON; it carries a `results` array; every element is an
 * object holding every field that channel's response schema marks required; the fields the
 * overlay shows are non-empty; and the returned words match the requested ones exactly,
 * with no word missing or misspelled. A word the model answers more than once is not a
 * failure: the repeat is folded into the entry already held, because a model that writes one
 * result per sense names its word once per sense, and losing definitions that arrived would
 * be the worse trade.
 *
 * Any failure rejects the whole batch rather than dropping individual words, because a
 * partial batch would silently explain the wrong thing. The caller sees either the full
 * validated batch or one message.
 *
 * @param channel       Which protocol was spoken; selects the response schema.
 * @param responseBody  Raw HTTP response body.
 * @param expectedInputs The payload the request asked for, in request order.
 * @return The explanations in request order on success, or a reader-facing message
 *         (never containing the API key, and marked for translation).
 * @throws std::logic_error If that channel's protocol has not been loaded.
 */
std::variant<QVector<Explanation>, QString> parseExplanations(Channel            channel,
                                                              const QByteArray&  responseBody,
                                                              const QStringList& expectedInputs,
                                                              QString const&     explanationLang = QStringLiteral("en"),
                                                              bool               multipleSenses  = false);

/// The longest English sentence the breakdown takes: the starting value, not yet measured on real text.
inline constexpr int kBreakdownMaxWords = 40;

/**
 * @brief Split a sentence into the words a breakdown indexes, on whitespace.
 *
 * Punctuation stays with the word it is attached to, so the words are the same units the reader
 * sees and the indices the model answers with name them unambiguously.
 *
 * @param sentence The selected text.
 * @return The words in order; empty for a blank selection.
 */
QStringList breakdownWords(QString const& sentence);

/**
 * @brief Whether a selection is one the breakdown is offered for: English, and no more than
 *        kBreakdownMaxWords words.
 *
 * "English" means no letter outside ASCII. Curly quotes and dashes are punctuation and do not
 * count against it.
 *
 * @param sentence The selected text.
 * @return True when the breakdown applies to @p sentence.
 */
bool breakdownApplies(QString const& sentence);

/// @return The wire code of @p role: S, V, C or A.
QString roleCode(Role role);

/// @brief A run of a sentence's text that plays one role: the unit the breakdown card colours.
struct BreakdownRun {
    QString text;
    QString role; ///< roleCode() of the run's role; empty for text no span marks.
};

/**
 * @brief Cut a sentence into the runs the breakdown card draws, in order.
 *
 * Whitespace between two words of one run belongs to that run, so a span reads as one band of colour
 * rather than a colour per word. Joined, the runs give back the sentence exactly.
 *
 * @param sentence The selected text.
 * @param spans    The validated answer from parseBreakdown(), indexed against breakdownWords().
 * @return The runs covering every character of @p sentence.
 */
QVector<BreakdownRun> breakdownRuns(QString const& sentence, QVector<RoleSpan> const& spans);

/// @return The roles @p spans use, as codes in the legend's fixed S, V, C, A order.
QStringList breakdownRoles(QVector<RoleSpan> const& spans);

/// @brief Whether a selection is one the spelling correction is offered for: a single token.
///
/// Decided by the token count alone, not by how the classifier filed the word: a misspelled word
/// the word list does not know is filed as an entity, and it still needs its correction (ADR 0129).
bool spellingApplies(QString const& text);

/// @brief Whether a selection is one the grammar correction is offered for: the same English sentences the
///        breakdown takes (UI-SPEC.md 4.10).
bool grammarApplies(QString const& text);

/// @brief One correction answer that failed the shape check; the reason is for the log only.
struct CorrectionRejected {};

/**
 * @brief Read the corrected text out of a spell or grammar answer.
 *
 * Checks the envelope and finish reason as parseBreakdown() does, then exactly one results entry
 * with a non-empty `corrected`. The spell channel also refuses a value with whitespace in it: a
 * sentence in place of a word is not a spelling.
 *
 * @param channel      Channel::Spell or Channel::Grammar; picks the whitespace rule.
 * @param responseBody Raw HTTP response body.
 * @return The corrected text, or CorrectionRejected.
 */
std::variant<QString, CorrectionRejected> parseCorrection(Channel channel, QByteArray const& responseBody);

/// @brief One run of corrected text; @p changed marks the words the model moved, for the underline.
struct CorrectionRun {
    QString text;
    bool    changed = false;
};

/**
 * @brief Cut the corrected sentence into runs, underlining the words that differ from the original.
 *
 * The comparison is word by word on whitespace, so a corrected word is marked whole. Joined, the runs
 * give back @p corrected exactly. Done here rather than trusting the model to mark its own edits.
 *
 * @param original  The reader's text.
 * @param corrected The model's corrected text.
 * @return The runs covering every character of @p corrected.
 */
QVector<CorrectionRun> correctionRuns(QString const& original, QString const& corrected);

/**
 * @brief Validate a breakdown answer. Every index must name a word, and no two runs may share one.
 *
 * Checks, in order: the envelope and finish reason as parseExplanations() does; exactly one results
 * entry; a non-empty spans array; each span's role is S, V, C or A; each index is a whole number in
 * [0, @p wordCount - 1] with from <= to; no two spans overlap. Any failure rejects the whole answer.
 *
 * @param responseBody Raw HTTP response body.
 * @param wordCount    Number of words the request sent.
 * @return The spans in order of their first word, or a reason for the log.
 */
std::variant<QVector<RoleSpan>, QString> parseBreakdown(QByteArray const& responseBody, int wordCount);

/**
 * @brief One answer assembled from the bytes of a streamed or whole-body reply.
 *
 * The reply handler owns the socket and nothing else: it hands every read to feed() and the last
 * read to finish(). Sniffing the two shapes, suppressing previews for an error status, consuming
 * SSE events, dropping a preview that drew nothing new, and the whole-body fallback all run here,
 * so they are exercised by recorded bytes rather than by a live or mock QNetworkReply.
 *
 * The final result goes through the same checks parseExplanations() uses, directly on the
 * model's content with no synthetic envelope in between.
 */
class StreamAssembler {
public:
    /**
     * @param channel        Which protocol is being spoken.
     * @param expectedInputs The payload the request asked for, for the finished check.
     * @param language       The explanation language the request was made in.
     * @param multipleSenses Whether the request asked for the multiple-sense shape.
     */
    StreamAssembler(Channel channel, QStringList expectedInputs, QString language, bool multipleSenses);

    /**
     * @brief Append bytes and report a preview that has something new to draw.
     * @param bytes  The read, which may end anywhere.
     * @param httpOk Whether the reply's status means the body is the answer. A false one still
     *               buffers the bytes but emits no preview.
     * @return The preview whose drawn fields changed, or nothing when none did.
     */
    std::optional<Explanation> feed(QByteArray const& bytes, bool httpOk);

    /**
     * @brief Append the last bytes and produce the validated result.
     * @param tail The final read.
     * @return The explanations, or a reader-facing reason.
     */
    std::variant<QVector<Explanation>, QString> finish(QByteArray const& tail);

    /// @return The token counts this answer carried; call after finish().
    Usage usage() const;

    /// @return Whether the answer arrived as SSE rather than as one whole body.
    bool streamed() const
    {
        return sse_;
    }

private:
    bool consume(QByteArray& buffer);

    Channel     channel_;
    QStringList expectedInputs_;
    QString     language_;
    bool        multipleSenses_;
    QByteArray  buffer_;
    QString     content_;
    QString     finishReason_;
    QJsonObject usageObject_;
    Explanation last_; ///< The preview emitted last, so a chunk that changes nothing is silent.
    bool        sse_     = false;
    bool        decided_ = false;
};

/**
 * @brief Read the ids out of OpenRouter's model answer.
 *
 * One source, one envelope. Every id the answer carries for a modality other than text, and
 * every routing variant (an id carrying `:`), is dropped -- what comes back is not the catalogue
 * but the part of it a chat completion can be sent to.
 *
 * A malformed answer is no answer: missing, non-array, oversized or otherwise unusable input
 * returns an empty list rather than a partial one that would hide the model the reader wanted.
 *
 * @param body Raw HTTP response body.
 * @return The ids this app can talk to, in the order the source gave them, or an empty list.
 */
QStringList parseModelIds(QByteArray const& body);

/**
 * @brief The ids one OpenRouter vendor prefix names, with that prefix removed.
 *
 * An id is matched on `prefix + "/"` exactly, so `deepseek-x/gpt` is not a `deepseek` id.
 *
 * @param ids    The whole catalogue, as parseModelIds() returned it.
 * @param prefix Vendor part of an OpenRouter id, such as "deepseek". An empty prefix returns
 *               @p ids unchanged: that is OpenRouter's own list, where the prefix is part of
 *               the id the API takes.
 * @return The matching ids, in the order @p ids gave them.
 */
QStringList idsForPrefix(QStringList const& ids, QString const& prefix);

/// @brief What one chunk of a streaming answer contributes to the whole.
///
/// A chunk is the `data:` payload of one SSE event; folding chunks rebuilds the one envelope
/// parseExplanations() already checks, so the finished-answer contract does not fork.
struct StreamDelta {
    QString     content;      ///< Text appended to the answer; empty on every chunk but a content one.
    QString     finishReason; ///< "stop" or "length", on the chunk where the model says it stopped.
    QJsonObject usage;        ///< Token counts, on the last chunk when the request asked for them.
};

/**
 * @brief Take the whole SSE events' data payloads off an unframed byte buffer.
 *
 * A chunk from the network can end anywhere: mid-line, mid-escape, or mid-UTF-8 code point.
 * Bytes are buffered until an event's blank-line terminator has arrived, and only then is the
 * event decoded -- so a multi-byte character split across two reads is never seen half-formed.
 * Both the LF and the CRLF terminator are accepted.
 *
 * @param buffer Bytes read so far. The bytes of the events returned are removed from it; a
 *               partial event and the bytes after it are left in place for the next read.
 * @return One entry per complete event that carried a `data:` field, in arrival order, with the
 *         prefix stripped and an event's several data lines joined.
 */
QVector<QByteArray> takeSsePayloads(QByteArray& buffer);

/**
 * @brief Read one SSE `data:` payload of an OpenAI-compatible stream.
 *
 * @param payload Event data, with the `data:` prefix already stripped.
 * @return The chunk's contributions, or nothing for `[DONE]`, a keep-alive, or anything else
 *         that is not one of the stream's own JSON chunks.
 */
std::optional<StreamDelta> parseStreamDelta(const QByteArray& payload);

/**
 * @brief Pull the fields that have finished arriving out of an answer still being written.
 *
 * The model answers one JSON object, and a stream delivers it a few characters at a time, so the
 * buffer usually ends mid-something. A field is taken only once its string value has closed --
 * half a word drawn is worse than the card growing one line later -- and a field whose value or
 * closing quote has not arrived is absent, exactly as when a finished answer leaves it out.
 *
 * Only `results[0]` is read: a batch of words draws the card for the answer named first, and the
 * rest are stored when the whole answer has arrived (see API.md section 3.5).
 *
 * @param channel Which protocol is being spoken; picks the field the title is read from.
 * @param content The model's answer as it has arrived; a fence or a line of prose is tolerated.
 * @return Whatever has completed so far. An answer with nothing complete yet yields an
 *         explanation with every field empty rather than a failure.
 */
Explanation parsePartialExplanation(Channel channel, const QString& content);

/**
 * @brief Read the token counts out of a response envelope.
 *
 * `usage` sits on the envelope, not in the payload, so it is outside everything
 * parseExplanations() checks: a response with perfect explanations and no `usage` is still a
 * success, and one with `usage` and a malformed payload still fails. Losing a line of the
 * cost tally is survivable; dropping an explanation that arrived intact is not.
 *
 * @param responseBody Raw HTTP response body.
 * @return The counts, or zero for any field the response omits or states as a non-number.
 */
Usage parseUsage(const QByteArray& responseBody);

}
