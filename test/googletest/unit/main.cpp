/**
 * @file main.cpp
 * @brief The unit suite's own main, because the Debug CRT's diagnostics must reach stderr.
 *
 * Under the default GTest::gtest_main, a Debug-CRT assert or an abort() raises the "Microsoft
 * Visual C++ Runtime Library" dialog, and a headless runner has nobody to click it -- the suite
 * sat on that dialog for half an hour on CI, at a different test each run. Routing every report
 * mode to stderr puts the assert's text in the CI log and, like pressing Ignore, lets the run
 * continue to the next test. _CALL_REPORTFAULT is dropped from the abort behavior for the same
 * reason: abort() reports to stderr and exits instead of showing a box.
 *
 * This replaces GTest::gtest_main only for lens_gtest_unit; the suites that need an event loop
 * already carry their own main, and the rest keep the library's.
 */
#include <crtdbg.h>
#include <cstdlib>

#include <gtest/gtest.h>

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
#endif
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
