#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

/**
 * @file pet_package_fs.h
 * @brief The file-system half of the pet package contract (skills/lens-pet-maker/references/pet-package-format.md section 5, rules 1 to 3, and the
 *        limits of section 4 that need a size or a count).
 *
 * A symbolic link and a directory junction need no privilege to create, and an archive can carry one, so the path a
 * package names is checked in the file system and not only as a string: every component inside the package must be
 * an ordinary file or folder, and the resolved file must still be under the package.
 */

namespace lens::app::pet {

/**
 * @brief Whether a path is a reparse point: a symbolic link or a directory junction, which can point anywhere.
 * @param path The path itself. The link is not followed.
 * @return True for a link or a junction; false for an ordinary file or folder, or for a path that does not exist.
 */
bool isReparsePoint(std::filesystem::path const& path);

/**
 * @brief Resolves a path a package wrote to the file it names, inside `root`, or refuses it.
 *
 * The spelling is checked first (no absolute path, drive, UNC prefix or `..`). Then each component under `root` must
 * exist as an ordinary entry, and the canonical location must still be under the canonical `root`.
 *
 * @param root The package's folder. Its own spelling is not checked, since the reader chose where it lives.
 * @param relative The path as written in the package document, in UTF-8.
 * @return The file's path, under `root`.
 * @throws core::pet::Refused PathEscapes if the path leaves the package by any of those means; Unreadable if it is missing.
 */
std::filesystem::path resolvePackageFile(std::filesystem::path const& root, std::string const& relative);

/**
 * @brief Walks a whole package and refuses it if it holds a link, or is past a count or size limit.
 *
 * Every entry is checked before it is followed, so a junction is refused rather than walked into. Files are counted
 * and their sizes summed; a JSON file over 256 KB or a PNG over 8 MB is refused by its own limit.
 *
 * @param root The package's folder.
 * @throws core::pet::Refused PathEscapes for a link inside the package, Unreadable if a folder cannot be listed,
 *         TooLarge past the file, byte or per-file limits.
 */
void checkPackageTree(std::filesystem::path const& root);

} // namespace lens::app::pet
