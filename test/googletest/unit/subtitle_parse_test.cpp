/** @file subtitle_parse_test.cpp
 * @brief The SubRip, SubStation and WebVTT readers and the cue timeline: what the scan reads out
 *        of a subtitle text, and which cue answers at a playback position.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/subtitle/subtitle.h"
#include "core/subtitle/subtitle_parse.h"

namespace {

using lens::core::subtitle::Anchor;
using lens::core::subtitle::Cue;
using lens::core::subtitle::CueTimeline;
using lens::core::subtitle::parseAss;
using lens::core::subtitle::parseSrt;
using lens::core::subtitle::parseVtt;

TEST(SubtitleSrt, ReadsCuesAndKeepsTheirLineBreaks)
{
    auto const cues = parseSrt("1\r\n"
                               "00:00:05,090 --> 00:00:07,860\r\n"
                               "Yeah, that sucks, dude.\r\n"
                               "\r\n"
                               "2\r\n"
                               "00:00:08,000 --> 00:00:09,500\r\n"
                               "And I hope you feel\r\n"
                               "really good about yourself.\r\n");
    ASSERT_EQ(cues.size(), 2u);
    EXPECT_EQ(cues[0].startMs, 5090);
    EXPECT_EQ(cues[0].endMs, 7860);
    ASSERT_EQ(cues[0].lines.size(), 1u);
    EXPECT_EQ(cues[0].lines[0], "Yeah, that sucks, dude.");
    ASSERT_EQ(cues[1].lines.size(), 2u);
    EXPECT_EQ(cues[1].lines[1], "really good about yourself.");
}

TEST(SubtitleSrt, SkipsBlocksWithoutTimingAndDecodesTags)
{
    auto const cues = parseSrt("not a cue\n"
                               "\n"
                               "00:00:01.000 --> 00:00:02.000\n"
                               "<i>Hello</i> &amp; welcome\n"
                               "\n"
                               "x --> y\n"
                               "broken\n");
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].lines[0], "Hello & welcome");
}

TEST(SubtitleSrt, RepairsAnEndBeforeItsStart)
{
    auto const cues = parseSrt("00:00:05,000 --> 00:00:04,000\nfive\n");
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].endMs, 6000);
}

TEST(SubtitleAss, ReadsDialogueAndStripsOverrides)
{
    auto const cues = parseAss("[Script Info]\n"
                               "Title: x\n"
                               "\n"
                               "[V4+ Styles]\n"
                               "Format: Name\n"
                               "\n"
                               "[Events]\n"
                               "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n"
                               "Dialogue: 0,0:00:05.09,0:00:07.86,Default,,0,0,0,,{\\i1}Yeah{\\i0}, that sucks, dude.\n"
                               "Dialogue: 0,0:00:08.00,0:00:09.00,Default,,0,0,0,,Line one\\NLine two\n");
    ASSERT_EQ(cues.size(), 2u);
    EXPECT_EQ(cues[0].startMs, 5090);
    EXPECT_EQ(cues[0].endMs, 7860);
    EXPECT_EQ(cues[0].lines[0], "Yeah, that sucks, dude.");
    ASSERT_EQ(cues[1].lines.size(), 2u);
    EXPECT_EQ(cues[1].lines[1], "Line two");
}

TEST(SubtitleAss, DropsPositionedCuesAndKeepsTheTopAnchor)
{
    auto const cues = parseAss("[Events]\n"
                               "Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,{\\an8}A sign at the top\n"
                               "Dialogue: 0,0:00:03.00,0:00:04.00,Default,,0,0,0,,{\\pos(100,200)}Moved elsewhere\n"
                               "Dialogue: 0,0:00:05.00,0:00:06.00,Default,,0,0,0,,{\\an5}Middle of the screen\n"
                               "Dialogue: 0,0:00:07.00,0:00:08.00,Default,,0,0,0,,{\\p1}m 0 0 l 100 0{\\p0}real words\n");
    ASSERT_EQ(cues.size(), 2u);
    EXPECT_EQ(cues[0].anchor, Anchor::Top);
    EXPECT_EQ(cues[0].lines[0], "A sign at the top");
    EXPECT_EQ(cues[1].lines[0], "real words");
}

TEST(SubtitleAss, HonoursTheEventsFormatLine)
{
    auto const cues = parseAss("[Events]\n"
                               "Format: Start, End, Style, Layer, Text\n"
                               "Dialogue: 0:00:02.00,0:00:03.00,Default,0,Swapped fields\n");
    ASSERT_EQ(cues.size(), 1u);
    EXPECT_EQ(cues[0].startMs, 2000);
    EXPECT_EQ(cues[0].lines[0], "Swapped fields");
}

TEST(SubtitleVtt, ReadsCuesAndIgnoresSettings)
{
    auto const cues = parseVtt("WEBVTT\n"
                               "\n"
                               "cue-1\n"
                               "00:01.000 --> 00:03.500 align:start position:10%\n"
                               "<c.yellow>Hello</c> there\n"
                               "\n"
                               "01:00:00.000 --> 01:00:02.000\n"
                               "An hour in\n");
    ASSERT_EQ(cues.size(), 2u);
    EXPECT_EQ(cues[0].startMs, 1000);
    EXPECT_EQ(cues[0].endMs, 3500);
    EXPECT_EQ(cues[0].lines[0], "Hello there");
    EXPECT_EQ(cues[1].startMs, 3600000);
}

TEST(SubtitleTimeline, FollowsPlaybackAndAnswersAfterSeek)
{
    std::vector<Cue> const cues{{.startMs = 1000, .endMs = 2000},
                                {.startMs = 3000, .endMs = 5000},
                                {.startMs = 9000, .endMs = 10000}};
    CueTimeline            timeline;
    timeline.reset(&cues);
    EXPECT_EQ(timeline.at(500), nullptr);
    EXPECT_EQ(timeline.at(1500), &cues[0]);
    EXPECT_EQ(timeline.at(2000), nullptr); // half-open: the end is out
    EXPECT_EQ(timeline.at(4000), &cues[1]);
    EXPECT_EQ(timeline.at(8500), nullptr);
    EXPECT_EQ(timeline.at(9500), &cues[2]);
    EXPECT_EQ(timeline.at(1500), &cues[0]); // a seek back re-seats the cursor
    EXPECT_EQ(timeline.at(3000), &cues[1]);
}

}
