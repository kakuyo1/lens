/**
 * @file storage_duty.cpp
 * @brief The single-document storage boundary used by the application duties.
 */

#include "storage_duty.h"

#include <QCoreApplication>

namespace lens::app {

StorageDuty::StorageDuty(core::KnownStore& store)
    : store_(store), stats_(store.document()), review_(store.document())
{
    // Two settings phase 1 carried and nothing reads any more. Taken out through the section like
    // every other write, so the boundary stays the only way into the document; remove() saves by
    // itself, and only when there was something to take away.
    auto keys = section();
    keys.remove("dragSensitivity");
    keys.remove("popupFrequency");
}

core::KnownStore& StorageDuty::knownStore()
{
    return store_;
}

const core::KnownStore& StorageDuty::knownStore() const
{
    return store_;
}

core::StatsStore& StorageDuty::statsStore()
{
    return stats_;
}

const core::StatsStore& StorageDuty::statsStore() const
{
    return stats_;
}

core::ReviewStore& StorageDuty::reviewStore()
{
    return review_;
}

const core::ReviewStore& StorageDuty::reviewStore() const
{
    return review_;
}

core::SettingsSection StorageDuty::section(std::string_view const name)
{
    return store_.section(name);
}

void StorageDuty::save() const
{
    store_.save();
}

bool StorageDuty::removeWord(QString const& lemma)
{
    const auto value = lemma.trimmed().toStdString();
    if (value.empty())
        return false;
    const bool marksRemoved   = store_.removeLemma(value);
    const bool historyRemoved = stats_.removeLemma(value);
    const bool reviewRemoved  = review_.removeLemma(value);
    const bool removed        = marksRemoved || historyRemoved || reviewRemoved;
    if (removed) {
        // A word can live only in the stats history and have no known mark of its own; the mark's
        // tombstone would then never be written, and sync would bring the word back. So a removal
        // always leaves a tombstone behind.
        store_.markDeleted(value);
        store_.save();
    }
    return removed;
}

QVariantList StorageDuty::levelOptions()
{
    const struct {
        const char* label;
        const char* group;
        const char* note;
    } levels[] = {
        {"B1–B2", QT_TRANSLATE_NOOP("lens::app::AppController", "CEFR"), ""},
        {"C1–C2", QT_TRANSLATE_NOOP("lens::app::AppController", "CEFR"), ""},
        {"CET-4", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), "Minimum requirement"},
        {"CET-6", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), ""},
        {"TEM-4", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), ""},
        {"TEM-8", QT_TRANSLATE_NOOP("lens::app::AppController", "National exams"), ""},
        {"IELTS", QT_TRANSLATE_NOOP("lens::app::AppController", "Study-abroad exams"), ""},
        {"TOEFL", QT_TRANSLATE_NOOP("lens::app::AppController", "Study-abroad exams"), ""},
    };

    QVariantList options;
    int          value = 0;
    for (const auto& level : levels) {
        options.append(QVariantMap{{"value", value++},
                                   {"label", QString::fromUtf8(level.label)},
                                   {"group", QCoreApplication::translate("lens::app::AppController", level.group)},
                                   {"note", QString::fromUtf8(level.note)}});
    }
    return options;
}

QVariantList StorageDuty::reviewFrequencyOptions()
{
    return QVariantList{
        QVariantMap{{"value", static_cast<int>(core::ReviewFrequency::Off)},
                    {"label", QCoreApplication::translate("lens::app::AppController", "Off")}},
        QVariantMap{{"value", static_cast<int>(core::ReviewFrequency::Every15To30Min)},
                    {"label", QCoreApplication::translate("lens::app::AppController", "Every 15–30 minutes")}},
        QVariantMap{{"value", static_cast<int>(core::ReviewFrequency::Every30To60Min)},
                    {"label", QCoreApplication::translate("lens::app::AppController", "Every 30–60 minutes")}},
        QVariantMap{{"value", static_cast<int>(core::ReviewFrequency::Every1To2Hours)},
                    {"label", QCoreApplication::translate("lens::app::AppController", "Every 1–2 hours")}},
    };
}

}
