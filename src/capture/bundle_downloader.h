#pragma once

#include <QObject>
#include <QString>

#include <filesystem>

#include "capture/rapidocr_bundle.h"

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

/**
 * @file bundle_downloader.h
 * @brief Fetches the pinned RapidOCR-json archive, checks it, unpacks it, and checks what it
 *        unpacked into (docs/adr/0149).
 *
 * One download at a time. The archive is written as `<name>.part`, checked against its exact size
 * and digest, and renamed once it passes; a failed or cancelled download removes the partial, and
 * a failed extraction keeps the verified archive so the reader can unpack it by hand. The unpacked
 * files are then checked against the catalogue's sizes before anything reports success.
 *
 * The download follows redirects, because GitHub answers release requests with a link to its
 * storage host. The request is a plain GET of a pinned address with no reader data in it
 * (SECURITY.md). Extraction runs the Windows bundle's own tar, which reads the 7z archive.
 *
 * Trust is not this class's business: it only puts files in place. The reader's trust digest is
 * written by the detection flow, as it always is (docs/adr/0132).
 */

namespace lens::capture {

class BundleDownloader : public QObject {
    Q_OBJECT
public:
    explicit BundleDownloader(QObject* parent = nullptr);

    /// @brief Start fetching and unpacking the pinned distribution into its root.
    ///
    /// Ignored while a download or an extraction is running.
    void start();

    /// @brief Stop what is running. `finished(false, "")` follows once the partial file is gone.
    void cancel();

    /// @return Whether a download or an extraction is running now.
    bool busy() const;

signals:
    /// @brief Bytes received so far, and the total the archive needs.
    void progressed(qint64 received, qint64 total);

    /**
     * @brief The distribution is whole and verified (@p ok), or the run stopped.
     * @param failure "" on success and on a cancel; "proxy" when nothing arrived (a mainland-China
     *                proxy is the usual cause); "generic" for any other download failure; "extract"
     *                when the verified archive could not be unpacked or its files did not check out.
     */
    void finished(bool ok, QString failure);

private:
    /// @brief Verify the downloaded archive and move on to the extraction.
    void archiveArrived();
    /// @brief Unpack the archive with the Windows bundle's tar, then check the unpacked files.
    void extract();
    /// @brief Put the run down: the partial file goes, and `finished(false, @p failure)` is reported.
    void fail(QString failure);

    enum class Phase {
        Idle,
        Downloading,
        Extracting,
    };

    QNetworkAccessManager* manager_ = nullptr;
    Phase                  phase_   = Phase::Idle;
    QString                archive_;       ///< The archive's path, once the download has begun.
    qint64                 received_  = 0; ///< Bytes received so far, kept for the proxy verdict.
    bool                   cancelled_ = false;
    QNetworkReply*         reply_     = nullptr;
    QFile*                 file_      = nullptr;
    QProcess*              tar_       = nullptr;
};

}
