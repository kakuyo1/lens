/**
 * @file dev_surface.cpp
 * @brief The command table run against the running application, for dev_surface.h.
 */

#include "dev_surface.h"

#include "core/domain/surface_table.h"
#include "app/app_controller.h"
#include "app/pet/pet_controller.h"
#include "dev_pipe.h"
#include "app/glass/glass_backdrop.h"
#include "app/paths.h"
#include "util/log.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QMetaObject>
#include <QPoint>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QVariant>
#include <QWheelEvent>
#include <QWindow>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

namespace lens::app {

namespace {

/// How long a `wait` may run before it answers with a timeout. Generous: a cold window's first frame takes a while.
constexpr int kWaitTimeoutMs = 5000;
/// How often a `wait` checks its condition.
constexpr int kWaitPollMs = 16;
/// How many pipe lines may wait behind an open `wait`. Past it a line is answered as refused, so a client that sends
/// faster than the waits resolve cannot grow the queue without bound.
constexpr qsizetype kMaxQueuedLines = 256;

/// Ends the run with @p code once the event loop is running. QCoreApplication::exit does nothing before exec(), and
/// startDevSurface runs before it.
void exitWhenRunning(int code)
{
    QTimer::singleShot(0, [code] { QCoreApplication::exit(code); });
}

/// A surface's opening function on Main.qml, and how many arguments the call passes. The first is
/// the undefined value a command has in place of the element a click came from; a method with
/// further parameters takes the same undefined for each, which is what its own defaults key on.
struct Opener {
    std::string_view surface;
    char const*      method;
    int              args;
};

/// How many rows carry a dev opener. Counted, not written: the openers are the surface table's rows
/// that name an `open` method (core/domain/surface_table.h, docs/adr/0084).
constexpr std::size_t openerCount()
{
    std::size_t count = 0;
    for (lens::core::surface::Row const& row : lens::core::surface::kRows)
        if (row.open != nullptr)
            ++count;
    return count;
}

/// The opener table, read straight off the surface rows rather than kept as a second hand-written
/// copy that would drift from the surface names (docs/adr/0084).
constexpr std::array<Opener, openerCount()> kOpeners = [] {
    std::array<Opener, openerCount()> openers{};
    std::size_t                       index = 0;
    for (lens::core::surface::Row const& row : lens::core::surface::kRows)
        if (row.open != nullptr)
            openers[index++] = Opener{.surface = row.name, .method = row.open, .args = row.openArity};
    return openers;
}();

/// A surface's navigation entry: the window method a `surface route` target walks. Read off the surface rows
/// like the openers, so a surface that grows internal pages is a row that names one more method.
struct Router {
    std::string_view surface;
    char const*      method;
};

/// How many rows carry a dev route, counted like the openers are.
constexpr std::size_t routerCount()
{
    std::size_t count = 0;
    for (lens::core::surface::Row const& row : lens::core::surface::kRows)
        if (row.route != nullptr)
            ++count;
    return count;
}

constexpr std::array<Router, routerCount()> kRouters = [] {
    std::array<Router, routerCount()> routers{};
    std::size_t                       index = 0;
    for (lens::core::surface::Row const& row : lens::core::surface::kRows)
        if (row.route != nullptr)
            routers[index++] = Router{.surface = row.name, .method = row.route};
    return routers;
}();

nlohmann::json success()
{
    return {{"ok", true}};
}

nlohmann::json failure(std::string error)
{
    return {{"ok", false}, {"error", std::move(error)}};
}

/// @return The surface's first scroller, found through the public meta objects alone: the
///         flickable headers are private, but contentY is a Q_PROPERTY any QObject can read. A
///         ListView and the other scrollers derive from QQuickFlickable, so the name walk
///         reaches them too.
QObject* scrollerOf(QObject const* window)
{
    auto isFlickable = [](QMetaObject const* meta) {
        for (auto const* m = meta; m != nullptr; m = m->superClass())
            if (qstrcmp(m->className(), "QQuickFlickable") == 0)
                return true;
        return false;
    };
    for (QObject* const child : window->findChildren<QObject*>())
        if (isFlickable(child->metaObject()))
            return child;
    return nullptr;
}

/// @return The scroller's numbers a scroll question is settled with: what the content says it
///         holds, where it stands and -- for a view -- how many rows it counts.
nlohmann::json scrollerFacts(QObject const* scroller)
{
    nlohmann::json facts{{"contentY", scroller->property("contentY").toDouble()},
                         {"contentHeight", scroller->property("contentHeight").toDouble()},
                         {"viewportHeight", scroller->property("height").toDouble()}};
    if (const QVariant count = scroller->property("count"); count.isValid())
        facts["count"] = count.toInt();
    return facts;
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

DevSurface::DevSurface(QObject* root, AppController* controller, pet::PetController* pet, core::dev::Build build)
    : QObject(root), root_(root), controller_(controller), pet_(pet), build_(std::move(build))
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

bool DevSurface::listenOnPipe(QString const& name)
{
    auto* const pipe = new DevPipe(name, this);
    if (not pipe->listen()) {
        delete pipe;
        return false;
    }
    pipe_ = pipe;
    connect(pipe_, &DevPipe::lineReceived, this, &DevSurface::onPipeLine);
    return true;
}

void DevSurface::onPipeLine(QString const& line)
{
    // Everything queued has run, so the lines are dropped rather than kept for the life of the run.
    if (next_ == script_.size()) {
        script_.clear();
        next_ = 0;
    }
    if (script_.size() - next_ >= kMaxQueuedLines) {
        answer(line, failure("the queue is full: wait for the answers before sending more"));
        return;
    }
    script_.append(line);
    if (not waiting_)
        step();
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
        if (parsed.command->verb == core::dev::Verb::Quit) {
            if (pipe_ != nullptr)
                pipe_->drain();
            QCoreApplication::quit();
            return;
        }
    }
    // With a pipe the run goes on until a quit line, because the client may send more.
    if (pipe_ == nullptr)
        QCoreApplication::quit();
}

void DevSurface::poll()
{
    if (not waiting_)
        return;
    bool const done = conditionHolds(*waiting_);
    // A condition wait gets the standing ceiling; a hold waits out exactly the seconds its line asked for, so a
    // 25-second hold answers as success at 25 seconds instead of timing out at the ceiling.
    auto const limit = waiting_->command.waitFor == core::dev::WaitFor::Seconds ? waiting_->command.waitSeconds * 1000
                                                                                : kWaitTimeoutMs;
    if (not done and waited_.elapsed() < limit)
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
        case core::dev::Verb::SurfaceRoute:
            return route(command.surface, command.target);
        case core::dev::Verb::SurfaceScroll:
            return scroll(command.surface, command.scrollY);
        case core::dev::Verb::SurfaceWheel:
            return wheel(command.surface, command.wheelSteps, command.wheelPad);
        case core::dev::Verb::State:
            return state();
        case core::dev::Verb::Version:
            return core::dev::versionJson(build_);
        case core::dev::Verb::Help:
            return core::dev::helpJson();
        case core::dev::Verb::Quit:
            // step() ends the script after this answer is written.
            return success();
        case core::dev::Verb::Selection:
            return injectSelection(command);
        case core::dev::Verb::SettingSet:
            return setSetting(command);
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

    auto* const host = panelHost();
    if (host == nullptr)
        return failure("surface show " + surface + ": SurfaceHost is not present");
    // The argument count has to match the method's parameters exactly: QMetaObject::invokeMethod
    // resolves a QML function by name and arity together, and a call one short of them answers
    // "not callable" for a surface the table claims to open. Every argument is the undefined a
    // command has in place of the element a click came from, which is what the methods' own
    // default handling keys on.
    bool const invoked = opener->args == 0
                             ? QMetaObject::invokeMethod(host, opener->method)
                         : opener->args == 1
                             ? QMetaObject::invokeMethod(host, opener->method, Q_ARG(QVariant, QVariant()))
                             : QMetaObject::invokeMethod(host, opener->method, Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
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
    auto* const host = panelHost();
    if (host == nullptr or not QMetaObject::invokeMethod(host, "hidePanels", Q_ARG(QVariant, QVariant())))
        return failure("surface hide-all: hidePanels is not callable");
    for (auto const* const name : {"bubble", "selectionBar"}) {
        if (auto* const surfaceWindow = window(name))
            QMetaObject::invokeMethod(surfaceWindow, "dismiss");
    }
    return success();
}

nlohmann::json DevSurface::route(std::string const& surface, std::string const& target) const
{
    auto const router = std::ranges::find(kRouters, surface, &Router::surface);
    if (router == kRouters.end())
        return failure("surface route " + surface + ": no command routes it yet");
    auto* const panel = window(surface);
    if (panel == nullptr)
        return failure("surface route " + surface + ": no such window");
    // Navigation of a hidden panel would be discarded on the next show, so it is refused here, where the
    // reason can name it, rather than left to the method's own blanket refusal for unknown targets.
    if (auto const* const panelWindow = qobject_cast<QQuickWindow*>(panel); panelWindow != nullptr and not panelWindow->isVisible())
        return failure("surface route " + surface + ": the panel is down; show it first");
    // The method says whether the target named a page it knows: a hidden panel's navigation would be
    // discarded on the next show, and an unknown category walks nowhere, so both are its refusal to give.
    QVariant   accepted{false};
    bool const invoked = QMetaObject::invokeMethod(panel, router->method, Q_RETURN_ARG(QVariant, accepted), Q_ARG(QVariant, QString::fromStdString(target)));
    if (not invoked)
        return failure("surface route " + surface + ": " + router->method + " is not callable");
    return accepted.toBool() ? success()
                             : failure("surface route " + surface + ": " + target + " names no page the panel offers");
}

nlohmann::json DevSurface::scroll(std::string const& surface, int y) const
{
    auto* const target = window(surface);
    if (target == nullptr)
        return failure("surface scroll " + surface + ": no such window");
    // A hidden surface's scroller is reset on the next show, the way a hidden panel's navigation is,
    // so the line would answer with a state the next show throws away.
    if (auto const* const surfaceWindow = qobject_cast<QQuickWindow*>(target); surfaceWindow != nullptr and not surfaceWindow->isVisible())
        return failure("surface scroll " + surface + ": the surface is down; show it first");
    QObject* scroller = scrollerOf(target);
    if (scroller == nullptr)
        return failure("surface scroll " + surface + ": the surface has nothing that scrolls");
    const double contentHeight = scroller->property("contentHeight").toDouble();
    const double height        = scroller->property("height").toDouble();
    const double lowest        = std::max(0.0, contentHeight - height);
    const double pinned        = std::min(std::max(0.0, static_cast<double>(y)), lowest);
    scroller->setProperty("contentY", pinned);
    // The answer carries the scroller as it stands after the pin, the numbers a scroll question
    // is settled with: a content taller than its rows can account for is a bug the pin has just
    // exposed, and reading it off the answer keeps the pointer out of the run (docs/qa.md).
    nlohmann::json answer = success();
    answer.merge_patch(scrollerFacts(scroller));
    return answer;
}

nlohmann::json DevSurface::wheel(std::string const& surface, int steps, bool const pad)
{
    auto* const target = window(surface);
    if (target == nullptr)
        return failure("surface wheel " + surface + ": no such window");
    auto* const surfaceWindow = qobject_cast<QQuickWindow*>(target);
    if (surfaceWindow == nullptr or not surfaceWindow->isVisible())
        return failure("surface wheel " + surface + ": the surface is down; show it first");
    QObject* scroller = scrollerOf(target);
    if (scroller == nullptr)
        return failure("surface wheel " + surface + ": the surface has nothing that scrolls");
    auto* const item = qobject_cast<QQuickItem*>(scroller);
    if (item == nullptr)
        return failure("surface wheel " + surface + ": the scroller is not an item");
    // The wheel arrives on the scroller's own centre, the way a reader's does, and travels the
    // same delivery a real wheel takes -- the window's event handling, the handlers on the items
    // under it, the Flickable's own bounds. A pinned contentY (surface scroll) proves the numbers;
    // this proves the gesture.
    const QPointF centre = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    const QPointF global = surfaceWindow->mapToGlobal(centre.toPoint());
    // Rising timestamps, the one thing the delivery reads the gesture's speed from: every event
    // posted in one turn would otherwise read as one instant, and a touchpad's momentum with it.
    quint64    timestamp = 0;
    const auto post      = [&](QPoint const& pixel, QPoint const& angle, Qt::ScrollPhase phase) {
        QWheelEvent wheelEvent(centre, global, pixel, angle, Qt::NoButton, Qt::NoModifier, phase, /*inverted=*/false);
        wheelEvent.setTimestamp(timestamp);
        QCoreApplication::sendEvent(surfaceWindow, &wheelEvent);
        timestamp += 16; // one frame apart, a two-finger swipe's own pace
    };
    if (not pad) {
        const QPoint tick{0, steps > 0 ? -120 : 120}; // one notch: down turns the content up
        for (int turned = 0; turned < std::abs(steps); ++turned)
            post(QPoint(), tick, Qt::NoScrollPhase);
    } else {
        // A touchpad's swipe: phases around the deltas, pixel units rather than notches, and a
        // momentum the Flickable itself keeps up once the fingers lift (the End). Spaced by real
        // time and flushed between events, the way a swipe's own pace arrives: posted in one turn
        // the gesture reads as one instant, and the smooth-scroll animation coalesces it into a
        // single step instead of the run of them a reader's fingers make.
        post(QPoint(), QPoint(), Qt::ScrollBegin);
        // Pixel deltas on this delivery scroll down with a positive y, the touchpad's own axis.
        const QPoint stride{0, steps > 0 ? 48 : -48}; // pixels per frame, a deliberate swipe
        for (int turned = 0; turned < std::abs(steps); ++turned) {
            QThread::msleep(12);
            QCoreApplication::processEvents(QEventLoop::AllEvents);
            post(stride, QPoint(), Qt::ScrollUpdate);
        }
        QThread::msleep(12);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        post(QPoint(), QPoint(), Qt::ScrollEnd);
    }
    nlohmann::json answer = success();
    answer.merge_patch(scrollerFacts(scroller));
    return answer;
}

nlohmann::json DevSurface::injectSelection(core::dev::Command const& command)
{
    auto const& region = command.region;
    // A left-to-right drag releases at the selection's bottom-right corner, so the bar anchors there, as it does for a drag.
    auto const anchor = QPoint{region.x + region.width, region.y + region.height};
    controller_->captureDuty()->beginSelection(anchor, QString::fromStdString(command.selectedText));
    return success();
}

nlohmann::json DevSurface::setSetting(core::dev::Command const& command)
{
    // The pet's switch and choice live in the PET section, not the settings table, so they go through the controller
    // that owns them. Both are the reader's own setters, ceremonies included.
    if (command.settingKey == core::dev::SettingKey::PetEnabled or command.settingKey == core::dev::SettingKey::Pet) {
        if (pet_ == nullptr)
            return failure("setting set pet: the desktop pet is not bound");
        if (command.settingKey == core::dev::SettingKey::PetEnabled) {
            auto const on = std::get<bool>(command.settingValue);
            pet_->setEnabled(on);
            return pet_->enabled() == on ? success() : failure("setting set petEnabled: refused");
        }
        auto const id = std::get<std::string>(command.settingValue);
        pet_->setPet(QString::fromStdString(id));
        return pet_->pet().toStdString() == id ? success() : failure("setting set pet: unknown pet");
    }

    // One call: the settings table owns the key, its clamp and the signal the write raises, so this
    // surface does not replay a setter per key it is allowed to touch (docs/adr/0083).
    const std::string key{core::dev::settingKeyName(command.settingKey)};
    QVariant          value;
    if (auto const* const text = std::get_if<std::string>(&command.settingValue))
        value = QString::fromStdString(*text);
    else if (auto const* const number = std::get_if<int>(&command.settingValue))
        value = *number;
    else if (auto const* const flag = std::get_if<bool>(&command.settingValue))
        value = *flag;

    return controller_->setSetting(QString::fromStdString(key), value)
               ? success()
               : failure("setting set " + key + ": refused");
}

nlohmann::json DevSurface::state() const
{
    core::dev::StateInput input;
    input.build = build_;

    auto const settings = controller_->property("settings").toMap();
    if (settings.contains(QStringLiteral("theme")))
        input.theme = settings.value(QStringLiteral("theme")).toString().toStdString();
    if (pet_ != nullptr)
        input.pet = core::dev::PetState{
            .id      = pet_->pet().toStdString(),
            .enabled = pet_->enabled(),
            .shown   = pet_->shown(),
            .action  = pet_->action().toStdString(),
        };

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
        // The scroller's stand, read without moving anything: `state` is the read-only half of the
        // scroll questions, the half a `surface scroll` line answers by moving to it.
        if (auto const* const scroller = scrollerOf(window(key)); scroller != nullptr) {
            surface.scrollY              = scroller->property("contentY").toDouble();
            surface.scrollContentHeight  = scroller->property("contentHeight").toDouble();
            surface.scrollViewportHeight = scroller->property("height").toDouble();
            if (const QVariant counted = scroller->property("count"); counted.isValid())
                surface.scrollCount = counted.toInt();
        }
        input.surfaces.push_back(std::move(surface));
    }
    return core::dev::stateJson(input);
}

QObject* DevSurface::window(std::string const& name) const
{
    return root_->findChild<QObject*>(QString::fromStdString(name), Qt::FindChildrenRecursively);
}

QObject* DevSurface::panelHost() const
{
    return root_->findChild<QObject*>(QStringLiteral("surfaceHost"), Qt::FindChildrenRecursively);
}

std::uint64_t DevSurface::paintedCount(std::string const& name) const
{
    // Not a map::at: a `wait <n>s` line names no surface, and asking for the frame count of nothing must answer
    // nothing rather than throw out of a Qt slot.
    auto const found = painted_.find(name);
    return found == painted_.end() ? 0 : found->second.load(std::memory_order_relaxed);
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
        case core::dev::WaitFor::Seconds:
            // A hold has no condition to watch; the elapsed time is the whole answer.
            return waited_.elapsed() >= command.waitSeconds * 1000;
    }
    return false;
}

void DevSurface::answer(QString const& line, nlohmann::json result)
{
    result["cmd"]   = line.toStdString();
    auto const text = result.dump();
    LENS_INFO("dev surface: {}", text);
    if (pipe_ != nullptr)
        pipe_->reply(QString::fromStdString(text));
    if (outputPath_.isEmpty())
        return;
    QFile output{outputPath_};
    if (output.open(QIODevice::WriteOnly | QIODevice::Append))
        output.write((QString::fromStdString(text) + QLatin1Char('\n')).toUtf8());
    else
        LENS_ERROR("dev surface: cannot append to {}", outputPath_.toStdString());
}

std::optional<std::filesystem::path> devSettingsFile(QStringList const& arguments)
{
    auto const value = argumentValue(arguments, QStringLiteral("--settings-file"));
    if (not value)
        return std::nullopt;
    auto path = toPath(*value);
    if (path.empty())
        return path;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
        LENS_WARN("settings directory {} could not be created: {}", narrow(path.parent_path()), ec.message());
    return path;
}

void startDevSurface(QObject* root, AppController* controller, pet::PetController* pet, QStringList const& arguments, core::dev::Build build)
{
    auto const scriptPath = argumentValue(arguments, QStringLiteral("--dev-script"));
    auto const pipeName   = argumentValue(arguments, QStringLiteral("--dev-pipe"));
    if (not scriptPath and not pipeName)
        return;
    // A bare --dev-pipe names nothing, and a pipe never falls back to a default name: the run is refused.
    if (pipeName and pipeName->isEmpty()) {
        LENS_ERROR("dev surface: --dev-pipe takes a name");
        exitWhenRunning(1);
        return;
    }

    QStringList lines;
    if (scriptPath) {
        QFile script{*scriptPath};
        if (not script.open(QIODevice::ReadOnly | QIODevice::Text)) {
            LENS_ERROR("dev surface: cannot read the script {}", scriptPath->toStdString());
            exitWhenRunning(1);
            return;
        }
        lines = QString::fromUtf8(script.readAll()).split(QLatin1Char('\n'));
    }
    auto const  outputPath = argumentValue(arguments, QStringLiteral("--dev-out")).value_or(QString{});
    auto* const surface    = new DevSurface(root, controller, pet, std::move(build));
    if (pipeName and not surface->listenOnPipe(*pipeName)) {
        LENS_ERROR("dev surface: cannot listen on the pipe '{}'", pipeName->toStdString());
        exitWhenRunning(1);
        return;
    }
    if (scriptPath)
        surface->runScript(lines, outputPath);
}

} // namespace lens::app
