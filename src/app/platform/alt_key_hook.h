#pragma once

/**
 * @file alt_key_hook.h
 * @brief Watches the Alt key, so the pencil layer can show signposts while it is held.
 *
 * Chrome's menu bar wakes when Alt is released with nothing pressed under it. While the pencil layer
 * is up, a lone Alt tap is therefore followed by a key nobody uses (0xE8), pressed under Alt just before
 * the release reaches the window, so the tap stays a tap for the menu bar.
 *
 * @note Windows only, like the rest of src/app. This is where the Win32 keyboard calls live.
 */

#include <QObject>

#include <functional>

namespace lens::app {

/// @brief What the hook has seen of the Alt key so far.
struct AltHold {
    bool held  = false; ///< The Alt key is down.
    bool chord = false; ///< Another key went down while it was held.
};

/**
 * @brief Applies one key event to the Alt state.
 * @param state    The state the earlier events left; updated.
 * @param vk       The key's virtual-key code.
 * @param down     True for a key going down, false for one coming up.
 * @param injected Whether an event was injected, by this program or another. Injected events change nothing.
 * @return True when this event releases Alt alone: Alt was held and no other key went down under it.
 */
bool altReleaseIsLoneTap(AltHold& state, unsigned vk, bool down, bool injected);

/**
 * @brief Installs the low-level keyboard hook and reports the Alt key going down and up.
 *
 * The hook is process-wide and its callback runs on the thread that installed it, which must be the
 * thread pumping messages. The reports are queued to the owner, so the callback stays short.
 */
class AltKeyHook : public QObject {
    Q_OBJECT
public:
    explicit AltKeyHook(QObject* parent = nullptr);

    /// @brief Uninstalls the hook if it is still installed.
    ~AltKeyHook() override;

    /**
     * @brief Install the hook.
     * @return True on success; on failure the reason is logged and false comes back. Nothing throws.
     */
    bool install();

    /// @brief The condition under which a lone Alt tap is kept a tap for the menu bar; read on each tap.
    void setMenuGuard(std::function<bool()> guard);

signals:
    /// @brief The Alt key went down; auto-repeat does not report it again.
    void altPressed();
    /// @brief The Alt key came up.
    void altReleased();

private:
    bool installed_ = false;
};

}
