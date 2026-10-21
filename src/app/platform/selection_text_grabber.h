#pragma once

#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

/**
 * @file selection_text_grabber.h
 * @brief Copies the reader's selection out of whatever application owns it.
 *
 * There is no API that reads another application's selection, so this works the way a person
 * would: put the clipboard aside, press the copy combination into the foreground window, take
 * what landed, and put the clipboard back. Two consequences are handled here rather than left
 * to be discovered — the clipboard is borrowed, and the copy combination is not the same
 * everywhere: Ctrl+C is SIGINT inside a terminal, so a terminal is sent its own copy key
 * instead of the Ctrl+C that every other application takes.
 *
 * @note Windows only, like the rest of src/app. The clipboard work uses the raw Win32
 *       clipboard API rather than OLE's, for the reason recorded in ClipboardSnapshot.
 */

namespace lens::app {

/// @brief Why one grab attempt ended the way it did.
enum class GrabStatus : std::uint8_t {
    Captured = 0,       ///< The text came back.
    ForegroundIsSelf,   ///< The foreground window belongs to this process; injecting would
                        ///< target our own surfaces, so nothing was sent.
    ClipboardBusy,      ///< The clipboard could not be taken; without a snapshot to restore,
                        ///< clobbering it is not allowed. Also returned on re-entry.
    CopyTimedOut,       ///< Nothing came back: there was no window to copy from, the clipboard
                        ///< did not change before the deadline, the application ignored the
                        ///< synthetic input, or an elevated window dropped it (UIPI).
    EmptyText,          ///< The clipboard changed but carried no text.
    ClipboardRewritten, ///< Another process rewrote the clipboard while the selection was read,
                        ///< and the policy is Silent: the selection is dropped.
};

/// @brief What to do when another process rewrites the clipboard during a grab.
enum class ClipboardPolicy : std::uint8_t {
    Topmost, ///< Keep the selection; the bubble goes over whatever the other process wrote.
    Silent,  ///< Drop the selection rather than show a bubble over a clipboard that is not ours.
};

/// @return The persisted key for @p policy, the vocabulary the settings document uses.
constexpr std::string_view clipboardPolicyKey(ClipboardPolicy const policy)
{
    return policy == ClipboardPolicy::Silent ? "silent" : "topmost";
}

/// @return The policy @p key names; a missing or unknown key reads as the default, Topmost.
inline ClipboardPolicy clipboardPolicyFromStored(std::string_view const key)
{
    return key == "silent" ? ClipboardPolicy::Silent : ClipboardPolicy::Topmost;
}

/// @return Whether a grab that saw the clipboard rewritten may still show its selection.
/// @note Silent drops only a rewritten clipboard: an untouched one is the reader's own, so the
///       popup is exactly what they asked for.
constexpr bool selectionSurvivesClipboardPolicy(ClipboardPolicy const policy, bool const clipboardReplaced)
{
    return not clipboardReplaced or policy == ClipboardPolicy::Topmost;
}

/// @brief The copy combination a terminal takes, and the terminal that takes it.
struct TerminalCopyKey {
    std::string_view process; ///< Executable file name, without its directory.
    unsigned         key;     ///< Virtual-key code of the combination's non-modifier half.
    bool             shift;   ///< Whether Shift is part of it.
};

/**
 * @brief The copy combination to send to @p executableName.
 *
 * A terminal turns Ctrl+C into SIGINT, so sending one into a shell would kill whatever is
 * running there. Its own copy key does the job instead. The check happens in the grab step,
 * which is the only place the foreground process matters.
 *
 * @param executableName Executable name, with or without a directory, case-insensitive.
 * @return The combination for the terminal family listed in the implementation, or nothing
 *         when the process is not one of them and Ctrl+C is the copy, as it is everywhere else.
 * @note The list is a safety net, not a taxonomy. An editor with an embedded terminal, a
 *       debugger, or a REPL is not covered — their process is the host application, which
 *       cannot be recognised from its name, and its Ctrl+C is the copy the embedded terminal
 *       itself takes. Extending the list is the fix if one bites.
 */
std::optional<TerminalCopyKey> terminalCopyKey(std::string_view executableName);

/**
 * @brief A copy of everything the clipboard held, kept as bytes this process owns.
 *
 * OLE looked like the right tool for this and is not: `OleSetClipboard` on an object that
 * came from `OleGetClipboard` fails with `CLIPBRD_E_CANT_CLOSE` in this process, whether or
 * not the clipboard changed in between and whether the content was put there by this process
 * or another one — measured, with `OleSetClipboard(nullptr)` succeeding in the same run, so
 * it is the object round trip that fails, not the setter. The raw API has none of that: every
 * format is read out and copied here, and written back as the clipboard is emptied and filled
 * again. A reader's copied image or formatted block therefore survives, which a text-only save
 * would quietly destroy.
 *
 * @note Some formats are handles rather than memory — bitmaps, palettes, enhanced metafiles,
 *       and the owner-display family — and cannot be copied this way. Those are skipped and
 *       logged by name, so what was dropped is never a mystery.
 */
class ClipboardSnapshot {
public:
    ClipboardSnapshot() = default;

    /**
     * @brief Copy everything the clipboard currently holds.
     * @return The snapshot. Check taken(): a snapshot that could not be read must never be
     *         restored, because restoring it would empty a clipboard it cannot refill.
     */
    static ClipboardSnapshot take();

    /// @return Whether the clipboard was read successfully.
    bool taken() const
    {
        return taken_;
    }

    /**
     * @brief Replace the clipboard with this copy.
     * @return True when every entry landed. A false return means the reader's clipboard was
     *         left short, so it is logged as a warning rather than swallowed.
     * @note Does nothing when taken() is false.
     */
    bool restore() const;

private:
    /// @brief One format and the bytes of its handle.
    struct Entry {
        unsigned               format = 0; ///< A CF_* value or a registered format id.
        std::vector<std::byte> bytes;      ///< The handle's contents, copied out.
    };

    bool               taken_ = false;
    std::vector<Entry> entries_;
};

/**
 * @brief The clipboard operations one grab needs, behind a seam.
 *
 * The production adapter is the Win32 clipboard. The in-memory fake in the unit suite lets the
 * borrow rules -- when the snapshot is put back and when it is left alone -- run without touching
 * the machine-wide clipboard. ClipboardSnapshot's own cases stay in the integration suite: they
 * exercise the real formats and the real handles.
 */
class ClipboardPort {
public:
    virtual ~ClipboardPort() = default;

    /// @return A copy of the clipboard, or nothing when it cannot be read.
    virtual std::optional<ClipboardSnapshot> take() = 0;

    /// @return True when the snapshot was put back in full.
    virtual bool restore(ClipboardSnapshot const& snapshot) = 0;

    /// @return The clipboard's sequence number now.
    virtual unsigned long sequence() = 0;

    /// @brief Wait for the clipboard to change, pumping messages while it waits.
    /// @return Whether the sequence number moved before the deadline.
    virtual bool waitForChange(unsigned long sequenceBefore) = 0;

    /// @return The text on the clipboard, or nothing when it holds none.
    virtual std::optional<QString> readText() = 0;
};

/**
 * @brief One borrow of the clipboard: snapshot it, then put it back unless someone else rewrote it.
 *
 * The restore rule is the part worth testing: a snapshot put back over a clipboard another process
 * wrote meanwhile would destroy that newer content. The borrow reads the sequence after our copy
 * has landed and again after the text has been read; a change between the two means someone else
 * wrote, and the snapshot is then left alone.
 */
class ClipboardBorrow {
public:
    explicit ClipboardBorrow(ClipboardPort& port)
        : port_(port)
    {}

    /// @brief Take the snapshot and remember the sequence a copy must move.
    /// @return False when the clipboard could not be read; nothing may be written then.
    bool begin();

    /// @return The sequence number a copy has to move.
    unsigned long sequenceBefore() const
    {
        return sequenceBefore_;
    }

    /// @brief Remember the sequence our landed copy left, before the text is read.
    void markCopySequence()
    {
        sequenceOfCopy_ = port_.sequence();
    }

    /// @brief Detect a later rewrite, and put the snapshot back when there was none.
    /// @param copyLanded Whether the copy changed the clipboard at all.
    /// @param replaced Set to whether another process wrote after our copy.
    /// @return Whether the snapshot was restored in full; true when there was nothing to restore.
    bool settle(bool copyLanded, bool& replaced);

    /// @brief Put the snapshot back without looking for a rewrite; for a copy that never went out.
    bool restoreNow()
    {
        return port_.restore(snapshot_);
    }

private:
    ClipboardPort&    port_;
    ClipboardSnapshot snapshot_;
    unsigned long     sequenceBefore_ = 0;
    unsigned long     sequenceOfCopy_ = 0;
};

/**
 * @brief Borrows the clipboard to copy the current selection out of the foreground window.
 */
class SelectionTextGrabber : public QObject {
    Q_OBJECT
public:
    explicit SelectionTextGrabber(QObject* parent = nullptr);

    /**
     * @brief Copy the foreground application's selection and read it back.
     *
     * Waits for the injected copy to land by polling the clipboard sequence number until it
     * changes or kGrabDeadlineMs passes, driving a nested event loop so Windows messages —
     * and the mouse hook — keep flowing while it waits. Takes tens of milliseconds. The
     * combination pressed is the foreground process's own (see terminalCopyKey).
     *
     * @param policy What to do when another process rewrites the clipboard while the selection
     *               is being read: keep the selection, or drop it silently.
     * @return The selection, or the GrabStatus saying why there is none. The reason is logged
     *         too. ClipboardRewritten is returned when a rewrite happened under Silent; under
     *         Topmost the text comes back and the clipboard is left as the other process wrote it.
     * @note Never call this from the mouse hook callback: the wait would overrun the budget
     *       Windows gives a low-level hook. Re-entering it returns ClipboardBusy.
     */
    std::variant<QString, GrabStatus> grab(ClipboardPolicy policy);

private:
    std::unique_ptr<ClipboardPort> clipboard_;
    bool                           grabbing_ = false;
};

}
