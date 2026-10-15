#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

/**
 * @file pet_package.h
 * @brief The contract a pet package is checked against (docs/pet-package-format.md): the refusal reasons, the limits,
 *        and the checks that need neither the file system nor Qt.
 *
 * Refusal is all or nothing. Every check throws Refused with one of the reasons in Refusal, and that reason is the only
 * thing the reader is shown about a refused package; the message is for the log and may quote the package.
 */

namespace lens::core::pet {

/// @brief Why a pet package was refused. The names are the ones section 6 of the format document lists.
enum class Refusal : std::uint8_t {
    Unreadable,
    Format,
    TooLarge,
    BadName,
    MissingRole,
    BadAnimation,
    BadAnchors,
    BadAccessory,
    BadAssetType,
    SheetSize,
    ExpressionIndex,
    PathEscapes,
    IdTaken,
};

/// @brief The exception every package check throws: why the package was refused, and a message for the log.
class Refused final : public std::runtime_error {
public:
    Refused(Refusal reason, std::string const& message)
        : std::runtime_error(message), reason_(reason)
    {
    }

    /// @return Why the package was refused.
    Refusal reason() const noexcept
    {
        return reason_;
    }

private:
    Refusal reason_;
};

/// @name Limits from docs/pet-package-format.md section 4. A value above its maximum refuses the package as TooLarge; a
///       value below its minimum is a Format or BadAnimation refusal, depending on the field.
/// @{
constexpr int kMinCanvas      = 16;
constexpr int kMaxCanvas      = 128;
constexpr int kMinScale       = 1;
constexpr int kMaxScale       = 4;
constexpr int kMaxFrames      = 64;
constexpr int kMaxFps         = 24;
constexpr int kMaxActions     = 32;
constexpr int kMaxAccessories = 32;
constexpr int kMaxIdlePool    = 16;
/// Longest declared side of a sheet, width or height: 64 frames of 128 px is 8192, so a sheet of any valid pet fits.
constexpr int kMaxSheetSide       = 8192;
constexpr int kMaxExpressionCells = 64;
constexpr int kMaxNameChars       = 32;
/// Decoded pixels across every sheet of one package.
constexpr std::uint64_t kMaxPixels       = 16ull * 1024 * 1024;
constexpr std::uint64_t kMaxPngBytes     = 8ull * 1024 * 1024;
constexpr std::uint64_t kMaxJsonBytes    = 256ull * 1024;
constexpr std::uint64_t kMaxPackageBytes = 32ull * 1024 * 1024;
constexpr std::size_t   kMaxPackageFiles = 512;
/// @}

/**
 * @brief Whether a relative path is spelled so that it stays under its folder: no root, no drive, no UNC prefix, no `..`.
 *
 * This is the spelling half of the rule. The resolved location is checked against the folder by the file system, which
 * this file does not reach.
 *
 * @param path The path as the package wrote it.
 * @return True when it is relative and has no `..` segment.
 */
bool isContainedSpelling(std::string_view path);

/// @return Whether `id` is a pet id: one to 32 characters, each a-z, 0-9 or underscore.
bool isPetId(std::string_view id);

/**
 * @brief Whether a string the reader will see as text is short and plain: 1 to kMaxNameChars code points, none of them a
 *        control or format character (bidi overrides and zero-width characters included).
 * @param text UTF-8 text, such as an action name or an accessory id.
 * @return True when the text is acceptable to show.
 */
bool isPlainLabel(std::string_view text);

/// @brief The pixel size a PNG declares in its IHDR chunk.
struct PngSize {
    std::uint32_t width;
    std::uint32_t height;
};

/**
 * @brief Reads a PNG's declared size from its first bytes, without decoding anything.
 * @param head The first bytes of the file; 24 are enough. A shorter or non-PNG head gives nothing.
 * @return The declared size, or nothing when the signature and the IHDR chunk are not both there.
 */
std::optional<PngSize> readPngSize(std::string_view head);

} // namespace lens::core::pet
