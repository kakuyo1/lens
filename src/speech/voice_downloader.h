#pragma once

#include <QObject>
#include <QString>

#include <filesystem>

#include "speech/voice_catalog.h"

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

/**
 * @file voice_downloader.h
 * @brief Fetches one local voice's files from its pinned repository, checks each one, and only then
 *        puts it in place.
 *
 * One voice at a time, one file at a time. Each file is written as `<name>.part`, checked against its
 * exact size and digest, and renamed to its own name once it passes. So a voice folder holds only
 * whole, verified files, and a run that stops half-way leaves `.part` files that nothing loads
 * (docs/adr/0135). A failed or cancelled download removes its own partial file; one killed by the
 * process ending leaves it, and the next start overwrites it.
 *
 * Redirects are followed, because the repository answers with a link to its storage host. The
 * request is a plain GET of a fixed address with no reader data in it (SECURITY.md).
 */

namespace lens::speech {

class VoiceDownloader : public QObject {
    Q_OBJECT
public:
    /// @param parent Owns the downloader and the network manager it makes.
    explicit VoiceDownloader(QObject* parent = nullptr);

    /**
     * @brief Start fetching @p model into @p root, under the voice's own folder.
     *
     * Ignored while another voice is downloading: one request in flight at a time keeps the transfer
     * from competing with the reader's own traffic.
     */
    void start(VoiceModel model, std::filesystem::path const& root);

    /// @brief Stop the transfer in flight. `finished(false)` follows once the partial file is gone.
    void cancel();

    /// @return Whether a voice is being fetched now.
    bool busy() const;

    /// @return The voice being fetched, or the last one fetched.
    VoiceModel model() const;

signals:
    /// @brief Bytes received so far across the voice's files, and the total it needs.
    void progressed(qint64 received, qint64 total);

    /// @brief The voice is whole and verified (@p ok), or the download stopped and left nothing behind.
    void finished(bool ok);

private:
    /// @brief Begin the next file, or report success once every file is in place.
    void fetchNext();
    /// @brief Put the transfer down: the partial file goes, and `finished(false)` is reported.
    void fail();

    QNetworkAccessManager* manager_;
    VoiceModel             model_ = VoiceModel::Kokoro;
    QString                folder_;
    std::size_t            index_    = 0;
    qint64                 received_ = 0; ///< Bytes of the files already in place.
    qint64                 total_    = 0;
    bool                   active_   = false;
    QNetworkReply*         reply_    = nullptr;
    QFile*                 file_     = nullptr;
};

}
