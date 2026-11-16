#pragma once

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QTimer>

#include <map>
#include <memory>

#include "speech/voice_output.h"

class QAudioOutput;
class QBuffer;
class QMediaPlayer;
class QTextToSpeech;

namespace lens::app {

class LocalVoice;

/**
 * @file qt_voice_output.h
 * @brief The VoiceOutput a reader's machine runs: Qt's player for recorded bytes, the Windows
 *        synthesiser Qt wraps, and sherpa-onnx for the local voices (docs/adr/0183).
 *
 * This class is the three devices SpeechDuty used to construct itself, moved behind the seam with
 * their lifetimes unchanged: the player is built on first use, so a run that never speaks never
 * opens an audio device; the synthesisers are built on first ask and kept, WinRT's asked before
 * SAPI's; the local engine stays loaded after the last press and is released once idle. Which of
 * the three a press goes to is the duty's decision (ADR 0134), never this class's.
 */
class QtVoiceOutput final : public QObject, public speech::VoiceOutput {
public:
    explicit QtVoiceOutput(QObject* parent = nullptr);
    ~QtVoiceOutput() override;

    QString systemVoice(QString const& accent) override;
    bool    installed(QString const& voiceId) override;
    bool    say(QString const& word, QString const& accent) override;
    bool    play(QByteArray const& bytes) override;
    void    speakLocal(QString const& word, QString const& accent, QString const& voiceId, LocalSink sink) override;
    void    stop() override;
    void    releaseLocalVoice(QString const& voiceId) override;

private:
    /// @brief Build the player on first use, so a run that never speaks never opens an audio device.
    void ensurePlayer();
    /// @brief The engine named @p name, built on first ask and kept.
    /// @return The engine, or nullptr when this Qt has no engine of that name.
    QTextToSpeech* engineFor(QString const& name);
    /// @brief The engine that serves @p accent and the voice it speaks, resolved once per accent.
    ///
    /// AppController::settings() reads the answer on every projection, and QML rebuilds a
    /// projection on every settings change; enumerating the engine's voices costs a WinRT round
    /// trip per read. What the engine's constructor enumerated cannot change during a run, so the
    /// answer is kept per accent.
    struct SystemVoice {
        QTextToSpeech* engine = nullptr;
        /// The chosen voice's name; empty when no engine has one for the accent.
        QString name;
    };
    SystemVoice const& systemVoiceFor(QString const& accent);
    /// @brief Report a state change the duty did not ask for; see VoiceOutput::onSoundState.
    void notifySound(bool playing, QString const& word);
    /// @brief Hand the local engine to the worker to be released there, after whatever is queued for it.
    void retireLocalVoice();

    std::unique_ptr<QMediaPlayer> player_;
    std::unique_ptr<QAudioOutput> output_;
    /// The mp3 or wav the player is reading. Alive from play() to EndOfMedia and not one frame longer.
    std::unique_ptr<QBuffer> buffer_;
    /// Set while stop() runs: clearing the source makes the player report NoMedia, and that
    /// report would come straight back here. The guard is what keeps one unbind one unbind.
    bool releasing_ = false;

    /// Each synthesiser, built on first use. Its own `stateChanged` is what says the word is
    /// being read and when it stopped, so nothing here guesses at how long a voice takes.
    ///
    /// Keyed by engine name because the engine is chosen per accent: WinRT's is asked first and
    /// SAPI is asked when WinRT has no voice for the accent, so a machine whose English voice is
    /// only in the older engine still speaks. Each is built at most once.
    std::map<QString, std::unique_ptr<QTextToSpeech>> engines_;
    /// Each accent's answer, kept: see systemVoiceFor().
    std::map<QString, SystemVoice> systemVoices_;
    /// The word the synthesiser was handed, which its state signals do not carry back.
    QString systemWord_;

    /// The local engine, bound to the voice and accent it was made for. Replaced when either changes, and
    /// released after five minutes without a press (localVoiceIdle_). Only the worker uses it: a queued task
    /// holds its own copy, so the engine outlives a release until the task that needs it has finished.
    std::shared_ptr<LocalVoice> localVoice_;
    /// Counts from the last local press. When it runs out the engine is released, and the next press loads it again.
    QTimer localVoiceIdle_;
    /// Runs the local engine on one thread: sherpa-onnx's engine is not entered from two threads at once.
    QThreadPool localPool_;
};

}
