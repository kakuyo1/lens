/** @file subtitle_parse.cpp
 * @brief The SubRip, WebVTT and SubStation readers, for subtitle_parse.h.
 */

#include "core/subtitle/subtitle_parse.h"

#include "util/text.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace lens::core::subtitle {
namespace {

constexpr std::uintmax_t kMaxSubtitleBytes = 64u * 1024 * 1024;

std::string_view trim(std::string_view const text)
{
    std::size_t first = 0;
    std::size_t last  = text.size();
    while (first < last and (util::isAsciiSpace(text[first]) or text[first] == '\0'))
        ++first;
    while (last > first and (util::isAsciiSpace(text[last - 1]) or text[last - 1] == '\0'))
        --last;
    return text.substr(first, last - first);
}

std::vector<std::string_view> splitLines(std::string_view const text)
{
    std::vector<std::string_view> lines;
    std::size_t                   start = 0;
    while (start <= text.size()) {
        auto const newline = text.find('\n', start);
        auto const end     = newline == std::string_view::npos ? text.size() : newline;
        lines.push_back(trim(text.substr(start, end - start)));
        if (newline == std::string_view::npos) break;
        start = newline + 1;
    }
    return lines;
}

bool startsWithIgnoreCase(std::string_view const text, std::string_view const prefix)
{
    if (text.size() < prefix.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i)
        if (util::asciiLower(text[i]) != util::asciiLower(prefix[i])) return false;
    return true;
}

std::string foldAscii(std::string_view const text)
{
    std::string folded{text};
    for (auto& c : folded)
        c = util::asciiLower(c);
    return folded;
}

void appendUtf8(std::string& out, char32_t const code)
{
    if (code <= 0x7F) {
        out += static_cast<char>(code);
    } else if (code <= 0x7FF) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

std::string decodeEntities(std::string_view const in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        if (in[i] == '&') {
            auto const semi = in.find(';', i + 1);
            if (semi != std::string_view::npos and semi - i <= 10) {
                auto const entity = in.substr(i + 1, semi - i - 1);
                if (entity == "amp") {
                    out += '&';
                    i = semi + 1;
                    continue;
                }
                if (entity == "lt") {
                    out += '<';
                    i = semi + 1;
                    continue;
                }
                if (entity == "gt") {
                    out += '>';
                    i = semi + 1;
                    continue;
                }
                if (entity == "quot") {
                    out += '"';
                    i = semi + 1;
                    continue;
                }
                if (entity == "apos") {
                    out += '\'';
                    i = semi + 1;
                    continue;
                }
                if (entity == "nbsp") {
                    out += ' ';
                    i = semi + 1;
                    continue;
                }
                if (not entity.empty() and entity.front() == '#') {
                    auto const digits = entity.substr(1);
                    bool const hex    = not digits.empty() and (digits.front() == 'x' or digits.front() == 'X');
                    char32_t   code   = 0;
                    bool       good   = not digits.empty() and (not hex or digits.size() > 1);
                    for (std::size_t k = hex ? 1 : 0; good and k < digits.size(); ++k) {
                        int const digit = hex ? (util::isAsciiDigit(digits[k])   ? digits[k] - '0'
                                                 : util::isAsciiLower(digits[k]) ? digits[k] - 'a' + 10
                                                 : util::isAsciiUpper(digits[k]) ? digits[k] - 'A' + 10
                                                                                 : -1)
                                              : (util::isAsciiDigit(digits[k]) ? digits[k] - '0' : -1);
                        if (digit < 0)
                            good = false;
                        else
                            code = code * (hex ? 16 : 10) + static_cast<char32_t>(digit);
                    }
                    if (good and code > 0 and code <= 0x10FFFF) {
                        appendUtf8(out, code);
                        i = semi + 1;
                        continue;
                    }
                }
            }
        }
        out += in[i];
        ++i;
    }
    return out;
}

/// @return @p in with every angle-bracket tag removed and the common entities decoded. The angle
///         tags cover the HTML SubRip keeps and the inline timestamps WebVTT allows.
std::string plainText(std::string_view const in)
{
    std::string out;
    out.reserve(in.size());
    bool inTag = false;
    for (char const c : in) {
        if (inTag) {
            if (c == '>') inTag = false;
            continue;
        }
        if (c == '<') {
            inTag = true;
            continue;
        }
        out += c;
    }
    return decodeEntities(out);
}

/// @brief Times the SRT and VTT formats spell: `HH:MM:SS,mmm`, `HH:MM:SS.mmm`, hourless
///        variants, and with the fraction omitted.
bool parseClockTime(std::string_view const in, std::int64_t* const msOut)
{
    std::int64_t parts[3] = {0, 0, 0};
    std::int64_t fraction = 0; // the last field's fraction of a second, in milliseconds
    int          count    = 0;
    std::size_t  start    = 0;
    while (true) {
        if (count == 3) return false; // a fourth field cannot happen
        auto const colon = in.find(':', start);
        auto const end   = colon == std::string_view::npos ? in.size() : colon;
        auto const part  = trim(in.substr(start, end - start));
        if (part.empty()) return false;
        // The last field may carry the fraction.
        auto const   dot   = part.find_first_of(".,");
        std::int64_t value = 0;
        for (std::size_t k = 0; k < (dot == std::string_view::npos ? part.size() : dot); ++k) {
            if (not util::isAsciiDigit(part[k])) return false;
            value = value * 10 + (part[k] - '0');
        }
        fraction = 0;
        if (dot != std::string_view::npos) {
            std::size_t digits = 0;
            for (std::size_t k = dot + 1; k < part.size() and digits < 3; ++k, ++digits) {
                if (not util::isAsciiDigit(part[k])) return false;
                fraction = fraction * 10 + (part[k] - '0');
            }
            while (digits < 3) {
                fraction *= 10;
                ++digits;
            }
        }
        parts[count++] = value;
        if (colon == std::string_view::npos) break;
        start = colon + 1;
    }
    if (count == 2) { // MM:SS
        parts[2] = parts[1];
        parts[1] = parts[0];
        parts[0] = 0;
    } else if (count != 3) {
        return false;
    }
    *msOut = (parts[0] * 3600 + parts[1] * 60 + parts[2]) * 1000 + fraction;
    return true;
}

/// @brief The ASS timestamp: `H:MM:SS.cc`, centisecond resolution.
bool parseAssTime(std::string_view const in, std::int64_t* const msOut)
{
    return parseClockTime(in, msOut);
}

std::vector<std::string> cueLines(std::string const& text)
{
    std::vector<std::string> lines;
    std::size_t              start = 0;
    while (start <= text.size()) {
        auto const newline = text.find('\n', start);
        auto const end     = newline == std::string::npos ? text.size() : newline;
        auto const line    = trim(std::string_view{text}.substr(start, end - start));
        if (not line.empty()) lines.emplace_back(line);
        if (newline == std::string::npos) break;
        start = newline + 1;
    }
    return lines;
}

void pushCue(std::vector<Cue>& cues, std::int64_t start, std::int64_t end, Anchor const anchor, std::vector<std::string> lines)
{
    if (lines.empty()) return;
    if (end <= start) end = start + 1000;
    cues.push_back({.startMs = start, .endMs = end, .anchor = anchor, .lines = std::move(lines)});
}

/// @brief Split @p text at commas, at most @p limit pieces: the last piece keeps the commas.
std::vector<std::string_view> splitCommas(std::string_view const text, std::size_t const limit)
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

/// @brief The vertical anchor the legacy \a command names, or nothing for its invalid values.
std::optional<bool> legacyAnchorTop(unsigned const value)
{
    if (value >= 1 and value <= 3) return false;
    if (value >= 5 and value <= 7) return true;
    if (value >= 9 and value <= 11) return std::nullopt; // middle: not the default band
    return std::nullopt;
}

}

std::optional<VisibleCue> assVisibleText(std::string_view const text)
{
    VisibleCue  cue;
    std::string out;
    bool        skip    = false;
    bool        drawing = false;

    for (std::size_t i = 0; i < text.size();) {
        char const c = text[i];
        if (c == '{') {
            auto const close = text.find('}', i + 1);
            if (close == std::string_view::npos) break; // unterminated override block: drop the rest
            auto const tags = text.substr(i + 1, close - i - 1);
            for (std::size_t k = 0; k < tags.size();) {
                if (tags[k] != '\\') {
                    ++k;
                    continue;
                }
                ++k;
                std::size_t wordEnd = k;
                while (wordEnd < tags.size() and util::isAsciiLetter(tags[wordEnd]))
                    ++wordEnd;
                auto const command = tags.substr(k, wordEnd - k);
                if (command == "pos" or command == "move" or command == "org" or command == "clip" or
                    command == "iclip") {
                    skip = true;
                } else if ((command == "an" or command == "a") and wordEnd < tags.size()) {
                    unsigned    value  = 0;
                    std::size_t digits = wordEnd;
                    while (digits < tags.size() and util::isAsciiDigit(tags[digits]) and digits - wordEnd < 2) {
                        value = value * 10 + static_cast<unsigned>(tags[digits] - '0');
                        ++digits;
                    }
                    if (digits > wordEnd) {
                        if (command == "an") {
                            if (value >= 1 and value <= 3)
                                cue.anchor = Anchor::Bottom;
                            else if (value >= 7 and value <= 9)
                                cue.anchor = Anchor::Top;
                            else
                                skip = true; // middle: not the default band
                        } else if (auto const top = legacyAnchorTop(value)) {
                            cue.anchor = *top ? Anchor::Top : Anchor::Bottom;
                        } else {
                            skip = true;
                        }
                    }
                } else if (command == "p" and wordEnd < tags.size() and util::isAsciiDigit(tags[wordEnd])) {
                    drawing = tags[wordEnd] != '0';
                }
                // Walk to the next command or the end of the block.
                k = wordEnd;
                if (k < tags.size() and tags[k] == '(') {
                    auto const close = tags.find(')', k);
                    k                = close == std::string_view::npos ? tags.size() : close + 1;
                } else {
                    while (k < tags.size() and tags[k] != '\\')
                        ++k;
                }
            }
            i = close + 1;
            continue;
        }
        if (drawing) {
            ++i;
            continue;
        }
        if (c == '\\' and i + 1 < text.size()) {
            char const next = text[i + 1];
            if (next == 'N' or next == 'n') {
                out += '\n';
                i += 2;
                continue;
            }
            if (next == 'h') {
                out += ' ';
                i += 2;
                continue;
            }
            out += next;
            i += 2;
            continue;
        }
        out += c;
        ++i;
    }

    if (skip) return std::nullopt;
    cue.lines = cueLines(out);
    if (cue.lines.empty()) return std::nullopt;
    return cue;
}

std::vector<Cue> parseSrt(std::string_view const text)
{
    std::vector<Cue>                    cues;
    std::vector<std::string_view> const lines = splitLines(text);
    std::size_t                         i     = 0;
    while (i < lines.size()) {
        auto const arrow = lines[i].find("-->");
        if (arrow == std::string_view::npos) {
            ++i;
            continue;
        }
        auto const   left      = trim(lines[i].substr(0, arrow));
        auto const   right     = trim(lines[i].substr(arrow + 3));
        auto const   space     = right.find_first_of(" \t");
        auto const   rightTime = space == std::string_view::npos ? right : right.substr(0, space);
        std::int64_t start     = 0;
        std::int64_t end       = 0;
        if (not parseClockTime(left, &start) or not parseClockTime(rightTime, &end)) {
            ++i;
            continue;
        }
        ++i;
        std::vector<std::string> textLines;
        while (i < lines.size()) {
            if (lines[i].empty()) {
                ++i;
                break;
            }
            if (lines[i].find("-->") != std::string_view::npos) break; // the next cue, unseparated
            auto const raw     = plainText(lines[i]);
            auto const cleaned = trim(raw);
            if (not cleaned.empty()) textLines.emplace_back(cleaned);
            ++i;
        }
        pushCue(cues, start, end, Anchor::Bottom, std::move(textLines));
    }
    return cues;
}

std::vector<Cue> parseAss(std::string_view const text)
{
    std::vector<Cue> cues;
    auto const       lines      = splitLines(text);
    bool             inEvents   = false;
    std::size_t      startField = 1;
    std::size_t      endField   = 2;
    std::size_t      textField  = 9;
    std::size_t      maxField   = 10;
    for (auto const line : lines) {
        if (line.empty()) continue;
        if (line.front() == '[') {
            inEvents = startsWithIgnoreCase(line, "[events]");
            continue;
        }
        if (not inEvents) continue;
        if (startsWithIgnoreCase(line, "format:")) {
            auto const fields = splitCommas(line.substr(7), 64);
            for (std::size_t index = 0; index < fields.size(); ++index) {
                auto const name = foldAscii(trim(fields[index]));
                if (name == "start")
                    startField = index;
                else if (name == "end")
                    endField = index;
                else if (name == "text")
                    textField = index;
            }
            maxField = std::max({startField, endField, textField}) + 1;
            continue;
        }
        if (not startsWithIgnoreCase(line, "dialogue:")) continue;
        auto const fields = splitCommas(line.substr(9), maxField);
        if (fields.size() <= std::max(startField, std::max(endField, textField))) continue;
        std::int64_t start = 0;
        std::int64_t end   = 0;
        if (not parseAssTime(trim(fields[startField]), &start) or not parseAssTime(trim(fields[endField]), &end))
            continue;
        auto const visible = assVisibleText(fields[textField]);
        if (not visible) continue;
        pushCue(cues, start, end, visible->anchor, visible->lines);
    }
    return cues;
}

std::vector<Cue> parseVtt(std::string_view const text)
{
    std::vector<Cue>                    cues;
    std::vector<std::string_view> const lines = splitLines(text);
    std::size_t                         i     = 0;
    while (i < lines.size()) {
        auto const arrow = lines[i].find("-->");
        if (arrow == std::string_view::npos) {
            ++i;
            continue;
        }
        auto const   left      = trim(lines[i].substr(0, arrow));
        auto const   right     = trim(lines[i].substr(arrow + 3));
        auto const   space     = right.find_first_of(" \t");
        auto const   rightTime = space == std::string_view::npos ? right : right.substr(0, space);
        std::int64_t start     = 0;
        std::int64_t end       = 0;
        if (not parseClockTime(left, &start) or not parseClockTime(rightTime, &end)) {
            ++i;
            continue;
        }
        ++i;
        std::vector<std::string> textLines;
        while (i < lines.size()) {
            if (lines[i].empty()) {
                ++i;
                break;
            }
            if (lines[i].find("-->") != std::string_view::npos) break;
            auto const raw     = plainText(lines[i]);
            auto const cleaned = trim(raw);
            if (not cleaned.empty()) textLines.emplace_back(cleaned);
            ++i;
        }
        pushCue(cues, start, end, Anchor::Bottom, std::move(textLines));
    }
    return cues;
}

std::vector<Cue> parseSubtitleFile(std::filesystem::path const& path, std::string* const codec)
{
    auto const ext8      = path.extension().u8string();
    auto       extension = foldAscii(std::string_view{reinterpret_cast<char const*>(ext8.data()), ext8.size()});
    if (not extension.empty() and extension.front() == '.') extension.erase(0, 1);
    if (extension != "srt" and extension != "vtt" and extension != "ass" and extension != "ssa") return {};
    std::error_code sizeError;
    auto const      size = std::filesystem::file_size(path, sizeError);
    if (sizeError or size > kMaxSubtitleBytes) return {};
    std::ifstream in(path, std::ios::binary);
    if (not in) return {};
    std::string data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (data.size() >= 3 and static_cast<unsigned char>(data[0]) == 0xEF and
        static_cast<unsigned char>(data[1]) == 0xBB and static_cast<unsigned char>(data[2]) == 0xBF)
        data.erase(0, 3);
    std::vector<Cue> cues = extension == "srt"   ? parseSrt(data)
                            : extension == "vtt" ? parseVtt(data)
                                                 : parseAss(data);
    std::stable_sort(cues.begin(), cues.end(), [](Cue const& a, Cue const& b) { return a.startMs < b.startMs; });
    if (codec != nullptr) *codec = extension;
    return cues;
}

}
