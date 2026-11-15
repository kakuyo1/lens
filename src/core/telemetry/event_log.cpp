/**
 * @file event_log.cpp
 * @brief Append, month rollover, retention and sequence recovery behind core/telemetry/event_log.h.
 */

#include "core/telemetry/event_log.h"

#include "core/crypto/hash.h"
#include "util/log.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace lens::core::telemetry {
namespace {

constexpr std::size_t kMonthLength = 7;

bool isMonth(std::string_view month)
{
    if (month.size() != kMonthLength)
        return false;
    for (std::size_t i = 0; i < month.size(); ++i) {
        if (i == 4) {
            if (month[i] != '-')
                return false;
        } else if (month[i] < '0' || month[i] > '9') {
            return false;
        }
    }
    const int value = (month[5] - '0') * 10 + (month[6] - '0');
    return value >= 1 && value <= 12;
}

int monthOrdinal(std::string_view month)
{
    const int year  = (month[0] - '0') * 1000 + (month[1] - '0') * 100 + (month[2] - '0') * 10 + (month[3] - '0');
    const int value = (month[5] - '0') * 10 + (month[6] - '0');
    return year * 12 + (value - 1);
}

std::optional<std::string> monthOf(std::string_view fileName)
{
    if (!fileName.starts_with(kFilePrefix) || !fileName.ends_with(kFileSuffix))
        return std::nullopt;
    if (fileName.size() != kFilePrefix.size() + kMonthLength + kFileSuffix.size())
        return std::nullopt;

    std::string month{fileName.substr(kFilePrefix.size(), kMonthLength)};
    if (!isMonth(month))
        return std::nullopt;
    return month;
}

nlohmann::json readStateFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return nlohmann::json::object();

    nlohmann::json document = nlohmann::json::parse(in, nullptr, false);
    if (document.is_discarded() || !document.is_object()) {
        LENS_WARN("event log: '{}' is not a valid state document; rebuilding from the month files", path.string());
        return nlohmann::json::object();
    }
    return document;
}

/// @return The sequence number the file's last complete line carries, or zero when it has none.
unsigned long long lastSequenceInFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LENS_WARN("event log: cannot read '{}' to recover its sequence", path.string());
        return 0;
    }

    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0)
        return 0;

    constexpr std::streamoff kTailBytes = 8192;
    const std::streamoff     start      = std::max<std::streamoff>(0, size - kTailBytes);
    in.seekg(start);

    std::string tail(static_cast<std::size_t>(size - start), '\0');
    in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
    tail.resize(static_cast<std::size_t>(in.gcount()));
    while (!tail.empty() && (tail.back() == '\n' || tail.back() == '\r'))
        tail.pop_back();

    const auto             newline = tail.rfind('\n');
    const std::string_view line    = newline == std::string::npos ? std::string_view{tail} : std::string_view{tail}.substr(newline + 1);
    if (const auto event = parseLine(line))
        return event->seq;

    // The window held no line break, or the last line is longer than it. Read the file properly
    // rather than let recovery miss a sequence; a partial last line from a crash is skipped.
    in.clear();
    in.seekg(0);
    std::vector<std::string> lines;
    std::string              streamed;
    while (std::getline(in, streamed))
        lines.push_back(streamed);
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (it->empty())
            continue;
        if (const auto event = parseLine(*it))
            return event->seq;
    }
    return 0;
}

/// @return Whether the file is empty or its last byte is a newline; a new file counts as ended.
bool endsWithNewline(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return true;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0)
        return true;
    in.seekg(size - 1);
    char last = '\0';
    in.get(last);
    return last == '\n';
}

} // namespace

EventLog::EventLog(std::filesystem::path root, std::string_view deviceId)
    : root_(std::move(root)), device_(crypto::sha1Hex(deviceId).substr(0, 8))
{
    if (deviceId.empty())
        LENS_WARN("event log: opened without a device id; every instance would share one short id");

    state_            = readStateFile(root_ / std::string(kStateFileName));
    const auto stored = state_.find("seq");
    if (stored != state_.end()) {
        if (stored->is_number_unsigned()) {
            seq_ = stored->get<unsigned long long>();
        } else if (stored->is_number_integer()) {
            const auto value = stored->get<long long>();
            if (value > 0)
                seq_ = static_cast<unsigned long long>(value);
        }
    }
    // A crash between a line and the state write leaves the state behind the file. The larger of
    // the two is the one that cannot reuse a sequence.
    seq_ = std::max(seq_, recoverSequence());
}

bool EventLog::append(std::string_view name, long long tsMs, std::string_view month, nlohmann::json data)
{
    const EventSpec* const spec = findEventSpec(name);
    if (spec == nullptr) {
        LENS_WARN("event log: refusing unregistered event '{}'", name);
        return false;
    }

    const std::string reason = validatePayload(*spec, data);
    if (!reason.empty()) {
        LENS_WARN("event log: refusing '{}': {}", spec->name, reason);
        return false;
    }

    if (!isMonth(month)) {
        LENS_WARN("event log: refusing '{}' for a month that is not YYYY-MM: '{}'", spec->name, month);
        return false;
    }
    if (std::string_view{openMonth_} != month)
        openMonth(month);

    Event event;
    event.tsMs   = tsMs;
    event.seq    = ++seq_;
    event.device = device_;
    event.name   = std::string(spec->name);
    event.data   = std::move(data);

    out_ << toLine(event) << '\n';
    if (!out_) {
        LENS_ERROR("event log: cannot write '{}'", monthFile(month).string());
        throw std::runtime_error("Cannot write the event log: " + monthFile(month).string());
    }

    if (!limitWarned_) {
        const std::streamoff position = out_.tellp();
        if (position > 0 && static_cast<std::uintmax_t>(position) > kMonthSoftLimitBytes) {
            LENS_WARN("event log: '{}' is over {} bytes; continuing, but the month is unusually large", monthFile(month).string(), kMonthSoftLimitBytes);
            limitWarned_ = true;
        }
    }
    return true;
}

void EventLog::flush()
{
    if (out_.is_open()) {
        out_.flush();
        if (!out_) {
            LENS_ERROR("event log: cannot flush '{}'", monthFile(openMonth_).string());
            throw std::runtime_error("Cannot flush the event log: " + monthFile(openMonth_).string());
        }
    }
    saveState();
}

std::vector<std::string> EventLog::prune(std::string_view currentMonth)
{
    std::vector<std::string> removed;
    if (!isMonth(currentMonth))
        return removed;

    const int cutoff = monthOrdinal(currentMonth) - (kRetentionMonths - 1);
    for (const std::string& month : months()) {
        if (monthOrdinal(month) >= cutoff)
            continue;

        std::error_code ec;
        if (std::filesystem::remove(monthFile(month), ec)) {
            removed.push_back(month);
        } else if (ec) {
            LENS_WARN("event log: cannot remove '{}': {}", monthFile(month).string(), ec.message());
        }
    }
    return removed;
}

std::vector<std::string> EventLog::months() const
{
    std::vector<std::string>            result;
    std::error_code                     ec;
    std::filesystem::directory_iterator directory(root_, ec);
    if (ec)
        return result;

    for (const auto& entry : directory) {
        std::error_code entryError;
        if (!entry.is_regular_file(entryError))
            continue;
        const auto month = monthOf(entry.path().filename().string());
        if (month)
            result.push_back(*month);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<Event> EventLog::readMonth(std::string_view month) const
{
    std::vector<Event> events;
    if (!isMonth(month))
        return events;

    const std::filesystem::path path = monthFile(month);
    std::ifstream               in(path, std::ios::binary);
    if (!in)
        return events;

    std::string line;
    std::size_t skipped = 0;
    while (std::getline(in, line)) {
        if (line.empty())
            continue;
        const auto event = parseLine(line);
        if (!event) {
            ++skipped;
            continue;
        }
        events.push_back(*event);
    }
    if (skipped != 0)
        LENS_DEBUG("event log: skipped {} malformed line(s) in '{}'", skipped, path.string());
    return events;
}

std::string EventLog::readMonthText(std::string_view month) const
{
    if (!isMonth(month))
        return {};
    std::ifstream in(monthFile(month), std::ios::binary);
    if (!in)
        return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::uintmax_t EventLog::totalBytes() const
{
    std::uintmax_t total = 0;
    for (std::string const& month : months()) {
        std::error_code ec;
        const auto      bytes = std::filesystem::file_size(monthFile(month), ec);
        if (!ec)
            total += bytes;
    }
    return total;
}

void EventLog::clear()
{
    if (out_.is_open())
        out_.close();
    openMonth_.clear();

    for (std::string const& month : months()) {
        std::error_code ec;
        if (!std::filesystem::remove(monthFile(month), ec) && ec)
            LENS_WARN("event log: cannot remove '{}': {}", monthFile(month).string(), ec.message());
    }
}

void EventLog::saveState()
{
    ensureRoot();
    state_["v"]   = kStateFormat;
    state_["seq"] = seq_;

    const std::filesystem::path target    = root_ / std::string(kStateFileName);
    const std::filesystem::path temporary = root_ / (std::string(kStateFileName) + ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            LENS_ERROR("event log: cannot write '{}'", temporary.string());
            throw std::runtime_error("Cannot write the event log state: " + temporary.string());
        }
        out << state_.dump(2) << '\n';
        out.flush();
        if (!out) {
            LENS_ERROR("event log: cannot flush '{}'", temporary.string());
            throw std::runtime_error("Cannot write the event log state: " + temporary.string());
        }
    }

    std::error_code ec;
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        // Windows refuses a rename onto an existing file; the state is recoverable from the month
        // files, so replacing it outright is the smaller loss than refusing to save at all.
        std::error_code ignored;
        std::filesystem::remove(target, ignored);
        std::filesystem::rename(temporary, target, ec);
    }
    if (ec) {
        LENS_ERROR("event log: cannot replace '{}': {}", target.string(), ec.message());
        throw std::runtime_error("Cannot write the event log state: " + target.string());
    }
}

unsigned long long EventLog::recoverSequence() const
{
    unsigned long long maxSequence = 0;
    for (const std::string& month : months())
        maxSequence = std::max(maxSequence, lastSequenceInFile(monthFile(month)));
    return maxSequence;
}

std::filesystem::path EventLog::monthFile(std::string_view month) const
{
    return root_ / (std::string(kFilePrefix) + std::string(month) + std::string(kFileSuffix));
}

void EventLog::openMonth(std::string_view month)
{
    if (out_.is_open())
        out_.close();

    ensureRoot();
    const std::filesystem::path path = monthFile(month);
    // A crash can leave the last line unfinished. A separator keeps the next event a line of its
    // own; the unfinished one stays malformed and is skipped by readers.
    const bool needsSeparator = !endsWithNewline(path);
    out_.open(path, std::ios::app | std::ios::binary);
    if (!out_) {
        LENS_ERROR("event log: cannot open '{}'", path.string());
        throw std::runtime_error("Cannot open the event log: " + path.string());
    }
    if (needsSeparator) {
        out_ << '\n';
        if (!out_) {
            LENS_ERROR("event log: cannot separate the unfinished line in '{}'", path.string());
            throw std::runtime_error("Cannot write the event log: " + path.string());
        }
    }
    openMonth_ = std::string(month);
    LENS_DEBUG("event log: writing '{}'", path.string());
}

void EventLog::ensureRoot() const
{
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    if (ec) {
        LENS_ERROR("event log: cannot create '{}': {}", root_.string(), ec.message());
        throw std::runtime_error("Cannot create the event log directory: " + root_.string());
    }
}

}
