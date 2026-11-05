/** @file alt_key_hook.cpp
 * @brief The Alt state machine and the Win32 keyboard hook around it.
 */
#include "alt_key_hook.h"

#include <QMetaObject>

#include <utility>

// Last, after the Qt headers: windows.h brings a few hundred macros with it (min and max among them).
#include <windows.h>

#include "util/log.h"

namespace lens::app {
namespace {

constexpr unsigned kVkMenu  = VK_MENU;
constexpr unsigned kVkLMenu = VK_LMENU;
constexpr unsigned kVkRMenu = VK_RMENU;
/// Unassigned in every layout and bound by no application: pressed under Alt, it keeps the tap from waking the menu bar.
constexpr unsigned kCancelKey = 0xE8;

bool isAltKey(unsigned const vk)
{
    return vk == kVkMenu or vk == kVkLMenu or vk == kVkRMenu;
}

HHOOK                 g_hook  = nullptr;
AltKeyHook*           g_owner = nullptr;
std::function<bool()> g_guard;
AltHold               g_hold;

void pressCancelKey()
{
    INPUT inputs[2]{};
    inputs[0].type       = INPUT_KEYBOARD;
    inputs[0].ki.wVk     = static_cast<WORD>(kCancelKey);
    inputs[1].type       = INPUT_KEYBOARD;
    inputs[1].ki.wVk     = static_cast<WORD>(kCancelKey);
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

LRESULT CALLBACK lowLevelKeyboardProc(int const code, WPARAM const wParam, LPARAM const lParam)
{
    if (code == HC_ACTION) {
        auto const* const event    = reinterpret_cast<KBDLLHOOKSTRUCT const*>(lParam);
        bool const        down     = wParam == WM_KEYDOWN or wParam == WM_SYSKEYDOWN;
        bool const        up       = wParam == WM_KEYUP or wParam == WM_SYSKEYUP;
        bool const        injected = (event->flags & LLKHF_INJECTED) != 0;
        if (down or up) {
            bool const heldBefore = g_hold.held;
            // Injected events are not the reader's: the cancel key below must not count as a key under Alt.
            bool const lone = altReleaseIsLoneTap(g_hold, static_cast<unsigned>(event->vkCode), down, injected);
            if (lone and g_guard and g_guard()) pressCancelKey();
            if (g_owner and not injected and isAltKey(static_cast<unsigned>(event->vkCode)) and heldBefore != g_hold.held) {
                AltKeyHook* const owner   = g_owner;
                bool const        pressed = g_hold.held;
                QMetaObject::invokeMethod(
                    owner,
                    [owner, pressed] {
                        if (pressed)
                            emit owner->altPressed();
                        else
                            emit owner->altReleased();
                    },
                    Qt::QueuedConnection);
            }
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

}

bool altReleaseIsLoneTap(AltHold& state, unsigned const vk, bool const down, bool const injected)
{
    if (injected) return false;
    if (isAltKey(vk)) {
        if (down) {
            // Auto-repeat arrives while Alt is still held and changes nothing.
            if (not state.held) state = AltHold{.held = true};
            return false;
        }
        bool const lone = state.held and not state.chord;
        state           = AltHold{};
        return lone;
    }
    if (down and state.held) state.chord = true;
    return false;
}

AltKeyHook::AltKeyHook(QObject* const parent)
    : QObject(parent)
{}

AltKeyHook::~AltKeyHook()
{
    if (g_hook != nullptr) UnhookWindowsHookEx(g_hook);
    g_hook  = nullptr;
    g_owner = nullptr;
    g_guard = nullptr;
    g_hold  = AltHold{};
}

bool AltKeyHook::install()
{
    if (installed_) return true;
    g_owner = this;
    g_hook  = SetWindowsHookExW(WH_KEYBOARD_LL, &lowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    if (g_hook == nullptr) {
        g_owner = nullptr;
        LENS_WARN("AltKeyHook::install: SetWindowsHookEx failed with error {}; the pencil layer's Alt keys are off", GetLastError());
        return false;
    }
    installed_ = true;
    return true;
}

void AltKeyHook::setMenuGuard(std::function<bool()> guard)
{
    g_guard = std::move(guard);
}

}
