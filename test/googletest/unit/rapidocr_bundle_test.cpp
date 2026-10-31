/**
 * @file rapidocr_bundle_test.cpp
 * @brief The pinned RapidOCR distribution without a network: the pins it carries, whether an
 *        unpacked folder counts as installed, and which paths it offers the detector.
 *
 * The catalogue is what the downloader fetches against and what the settings page reads, so the
 * pins are pinned here too -- a digest that drifts from the audited release must fail here before
 * it ships. The downloader's own transport is not exercised; a case cannot reach GitHub.
 */

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <filesystem>
#include <gtest/gtest.h>

#include "capture/rapidocr_bundle.h"

namespace {

using lens::capture::BundleFile;
using lens::capture::BundleSpec;
using lens::capture::bundleExecutablePath;
using lens::capture::bundleFolder;
using lens::capture::bundleInstalled;
using lens::capture::bundleModelsDirectoryPath;
using lens::capture::bundleRoot;
using lens::capture::bundleSpec;
using lens::capture::verifyBundleFile;

/// @brief Create a file of exactly @p size bytes at @p path, under directories made as needed.
void makeFile(QString const& path, qint64 size)
{
    QDir{QFileInfo{path}.absolutePath()}.mkpath(QStringLiteral("."));
    QFile file{path};
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_TRUE(file.resize(size));
}

/// @brief Lay the whole distribution out under @p root, at the pins' exact sizes.
void makeDistribution(QString const& root)
{
    for (BundleFile const& file : bundleSpec().files)
        makeFile(root + QStringLiteral("/RapidOCR-json_v0.2.0/") + file.name, file.size);
}

/// @brief The inner folder a distribution unpacks into, under @p root.
QString innerFolder(QString const& root)
{
    return root + QStringLiteral("/") + bundleSpec().innerFolder;
}

} // namespace

/// The pins are the audit's: one archive, the folder it unpacks, and the files the detector reads.
TEST(RapidOcrBundle, TheCataloguePinsTheAuditedRelease)
{
    BundleSpec const& spec = bundleSpec();
    EXPECT_EQ(spec.url,
              QStringLiteral("https://github.com/hiroi-sora/RapidOCR-json/releases/download/v0.2.0/RapidOCR-json_v0.2.0.7z"));
    EXPECT_EQ(spec.size, 73461693);
    EXPECT_EQ(spec.sha256,
              QStringLiteral("7ad9b283d03436c6cd0296723188699299cb4e5cf9140b410c59543aa5793c40"));
    EXPECT_EQ(spec.innerFolder, QStringLiteral("RapidOCR-json_v0.2.0"));

    // The executable's digest is the audit's own; the six model files are the ones the trust
    // digest covers (rapidOcrModelFiles() for both languages).
    ASSERT_EQ(spec.files.size(), 7u);
    EXPECT_EQ(spec.files[0].name, QStringLiteral("RapidOCR-json.exe"));
    EXPECT_EQ(spec.files[0].size, 16849920);
    EXPECT_EQ(spec.files[0].sha256,
              QStringLiteral("616bd43a4672ee9fcf4d1ca607d7d9ec413b45d489482064d97e00a7efa9d76e"));
}

/// Installed is derived from disk: every file at its exact size, and nothing less.
TEST(RapidOcrBundle, InstalledIsDerivedFromDisk)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    EXPECT_FALSE(bundleInstalled(root.path().toStdWString())) << "an empty root installs nothing";

    makeDistribution(root.path());
    EXPECT_TRUE(bundleInstalled(root.path().toStdWString()));

    // One pin file missing -- the state a half-deleted folder leaves -- is not installed.
    QFile{innerFolder(root.path()) + QStringLiteral("/models/dict_en.txt")}.remove();
    EXPECT_FALSE(bundleInstalled(root.path().toStdWString()));
}

/// One byte short, the way a cut-off unpack would leave it, is not installed.
TEST(RapidOcrBundle, AFileOneByteShortIsNotInstalled)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    makeDistribution(root.path());
    QFile file{innerFolder(root.path()) + QStringLiteral("/models/dict_en.txt")};
    ASSERT_TRUE(file.resize(189));
    EXPECT_FALSE(bundleInstalled(root.path().toStdWString()));
}

/// The verify is size and digest together; a right-sized file with the wrong bytes refuses.
TEST(RapidOcrBundle, VerifyChecksTheDigestNotJustTheSize)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/file.bin");

    QFile out{path};
    ASSERT_TRUE(out.open(QIODevice::WriteOnly));
    const QByteArray bytes = QByteArrayLiteral("12345678");
    out.write(bytes);
    out.close();
    QCryptographicHash hash{QCryptographicHash::Sha256};
    hash.addData(bytes);

    BundleFile const pinned{QStringLiteral("file.bin"), bytes.size(), QString::fromLatin1(hash.result().toHex())};
    EXPECT_TRUE(verifyBundleFile(path, pinned));

    // Same size, different bytes: the digest says no where the size check alone would pass.
    QFile tampered{path};
    ASSERT_TRUE(tampered.open(QIODevice::WriteOnly | QIODevice::Truncate));
    tampered.write(QByteArrayLiteral("12345679"));
    tampered.close();
    EXPECT_FALSE(verifyBundleFile(path, pinned));
}

/// The paths a completed distribution offers are the ones the detector and trust digest read.
TEST(RapidOcrBundle, ThePathsAreTheDetectorShape)
{
    EXPECT_EQ(QString::fromStdWString(bundleFolder().filename().wstring()),
              QStringLiteral("RapidOCR-json_v0.2.0"));
    EXPECT_EQ(QString::fromStdWString(bundleExecutablePath().filename().wstring()),
              QStringLiteral("RapidOCR-json.exe"));
    EXPECT_EQ(QString::fromStdWString(bundleModelsDirectoryPath().filename().wstring()),
              QStringLiteral("models"));
    EXPECT_EQ(QString::fromStdWString(bundleRoot().filename().wstring()),
              QStringLiteral("rapidocr"));
}
