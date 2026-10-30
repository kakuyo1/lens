#pragma once

/**
 * @file onboarding_duty.h
 * @brief Raises the first-run notice at launch and records that it was raised (docs/adr/0144).
 *
 * The rule is core/domain/onboarding.h. This is the one place that reads the document for it, writes its
 * flag, and puts the notice up through the explanation duty's notice slot, the same slot a failed request uses.
 */

#include "storage_duty.h"

namespace lens::app {

class ExplanationDuty;

class OnboardingDuty final {
public:
    /**
     * @param storage     The document owner; `ONBOARDING` is this duty's section of it.
     * @param explanation The notice slot the reason card is raised through.
     */
    OnboardingDuty(StorageDuty& storage, ExplanationDuty& explanation);

    /**
     * @brief Apply the launch plan: record the guidance as raised, and put the notice up when the plan says so.
     * @param forced The build raises the notice on every launch (LENS_FORCE_ONBOARDING), and records nothing.
     */
    void startAtLaunch(bool forced);

private:
    StorageDuty&     storage_;
    ExplanationDuty& explanation_;
};

}
