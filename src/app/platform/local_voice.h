#pragma once

#include <QByteArray>
#include <QString>

#include <filesystem>
#include <optional>

#include "speech/voice_catalog.h"

struct SherpaOnnxOfflineTts;

/**
 * @file local_voice.h
 * @brief One downloaded voice, bound to one accent, spoken through sherpa-onnx.
 *
 * The engine is loaded by the first speak(), not by the constructor: loading a 114 MB model takes a
 * second or two, and it has to happen on the thread that speaks, not on the one that draws the card.
 * A voice is bound to an accent because Kokoro's lexicon is chosen by accent, and the engine reads
 * its lexicon once, at load (docs/adr/0135). Changing accent means a new object and a new load.
 *
 * Only this file names sherpa-onnx, and only the `lens` executable links it.
 */

namespace lens::app {

class LocalVoice {
public:
    /**
     * @brief Bind @p model to @p accent, with its files under @p root. Loads nothing.
     * @param accent "us" or "uk"; the caller checks speech::serves() first.
     */
    LocalVoice(speech::VoiceModel model, QString accent, std::filesystem::path root);
    ~LocalVoice();
    LocalVoice(LocalVoice const&)            = delete;
    LocalVoice& operator=(LocalVoice const&) = delete;

    /// @return The voice this object speaks with.
    speech::VoiceModel model() const;
    /// @return The accent this object speaks in.
    QString accent() const;

    /**
     * @brief Read one word.
     *
     * Blocking: one call takes a fraction of a second once loaded. Not thread-safe; the duty runs
     * every call on one worker thread.
     *
     * @param word A card's headword, already checked by speech::isHeadword().
     * @return A WAV, or nothing when the engine is missing or cannot read the word (a word outside the
     *         lexicon makes the engine fail, and that is the card staying silent).
     */
    std::optional<QByteArray> speak(QString const& word);

private:
    /// @return Whether the engine is loaded, loading it on the first call.
    bool load();

    speech::VoiceModel          model_;
    QString                     accent_;
    std::filesystem::path       root_;
    const SherpaOnnxOfflineTts* tts_ = nullptr;
};

}
