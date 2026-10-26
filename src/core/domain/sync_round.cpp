/**
 * @file sync_round.cpp
 * @brief One sync round, for sync_round.h.
 */

#include "core/domain/sync_round.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <utility>

#include "core/domain/sync_merge.h"
#include "core/domain/sync_run.h"
#include "core/domain/sync_state.h"
#include "util/log.h"
#include "util/text.h"

namespace lens::core::sync {

namespace {

/// The gist's description and the file in it; both are part of the cloud format (docs/adr/0042).
constexpr auto kDescription = "lens-sync";
constexpr auto kFileName    = "lens-sync.json";

/// Backoff for a failed automatic sync: 30 s, 1 min, 2 min, 4 min, then 8 min for each attempt after.
/// Capped, so a sync that keeps failing (no network, no proxy) costs one request per 8 minutes.
constexpr int kRetryBaseMs = 30 * 1000;
constexpr int kRetryMaxMs  = 8 * 60 * 1000;

/// @brief Holds the applying flag for the life of the scope, so a throw inside the apply cannot leave it set.
class ApplyingGuard {
public:
    explicit ApplyingGuard(bool& flag)
        : flag_(flag)
    {
        flag_ = true;
    }
    ~ApplyingGuard()
    {
        flag_ = false;
    }
    ApplyingGuard(const ApplyingGuard&)            = delete;
    ApplyingGuard& operator=(const ApplyingGuard&) = delete;

private:
    bool& flag_;
};

/// @return The error code the settings page shows for @p why.
std::string errorFor(Answer::Failure why)
{
    switch (why) {
        case Answer::Failure::Unauthorised: return "signInExpired";
        case Answer::Failure::NotFound: return "noGist";
        case Answer::Failure::Proxy: return "proxy";
        case Answer::Failure::RefusedRefresh: return "signInExpired";
        case Answer::Failure::Generic: break;
        case Answer::Failure::None: break;
    }
    return "generic";
}

/// @brief Whether a failure can clear by itself, so an automatic run is retried rather than reported once.
bool transient(const std::string& code)
{
    return code == "generic" || code == "proxy" || code == "noGist";
}

}

SyncRound::SyncRound(Transport& transport, KnownStore& known, StatsStore& stats, SyncPartition& partition, Clock clock, Hooks hooks)
    : transport_(transport), known_(known), stats_(stats), partition_(partition), clock_(std::move(clock)), hooks_(std::move(hooks))
{
    if (!clock_.epochMs)
        clock_.epochMs = [] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); };
    if (!clock_.minute)
        clock_.minute = [] { return util::localMinuteNow(); };
}

bool SyncRound::busy() const
{
    return step_ != Step::Idle;
}

bool SyncRound::applying() const
{
    return applying_;
}

bool SyncRound::carriesNewContent() const
{
    return contentFingerprint(known_, stats_) != uploadedFingerprint_;
}

void SyncRound::notifyChanged()
{
    if (hooks_.changed)
        hooks_.changed();
}

void SyncRound::begin(const Start& start)
{
    if (busy()) {
        requestRun();
        return;
    }
    // With no passphrase and no stored key, only the reader can start this run.
    if (start.passphrase.empty() && !partition_.hasKey()) {
        if (hooks_.error)
            hooks_.error("passphrase");
        return;
    }
    if (!partition_.signedIn())
        return;

    passphrase_       = start.passphrase;
    manual_           = start.manual;
    refreshedThisRun_ = false;
    resume_           = Step::Idle;
    if (hooks_.error)
        hooks_.error(""); // a run the reader can start clears the last one's report
    step_ = Step::Listing;
    notifyChanged();
    ask(Request::Kind::List);
}

void SyncRound::removeCloud()
{
    if (busy())
        return;
    const std::string id = partition_.gistId();
    if (id.empty()) {
        if (hooks_.error)
            hooks_.error("noGist");
        return;
    }
    if (!partition_.signedIn())
        return;

    // The reader pressed the button, so a failure is reported rather than retried.
    manual_           = true;
    refreshedThisRun_ = false;
    resume_           = Step::Idle;
    gistId_           = id;
    if (hooks_.error)
        hooks_.error("");
    step_ = Step::Deleting;
    notifyChanged();
    ask(Request::Kind::Delete);
}

void SyncRound::requestRun()
{
    if (busy())
        dirty_ = true;
}

void SyncRound::forget()
{
    // An answer to a request this run had already asked for is not the next run's.
    ++generation_;
    passphrase_.clear();
    sealed_.clear();
    uploadGistId_.clear();
    gistId_.clear();
    pendingFingerprint_.clear();
    uploadedFingerprint_.clear();
    step_             = Step::Idle;
    resume_           = Step::Idle;
    applying_         = false;
    manual_           = false;
    dirty_            = false;
    refreshedThisRun_ = false;
    retryAttempt_     = 0;
}

void SyncRound::ask(Request::Kind kind)
{
    Request request;
    request.kind  = kind;
    request.token = partition_.token();
    switch (kind) {
        case Request::Kind::List: request.description = kDescription; break;
        case Request::Kind::Read:
            request.gistId   = gistId_;
            request.fileName = kFileName;
            break;
        case Request::Kind::Create:
            request.description = kDescription;
            request.fileName    = kFileName;
            request.content     = sealed_;
            break;
        case Request::Kind::Update:
            request.gistId   = uploadGistId_;
            request.fileName = kFileName;
            request.content  = sealed_;
            break;
        case Request::Kind::Delete: request.gistId = gistId_; break;
        // A refresh spends the token the refused call left on this device, not the one it used.
        case Request::Kind::Refresh: request.token = partition_.refreshToken(); break;
    }

    const unsigned generation = generation_;
    const Step     expected   = step_;
    transport_.send(request, [this, generation, expected](const Answer& answer) {
        if (generation != generation_)
            return;
        onAnswer(expected, answer);
    });
}

void SyncRound::onAnswer(Step expected, const Answer& answer)
{
    if (step_ != expected)
        return;
    switch (expected) {
        case Step::Listing: onListed(answer); break;
        case Step::Reading: onRead(answer); break;
        case Step::Creating: onCreate(answer); break;
        case Step::Writing: onUpdate(answer); break;
        case Step::Deleting: onDelete(answer); break;
        case Step::Refreshing: onRefresh(answer); break;
        case Step::Idle: break;
    }
}

void SyncRound::onListed(const Answer& answer)
{
    if (answer.failure != Answer::Failure::None) {
        callFailed(answer.failure);
        return;
    }
    // No gist on this account yet: the merge starts from this device alone.
    if (answer.gistId.empty()) {
        process(std::string(), std::string());
        return;
    }
    gistId_ = answer.gistId;
    step_   = Step::Reading;
    ask(Request::Kind::Read);
}

void SyncRound::onRead(const Answer& answer)
{
    if (answer.failure != Answer::Failure::None) {
        callFailed(answer.failure);
        return;
    }
    process(answer.gistId.empty() ? gistId_ : answer.gistId, answer.content);
}

void SyncRound::onCreate(const Answer& answer)
{
    if (answer.failure != Answer::Failure::None) {
        callFailed(answer.failure);
        return;
    }
    partition_.setGistId(answer.gistId);
    uploaded();
}

void SyncRound::onUpdate(const Answer& answer)
{
    if (answer.failure != Answer::Failure::None) {
        callFailed(answer.failure);
        return;
    }
    uploaded();
}

void SyncRound::onDelete(const Answer& answer)
{
    if (answer.failure != Answer::Failure::None) {
        callFailed(answer.failure);
        return;
    }
    partition_.forgetCloudCopy();
    // The cloud copy is gone, so the last upload no longer describes it: the next run uploads even
    // when nothing on this device has changed.
    uploadedFingerprint_.clear();
    succeed();
}

void SyncRound::onRefresh(const Answer& answer)
{
    if (answer.failure != Answer::Failure::None) {
        // A refused refresh token is proof the stored sign-in is dead; forget it, so the page
        // offers sign-in again. Either way the run ends here rather than leaving its state behind.
        if (answer.failure == Answer::Failure::RefusedRefresh)
            partition_.forgetTokens();
        fail(errorFor(answer.failure));
        return;
    }

    partition_.storeTokens(answer.access, answer.refresh);
    // Back to what the 401 interrupted, once: a sync re-lists (listing is where every run starts),
    // a delete asks for the id it was given again.
    const Step resume = resume_;
    resume_           = Step::Idle;
    if (resume == Step::Deleting) {
        gistId_ = partition_.gistId();
        if (gistId_.empty()) {
            fail("noGist");
            return;
        }
        step_ = Step::Deleting;
        ask(Request::Kind::Delete);
        return;
    }
    step_ = Step::Listing;
    ask(Request::Kind::List);
}

void SyncRound::callFailed(Answer::Failure why)
{
    // An access token GitHub stopped accepting. An OAuth app whose user tokens expire answers this
    // eight hours after every sign-in (docs/adr/0042); the refresh token buys the next one, and the
    // run carries on from where the 401 stopped it. No expiry is stored and nothing is renewed
    // early -- a 401 is the trigger.
    if (why == Answer::Failure::Unauthorised && syncrun::shouldRenewSignIn(true, partition_.hasRefreshToken(), refreshedThisRun_)) {
        refreshedThisRun_ = true;
        resume_           = step_ == Step::Deleting ? Step::Deleting : Step::Listing;
        step_             = Step::Refreshing;
        LENS_INFO("the gist call was refused; renewing the sign-in token and carrying on");
        ask(Request::Kind::Refresh);
        return;
    }
    // The gist is gone: forget its id, so the next run makes a new one.
    if (why == Answer::Failure::NotFound)
        partition_.forgetGistId();
    // A 401 that no refresh answered means the sign-in is dead for good: forget it, so the page
    // offers the sign-in button rather than a Sync now that cannot work.
    if (why == Answer::Failure::Unauthorised) {
        LENS_WARN("the GitHub sign-in is no longer accepted and cannot be renewed; the reader has to sign in again");
        partition_.forgetTokens();
    }
    fail(errorFor(why));
}

void SyncRound::process(const std::string& gistId, const std::string& envelopeText)
{
    try {
        const auto opened = syncrun::openCloudCopy({.envelopeText = envelopeText,
                                                    .passphrase   = passphrase_,
                                                    .stored       = partition_.storedKey()});
        // Written before the error is read, because a run that stopped with "generic" may have
        // derived a key that opened the cloud copy, and that key is this device's either way.
        if (opened.keyToStore)
            partition_.storeKey(*opened.keyToStore);
        if (!opened.error.empty() || !opened.key) {
            // A run that stopped has no key to seal under. An empty code beside a missing key is
            // something other than what the reader did, so it is reported as the generic failure.
            fail(opened.error.empty() ? "generic" : opened.error);
            return;
        }

        const std::string    device = partition_.deviceId();
        const nlohmann::json local  = buildLocalDocument(known_, stats_, partition_.settingsSnapshot(), device, clock_.epochMs());
        const nlohmann::json merged = merge(local, opened.remote, device);

        const unsigned generation = generation_;
        {
            ApplyingGuard applying(applying_);
            applyMarks(known_, merged);
            applyHistory(stats_, merged);
            applyDaily(stats_, merged, device);
            applySettings(known_, known_.section(), merged);
            const nlohmann::json settings = merged.value("settings", nlohmann::json::object());
            partition_.setSettingsSnapshot(snapshotOf(settings));
            known_.save();
            if (hooks_.applied)
                hooks_.applied();
        }
        // A listener of the applied hook may have signed out: the run was abandoned there, and the
        // upload it was about to ask for is not this device's to make any more.
        if (generation != generation_)
            return;

        // Taken here, after the apply: what this upload carries is the state it leaves behind. A
        // change made during the run is not in it, so it still counts as new and schedules the next.
        pendingFingerprint_ = contentFingerprint(known_, stats_);

        const auto upload = syncrun::sealUpload({.key                 = *opened.key,
                                                 .merged              = merged,
                                                 .gistId              = gistId,
                                                 .manual              = manual_,
                                                 .fingerprint         = pendingFingerprint_,
                                                 .uploadedFingerprint = uploadedFingerprint_});
        if (upload.command == syncrun::Upload::Command::None) {
            // Nothing to send is still a sync that worked: it stamps the time and clears a backoff
            // left by an earlier failure.
            partition_.setLastSyncedAt(clock_.minute());
            succeed();
            return;
        }

        sealed_       = upload.sealed;
        uploadGistId_ = upload.gistId;
        if (upload.command == syncrun::Upload::Command::Create) {
            step_ = Step::Creating;
            ask(Request::Kind::Create);
        } else {
            step_ = Step::Writing;
            ask(Request::Kind::Update);
        }
    } catch (const std::exception& failure) {
        LENS_ERROR("sync: {}", failure.what());
        fail("generic");
    }
}

void SyncRound::uploaded()
{
    partition_.setLastSyncedAt(clock_.minute());
    uploadedFingerprint_ = pendingFingerprint_;
    succeed();
}

void SyncRound::succeed()
{
    end(true);
}

void SyncRound::fail(std::string code)
{
    if (hooks_.error)
        hooks_.error(code);
    // Read before end(), which clears the run: only a failure that can clear by itself is retried. A
    // wrong passphrase, an expired sign-in and a refused one need the reader, so retrying them would
    // only repeat the same error. A run the reader asked for is reported, not retried.
    const bool retry = transient(code) && !manual_;
    end(false);
    if (!retry)
        return;
    // Back off from 30 s, doubling, and stop at 8 min so a device with no network costs one request
    // per 8 minutes rather than one per 30 seconds.
    const int delay = std::min(kRetryMaxMs, kRetryBaseMs << std::min(retryAttempt_, 8));
    ++retryAttempt_;
    if (hooks_.retry)
        hooks_.retry(std::chrono::milliseconds(delay));
}

void SyncRound::end(bool succeeded)
{
    passphrase_.clear();
    sealed_.clear();
    uploadGistId_.clear();
    gistId_.clear();
    pendingFingerprint_.clear();
    step_             = Step::Idle;
    resume_           = Step::Idle;
    manual_           = false;
    refreshedThisRun_ = false;
    if (succeeded)
        retryAttempt_ = 0;
    notifyChanged();
    if (hooks_.finished)
        hooks_.finished(succeeded);
    if (dirty_) {
        dirty_ = false;
        if (hooks_.rerun)
            hooks_.rerun();
    }
}

}
