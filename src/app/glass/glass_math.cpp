/**
 * @file glass_math.cpp
 * @brief Text tone and the displacement map (see glass_math.h).
 */

#include "glass_math.h"

#include <QPainter>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace lens::app {

namespace {

/// The liquid theme's default text, #101319.
constexpr QRgb kDarkText{qRgb(0x10, 0x13, 0x19)};
/// The dark theme's body text, #e9edf3.
constexpr QRgb kLightText{qRgb(0xe9, 0xed, 0xf3)};
constexpr int  kSampleStride = 4;
/// The softness, in device-independent pixels, of the mask the refraction's offsets are read from.
constexpr double kMapSoftness = 18.0;

/// A w-by-h field. Single precision is plenty for 8-bit output and halves the memory the blur walks.
using Field = std::vector<float>;

/// WCAG relative luminance of an sRGB channel value in [0, 255].
double linearChannel(double value)
{
    auto const c = value / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(double red, double green, double blue)
{
    return 0.2126 * linearChannel(red) + 0.7152 * linearChannel(green) + 0.0722 * linearChannel(blue);
}

double contrast(double first, double second)
{
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}

double tenthPercentile(std::vector<double>& values)
{
    auto const index = values.size() / 10;
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

/// One separable box-blur pass over a w-by-h field, clamped at the edges, in place. Running sums keep the cost
/// independent of the radius. The vertical pass adds whole rows, so memory is walked in order.
void boxBlur(Field& field, int w, int h, int radius)
{
    auto const span  = 1.0f / static_cast<float>(2 * radius + 1);
    auto const width = static_cast<std::size_t>(w);
    auto const reach = static_cast<std::size_t>(radius);
    Field      rows(field.size());

    // Each row is padded with its edge values, so the running sum never clamps an index. The spare element lets the
    // update after the last output read in range.
    Field line(width + 2 * reach + 1);
    for (int y = 0; y < h; ++y) {
        auto const* src = field.data() + static_cast<std::size_t>(y) * width;
        std::fill(line.begin(), line.begin() + radius, src[0]);
        std::copy(src, src + w, line.begin() + radius);
        std::fill(line.begin() + radius + w, line.end(), src[w - 1]);
        float sum = 0.0f;
        for (std::size_t k = 0; k <= 2 * reach; ++k)
            sum += line[k];
        auto* dst = rows.data() + static_cast<std::size_t>(y) * width;
        for (std::size_t x = 0; x < width; ++x) {
            dst[x] = sum * span;
            sum += line[x + 2 * reach + 1] - line[x];
        }
    }

    auto const row = [&](int y) { return rows.data() + static_cast<std::size_t>(std::clamp(y, 0, h - 1)) * width; };
    Field      sums(width, 0.0f);
    for (int k = -radius; k <= radius; ++k) {
        auto const* src = row(k);
        for (std::size_t x = 0; x < width; ++x)
            sums[x] += src[x];
    }
    for (int y = 0; y < h; ++y) {
        auto* dst = field.data() + static_cast<std::size_t>(y) * width;
        for (std::size_t x = 0; x < width; ++x)
            dst[x] = sums[x] * span;
        auto const* add = row(y + radius + 1);
        auto const* sub = row(y - radius);
        for (std::size_t x = 0; x < width; ++x)
            sums[x] += add[x] - sub[x];
    }
}

/// The card's coverage, 0 to 1: the rounded rectangle of @p size, antialiased along its edge.
Field roundedCoverage(QSize const size, double const radius)
{
    QImage mask(size, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(0, 0, size.width(), size.height()), radius, radius);
    }
    Field alpha(static_cast<std::size_t>(size.width()) * static_cast<std::size_t>(size.height()));
    for (int y = 0; y < size.height(); ++y) {
        auto const* row = reinterpret_cast<QRgb const*>(mask.constScanLine(y));
        for (int x = 0; x < size.width(); ++x)
            alpha[static_cast<std::size_t>(y) * size.width() + x] = static_cast<float>(qAlpha(row[x])) / 255.0f;
    }
    return alpha;
}

} // namespace

ToneRead readTone(QImage const& backdrop, double const fillAlpha)
{
    return readTone(backdrop, backdrop.rect(), fillAlpha);
}

ToneRead readTone(QImage const& backdrop, QRect const& region, double const fillAlpha)
{
    if (backdrop.isNull()) return {};

    auto const area = region.intersected(backdrop.rect());
    if (area.isEmpty()) return {};

    auto const darkLuminance  = luminance(qRed(kDarkText), qGreen(kDarkText), qBlue(kDarkText));
    auto const lightLuminance = luminance(qRed(kLightText), qGreen(kLightText), qBlue(kLightText));

    auto const          columns = static_cast<std::size_t>((area.width() + kSampleStride - 1) / kSampleStride);
    auto const          rows    = static_cast<std::size_t>((area.height() + kSampleStride - 1) / kSampleStride);
    std::vector<double> againstDark;
    std::vector<double> againstLight;
    againstDark.reserve(columns * rows);
    againstLight.reserve(columns * rows);
    // The sheet the live path samples is 32-bit, so its rows are read in place rather than through QImage::pixel(),
    // whose per-pixel format dispatch and QColor construction were a measurable part of a 144 Hz frame.
    auto const packed = backdrop.depth() == 32;
    for (int y = area.top(); y <= area.bottom(); y += kSampleStride) {
        auto const* const row = packed ? reinterpret_cast<QRgb const*>(backdrop.constScanLine(y)) : nullptr;
        for (int x = area.left(); x <= area.right(); x += kSampleStride) {
            auto const pixel = packed ? row[x] : backdrop.pixel(x, y);
            // The card's white fill lies over the picture, so the text stands on the composite.
            auto const composite = luminance(fillAlpha * 255.0 + (1.0 - fillAlpha) * qRed(pixel),
                                             fillAlpha * 255.0 + (1.0 - fillAlpha) * qGreen(pixel),
                                             fillAlpha * 255.0 + (1.0 - fillAlpha) * qBlue(pixel));
            againstDark.push_back(contrast(composite, darkLuminance));
            againstLight.push_back(contrast(composite, lightLuminance));
        }
    }
    if (againstDark.empty()) return {};
    return {tenthPercentile(againstDark), tenthPercentile(againstLight)};
}

bool settleTone(ToneRead const& read, bool const settledLight, int& streak, int const hold)
{
    auto const challenger = settledLight ? read.againstDark : read.againstLight;
    auto const settled    = settledLight ? read.againstLight : read.againstDark;
    // The other tone has to read decisively better before it even counts as asking for a change…
    if (challenger <= settled * kToneMargin) {
        streak = 0;
        return settledLight;
    }
    // …and then to ask on `hold` reads running, so a sample that swings cannot flip the tone on alternate reads.
    if (++streak < hold) return settledLight;
    streak = 0;
    return !settledLight;
}

QImage displacementMap(QSize const size, double const radius, double const blurPx)
{
    int const w = size.width();
    int const h = size.height();
    if (w <= 0 or h <= 0) return {};

    auto alpha = roundedCoverage(size, radius);

    // Three box passes approximate a Gaussian of the requested softness.
    auto const boxRadius = std::max(1, static_cast<int>(std::lround(blurPx / 2.0)));
    for (int pass = 0; pass < 3; ++pass)
        boxBlur(alpha, w, h, boxRadius);

    auto const at = [&](int x, int y) {
        return alpha[static_cast<std::size_t>(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)];
    };
    // The mask rises toward the centre, so a central difference is the inward gradient, and the
    // offset points inward: edge pixels borrow content from inside the glass.
    Field gx(alpha.size());
    Field gy(alpha.size());
    float peak = 0.0f;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto const i = static_cast<std::size_t>(y) * w + x;
            gx[i]        = (at(x + 1, y) - at(x - 1, y)) / 2.0f;
            gy[i]        = (at(x, y + 1) - at(x, y - 1)) / 2.0f;
            peak         = std::max(peak, std::hypot(gx[i], gy[i]));
        }

    QImage     map(w, h, QImage::Format_ARGB32);
    auto const encode = [peak](float component) {
        if (peak <= 0.0f) return 128;
        return static_cast<int>(std::lround(std::clamp(128.0f + 127.0f * component / peak, 0.0f, 255.0f)));
    };
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<QRgb*>(map.scanLine(y));
        for (int x = 0; x < w; ++x) {
            auto const i = static_cast<std::size_t>(y) * w + x;
            row[x]       = qRgba(encode(gx[i]), encode(gy[i]), 128, 255);
        }
    }
    return map;
}

QImage refractionMap(QSize const size, double const radius, double const scale)
{
    return displacementMap(size, radius * scale, kMapSoftness * scale);
}

QRect toNativeRect(QRect const logical, QRect const screenLogical, QPoint const nativeOrigin, double const scale)
{
    auto const offset = logical.topLeft() - screenLogical.topLeft();
    auto const corner = nativeOrigin + QPoint(qRound(offset.x() * scale), qRound(offset.y() * scale));
    return QRect(corner, QSize(qRound(logical.width() * scale), qRound(logical.height() * scale)));
}

std::optional<std::size_t> nextLive(std::vector<bool> const& live, std::size_t const after)
{
    auto const count = live.size();
    for (auto step = std::size_t{1}; step <= count; ++step) {
        auto const index = (after + step) % count;
        if (live[index]) return index;
    }
    return std::nullopt;
}

quint64 pictureFingerprint(QImage const& picture)
{
    if (picture.isNull()) return 0;
    // Smooth rather than fast: averaging is the point, since one changed pixel should not count.
    auto const small = picture.scaled(kFingerprintSide, kFingerprintSide, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_Grayscale8);
    // Five bits a channel, not eight: the last three are below what the blur leaves visible, and keeping them
    // would make a caret or an antialiased edge count as a changed desktop.
    quint64 hash = 1469598103934665603ULL;
    for (int y = 0; y < small.height(); ++y) {
        auto const* row = small.constScanLine(y);
        for (int x = 0; x < small.width(); ++x) {
            hash ^= static_cast<quint64>(row[x] >> 3);
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

} // namespace lens::app
