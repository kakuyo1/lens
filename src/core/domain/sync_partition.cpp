/**
 * @file sync_partition.cpp
 * @brief The typed `sync` object, for sync_partition.h.
 */

#include "core/domain/sync_partition.h"

#include <cstddef>
#include <random>

#include "core/crypto/vault.h"

namespace lens::core::sync {
namespace {

/// The one object of the settings document this feature owns, and every key in it.
constexpr auto kSection      = "sync";
constexpr auto kToken        = "token";
constexpr auto kRefreshToken = "refreshToken";
constexpr auto kLogin        = "login";
constexpr auto kDeviceId     = "deviceId";
constexpr auto kGistId       = "gistId";
constexpr auto kKey          = "key";
constexpr auto kSalt         = "salt";
constexpr auto kIterations   = "iterations";
constexpr auto kSnapshot     = "settingsSnapshot";
constexpr auto kLastSyncedAt = "lastSyncedAt";

/// @return A fresh device id in the shape QUuid::WithoutBraces wrote: 8-4-4-4-12 lower-case hex.
/// @note The id is opaque and not a secret; the shape is kept so a document written by either
///       build reads the same.
std::string randomDeviceId()
{
    std::random_device                 source;
    std::mt19937                       generator(source());
    std::uniform_int_distribution<int> nibble(0, 15);
    std::string                        id(36, '-');
    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23)
            continue;
        id[i] = "0123456789abcdef"[nibble(generator)];
    }
    return id;
}

}

SyncPartition::SyncPartition(KnownStore& store, DeviceId newDeviceId)
    : store_(store), newDeviceId_(std::move(newDeviceId))
{
}

SettingsSection SyncPartition::section() const
{
    return store_.section(kSection);
}

bool SyncPartition::signedIn() const
{
    return !token().empty();
}

std::string SyncPartition::token() const
{
    return section().value<std::string>(kToken, "");
}

bool SyncPartition::hasRefreshToken() const
{
    return !refreshToken().empty();
}

std::string SyncPartition::refreshToken() const
{
    return section().value<std::string>(kRefreshToken, "");
}

void SyncPartition::storeTokens(std::string access, std::string refresh)
{
    auto token = section();
    token.set<std::string>(kToken, access);
    // An app whose user tokens do not expire sends no refresh token, and an empty one would make
    // the next 401 look renewable when it is not.
    if (refresh.empty())
        token.remove(kRefreshToken);
    else
        token.set<std::string>(kRefreshToken, refresh);
}

void SyncPartition::forgetTokens()
{
    auto token = section();
    token.remove(kToken);
    token.remove(kRefreshToken);
}

std::string SyncPartition::login() const
{
    return section().value<std::string>(kLogin, "");
}

void SyncPartition::setLogin(std::string login)
{
    section().set<std::string>(kLogin, login);
}

std::string SyncPartition::deviceId()
{
    auto        section = this->section();
    std::string id      = section.value<std::string>(kDeviceId, "");
    if (!id.empty())
        return id;
    id = newDeviceId_ ? newDeviceId_() : randomDeviceId();
    section.set<std::string>(kDeviceId, id);
    return id;
}

std::string SyncPartition::gistId() const
{
    return section().value<std::string>(kGistId, "");
}

void SyncPartition::setGistId(std::string gistId)
{
    section().set<std::string>(kGistId, gistId);
}

void SyncPartition::forgetGistId()
{
    section().remove(kGistId);
}

bool SyncPartition::hasKey() const
{
    return !section().value<std::string>(kKey, "").empty();
}

std::optional<syncrun::StoredKey> SyncPartition::storedKey() const
{
    const auto section    = this->section();
    const auto key        = vault::decodeBase64(section.value<std::string>(kKey, ""));
    const auto salt       = vault::decodeBase64(section.value<std::string>(kSalt, ""));
    const int  iterations = section.value<int>(kIterations, 0);
    if (!key || !salt || key->size() != 32 || iterations <= 0)
        return std::nullopt;
    return syncrun::StoredKey{*key, vault::KdfParams{*salt, static_cast<unsigned>(iterations)}};
}

void SyncPartition::storeKey(const syncrun::StoredKey& key)
{
    auto section = this->section();
    section.set<std::string>(kKey, vault::encodeBase64(key.key));
    section.set<std::string>(kSalt, vault::encodeBase64(key.params.salt));
    section.set<int>(kIterations, static_cast<int>(key.params.iterations));
}

nlohmann::json SyncPartition::settingsSnapshot() const
{
    const nlohmann::json& document = store_.document();
    if (!document.is_object())
        return nlohmann::json::object();
    const auto sync = document.find(kSection);
    if (sync == document.end() || !sync->is_object())
        return nlohmann::json::object();
    const auto snapshot = sync->find(kSnapshot);
    if (snapshot == sync->end() || !snapshot->is_object())
        return nlohmann::json::object();
    return *snapshot;
}

void SyncPartition::setSettingsSnapshot(const nlohmann::json& snapshot)
{
    section().setRaw(kSnapshot, snapshot);
}

std::string SyncPartition::lastSyncedAt() const
{
    return section().value<std::string>(kLastSyncedAt, "");
}

void SyncPartition::setLastSyncedAt(std::string minute)
{
    section().set<std::string>(kLastSyncedAt, minute);
}

void SyncPartition::forgetCloudCopy()
{
    auto section = this->section();
    for (const char* key : {kGistId, kKey, kSalt, kIterations, kLastSyncedAt})
        section.remove(key);
}

void SyncPartition::forgetAll()
{
    auto section = this->section();
    for (const char* key : {kToken, kRefreshToken, kLogin, kGistId, kKey, kSalt, kIterations, kSnapshot, kLastSyncedAt})
        section.remove(key);
}

}
