/**
 * @file mouse_selection_hook.cpp
 * @brief The gesture rule, and the low-level mouse hook that feeds it.
 */

#include "mouse_selection_hook.h"

#include <cstdlib>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>

#include "capture/capture_policy.h"
#include "util/log.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max
// among them), and including it first would let them loose on the standard library.
#include <windows.h>

namespace lens::app {
namespace {

constexpr int kDragSlopPx = 4;

/**
 * The gesture bookkeeping the callback accumulates, and the system thresholds it judges
 * against. File scope rather than members, because LowLevelMouseProc is a free function:
 * Windows hands it no user data, so it has nothing to reach an instance through.
 */
struct GestureTracker {
    int  downX         = 0;
    int  downY         = 0;
    bool overOwnWindow = false;

    void onPress(int x, int y, bool own) // NOLINT(bugprone-easily-swappable-parameters): onPress takes its 2 int arguments in a fixed positional order; the same-typed group cannot be reordered without changing the call's meaning
    {
        downX         = x;
        downY         = y;
        overOwnWindow = own;

        // A press on one of our own surfaces is not the start of a selection: the reader is
        // dragging a panel or working a control, and the text the pipeline would go on to copy
        // is whatever the application behind it has selected.
    }

    /// @return Where the button came up, when the gesture was a selection.
    std::optional<QPoint> onRelease(int x, int y) const
    {
        if (overOwnWindow) return std::nullopt;

        const Gesture gesture{downX, downY, x, y};
        if (!isSelectionGesture(gesture, kDragSlopPx)) return std::nullopt;
        return QPoint(x, y);
    }
};

/// @return True when the point is over a window this process owns.
///
/// @note Deliberately the window and not the card: a surface's transparent shadow margin counts
///       as ours too, so a selection started within 26 pixels of a panel is dropped as well. The
///       alternative -- asking the surfaces for their card rectangles -- would put the geometry
///       in two places, and the cost of this is one missed action bar in a place the reader was
///       already reaching past a panel to begin with.
bool overOurWindow(POINT pt)
{
    const HWND under = WindowFromPoint(pt);
    if (under == nullptr) return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(under, &pid);
    return pid == GetCurrentProcessId();
}

GestureTracker      g_tracker;
MouseSelectionHook* g_owner = nullptr;

HHOOK                   g_handle       = nullptr;
DWORD                   g_hookThreadId = 0;
DWORD                   g_hookError    = 0;
std::thread             g_hookThread;
std::mutex              g_startMutex;
std::condition_variable g_started;
bool                    g_startDone = false;

void postToOwner(MouseSelectionHook* owner, std::function<void()> report)
{
    QMetaObject::invokeMethod(owner, std::move(report), Qt::QueuedConnection); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): QMetaObject::invokeMethod transfers the queued lambda into a QMetaCallEvent; the analyzer cannot see that ownership transfer
}

void emitPressedLater(const POINT& pt)
{
    MouseSelectionHook* const owner = g_owner;
    const QPoint              at(pt.x, pt.y);
    postToOwner(owner, [owner, at] { emit owner->pointerPressed(at); });
}

LRESULT CALLBACK lowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HC_ACTION && g_owner != nullptr) {                          // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): QMetaObject::invokeMethod transfers the queued lambda into a QMetaCallEvent; the analyzer cannot see that ownership transfer
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam); // NOLINT(performance-no-int-to-ptr): the Win32 API hands this handle back as an integer; the cast is the documented round-trip
        switch (wParam) {
            case WM_LBUTTONDOWN:
                // WindowFromPoint is a single lookup, on the one message where the answer is
                // needed; the callback's budget is the reason the rest of the work is deferred.
                g_tracker.onPress(info->pt.x, info->pt.y, overOurWindow(info->pt));
                emitPressedLater(info->pt);
                break;

            case WM_RBUTTONDOWN:
                emitPressedLater(info->pt);
                break;

            case WM_LBUTTONUP:
                if (const auto anchor = g_tracker.onRelease(info->pt.x, info->pt.y)) {
                    // Emitted from file scope because the callback must be a free function; Qt's
                    // signals are public, so this is legal.
                    MouseSelectionHook* const owner = g_owner;
                    postToOwner(owner, [owner, point = *anchor] { emit owner->selectionReleased(point); });
                }
                break;

            default:
                break;
        }
    }

    // Always chain on: something else may legitimately have installed a hook too.
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

/// @brief Runs on the hook's own thread: install, then pump until asked to stop.
void hookThreadMain()
{
    // A thread gets its message queue on the first peek, and PostThreadMessage cannot reach this
    // thread until it has one -- which is how the destructor asks it to leave.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    g_handle = SetWindowsHookExW(WH_MOUSE_LL, &lowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    {
        const std::lock_guard<std::mutex> lock(g_startMutex);
        g_hookThreadId = GetCurrentThreadId();
        g_hookError    = g_handle == nullptr ? GetLastError() : 0;
        g_startDone    = true;
    }
    g_started.notify_all();

    if (g_handle == nullptr)
        return;

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Unhooked here rather than from the destructor: this is the thread the callback runs on, so
    // it is the only one that can know no callback is in flight.
    UnhookWindowsHookEx(g_handle);
    g_handle = nullptr;
    g_owner  = nullptr;
}

} // namespace

bool isSelectionGesture(const Gesture& gesture, int dragSlopPx)
{
    const auto dx = std::int64_t{gesture.upX} - gesture.downX;
    const auto dy = std::int64_t{gesture.upY} - gesture.downY;
    return capture::exceedsDragThreshold(dx, dy, dragSlopPx);
}

MouseSelectionHook::MouseSelectionHook(QObject* parent)
    : QObject(parent)
{}

MouseSelectionHook::~MouseSelectionHook()
{
    if (!installed_ || g_owner != this)
        return;

    // Ask the hook's thread to leave and wait for it: it unhooks on the way out, from the thread
    // the callback runs on, which is the only place that can know none is in flight.
    if (g_hookThreadId != 0)
        PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
    if (g_hookThread.joinable())
        g_hookThread.join();
    installed_ = false;
}

bool MouseSelectionHook::install()
{
    if (installed_) return true;

    g_owner = this;
    {
        std::unique_lock<std::mutex> lock(g_startMutex);
        g_startDone  = false;
        g_hookThread = std::thread(hookThreadMain);
        g_started.wait(lock, [] { return g_startDone; });
    }

    if (g_handle == nullptr) {
        LENS_CRITICAL("MouseSelectionHook::install: SetWindowsHookEx failed with error {}", g_hookError);
        g_owner = nullptr;
        g_hookThread.join();
        return false;
    }

    installed_ = true;
    LENS_INFO("MouseSelectionHook::install: listening on its own thread ({} px drag slop; click-to-select disabled)", kDragSlopPx);
    return true;
}

}
