/**
 * @file pet_window_native.cpp
 * @brief Win32 implementation of the pet window's capture exclusion and pass-through.
 */

#include "app/pet/pet_window_native.h"

#include "util/log.h"

#include <windows.h>

#include <QScreen>
#include <QWindow>
#include <QtGui/qscreen_platform.h>

namespace lens::app::pet {

namespace {

/// @return The native handle of a window Qt has created. Qt creates it on first winId() call.
HWND handleOf(QWindow const& window)
{
    return reinterpret_cast<HWND>(window.winId());
}

/// @return True when this run leaves every window in screen captures.
///
/// The exclusion is a privacy feature (docs/adr/0038, docs/adr/0041): an excluded surface is
/// absent from screenshots and screen shares alike, which is also why scripts/qa/ui-capture.ps1
/// photographs an empty desktop. QA asks for the other side of that trade with
/// `LENS_QA_VISIBLE_IN_CAPTURE`, and the callers take their no-exclusion path.
bool windowStaysVisibleInCapture()
{
    static bool const visible = [] {
        auto const asked = qgetenv("LENS_QA_VISIBLE_IN_CAPTURE");
        if (asked.isEmpty()) return false;
        LENS_WARN("LENS_QA_VISIBLE_IN_CAPTURE is set: no window is kept out of screen captures, so every "
                  "surface shows up in screenshots and screen shares. QA only; never leave it set.");
        return true;
    }();
    return visible;
}

} // namespace

QString deviceNameOf(QScreen const& screen)
{
    auto const* native = screen.nativeInterface<QNativeInterface::QWindowsScreen>();
    if (not native) return {};

    auto info   = MONITORINFOEXW{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(native->handle(), &info) == FALSE) return {};
    return QString::fromWCharArray(info.szDevice);
}

bool setExcludedFromCapture(QWindow const& window, bool const excluded)
{
    if (windowStaysVisibleInCapture()) return false;
    return SetWindowDisplayAffinity(handleOf(window), excluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE) != FALSE;
}

bool excludeFromCapture(QWindow const& window)
{
    return setExcludedFromCapture(window, true);
}

std::intptr_t passthroughStyle(std::intptr_t style, bool on)
{
    // Layered stays on either way: transparency only takes effect on a layered window.
    if (on) return style | static_cast<std::intptr_t>(WS_EX_LAYERED | WS_EX_TRANSPARENT);
    return style & ~static_cast<std::intptr_t>(WS_EX_TRANSPARENT);
}

void setPassthrough(QWindow const& window, bool on)
{
    auto const hwnd  = handleOf(window);
    auto const style = static_cast<std::intptr_t>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, static_cast<LONG_PTR>(passthroughStyle(style, on)));
}

} // namespace lens::app::pet
