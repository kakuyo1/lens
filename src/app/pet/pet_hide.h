#pragma once

#include <chrono>
#include <optional>

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>

#include "app/pet/pet_wander.h"

/**
 * @file pet_hide.h
 * @brief One edge hide from the first slide to the moment the pet is back on screen: the peek, the pointer's lean,
 *        the ask's pause, the call-out and the snap-back (ADR 0076, 0095).
 *
 * Pure logic: the controller owns the window, the frame clock, the state machine, the screen lookup and the hover
 * sense, and writes what this module answers. PetWander stays its geometry: the hidden base, the slide, the peek
 * offset and the drag-out rule.
 */

namespace lens::app::pet {

class PetHide {
public:
    /// @brief The figure's ink in its own canvas units, and the canvas the window scales by. The sliver a hidden pet
    ///        leaves visible is measured on this ink, never on the square canvas.
    struct Ink {
        /// Side of one square canvas, in pixels.
        double canvas{0.0};
        /// The peek figure's ink edges, in canvas units.
        double left{0.0};
        double right{0.0};
        /// The figure's ink top and bottom, in canvas units.
        double top{0.0};
        double bottom{0.0};

        /// @return The peek ink's centre and the figure's vertical middle, in canvas units.
        [[nodiscard]] QPointF centre() const;
    };

    /// @brief One tick's answer.
    struct Step {
        /// Where the window goes. Absent while the reader is dragging it.
        std::optional<QPoint> position;
        /// Whether the peek loop should be playing; the controller starts it if the pet has the art.
        bool loopPeek{false};
        /// Whether the call-out reached its spot. `position` is then the pet's new home.
        bool left{false};
    };

    /// @brief What a review question does to the hide.
    struct Ask {
        QPoint position;
        /// Whether a call-out was already running, so the hide ends here and the caller settles it.
        bool left{false};
    };

    /// @brief What a release does to the hide.
    struct Drop {
        /// The reader pulled the pet back inside, so the hide is over.
        bool pulledOut{false};
        /// Where the window snaps back when the drag was too short; unused when pulled out.
        QPoint snapBackTo;
    };

    /**
     * @brief Starts a hide.
     * @param side The edge it hides against.
     * @param from The window's top-left now; the slide starts here.
     * @param size The window size.
     * @param area The work area of the screen it hides on.
     * @param screen That screen's device name, so a later change can find it again.
     * @param ink The figure's ink, which the sliver and its lean are measured on.
     */
    void begin(EdgeSide side, QPoint from, QSize size, QRect area, QString screen, Ink ink);

    /// @brief Forgets the hide where it is, as when the pet is turned off or replaced.
    void               stop();
    [[nodiscard]] bool active() const
    {
        return active_;
    }
    [[nodiscard]] EdgeSide side() const
    {
        return side_;
    }
    [[nodiscard]] QRect area() const
    {
        return area_;
    }
    [[nodiscard]] QString screen() const
    {
        return screen_;
    }
    /// @return The figure's ink as the hide measured it.
    [[nodiscard]] Ink ink() const
    {
        return ink_;
    }

    /// @return Whether the pet is fully out, still and peeking: no slide, drag, ask or call-out.
    [[nodiscard]] bool peeking() const;
    /// @return Whether a release at `at` has brought enough of the window inside to end the hide (ADR 0095).
    [[nodiscard]] bool pulledOut(QPoint at, QSize size) const;

    /// @brief The reader took hold of the window; tick() stops moving it until drop().
    void dragStart();
    /// @brief The reader let go. Ends the hide when pulled out, otherwise snaps it back to the hidden base.
    Drop drop(QPoint at, QSize size);

    /// @brief A click on the sliver: slide back to where the hide started, ending there (ADR 0095).
    void lureOut(QPoint at, QSize size);
    /// @brief A review question: wait at the hidden base until the state machine is idle again.
    Ask ask();

    /**
     * @brief Recomputes the hidden base after the window or its screen changed.
     * @param area The work area to hide on; the same one unless the screen changed.
     * @param size The window size.
     * @return The new base the caller writes.
     */
    QPoint refit(QRect area, QSize size);

    /**
     * @brief The pointer's distance to the sliver a hidden pet leaves visible: the peek ink clipped at the work-area
     *        edge, in canvas units. The rows above and below the figure never count.
     * @param cursor The pointer, in screen pixels.
     * @param origin The window's top-left now.
     */
    [[nodiscard]] double distanceToSliver(QPoint cursor, QPoint origin) const;

    /**
     * @brief Moves the hide on.
     * @param step Time since the last tick.
     * @param idle Whether the state machine is idle, so an ask pause can end and the peek can play.
     * @param pointerNear Whether the pointer is near the visible sliver; the pet leans in while it is.
     */
    Step tick(std::chrono::milliseconds step, bool idle, bool pointerNear);

private:
    /// @return The window's pixels per canvas unit.
    [[nodiscard]] double scale() const;

    bool                      active_{false};
    EdgeSide                  side_{EdgeSide::Left};
    QString                   screen_;
    QRect                     area_;
    QSize                     size_;
    Ink                       ink_;
    QPoint                    base_;
    QPoint                    from_;
    std::chrono::milliseconds slide_{0};
    std::chrono::milliseconds peekClock_{0};
    double                    lean_{0.0};
    bool                      leaving_{false};
    QPoint                    leaveFrom_;
    QPoint                    leaveTo_;
    std::chrono::milliseconds leave_{0};
    bool                      asking_{false};
    bool                      dragging_{false};
};

} // namespace lens::app::pet
