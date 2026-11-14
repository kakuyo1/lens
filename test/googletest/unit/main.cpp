/**
 * @file main.cpp
 * @brief The unit suite's own main, because the Debug CRT's diagnostics must reach stderr, and
 *        an abort must leave a stack behind.
 *
 * Under the default GTest::gtest_main, a Debug-CRT assert or an abort() raises the "Microsoft
 * Visual C++ Runtime Library" dialog, and a headless runner has nobody to click it -- the suite
 * sat on that dialog for half an hour on CI, at a different test each run. Routing every report
 * mode to stderr puts the assert's text in the CI log and, like pressing Ignore, lets the run
 * continue to the next test. _CALL_REPORTFAULT is dropped from the abort behavior for the same
 * reason: abort() reports to stderr and exits instead of showing a box.
 *
 * A bare abort() -- one with no assert text and no terminate message before it -- still says
 * nothing about where it came from, which is what the ASan job's run needed. The SIGABRT
 * handler prints the aborting thread's stack, symbolized in-process against the PDB beside the
 * executable, and exits before the runtime's own abort machinery can run.
 *
 * This replaces GTest::gtest_main only for lens_gtest_unit; the suites that need an event loop
 * already carry their own main, and the rest keep the library's.
 */
#include <windows.h>
#include <crtdbg.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <dbghelp.h>

#include <gtest/gtest.h>

namespace {

/// @brief Print the aborting thread's call stack, symbolized against the PDB beside the exe, and exit.
/// @param signal Unused; the handler only ever fires for SIGABRT.
/// @note Abort cannot return, so the handler ends the process itself; the runtime's own exit
///       path would re-enter abort and lose the stack this prints.
void onAbort(int /*signal*/)
{
    HANDLE const process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    void*      frames[62] = {};
    WORD const count      = CaptureStackBackTrace(0, 62, frames, nullptr);
    std::fprintf(stderr, "\n=== SIGABRT stack, %u frames:\n", static_cast<unsigned>(count));

    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto*                     symbol                            = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct                                        = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen                                          = 255;
    for (WORD frame = 0; frame < count; ++frame) {
        DWORD64 const address      = reinterpret_cast<DWORD64>(frames[frame]);
        DWORD64       displacement = 0;
        if (SymFromAddr(process, address, &displacement, symbol))
            std::fprintf(stderr, "  #%-2u %s + 0x%llx\n", static_cast<unsigned>(frame), symbol->Name, static_cast<unsigned long long>(displacement));
        else
            std::fprintf(stderr, "  #%-2u 0x%llx\n", static_cast<unsigned>(frame), static_cast<unsigned long long>(address));
    }
    std::fflush(stderr);
    _exit(3);
}

} // namespace

int main(int argc, char** argv)
{
#if defined(_DEBUG)
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    std::signal(SIGABRT, onAbort);
#endif
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
