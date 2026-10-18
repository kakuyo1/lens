/**
 * @file dev_surface.cpp
 * @brief The command table run against the running application, for dev_surface.h.
 */

#include "dev_surface.h"

#include "app_controller.h"
#include "glass_backdrop.h"
#include "util/log.h"

#include <QCoreApplication>
#include <QFile>
#include <QMetaObject>
#include <QQuickWindow>
#include <QVariant>
#include <QWindow>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace lens::app {

namespace {

/// How long a `wait` may run before it answers with a timeout. Generous: a cold window's first frame takes a while.
constexpr int kWaitTimeoutMs = 5000;
/// How often a `wait` checks its condition.
constexpr int kWaitPollMs = 16;

/// A surface's opening function on Main.qml. The ones that take `from` are given the undefined value, which is what a
/// command has in place of the element a click came from.
struct Opener {
    std::string_view surface;
    char const*      method;
    bool             takesFrom;
};

constexpr std::array<Opener, 6> kOpeners{{
    {"settings", "showSettings", true},
    {"tray", "showTrayMenu", false},
    {"stats", "showStats", true},
    {"words", "showWords", true},
    {"yearWords", "showYearWords", true},
    {"cost", "showCost", true},
}};

nlohmann::json success()
{
    return {{"ok", true}};
}

nlohmann::json failure(std::string error)
{
    return {{"ok", false}, {"error", std::move(error)}};
}

/// The value of `--name=value` on the command line, when there is one.
std::optional<QString> argumentValue(QStringList const& arguments, QString const& name)
{
    QString const prefix = name + QLatin1Char('=');
    for (auto const& argument : arguments) {
        if (argument.startsWith(prefix))
            return argument.mid(prefix.size());
    }
    return std::nullopt;
}

} // namespace

DevSurface::DevSurface(QObject* root, AppController* controller, core::dev::Build build)
    : QObject(root), root_(root), controller_(controller), build_(std::move(build))
{
    // Every name gets its counter before any window is wired, so the map is never inserted into after this point.
    for (auto const name : core::dev::kSurfaceNames)
        painted_.try_emplace(std::string{name}, 0);

    for (auto const name : core::dev::kSurfaceNames) {
        auto* const surfaceWindow = qobject_cast<QQuickWindow*>(window(std::string{name}));
        if (surfaceWindow == nullptr)
            continue;
        auto* const counter = &painted_.at(std::string{name});
        connect(surfaceWindow, &QQuickWindow::frameSwapped, this, [counter] { counter->fetch_add(1, std::memory_order_relaxed); }, Qt::DirectConnection);
    }

    poll_.setInterval(kWaitPollMs);
    connect(&poll_, &QTimer::timeout, this, &DevSurface::poll);
}

void DevSurface::runScript(QStringList const& lines, QString const& outputPath)
{
    outputPath_ = outputPath;
    if (not outputPath_.isEmpty()) {
        // Truncated here, so a run's answers are the only lines in the file.
        QFile output{outputPath_};
        if (not output.open(QIODevice::WriteOnly | QIODevice::Truncate))
            LENS_ERROR("dev surface: cannot write the answers to {}", outputPath_.toStdString());
    }
    script_ = lines;
    next_   = 0;
    QTimer::singleShot(0, this, &DevSurface::step);
}

void DevSurface::step()
{
    while (next_ < script_.size()) {
        auto const line = script_.at(next_++).trimmed();
        if (line.isEmpty() or line.startsWith(u'#'))
            continue;

        auto const parsed = core::dev::parseCommand(line.toStdString());
        if (not parsed.command) {
            answer(line, failure(parsed.error));
            continue;
        }
        if (parsed.command->verb == core::dev::Verb::Wait) {
            waiting_ = Waiting{.line = line, .command = *parsed.command, .baseline = paintedCount(parsed.command->surface)};
            waited_.start();
            poll_.start();
            return;
        }
        answer(line, execute(*parsed.command));
    }
    QCoreApplication::quit();
}

void DevSurface::poll()
{
    if (not waiting_)
        return;
    bool const done = conditionHolds(*waiting_);
    if (not done and waited_.elapsed() < kWaitTimeoutMs)
        return;

    poll_.stop();
    auto const line = waiting_->line;
    answer(line, done ? success() : failure("timed out before the condition held"));
    waiting_.reset();
    step();
}

nlohmann::json DevSurface::execute(core::dev::Command const& command)
{
    switch (command.verb) {
        case core::dev::Verb::SurfaceShow:
            return show(command.surface);
        case core::dev::Verb::SurfaceHide:
            return hide(command.surface);
        case core::dev::Verb::SurfaceHideAll:
            return hideAll();
        case core::dev::Verb::State:
            return state();
        case core::dev::Verb::Version:
            return core::dev::versionJson(build_);
        case core::dev::Verb::Help:
            return core::dev::helpJson();
        case core::dev::Verb::Wait:
            break;
    }
    // A wait is answered by the script, which can keep running while it waits; it never reaches here.
    return failure("wait cannot run synchronously");
}

nlohmann::json DevSurface::show(std::string const& surface)
{
    auto const opener = std::ranges::find(kOpeners, surface, &Opener::surface);
    if (opener == kOpeners.end())
        return failure("surface show " + surface + ": no command opens it yet");

    bool const invoked = opener->takesFrom
                             ? QMetaObject::invokeMethod(root_, opener->method, Q_ARG(QVariant, QVariant()))
                             : QMetaObject::invokeMethod(root_, opener->method);
    return invoked ? success() : failure("surface show " + surface + ": " + opener->method + " is not callable");
}

nlohmann::json DevSurface::hide(std::string const& surface)
{
    auto* const target = window(surface);
    if (target == nullptr)
        return failure("surface hide " + surface + ": no such window");
    target->setProperty("visible", false);
    return success();
}

nlohmann::json DevSurface::hideAll()
{
    // The three calls a click outside makes: the panels, the bubble and the selection bar.
    if (not QMetaObject::invokeMethod(root_, "hidePanels", Q_ARG(QVariant, QVariant())))
        return failure("surface hide-all: hidePanels is not callable");
    for (auto const* const name : {"bubble", "selectionBar"}) {
        if (auto* const surfaceWindow = window(name))
            QMetaObject::invokeMethod(surfaceWindow, "dismiss");
    }
    return success();
}

nlohmann::json DevSurface::state() const
{
    core::dev::StateInput input;
    input.build = build_;

    auto const settings = controller_->property("settings").toMap();
    if (settings.contains(QStringLiteral("theme")))
        input.theme = settings.value(QStringLiteral("theme")).toString().toStdString();

    auto windows = root_->findChildren<QWindow*>();
    if (auto* const self = qobject_cast<QWindow*>(root_))
        windows.prepend(self);
    input.windowsTotal   = static_cast<int>(windows.size());
    input.windowsVisible = static_cast<int>(std::ranges::count_if(windows, [](QWindow const* candidate) { return candidate->isVisible(); }));

    for (auto const name : core::dev::kSurfaceNames) {
        auto const              key = std::string{name};
        core::dev::SurfaceState surface{.name = key};
        if (auto* const surfaceWindow = qobject_cast<QWindow*>(window(key))) {
            surface.visible = surfaceWindow->isVisible();
            surface.painted = paintedCount(key);
        }
        if (auto* const backdropSheet = controller_->glassBackdrop()->surface(QString::fromStdString(key))) {
            surface.serial    = backdropSheet->serial();
            surface.publishes = backdropSheet->publishes();
            surface.live      = backdropSheet->live();
            auto const region = backdropSheet->sheet();
            if (not region.isEmpty())
                surface.sheet = std::to_string(region.width()) + "x" + std::to_string(region.height());
        }
        input.surfaces.push_back(std::move(surface));
    }
    return core::dev::stateJson(input);
}

QObject* DevSurface::window(std::string const& name) const
{
    return root_->findChild<QObject*>(QString::fromStdString(name), Qt::FindChildrenRecursively);
}

std::uint64_t DevSurface::paintedCount(std::string const& name) const
{
    return painted_.at(name).load(std::memory_order_relaxed);
}

bool DevSurface::conditionHolds(Waiting const& waiting) const
{
    auto const& command = waiting.command;
    auto* const target  = qobject_cast<QWindow*>(window(command.surface));
    bool const  visible = target != nullptr and target->isVisible();
    switch (command.waitFor) {
        case core::dev::WaitFor::Visible:
            return visible;
        case core::dev::WaitFor::Hidden:
            return not visible;
        case core::dev::WaitFor::Painted:
            return paintedCount(command.surface) > waiting.baseline;
    }
    return false;
}

void DevSurface::answer(QString const& line, nlohmann::json result)
{
    result["cmd"]   = line.toStdString();
    auto const text = result.dump();
    LENS_INFO("dev surface: {}", text);
    if (outputPath_.isEmpty())
        return;
    QFile output{outputPath_};
    if (output.open(QIODevice::WriteOnly | QIODevice::Append))
        output.write((QString::fromStdString(text) + QLatin1Char('\n')).toUtf8());
    else
        LENS_ERROR("dev surface: cannot append to {}", outputPath_.toStdString());
}

void startDevSurface(QObject* root, AppController* controller, QStringList const& arguments, core::dev::Build build)
{
    auto const scriptPath = argumentValue(arguments, QStringLiteral("--dev-script"));
    if (not scriptPath)
        return;

    QFile script{*scriptPath};
    if (not script.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LENS_ERROR("dev surface: cannot read the script {}", scriptPath->toStdString());
        QCoreApplication::exit(1);
        return;
    }
    auto const  outputPath = argumentValue(arguments, QStringLiteral("--dev-out")).value_or(QString{});
    auto* const surface    = new DevSurface(root, controller, std::move(build));
    surface->runScript(QString::fromUtf8(script.readAll()).split(QLatin1Char('\n')), outputPath);
}

} // namespace lens::app
