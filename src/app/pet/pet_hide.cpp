/**
 * @file pet_hide.cpp
 * @brief Implementation of PetHide: the slide, the peek, the lean, the ask pause and the call-out.
 */

#include "app/pet/pet_hide.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "app/pet/pet_placement.h"

namespace lens::app::pet {

using namespace std::chrono_literals;

QPointF PetHide::Ink::centre() const
{
    return QPointF{(left + right) / 2.0, (top + bottom) / 2.0};
}

double PetHide::scale() const
{
    if (ink_.canvas <= 0.0 or size_.width() <= 0) return 1.0;
    return static_cast<double>(size_.width()) / ink_.canvas;
}

void PetHide::begin(EdgeSide side, QPoint from, QSize size, QRect area, QString screen, Ink ink)
{
    mode_      = Mode::Sliding;
    side_      = side;
    screen_    = std::move(screen);
    area_      = area;
    size_      = size;
    ink_       = ink;
    from_      = from;
    base_      = PetWander::hiddenBase(area, size, side, from, ink.left * scale(), ink.right * scale());
    slide_     = 0ms;
    peekClock_ = 0ms;
    lean_      = 0.0;
    leaveFrom_ = {};
    leaveTo_   = {};
    leave_     = 0ms;
    dragging_  = false;
}

void PetHide::stop()
{
    mode_      = Mode::Off;
    dragging_  = false;
    slide_     = 0ms;
    peekClock_ = 0ms;
    lean_      = 0.0;
    leave_     = 0ms;
}

bool PetHide::peeking() const
{
    return mode_ == Mode::Peeking and not dragging_;
}

bool PetHide::pulledOut(QPoint at, QSize size) const
{
    return mode_ != Mode::Off and PetWander::draggedOut(at, size, area_);
}

void PetHide::dragStart()
{
    if (mode_ != Mode::Off) dragging_ = true;
}

PetHide::Drop PetHide::drop(QPoint at, QSize size)
{
    if (mode_ == Mode::Off) return {};
    dragging_ = false;
    if (PetWander::draggedOut(at, size, area_)) {
        mode_ = Mode::Off;
        return {.pulledOut = true, .snapBackTo = {}};
    }

    // A drag that never pulled the pet out puts the hide back as it was, including one caught mid-call-out.
    if (mode_ == Mode::Sliding or mode_ == Mode::Leaving) mode_ = Mode::Peeking;
    lean_      = 0.0;
    slide_     = PetWander::kHideSlide;
    peekClock_ = 0ms;
    return {.pulledOut = false, .snapBackTo = base_};
}

void PetHide::lureOut(QPoint at, QSize size)
{
    if (mode_ == Mode::Off or dragging_ or mode_ == Mode::Leaving) return;
    mode_      = Mode::Leaving;
    leave_     = 0ms;
    leaveFrom_ = at;
    leaveTo_   = clampedWithin(from_, size, area_);
}

PetHide::Ask PetHide::ask()
{
    if (mode_ == Mode::Off) return {};
    if (mode_ == Mode::Leaving) return {.position = leaveTo_, .left = true};

    // The question and its sign stand beside the hidden base; the peek waits until the machine is idle again.
    mode_      = Mode::Asking;
    slide_     = PetWander::kHideSlide;
    peekClock_ = 0ms;
    return {.position = base_, .left = false};
}

QPoint PetHide::refit(QRect area, QSize size)
{
    if (mode_ == Mode::Off) return {};
    area_ = area;
    size_ = size;
    base_ = PetWander::hiddenBase(area_, size_, side_, base_, ink_.left * scale(), ink_.right * scale());
    if (mode_ == Mode::Sliding) mode_ = Mode::Peeking;
    slide_     = PetWander::kHideSlide;
    peekClock_ = 0ms;
    return base_;
}

double PetHide::distanceToSliver(QPoint cursor, QPoint origin) const
{
    if (mode_ == Mode::Off or ink_.canvas <= 0.0 or size_.width() <= 0) return 0.0;

    auto const sx = scale();
    auto const sy = size_.height() / ink_.canvas;
    auto       x1 = origin.x() + ink_.left * sx;
    auto       x2 = origin.x() + ink_.right * sx;
    auto const y1 = origin.y() + ink_.top * sy;
    auto const y2 = origin.y() + ink_.bottom * sy;
    if (side_ == EdgeSide::Left)
        x1 = std::max(x1, static_cast<double>(area_.left()));
    else
        x2 = std::min(x2, static_cast<double>(area_.right() + 1));
    auto const dx = std::max({x1 - cursor.x(), 0.0, cursor.x() - x2});
    auto const dy = std::max({y1 - cursor.y(), 0.0, cursor.y() - y2});
    return std::hypot(dx, dy) / sx;
}

QPointF PetHide::sliverCentre(QPoint origin) const
{
    if (mode_ == Mode::Off or ink_.canvas <= 0.0 or size_.width() <= 0) return ink_.centre();

    auto const sx = scale();
    auto       x1 = origin.x() + ink_.left * sx;
    auto       x2 = origin.x() + ink_.right * sx;
    if (side_ == EdgeSide::Left)
        x1 = std::max(x1, static_cast<double>(area_.left()));
    else
        x2 = std::min(x2, static_cast<double>(area_.right() + 1));
    // The look point must sit on the part the reader can see, not on the full window's ink: with the window mostly
    // past the edge, the full centre lies off screen and every cursor beside the sliver reads as one direction.
    auto const midX = (x1 + x2) / 2.0;
    return QPointF{(midX - origin.x()) / sx, (ink_.top + ink_.bottom) / 2.0};
}

PetHide::Step PetHide::tick(std::chrono::milliseconds step, bool idle, bool pointerNear)
{
    if (mode_ == Mode::Off or dragging_) return {};

    if (mode_ == Mode::Asking and idle) {
        // The question and its reaction are over: the pet picks the peek up from the start.
        mode_      = Mode::Peeking;
        peekClock_ = 0ms;
    }

    if (mode_ == Mode::Leaving) {
        leave_ += step;
        if (leave_ < PetWander::kHideSlide)
            return {.position = PetWander::slideAt(leaveFrom_, leaveTo_, leave_)};
        return {.position = leaveTo_, .left = true};
    }

    auto answer = Step{};
    if (mode_ == Mode::Sliding) {
        slide_          = std::min(PetWander::kHideSlide, slide_ + step);
        answer.position = PetWander::slideAt(from_, base_, slide_);
        if (slide_ >= PetWander::kHideSlide) mode_ = Mode::Peeking;
    } else if (mode_ == Mode::Asking) {
        answer.position = base_;
    } else {
        peekClock_ += step;
        // A pointer near the visible sliver earns a further lean toward the screen, ramped so it reads as a lean.
        // The peek itself keeps its own cycle underneath (ADR 0095).
        auto const target = pointerNear ? static_cast<double>(PetWander::kHoverLean) : 0.0;
        auto const rate   = static_cast<double>(PetWander::kHoverLean) / PetWander::kHoverLeanTime.count();
        lean_             = lean_ < target ? std::min(target, lean_ + rate * static_cast<double>(step.count()))
                                           : std::max(target, lean_ - rate * static_cast<double>(step.count()));
        auto at           = base_;
        at.rx() += (side_ == EdgeSide::Left ? 1 : -1) *
                   (PetWander::peekOffset(peekClock_) + static_cast<int>(std::lround(lean_)));
        answer.position = at;
    }

    // A reaction or the sign ends into idle; the hide keeps going and picks its peek loop back up here.
    answer.loopPeek = idle;
    return answer;
}

} // namespace lens::app::pet
