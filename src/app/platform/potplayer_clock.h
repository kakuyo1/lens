#pragma once

/**
 * @file potplayer_clock.h
 * @brief The PotPlayer window behind the scan: its playback clock through window messages, and
 *        the media file it plays, resolved from the process command line or the player's own
 *        playlist file.
 *
 * The player answers WM_USER queries: 20484 the current position in milliseconds, 20486 the play
 * status (1 paused, 2 playing), 24624/24625 the video's pixel size. The media path never appears
 * in a message and the window title carries the file name only, so a candidate path counts only
 * when its file name is the one the title shows. Windows only.
 */

#include <QRect>
#include <QSize>
#include <QString>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lens::app {

/// @return Whether @p window's class is one of PotPlayer's ("PotPlayer64" and its siblings).
bool isPotPlayerWindow(quintptr window);

/// @return The playback position in media milliseconds; nothing when the window does not answer.
std::optional<std::int64_t> potplayerTimeMs(quintptr window);

/// @return 1 while paused, 2 while playing; nothing when the window does not answer.
std::optional<int> potplayerPlayStatus(quintptr window);

/// @return The video's pixel size; nothing while no video is loaded or the window does not answer.
std::optional<QSize> potplayerVideoSize(quintptr window);

/// @return The rectangle the video's pixels occupy on screen: the video's size fitted into the
///         window's client area, centered, in physical pixels. The client area itself when the
///         size cannot be read.
std::optional<QRect> potplayerVideoRect(quintptr window);

/// @return The player executable's full path, which names the playlist file and the registry
///         section the caption style lives under; empty when the process cannot be queried.
std::wstring potplayerExecutablePath(quintptr window);

/// @return The window's title as the player spells it.
QString potplayerWindowTitle(quintptr window);

/// @return The media file name @p title carries: the title is "<file name> - PotPlayer", so the
///         suffix comes off. Empty when the title names no file (the player at rest).
QString potplayerFileNameFromTitle(QString const& title);

/// @return The media file behind @p window when it is the one the title names: the first command
///         line argument of that file name, else the playname= entry of
///         `<executable dir>\Playlist\<executable stem>.dpl`.
std::optional<std::filesystem::path> potplayerMediaPath(quintptr window);

/// @return The argument whose file name is @p fileName, case-insensitive; nothing when none is.
/// @note Matching is by name alone: the caller passes the name the window title shows, which is
///       what keeps a stale command line from naming another file. URL arguments never match.
std::optional<std::filesystem::path> mediaArgumentFrom(std::vector<std::wstring> const& arguments,
                                                       std::wstring const&              fileName);

/// @brief Read the playname= entry of a playlist document.
/// @param playlistBytes The file's bytes, UTF-8 (a BOM is tolerated) or UTF-16LE with its BOM.
/// @return The entry whose file name is @p fileName, case-insensitive; nothing when none is.
std::optional<std::filesystem::path> mediaPathFromPlaylist(std::string_view    playlistBytes,
                                                           std::wstring const& fileName);

}
