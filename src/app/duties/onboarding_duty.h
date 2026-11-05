#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include "storage_duty.h"

namespace lens::app {

/**
 * @brief Raises the first-run wizard at launch and records that the guidance was given (docs/adr/0144, docs/adr/0148).
 *
 * The rule is core/domain/onboarding.h. This duty is the one place that reads the document for it,
 * puts the wizard up through wizardRequested(), and writes the record when the wizard reports back
 * -- finished or skipped; the record is the wizard's to give, not the launch's.
 */
class OnboardingDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.onboardingDuty")
public:
    /**
     * @param storage The document owner; `ONBOARDING` is this duty's section of it.
     * @param parent  Qt object owner.
     */
    explicit OnboardingDuty(StorageDuty& storage, QObject* parent = nullptr);

    /**
     * @brief Apply the launch plan: record what it says, and put the wizard up when it says so.
     * @param forced The build raises the wizard on every launch (LENS_FORCE_ONBOARDING), and records nothing.
     */
    void startAtLaunch(bool forced);

    /// @brief Record the guidance as given, whether the reader finished the wizard or skipped it.
    ///
    /// A forced launch records nothing, so switching the build back to the normal rule brings the
    /// first-run behaviour back.
    Q_INVOKABLE void finish();

    /// @return Whether the document records the guidance as given.
    Q_INVOKABLE bool isDone() const;

signals:
    /// @brief The wizard goes up; Main.qml shows the surface.
    void wizardRequested();

private:
    StorageDuty& storage_;
    bool         forced_ = false;
};

}
