/**
 * @file voice_downloader.cpp
 * @brief The file-at-a-time fetch behind voice_downloader.h.
 */

#include "speech/voice_downloader.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "util/log.h"

namespace lens::speech {
namespace {

/// An hour for one file. The largest is 114 MB; a slow link should not be cut off partway through.
constexpr int kTransferTimeoutMs = 60 * 60 * 1000;

} // namespace

VoiceDownloader::VoiceDownloader(QObject* parent)
    : QObject(parent),
      manager_(new QNetworkAccessManager(this))
{
}

void VoiceDownloader::start(VoiceModel model, std::filesystem::path const& root)
{
    if (active_)
        return;
    VoiceSpec const& spec = voiceSpec(model);
    model_                = model;
    folder_               = QString::fromStdWString((root / spec.id.toStdWString()).wstring());
    index_                = 0;
    received_             = 0;
    total_                = 0;
    for (VoiceFile const& file : spec.files)
        total_ += file.size;

    std::error_code error;
    std::filesystem::create_directories(folder_.toStdWString(), error);
    if (error) {
        LENS_WARN("voice '{}': cannot create its folder", spec.id.toStdString());
        emit finished(false);
        return;
    }
    active_ = true;
    fetchNext();
}

void VoiceDownloader::cancel()
{
    // The finished handler is what reports the stop, so the partial file goes through the same path
    // as any other failure.
    if (reply_ != nullptr)
        reply_->abort();
}

bool VoiceDownloader::busy() const
{
    return active_;
}

VoiceModel VoiceDownloader::model() const
{
    return model_;
}

void VoiceDownloader::fetchNext()
{
    VoiceSpec const& spec = voiceSpec(model_);
    if (index_ == spec.files.size()) {
        active_ = false;
        LENS_INFO("voice '{}' is in place", spec.id.toStdString());
        emit finished(true);
        return;
    }
    const VoiceFile file   = spec.files[index_];
    const QString   target = folder_ + QLatin1Char('/') + file.name;

    file_ = new QFile{target + QStringLiteral(".part"), this};
    if (not file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail();
        return;
    }

    QNetworkRequest request{QUrl{QStringLiteral("https://huggingface.co/%1/resolve/%2/%3").arg(spec.repo, spec.revision, file.name)}};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);
    reply_ = manager_->get(request);

    connect(reply_, &QNetworkReply::readyRead, this, [this, file] {
        file_->write(reply_->readAll());
        // Past the pinned size is not the file the repository published, so the transfer stops there
        // rather than filling the disk.
        if (file_->pos() > file.size) {
            reply_->abort();
            return;
        }
        emit progressed(received_ + file_->pos(), total_);
    });
    connect(reply_, &QNetworkReply::finished, this, [this, file, target] {
        // The tail can arrive with the finish signal, so it is read and written here as well.
        const QByteArray rest        = reply_->readAll();
        const bool       transferred = reply_->error() == QNetworkReply::NoError;
        reply_->deleteLater();
        reply_ = nullptr;
        if (transferred)
            file_->write(rest);
        file_->close();

        if (not transferred or not verifyFile(file_->fileName(), file)) {
            fail();
            return;
        }
        QFile::remove(target);
        if (not file_->rename(target)) {
            fail();
            return;
        }
        file_->deleteLater();
        file_ = nullptr;
        received_ += file.size;
        ++index_;
        fetchNext();
    });
}

void VoiceDownloader::fail()
{
    if (file_ != nullptr) {
        file_->remove();
        file_->deleteLater();
        file_ = nullptr;
    }
    active_ = false;
    LENS_WARN("voice '{}' did not download", voiceSpec(model_).id.toStdString());
    emit finished(false);
}

}
