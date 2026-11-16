#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

/**
 * @file voice_output.h
 * @brief The sound side of pronunciation: the machine's three voice engines and the player they
 *        all end at, behind one seam (docs/adr/0183).
 *
 * SpeechDuty decides what is heard -- the engine the reader chose, the fallback rule, the
 * recording cache, which press owns the speaker -- and asks this for how it is heard. The Qt
 * adapter (app/platform/qt_voice_output.h) is what a reader's machine runs; a silent adapter is
 * what a case runs, so the press path is exercised without a sound card. ADR 0134 owns the engine
 * choice and this module never makes one: `say`, `speakLocal` and `play` are asked, not picked.
 *
 * Every call runs on the thread that owns the adapter, except the local synthesis, whose sink is
 * called back on the calling thread. The adapter never reports progress the caller did not ask
 * for except through onSoundState: nothing here notifies, retries or focuses (ADR 0107).
 */

namespace lens::speech {

class VoiceOutput {
public:
    /// @brief Called with the WAV, or not at all when the voice could not read the word.
    using LocalSink = std::function<void(QByteArray const& wav)>;

    virtual ~VoiceOutput() = default;

    /**
     * @brief The sound's own state, when it changes without a press asking: `true` with the word
     *        when a synthesiser takes it (the Windows engine reports Speaking asynchronously),
     *        `false` when it finished or failed, and when a clip ends.
     */
    std::function<void(bool playing, QString const& word)> onSoundState;

    /**
     * @brief The system voice that serves @p accent.
     * @param accent "us" or "uk".
     * @return Its name, or empty when no engine on this machine has one for the accent -- the
     *         answer the settings page shows and systemAvailable() is read from.
     */
    virtual QString systemVoice(QString const& accent) = 0;

    /**
     * @brief Whether the local voice @p voiceId is on this machine, every file at its exact size.
     * @param voiceId "kokoro" or "ljspeech", as the settings page names it.
     */
    virtual bool installed(QString const& voiceId) = 0;

    /**
     * @brief Read @p word through the system voice for @p accent.
     * @return False when no engine here serves the accent; the word is then not read.
     */
    virtual bool say(QString const& word, QString const& accent) = 0;

    /**
     * @brief Put @p bytes through the player, replacing whatever it held.
     * @return False when the recording could not be opened.
     */
    virtual bool play(QByteArray const& bytes) = 0;

    /**
     * @brief Read @p word with the local voice, on its own worker.
     * @param voiceId "kokoro" or "ljspeech"; the caller has checked installed() and speech::serves().
     * @param sink Runs once on the thread that called this, when a WAV came out.
     */
    virtual void speakLocal(QString const& word, QString const& accent, QString const& voiceId, LocalSink sink) = 0;

    /// @brief Silence everything: the clip, and any synthesiser still reading.
    virtual void stop() = 0;

    /**
     * @brief Let the local engine go, and return once no worker is reading the voice's files.
     * @param voiceId The voice being removed; another voice's loaded engine is left alone.
     */
    virtual void releaseLocalVoice(QString const& voiceId) = 0;
};

}
