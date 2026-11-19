/** @file subtitle_mkv_test.cpp
 * @brief The Matroska subtitle extraction, driven by files the test writes itself: a minimal EBML
 *        writer lays down track entries and clusters, and the reader is asked what it finds.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "core/subtitle/subtitle.h"
#include "core/subtitle/subtitle_mkv.h"

namespace {

using lens::core::subtitle::Anchor;
using lens::core::subtitle::readMkvSubtitleTracks;
using lens::core::subtitle::TextTrack;

void putId(std::string& out, std::uint32_t const id)
{
    int const bytes = id <= 0xFF ? 1 : id <= 0xFFFF ? 2
                                   : id <= 0xFFFFFF ? 3
                                                    : 4;
    for (int k = bytes - 1; k >= 0; --k)
        out += static_cast<char>((id >> (8 * k)) & 0xFF);
}

int vintLength(std::uint64_t const value)
{
    int length = 1;
    while (length < 8 and value >= (std::uint64_t{1} << (7 * length)) - 1)
        ++length;
    return length;
}

void putVint(std::string& out, std::uint64_t const value)
{
    auto const  length = vintLength(value);
    std::string bytes(static_cast<std::size_t>(length), '\0');
    for (int k = 0; k < length; ++k)
        bytes[static_cast<std::size_t>(k)] = static_cast<char>((value >> (8 * (length - 1 - k))) & 0xFF);
    bytes[0] = static_cast<char>(static_cast<unsigned char>(bytes[0]) |
                                 (0x80 >> (length > 0 ? length - 1 : 0)));
    out += bytes;
}

std::string element(std::uint32_t const id, std::string_view const payload)
{
    std::string out;
    putId(out, id);
    putVint(out, payload.size());
    out += payload;
    return out;
}

std::string uintElement(std::uint32_t const id, std::uint64_t const value) // NOLINT(bugprone-easily-swappable-parameters): uintElement takes its 2 integer arguments in a fixed positional order; the same-typed group cannot be reordered without changing the call's meaning
{
    int bytes = 1;
    while (bytes < 8 and (value >> (8 * bytes)) != 0)
        ++bytes;
    std::string payload(static_cast<std::size_t>(bytes), '\0');
    for (int k = bytes - 1; k >= 0; --k)
        payload[static_cast<std::size_t>(bytes - 1 - k)] = static_cast<char>((value >> (8 * k)) & 0xFF);
    return element(id, payload);
}

std::string stringElement(std::uint32_t const id, std::string_view const value)
{
    return element(id, value);
}

/// @brief One block: its track number as a vint, the relative timestamp, the flags byte and the
///        frame bytes. @p laced sets the Xiph lacing bits the reader must skip on.
std::string blockPayload(std::uint64_t const trackNumber, std::string_view const text, bool const laced = false)
{
    std::string out;
    putVint(out, trackNumber);
    out += '\0';
    out += '\0'; // relative timestamp 0
    out += static_cast<char>(laced ? 0x86 : 0x80);
    if (laced) out += '\0'; // a lacing count the reader never reaches
    out += text;
    return out;
}

std::string trackEntry(std::uint64_t const number, std::uint64_t const type, std::string_view const codec, std::string_view const language = {}, std::string_view const name = {}, bool const isDefault = false, bool const forced = false, bool const hearingImpaired = false, bool const compressed = false) // NOLINT(bugprone-easily-swappable-parameters): trackEntry builds one MKV element field by field in a fixed positional order; the same-typed group cannot be reordered without changing the fixture's meaning
{
    std::string payload;
    payload += uintElement(0xD7, number);
    payload += uintElement(0x83, type);
    payload += stringElement(0x86, codec);
    if (not language.empty()) payload += stringElement(0x22B59C, language);
    if (not name.empty()) payload += stringElement(0x536E, name);
    if (isDefault) payload += uintElement(0x88, 1);
    if (forced) payload += uintElement(0x55AA, 1);
    if (hearingImpaired) payload += uintElement(0x55AB, 1);
    if (compressed) payload += element(0x6D80, {});
    return element(0xAE, payload);
}

std::string cluster(std::uint64_t const timestamp, std::string const& children)
{
    return element(0x1F43B675, uintElement(0xE7, timestamp) + children);
}

std::string blockGroup(std::uint64_t const trackNumber, std::string_view const text, std::uint64_t const durationTicks)
{
    return element(0xA0, element(0xA1, blockPayload(trackNumber, text)) + uintElement(0x9B, durationTicks));
}

std::string sampleMkv()
{
    std::string tracks;
    tracks += trackEntry(1, 2, "A_AAC", "eng");                                         // audio: never read
    tracks += trackEntry(2, 0x11, "S_TEXT/UTF8", "eng", "", true);                      // the one
    tracks += trackEntry(3, 0x11, "S_TEXT/UTF8", "eng", "SDH");                         // caption track
    tracks += trackEntry(4, 0x11, "S_HDMV/PGS", "eng");                                 // bitmap: skipped
    tracks += trackEntry(5, 0x11, "S_TEXT/UTF8", "eng", "", false, false, false, true); // compressed: skipped
    tracks += trackEntry(6, 0x11, "S_TEXT/ASS", "eng");                                 // ASS text

    std::string clusters;
    clusters += cluster(0, blockGroup(2, "Hello world\nSecond line", 2000));
    clusters += cluster(10000, element(0xA3, blockPayload(2, "The next cue")) + element(0xA3, blockPayload(3, "SDH says hi")) + element(0xA3, blockPayload(1, "audio bytes")));
    clusters += cluster(15000, element(0xA3, blockPayload(2, "laced", true)));
    clusters += cluster(20000, blockGroup(6, "0,0,Default,,0,0,0,,{\\an8}Top line", 1500));

    std::string const info    = element(0x1549A966, uintElement(0x2AD7B1, 1000000));
    std::string const segment = element(0x18538067, info + element(0x1654AE6B, tracks) + clusters);
    return element(0x1A45DFA3, stringElement(0x4282, "matroska")) + segment;
}

class TempFile {
public:
    explicit TempFile(std::string_view const bytes)
    {
        static std::atomic<int> counter{0};
        auto const              stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_                         = std::filesystem::temp_directory_path() /
                ("lens-subtitle-" + std::to_string(stamp) + "-" + std::to_string(counter++) + ".mkv");
        std::ofstream out{path_, std::ios::binary};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    ~TempFile()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }
    std::filesystem::path const& path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

TEST(SubtitleMkv, ReadsTheTextTracksAndSkipsTheRest)
{
    TempFile const               file{sampleMkv()};
    std::vector<TextTrack> const tracks = readMkvSubtitleTracks(file.path());
    ASSERT_EQ(tracks.size(), 3u);

    EXPECT_EQ(tracks[0].number, 2);
    EXPECT_EQ(tracks[0].language, "eng");
    EXPECT_TRUE(tracks[0].isDefault);
    ASSERT_EQ(tracks[0].cues.size(), 2u);
    EXPECT_EQ(tracks[0].cues[0].startMs, 0);
    EXPECT_EQ(tracks[0].cues[0].endMs, 2000);
    ASSERT_EQ(tracks[0].cues[0].lines.size(), 2u);
    EXPECT_EQ(tracks[0].cues[0].lines[0], "Hello world");
    EXPECT_EQ(tracks[0].cues[0].lines[1], "Second line");
    EXPECT_EQ(tracks[0].cues[1].startMs, 10000);
    EXPECT_EQ(tracks[0].cues[1].endMs, 13000); // no duration of its own: a short tail

    EXPECT_EQ(tracks[1].number, 3);
    EXPECT_EQ(tracks[1].name, "SDH");
    ASSERT_EQ(tracks[1].cues.size(), 1u);
    EXPECT_EQ(tracks[1].cues[0].lines[0], "SDH says hi");

    EXPECT_EQ(tracks[2].number, 6);
    EXPECT_EQ(tracks[2].codec, "S_TEXT/ASS");
    ASSERT_EQ(tracks[2].cues.size(), 1u);
    EXPECT_EQ(tracks[2].cues[0].startMs, 20000);
    EXPECT_EQ(tracks[2].cues[0].endMs, 21500);
    EXPECT_EQ(tracks[2].cues[0].anchor, Anchor::Top);
    EXPECT_EQ(tracks[2].cues[0].lines[0], "Top line");
}

TEST(SubtitleMkv, AnswersNothingForAFileThatIsNotMatroska)
{
    TempFile const file{"this is not an EBML file at all"};
    EXPECT_TRUE(readMkvSubtitleTracks(file.path()).empty());
}

}
