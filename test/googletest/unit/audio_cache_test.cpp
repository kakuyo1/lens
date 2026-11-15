/**
 * @file audio_cache_test.cpp
 * @brief The pronunciation cache's files, without a profile: store and load round-trip, the keys
 *        that are refused, the broken files that remove themselves, and the eviction that keeps the
 *        ceiling with the oldest wanted first.
 *
 * The ceiling the cases exercise comes through the constructor's seam (audio_cache.h); planting a
 * fifty-megabyte cache to reach the shipping number would be the slower way to test the same loop.
 */

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <chrono>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "speech/audio_cache.h"

namespace {

using lens::speech::AudioCache;

/// A key as speech_pure's cacheKey() makes them: forty lower-case hex digits. These three are
/// distinct and differ only in the last byte, so a mix-up between them is visible.
const QString kOldest = QStringLiteral("0123456789abcdef0123456789abcdef01234567");
const QString kMiddle = QStringLiteral("0123456789abcdef0123456789abcdef01234568");
const QString kNewest = QStringLiteral("0123456789abcdef0123456789abcdef01234569");

const QByteArray kBytes = QByteArray(400, 'a');

/// @return The path the cache stores @p key under.
std::filesystem::path fileFor(std::filesystem::path const& root, QString const& key)
{
    return root / (key.toStdWString() + L".mp3");
}

/// @brief Plant one file in the cache root, then set its wanted-time back, as if it had sat there.
void plant(std::filesystem::path const& root, QString const& key, QByteArray const& bytes, std::chrono::minutes age)
{
    QFile file{QString::fromStdWString(fileFor(root, key).wstring())};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(bytes), bytes.size());
    file.close();
    std::error_code error;
    std::filesystem::last_write_time(fileFor(root, key), std::filesystem::file_time_type::clock::now() - age, error);
    ASSERT_FALSE(error);
}

} // namespace

TEST(AudioCache, StoreThenLoadReturnsTheBytes)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root = dir.path().toStdWString();
    AudioCache                  cache{root};

    EXPECT_TRUE(cache.store(kOldest, kBytes));
    const auto loaded = cache.load(kOldest);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(*loaded, kBytes);
    // One mp3 file per key, named by the key, under the root the case gave.
    EXPECT_TRUE(std::filesystem::exists(fileFor(root, kOldest)));
    EXPECT_EQ(cache.bytesOnDisk(), static_cast<qint64>(kBytes.size()));
}

TEST(AudioCache, EmptyBytesAndKeysThatAreNotKeysAreRefused)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root = dir.path().toStdWString();
    AudioCache                  cache{root};

    // A key is forty hex digits; anything else did not come from cacheKey() and must not become a
    // file name -- not too short, not too long, not a non-hex character among the hex.
    EXPECT_FALSE(cache.store(QString{}, kBytes));
    EXPECT_FALSE(cache.store(QStringLiteral("0123456789abcdef0123456789abcdef0123456"), kBytes));
    EXPECT_FALSE(cache.store(QStringLiteral("0123456789abcdef0123456789abcdef012345678"), kBytes));
    EXPECT_FALSE(cache.store(QStringLiteral("0123456789abcdef0123456789abcdef0123456g"), kBytes));
    EXPECT_FALSE(cache.store(kOldest, QByteArray{}));
    EXPECT_TRUE(QDir{dir.path()}.entryList(QDir::Files).isEmpty());

    EXPECT_FALSE(cache.load(QString{}).has_value());
    EXPECT_FALSE(cache.load(QStringLiteral("0123456789abcdef0123456789abcdef0123456g")).has_value());
}

TEST(AudioCache, AnAbsentCacheAnswersNothing)
{
    // No store has ever happened: no directory, no entry, no crash.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const AudioCache cache{std::filesystem::path{dir.path().toStdWString()} / L"absent"};

    EXPECT_FALSE(cache.load(kOldest).has_value());
    EXPECT_EQ(cache.bytesOnDisk(), 0);
}

TEST(AudioCache, AFileThatCannotBeReadIsRemovedRatherThanFoundAgain)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root = dir.path().toStdWString();
    AudioCache                  cache{root};

    // Zero bytes is not a recording: whatever wrote it stopped half-way, and the next load removes
    // it instead of answering silence for that word forever.
    plant(root, kOldest, QByteArray{}, std::chrono::minutes{0});
    EXPECT_FALSE(cache.load(kOldest).has_value());
    EXPECT_FALSE(std::filesystem::exists(fileFor(root, kOldest)));
}

TEST(AudioCache, StoreMakesTheDirectoryTreeItSitsIn)
{
    // Nothing exists until the first store; the first store makes every level it needs.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root = std::filesystem::path{dir.path().toStdWString()} / L"deep" / L"audio";
    AudioCache                  cache{root};

    EXPECT_TRUE(cache.store(kOldest, kBytes));
    EXPECT_TRUE(std::filesystem::exists(fileFor(root, kOldest)));
}

TEST(AudioCache, PruneEvictsTheOldestWantedFirst)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root = dir.path().toStdWString();
    // The ceiling the case passes: three planted files at 400 bytes each run past it, and the
    // pruning that the fourth store runs evicts until it is met again.
    AudioCache cache{root, 1000};

    plant(root, kOldest, kBytes, std::chrono::hours{2});
    plant(root, kMiddle, kBytes, std::chrono::hours{1});
    plant(root, kNewest, kBytes, std::chrono::minutes{0});
    EXPECT_EQ(cache.bytesOnDisk(), 3 * static_cast<qint64>(kBytes.size()));

    // The fourth file tips it over; the two oldest go, and the newest two stay.
    EXPECT_TRUE(cache.store(QStringLiteral("0123456789abcdef0123456789abcdef01234560"), kBytes));
    EXPECT_FALSE(std::filesystem::exists(fileFor(root, kOldest)));
    EXPECT_FALSE(std::filesystem::exists(fileFor(root, kMiddle)));
    EXPECT_TRUE(std::filesystem::exists(fileFor(root, kNewest)));
    EXPECT_TRUE(std::filesystem::exists(fileFor(root, QStringLiteral("0123456789abcdef0123456789abcdef01234560"))));
    EXPECT_EQ(cache.bytesOnDisk(), 2 * static_cast<qint64>(kBytes.size()));
}

TEST(AudioCache, AHitMarksTheFileAsWanted)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root = dir.path().toStdWString();
    AudioCache                  cache{root, 1000};

    plant(root, kOldest, kBytes, std::chrono::hours{2});
    plant(root, kMiddle, kBytes, std::chrono::hours{1});

    // The hit moves the older file's time forward: the single eviction that the next store runs
    // takes the one that was not just wanted, where without the touch it would take the hit's own
    // file. One eviction, exactly the one the touch moves out of the way.
    ASSERT_TRUE(cache.load(kOldest).has_value());

    EXPECT_TRUE(cache.store(kNewest, kBytes));
    EXPECT_FALSE(std::filesystem::exists(fileFor(root, kMiddle)));
    EXPECT_TRUE(std::filesystem::exists(fileFor(root, kOldest)));
    EXPECT_TRUE(std::filesystem::exists(fileFor(root, kNewest)));
}

TEST(AudioCache, ClearRemovesEverythingAndLeavesNothingToCount)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root = dir.path().toStdWString();
    AudioCache                  cache{root};

    ASSERT_TRUE(cache.store(kOldest, kBytes));
    cache.clear();
    EXPECT_FALSE(std::filesystem::exists(root));
    EXPECT_EQ(cache.bytesOnDisk(), 0);
    EXPECT_FALSE(cache.load(kOldest).has_value());
}
