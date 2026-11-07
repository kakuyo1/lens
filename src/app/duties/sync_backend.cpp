/**
 * @file sync_backend.cpp
 * @brief The provider factory, for sync_backend.h.
 */

#include "app/duties/sync_backend.h"

#include "app/duties/github_backend.h"
#include "app/duties/webdav_backend.h"
#include "util/log.h"

namespace lens::app {

SyncBackend::SyncBackend(QObject* parent)
    : QObject(parent)
{
}

std::unique_ptr<SyncBackend> makeSyncBackend(std::string_view provider, const std::filesystem::path& dataDir, QNetworkAccessManager* manager, core::sync::SyncPartition& partition, QObject* parent)
{
    if (provider == "webdav")
        return std::make_unique<WebdavBackend>(partition, manager, parent);
    // The partition already applies the default, so this branch is for a hand-edited document; a
    // fallback keeps the duty working instead of leaving it without a backend.
    if (provider != core::sync::kDefaultProvider)
        LENS_WARN("sync: unknown provider '{}', falling back to '{}'", std::string(provider), std::string(core::sync::kDefaultProvider));
    return std::make_unique<GithubBackend>(dataDir, manager, partition, parent);
}

}
