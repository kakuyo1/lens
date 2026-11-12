/** @file page_reader_scintilla.cpp
 * @brief The scan's page reader over a Scintilla editor, for page_reader_scintilla.h.
 */

#include "page_reader_scintilla.h"

#include <QPoint>
#include <QRect>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cwchar>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "capture/scintilla_words.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max among them).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace lens::app::page_reader_scintilla {
namespace {

/// Messages a Scintilla window answers; the numbers are the published Scintilla 5 ones.
/// SCI_GETTEXTRANGEFULL replaced SCI_GETTEXTRANGE(2162) in 5.2.3 for documents over 2 GiB.
/// Sending 2162 to a current editor reads the old 32-bit struct and stops the process, so it is
/// deliberately not defined here.
constexpr UINT        kGetLength          = 2006;
constexpr UINT        kGetTextRangeFull   = 2039;
constexpr UINT        kPositionFromPoint  = 2022;
constexpr UINT        kPointXFromPosition = 2164;
constexpr UINT        kPointYFromPosition = 2165;
constexpr UINT        kLineFromPosition   = 2166;
constexpr UINT        kTextHeight         = 2279;
constexpr UINT        kGetCodePage        = 2137;
constexpr int         kUtf8CodePage       = 65001;
constexpr std::size_t kMaxDocumentBytes   = 4u * 1024u * 1024u;
constexpr DWORD       kMessageTimeoutMs   = 120;

/// The shape Scintilla 5 expects for SCI_GETTEXTRANGEFULL. Sci_Position is ptrdiff_t on Win64.
struct TextRangeFull {
    std::ptrdiff_t cpMin;
    std::ptrdiff_t cpMax;
    char*          lpstrText;
};

/// @return The answer to a Scintilla message; nothing when the window did not answer in time.
/// @note Zero is a real answer (a line's first glyph is at x 0, an empty range has length 0), so
///       failure is the timeout and not the value.
std::optional<LRESULT> ask(HWND const editor, UINT const message, WPARAM const w = 0, LPARAM const l = 0)
{
    DWORD_PTR answer = 0;
    if (not SendMessageTimeoutW(editor, message, w, l, SMTO_ABORTIFHUNG, kMessageTimeoutMs, &answer)) return std::nullopt;
    return static_cast<LRESULT>(answer);
}

/// @return The answer, or zero when the window did not answer; for the reads that treat zero as "none".
LRESULT send(HWND const editor, UINT const message, WPARAM const w = 0, LPARAM const l = 0)
{
    return ask(editor, message, w, l).value_or(0);
}

QRect rectOf(HWND const window)
{
    RECT box{};
    if (not GetWindowRect(window, &box)) return {};
    return QRect{box.left, box.top, box.right - box.left, box.bottom - box.top};
}

/// @return The screen position of the editor's client origin.
QPoint clientOriginOf(HWND const editor)
{
    POINT at{0, 0};
    if (not ClientToScreen(editor, &at)) return {};
    return QPoint{at.x, at.y};
}

/// @brief The largest visible Scintilla child of @p window; Notepad++ owns one per editor view.
HWND scintillaChildOf(quintptr const window)
{
    struct Search {
        HWND best = nullptr;
        int  area = 0;
    } search;
    EnumChildWindows(reinterpret_cast<HWND>(window), [](HWND const child, LPARAM const raw) -> BOOL {
                         wchar_t name[64]{};
                         if (GetClassNameW(child, name, 64) == 0 or std::wcscmp(name, L"Scintilla") != 0) return TRUE;
                         if (not IsWindowVisible(child)) return TRUE;
                         auto* const found = reinterpret_cast<Search*>(raw);
                         RECT box{};
                         if (not GetWindowRect(child, &box)) return TRUE;
                         int const area = (box.right - box.left) * (box.bottom - box.top);
                         if (area > found->area) {
                             found->area = area;
                             found->best = child;
                         }
                         return TRUE; }, reinterpret_cast<LPARAM>(&search));
    return search.best;
}

/// @brief Memory in another process for one cross-process text read; freed on the way out.
class RemoteBuffer {
public:
    RemoteBuffer(HANDLE const process, std::size_t const bytes)
        : process_{process}, address_{VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT, PAGE_READWRITE)}, bytes_{bytes}
    {}
    ~RemoteBuffer()
    {
        if (address_) VirtualFreeEx(process_, address_, 0, MEM_RELEASE);
    }
    RemoteBuffer(RemoteBuffer const&)            = delete;
    RemoteBuffer& operator=(RemoteBuffer const&) = delete;

    void* get() const
    {
        return address_;
    }
    std::size_t size() const
    {
        return bytes_;
    }
    bool valid() const
    {
        return address_ != nullptr;
    }

private:
    HANDLE      process_;
    void*       address_;
    std::size_t bytes_;
};

/// @brief The whole document of @p editor, read through its own message across the process line.
/// @return Nothing when the editor is gone, too large, or did not answer.
std::optional<std::string> readDocument(HWND const editor)
{
    if (not IsWindow(editor)) return std::nullopt;
    auto const length = static_cast<std::ptrdiff_t>(send(editor, kGetLength));
    if (length <= 0 or static_cast<std::size_t>(length) > kMaxDocumentBytes) return std::nullopt;

    DWORD pid = 0;
    GetWindowThreadProcessId(editor, &pid);
    if (pid == 0) return std::nullopt;
    HANDLE const process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (not process) return std::nullopt;
    struct Closer {
        HANDLE process;
        ~Closer()
        {
            CloseHandle(process);
        }
    } closer{process};

    std::size_t const bytes = static_cast<std::size_t>(length) + 1;
    RemoteBuffer      text{process, bytes};
    RemoteBuffer      range{process, sizeof(TextRangeFull)};
    if (not text.valid() or not range.valid()) return std::nullopt;

    TextRangeFull const request{0, length, static_cast<char*>(text.get())};
    SIZE_T              written = 0;
    if (not WriteProcessMemory(process, range.get(), &request, sizeof(request), &written) or written != sizeof(request))
        return std::nullopt;
    auto const copied = send(editor, kGetTextRangeFull, 0, reinterpret_cast<LPARAM>(range.get()));
    if (copied <= 0) return std::nullopt;

    std::string out(static_cast<std::size_t>(copied), '\0');
    SIZE_T      read = 0;
    if (not ReadProcessMemory(process, text.get(), out.data(), out.size(), &read) or read != out.size()) return std::nullopt;
    return out;
}

/// @return The document as UTF-8. Scintilla stores UTF-8 when the code page says so; other code
///         pages still leave ASCII words byte-aligned, which is all the word splitter reads.
std::string utf8Of(std::string const& raw, int const codePage)
{
    if (codePage == kUtf8CodePage or codePage == 0) return raw;
    return QString::fromLocal8Bit(raw.data(), static_cast<int>(raw.size())).toUtf8().toStdString();
}

/// @brief One word of a Scintilla document: its place in the page's unit table, and the generation
///        of the text that table was built from.
class ScintillaWord final : public capture::Word {
public:
    ScintillaWord(std::size_t const index, std::uint64_t const generation)
        : index_{index}, generation_{generation}
    {}

    std::size_t index() const
    {
        return index_;
    }
    std::uint64_t generation() const
    {
        return generation_;
    }

private:
    std::size_t   index_;
    std::uint64_t generation_;
};

/// @brief One document of a Scintilla editor, read once and walked by byte offsets.
class ScintillaPage final : public capture::Page {
public:
    ScintillaPage(HWND const editor, std::string text, int const codePage)
        : editor_{editor}, text_{std::move(text)}, codePage_{codePage},
          units_{capture::scintillaWordUnits(text_)}, generation_{nextGeneration()}
    {}

    bool                             present() const override;
    capture::Band                    box() const override;
    std::string                      text() const override;
    std::shared_ptr<capture::Word>   wordAt(int x, int y) override;
    std::optional<capture::Band>     box(capture::Word const& word) override;
    std::optional<capture::WordRead> read(capture::Word const& word) override;
    std::shared_ptr<capture::Word>   step(capture::Word const& word, int delta) override;

private:
    /// @return A generation no earlier page used, so a handle from one can never name another's word.
    static std::uint64_t nextGeneration();
    /// @return The unit a word handle names; nothing when the handle is from another generation.
    capture::ScintillaWordUnit const* unitOf(capture::Word const& word) const;
    /// @return The screen box of a byte range, or nothing when it wraps, is empty or failed to answer.
    std::optional<capture::Band> boxOf(std::size_t start, std::size_t end) const;
    /// @return The screen x of a byte offset; nothing when the editor did not answer.
    std::optional<int> xOf(std::size_t offset) const;

    HWND                                    editor_;
    std::string                             text_;
    int                                     codePage_;
    std::vector<capture::ScintillaWordUnit> units_;
    std::uint64_t                           generation_;
};

std::uint64_t ScintillaPage::nextGeneration()
{
    static std::atomic<std::uint64_t> last{0};
    return ++last;
}

capture::ScintillaWordUnit const* ScintillaPage::unitOf(capture::Word const& word) const
{
    auto const& held = static_cast<ScintillaWord const&>(word);
    if (held.generation() != generation_ or held.index() >= units_.size()) return nullptr;
    return &units_[held.index()];
}

std::optional<int> ScintillaPage::xOf(std::size_t const offset) const
{
    auto const x = ask(editor_, kPointXFromPosition, 0, static_cast<LPARAM>(offset));
    if (not x) return std::nullopt;
    return clientOriginOf(editor_).x() + static_cast<int>(*x);
}

std::optional<capture::Band> ScintillaPage::boxOf(std::size_t const start, std::size_t const end) const
{
    if (end <= start or not IsWindow(editor_)) return std::nullopt;
    auto const left  = xOf(start);
    auto const right = xOf(end);
    if (not left or not right or *right <= *left) return std::nullopt;
    auto const y    = ask(editor_, kPointYFromPosition, 0, static_cast<LPARAM>(start));
    auto const endY = ask(editor_, kPointYFromPosition, 0, static_cast<LPARAM>(end));
    if (not y or not endY or *y != *endY) return std::nullopt; // a wrapped word has no one box
    auto const line = ask(editor_, kLineFromPosition, static_cast<WPARAM>(start), 0);
    if (not line) return std::nullopt;
    auto const height = ask(editor_, kTextHeight, static_cast<WPARAM>(*line), 0);
    if (not height or *height <= 0) return std::nullopt;
    return capture::Band{.x = *left, .y = clientOriginOf(editor_).y() + static_cast<int>(*y), .width = *right - *left, .height = static_cast<int>(*height)};
}

bool ScintillaPage::present() const
{
    if (not IsWindow(editor_)) return false;
    auto const current = readDocument(editor_);
    if (not current) return false;
    // A document that changed under the track is a new page: the word handles and the names from
    // the old one are stale. Say so; TrackCore drops the page and acquires again.
    return *current == text_;
}

capture::Band ScintillaPage::box() const
{
    auto const box = rectOf(editor_);
    return capture::Band{.x = box.x(), .y = box.y(), .width = box.width(), .height = box.height()};
}

std::string ScintillaPage::text() const
{
    return utf8Of(text_, codePage_);
}

std::shared_ptr<capture::Word> ScintillaPage::wordAt(int const x, int const y)
{
    if (not IsWindow(editor_) or units_.empty()) return nullptr;
    auto const origin   = clientOriginOf(editor_);
    auto const position = ask(editor_, kPositionFromPoint, static_cast<WPARAM>(x - origin.x()), static_cast<LPARAM>(y - origin.y()));
    if (not position or *position < 0) return nullptr;
    auto const at = static_cast<std::size_t>(*position);

    // The units are in reading order, so the one at or before the point is found without a walk.
    // A point in the space after a word lands on the next unit, which is the word a reader would
    // say they pointed at -- the same answer UI Automation's RangeFromPoint gives.
    auto const  after = std::upper_bound(units_.begin(), units_.end(), at, [](std::size_t const pos, capture::ScintillaWordUnit const& unit) {
        return pos < unit.start;
    });
    std::size_t index = after == units_.begin() ? 0 : static_cast<std::size_t>(after - units_.begin()) - 1;
    return std::make_shared<ScintillaWord>(index, generation_);
}

std::optional<capture::Band> ScintillaPage::box(capture::Word const& word)
{
    auto const* const unit = unitOf(word);
    if (not unit) return std::nullopt;
    return boxOf(unit->start, unit->end);
}

std::optional<capture::WordRead> ScintillaPage::read(capture::Word const& word)
{
    auto const* const unit = unitOf(word);
    if (not unit) return std::nullopt;
    auto const full = boxOf(unit->start, unit->end);
    if (not full or unit->start >= text_.size() or unit->end > text_.size()) return std::nullopt;
    std::optional<capture::Band> lettersEnd;
    if (unit->lettersEnd < unit->end) {
        if (auto const x = xOf(unit->lettersEnd); x and *x > full->x and *x < full->x + full->width)
            lettersEnd = capture::Band{.x = *x, .y = full->y, .width = 0, .height = full->height};
    }
    return capture::WordRead{.text       = text_.substr(unit->start, unit->end - unit->start),
                             .fullBox    = *full,
                             .lettersEnd = lettersEnd};
}

std::shared_ptr<capture::Word> ScintillaPage::step(capture::Word const& word, int const delta)
{
    auto const& held = static_cast<ScintillaWord const&>(word);
    if (held.generation() != generation_) return nullptr;
    auto const next = static_cast<std::int64_t>(held.index()) + delta;
    if (next < 0 or next >= static_cast<std::int64_t>(units_.size())) return nullptr;
    return std::make_shared<ScintillaWord>(static_cast<std::size_t>(next), generation_);
}

} // namespace

ScintillaPageReader::ScintillaPageReader(quintptr const window)
    : window_{window}
{}

ScintillaPageReader::~ScintillaPageReader() = default;

std::shared_ptr<capture::Page> ScintillaPageReader::acquire(capture::Band const& band)
{
    if (page_ and page_->present()) return page_;
    page_.reset();
    auto const editor = scintillaChildOf(window_);
    if (not editor) return nullptr;
    auto const raw = readDocument(editor);
    if (not raw or raw->empty()) return nullptr;
    auto const codePage = static_cast<int>(send(editor, kGetCodePage));
    auto const box      = rectOf(editor);
    // An editor that covers none of the window the scan was placed on is not the page on screen.
    if (not box.intersects(QRect{band.x, band.y, band.width, band.height})) return nullptr;
    page_ = std::make_shared<ScintillaPage>(editor, *raw, codePage);
    return page_;
}

void ScintillaPageReader::drop()
{
    page_.reset();
}

std::string ScintillaPageReader::anyText()
{
    auto const editor = scintillaChildOf(window_);
    if (not editor) return {};
    auto const raw = readDocument(editor);
    if (not raw) return {};
    return utf8Of(*raw, static_cast<int>(send(editor, kGetCodePage)));
}

std::unique_ptr<capture::PageReader> makeReader(quintptr const window)
{
    if (not scintillaChildOf(window)) return nullptr;
    return std::make_unique<ScintillaPageReader>(window);
}

}
