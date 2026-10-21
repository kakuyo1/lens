/**
 * @file text.cpp
 * @brief Implementations of Qt-free shared text and stored-minute operations.
 */

#include "util/text.h"

#include <algorithm>
#include <ctime>

namespace lens::util {

std::string localMinuteNow()
{
    auto const now = std::time(nullptr);
    std::tm    local{};
    localtime_s(&local, &now);

    char text[17];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M", &local);
    return text;
}

std::string datePart(std::string_view minute)
{
    constexpr std::size_t kDateLength = 10;
    if (minute.size() < kDateLength)
        return {};
    return std::string{minute.substr(0, kDateLength)};
}

int clampedAdd(int value, int delta)
{
    return std::max(0, value + delta);
}

std::string asciiLower(std::string_view text)
{
    std::string out{text};
    for (char& c : out)
        c = asciiLower(c);
    return out;
}

std::optional<std::vector<char32_t>> decodeUtf8(std::string_view text)
{
    auto points = std::vector<char32_t>{};
    points.reserve(text.size());

    for (std::size_t at = 0; at < text.size();) {
        auto const  lead  = static_cast<unsigned char>(text[at]);
        std::size_t width = 0;
        char32_t    point = 0;
        if (lead < 0x80) {
            point = lead;
            width = 1;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            point = lead & 0x1F;
            width = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            point = lead & 0x0F;
            width = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            point = lead & 0x07;
            width = 4;
        } else {
            return std::nullopt;
        }

        if (text.size() - at < width)
            return std::nullopt;
        for (std::size_t k = 1; k < width; ++k) {
            auto const continuation = static_cast<unsigned char>(text[at + k]);
            if ((continuation & 0xC0) != 0x80)
                return std::nullopt;
            point = (point << 6) | (continuation & 0x3F);
        }

        constexpr char32_t kFirstOfWidth[] = {0, 0, 0x80, 0x800, 0x10000};
        if (point < kFirstOfWidth[width] || (point >= 0xD800 && point <= 0xDFFF) || point > 0x10FFFF)
            return std::nullopt;
        points.push_back(point);
        at += width;
    }
    return points;
}

}
