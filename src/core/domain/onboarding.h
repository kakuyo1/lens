#pragma once

/**
 * @file onboarding.h
 * @brief What one launch does about the first-run guidance (docs/adr/0144, docs/adr/0148).
 *
 * A reader who has never been guided is told once, as the three-step wizard. A reader whose
 * document already holds an API key configured Lens before guidance existed and is not told at
 * all. The wizard's own finish -- completed or skipped -- writes the record, so a launch that
 * raises the wizard records nothing yet; a launch that skips the raise records it at once.
 */

namespace lens::core::onboarding {

/// @brief What one launch does: whether the wizard goes up, and whether the document is told so.
struct LaunchPlan {
    bool raiseWizard = false; ///< Put the first-run wizard up.
    bool markDone    = false; ///< Record the guidance as given, so the next launch does not repeat it.
};

/**
 * @brief The plan for one launch.
 * @param done      The document already records the guidance as given.
 * @param hasApiKey The document holds an API key.
 * @param forced    The build raises the wizard on every launch. A forced launch records nothing, so
 *                  switching the build back to the normal rule brings the first-run behaviour back.
 */
inline LaunchPlan launchPlan(bool done, bool hasApiKey, bool forced)
{
    if (forced)
        return LaunchPlan{.raiseWizard = true, .markDone = false};
    if (done)
        return LaunchPlan{};
    if (hasApiKey)
        return LaunchPlan{.markDone = true};
    return LaunchPlan{.raiseWizard = true, .markDone = false};
}

}
