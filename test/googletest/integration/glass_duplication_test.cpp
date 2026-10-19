/**
 * @file glass_duplication_test.cpp
 * @brief Whether a duplicated output's crop really shows what is at that place on the screen, rotations included.
 *
 * The frame the desktop duplication hands over is in the panel's orientation, while the glass knows the desktop's:
 * the measurement machine's external monitor is 1440 x 2560 on the desktop and a 2560 x 1440 frame, turned 90 degrees.
 * A wrong turn or a wrong rectangle produces a picture that is plausible and in the wrong place, which no unit test can
 * see, so this paints a four-colour window at a known spot on every output, reads it through GlassDuplication and
 * checks two things: the quadrant in each corner is the colour that was painted there, and the same crop read with GDI
 * holds the same pixels.
 *
 * It shows a 420 x 260 window for about a second, so it is off unless asked for:
 *
 *     ./scripts/build/build.bat --target lens_gtest_integration
 *     LENS_GLASS_DUP_TEST=1 PATH="$QT_ROOT/bin:$PATH" \
 *       ./build-ninja/test/googletest/lens_gtest_integration.exe --gtest_filter='GlassDuplication.*'
 *
 * It needs an interactive desktop session (a CI runner has none) and prints each screen's verdict rather than stopping
 * at the first one, because the point of docs/adr/0072 is that the answer is per output. Only pixel counts and colours
 * are printed; no frame is written or kept (docs/adr/0034, SECURITY.md).
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <gtest/gtest.h>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QString>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32

#include "app/glass_desktop.h"
#include "app/glass_duplication.h"

namespace {

using lens::app::GlassDuplication;
using lens::app::readDesktopRegion;
using lens::app::SheetScale;

/// The window the test paints: big enough to survive the blur's edge and small enough for a portrait screen.
constexpr int kCardWidth  = 420;
constexpr int kCardHeight = 260;
/// How far past the window the sheet reaches, in device pixels. The glass does not need this here, but the crop is the
/// same shape as a real one, so the margins are part of what is compared.
constexpr int kMargin = 40;
/// How far a sampled channel may be from the painted one. Composition is exact for a solid colour, but the display path
/// may apply a colour transform; this is wide enough for that and far narrower than the distance between two quadrants.
constexpr int kColourTolerance = 32;
/// The colours the four quadrants are filled with, in window order: top-left, top-right, bottom-left, bottom-right.
constexpr COLORREF kQuadrants[4] = {RGB(220, 40, 40), RGB(40, 180, 60), RGB(40, 70, 210), RGB(240, 240, 240)};

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

/// Paint the window in four solid quadrants, so a turned or shifted crop cannot pass by luck.
LRESULT CALLBACK quadrantWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message != WM_PAINT) return DefWindowProcW(window, message, wParam, lParam);
    auto       paint = PAINTSTRUCT{};
    auto const dc    = BeginPaint(window, &paint);
    auto       area  = RECT{};
    GetClientRect(window, &area);
    auto const halfX = (area.right - area.left) / 2;
    auto const halfY = (area.bottom - area.top) / 2;
    auto const parts = std::array<RECT, 4>{
        RECT{0, 0, halfX, halfY},
        RECT{halfX, 0, area.right, halfY},
        RECT{0, halfY, halfX, area.bottom},
        RECT{halfX, halfY, area.right, area.bottom},
    };
    for (int i = 0; i < 4; ++i) {
        auto const brush = CreateSolidBrush(kQuadrants[i]);
        FillRect(dc, &parts[static_cast<std::size_t>(i)], brush);
        DeleteObject(brush);
    }
    EndPaint(window, &paint);
    return 0;
}

/// @brief The test window centred on @p screen. The class is registered once per process.
/// @return The window, or nullptr when the class or the window could not be made.
HWND makeWindow(QRect const screen)
{
    static auto registered = false;
    if (not registered) {
        auto description          = WNDCLASSEXW{};
        description.cbSize        = sizeof(description);
        description.lpfnWndProc   = quadrantWndProc;
        description.hInstance     = GetModuleHandleW(nullptr);
        description.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        description.lpszClassName = L"LensGlassDuplicationTest";
        if (RegisterClassExW(&description) == 0) {
            std::printf("  window class: error %lu\n", static_cast<unsigned long>(GetLastError()));
            return nullptr;
        }
        registered = true;
    }
    auto const left = screen.left() + (screen.width() - kCardWidth) / 2;
    auto const top  = screen.top() + (screen.height() - kCardHeight) / 2;
    return CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"LensGlassDuplicationTest", L"Lens duplication test", WS_POPUP, left, top, kCardWidth, kCardHeight, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

/// @brief Pump this thread's messages and repaint @p window for @p milliseconds.
void keepPainting(HWND window, int const milliseconds)
{
    auto const deadline = GetTickCount64() + static_cast<ULONGLONG>(milliseconds);
    while (GetTickCount64() < deadline) {
        auto message = MSG{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        InvalidateRect(window, nullptr, TRUE);
        UpdateWindow(window);
        Sleep(16);
    }
}

/// What waiting for a frame produced.
struct FrameWait {
    QImage image;
    bool   usable = true; ///< False when the session fell back to GDI; true with no picture means no frame arrived.
};

/// @brief Poll @p session for a new frame and return the crop, repainting @p window while it waits.
FrameWait nextFrame(GlassDuplication& session, QRect const sheet, HWND window, int const milliseconds)
{
    auto const deadline = GetTickCount64() + static_cast<ULONGLONG>(milliseconds);
    while (GetTickCount64() < deadline) {
        auto read = session.read(sheet, 1.0);
        if (read.newFrame and not read.image.isNull()) return {std::move(read.image), true};
        if (not read.usable) return {{}, false};
        keepPainting(window, 16);
    }
    return {};
}

/// @return How far @p colour is from @p reference, in the largest channel difference.
int distance(QColor const& colour, QColor const& reference)
{
    return std::max({std::abs(colour.red() - reference.red()), std::abs(colour.green() - reference.green()), std::abs(colour.blue() - reference.blue())});
}

/// @return The fraction of sampled pixels in @p area where @p left and @p right agree within twelve levels.
double agreement(QImage const& left, QImage const& right, QRect const area)
{
    if (area.isEmpty()) return 0.0;
    auto matching = 0;
    auto total    = 0;
    for (int y = area.top(); y <= area.bottom(); y += 4)
        for (int x = area.left(); x <= area.right(); x += 4) {
            auto const a = left.pixel(x, y);
            auto const b = right.pixel(x, y);
            auto const d = std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
            ++total;
            if (d <= 12) ++matching;
        }
    return total == 0 ? 0.0 : static_cast<double>(matching) / total;
}

/// @brief Try one output: paint a window on it, crop it from a duplicated frame, and check the crop.
/// @return True when the output duplicated and the crop matched the painted window.
bool testOutput(Monitor const& monitor)
{
    auto const card  = QRect(monitor.rect.left() + (monitor.rect.width() - kCardWidth) / 2,
                            monitor.rect.top() + (monitor.rect.height() - kCardHeight) / 2,
                            kCardWidth,
                            kCardHeight);
    auto const sheet = card.adjusted(-kMargin, -kMargin, kMargin, kMargin);
    if (not monitor.rect.contains(sheet)) {
        std::printf("  %s: 窗口放不下, 跳过\n", monitor.device.toStdString().c_str());
        return false;
    }

    auto session = GlassDuplication(monitor.device, monitor.rect);
    auto window  = makeWindow(monitor.rect);
    if (window == nullptr) {
        std::printf("  %s: 建不出测试窗口\n", monitor.device.toStdString().c_str());
        return false;
    }
    ShowWindow(window, SW_SHOWNA);
    keepPainting(window, 300);

    auto const frame = nextFrame(session, sheet, window, 1500);
    // GDI is read while the window is still up, so the two pictures show the same desktop.
    auto const gdi = readDesktopRegion(sheet, SheetScale::Full, true);
    DestroyWindow(window);
    if (not frame.usable) {
        std::printf("  %s: 复制不可用, 该屏留在 GDI\n", monitor.device.toStdString().c_str());
        return false;
    }
    if (frame.image.isNull()) {
        std::printf("  %s: 1.5 秒内没有新帧\n", monitor.device.toStdString().c_str());
        return false;
    }
    if (gdi.isNull()) {
        std::printf("  %s: GDI 读不到桌面\n", monitor.device.toStdString().c_str());
        return false;
    }

    // The four quadrant centres, in the crop's own coordinates. Each must show the colour painted at that corner.
    auto const cardInCrop = card.translated(-sheet.topLeft());
    auto const expected   = std::array<QColor, 4>{
        QColor(GetRValue(kQuadrants[0]), GetGValue(kQuadrants[0]), GetBValue(kQuadrants[0])),
        QColor(GetRValue(kQuadrants[1]), GetGValue(kQuadrants[1]), GetBValue(kQuadrants[1])),
        QColor(GetRValue(kQuadrants[2]), GetGValue(kQuadrants[2]), GetBValue(kQuadrants[2])),
        QColor(GetRValue(kQuadrants[3]), GetGValue(kQuadrants[3]), GetBValue(kQuadrants[3])),
    };
    auto const samples  = std::array<QPointF, 4>{QPointF(0.25, 0.25), QPointF(0.75, 0.25), QPointF(0.25, 0.75), QPointF(0.75, 0.75)};
    auto       readable = true;
    for (int i = 0; i < 4; ++i) {
        auto const px  = cardInCrop.left() + static_cast<int>(cardInCrop.width() * samples[i].x());
        auto const py  = cardInCrop.top() + static_cast<int>(cardInCrop.height() * samples[i].y());
        auto const rgb = frame.image.pixel(px, py);
        auto const got = QColor(qRed(rgb), qGreen(rgb), qBlue(rgb));
        if (distance(got, expected[static_cast<std::size_t>(i)]) > kColourTolerance) {
            std::printf("  %s: 象限 %d 是 (%d,%d,%d), 画的是 (%d,%d,%d) —— 旋转或裁剪不对\n",
                        monitor.device.toStdString().c_str(),
                        i,
                        got.red(),
                        got.green(),
                        got.blue(),
                        expected[static_cast<std::size_t>(i)].red(),
                        expected[static_cast<std::size_t>(i)].green(),
                        expected[static_cast<std::size_t>(i)].blue());
            readable = false;
        }
    }
    // Only the painted window is compared between the two reads: the margins show the live desktop, which may move
    // between the two grabs (the wallpaper of the measurement machine animates) and would make the comparison noise.
    auto const agree = agreement(frame.image, gdi, cardInCrop.adjusted(2, 2, -2, -2));
    std::printf("  %s: %s, 复制与 GDI 相同像素 %.1f%%, 帧 %dx%d\n", monitor.device.toStdString().c_str(), readable ? "象限对得上" : "象限对不上", agree * 100.0, frame.image.width(), frame.image.height());
    return readable and agree >= 0.90;
}

} // namespace

/// @brief Every output: does its duplicated frame show the window painted on it, in the right place and orientation?
TEST(GlassDuplication, CropFollowsTheRotatedScreen)
{
    auto const* const requested = std::getenv("LENS_GLASS_DUP_TEST");
    if (requested == nullptr or std::string(requested) != "1")
        GTEST_SKIP() << "会在屏上显示一个四色窗口约一秒; 设 LENS_GLASS_DUP_TEST=1 才跑";

    auto const all = monitors();
    ASSERT_FALSE(all.empty()) << "没有输出: 这不是一个交互桌面会话";
    auto duplicated = 0;
    for (auto const& monitor : all)
        if (testOutput(monitor)) ++duplicated;
    std::printf("\n  要点: 至少一块屏通过 = 复制裁剪(含旋转)可用于那段屏幕; 每块屏独立判定, 失败的那块回 GDI。\n");
    EXPECT_GT(duplicated, 0) << "没有一块屏的复制裁剪与画上去的窗口一致";
}

#else

TEST(GlassDuplication, CropFollowsTheRotatedScreen)
{
    GTEST_SKIP() << "桌面复制只在 Windows 上存在";
}

#endif
