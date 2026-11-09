/**
 * @file update_duty.cpp
 * @brief Owns the update check: when it runs, what it says, and what the reader chose.
 */

#include "update_duty.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include "util/log.h"

namespace lens::app {

UpdateDuty::UpdateDuty(StorageDuty&            storage,
                       update::UpdateClient&   client,
                       update::DownloadClient& downloader,
                       const QString&          currentVersion,
                       Launcher                launcher,
                       QString                 downloadDir,
                       DateProvider            dateProvider,
                       QObject*                parent)
    : QObject(parent),
      storage_(storage),
      client_(client),
      downloader_(downloader),
      launcher_(std::move(launcher)),
      dateProvider_(std::move(dateProvider)),
      downloadDir_(std::move(downloadDir)),
      current_(currentVersion)
{
    if (not launcher_) {
        launcher_ = [](const QString& path, const QStringList& arguments) {
            // No arguments and no shell: the installer is a program the reader ran on purpose, not
            // a silent step, so it opens its own window and asks.
            return QProcess::startDetached(path, arguments);
        };
    }
    connect(&client_, &update::UpdateClient::finished, this, [this](update::Release release) {
        inFlight_ = false;
        applyResult(std::move(release), manual_);
    });
    connect(&client_, &update::UpdateClient::offline, this, [this] {
        inFlight_ = false;
        reportOffline(manual_);
    });
    connect(&downloader_, &update::DownloadClient::sumsReady, this, &UpdateDuty::onSumsReady);
    connect(&downloader_, &update::DownloadClient::installerReady, this, &UpdateDuty::onInstallerReady);
    connect(&downloader_, &update::DownloadClient::progressed, this, [this](qint64 received, qint64 total) {
        if (download_.stage != update::DownloadStage::Downloading)
            return;
        download_.received = received;
        if (total > 0)
            download_.total = total;
        download_.percent = download_.total > 0 ? static_cast<int>(qMin<qint64>(100, received * 100 / download_.total)) : 0;
        // The pace is the average since the transfer started, so a burst of buffered bytes does
        // not promise a finish the rest of the transfer cannot keep. Under half a second there is
        // no average worth showing, and the card leaves the estimate out.
        download_.etaSeconds = -1;
        if (download_.total > 0 and received > 0 and transferClock_.isValid()) {
            const qint64 elapsedMs = transferClock_.elapsed();
            if (elapsedMs >= 500) {
                const qint64 speed     = received * 1000 / elapsedMs;
                const qint64 remaining = qMax<qint64>(0, download_.total - received);
                download_.etaSeconds   = speed > 0 ? static_cast<int>((remaining + speed - 1) / speed) : -1;
            }
        }
        emit downloadChanged();
    });
    connect(&downloader_, &update::DownloadClient::failed, this, [this](update::DownloadClient::Failure why) {
        if (cancelling_)
            return;
        // Only a download the reader asked for can fail as one. The checksum list is read without
        // the reader's asking, so its failure is logged and the card keeps View only.
        if (download_.stage != update::DownloadStage::Downloading and download_.stage != update::DownloadStage::Verifying) {
            LENS_WARN("the checksum list could not be read; the card keeps View only");
            return;
        }
        deletePartial();
        failDownload(why == update::DownloadClient::Failure::Proxy ? QStringLiteral("proxy") : QStringLiteral("generic"));
    });
    LENS_INFO("update check ready: current version {}", current_.toStdString());
}

QVariantMap UpdateDuty::update() const
{
    return QVariantMap{{QStringLiteral("state"), state_},
                       {QStringLiteral("current"), current_},
                       {QStringLiteral("latest"), latest_},
                       {QStringLiteral("notes"), notes_},
                       {QStringLiteral("pageUrl"), pageUrl_}};
}

update::StoredUpdate UpdateDuty::stored() const
{
    update::StoredUpdate settings;
    auto const           section = storage_.section("UPDATE");
    settings.autoCheck           = section.value<bool>("autoCheck", settings.autoCheck);
    settings.lastCheckDate       = QString::fromStdString(section.value<std::string>("lastCheckDate", {}));
    settings.skipped             = QString::fromStdString(section.value<std::string>("skipped", {}));
    return settings;
}

void UpdateDuty::writeAutoCheck(bool on)
{
    storage_.section("UPDATE").set<bool>("autoCheck", on);
}

void UpdateDuty::writeLastCheckDate(const QString& date)
{
    storage_.section("UPDATE").set<std::string>("lastCheckDate", date.toStdString());
}

void UpdateDuty::writeSkipped(const QString& version)
{
    storage_.section("UPDATE").set<std::string>("skipped", version.toStdString());
}

void UpdateDuty::setAutoCheck(bool on)
{
    if (stored().autoCheck == on)
        return;
    writeAutoCheck(on);
    LENS_INFO("the startup update check is now {}", on ? "on" : "off");
    emit updateChanged();
}

bool UpdateDuty::autoCheck() const
{
    return stored().autoCheck;
}

void UpdateDuty::checkAtStartup()
{
    const QString today = dateProvider_().toString(Qt::ISODate);
    if (not update::shouldCheckAutomatically(stored(), today)) {
        LENS_TRACE("the startup update check was not due on {}", today.toStdString());
        return;
    }
    // Written before the answer is known: a machine that cannot reach the source must not
    // retry on every launch, and a failed attempt is still an attempt.
    writeLastCheckDate(today);
    startCheck(false);
}

void UpdateDuty::checkForUpdates()
{
    startCheck(true);
}

void UpdateDuty::startCheck(bool manual)
{
    if (inFlight_) {
        // A reader who pressed the button during the startup check is answered by the answer
        // already on its way, as what they asked for: dropping the press would leave the status row
        // saying nothing on a dead network, and would keep the card down for a version they had
        // chosen to skip.
        manual_ = manual_ or manual;
        LENS_INFO("a check is already running; the answer will be reported as {}",
                  manual_ ? "asked for" : "automatic");
        return;
    }
    manual_   = manual;
    inFlight_ = true;
    state_    = QStringLiteral("checking");
    // The card is not taken down for a check the reader asked for. It is what they are reading,
    // and a window that blinks out and comes back in the middle of the screen is a card that
    // moved under their cursor. cardVisible() counts `checking` as still up, and the answer
    // decides: a newer version replaces what is on the card, anything else takes it down.
    emit updateChanged();
    LENS_INFO("checking for updates ({})", manual ? "asked for" : "at startup");
    client_.fetch();
}

void UpdateDuty::applyResult(update::Release const& release, bool manual)
{
    // Before anything below can change it: the card's visibility on entry says whether this answer is a new offer.
    const bool wasVisible = cardVisible_;

    // A transfer belongs to the version it was started for. If this answer names a different
    // one, the transfer is given up on first: its half-written file, its digest and the name it
    // would land under all name the old release, and letting it finish would put a checked
    // file under a name it was never checked as.
    const bool downloading = download_.stage == update::DownloadStage::Downloading or download_.stage == update::DownloadStage::Verifying;
    if (downloading and release.versionText != download_.version) {
        LENS_INFO("a newer release arrived while {} was downloading; the transfer was cancelled", download_.version.toStdString());
        cancelDownload();
    }
    // An installer that finished for one version is not offered under another's card, and is
    // not launched from one: the card's version and the file's version must be the same.
    const bool finished = download_.stage == update::DownloadStage::Ready or download_.stage == update::DownloadStage::Failed;
    if (finished and release.versionText != download_.version)
        discardDownload();
    // Recomputed: the cancel above may have just ended the transfer.
    const bool busy = download_.stage == update::DownloadStage::Downloading or download_.stage == update::DownloadStage::Verifying;

    latest_  = release.versionText;
    notes_   = release.notes;
    pageUrl_ = release.pageUrl;

    const auto current   = util::parseTag(current_.toStdString());
    const bool available = current and update::isUpdateAvailable(release.version, *current);
    state_               = available ? QStringLiteral("available") : QStringLiteral("upToDate");

    // The automatic path is the one that may stay quiet, and `skipped` is compared against the
    // exact version -- so a newer release prompts again. The manual path reports whatever is
    // really there, and raises the card when there is something new to look at.
    cardVisible_ = available and (manual or update::shouldPrompt(stored(), latest_, available));

    // Whether this release can be downloaded at all is a fact about the release, and the card
    // cannot offer a download without it. It is one small request, and it is asked for only
    // when a newer version was found -- which is to say, only when the reader is about to be
    // shown a card at all. It fetches a list; it downloads nothing.
    // Not while a transfer is running: the state of that transfer is its own, and a new list
    // request would only replace the one it is still waiting on.
    // A verified installer keeps the answer it was checked against: a re-check of the same version
    // must not reset it, or a failed list read would turn a ready download back into idle.
    if (not busy and download_.stage != update::DownloadStage::Ready) {
        download_.list = {};
        if (available) {
            download_.list.stage   = update::DigestList::Stage::Asked;
            download_.list.version = latest_;
            downloader_.fetchSums(latest_);
        }
    }

    LENS_INFO("update check answered: latest {} against {} -- {}", latest_.toStdString(), current_.toStdString(), available ? "newer" : "nothing new");
    if (cardVisible_ and not wasVisible) emit newVersionOffered();
    emit updateChanged();
}

void UpdateDuty::reportOffline(bool manual)
{
    // A transfer for the version this answer just cleared has no card to belong to, and its list
    // would be dropped on arrival; it is given up on here, as a newer answer would give it up.
    if (download_.stage == update::DownloadStage::Downloading or download_.stage == update::DownloadStage::Verifying)
        cancelDownload();
    latest_.clear();
    notes_.clear();
    pageUrl_.clear();
    cardVisible_ = false;
    // An automatic check that could not reach the source leaves the state as it was: the reader
    // did not ask, so there is nothing to report. The manual one is the reader asking, and is
    // answered.
    state_ = manual ? QStringLiteral("offline") : QStringLiteral("idle");
    LENS_WARN("the update source could not be reached; {}",
              manual ? "reporting it" : "saying nothing, since nobody asked");
    emit updateChanged();
}

void UpdateDuty::skipUpdate()
{
    if (latest_.isEmpty())
        return;
    writeSkipped(latest_);
    cardVisible_ = false;
    LENS_INFO("skipped version {}", latest_.toStdString());
    emit updateChanged();
}

void UpdateDuty::openReleasePage()
{
    const QUrl url{pageUrl_};
    if (not url.isValid() or url.scheme() != QLatin1String("https") or url.host().isEmpty()) {
        LENS_WARN("the release page is not an https URL with a host; nothing was opened");
        return;
    }
    cardVisible_ = false;
    emit updateChanged();
    LENS_INFO("opening the release page in the browser");
    QDesktopServices::openUrl(url);
}

void UpdateDuty::closeCard()
{
    if (not cardVisible_)
        return;
    cardVisible_ = false;
    emit updateChanged();
}

bool UpdateDuty::cardVisible() const
{
    // `checking` counts as up: a check that is under way does not retract what the card is
    // already saying, and the surfaces take the card down when the answer says otherwise.
    return cardVisible_ and (state_ == QLatin1String("available") or state_ == QLatin1String("checking"));
}

QVariantMap UpdateDuty::download() const
{
    return update::downloadMap(download_);
}

QString UpdateDuty::downloadDirectory() const
{
    if (not downloadDir_.isEmpty())
        return downloadDir_;

    // The reader's own cache, not the install tree: an installer this program writes has no
    // business in a folder the program may not be allowed to write, and this is the one place
    // on the machine where a per-user file already belongs.
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/updates");
    QDir().mkpath(directory);
    return directory;
}

QString UpdateDuty::installerPath() const
{
    const QString name = update::installerName(download_.version);
    if (name.isEmpty())
        return {};
    return downloadDirectory() + QLatin1Char('/') + name;
}

QString UpdateDuty::partialPath() const
{
    const QString name = update::installerName(download_.version);
    if (name.isEmpty())
        return {};
    return downloadDirectory() + QLatin1Char('/') + name + QStringLiteral(".part");
}

void UpdateDuty::deletePartial()
{
    const QString partial = partialPath();
    if (partial.isEmpty())
        return;
    QFile::remove(partial);
}

void UpdateDuty::discardDownload()
{
    // The file's name is this version's, so it is removed before the version is forgotten.
    QFile::remove(installerPath());
    deletePartial();
    download_.list = {};
    download_.version.clear();
    resetDownload(update::DownloadStage::Idle);
    LENS_INFO("the downloaded installer for another version was discarded");
}

void UpdateDuty::resetDownload(update::DownloadStage const stage)
{
    download_.stage      = stage;
    download_.percent    = 0;
    download_.received   = 0;
    download_.total      = 0;
    download_.etaSeconds = -1;
    transferClock_.invalidate();
    if (stage != update::DownloadStage::Failed)
        download_.failure.clear();
    emit downloadChanged();
}

void UpdateDuty::beginTransfer()
{
    download_.received   = 0;
    download_.total      = 0;
    download_.etaSeconds = -1;
    transferClock_.start();
    downloader_.fetchInstaller(download_.version, partialPath());
}

void UpdateDuty::failDownload(const QString& kind)
{
    download_.failure = kind;
    resetDownload(update::DownloadStage::Failed);
    LENS_WARN("the update download failed ({})", kind.toStdString());
}

void UpdateDuty::downloadUpdate()
{
    // A download is a thing a reader asked for on a card they are looking at. The automatic
    // check never comes through here, and a check that has not found anything cannot start one.
    if (state_ != QLatin1String("available") or latest_.isEmpty()) {
        LENS_WARN("a download was asked for with no release on offer; nothing was requested");
        return;
    }
    if (download_.stage == update::DownloadStage::Downloading or download_.stage == update::DownloadStage::Verifying)
        return; // one download at a time

    // Remembered for the whole download: the file it writes, the digest it is checked against
    // and the name it lands under are all this version's, whatever a later check finds.
    download_.version = latest_;
    deletePartial(); // a half-written file from an earlier run is not an answer to anything
    resetDownload(update::DownloadStage::Downloading);

    if (download_.list.stage == update::DigestList::Stage::Known) {
        if (not download_.available()) {
            resetDownload(update::DownloadStage::Idle);
            LENS_INFO("this release publishes no checksum; the card keeps View only");
            return;
        }
        beginTransfer();
        return;
    }
    // The list this release publishes has not been read yet, so the press is remembered and
    // answered by the answer rather than guessing whether the download exists at all.
    download_.list.wanted  = true;
    download_.list.version = latest_;
    downloader_.fetchSums(latest_);
}

void UpdateDuty::cancelDownload()
{
    // Only a transfer in progress can be cancelled. A verified installer is kept until the reader
    // installs it or a newer answer discards it; cancelling it here would orphan it on disk.
    if (download_.stage != update::DownloadStage::Downloading and download_.stage != update::DownloadStage::Verifying)
        return;
    // A check in progress has the file open; closing it here is what lets the delete below
    // succeed on Windows. The queued verifyStep that follows finds the state gone and returns.
    if (download_.stage == update::DownloadStage::Verifying) {
        verifyFile_.close();
        verifyHash_.reset();
        verifyPath_.clear();
    }
    // The transfer reports its own stop through `failed`; that report is not a failure the reader
    // should see, because the reader asked for this one.
    cancelling_ = true;
    downloader_.cancel();
    cancelling_ = false;
    deletePartial();
    // The offer stays: the digest is a fact about the release, not about this transfer. A second
    // press transfers again and is checked against the same digest.
    download_.list.wanted = false;
    download_.version.clear();
    resetDownload(update::DownloadStage::Idle);
    LENS_INFO("the update download was cancelled");
}

void UpdateDuty::onSumsReady(QByteArray sums)
{
    // A list is only for the version it was asked for. A newer answer may have asked for another
    // one since, and that list is then not this card's list: it is ignored, and the one for the
    // current version is the one that counts.
    if (download_.list.version != latest_)
        return;
    const auto digest = update::expectedDigest(sums, update::installerName(latest_));
    // Fail closed: a release that publishes no digest for this installer cannot be checked, and
    // a file this program cannot check is not one it downloads.
    download_.list.stage  = update::DigestList::Stage::Known;
    download_.list.digest = digest.value_or(std::string{});

    if (not download_.available()) {
        download_.list.wanted = false;
        resetDownload(update::DownloadStage::Idle);
        LENS_INFO("this release publishes no checksum for the installer; the card keeps View only");
        return;
    }

    emit downloadChanged();
    if (download_.list.wanted) {
        // The reader pressed Download while the list was still being read.
        download_.list.wanted = false;
        download_.version     = latest_;
        beginTransfer();
    }
}

void UpdateDuty::onInstallerReady(QString path)
{
    resetDownload(update::DownloadStage::Verifying);
    if (path.isEmpty() or download_.list.digest.empty()) {
        deletePartial();
        failDownload(QStringLiteral("checksum"));
        return;
    }

    verifyPath_ = path;
    verifyFile_.setFileName(path);
    if (not verifyFile_.open(QIODevice::ReadOnly)) {
        LENS_WARN("the downloaded file could not be read for checking");
        verifyPath_.clear();
        deletePartial();
        failDownload(QStringLiteral("checksum"));
        return;
    }
    verifyHash_.reset();
    // One chunk per turn of the event loop, not the whole file in this call: the card has to be
    // able to paint the verifying state, and its Cancel has to be able to answer before the
    // check is over.
    QMetaObject::invokeMethod(this, &UpdateDuty::verifyStep, Qt::QueuedConnection);
}

void UpdateDuty::verifyStep()
{
    if (download_.stage != update::DownloadStage::Verifying)
        return;

    // A megabyte per turn keeps each turn short enough that a click lands between them.
    constexpr qint64 turnBytes = 1024 * 1024;
    qint64           read      = 0;
    while (read < turnBytes) {
        const QByteArray chunk = verifyFile_.read(qMin<qint64>(64 * 1024, turnBytes - read));
        if (chunk.isEmpty())
            break;
        verifyHash_.addData(chunk);
        read += chunk.size();
    }
    if (verifyFile_.error() != QFileDevice::NoError) {
        verifyFile_.close();
        deletePartial();
        failDownload(QStringLiteral("checksum"));
        return;
    }
    if (not verifyFile_.atEnd()) {
        QMetaObject::invokeMethod(this, &UpdateDuty::verifyStep, Qt::QueuedConnection);
        return;
    }

    verifyFile_.close();
    const QString actual = QString::fromLatin1(verifyHash_.result().toHex());
    if (actual.compare(QString::fromStdString(download_.list.digest), Qt::CaseInsensitive) != 0) {
        deletePartial();
        failDownload(QStringLiteral("checksum"));
        return;
    }

    // Checked, and only now a file with the name the release publishes it under. Any older
    // installer is replaced rather than kept beside it.
    const QString target = installerPath();
    QFile::remove(target);
    if (not QFile::rename(verifyPath_, target)) {
        LENS_WARN("the checked installer could not be moved into place");
        QFile::remove(verifyPath_); // a checked partial is not left behind where nothing will clean it up
        verifyPath_.clear();
        failDownload(QStringLiteral("generic"));
        return;
    }
    verifyPath_.clear();
    LENS_INFO("the installer for {} was downloaded and checked", latest_.toStdString());
    resetDownload(update::DownloadStage::Ready);
}

void UpdateDuty::installUpdate()
{
    const QString path = installerPath();
    if (download_.stage != update::DownloadStage::Ready or path.isEmpty()) {
        LENS_WARN("an install was asked for with nothing downloaded");
        return;
    }
    // Checked again, here, rather than trusted from the moment it landed: this is the last
    // point at which the program can refuse to run a file.
    if (download_.list.digest.empty() or not update::digestMatches(path, download_.list.digest)) {
        QFile::remove(path);
        failDownload(QStringLiteral("checksum"));
        return;
    }

    // A launch that did not start leaves Lens running with the card saying why, rather than
    // closing the application with nothing shown.
    if (not launcher_(path, QStringList{})) {
        LENS_WARN("the verified installer could not be started");
        failDownload(QStringLiteral("generic"));
        return;
    }
    LENS_INFO("the verified installer was started; Lens is closing");
    // The tray's own quit path, which is the application's exit.
    QCoreApplication::quit();
}

}