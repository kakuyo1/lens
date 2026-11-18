/** @file page_reader_uia.cpp
 * @brief The scan's page reader over UI Automation, for page_reader_uia.h.
 */

#include "page_reader_uia.h"

#include <QRect>
#include <QString>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>
#include <utility>

#include "capture/word_track.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max among them).
#include "uia_text_range_read.h"

namespace lens::app::page_reader_uia {
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
    if (FAILED(automation.ElementFromHandle(reinterpret_cast<HWND>(window), &raw))) return {}; // NOLINT(performance-no-int-to-ptr): the Win32 API hands this handle back as an integer; the cast is the documented round-trip
    return Com<IUIAutomationElement>{raw};
}

/// @return Whether @p window is SumatraPDF's, whose UIA text (3.7 and later) differs from a
///         browser's in two places this file reads around (docs/adr/0197).
bool sumatraWindow(quintptr const window)
{
    auto const hwnd = reinterpret_cast<HWND>(window); // NOLINT(performance-no-int-to-ptr): see elementOf
    wchar_t    name[64]{};
    if (GetClassNameW(hwnd, name, 64) > 0 and QString::fromWCharArray(name) == QLatin1String("SUMATRA_PDF_FRAME")) return true;
    return FindWindowExW(hwnd, nullptr, L"SUMATRA_PDF_CANVAS", nullptr) != nullptr;
}

/// @brief The COM operations the read timing breaks down (docs/adr/0188).
enum class Op : int { Present,
                      Text,
                      PageBox,
                      WordAt,
                      WordBox,
                      Read,
                      Step,
                      Search,
                      Visible,
                      Count };

struct OpStat {
    int    calls   = 0;
    double totalMs = 0.0;
    double maxMs   = 0.0;
};

/// One thread's read cost: the track steps its reader on a single worker, so the accumulator is
/// thread-local and lock-free, drained once a second by the shell that owns the clock.
thread_local OpStat t_ops[static_cast<int>(Op::Count)];

/// @brief Times one COM operation into the thread's accumulator on destruction.
class ScopedOp {
public:
    explicit ScopedOp(Op const op)
        : op_{op}, start_{std::chrono::steady_clock::now()}
    {}
    ~ScopedOp()
    {
        double const ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count();
        OpStat&      s  = t_ops[static_cast<int>(op_)];
        ++s.calls;
        s.totalMs += ms;
        s.maxMs = (std::max)(s.maxMs, ms);
    }

private:
    Op                                    op_;
    std::chrono::steady_clock::time_point start_;
};

/// The page's document the search picks: the largest document of the window, with its clip.
struct PageDocument {
    Com<IUIAutomationElement>     element;
    Com<IUIAutomationTextPattern> pattern;
    /// The intersection of the document frames above the page. A PDF viewer's document covers its own toolbar,
    /// but the frame it sits in begins below that toolbar, so the frame is what the reader can see.
    std::optional<QRect> clip;
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

/// @return The document controls under @p root, or nothing when the tree cannot say.
Com<IUIAutomationElementArray> documentsOf(IUIAutomation& automation, IUIAutomationElement& root)
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
    return Com<IUIAutomationElementArray>{arrayRaw};
}

PageDocument pageOf(IUIAutomation& automation, IUIAutomationElement& root, QRect const clip)
{
    auto const array = documentsOf(automation, root);
    if (not array) return {};
    int count = 0;
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
        LONGLONG     chosenArea  = -1;
        int          chosenDepth = 0;
        for (auto& candidate : candidates) {
            if (candidate.area <= 0 or candidate.area * 4 < largest) continue;
            if (not allowVirtual and virtualDocument(candidate)) continue;
            if (page.element and (candidate.depth < chosenDepth or (candidate.depth == chosenDepth and candidate.area <= chosenArea))) continue;
            page.element = std::move(candidate.element);
            page.pattern = std::move(candidate.pattern);
            page.clip    = candidate.clip;
            chosenArea   = candidate.area;
            chosenDepth  = candidate.depth;
        }
        if (page.element) return page;
    }
    return {};
}

/// @return The band in the form the window geometry takes.
QRect rectOf(capture::Band const& band)
{
    return {band.x, band.y, band.width, band.height};
}

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

/// @return The word @p range holds, spelled as the page has it; empty when it cannot be read.
QString wordTextOf(IUIAutomationTextRange& range)
{
    BSTR text = nullptr;
    if (FAILED(range.GetText(64, &text)) or not text) return {};
    QString const word = QString::fromWCharArray(text, static_cast<int>(SysStringLen(text)));
    SysFreeString(text);
    return word;
}

/// @return Whether @p token carries a letter. A PDF can keep the closing period as a word unit of
///         its own; such a token has none (docs/adr/0200).
bool hasLetter(QString const& token)
{
    for (auto const character : token)
        if (character.isLetter()) return true;
    return false;
}

/// @return The whole text of @p pattern's document range; empty when it cannot be read.
QString textOfPattern(IUIAutomationTextPattern& pattern)
{
    IUIAutomationTextRange* rangeRaw = nullptr;
    if (FAILED(pattern.get_DocumentRange(&rangeRaw)) or not rangeRaw) return {};
    Com<IUIAutomationTextRange> range{rangeRaw};
    BSTR                        body = nullptr;
    if (FAILED(range->GetText(-1, &body))) return {};
    QString const text = QString::fromWCharArray(body, static_cast<int>(SysStringLen(body)));
    SysFreeString(body);
    return text;
}

/// @return The text of the page that holds @p at; empty when the point or the page cannot be read.
/// @note A PDF viewer's document range is the whole file, so the names and the language gate read
///       the page on screen instead (docs/adr/0197).
QString textOfPageAt(IUIAutomationTextPattern& pattern, QPoint const at)
{
    POINT const             point{at.x(), at.y()};
    IUIAutomationTextRange* rangeRaw = nullptr;
    if (FAILED(pattern.RangeFromPoint(point, &rangeRaw)) or not rangeRaw) return {};
    Com<IUIAutomationTextRange> range{rangeRaw};
    if (FAILED(range->ExpandToEnclosingUnit(TextUnit_Page))) return {};
    BSTR body = nullptr;
    if (FAILED(range->GetText(-1, &body)) or not body) return {};
    QString const text = QString::fromWCharArray(body, static_cast<int>(SysStringLen(body)));
    SysFreeString(body);
    return text;
}

/// @return @p box cut to the line a SumatraPDF word's dash should sit under. Its word rects span the
///         whole line pitch, whose bottom lies well below the glyphs, so the dash would hang low;
///         cutting a sixth of the height lifts it back under the letters (docs/adr/0197).
QRect lineBoxOf(QRect const box, bool const sumatra)
{
    if (not sumatra) return box;
    int const lift = (std::max)(2, box.height() / 6);
    return QRect{box.x(), box.y(), box.width(), (std::max)(1, box.height() - lift)};
}

/// @brief A word of a UI Automation page: the word unit's own text range.
class UiaWord final : public capture::Word {
public:
    explicit UiaWord(Com<IUIAutomationTextRange> range)
        : range_{std::move(range)}
    {}

    /// @return The word unit's range; the page reads through it, and never moves it.
    IUIAutomationTextRange& range() const
    {
        return *range_;
    }

    /// @return The element the range sits in, read once and kept: the sight pass compares it with every hit.
    IUIAutomationElement* enclosing() const
    {
        if (not enclosing_) {
            IUIAutomationElement* raw = nullptr;
            if (SUCCEEDED(range_->GetEnclosingElement(&raw))) enclosing_.reset(raw);
        }
        return enclosing_.get();
    }

private:
    Com<IUIAutomationTextRange>       range_;
    mutable Com<IUIAutomationElement> enclosing_;
};

/// @brief One page of a window, read through UI Automation's TextPattern.
class UiaPage final : public capture::Page {
public:
    UiaPage(std::shared_ptr<IUIAutomation> automation,
            quintptr const                 window,
            Com<IUIAutomationElement>      element,
            Com<IUIAutomationTextPattern>  pattern,
            std::optional<QRect>           clip,
            bool const                     sumatra)
        : automation_{std::move(automation)}, window_{window}, element_{std::move(element)}, pattern_{std::move(pattern)}, clip_{clip}, sumatra_{sumatra}
    {}

    bool                             present() const override;
    capture::Band                    box() const override;
    std::string                      text() const override;
    std::shared_ptr<capture::Word>   wordAt(int x, int y) override;
    std::optional<capture::Band>     box(capture::Word const& word) override;
    std::optional<capture::WordRead> read(capture::Word const& word) override;
    bool                             visible(capture::Word const& word, capture::Band const& box) override;
    std::chrono::microseconds        sightCost() const override;

private:
    bool                           visibleProbe(capture::Word const& word, capture::Band const& box);
    std::shared_ptr<capture::Word> step(capture::Word const& word, int delta, bool* awaiting) override;
    std::shared_ptr<capture::Word> stepByBox(capture::Word const& word, int delta, bool* awaiting);

private:
    std::shared_ptr<IUIAutomation> automation_; ///< The page outlives the reader that found it, so it holds the automation instance itself.
    quintptr const                 window_;
    Com<IUIAutomationElement>      element_;
    Com<IUIAutomationTextPattern>  pattern_;
    std::optional<QRect> const     clip_;
    bool const                     sumatra_;
    /// The hit-test's own running mean, never drained: the shell's per-second drain reset the shared
    /// accumulator to zero, re-opening the cost gate every second and flapping the placed set
    /// between filtered and whole -- the flicker the reader saw (docs/adr/0193).
    mutable double sightMeanMs_ = 0;
    /// The page's birth: probes inside the browser's cold-start window answer tens of milliseconds
    /// even on a web page, so they teach the gate nothing (docs/adr/0193).
    std::chrono::steady_clock::time_point const bornAt_ = std::chrono::steady_clock::now();
};

/// @return Whether the page still hangs under its window; false when the tree cannot say.
bool UiaPage::present() const
{
    ScopedOp const op{Op::Present};
    // Climb from the page to its window root rather than enumerate every document under it: a
    // document the browser rebuilt or dropped (a navigation, a discarded tab, a renderer swap) no
    // longer reaches its window, and the climb costs the tree's depth, not its node count. A
    // background tab's sliver still reaches the root -- fillsTheBand rejects that, not this
    // (docs/adr/0163). The descendant FindAll this replaces cost ~180 ms a call on a heavy tree,
    // twice a second; the climb is a handful of parent reads (docs/adr/0188).
    auto const root = elementOf(*automation_, window_);
    if (not root) return false;
    IUIAutomationTreeWalker* walkerRaw = nullptr;
    if (FAILED(automation_->get_ControlViewWalker(&walkerRaw)) or not walkerRaw) return false;
    Com<IUIAutomationTreeWalker> walker{walkerRaw};
    element_->AddRef();
    Com<IUIAutomationElement> current{element_.get()};
    for (int level = 0; level < kMaxLevels; ++level) {
        BOOL same = FALSE;
        if (SUCCEEDED(automation_->CompareElements(current.get(), root.get(), &same)) and same) return true;
        IUIAutomationElement* parentRaw = nullptr;
        if (FAILED(walker->GetParentElement(current.get(), &parentRaw)) or not parentRaw) return false;
        current.reset(parentRaw);
    }
    return false;
}

/// @return The page's box as it stands now, in physical screen pixels; empty when the element cannot say.
capture::Band UiaPage::box() const
{
    ScopedOp const op{Op::PageBox};
    RECT           rect{};
    if (FAILED(element_->get_CurrentBoundingRectangle(&rect))) return {};
    QRect const box{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    return bandOf(clip_ ? box.intersected(*clip_) : box);
}

std::string UiaPage::text() const
{
    ScopedOp const op{Op::Text};
    if (not sumatra_) return textOfPattern(*pattern_).toStdString();
    // A PDF viewer's document range is the whole file: the on-screen page is the one whose names and
    // language the track can use (docs/adr/0197).
    RECT rect{};
    if (FAILED(element_->get_CurrentBoundingRectangle(&rect))) return {};
    return textOfPageAt(*pattern_, QPoint{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}).toStdString();
}

/// @return The word under the point, where a read starts; null when the page cannot answer there.
std::shared_ptr<capture::Word> UiaPage::wordAt(int const x, int const y)
{
    ScopedOp const          op{Op::WordAt};
    POINT const             at{x, y};
    IUIAutomationTextRange* raw = nullptr;
    if (FAILED(pattern_->RangeFromPoint(at, &raw)) or not raw) return nullptr;
    Com<IUIAutomationTextRange> word{raw};
    word->ExpandToEnclosingUnit(TextUnit_Word);
    return std::make_shared<UiaWord>(std::move(word));
}

/// @return The word's first box; nothing when it has none.
std::optional<capture::Band> UiaPage::box(capture::Word const& word)
{
    ScopedOp const op{Op::WordBox};
    auto const     boxes = boxesOf(static_cast<UiaWord const&>(word).range());
    if (boxes.empty()) return std::nullopt;
    return bandOf(boxes.front());
}

/// @brief The word's text and boxes, untrimmed. Where the letters end is read the way the word
///        iteration itself reads: a direct retraction of the range's end, and when Chrome refuses
///        that (it answers moved=-count), a one-character range stepped to the first trailing
///        character, whose box says where the letters end.
std::optional<capture::WordRead> UiaPage::read(capture::Word const& word)
{
    ScopedOp const          op{Op::Read};
    IUIAutomationTextRange& range = static_cast<UiaWord const&>(word).range();
    QString const           text  = wordTextOf(range);
    std::string const       utf8  = text.toStdString();
    int const               cut   = capture::trailingPunctOf(utf8);
    QRect const             full  = lineBoxOf(firstBoxOf(range), sumatra_);
    if (cut == 0) return full.isEmpty() ? std::nullopt : std::optional<capture::WordRead>{{utf8, bandOf(full), std::nullopt}};

    // A clone of a range the page has just retired hands back an empty handle; a virtual call
    // through it would fault the process. Say the letters' end could not be read -- the shape a
    // failed Move below already returns -- and let the caller trim by its own policy.
    auto const retracted = cloneOf(range);
    if (not retracted) return capture::WordRead{utf8, bandOf(full), std::nullopt};
    int moved = 0;
    if (SUCCEEDED(retracted->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, -cut, &moved)) and moved == cut) {
        auto const boxes = boxesOf(*retracted);
        if (boxes.empty()) return capture::WordRead{utf8, bandOf(full), capture::Band{}}; // nothing but punctuation: the token leaves the scan
        QRect const front = boxes.front();
        return capture::WordRead{utf8, bandOf(full), bandOf(QRect{front.x() + front.width(), front.y(), 0, front.height()})};
    }
    auto const cursor = cloneOf(range);
    if (not cursor) return capture::WordRead{utf8, bandOf(full), std::nullopt};
    int const junkAt = static_cast<int>(text.size() - cut);
    if (FAILED(cursor->Move(TextUnit_Character, junkAt, &moved)) or moved != junkAt) return capture::WordRead{utf8, bandOf(full), std::nullopt};
    cursor->ExpandToEnclosingUnit(TextUnit_Character);
    auto const boxes = boxesOf(*cursor);
    if (boxes.empty()) return capture::WordRead{utf8, bandOf(full), std::nullopt};
    return capture::WordRead{utf8, bandOf(full), bandOf(boxes.front())};
}

/// @return One word along the reading order; null at either end of the document, or when the walk fails.
std::shared_ptr<capture::Word> UiaPage::step(capture::Word const& word, int const delta, bool* const awaiting)
{
    ScopedOp const op{Op::Step};
    if (sumatra_) return stepByBox(word, delta, awaiting);
    auto next = cloneOf(static_cast<UiaWord const&>(word).range());
    if (not next) return nullptr;
    int moved = 0;
    if (FAILED(next->Move(TextUnit_Word, delta, &moved)) or moved == 0) return nullptr;
    next->ExpandToEnclosingUnit(TextUnit_Word);
    return std::make_shared<UiaWord>(std::move(next));
}

/// @brief One word along the reading order on a SumatraPDF page. Its provider's Move does not
///        advance from a range that is already a word (upstream #321), so the target is read from
///        the boxes the moved range covers -- the last is the next word's, the first is the
///        previous word's -- and RangeFromPoint turns that box's centre into a word handle
///        (docs/adr/0197).
/// @note A moved range that lands on a punctuation-only token is stepped past: RangeFromPoint on
///       the period would expand back to the word before it and hand the walk the same word
///       forever, never reaching the next page (docs/adr/0200).
/// @note A moved range whose word has no box yet is a page that is not laid out: the step answers
///       null with @p awaiting set, so the walk waits for the layout instead of calling the
///       document ended (docs/adr/0199).
std::shared_ptr<capture::Word> UiaPage::stepByBox(capture::Word const& word, int const delta, bool* const awaiting)
{
    auto probe = cloneOf(static_cast<UiaWord const&>(word).range());
    if (not probe) return nullptr;
    constexpr int kMaxPunctuationHops = 8;
    for (int hop = 0; hop < kMaxPunctuationHops; ++hop) {
        int moved = 0;
        if (FAILED(probe->Move(TextUnit_Word, delta, &moved)) or moved == 0) return nullptr;
        auto const boxes = boxesOf(*probe);
        if (boxes.empty()) {
            if (awaiting) *awaiting = true;
            return nullptr;
        }
        QRect const& place = delta > 0 ? boxes.back() : boxes.front();
        if (place.isEmpty()) {
            if (awaiting) *awaiting = true;
            return nullptr;
        }
        if (not hasLetter(wordTextOf(*probe))) continue;
        POINT const             at{place.center().x(), place.center().y()};
        IUIAutomationTextRange* raw = nullptr;
        if (FAILED(pattern_->RangeFromPoint(at, &raw)) or not raw) {
            if (awaiting) *awaiting = true;
            return nullptr;
        }
        Com<IUIAutomationTextRange> target{raw};
        if (FAILED(target->ExpandToEnclosingUnit(TextUnit_Word))) {
            if (awaiting) *awaiting = true;
            return nullptr;
        }
        return std::make_shared<UiaWord>(std::move(target));
    }
    if (awaiting) *awaiting = true;
    return nullptr;
}

/// @return Whether the word is the topmost content at @p box, timed into the page's own running mean:
///         the cost gate reads that mean, and it must survive the shell's per-second drain.
bool UiaPage::visible(capture::Word const& word, capture::Band const& box)
{
    // SumatraPDF answers every hit inside the page with the page element, not with the word's own
    // container, so the probe below would read every word as covered. Its overlays are separate
    // native controls and it offers no cheaper signal, so its places draw as they read (the
    // no-mechanics answer of docs/adr/0193, recorded with the reader in docs/adr/0197).
    if (sumatra_) return true;
    auto const   start = std::chrono::steady_clock::now();
    bool const   seen  = visibleProbe(word, box);
    double const ms    = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (std::chrono::steady_clock::now() - bornAt_ > std::chrono::milliseconds{1500})
        sightMeanMs_ = sightMeanMs_ == 0 ? ms : sightMeanMs_ * 0.7 + ms * 0.3;
    return seen;
}

/// @return The page's own mean hit-test cost, learned once the cold-start window passes and latched
///         for the page's life: a PDF viewer's twenty-millisecond point query closes the gate once,
///         not once a second. Nothing is said while the page is still cold.
std::chrono::microseconds UiaPage::sightCost() const
{
    if (std::chrono::steady_clock::now() - bornAt_ < std::chrono::milliseconds{1500}) return {};
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<double, std::milli>{sightMeanMs_});
}

/// @return Whether the word is the topmost content at @p box: the screen's hit-test names what stands
///         over the place, and the hit's own text scope says whether the word lies inside it.
bool UiaPage::visibleProbe(capture::Word const& word, capture::Band const& box)
{
    ScopedOp const op{Op::Visible};
    // The lower quarter of the box: inside the letters, yet near the line's own place, so a control
    // covering the word's bottom half hides it too. The centre would miss that half-covered row.
    POINT const           at{box.x + box.width / 2, box.y + (3 * box.height) / 4};
    IUIAutomationElement* hitRaw = nullptr;
    if (FAILED(automation_->ElementFromPoint(at, &hitRaw)) or not hitRaw) return true; // the screen cannot say: the line stands
    Com<IUIAutomationElement> hit{hitRaw};
    UiaWord const&            uiaWord = static_cast<UiaWord const&>(word);
    // The word's own element under the point is the common visible case: two calls answer it.
    if (auto const enclosing = uiaWord.enclosing()) {
        BOOL same = FALSE;
        if (SUCCEEDED(automation_->CompareElements(hit.get(), enclosing, &same)) and same) return true;
    }
    IUIAutomationTextRange* scopeRaw = nullptr;
    if (FAILED(pattern_->RangeFromChild(hit.get(), &scopeRaw)) or not scopeRaw) return false; // a control with no text stands over the place
    Com<IUIAutomationTextRange> scope{scopeRaw};
    IUIAutomationTextRange&     range      = uiaWord.range();
    int                         startOrder = 0;
    int                         endOrder   = 0;
    if (FAILED(scope->CompareEndpoints(TextPatternRangeEndpoint_Start, &range, TextPatternRangeEndpoint_Start, &startOrder)) or
        FAILED(scope->CompareEndpoints(TextPatternRangeEndpoint_End, &range, TextPatternRangeEndpoint_End, &endOrder)))
        return true;
    if (startOrder > 0 or endOrder < 0) return false; // the hit answers for another control's text: the word sits under it
    // The hit holds the word but is not its own element: a container above it. A tight one is the
    // word's wrapper (a link, a span); a page-sized one is a control the tree pruned for having no
    // text, standing over the place with nothing of its own to answer for it.
    RECT hitBox{};
    if (FAILED(hit->get_CurrentBoundingRectangle(&hitBox))) return true;
    return hitBox.bottom - hitBox.top <= 2 * box.height and hitBox.right - hitBox.left <= 4 * box.width;
}

/// The page of the window last read, kept between reads: finding it searches the whole window tree, which costs
/// about as much as a full walk, and a probe runs several times a second.
struct PageCache {
    std::mutex               guard;
    quintptr                 window = 0;
    std::shared_ptr<UiaPage> page;
};

PageCache& pageCache()
{
    static PageCache cache;
    return cache;
}

/// @return The text the names fall back to before the first acquire: the largest document's whole
///         range for a browser, the page on screen for a PDF viewer (docs/adr/0197).
std::string windowText(quintptr const window, bool const sumatra)
{
    ComApartment apartment;
    auto const   automation = newAutomation();
    if (not automation) return {};
    auto const root = elementOf(*automation, window);
    if (not root) return {};
    auto const page = pageOf(*automation, *root, QRect{});
    if (not page.pattern) return {};
    if (not sumatra) return textOfPattern(*page.pattern).toStdString();
    RECT rect{};
    if (FAILED(page.element->get_CurrentBoundingRectangle(&rect))) return {};
    return textOfPageAt(*page.pattern, QPoint{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}).toStdString();
}

}

UiaPageReader::UiaPageReader(quintptr const window)
    : window_{window}, sumatra_{sumatraWindow(window)}
{}

UiaPageReader::~UiaPageReader()
{
    drop();
}

/// @return The page of @p window: the cached one while the window is the same, else a fresh search; null when there is none.
std::shared_ptr<capture::Page> UiaPageReader::acquire(capture::Band const& band)
{
    auto& cache = pageCache();
    {
        std::lock_guard const lock{cache.guard};
        if (cache.window == window_ and cache.page) return cache.page;
    }
    ScopedOp const op{Op::Search}; // a cache hit returned above; the rest is the whole-tree search
    if (not automation_) {
        IUIAutomation* raw = nullptr;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw))) or not raw) return nullptr;
        automation_.reset(raw, ComRelease{});
    }
    auto const root = elementOf(*automation_, window_);
    if (not root) return nullptr;
    auto page = pageOf(*automation_, *root, rectOf(band));
    if (not page.pattern) return nullptr;
    auto                  document = std::make_shared<UiaPage>(automation_, window_, std::move(page.element), std::move(page.pattern), page.clip, sumatra_);
    std::lock_guard const lock{cache.guard};
    cache.window = window_;
    cache.page   = document;
    return document;
}

/// Drops the cached page of this reader's window, so the next read searches for it again.
void UiaPageReader::drop()
{
    auto&                 cache = pageCache();
    std::lock_guard const lock{cache.guard};
    if (cache.window == window_) cache.page.reset();
}

std::string UiaPageReader::anyText()
{
    return windowText(window_, sumatra_);
}

std::string anyTextOf(quintptr const window)
{
    return windowText(window, false);
}

void warmTreeOf(quintptr const window)
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

ReaderTiming drainReadTiming()
{
    auto const take = [](Op const op) -> ReadTiming {
        OpStat&          s = t_ops[static_cast<int>(op)];
        ReadTiming const r{.calls = s.calls, .totalMs = s.totalMs, .maxMs = s.maxMs};
        s = OpStat{};
        return r;
    };
    return {.present = take(Op::Present),
            .text    = take(Op::Text),
            .pageBox = take(Op::PageBox),
            .wordAt  = take(Op::WordAt),
            .wordBox = take(Op::WordBox),
            .read    = take(Op::Read),
            .step    = take(Op::Step),
            .search  = take(Op::Search),
            .visible = take(Op::Visible)};
}

}
