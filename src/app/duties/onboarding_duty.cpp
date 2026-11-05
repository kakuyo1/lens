/**
 * @file onboarding_duty.cpp
 * @brief The first-run wizard's launch step (docs/adr/0144, docs/adr/0148).
 */

#include "onboarding_duty.h"

#include "core/domain/onboarding.h"
#include "util/log.h"

namespace lens::app {

OnboardingDuty::OnboardingDuty(StorageDuty& storage, QObject* parent)
    : QObject(parent), storage_(storage)
{
}

void OnboardingDuty::startAtLaunch(bool forced)
{
    forced_               = forced;
    auto       onboarding = storage_.section("ONBOARDING");
    auto const hasKey     = not storage_.section().value<std::string>("API-KEY", {}).empty();
    auto const plan       = core::onboarding::launchPlan(onboarding.value<bool>("done", false), hasKey, forced);
    if (plan.markDone)
        onboarding.set<bool>("done", true);
    if (not plan.raiseWizard)
        return;

    emit wizardRequested();
    LENS_INFO("first-run wizard raised (forced: {})", forced);
}

void OnboardingDuty::finish()
{
    if (forced_)
        return;
    storage_.section("ONBOARDING").set<bool>("done", true);
    LENS_INFO("first-run guidance recorded");
}

bool OnboardingDuty::isDone() const
{
    return storage_.section("ONBOARDING").value<bool>("done", false);
}

}
