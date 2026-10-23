#pragma once

/**
 * @file model_catalog.h
 * @brief The models a service is known to carry, and whether changing service moves the model.
 *
 * These are the two rules of docs/adr/0017 that docs/adr/0020 kept, pulled out of ExplanationDuty
 * so a case can state them on their own rather than only across a whole round trip through a faked
 * QNetworkReply and an event loop. What belongs here needs neither the settings document nor the
 * network: the ids a service carries, and the one decision a change of service makes. Reading the
 * served list out of the document and asking the source for a new one stay in the duty, which has
 * the document and the client.
 */

#include <QJsonArray>
#include <QString>
#include <QStringList>

#include <optional>

namespace lens::app {

/**
 * @brief The model ids a service is known to carry.
 * @param served What the shared source listed for this service last time; empty when it never has.
 * @param seeded The catalog's own list for the service, which is a seed and nothing more.
 * @return What the source listed, when there is any of it; otherwise every entry of the seed that
 *         is a usable name.
 *
 * The list belongs to the reader and not to the shipped file: the ids a service carries change
 * under the app, and the catalog is what a machine that has never reached the source starts from.
 */
inline QStringList modelsKnownFor(QStringList const& served, QJsonArray const& seeded)
{
    if (not served.isEmpty())
        return served;
    QStringList ids;
    for (const auto& id : seeded)
        if (id.isString() and not id.toString().isEmpty())
            ids.append(id.toString());
    return ids;
}

/**
 * @brief The model a change of service puts in force, when it puts one there.
 * @param changed Whether the reader is actually changing service. Re-picking the one already in
 *                force is a tap on the dropdown row; a document that has never chosen one counts
 *                as a change, because the first pick is one.
 * @param known   The model ids the service being switched to is known to carry.
 * @param model   The model in force right now.
 * @return The model to move to, or nothing to leave the one in force alone.
 *
 * A model name from another service is the 400 whose message names nothing, and the reader changed
 * the service rather than the model (docs/adr/0017). Two conditions hold it back. A model the
 * service does carry is left exactly as the reader set it, so switching back and forth does not
 * lose a choice that was still valid. And this correction belongs to the reader changing the
 * service and to nothing else: the shared source's list is the whole industry rather than the one
 * service's shelf, so it is no authority on what that service carries (docs/adr/0020).
 */
inline std::optional<QString> modelFollowingProvider(bool changed, QStringList const& known, QString const& model)
{
    if (not changed or known.isEmpty() or known.contains(model))
        return {};
    return known.front();
}

}
