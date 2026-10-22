#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

#include "core/document/settings_section.h"
#include "core/domain/settings_table.h"

/**
 * @file settings_access.h
 * @brief Reading and writing one row of the settings table: the only place a key's rule is applied.
 *
 * The projection reads through read(), every write goes through write(), and restore-defaults writes
 * defaultValue(). A row's clamp, its members, its colour and its family rule therefore hold on both
 * sides of the document, which is what stops a reader from seeing a value a writer would refuse.
 */

namespace lens::app::settings {

/// @return The row's key, as QML reads it: the projection's own spelling of it.
QString keyOf(core::settings::Row const& row);

/**
 * @brief Read one row out of @p section.
 *
 * @param section The document, or the object the row's key lives in.
 * @param row     The row to read.
 * @return The stored value brought into the row's rule, or the row's default when nothing is stored.
 */
QVariant read(core::SettingsSection section, core::settings::Row const& row);

/**
 * @brief Store @p value through @p row's rule, and save.
 *
 * @param section The document, or the object the row's key lives in.
 * @param row     The row to write.
 * @param value   The value the reader picked; a number, a switch or a string.
 * @return False when the row refuses the value outright -- a colour that is not #rrggbb, a family
 *         this machine does not have, a member a Choice does not list -- and nothing was written.
 */
bool write(core::SettingsSection section, core::settings::Row const& row, QVariant const& value);

/// @return The value restore-defaults writes for @p row.
QVariant defaultValue(core::settings::Row const& row);

/// @return The closed range a row's slider runs over, as {low, high}; empty for a row with none.
QVariantMap rangeOf(core::settings::Row const& row);

} // namespace lens::app::settings
