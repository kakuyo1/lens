/**
 * @file dev_line_framer_test.cpp
 * @brief The developer pipe's line framing (docs/adr/0068): bytes become whole lines, a partial line waits for its break,
 *        and a line past the cap ends the stream.
 */

#include <gtest/gtest.h>

#include "core/dev_line_framer.h"

#include <string>
#include <vector>

namespace {

using lens::core::dev::LineFramer;

TEST(DevLineFramer, CompleteLinesComeBackWithoutTheirBreaks)
{
    LineFramer framer;
    auto const out = framer.feed("version\nstate\n");
    EXPECT_FALSE(out.overflow);
    EXPECT_EQ(out.lines, (std::vector<std::string>{"version", "state"}));
}

TEST(DevLineFramer, APartialLineWaitsForItsBreak)
{
    LineFramer framer;
    EXPECT_TRUE(framer.feed("sta").lines.empty());
    EXPECT_EQ(framer.feed("te\nquit").lines, (std::vector<std::string>{"state"}));
    EXPECT_EQ(framer.feed("\n").lines, (std::vector<std::string>{"quit"}));
}

TEST(DevLineFramer, CarriageReturnBeforeTheBreakIsDropped)
{
    LineFramer framer;
    EXPECT_EQ(framer.feed("state\r\n").lines, (std::vector<std::string>{"state"}));
    // The break itself may arrive in a later chunk than the carriage return.
    EXPECT_TRUE(framer.feed("quit\r").lines.empty());
    EXPECT_EQ(framer.feed("\n").lines, (std::vector<std::string>{"quit"}));
}

TEST(DevLineFramer, ALineAtTheCapIsKept)
{
    LineFramer framer;
    auto const line = std::string(LineFramer::kMaxLineBytes, 'x');
    auto const out  = framer.feed(line + "\n");
    EXPECT_FALSE(out.overflow);
    EXPECT_EQ(out.lines, (std::vector<std::string>{line}));
}

TEST(DevLineFramer, ALineOverTheCapEndsTheStream)
{
    LineFramer framer;
    EXPECT_TRUE(framer.feed(std::string(LineFramer::kMaxLineBytes + 1, 'x')).overflow);
}

TEST(DevLineFramer, TheCapCountsAcrossChunks)
{
    LineFramer framer;
    EXPECT_FALSE(framer.feed(std::string(LineFramer::kMaxLineBytes, 'x')).overflow);
    EXPECT_TRUE(framer.feed("xx").overflow);
}

} // namespace
