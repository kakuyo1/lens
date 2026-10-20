/**
 * @file glass_capture_probe_test.cpp
 * @brief What the desktop is willing to give the glass backdrop, measured on a real desktop session.
 *
 * The glass reads the desktop through GDI today, at a quarter of the device resolution, about fifteen times a second
 * in all (docs/adr/0040, docs/adr/0041). That rate came from an offline probe on one machine and was never measured
 * inside the application, so both things it decided — the rate, and the quarter — carry the doubts the ADR recorded.
 * This answers the hardware half of the question:
 *
 *   1. which adapter owns which output, and the rate and scale Windows runs each output at (the hybrid mapping that
 *      made ADR 0040 give up on DXGI: a device built on one adapter cannot duplicate an output driven by another);
 *   2. whether an output can be duplicated once the device is built on the adapter that owns it;
 *   3. how many frames that output really delivers, and what a frame costs to reach — against what the GDI read
 *      costs on the same machine right now;
 *   4. whether an acquired frame can be sampled or shared at all, which is what decides whether a GPU path can hand
 *      the desktop to the scene graph without a CPU round trip.
 *
 * Like the rest of this suite it prints rather than asserts on time: a wall-clock threshold is flaky on a machine that
 * is also drawing the desktop. The facts it does assert are qualitative — an output duplicates, or it does not.
 *
 * Reading the output: `帧/s` next to the refresh rate says what rate a duplicated backdrop could reach; `幕布拷回 CPU`
 * is the price of handing a frame to the pipeline as it is built today; `GDI 读屏` is what it pays per read now.
 *
 * Run it by hand, on the machine whose glass you are asking about, from an interactive desktop session:
 *
 *     ./scripts/build/build.bat --target lens_gtest_glass_probe
 *     ./build-ninja/test/googletest/lens_gtest_glass_probe.exe
 *
 *     # The exclusion case paints a solid magenta square on one screen for about a second, so it is off unless asked for:
 *     LENS_PROBE_EXCLUSION=1 ./build-ninja/test/googletest/lens_gtest_glass_probe.exe --gtest_filter='*Excluded*'
 *
 * Nothing leaves the process: frames are sampled at a few points, counted and thrown away — never written to disk and
 * never logged as pixels (docs/adr/0034, SECURITY.md). Only one process may duplicate a given output at a time, so
 * close screen recorders first or the acquires report what the recorder cost rather than what the desktop cost. A CI
 * runner has no desktop session to duplicate, which makes this suite human-run like the integration one.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <shellscalingapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#ifdef _WIN32

using Microsoft::WRL::ComPtr;

namespace {

/// The sheet the settings window takes on a 125% screen, in device pixels: ADR 0040's own measurement size, so its
/// 13.4 ms and the number printed here are the same quantity read on two machines.
constexpr int kSheetWidth  = 1075;
constexpr int kSheetHeight = 1231;
/// Seconds to watch a duplicated output for: short enough that a still desktop shows up in the timeouts, long enough
/// that a frame rate divides out of it.
constexpr double kWatchSeconds = 1.5;
/// AcquireNextFrame's wait. Longer than one frame at 60 Hz, so a timeout means the desktop is still, not that the
/// call is slow.
constexpr UINT kAcquireTimeoutMs = 100;
/// How many times to repeat a GDI read before taking the median.
constexpr int kGdiRuns = 30;

/// The window the exclusion case paints: solid magenta, big enough for a sheet to cover, out of the taskbar.
constexpr wchar_t kProbeClass[] = L"LensGlassProbeCard";
constexpr wchar_t kProbeTitle[] = L"Lens glass probe";
constexpr int     kProbeWidth   = 420;
constexpr int     kProbeHeight  = 260;
/// How far apart the magenta samples are across a frame. The square is 420x260, so a step of 8 leaves about
/// seventeen thousand points inside it: presence is never a question of luck, and the whole frame stays a scan.
constexpr int kMagentaStep = 8;

/**
 * @brief The adapter an output belongs to, with everything about that output worth printing.
 * @note The interface pointers are kept so a duplication can be built on the owning adapter, which is the whole
 *       point of the inventory: pairing a device with the wrong adapter is what ADR 0040 hit.
 */
struct ScreenInfo {
    UINT                  adapterIndex = 0;
    UINT                  outputIndex  = 0;
    std::wstring          adapterName;
    std::wstring          deviceName;
    RECT                  desktop{}; ///< The output's rectangle, in desktop pixels.
    HMONITOR              monitor   = nullptr;
    UINT                  refreshHz = 0;
    UINT                  dpi       = 0;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIOutput1>  output;
};

/** @brief Make the process per-monitor DPI aware, so desktop rectangles are pixels and not a scaled lie.
 *
 * DXGI reports pixel rectangles while a DPI-unaware process sees every coordinate through the scaler, which is how
 * ADR 0040's untested 坐标映射 question becomes a wrong rectangle rather than an error.
 */
void makePerMonitorDpiAware()
{
    using SetContextFn   = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    static const auto fn = reinterpret_cast<SetContextFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (fn != nullptr) {
        if (fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
        if (fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE)) return;
    }
    SetProcessDPIAware();
}

/** @return The median of @p samples, in milliseconds: the odd two-hundred-millisecond stall is what a mean hides. */
double median(std::vector<double> samples)
{
    if (samples.empty()) return 0.0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

/** @brief A monotonic stopwatch that reads as milliseconds. */
class Clock {
public:
    Clock()
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        m_frequency = static_cast<double>(frequency.QuadPart) / 1000.0;
        reset();
    }
    /// @return Milliseconds since the clock was last reset.
    double ms() const
    {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - m_start.QuadPart) / m_frequency;
    }
    void reset()
    {
        QueryPerformanceCounter(&m_start);
    }

private:
    LARGE_INTEGER m_start{};
    double        m_frequency = 1.0;
};

/** @brief Create the device duplication needs on the adapter that owns @p screen.
 * @param adapter The adapter owning the output to be duplicated.
 * @param device Receives the device.
 * @param context Receives the immediate context.
 * @return The HRESULT of D3D11CreateDevice.
 * @throws nothing: every failure is returned, because a refusal is the answer the probe is asking for.
 * @note D3D_DRIVER_TYPE_UNKNOWN is not a choice: passing an adapter pins the driver type to UNKNOWN.
 */
HRESULT createDeviceOnAdapter(IDXGIAdapter1* adapter, ID3D11Device** device, ID3D11DeviceContext** context)
{
    return D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, device, nullptr, context);
}

/** @brief Everything one duplicated output can say about the cost of a frame.
 *
 * The copy timings decide the shape of a GPU path: the acquired desktop texture is not meant to be sampled or read,
 * so a frame has to be copied before anything can look at it. What that copy costs — a sheet, or a whole output — is
 * the difference between handing the frame to the pipeline as it stands and keeping it on the GPU.
 */
struct DupReadout {
    HRESULT result           = S_OK; ///< DuplicateOutput's HRESULT, or the first acquire that failed afterwards.
    bool    duplicated       = false;
    long    frames           = 0; ///< Frames the desktop really produced while watched.
    long    waits            = 0; ///< Waits that timed out: a desktop that is not moving, which is the idle case.
    double  seconds          = 0.0;
    double  medianGapMs      = 0.0; ///< Median between two frames: the refresh rate the output really runs at.
    double  medianReleaseMs  = 0.0; ///< CPU time of ReleaseFrame alone, the only part of the wait that is work.
    double  pollMs           = 0.0; ///< A zero-wait AcquireNextFrame: the call's own overhead.
    double  medianSheetMs    = 0.0; ///< Copy a sheet-sized region out and map it back to the CPU.
    double  medianFullMs     = 0.0; ///< Copy the whole output and map it back to the CPU.
    UINT    sheetWidth       = 0;
    UINT    sheetHeight      = 0;
    UINT    fullWidth        = 0;
    UINT    fullHeight       = 0;
    bool    acquiredMappable = false; ///< Whether the acquired resource answers a CPU map (it is not meant to).
    UINT    acquiredBind     = 0;
    UINT    acquiredUsage    = 0;
    UINT    acquiredMisc     = 0;
    HRESULT sharedAttempt    = E_FAIL; ///< Whether a texture made here can be opened by another device.
};

/** @brief Make the four textures the timings need, once the frame's own size is known. */
bool makeCopyTargets(ID3D11Device* device, const D3D11_TEXTURE2D_DESC& source, const D3D11_TEXTURE2D_DESC& sheet, DupReadout& out, ComPtr<ID3D11Texture2D>& sheetTexture, ComPtr<ID3D11Texture2D>& sheetStaging, ComPtr<ID3D11Texture2D>& fullTexture, ComPtr<ID3D11Texture2D>& fullStaging)
{
    auto sheetDefault              = sheet;
    sheetDefault.Usage             = D3D11_USAGE_DEFAULT;
    sheetDefault.BindFlags         = D3D11_BIND_SHADER_RESOURCE;
    sheetDefault.CPUAccessFlags    = 0;
    sheetDefault.MiscFlags         = 0;
    auto fullDefault               = sheetDefault;
    fullDefault.Width              = source.Width;
    fullDefault.Height             = source.Height;
    auto fullStagingDesc           = sheetDefault;
    fullStagingDesc.Usage          = D3D11_USAGE_STAGING;
    fullStagingDesc.BindFlags      = 0;
    fullStagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    fullStagingDesc.Width          = source.Width;
    fullStagingDesc.Height         = source.Height;

    out.sheetWidth  = sheet.Width;
    out.sheetHeight = sheet.Height;
    out.fullWidth   = source.Width;
    out.fullHeight  = source.Height;
    return SUCCEEDED(device->CreateTexture2D(&sheetDefault, nullptr, &sheetTexture)) &&
           SUCCEEDED(device->CreateTexture2D(&sheet, nullptr, &sheetStaging)) &&
           SUCCEEDED(device->CreateTexture2D(&fullDefault, nullptr, &fullTexture)) &&
           SUCCEEDED(device->CreateTexture2D(&fullStagingDesc, nullptr, &fullStaging));
}

/** @brief Whether a texture made on this device can be handed to another device, which is what a GPU path needs. */
HRESULT trySharedTexture(ID3D11Device* device)
{
    auto desc             = D3D11_TEXTURE2D_DESC{};
    desc.Width            = 64;
    desc.Height           = 64;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_DEFAULT;
    desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags        = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    auto texture          = ComPtr<ID3D11Texture2D>();
    return device->CreateTexture2D(&desc, nullptr, &texture);
}

/** @brief Watch @p screen for frames, timing what a frame costs to get at.
 * @param screen The output to duplicate; its adapter must be the one that owns it.
 * @param sheet The region to copy, in desktop pixels.
 * @return The readout; @c duplicated is false when Windows refused, and @c result says why.
 */
DupReadout measureDuplication(const ScreenInfo& screen, RECT sheet)
{
    auto out     = DupReadout{};
    auto device  = ComPtr<ID3D11Device>();
    auto context = ComPtr<ID3D11DeviceContext>();
    if (const auto hr = createDeviceOnAdapter(screen.adapter.Get(), &device, &context); FAILED(hr)) {
        out.result = hr;
        return out;
    }

    auto duplication = ComPtr<IDXGIOutputDuplication>();
    out.result       = screen.output->DuplicateOutput(device.Get(), &duplication);
    if (FAILED(out.result)) return out;
    out.duplicated = true;

    // A duplicated frame is the output alone, starting at its own corner, while the sheet arrives in desktop
    // coordinates: every offset into the frame is taken relative to the output.
    const auto box = D3D11_BOX{
        static_cast<UINT>(sheet.left - screen.desktop.left),
        static_cast<UINT>(sheet.top - screen.desktop.top),
        0,
        static_cast<UINT>(sheet.right - screen.desktop.left),
        static_cast<UINT>(sheet.bottom - screen.desktop.top),
        1,
    };
    auto sheetDesc             = D3D11_TEXTURE2D_DESC{};
    sheetDesc.Width            = static_cast<UINT>(sheet.right - sheet.left);
    sheetDesc.Height           = static_cast<UINT>(sheet.bottom - sheet.top);
    sheetDesc.MipLevels        = 1;
    sheetDesc.ArraySize        = 1;
    sheetDesc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    sheetDesc.SampleDesc.Count = 1;
    sheetDesc.Usage            = D3D11_USAGE_STAGING;
    sheetDesc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;

    auto sourceDesc   = D3D11_TEXTURE2D_DESC{};
    auto sheetTexture = ComPtr<ID3D11Texture2D>();
    auto sheetStaging = ComPtr<ID3D11Texture2D>();
    auto fullTexture  = ComPtr<ID3D11Texture2D>();
    auto fullStaging  = ComPtr<ID3D11Texture2D>();
    auto targetsReady = false;

    auto releaseTimes = std::vector<double>{};
    auto sheetTimes   = std::vector<double>{};
    auto fullTimes    = std::vector<double>{};
    auto gaps         = std::vector<double>{};

    Clock watch;
    auto  lastFrame = 0.0;
    auto  firstSeen = false;
    while (watch.ms() < kWatchSeconds * 1000.0) {
        auto       frame    = DXGI_OUTDUPL_FRAME_INFO{};
        auto       resource = ComPtr<IDXGIResource>();
        const auto hr       = duplication->AcquireNextFrame(kAcquireTimeoutMs, &frame, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            ++out.waits;
            continue;
        }
        if (hr == DXGI_ERROR_ACCESS_LOST) break;
        if (FAILED(hr)) {
            out.result = hr;
            break;
        }

        const auto now = watch.ms();
        if (firstSeen) gaps.push_back(now - lastFrame);
        lastFrame = now;
        firstSeen = true;
        ++out.frames;

        auto texture = ComPtr<ID3D11Texture2D>();
        if (FAILED(resource.As(&texture))) {
            duplication->ReleaseFrame();
            continue;
        }

        if (!targetsReady) {
            texture->GetDesc(&sourceDesc);
            out.acquiredBind  = sourceDesc.BindFlags;
            out.acquiredUsage = static_cast<UINT>(sourceDesc.Usage);
            out.acquiredMisc  = sourceDesc.MiscFlags;
            // Does the acquired resource answer a CPU map at all? The docs say no, and the copy timings below exist
            // because of that — this is the line that records it rather than assumes it.
            auto probe           = D3D11_MAPPED_SUBRESOURCE{};
            out.acquiredMappable = SUCCEEDED(context->Map(texture.Get(), 0, D3D11_MAP_READ, 0, &probe));
            if (out.acquiredMappable) context->Unmap(texture.Get(), 0);
            out.sharedAttempt = trySharedTexture(device.Get());
            targetsReady      = makeCopyTargets(device.Get(), sourceDesc, sheetDesc, out, sheetTexture, sheetStaging, fullTexture, fullStaging);
            if (!targetsReady) {
                duplication->ReleaseFrame();
                break;
            }
        }

        // The sheet path: copy the region a card sits on, then bring it back to the CPU the way today's pipeline
        // would have to receive it.
        auto sheetCopy = Clock{};
        context->CopySubresourceRegion(sheetTexture.Get(), 0, 0, 0, 0, texture.Get(), 0, &box);
        context->CopyResource(sheetStaging.Get(), sheetTexture.Get());
        auto mapped = D3D11_MAPPED_SUBRESOURCE{};
        if (SUCCEEDED(context->Map(sheetStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
            const auto* bytes = static_cast<const volatile uint8_t*>(mapped.pData);
            const auto  touch = bytes[0] + bytes[mapped.DepthPitch > 1 ? 1 : 0];
            (void)touch;
            context->Unmap(sheetStaging.Get(), 0);
        }
        sheetTimes.push_back(sheetCopy.ms());

        // The whole-output path: one read that could serve every surface on this screen at once.
        auto fullCopy = Clock{};
        context->CopyResource(fullTexture.Get(), texture.Get());
        context->CopyResource(fullStaging.Get(), fullTexture.Get());
        if (SUCCEEDED(context->Map(fullStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) context->Unmap(fullStaging.Get(), 0);
        fullTimes.push_back(fullCopy.ms());

        const auto release = Clock{};
        duplication->ReleaseFrame();
        releaseTimes.push_back(release.ms());
    }

    out.seconds         = watch.ms() / 1000.0;
    out.medianGapMs     = median(gaps);
    out.medianReleaseMs = median(releaseTimes);
    out.medianSheetMs   = median(sheetTimes);
    out.medianFullMs    = median(fullTimes);
    return out;
}

/** @brief Print one output's inventory line. */
void reportScreen(const ScreenInfo& screen)
{
    std::printf("  屏 %ls  适配器 %u/%u  %ldx%ld @%uHz  DPI %u  属于: %ls\n",
                screen.deviceName.c_str(),
                screen.adapterIndex,
                screen.outputIndex,
                screen.desktop.right - screen.desktop.left,
                screen.desktop.bottom - screen.desktop.top,
                screen.refreshHz,
                screen.dpi,
                screen.adapterName.c_str());
}

/** @brief Print one duplication attempt and the timings it produced. */
void reportDuplication(const ScreenInfo& screen, const DupReadout& readout)
{
    if (!readout.duplicated) {
        std::printf("      复制: 拒绝 (HRESULT 0x%08lX) —— 这块屏只能继续走 GDI\n", static_cast<unsigned long>(readout.result));
        return;
    }
    const auto rate = readout.seconds > 0.0 ? static_cast<double>(readout.frames) / readout.seconds : 0.0;
    std::printf("      复制: 可用\n");
    std::printf("      观察 %.2fs: %ld 帧到达 (%.1f 帧/s, 屏跑 %uHz), %ld 次超时=桌面未动\n",
                readout.seconds,
                readout.frames,
                rate,
                screen.refreshHz,
                readout.waits);
    std::printf("      帧间隔中位 %.2f ms (= 1/%.0f 秒); ReleaseFrame 调用本身中位 %.2f ms\n", readout.medianGapMs, readout.medianGapMs > 0.0 ? 1000.0 / readout.medianGapMs : 0.0, readout.medianReleaseMs);
    std::printf("      幕布 %ux%u 拷回 CPU 中位 %.2f ms; 整屏 %ux%u 拷回 CPU 中位 %.2f ms\n",
                readout.sheetWidth,
                readout.sheetHeight,
                readout.medianSheetMs,
                readout.fullWidth,
                readout.fullHeight,
                readout.medianFullMs);
    std::printf("      取到的纹理 BindFlags 0x%X Usage %u MiscFlags 0x%X; 直接 Map: %s\n",
                readout.acquiredBind,
                readout.acquiredUsage,
                readout.acquiredMisc,
                readout.acquiredMappable ? "成功" : "拒绝");
    std::printf("      本进程建 SHARED_KEYED_MUTEX 纹理(跨装置交付的前提): %s\n", SUCCEEDED(readout.sharedAttempt) ? "成功" : "失败");
}

/** @brief The sheet-shaped region at the centre of @p screen, in desktop pixels. */
RECT sheetOn(const ScreenInfo& screen)
{
    const auto width  = screen.desktop.right - screen.desktop.left;
    const auto height = screen.desktop.bottom - screen.desktop.top;
    const auto left   = screen.desktop.left + std::max<LONG>(0, (width - kSheetWidth) / 2);
    const auto top    = screen.desktop.top + std::max<LONG>(0, (height - kSheetHeight) / 2);
    return RECT{left, top, std::min(left + kSheetWidth, screen.desktop.right), std::min(top + kSheetHeight, screen.desktop.bottom)};
}

/** @brief The GDI read the application uses today, timed.
 * @param native The region to read, in desktop pixels.
 * @param divisor 1 to read at device resolution, 4 for the quarter the sheet is kept at.
 * @param captureBlt Whether to add CAPTUREBLT, the flag that pulls layered windows into a GDI read.
 * @return The median milliseconds over kGdiRuns reads, or -1 when the read never worked.
 */
double measureGdi(RECT native, int divisor, bool captureBlt)
{
    const auto sourceW = native.right - native.left;
    const auto sourceH = native.bottom - native.top;
    const auto width   = std::max<LONG>(1, sourceW / divisor);
    const auto height  = std::max<LONG>(1, sourceH / divisor);

    const auto screen            = GetDC(nullptr);
    const auto memory            = CreateCompatibleDC(screen);
    auto       info              = BITMAPINFO{};
    info.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth       = width;
    info.bmiHeader.biHeight      = -height;
    info.bmiHeader.biPlanes      = 1;
    info.bmiHeader.biBitCount    = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void*      bits              = nullptr;
    const auto dib               = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib == nullptr) {
        DeleteDC(memory);
        ReleaseDC(nullptr, screen);
        return -1.0;
    }
    const auto previous = SelectObject(memory, dib);
    SetStretchBltMode(memory, HALFTONE);
    SetBrushOrgEx(memory, 0, 0, nullptr);

    auto times = std::vector<double>{};
    for (int run = 0; run < kGdiRuns; ++run) {
        Clock clock;
        StretchBlt(memory, 0, 0, width, height, screen, native.left, native.top, sourceW, sourceH, captureBlt ? static_cast<DWORD>(SRCCOPY | CAPTUREBLT) : SRCCOPY);
        times.push_back(clock.ms());
    }

    SelectObject(memory, previous);
    DeleteObject(dib);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return median(times);
}

/// Paint the probe window solid magenta, so a frame can be checked for it without knowing what the desktop looks like.
LRESULT CALLBACK probeWndProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_PAINT) {
        auto       paint = PAINTSTRUCT{};
        const auto dc    = BeginPaint(window, &paint);
        const auto brush = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(dc, &paint.rcPaint, brush);
        DeleteObject(brush);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

/** @brief The probe window, centred on @p desktop's rectangle, in that output's own pixels.
 * @return The window, or nothing when the class or the window could not be made.
 */
HWND makeProbeWindow(RECT desktop)
{
    static bool registered = false;
    if (!registered) {
        auto wc          = WNDCLASSEXW{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = probeWndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kProbeClass;
        if (RegisterClassExW(&wc) == 0) return nullptr;
        registered = true;
    }
    const auto left = desktop.left + ((desktop.right - desktop.left) - kProbeWidth) / 2;
    const auto top  = desktop.top + ((desktop.bottom - desktop.top) - kProbeHeight) / 2;
    return CreateWindowExW(WS_EX_TOOLWINDOW, kProbeClass, kProbeTitle, WS_POPUP, left, top, kProbeWidth, kProbeHeight, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

/** @brief Pump the queue and ask for a repaint, so the desktop really changes and a frame arrives. */
void keepPainting(HWND window)
{
    auto message = MSG{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    InvalidateRect(window, nullptr, TRUE);
    UpdateWindow(window);
}

/** @brief How many points of @p frame read as the probe window's magenta.
 * @param device The device to make the read-back textures on.
 * @param context The context to copy and map with.
 * @param frame The acquired desktop texture.
 * @param step How far apart the sampled points are, in pixels.
 * @return The count of points that matched; only the count is kept.
 * @note The grid is deliberately coordinate-free. A rotated output's frame does not line up with the desktop
 *       rectangle DXGI reports for it — that mismatch is itself a fact worth recording — and this test only needs to
 *       know whether the square is in the picture at all.
 */
double countMagenta(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* frame, int step)
{
    auto desc = D3D11_TEXTURE2D_DESC{};
    frame->GetDesc(&desc);
    auto copyDesc           = desc;
    copyDesc.Usage          = D3D11_USAGE_DEFAULT;
    copyDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
    copyDesc.CPUAccessFlags = 0;
    copyDesc.MiscFlags      = 0;
    auto readDesc           = copyDesc;
    readDesc.Usage          = D3D11_USAGE_STAGING;
    readDesc.BindFlags      = 0;
    readDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    auto copy    = ComPtr<ID3D11Texture2D>();
    auto staging = ComPtr<ID3D11Texture2D>();
    if (FAILED(device->CreateTexture2D(&copyDesc, nullptr, &copy)) || FAILED(device->CreateTexture2D(&readDesc, nullptr, &staging))) return -1.0;
    context->CopyResource(copy.Get(), frame);
    context->CopyResource(staging.Get(), copy.Get());

    auto mapped = D3D11_MAPPED_SUBRESOURCE{};
    if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return -1.0;
    const auto* row   = static_cast<const uint8_t*>(mapped.pData);
    const auto  pitch = mapped.RowPitch;
    auto        hits  = 0;
    for (UINT y = 0; y < desc.Height; y += static_cast<UINT>(step)) {
        for (UINT x = 0; x < desc.Width; x += static_cast<UINT>(step)) {
            const auto bgra = row + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
            if (bgra[2] > 240 && bgra[1] < 24 && bgra[0] > 240) ++hits; // R=255 G=0 B=255, in BGRA order.
        }
    }
    context->Unmap(staging.Get(), 0);
    return static_cast<double>(hits);
}

/** @brief The most magenta seen in any frame over @p milliseconds of frames from @p duplication. */
double watchMagenta(IDXGIOutputDuplication* duplication, ID3D11Device* device, ID3D11DeviceContext* context, HWND window, double milliseconds)
{
    const auto deadline = static_cast<double>(GetTickCount64()) + milliseconds;
    auto       best     = 0.0;
    while (static_cast<double>(GetTickCount64()) < deadline) {
        keepPainting(window);
        Sleep(16);
        auto       frame    = DXGI_OUTDUPL_FRAME_INFO{};
        auto       resource = ComPtr<IDXGIResource>();
        const auto hr       = duplication->AcquireNextFrame(kAcquireTimeoutMs, &frame, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT || hr == DXGI_ERROR_ACCESS_LOST) continue;
        if (FAILED(hr)) break;
        auto texture = ComPtr<ID3D11Texture2D>();
        if (FAILED(resource.As(&texture))) {
            duplication->ReleaseFrame();
            continue;
        }
        const auto hits = countMagenta(device, context, texture.Get(), kMagentaStep);
        duplication->ReleaseFrame();
        if (hits >= 0.0) best = std::max(best, hits);
    }
    return best;
}

/** @brief Every hardware adapter's outputs, with the rate and scale each one runs at. */
std::vector<ScreenInfo> enumerateScreens()
{
    auto factory = ComPtr<IDXGIFactory1>();
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return {};
    auto screens = std::vector<ScreenInfo>{};
    for (UINT adapterIndex = 0;; ++adapterIndex) {
        auto adapter = ComPtr<IDXGIAdapter1>();
        if (factory->EnumAdapters1(adapterIndex, &adapter) != S_OK) break;
        auto desc = DXGI_ADAPTER_DESC1{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;
        std::printf("  适配器 %u: %ls  专用显存 %llu MB  %s%s\n",
                    adapterIndex,
                    desc.Description,
                    static_cast<unsigned long long>(desc.DedicatedVideoMemory / 1048576),
                    (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ? "软件(WARP)" : "硬件",
                    (desc.Flags & DXGI_ADAPTER_FLAG_REMOTE) != 0 ? " 远程会话" : "");
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
        for (UINT outputIndex = 0;; ++outputIndex) {
            auto plain = ComPtr<IDXGIOutput>();
            if (adapter->EnumOutputs(outputIndex, &plain) != S_OK) break;
            auto out = DXGI_OUTPUT_DESC{};
            if (FAILED(plain->GetDesc(&out))) continue;
            auto output = ComPtr<IDXGIOutput1>();
            if (FAILED(plain.As(&output))) {
                std::printf("    %ls 拿不到 IDXGIOutput1, 无从复制\n", out.DeviceName);
                continue;
            }
            auto mode   = DEVMODEW{};
            mode.dmSize = sizeof(mode);
            EnumDisplaySettingsW(out.DeviceName, ENUM_CURRENT_SETTINGS, &mode);
            auto dpi = UINT{0};
            GetDpiForMonitor(out.Monitor, MDT_EFFECTIVE_DPI, &dpi, &dpi);
            auto screen = ScreenInfo{
                .adapterIndex = adapterIndex,
                .outputIndex  = outputIndex,
                .adapterName  = desc.Description,
                .deviceName   = out.DeviceName,
                .desktop      = out.DesktopCoordinates,
                .monitor      = out.Monitor,
                .refreshHz    = mode.dmDisplayFrequency,
                .dpi          = dpi,
                .adapter      = adapter,
                .output       = output,
            };
            screens.push_back(std::move(screen));
        }
    }
    return screens;
}

#endif // _WIN32

/// @brief Which adapter owns which output, and what Windows runs each one at.
TEST(GlassCaptureProbe, ScreensAndTheirAdapters)
{
#ifdef _WIN32
    makePerMonitorDpiAware();
    const auto screens = enumerateScreens();
    std::printf("\n  === 输出与所属适配器 ===\n");
    for (const auto& screen : screens)
        reportScreen(screen);
    std::printf("\n  要点: 装置只能复制属于它那块适配器的输出; 内屏与外屏分属两块适配器时, 必须按屏各建一台。\n");
    EXPECT_FALSE(screens.empty());
#else
    GTEST_SKIP() << "桌面复制只在 Windows 上存在";
#endif
}

/// @brief Duplicate every output on its own adapter, and time what a frame really costs.
TEST(GlassCaptureProbe, DuplicateEveryOutput)
{
#ifdef _WIN32
    makePerMonitorDpiAware();
    const auto screens = enumerateScreens();
    ASSERT_FALSE(screens.empty()) << "没有可枚举的输出: 这不是一个交互桌面会话";
    auto anyDuplicated = false;
    for (const auto& screen : screens) {
        std::printf("\n");
        reportScreen(screen);
        const auto readout = measureDuplication(screen, sheetOn(screen));
        reportDuplication(screen, readout);
        if (readout.duplicated) anyDuplicated = true;
    }
    std::printf("\n  要点: 至少一块屏可复制 = 流畅档在这台机器上可行; 全部拒绝 = 只能留在 CPU 路径。\n");
    EXPECT_TRUE(anyDuplicated);
#else
    GTEST_SKIP() << "桌面复制只在 Windows 上存在";
#endif
}

/// @brief For every output, try duplicating it with a device built on every hardware adapter.
///
/// ADR 0040 recorded the internal panel's refusal as an adapter mismatch — 装置建在 4060 上、内屏归核显 — and gave up
/// on DXGI because of it. This asks the question both ways: the owning adapter, and each of the others. Whether the
/// refusal follows the panel or follows the adapter is what decides if a fix exists at all.
TEST(GlassCaptureProbe, DuplicateEveryAdapterOutputPair)
{
#ifdef _WIN32
    makePerMonitorDpiAware();
    auto factory = ComPtr<IDXGIFactory1>();
    ASSERT_EQ(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), S_OK);
    auto cards = std::vector<std::pair<UINT, ComPtr<IDXGIAdapter1>>>{};
    for (UINT index = 0;; ++index) {
        auto adapter = ComPtr<IDXGIAdapter1>();
        if (factory->EnumAdapters1(index, &adapter) != S_OK) break;
        auto desc = DXGI_ADAPTER_DESC1{};
        if (FAILED(adapter->GetDesc1(&desc))) continue;
        if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue;
        cards.emplace_back(index, adapter);
    }
    ASSERT_FALSE(cards.empty());

    for (const auto& screen : enumerateScreens()) {
        auto out = DXGI_OUTPUT_DESC{};
        screen.output->GetDesc(&out);
        std::printf("\n  屏 %ls  原点在 (%ld,%ld)  %s  旋转 %d   DXGI 列在适配器 %u 名下\n",
                    out.DeviceName,
                    out.DesktopCoordinates.left,
                    out.DesktopCoordinates.top,
                    out.AttachedToDesktop ? "已接桌面" : "未接桌面",
                    static_cast<int>(out.Rotation),
                    screen.adapterIndex);
        for (const auto& [index, adapter] : cards) {
            auto       device  = ComPtr<ID3D11Device>();
            auto       context = ComPtr<ID3D11DeviceContext>();
            const auto made    = createDeviceOnAdapter(adapter.Get(), &device, &context);
            if (FAILED(made)) {
                std::printf("      装置@适配器 %u: 建不出来 (0x%08lX)\n", index, static_cast<unsigned long>(made));
                continue;
            }
            auto       duplication = ComPtr<IDXGIOutputDuplication>();
            const auto hr          = screen.output->DuplicateOutput(device.Get(), &duplication);
            std::printf("      装置@适配器 %u 复制此屏: %s%s (0x%08lX)\n",
                        index,
                        SUCCEEDED(hr) ? "可用" : "拒绝",
                        index == screen.adapterIndex ? " [所属适配器]" : "",
                        static_cast<unsigned long>(hr));
        }
    }
    std::printf("\n  要点: 只有所属适配器能复制 = 按屏建装置即可修好; 任何适配器都拒绝 = 这块屏不提供桌面复制。\n");
#else
    GTEST_SKIP() << "桌面复制只在 Windows 上存在";
#endif
}

/// @brief ADR 0040's GDI numbers, re-measured on this machine: at the quarter kept today, and at full resolution.
TEST(GlassCaptureProbe, GdiReadCostToday)
{
#ifdef _WIN32
    makePerMonitorDpiAware();
    const auto screens = enumerateScreens();
    ASSERT_FALSE(screens.empty()) << "没有可枚举的输出: 这不是一个交互桌面会话";
    for (const auto& screen : screens) {
        const auto sheet   = sheetOn(screen);
        const auto quarter = measureGdi(sheet, 4, false);
        const auto whole   = measureGdi(sheet, 1, false);
        const auto layered = measureGdi(sheet, 1, true);
        std::printf("\n  %ls 幕布 %ldx%ld 设备像素:\n", screen.deviceName.c_str(), sheet.right - sheet.left, sheet.bottom - sheet.top);
        std::printf("      缩到 1/4 中位 %.2f ms   (ADR 0040 记的 14.2 ms)\n", quarter);
        std::printf("      全分辨率 中位 %.2f ms   (ADR 0040 记的 13.4 ms)\n", whole);
        std::printf("      全分辨率+CAPTUREBLT 中位 %.2f ms\n", layered);
        if (whole > 0.0) {
            const auto rate = 1000.0 / whole;
            const auto mibs = static_cast<double>(sheet.right - sheet.left) * static_cast<double>(sheet.bottom - sheet.top) * 4.0 / 1048576.0 * rate;
            std::printf("      按全分辨率读的上限: %.0f 帧/s, %.0f MiB/s\n", rate, mibs);
        }
    }
    std::printf("\n  要点: 读屏与降采样几乎等价 = 1/4 只为喂饱 CPU 模糊而牺牲画质; 帧率上限就是 1000/中位ms。\n");
#else
    GTEST_SKIP() << "GDI 只在 Windows 上存在";
#endif
}

/// @brief Whether a window kept out of captures really vanishes from a duplicated frame.
TEST(GlassCaptureProbe, ExcludedWindowVanishesFromDuplication)
{
#ifdef _WIN32
    const auto* const requested = std::getenv("LENS_PROBE_EXCLUSION");
    if (requested == nullptr || std::string(requested) != "1") GTEST_SKIP() << "会在屏上显示一个品红方块约一秒; 设 LENS_PROBE_EXCLUSION=1 才跑";
    makePerMonitorDpiAware();
    const auto screens = enumerateScreens();
    ASSERT_FALSE(screens.empty()) << "没有可枚举的输出";

    // 排除只能在能复制的屏上验证。混合显卡机器上内屏常常不提供复制, 所以逐块屏试, 取第一块能用的那块。
    auto device      = ComPtr<ID3D11Device>();
    auto context     = ComPtr<ID3D11DeviceContext>();
    auto duplication = ComPtr<IDXGIOutputDuplication>();
    auto screen      = ScreenInfo{};
    for (const auto& candidate : screens) {
        auto made = ComPtr<ID3D11Device>();
        auto ctx  = ComPtr<ID3D11DeviceContext>();
        auto dup  = ComPtr<IDXGIOutputDuplication>();
        if (FAILED(createDeviceOnAdapter(candidate.adapter.Get(), &made, &ctx))) continue;
        if (FAILED(candidate.output->DuplicateOutput(made.Get(), &dup))) continue;
        device      = made;
        context     = ctx;
        duplication = dup;
        screen      = candidate;
        break;
    }
    if (!duplication) GTEST_SKIP() << "这台机器没有任何输出可复制, 排除项在桌面复制上无从验证";
    std::printf("\n  在 %ls (由 %ls 列出) 上验证\n", screen.deviceName.c_str(), screen.adapterName.c_str());

    const auto window = makeProbeWindow(screen.desktop);
    ASSERT_NE(window, nullptr);
    ShowWindow(window, SW_SHOWNA);
    // The square is the control: it has to be on top, or a window the reader left over the centre of the output makes
    // the control read zero and the case fails for a reason that has nothing to do with capture exclusion.
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    UpdateWindow(window);
    Sleep(250);

    const auto shown    = watchMagenta(duplication.Get(), device.Get(), context.Get(), window, 1200.0);
    const auto set      = SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE) != FALSE;
    auto       affinity = DWORD{0};
    GetWindowDisplayAffinity(window, &affinity);
    const auto hidden = watchMagenta(duplication.Get(), device.Get(), context.Get(), window, 1200.0);
    DestroyWindow(window);

    std::printf("\n  排除调用 %s, affinity 读回 0x%X\n", set ? "成功" : "被拒", static_cast<unsigned>(affinity));
    std::printf("  复制帧里读到品红的采样点: 排除前 %.0f, 排除后 %.0f (方块本身约 %d 点)\n", shown, hidden, kProbeWidth * kProbeHeight / (kMagentaStep * kMagentaStep));
    ASSERT_TRUE(set) << "系统拒绝了排除, 这条用例说明不了任何事";
    // The tolerance is deliberately not zero: what sits behind the square is still there; only the square's own
    // colour is expected to go.
    EXPECT_GT(shown, 0.0) << "方块没出现在复制帧里: 对照失败, 排除后的 0 不说明任何事";
    EXPECT_EQ(hidden, 0.0);
#else
    GTEST_SKIP() << "抓屏排除只在 Windows 上存在";
#endif
}

} // namespace
