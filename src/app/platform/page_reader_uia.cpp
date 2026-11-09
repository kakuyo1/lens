/** @file page_reader_uia.cpp
 * @brief The scan's page reader over UI Automation, for page_reader_uia.h.
 */

#include "page_reader_uia.h"

#include <QRect>
#include <QString>

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
    if (FAILED(automation.ElementFromHandle(reinterpret_cast<HWND>(window), &raw))) return {};
    return Com<IUIAutomationElement>{raw};
}

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

private:
    Com<IUIAutomationTextRange> range_;
};

/// @brief One page of a window, read through UI Automation's TextPattern.
class UiaPage final : public capture::Page {
public:
    UiaPage(std::shared_ptr<IUIAutomation> automation, quintptr const window, Com<IUIAutomationElement> element, Com<IUIAutomationTextPattern> pattern, std::optional<QRect> clip)
        : automation_{std::move(automation)}, window_{window}, element_{std::move(element)}, pattern_{std::move(pattern)}, clip_{std::move(clip)}
    {}

    bool                             present() const override;
    capture::Band                    box() const override;
    std::string                      text() const override;
    std::shared_ptr<capture::Word>   wordAt(int x, int y) override;
    std::optional<capture::Band>     box(capture::Word const& word) override;
    std::optional<capture::WordRead> read(capture::Word const& word) override;
    std::shared_ptr<capture::Word>   step(capture::Word const& word, int delta) override;

private:
    std::shared_ptr<IUIAutomation> automation_; ///< The page outlives the reader that found it, so it holds the automation instance itself.
    quintptr const                 window_;
    Com<IUIAutomationElement>      element_;
    Com<IUIAutomationTextPattern>  pattern_;
    std::optional<QRect> const     clip_;
};

/// @return Whether the page is still one of the window's documents; false when the tree cannot say.
bool UiaPage::present() const
{
    auto const root      = elementOf(*automation_, window_);
    auto const documents = root ? documentsOf(*automation_, *root) : Com<IUIAutomationElementArray>{};
    if (not documents) return false;
    int count = 0;
    documents->get_Length(&count);
    for (int i = 0; i < count; ++i) {
        IUIAutomationElement* raw = nullptr;
        if (FAILED(documents->GetElement(i, &raw)) or not raw) continue;
        Com<IUIAutomationElement> element{raw};
        BOOL                      same = FALSE;
        if (SUCCEEDED(automation_->CompareElements(element.get(), element_.get(), &same)) and same) return true;
    }
    return false;
}

/// @return The page's box as it stands now, in physical screen pixels; empty when the element cannot say.
capture::Band UiaPage::box() const
{
    RECT rect{};
    if (FAILED(element_->get_CurrentBoundingRectangle(&rect))) return {};
    QRect const box{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    return bandOf(clip_ ? box.intersected(*clip_) : box);
}

std::string UiaPage::text() const
{
    return textOfPattern(*pattern_).toStdString();
}

/// @return The word under the point, where a read starts; null when the page cannot answer there.
std::shared_ptr<capture::Word> UiaPage::wordAt(int const x, int const y)
{
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
    auto const boxes = boxesOf(static_cast<UiaWord const&>(word).range());
    if (boxes.empty()) return std::nullopt;
    return bandOf(boxes.front());
}

/// @brief The word's text and boxes, untrimmed. Where the letters end is read the way the word
///        iteration itself reads: a direct retraction of the range's end, and when Chrome refuses
///        that (it answers moved=-count), a one-character range stepped to the first trailing
///        character, whose box says where the letters end.
std::optional<capture::WordRead> UiaPage::read(capture::Word const& word)
{
    IUIAutomationTextRange& range = static_cast<UiaWord const&>(word).range();
    QString const           text  = wordTextOf(range);
    std::string const       utf8  = text.toStdString();
    int const               cut   = capture::trailingPunctOf(utf8);
    QRect const             full  = firstBoxOf(range);
    if (cut == 0) return full.isEmpty() ? std::nullopt : std::optional<capture::WordRead>{{utf8, bandOf(full), std::nullopt}};

    auto const retracted = cloneOf(range);
    int        moved     = 0;
    if (SUCCEEDED(retracted->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, -cut, &moved)) and moved == cut) {
        auto const boxes = boxesOf(*retracted);
        if (boxes.empty()) return capture::WordRead{utf8, bandOf(full), capture::Band{}}; // nothing but punctuation: the token leaves the scan
        QRect const front = boxes.front();
        return capture::WordRead{utf8, bandOf(full), bandOf(QRect{front.x() + front.width(), front.y(), 0, front.height()})};
    }
    auto const cursor = cloneOf(range);
    int const  junkAt = text.size() - cut;
    if (FAILED(cursor->Move(TextUnit_Character, junkAt, &moved)) or moved != junkAt) return capture::WordRead{utf8, bandOf(full), std::nullopt};
    cursor->ExpandToEnclosingUnit(TextUnit_Character);
    auto const boxes = boxesOf(*cursor);
    if (boxes.empty()) return capture::WordRead{utf8, bandOf(full), std::nullopt};
    return capture::WordRead{utf8, bandOf(full), bandOf(boxes.front())};
}

/// @return One word along the reading order; null at either end of the document, or when the walk fails.
std::shared_ptr<capture::Word> UiaPage::step(capture::Word const& word, int const delta)
{
    auto next = cloneOf(static_cast<UiaWord const&>(word).range());
    if (not next) return nullptr;
    int moved = 0;
    if (FAILED(next->Move(TextUnit_Word, delta, &moved)) or moved == 0) return nullptr;
    next->ExpandToEnclosingUnit(TextUnit_Word);
    return std::make_shared<UiaWord>(std::move(next));
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

}

UiaPageReader::UiaPageReader(quintptr const window)
    : window_{window}
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
    if (not automation_) {
        IUIAutomation* raw = nullptr;
        if (FAILED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw))) or not raw) return nullptr;
        automation_.reset(raw, ComRelease{});
    }
    auto const root = elementOf(*automation_, window_);
    if (not root) return nullptr;
    auto page = pageOf(*automation_, *root, rectOf(band));
    if (not page.pattern) return nullptr;
    auto                  document = std::make_shared<UiaPage>(automation_, window_, std::move(page.element), std::move(page.pattern), std::move(page.clip));
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
    return anyTextOf(window_);
}

std::string anyTextOf(quintptr const window)
{
    ComApartment apartment;
    auto const   automation = newAutomation();
    if (not automation) return {};
    auto const root = elementOf(*automation, window);
    if (not root) return {};
    auto const page = pageOf(*automation, *root, QRect{});
    if (not page.pattern) return {};
    return textOfPattern(*page.pattern).toStdString();
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

}
