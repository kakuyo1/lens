/**
 * @file hover_sense.cpp
 * @brief Implementation of HoverSense: two rings, two held moments, and one cooldown.
 */

#include "core/domain/pet/hover_sense.h"

#include <algorithm>

namespace lens::core::pet {

HoverSense::HoverSense(HoverTiming timing_)
    : timing_{timing_}
{
}

void HoverSense::reset()
{
    now_          = std::chrono::milliseconds{0};
    zone_         = HoverZone::Outside;
    outsideSince_ = std::chrono::milliseconds{0};
    nearSince_.reset();
    lastNoticeAt_.reset();
    approachNoticed_ = false;
    approachDwelled_ = false;
    approachQuiet_   = false;
}

bool HoverSense::cooling() const
{
    return lastNoticeAt_ and now_ - *lastNoticeAt_ < timing_.cooldown;
}

HoverUpdate HoverSense::update(std::chrono::milliseconds step, double distance, bool idle)
{
    now_ += std::max(step, std::chrono::milliseconds{0});
    auto const previous = zone_;
    auto const next     = zoneAt(distance, previous);

    if (next == HoverZone::Outside) {
        if (previous != HoverZone::Outside) outsideSince_ = now_;
        nearSince_.reset();
        zone_ = next;
        if (now_ - outsideSince_ >= kRearm) clearApproach();
        return {next, false, false};
    }

    zone_ = next;
    if (next == HoverZone::Near) {
        if (not nearSince_) nearSince_ = now_;
    } else {
        nearSince_.reset();
    }
    if (not nearSince_) return {next, false, false};

    auto const greetedAt = *nearSince_ + timing_.noticeDelay;
    auto const dwellAt   = *nearSince_ + timing_.dwellDelay;

    if (idle and not approachNoticed_ and now_ >= greetedAt) {
        if (not cooling()) {
            lastNoticeAt_    = now_;
            approachNoticed_ = true;
            return {next, true, false};
        }
        // The pet has noticed, but it already greeted recently: it looks, and does not perform.
        approachNoticed_ = true;
        approachQuiet_   = true;
    }

    if (idle and approachNoticed_ and not approachDwelled_ and not approachQuiet_ and now_ >= dwellAt) {
        approachDwelled_ = true;
        return {next, false, true};
    }

    return {next, false, false};
}

HoverZone HoverSense::zoneAt(double distance, HoverZone previous) const
{
    auto const held = [distance](double threshold, bool inside) {
        return distance <= threshold + (inside ? kExitMargin : 0.0);
    };
    if (held(kNearRadius, previous == HoverZone::Near)) return HoverZone::Near;
    if (held(kGazeRadius, previous != HoverZone::Outside)) return HoverZone::Gaze;
    return HoverZone::Outside;
}

void HoverSense::clearApproach()
{
    approachNoticed_ = false;
    approachDwelled_ = false;
    approachQuiet_   = false;
}

} // namespace lens::core::pet
