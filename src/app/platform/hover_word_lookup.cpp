/**
 * @file hover_word_lookup.cpp
 * @brief The UI Automation read behind the hover lookup.
 */

#include "hover_word_lookup.h"

#include <QFileInfo>

#include <iterator>
#include <memory>

#include "capture/scan_pipeline.h"

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

/// @return The first box of @p range that holds @p at, when one does.
std::optional<QRect> boxHolding(IUIAutomationTextRange& range, POINT const at)
{
    SAFEARRAY* rects = nullptr;
    if (FAILED(range.GetBoundingRectangles(&rects)) or not rects) return std::nullopt;

    std::optional<QRect> found;
    long                 lo = 0, hi = -1;
    SafeArrayGetLBound(rects, 1, &lo);
    SafeArrayGetUBound(rects, 1, &hi);
    // Each box is four doubles: x, y, width, height.
    for (long i = lo; i + 3 <= hi and not found; i += 4) {
        double v[4] = {};
        for (long k = 0; k < 4; ++k) {
            long idx = i + k;
            SafeArrayGetElement(rects, &idx, &v[k]);
        }
        QRect const box{static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]), static_cast<int>(v[3])};
        if (box.contains(QPoint{at.x, at.y})) found = box;
    }
    SafeArrayDestroy(rects);
    return found;
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

}

QString pageTextOf(quintptr const window)
{
    ComApartment apartment;
    auto const   automation = newAutomation();
    if (not automation) return {};
    auto const root = elementOf(*automation, window);
    if (not root) return {};

    // Only document controls: a browser's page is one, its tabs and toolbars are not.
    VARIANT documentType{};
    documentType.vt                      = VT_I4;
    documentType.lVal                    = UIA_DocumentControlTypeId;
    IUIAutomationCondition* conditionRaw = nullptr;
    if (FAILED(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, documentType, &conditionRaw)) or not conditionRaw) return {};
    Com<IUIAutomationCondition> documents{conditionRaw};

    IUIAutomationElementArray* arrayRaw = nullptr;
    if (FAILED(root->FindAll(TreeScope_Descendants, documents.get(), &arrayRaw)) or not arrayRaw) return {};
    Com<IUIAutomationElementArray> array{arrayRaw};
    int                            count = 0;
    array->get_Length(&count);

    // The largest document is the page; the browser's other documents are small.
    Com<IUIAutomationTextPattern> page;
    LONGLONG                      bestArea = -1;
    for (int i = 0; i < count; ++i) {
        IUIAutomationElement* elementRaw = nullptr;
        if (FAILED(array->GetElement(i, &elementRaw)) or not elementRaw) continue;
        Com<IUIAutomationElement> element{elementRaw};
        RECT                      box{};
        if (FAILED(element->get_CurrentBoundingRectangle(&box))) continue;
        LONGLONG const area = static_cast<LONGLONG>(box.right - box.left) * (box.bottom - box.top);
        if (area <= bestArea) continue;
        IUnknown* patternRaw = nullptr;
        if (FAILED(element->GetCurrentPattern(UIA_TextPatternId, &patternRaw)) or not patternRaw) continue;
        Com<IUnknown>             pattern{patternRaw};
        IUIAutomationTextPattern* textRaw = nullptr;
        if (FAILED(pattern->QueryInterface(IID_PPV_ARGS(&textRaw))) or not textRaw) continue;
        page.reset(textRaw);
        bestArea = area;
    }
    if (not page) return {};

    IUIAutomationTextRange* rangeRaw = nullptr;
    if (FAILED(page->get_DocumentRange(&rangeRaw)) or not rangeRaw) return {};
    Com<IUIAutomationTextRange> range{rangeRaw};
    BSTR                        body = nullptr;
    if (FAILED(range->GetText(-1, &body))) return {};
    QString const text = QString::fromWCharArray(body, static_cast<int>(SysStringLen(body)));
    SysFreeString(body);
    return text;
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
