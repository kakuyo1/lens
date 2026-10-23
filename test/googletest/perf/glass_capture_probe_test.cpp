/**
 * @file glass_capture_probe_test.cpp
 * @brief What the desktop gives the glass backdrop, measured through the application's own read path.
 *
 * The probe drives the same seam production does: `GlassDuplication` for a duplicated output, `GdiSource` for the
 * compositor's screen DC, both answering `DesktopSource::read` (src/app/glass/glass_source.h), so the number it prints
 * is the number the application pays (docs/adr/0086). What is left in this file is measurement, not a second read path.
 *
 * It answers:
 *
 *   1. which adapter owns which output, and what Windows runs each output at;
 *   2. whether an output can be duplicated at all, and if so, how many frames reach the application and what one
 *      copied sheet costs through `GlassDuplication::read`;
 *   3. what a GDI read of the same sheet costs, at full resolution and at a quarter;
 *   4. whether capture exclusion hides the card from a duplicated frame as it does from GDI.
 *
 * The GPU-path questions — whether the acquired texture can be mapped or shared, and what a whole-output copy costs —
 * settled the CPU-copy decision in ADR-0072 and are recorded in
 * `docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md`. They are not re-measured here: the seam the
 * application reads through does not expose the raw texture.
 *
 * Like the rest of this suite it prints rather than asserts on time: a wall-clock threshold is flaky on a machine that
 * is also drawing the desktop. It asserts that an output duplicates at all, and that capture exclusion works.
 *
 * Run it by hand, on the machine whose glass you are asking about, from an interactive desktop session:
 *
 *     ./scripts/build/build.bat --target lens_gtest_glass_probe
 *     PATH="$QT_ROOT/bin:$PATH" ./build-ninja/test/googletest/lens_gtest_glass_probe.exe
 *
 *     # The exclusion case paints a solid magenta square on one screen for about two seconds, so it is off unless asked:
 *     LENS_PROBE_EXCLUSION=1 PATH="$QT_ROOT/bin:$PATH" \
 *       ./build-ninja/test/googletest/lens_gtest_glass_probe.exe --gtest_filter='*Excluded*'
 *
 * Nothing leaves the process: frames are sampled at a few points, counted and thrown away — never written to disk and
 * never logged as pixels (docs/adr/0034, SECURITY.md). Only one process may duplicate a given output at a time, so
 * close screen recorders first or the acquires report what the recorder cost rather than what the desktop cost. A CI
 * runner has no desktop session to duplicate, which makes this suite human-run like the integration one.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QImage>
#include <QRect>
#include <QString>

#include "app/glass/glass_capability.h"
#include "app/glass/glass_duplication.h"
#include "app/glass/glass_source.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32

namespace {

using lens::app::GdiSource;
using lens::app::GlassDuplication;
using lens::app::SheetScale;

/// The sheet the settings window takes on a 125% screen, in device pixels: ADR 0040's own measurement size, so its
/// 13.4 ms and the number printed here are the same quantity read on two machines.
constexpr int kSheetWidth  = 1075;
constexpr int kSheetHeight = 1231;
/// Seconds to watch a duplicated output for: short enough that a still desktop shows up in the timeouts, long enough
/// that a frame rate divides out of it.
constexpr double kWatchSeconds = 1.5;
/// AcquireNextFrame's wait. Longer than one frame at 60 Hz, so a timeout means the desktop is still, not that the
/// call is slow.
constexpr int kAcquireTimeoutMs = 100;
/// How many times to repeat a GDI read before taking the median.
constexpr int kGdiRuns = 30;

/// The window the exclusion case paints: solid magenta, big enough for a sheet to cover, out of the taskbar.
constexpr wchar_t kProbeClass[] = L"LensGlassProbeCard";
constexpr wchar_t kProbeTitle[] = L"Lens glass probe";
constexpr int     kProbeWidth   = 420;
constexpr int     kProbeHeight  = 260;
/// How far apart the magenta samples are across a sheet. The square is 420x260, so a step of 8 leaves about
/// seventeen hundred points inside it: presence is never a question of luck, and the whole sheet stays a scan.
constexpr int kMagentaStep = 8;

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

/// @brief One output's device name and rectangle, in physical desktop pixels.
struct Monitor {
    QString device;
    QRect   rect;
};

/// @brief Append one monitor to the list @p data points at.
BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM const data)
{
    auto info   = MONITORINFOEXW{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) return TRUE;
    auto& list = *reinterpret_cast<std::vector<Monitor>*>(data);
    list.push_back(Monitor{
        QString::fromWCharArray(info.szDevice),
        QRect(info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top),
    });
    return TRUE;
}

/// @return Every attached output, in EnumDisplayMonitors order.
std::vector<Monitor> monitors()
{
    auto found = std::vector<Monitor>{};
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&found));
    return found;
}

/// @brief The sheet-shaped region at the centre of @p screen, in desktop pixels.
QRect sheetOn(QRect const screen)
{
    auto const left = screen.left() + qMax(0, (screen.width() - kSheetWidth) / 2);
    auto const top  = screen.top() + qMax(0, (screen.height() - kSheetHeight) / 2);
    return QRect(left, top, qMin(kSheetWidth, screen.right() - left + 1), qMin(kSheetHeight, screen.bottom() - top + 1));
}

/// Paint the probe window solid magenta, so a frame can be checked for it without knowing what the desktop looks like.
LRESULT CALLBACK probeWndProc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_PAINT) {
        auto       paint = PAINTSTRUCT{};
        auto const dc    = BeginPaint(window, &paint);
        auto const brush = CreateSolidBrush(RGB(255, 0, 255));
        FillRect(dc, &paint.rcPaint, brush);
        DeleteObject(brush);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, w, l);
}

/** @brief The probe window, centred on @p screen, in that output's own pixels.
 * @return The window, or nothing when the class or the window could not be made.
 */
HWND makeProbeWindow(QRect const screen)
{
    static auto registered = false;
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
    const auto left = screen.left() + (screen.width() - kProbeWidth) / 2;
    const auto top  = screen.top() + (screen.height() - kProbeHeight) / 2;
    return CreateWindowExW(WS_EX_TOOLWINDOW, kProbeClass, kProbeTitle, WS_POPUP, left, top, kProbeWidth, kProbeHeight, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

/** @brief Pump the queue and ask for a repaint, so the desktop really changes and a frame arrives. */
void keepPainting(HWND window)
{
    auto message = MSG{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    InvalidateRect(window, nullptr, TRUE);
    UpdateWindow(window);
}

/** @return How many sampled pixels of @p sheet read as the probe window's magenta. */
int countMagenta(QImage const& sheet)
{
    auto hits = 0;
    for (int y = 0; y < sheet.height(); y += kMagentaStep)
        for (int x = 0; x < sheet.width(); x += kMagentaStep) {
            auto const rgb = sheet.pixel(x, y);
            if (qRed(rgb) > 240 && qGreen(rgb) < 24 && qBlue(rgb) > 240) ++hits; // R=255 G=0 B=255.
        }
    return hits;
}

/** @brief The most magenta seen in any copied frame over @p milliseconds, repainting @p window while it waits. */
int watchMagenta(GlassDuplication& session, QRect const sheet, HWND window, int const milliseconds)
{
    QElapsedTimer watch;
    watch.start();
    auto best = 0;
    while (watch.elapsed() < milliseconds) {
        keepPainting(window);
        auto read = session.read(sheet, 1.0, kAcquireTimeoutMs);
        if (read.refused) break;
        if (read.newFrame and not read.image.isNull()) best = std::max(best, countMagenta(read.image));
    }
    return best;
}

/** @brief Whether @p monitor duplicates, and if so, whether exclusion hides the painted square from it.
 * @return False when the output cannot duplicate, so the caller tries the next one.
 */
bool probeExclusion(Monitor const& monitor)
{
    auto const sheet   = sheetOn(monitor.rect);
    auto       session = GlassDuplication{monitor.device, monitor.rect};
    // The first read opens the session and reports the refusal; a duplicated session also drops its own first frame.
    if (session.read(sheet, 1.0, 0).refused) return false;

    auto const window = makeProbeWindow(monitor.rect);
    if (window == nullptr) {
        ADD_FAILURE() << "建不出测试窗口: " << monitor.device.toStdString();
        return true;
    }
    ShowWindow(window, SW_SHOWNA);
    // The square is the control: it has to be on top, or a window the reader left over the centre of the output makes
    // the control read zero and the case fails for a reason that has nothing to do with capture exclusion.
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    UpdateWindow(window);
    Sleep(250);

    auto const shown    = watchMagenta(session, sheet, window, 1200);
    auto const set      = SetWindowDisplayAffinity(window, WDA_EXCLUDEFROMCAPTURE) != FALSE;
    auto       affinity = DWORD{0};
    GetWindowDisplayAffinity(window, &affinity);
    auto const hidden = watchMagenta(session, sheet, window, 1200);
    DestroyWindow(window);

    std::printf("\n  在 %s 上验证\n", monitor.device.toStdString().c_str());
    std::printf("  排除调用 %s, affinity 读回 0x%X\n", set ? "成功" : "被拒", static_cast<unsigned>(affinity));
    std::printf("  复制帧里读到品红的采样点: 排除前 %d, 排除后 %d (方块本身约 %d 点)\n", shown, hidden, kProbeWidth * kProbeHeight / (kMagentaStep * kMagentaStep));
    EXPECT_TRUE(set) << "系统拒绝了排除, 这条用例说明不了任何事";
    // The tolerance is deliberately not zero: what sits behind the square is still there; only the square's own
    // colour is expected to go.
    EXPECT_GT(shown, 0) << "方块没出现在复制帧里: 对照失败, 排除后的 0 不说明任何事";
    EXPECT_EQ(hidden, 0);
    return true;
}

} // namespace

#endif // _WIN32

/// @brief Which adapter owns which output, and what Windows runs each one at.
TEST(GlassCaptureProbe, ScreensAndTheirAdapters)
{
#ifdef _WIN32
    makePerMonitorDpiAware();
    auto const lines = lens::app::glassScreenLines();
    std::printf("\n  === 输出与所属适配器 ===\n");
    for (auto const& line : lines)
        std::printf("  %s\n", line.toStdString().c_str());
    std::printf("\n  要点: 可用性是逐输出的(ADR 0072); 这块屏能不能复制看下一条用例。\n");
    EXPECT_FALSE(lines.isEmpty());
#else
    GTEST_SKIP() << "桌面复制只在 Windows 上存在";
#endif
}

/// @brief Duplicate every output and measure what the application's own read path delivers.
TEST(GlassCaptureProbe, DuplicateEveryOutput)
{
#ifdef _WIN32
    makePerMonitorDpiAware();
    auto const all = monitors();
    ASSERT_FALSE(all.empty()) << "没有可枚举的输出: 这不是一个交互桌面会话";
    auto anyDuplicated = false;
    for (auto const& monitor : all) {
        auto const    sheet   = sheetOn(monitor.rect);
        auto          session = GlassDuplication{monitor.device, monitor.rect};
        auto          frames  = 0;
        auto          polls   = 0;
        auto          copies  = std::vector<double>{};
        auto          refused = false;
        QElapsedTimer watch;
        watch.start();
        while (watch.elapsed() < static_cast<qint64>(kWatchSeconds * 1000.0)) {
            auto read = session.read(sheet, 1.0, kAcquireTimeoutMs);
            if (read.refused) {
                refused = true;
                break;
            }
            if (read.newFrame and not read.image.isNull()) {
                ++frames;
                copies.push_back(read.copyMs);
            } else {
                ++polls;
            }
        }
        std::printf("\n  屏 %s  幕布 %dx%d 设备像素\n", monitor.device.toStdString().c_str(), sheet.width(), sheet.height());
        if (refused) {
            std::printf("      复制: 拒绝 —— 这块屏留在 GDI(ADR 0072 的常态, 不是错误)\n");
            continue;
        }
        anyDuplicated   = true;
        auto const rate = static_cast<double>(frames) / kWatchSeconds;
        std::printf("      复制: 可用, 观察 %.2fs: %d 帧到达 (%.1f 帧/s), %d 次没有新帧=桌面未动\n", kWatchSeconds, frames, rate, polls);
        std::printf("      幕布拷回 CPU 中位 %.2f ms (含复制、Map 与旋转, 即 GlassDuplication::read 的整段)\n", median(copies));
    }
    std::printf("\n  要点: 至少一块屏可复制 = 流畅档在这台机器上可行; 全部拒绝 = 只能留在 GDI。\n");
    EXPECT_TRUE(anyDuplicated);
#else
    GTEST_SKIP() << "桌面复制只在 Windows 上存在";
#endif
}

/// @brief ADR 0040's GDI numbers, re-measured through the same source the application reads with.
TEST(GlassCaptureProbe, GdiReadCostToday)
{
#ifdef _WIN32
    makePerMonitorDpiAware();
    auto const all = monitors();
    ASSERT_FALSE(all.empty()) << "没有可枚举的输出: 这不是一个交互桌面会话";
    auto quarter = GdiSource{true, SheetScale::Quarter};
    auto full    = GdiSource{true, SheetScale::Full};
    for (auto const& monitor : all) {
        auto const sheet = sheetOn(monitor.rect);
        auto       q     = std::vector<double>{};
        auto       f     = std::vector<double>{};
        q.reserve(kGdiRuns);
        f.reserve(kGdiRuns);
        for (int run = 0; run < kGdiRuns; ++run) {
            q.push_back(quarter.read(sheet, 1.0).copyMs);
            f.push_back(full.read(sheet, 1.0).copyMs);
        }
        auto const quarterMs = median(q);
        auto const fullMs    = median(f);
        std::printf("\n  %s 幕布 %dx%d 设备像素:\n", monitor.device.toStdString().c_str(), sheet.width(), sheet.height());
        std::printf("      缩到 1/4 中位 %.2f ms   (ADR 0040 记的 14.2 ms)\n", quarterMs);
        std::printf("      全分辨率 中位 %.2f ms   (ADR 0040 记的 13.4 ms)\n", fullMs);
        if (fullMs > 0.0) {
            auto const rate = 1000.0 / fullMs;
            auto const mibs = static_cast<double>(sheet.width()) * static_cast<double>(sheet.height()) * 4.0 / 1048576.0 * rate;
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
    auto const* const requested = std::getenv("LENS_PROBE_EXCLUSION");
    if (requested == nullptr || std::string(requested) != "1")
        GTEST_SKIP() << "会在屏上显示一个品红方块约两秒; 设 LENS_PROBE_EXCLUSION=1 才跑";
    makePerMonitorDpiAware();
    auto const all = monitors();
    ASSERT_FALSE(all.empty()) << "没有可枚举的输出";
    // 排除只能在能复制的屏上验证。混合显卡机器上内屏常常不提供复制, 所以逐块屏试, 取第一块能用的。
    for (auto const& monitor : all)
        if (probeExclusion(monitor)) return;
    GTEST_SKIP() << "这台机器没有任何输出可复制, 排除项在桌面复制上无从验证";
#else
    GTEST_SKIP() << "抓屏排除只在 Windows 上存在";
#endif
}
