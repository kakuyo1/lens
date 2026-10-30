/**
 * @file onboarding_duty.cpp
 * @brief The first-run notice's launch step (docs/adr/0144).
 */

#include "onboarding_duty.h"

#include <QCoreApplication>
#include <QString>

#include "app/notice.h"
#include "core/domain/onboarding.h"
#include "explanation_duty.h"
#include "util/log.h"

namespace lens::app {

OnboardingDuty::OnboardingDuty(StorageDuty& storage, ExplanationDuty& explanation)
    : storage_(storage), explanation_(explanation)
{
}

void OnboardingDuty::startAtLaunch(bool forced)
{
    auto       onboarding = storage_.section("ONBOARDING");
    auto const hasKey     = not storage_.section().value<std::string>("API-KEY", {}).empty();
    auto const plan       = core::onboarding::launchPlan(onboarding.value<bool>("done", false), hasKey, forced);
    if (plan.markDone)
        onboarding.set<bool>("done", true);
    if (not plan.raiseNotice)
        return;

    explanation_.showNotice(QCoreApplication::translate("lens::app::OnboardingDuty", "Lens is running in the tray"),
                            QCoreApplication::translate("lens::app::OnboardingDuty", "Select English text to see its explanation beside it. Lookups need an API key, so add one first. The icon is at the bottom right of the taskbar; if you cannot see it, click ^."),
                            QString::fromLatin1(kNoticeInfo),
                            QCoreApplication::translate("lens::app::OnboardingDuty", "Add API key"),
                            {},
                            QString::fromLatin1(kNoticeIntentApiKey));
    LENS_INFO("first-run notice raised (forced: {})", forced);
}

}
