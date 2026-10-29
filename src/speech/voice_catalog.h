#pragma once

#include <QByteArray>
#include <QString>

#include <filesystem>
#include <span>

/**
 * @file voice_catalog.h
 * @brief The local voices docs/adr/0135 allows: the files each one is made of, the accent it speaks,
 *        where it lives, and how a finished recording is wrapped.
 *
 * No sherpa-onnx and no network in here. The catalogue is what a downloader fetches and an engine
 * loads, so a case can pin every number in it. The engine is src/app/platform/local_voice.h; the fetch
 * is voice_downloader.h.
 *
 * Every file is pinned to one revision of its Hugging Face repository, and each size and digest was
 * read from that revision. Moving a pin is a decision recorded in an ADR, not a refresh.
 */

namespace lens::speech {

/// @brief A local voice the settings page can name. Exactly these two ship in the catalogue.
enum class VoiceModel {
    Kokoro,   ///< Kokoro int8 multi-lang v1.1: English in both accents. What automatic means.
    LjSpeech, ///< VITS trained on LJSpeech: one American voice. Only when the reader names it.
};

/// @brief One file a voice needs, and the bytes it must be.
struct VoiceFile {
    QString name;   ///< The file's name in the repository, and in the voice's folder.
    qint64  size;   ///< The exact byte count.
    QString sha256; ///< Lower-case hex SHA-256 of the whole file.
};

/// @brief Where a voice comes from and what it is made of.
struct VoiceSpec {
    QString                    id;       ///< The folder under the voices root, and the value the settings page names.
    QString                    repo;     ///< The Hugging Face repository, `owner/name`.
    QString                    revision; ///< The commit of that repository the files are pinned to.
    std::span<VoiceFile const> files;    ///< Everything the engine loads, and nothing else.
};

/**
 * @brief The voice a `speechLocalEngine` value names.
 * @param setting A stored `speechLocalEngine`.
 * @return LjSpeech for "ljspeech"; Kokoro for "kokoro" and for "auto". Anything else reads as Kokoro,
 *         so an unknown value is the automatic choice rather than silence.
 */
VoiceModel voiceFrom(QString const& setting);

/// @return The catalogue entry for @p model.
VoiceSpec const& voiceSpec(VoiceModel model);

/**
 * @brief Whether @p model has an English voice for @p accent.
 * @param accent "us" or "uk". LjSpeech has no British voice, so it serves American only.
 */
bool serves(VoiceModel model, QString const& accent);

/**
 * @brief The speaker id @p model reads @p accent with.
 *
 * Kokoro's ids are the order its voices file is written in (sherpa-onnx's
 * scripts/kokoro/v1.1-zh/generate_voices_bin.py): 0 is af_maple, American; 2 is bf_vale, British.
 * LjSpeech has one speaker. Only meaningful where serves() is true.
 */
int speakerFor(VoiceModel model, QString const& accent);

/// @return The pronunciation lexicon @p accent reads through: one of the voice's own files.
QString lexiconFor(VoiceModel model, QString const& accent);

/// @return The folder the voices live in: `%APPDATA%\Lens\voices`.
std::filesystem::path voiceRoot();

/**
 * @brief Whether every file @p model needs is under @p root at its exact size.
 *
 * A size check, not a digest. It runs on every press, and the digest was checked when the file
 * arrived (verifyFile()). A file replaced with one of the same size would pass; that is the limit.
 */
bool installed(VoiceModel model, std::filesystem::path const& root);

/// @return Whether @p path is exactly @p file: its size, and its SHA-256.
bool verifyFile(QString const& path, VoiceFile const& file);

/**
 * @brief Wrap float samples as a 16-bit mono PCM WAV.
 * @param samples    Samples in [-1, 1]; anything outside is clipped.
 * @param sampleRate The rate the engine produced them at.
 */
QByteArray wavBytes(std::span<float const> samples, int sampleRate);

}
