#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "core/document/known_store.h"
#include "core/domain/sync_run.h"

/**
 * @file sync_partition.h
 * @brief The `sync` object of the settings document, read and written through the names this
 *        feature owns (docs/adr/0042).
 *
 * Every key a sync keeps lives here: the sign-in's tokens and login, the cloud copy's id and derived
 * key, this device's id, and the time and settings snapshot the last successful run left. Nothing
 * outside this file spells one of those keys, so a key renamed or dropped happens in one place.
 */

namespace lens::core::sync {

class SyncPartition {
public:
    /// @brief Makes a device id on the first run on a device; an empty one uses a random id.
    using DeviceId = std::function<std::string()>;

    /**
     * @brief Bind the partition to the document that carries it.
     * @param store The document owner; its `sync` object is this partition.
     * @param newDeviceId How a missing device id is made, for a test that wants a stable one.
     */
    explicit SyncPartition(KnownStore& store, DeviceId newDeviceId = {});

    /// @return Whether an access token is stored.
    [[nodiscard]] bool        signedIn() const;
    [[nodiscard]] std::string token() const;
    [[nodiscard]] bool        hasRefreshToken() const;
    [[nodiscard]] std::string refreshToken() const;
    /// @brief Keep the pair a sign-in or a refresh produced; an empty refresh removes the key.
    void storeTokens(std::string access, std::string refresh);
    /// @brief Forget both tokens, as after a refusal that proves the sign-in is dead.
    void forgetTokens();

    [[nodiscard]] std::string login() const;
    void                      setLogin(std::string login);

    /// @return This device's id, made and stored on the first call.
    std::string deviceId();

    [[nodiscard]] std::string gistId() const;
    void                      setGistId(std::string gistId);
    /// @brief Forget the cloud copy's id alone, as when the gist is no longer there.
    void forgetGistId();

    /// @return Whether a derived key is stored.
    [[nodiscard]] bool hasKey() const;
    /// @return The key and the parameters it was derived under, or nullopt when missing or malformed.
    [[nodiscard]] std::optional<syncrun::StoredKey> storedKey() const;
    /// @brief Keep the key a run derived, so the next one needs no passphrase.
    void storeKey(const syncrun::StoredKey& key);

    /// @return The settings object the last successful run left, or an empty object.
    [[nodiscard]] nlohmann::json settingsSnapshot() const;
    void                         setSettingsSnapshot(const nlohmann::json& snapshot);

    [[nodiscard]] std::string lastSyncedAt() const;
    void                      setLastSyncedAt(std::string minute);

    /// @brief Forget the cloud copy's identity after it was deleted: its id, its key and its time.
    void forgetCloudCopy();
    /// @brief Forget everything a sign-out takes, leaving this device's id in place.
    void forgetAll();

private:
    /// @return The `sync` object as a section of the document.
    SettingsSection section() const;

    KnownStore& store_;
    DeviceId    newDeviceId_;
};

}
