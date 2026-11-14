/**
 * @file crash.cpp
 * @brief Names, the summary format and the retention rule behind crash.h.
 */

#include "util/crash.h"

#include <algorithm>
#include <system_error>
#include <utility>

namespace lens::util {
namespace {

/// Suffix of the summary file that sits beside a dump of the same stem.
const std::string kSummarySuffix = ".crash.txt";

/// @brief Trim spaces and tabs from both ends. Summary lines are machine-written, so nothing
///        else can appear around a field.
std::string_view trimmed(std::string_view text)
{
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

/// @brief The summary file that sits beside a dump: the dump's stem with the summary suffix.
std::filesystem::path summaryBeside(const std::filesystem::path& dump)
{
    return dump.parent_path() / (dump.stem().string() + kSummarySuffix);
}

/// @brief The dump a summary belongs to: the summary suffix replaced with the dump one.
std::filesystem::path dumpBehind(const std::filesystem::path& summary)
{
    std::string name = summary.filename().string();
    name.resize(name.size() - kSummarySuffix.size());
    return summary.parent_path() / (name + ".dmp");
}

}

std::filesystem::path crashDumpDir(const std::filesystem::path& logDir)
{
    return logDir / kCrashDumpDirName;
}

std::string crashDumpStem(std::string_view version, std::string_view localTime, unsigned long processId)
{
    std::string stem{"lens-"};
    stem += version;
    stem += '-';
    stem += localTime;
    stem += '-';
    stem += std::to_string(processId);
    return stem;
}

std::string serializeCrashSummary(const CrashSummary& summary)
{
    std::string text;
    const auto  line = [&text](std::string_view key, const std::string& value) {
        if (value.empty()) return;
        text += key;
        text += ": ";
        text += value;
        text += '\n';
    };
    line("version", summary.version);
    line("time", summary.time);
    line("code", summary.code);
    line("address", summary.address);
    line("module", summary.module);
    line("dump", summary.dump);
    return text;
}

CrashSummary parseCrashSummary(std::string_view text)
{
    CrashSummary summary;
    const auto   assign = [&summary](std::string_view key, std::string_view value) {
        if (key == "version")
            summary.version = value;
        else if (key == "time")
            summary.time = value;
        else if (key == "code")
            summary.code = value;
        else if (key == "address")
            summary.address = value;
        else if (key == "module")
            summary.module = value;
        else if (key == "dump")
            summary.dump = value;
    };

    while (!text.empty()) {
        const auto       end  = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos)
            assign(trimmed(line.substr(0, colon)), trimmed(line.substr(colon + 1)));
        if (end == std::string_view::npos) break;
        text.remove_prefix(end + 1);
    }
    return summary;
}

std::size_t pruneCrashDumps(const std::filesystem::path& dumpDir, std::size_t keep)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(dumpDir, ec)) return 0;

    struct DumpFile {
        std::filesystem::path           path;
        std::filesystem::file_time_type written;
    };

    std::vector<DumpFile>              dumps;
    std::vector<std::filesystem::path> summaries;
    for (const auto& entry : std::filesystem::directory_iterator(dumpDir)) {
        // A name the narrow encoding cannot carry would throw mid-iteration. A file the crash
        // path did not name is not an artifact, so it is skipped whole instead of ending the walk.
        try {
            if (!entry.is_regular_file()) continue;
            const auto name = entry.path().filename().string();
            // The marker ends in the summary suffix but is not one: it is the handoff, and only
            // reportStartupCrash removes it. A pruner that ate it would swallow the news.
            if (name == kCrashMarkerName) continue;
            if (name.ends_with(".dmp"))
                dumps.push_back({entry.path(), entry.last_write_time()});
            else if (name.ends_with(kSummarySuffix))
                summaries.push_back(entry.path());
        } catch (const std::filesystem::filesystem_error&) {
            continue;
        }
    }

    std::sort(dumps.begin(), dumps.end(), [](const DumpFile& a, const DumpFile& b) {
        if (a.written != b.written) return a.written > b.written;
        return a.path.filename().string() > b.path.filename().string();
    });

    std::size_t removed = 0;
    const auto  remove  = [&removed](const std::filesystem::path& file) {
        std::error_code removeEc;
        if (std::filesystem::remove(file, removeEc)) ++removed;
    };

    // Newest first, so the kept prefix is the survivors; a removed dump takes its summary along.
    for (std::size_t i = keep; i < dumps.size(); ++i) {
        remove(dumps[i].path);
        remove(summaryBeside(dumps[i].path));
    }
    // A summary whose dump never landed says only what the marker says; without this it would
    // sit in the directory forever after a dump write that failed.
    for (const auto& summary : summaries) {
        if (!std::filesystem::exists(dumpBehind(summary), ec)) remove(summary);
    }
    return removed;
}

namespace {

/// Bytes a URL query may carry raw, RFC 3986 2.3's unreserved set; everything else in the
/// template's text goes through as two hex digits, UTF-8 bytes included.
std::string percentEncoded(std::string_view text)
{
    static constexpr const char* kDigits = "0123456789ABCDEF";
    std::string                  out;
    out.reserve(text.size());
    for (const char c : text) {
        const auto byte       = static_cast<unsigned char>(c);
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' || byte == '~';
        if (unreserved) {
            out += static_cast<char>(byte);
        } else {
            out += '%';
            out += kDigits[byte >> 4];
            out += kDigits[byte & 0xF];
        }
    }
    return out;
}

}

std::string crashIssueUrl(const CrashSummary& summary)
{
    // Facts only, straight from the summary; the steps stay the reader's to write. The same
    // wording the facade used before the builder moved here, so both callers read alike.
    std::string facts;
    const auto  line = [&facts](const char* label, const std::string& value) {
        if (value.empty()) return;
        if (!facts.empty()) facts += '\n';
        facts += "- ";
        facts += label;
        facts += ": ";
        facts += value;
    };
    line("Lens version", summary.version);
    line("Exception code", summary.code);
    line("Faulting address", summary.address);
    line("Module", summary.module);
    line("Time", summary.time);

    const std::string title = "Lens " + (summary.version.empty() ? std::string("unknown") : summary.version) + " crashed (" + (summary.code.empty() ? std::string("unknown") : summary.code) + ")";
    const std::string body =
        std::string("A crash dump (`.dmp`) and a summary (`.crash.txt`) were saved under the Lens log "
                    "directory's `dumps` folder. Please attach the pair that matches this crash.\n\n"
                    "### Crash\n\n") +
        facts + "\n\n### Steps to reproduce\n\n1. ";
    return "https://github.com/kakuyo1/lens/issues/new?title=" + percentEncoded(title) + "&body=" + percentEncoded(body);
}

}
