/** @file subtitle_mkv.cpp
 * @brief The EBML walk behind subtitle_mkv.h.
 */

#include "core/subtitle/subtitle_mkv.h"

#include "core/subtitle/subtitle_parse.h"
#include "util/log.h"
#include "util/text.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace lens::core::subtitle {
namespace {

// Element IDs exactly as the file spells them, marker bits included.
constexpr std::uint32_t kSegment           = 0x18538067;
constexpr std::uint32_t kInfo              = 0x1549A966;
constexpr std::uint32_t kTimestampScale    = 0x2AD7B1;
constexpr std::uint32_t kTracks            = 0x1654AE6B;
constexpr std::uint32_t kTrackEntry        = 0xAE;
constexpr std::uint32_t kTrackNumber       = 0xD7;
constexpr std::uint32_t kTrackType         = 0x83;
constexpr std::uint32_t kCodecId           = 0x86;
constexpr std::uint32_t kTrackLanguage     = 0x22B59C;
constexpr std::uint32_t kTrackLanguageIetf = 0x22B59D;
constexpr std::uint32_t kTrackName         = 0x536E;
constexpr std::uint32_t kFlagDefault       = 0x88;
constexpr std::uint32_t kFlagForced        = 0x55AA;
constexpr std::uint32_t kFlagHearing       = 0x55AB;
constexpr std::uint32_t kContentEncodings  = 0x6D80;
constexpr std::uint32_t kCluster           = 0x1F43B675;
constexpr std::uint32_t kClusterTimestamp  = 0xE7;
constexpr std::uint32_t kSimpleBlock       = 0xA3;
constexpr std::uint32_t kBlockGroup        = 0xA0;
constexpr std::uint32_t kBlock             = 0xA1;
constexpr std::uint32_t kBlockDuration     = 0x9B;

constexpr std::uint64_t kUnknownSize     = ~std::uint64_t{0};
constexpr std::uint64_t kMaxPayloadBytes = 8ull * 1024 * 1024;
constexpr std::uint64_t kMaxStringBytes  = 64ull * 1024;
/// The span the last cue of a track is held for when neither it nor its successor says how long.
constexpr std::int64_t kLastCueTailMs = 3000;

/// @return The byte length a leading marker byte announces for an ID (1..4), or zero.
int idLengthOf(std::uint8_t const first)
{
    if (first & 0x80) return 1;
    if (first & 0x40) return 2;
    if (first & 0x20) return 3;
    if (first & 0x10) return 4;
    return 0;
}

/// @return The byte length a leading marker byte announces for a size (1..8), or zero.
int sizeLengthOf(std::uint8_t const first)
{
    for (int length = 1; length <= 8; ++length)
        if (first & (0x80 >> (length - 1))) return length;
    return 0;
}

bool supportedCodec(std::string_view const codec)
{
    return codec == "S_TEXT/UTF8" or codec == "S_TEXT/ASS" or codec == "S_TEXT/SSA" or codec == "S_TEXT/WEBVTT";
}

class Reader {
public:
    explicit Reader(std::istream& in)
        : in_(in)
    {
        in_.seekg(0, std::ios::end);
        auto const end = in_.tellg();
        size_          = end < 0 ? 0 : static_cast<std::uint64_t>(end);
        in_.seekg(0, std::ios::beg);
    }

    [[nodiscard]] std::uint64_t position()
    {
        return static_cast<std::uint64_t>(in_.tellg());
    }
    [[nodiscard]] std::uint64_t size() const
    {
        return size_;
    }

    bool readByte(std::uint8_t* const out)
    {
        char byte = 0;
        if (not in_.get(byte)) return false;
        *out = static_cast<std::uint8_t>(byte);
        return true;
    }

    bool readBytes(char* const out, std::size_t const count)
    {
        return static_cast<bool>(in_.read(out, static_cast<std::streamsize>(count)));
    }

    bool skip(std::uint64_t const count)
    {
        auto const here = position();
        if (here > size_ or count > size_ - here) return false;
        in_.seekg(static_cast<std::streamoff>(count), std::ios::cur);
        return static_cast<bool>(in_);
    }

    bool seekTo(std::uint64_t const at)
    {
        if (at > size_) return false;
        in_.seekg(static_cast<std::streamoff>(at), std::ios::beg);
        return static_cast<bool>(in_);
    }

    struct Element {
        std::uint32_t id    = 0;
        std::uint64_t size  = kUnknownSize;
        bool          valid = false;
    };

    Element header()
    {
        std::uint8_t first = 0;
        if (not readByte(&first)) return {};
        auto const idLength = idLengthOf(first);
        if (idLength == 0) return {};
        std::uint32_t id = first;
        for (int k = 1; k < idLength; ++k) {
            std::uint8_t next = 0;
            if (not readByte(&next)) return {};
            id = (id << 8) | next;
        }
        std::uint8_t sizeFirst = 0;
        if (not readByte(&sizeFirst)) return {};
        auto const sizeLength = sizeLengthOf(sizeFirst);
        if (sizeLength == 0) return {};
        std::uint64_t const mask    = 0xFFu >> sizeLength;
        std::uint64_t       size    = sizeFirst & mask;
        bool                allOnes = size == mask;
        for (int k = 1; k < sizeLength; ++k) {
            std::uint8_t next = 0;
            if (not readByte(&next)) return {};
            size    = (size << 8) | next;
            allOnes = allOnes and next == 0xFF;
        }
        return {.id = id, .size = allOnes ? kUnknownSize : size, .valid = true};
    }

    std::uint64_t readUnsigned(std::uint64_t const count)
    {
        if (count > 8) {
            skip(count);
            return 0;
        }
        std::uint64_t value = 0;
        for (std::uint64_t k = 0; k < count; ++k) {
            std::uint8_t byte = 0;
            if (not readByte(&byte)) return 0;
            value = (value << 8) | byte;
        }
        return value;
    }

    std::string readString(std::uint64_t const count)
    {
        if (count > kMaxStringBytes) {
            skip(count);
            return {};
        }
        std::string out(static_cast<std::size_t>(count), '\0');
        if (count > 0 and not readBytes(out.data(), out.size())) return {};
        while (not out.empty() and out.back() == '\0')
            out.pop_back();
        return out;
    }

private:
    std::istream& in_;
    std::uint64_t size_ = 0;
};

struct ScanState {
    std::int64_t                                   timestampScaleNs = 1000000;
    std::vector<TextTrack>                         tracks;
    std::unordered_map<std::uint64_t, std::size_t> textTracksByNumber;
    int                                            skippedCompressed = 0;
    int                                            skippedLaced      = 0;
};

/// @return Whether @p size bytes of payload fit before the file's end, at @p out when they do.
bool endOf(Reader& reader, std::uint64_t const size, std::uint64_t* const out)
{
    auto const here = reader.position();
    if (size == kUnknownSize or here > reader.size() or size > reader.size() - here) return false;
    *out = here + size;
    return true;
}

bool skipElement(Reader& reader, Reader::Element const& element)
{
    if (element.size == kUnknownSize) return false;
    return reader.skip(element.size);
}

bool parseInfo(Reader& reader, Reader::Element const& element, ScanState& state)
{
    std::uint64_t end = 0;
    if (not endOf(reader, element.size, &end)) return false;
    while (reader.position() < end) {
        auto const child = reader.header();
        if (not child.valid or child.size == kUnknownSize) return false;
        if (child.id == kTimestampScale) {
            auto const scale = reader.readUnsigned(child.size);
            if (scale > 0 and scale <= 1000000000) state.timestampScaleNs = static_cast<std::int64_t>(scale);
        } else if (not reader.skip(child.size)) {
            return false;
        }
    }
    return true;
}

bool parseTracks(Reader& reader, Reader::Element const& element, ScanState& state)
{
    std::uint64_t end = 0;
    if (not endOf(reader, element.size, &end)) return false;
    while (reader.position() < end) {
        auto const child = reader.header();
        if (not child.valid) return false;
        std::uint64_t entryEnd = 0;
        if (not endOf(reader, child.size, &entryEnd)) return false;
        if (child.id != kTrackEntry) {
            if (not reader.skip(child.size)) return false;
            continue;
        }
        TextTrack     track;
        bool          subtitle   = false;
        bool          compressed = false;
        bool          hasNumber  = false;
        std::uint64_t number     = 0;
        std::string   language;
        std::string   languageIetf;
        while (reader.position() < entryEnd) {
            auto const field = reader.header();
            if (not field.valid or field.size == kUnknownSize) return false;
            switch (field.id) {
                case kTrackNumber:
                    number    = reader.readUnsigned(field.size);
                    hasNumber = true;
                    break;
                case kTrackType:
                    subtitle = reader.readUnsigned(field.size) == 0x11;
                    break;
                case kCodecId:
                    track.codec = reader.readString(field.size);
                    break;
                case kTrackLanguage:
                    language = reader.readString(field.size);
                    break;
                case kTrackLanguageIetf:
                    languageIetf = reader.readString(field.size);
                    break;
                case kTrackName:
                    track.name = reader.readString(field.size);
                    break;
                case kFlagDefault:
                    track.isDefault = reader.readUnsigned(field.size) != 0;
                    break;
                case kFlagForced:
                    track.forced = reader.readUnsigned(field.size) != 0;
                    break;
                case kFlagHearing:
                    track.hearingImpaired = reader.readUnsigned(field.size) != 0;
                    break;
                case kContentEncodings:
                    compressed = true;
                    if (not reader.skip(field.size)) return false;
                    break;
                default:
                    if (not reader.skip(field.size)) return false;
                    break;
            }
        }
        if (not subtitle or not hasNumber or number == 0) continue;
        if (not supportedCodec(track.codec)) continue;
        if (compressed) {
            ++state.skippedCompressed;
            continue;
        }
        track.number   = static_cast<int>(number);
        track.language = util::asciiLower(languageIetf.empty() ? language : languageIetf);
        state.textTracksByNumber.emplace(number, state.tracks.size());
        state.tracks.push_back(std::move(track));
    }
    return true;
}

/// @brief The lines of a raw SubRip (or WebVTT) block payload, angle tags stripped for the latter.
std::vector<std::string> blockLines(std::string_view const payload, bool const stripTags)
{
    std::vector<std::string> lines;
    std::size_t              start = 0;
    while (start <= payload.size()) {
        auto const       newline = payload.find('\n', start);
        auto const       end     = newline == std::string_view::npos ? payload.size() : newline;
        std::string_view line    = payload.substr(start, end - start);
        std::string      cleaned;
        if (stripTags) {
            bool inTag = false;
            for (char const c : line) {
                if (inTag) {
                    if (c == '>') inTag = false;
                    continue;
                }
                if (c == '<') {
                    inTag = true;
                    continue;
                }
                cleaned += c;
            }
        } else {
            cleaned = std::string{line};
        }
        // Trim the line's own whitespace, the carriage return of CRLF included.
        std::size_t first = 0;
        std::size_t last  = cleaned.size();
        while (first < last and (util::isAsciiSpace(cleaned[first]) or cleaned[first] == '\0'))
            ++first;
        while (last > first and (util::isAsciiSpace(cleaned[last - 1]) or cleaned[last - 1] == '\0'))
            --last;
        if (last > first) lines.emplace_back(cleaned.substr(first, last - first));
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }
    return lines;
}

/// @brief Split @p text at its first @p limit - 1 commas; the last piece keeps the commas.
std::vector<std::string_view> commaFields(std::string_view const text, std::size_t const limit)
{
    std::vector<std::string_view> pieces;
    std::size_t                   start = 0;
    while (pieces.size() + 1 < limit) {
        auto const comma = text.find(',', start);
        if (comma == std::string_view::npos) break;
        pieces.push_back(text.substr(start, comma - start));
        start = comma + 1;
    }
    pieces.push_back(text.substr(start));
    return pieces;
}

bool parseBlock(Reader& reader, std::uint64_t const blockSize, std::int64_t const clusterTs, std::int64_t const durationMs, ScanState& state) // NOLINT(bugprone-easily-swappable-parameters): parseBlock takes its 3 integer arguments in a fixed positional order; the same-typed group cannot be reordered without changing the call's meaning
{
    auto const   blockStart = reader.position();
    std::uint8_t first      = 0;
    if (not reader.readByte(&first)) return false;
    auto const numberLength = sizeLengthOf(first);
    if (numberLength == 0) return false;
    std::uint64_t trackNumber = first & (0xFFu >> numberLength);
    for (int k = 1; k < numberLength; ++k) {
        std::uint8_t byte = 0;
        if (not reader.readByte(&byte)) return false;
        trackNumber = (trackNumber << 8) | byte;
    }
    std::uint8_t timeHigh = 0;
    std::uint8_t timeLow  = 0;
    std::uint8_t flags    = 0;
    if (not reader.readByte(&timeHigh) or not reader.readByte(&timeLow) or not reader.readByte(&flags))
        return false;
    auto const consumed = reader.position() - blockStart;
    if (consumed > blockSize) return false;
    auto const payloadBytes = blockSize - consumed;

    auto const found = state.textTracksByNumber.find(trackNumber);
    if (found == state.textTracksByNumber.end()) return reader.skip(payloadBytes);
    if ((flags & 0x06) != 0) { // laced blocks carry several frames; a subtitle block never does
        ++state.skippedLaced;
        return reader.skip(payloadBytes);
    }
    if (payloadBytes == 0 or payloadBytes > kMaxPayloadBytes) return reader.skip(payloadBytes);

    std::string payload(static_cast<std::size_t>(payloadBytes), '\0');
    if (not reader.readBytes(payload.data(), payload.size())) return false;

    auto const relative = static_cast<std::int16_t>(static_cast<std::uint16_t>((timeHigh << 8) | timeLow));
    auto const startMs =
        (static_cast<std::int64_t>(clusterTs) + relative) * state.timestampScaleNs / 1000000;

    auto&                     track = state.tracks[found->second];
    std::optional<VisibleCue> visible;
    if (track.codec == "S_TEXT/UTF8" or track.codec == "S_TEXT/WEBVTT") {
        VisibleCue cue;
        cue.lines = blockLines(payload, track.codec == "S_TEXT/WEBVTT");
        if (not cue.lines.empty()) visible = std::move(cue);
    } else {
        // The ASS/SSA block text: ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text.
        auto const fields = commaFields(payload, 9);
        if (fields.size() == 9) visible = assVisibleText(fields.back());
    }
    if (not visible) return true;
    Cue cue;
    cue.startMs = startMs;
    cue.endMs   = durationMs > 0 ? startMs + durationMs : 0;
    cue.anchor  = visible->anchor;
    cue.lines   = std::move(visible->lines);
    track.cues.push_back(std::move(cue));
    return true;
}

bool parseBlockGroup(Reader& reader, Reader::Element const& element, std::int64_t const clusterTs, ScanState& state)
{
    std::uint64_t end = 0;
    if (not endOf(reader, element.size, &end)) return false;
    std::uint64_t blockAt     = kUnknownSize;
    std::uint64_t blockSize   = 0;
    std::uint64_t durationRaw = 0;
    while (reader.position() < end) {
        auto const child = reader.header();
        if (not child.valid or child.size == kUnknownSize) return false;
        if (child.id == kBlock) {
            blockAt   = reader.position();
            blockSize = child.size;
            if (not reader.skip(child.size)) return false;
        } else if (child.id == kBlockDuration) {
            durationRaw = reader.readUnsigned(child.size);
        } else if (not reader.skip(child.size)) {
            return false;
        }
    }
    if (blockAt == kUnknownSize) return true;
    if (not reader.seekTo(blockAt)) return false;
    auto const durationMs = durationRaw <= 100000000
                                ? static_cast<std::int64_t>(durationRaw) * state.timestampScaleNs / 1000000
                                : 0; // an implausible duration is treated as absent
    if (not parseBlock(reader, blockSize, clusterTs, durationMs, state)) return false;
    return reader.seekTo(end);
}

bool parseCluster(Reader& reader, Reader::Element const& element, ScanState& state)
{
    std::uint64_t end = 0;
    if (not endOf(reader, element.size, &end)) return false;
    std::int64_t clusterTs = 0;
    while (reader.position() < end) {
        auto const child = reader.header();
        if (not child.valid or child.size == kUnknownSize) return false;
        if (child.id == kClusterTimestamp) {
            clusterTs = static_cast<std::int64_t>(reader.readUnsigned(child.size));
        } else if (child.id == kSimpleBlock) {
            if (not parseBlock(reader, child.size, clusterTs, 0, state)) return false;
        } else if (child.id == kBlockGroup) {
            if (not parseBlockGroup(reader, child, clusterTs, state)) return false;
        } else if (not reader.skip(child.size)) {
            return false;
        }
    }
    return true;
}

bool parseSegmentChild(Reader& reader, Reader::Element const& element, ScanState& state)
{
    switch (element.id) {
        case kInfo: return parseInfo(reader, element, state);
        case kTracks: return parseTracks(reader, element, state);
        case kCluster: return parseCluster(reader, element, state);
        default: return skipElement(reader, element);
    }
}

/// @brief Give every cue a span end: the block's own duration when it said one, else the next
///        cue's start, the last cue a short tail.
void fillMissingEnds(std::vector<TextTrack>& tracks)
{
    for (auto& track : tracks) {
        auto& cues = track.cues;
        std::stable_sort(cues.begin(), cues.end(), [](Cue const& a, Cue const& b) { return a.startMs < b.startMs; });
        for (std::size_t i = 0; i < cues.size(); ++i) {
            if (cues[i].endMs > cues[i].startMs) continue;
            auto const nextStart = i + 1 < cues.size() ? cues[i + 1].startMs : cues[i].startMs + kLastCueTailMs;
            cues[i].endMs        = std::max(cues[i].startMs + 1, nextStart);
        }
    }
}

}

std::vector<TextTrack> readMkvSubtitleTracks(std::filesystem::path const& path)
{
    std::ifstream in(path, std::ios::binary);
    if (not in) {
        LENS_WARN("subtitles: cannot open the video to read its tracks");
        return {};
    }
    Reader    reader{in};
    ScanState state;

    std::uint64_t segmentEnd    = 0;
    bool          insideSegment = false;
    while (reader.position() < reader.size()) {
        if (insideSegment) {
            if (reader.position() >= segmentEnd) {
                insideSegment = false;
                continue;
            }
            auto const child = reader.header();
            if (not child.valid or not parseSegmentChild(reader, child, state)) break;
            continue;
        }
        auto const element = reader.header();
        if (not element.valid) break;
        if (element.id != kSegment) {
            if (not skipElement(reader, element)) break;
            continue;
        }
        if (element.size == kUnknownSize) {
            segmentEnd = reader.size();
        } else {
            if (element.size > reader.size() - reader.position()) break;
            segmentEnd = reader.position() + element.size;
        }
        insideSegment = true;
    }

    fillMissingEnds(state.tracks);
    if (state.skippedCompressed > 0)
        LENS_WARN("subtitles: {} compressed text track(s) skipped", state.skippedCompressed);
    if (state.skippedLaced > 0)
        LENS_WARN("subtitles: {} laced subtitle block(s) skipped", state.skippedLaced);
    return std::move(state.tracks);
}

}
