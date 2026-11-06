/**
 * @file hover_word_lookup.cpp
 * @brief The UI Automation read behind the hover lookup.
 */

#include "hover_word_lookup.h"

#include <QFileInfo>

#include <chrono>
#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#include "capture/page_alignment.h"
#include "capture/scan_pipeline.h"
#include "capture/word_track.h"
#include "util/log.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max among them).
#include <windows.h>
#include <UIAutomation.h>
#include <oleauto.h>

namespace lens::app {
namespace {

/// Levels climbed from the hit element to the text provider. Twitter's page needs 11; the cap keeps
/// a deeper tree from costing more than the dwell that started the lookup.
constexpr int kMaxLevels = 50;
/// Words walked before the lookup gives up. A paragraph is measured at about 2 ms a word.
constexpr int kMaxWords = 200;

/// Releases a COM interface when the holder goes out of scope.
struct ComRelease {
    template <class T>
    void operator()(T* const object) const
    {
        if (object) object->Release();
    }
};

template <class T>
using Com = std::unique_ptr<T, ComRelease>;

/// Initialises COM for the calling thread, and undoes it only when this object was the one to do it.
class ComApartment {
public:
    ComApartment()
        : owned_(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
    {}
    ~ComApartment()
    {
        if (owned_) CoUninitialize();
    }
    ComApartment(ComApartment const&)            = delete;
    ComApartment& operator=(ComApartment const&) = delete;

private:
    bool owned_;
};

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

/// @return Every box of @p range in the order UI Automation gives them; empty when the range has none.
std::vector<QRect> boxesOf(IUIAutomationTextRange& range)
{
    SAFEARRAY* rects = nullptr;
    if (FAILED(range.GetBoundingRectangles(&rects)) or not rects) return {};

    std::vector<QRect> boxes;
    long               lo = 0, hi = -1;
    SafeArrayGetLBound(rects, 1, &lo);
    SafeArrayGetUBound(rects, 1, &hi);
    // Each box is four doubles: x, y, width, height.
    for (long i = lo; i + 3 <= hi; i += 4) {
        double v[4] = {};
        for (long k = 0; k < 4; ++k) {
            long idx = i + k;
            SafeArrayGetElement(rects, &idx, &v[k]);
        }
        boxes.push_back(QRect{static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]), static_cast<int>(v[3])});
    }
    SafeArrayDestroy(rects);
    return boxes;
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

namespace {

Com<IUIAutomation> newAutomation()
{
    IUIAutomation* raw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw))) or not raw) return {};
    return Com<IUIAutomation>{raw};
}

Com<IUIAutomationElement> elementOf(IUIAutomation& automation, quintptr const window)
{
    IUIAutomationElement* raw = nullptr;
    if (FAILED(automation.ElementFromHandle(reinterpret_cast<HWND>(window), &raw))) return {};
    return Com<IUIAutomationElement>{raw};
}

/// The page's document as pageTextOf picks it: the largest document of the window, with its box.
struct PageDocument {
    Com<IUIAutomationElement>     element;
    Com<IUIAutomationTextPattern> pattern;
    RECT                          box{};
    /// The intersection of the document frames above the page. A PDF viewer's document covers its own toolbar,
    /// but the frame it sits in begins below that toolbar, so the frame is what the reader can see.
    std::optional<QRect> clip;
    int                  depth = 0; ///< How many elements lie above the page in the tree.
};

/// @return How many elements lie above @p element in the tree, capped at kMaxLevels.
int depthOf(IUIAutomation& automation, IUIAutomationElement& element)
{
    IUIAutomationTreeWalker* walkerRaw = nullptr;
    if (FAILED(automation.get_ControlViewWalker(&walkerRaw)) or not walkerRaw) return 0;
    Com<IUIAutomationTreeWalker> walker{walkerRaw};

    int depth = 0;
    element.AddRef();
    Com<IUIAutomationElement> current{&element};
    for (; depth < kMaxLevels; ++depth) {
        IUIAutomationElement* parentRaw = nullptr;
        if (FAILED(walker->GetParentElement(current.get(), &parentRaw)) or not parentRaw) break;
        current.reset(parentRaw);
    }
    return depth;
}

/// @return The intersection of the boxes of the document controls above @p element; nothing when there are none.
std::optional<QRect> documentClipOf(IUIAutomation& automation, IUIAutomationElement& element)
{
    IUIAutomationTreeWalker* walkerRaw = nullptr;
    if (FAILED(automation.get_ControlViewWalker(&walkerRaw)) or not walkerRaw) return std::nullopt;
    Com<IUIAutomationTreeWalker> walker{walkerRaw};

    std::optional<QRect> clip;
    element.AddRef();
    Com<IUIAutomationElement> current{&element};
    for (int level = 0; level < kMaxLevels; ++level) {
        IUIAutomationElement* parentRaw = nullptr;
        if (FAILED(walker->GetParentElement(current.get(), &parentRaw)) or not parentRaw) break;
        current.reset(parentRaw);
        CONTROLTYPEID type = 0;
        RECT          box{};
        if (FAILED(current->get_CurrentControlType(&type)) or type != UIA_DocumentControlTypeId) continue;
        if (FAILED(current->get_CurrentBoundingRectangle(&box))) continue;
        QRect const frame{box.left, box.top, box.right - box.left, box.bottom - box.top};
        clip = clip ? clip->intersected(frame) : frame;
    }
    return clip;
}

PageDocument pageOf(IUIAutomation& automation, IUIAutomationElement& root, QRect const clip)
{
    // Only document controls: a browser's page is one, its tabs and toolbars are not.
    VARIANT documentType{};
    documentType.vt                      = VT_I4;
    documentType.lVal                    = UIA_DocumentControlTypeId;
    IUIAutomationCondition* conditionRaw = nullptr;
    if (FAILED(automation.CreatePropertyCondition(UIA_ControlTypePropertyId, documentType, &conditionRaw)) or not conditionRaw) return {};
    Com<IUIAutomationCondition> documents{conditionRaw};

    IUIAutomationElementArray* arrayRaw = nullptr;
    if (FAILED(root.FindAll(TreeScope_Descendants, documents.get(), &arrayRaw)) or not arrayRaw) return {};
    Com<IUIAutomationElementArray> array{arrayRaw};
    int                            count = 0;
    array->get_Length(&count);

    // A document's effective box is its own box cut to the document frames above it, and to @p clip when one is given.
    // The page is the innermost document that still covers most of the largest one: an embedded viewer (a PDF) sits
    // inside the page that holds it, and that page's box takes in the viewer's toolbar. A small frame (an ad) does not qualify.
    struct Candidate {
        Com<IUIAutomationElement>     element;
        Com<IUIAutomationTextPattern> pattern;
        RECT                          box{};
        std::optional<QRect>          clip;
        LONGLONG                      area  = 0;
        int                           depth = 0;
    };
    std::vector<Candidate> candidates;
    LONGLONG               largest = 0;
    for (int i = 0; i < count; ++i) {
        IUIAutomationElement* elementRaw = nullptr;
        if (FAILED(array->GetElement(i, &elementRaw)) or not elementRaw) continue;
        Candidate candidate;
        candidate.element.reset(elementRaw);
        if (FAILED(candidate.element->get_CurrentBoundingRectangle(&candidate.box))) continue;
        IUnknown* patternRaw = nullptr;
        if (FAILED(candidate.element->GetCurrentPattern(UIA_TextPatternId, &patternRaw)) or not patternRaw) continue;
        Com<IUnknown>             pattern{patternRaw};
        IUIAutomationTextPattern* textRaw = nullptr;
        if (FAILED(pattern->QueryInterface(IID_PPV_ARGS(&textRaw))) or not textRaw) continue;
        candidate.pattern.reset(textRaw);
        QRect const box{candidate.box.left, candidate.box.top, candidate.box.right - candidate.box.left, candidate.box.bottom - candidate.box.top};
        candidate.clip  = documentClipOf(automation, *candidate.element);
        QRect effective = candidate.clip ? box.intersected(*candidate.clip) : box;
        if (not clip.isEmpty()) effective = effective.intersected(clip);
        candidate.area  = static_cast<LONGLONG>(effective.width()) * effective.height();
        candidate.depth = depthOf(automation, *candidate.element);
        largest         = (std::max)(largest, candidate.area);
        candidates.push_back(std::move(candidate));
    }

    // shortcut: a PDF viewer lays its whole file out as one document, many screens tall and scrolled above the window;
    // its words are the same as the visible page's, but its box is not where they are. First pass skips such documents,
    // the second pass keeps the rule as it was when only such documents are left. Revisit if a long article trips it.
    RECT windowBox{};
    root.get_CurrentBoundingRectangle(&windowBox);
    constexpr int kVirtualDocumentShare = 4;
    int const     windowHeight          = windowBox.bottom - windowBox.top;
    auto const    virtualDocument       = [&](Candidate const& candidate) {
        return windowHeight > 0 and candidate.box.top < windowBox.top and candidate.box.bottom - candidate.box.top > kVirtualDocumentShare * windowHeight;
    };
    for (bool const allowVirtual : {false, true}) {
        PageDocument page;
        LONGLONG     chosenArea = -1;
        for (auto& candidate : candidates) {
            if (candidate.area <= 0 or candidate.area * 4 < largest) continue;
            if (not allowVirtual and virtualDocument(candidate)) continue;
            if (page.element and (candidate.depth < page.depth or (candidate.depth == page.depth and candidate.area <= chosenArea))) continue;
            page.element = std::move(candidate.element);
            page.pattern = std::move(candidate.pattern);
            page.box     = candidate.box;
            page.clip    = candidate.clip;
            page.depth   = candidate.depth;
            chosenArea   = candidate.area;
        }
        if (page.element) return page;
    }
    return {};
}

/// The page of the window last read, kept between reads: finding it searches the whole window tree, which costs
/// about as much as a full walk, and a probe runs several times a second.
struct PageCache {
    std::mutex                    guard;
    quintptr                      window = 0;
    std::shared_ptr<PageDocument> document;
};

PageCache& pageCache()
{
    static PageCache cache;
    return cache;
}

/// @return The page of @p window: the cached one while the window is the same, else a fresh search; null when there is none.
std::shared_ptr<PageDocument> pageOfWindow(IUIAutomation& automation, quintptr const window, QRect const clip)
{
    auto& cache = pageCache();
    {
        std::lock_guard const lock{cache.guard};
        if (cache.window == window and cache.document) return cache.document;
    }
    auto const root = elementOf(automation, window);
    if (not root) return nullptr;
    auto page = pageOf(automation, *root, clip);
    if (not page.pattern) return nullptr;
    auto                  document = std::make_shared<PageDocument>(std::move(page));
    std::lock_guard const lock{cache.guard};
    cache.window   = window;
    cache.document = document;
    return document;
}

/// Drops the cached page of @p window, so the next read searches for it again.
void forgetPageOf(quintptr const window)
{
    auto&                 cache = pageCache();
    std::lock_guard const lock{cache.guard};
    if (cache.window == window) cache.document.reset();
}

/// @return The page's box as it stands now, in physical screen pixels; empty when the element cannot say.
QRect currentBoxOf(PageDocument const& page)
{
    RECT box{};
    if (FAILED(page.element->get_CurrentBoundingRectangle(&box))) return {};
    QRect const rect{box.left, box.top, box.right - box.left, box.bottom - box.top};
    return page.clip ? rect.intersected(*page.clip) : rect;
}

/// @return The word @p range holds, spelled as the page has it; empty when it cannot be read.
QString wordTextOf(IUIAutomationTextRange& range)
{
    BSTR text = nullptr;
    if (FAILED(range.GetText(64, &text)) or not text) return {};
    QString const word = QString::fromWCharArray(text, static_cast<int>(SysStringLen(text)));
    SysFreeString(text);
    return word;
}

/// @return The word under the middle of @p visible, where a read starts; null when the page cannot answer there.
Com<IUIAutomationTextRange> middleWordOf(IUIAutomationTextPattern& page, QRect const visible)
{
    POINT const             middle{visible.center().x(), visible.center().y()};
    IUIAutomationTextRange* raw = nullptr;
    if (FAILED(page.RangeFromPoint(middle, &raw)) or not raw) return {};
    Com<IUIAutomationTextRange> word{raw};
    word->ExpandToEnclosingUnit(TextUnit_Word);
    return word;
}

}

QString pageTextOf(quintptr const window)
{
    ComApartment apartment;
    auto const   automation = newAutomation();
    if (not automation) return {};
    auto const root = elementOf(*automation, window);
    if (not root) return {};
    auto const page = pageOf(*automation, *root, QRect{});
    if (not page.pattern) return {};

    IUIAutomationTextRange* rangeRaw = nullptr;
    if (FAILED(page.pattern->get_DocumentRange(&rangeRaw)) or not rangeRaw) return {};
    Com<IUIAutomationTextRange> range{rangeRaw};
    BSTR                        body = nullptr;
    if (FAILED(range->GetText(-1, &body))) return {};
    QString const text = QString::fromWCharArray(body, static_cast<int>(SysStringLen(body)));
    SysFreeString(body);
    return text;
}

namespace {

/// Words one track step reads at most while the anchor holds still; short steps keep the anchor read every frame or two.
constexpr int kTrackChunkWords = 12;
/// Steps the anchor must hold still before the page's names are re-read; the read costs a few hundred ms.
constexpr int kStillSteps = 8;
/// How often the names of the page are re-read once they are known; the page's whole text costs about 300 ms.
constexpr auto kNameRefresh = std::chrono::seconds{5};
/// The wait before the names are asked for again while they are still unknown.
constexpr auto kNameRetry = std::chrono::seconds{1};
/// Steps of a track are this far apart.
constexpr auto kTrackPeriod = std::chrono::milliseconds{16};
/// Steps the anchor may read empty or outside the band before the track gives it up; a read that races a scroll is brief.
constexpr int kMissedSteps = 6;
/// The anchor moves once it comes within this share of the band's height of the band's top or bottom edge.
constexpr int kEdgeShare = 5;

/// @return The first box of @p range, or an empty one when it has none.
QRect firstBoxOf(IUIAutomationTextRange& range)
{
    auto const boxes = boxesOf(range);
    return boxes.empty() ? QRect{} : boxes.front();
}

/// @return A second handle on @p range's position, so a walk can move one handle without moving the other.
Com<IUIAutomationTextRange> cloneOf(IUIAutomationTextRange& range)
{
    IUIAutomationTextRange* raw = nullptr;
    if (FAILED(range.Clone(&raw)) or not raw) return {};
    return Com<IUIAutomationTextRange>{raw};
}

/// @return The band in the form the placement arithmetic takes.
capture::Band bandOf(QRect const band)
{
    return {.x = band.x(), .y = band.y(), .width = band.width(), .height = band.height()};
}

/// @return The placed words in the form the track reports them.
std::vector<TrackedWord> trackedOf(std::vector<capture::PlacedSpan> const& placed)
{
    std::vector<TrackedWord> words;
    words.reserve(placed.size());
    for (auto const& span : placed)
        words.push_back({.id = span.id, .box = QRect{span.x, span.y, span.width, span.height}});
    return words;
}

/// What one track keeps for its page. Only its worker thread touches it, so every COM object here stays on that thread.
class TrackState {
public:
    explicit TrackState(quintptr const window)
        : window_{window}
    {}

    /// @return What this step reports; nothing when the step changed nothing.
    std::optional<TrackFrame> step(QRect band);

private:
    std::optional<TrackFrame> acquire(QRect band);
    std::optional<TrackFrame> lose();
    /// @return Whether the anchor moved to a word nearer the middle of @p band.
    bool moveAnchorIfNearEdge(QRect band);
    /// @return Whether words past the last one read forward were added.
    bool extendForward(QRect band, TrackFrame& frame);
    /// @return Whether words past the first one read backward were added.
    bool extendBackward(QRect band, TrackFrame& frame);
    void refreshNamesIfDue();
    /// Names are not words to learn: the page writes them capitalized throughout.
    bool isName(QString const& text) const;

    quintptr                              window_;
    Com<IUIAutomation>                    automation_;
    std::shared_ptr<PageDocument>         page_;
    Com<IUIAutomationTextRange>           anchor_;
    QRect                                 anchorBox_;
    std::vector<capture::TrackedSpan>     spans_; ///< In reading order, so the words past either end are the next to read.
    Com<IUIAutomationTextRange>           forward_;
    Com<IUIAutomationTextRange>           backward_;
    bool                                  forwardDone_  = false;
    bool                                  backwardDone_ = false;
    std::unordered_set<std::string>       names_;
    bool                                  namesLoaded_ = false;
    std::chrono::steady_clock::time_point namesAt_{};
    int                                   stillSteps_ = 0;
    int                                   nextId_     = 0;
    // Diagnostic counters for the scroll test, one summary line a second. TEMPORARY: remove with the probe.
    int                                   moves_    = 0;
    int                                   refusals_ = 0;
    int                                   losses_   = 0;
    int                                   missed_   = 0;
    std::chrono::steady_clock::time_point reportedAt_{};
    int                                   missedSteps_ = 0; ///< Steps in a row the anchor has read empty or outside the band.
    int                                   framesSent_  = 0; ///< TEMPORARY diagnostic: frames returned in the last second.
};

std::optional<TrackFrame> TrackState::step(QRect const band)
{
    if (not automation_) automation_ = newAutomation();
    if (not automation_) return std::nullopt;
    if (not anchor_) return acquire(band);
    // The window's box takes in the browser's tabs, address bar and bookmarks, which are not the page: the page's own box clips.
    QRect const visible = page_ ? band.intersected(currentBoxOf(*page_)) : band;

    auto const now = std::chrono::steady_clock::now();
    if (reportedAt_ == std::chrono::steady_clock::time_point{}) reportedAt_ = now;
    if (now - reportedAt_ >= std::chrono::seconds{1}) {
        if (moves_ + refusals_ + losses_ + missed_ + framesSent_ > 0)
            LENS_INFO("scan track, last second: {} anchor move(s), {} refused, {} missed read(s), {} lost, {} span(s) held, {} frame(s) sent, visible y {}..{}", moves_, refusals_, missed_, losses_, spans_.size(), framesSent_, visible.top(), visible.bottom());
        moves_ = refusals_ = losses_ = missed_ = framesSent_ = 0;
        reportedAt_                                          = now;
    }

    QRect const box = firstBoxOf(*anchor_);
    if (box.isEmpty() or not visible.contains(box.center())) {
        // A read that races a scroll comes back empty for a step or two. The lines keep their last place until it does
        // (the page may have moved on, so they can sit a little off their words), rather than vanishing and rebuilding.
        ++missed_;
        if (++missedSteps_ < kMissedSteps) return std::nullopt;
        return lose();
    }
    missedSteps_ = 0;

    bool moved = false;
    if (box != anchorBox_) {
        anchorBox_  = box;
        stillSteps_ = 0;
        moved       = true;
        moveAnchorIfNearEdge(visible);
    } else {
        ++stillSteps_;
    }

    TrackFrame frame;
    // Words are read while the page moves too: a chunk is kept only when the anchor did not move during it.
    // The names are re-read only once the page holds still, since that read is a few hundred ms of COM.
    if (stillSteps_ >= kStillSteps) refreshNamesIfDue();
    bool const extended = extendForward(visible, frame) or extendBackward(visible, frame);
    if (not moved and not extended) return std::nullopt;
    frame.visible = trackedOf(capture::placeSpans(spans_, anchorBox_.x(), anchorBox_.y(), bandOf(visible)));
    ++framesSent_;
    return frame;
}

std::optional<TrackFrame> TrackState::acquire(QRect const band)
{
    refreshNamesIfDue();
    // The names decide which words are words to learn, so nothing is read before they are known.
    if (not namesLoaded_) return std::nullopt;
    page_ = pageOfWindow(*automation_, window_, band);
    if (not page_) return std::nullopt;
    QRect const visible = band.intersected(currentBoxOf(*page_));
    if (visible.isEmpty()) return std::nullopt;
    auto anchor = middleWordOf(*page_->pattern, visible);
    if (not anchor) {
        forgetPageOf(window_);
        page_.reset();
        return std::nullopt;
    }
    QRect const box = firstBoxOf(*anchor);
    if (box.isEmpty()) return std::nullopt;

    anchor_       = std::move(anchor);
    anchorBox_    = box;
    forward_      = cloneOf(*anchor_);
    backward_     = cloneOf(*anchor_);
    forwardDone_  = false;
    backwardDone_ = false;
    stillSteps_   = 0;
    spans_.assign(1, {.id = nextId_++, .dx = 0, .dy = 0, .width = box.width(), .height = box.height()});

    TrackFrame    frame;
    QString const text = wordTextOf(*anchor_);
    if (not isName(text)) frame.added.push_back({.id = spans_.front().id, .text = text});
    frame.visible = trackedOf(capture::placeSpans(spans_, anchorBox_.x(), anchorBox_.y(), bandOf(band)));
    return frame;
}

std::optional<TrackFrame> TrackState::lose()
{
    ++losses_;
    missedSteps_ = 0;
    anchor_.reset();
    forward_.reset();
    backward_.reset();
    spans_.clear();
    forwardDone_  = false;
    backwardDone_ = false;
    stillSteps_   = 0;
    TrackFrame frame;
    frame.lost = true;
    return frame;
}

bool TrackState::moveAnchorIfNearEdge(QRect const band)
{
    int const margin = band.height() / kEdgeShare;
    int const centre = anchorBox_.center().y();
    if (centre - band.top() >= margin and band.bottom() - centre >= margin) return false;
    QRect const visible = band.intersected(currentBoxOf(*page_));
    if (visible.isEmpty()) return false;
    auto middle = middleWordOf(*page_->pattern, visible);
    if (not middle) {
        ++refusals_;
        return false;
    }
    QRect const box = firstBoxOf(*middle);
    // The old anchor must not have moved while the new one was found: both boxes are taken at one scroll position.
    if (box.isEmpty() or firstBoxOf(*anchor_) != anchorBox_) {
        ++refusals_;
        return false;
    }
    capture::rebaseSpans(spans_, box.x() - anchorBox_.x(), box.y() - anchorBox_.y());
    anchor_    = std::move(middle);
    anchorBox_ = box;
    ++moves_;
    return true;
}

bool TrackState::extendForward(QRect const band, TrackFrame& frame)
{
    if (forwardDone_ or not forward_ or spans_.empty()) return false;
    auto const& last = spans_.back();
    if (anchorBox_.y() + last.dy + last.height >= band.bottom()) return false;

    auto                              cursor = cloneOf(*forward_);
    std::vector<capture::TrackedSpan> fresh;
    std::vector<ReadWord>             words;
    bool                              ended = false;
    for (int walked = 0; walked < kTrackChunkWords; ++walked) {
        auto next  = cloneOf(*cursor);
        int  moved = 0;
        if (not next or FAILED(next->Move(TextUnit_Word, 1, &moved)) or moved == 0) {
            ended = true;
            break;
        }
        next->ExpandToEnclosingUnit(TextUnit_Word);
        QRect const box = firstBoxOf(*next);
        if (not box.isEmpty()) {
            if (box.top() > band.bottom()) break;
            QString const text = wordTextOf(*next);
            fresh.push_back({.id = nextId_++, .dx = box.x() - anchorBox_.x(), .dy = box.y() - anchorBox_.y(), .width = box.width(), .height = box.height()});
            if (not isName(text)) words.push_back({.id = fresh.back().id, .text = text});
        }
        cursor = std::move(next);
    }
    // The anchor must not have moved during the read: these words would then sit at two scroll positions.
    if (firstBoxOf(*anchor_) != anchorBox_) return false;
    spans_.insert(spans_.end(), fresh.begin(), fresh.end());
    forward_ = std::move(cursor);
    if (ended) forwardDone_ = true;
    frame.added.insert(frame.added.end(), words.begin(), words.end());
    return not fresh.empty();
}

bool TrackState::extendBackward(QRect const band, TrackFrame& frame)
{
    if (backwardDone_ or not backward_ or spans_.empty()) return false;
    auto const& first = spans_.front();
    if (anchorBox_.y() + first.dy <= band.top()) return false;

    auto                              cursor = cloneOf(*backward_);
    std::vector<capture::TrackedSpan> fresh; // nearest first
    std::vector<ReadWord>             words;
    bool                              ended = false;
    for (int walked = 0; walked < kTrackChunkWords; ++walked) {
        auto next  = cloneOf(*cursor);
        int  moved = 0;
        if (not next or FAILED(next->Move(TextUnit_Word, -1, &moved)) or moved == 0) {
            ended = true;
            break;
        }
        next->ExpandToEnclosingUnit(TextUnit_Word);
        QRect const box = firstBoxOf(*next);
        if (not box.isEmpty()) {
            if (box.bottom() < band.top()) break;
            QString const text = wordTextOf(*next);
            fresh.push_back({.id = nextId_++, .dx = box.x() - anchorBox_.x(), .dy = box.y() - anchorBox_.y(), .width = box.width(), .height = box.height()});
            if (not isName(text)) words.push_back({.id = fresh.back().id, .text = text});
        }
        cursor = std::move(next);
    }
    if (firstBoxOf(*anchor_) != anchorBox_) return false;
    spans_.insert(spans_.begin(), fresh.rbegin(), fresh.rend());
    backward_ = std::move(cursor);
    if (ended) backwardDone_ = true;
    frame.added.insert(frame.added.end(), words.begin(), words.end());
    return not fresh.empty();
}

void TrackState::refreshNamesIfDue()
{
    auto const now = std::chrono::steady_clock::now();
    if (now - namesAt_ < (namesLoaded_ ? kNameRefresh : kNameRetry)) return;
    namesAt_        = now;
    auto const text = pageTextOf(window_).toStdString();
    if (text.empty()) return;
    names_       = capture::nameSet(text);
    namesLoaded_ = true;
}

bool TrackState::isName(QString const& text) const
{
    return names_.contains(capture::normalizedWord(text.trimmed().toStdString()));
}

}

PageTrack::PageTrack(quintptr const window, QRect const band, Deliver deliver)
    : window_{window}, deliver_{std::move(deliver)}, band_{band}
{
    worker_ = std::jthread{[this](std::stop_token stop) { run(stop); }};
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

void PageTrack::run(std::stop_token const stop)
{
    ComApartment apartment;
    TrackState   state{window_};
    auto         next = std::chrono::steady_clock::now();
    while (not stop.stop_requested()) {
        QRect band;
        {
            std::lock_guard const lock{guard_};
            band = band_;
        }
        if (auto frame = state.step(band)) deliver_(std::move(*frame));
        next += kTrackPeriod;
        auto const now = std::chrono::steady_clock::now();
        if (next < now) next = now; // a long step does not make the steps after it burst
        std::this_thread::sleep_until(next);
    }
}

void warmPageTreeOf(quintptr const window)
{
    ComApartment apartment;
    auto const   automation = newAutomation();
    if (not automation) return;
    auto const root = elementOf(*automation, window);
    if (not root) return;
    IUIAutomationCondition* anyRaw = nullptr;
    if (FAILED(automation->CreateTrueCondition(&anyRaw)) or not anyRaw) return;
    Com<IUIAutomationCondition> any{anyRaw};
    IUIAutomationElementArray*  arrayRaw = nullptr;
    root->FindAll(TreeScope_Descendants, any.get(), &arrayRaw);
    Com<IUIAutomationElementArray> array{arrayRaw}; // the query is the point; its result is dropped at once
}

}
