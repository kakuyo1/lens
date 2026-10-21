/**
 * @file vault.cpp
 * @brief The BCrypt calls behind vault.h, and the strict base64 and envelope checks around them.
 */

#include "core/crypto/vault.h"

#include <windows.h>
#include <bcrypt.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace lens::core::vault {

namespace {

constexpr std::size_t kKeyBytes   = 32; ///< AES-256.
constexpr std::size_t kSaltBytes  = 16;
constexpr std::size_t kNonceBytes = 12; ///< GCM's standard nonce size.
constexpr std::size_t kTagBytes   = 16; ///< The full GCM tag.

/// STATUS_AUTH_TAG_MISMATCH, from ntstatus.h, which bcrypt.h does not include.
constexpr NTSTATUS kAuthTagMismatch = static_cast<NTSTATUS>(0xC000A002u);

/// Closes an algorithm provider when its handle goes out of scope.
struct AlgorithmCloser {
    void operator()(void* handle) const
    {
        BCryptCloseAlgorithmProvider(handle, 0);
    }
};
using AlgorithmHandle = std::unique_ptr<void, AlgorithmCloser>;

/// Destroys a key handle when it goes out of scope.
struct KeyCloser {
    void operator()(void* handle) const
    {
        BCryptDestroyKey(handle);
    }
};
using KeyHandle = std::unique_ptr<void, KeyCloser>;

/// An AES key and the provider it was made from. The key is destroyed first, then the provider.
struct GcmKey {
    AlgorithmHandle algorithm;
    KeyHandle       key;
};

/**
 * @brief Turn a failed NTSTATUS into an exception that names only the code.
 * @param status The value the BCrypt call returned.
 * @param what   The step that failed, for the message.
 * @throws std::runtime_error If @p status is not a success.
 */
void check(NTSTATUS status, const char* what)
{
    if (status >= 0) // NTSTATUS is a success for any non-negative value.
        return;
    char code[16];
    std::snprintf(code, sizeof code, "0x%08lX", static_cast<unsigned long>(status));
    throw std::runtime_error(std::string{"vault: BCrypt "} + what + " failed, NTSTATUS " + code);
}

/// @return @p count bytes from the system random source.
Bytes randomBytes(std::size_t count)
{
    Bytes out(count);
    check(BCryptGenRandom(nullptr, out.data(), static_cast<ULONG>(out.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG),
          "random");
    return out;
}

/// @return An algorithm provider for @p id, opened with @p flags.
AlgorithmHandle openAlgorithm(LPCWSTR id, ULONG flags)
{
    BCRYPT_ALG_HANDLE handle = nullptr;
    check(BCryptOpenAlgorithmProvider(&handle, id, nullptr, flags), "open algorithm");
    return AlgorithmHandle{handle};
}

/// @return AES in GCM mode, keyed with @p key, which must be 32 bytes.
GcmKey importGcmKey(const Bytes& key)
{
    GcmKey out{openAlgorithm(BCRYPT_AES_ALGORITHM, 0), KeyHandle{}};
    check(BCryptSetProperty(out.algorithm.get(), BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<PWSTR>(BCRYPT_CHAIN_MODE_GCM)), sizeof(BCRYPT_CHAIN_MODE_GCM), 0),
          "set GCM mode");

    BCRYPT_KEY_HANDLE handle = nullptr;
    check(BCryptGenerateSymmetricKey(out.algorithm.get(), &handle, nullptr, 0, const_cast<PUCHAR>(key.data()), static_cast<ULONG>(key.size()), 0),
          "import key");
    out.key.reset(handle);
    return out;
}

/// @return The GCM mode info for one call, pointing at @p nonce and at @p tag.
BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo(const Bytes& nonce, Bytes& tag)
{
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info{};
    info.cbSize        = sizeof info;
    info.dwInfoVersion = BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO_VERSION;
    info.pbNonce       = const_cast<PUCHAR>(nonce.data());
    info.cbNonce       = static_cast<ULONG>(nonce.size());
    info.pbTag         = tag.data();
    info.cbTag         = static_cast<ULONG>(tag.size());
    return info;
}

/// @return The ciphertext of @p plaintext; @p tag receives the tag that authenticates it.
Bytes encryptGcm(const Bytes& key, const Bytes& nonce, const Bytes& plaintext, Bytes& tag)
{
    const GcmKey gcm = importGcmKey(key);
    // ponytail: the buffer is never empty, so data() is never null for a zero-length message, which
    // the SDK documents nowhere. The length passed to CNG stays the true size.
    Bytes out(plaintext.size() + 1);
    tag.assign(kTagBytes, 0);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info    = authInfo(nonce, tag);
    ULONG                                 written = 0;
    check(BCryptEncrypt(gcm.key.get(), const_cast<PUCHAR>(plaintext.data()), static_cast<ULONG>(plaintext.size()), &info, nullptr, 0, out.data(), static_cast<ULONG>(out.size()), &written, 0),
          "encrypt");
    out.resize(plaintext.size());
    return out;
}

/// @return The plaintext, or std::nullopt when @p tag does not verify.
std::optional<Bytes> decryptGcm(const Bytes& key, const Bytes& nonce, const Bytes& ciphertext, Bytes tag)
{
    const GcmKey gcm = importGcmKey(key);
    // ponytail: same as encryptGcm, a non-empty buffer; resized to the true length below.
    Bytes                                 out(ciphertext.size() + 1);
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info    = authInfo(nonce, tag);
    ULONG                                 written = 0;
    const NTSTATUS                        status =
        BCryptDecrypt(gcm.key.get(), const_cast<PUCHAR>(ciphertext.data()), static_cast<ULONG>(ciphertext.size()), &info, nullptr, 0, out.data(), static_cast<ULONG>(out.size()), &written, 0);
    if (status == kAuthTagMismatch)
        return std::nullopt;
    check(status, "decrypt");
    out.resize(ciphertext.size());
    return out;
}

/// The fields of an envelope, once each one has been checked.
struct Envelope {
    KdfParams params;
    Bytes     nonce;
    Bytes     ciphertext; ///< The body with the tag appended.
};

/**
 * @brief Read and check every field of an envelope.
 *
 * The envelope may come from the network, so each field is checked for its type and its length
 * before anything trusts it. A field that is absent, of the wrong type, or the wrong size makes the
 * whole envelope malformed.
 *
 * @param envelope The envelope object.
 * @return The fields, or std::nullopt when any of them is not as seal() writes it.
 */
std::optional<Envelope> readEnvelope(const nlohmann::json& envelope)
{
    if (!envelope.is_object())
        return std::nullopt;

    const auto format     = envelope.find("format");
    const auto kdf        = envelope.find("kdf");
    const auto iterations = envelope.find("iterations");
    const auto salt       = envelope.find("salt");
    const auto nonce      = envelope.find("nonce");
    const auto ciphertext = envelope.find("ciphertext");
    const auto end        = envelope.end();

    if (format == end || !format->is_number_integer() || format->get<long long>() != 1)
        return std::nullopt;
    if (kdf == end || !kdf->is_string() || kdf->get_ref<const std::string&>() != "pbkdf2-sha256")
        return std::nullopt;
    if (iterations == end || !iterations->is_number_unsigned())
        return std::nullopt;
    const std::uint64_t count = iterations->get<std::uint64_t>();
    if (count == 0 || count > (std::numeric_limits<unsigned>::max)())
        return std::nullopt;
    if (salt == end || !salt->is_string() || nonce == end || !nonce->is_string() || ciphertext == end ||
        !ciphertext->is_string())
        return std::nullopt;

    auto saltBytes   = decodeBase64(salt->get_ref<const std::string&>());
    auto nonceBytes  = decodeBase64(nonce->get_ref<const std::string&>());
    auto cipherBytes = decodeBase64(ciphertext->get_ref<const std::string&>());
    if (!saltBytes || !nonceBytes || !cipherBytes)
        return std::nullopt;
    if (saltBytes->size() != kSaltBytes || nonceBytes->size() != kNonceBytes || cipherBytes->size() < kTagBytes)
        return std::nullopt;

    return Envelope{KdfParams{std::move(*saltBytes), static_cast<unsigned>(count)}, std::move(*nonceBytes), std::move(*cipherBytes)};
}

/// @return The 6-bit value of one base64 character, or -1 when it is outside the alphabet.
int sextet(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

} // namespace

KdfParams newParams()
{
    return KdfParams{randomBytes(kSaltBytes), kIterations};
}

Bytes deriveKey(std::string_view passphrase, const KdfParams& params)
{
    const AlgorithmHandle prf = openAlgorithm(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    Bytes                 key(kKeyBytes);
    // BCrypt reads the passphrase and never writes it; the parameter is non-const only by its signature.
    check(BCryptDeriveKeyPBKDF2(prf.get(), reinterpret_cast<PUCHAR>(const_cast<char*>(passphrase.data())), static_cast<ULONG>(passphrase.size()), const_cast<PUCHAR>(params.salt.data()), static_cast<ULONG>(params.salt.size()), params.iterations, key.data(), static_cast<ULONG>(key.size()), 0),
          "derive key");
    return key;
}

nlohmann::json seal(const Bytes& key, std::string_view plaintext, const KdfParams& params)
{
    const Bytes nonce = randomBytes(kNonceBytes);
    Bytes       message(plaintext.begin(), plaintext.end());
    Bytes       tag;
    Bytes       body = encryptGcm(key, nonce, message, tag);
    body.insert(body.end(), tag.begin(), tag.end());

    nlohmann::json envelope = nlohmann::json::object();
    envelope["format"]      = 1;
    envelope["kdf"]         = "pbkdf2-sha256";
    envelope["iterations"]  = params.iterations;
    envelope["salt"]        = encodeBase64(params.salt);
    envelope["nonce"]       = encodeBase64(nonce);
    envelope["ciphertext"]  = encodeBase64(body);
    return envelope;
}

std::optional<std::string> open(const Bytes& key, const nlohmann::json& envelope)
{
    const auto parsed = readEnvelope(envelope);
    if (!parsed || key.size() != kKeyBytes)
        return std::nullopt;

    const auto  split = parsed->ciphertext.end() - static_cast<std::ptrdiff_t>(kTagBytes);
    const Bytes body(parsed->ciphertext.begin(), split);
    const Bytes tag(split, parsed->ciphertext.end());
    const auto  plain = decryptGcm(key, parsed->nonce, body, tag);
    if (!plain)
        return std::nullopt;
    return std::string(plain->begin(), plain->end());
}

std::optional<KdfParams> paramsOf(const nlohmann::json& envelope)
{
    const auto parsed = readEnvelope(envelope);
    if (!parsed)
        return std::nullopt;
    return parsed->params;
}

std::string encodeBase64(const Bytes& bytes)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string           out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const std::size_t left  = bytes.size() - i;
        const unsigned    chunk = (static_cast<unsigned>(bytes[i]) << 16) |
                               (left > 1 ? static_cast<unsigned>(bytes[i + 1]) << 8 : 0u) |
                               (left > 2 ? static_cast<unsigned>(bytes[i + 2]) : 0u);
        out += kAlphabet[(chunk >> 18) & 0x3F];
        out += kAlphabet[(chunk >> 12) & 0x3F];
        out += left > 1 ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
        out += left > 2 ? kAlphabet[chunk & 0x3F] : '=';
    }
    return out;
}

std::optional<Bytes> decodeBase64(std::string_view text)
{
    if (text.size() % 4 != 0)
        return std::nullopt;

    Bytes out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool last      = i + 4 == text.size();
        int        values[4] = {0, 0, 0, 0};
        int        padding   = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            const char c = text[i + j];
            if (c == '=') {
                // Padding ends the text, and never starts in the first two places of a quartet.
                if (!last || j < 2)
                    return std::nullopt;
                ++padding;
            } else {
                // Data after padding is malformed.
                if (padding > 0)
                    return std::nullopt;
                values[j] = sextet(c);
                if (values[j] < 0)
                    return std::nullopt;
            }
        }
        out.push_back(static_cast<unsigned char>((values[0] << 2) | (values[1] >> 4)));
        if (padding < 2)
            out.push_back(static_cast<unsigned char>(((values[1] & 0xF) << 4) | (values[2] >> 2)));
        if (padding < 1)
            out.push_back(static_cast<unsigned char>(((values[2] & 0x3) << 6) | values[3]));
    }
    return out;
}

}
