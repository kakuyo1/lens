/**
 * @file reporter.cpp
 * @brief The companion process a crash spawns: one native dialog, up the moment Lens dies.
 *
 * The crashing process cannot show UI of its own -- its heap and its render loop are the things
 * that just broke -- so crash_dump.cpp writes the dump, the summary and the marker, then starts
 * this one with the marker path and the dumps directory. Pure Win32 on purpose: no Qt to load,
 * no runtime state shared with the process that died.
 *
 * It reads the marker (the summary format lives in src/util/crash.cpp), picks its language from
 * the interface language in %APPDATA%\Lens\settings.json, and offers the dumps folder and the
 * prefilled issue. The strings are translated by hand and compiled in: the reporter has no Qt,
 * so the lupdate flow cannot reach it (docs/adr/0175) -- edit them here when wording changes,
 * and keep them beside the five languages the .ts files carry.
 */

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "util/crash.h"

namespace {

/// The wording of one language. Four sentences and two labels, no more than the dialog says.
struct Strings {
    const wchar_t* title;      ///< The window title.
    const wchar_t* body;       ///< The explanation; the facts and the folder path follow it.
    const wchar_t* openFolder; ///< The button that opens the dumps folder.
    const wchar_t* report;     ///< The button that opens the prefilled issue.
};

constexpr Strings kEnglish{
    L"Lens crashed",
    L"The previous run crashed. A crash dump and a summary were saved on this machine; nothing "
    L"in them leaves it unless you attach them yourself.",
    L"Open dump folder",
    L"Report issue",
};

constexpr Strings kSimplifiedChinese{
    L"Lens 崩溃了",
    L"上次运行崩溃了。崩溃转储与摘要已保存在本机；除非你亲手附上，它们不会离开这台机器。",
    L"打开转储文件夹",
    L"报告问题",
};

constexpr Strings kTraditionalChinese{
    L"Lens 已當機",
    L"上次執行已崩潰。崩潰轉儲與摘要已存放在本機；除非你親手附上，它們不會離開這台機器。",
    L"開啟轉儲資料夾",
    L"回報問題",
};

constexpr Strings kSpanish{
    L"Lens se interrumpió",
    L"La ejecución anterior se interrumpió. El volcado de memoria y el resumen quedaron "
    L"guardados en este equipo; no salen de él a menos que tú los adjuntes.",
    L"Abrir carpeta de volcados",
    L"Informar del problema",
};

constexpr Strings kJapanese{
    L"Lens がクラッシュしました",
    L"前回の実行がクラッシュしました。クラッシュ ダンプと概要はこのマシンに保存されています。"
    L"あなたが添付しない限り、外に出ることはありません。",
    L"ダンプ フォルダーを開く",
    L"問題を報告",
};

/// @brief The strings for one of the five interface languages, English for "en" and the unknown.
const Strings& stringsFor(std::string_view language)
{
    if (language == "zh") return kSimplifiedChinese;
    if (language == "zh-Hant") return kTraditionalChinese;
    if (language == "es") return kSpanish;
    if (language == "ja") return kJapanese;
    return kEnglish;
}

/// @brief The interface language the settings document carries: the raw value of "uiLanguage".
///
/// A hand scan rather than a JSON parser: the reporter must not depend on the shape of the
/// document beyond this one word, and an unreadable or absent document means English, the same
/// fallback the interface itself keeps.
std::string interfaceLanguage()
{
    wchar_t     appData[MAX_PATH];
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    std::ifstream file(std::filesystem::path(appData) / L"Lens" / L"settings.json");
    if (!file.is_open()) return {};
    std::ostringstream read;
    read << file.rdbuf();
    const std::string text = read.str();
    const std::string key  = "\"uiLanguage\"";
    const auto        at   = text.find(key);
    if (at == std::string::npos) return {};
    const auto colon = text.find(':', at + key.size());
    const auto quote = text.find('"', colon == std::string::npos ? at : colon);
    if (colon == std::string::npos || quote == std::string::npos) return {};
    const auto end = text.find('"', quote + 1);
    if (end == std::string::npos) return {};
    return text.substr(quote + 1, end - quote - 1);
}

/// @brief UTF-8 to UTF-16, for the summary fields and the issue URL the helpers return.
std::wstring widened(std::string_view utf8)
{
    if (utf8.empty()) return {};
    const int    chars = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(chars, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), chars);
    return wide;
}

/// Control ids in the dialog.
constexpr int kOpenFolder = 1001;
constexpr int kReport     = 1002;

/// @brief The DPI awareness call, taken by name: the function ships in every Windows 10 up, but
///       its exact type sits behind an SDK guard this file need not depend on.
using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(void*);

Strings                  strings;         ///< The wording of the language this run picked.
lens::util::CrashSummary summaryOfRun;    ///< The summary the marker carried, parsed once.
std::wstring             dumpsDir;        ///< The folder the crash artifacts sit in, from the command line.
std::wstring             bodyText;        ///< The full body paragraph, facts and folder path included.
RECT                     bodyRect   = {}; ///< The body static's client rectangle, measured before creation.
RECT                     folderRect = {}; ///< The open-folder button's client rectangle.
RECT                     reportRect = {}; ///< The report button's client rectangle.

/// @brief One DPI unit: every layout measure goes through it, 96 dpi is the base.
int scaled(int value, UINT dpi)
{
    return MulDiv(value, static_cast<int>(dpi), 96);
}

/// @brief The system message font, which is what a dialog of Windows' own would draw with.
HFONT messageFont()
{
    NONCLIENTMETRICSW metrics{};
    metrics.cbSize = sizeof metrics;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
    return CreateFontIndirectW(&metrics.lfMessageFont);
}

/// @brief The icon the package knows: the same lens.ico the main executable carries.
HICON reportIcon(HINSTANCE instance)
{
    if (HICON icon = LoadIconW(instance, L"IDI_LENS"); icon != nullptr) return icon;
    return LoadIconW(nullptr, IDI_APPLICATION);
}

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_CREATE: {
            const UINT  dpi  = GetDpiForWindow(window);
            const HFONT font = messageFont();
            CreateWindowExW(0, L"STATIC", bodyText.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT, bodyRect.left, bodyRect.top, bodyRect.right - bodyRect.left, bodyRect.bottom - bodyRect.top, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(0)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", strings.openFolder, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, folderRect.left, folderRect.top, folderRect.right - folderRect.left, folderRect.bottom - folderRect.top, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOpenFolder)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", strings.report, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, reportRect.left, reportRect.top, reportRect.right - reportRect.left, reportRect.bottom - reportRect.top, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kReport)), nullptr, nullptr);
            // The system message font is not a default; without the assignment every control
            // draws in the UI font of 1995.
            EnumChildWindows(window, [](HWND child, LPARAM font) -> BOOL {
                                 SendMessageW(child, WM_SETFONT, static_cast<WPARAM>(font), TRUE);
                                 return TRUE; }, reinterpret_cast<LPARAM>(font));
            SetFocus(window);
            return 0;
        }
        case WM_COMMAND: {
            if (HIWORD(wParam) != BN_CLICKED) break;
            switch (LOWORD(wParam)) {
                case kOpenFolder:
                    ShellExecuteW(window, L"open", dumpsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                case kReport: {
                    // Built fresh on the press: the builder is pure, and the dialog is the only
                    // window this process will ever own.
                    const std::wstring url = widened(lens::util::crashIssueUrl(summaryOfRun));
                    ShellExecuteW(window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                    return 0;
                }
                default:
                    break;
            }
            break;
        }
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    int     argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) return 0;
    // argv[1] is the marker the handler wrote last, its content the summary; argv[2] the folder
    // the artifacts sit in. Anything else is a run nothing asked for, and the dialog stays down.
    if (argc < 3) {
        LocalFree(argv);
        return 0;
    }
    std::ifstream      markerFile(argv[1]);
    std::ostringstream read;
    read << markerFile.rdbuf();
    const lens::util::CrashSummary summary = lens::util::parseCrashSummary(read.str());
    summaryOfRun                           = summary;
    dumpsDir                               = argv[2];
    strings                                = stringsFor(interfaceLanguage());
    LocalFree(argv);

    // The body carries the explanation, the facts the summary kept, and the folder it all sits
    // in -- the three things a reader needs before the next-launch card repeats them.
    bodyText = strings.body;
    std::wstring facts;
    if (!summary.code.empty()) facts += widened(summary.code);
    if (!summary.time.empty()) {
        facts += L" · ";
        facts += widened(summary.time);
    }
    if (!facts.empty()) {
        bodyText += L"\n\n";
        bodyText += facts;
    }
    bodyText += L"\n\n";
    bodyText += dumpsDir;

    if (const auto setDpi = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32"), "SetProcessDpiAwarenessContext"))))
        setDpi(reinterpret_cast<void*>(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    WNDCLASSEXW windowClass{sizeof windowClass};
    windowClass.style         = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc   = &procedure;
    windowClass.hInstance     = instance;
    windowClass.hIcon         = reportIcon(instance);
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"LensReporter";
    RegisterClassExW(&windowClass);

    // Size the client to its text: measure the body at the card's width, hang the button row
    // below, and let a long Spanish or Japanese paragraph grow the dialog instead of clipping.
    const UINT dpi       = GetDpiForSystem();
    const int  margin    = scaled(18, dpi);
    const int  cardWidth = scaled(430, dpi);
    const int  textWidth = cardWidth - 2 * margin;
    const int  buttonH   = scaled(34, dpi);
    const int  buttonGap = scaled(10, dpi);
    const int  rowGap    = scaled(14, dpi);

    const HDC     screen   = GetDC(nullptr);
    const HFONT   font     = messageFont();
    const HGDIOBJ earlier  = SelectObject(screen, font);
    RECT          measured = {0, 0, textWidth, 0};
    DrawTextW(screen, bodyText.c_str(), -1, &measured, DT_CALCRECT | DT_WORDBREAK);
    SelectObject(screen, earlier);
    ReleaseDC(nullptr, screen);

    bodyRect   = {margin, margin, margin + textWidth, margin + measured.bottom};
    reportRect = {cardWidth - margin - scaled(150, dpi), bodyRect.bottom + rowGap, cardWidth - margin, bodyRect.bottom + rowGap + buttonH};
    folderRect = {reportRect.left - buttonGap - scaled(150, dpi), reportRect.top, reportRect.left - buttonGap, reportRect.bottom};

    RECT outer = {0, 0, cardWidth, reportRect.bottom + margin};
    AdjustWindowRect(&outer, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    const int width  = outer.right - outer.left;
    const int height = outer.bottom - outer.top;

    // On the primary screen, centred: the crash could have happened anywhere, and the one screen
    // every layout knows is where a reader looks first.
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

    const HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, L"LensReporter", strings.title, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, x, y, width, height, nullptr, nullptr, instance, nullptr);
    if (window != nullptr) {
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return 0;
}
