/** @file subtitle_source_test.cpp
 * @brief The source choice: which embedded track wins the scoring rule, which external file a
 *        directory offers, and what loadSubtitlesFor answers end to end.
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "core/subtitle/subtitle.h"
#include "core/subtitle/subtitle_source.h"

namespace {

using lens::core::subtitle::Anchor;
using lens::core::subtitle::chooseExternalSubtitle;
using lens::core::subtitle::chooseTextTrack;
using lens::core::subtitle::loadSubtitlesFor;
using lens::core::subtitle::TextTrack;

TextTrack makeTrack(int const number, std::string language, std::string name, std::vector<std::string> const& sample)
{
    TextTrack    track{.number = number, .codec = "S_TEXT/UTF8", .language = std::move(language), .name = std::move(name)};
    std::int64_t start = 0;
    for (auto const& line : sample) {
        track.cues.push_back({.startMs = start, .endMs = start + 1000, .anchor = Anchor::Bottom, .lines = {line}});
        start += 2000;
    }
    return track;
}

class TempDir {
public:
    TempDir()
    {
        static std::atomic<int> counter{0};
        auto const              stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_                         = std::filesystem::temp_directory_path() /
                ("lens-subtitle-source-" + std::to_string(stamp) + "-" + std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }
    ~TempDir()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    std::filesystem::path const& path() const
    {
        return path_;
    }

    void write(std::string const& name, std::string const& bytes) const
    {
        std::ofstream out{path_ / name, std::ios::binary};
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

private:
    std::filesystem::path path_;
};

TEST(SubtitleChoice, PrefersPlainEnglishOverSdhAndBilingual)
{
    std::vector<TextTrack> tracks{
        makeTrack(1, "eng", "chs.eng", {"你好世界 Hello world", "这是一句中文字幕 with English"}),
        makeTrack(2, "eng", "", {"Yeah, that sucks, dude."}),
        makeTrack(3, "eng", "English SDH", {"[ door slams ]"}),
        makeTrack(4, "eng", "", {"Forced sign"}),
    };
    tracks[3].forced = true;
    EXPECT_EQ(chooseTextTrack(tracks), 1);
}

TEST(SubtitleChoice, RefusesAChineseTrackAndKeepsTheOnlyText)
{
    std::vector<TextTrack> const chinese{makeTrack(1, "chi", "", {"这是一个中文字幕测试。"})};
    EXPECT_EQ(chooseTextTrack(chinese), -1);
    std::vector<TextTrack> const single{makeTrack(1, "jpn", "", {"Hello in latin letters"})};
    EXPECT_EQ(chooseTextTrack(single), 0);
}

TEST(SubtitleChoice, EnOutranksTheBareStemAndOtherSuffixes)
{
    TempDir const dir;
    dir.write("South.Park.S01E12.mkv", "");
    dir.write("South.Park.S01E12.srt", "1\n00:00:01,000 --> 00:00:02,000\nbare\n\n");
    dir.write("South.Park.S01E12.en.srt", "1\n00:00:01,000 --> 00:00:02,000\nenglish\n\n");
    dir.write("South.Park.S01E12.zh.srt", "1\n00:00:01,000 --> 00:00:02,000\nchinese\n\n");
    auto const chosen = chooseExternalSubtitle(dir.path() / "South.Park.S01E12.mkv");
    EXPECT_EQ(chosen.filename().string(), "South.Park.S01E12.en.srt");
}

TEST(SubtitleSource, TheExternalFileWinsOverTheEmbeddedTracks)
{
    TempDir const dir;
    dir.write("episode.mkv", ""); // unreadable as Matroska: the external file has to carry the load
    dir.write("episode.en.vtt", "WEBVTT\n\n00:01.000 --> 00:02.000\nFrom the file\n");
    auto const load = loadSubtitlesFor(dir.path() / "episode.mkv");
    ASSERT_FALSE(load.cues.empty());
    EXPECT_EQ(load.cues[0].lines[0], "From the file");
    EXPECT_EQ(load.codec, "vtt");
}

TEST(SubtitleSource, ExplainsWhyNothingWasFound)
{
    TempDir const dir;
    dir.write("episode.mkv", "definitely not matroska");
    auto const load = loadSubtitlesFor(dir.path() / "episode.mkv");
    EXPECT_TRUE(load.cues.empty());
    EXPECT_FALSE(load.note.empty());
}

}
