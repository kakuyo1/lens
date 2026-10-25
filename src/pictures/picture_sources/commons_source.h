#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include "pictures/picture_source.h"

class QNetworkAccessManager;

/**
 * @file commons_source.h
 * @brief The Wikimedia Commons source: one search, and the thumbnails it named.
 *
 * Commons is the only source this version wires: its images carry a machine-readable licence and
 * author, it has no API key, and it serves a 480 px thumbnail for each file. It has no SLA and no
 * safe-search, two known limits.
 */

namespace lens::pictures {

class CommonsSource final : public QObject, public PictureSource {
    Q_OBJECT
public:
    /**
     * @brief Build the source, optionally over a caller-supplied network manager.
     *
     * The manager is the seam a test answers through, the same one LlmClient and UpdateClient take.
     * Null makes one owned by this object, which is what the application passes: the default
     * manager's proxy behaviour is the one this lookup wants.
     *
     * @param manager Network manager to ask through, or null to make one.
     */
    explicit CommonsSource(QNetworkAccessManager* manager = nullptr, QObject* parent = nullptr);

    void search(QString const& query, PictureSink sink) override;
    void fetch(QUrl const& url, ByteSink sink) override;

    /// @return The API address one query is searched with, the reader's text percent-encoded.
    static QUrl searchUrl(QString const& query);

private:
    QNetworkAccessManager* manager_;
    QString                userAgent_;
};

}