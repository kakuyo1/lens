#pragma once

#include <QList>
#include <QMutex>
#include <QPointer>
#include <QQuickWindow>

#include <atomic>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>
#endif

/**
 * @file surface_graphics.h
 * @brief How the surfaces share one D3D11 device and hand a hidden window's graphics resources back.
 *
 * The device and the release policy live here rather than in main.cpp's anonymous namespace so the
 * composition root only names the steps; the device is declared before the engine so it outlives
 * every window that adopts it.
 */

namespace lens::app {

#ifdef _WIN32
/// One D3D11 device for every Quick window, and the lock that makes sharing its immediate context
/// safe. D3D11 serializes individual calls, not frame sequences, so two render threads' interleaved
/// frames would draw into each other's render targets; the threaded render loop guarantees
/// beforeFrameBegin and afterFrameEnd come in pairs, so the bracket always opens.
class SharedGraphics {
public:
    /// @brief Create the device, BGRA-capable as the scene graph needs.
    /// @return False when no usable device exists; the surfaces then keep Qt's per-window devices.
    bool create();

    /// @brief Adopt the device on one window and bracket its frames. Before scene graph init.
    void adopt(QQuickWindow* window);

private:
    /// A render thread reports an invalidation; a removed device means every window must adopt a
    /// new one, because the loop's own retry would re-adopt the lost device forever. The rebuild
    /// runs on the GUI thread: setGraphicsDevice is a GUI-thread call, and a visible window's
    /// render thread only parks while the window is hidden, hence the hide/show blink on recovery.
    void noteInvalidation();

    void recover();

    Microsoft::WRL::ComPtr<ID3D11Device>        device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    QMutex                                      frameMutex_;
    std::atomic<int>                            recovering_{0};
    QList<QPointer<QQuickWindow>>               windows_;
};
#else
/// Off Windows the surfaces keep Qt's own devices; the pointer is never dereferenced.
class SharedGraphics;
#endif

/**
 * @brief Hand a hidden surface's graphics resources back to the driver.
 *
 * QQuickWindow keeps its graphics resources -- the device, the swapchain, the buffers and the
 * textures, and with them the driver's worker threads -- until the last window is deleted, and
 * setPersistentGraphics() defaults to true, so a hidden window keeps the lot. Measured with
 * .workbuddy-ai/tools/qwindow-probe on this machine: showing a second window costs about 75
 * driver threads and 1,200 handles, hiding returns none of them under Qt's defaults, and
 * releaseResources() with both persistence hints off returns all of them in one call.
 *
 * The hints only take effect before a window's scene graph is initialised, so this runs straight
 * after the QML is loaded and before the event loop. Every surface but the root window is hidden
 * at that point; the root is never shown and the pet is up from the start, and neither of those
 * should be released anyway.
 *
 * @param shared The device every window adopts, or null to keep Qt's per-window devices.
 */
void releaseHiddenSurfaces(QObject* root, SharedGraphics* shared = nullptr);

} // namespace lens::app