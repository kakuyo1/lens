/**
 * @file glass_capability.cpp
 * @brief The DXGI screen inventory behind the glass pipeline's first log line (see glass_capability.h).
 */

#include "glass_capability.h"

#include <QString>

#ifdef _WIN32
#include <dxgi1_2.h>
#include <windows.h>
#endif

namespace lens::app {

#ifdef _WIN32
namespace {

/// The rotation Windows reports for an output, in words a reader of the log can act on. A rotated panel's desktop
/// rectangle and its frame do not line up: the external monitor of the measurement machine reads
/// 1440 x 2560 on the desktop and duplicates a 2560 x 1440 frame
/// (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md 3.1).
/// @return The rotation as English text, and the degrees where Windows has a name for them.
char const* rotationText(DXGI_MODE_ROTATION const rotation)
{
    switch (rotation) {
        case DXGI_MODE_ROTATION_IDENTITY: return "identity";
        case DXGI_MODE_ROTATION_ROTATE90: return "rotated 90";
        case DXGI_MODE_ROTATION_ROTATE180: return "rotated 180";
        case DXGI_MODE_ROTATION_ROTATE270: return "rotated 270";
        default: return "unspecified";
    }
}

} // namespace
#endif

QStringList glassScreenLines()
{
#ifdef _WIN32
    auto           lines   = QStringList{};
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return lines;
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(adapterIndex, &adapter) != S_OK) break;
        auto desc = DXGI_ADAPTER_DESC1{};
        if (SUCCEEDED(adapter->GetDesc1(&desc)) && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            adapter->Release();
            continue; // The WARP adapter has no screen of its own to list.
        }
        auto const name    = QString::fromWCharArray(desc.Description);
        auto       outputs = 0;
        for (UINT outputIndex = 0;; ++outputIndex) {
            IDXGIOutput* output = nullptr;
            if (adapter->EnumOutputs(outputIndex, &output) != S_OK) break;
            auto out = DXGI_OUTPUT_DESC{};
            if (SUCCEEDED(output->GetDesc(&out))) {
                auto mode   = DEVMODEW{};
                mode.dmSize = sizeof(mode);
                EnumDisplaySettingsW(out.DeviceName, ENUM_CURRENT_SETTINGS, &mode);
                ++outputs;
                lines << QStringLiteral("glass screen %1 on adapter %2 (%3): %4x%5 at (%6,%7), %8 Hz, %9, %10")
                             .arg(QString::fromWCharArray(out.DeviceName))
                             .arg(adapterIndex)
                             .arg(name)
                             .arg(out.DesktopCoordinates.right - out.DesktopCoordinates.left)
                             .arg(out.DesktopCoordinates.bottom - out.DesktopCoordinates.top)
                             .arg(out.DesktopCoordinates.left)
                             .arg(out.DesktopCoordinates.top)
                             .arg(mode.dmDisplayFrequency)
                             .arg(QString::fromLatin1(rotationText(out.Rotation)))
                             .arg(out.AttachedToDesktop ? QStringLiteral("attached") : QStringLiteral("detached"));
            }
            output->Release();
        }
        if (outputs == 0)
            lines << QStringLiteral("glass adapter %1 (%2) lists no screen").arg(adapterIndex).arg(name);
        adapter->Release();
    }
    factory->Release();
    return lines;
#else
    return {};
#endif
}

} // namespace lens::app
