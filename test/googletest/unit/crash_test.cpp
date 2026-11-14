/**
 * @file crash_test.cpp
 * @brief The pure half of crash forensics: file names, the summary format and the retention
 *        rule, offline in a scratch directory like log_test's.
 *
 * The crash path itself runs once per process death and cannot be pinned by a test; what can is
 * everything around it -- the names both sides of the handoff agree on, the summary that travels
 * from the dying process to the next launch, and the rule that keeps the dumps directory from
 * growing without bound.
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "util/crash.h"

namespace {

namespace fs = std::filesystem;

/// @brief A scratch directory under the system temp folder, removed with the object.
///
/// One fixed name per tag is enough: the cases run in one process, one after another, and each
/// removes what it made on the way out.
class ScratchDir {
public:
    explicit ScratchDir(const char* tag)
        : path_(fs::temp_directory_path() / (std::string("lens-crash-test-") + tag))
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
        fs::create_directories(path_, ec);
    }

    ~ScratchDir()
    {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    const fs::path& path() const
    {
        return path_;
    }

private:
    fs::path path_;
};

/// @brief One dump and its summary, their last-write time @p minutesAgo minutes behind now.
void writeDump(const fs::path& dir, const std::string& stem, int minutesAgo)
{
    {
        std::ofstream dump(dir / (stem + ".dmp"));
        dump << "dump";
    }
    {
        std::ofstream summary(dir / (stem + ".crash.txt"));
        summary << "time: 2026-11-14 15:30:00\n";
    }
    const auto      written = fs::file_time_type::clock::now() - std::chrono::minutes(minutesAgo);
    std::error_code ec;
    fs::last_write_time(dir / (stem + ".dmp"), written, ec);
    fs::last_write_time(dir / (stem + ".crash.txt"), written, ec);
}

} // namespace

/// Both the dying process and the next launch derive the directory from the same name; the
/// agreement between the two sides of the handoff is the whole feature.
TEST(CrashPaths, TheDumpDirectoryIsDumpsUnderTheLogDirectory)
{
    EXPECT_EQ(lens::util::crashDumpDir("logs"), fs::path("logs") / "dumps");
}

/// The stem carries the version, the compact time and the pid: two crashes in one second are
/// distinct files, and a reader of the directory can tell which run a file came from.
TEST(CrashPaths, TheStemSeparatesCrashesByTimeAndProcess)
{
    const std::string stem = lens::util::crashDumpStem("1.0.0", "20261114-153000", 1234);
    EXPECT_EQ(stem, "lens-1.0.0-20261114-153000-1234");

    const std::string otherPid  = lens::util::crashDumpStem("1.0.0", "20261114-153000", 5678);
    const std::string otherTime = lens::util::crashDumpStem("1.0.0", "20261114-153001", 1234);
    EXPECT_NE(stem, otherPid);
    EXPECT_NE(stem, otherTime);
}

/// The summary travels verbatim from the dying process to the marker the next launch reads, so
/// what serialize writes parse must read back unchanged; empty fields stay out of the file.
TEST(CrashSummary, SerializeOmitsEmptyFieldsAndRoundTrips)
{
    lens::util::CrashSummary summary;
    summary.version = "1.0.0";
    summary.time    = "2026-11-14 15:30:00";
    summary.code    = "0xC0000005";
    summary.address = "0x00007FF6B2E4123A";
    summary.module  = "lens.exe";
    summary.dump    = "lens-1.0.0-20261114-153000-1234.dmp";

    const std::string text = lens::util::serializeCrashSummary(summary);
    EXPECT_EQ(text,
              "version: 1.0.0\n"
              "time: 2026-11-14 15:30:00\n"
              "code: 0xC0000005\n"
              "address: 0x00007FF6B2E4123A\n"
              "module: lens.exe\n"
              "dump: lens-1.0.0-20261114-153000-1234.dmp\n");

    const lens::util::CrashSummary readBack = lens::util::parseCrashSummary(text);
    EXPECT_EQ(readBack.version, summary.version);
    EXPECT_EQ(readBack.time, summary.time);
    EXPECT_EQ(readBack.code, summary.code);
    EXPECT_EQ(readBack.address, summary.address);
    EXPECT_EQ(readBack.module, summary.module);
    EXPECT_EQ(readBack.dump, summary.dump);
}

/// A crash that produced no dump still writes a summary -- the dump field is the one that may be
/// empty -- and the next launch parses it without guessing.
TEST(CrashSummary, AFieldThatWasNeverWrittenStaysEmpty)
{
    lens::util::CrashSummary summary;
    summary.version = "1.0.0";
    summary.code    = "0xE0000001";

    const std::string text = lens::util::serializeCrashSummary(summary);
    EXPECT_EQ(text, "version: 1.0.0\ncode: 0xE0000001\n");

    const lens::util::CrashSummary readBack = lens::util::parseCrashSummary(text);
    EXPECT_EQ(readBack.version, "1.0.0");
    EXPECT_EQ(readBack.code, "0xE0000001");
    EXPECT_EQ(readBack.time, "");
    EXPECT_EQ(readBack.module, "");
}

/// The format is read by the next launch only, but it lands in files a person opens too; unknown
/// keys, a value-less line and Windows line endings all parse instead of failing.
TEST(CrashSummary, ParsingToleratesWhatAFileMayCarry)
{
    const lens::util::CrashSummary readBack = lens::util::parseCrashSummary(
        "process: 1234\r\n"
        "code: 0xC0000005\r\n"
        "pid\r\n"
        "\r\n"
        "module: lens.exe\r\n");
    EXPECT_EQ(readBack.code, "0xC0000005");
    EXPECT_EQ(readBack.module, "lens.exe");
    EXPECT_EQ(readBack.version, "");
}

/// The directory keeps the newest dumps and their summaries; the ones beyond the keep go, the
/// summaries that describe them with them.
TEST(CrashRetention, PruneKeepsTheNewestDumpsAndTheirSummaries)
{
    ScratchDir scratch("prune");
    for (int i = 0; i < 7; ++i)
        writeDump(scratch.path(), "lens-1.0.0-dump" + std::to_string(i), i);
    // A file the crash path does not name stays: the pruner owns its own names, not the directory.
    {
        std::ofstream other(scratch.path() / "lens.log");
        other << "log";
    }

    EXPECT_EQ(lens::util::pruneCrashDumps(scratch.path(), 5), 4u);

    EXPECT_TRUE(fs::exists(scratch.path() / "lens-1.0.0-dump0.dmp"));
    EXPECT_TRUE(fs::exists(scratch.path() / "lens-1.0.0-dump4.crash.txt"));
    EXPECT_FALSE(fs::exists(scratch.path() / "lens-1.0.0-dump5.dmp"));
    EXPECT_FALSE(fs::exists(scratch.path() / "lens-1.0.0-dump5.crash.txt"));
    EXPECT_FALSE(fs::exists(scratch.path() / "lens-1.0.0-dump6.dmp"));
    EXPECT_FALSE(fs::exists(scratch.path() / "lens-1.0.0-dump6.crash.txt"));
    EXPECT_TRUE(fs::exists(scratch.path() / "lens.log"));
}

/// A summary whose dump never landed says only what the marker says; the pruner clears it so a
/// failed dump write does not leave a stray behind forever.
TEST(CrashRetention, PruneRemovesASummaryWhoseDumpNeverLanded)
{
    ScratchDir scratch("orphan");
    writeDump(scratch.path(), "lens-1.0.0-kept", 0);
    {
        std::ofstream orphan(scratch.path() / "lens-1.0.0-orphan.crash.txt");
        orphan << "code: 0xC0000005\n";
    }

    EXPECT_EQ(lens::util::pruneCrashDumps(scratch.path(), 5), 1u);
    EXPECT_TRUE(fs::exists(scratch.path() / "lens-1.0.0-kept.dmp"));
    EXPECT_TRUE(fs::exists(scratch.path() / "lens-1.0.0-kept.crash.txt"));
    EXPECT_FALSE(fs::exists(scratch.path() / "lens-1.0.0-orphan.crash.txt"));
}

/// Nothing has crashed yet on a fresh machine: the pruner meets a directory that does not exist
/// and removes nothing.
TEST(CrashRetention, ADirectoryThatDoesNotExistPrunesNothing)
{
    ScratchDir scratch("missing");
    EXPECT_EQ(lens::util::pruneCrashDumps(scratch.path() / "absent", 5), 0u);
}

/// The marker ends in the summary suffix but is not one; it is the handoff to the next launch,
/// and a prune that ran between the crash and the notice must not eat the news.
TEST(CrashRetention, PruneLeavesTheMarkerAlone)
{
    ScratchDir scratch("marker");
    writeDump(scratch.path(), "lens-1.0.0-kept", 0);
    {
        std::ofstream marker(scratch.path() / lens::util::kCrashMarkerName);
        marker << "code: 0xC0000005\n";
    }

    EXPECT_EQ(lens::util::pruneCrashDumps(scratch.path(), 5), 0u);
    EXPECT_TRUE(fs::exists(scratch.path() / lens::util::kCrashMarkerName));
}

/// The facade and the reporter process open the same URL, so the template's shape is pinned
/// here: base, encoded title, and the facts the body quotes, spaces and all.
TEST(CrashIssueUrl, TheTemplateCarriesTheBaseTheTitleAndTheFacts)
{
    lens::util::CrashSummary summary;
    summary.version = "1.0.0";
    summary.time    = "2026-11-14 11:01:51";
    summary.code    = "0xC0000005";
    summary.address = "0x00007FF68EA44BF6";
    summary.module  = "lens.exe";

    const std::string url = lens::util::crashIssueUrl(summary);
    EXPECT_TRUE(url.starts_with("https://github.com/kakuyo1/lens/issues/new?title="))
        << "the issue URL does not open the repository's new-issue form: " << url;
    EXPECT_NE(url.find("Lens%201.0.0%20crashed%20%280xC0000005%29"), std::string::npos)
        << "the title is not the encoded form the issue list shows";
    EXPECT_NE(url.find("&body="), std::string::npos);
    EXPECT_NE(url.find("-%20Lens%20version%3A%201.0.0"), std::string::npos)
        << "the version fact is missing from the encoded body";
    EXPECT_NE(url.find("Time%3A%202026-11-14%2011%3A01%3A51"), std::string::npos)
        << "the time fact is missing from the encoded body";
    EXPECT_NE(url.find("Steps%20to%20reproduce"), std::string::npos);
    // Nothing the body carries rides raw: a stray space would cut the URL at the browser.
    EXPECT_EQ(url.find(' '), std::string::npos);
}

/// A summary missing fields says "unknown" where the title needs one and drops the empty lines
/// where the body carries none, so the issue never opens with half a sentence.
TEST(CrashIssueUrl, EmptyFieldsSayUnknownAndLeaveNoLine)
{
    lens::util::CrashSummary summary;
    summary.code = "0xE0000001";

    const std::string url = lens::util::crashIssueUrl(summary);
    EXPECT_NE(url.find("Lens%20unknown%20crashed%20%280xE0000001%29"), std::string::npos);
    EXPECT_EQ(url.find("Lens%20version"), std::string::npos)
        << "an empty version still produced a fact line";
}
