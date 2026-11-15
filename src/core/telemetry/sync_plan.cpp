/**
 * @file sync_plan.cpp
 * @brief The event cloud reconciliation behind core/telemetry/sync_plan.h.
 */

#include "core/telemetry/sync_plan.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lens::core::telemetry {
namespace {

constexpr std::size_t kDeviceLength   = 8;
constexpr std::size_t kMonthLength    = 7;
constexpr std::size_t kRevisionLength = 4;
constexpr std::size_t kHashLength     = 8;
constexpr std::size_t kFileLength     = 52;

bool isLowerHex(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](char const c) {
        return (c >= '0' and c <= '9') or (c >= 'a' and c <= 'f');
    });
}

bool isMonth(std::string_view month)
{
    if (month.size() != kMonthLength or month[4] != '-')
        return false;
    for (std::size_t const at : {0, 1, 2, 3, 5, 6})
        if (month[at] < '0' or month[at] > '9')
            return false;
    const int value = (month[5] - '0') * 10 + (month[6] - '0');
    return value >= 1 and value <= 12;
}

int monthOrdinal(std::string_view month)
{
    const int year  = (month[0] - '0') * 1000 + (month[1] - '0') * 100 + (month[2] - '0') * 10 + (month[3] - '0');
    const int value = (month[5] - '0') * 10 + (month[6] - '0');
    return year * 12 + (value - 1);
}

std::string revisionText(int const revision)
{
    std::string text = std::to_string(revision);
    return std::string(kRevisionLength - text.size(), '0') + text;
}

} // namespace

std::optional<EventFileRef> parseEventFileName(std::string_view name)
{
    if (name.size() != kFileLength or not name.starts_with(kEventFilePrefix) or not name.ends_with(kEventFileSuffix))
        return std::nullopt;

    const std::string_view device      = name.substr(kEventFilePrefix.size(), kDeviceLength);
    const std::size_t      afterDevice = kEventFilePrefix.size() + kDeviceLength;
    if (name[afterDevice] != '-')
        return std::nullopt;
    const std::string_view month = name.substr(afterDevice + 1, kMonthLength);
    if (not isMonth(month))
        return std::nullopt;

    const std::size_t afterMonth = afterDevice + 1 + kMonthLength;
    if (name[afterMonth] != '-' or name[afterMonth + 1] != 'r')
        return std::nullopt;
    const std::string_view revisionPart = name.substr(afterMonth + 2, kRevisionLength);
    if (not std::all_of(revisionPart.begin(), revisionPart.end(), [](char const c) { return c >= '0' and c <= '9'; }))
        return std::nullopt;
    int revision            = 0;
    const auto [end, error] = std::from_chars(revisionPart.data(), revisionPart.data() + revisionPart.size(), revision);
    if (error != std::errc{} or end != revisionPart.data() + revisionPart.size() or revision < 1)
        return std::nullopt;

    const std::size_t afterRevision = afterMonth + 2 + kRevisionLength;
    if (name[afterRevision] != '-')
        return std::nullopt;
    const std::string_view hash = name.substr(afterRevision + 1, kHashLength);
    if (not isLowerHex(device) or not isLowerHex(hash))
        return std::nullopt;

    return EventFileRef{std::string(name), std::string(device), std::string(month), revision, std::string(hash)};
}

std::string eventFileName(std::string_view device, std::string_view month, int const revision, std::string_view hash) // NOLINT(bugprone-easily-swappable-parameters): the file name is positional; the device, month and hash cannot be reordered without producing a different name
{
    if (device.size() != kDeviceLength or not isLowerHex(device) or not isMonth(month) or hash.size() != kHashLength or
        not isLowerHex(hash) or revision < 1 or revision > 9999)
        return {};
    return std::string(kEventFilePrefix) + std::string(device) + "-" + std::string(month) + "-r" + revisionText(revision) +
           "-" + std::string(hash) + std::string(kEventFileSuffix);
}

SyncState syncStateFromJson(nlohmann::json const& state)
{
    SyncState sync;
    if (not state.is_object())
        return sync;

    if (auto const uploaded = state.find("uploaded"); uploaded != state.end() and uploaded->is_object()) {
        for (auto const& [month, value] : uploaded->items()) {
            if (not isMonth(month) or not value.is_object())
                continue;
            auto const file = value.find("file");
            if (file == value.end() or not file->is_string())
                continue;
            auto const ref = parseEventFileName(file->get_ref<std::string const&>());
            if (not ref or ref->month != month)
                continue;
            auto const day       = value.find("day");
            sync.uploaded[month] = UploadedMonth{ref->name, ref->hash, ref->revision, day != value.end() and day->is_string() ? day->get<std::string>() : std::string{}};
        }
    }

    if (auto const pending = state.find("pendingDeletes"); pending != state.end() and pending->is_array()) {
        for (auto const& value : *pending)
            if (value.is_string() and not value.get_ref<std::string const&>().empty())
                sync.pendingDeletes.insert(value.get<std::string>());
    }
    if (auto const all = state.find("deleteAll"); all != state.end() and all->is_boolean())
        sync.deleteAll = all->get<bool>();
    return sync;
}

void writeSyncState(nlohmann::json& state, SyncState const& sync)
{
    if (not state.is_object())
        state = nlohmann::json::object();

    nlohmann::json uploaded = nlohmann::json::object();
    for (auto const& [month, record] : sync.uploaded)
        uploaded[month] = {{"file", record.file}, {"hash", record.hash}, {"revision", record.revision}, {"day", record.day}};
    state["uploaded"] = std::move(uploaded);

    nlohmann::json pending = nlohmann::json::array();
    for (std::string const& file : sync.pendingDeletes)
        pending.push_back(file);
    state["pendingDeletes"] = std::move(pending);
    state["deleteAll"]      = sync.deleteAll;
}

Plan planEventSync(PlanInput input)
{
    Plan plan;
    plan.state = std::move(input.state);

    std::map<std::string, std::string> local;
    const int                          keepMonths = input.retentionMonths > 0 ? input.retentionMonths : 1;
    const int                          cutoff     = isMonth(input.currentMonth) ? monthOrdinal(input.currentMonth) - (keepMonths - 1) : 0;
    for (LocalMonth const& month : input.local) {
        if (not isMonth(month.month) or month.hash.size() != kHashLength or not isLowerHex(month.hash))
            continue;
        if (isMonth(input.currentMonth) and monthOrdinal(month.month) < cutoff)
            continue;
        local[month.month] = month.hash;
    }

    std::set<std::string>                            remoteNames;
    std::map<std::string, std::vector<EventFileRef>> own;
    std::map<std::string, EventFileRef>              newest;
    for (std::string const& name : input.remote) {
        if (not remoteNames.insert(name).second)
            continue;
        auto const ref = parseEventFileName(name);
        if (not ref or ref->device != input.device)
            continue;
        own[ref->month].push_back(*ref);
        auto const current = newest.find(ref->month);
        if (current == newest.end() or ref->revision > current->second.revision)
            newest[ref->month] = *ref;
    }

    std::set<std::string> deletes;

    // The reader deleted the cloud copy. Every event file goes, including another device's and a
    // month whose local file is still here; the flag stays until the duty has deleted them all.
    if (plan.state.deleteAll) {
        for (std::string const& name : input.remote)
            if (name.starts_with(kEventFilePrefix))
                deletes.insert(name);
        for (std::string const& name : plan.state.pendingDeletes) {
            auto const ref = parseEventFileName(name);
            if (ref)
                deletes.insert(name);
        }
        plan.deletes.assign(deletes.begin(), deletes.end());
        plan.state.uploaded.clear();
        plan.state.pendingDeletes = deletes;
        return plan;
    }

    // A record whose file is no longer there describes nothing. A month whose newest remote file
    // matches its local hash is adopted below, so a lost state file rebuilds from the remote list.
    for (auto it = plan.state.uploaded.begin(); it != plan.state.uploaded.end();) {
        if (not remoteNames.contains(it->second.file))
            it = plan.state.uploaded.erase(it);
        else
            ++it;
    }
    for (auto const& [month, hash] : local) {
        auto const remote = newest.find(month);
        if (remote == newest.end() or remote->second.hash != hash)
            continue;
        auto const record = plan.state.uploaded.find(month);
        if (record == plan.state.uploaded.end() or record->second.revision < remote->second.revision) {
            const std::string day =
                record != plan.state.uploaded.end() and record->second.file == remote->second.name ? record->second.day : std::string{};
            plan.state.uploaded[month] = UploadedMonth{remote->second.name, remote->second.hash, remote->second.revision, day};
        }
    }

    // What may not be deleted before a new version lands: every remote copy of a local month whose
    // newest file does not carry the local content. Deleting the old copy first would lose the only
    // cloud version when the upload then fails.
    std::set<std::string> protectedFiles;
    for (auto const& [month, hash] : local) {
        auto const remote = newest.find(month);
        if (remote != newest.end() and remote->second.hash == hash)
            continue;
        for (EventFileRef const& ref : own[month])
            protectedFiles.insert(ref.name);
    }

    for (auto const& [month, hash] : local) {
        auto const remote = newest.find(month);
        auto const record = plan.state.uploaded.find(month);
        if (remote != newest.end() and remote->second.hash == hash) {
            for (EventFileRef const& ref : own[month])
                if (ref.name != remote->second.name)
                    deletes.insert(ref.name);
            continue;
        }

        const bool closed = month != input.currentMonth;
        const bool capped = not input.manual and not closed and not input.today.empty() and
                            record != plan.state.uploaded.end() and record->second.day == input.today;
        if (capped)
            continue;

        int highest = remote != newest.end() ? remote->second.revision : 0;
        if (record != plan.state.uploaded.end())
            highest = std::max(highest, record->second.revision);
        if (highest >= 9999)
            continue;

        const int         revision = highest + 1;
        std::string const file     = eventFileName(input.device, month, revision, hash);
        if (file.empty())
            continue;

        UploadAction action{month, file, hash, revision, input.today, {}};
        for (EventFileRef const& ref : own[month])
            action.retire.push_back(ref.name);
        plan.uploads.push_back(std::move(action));
    }

    // A month with no local file is gone, whether it was pruned by retention or cleared by the
    // reader; every copy this device owns goes with it. Other devices' files are left alone.
    for (auto const& [month, refs] : own) {
        if (local.contains(month))
            continue;
        for (EventFileRef const& ref : refs)
            deletes.insert(ref.name);
        plan.state.uploaded.erase(month);
    }

    std::set<std::string> pending;
    for (std::string const& file : plan.state.pendingDeletes) {
        auto const ref = parseEventFileName(file);
        if (ref and ref->device == input.device)
            pending.insert(file);
    }

    std::set<std::string> keep;
    for (auto const& [month, record] : plan.state.uploaded)
        keep.insert(record.file);
    for (auto const& [month, ref] : newest) {
        auto const localIt = local.find(month);
        if (localIt != local.end() and localIt->second == ref.hash)
            keep.insert(ref.name);
    }

    for (auto const& upload : plan.uploads)
        for (std::string const& file : upload.retire)
            protectedFiles.insert(file);

    std::set<std::string> deferred;
    for (std::string const& file : pending) {
        if (keep.contains(file))
            continue;
        if (protectedFiles.contains(file)) {
            deferred.insert(file);
            continue;
        }
        deletes.insert(file);
    }
    for (auto const& upload : plan.uploads)
        for (std::string const& file : upload.retire)
            if (keep.contains(file) and pending.contains(file))
                deferred.insert(file);

    plan.deletes.assign(deletes.begin(), deletes.end());
    plan.state.pendingDeletes = deferred;
    for (std::string const& file : deletes)
        plan.state.pendingDeletes.insert(file);
    return plan;
}

void commitUpload(SyncState& state, UploadAction const& upload)
{
    state.uploaded[upload.month] = UploadedMonth{upload.file, upload.hash, upload.revision, upload.day};
    state.pendingDeletes.erase(upload.file);
    for (std::string const& file : upload.retire)
        if (file != upload.file)
            state.pendingDeletes.insert(file);
}

void commitDelete(SyncState& state, std::string const& file)
{
    state.pendingDeletes.erase(file);
    for (auto it = state.uploaded.begin(); it != state.uploaded.end();) {
        if (it->second.file == file)
            it = state.uploaded.erase(it);
        else
            ++it;
    }
}

std::vector<EventFileRef> latestEventFiles(std::vector<std::string> const& remote, std::string_view month)
{
    std::map<std::string, EventFileRef> latest;
    if (not isMonth(month))
        return {};
    for (std::string const& name : remote) {
        auto const ref = parseEventFileName(name);
        if (not ref or ref->month != month)
            continue;
        auto const current = latest.find(ref->device);
        if (current == latest.end() or ref->revision > current->second.revision)
            latest[ref->device] = *ref;
    }
    std::vector<EventFileRef> result;
    result.reserve(latest.size());
    for (auto const& [device, ref] : latest)
        result.push_back(ref);
    return result;
}

}
