/**
 * @file glass_capture_test.cpp
 * @brief Whether Windows Graphics Capture opens on each screen and delivers frames (docs/adr/0087).
 *
 * The application reaches this source only on a screen DXGI duplication refuses, which is the internal panel of the
 * measurement machine (docs/adr/0072). This case asks the source directly, on every output, so a machine whose screen
 * cannot be duplicated still shows whether the fallback engine works there.
 *
 * It prints the frame rate rather than asserting on it: a static desktop delivers frames at the compositor's pace, not
 * at a number a test could name, and a CI runner has no desktop to capture. What it asserts is that a frame came back
 * and that the capture is refused only where the system says it cannot draw one without the border or the pointer.
 *
 * Run:
 *
 *     ./scripts/build/build.bat --target lens_gtest_integration
 *     LENS_GLASS_WGC_TEST=1 PATH="$QT_ROOT/bin:$PATH" \
 *       ./build-ninja/test/googletest/lens_gtest_integration.exe --gtest_filter='GlassCapture.*'
 *
 * The capture is read-only: it copies pixels into memory and releases them, never writes a file (SECURITY.md).
 */

#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QRect>
#include <QString>

#include "app/glass/glass_capture.h"

#include <cstdio>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

#ifdef _WIN32

struct Monitor {
    QString  name;
    HMONITOR handle = nullptr;
    QRect    rect;
};

BOOL CALLBACK collectMonitor(HMONITOR const handle, HDC, LPRECT, LPARAM const data)
{
    auto* const monitors = reinterpret_cast<std::vector<Monitor>*>(data);
    auto        info     = MONITORINFOEXW{};
    info.cbSize          = sizeof(info);
    if (GetMonitorInfoW(handle, &info) == FALSE) return TRUE;
    auto const& rc = info.rcMonitor;
    monitors->push_back(
        {QString::fromWCharArray(info.szDevice), handle, QRect(rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top)});
    return TRUE;
}

std::vector<Monitor> monitors()
{
    auto found = std::vector<Monitor>{};
    EnumDisplayMonitors(nullptr, nullptr, collectMonitor, reinterpret_cast<LPARAM>(&found));
    return found;
}

#endif

} // namespace

TEST(GlassCapture, ReadsFramesFromTheScreensItCanCapture)
{
#ifdef _WIN32
    if (qEnvironmentVariableIsEmpty("LENS_GLASS_WGC_TEST")) {
        GTEST_SKIP() << "set LENS_GLASS_WGC_TEST=1 to open real capture sessions";
    }
    auto const all = monitors();
    ASSERT_FALSE(all.empty()) << "没有可枚举的输出: 这不是一个交互桌面会话";

    auto captured = 0;
    for (auto const& monitor : all) {
        auto capture  = lens::app::GlassCapture{monitor.name, monitor.rect, reinterpret_cast<quintptr>(monitor.handle)};
        auto frames   = 0;
        auto refusals = 0;
        auto elapsed  = QElapsedTimer{};
        elapsed.start();
        while (elapsed.elapsed() < 1000) {
            auto const read = capture.read(monitor.rect, 1.0, 16);
            if (read.refused) {
                ++refusals;
                break;
            }
            if (read.newFrame and not read.image.isNull()) {
                ++frames;
                // The sheet drops a frame that repeats the picture in force by this value; a source that leaves it out
                // would put every compositor frame back on the GUI thread (docs/adr/0087).
                EXPECT_NE(read.fingerprint, 0u) << "捕获到的帧没有带指纹，幕布无法跳过未改变的画面";
            }
        }
        auto const rate   = frames > 0 ? frames * 1000.0 / elapsed.elapsed() : 0.0;
        auto const status = refusals > 0
                                ? QStringLiteral("refused")
                                : QStringLiteral("available, %1 ms watched: %2 frames (%3/s)")
                                      .arg(elapsed.elapsed())
                                      .arg(frames)
                                      .arg(rate, 0, 'f', 1);
        std::printf("\n  屏 %s  桌面 %dx%d\n", monitor.name.toStdString().c_str(), monitor.rect.width(), monitor.rect.height());
        std::printf("      Windows Graphics Capture: %s\n", status.toStdString().c_str());
        if (refusals == 0) {
            ++captured;
            EXPECT_GT(frames, 0) << "捕获会话打开了一秒却没有送到一帧";
        }
        capture.close();
    }
    EXPECT_GT(captured, 0) << "这台机器的所有输出都拒绝了 Windows Graphics Capture, 玻璃只能留在 GDI";
#else
    GTEST_SKIP() << "Windows Graphics Capture 只在 Windows 上存在";
#endif
}
