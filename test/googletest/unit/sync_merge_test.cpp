/**
 * @file sync_merge_test.cpp
 * @brief sync::merge, offline: which side wins each field when this device and the cloud disagree.
 *
 * Each case builds its documents inline and reads the merged one back. There is no file and no
 * network here: the merge is a function of its inputs, so every expectation is written out.
 */

#include <cstdio>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/sync_merge.h"

namespace {

using lens::core::sync::kFormat;
using lens::core::sync::kMaxHistory;
using lens::core::sync::merge;
using nlohmann::json;

const std::string kThisDevice  = "this-device";
const std::string kOtherDevice = "other-device";

/// @return A history row for @p index: a unique word, and a minute that sorts in index order.
json historyRow(int index)
{
    char minute[32];
    std::snprintf(minute, sizeof minute, "2026-10-%02d %02d:%02d", 1 + index / 1440, index / 60 % 24, index % 60);
    return json{{"word", "w" + std::to_string(index)}, {"minute", minute}, {"verdict", "new"}};
}

} // namespace

TEST(SyncMerge, NewerMarkBeatsOlderMark)
{
    const json local  = json{{"known", json{{"panel", json{{"learned", true}, {"at", 100}}}}}};
    const json remote = json{{"known", json{{"panel", json{{"learned", false}, {"at", 200}}}}}};

    const json merged = merge(local, remote, kThisDevice);
    EXPECT_EQ(merged["known"]["panel"], (json{{"learned", false}, {"at", 200}}));

    const json swapped = merge(remote, local, kThisDevice);
    EXPECT_EQ(swapped["known"]["panel"], (json{{"learned", false}, {"at", 200}}));
}

TEST(SyncMerge, NewerTombstoneBeatsOlderMark)
{
    const json local  = json{{"known", json{{"panel", json{{"learned", true}, {"at", 100}}}}}};
    const json remote = json{{"deleted", json{{"panel", 300}}}};

    const json merged = merge(local, remote, kThisDevice);
    EXPECT_FALSE(merged["known"].contains("panel"));
    EXPECT_EQ(merged["deleted"]["panel"], 300);
}

TEST(SyncMerge, MarkingAfterADeleteRestoresTheWord)
{
    const json local  = json{{"deleted", json{{"panel", 300}}}};
    const json remote = json{{"known", json{{"panel", json{{"learned", true}, {"at", 400}}}}}};

    const json merged = merge(local, remote, kThisDevice);
    EXPECT_EQ(merged["known"]["panel"], (json{{"learned", true}, {"at", 400}}));
    EXPECT_FALSE(merged["deleted"].contains("panel"));
}

TEST(SyncMerge, TieKeepsTheLocalMarkAndTheTombstoneWins)
{
    const json mark  = json{{"known", json{{"panel", json{{"learned", true}, {"at", 100}}}}}};
    const json other = json{{"known", json{{"panel", json{{"learned", false}, {"at", 100}}}}}};
    EXPECT_EQ(merge(mark, other, kThisDevice)["known"]["panel"], (json{{"learned", true}, {"at", 100}}));

    const json tomb   = json{{"deleted", json{{"panel", 100}}}};
    const json merged = merge(mark, tomb, kThisDevice);
    EXPECT_FALSE(merged["known"].contains("panel"));
    EXPECT_EQ(merged["deleted"]["panel"], 100);
}

TEST(SyncMerge, MissingTimestampCountsAsZero)
{
    const json unstamped = json{{"known", json{{"panel", json{{"learned", true}}}}}};
    const json stamped   = json{{"known", json{{"panel", json{{"learned", false}, {"at", 1}}}}}};

    EXPECT_EQ(merge(unstamped, stamped, kThisDevice)["known"]["panel"], (json{{"learned", false}, {"at", 1}}));
}

TEST(SyncMerge, HistoryUnionDedupesAndSortsNewestFirst)
{
    const json local  = json{{"history", json::array({json{{"word", "alpha"}, {"minute", "2026-10-14 10:00"}, {"verdict", "new"}}, json{{"word", "beta"}, {"minute", "2026-10-14 09:00"}, {"verdict", "known"}}})}};
    const json remote = json{{"history", json::array({json{{"word", "alpha"}, {"minute", "2026-10-14 10:00"}, {"verdict", "known"}}, json{{"word", "gamma"}, {"minute", "2026-10-14 11:00"}, {"verdict", "new"}}})}};

    const json merged = merge(local, remote, kThisDevice);
    ASSERT_EQ(merged["history"].size(), 3u);
    EXPECT_EQ(merged["history"][0]["word"], "gamma");
    EXPECT_EQ(merged["history"][1]["word"], "alpha");
    EXPECT_EQ(merged["history"][1]["verdict"], "new");
    EXPECT_EQ(merged["history"][2]["word"], "beta");
}

TEST(SyncMerge, HistoryTruncatesToTheNewestRows)
{
    // Local holds 0..1500 and remote 1000..2000, so the union is 2001 distinct rows: the overlap
    // must not count twice, and exactly the oldest row (w0) is the one dropped.
    json local  = json::array();
    json remote = json::array();
    for (int i = 0; i <= 1500; ++i)
        local.push_back(historyRow(i));
    for (int i = 1000; i <= 2000; ++i)
        remote.push_back(historyRow(i));

    const json merged = merge(json{{"history", local}}, json{{"history", remote}}, kThisDevice);
    ASSERT_EQ(merged["history"].size(), kMaxHistory);
    EXPECT_EQ(merged["history"][0]["word"], "w2000");
    EXPECT_EQ(merged["history"].back()["word"], "w1");
}

TEST(SyncMerge, LocalBucketForThisDeviceWinsAndRemoteCopyIsIgnored)
{
    const json local  = json{{"daily", json{{kThisDevice, json{{"2026-10-14", json{{"pops", 3}}}}}}}};
    const json remote = json{{"daily", json{{kThisDevice, json{{"2026-10-14", json{{"pops", 99}}}, {"2026-10-13", json{{"pops", 50}}}}}, {kOtherDevice, json{{"2026-10-13", json{{"pops", 7}}}}}}}};

    const json merged = merge(local, remote, kThisDevice);
    EXPECT_EQ(merged["daily"][kThisDevice], (json{{"2026-10-14", json{{"pops", 3}}}}));
    EXPECT_EQ(merged["daily"][kOtherDevice], (json{{"2026-10-13", json{{"pops", 7}}}}));
}

TEST(SyncMerge, OtherDeviceBucketsComeWholeFromRemoteAndFallBackToLocal)
{
    const json local  = json{{"daily", json{{kOtherDevice, json{{"2026-10-12", json{{"pops", 1}}}}}, {"third-device", json{{"2026-10-11", json{{"pops", 4}}}}}}}};
    const json remote = json{{"daily", json{{kOtherDevice, json{{"2026-10-13", json{{"pops", 7}}}}}}}};

    const json merged = merge(local, remote, kThisDevice);
    // Copied whole, not summed: the stale local day for other-device is not carried over.
    EXPECT_EQ(merged["daily"][kOtherDevice], (json{{"2026-10-13", json{{"pops", 7}}}}));
    // Remote has no bucket for third-device, so the local one stays.
    EXPECT_EQ(merged["daily"]["third-device"], (json{{"2026-10-11", json{{"pops", 4}}}}));
}

TEST(SyncMerge, NewerSettingWinsAndTieKeepsLocal)
{
    const json local  = json{{"settings", json{{"theme", json{{"value", "dark"}, {"at", 10}}}, {"level", json{{"value", 2}, {"at", 5}}}}}};
    const json remote = json{{"settings", json{{"theme", json{{"value", "light"}, {"at", 20}}}, {"level", json{{"value", 4}, {"at", 5}}}}}};

    const json merged = merge(local, remote, kThisDevice);
    EXPECT_EQ(merged["settings"]["theme"], (json{{"value", "light"}, {"at", 20}}));
    EXPECT_EQ(merged["settings"]["level"], (json{{"value", 2}, {"at", 5}}));
}

TEST(SyncMerge, MissingFieldsAndGarbageDoNotThrow)
{
    json merged;
    EXPECT_NO_THROW(merged = merge(json(), json(), kThisDevice));
    EXPECT_EQ(merged["format"], kFormat);
    EXPECT_TRUE(merged["known"].empty());
    EXPECT_TRUE(merged["deleted"].empty());
    EXPECT_TRUE(merged["history"].empty());
    EXPECT_TRUE(merged["daily"].empty());
    EXPECT_TRUE(merged["settings"].empty());

    const json junk    = json{{"known", json::array()},
                              {"deleted", "text"},
                              {"history", json::array({json{{"minute", "2026-10-01 00:00"}}})},
                              {"daily", json{{kThisDevice, 3}}},
                              {"settings", json{{"x", json{{"at", 1}}}}}};
    const json rubbish = json{{"known", json{{"lemma", json{{"at", 5}}}}},
                              {"deleted", json{{"lemma", "nope"}}}};
    EXPECT_NO_THROW(merged = merge(junk, rubbish, kThisDevice));
    EXPECT_TRUE(merged["known"].empty());
    EXPECT_TRUE(merged["deleted"].empty());
    EXPECT_TRUE(merged["history"].empty());
    EXPECT_TRUE(merged["settings"].empty());

    EXPECT_NO_THROW(merge(json("text"), json(42), kThisDevice));
}

TEST(SyncMerge, MergeIsIdempotentAndSymmetricWithoutTies)
{
    // No field here is tied across the two sides, so the order of the arguments must not matter.
    // daily is left out of the symmetry check: its local bucket is the one for the local side's
    // own device, so swapping the sides swaps which bucket is kept.
    const json local = json{
        {"known", json{{"panel", json{{"learned", true}, {"at", 100}}}, {"beta", json{{"learned", true}, {"at", 80}}}}},
        {"history", json::array({json{{"word", "panel"}, {"minute", "2026-10-14 10:00"}, {"verdict", "known"}}})},
        {"daily", json{{kThisDevice, json{{"2026-10-14", json{{"pops", 2}}}}}}},
        {"settings", json{{"theme", json{{"value", "dark"}, {"at", 10}}}}}};
    const json remote = json{
        {"known", json{{"alpha", json{{"learned", false}, {"at", 200}}}}},
        {"deleted", json{{"beta", 90}}},
        {"history", json::array({json{{"word", "alpha"}, {"minute", "2026-10-14 11:00"}, {"verdict", "new"}}})},
        {"daily", json{{kOtherDevice, json{{"2026-10-13", json{{"pops", 5}}}}}}},
        {"settings", json{{"level", json{{"value", 3}, {"at", 20}}}}}};

    const json merged = merge(local, remote, kThisDevice);
    EXPECT_EQ(merged["known"], (json{{"panel", json{{"learned", true}, {"at", 100}}}, {"alpha", json{{"learned", false}, {"at", 200}}}}));
    EXPECT_EQ(merged["deleted"], (json{{"beta", 90}}));
    EXPECT_EQ(merged["history"].size(), 2u);

    EXPECT_EQ(merge(local, merged, kThisDevice), merged);

    const json swapped = merge(remote, local, kThisDevice);
    EXPECT_EQ(swapped["known"], merged["known"]);
    EXPECT_EQ(swapped["deleted"], merged["deleted"]);
    EXPECT_EQ(swapped["history"], merged["history"]);
    EXPECT_EQ(swapped["settings"], merged["settings"]);
}
