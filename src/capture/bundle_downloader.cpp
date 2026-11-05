/**
 * @file bundle_downloader.cpp
 * @brief The fetch, verify and unpack steps behind bundle_downloader.h.
 */

#include "capture/bundle_downloader.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrl>

#include "util/log.h"

namespace lens::capture {
namespace {

/// Half an hour for the archive. It is 70 MB; a slow link should not be cut off partway through.
constexpr int kTransferTimeoutMs = 30 * 60 * 1000;

/// @return The tar the Windows bundle ships, or "" when this machine has none.
///
/// The Windows directory comes from the environment rather than spelled out: the install root of
/// the OS is the reader's own, not a machine path this repository pins.
QString tarExecutable()
{
    const QString systemRoot = QString::fromLocal8Bit(qgetenv("SystemRoot"));
    if (systemRoot.isEmpty())
        return QString{};
    const QString tar = systemRoot + QStringLiteral("\\System32\\tar.exe");
    return QFileInfo{tar}.isExecutable() ? tar : QString{};
}

QString rootPath()
{
    return QString::fromStdWString(bundleRoot().wstring());
}

} // namespace

BundleDownloader::BundleDownloader(QObject* parent)
    : QObject(parent),
      manager_(new QNetworkAccessManager(this))
{
}

void BundleDownloader::start()
{
    if (phase_ != Phase::Idle)
        return;
    BundleSpec const& spec = bundleSpec();
    archive_               = rootPath() + QLatin1Char('/') + QUrl{spec.url}.fileName();
    received_              = 0;
    cancelled_             = false;

    std::error_code error;
    std::filesystem::create_directories(bundleRoot(), error);
    if (error) {
        LENS_WARN("rapidocr bundle: cannot create {}", rootPath().toStdString());
        emit finished(false, QStringLiteral("generic"));
        return;
    }

    file_ = new QFile{archive_ + QStringLiteral(".part"), this};
    if (not file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(QStringLiteral("generic"));
        return;
    }
    phase_ = Phase::Downloading;

    QNetworkRequest request{QUrl{spec.url}};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);
    reply_ = manager_->get(request);

    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        file_->write(reply_->readAll());
        received_ = file_->pos();
        // Past the pinned size is not the archive the release published, so the transfer stops
        // there rather than filling the disk.
        if (received_ > bundleSpec().size) {
            reply_->abort();
            return;
        }
        emit progressed(received_, bundleSpec().size);
    });
    connect(reply_, &QNetworkReply::finished, this, [this] {
        // The tail can arrive with the finish signal, so it is read and written here as well.
        const QByteArray rest        = reply_->readAll();
        const bool       transferred = reply_->error() == QNetworkReply::NoError;
        const QByteArray reason      = reply_->errorString().toUtf8();
        const int        code        = reply_->error();
        reply_->deleteLater();
        reply_ = nullptr;
        if (transferred)
            file_->write(rest);
        file_->close();
        if (not transferred) {
            LENS_WARN("rapidocr bundle transfer failed after {} bytes: {} ({})",
                      received_,
                      reason.toStdString(),
                      code);
            fail(cancelled_ ? QString{} : received_ == 0 ? QStringLiteral("proxy")
                                                         : QStringLiteral("generic"));
            return;
        }
        archiveArrived();
    });
}

void BundleDownloader::cancel()
{
    // The finished handlers report the stop, so the partial file goes through the same path as any
    // other failure -- with an empty failure, because a cancel is not one.
    cancelled_ = true;
    if (reply_ != nullptr)
        reply_->abort();
    if (tar_ != nullptr)
        tar_->kill();
}

bool BundleDownloader::busy() const
{
    return phase_ != Phase::Idle;
}

void BundleDownloader::archiveArrived()
{
    file_->deleteLater();
    file_ = nullptr;
    // The verified bytes are still the .part file: check them first, and only then let them take
    // the archive's name.
    const QString part = archive_ + QStringLiteral(".part");
    if (not verifyBundleFile(part, BundleFile{QUrl{bundleSpec().url}.fileName(), bundleSpec().size, bundleSpec().sha256})) {
        // The archive is not what the release published; it goes, and nothing is kept.
        LENS_WARN("rapidocr bundle failed its digest at {} bytes; the download goes",
                  QFileInfo{part}.size());
        QFile::remove(part);
        fail(QStringLiteral("generic"));
        return;
    }
    QFile::remove(archive_);
    if (not QFile::rename(part, archive_)) {
        fail(QStringLiteral("generic"));
        return;
    }
    if (cancelled_) {
        phase_ = Phase::Idle;
        emit finished(false, QString{});
        return;
    }
    extract();
}

void BundleDownloader::extract()
{
    const QString tar = tarExecutable();
    if (tar.isEmpty()) {
        fail(QStringLiteral("extract"));
        return;
    }
    phase_ = Phase::Extracting;
    tar_   = new QProcess{this};
    tar_->setProgram(tar);
    tar_->setArguments({QStringLiteral("-xf"), archive_, QStringLiteral("-C"), rootPath()});
    connect(tar_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        tar_->deleteLater();
        tar_ = nullptr;
        if (cancelled_ || status != QProcess::NormalExit || exitCode != 0) {
            // The verified archive stays where it is, so the reader can unpack it by hand.
            phase_ = Phase::Idle;
            LENS_WARN("rapidocr bundle: tar exited {} on {}", exitCode, rootPath().toStdString());
            emit finished(false, cancelled_ ? QString{} : QStringLiteral("extract"));
            return;
        }
        if (not bundleInstalled(bundleRoot())) {
            fail(QStringLiteral("extract"));
            return;
        }
        QFile::remove(archive_);
        phase_ = Phase::Idle;
        LENS_INFO("rapidocr bundle is in place under {}", rootPath().toStdString());
        emit finished(true, QString{});
    });
    tar_->start();
}

void BundleDownloader::fail(QString failure)
{
    if (file_ != nullptr) {
        file_->remove();
        file_->deleteLater();
        file_ = nullptr;
    }
    if (tar_ != nullptr) {
        tar_->deleteLater();
        tar_ = nullptr;
    }
    phase_ = Phase::Idle;
    LENS_WARN("rapidocr bundle did not download ({})", failure.toStdString());
    emit finished(false, failure);
}

}
