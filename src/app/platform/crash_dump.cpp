/**
 * @file crash_dump.cpp
 * @brief The Win32 half of crash forensics: one funnel into one set of artifacts, written with
 *        nothing that could itself be broken.
 *
 * Every handler -- the SEH filter and the four CRT funnels -- lands in writeArtifacts() behind a
 * once-flag: a second fault inside the first one's writer only escalates the death. The writer
 * touches Win32 file APIs and dbghelp alone, never spdlog or the Qt heap, because the heap a
 * crash would want to describe is the one the writer must not disturb. A crash therefore leaves
 * no log line; the next launch's reportStartupCrash() is the record that the chain ran.
 */

#include "app/platform/crash_dump.h"

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include "util/crash.h"

namespace lens::app {
namespace {

/// Pseudo-codes the CRT funnels crash with, above any code Windows itself raises, so a summary
/// reads without this file's table at hand.
constexpr unsigned kCodeTerminate        = 0xE0000001; // uncaught C++ exception, via std::terminate
constexpr unsigned kCodeInvalidParameter = 0xE0000002; // CRT invalid parameter
constexpr unsigned kCodePureCall         = 0xE0000003; // pure virtual call
constexpr unsigned kCodeAbort            = 0xE0000004; // abort(), qFatal included

/// Set by whichever funnel arrives first: the artifacts exist, and every later arrival dies
/// instead of writing a file the first writer has open.
std::atomic_flag g_artifactsWritten;

std::filesystem::path g_dumpDir;

/// @brief Wide to UTF-8, for the module name a non-ASCII install can carry.
std::string utf8(const std::wstring& wide)
{
    if (wide.empty()) return {};
    const int   bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string narrow(bytes, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), narrow.data(), bytes, nullptr, nullptr);
    return narrow;
}

/// @brief Local time, "YYYYMMDD-HHMMSS": the compact form the dump stem carries.
std::string localTimeCompact()
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    char text[32];
    std::snprintf(text, sizeof text, "%04u%02u%02u-%02u%02u%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    return text;
}

/// @brief Local time, "YYYY-MM-DD HH:MM:SS": the readable form the summary carries.
std::string localTimeReadable()
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    char text[32];
    std::snprintf(text, sizeof text, "%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    return text;
}

/// @brief The module holding @p address, file name only: the best effort a dying process makes.
std::string moduleOf(const void* address)
{
    HMODULE module = nullptr;
    if (address == nullptr || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                  static_cast<LPCWSTR>(address),
                                                  &module))
        return {};
    wchar_t     path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    std::wstring_view name(path, length);
    const auto        slash = name.find_last_of(L'\\');
    if (slash != std::wstring_view::npos) name.remove_prefix(slash + 1);
    return utf8(std::wstring(name));
}

/// @brief The instruction a context stopped at, for the synthetic record the CRT funnels build.
void* instructionOf(CONTEXT& context)
{
#if defined(_WIN64)
    return reinterpret_cast<void*>(context.Rip);
#else
    return reinterpret_cast<void*>(context.Eip);
#endif
}

/// @brief Write one small text file; a failure here is silent, the marker simply never appears.
void writeTextFile(const std::filesystem::path& path, std::string_view text)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

/// @brief The dump, then the summary, then the marker, in that order: the marker's presence is
///        the handoff, and it is only honest once what it points at is on the disk.
void writeArtifacts(DWORD code, EXCEPTION_POINTERS* pointers)
{
    if (g_artifactsWritten.test_and_set()) return;

    const std::string           stem        = lens::util::crashDumpStem(LENS_VERSION, localTimeCompact(), GetCurrentProcessId());
    const std::filesystem::path dumpPath    = g_dumpDir / (stem + ".dmp");
    const std::filesystem::path summaryPath = g_dumpDir / (stem + ".crash.txt");

    // The dump first: it is the one artifact that needs the process alive, and failing there
    // must not keep the summary and the marker from landing.
    bool   dumpWritten = false;
    HANDLE file        = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        // MiniDumpNormal keeps the stacks and the module table, which is what a faulting address
        // is read against with the PDBs a build tree has. The fuller dump is for a by-hand round
        // when a stack alone does not say enough: it carries the heap the reader's words live in,
        // so it is never the default (SECURITY.md).
        MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithUnloadedModules);
        if (GetEnvironmentVariableW(L"LENS_CRASH_FULL_DUMP", nullptr, 0) != 0)
            dumpType = static_cast<MINIDUMP_TYPE>(dumpType | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithProcessThreadData);
        MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), pointers, FALSE};
        dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, dumpType, pointers != nullptr ? &exception : nullptr, nullptr, nullptr) != FALSE;
        CloseHandle(file);
        // A failed write leaves a file behind that no tool opens; the summary says a dump exists
        // only when one does.
        if (!dumpWritten) DeleteFileW(dumpPath.c_str());
    }

    const void* faultAddress = nullptr;
    if (pointers != nullptr && pointers->ExceptionRecord != nullptr)
        faultAddress = pointers->ExceptionRecord->ExceptionAddress;

    char codeText[16];
    std::snprintf(codeText, sizeof codeText, "0x%08X", code);
    char addressText[32];
    if (faultAddress != nullptr)
        std::snprintf(addressText, sizeof addressText, "0x%016llX", static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(faultAddress)));

    lens::util::CrashSummary summary;
    summary.version               = LENS_VERSION;
    summary.time                  = localTimeReadable();
    summary.code                  = codeText;
    summary.address               = faultAddress != nullptr ? addressText : "";
    summary.module                = moduleOf(faultAddress);
    summary.dump                  = dumpWritten ? dumpPath.filename().string() : "";
    const std::string summaryText = lens::util::serializeCrashSummary(summary);

    writeTextFile(summaryPath, summaryText);
    writeTextFile(g_dumpDir / lens::util::kCrashMarkerName, summaryText);
}

/// @brief The entry the four CRT funnels share: capture this thread's context and build the
///        record the SEH filter would have carried, so the dump shows where the funnel was.
void crashWithSyntheticRecord(unsigned code)
{
    CONTEXT context;
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode    = static_cast<DWORD>(code);
    record.ExceptionAddress = instructionOf(context);
    EXCEPTION_POINTERS pointers{&record, &context};
    writeArtifacts(record.ExceptionCode, &pointers);
}

/// @brief Death after the artifacts is TerminateProcess rather than another CRT path: abort()
///        would come back through this funnel with the once-flag already set, and a broken CRT
///        must not limp on.
void dieAfterArtifacts()
{
    TerminateProcess(GetCurrentProcess(), 3);
}

void terminateHandler()
{
    crashWithSyntheticRecord(kCodeTerminate);
    dieAfterArtifacts();
}

void pureCallHandler()
{
    crashWithSyntheticRecord(kCodePureCall);
    dieAfterArtifacts();
}

void invalidParameterHandler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    crashWithSyntheticRecord(kCodeInvalidParameter);
    dieAfterArtifacts();
}

void sigabrtHandler(int)
{
    crashWithSyntheticRecord(kCodeAbort);
    dieAfterArtifacts();
}

/// @brief The SEH entry. Artifacts first, then the search goes on: Windows' own reporting and a
///        debugger's second chance still happen, and the dying process owes nothing more.
LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* pointers)
{
    writeArtifacts(pointers != nullptr ? pointers->ExceptionRecord->ExceptionCode : 0, pointers);
    return EXCEPTION_CONTINUE_SEARCH;
}

}

void installCrashDumpHandler(const std::filesystem::path& logDir)
{
    g_dumpDir = lens::util::crashDumpDir(logDir);
    std::error_code ec;
    // A directory that cannot be made only means every write below will fail; the run goes on.
    std::filesystem::create_directories(g_dumpDir, ec);

    SetUnhandledExceptionFilter(&unhandledExceptionFilter);
    std::set_terminate(&terminateHandler);
    _set_purecall_handler(&pureCallHandler);
    _set_invalid_parameter_handler(&invalidParameterHandler);
    signal(SIGABRT, &sigabrtHandler);

    // The QA round: LENS_CRASH_TEST makes this launch die at once through the real SEH path, so
    // the whole chain -- dump, summary, marker, next-launch notice -- can be walked by hand
    // (docs/qa.md). The store through a null pointer is the crash; volatile keeps the compiler
    // from folding it away.
    if (GetEnvironmentVariableW(L"LENS_CRASH_TEST", nullptr, 0) != 0) {
        volatile int* nowhere = nullptr;
        *nowhere              = 1;
    }
}

}
