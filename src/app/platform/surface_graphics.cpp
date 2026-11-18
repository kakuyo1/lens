/**
 * @file surface_graphics.cpp
 * @brief The shared device and the hidden-surface release policy: see surface_graphics.h.
 */

#include "surface_graphics.h"

#include <QChildEvent>
#include <QCoreApplication>
#include <QQuickGraphicsDevice>
#include <QTimer>

#include "util/log.h"

namespace lens::app {

#ifdef _WIN32
using Microsoft::WRL::ComPtr;

bool SharedGraphics::create()
{
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    auto*                   raw      = static_cast<ID3D11Device*>(nullptr);
    auto                    level    = D3D_FEATURE_LEVEL{};
    auto const              hr       = D3D11CreateDevice(nullptr,
                                      D3D_DRIVER_TYPE_HARDWARE,
                                      nullptr,
                                      D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                      levels,
                                      2,
                                      D3D11_SDK_VERSION,
                                      &raw,
                                      &level,
                                      nullptr);
    if (FAILED(hr) or raw == nullptr) {
        LENS_WARN("shared D3D11 device unavailable (0x{:08X}); surfaces keep their own devices",
                  static_cast<unsigned long>(hr));
        return false;
    }
    device_.Attach(raw);
    device_->GetImmediateContext(context_.GetAddressOf());
    LENS_INFO("shared D3D11 device up, feature level 0x{:X}", static_cast<unsigned>(level));
    return context_ != nullptr;
}

void SharedGraphics::adopt(QQuickWindow* window)
{
    window->setGraphicsDevice(QQuickGraphicsDevice::fromDeviceAndContext(device_.Get(), context_.Get()));
    QObject::connect(window, &QQuickWindow::beforeFrameBegin, window, [this] { frameMutex_.lock(); }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::afterFrameEnd, window, [this] { frameMutex_.unlock(); }, Qt::DirectConnection);
    QObject::connect(window, &QQuickWindow::sceneGraphInvalidated, window, [this] { noteInvalidation(); }, Qt::DirectConnection); // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks): the queued recovery lambda below becomes a QMetaCallEvent the event loop owns; the analyzer reads the allocation as a leak
    windows_.append(window);
    LENS_DEBUG("surface tuned: {}", window->objectName().toStdString());
}

void SharedGraphics::noteInvalidation()
{
    if (device_ == nullptr or device_->GetDeviceRemovedReason() == S_OK)
        return;
    if (recovering_.exchange(1) == 0)
        QMetaObject::invokeMethod(qApp, [this] { recover(); }, Qt::QueuedConnection);
}

void SharedGraphics::recover()
{
    auto const reason = static_cast<unsigned long>(device_->GetDeviceRemovedReason());
    if (not create()) {
        recovering_.store(0);
        return;
    }
    LENS_WARN("shared D3D11 device was removed (0x{:08X}); rebuilt and re-adopted on {} window(s)",
              reason,
              windows_.count());
    for (const QPointer<QQuickWindow>& window : windows_) {
        if (window == nullptr)
            continue;
        bool const wasVisible = window->isVisible();
        if (wasVisible)
            window->hide();
        window->setGraphicsDevice(QQuickGraphicsDevice::fromDeviceAndContext(device_.Get(), context_.Get()));
        if (wasVisible)
            window->show();
    }
    recovering_.store(0);
}
#endif

namespace {

/// How long a surface stays hidden before its graphics resources are released. Panels hide and
/// re-show while they are being repositioned, and rebuilding the scene graph on every such hop
/// costs more than the memory it saves, so a window that is back within this window is left alone.
constexpr int kReleaseDelayMs = 500;

/// @brief Wire one surface: the shared device and the frame lock when there is one, then the
///        release policy. Must run before the window's scene graph initializes.
void tuneSurface(QQuickWindow* window, [[maybe_unused]] SharedGraphics* shared)
{
#ifdef _WIN32
    if (shared != nullptr)
        shared->adopt(window);
#endif
    window->setPersistentGraphics(false);
    window->setPersistentSceneGraph(false);
    QObject::connect(window, &QWindow::visibleChanged, window, [window] {
        if (window->isVisible())
            return;
        QTimer::singleShot(kReleaseDelayMs, window, [window] {
            if (not window->isVisible())
                window->releaseResources();
        });
    });
}

/// @brief Reaches windows QML creates after startup: the tune must cover every surface, not only
///        the ones that existed when the QML loaded. QML Window items are QQuickWindow subclasses
///        parented into the QML object tree, and Controls popups open windows of their own at run
///        time, so ChildAdded on any object is the one hook that sees them all.
class LateWindowTuner : public QObject {
public:
    LateWindowTuner(QObject* parent, SharedGraphics* shared)
        : QObject(parent), shared_(shared)
    {}

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::ChildAdded) {
            if (auto* window = qobject_cast<QQuickWindow*>(static_cast<QChildEvent*>(event)->child()))
                tuneSurface(window, shared_);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    SharedGraphics* shared_;
};

} // namespace

void releaseHiddenSurfaces(QObject* root, SharedGraphics* shared)
{
    if (auto* self = qobject_cast<QQuickWindow*>(root))
        tuneSurface(self, shared);
    for (auto* window : root->findChildren<QQuickWindow*>())
        tuneSurface(window, shared);
    // Installed after the first pass so no window is tuned twice: a second frame-lock connection
    // would lock the same mutex twice on one render thread.
    qApp->installEventFilter(new LateWindowTuner(root, shared));
}

} // namespace lens::app