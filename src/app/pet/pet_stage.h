#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <QPointF>
#include <QString>

#include "app/pet/pet_scenes.h"
#include "core/domain/pet/pet_mood.h"
#include "core/domain/pet/pet_state.h"

/**
 * @file pet_stage.h
 * @brief The scene stage: the idle lottery, the entry timeline, the frame, and the two after-contexts
 *        (ADR 0049, 0093, 0101, 0102).
 *
 * Pure logic: no window, no QObject, and no clock of its own -- the controller reads the wall clock and the monotonic
 * one and hands both in, so a test pins the moment and the frame by handing down values. The controller keeps the
 * sheets, the URLs and the window; this module answers which scene the idle turn plays, how far an entry has come,
 * and where the pet sits in the scene's frame.
 */

namespace lens::app::pet {

class PetStage {
public:
    /// @brief A reading this long earns the after-reading context: the pet answers a session, not a glance (ADR 0093).
    static constexpr std::chrono::milliseconds kLongReading{180'000};
    /// @brief How long after a long reading the after-reading context stays up.
    static constexpr std::chrono::milliseconds kReadingAfterglow{120'000};
    /// @brief How long after a stroll ends the after-return context stays up.
    static constexpr std::chrono::milliseconds kReturnAfterglow{90'000};
    /// @brief The fold where a settle hands the idle pose over to the scene pose (ADR 0101).
    static constexpr std::chrono::milliseconds kSceneCrossfade{180};
    /// @brief How long a landing's particles last. They may outlive the entry that started them.
    static constexpr std::chrono::milliseconds kImpact{320};

    /// @brief One scene the idle lottery may pick: the weights its mood table gives, and how far a worn accessory
    ///        tilts it (ADR 0102).
    struct Offer {
        core::pet::ActionName name;
        core::pet::SceneMood  mood;
        double                favour{1.0};
    };

    /// @brief The body a hop or a settle holds until the landing: the idle action's playback, and the frame it was
    ///        already on, so a hop does not restart the idle bob.
    struct Pose {
        int  frames{0};
        int  fps{1};
        bool loop{false};
        int  frame{0};
    };

    /// @brief What one frame of an entry shows. Outside an entry every field is the neutral value the scene draws.
    struct EntryFrame {
        /// Whether the pet is drawn in front of the props yet; a hop crosses them at its own point (ADR 0101).
        bool inFront{false};
        /// How far the props have come in, and how far they still rise.
        double propAlpha{1.0};
        double propOffset{0.0};
        /// The two bodies' opacities: the idle pose a hop holds, and the scene pose a settle folds in.
        double idleAlpha{1.0};
        double sceneAlpha{0.0};
        /// Where the pet sits in the frame and how large, already eased.
        QPointF shift{};
        double  scale{1.0};
        /// Whether this frame wears the scene's own first frame; otherwise it wears the held pose at `poseFrame`.
        bool scenePose{false};
        int  poseFrame{0};
    };

    /// @brief What one tick of an entry answers.
    struct EntryStep {
        /// The frame the entry wants now. Empty once the entry has landed, and when none runs.
        std::optional<EntryFrame> frame;
        /// The entry landed on this tick: the caller starts the impact and shows the scene's own first frame.
        bool landed{false};
    };

    /// @brief A landing's particles.
    struct Impact {
        QString kind;
        QPointF at;
        /// 0 at the landing, 1 when the last of them has gone.
        double progress{0.0};
    };

    /// @brief Where the pet sits in a scene at one frame, and how the stage aligns the props to its feet.
    struct Frame {
        /// The frame the data actually has. An index past the end reads the last one, so a scene whose data is
        /// shorter than its action still shows rather than dropping its props mid-scene.
        int     index{0};
        QPointF shift{};
        double  scale{1.0};
        QPointF align{};
    };

    /// @param roll A source in [0, 1), drawn once per lottery.
    explicit PetStage(core::pet::RandomSource roll);

    /// @name The moment (ADR 0093). The controller reads both clocks; these take the readings.
    /// @{
    /// @brief An explanation went up.
    void readingBegan(std::chrono::steady_clock::time_point now);
    /// @brief An explanation came down: a long one starts the after-reading context, a glance earns nothing.
    void readingEnded(std::chrono::steady_clock::time_point now);
    /// @brief A stroll ended, so the pet is back at rest.
    void returnedHome(std::chrono::steady_clock::time_point now);
    /// @brief The moment the idle turn runs in: the day stretch the caller read, and the two after-contexts.
    [[nodiscard]] core::pet::PetMood mood(std::chrono::steady_clock::time_point now, int minuteOfDay) const;
    /// @}

    /**
     * @brief Draws one of `offers`: every scene keeps a chance, the weights only tilt it (ADR 0093).
     * @param offers The scenes this pet plays, with their weights and favours. Empty for a pet with none.
     * @param mood The moment the draw runs in.
     * @return The scene picked, or nothing when no offer is left. The last scene drawn is refused while another is
     *         on offer, so a four-scene pool reads as a pool.
     */
    std::optional<core::pet::ActionName> draw(std::vector<Offer> const& offers, core::pet::PetMood const& mood);
    /// @brief Records a scene the settings page started, so the next idle draw does not bring it straight back.
    void notePlayed(core::pet::ActionName const& name);

    /**
     * @brief Starts an entry for `scene`, which must carry an `entry` block.
     * @param pose The body a hop or a settle holds until the landing. A fade ignores it.
     * @param inkTop The pet's ink top in canvas units, which caps a hop's arc under the window's edge.
     */
    void beginEntry(Scene const& scene, Pose pose, double inkTop);
    /**
     * @brief Moves the entry and its particles on by `step`.
     * @return The frame the entry wants now, or the landing that ends it. A landing's own tick does not age the
     *         particles it starts.
     */
    EntryStep tick(std::chrono::milliseconds step);
    /// @brief Ends the entry at its landing now, without its impact, keeping the scene remembered as entered.
    void landEntry();
    /// @brief Forgets the entry, its impact and the scene it belonged to, as when the scene ends or motion is reduced
    ///        and the pet goes back to being shown plainly.
    void stopEntry();
    /// @brief Forgets the pet's scenes and the lottery's memory, and any entry with them. The two after-contexts
    ///        belong to the reader rather than to the pet, so they stay.
    void reset();

    [[nodiscard]] bool entering() const
    {
        return entry_.has_value();
    }
    /// @return The scene whose entry has begun, empty when none has. It stays named after the landing, so the
    ///         controller does not start the same entry again on the next tick.
    [[nodiscard]] std::string const& sceneId() const
    {
        return sceneId_;
    }
    /// @return The frame the entry is showing now; the neutral frame when none runs.
    [[nodiscard]] EntryFrame const& frame() const
    {
        return frame_;
    }
    /// @return The landing's particles while they last, which may be after the entry itself has ended.
    [[nodiscard]] std::optional<Impact> impact() const;
    /// @return The scene the last draw or the settings page named; empty before the first.
    [[nodiscard]] std::string const& lastPlayed() const
    {
        return last_;
    }

    /// @brief Where the pet sits in `scene` at `frame`, and how the props align to its feet.
    /// @param feet The pet's own feet in canvas units, measured when it loaded.
    static Frame frameOf(Scene const& scene, int frame, QPointF feet);

private:
    /// @brief The entry running: the scene it belongs to, the body it holds, and how long it has come.
    struct EntryState {
        Scene const*              scene{nullptr};
        Pose                      pose;
        double                    inkTop{0.0};
        std::chrono::milliseconds elapsed{0};
    };

    /// @brief The landing's particles, with the time they have run.
    struct ImpactState {
        QString                   kind;
        QPointF                   at;
        std::chrono::milliseconds elapsed{0};
    };

    /// @return The frame the entry wants at its current elapsed time.
    [[nodiscard]] EntryFrame frameAt() const;
    /// @return The held pose's frame at `elapsed`, wrapped for a loop and held at the last for a one-shot.
    [[nodiscard]] int poseFrameAt(std::chrono::milliseconds elapsed) const;

    core::pet::RandomSource    roll_;
    std::string                last_;
    std::string                sceneId_;
    std::optional<EntryState>  entry_;
    std::optional<ImpactState> impact_;
    EntryFrame                 frame_;
    /// Whether the entry began this tick, which does not advance it.
    bool justBegan_{false};

    /// The reading now running, and when each after-context expires (ADR 0093).
    std::optional<std::chrono::steady_clock::time_point> readingSince_;
    std::chrono::steady_clock::time_point                readingAfter_{};
    std::chrono::steady_clock::time_point                returnAfter_{};
};

} // namespace lens::app::pet
