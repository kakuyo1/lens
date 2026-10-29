/**
 * @file ocr_crypto_test.cpp
 * @brief HMAC and digest vectors from the RFCs the two signing schemes rely on.
 */
#include <gtest/gtest.h>

#include "ocr/ocr_crypto.h"

namespace {

using namespace lens::ocr;

TEST(OcrCrypto, HmacSha256MatchesRfc4231)
{
    EXPECT_EQ(detail::hmacSha256(QByteArrayLiteral("Jefe"), QByteArrayLiteral("what do ya want for nothing?")).toHex(),
              QByteArrayLiteral("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
}

TEST(OcrCrypto, HmacSha1MatchesRfc2202)
{
    EXPECT_EQ(detail::hmacSha1(QByteArrayLiteral("Jefe"), QByteArrayLiteral("what do ya want for nothing?")).toHex(),
              QByteArrayLiteral("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));
}

/// A key longer than the 64-byte block is hashed first; the RFC's case 6 pins that branch.
TEST(OcrCrypto, HmacSha256HashesAnOversizedKey)
{
    QByteArray const key(131, '\xaa');
    EXPECT_EQ(detail::hmacSha256(key, QByteArrayLiteral("Test Using Larger Than Block-Size Key - Hash Key First")).toHex(),
              QByteArrayLiteral("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
}

TEST(OcrCrypto, Sha256HexIsLowercase)
{
    EXPECT_EQ(detail::sha256Hex(QByteArrayLiteral("abc")),
              QByteArrayLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

}
