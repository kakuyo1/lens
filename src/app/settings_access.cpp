/**
 * @file settings_access.cpp
 * @brief One row of the settings table, read and written, for settings_access.h.
 */

#include "settings_access.h"

#include <QFontDatabase>
#include <QRegularExpression>

#include <algorithm>
#include <string>

#include "util/log.h"

namespace lens::app::settings {
namespace {

using core::settings::Kind;
using core::settings::Row;

/// @return The text a row stores for @p value; QML may hand a number where the row keeps a string.
std::string textOf(QVariant const& value)
{
    return value.toString().toStdString();
}

/// @return Whether @p text is #rrggbb, the only shape the colour setting stores.
bool isHexColor(QString const& text)
{
    static const QRegularExpression pattern{QStringLiteral("^#[0-9a-fA-F]{6}$")};
    return pattern.match(text).hasMatch();
}

/// @return @p row's default as text, which is what its Colour, Font, Text and Choice rows store.
QString defaultTextOf(Row const& row)
{
    return QString::fromStdString(std::string{row.fallback.text});
}

} // namespace

QString keyOf(Row const& row)
{
    return QString::fromLatin1(row.key.data(), qsizetype(row.key.size()));
}

QVariant read(core::SettingsSection section, Row const& row)
{
    switch (row.kind) {
        case Kind::Toggle:
            return section.value<bool>(row.key, row.fallback.toggle);
        case Kind::Integer:
            // Clamped rather than fallen back: a hand-edited document reads as the nearer edge, the
            // same value a writer would have stored, so the two sides never disagree.
            return std::clamp(section.value<int>(row.key, row.fallback.number), row.low, row.high);
        case Kind::Choice: {
            const QString stored = QString::fromStdString(section.value<std::string>(row.key, std::string{row.fallback.text}));
            return core::settings::accepts(row, stored.toStdString()) ? stored : defaultTextOf(row);
        }
        case Kind::Color: {
            const QString stored = QString::fromStdString(section.value<std::string>(row.key, std::string{row.fallback.text}));
            return isHexColor(stored) ? stored : defaultTextOf(row);
        }
        case Kind::Text:
        case Kind::Font:
            return QString::fromStdString(section.value<std::string>(row.key, std::string{row.fallback.text}));
    }
    return {};
}

bool write(core::SettingsSection section, Row const& row, QVariant const& value)
{
    switch (row.kind) {
        case Kind::Toggle:
            section.set<bool>(row.key, value.toBool());
            return true;
        case Kind::Integer:
            section.set<int>(row.key, std::clamp(value.toInt(), row.low, row.high));
            return true;
        case Kind::Choice: {
            const QString chosen = value.toString();
            if (not core::settings::accepts(row, chosen.toStdString())) {
                LENS_WARN("'{}' does not take '{}'; leaving the setting alone", std::string{row.key}, chosen.toStdString());
                return false;
            }
            section.set<std::string>(row.key, chosen.toStdString());
            return true;
        }
        case Kind::Color: {
            const QString chosen = value.toString();
            if (not isHexColor(chosen)) {
                LENS_WARN("unknown colour '{}' for '{}'; leaving the setting alone", chosen.toStdString(), std::string{row.key});
                return false;
            }
            section.set<std::string>(row.key, chosen.toLower().toStdString());
            return true;
        }
        case Kind::Font: {
            // An empty family is the reader going back to the face the application installed, and it
            // asks the font database nothing: the unit suite runs without a GUI application.
            const QString chosen = value.toString().trimmed();
            if (not chosen.isEmpty() and not QFontDatabase::hasFamily(chosen)) {
                LENS_WARN("unknown font family '{}'; leaving the setting alone", chosen.toStdString());
                return false;
            }
            section.set<std::string>(row.key, chosen.toStdString());
            return true;
        }
        case Kind::Text:
            section.set<std::string>(row.key, textOf(value));
            return true;
    }
    return false;
}

QVariant defaultValue(Row const& row)
{
    switch (row.kind) {
        case Kind::Toggle:
            return row.fallback.toggle;
        case Kind::Integer:
            return row.fallback.number;
        case Kind::Choice:
        case Kind::Text:
        case Kind::Color:
        case Kind::Font:
            return defaultTextOf(row);
    }
    return {};
}

QVariantMap rangeOf(Row const& row)
{
    if (row.kind != Kind::Integer)
        return {};
    return {{QStringLiteral("low"), row.low}, {QStringLiteral("high"), row.high}};
}

} // namespace lens::app::settings
