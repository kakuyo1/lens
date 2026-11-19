#pragma once

/**
 * @file uia_text_range_read.h
 * @brief The UI Automation primitives both sides of the scan's page reader share: the COM handle,
 *        the apartment, and the boxes a text range covers.
 *
 * Internal to src/app/platform: the hover lookup (hover_word_lookup.cpp) and the scan's page reader
 * (page_reader_uia.cpp) each use a handful of the same primitives, so they live here once.
 *
 * @note Windows only. Include this header last, after every Qt and standard header: windows.h
 *       brings a few hundred macros with it (min and max among them).
 */

#include <QRect>

#include <memory>
#include <vector>

#include "capture/word_track.h"

// Last, after everything else: windows.h brings a few hundred macros with it (min and max among them).
#include <windows.h>
#include <UIAutomation.h>
#include <oleauto.h>

namespace lens::app {

/// Levels climbed from a hit element to the text provider, or up to a page's document frames. Twitter's
/// page needs 11; the cap keeps a deeper tree from costing more than the dwell that started the lookup.
constexpr int kMaxLevels = 50;

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

/// How long UI Automation waits for a provider to answer one call before failing it, in ms: the
/// documented default, pinned here because a target that stopped pumping must fail the call rather
/// than hold the thread that made it.
constexpr DWORD kUiaConnectionTimeoutMs = 2000;
/// The bound on one whole text-pattern transaction, in ms: the documented default, pinned for the
/// same reason -- a read against a silent target ends instead of borrowing the provider's patience.
constexpr DWORD kUiaTransactionTimeoutMs = 20000;

/// @return A UI Automation client with the timeouts above in force, or null when COM refused one.
/// @note CUIAutomation8, not CUIAutomation: only the v2-or-later object carries the timeouts, and a
///       client whose provider stops pumping was seen holding its worker inside UI Automation's MSAA
///       bridge, sending a window message that never came back (page_reader_uia.cpp's sight probe).
inline Com<IUIAutomation> newAutomation()
{
    IUIAutomation* raw = nullptr;
    if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw))) or raw == nullptr) return {};
    Com<IUIAutomation> automation{raw};
    IUIAutomation2*    timed = nullptr;
    if (SUCCEEDED(raw->QueryInterface(IID_PPV_ARGS(&timed))) and timed != nullptr) {
        Com<IUIAutomation2> holder{timed};
        holder->put_ConnectionTimeout(kUiaConnectionTimeoutMs);
        holder->put_TransactionTimeout(kUiaTransactionTimeoutMs);
    }
    return automation;
}

/// @return Every box of @p range in the order UI Automation gives them; empty when the range has none.
inline std::vector<QRect> boxesOf(IUIAutomationTextRange& range)
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

/// @return The band in the form the placement arithmetic takes.
inline capture::Band bandOf(QRect const band)
{
    return {.x = band.x(), .y = band.y(), .width = band.width(), .height = band.height()};
}

}
