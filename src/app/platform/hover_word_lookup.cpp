/**
 * @file hover_word_lookup.cpp
 * @brief The UI Automation read behind the hover lookup, and the shell that steps the scan's track.
 */

#include "hover_word_lookup.h"

#include <QFileInfo>

#include <chrono>
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

#include "capture/scan_pipeline.h"
#include "page_reader_scintilla.h"
#include "page_reader_uia.h"
#include "util/log.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max among them).
#include "uia_text_range_read.h"

namespace lens::app {
namespace {

/// Words walked before the lookup gives up. A paragraph is measured at about 2 ms a word.
constexpr int kMaxWords = 200;

/// @return The file name of the process's executable in lower case, or empty when it cannot be read.
QString processNameOf(DWORD const pid)
{
    std::unique_ptr<void, decltype(&CloseHandle)> process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), &CloseHandle};
    if (not process) return {};
    wchar_t path[MAX_PATH * 2]; // extended-length paths are longer than MAX_PATH
    DWORD   size = static_cast<DWORD>(std::size(path));
    if (not QueryFullProcessImageNameW(process.get(), 0, path, &size)) return {};
    return QFileInfo{QString::fromWCharArray(path, static_cast<int>(size))}.fileName().toLower();
}

/// The text pattern a hit element can read through, and how far up the tree it was found.
struct TextProvider {
    Com<IUIAutomationTextPattern> pattern;
    int                           levels = 0; ///< 0 when the hit element offers the pattern itself.
};

/// Climbs from @p start, at most kMaxLevels, to the first element that offers TextPattern.
TextProvider findTextProvider(IUIAutomationTreeWalker& walker, IUIAutomationElement& start)
{
    start.AddRef();
    Com<IUIAutomationElement> current{&start};
    TextProvider              provider;
    for (; current and provider.levels < kMaxLevels; ++provider.levels) {
        IUnknown* patternRaw = nullptr;
        if (SUCCEEDED(current->GetCurrentPattern(UIA_TextPatternId, &patternRaw)) and patternRaw) {
            Com<IUnknown>             pattern{patternRaw};
            IUIAutomationTextPattern* text = nullptr;
            if (SUCCEEDED(pattern->QueryInterface(IID_PPV_ARGS(&text))) and text) {
                provider.pattern.reset(text);
                return provider;
            }
        }
        IUIAutomationElement* parentRaw = nullptr;
        walker.GetParentElement(current.get(), &parentRaw);
        current.reset(parentRaw);
    }
    return {};
}

/// @return The first box of @p range that holds @p at, when one does.
std::optional<QRect> boxHolding(IUIAutomationTextRange& range, POINT const at)
{
    for (auto const& box : boxesOf(range))
        if (box.contains(QPoint{at.x, at.y})) return box;
    return std::nullopt;
}

/// @return The first word of @p scope whose box holds @p at, with that box.
std::optional<HoverWord> wordAt(IUIAutomationTextRange& scope, POINT const at)
{
    IUIAutomationTextRange* cursorRaw = nullptr;
    if (FAILED(scope.Clone(&cursorRaw)) or not cursorRaw) return std::nullopt;
    Com<IUIAutomationTextRange> cursor{cursorRaw};
    cursor->ExpandToEnclosingUnit(TextUnit_Word);

    for (int walked = 0; walked < kMaxWords; ++walked) {
        if (auto const box = boxHolding(*cursor, at)) {
            BSTR text = nullptr;
            if (FAILED(cursor->GetText(100, &text)) or not text) return std::nullopt;
            HoverWord word{QString::fromWCharArray(text, static_cast<int>(SysStringLen(text))), *box};
            SysFreeString(text);
            return word;
        }
        int moved = 0;
        if (FAILED(cursor->Move(TextUnit_Word, 1, &moved)) or moved == 0) break;
        cursor->ExpandToEnclosingUnit(TextUnit_Word);
        int order = 0;
        scope.CompareEndpoints(TextPatternRangeEndpoint_End, cursor.get(), TextPatternRangeEndpoint_Start, &order);
        if (order <= 0) break; // the scope ended before this word
    }
    return std::nullopt;
}

/// Steps of a track are this far apart.
constexpr auto kTrackPeriod = std::chrono::milliseconds{16};

}

QPoint hoverCursorPos()
{
    POINT pt{};
    GetCursorPos(&pt);
    return QPoint{pt.x, pt.y};
}

std::optional<HoverWord> hoverWordAt(QPoint const at, std::vector<std::string> const& whitelist)
{
    POINT const pt{at.x(), at.y()};
    HWND const  window = WindowFromPoint(pt);
    if (window == nullptr) return std::nullopt;

    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId()) return std::nullopt; // never read this program's own surfaces

    QString const process = processNameOf(pid);
    if (process.isEmpty() or not capture::allowedProcess(process.toStdString(), whitelist, false)) return std::nullopt;

    ComApartment apartment;

    IUIAutomation* automationRaw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automationRaw))) or not automationRaw)
        return std::nullopt;
    Com<IUIAutomation> automation{automationRaw};

    IUIAutomationElement* hitRaw = nullptr;
    if (FAILED(automation->ElementFromPoint(pt, &hitRaw)) or not hitRaw) return std::nullopt;
    Com<IUIAutomationElement> hit{hitRaw};

    BOOL password = FALSE;
    hit->get_CurrentIsPassword(&password);
    if (password) return std::nullopt;

    IUIAutomationTreeWalker* walkerRaw = nullptr;
    if (FAILED(automation->get_ControlViewWalker(&walkerRaw)) or not walkerRaw) return std::nullopt;
    Com<IUIAutomationTreeWalker> walker{walkerRaw};

    TextProvider const provider = findTextProvider(*walker, *hit);
    if (not provider.pattern) return std::nullopt;

    // The element's own range. An element that holds its text answers for it directly; a link inside the
    // page's text gets the slice of the page's text it spans, so its words are read in place.
    IUIAutomationTextRange* scopeRaw = nullptr;
    HRESULT const           scoped   = provider.levels == 0 ? provider.pattern->get_DocumentRange(&scopeRaw)
                                                            : provider.pattern->RangeFromChild(hit.get(), &scopeRaw);
    if (FAILED(scoped) or not scopeRaw) return std::nullopt;
    Com<IUIAutomationTextRange> scope{scopeRaw};

    return wordAt(*scope, pt);
}

void warmPageTreeOf(quintptr const window)
{
    page_reader_uia::warmTreeOf(window);
}

PageTrack::PageTrack(quintptr const window, QRect const band, Deliver deliver)
    : window_{window}, deliver_{std::move(deliver)}, band_{band}
{
    worker_ = std::jthread{[this](std::stop_token const& stop) { run(stop); }};
}

PageTrack::~PageTrack()
{
    worker_.request_stop();
}

void PageTrack::setBand(QRect const band)
{
    std::lock_guard const lock{guard_};
    band_ = band;
}

void PageTrack::run(std::stop_token const& stop)
{
    ComApartment apartment; // COM lives on this thread; the UIA reader and its pages answer to it
    // The reader is chosen by the window's own controls, not by the foreground gate: a Scintilla
    // editor carries its document in its own messages, every other window goes through UIA.
    auto reader = page_reader_scintilla::makeReader(window_);
    if (not reader) reader = std::make_unique<page_reader_uia::UiaPageReader>(window_);
    capture::TrackCore core{*reader};
    auto               next = std::chrono::steady_clock::now();
    // The read's wall-clock cost, logged once a second: a step that spikes names the name read or
    // the page search, and the first line's latency is measured from each page the track takes up
    // (docs/adr/0188). The track owns no clock of its own; the shell that steps it measures.
    auto   timingAt      = next;
    auto   trackEpoch    = next;
    bool   firstLineSeen = false;
    int    stepCount     = 0;
    double stepTotalMs   = 0.0;
    double stepMaxMs     = 0.0;
    while (not stop.stop_requested()) {
        capture::Band band;
        {
            std::lock_guard const lock{guard_};
            band = bandOf(band_);
        }
        auto const   stepStart = std::chrono::steady_clock::now();
        auto const   out       = core.step(band, stepStart);
        double const stepMs    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - stepStart).count();
        ++stepCount;
        stepTotalMs += stepMs;
        stepMaxMs = (std::max)(stepMaxMs, stepMs);
        if (stepStart - timingAt >= std::chrono::seconds{1}) {
            LENS_INFO("scan track timing, last second: {} step(s), avg {:.1f} ms, max {:.1f} ms",
                      stepCount,
                      stepCount > 0 ? stepTotalMs / stepCount : 0.0,
                      stepMaxMs);
            // The same second broken down by COM operation: present/text/search are the periodic
            // whole-tree reads, and their max names the spike the step line only shows (docs/adr/0188).
            auto const rt = page_reader_uia::drainReadTiming();
            if (rt.present.calls + rt.text.calls + rt.pageBox.calls + rt.wordAt.calls + rt.wordBox.calls + rt.read.calls + rt.step.calls + rt.search.calls + rt.visible.calls > 0)
                LENS_INFO("scan read cost, last second: present {} call(s) {:.0f} ms max {:.0f}, text {} {:.0f} max {:.0f}, search {} {:.0f} max {:.0f}, pageBox {} {:.0f}, wordAt {} {:.0f}, wordBox {} {:.0f}, read {} {:.0f}, step {} {:.0f}, visible {} {:.0f} max {:.0f}",
                          rt.present.calls,
                          rt.present.totalMs,
                          rt.present.maxMs,
                          rt.text.calls,
                          rt.text.totalMs,
                          rt.text.maxMs,
                          rt.search.calls,
                          rt.search.totalMs,
                          rt.search.maxMs,
                          rt.pageBox.calls,
                          rt.pageBox.totalMs,
                          rt.wordAt.calls,
                          rt.wordAt.totalMs,
                          rt.wordBox.calls,
                          rt.wordBox.totalMs,
                          rt.read.calls,
                          rt.read.totalMs,
                          rt.step.calls,
                          rt.step.totalMs,
                          rt.visible.calls,
                          rt.visible.totalMs,
                          rt.visible.maxMs);
            timingAt    = stepStart;
            stepCount   = 0;
            stepTotalMs = 0.0;
            stepMaxMs   = 0.0;
        }
        if (out.report) {
            auto const& report = *out.report;
            if (report.moves + report.refusals + report.losses + report.missedReads + report.framesSent + report.nameReads + report.pageSearches + report.sightChecks > 0)
                LENS_INFO("scan track, last second: {} anchor move(s), {} refused ({} moved), {} missed ({} empty/{} out), {} lost, {} span(s) held, {} frame(s) sent, {} name read(s), {} page search(es), {} sight check(s), visible y {}..{}",
                          report.moves,
                          report.refusals,
                          report.refusalsMoved,
                          report.missedReads,
                          report.missedEmpty,
                          report.missedOut,
                          report.losses,
                          report.spanCount,
                          report.framesSent,
                          report.nameReads,
                          report.pageSearches,
                          report.sightChecks,
                          report.visibleTop,
                          report.visibleBottom);
            if (not report.note.empty()) LENS_INFO("scan page {}", report.note);
        }
        if (out.frame) {
            if (out.frame->lost) {
                trackEpoch    = std::chrono::steady_clock::now();
                firstLineSeen = false;
            } else if (not firstLineSeen and not out.frame->visible.empty()) {
                firstLineSeen = true;
                LENS_INFO("scan first line after {:.0f} ms",
                          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - trackEpoch).count());
            }
            deliver_(*out.frame);
        }
        next += kTrackPeriod;
        auto const now = std::chrono::steady_clock::now();
        if (next < now) next = now; // a long step does not make the steps after it burst
        std::this_thread::sleep_until(next);
    }
}

}
