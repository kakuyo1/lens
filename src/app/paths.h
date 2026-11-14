#pragma once

#include <QString>

#include <filesystem>
#include <string>

/**
 * @file paths.h
 * @brief The path helpers src/app needs, and the two boundaries among them.
 *
 * These live here rather than in main.cpp's anonymous namespace because nothing in there can be
 * reached from a test. The boundaries the cases lock: settingsPath() keeps the reader's API key
 * in the profile and never in the install tree, and logDirectory() keeps the log -- which can
 * carry looked-up words -- out of an installed build's install folder the same way.
 */

namespace lens::app {

/// @return @p text as a std::filesystem::path, for the loaders that take one.
inline std::filesystem::path toPath(const QString& text)
{
    return std::filesystem::path{text.toStdWString()};
}

/// @return @p path as the text Qt reads, for QFile, QImageReader and QUrl.
inline QString widen(const std::filesystem::path& path)
{
    return QString::fromStdWString(path.wstring());
}

/// @return @p path as the log wants it: UTF-8, whatever the account is named.
inline std::string narrow(const std::filesystem::path& path)
{
    return widen(path).toStdString();
}

/**
 * @brief The settings document's path, making its directory and carrying a legacy copy over.
 *
 * `%APPDATA%\Lens\settings.json`, from QStandardPaths::AppDataLocation with the application
 * name main() sets and no organization name -- naming one would add a directory level for a
 * name the product does not use. The install folder is wrong for this file twice over: it can
 * be read-only under Program Files, and it is shared by every account, while the document
 * carries one reader's API key and word marks.
 *
 * A first run whose profile copy is missing copies @p legacy over, once, so a key already
 * configured keeps working. Only the path is ever logged -- the document holds the key, and
 * its contents do not belong in a log.
 *
 * @param legacy Document to carry over from, when the profile has none yet. May be empty.
 * @return The path to load and save; its directory exists on return unless it could not be
 *         created at all.
 */
std::filesystem::path settingsPath(const std::filesystem::path& legacy);

/// @brief Whether this is an installed build: `data/` sits beside the executable.
///
/// The one marker both install-tree questions read -- where the word list comes from, and where
/// the log goes (docs/adr/0178). A build tree has no `data/` next to its executable, because
/// nothing writes one.
bool installedBuild();

/**
 * @brief The directory the rotating log and the crash dumps live in.
 *
 * An installed build keeps them in the profile, `%APPDATA%\Lens\logs`, for the three reasons the
 * install folder is wrong for the log: a launch there starts in it, so a relative name lands the
 * log under Program Files; that folder may be read-only; it is shared by every account, while the
 * log can carry the words the reader looked up. The directory is resolved before main() builds a
 * QCoreApplication -- the crash handler is installed first -- so it cannot go through
 * QStandardPaths. A development tree keeps the repository's `logs/`, relative to the working
 * directory the run starts in, so `tail -f logs/lens.log` and the QA runners keep reading where
 * they always have.
 *
 * @param installedBuild The answer installedBuild() gives, named rather than resolved here so a
 *                       test drives both branches.
 * @return The directory. It is not created; log::init() and the crash handler make it when they
 *         run.
 */
std::filesystem::path logDirectory(bool installedBuild);

}
