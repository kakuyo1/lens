/**
 * @file vault_test.cpp
 * @brief Offline tests for the passphrase key, the sealed envelope, and the base64 it is written in.
 */

#include <gtest/gtest.h>

#include "core/crypto/vault.h"

namespace vault = lens::core::vault;

using vault::Bytes;
using vault::KdfParams;

namespace {

/// Parameters with a fresh salt and a small iteration count, so a case runs in milliseconds.
KdfParams quickParams()
{
    KdfParams params  = vault::newParams();
    params.iterations = 1000;
    return params;
}

/// @return @p envelope with the first byte of one base64 field flipped.
nlohmann::json withFlippedByte(nlohmann::json envelope, const char* field)
{
    Bytes flipped = vault::decodeBase64(envelope[field].get<std::string>()).value();
    flipped[0] ^= 0x01;
    envelope[field] = vault::encodeBase64(flipped);
    return envelope;
}

} // namespace

TEST(Vault, RoundTripsAPlaintextUnderTheSamePassphrase)
{
    const KdfParams params = quickParams();
    const Bytes     key    = vault::deriveKey("correct horse", params);
    const auto      sealed = vault::seal(key, R"({"known":{"panel":true}})", params);

    const auto opened = vault::open(key, sealed);
    ASSERT_TRUE(opened.has_value());
    EXPECT_EQ(*opened, R"({"known":{"panel":true}})");
}

TEST(Vault, AWrongPassphraseOpensToNothing)
{
    const KdfParams params = quickParams();
    const auto      sealed = vault::seal(vault::deriveKey("correct horse", params), "secret", params);

    EXPECT_FALSE(vault::open(vault::deriveKey("battery staple", params), sealed).has_value());
}

TEST(Vault, AFlippedCiphertextByteOpensToNothing)
{
    const KdfParams params = quickParams();
    const Bytes     key    = vault::deriveKey("correct horse", params);
    const auto      sealed = vault::seal(key, "secret", params);

    EXPECT_FALSE(vault::open(key, withFlippedByte(sealed, "ciphertext")).has_value());
}

TEST(Vault, AChangedNonceOpensToNothing)
{
    const KdfParams params = quickParams();
    const Bytes     key    = vault::deriveKey("correct horse", params);
    const auto      sealed = vault::seal(key, "secret", params);

    EXPECT_FALSE(vault::open(key, withFlippedByte(sealed, "nonce")).has_value());
}

TEST(Vault, SealingTheSamePlaintextTwiceGivesDifferentEnvelopes)
{
    const KdfParams params = quickParams();
    const Bytes     key    = vault::deriveKey("correct horse", params);
    const auto      first  = vault::seal(key, "secret", params);
    const auto      second = vault::seal(key, "secret", params);

    EXPECT_NE(first["nonce"], second["nonce"]);
    EXPECT_NE(first["ciphertext"], second["ciphertext"]);
}

TEST(Vault, AMalformedEnvelopeOpensToNothing)
{
    const KdfParams params = quickParams();
    const Bytes     key    = vault::deriveKey("correct horse", params);
    const auto      sealed = vault::seal(key, "secret", params);

    EXPECT_FALSE(vault::open(key, nlohmann::json::object()).has_value());

    auto wrongFormat      = sealed;
    wrongFormat["format"] = 2;
    EXPECT_FALSE(vault::open(key, wrongFormat).has_value());

    auto shortCiphertext          = sealed;
    shortCiphertext["ciphertext"] = vault::encodeBase64(Bytes(8, 0x00));
    EXPECT_FALSE(vault::open(key, shortCiphertext).has_value());

    EXPECT_FALSE(vault::open(Bytes(16, 0x00), sealed).has_value());
}

TEST(Vault, Base64RoundTripsEveryLength)
{
    for (std::size_t length = 0; length < 6; ++length) {
        Bytes bytes(length);
        for (std::size_t i = 0; i < length; ++i)
            bytes[i] = static_cast<unsigned char>(i * 37 + 11);

        const auto decoded = vault::decodeBase64(vault::encodeBase64(bytes));
        ASSERT_TRUE(decoded.has_value()) << "length " << length;
        EXPECT_EQ(*decoded, bytes) << "length " << length;
    }
}

TEST(Vault, Base64MatchesTheStandardEncoding)
{
    EXPECT_EQ(vault::encodeBase64(Bytes{}), "");
    EXPECT_EQ(vault::encodeBase64(Bytes{0x66}), "Zg==");
    EXPECT_EQ(vault::encodeBase64(Bytes{0x66, 0x6f}), "Zm8=");
    EXPECT_EQ(vault::encodeBase64(Bytes{0x66, 0x6f, 0x6f}), "Zm9v");
}

TEST(Vault, Base64RejectsTextThatIsNotCanonical)
{
    for (const char* text : {"Zg=", "Z===", "Zg==Zg==", "Zm9v!", "Zm9", "Zg=A", "Zm 9"}) {
        EXPECT_FALSE(vault::decodeBase64(text).has_value()) << text;
    }
}

TEST(Vault, ParamsRoundTripThroughTheEnvelope)
{
    KdfParams params  = quickParams();
    params.iterations = 1234;
    const auto sealed = vault::seal(vault::deriveKey("correct horse", params), "secret", params);

    const auto back = vault::paramsOf(sealed);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(back->salt, params.salt);
    EXPECT_EQ(back->iterations, 1234u);

    EXPECT_FALSE(vault::paramsOf(nlohmann::json::object()).has_value());
}

TEST(Vault, DerivedKeyDependsOnThePassphraseAndTheSalt)
{
    const KdfParams params = quickParams();
    const Bytes     key    = vault::deriveKey("correct horse", params);

    EXPECT_EQ(key.size(), 32u);
    EXPECT_EQ(key, vault::deriveKey("correct horse", params));
    EXPECT_NE(key, vault::deriveKey("battery staple", params));

    KdfParams otherSalt = params;
    otherSalt.salt[0] ^= 0x01;
    EXPECT_NE(key, vault::deriveKey("correct horse", otherSalt));
}

TEST(Vault, DefaultIterationsDeriveAKey)
{
    const KdfParams params = vault::newParams();

    EXPECT_EQ(params.salt.size(), 16u);
    EXPECT_EQ(params.iterations, vault::kIterations);
    EXPECT_EQ(vault::deriveKey("correct horse", params).size(), 32u);
}
