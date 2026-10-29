/**
 * @file model_catalog_duty.cpp
 * @brief Which service Lens asks, which model it asks for, and the list the model field offers.
 */

#include "model_catalog_duty.h"

#include <QCoreApplication>
#include <QJsonArray>

#include <string>
#include <vector>

#include "llm/llm_protocol.h"
#include "llm/llm_pure.h"
#include "model_catalog.h"
#include "util/log.h"

namespace lens::app {

ModelCatalogDuty::ModelCatalogDuty(StorageDuty& storage, llm::LlmClient& llm, QObject* parent)
    : QObject(parent), storage_(storage), llm_(llm)
{
    connect(&llm_, &llm::LlmClient::modelsFetched, this, &ModelCatalogDuty::noteModelsFetched);
    connect(&llm_, &llm::LlmClient::serviceModelsFetched, this, &ModelCatalogDuty::noteServiceModels);
}

bool ModelCatalogDuty::setProvider(QString const& provider)
{
    auto const entry = llm::serviceProvider(provider);
    if (entry.isEmpty() || not llm_.setProvider(provider)) return false;
    // Whether the reader is actually changing service, read before the document below is written:
    // that is the whole condition the correction is for. Re-picking the provider already in force is
    // a tap on the dropdown row, not a change, and with a shared model list that tap would replace a
    // valid model with whichever id the source happens to list first (docs/adr/0020). A document
    // that has never chosen one counts as a change -- the first pick is one. modelsKnownFor() and
    // modelFollowingProvider() hold the rest of the rule, in model_catalog.h.
    const bool changed = storage_.section().value<std::string>("PROVIDER", {}) != provider.toStdString();
    auto       keys    = storage_.section();
    keys.set<std::string>("PROVIDER", provider.toStdString());
    keys.set<std::string>("URL", llm_.baseUrl().toString().toStdString());

    if (const auto follows = modelFollowingProvider(changed, modelsFor(provider), llm_.model())) {
        LENS_INFO("model follows the provider: '{}' -> '{}'", llm_.model().toStdString(), follows->toStdString());
        llm_.setModel(*follows);
        keys.set<std::string>("MODEL", follows->toStdString());
    }
    rebuildModels();
    emit settingsChanged();
    llm_.fetchModels();
    return true;
}

QStringList ModelCatalogDuty::modelsFor(QString const& provider) const
{
    // What the source listed for this provider last time wins; the catalog's list is a seed for a
    // machine that has never reached it.
    auto const  served = storage_.section("MODELS").value<std::vector<std::string>>(provider.toStdString(), {});
    QStringList listed;
    listed.reserve(static_cast<qsizetype>(served.size()));
    for (const std::string& id : served)
        listed.append(QString::fromStdString(id));
    return modelsKnownFor(listed, llm::serviceProvider(provider).value("models").toArray());
}

QString ModelCatalogDuty::currentProvider() const
{
    return QString::fromStdString(storage_.section().value<std::string>("PROVIDER", llm::serviceCatalog().value("defaultProvider").toString().toStdString()));
}

void ModelCatalogDuty::rebuildModels()
{
    QVariantList rebuilt;
    for (const QString& id : modelsFor(currentProvider()))
        rebuilt.append(QVariantMap{{"value", id}, {"label", id}, {"group", ""}, {"note", ""}});
    if (rebuilt == models_) return;
    models_ = std::move(rebuilt);
    emit modelsChanged();
}

void ModelCatalogDuty::refreshModels()
{
    rebuildModels();
    llm_.fetchModels();
}

void ModelCatalogDuty::noteModelsFetched(QStringList models)
{
    if (models.isEmpty()) return;
    // One answer, several caches: each provider takes the ids its own vendor prefix names, and a
    // provider with no prefix gets no entry at all.
    auto written = storage_.section("MODELS");
    int  filled  = 0;
    int  total   = 0;
    for (const auto& value : llm::serviceCatalog().value("providers").toArray()) {
        const auto provider = value.toObject();
        if (not provider.contains("openRouterPrefix")) continue;
        const QStringList ids = llm::idsForPrefix(models, provider.value("openRouterPrefix").toString());
        if (ids.isEmpty()) continue;
        ++filled;
        total += ids.size();
        std::vector<std::string> served;
        served.reserve(static_cast<std::size_t>(ids.size()));
        for (const QString& id : ids)
            served.push_back(id.toStdString());
        written.set<std::vector<std::string>>(provider.value("value").toString().toStdString(), served);
    }
    if (filled == 0) return;
    LENS_INFO("the model source lists {} model(s) across {} provider(s)", total, filled);

    // The model in force is not touched here. This answer describes the whole industry, not the
    // one service standing, so "not in the list" says nothing about what that service carries --
    // the correction belongs to the reader changing the service (docs/adr/0020). Nothing to save
    // either: every write above persisted itself.
    rebuildModels();
}

void ModelCatalogDuty::noteServiceModels(QStringList models)
{
    if (models.isEmpty()) return;
    std::vector<std::string> served;
    served.reserve(static_cast<std::size_t>(models.size()));
    for (const QString& id : models)
        served.push_back(id.toStdString());
    storage_.section("MODELS").set<std::vector<std::string>>(currentProvider().toStdString(), served);
    LENS_INFO("the standing service lists {} model(s)", served.size());

    // As with the catalogue, the model in force is left alone: the reader chose it, and a list is
    // not a reason to answer them with another one.
    rebuildModels();
}

QVariantMap ModelCatalogDuty::settings() const
{
    auto const& catalog = llm::serviceCatalog();
    auto const  model   = llm_.model();
    auto const  pricing = catalog.value("pricing").toObject();
    auto const  price   = pricing.value("models").toObject().value(model).toObject();
    // Spelled in the surface's context, where the four strings below already are: this map is what
    // the settings popup draws, and lupdate reads literal arguments only.
    auto const   priceText = price.isEmpty() ? QCoreApplication::translate("SettingsPopup", "No listed price; recorded cost is zero.")
                                             : QCoreApplication::translate("SettingsPopup", "Input %1 / Output %2 %3 per %4 tokens").arg(price.value("input").toDouble()).arg(price.value("output").toDouble()).arg(pricing.value("currency").toString()).arg(pricing.value("unit").toInteger());
    QVariantList providers;
    for (auto const& value : catalog.value("providers").toArray()) {
        auto       option = value.toObject().toVariantMap();
        auto const group  = option.value("group").toString();
        option["group"]   = group == QLatin1String("Domestic")        ? QCoreApplication::translate("SettingsPopup", "Domestic")
                            : group == QLatin1String("International") ? QCoreApplication::translate("SettingsPopup", "International")
                                                                      : QCoreApplication::translate("SettingsPopup", "Other");
        if (option.value("value").toString() == QLatin1String("custom")) option["label"] = QCoreApplication::translate("SettingsPopup", "Custom service");
        providers.append(option);
    }
    // The model list is deliberately not in here. It is the one long thing the settings page
    // reads -- hundreds of ids from a service that resells models -- and this map is rebuilt for
    // each of the page's ~40 bindings on every change, so carrying it meant rebuilding hundreds of
    // maps dozens of times per change. It has its own property and its own cache (models()).
    return {{"languages", catalog.value("languages").toArray().toVariantList()},
            {"providers", providers},
            {"model", model},
            {"modelPrice", priceText}};
}

QVariantList ModelCatalogDuty::models() const
{
    return models_;
}

bool ModelCatalogDuty::setModel(QString const& model)
{
    const auto value = model.trimmed();
    if (value.isEmpty()) return false;
    llm_.setModel(value);
    storage_.section().set<std::string>("MODEL", value.toStdString());
    return true;
}

}
