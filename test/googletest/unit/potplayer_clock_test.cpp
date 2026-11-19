/** @file potplayer_clock_test.cpp
 * @brief The PotPlayer module's pure half: the title-to-file-name rule, the command-line match and
 *        the playlist entry. The window-message half needs a running player and is checked live.
 */

#include <gtest/gtest.h>

#include <QString>

#include <string>
#include <vector>

#include "app/platform/potplayer_clock.h"

namespace {

using lens::app::mediaArgumentFrom;
using lens::app::mediaPathFromPlaylist;
using lens::app::potplayerFileNameFromTitle;

TEST(PotPlayerTitle, StripsThePlayerSuffix)
{
    EXPECT_EQ(potplayerFileNameFromTitle(QStringLiteral("South.Park.S01E12.1080p.mkv - PotPlayer")),
              QStringLiteral("South.Park.S01E12.1080p.mkv"));
    EXPECT_TRUE(potplayerFileNameFromTitle(QStringLiteral("PotPlayer")).isEmpty());
    EXPECT_EQ(potplayerFileNameFromTitle(QStringLiteral("  foo.mkv  ")), QStringLiteral("foo.mkv"));
}

TEST(PotPlayerArguments, MatchesByFileNameAlone)
{
    std::vector<std::wstring> const arguments{L"B:\\PotPlayer\\PotPlayerMini64.exe",
                                              L"D:\\downloads\\South.Park.S01E12.mkv"};
    auto const                      found = mediaArgumentFrom(arguments, L"south.park.s01e12.MKV");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->wstring(), L"D:\\downloads\\South.Park.S01E12.mkv");
    EXPECT_FALSE(mediaArgumentFrom(arguments, L"another.mkv").has_value());
}

TEST(PotPlayerArguments, NeverMatchesAUrl)
{
    std::vector<std::wstring> const arguments{L"https://example.com/episode.mkv"};
    EXPECT_FALSE(mediaArgumentFrom(arguments, L"episode.mkv").has_value());
}

TEST(PotPlayerPlaylist, ReadsThePlaynameEntry)
{
    std::string const utf8  = "\xEF\xBB\xBF"
                              "DAUMPLAYLIST\r\n"
                              "playname=D:\\downloads\\South.Park.S01E12.mkv\r\n"
                              "topindex=0\r\n";
    auto const        found = mediaPathFromPlaylist(utf8, L"south.park.s01e12.mkv");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->wstring(), L"D:\\downloads\\South.Park.S01E12.mkv");
    EXPECT_FALSE(mediaPathFromPlaylist(utf8, L"other.mkv").has_value());
}

TEST(PotPlayerPlaylist, ReadsUtf16Bytes)
{
    std::wstring const text = L"DAUMPLAYLIST\nplayname=D:\\\u7247\u5B50\\episode.mkv\n";
    std::string        bytes;
    bytes += static_cast<char>(0xFF);
    bytes += static_cast<char>(0xFE);
    bytes.append(reinterpret_cast<char const*>(text.data()), text.size() * sizeof(wchar_t));
    auto const found = mediaPathFromPlaylist(bytes, L"episode.mkv");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->wstring(), L"D:\\\u7247\u5B50\\episode.mkv");
}

}
