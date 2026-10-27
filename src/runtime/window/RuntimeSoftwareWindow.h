#pragma once
#include "runtime/presentation/SoftwareFrame.h"
#include "ui/input/RuntimeInputPixelValues.h"
#include <cstdint>
#include <memory>
#include <string>

namespace concord::window {
enum class SoftwareWindowKey { Other, Escape, Return, Space, Up, Down, Home, End };
enum class SoftwareWindowEventKind {
    Resize, Expose, Minimize, Restore, Move, LeftDown, LeftUp, DoubleClick,
    RightUp, Leave, CaptureLost, FocusLost, KeyDown, Wheel, Close
};
struct SoftwareWindowEvent {
    SoftwareWindowEventKind kind = SoftwareWindowEventKind::Expose;
    RuntimeInputPixelPoint point{};
    std::int32_t width=0, height=0, notches=0;
    SoftwareWindowKey key=SoftwareWindowKey::Other;
    bool repeat=false;
};
class SoftwareWindowEvents {
public:
    virtual ~SoftwareWindowEvents()=default;
    virtual void windowEvent(const SoftwareWindowEvent&)=0;
};
enum class SoftwareWindowDrawing { BitmapFrame, NativeSystemFont, D3D, SdlRenderer };
struct SoftwareWindowOptions {
    std::int32_t width=800, height=600;
    SoftwareWindowDrawing drawing=SoftwareWindowDrawing::BitmapFrame;
    bool embeddedParent=false;
    bool nativeRandomEditor=false;
};
// Explicit creation-time capability contract. A missing/unsupported provider
// is an error; callers must never silently substitute the reference backend.
inline std::string softwareWindowUnsupported(const SoftwareWindowOptions& options) {
    if(options.drawing!=SoftwareWindowDrawing::BitmapFrame)
        return "SDL3 window supports the completed bitmap software frame only; system fonts, D3D and SDL rendering require another verified provider";
    if(options.embeddedParent) return "SDL3 skirmish window does not support an embedded native parent";
    if(options.nativeRandomEditor) return "SDL3 skirmish window does not support the native random-map editor";
    if(options.width<=0||options.height<=0) return "Window pixel dimensions must be positive";
    return {};
}
class SoftwareWindow {
public:
    virtual ~SoftwareWindow()=default;
    virtual presentation::FramePresenter& presenter() noexcept=0;
    virtual bool poll()=0;
    virtual void wait(std::uint32_t milliseconds)=0;
    virtual std::uint64_t nowMs() const noexcept=0;
    virtual bool focus()=0;
    virtual bool capture()=0;
    virtual void releaseCapture()=0;
    virtual bool captured() const noexcept=0;
    virtual bool alive() const noexcept=0;
    virtual bool drawable() const noexcept=0;
    virtual void close()=0;
    virtual bool size(std::int32_t&,std::int32_t&) const noexcept=0;
    virtual bool resize(std::int32_t,std::int32_t)=0;
    virtual bool minimize()=0;
    virtual bool restore()=0;
    virtual const std::string& error() const noexcept=0;
};
// SDL types, library loading, native handles and surface format conversion stay
// in this factory's private implementation. Events and frames are synchronous.
std::unique_ptr<SoftwareWindow> createSdl3SoftwareWindow(
    const SoftwareWindowOptions&, SoftwareWindowEvents&,
    presentation::FramePresentStatistics&, std::string& error);
} // namespace concord::window
