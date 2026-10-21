#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

/**
 * @file text.h
 * @brief Small Qt-free text and stored-minute operations shared by Lens modules.
 */

namespace lens::util {

/**
 * @brief The format every stored minute uses, producer and parser alike.
 *
 * `history.minute` and `lastSyncedAt` are written in this form, and datePart() assumes it. A
 * parser that spells the format again can drift from the writers without either side failing
 * to compile, so the Qt spelling is declared here; localMinuteNow() produces it.
 */
inline constexpr char kMinuteFormat[] = "yyyy-MM-dd HH:mm";

/**
 * @brief Return the current local time in kMinuteFormat.
 * @return Sixteen characters, such as `2026-10-03 14:20`, in the machine's local time zone.
 */
std::string localMinuteNow();

/**
 * @brief Return the date portion of a stored local minute.
 * @param minute A timestamp in `YYYY-MM-DD HH:MM` form.
 * @return The first ten characters, or an empty string when the input is too short.
 */
std::string datePart(std::string_view minute);

/**
 * @brief Add a signed tally delta without allowing a negative count.
 * @param value Existing non-negative count.
 * @param delta Change to apply.
 * @return The changed count, clamped at zero.
 */
int clampedAdd(int value, int delta);

/// @return Whether @p c is an ASCII upper-case letter.
constexpr bool isAsciiUpper(char const c) noexcept
{
    return c >= 'A' and c <= 'Z';
}

/// @return Whether @p c is an ASCII lower-case letter.
constexpr bool isAsciiLower(char const c) noexcept
{
    return c >= 'a' and c <= 'z';
}

/// @return Whether @p c is an ASCII letter, A-Z or a-z.
constexpr bool isAsciiLetter(char const c) noexcept
{
    return isAsciiUpper(c) or isAsciiLower(c);
}

/// @return Whether @p c is an ASCII digit, 0-9.
constexpr bool isAsciiDigit(char const c) noexcept
{
    return c >= '0' and c <= '9';
}

/// @return Whether @p c is an ASCII letter or digit.
constexpr bool isAsciiAlnum(char const c) noexcept
{
    return isAsciiLetter(c) or isAsciiDigit(c);
}

/// @return Whether @p c is one of the six ASCII whitespace characters.
constexpr bool isAsciiSpace(char const c) noexcept
{
    return c == ' ' or c == '\t' or c == '\n' or c == '\r' or c == '\v' or c == '\f';
}

/// @return @p c folded to lower case when it is an ASCII upper-case letter, else @p c unchanged.
constexpr char asciiLower(char const c) noexcept
{
    return isAsciiUpper(c) ? static_cast<char>(c - 'A' + 'a') : c;
}

/**
 * @brief Fold the ASCII letters in @p text to lower case.
 * @param text Bytes to fold; every non-ASCII byte is left untouched.
 * @return A copy with A-Z replaced by a-z.
 */
std::string asciiLower(std::string_view text);

/**
 * @brief Decode UTF-8 into code points, strictly.
 *
 * @param text Bytes to decode.
 * @return One code point per character, or nothing when @p text is not well-formed: a stray
 *         continuation byte, a truncated sequence, an overlong form, a surrogate, or a point
 *         above U+10FFFF.
 */
std::optional<std::vector<char32_t>> decodeUtf8(std::string_view text);

}
