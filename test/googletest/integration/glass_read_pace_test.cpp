/**
 * @file glass_read_pace_test.cpp
 * @brief Whether the pace of the desktop read, rather than its size, is what ADR 0040's 13.4 ms turned into.
 *
 * Three numbers describe one quantity. ADR 0040 measured a 1075 × 1231 read at 13.4 ms and set the refresh budget from
 * it; the 2026-10-19 probe measures 25.2 ms for the same region read back to back, at 1/4 scale, on the same kind of
 * machine (docs/metrics/runtime-performance/glass-capture-capability-2026-10-19.md); and the in-application CPU record
 * of 2026-10-18 -- 5.0 s of CPU over 50 s with the refresh running -- divides out to about 6.7 ms a read
 * (docs/metrics/resource-footprint/glass-memory-2026-10-18.md 9.7). The frame rate and the whole CPU budget are decided
 * by which of these the application actually pays, and the surviving hypothesis for the four-fold spread is pacing:
 * GDI reads of the compositor's screen DC may serialize against what is being drawn, which a tight loop exaggerates and
 * a 67 ms timer hides.
 *
 * So this reads the same rectangle twice over, once back to back and once at the pace the application keeps, and prints
 * both. A second case answers the other half of the same budget: whether the price follows the *area* read, which is
 * what the "read less of the screen" lever is worth -- the sheet, the same sheet without its 160 DIP margin, and an
 * action bar's card, all three at the application's pace. No window, no sheet, no surface: the point is that nothing
 * else is in the measurement. The read is the application's own, taken from src/app/glass_desktop.h, and the
 * percentiles come from src/app/glass_stats.h -- the same reducer that writes the application's log line, so a number
 * here and a number in `logs/lens.log` mean the same thing.
 *
 * Like every target of this suite it prints rather than asserts on time. What it asserts is that each round really
 * read. Ninety seconds is the cost of one run; it is human-run because it reads the real screen, and a CI runner has
 * no desktop to read.
 *
 * Run:
 *
 *     ./scripts/build/build.bat --target lens_gtest_integration
 *     PATH="$QT_ROOT/bin:$PATH" ./build-ninja/test/googletest/lens_gtest_integration.exe --gtest_filter='GlassReadPace.*'
 *
 * Nothing is on the reader's card here: the region is whatever is on the primary screen at that moment, so close
 * anything private first, or let the glass surfaces be excluded -- the read is the same read either way.
 */

#include <gtest/gtest.h>

#include <QImage>
#include <QRect>

#include "app/glass_desktop.h"
#include "app/glass_stats.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

#ifdef _WIN32
#define NOMINMAX // std::max and std::min below are the templates, not windows.h's macros.
#include <windows.h>
#endif

namespace {

using lens::app::GlassStats;
using lens::app::readDesktopRegion;
using lens::app::SheetScale;

/// The sheet size ADR 0040 measured and the probe re-measured, so all three numbers describe the same rectangle.
constexpr int kSheetWidth  = 1075;
constexpr int kSheetHeight = 1231;
/// The refresh period the application keeps (src/app/glass_backdrop.cpp's kLiveIntervalMs, docs/adr/0040).
constexpr auto kPace = std::chrono::milliseconds(67);
/// How long a pacing round runs. Twenty seconds is about 300 paced reads, enough for a median to stop moving.
constexpr int kPaceRoundSeconds = 20;
/// How long an area round runs: about 150 paced reads, enough to set three sizes against each other.
constexpr int kAreaRoundSeconds = 10;
/// What the sheet is kept at today, and the resolution a GPU blur would let it be read at.
constexpr auto kQuarter = SheetScale::Quarter;
constexpr auto kFull    = SheetScale::Full;

/// The card that sits inside ADR 0040's sheet: the sheet less the 160 DIP margin on every side, at 125% scale.
constexpr int kCardWidth  = 675;
constexpr int kCardHeight = 831;
/// An action bar's card, the smallest glass surface there is: reading the card alone is what it would cost.
constexpr int kBarWidth  = 420;
constexpr int kBarHeight = 260;

/// @return A region of @p width by @p height, centred on the primary screen and clamped to it, in device pixels.
QRect sheetOf(int const width, int const height)
{
#ifdef _WIN32
    auto const monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    auto       info    = MONITORINFO{};
    info.cbSize        = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) == FALSE) return {};
    auto const screenWidth  = static_cast<int>(info.rcMonitor.right - info.rcMonitor.left);
    auto const screenHeight = static_cast<int>(info.rcMonitor.bottom - info.rcMonitor.top);
    // Centred when the screen is bigger than the rectangle, and flush with its corner when it is not: an origin moved
    // inward by a negative offset would leave the right edge off the screen.
    auto const left = width < screenWidth ? info.rcMonitor.left : info.rcMonitor.left + (screenWidth - width) / 2;
    auto const top  = height < screenHeight ? info.rcMonitor.top : info.rcMonitor.top + (screenHeight - height) / 2;
    return QRect{static_cast<int>(left), static_cast<int>(top), std::min(width, screenWidth), std::min(height, screenHeight)};
#else
    return {};
#endif
}

/**
 * @brief Read @p sheet for @p seconds, at @p pace or as fast as the machine allows.
 * @param sheet The region to read.
 * @param scale What to shrink it by on the way in.
 * @param paced Whether to wait for the next 67 ms boundary before each read.
 * @param stats Where to tally the durations.
 * @param seconds How long to keep reading.
 * @return How many reads landed.
 */
int readRound(QRect const sheet, SheetScale const scale, bool const paced, GlassStats& stats, int const seconds)
{
    auto const began = std::chrono::steady_clock::now();
    auto       reads = 0;
    while (std::chrono::steady_clock::now() - began < std::chrono::seconds(seconds)) {
        auto const started = std::chrono::steady_clock::now();
        auto const picture = readDesktopRegion(sheet, scale, true);
        auto const took    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (picture.isNull()) break; // No desktop to read: the round reports itself as unable to run.
        stats.addRead(took);
        ++reads;
        if (not paced) continue;
        // Wait out the rest of the period, the way the timer does: the next read starts 67 ms after this one began,
        // not 67 ms after it finished.
        auto const until = started + kPace;
        if (std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(until - std::chrono::steady_clock::now());
    }
    return reads;
}

/// @brief Run one round on a fresh tally and print it under @p label.
/// @return How many reads landed.
int report(QRect const sheet, SheetScale const scale, bool const paced, char const* const label, int const seconds)
{
    auto       stats = GlassStats();
    auto const reads = readRound(sheet, scale, paced, stats, seconds);
    std::printf("  %s (%ds, %.2f Mpx 源): %s\n", label, seconds, static_cast<double>(sheet.width()) * static_cast<double>(sheet.height()) / 1.0e6, stats.takeSummaryLine(seconds).toStdString().c_str());
    return reads;
}

TEST(GlassReadPace, PacedAgainstTight)
{
#ifdef _WIN32
    auto const sheet = sheetOf(kSheetWidth, kSheetHeight);
    ASSERT_FALSE(sheet.isEmpty()) << "没有主屏可读：这不是一个交互桌面会话";
    auto const first = readDesktopRegion(sheet, kQuarter, true);
    if (first.isNull()) GTEST_SKIP() << "GDI 读不到桌面，跳过";

    std::printf("\n  === 幕布 %dx%d 设备像素 ===\n", sheet.width(), sheet.height());
    // Same order every run: the last round sees the desktop the previous ones were drawing, so a comparison is only
    // read down this list.
    auto const quarterPaced = report(sheet, kQuarter, true, "按应用的节奏读 1/4", kPaceRoundSeconds);
    auto const quarterTight = report(sheet, kQuarter, false, "紧循环读 1/4", kPaceRoundSeconds);
    auto const fullPaced    = report(sheet, kFull, true, "按应用的节奏读全分辨率", kPaceRoundSeconds);
    // The claim stage 2 leans on -- that reading at full resolution costs what reading at a quarter costs -- is checked
    // at the application's pace, not only in the probe's tight loop.
    EXPECT_GT(quarterPaced, kPaceRoundSeconds * 5);
    EXPECT_GT(quarterTight, kPaceRoundSeconds * 20);
    EXPECT_GT(fullPaced, kPaceRoundSeconds * 5);
    std::printf("\n  要点: 两条 1/4 的中位数差多少，就是“节奏”值多少；两条按节奏的中位数差多少，就是 1/4 降采样省了多少。\n");
#else
    GTEST_SKIP() << "读屏只在 Windows 上存在";
#endif
}

/// @brief Whether the price of a read follows the area it covers, which is what the "read less" lever is worth.
TEST(GlassReadPace, AreaSetsThePrice)
{
#ifdef _WIN32
    auto const sheet = sheetOf(kSheetWidth, kSheetHeight);
    ASSERT_FALSE(sheet.isEmpty()) << "没有主屏可读：这不是一个交互桌面会话";
    if (readDesktopRegion(sheet, kQuarter, true).isNull()) GTEST_SKIP() << "GDI 读不到桌面，跳过";

    std::printf("\n  === 同一节奏、同一缩小比，只换源面积 ===\n");
    auto const whole = report(sheetOf(kSheetWidth, kSheetHeight), kQuarter, true, "整张幕布", kAreaRoundSeconds);
    auto const card  = report(sheetOf(kCardWidth, kCardHeight), kQuarter, true, "幕布去掉 160 DIP 边距后的卡片", kAreaRoundSeconds);
    auto const bar   = report(sheetOf(kBarWidth, kBarHeight), kQuarter, true, "动作条卡片", kAreaRoundSeconds);
    EXPECT_GT(whole, 0);
    EXPECT_GT(card, 0);
    EXPECT_GT(bar, 0);
    std::printf("\n  要点: 三条中位数随面积掉多少，缩小读取区域这条杠杆就值多少；掉得少，说明价与面积无关，得换采集方式。\n");
#else
    GTEST_SKIP() << "读屏只在 Windows 上存在";
#endif
}

} // namespace
