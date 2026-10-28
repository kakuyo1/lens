#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <map>
#include <memory>

#include "app/duties/storage_duty.h"
#include "speech/audio_cache.h"
#include "speech/pronunciation_source.h"

class QAudioOutput;
class QMediaPlayer;
class QTextToSpeech;

/**
 * @file speech_duty.h
 * @brief One word, spoken once: the engine the reader chose, the recording that answers, and the
 *        player that puts it out.
 *
 * The contract is docs/adr/0134 and it is narrow on purpose. Only a word card's headword is ever
 * spoken -- not a sentence, an entity, a translation or a breakdown -- because the online fallback
 * is a dictionary's entry recordings rather than a synthesiser and answers 500 for a sentence. The
 * surface rules (where the speaker is drawn, what turns it) live in UI-SPEC.md 4.3; this class owns
 * the three engines and the order they are tried in, and nothing about a card.
 *
 * Three things are deliberately absent:
 *   - No notification, no retry, no focus. A word that cannot be spoken is not spoken, and the card
 *     stays exactly as usable as it was (ADR 0107).
 *   - No budget. A recording is free and is not sent to the explanation service, so it never reaches
 *     CostDuty or the daily total.
 *   - No state that outlives the sound. `playing` drives the speaker icon's colour and nothing else:
 *     it never changes the card's layout, its anchor or its clock.
 *
 * Two engines are wired: the Windows synthesiser Qt wraps (the default, through Qt's TextToSpeech
 * module -- WinRT's engine first, SAPI when the platform has no WinRT one) and the dictionary's own
 * recordings (the default fallback, API.md section 9). The third, a local neural model, is not in
 * this build: a voice has to be downloaded and verified first, and nothing downloads one yet.
 *
 * The synthesiser is built on first use rather than at construction. A run that never speaks and
 * never opens the settings page never initialises a speech engine, and a machine whose voice pack
 * has nothing for the accent in force answers "unavailable" once and keeps answering it.
 */

namespace lens::app {

class SpeechDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.speechDuty")
public:
    /**
     * @brief Bind the duty to the source that fetches and the cache that remembers.
     *
     * Both are taken rather than built, the seam PictureDuty takes: the cache root and the network
     * manager are the composition root's, so the duty's own decisions can be reached without a
     * socket or a sound card.
     *
     * @param source  The recording source, owned by the caller.
     * @param cache   The audio cache, owned by the caller.
     * @param storage The document the two switches and the two engine rows are read from.
     */
    SpeechDuty(speech::PronunciationSource& source, speech::AudioCache& cache, StorageDuty& storage, QObject* parent = nullptr);
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
    Q_INVOKABLE void speak(QString word);

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
    /// @return Whether the accent in force has a downloaded, verified local voice. Always false
    ///         until the model downloader lands (step 4).
    bool localAvailable() const;

    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(QString playingWord READ playingWord NOTIFY playingChanged)
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY cacheBytesChanged)

signals:
    /// @brief Something started or stopped; `playing` and `playingWord` hold the answer.
    void playingChanged();
    /// @brief The cache grew or was cleared; the About page's size line reads it.
    void cacheBytesChanged();

private:
    /// @brief Build the player on first use, so a run that never speaks never opens an audio device.
    void ensurePlayer();
    /// @brief The engine named @p name, built on first ask and kept.
    /// @return The engine, or nullptr when this Qt has no engine of that name.
    QTextToSpeech* engineFor(QString const& name);
    /// @brief The engine that can read @p accent, or nullptr when none on this machine can.
    QTextToSpeech* systemFor(QString const& accent);
    /// @brief Read @p word through the system voice for the accent in force.
    void speakSystem(QString const& word);
    /// @brief The dictionary's recording: the cache first, the source only on a miss.
    void playOnline(QString const& word, QString const& accent);
    /**
     * @brief Put @p bytes through the player.
     *
     * The old source is unbound before the new buffer is attached, and the buffer is held for as
     * long as the player reads it: a QBuffer destroyed under a player that still holds it is a
     * dangling read, which is why goldendict-ng's player unbinds in stop() and this one does too
     * (docs/research/tts-survey-2026-10-28.html section 04).
     */
    void playBytes(QString const& word, QByteArray const& bytes);
    /// @brief Detach the buffer and put the player down; the playing flags follow.
    void release();
    void setPlaying(bool playing, QString const& word = {});

    speech::PronunciationSource& source_;
    speech::AudioCache&          cache_;
    StorageDuty&                 storage_;

    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> output_;
    /// The mp3 the player is reading. Alive from play() to EndOfMedia and not one frame longer.
    std::unique_ptr<QBuffer> buffer_;

    /// The Windows synthesiser, built on first use. Its own `stateChanged` is what says the word is
    /// being read and when it stopped, so this class never guesses at how long a voice takes.
    ///
    /// Keyed by engine name because the engine is chosen per accent: WinRT's is asked first and
    /// SAPI is asked when WinRT has no voice for the accent, so a machine whose English voice is
    /// only in the older engine still speaks. Each is built at most once.
    std::map<QString, std::unique_ptr<QTextToSpeech>> engines_;
    /// The word the synthesiser was handed, which its state signals do not carry back.
    QString systemWord_;

    /// Which press owns the speaker. A recording that arrives for an older one is still cached and
    /// is played nowhere -- the same rule PictureDuty's requestId_ states for the picture strip.
    int     requestId_ = 0;
    bool    playing_   = false;
    QString playingWord_;
    /// Set while release() runs: clearing the source makes the player report NoMedia, and that
    /// report would come straight back here. The guard is what keeps one unbind one unbind.
    bool releasing_ = false;
};

}
