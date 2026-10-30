#pragma once

/**
 * @file onboarding.h
 * @brief What one launch does about the first-run notice (docs/adr/0144).
 *
 * A reader who has never been told that Lens lives in the tray is told once. A reader whose document
 * already holds an API key configured Lens before the notice existed and is not told at all; the
 * document records the guidance as raised either way, so a later launch does not repeat it.
 */

namespace lens::core::onboarding {

/// @brief What one launch does: whether the notice goes up, and whether the document is told so.
struct LaunchPlan {
    bool raiseNotice = false; ///< Put the notice up.
    bool markDone    = false; ///< Record that the guidance was raised, so the next launch does not repeat it.
};

/**
 * @brief The plan for one launch.
 * @param done      The document already records the guidance as raised.
 * @param hasApiKey The document holds an API key.
 * @param forced    The build raises the notice on every launch. A forced launch records nothing, so
 *                  switching the build back to the normal rule brings the first-run behaviour back.
 */
inline LaunchPlan launchPlan(bool done, bool hasApiKey, bool forced)
{
    if (forced)
        return LaunchPlan{.raiseNotice = true, .markDone = false};
    if (done)
        return LaunchPlan{};
    return LaunchPlan{.raiseNotice = not hasApiKey, .markDone = true};
}

}
