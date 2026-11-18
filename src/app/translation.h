#pragma once

#include <QString>
#include <QTranslator>

#include <memory>

/**
 * @file translation.h
 * @brief The interface translation: load one built-in .qm and keep exactly one installed.
 */

namespace lens::app {

/// @return A translator for one of the .qm files built into the binary, or a null one.
std::unique_ptr<QTranslator> loadTranslation(const QString& language);

/// @brief Keep exactly one interface translation installed.
class TranslationKeeper {
public:
    void select(const QString& language);

private:
    std::unique_ptr<QTranslator> translator_;
    QString                      current_;
};

} // namespace lens::app