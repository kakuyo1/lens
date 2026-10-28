#pragma once

#include <string>
#include <string_view>

/**
 * @file hash.h
 * @brief The one digest the application needs outside the vault: a name for a cached file.
 *
 * SHA-1 is not a security boundary here and is not used as one. API.md section 9 fixes the audio
 * cache's file name as `sha1(accent|word)`, so the digest is a naming function: it turns a word the
 * reader looked at -- which may be any text at all -- into a name a file system will take, and it
 * keeps two accents' recordings of one word apart. Nothing is authenticated with it.
 *
 * Like the vault, the digest comes from the Windows CNG provider, so core stays free of Qt and the
 * build gains no dependency.
 */

namespace lens::core::crypto {

/**
 * @brief SHA-1 of @p bytes, as 40 lowercase hex digits.
 * @param bytes The text or bytes to name.
 * @return The digest in hex. A failure inside CNG answers an empty string rather than throwing:
 *         the caller is naming a cache file, and a name it cannot make is a cache miss.
 */
std::string sha1Hex(std::string_view bytes);

}
