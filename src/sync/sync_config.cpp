/**
 * @file sync_config.cpp
 * @brief Reads the OAuth App's client id from data/sync.json.
 */

#include "sync_config.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include "util/log.h"

namespace lens::sync {

SyncConfig loadSyncConfig(const std::filesystem::path& path)
{
    LENS_TRACE("loadSyncConfig: reading '{}'", path.string());

    // The wide form: path.string() is the local narrow code page, which mangles a non-ASCII install path.
    QFile file(QString::fromStdWString(path.wstring()));
    if (not file.open(QIODevice::ReadOnly)) {
        LENS_WARN("the sync settings could not be read; sign-in is off");
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (not document.isObject()) {
        LENS_WARN("the sync settings are not a JSON object; sign-in is off");
        return {};
    }

    SyncConfig config;
    config.clientId = document.object().value("clientId").toString().trimmed();
    if (config.clientId.isEmpty())
        LENS_INFO("no GitHub OAuth App is registered in data/sync.json; sign-in is off");
    return config;
}

}
