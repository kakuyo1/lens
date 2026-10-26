#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>

#include "core/document/known_store.h"
#include "core/document/stats_store.h"
#include "core/domain/sync_partition.h"

/**
 * @file sync_round.h
 * @brief One sync from the listing of the cloud copy to the upload that follows it: the order, the
 *        401's renewal, the backoff after a failure, and the run a change during this one asks for
 *        (docs/adr/0042, 0057).
 *
 * No Qt, no timer, no window: a run starts from begin(), the transport answers through the port, and
 * each answer either asks for the next request, writes the merged document into the stores, or ends.
 * SyncDuty is the adapter: it owns the timers and the surfaces' state, and arms a retry with the delay
 * this module names.
 */

namespace lens::core::sync {

/// @brief One request a round asks the transport to carry.
struct Request {
    /// @brief Which call to make.
    enum class Kind {
        List,   ///< Find the sync gist by its description.
        Read,   ///< Read one gist's file.
        Create, ///< Make the sync gist with the file.
        Update, ///< Replace the file in the sync gist.
        Delete, ///< Delete the sync gist.
        Refresh ///< Spend the refresh token on a new pair of tokens.
    };

    Kind        kind = Kind::List;
    std::string token;       ///< Bearer for every gist call; the refresh token for Refresh.
    std::string gistId;      ///< Read, Update, Delete.
    std::string description; ///< List, Create: the description the sync gist carries.
    std::string fileName;    ///< Read, Create, Update: the file the cloud copy lives in.
    std::string content;     ///< Create, Update: the file's text.
};

/// @brief The transport's answer to one request.
struct Answer {
    /// @brief Why a call did not answer with what was asked for.
    enum class Failure {
        None,
        Unauthorised,   ///< The access token is refused: revoked, or it lost the gist scope.
        NotFound,       ///< The gist is not there any more.
        Proxy,          ///< The connection never got a byte.
        Generic,        ///< Anything else, or a body that was not what was asked for.
        RefusedRefresh, ///< The refresh token itself is refused.
    };

    Failure     failure = Failure::None;
    std::string gistId;  ///< List: the id, empty when no sync gist exists. Read, Create, Delete: the id.
    std::string content; ///< Read: the file's text; empty when the file is absent.
    std::string access;  ///< Refresh: the new access token.
    std::string refresh; ///< Refresh: the new refresh token; empty when the app issues none.
};

/**
 * @brief The network a round asks; the application's gist and auth clients are the production one.
 *
 * A reply is delivered exactly once and may arrive after send() returns. A round asks for one request
 * at a time, so a transport that answers in order never sees two outstanding.
 */
class Transport {
public:
    virtual ~Transport() = default;

    using Reply = std::function<void(const Answer&)>;
    /// @brief Carry @p request and call @p reply once, when the answer is in.
    virtual void send(const Request& request, Reply reply) = 0;
};

/// @brief What a round tells its adapter while it runs.
struct Hooks {
    /// @brief The phase or the last-synced time changed; the surfaces should re-read what they show.
    std::function<void()> changed;
    /// @brief The error code changed; an empty one clears it.
    std::function<void(std::string)> error;
    /// @brief A run ended; true when it succeeded. Called before retry().
    std::function<void(bool)> finished;
    /// @brief A failed automatic run asks to run again after this delay; the adapter arms the timer.
    std::function<void(std::chrono::milliseconds)> retry;
    /// @brief A run was asked for during this one; the adapter schedules it once this one ends.
    std::function<void()> rerun;
    /// @brief The merged document was written into the stores, inside the apply; forward it.
    std::function<void()> applied;
};

class SyncRound {
public:
    /// @brief What starts a run.
    struct Start {
        std::string passphrase;     ///< Empty uses the key stored on this device.
        bool        manual = false; ///< The reader asked; a failure is reported, not retried.
    };

    /// @brief The times a run stamps and merges with; a test pins both.
    struct Clock {
        /// @return Milliseconds since the epoch, for the setting stamps a merge compares.
        std::function<long long()> epochMs;
        /// @return The local minute in util::kMinuteFormat, which the settings page shows.
        std::function<std::string()> minute;
    };

    /**
     * @brief Bind the round to the stores, the partition and the network it runs on.
     * @param transport The network; see Transport.
     * @param known     The document owner the merge is written into.
     * @param stats     History and daily tallies, bound to the same document.
     * @param partition The typed `sync` object of that document.
     * @param clock     The times; empty members use the system clock.
     * @param hooks     What to tell the adapter; any member may be empty.
     */
    SyncRound(Transport& transport, KnownStore& known, StatsStore& stats, SyncPartition& partition, Clock clock = {}, Hooks hooks = {});

    /**
     * @brief Run one sync. A run already in flight books another one instead.
     * @param start The passphrase this run was given, and who asked for it.
     * @note With no passphrase and no stored key the run does not start; the error hook names it.
     */
    void begin(const Start& start);

    /// @brief Delete the cloud copy, renewing a refused call once. A run in flight is left alone.
    void removeCloud();

    /// @brief A run was asked for while one is in flight: run another when this one ends.
    void requestRun();

    /// @brief Forget this session's memory and abandon any run in flight; the partition is the caller's.
    void forget();

    /// @return Whether a run is in flight.
    [[nodiscard]] bool busy() const;
    /// @return Whether the merged document is being written right now; a save-driven schedule is ignored then.
    [[nodiscard]] bool applying() const;
    /// @return Whether the stores carry something the last successful upload did not.
    [[nodiscard]] bool carriesNewContent() const;

private:
    enum class Step { Idle,
                      Listing,
                      Reading,
                      Creating,
                      Writing,
                      Deleting,
                      Refreshing };

    /// @brief Ask the transport for @p kind, with the id, file and text the step carries.
    void ask(Request::Kind kind);
    /// @brief Take one answer the step @p expected asked for.
    void onAnswer(Step expected, const Answer& answer);
    void onListed(const Answer& answer);
    void onRead(const Answer& answer);
    void onCreate(const Answer& answer);
    void onUpdate(const Answer& answer);
    void onDelete(const Answer& answer);
    void onRefresh(const Answer& answer);
    /// @brief A refused gist call: renew a 401 once, otherwise end the run with the code it names.
    void callFailed(Answer::Failure why);
    /// @brief Open the cloud copy, merge it, write it, and ask for the upload it needs.
    void process(const std::string& gistId, const std::string& envelopeText);
    /// @brief The upload landed: stamp the time and remember what went up.
    void uploaded();
    void succeed();
    /// @brief End a failed run, arming the backoff when it can clear by itself.
    void fail(std::string code);
    void end(bool succeeded);
    void notifyChanged();

    Transport&     transport_;
    KnownStore&    known_;
    StatsStore&    stats_;
    SyncPartition& partition_;
    Clock          clock_;
    Hooks          hooks_;
    Step           step_ = Step::Idle;
    /// Where a 401's renewal carries on from: redo the sync, or redo the delete.
    Step resume_           = Step::Idle;
    bool applying_         = false;
    bool manual_           = false;
    bool dirty_            = false;
    bool refreshedThisRun_ = false;
    int  retryAttempt_     = 0;
    /// Incremented by forget(), so an answer to an abandoned run is not taken for a new one's.
    unsigned    generation_ = 0;
    std::string passphrase_; ///< Held for the run that was given it; cleared when the run ends.
    std::string gistId_;     ///< The gist the step in flight names.
    std::string sealed_;     ///< The envelope the upload carries.
    std::string uploadGistId_;
    /// What the last successful upload carried; empty until one has succeeded in this session, so
    /// the first sync after launch always uploads.
    std::string uploadedFingerprint_;
    /// The fingerprint of the upload in flight; it becomes uploadedFingerprint_ only once the upload
    /// succeeds, so a failed upload leaves the last good one in place and the next change still syncs.
    std::string pendingFingerprint_;
};

}
