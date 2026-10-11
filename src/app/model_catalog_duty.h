#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include "llm/llm_client.h"
#include "storage_duty.h"

/**
 * @file model_catalog_duty.h
 * @brief Which service Lens asks, which model it asks for, and the list the model field offers.
 *
 * This is the provider and model-list half of what ExplanationDuty used to be: the rules of
 * docs/adr/0017, 0018 and 0020, and nothing about explanations. The two share the document and the
 * client and no state at all, so what each is for only ever met in the interface they were behind.
 */

namespace lens::app {

class ModelCatalogDuty final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    QML_UNCREATABLE("Owned by Controller; reached as Controller.modelCatalogDuty")
public:
    /// @brief Bind the catalogue to the document that stores the reader's choice and to the client
    ///        that choice is applied to.
    ModelCatalogDuty(StorageDuty& storage, llm::LlmClient& llm, QObject* parent = nullptr);

    /// @brief Ask the shared model source for the whole catalogue.
    Q_INVOKABLE void refreshModels();

    /// @return The provider in force, from the document, or the catalog's default when none is
    ///         stored. Public because the facade reads the same key to restore the wire options,
    ///         and one definition of "the default provider" is better than three.
    QString currentProvider() const;

    /**
     * @brief The models the provider in force carries, as {value,label,group,note}.
     *
     * Read off a cache rebuilt where the answer can change rather than built on the read: this is
     * the settings page's most-read list and its longest by far -- a service that resells other
     * people's models answers with several hundred ids -- and the map per id that `settings()`
     * used to carry was rebuilt for every one of the settings page's bindings, on every change.
     */
    QVariantList models() const;

    /// @brief The catalogue's half of the settings page: the explanation languages, the services,
    ///        the model in force and what that model costs.
    QVariantMap settings() const;

    /// @brief Point the client at a catalog service.
    ///
    /// The model follows the service (docs/adr/0017): a model name from another one is a 400 whose
    /// message names nothing, and the service is the only thing the reader meant to choose. The
    /// list fetched last time wins over the catalog's, and asking for a fresh one happens here too
    /// -- it costs nothing. @return False for an unknown provider.
    Q_INVOKABLE bool setProvider(QString const& provider);

    /// @brief Put a model in force, whatever the service in hand lists.
    /// @return False for a name that is nothing but whitespace, which is not a model.
    /// @note The model list does not correct this: the field is typeable on purpose, so a private
    ///       deployment or a name the source has never listed can be asked for by hand
    ///       (docs/adr/0017).
    bool setModel(QString const& model);

    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)

signals:
    /// @brief Something the settings page reads has changed: the service or the model.
    void settingsChanged();
    /// @brief The provider in force carries a different list of models than it did.
    ///
    /// Its own signal because it is a separate property: the list is the one thing here big enough
    /// that rebuilding it for every reader of settings() is what made a provider change feel slow.
    void modelsChanged();

private:
    /// @return The models known for @p provider: what the source listed for it last time, else the
    ///         catalog's own seed, else empty -- a custom endpoint names no models at all.
    QStringList modelsFor(QString const& provider) const;
    /// @brief Split one shared answer into the per-provider caches the catalog names.
    ///
    /// @p models is the whole catalogue; each provider takes the ids its `openRouterPrefix` names.
    /// A fetched list never rewrites the model in force (docs/adr/0020): it is a list of the whole
    /// industry, not of the one service standing, so it is not the authority on what that service
    /// carries.
    void noteModelsFetched(QStringList models);
    /// @brief Rebuild the cached model list from the provider in force, and say so if it moved.
    void rebuildModels();

    StorageDuty& storage_;
    llm::LlmClient& llm_;
    /// The provider in force's models, in the shape the settings page draws.
    QVariantList models_;
};

}
