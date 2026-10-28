/**
 * @file hash.cpp
 * @brief The one BCrypt digest behind hash.h.
 */

#include "core/crypto/hash.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <memory>
#include <vector>

namespace lens::core::crypto {
namespace {

struct AlgorithmCloser {
    void operator()(void* handle) const
    {
        BCryptCloseAlgorithmProvider(handle, 0);
    }
};
using AlgorithmHandle = std::unique_ptr<void, AlgorithmCloser>;

struct HashCloser {
    void operator()(void* handle) const
    {
        BCryptDestroyHash(handle);
    }
};
using HashHandle = std::unique_ptr<void, HashCloser>;

constexpr std::size_t kDigestBytes = 20; ///< SHA-1's own length.

} // namespace

std::string sha1Hex(std::string_view bytes)
{
    AlgorithmHandle algorithm;
    {
        BCRYPT_ALG_HANDLE opened = nullptr;
        if (BCryptOpenAlgorithmProvider(&opened, BCRYPT_SHA1_ALGORITHM, nullptr, 0) < 0)
            return {};
        algorithm.reset(opened);
    }

    // The provider's object and digest sizes are properties, not constants of the algorithm: a
    // provider is allowed to answer a different pair, so both are asked for rather than assumed.
    DWORD objectBytes = 0;
    DWORD written     = 0;
    if (BCryptGetProperty(algorithm.get(), BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes), &written, 0) < 0)
        return {};
    DWORD digestBytes = 0;
    if (BCryptGetProperty(algorithm.get(), BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&digestBytes), sizeof(digestBytes), &written, 0) < 0)
        return {};
    if (digestBytes == 0 or digestBytes > 64)
        return {};

    std::array<unsigned char, 64> digest{};
    // The provider's own working state, which is larger than any digest it produces: SHA-1's is
    // a few hundred bytes on CNG, so it is allocated to the size asked for rather than guessed.
    std::vector<unsigned char> object(objectBytes);
    if (objectBytes == 0)
        return {};
    BCRYPT_HASH_HANDLE hashing = nullptr;
    if (BCryptCreateHash(algorithm.get(), &hashing, object.data(), objectBytes, nullptr, 0, 0) < 0)
        return {};
    HashHandle hash{hashing};

    if (not bytes.empty() and BCryptHashData(hash.get(),
                                             reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                             static_cast<ULONG>(bytes.size()),
                                             0) < 0)
        return {};

    if (BCryptFinishHash(hash.get(), digest.data(), digestBytes, 0) < 0)
        return {};

    static constexpr char kHex[] = "0123456789abcdef";
    std::string           out;
    out.reserve(digestBytes * 2);
    for (DWORD index = 0; index < digestBytes; ++index) {
        out.push_back(kHex[digest[index] >> 4]);
        out.push_back(kHex[digest[index] & 0x0f]);
    }
    return out;
}

} // namespace lens::core::crypto
