/** @file subtitle_source.cpp
 * @brief The source choice behind subtitle_source.h: the scoring rule, the external-file scan and
 *        the load that ties the two parsers together.
 */

#include "core/subtitle/subtitle_source.h"

#include "core/subtitle/subtitle_mkv.h"
#include "core/subtitle/subtitle_parse.h"
#include "util/text.h"

#include <limits>
#include <system_error>
#include <utility>

namespace lens::core::subtitle {
namespace {

constexpr int         kNever      = std::numeric_limits<int>::min();
constexpr std::size_t kSampleCues = 40;
/// Above this share of the sampled letters the track is a bilingual or Chinese one, not the
/// English dialogue the scan reads.
constexpr double kCjkLimit = 0.15;

bool isCjkCodePoint(char32_t const code)
{
    return (code >= 0x2E80 and code <= 0x303F) or // radicals and CJK punctuation
           (code >= 0x3040 and code <= 0x30FF) or // kana
           (code >= 0x3400 and code <= 0x4DBF) or (code >= 0x4E00 and code <= 0x9FFF) or
           (code >= 0xF900 and code <= 0xFAFF) or (code >= 0xFF00 and code <= 0xFFEF); // fullwidth forms
}

/// @return The share of @p track's sampled letters that are CJK; 0 when nothing decodes.
double cjkShare(TextTrack const& track)
{
    std::size_t cjk     = 0;
    std::size_t letters = 0;
    for (std::size_t i = 0; i < track.cues.size() and i < kSampleCues; ++i) {
        for (auto const& line : track.cues[i].lines) {
            auto const decoded = util::decodeUtf8(line);
            if (not decoded) continue;
            for (auto const code : *decoded) {
                if (code < 0x80) {
                    if (util::isAsciiAlnum(static_cast<char>(code))) ++letters;
                    continue;
                }
                ++letters;
                if (isCjkCodePoint(code)) ++cjk;
            }
        }
    }
    return letters == 0 ? 0.0 : static_cast<double>(cjk) / static_cast<double>(letters);
}

/// @return Whether @p name carries an SDH/CC style marker as a word of its own.
bool nameMarksCaptions(std::string const& name)
{
    std::string const lower = util::asciiLower(name);
    std::string       token;
    for (char const c : lower + '.') {
        if (util::isAsciiAlnum(c)) {
            token += c;
            continue;
        }
        if (token == "sdh" or token == "cc" or token == "hearing") return true;
        token.clear();
    }
    return false;
}

int scoreOf(TextTrack const& track)
{
    if (track.forced or track.cues.empty()) return kNever;
    if (cjkShare(track) > kCjkLimit) return kNever;
    int score = 0;
    if (track.language.starts_with("en")) {
        score += 40;
    } else if (not track.language.empty()) {
        score -= 20; // named as another language: the reader wants the English dialogue
    }
    if (nameMarksCaptions(track.name)) score -= 15;
    if (track.hearingImpaired) score -= 15;
    if (track.isDefault) score += 5;
    return score;
}

std::string toUtf8(std::filesystem::path const& path)
{
    auto const u8 = path.u8string();
    return std::string{reinterpret_cast<char const*>(u8.data()), u8.size()};
}

std::string extensionOf(std::filesystem::path const& path)
{
    auto const        u8 = path.extension().u8string();
    std::string const ext{reinterpret_cast<char const*>(u8.data()), u8.size()};
    return ext.empty() or ext.front() != '.' ? ext : ext.substr(1);
}

std::string fileNameOf(std::filesystem::path const& path)
{
    auto const u8 = path.filename().u8string();
    return std::string{reinterpret_cast<char const*>(u8.data()), u8.size()};
}

}

int chooseTextTrack(std::vector<TextTrack> const& tracks)
{
    int best      = -1;
    int bestScore = kNever;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        auto const score = scoreOf(tracks[i]);
        if (score > bestScore) {
            best      = static_cast<int>(i);
            bestScore = score;
        }
    }
    return best;
}

std::filesystem::path chooseExternalSubtitle(std::filesystem::path const& videoPath)
{
    std::error_code   error;
    auto const        dir    = videoPath.parent_path().empty() ? std::filesystem::path{"."} : videoPath.parent_path();
    auto const        stemU8 = videoPath.stem().u8string();
    std::string const stem   = util::asciiLower(std::string{reinterpret_cast<char const*>(stemU8.data()), stemU8.size()});

    struct Candidate {
        int                   suffixRank = -1; // 3 en/eng, 2 bare stem, 1 another suffix
        int                   extRank    = 99;
        std::string           name;
        std::filesystem::path path;
    } best;

    for (std::filesystem::directory_iterator it{dir, error}, end; not error and it != end; it.increment(error)) {
        std::error_code fileError;
        if (not it->is_regular_file(fileError) or fileError) continue;
        auto const ext     = util::asciiLower(extensionOf(it->path()));
        int const  extRank = ext == "srt" ? 0 : ext == "vtt" ? 1
                                            : ext == "ass"   ? 2
                                            : ext == "ssa"   ? 3
                                                             : -1;
        if (extRank < 0) continue;
        auto const  name = util::asciiLower(fileNameOf(it->path()));
        std::string bare = stem;
        bare += '.';
        bare += ext;
        int suffixRank = -1;
        if (name == bare) {
            suffixRank = 2;
        } else if (name.starts_with(stem + ".") and name.ends_with("." + ext)) {
            auto const middle = name.substr(stem.size() + 1, name.size() - stem.size() - ext.size() - 2);
            auto const dot    = middle.rfind('.');
            auto const last   = dot == std::string::npos ? middle : middle.substr(dot + 1);
            if (not middle.empty()) suffixRank = last == "en" or last == "eng" ? 3 : 1;
        }
        if (suffixRank < 0) continue;
        bool const better = suffixRank > best.suffixRank or
                            (suffixRank == best.suffixRank and
                             (extRank < best.extRank or (extRank == best.extRank and name < best.name)));
        if (better) best = {.suffixRank = suffixRank, .extRank = extRank, .name = name, .path = it->path()};
    }
    return best.path;
}

SubtitleLoad loadSubtitlesFor(std::filesystem::path const& videoPath)
{
    SubtitleLoad load;
    auto const   external = chooseExternalSubtitle(videoPath);
    if (not external.empty()) {
        std::string codec;
        load.cues = parseSubtitleFile(external, &codec);
        if (not load.cues.empty()) {
            load.path  = toUtf8(external);
            load.codec = std::move(codec);
            return load;
        }
        load.note = "the external subtitle beside the video could not be read";
    }
    auto       tracks = readMkvSubtitleTracks(videoPath);
    auto const index  = chooseTextTrack(tracks);
    if (index < 0) {
        if (not load.note.empty()) load.note += "; ";
        load.note += tracks.empty() ? "no text subtitle track the scan can read"
                                    : "no subtitle track matched the learner's language";
        return load;
    }
    auto& track = tracks[static_cast<std::size_t>(index)];
    load.path   = toUtf8(videoPath);
    load.codec  = track.codec;
    if (not track.language.empty()) load.codec += " " + track.language;
    load.cues = std::move(track.cues);
    return load;
}

}
