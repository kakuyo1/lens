/**
 * @file glass_capture.cpp
 * @brief Windows Graphics Capture for the screens duplication refuses (see glass_capture.h).
 */

#include "glass_capture.h"

#include "glass_math.h"
#include "util/log.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QSize>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

// Windows.Foundation.h brings the IClosable definitions the capture classes call; the capture header only carries
// their declarations.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <wrl/client.h>
#endif

namespace lens::app {

#ifdef _WIN32

namespace {

using Microsoft::WRL::ComPtr;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame;
using winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool;
using winrt::Windows::Graphics::Capture::GraphicsCaptureItem;
using winrt::Windows::Graphics::Capture::GraphicsCaptureSession;
using winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice;

/// The same cooldown a failed duplication session keeps: a refusal that can pass is retried after this.
constexpr ULONGLONG kRetryMs = 5000;

/// @brief The GPU DWM scans @p monitor out with, from the display path's target.
///
/// On a hybrid machine the screen may be driven by the integrated GPU while DXGI lists its output on the discrete one,
/// and a capture device on the wrong GPU copies every frame across system memory: measured at 81 frames a second on the
/// internal panel against 130 on the scanout adapter. The display configuration names the target's adapter, and that
/// LUID finds the matching DXGI adapter (microsoft/Windows.UI.Composition-Win32-Samples issue 61).
///
/// @param monitor The output's native handle.
/// @param factory The DXGI factory whose adapters are searched.
/// @return The adapter, or nothing when the path cannot be read: the default adapter is the fallback.
ComPtr<IDXGIAdapter1> scanoutAdapter(HMONITOR const monitor, IDXGIFactory1* const factory)
{
    auto info   = MONITORINFOEXW{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) return nullptr;

    auto pathCount = UINT32{0};
    auto modeCount = UINT32{0};
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return nullptr;
    auto paths = std::vector<DISPLAYCONFIG_PATH_INFO>(pathCount);
    auto modes = std::vector<DISPLAYCONFIG_MODE_INFO>(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS)
        return nullptr;

    for (UINT32 index = 0; index < pathCount; ++index) {
        auto source             = DISPLAYCONFIG_SOURCE_DEVICE_NAME{};
        source.header.type      = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size      = sizeof(source);
        source.header.adapterId = paths[index].sourceInfo.adapterId;
        source.header.id        = paths[index].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        if (_wcsicmp(source.viewGdiDeviceName, info.szDevice) != 0) continue;

        auto const& luid = paths[index].targetInfo.adapterId;
        for (UINT32 search = 0;; ++search) {
            auto adapter = ComPtr<IDXGIAdapter1>{};
            if (factory->EnumAdapters1(search, adapter.GetAddressOf()) != S_OK) break;
            auto description = DXGI_ADAPTER_DESC1{};
            if (FAILED(adapter->GetDesc1(&description))) continue;
            if (description.AdapterLuid.LowPart == luid.LowPart and description.AdapterLuid.HighPart == luid.HighPart)
                return adapter;
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace

struct GlassCapture::Impl {
    Impl(QString name, QRect rect, HMONITOR handle)
        : deviceName(std::move(name)), desktop(std::move(rect)), monitor(handle)
    {}

    /// @brief Open the device and the capture, if they are not open and a retry is due.
    /// @return True when a frame may be waited for. Runs on the worker thread.
    bool ensure();
    /// @brief The staging texture the sheet is copied into, made for @p size and @p format when it does not fit.
    bool ensureStaging(QSize size, DXGI_FORMAT format);
    /// @brief Remember a failure, release what was open, and set the cooldown or the permanence.
    void fail(HRESULT hr, bool permanentFailure);
    /// @brief Release the capture session, the pool and the device. Runs on the worker thread.
    void release();
    /// @brief Take the frame the pool last reported, replacing any older one. Runs on the pool's thread.
    void onFrameArrived();

    QString deviceName;
    QRect   desktop;
    /// The output's native handle, used only to address the capture item.
    HMONITOR monitor = nullptr;

    /// True while the capture is open, read from the GUI thread. The rest of the state is the worker's.
    std::atomic<bool> operational{false};
    /// True after a refusal no retry can fix; read from the GUI thread.
    std::atomic<bool> permanent{false};
    /// The tick a failed session may be tried again, in GetTickCount64() milliseconds; 0 when none.
    std::atomic<ULONGLONG> retryAt{0};
    /// Set by close(). The object is never opened again.
    std::atomic<bool> closed{false};

    ComPtr<ID3D11Device>        device;
    ComPtr<ID3D11DeviceContext> context;
    /// The frame's sheet region, in a staging texture that maps it back to the CPU.
    ComPtr<ID3D11Texture2D> staging;
    QSize                   stagingSize;
    DXGI_FORMAT             stagingFormat = DXGI_FORMAT_UNKNOWN;

    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession     session{nullptr};
    winrt::event_token         frameToken{};

    /// Guards the frame the pool last handed over and the wait for the next one.
    std::mutex              mutex;
    std::condition_variable arrived;
    Direct3D11CaptureFrame  latest{nullptr};
};

bool GlassCapture::Impl::ensure()
{
    if (closed) return false;
    if (session != nullptr) return true;
    if (permanent) return false;
    if (GetTickCount64() < retryAt) return false;
    if (monitor == nullptr) return false;

    release();

    // The worker thread has no apartment of its own, and every WinRT call below needs one. An already initialized
    // thread (S_FALSE) and one Qt left in single-threaded mode are both fine here.
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    try {
        if (not GraphicsCaptureSession::IsSupported()) {
            fail(E_NOTIMPL, true);
            return false;
        }

        // The device the pool draws into; it needs BGRA support and, on a hybrid machine, the GPU the display scans
        // out with (scanoutAdapter). D3D11CreateDevice takes the adapter explicitly, so the driver type is UNKNOWN.
        auto factory = ComPtr<IDXGIFactory1>{};
        CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        auto const adapter    = scanoutAdapter(monitor, factory.Get());
        auto*      rawDevice  = static_cast<ID3D11Device*>(nullptr);
        auto*      rawContext = static_cast<ID3D11DeviceContext*>(nullptr);
        auto const created    = D3D11CreateDevice(adapter.Get(),
                                               adapter != nullptr ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                                               nullptr,
                                               D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                               nullptr,
                                               0,
                                               D3D11_SDK_VERSION,
                                               &rawDevice,
                                               nullptr,
                                               &rawContext);
        device.Attach(rawDevice);
        context.Attach(rawContext);
        if (FAILED(created) or device == nullptr or context == nullptr) {
            fail(created, false);
            return false;
        }

        auto dxgiDevice = ComPtr<IDXGIDevice>{};
        if (FAILED(device.As(&dxgiDevice))) {
            fail(E_FAIL, false);
            return false;
        }
        winrt::com_ptr<IInspectable> inspectable;
        auto const                   wrapped = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put());
        if (FAILED(wrapped)) {
            fail(wrapped, false);
            return false;
        }
        auto const frameDevice = inspectable.as<IDirect3DDevice>();

        auto const interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        auto       item    = GraphicsCaptureItem{nullptr};
        auto const made    = interop->CreateForMonitor(monitor,
                                                    winrt::guid_of<GraphicsCaptureItem>(),
                                                    winrt::put_abi(item));
        if (FAILED(made)) {
            fail(made, false);
            return false;
        }
        auto const size = QSize(item.Size().Width, item.Size().Height);
        if (size != desktop.size()) {
            // shortcut: a rotated output's captured picture is not in the desktop's orientation here, and mapping
            // the sheet through it is the same work frameRectFor() already does for duplication. That screen keeps
            // DXGI and GDI; add the mapping when a rotated screen is left with this source alone.
            fail(E_NOTIMPL, true);
            return false;
        }

        // Four buffers: the frame the handler holds while the glass draws the previous one has to leave room for the
        // next compositions, and the measured delivery was 95 frames a second with two, 107 with three and 130 (the
        // compositor's own pace) with four. The cost is four screen-sized BGRA buffers while the session is open.
        pool    = Direct3D11CaptureFramePool::CreateFreeThreaded(frameDevice,
                                                              winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                                                              4,
                                                              item.Size());
        session = pool.CreateCaptureSession(item);
        // Windows 11 24H2 floors the interval between delivered frames at about a third of the screen's rate: 48
        // frames a second on a 144 Hz panel without this line, 130 with it. Session5 is that build's interface; an
        // older system keeps the floor, which still beats the GDI read it replaces.
        if (session.try_as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession5>())
            session.as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession5>().MinUpdateInterval(
                winrt::Windows::Foundation::TimeSpan{});
        // Neither the pointer nor the system's capture border is part of the desktop picture. Windows 10 has
        // neither interface, and a capture there would put a yellow border around the reader's screen: refuse.
        session.as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession2>().IsCursorCaptureEnabled(false);
        session.as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession3>().IsBorderRequired(false);
        frameToken = pool.FrameArrived([this](auto const&, auto const&) { onFrameArrived(); });
        session.StartCapture();

        auto adapterDesc = DXGI_ADAPTER_DESC{};
        if (adapter == nullptr or FAILED(adapter->GetDesc(&adapterDesc)))
            adapterDesc.Description[0] = L'\0';
        retryAt     = 0;
        operational = true;
        LENS_INFO("glass capture on {}: Windows Graphics Capture, {}x{}, device on {}, the live refresh follows it",
                  deviceName.toStdString(),
                  size.width(),
                  size.height(),
                  QString::fromWCharArray(adapterDesc.Description).toStdString());
        return true;
    } catch (winrt::hresult_error const& error) {
        fail(error.code(), false);
        return false;
    }
}

bool GlassCapture::Impl::ensureStaging(QSize const size, DXGI_FORMAT const format)
{
    if (staging != nullptr and stagingSize == size and stagingFormat == format) return true;
    staging.Reset();
    stagingSize   = {};
    stagingFormat = DXGI_FORMAT_UNKNOWN;

    auto description             = D3D11_TEXTURE2D_DESC{};
    description.Width            = static_cast<UINT>(size.width());
    description.Height           = static_cast<UINT>(size.height());
    description.MipLevels        = 1;
    description.ArraySize        = 1;
    description.Format           = format;
    description.SampleDesc.Count = 1;
    description.Usage            = D3D11_USAGE_STAGING;
    description.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&description, nullptr, staging.GetAddressOf()))) return false;
    stagingSize   = size;
    stagingFormat = format;
    return true;
}

void GlassCapture::Impl::fail(HRESULT const hr, bool const permanentFailure)
{
    release();
    operational = false;
    permanent   = permanentFailure;
    retryAt     = permanentFailure ? 0 : GetTickCount64() + kRetryMs;
    // A refusal is a fact about the machine's screens, not an error the reader can act on: duplication and GDI still
    // cover the screen. Log it once per attempt, with the layout and never with pixels (SECURITY.md).
    LENS_INFO("glass capture on {}: unavailable (0x{:08X}), the glass falls back on this screen",
              deviceName.toStdString(),
              static_cast<unsigned long>(hr));
}

void GlassCapture::Impl::release()
{
    operational = false;
    try {
        if (pool != nullptr) pool.FrameArrived(frameToken);
    } catch (winrt::hresult_error const&) {
        // The pool is already closed; there is no handler to take off.
    }
    {
        std::lock_guard const lock(mutex);
        latest = nullptr;
    }
    try {
        if (session != nullptr) session.Close();
        if (pool != nullptr) pool.Close();
    } catch (winrt::hresult_error const&) {
        // A session that is already closing needs nothing more.
    }
    pool       = nullptr;
    session    = nullptr;
    frameToken = {};
    staging.Reset();
    stagingSize   = {};
    stagingFormat = DXGI_FORMAT_UNKNOWN;
    context.Reset();
    device.Reset();
}

void GlassCapture::Impl::onFrameArrived()
{
    try {
        auto frame = pool.TryGetNextFrame();
        if (frame == nullptr) return;
        {
            std::lock_guard const lock(mutex);
            // The frame that was waiting goes back to the pool: only the newest picture is worth a copy.
            latest = std::move(frame);
        }
        arrived.notify_one();
    } catch (winrt::hresult_error const&) {
        // A frame that arrives while the pool is closing has nothing to hand over.
    }
}

#endif // _WIN32

GlassCapture::GlassCapture(QString deviceName, QRect const desktop, quintptr const monitor)
    : deviceName_(std::move(deviceName))
#ifdef _WIN32
      ,
      impl_(std::make_unique<Impl>(deviceName_, desktop, reinterpret_cast<HMONITOR>(monitor)))
#else
      ,
      impl_(std::make_unique<Impl>())
#endif
{
    Q_UNUSED(desktop)
    Q_UNUSED(monitor)
}

GlassCapture::~GlassCapture() = default;

QString const& GlassCapture::deviceName() const
{
    return deviceName_;
}

bool GlassCapture::canFollow() const
{
#ifdef _WIN32
    if (impl_->closed or impl_->permanent) return false;
    if (impl_->operational) return true;
    return GetTickCount64() >= impl_->retryAt;
#else
    return false;
#endif
}

void GlassCapture::close()
{
#ifdef _WIN32
    impl_->closed = true;
    impl_->release();
#endif
}

DesktopRead GlassCapture::read(QRect const native, double const scale, int const timeoutMs)
{
#ifdef _WIN32
    auto& session = *impl_;
    if (session.closed) return {};
    if (not session.ensure()) return {.refused = true};

    auto frame = Direct3D11CaptureFrame{nullptr};
    {
        std::unique_lock lock(session.mutex);
        if (timeoutMs > 0)
            session.arrived.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&session] {
                return session.latest != nullptr or session.closed.load();
            });
        if (session.latest == nullptr) return {};
        frame = std::move(session.latest);
    }

    auto const surface = frame.Surface();
    auto const access  = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    auto       texture = ComPtr<ID3D11Texture2D>{};
    if (access == nullptr or FAILED(access->GetInterface(IID_PPV_ARGS(&texture)))) {
        frame.Close();
        session.fail(E_FAIL, false);
        return {.refused = true};
    }

    auto description = D3D11_TEXTURE2D_DESC{};
    texture->GetDesc(&description);
    if (description.Format != DXGI_FORMAT_B8G8R8A8_UNORM and description.Format != DXGI_FORMAT_B8G8R8X8_UNORM) {
        // An HDR desktop is not BGRA; the colours would be wrong rather than merely softer.
        frame.Close();
        session.fail(E_NOTIMPL, true);
        return {.refused = true};
    }
    auto const content     = frame.ContentSize();
    auto const contentSize = QSize(static_cast<int>(content.Width), static_cast<int>(content.Height));
    if (contentSize != session.desktop.size()) {
        // A display mode change, or a rotated output whose frame is not in the desktop's orientation.
        frame.Close();
        session.fail(E_NOTIMPL, true);
        return {.refused = true};
    }

    auto region = native.translated(-session.desktop.x(), -session.desktop.y());
    region      = region.intersected(QRect(QPoint(0, 0), contentSize));
    if (region.isEmpty()) {
        frame.Close();
        return {};
    }
    if (not session.ensureStaging(region.size(), description.Format)) {
        frame.Close();
        session.fail(E_FAIL, false);
        return {.refused = true};
    }

    QElapsedTimer copyTimer;
    copyTimer.start();
    auto const box = D3D11_BOX{static_cast<UINT>(region.x()),
                               static_cast<UINT>(region.y()),
                               0,
                               static_cast<UINT>(region.x() + region.width()),
                               static_cast<UINT>(region.y() + region.height()),
                               1};
    session.context->CopySubresourceRegion(session.staging.Get(), 0, 0, 0, 0, texture.Get(), 0, &box);
    auto mapped = D3D11_MAPPED_SUBRESOURCE{};
    if (FAILED(session.context->Map(session.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        frame.Close();
        session.fail(E_FAIL, false);
        return {.refused = true};
    }
    // The mapped rows are read where they lie; the copy below is the picture's own memory once the staging texture is
    // unmapped. Format_RGB32 ignores the fourth byte, and the upload is opaque (docs/adr/0070).
    auto const view = QImage(static_cast<uchar const*>(mapped.pData),
                             region.width(),
                             region.height(),
                             static_cast<qsizetype>(mapped.RowPitch),
                             QImage::Format_RGB32);
    // Windows Graphics Capture has no dirty rectangles: a frame arrives whenever the compositor draws the monitor again,
    // which includes this application's own repaint while a glass slider moves. The fingerprint lets the sheet drop a
    // frame whose picture is already in force, instead of uploading it and blurring it a second time at the screen's
    // pace (docs/adr/0087). It is taken on the worker, so the GUI thread only ever compares it.
    auto const fingerprint = pictureFingerprint(view);
    auto       image       = view.copy();
    session.context->Unmap(session.staging.Get(), 0);
    frame.Close();
    image.setDevicePixelRatio(scale);
    return {.image       = std::move(image),
            .newFrame    = true,
            .copyMs      = static_cast<double>(copyTimer.nsecsElapsed()) / 1.0e6,
            .fingerprint = fingerprint};
#else
    Q_UNUSED(native)
    Q_UNUSED(scale)
    Q_UNUSED(timeoutMs)
    return {.refused = true};
#endif
}

} // namespace lens::app
