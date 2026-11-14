#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file vault.h
 * @brief The encryption the document's secrets ride on: a passphrase becomes a key and the key
 *        seals the cloud copy into an envelope that is safe to hand to GitHub, and the secrets that
 *        stay on the device are sealed to the reader's Windows account.
 *
 * PBKDF2-HMAC-SHA256 turns the passphrase into a 32-byte key, and AES-256-GCM seals the document.
 * Both come from the Windows CNG provider (BCrypt) that ships with the system, so no dependency is
 * added. The envelope is plain JSON: its salt, nonce and iteration count are not secret, and a new
 * device needs them to derive the same key. docs/adr/0042 names the format.
 *
 * The secrets the settings document holds at rest -- the sync tokens and the WebDAV app password --
 * go through DPAPI instead (docs/adr/0177): no passphrase to ask for, and the seal is bound to the
 * reader's Windows account. It defends the file at rest -- a backup, another account, a copy lifted
 * off the machine -- not the account itself; anything running as the reader can open it.
 *
 * Nothing here logs. A failure carries the error code only, never the passphrase, the key or the
 * plaintext.
 */

namespace lens::core::vault {

/// @brief Raw bytes; keys, salts, nonces and ciphertext all use this.
using Bytes = std::vector<unsigned char>;

/// @brief PBKDF2 iteration count for a new passphrase: the OWASP Password Storage Cheat Sheet's
///        figure for PBKDF2-HMAC-SHA256.
inline constexpr unsigned kIterations = 600000;

/// @brief The salt and iteration count a key was derived with. Both are written into the envelope.
struct KdfParams {
    Bytes    salt;
    unsigned iterations = kIterations;
};

/// @return Fresh parameters: a random 16-byte salt and kIterations.
/// @throws std::runtime_error If the system random source fails.
KdfParams newParams();

/**
 * @brief Derive the 32-byte AES key from a passphrase.
 *
 * @param passphrase The reader's passphrase, as UTF-8. Not stored anywhere by this function.
 * @param params     The salt and iteration count; the same pair always gives the same key.
 * @throws std::runtime_error If BCrypt refuses the parameters.
 */
Bytes deriveKey(std::string_view passphrase, const KdfParams& params);

/**
 * @brief Seal a document into an envelope.
 *
 * The nonce is random for every call, so sealing the same plaintext twice gives two envelopes. The
 * ciphertext carries the 16-byte GCM tag at its end.
 *
 * @param key       A key from deriveKey().
 * @param plaintext The document, as bytes of text.
 * @param params    The parameters @p key was derived with, written into the envelope.
 * @return The envelope object: format, kdf, iterations, salt, nonce and ciphertext.
 * @throws std::runtime_error If BCrypt fails.
 */
nlohmann::json seal(const Bytes& key, std::string_view plaintext, const KdfParams& params);

/**
 * @brief Open an envelope with a key.
 *
 * @param key      The key derived from the passphrase.
 * @param envelope An envelope as seal() wrote it, or whatever came back from the network.
 * @return The plaintext, or std::nullopt when the envelope is malformed, the key is the wrong
 *         size, or the tag does not verify (a wrong passphrase or a changed envelope). Never
 *         returns part of a plaintext.
 */
std::optional<std::string> open(const Bytes& key, const nlohmann::json& envelope);

/**
 * @brief The salt and iteration count an envelope was sealed with, so a new device can derive the key.
 * @return The parameters, or std::nullopt when the envelope is malformed.
 */
std::optional<KdfParams> paramsOf(const nlohmann::json& envelope);

/**
 * @brief Seal @p plaintext to the reader's Windows account with DPAPI.
 *
 * The blob opens only for the same account on the same machine, which is the scope the settings
 * document already has. The output is random for every call.
 *
 * @param plaintext The secret, as UTF-8.
 * @return The protected bytes, ready for encodeBase64().
 * @throws std::runtime_error If CryptProtectData fails.
 */
Bytes dpapiSeal(std::string_view plaintext);

/**
 * @brief Open bytes dpapiSeal() wrote.
 *
 * @param sealed The protected bytes.
 * @return The plaintext, or std::nullopt when this account cannot open them -- another account's
 *         seal, or bytes that were never sealed. Never returns part of a plaintext.
 */
std::optional<std::string> dpapiOpen(const Bytes& sealed);

/// @return @p bytes in standard base64, with padding.
std::string encodeBase64(const Bytes& bytes);

/**
 * @brief Read standard base64 back, strictly.
 *
 * @param text Base64 with padding. Whitespace and any character outside the alphabet are rejected.
 * @return The bytes, or std::nullopt for text that is not canonical base64.
 */
std::optional<Bytes> decodeBase64(std::string_view text);

}
