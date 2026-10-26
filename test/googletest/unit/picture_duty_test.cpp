/**
 * @file picture_duty_test.cpp
 * @brief The picture duty's closed failure vocabulary: no results, unreachable, download.
 *
 * The source is a fake that answers at once, and the cache is a temporary directory. Nothing here
 * touches the network or a window; the point is that the card is told which failure to name, not a
 * transport's own words.
 */

#include <filesystem>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QUrl>
#include <gtest/gtest.h>

#include "app/duties/picture_duty.h"
#include "pictures/picture_cache.h"
#include "pictures/picture_source.h"

namespace {

using lens::app::PictureDuty;
using lens::pictures::ByteSink;
using lens::pictures::Picture;
using lens::pictures::PictureCache;
using lens::pictures::PictureSink;
using lens::pictures::PictureSource;

/// A source with one fixed answer: empty, unreachable, or one candidate whose download fails.
class FakeSource final : public PictureSource {
public:
    enum class Mode { NoResults,
                      Unreachable,
                      DownloadFails,
                      NeverAnswers };

    explicit FakeSource(Mode mode)
        : mode_(mode)
    {}

    void search(QString const&, PictureSink sink) override
    {
        if (mode_ == Mode::NeverAnswers)
            return; // the deadline, not this source, is what ends the lookup
        if (mode_ == Mode::Unreachable) {
            sink({}, false);
            return;
        }
        if (mode_ == Mode::NoResults) {
            sink({}, true);
            return;
        }
        Picture picture;
        picture.thumbnail = QUrl{QStringLiteral("https://upload.wikimedia.org/wikipedia/commons/x.jpg")};
        picture.page      = QUrl{QStringLiteral("https://commons.wikimedia.org/wiki/File:X.jpg")};
        picture.caption   = QStringLiteral("a hand tool");
        picture.author    = QStringLiteral("A Person");
        picture.license   = QStringLiteral("CC BY-SA 4.0");
        picture.source    = QStringLiteral("Wikimedia Commons");
        sink({picture}, true);
    }

    void fetch(QUrl const&, ByteSink sink) override
    {
        sink({}, false);
    }

private:
    Mode mode_;
};

/// The application the deadline's timer fires in; every case here needs one.
struct QtApplication {
    int              argc     = 1;
    char             name[32] = "picture_duty_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application{argc, argv};
};

/// @brief One duty over a cache directory of its own.
struct PictureDutyTest : ::testing::Test {
    /// The deadline is a timer, so every case here runs with an application to fire it in.
    QtApplication qt;

    void SetUp() override
    {
        root_ = std::filesystem::temp_directory_path() / "lens_picture_duty_test";
        std::filesystem::remove_all(root_);
        cache_ = std::make_unique<PictureCache>(root_);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(root_);
    }

    std::filesystem::path         root_;
    std::unique_ptr<PictureCache> cache_;
};

TEST_F(PictureDutyTest, NamesNoResults)
{
    FakeSource  source(FakeSource::Mode::NoResults);
    PictureDuty duty(source, *cache_);
    duty.request(QStringLiteral("garden trowel"));

    const auto pictures = duty.pictures();
    EXPECT_EQ(pictures.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    EXPECT_EQ(pictures.value(QStringLiteral("failure")).toString(), QStringLiteral("no-results"));
}

TEST_F(PictureDutyTest, NamesAnUnreachableSearch)
{
    FakeSource  source(FakeSource::Mode::Unreachable);
    PictureDuty duty(source, *cache_);
    duty.request(QStringLiteral("garden trowel"));

    const auto pictures = duty.pictures();
    EXPECT_EQ(pictures.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    EXPECT_EQ(pictures.value(QStringLiteral("failure")).toString(), QStringLiteral("unreachable"));
}

TEST_F(PictureDutyTest, NamesAFailedDownload)
{
    FakeSource  source(FakeSource::Mode::DownloadFails);
    PictureDuty duty(source, *cache_);
    duty.request(QStringLiteral("garden trowel"));

    const auto pictures = duty.pictures();
    EXPECT_EQ(pictures.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    EXPECT_EQ(pictures.value(QStringLiteral("failure")).toString(), QStringLiteral("download"));
}

/// A search that never answers ends at the deadline, not in a crash: settle used to take the
/// deadline's `live_` by reference and release it mid-body, and the next read went through null
/// (dump lens.exe.5160.dmp, 2026-10-26: AV at QString::isEmpty on a null Fetch).
TEST_F(PictureDutyTest, SettlesAnUnansweredSearchAtTheDeadline)
{
    FakeSource  source(FakeSource::Mode::NeverAnswers);
    PictureDuty duty(source, *cache_, nullptr, 20);
    duty.request(QStringLiteral("garden trowel"));
    EXPECT_EQ(duty.pictures().value(QStringLiteral("state")).toString(), QStringLiteral("loading"));

    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 2000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (duty.pictures().value(QStringLiteral("state")).toString() != QStringLiteral("loading"))
            break;
    }

    const auto pictures = duty.pictures();
    EXPECT_EQ(pictures.value(QStringLiteral("state")).toString(), QStringLiteral("failed"));
    EXPECT_EQ(pictures.value(QStringLiteral("failure")).toString(), QStringLiteral("unreachable"));
}

} // namespace
