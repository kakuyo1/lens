#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>

#include "core/vault.h"

/**
 * @file sync_run.h
 * @brief The two decisions one sync run makes that are not the run's own: which key opens the
 *        cloud copy, and whether anything goes back up (docs/adr/0042).
 *
 * Pure: text and JSON in, text and JSON out. No file, no network, no Qt, and nothing logged -- the
 * passphrase, the derived key and the cloud copy are all the reader's (SECURITY.md). The
 * encryption is core/vault.h's and the merge is core/sync_merge.h's; what lives here is the order
 * the two are called in, which is the part that had no case behind it: first-run key creation,
 * reopening an existing cloud copy, and the pass that has nothing to send were only reachable by
 * driving a real GistClient through a real event loop.
 */

namespace lens::core::syncrun {

/// @brief A derived key with the parameters it was derived under; the pair a cloud copy opens under.
struct StoredKey {
    vault::Bytes     key;
    vault::KdfParams params;
};

/// @brief What one reply to a gist call hands the run: the cloud copy, and what this device holds.
struct CloudRequest {
    /// The envelope read out of the gist's file, as text. Empty when the gist holds no cloud copy
    /// yet, and the account carries no sync gist at all on the run that found none.
    std::string envelopeText;
    /// The passphrase this run was given; empty when the stored key is to be used.
    std::string passphrase;
    /// The key stored on this device, when there is one.
    std::optional<StoredKey> stored;
};

/// @brief What opening the cloud copy leaves behind: the document behind it, the key, and any error.
struct OpenedCloud {
    /// The cloud copy as the merge reads it; empty when there is none to read.
    nlohmann::json remote = nlohmann::json::object();
    /// The key this run works under. Empty only when the run stopped before it had one.
    std::optional<StoredKey> key;
    /// The key to write into the sync section. Set only when a passphrase was given, because a run
    /// that opened with the stored key has nothing new to store.
    std::optional<StoredKey> keyToStore;
    /// The error code the settings page shows: empty, "generic" or "passphrase".
    std::string error;
};

/**
 * @brief Open the cloud copy: derive a key or reopen the stored one, and read the document behind it.
 *
 * A cloud copy already exists on the run that finds a gist, so its own salt and iteration count are
 * what a passphrase is derived under -- fresh parameters would not open it. With no cloud copy the
 * run mints fresh ones, which is the first-run key.
 *
 * @param request The envelope as it came back, the passphrase, and the key already on this device.
 * @return The cloud copy, the key in force, the key to store, or the code the run ends with. A
 *         passphrase that does not open the cloud copy stores nothing, so a key this device already
 *         had survives a mistyped one; a cloud copy that opens but holds something other than a
 *         document is a failed sync whose key is kept, because it did open what was there.
 * @throws std::runtime_error If the system random source or BCrypt fails.
 */
inline OpenedCloud openCloudCopy(const CloudRequest& request)
{
    OpenedCloud opened;

    // A gist file that will not parse is not a cloud copy this run can merge with, and the reader
    // is told the sync failed rather than that their passphrase was wrong.
    nlohmann::json envelope; // null: there is nothing to open
    if (!request.envelopeText.empty()) {
        envelope = nlohmann::json::parse(request.envelopeText, nullptr, false);
        if (envelope.is_discarded() || !envelope.is_object()) {
            opened.error = "generic";
            return opened;
        }
    }
    const bool hasEnvelope = !envelope.is_null();

    if (!request.passphrase.empty()) {
        vault::KdfParams params;
        if (hasEnvelope) {
            const auto sealedWith = vault::paramsOf(envelope);
            if (!sealedWith) {
                opened.error = "generic";
                return opened;
            }
            params = *sealedWith;
        } else {
            params = vault::newParams();
        }

        auto key = vault::deriveKey(request.passphrase, params);
        if (hasEnvelope) {
            const auto plaintext = vault::open(key, envelope);
            if (!plaintext) {
                opened.error = "passphrase";
                return opened;
            }
            const auto remote = nlohmann::json::parse(*plaintext, nullptr, false);
            if (remote.is_discarded() || !remote.is_object()) {
                // The key opened what was there, so it is this device's key either way; the run is
                // still reported as failed, and the next one does not need the passphrase again.
                opened.keyToStore = StoredKey{std::move(key), params};
                opened.error      = "generic";
                return opened;
            }
            opened.remote = remote;
        }
        opened.key        = StoredKey{std::move(key), params};
        opened.keyToStore = *opened.key;
        return opened;
    }

    if (!request.stored) {
        // Nothing on this device and no passphrase for this run: only the reader can supply one.
        opened.error = "passphrase";
        return opened;
    }
    opened.key = *request.stored;
    if (!hasEnvelope)
        return opened;

    const auto plaintext = vault::open(opened.key->key, envelope);
    if (!plaintext) {
        opened.error = "passphrase";
        return opened;
    }
    const auto remote = nlohmann::json::parse(*plaintext, nullptr, false);
    if (remote.is_discarded() || !remote.is_object()) {
        opened.error = "generic";
        return opened;
    }
    opened.remote = remote;
    return opened;
}

/// @brief What one run ends with: the gist call that carries the sealed copy, or that there is none.
struct Upload {
    /// @brief Which gist call to make. None when there is nothing to send, and the run is over.
    enum class Command { None,
                         Create,
                         Update };

    Command command = Command::None;
    /// The gist to replace; empty when @c command is Create.
    std::string gistId;
    /// The sealed envelope as the gist file's text; empty when @c command is None.
    std::string sealed;
};

/// @brief What one run would put into the cloud copy, and where it would put it.
struct UploadRequest {
    /// The key in force, as openCloudCopy() left it.
    StoredKey key;
    /// The merged document both sides keep.
    nlohmann::json merged;
    /// The gist's id; empty when the account carries no sync gist yet.
    std::string gistId;
    /// Whether the reader asked for this run, against a timer or a local change.
    bool manual = false;
    /// What this upload would carry, taken after the merged document was applied: a change made
    /// during the run is not in it, so it still counts as new and schedules the next sync.
    std::string fingerprint;
    /// What the last successful upload carried.
    std::string uploadedFingerprint;
};

/**
 * @brief Seal the merged document and say which gist call carries it.
 * @param request The key in force, the merged document, the gist, and the two fingerprints.
 * @return The call to make and the sealed text for it. Command::None when this pass has nothing to
 *         send, which is a sync that worked: the caller ends the run without an upload.
 * @throws std::runtime_error If BCrypt fails.
 */
inline Upload sealUpload(const UploadRequest& request)
{
    // A timer pass that merged the cloud's changes and found nothing new on this device has pulled
    // and has nothing to send back. Sealing and uploading it anyway is what makes the ten-minute
    // poll re-upload an unchanged copy every time it fires. A reader's own sync always uploads, and
    // so does an account with no gist: there is nothing there yet, whatever the fingerprints say.
    if (!request.manual && !request.gistId.empty() && request.fingerprint == request.uploadedFingerprint)
        return {};

    const auto sealed  = vault::seal(request.key.key, request.merged.dump(), request.key.params);
    const auto command = request.gistId.empty() ? Upload::Command::Create : Upload::Command::Update;
    return {command, request.gistId, sealed.dump()};
}

/**
 * @brief Whether a refused gist call is worth renewing before it is reported.
 * @param unauthorised    The call came back 401: the access token is no longer accepted.
 * @param hasRefreshToken A refresh token is stored, so a new pair of tokens can be bought.
 * @param renewedThisRun  A renewal has already been tried in this run.
 * @return True when the run carries on from where the 401 stopped it. One renewal per run whether
 *         it worked or not, so a sign-in that is gone for good costs one request rather than one
 *         per call.
 */
constexpr bool shouldRenewSignIn(bool unauthorised, bool hasRefreshToken, bool renewedThisRun)
{
    return unauthorised && hasRefreshToken && !renewedThisRun;
}

} // namespace lens::core::syncrun