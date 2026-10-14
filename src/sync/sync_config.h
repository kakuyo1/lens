#pragma once

#include <QString>

#include <filesystem>

/**
 * @file sync_config.h
 * @brief The one value sign-in needs from the data directory: the GitHub OAuth App's client id.
 *
 * The id is public -- a device-flow app has no secret -- so it ships in `data/sync.json`. An empty
 * id means the project has not registered an app yet, and the sign-in entry says so instead of
 * asking GitHub with nothing.
 */

namespace lens::sync {

/// @brief What the sync module reads from `data/sync.json`.
struct SyncConfig {
    QString clientId; ///< Empty when no OAuth App is registered; sign-in is then off.
};

/**
 * @brief Read the client id from `data/sync.json`.
 *
 * @param path Normally `<repo>/data/sync.json`.
 * @return The config. A missing or unreadable file, or one without a string `clientId`, yields an
 *         empty id rather than an error: the caller treats that as "not configured".
 */
SyncConfig loadSyncConfig(const std::filesystem::path& path);

}
