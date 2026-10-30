/**
 * @file tray.cpp
 * @brief The tray icon and its tooltip. The menu itself is a surface; see tray.h.
 */

#include "tray.h"

#include <QGuiApplication>
#include <QIcon>
#include <QJSEngine>
#include <QQmlEngine>
#include <QStyleHints>
#include <QSystemTrayIcon>

#include <utility>

#include "util/log.h"

namespace lens::app {
namespace {

/// Strings go through translate() with both the context and the text spelled out at the
/// call site: lupdate only reads literal arguments, so a helper taking the context as a
/// parameter would extract nothing.
QString tr(const char* text)
{
    return QCoreApplication::translate("lens::app::Tray", text);
}

/// @return The icon for one state, for a taskbar of the given lightness.
/// @note The name suffix says which taskbar the art is for: the glyph is a mid-tone ring and
///       a filled centre, and the grey it uses when switched off has to be dark on a light
///       taskbar and light on a dark one.
QIcon iconFor(const char* state, bool darkTaskbar)
{
    return QIcon(QStringLiteral(":/icons/tray-%1-%2.svg")
                     .arg(QLatin1String(state), QLatin1String(darkTaskbar ? "ondark" : "onlight")));
}

bool taskbarIsDark()
{
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

TrayState stateOf(TrayReadout const& readout)
{
    return trayState(readout.budgetExhausted, readout.busy, readout.selectionCapture);
}

} // namespace

Tray::Tray(std::function<TrayReadout()> readout, QObject* parent)
    : QObject(parent), readout_(std::move(readout)), icon_(new QSystemTrayIcon(this)), budgetRefreshTimer_(this)
{
    connect(icon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::Context) {
            emit geometryChanged();
            emit menuRequested();
        }
    });

    // The reader can switch Windows between light and dark while the app runs, and the icon
    // is drawn for one of the two.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] { refresh(); });

    // No controller signal is guaranteed at midnight. Polling this shell-only state prevents
    // yesterday's exhausted icon from lingering into a new local day.
    budgetRefreshTimer_.setInterval(60 * 1000);
    connect(&budgetRefreshTimer_, &QTimer::timeout, this, &Tray::refresh);
    budgetRefreshTimer_.start();

    refresh();
}

Tray::~Tray() = default;

// The QML singleton the surfaces name as Tray. See AppController::provide() for why the
// instance is handed in rather than built here.
Tray* Tray::instance_ = nullptr;

void Tray::provide(Tray* instance)
{
    instance_ = instance;
}

Tray* Tray::create(QQmlEngine* engine, QJSEngine* scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)
    Q_ASSERT(instance_ != nullptr);
    QQmlEngine::setObjectOwnership(instance_, QQmlEngine::CppOwnership);
    return instance_;
}

bool Tray::show()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        LENS_CRITICAL("no system tray on this desktop; the app has no way to be reached");
        return false;
    }
    // An icon that failed to load leaves a blank slot in the tray, which reads as a missing app
    // rather than a broken resource path.
    if (icon_->icon().isNull())
        LENS_ERROR("the tray icon did not load; check the :/icons resources");

    icon_->show();
    LENS_INFO("tray icon is up");
    return true;
}

QRect Tray::geometry() const
{
    // Kept, not just read: see the note on the declaration. The icon does not move, and a
    // stale answer is a panel in the right place rather than one on the wrong screen.
    const QRect live = icon_->geometry();
    if (live.width() > 0)
        lastGeometry_ = live;
    return lastGeometry_;
}

void Tray::refresh()
{
    const TrayReadout readout = readout_();
    icon_->setIcon(iconFor(trayIconName(stateOf(readout)), taskbarIsDark()));

    // The day's tally counts pop events, so the unit is lookups, not words (ADR 0104): "words"
    // is reserved for the deduplicated vocabulary the words popup titles.
    const QString today = tr("Today %1 lookups").arg(QString::number(readout.todayPops));
    const QString mode  = readout.hasApiKey ? readout.mode : tr("No API key yet");
    icon_->setToolTip(QStringLiteral("Lens · %1 · %2").arg(mode, today));
}

}
