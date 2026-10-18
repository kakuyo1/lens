#pragma once

/**
 * @file dev_surface.h
 * @brief The developer command surface: runs the command table against the surfaces of the running application
 *        (docs/adr/0068).
 *
 * Compiled in only by LENS_ENABLE_DEV_SURFACE, and never shipped. The grammar and the shape of the answers are in
 * core/dev_command.h; this class is what a command does. A surface command calls the function the surface already has
 * (Main.qml's showSettings(), the window's visible property), so a measurement runs the code the reader's click runs.
 */

#include "core/dev_command.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace lens::app {

class AppController;

/**
 * @brief Runs command-table lines against one running application's surfaces.
 *
 * The surfaces are found by the object names their windows carry. Each surface's frames are counted from the moment
 * this object is built, so a first frame that lands before a command asks for it is still counted.
 */
class DevSurface : public QObject {
    Q_OBJECT

public:
    /// @param root The Main window the surfaces are declared in. It is also this object's parent.
    /// @param controller The controller the glass sheets and the settings are read from.
    /// @param build What the state reports about this executable.
    DevSurface(QObject* root, AppController* controller, core::dev::Build build);

    /// @brief Run a script one line at a time, then quit. Blank lines and `#` comments are skipped.
    /// @param lines The script, already split into lines.
    /// @param outputPath Where each answer is appended as one JSON line, truncated first. Empty to log only.
    /// @note A `wait` line keeps the script going from the event loop, so the window stays responsive while it waits.
    void runScript(QStringList const& lines, QString const& outputPath);

private:
    /// A `wait` line that has not been answered yet.
    struct Waiting {
        QString            line;
        core::dev::Command command;
        /// The surface's frame count when the wait began; a painted wait is done once it has moved past this.
        std::uint64_t baseline;
    };

    /// @brief Run a line that does not wait. A refused or failed line answers with "ok": false.
    nlohmann::json execute(core::dev::Command const& command);
    nlohmann::json show(std::string const& surface);
    nlohmann::json hide(std::string const& surface);
    nlohmann::json hideAll();
    nlohmann::json state() const;

    /// @return The window carrying @p name as its object name, or nullptr when there is none.
    QObject*      window(std::string const& name) const;
    std::uint64_t paintedCount(std::string const& name) const;
    bool          conditionHolds(Waiting const& waiting) const;

    /// Run lines from the script until one waits or the script ends.
    void step();
    /// One tick of an open wait: answer once its condition holds or it times out.
    void poll();
    /// Record one answer, for @p line, to the log and to the output file.
    void answer(QString const& line, nlohmann::json result);

    QObject*         root_;
    AppController*   controller_;
    core::dev::Build build_;
    /// Frames swapped per surface window. Filled once in the constructor and only read after, so the render thread can
    /// count into it without a lock.
    std::map<std::string, std::atomic<std::uint64_t>> painted_;
    QStringList                                       script_;
    qsizetype                                         next_ = 0;
    QString                                           outputPath_;
    QTimer                                            poll_;
    QElapsedTimer                                     waited_;
    std::optional<Waiting>                            waiting_;
};

/**
 * @brief Start the command surface when the command line asks for it: `--dev-script=<file>`, with its answers written
 *        to `--dev-out=<file>`. Without `--dev-script` nothing is created, so the run is the same as without the surface.
 * @param root The Main window the surfaces are declared in.
 * @param controller The controller the surfaces are driven through.
 * @param arguments The command line, as QCoreApplication::arguments() gives it.
 * @param build What the state reports about this executable.
 */
void startDevSurface(QObject* root, AppController* controller, QStringList const& arguments, core::dev::Build build);

} // namespace lens::app
