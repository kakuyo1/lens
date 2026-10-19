/**
 * @file glass_desktop.cpp
 * @brief The GDI read behind every glass sheet (see glass_desktop.h).
 */

#include "glass_desktop.h"

#include <QImage>

#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#endif

namespace lens::app {

QImage readDesktopRegion(QRect const native, SheetScale const scale, bool const onDesktop)
{
    auto const step = static_cast<int>(scale) < 1 ? 1 : static_cast<int>(scale);
    auto const size = QSize(qMax(1, native.width() / step), qMax(1, native.height() / step));
    if (not onDesktop) {
        // A test platform has no desktop to read, and must not read the real one: a black sheet stands in for it.
        auto black = QImage(size, QImage::Format_RGB32);
        black.fill(Qt::black);
        return black;
    }
#ifdef _WIN32
    // GDI reads what is on the screen. While the glass shows, the surface's window is kept out of captures, so the read
    // gets what lies behind the card and not the card itself (docs/adr/0038).
    auto const screen            = GetDC(nullptr);
    auto const memory            = CreateCompatibleDC(screen);
    auto       info              = BITMAPINFO{};
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = size.width();
    info.bmiHeader.biHeight      = -size.height(); // top-down, as QImage stores its rows
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void*      bits              = nullptr;
    auto const dib               = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    auto       read              = QImage();
    if (dib != nullptr) {
        auto const previous = SelectObject(memory, dib);
        SetStretchBltMode(memory, HALFTONE);
        SetBrushOrgEx(memory, 0, 0, nullptr);
        StretchBlt(memory, 0, 0, size.width(), size.height(), screen, native.x(), native.y(), native.width(), native.height(), SRCCOPY);
        // GDI leaves the fourth byte at zero. Format_RGB32 calls it opaque, but a texture upload that reads the byte
        // makes the sheet transparent and the glass draws nothing (docs/adr/0070), so it is set here, before the copy.
        auto* const pixels = static_cast<QRgb*>(bits);
        auto const  count  = static_cast<std::size_t>(size.width()) * static_cast<std::size_t>(size.height());
        for (std::size_t i = 0; i < count; ++i)
            pixels[i] |= 0xff000000u;
        read = QImage(static_cast<uchar*>(bits), size.width(), size.height(), QImage::Format_RGB32).copy();
        SelectObject(memory, previous);
        DeleteObject(dib);
    }
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return read;
#else
    return {};
#endif
}

} // namespace lens::app
