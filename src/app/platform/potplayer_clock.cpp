/** @file potplayer_clock.cpp
 * @brief The window-message clock and the media resolution behind potplayer_clock.h.
 */

#include "app/platform/potplayer_clock.h"

#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include <windows.h>
#include <shellapi.h>
#include <winternl.h>

namespace lens::app {
namespace {

/// @brief The queries the player answers with a WM_USER message, the payload in wParam.
enum class Query : std::uintptr_t {
    CurrentTimeMs = 20484,
    PlayStatus    = 20486, ///< 1 paused, 2 playing.
    VideoWidth    = 24624,
    VideoHeight   = 24625,
};

/// Long enough for an idle player, short enough that a hung one stalls one step, not the track.
constexpr UINT kQueryTimeoutMs = 40;
/// The playlist file is a list of paths; a megabyte holds every real one, and playname sits at
/// the top.
constexpr std::streamsize kMaxPlaylistBytes              = 1024LL * 1024;
constexpr ULONG           kProcessCommandLineInformation = 60;

std::optional<LRESULT> query(quintptr const window, Query const id)
{
    if (window == 0) return std::nullopt;
    DWORD_PTR  answer    = 0;
    auto const delivered = SendMessageTimeoutW(reinterpret_cast<HWND>(window), WM_USER, // NOLINT(performance-no-int-to-ptr): the Win32 API hands this handle back as an integer; the cast is the documented round-trip
                                               static_cast<WPARAM>(id),
                                               0,
                                               SMTO_ABORTIFHUNG | SMTO_BLOCK,
                                               kQueryTimeoutMs,
                                               &answer);
    if (delivered == 0) return std::nullopt;
    return static_cast<LRESULT>(answer);
}

std::wstring classNameOf(quintptr const window)
{
    wchar_t    name[64] = {};
    auto const length   = GetClassNameW(reinterpret_cast<HWND>(window), name, 64); // NOLINT(performance-no-int-to-ptr): see query
    return std::wstring{name, static_cast<std::size_t>(length > 0 ? length : 0)};
}

bool sameFileName(std::filesystem::path const& candidate, std::wstring const& fileName)
{
    if (fileName.empty()) return false;
    auto const name = candidate.filename().wstring();
    return QString::fromStdWString(name).compare(QString::fromStdWString(fileName), Qt::CaseInsensitive) == 0;
}

std::wstring processPathOf(quintptr const window)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(reinterpret_cast<HWND>(window), &pid); // NOLINT(performance-no-int-to-ptr): see query
    if (pid == 0) return {};
    auto const process = std::unique_ptr<void, decltype(&CloseHandle)>{
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), &CloseHandle};
    if (not process) return {};
    wchar_t path[32768]{};
    DWORD   size = static_cast<DWORD>(std::size(path));
    if (not QueryFullProcessImageNameW(process.get(), 0, path, &size)) return {};
    return std::wstring{path, size};
}

std::wstring commandLineOf(DWORD const pid)
{
    using NtQueryInformationProcessFn = NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static auto const ntQuery         = reinterpret_cast<NtQueryInformationProcessFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (ntQuery == nullptr) return {};
    auto const process = std::unique_ptr<void, decltype(&CloseHandle)>{
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), &CloseHandle};
    if (not process) return {};
    ULONG needed = 0;
    ntQuery(process.get(), kProcessCommandLineInformation, nullptr, 0, &needed);
    if (needed == 0) return {};
    std::vector<char> buffer(needed);
    if (ntQuery(process.get(), kProcessCommandLineInformation, buffer.data(), needed, &needed) < 0) return {};
    auto const* const text = reinterpret_cast<UNICODE_STRING const*>(buffer.data());
    if (text->Buffer == nullptr or text->Length == 0) return {};
    return std::wstring{text->Buffer, text->Length / sizeof(wchar_t)};
}

std::vector<std::wstring> splitCommandLine(std::wstring const& commandLine)
{
    std::vector<std::wstring> arguments;
    if (commandLine.empty()) return arguments;
    int         count = 0;
    auto* const argv  = CommandLineToArgvW(commandLine.c_str(), &count);
    if (argv == nullptr) return arguments;
    arguments.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        arguments.emplace_back(argv[i]);
    LocalFree(reinterpret_cast<HLOCAL>(argv));
    return arguments;
}

std::optional<std::filesystem::path> playlistMediaPath(std::filesystem::path const& executable, std::wstring const& fileName)
{
    if (executable.empty()) return std::nullopt;
    std::error_code error;
    auto const      playlist = executable.parent_path() / L"Playlist" / (executable.stem().wstring() + L".dpl");
    auto const      size     = std::filesystem::file_size(playlist, error);
    if (error or size == 0 or size > 4 * kMaxPlaylistBytes) return std::nullopt;
    std::ifstream in{playlist, std::ios::binary};
    if (not in) return std::nullopt;
    std::string bytes(static_cast<std::size_t>(std::min<std::uintmax_t>(size, static_cast<std::uintmax_t>(kMaxPlaylistBytes))), '\0');
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    return mediaPathFromPlaylist(bytes, fileName);
}

}

bool isPotPlayerWindow(quintptr const window)
{
    if (window == 0) return false;
    auto const name = classNameOf(window);
    return name.rfind(L"PotPlayer", 0) == 0;
}

std::optional<std::int64_t> potplayerTimeMs(quintptr const window)
{
    auto const answer = query(window, Query::CurrentTimeMs);
    if (not answer) return std::nullopt;
    return static_cast<std::int64_t>(*answer);
}

std::optional<int> potplayerPlayStatus(quintptr const window)
{
    auto const answer = query(window, Query::PlayStatus);
    if (not answer) return std::nullopt;
    return static_cast<int>(*answer);
}

std::optional<QSize> potplayerVideoSize(quintptr const window)
{
    auto const width  = query(window, Query::VideoWidth);
    auto const height = query(window, Query::VideoHeight);
    if (not width or not height or *width <= 0 or *height <= 0) return std::nullopt;
    return QSize{static_cast<int>(*width), static_cast<int>(*height)};
}

std::optional<QRect> potplayerVideoRect(quintptr const window)
{
    if (window == 0) return std::nullopt;
    auto const hwnd = reinterpret_cast<HWND>(window); // NOLINT(performance-no-int-to-ptr): see query
    RECT       client{};
    if (not GetClientRect(hwnd, &client)) return std::nullopt;
    POINT origin{0, 0};
    if (not ClientToScreen(hwnd, &origin)) return std::nullopt;
    QRect area{origin.x, origin.y, client.right - client.left, client.bottom - client.top};
    if (area.isEmpty()) return std::nullopt;
    // PotPlayer draws its toolbar and control bar inside the client area; the video viewport is
    // the largest visible child window (1920x896 of a 1920x1021 client on the default skin).
    // Without a child to point at, the client area stays the best reading.
    QRect     viewport;
    long long viewportArea = 0;
    for (HWND child = GetWindow(hwnd, GW_CHILD); child != nullptr; child = GetWindow(child, GW_HWNDNEXT)) {
        if (not IsWindowVisible(child)) continue;
        RECT childRect{};
        if (not GetWindowRect(child, &childRect)) continue;
        QRect const clipped = QRect{childRect.left, childRect.top, childRect.right - childRect.left, childRect.bottom - childRect.top}
                                  .intersected(area);
        auto const childArea = static_cast<long long>(clipped.width()) * clipped.height();
        if (childArea > viewportArea) {
            viewportArea = childArea;
            viewport     = clipped;
        }
    }
    if (not viewport.isEmpty()) area = viewport;
    auto const video = potplayerVideoSize(window);
    if (not video) return area;
    QSize const fitted = video->scaled(area.size(), Qt::KeepAspectRatio);
    return QRect{area.center() - QPoint{fitted.width() / 2, fitted.height() / 2}, fitted};
}

std::wstring potplayerExecutablePath(quintptr const window)
{
    return processPathOf(window);
}

QString potplayerWindowTitle(quintptr const window)
{
    if (window == 0) return {};
    auto const hwnd   = reinterpret_cast<HWND>(window); // NOLINT(performance-no-int-to-ptr): see query
    auto const length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    auto const   copied = GetWindowTextW(hwnd, title.data(), length + 1);
    title.resize(static_cast<std::size_t>(copied > 0 ? copied : 0));
    return QString::fromStdWString(title);
}

QString potplayerFileNameFromTitle(QString const& title)
{
    QString name = title;
    if (name.endsWith(QLatin1String(" - PotPlayer"), Qt::CaseInsensitive)) name.chop(QLatin1String(" - PotPlayer").size());
    name = name.trimmed();
    if (name.compare(QLatin1String("PotPlayer"), Qt::CaseInsensitive) == 0) return {};
    return name;
}

std::optional<std::filesystem::path> potplayerMediaPath(quintptr const window)
{
    if (not isPotPlayerWindow(window)) return std::nullopt;
    auto const fileName = potplayerFileNameFromTitle(potplayerWindowTitle(window)).toStdWString();
    if (fileName.empty()) return std::nullopt;
    DWORD pid = 0;
    GetWindowThreadProcessId(reinterpret_cast<HWND>(window), &pid); // NOLINT(performance-no-int-to-ptr): see query
    if (pid != 0) {
        if (auto const fromArguments = mediaArgumentFrom(splitCommandLine(commandLineOf(pid)), fileName))
            return fromArguments;
    }
    return playlistMediaPath(std::filesystem::path{processPathOf(window)}, fileName);
}

std::optional<std::filesystem::path> mediaArgumentFrom(std::vector<std::wstring> const& arguments,
                                                       std::wstring const&              fileName)
{
    for (auto const& argument : arguments) {
        if (argument.find(L"://") != std::wstring::npos) continue; // a stream, not a file
        auto candidate = std::filesystem::path{argument};
        if (sameFileName(candidate, fileName)) return candidate;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> mediaPathFromPlaylist(std::string_view const playlistBytes,
                                                           std::wstring const&    fileName)
{
    QString body;
    if (playlistBytes.size() >= 2 and static_cast<unsigned char>(playlistBytes[0]) == 0xFF and
        static_cast<unsigned char>(playlistBytes[1]) == 0xFE) {
        body = QString::fromUtf16(reinterpret_cast<char16_t const*>(playlistBytes.data() + 2),
                                  static_cast<qsizetype>((playlistBytes.size() - 2) / 2));
    } else {
        auto view = playlistBytes;
        if (view.size() >= 3 and static_cast<unsigned char>(view[0]) == 0xEF and
            static_cast<unsigned char>(view[1]) == 0xBB and static_cast<unsigned char>(view[2]) == 0xBF)
            view.remove_prefix(3);
        body = QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
    }
    for (auto const& rawLine : body.split(QLatin1Char('\n'))) {
        auto line = rawLine;
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
        if (not line.startsWith(QLatin1String("playname="))) continue;
        auto const value = line.mid(static_cast<qsizetype>(QLatin1String("playname=").size()));
        if (value.isEmpty()) continue;
        auto candidate = std::filesystem::path{value.toStdWString()};
        if (sameFileName(candidate, fileName)) return candidate;
    }
    return std::nullopt;
}

}
