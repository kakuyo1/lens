#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "app/duties/storage_duty.h"
#include "speech/audio_cache.h"
#include "speech/online_sources.h"
#include "speech/voice_downloader.h"

namespace lens::speech {
class VoiceOutput;
}

/**
 * @file speech_duty.h
 * @brief One word, spoken once: the engine the reader chose, the recording that answers, and the
 *        press that owns the speaker.
 *
 * The contract is docs/adr/0134 and it is narrow on purpose. Only a word card's headword is ever
 * spoken -- not a sentence, an entity, a translation or a breakdown -- because the online fallback
 * is a dictionary's entry recordings rather than a synthesiser and answers 500 for a sentence. The
 * surface rules (where the speaker is drawn, what turns it) live in UI-SPEC.md 4.3; this class owns
 * which of the three engines a press goes to and nothing about a card.
 *
 * Three things are deliberately absent:
 *   - No notification, no retry, no focus. A word that cannot be spoken is not spoken, and the card
 *     stays exactly as usable as it was (ADR 0107).
 *   - No budget. A recording is free and is not sent to the explanation service, so it never reaches
 *     CostDuty or the daily total.
 *   - No state that outlives the sound. `playing` drives the speaker icon's colour and nothing else:
 *     it never changes the card's layout, its anchor or its clock.
 *
 * Three engines: the Windows synthesiser Qt wraps (the default), the online recordings the reader
 * picked (API.md sections 9 and 9.2, docs/adr/0138), and a local voice the reader downloaded and
 * verified (docs/adr/0135). The local voice is never named by a fallback the reader did not choose.
 * What the engines and the player are is behind speech::VoiceOutput (docs/adr/0183): a press is
 * planned here, and every device question -- whether this machine has a voice for the accent,
 * whether the local voice is installed, how a word comes out -- is the output's answer. A case
 * replaces the output with a silent one and drives the whole press path without a sound card.
 */

namespace lens::app {

class SpeechDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.speechDuty")
public:
    /**
     * @brief Bind the duty to the source that fetches, the cache that remembers, and the machine's
     *        output.
     *
     * The source and the cache are taken rather than built, the seam PictureDuty takes: the cache
     * root and the network manager are the composition root's, so the duty's own decisions can be
     * reached without a socket or a sound card.
     *
     * @param sources The recording source, owned by the caller.
     * @param cache   The audio cache, owned by the caller.
     * @param output  The machine's voice engines and player, owned by the caller and outliving this
     *                object.
     * @param storage The document the two switches and the two engine rows are read from.
     */
    SpeechDuty(speech::OnlineSources& sources, speech::AudioCache& cache, speech::VoiceOutput& output, StorageDuty& storage, QObject* parent = nullptr);
    ~SpeechDuty() override;

    /**
     * @brief Read one word in the accent in force, through the engine in force.
     *
     * A press while something is playing stops it first: the reader pressing the speaker twice
     * wants the word again, not two recordings on top of each other. Anything that is not a single
     * headword is ignored, and so is a word no engine can serve -- silently, both times.
     *
     * @param word The card's headword. Only this is sent anywhere (SECURITY.md's outbound list).
     */
    Q_INVOKABLE void speak(QString const& word);

    /// @brief Stop whatever is playing and forget it. Called when the card goes, too.
    Q_INVOKABLE void stop();

    /// @return Whether a recording is coming out of the speaker now.
    bool playing() const;
    /// @return The word being read, so a card can tell its own speaker from another card's.
    QString playingWord() const;
    /// @return The bytes the cache holds, for the About page's one storage line.
    qint64 cacheBytes() const;
    /// @brief Delete every cached recording; the next press downloads it again.
    Q_INVOKABLE void clearCache();

    // Read by AppController::settings() rather than by a surface: each one describes this machine
    // at the moment the projection is taken, and a projection is where a surface gets that sort of
    // fact (the settings page says "Unavailable" from these three, and nothing binds to them).
    // Not const: the first call builds the synthesiser, which is what makes the answer knowable.

    /// @return The accent in force, "us" or "uk", as the settings document stores it.
    QString accent() const;
    /// @return Whether this machine has a system voice for the accent in force. False on a machine
    ///         whose voice pack has no English voice for it, and false when Qt has no engine at all.
    bool systemAvailable();
    /// @return The system voice's own name, for the settings page. Empty when there is none: the
    ///         reader has to be able to see whose voice they are hearing (ADR 0134).
    QString systemVoice();
    /// @return Whether the local voice the setting names serves the accent in force and is on this
    ///         machine, in full. The check is a size check (speech::installed()).
    bool localAvailable() const;

    /// @brief Fetch the named local voice ("kokoro" or "ljspeech") into this machine. Ignored while
    ///        another is downloading, and for an id the catalogue does not hold.
    Q_INVOKABLE void downloadVoice(QString const& id);
    /// @brief Stop the download in flight; the partial files go with it.
    Q_INVOKABLE void cancelVoiceDownload();
    /// @brief Delete the named voice's files from this machine. Ignored while it is downloading.
    Q_INVOKABLE void removeVoice(QString const& id);
    /// @return Per local voice, keyed by its id: `installed`, `downloading`, and `percent` while it is.
    QVariantMap voiceStates() const;

    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString playingWord READ playingWord NOTIFY playingChanged)
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY cacheBytesChanged)
    Q_PROPERTY(QVariantMap voiceStates READ voiceStates NOTIFY voicesChanged)

signals:
    /// @brief Something started or stopped; `playing` and `playingWord` hold the answer.
    void playingChanged();
    /// @brief The cache grew or was cleared; the About page's size line reads it.
    void cacheBytesChanged();
    /// @brief A voice started, finished, was removed, or moved a whole percent while downloading.
    void voicesChanged();

private:
    /// @brief Read @p word through the system voice for the accent in force.
    void speakSystem(QString const& word);
    /// @brief The dictionary's recording: the cache first, the source only on a miss.
    void playOnline(QString const& word, QString const& accent);
    /// @brief Read @p word through the local voice the setting names, on the output's worker; the
    ///        recording is played back on this thread, and only if it is still the press that is current.
    void speakLocal(QString const& word, QString const& accent);
    /// @brief Put @p bytes through the output and light the speaker.
    void playBytes(QString const& word, QByteArray const& bytes);
    void setPlaying(bool playing, QString const& word = {});

    speech::OnlineSources& sources_;
    speech::AudioCache&    cache_;
    speech::VoiceOutput&   output_;
    StorageDuty&           storage_;

    /// Fetches the local voices. Built with no parent: a member that also had the duty as its parent
    /// would be deleted twice, once by the parent and once as a member.
    speech::VoiceDownloader downloader_;
    /// The percent of the voice being downloaded, moved on whole steps so the page is not redrawn per chunk.
    int voicePercent_ = 0;

    /// Which press owns the speaker. A recording that arrives for an older one is still cached and
    /// is played nowhere -- the same rule PictureDuty's requestId_ states for the picture strip.
    int     requestId_ = 0;
    bool    playing_   = false;
    QString playingWord_;
};

}
