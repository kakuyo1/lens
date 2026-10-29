#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <QThreadPool>
#include <QVariantMap>

#include <map>
#include <memory>

#include "app/duties/storage_duty.h"
#include "speech/audio_cache.h"
#include "speech/pronunciation_source.h"
#include "speech/voice_downloader.h"

class QAudioOutput;
class QMediaPlayer;
class QTextToSpeech;

namespace lens::app {
class LocalVoice;
}

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
 * Three engines: the Windows synthesiser Qt wraps (the default, through Qt's TextToSpeech module --
 * WinRT's engine first, SAPI when the platform has no WinRT one), the dictionary's own recordings
 * (API.md section 9), and a local voice the reader downloaded and verified (docs/adr/0135), spoken
 * through sherpa-onnx on one worker thread. The local voice is never named by a fallback the reader did
 * not choose.
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
    /// @return Whether the local voice the setting names serves the accent in force and is on this
    ///         machine, in full. The check is a size check (speech::installed()).
    bool localAvailable() const;

    /// @brief Fetch the named local voice ("kokoro" or "ljspeech") into this machine. Ignored while
    ///        another is downloading, and for an id the catalogue does not hold.
    Q_INVOKABLE void downloadVoice(QString id);
    /// @brief Stop the download in flight; the partial files go with it.
    Q_INVOKABLE void cancelVoiceDownload();
    /// @brief Delete the named voice's files from this machine. Ignored while it is downloading.
    Q_INVOKABLE void removeVoice(QString id);
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
    /// @brief Build the player on first use, so a run that never speaks never opens an audio device.
    void ensurePlayer();
    /// @brief The engine named @p name, built on first ask and kept.
    /// @return The engine, or nullptr when this Qt has no engine of that name.
    QTextToSpeech* engineFor(QString const& name);
    /// @brief The engine that serves @p accent and the voice it speaks, resolved once per accent.
    ///
    /// AppController::settings() reads the two answers below on every projection, and QML rebuilds
    /// a projection on every settings change; enumerating the engine's voices costs a WinRT round
    /// trip per read. What the engine's constructor enumerated cannot change during a run, so the
    /// answer is kept per accent.
    struct SystemVoice {
        QTextToSpeech* engine = nullptr;
        /// The chosen voice's name; empty when no engine has one for the accent.
        QString name;
    };
    SystemVoice const& systemVoiceFor(QString const& accent);
    /// @brief The engine that can read @p accent, or nullptr when none on this machine can.
    QTextToSpeech* systemFor(QString const& accent);
    /// @brief Read @p word through the system voice for the accent in force.
    void speakSystem(QString const& word);
    /// @brief The dictionary's recording: the cache first, the source only on a miss.
    void playOnline(QString const& word, QString const& accent);
    /// @brief Read @p word through the local voice the setting names, on the worker; the recording is
    ///        played back on this thread, and only if it is still the press that is current.
    void speakLocal(QString const& word, QString const& accent);
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
    /// Each accent's answer, kept: see systemVoiceFor(). Keyed by accent because the engine is
    /// chosen per accent, and a machine may serve British from one engine and American from another.
    std::map<QString, SystemVoice> systemVoices_;
    /// The word the synthesiser was handed, which its state signals do not carry back.
    QString systemWord_;

    /// The local engine, bound to the voice and accent it was made for. Replaced when either changes,
    /// and only ever used by the worker: the copy a queued task holds keeps it alive until that task ends.
    std::shared_ptr<LocalVoice> localVoice_;
    /// Fetches the local voices. Built with no parent: a member that also had the duty as its parent
    /// would be deleted twice, once by the parent and once as a member.
    speech::VoiceDownloader downloader_;
    /// Runs the local engine on one thread: sherpa-onnx's engine is not entered from two threads at once.
    QThreadPool localPool_;
    /// The percent of the voice being downloaded, moved on whole steps so the page is not redrawn per chunk.
    int voicePercent_ = 0;

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
