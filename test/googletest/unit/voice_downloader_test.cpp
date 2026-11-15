/**
 * @file voice_downloader_test.cpp
 * @brief The fetch behind voice_downloader.h, driven end to end over a stubbed network and a
 *        catalogue of its own.
 *
 * The production catalogue pins digests of files nobody can reproduce here, which is why the
 * downloader takes its catalogue through a seam (voice_downloader.h): a case supplies two small
 * files and computes their SHA-256 the same way verifyFile() checks them. Every transfer, refusal
 * and interruption path lands in this file; the digest arithmetic itself lives in
 * voice_catalog_test.cpp.
 */

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "scripted_manager.h"
#include "speech/voice_downloader.h"

namespace {

using lens::speech::VoiceDownloader;
using lens::speech::VoiceFile;
using lens::speech::VoiceModel;
using lens::speech::VoiceSpec;
using lens::test::ScriptedManager;

/// One QCoreApplication for a case's duration: the downloader's network objects need one, and Qt
/// allows one at a time, so each case that touches them makes its own.
struct QtApplication {
    QtApplication()
        : application(argc, argv)
    {
    }

    int              argc     = 1;
    char             name[24] = "voice_downloader_test";
    char*            argv[2]  = {name, nullptr};
    QCoreApplication application;
};

/// @return Whether @p done came true within @p ms, turning the loop so a reply can arrive.
bool spinUntil(const std::function<bool()>& done, int ms = 5000)
{
    QElapsedTimer clock;
    clock.start();
    while (not done() and clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

/// @return The lower-case hex SHA-256 the catalogue carries for @p bytes.
QString digestOf(QByteArray const& bytes)
{
    return QString::fromUtf8(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

const QByteArray kFirst  = QByteArray(64, 'x');
const QByteArray kSecond = QByteArray("second file bytes");

/// The two files the test catalogue offers. The span points at static storage, so the reference the
/// catalogue function returns outlives every case.
const VoiceFile kFiles[] = {
    {QStringLiteral("tokens.txt"), kFirst.size(), digestOf(kFirst)},
    {QStringLiteral("model.bin"), kSecond.size(), digestOf(kSecond)},
};
const VoiceSpec kSpec{QStringLiteral("test-voice"), QStringLiteral("owner/repo"), QStringLiteral("rev0"), std::span<VoiceFile const>{kFiles}};

/// @return The catalogue seam handing out the two files above.
VoiceDownloader::Catalog testCatalog()
{
    return [](VoiceModel) -> VoiceSpec const& { return kSpec; };
}

/// What the downloader reported. Plain connections rather than a QSignalSpy: this target links Qt
/// Core and Network, not Qt Test.
struct Reports {
    int                 finished = 0;
    bool                ok       = false;
    std::vector<qint64> received;
    qint64              total = 0;
};

void record(VoiceDownloader& downloader, Reports& seen)
{
    QObject::connect(&downloader, &VoiceDownloader::progressed, [&seen](qint64 received, qint64 total) {
        seen.received.push_back(received);
        seen.total = total;
    });
    QObject::connect(&downloader, &VoiceDownloader::finished, [&seen](bool ok) {
        ++seen.finished;
        seen.ok = ok;
    });
}

} // namespace

TEST(VoiceDownloader, TwoVerifiedFilesLandUnderTheirOwnNames)
{
    QtApplication qt;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    ScriptedManager manager{500};
    manager.queue(200, kFirst);
    manager.queue(200, kSecond);
    VoiceDownloader downloader{&manager, testCatalog()};
    Reports         seen;
    record(downloader, seen);

    downloader.start(VoiceModel::Kokoro, std::filesystem::path{root.path().toStdWString()});
    ASSERT_TRUE(spinUntil([&seen] { return seen.finished == 1; }));

    EXPECT_TRUE(seen.ok);
    EXPECT_FALSE(downloader.busy());
    ASSERT_EQ(manager.sent.size(), 2);
    EXPECT_TRUE(manager.sent[0].url.contains(QStringLiteral("huggingface.co/owner/repo/resolve/rev0/tokens.txt")));
    EXPECT_TRUE(manager.sent[1].url.contains(QStringLiteral("huggingface.co/owner/repo/resolve/rev0/model.bin")));

    // Both files are in place whole, named after their repository files, and no .part survives.
    const QDir voiceDir{root.filePath(QStringLiteral("test-voice"))};
    QFile      first{voiceDir.filePath(QStringLiteral("tokens.txt"))};
    QFile      second{voiceDir.filePath(QStringLiteral("model.bin"))};
    ASSERT_TRUE(first.open(QIODevice::ReadOnly));
    EXPECT_EQ(first.readAll(), kFirst);
    first.close();
    ASSERT_TRUE(second.open(QIODevice::ReadOnly));
    EXPECT_EQ(second.readAll(), kSecond);
    second.close();
    EXPECT_TRUE(voiceDir.entryList({QStringLiteral("*.part")}, QDir::Files).isEmpty());

    // The total is the spec's own, and the last count is everything received.
    ASSERT_FALSE(seen.received.empty());
    EXPECT_EQ(seen.received.back(), kFirst.size() + kSecond.size());
    EXPECT_EQ(seen.total, kFirst.size() + kSecond.size());
}

TEST(VoiceDownloader, ABodyThatFailsItsCheckLeavesNothingBehind)
{
    QtApplication qt;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    // The right size with the wrong content: the digest is what refuses it. A wrong size fails the
    // same check, so one refusal stands for both.
    ScriptedManager manager{500};
    manager.queue(200, QByteArray(64, 'y'));
    VoiceDownloader downloader{&manager, testCatalog()};
    Reports         seen;
    record(downloader, seen);

    downloader.start(VoiceModel::Kokoro, std::filesystem::path{root.path().toStdWString()});
    ASSERT_TRUE(spinUntil([&seen] { return seen.finished == 1; }));

    EXPECT_FALSE(seen.ok);
    EXPECT_FALSE(downloader.busy());
    const QDir voiceDir{root.filePath(QStringLiteral("test-voice"))};
    EXPECT_TRUE(voiceDir.entryList(QDir::Files).isEmpty());
}

TEST(VoiceDownloader, ARefusedAnswerEndsTheDownload)
{
    QtApplication qt;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    ScriptedManager manager{500};
    manager.queue(500, QByteArray("service unavailable"));
    VoiceDownloader downloader{&manager, testCatalog()};
    Reports         seen;
    record(downloader, seen);

    downloader.start(VoiceModel::Kokoro, std::filesystem::path{root.path().toStdWString()});
    ASSERT_TRUE(spinUntil([&seen] { return seen.finished == 1; }));

    EXPECT_FALSE(seen.ok);
    EXPECT_FALSE(downloader.busy());
    // The refused file never opens a second request.
    EXPECT_EQ(manager.requests, 1);
}

TEST(VoiceDownloader, AFileThatOutgrowsItsPinnedSizeIsCutOff)
{
    QtApplication qt;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    // Fifty bytes for a file pinned at sixty-four is fine, but the other way round is not: past the
    // pinned size the transfer stops there rather than filling the disk.
    ScriptedManager manager{500};
    manager.queue(200, QByteArray(65, 'x'));
    VoiceDownloader downloader{&manager, testCatalog()};
    Reports         seen;
    record(downloader, seen);

    downloader.start(VoiceModel::Kokoro, std::filesystem::path{root.path().toStdWString()});
    ASSERT_TRUE(spinUntil([&seen] { return seen.finished == 1; }));

    EXPECT_FALSE(seen.ok);
    EXPECT_FALSE(downloader.busy());
    const QDir voiceDir{root.filePath(QStringLiteral("test-voice"))};
    EXPECT_TRUE(voiceDir.entryList(QDir::Files).isEmpty());
}

TEST(VoiceDownloader, CancelStopsTheTransferThroughTheFailurePath)
{
    QtApplication qt;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    // A reply that never answers, so the case drives the stop itself: the partial file goes, and
    // the finished handler reports it the way any other failure is reported.
    ScriptedManager manager{500, {}, false};
    VoiceDownloader downloader{&manager, testCatalog()};
    Reports         seen;
    record(downloader, seen);

    downloader.start(VoiceModel::Kokoro, std::filesystem::path{root.path().toStdWString()});
    ASSERT_TRUE(downloader.busy());
    downloader.cancel();
    ASSERT_TRUE(spinUntil([&seen] { return seen.finished == 1; }));

    EXPECT_FALSE(seen.ok);
    EXPECT_FALSE(downloader.busy());
    const QDir voiceDir{root.filePath(QStringLiteral("test-voice"))};
    EXPECT_TRUE(voiceDir.entryList(QDir::Files).isEmpty());
}

TEST(VoiceDownloader, ASecondStartWhileOneRunsIsIgnored)
{
    QtApplication qt;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    ScriptedManager manager{500, {}, false};
    VoiceDownloader downloader{&manager, testCatalog()};
    Reports         seen;
    record(downloader, seen);

    downloader.start(VoiceModel::Kokoro, std::filesystem::path{root.path().toStdWString()});
    downloader.start(VoiceModel::LjSpeech, std::filesystem::path{root.path().toStdWString()});

    // The second call left: one request in flight, and the model is still the first voice's.
    EXPECT_EQ(manager.requests, 1);
    EXPECT_EQ(downloader.model(), VoiceModel::Kokoro);
    EXPECT_TRUE(downloader.busy());
    EXPECT_EQ(seen.finished, 0);
}

TEST(VoiceDownloader, AFolderThatCannotBeMadeEndsItWithoutARequest)
{
    QtApplication qt;
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    // The root is a file, not a directory: the folder under it cannot be made, and nothing is asked
    // of the network at all.
    const QString blocker = root.filePath(QStringLiteral("blocker"));
    QFile         file{blocker};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();

    ScriptedManager manager{500};
    VoiceDownloader downloader{&manager, testCatalog()};
    Reports         seen;
    record(downloader, seen);

    downloader.start(VoiceModel::Kokoro, std::filesystem::path{blocker.toStdWString()});
    ASSERT_TRUE(spinUntil([&seen] { return seen.finished == 1; }));

    EXPECT_FALSE(seen.ok);
    EXPECT_EQ(manager.requests, 0);
    EXPECT_FALSE(downloader.busy());
}
