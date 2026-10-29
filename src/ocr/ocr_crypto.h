/** @file ocr_crypto.h
 * @brief HMAC and hashing for the two services that sign their requests.
 *
 * Qt has the digests but not HMAC, and the two signing schemes here (Tencent's TC3 uses
 * HMAC-SHA256, Aliyun's RPC style uses HMAC-SHA1) differ only in the digest, so one
 * implementation serves both.
 */
#pragma once

#include <QByteArray>
#include <QCryptographicHash>

#include <array>

namespace lens::ocr::detail {

inline QByteArray hmac(QCryptographicHash::Algorithm const algorithm, QByteArray const& key, QByteArray const& message)
{
    constexpr int                kBlockSize = 64;
    std::array<char, kBlockSize> block{};
    auto const                   effective = key.size() > kBlockSize
                                                 ? QCryptographicHash::hash(key, algorithm)
                                                 : key;
    for (int i = 0; i < effective.size(); ++i)
        block[static_cast<std::size_t>(i)] = effective[i];

    QByteArray inner(kBlockSize, '\0');
    QByteArray outer(kBlockSize, '\0');
    for (int i = 0; i < kBlockSize; ++i) {
        inner[i] = static_cast<char>(block[static_cast<std::size_t>(i)] ^ 0x36);
        outer[i] = static_cast<char>(block[static_cast<std::size_t>(i)] ^ 0x5c);
    }

    QCryptographicHash hash{algorithm};
    hash.addData(outer);
    hash.addData(QCryptographicHash::hash(inner + message, algorithm));
    return hash.result();
}

inline QByteArray hmacSha256(QByteArray const& key, QByteArray const& message)
{
    return hmac(QCryptographicHash::Sha256, key, message);
}

inline QByteArray hmacSha1(QByteArray const& key, QByteArray const& message)
{
    return hmac(QCryptographicHash::Sha1, key, message);
}

inline QByteArray sha256Hex(QByteArray const& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

}
