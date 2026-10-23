/**
 * @file glass_duplication.cpp
 * @brief The DXGI desktop duplication behind the live glass refresh (see glass_duplication.h).
 */

#include "glass_duplication.h"

#include "util/log.h"

#include <QElapsedTimer>
#include <QTransform>

#include <algorithm>
#include <atomic>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace lens::app {

namespace {

#ifdef _WIN32

using Microsoft::WRL::ComPtr;

/// How long a failure that can pass (another recorder holding the output, a mode switch) is left alone, in ms.
constexpr ULONGLONG kRetryMs = 5000;

/// @brief The rotation our mapping words, from the one a duplication description reports.
/// @note The two are inverses. `DXGI_OUTDUPL_DESC::Rotation` is the turn that takes the frame back to the desktop; our
///       ScreenRotation says how the desktop was turned into the frame. The measurement machine's portrait monitor
///       reports 90 on its output and 270 here, and its acquired 2560 x 1440 frame really is the 1440 x 2560 desktop
///       turned 90 degrees clockwise -- the integration case paints four quadrants and reads them back to settle it.
ScreenRotation rotationOf(DXGI_MODE_ROTATION const rotation)
{
    switch (rotation) {
        case DXGI_MODE_ROTATION_ROTATE90: return ScreenRotation::Rotate270;
        case DXGI_MODE_ROTATION_ROTATE180: return ScreenRotation::Rotate180;
        case DXGI_MODE_ROTATION_ROTATE270: return ScreenRotation::Rotate90;
        default: return ScreenRotation::Identity;
    }
}

/// @return Text for the log line: the rotation and the sides it swaps, without naming a device.
char const* rotationText(ScreenRotation const rotation)
{
    switch (rotation) {
        case ScreenRotation::Rotate90: return "rotated 90";
        case ScreenRotation::Rotate180: return "rotated 180";
        case ScreenRotation::Rotate270: return "rotated 270";
        default: return "identity";
    }
}

/// @return Whether a failed `DuplicateOutput` is a refusal that cannot change while the output exists.
bool refusalIsPermanent(HRESULT const hr)
{
    // The internal panel of the measurement machine answers DXGI_ERROR_UNSUPPORTED on every adapter. Another recorder
    // (E_ACCESSDENIED) or a session that is switching outputs (DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) can pass, and a
    // display mode change surfaces as ACCESS_LOST once the session was running.
    return hr == DXGI_ERROR_UNSUPPORTED or hr == E_INVALIDARG or hr == E_NOTIMPL;
}

/// @return Whether @p rect and @p other share any pixel.
bool intersects(RECT const& rect, RECT const& other)
{
    return rect.left < other.right and other.left < rect.right and rect.top < other.bottom and other.top < rect.bottom;
}

/// @brief Whether a frame's changed regions reach @p sheet, in the frame's coordinates.
///
/// The duplication API says what moved and what was redrawn with every frame. A frame that names neither inside the
/// sheet is a frame of some other part of the screen -- the animated wallpaper on the far side of the monitor, say --
/// and copying it back would spend the copy to publish nothing. The move rectangles' destinations are redrawn content;
/// their sources are the content that left, so both count as touched.
///
/// @param duplication The session's duplication, with a frame acquired.
/// @param info The frame's information, whose metadata size says how much there is to ask for.
/// @param sheet The sheet's rectangle in the frame.
/// @param buffer The session's metadata scratch buffer, kept between frames rather than allocated at sixty a second.
/// @return True when the sheet has to be copied, including when the metadata cannot be read (copy rather than miss).
bool frameTouches(IDXGIOutputDuplication* const duplication, DXGI_OUTDUPL_FRAME_INFO const& info, RECT const& sheet, std::vector<BYTE>& buffer)
{
    // No accumulated frames means only the pointer moved, and the pointer is not part of the acquired image.
    if (info.AccumulatedFrames == 0) return false;
    if (info.TotalMetadataBufferSize == 0) return false;
    if (buffer.size() < info.TotalMetadataBufferSize) buffer.resize(info.TotalMetadataBufferSize);

    auto required = UINT{0};
    if (duplication->GetFrameMoveRects(info.TotalMetadataBufferSize,
                                       reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(buffer.data()),
                                       &required) == S_OK) {
        auto const* const moves = reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT const*>(buffer.data());
        for (UINT index = 0; index < required / sizeof(DXGI_OUTDUPL_MOVE_RECT); ++index) {
            auto const& move = moves[index];
            // The content moved has the destination's size; it left from SourcePoint, which the API gives as a corner.
            auto const width  = move.DestinationRect.right - move.DestinationRect.left;
            auto const height = move.DestinationRect.bottom - move.DestinationRect.top;
            auto const from   = RECT{move.SourcePoint.x, move.SourcePoint.y, move.SourcePoint.x + width, move.SourcePoint.y + height};
            if (intersects(move.DestinationRect, sheet) or intersects(from, sheet)) return true;
        }
    } else {
        return true;
    }
    if (duplication->GetFrameDirtyRects(info.TotalMetadataBufferSize, reinterpret_cast<RECT*>(buffer.data()), &required) == S_OK) {
        auto const* const dirty = reinterpret_cast<RECT const*>(buffer.data());
        for (UINT index = 0; index < required / sizeof(RECT); ++index)
            if (intersects(dirty[index], sheet)) return true;
        return false;
    }
    return true;
}

#endif

} // namespace

QRect frameRectFor(QRect const screenRect, QSize const frameSize, ScreenRotation const rotation)
{
    switch (rotation) {
        case ScreenRotation::Rotate90:
            // The frame is the screen turned clockwise: the screen's left column becomes the frame's top row's end.
            return {frameSize.width() - screenRect.y() - screenRect.height(), screenRect.x(), screenRect.height(), screenRect.width()};
        case ScreenRotation::Rotate180:
            return {frameSize.width() - screenRect.x() - screenRect.width(),
                    frameSize.height() - screenRect.y() - screenRect.height(),
                    screenRect.width(),
                    screenRect.height()};
        case ScreenRotation::Rotate270:
            return {screenRect.y(), frameSize.height() - screenRect.x() - screenRect.width(), screenRect.height(), screenRect.width()};
        case ScreenRotation::Identity: break;
    }
    return screenRect;
}

#ifdef _WIN32

struct GlassDuplication::Impl {
    Impl(QString name, QRect rectangle)
        : deviceName(std::move(name)), desktop(std::move(rectangle))
    {}

    /// @brief Open the device and the duplication, if they are not open and a retry is due.
    /// @return True when the session can read. Runs on the worker thread.
    bool ensure();
    /// @brief Remember a failure, release what was open, and set the cooldown or the permanence.
    void fail(HRESULT hr, bool permanentFailure);
    /// @brief Make the staging texture for a @p size frame rectangle, reusing it when it fits.
    bool createStaging(QSize size);
    /// @brief Release the device, the duplication and the textures. Thread and state of failure are left alone.
    void release();

    QString deviceName;
    QRect   desktop;
    /// True while the duplication is open, read by usable() from the GUI thread.
    std::atomic<bool> operational{false};
    /// True after a refusal that no retry can fix; read by usable().
    std::atomic<bool> permanent{false};
    /// The tick a failed session may be tried again, in GetTickCount64() milliseconds; 0 when none.
    std::atomic<ULONGLONG> retryAt{0};

    ComPtr<ID3D11Device>           device;
    ComPtr<ID3D11DeviceContext>    context;
    ComPtr<IDXGIOutputDuplication> duplication;
    DXGI_OUTDUPL_DESC              desc{};
    QSize                          frameSize;
    ScreenRotation                 rotation = ScreenRotation::Identity;
    /// The copied frame rectangle, in a staging texture that maps it back to the CPU.
    ComPtr<ID3D11Texture2D> staging;
    QSize                   stagingSize;
    /// Scratch the frame metadata is read into; a few hundred bytes to a few kilobytes, reused every frame.
    std::vector<BYTE> metadata;
    /// The first acquired frame of a session is the desktop as it stood before the duplication existed, and on the
    /// rotated monitor it came back black. The GDI picture the card already holds is current, so this one is dropped and
    /// the next frame -- a frame the desktop really produced while the session watched -- is the first to publish.
    bool firstFrame = true;
};

bool GlassDuplication::Impl::ensure()
{
    if (duplication != nullptr) return true;
    if (permanent) return false;
    if (GetTickCount64() < retryAt) return false;

    release();
    auto factory = ComPtr<IDXGIFactory1>{};
    if (auto const hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory)); FAILED(hr)) {
        fail(hr, false);
        return false;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        auto adapter = ComPtr<IDXGIAdapter1>{};
        if (factory->EnumAdapters1(adapterIndex, &adapter) != S_OK) break;
        auto adapterDesc = DXGI_ADAPTER_DESC1{};
        if (FAILED(adapter->GetDesc1(&adapterDesc))) continue;
        if ((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) continue; // WARP lists no output to duplicate.

        for (UINT outputIndex = 0;; ++outputIndex) {
            auto output = ComPtr<IDXGIOutput>{};
            if (adapter->EnumOutputs(outputIndex, &output) != S_OK) break;
            auto outputDesc = DXGI_OUTPUT_DESC{};
            if (FAILED(output->GetDesc(&outputDesc))) continue;
            auto const name = QString::fromWCharArray(outputDesc.DeviceName);
            auto const rect = QRect(outputDesc.DesktopCoordinates.left, outputDesc.DesktopCoordinates.top, outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left, outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top);
            if (name.compare(deviceName, Qt::CaseInsensitive) != 0 and rect != desktop) continue;
            // Qt and DXGI can name the same output differently; a matching rectangle is enough from here, and the
            // session keeps the rectangle it found rather than the one it was given.
            desktop = rect;

            auto output1 = ComPtr<IDXGIOutput1>{};
            if (auto const hr = output.As(&output1); FAILED(hr)) {
                fail(hr, true);
                return false;
            }
            // The device must be built on the adapter that owns the output: a device on another one is refused, which is
            // the hybrid-machine refusal ADR 0040 recorded as a dead end.
            auto const made = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
            if (FAILED(made)) {
                fail(made, false);
                return false;
            }
            if (auto const hr = output1->DuplicateOutput(device.Get(), &duplication); FAILED(hr)) {
                fail(hr, refusalIsPermanent(hr));
                return false;
            }
            duplication->GetDesc(&desc);
            frameSize = QSize(static_cast<int>(desc.ModeDesc.Width), static_cast<int>(desc.ModeDesc.Height));
            rotation  = rotationOf(desc.Rotation);
            // The copy below reads the frame as BGRA, so an output in another format (an HDR desktop, for one) is left
            // to GDI rather than shown in the wrong colours.
            if (frameSize.isEmpty() or (desc.ModeDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM and desc.ModeDesc.Format != DXGI_FORMAT_B8G8R8X8_UNORM)) {
                // A format or mode can change back (an HDR desktop switched off), so this is retried rather than
                // remembered forever like the panel's refusal.
                fail(E_FAIL, false);
                return false;
            }
            retryAt     = 0;
            operational = true;
            // The mode is in the desktop's orientation; the frame itself is measured on the first acquire, which is
            // where a rotated output's true size shows up. Nothing is logged as "frame" until then.
            LENS_INFO("glass duplication on {}: opened for a {}x{} mode, {}, the live refresh can follow it", deviceName.toStdString(), frameSize.width(), frameSize.height(), rotationText(rotation));
            return true;
        }
    }
    // The output is not in the inventory any more: a display was unplugged between the sheet's read and now. The screen
    // may come back, so this is retried on the cooldown like the other failures that can pass.
    fail(E_FAIL, false);
    return false;
}

void GlassDuplication::Impl::fail(HRESULT const hr, bool const permanentFailure)
{
    release();
    operational = false;
    permanent   = permanentFailure;
    retryAt     = permanentFailure ? 0 : GetTickCount64() + kRetryMs;
    // A refusal is a fact about the machine's screens, not an error the reader can act on: Windows Graphics Capture or
    // GDI takes over. Log it once per attempt and only with the layout, never with pixels (SECURITY.md).
    LENS_INFO("glass duplication on {}: unavailable (0x{:08X}), the glass falls back on this screen{}",
              deviceName.toStdString(),
              static_cast<unsigned long>(hr),
              permanentFailure ? " from now on" : " for now");
}

void GlassDuplication::Impl::release()
{
    operational = false;
    // A session opened again gets its own unreliable first frame.
    firstFrame = true;
    duplication.Reset();
    staging.Reset();
    stagingSize = {};
    context.Reset();
    device.Reset();
}

bool GlassDuplication::Impl::createStaging(QSize const size)
{
    if (staging != nullptr and stagingSize == size) return true;
    staging.Reset();
    stagingSize = {};

    auto description             = D3D11_TEXTURE2D_DESC{};
    description.Width            = static_cast<UINT>(size.width());
    description.Height           = static_cast<UINT>(size.height());
    description.MipLevels        = 1;
    description.ArraySize        = 1;
    description.Format           = desc.ModeDesc.Format;
    description.SampleDesc.Count = 1;
    description.Usage            = D3D11_USAGE_STAGING;
    description.BindFlags        = 0;
    description.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
    if (auto const hr = device->CreateTexture2D(&description, nullptr, &staging); FAILED(hr)) {
        LENS_INFO("glass duplication on {}: staging texture {}x{} refused (0x{:08X})", deviceName.toStdString(), size.width(), size.height(), static_cast<unsigned long>(hr));
        return false;
    }
    stagingSize = size;
    return true;
}

#else // !_WIN32

struct GlassDuplication::Impl {};

#endif // _WIN32

GlassDuplication::GlassDuplication(QString deviceName, QRect desktop)
#ifdef _WIN32
    : impl_(std::make_unique<Impl>(std::move(deviceName), std::move(desktop)))
#else
    : impl_(nullptr)
#endif
{
}

GlassDuplication::~GlassDuplication() = default;

QString const& GlassDuplication::deviceName() const
{
#ifdef _WIN32
    return impl_->deviceName;
#else
    static auto const empty = QString{};
    return empty;
#endif
}

bool GlassDuplication::canFollow() const
{
#ifdef _WIN32
    auto const& session = *impl_;
    if (session.permanent.load()) return false;
    if (session.operational.load()) return true;
    return GetTickCount64() >= session.retryAt.load();
#else
    return false;
#endif
}

DesktopRead GlassDuplication::read(QRect const native, double const scale, int const timeoutMs)
{
#ifdef _WIN32
    auto& session = *impl_;
    if (not session.ensure()) return {.refused = true};

    auto       frame    = DXGI_OUTDUPL_FRAME_INFO{};
    auto       resource = ComPtr<IDXGIResource>{};
    auto const hr       = session.duplication->AcquireNextFrame(static_cast<UINT>(std::max(0, timeoutMs)), &frame, &resource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) return {};
    if (FAILED(hr)) {
        session.fail(hr, false);
        return {.refused = true};
    }
    QElapsedTimer copyTimer;
    copyTimer.start();
    auto texture = ComPtr<ID3D11Texture2D>{};
    if (FAILED(resource.As(&texture))) {
        session.duplication->ReleaseFrame();
        return {};
    }
    // The duplication description's mode is in the desktop's orientation; the acquired surface is not (the rotated
    // monitor of the measurement machine reports 1440 x 2560 there and hands over 2560 x 1440). The frame's own
    // description is the only one that says what the copy boxes are measured against, and it is logged once because a
    // rotated output is exactly the case where the two disagree.
    auto sourceDesc = D3D11_TEXTURE2D_DESC{};
    texture->GetDesc(&sourceDesc);
    auto const actual = QSize(static_cast<int>(sourceDesc.Width), static_cast<int>(sourceDesc.Height));
    if (session.firstFrame)
        LENS_INFO("glass duplication on {}: the frame is {}x{}, the desktop {}x{}", session.deviceName.toStdString(), actual.width(), actual.height(), session.desktop.width(), session.desktop.height());
    if (actual != session.frameSize) session.frameSize = actual;

    if (session.firstFrame) {
        session.firstFrame = false;
        session.duplication->ReleaseFrame();
        return {};
    }

    auto const screen = QRect(QPoint(0, 0), QSize(session.desktop.width(), session.desktop.height()));
    auto       local  = native.translated(-session.desktop.x(), -session.desktop.y());
    // A sheet that is not wholly on this output has no frame to cut: the caller's GDI read clamps to the screen.
    if (local.isEmpty() or not screen.contains(local)) {
        session.duplication->ReleaseFrame();
        return {};
    }
    auto const box       = frameRectFor(local, session.frameSize, session.rotation);
    auto const frameRect = box.intersected(QRect(QPoint(0, 0), session.frameSize));
    if (frameRect.isEmpty()) {
        session.duplication->ReleaseFrame();
        return {};
    }
    auto const sheetInFrame = RECT{frameRect.left(), frameRect.top(), frameRect.left() + frameRect.width(), frameRect.top() + frameRect.height()};
    if (not frameTouches(session.duplication.Get(), frame, sheetInFrame, session.metadata)) {
        session.duplication->ReleaseFrame();
        return {};
    }
    if (not session.createStaging(frameRect.size())) {
        // The frame is still acquired here; it goes before the device does.
        session.duplication->ReleaseFrame();
        session.fail(E_FAIL, false);
        return {.refused = true};
    }

    // The acquired texture cannot be mapped; the copy the probe measured at 0.75 ms for a sheet is the way back.
    // The staging texture is the copy's destination directly: an intermediate default-usage texture bought nothing
    // and walked the same pixels twice on the GPU (docs/adr/0074).
    auto const source = D3D11_BOX{static_cast<UINT>(frameRect.x()), static_cast<UINT>(frameRect.y()), 0, static_cast<UINT>(frameRect.x() + frameRect.width()), static_cast<UINT>(frameRect.y() + frameRect.height()), 1};
    session.context->CopySubresourceRegion(session.staging.Get(), 0, 0, 0, 0, texture.Get(), 0, &source);
    auto mapped = D3D11_MAPPED_SUBRESOURCE{};
    if (FAILED(session.context->Map(session.staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        session.duplication->ReleaseFrame();
        session.fail(E_FAIL, false);
        return {.refused = true};
    }
    // Read the mapped rows where they lie. Copying them into a QImage and turning that copy is two full passes over
    // the sheet before it can be published; the view lets the one turn write the destination directly (docs/adr/0074).
    auto const sourceView = QImage(static_cast<uchar const*>(mapped.pData),
                                   frameRect.width(),
                                   frameRect.height(),
                                   static_cast<qsizetype>(mapped.RowPitch),
                                   QImage::Format_RGB32);
    auto       image      = QImage{};
    switch (session.rotation) {
        case ScreenRotation::Rotate90: image = sourceView.transformed(QTransform().rotate(-90), Qt::FastTransformation); break;
        case ScreenRotation::Rotate180: image = sourceView.transformed(QTransform().rotate(180), Qt::FastTransformation); break;
        case ScreenRotation::Rotate270: image = sourceView.transformed(QTransform().rotate(90), Qt::FastTransformation); break;
        case ScreenRotation::Identity: image = sourceView.copy(); break;
    }
    session.context->Unmap(session.staging.Get(), 0);
    session.duplication->ReleaseFrame();

    if (image.format() != QImage::Format_RGB32) image = image.convertToFormat(QImage::Format_RGB32);
    image.setDevicePixelRatio(scale);
    return {.image = std::move(image), .newFrame = true, .copyMs = static_cast<double>(copyTimer.nsecsElapsed()) / 1.0e6};
#else
    Q_UNUSED(native)
    Q_UNUSED(scale)
    return {.refused = true};
#endif
}

} // namespace lens::app
