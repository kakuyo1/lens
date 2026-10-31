#pragma once

#include <QString>

#include <filesystem>
#include <span>

/**
 * @file rapidocr_bundle.h
 * @brief The one RapidOCR-json distribution docs/adr/0149 allows Lens to download: where it comes
 *        from, what it must weigh, and where it is put.
 *
 * No network and no extraction in here. The catalogue is what the downloader fetches (bundle_downloader.h)
 * and what the detector reads (ocr_rapidocr.h), so a case can pin every number in it.
 *
 * The archive and every file inside it are pinned to the v0.2.0 release the adapter serves, each with
 * the size and digest read from that release (docs/metrics/ocr-baseline-2026-10-28.md, section 2).
 * Moving a pin is a decision recorded in an ADR, not a refresh.
 */

namespace lens::capture {

/// @brief One file the unpacked distribution must hold, and the bytes it must be.
struct BundleFile {
    QString name;   ///< The file's path inside the distribution folder.
    qint64  size;   ///< The exact byte count.
    QString sha256; ///< Lower-case hex SHA-256 of the whole file.
};

/// @brief The pinned distribution: one GitHub release asset and the files it must unpack into.
struct BundleSpec {
    QString                     url;         ///< The release asset's download address.
    qint64                      size;        ///< The archive's exact byte count.
    QString                     sha256;      ///< Lower-case hex SHA-256 of the whole archive.
    QString                     innerFolder; ///< The folder the archive unpacks into, under the root.
    std::span<BundleFile const> files;       ///< Everything the detector and the trust digest read.
};

/// @return The catalogue entry: the v0.2.0 distribution.
BundleSpec const& bundleSpec();

/// @return The folder the bundle lives in: `%APPDATA%\Lens\rapidocr`.
std::filesystem::path bundleRoot();

/// @return The distribution folder the archive unpacks into, under bundleRoot().
std::filesystem::path bundleFolder();

/// @return The executable and the model folder a completed distribution offers, as the detector
///         reads them. Only meaningful once bundleInstalled() is true.
std::filesystem::path bundleExecutablePath();
std::filesystem::path bundleModelsDirectoryPath();

/// @return The archive's own path under the root, where the verified 7z sits until it unpacks.
std::filesystem::path bundleArchivePath();

/// @brief Whether every file the spec names sits under @p root, in the distribution folder, at its
///        exact size.
///
/// A size check, not a digest. It runs on every settings read; the digests were checked when the
/// files arrived -- the archive's on download, the executable's at unpack. A file replaced with one
/// of the same size would pass here; the engine's own trust digest is the deeper check.
bool bundleInstalled(std::filesystem::path const& root);

/// @return Whether @p path is exactly @p file: its size, and its SHA-256.
bool verifyBundleFile(QString const& path, BundleFile const& file);

}
