/**
 * @file glass_math.cpp
 * @brief Text tone and displacement map for the glass themes (see glass_math.h).
 */

#include "glass_math.h"

#include <QColor>
#include <QPainter>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lens::app {

namespace {

/// The liquid theme's default text, #101319.
constexpr QRgb kDarkText{qRgb(0x10, 0x13, 0x19)};
/// The dark theme's body text, #e9edf3.
constexpr QRgb kLightText{qRgb(0xe9, 0xed, 0xf3)};
constexpr int  kSampleStride = 4;

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

/// One separable box-blur pass over a w-by-h field, clamped at the edges, in place.
void boxBlur(std::vector<double>& field, int w, int h, int radius)
{
    std::vector<double> out(field.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double sum = 0.0;
            for (int k = -radius; k <= radius; ++k)
                sum += field[static_cast<std::size_t>(y) * w + std::clamp(x + k, 0, w - 1)];
            out[static_cast<std::size_t>(y) * w + x] = sum / (2 * radius + 1);
        }
    field.swap(out);
    out.assign(field.size(), 0.0);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double sum = 0.0;
            for (int k = -radius; k <= radius; ++k)
                sum += field[static_cast<std::size_t>(std::clamp(y + k, 0, h - 1)) * w + x];
            out[static_cast<std::size_t>(y) * w + x] = sum / (2 * radius + 1);
        }
    field.swap(out);
}

} // namespace

bool prefersLightText(QImage const& backdrop, double const fillAlpha)
{
    if (backdrop.isNull()) return false;

    auto const darkLuminance  = luminance(qRed(kDarkText), qGreen(kDarkText), qBlue(kDarkText));
    auto const lightLuminance = luminance(qRed(kLightText), qGreen(kLightText), qBlue(kLightText));

    std::vector<double> againstDark;
    std::vector<double> againstLight;
    for (int y = 0; y < backdrop.height(); y += kSampleStride)
        for (int x = 0; x < backdrop.width(); x += kSampleStride) {
            auto const pixel = QColor(backdrop.pixel(x, y));
            // The card's white fill lies over the picture, so the text stands on the composite.
            auto const composite = luminance(fillAlpha * 255.0 + (1.0 - fillAlpha) * pixel.red(),
                                             fillAlpha * 255.0 + (1.0 - fillAlpha) * pixel.green(),
                                             fillAlpha * 255.0 + (1.0 - fillAlpha) * pixel.blue());
            againstDark.push_back(contrast(composite, darkLuminance));
            againstLight.push_back(contrast(composite, lightLuminance));
        }
    if (againstDark.empty()) return false;
    return tenthPercentile(againstLight) > tenthPercentile(againstDark);
}

QImage displacementMap(QSize const size, double const radius, double const blurPx)
{
    int const w = size.width();
    int const h = size.height();
    if (w <= 0 or h <= 0) return {};

    QImage mask(w, h, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawRoundedRect(QRectF(0, 0, w, h), radius, radius);
    }

    std::vector<double> alpha(static_cast<std::size_t>(w) * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            alpha[static_cast<std::size_t>(y) * w + x] = qAlpha(mask.pixel(x, y)) / 255.0;

    // Three box passes approximate a Gaussian of the requested softness.
    auto const boxRadius = std::max(1, static_cast<int>(std::lround(blurPx / 2.0)));
    for (int pass = 0; pass < 3; ++pass)
        boxBlur(alpha, w, h, boxRadius);

    auto const at = [&](int x, int y) {
        return alpha[static_cast<std::size_t>(std::clamp(y, 0, h - 1)) * w + std::clamp(x, 0, w - 1)];
    };
    // The mask rises toward the centre, so a central difference is the inward gradient, and the
    // offset points inward: edge pixels borrow content from inside the glass.
    std::vector<double> gx(alpha.size());
    std::vector<double> gy(alpha.size());
    double peak = 0.0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto const i = static_cast<std::size_t>(y) * w + x;
            gx[i]       = (at(x + 1, y) - at(x - 1, y)) / 2.0;
            gy[i]       = (at(x, y + 1) - at(x, y - 1)) / 2.0;
            peak        = std::max(peak, std::hypot(gx[i], gy[i]));
        }

    QImage map(w, h, QImage::Format_ARGB32);
    auto const encode = [peak](double component) {
        if (peak <= 0.0) return 128;
        return static_cast<int>(std::lround(std::clamp(128.0 + 127.0 * component / peak, 0.0, 255.0)));
    };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            auto const i = static_cast<std::size_t>(y) * w + x;
            map.setPixel(x, y, qRgba(encode(gx[i]), encode(gy[i]), 128, 255));
        }
    return map;
}

} // namespace lens::app
