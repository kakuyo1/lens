/**
 * @file pet_package.cpp
 * @brief Implementation of the package checks that need neither the file system nor Qt.
 */

#include "core/pet/pet_package.h"
#include "util/text.h"

#include <algorithm>
#include <array>
#include <utility>

namespace lens::core::pet {

namespace {

/// The eight bytes every PNG starts with.
constexpr std::string_view kPngSignature{"\x89PNG\r\n\x1a\n", 8};

/// Longest pet id, in characters; the name limit happens to be the same number, but they are separate rules.
constexpr std::size_t kMaxIdChars = 32;

/// @return Whether a code point is a control (Cc) or a format character (Cf), the two classes a reader sees as nothing:
///         the C0 and C1 controls, and the Unicode 15 format characters, which include the soft hyphen, the Arabic
///         format marks, the zero-width and bidi-control runs, the word joiner, the byte-order mark and the tag characters.
bool isControlOrFormat(char32_t point)
{
    constexpr std::array<std::pair<char32_t, char32_t>, 22> kRanges{{
        {0x0000, 0x001F},
        {0x007F, 0x009F},
        {0x00AD, 0x00AD},
        {0x0600, 0x0605},
        {0x061C, 0x061C},
        {0x06DD, 0x06DD},
        {0x070F, 0x070F},
        {0x0890, 0x0891},
        {0x08E2, 0x08E2},
        {0x180E, 0x180E},
        {0x200B, 0x200F},
        {0x202A, 0x202E},
        {0x2060, 0x206F},
        {0xFEFF, 0xFEFF},
        {0xFFF9, 0xFFFB},
        {0x110BD, 0x110BD},
        {0x110CD, 0x110CD},
        {0x13430, 0x1343F},
        {0x1BCA0, 0x1BCA3},
        {0x1D173, 0x1D17A},
        {0xE0001, 0xE0001},
        {0xE0020, 0xE007F},
    }};
    return std::any_of(kRanges.begin(), kRanges.end(), [point](auto const& range) { return point >= range.first and point <= range.second; });
}

} // namespace

bool isContainedSpelling(std::string_view path)
{
    if (path.empty()) return false;
    // A leading slash is an absolute path or a UNC share, and a colon is a drive letter, in either spelling of the root.
    if (path.front() == '/' or path.front() == '\\') return false;
    if (path.find(':') != std::string_view::npos) return false;
    if (path.find('\0') != std::string_view::npos) return false;

    auto segment = std::size_t{0};
    while (segment <= path.size()) {
        auto const end  = path.find_first_of("/\\", segment);
        auto const size = (end == std::string_view::npos ? path.size() : end) - segment;
        if (path.substr(segment, size) == "..") return false;
        if (end == std::string_view::npos) break;
        segment = end + 1;
    }
    return true;
}

bool isPetId(std::string_view id)
{
    if (id.empty() or id.size() > kMaxIdChars) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return util::isAsciiLower(c) or util::isAsciiDigit(c) or c == '_'; });
}

bool isPlainLabel(std::string_view text)
{
    auto const points = util::decodeUtf8(text);
    if (not points or points->empty() or static_cast<int>(points->size()) > kMaxNameChars) return false;
    return std::none_of(points->begin(), points->end(), isControlOrFormat);
}

std::optional<PngSize> readPngSize(std::string_view head)
{
    // The IHDR chunk follows the signature: four bytes of length, the type "IHDR", then width and height, both big-endian.
    constexpr std::size_t kHeaderEnd = 24;
    if (head.size() < kHeaderEnd or head.substr(0, kPngSignature.size()) != kPngSignature) return std::nullopt;
    if (head.substr(12, 4) != "IHDR") return std::nullopt;

    auto const be32 = [head](std::size_t at) {
        return (static_cast<std::uint32_t>(static_cast<unsigned char>(head[at])) << 24) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(head[at + 1])) << 16) |
               (static_cast<std::uint32_t>(static_cast<unsigned char>(head[at + 2])) << 8) |
               static_cast<std::uint32_t>(static_cast<unsigned char>(head[at + 3]));
    };
    auto const size = PngSize{be32(16), be32(20)};
    if (size.width == 0 or size.height == 0) return std::nullopt;
    return size;
}

} // namespace lens::core::pet
