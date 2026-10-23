#pragma once

#include <QString>

#include "llm/llm_protocol.h"

/**
 * @file settings_defaults.h
 * @brief The two defaults the shared service catalogue names, and the address that comes with them.
 *
 * Every value a settings document falls back to lives in one row of core/domain/settings_table.h.
 * These three are the exception: they are written down nowhere in the application. The provider a
 * fresh install asks is the catalogue's `defaultProvider`, and the model and endpoint are whatever
 * that provider's seed lists. Reading them from the catalogue is what keeps a new install and
 * restore-defaults from naming a service the catalogue has since moved away from.
 */

namespace lens::app::defaults {

/// @return The provider a document that names none asks, from the shared catalogue.
inline QString defaultProviderName()
{
    return llm::serviceCatalog().value("defaultProvider").toString();
}

/// @return The first model the default provider's seed lists; empty when it has no seed.
inline QString defaultModelName()
{
    const auto models = llm::serviceProvider(defaultProviderName()).value("models").toArray();
    return models.isEmpty() ? QString{} : models.first().toString();
}

/// @return The default provider's endpoint, as the shared catalogue names it.
inline QString defaultBaseUrl()
{
    return llm::serviceProvider(defaultProviderName()).value("baseUrl").toString();
}

}
