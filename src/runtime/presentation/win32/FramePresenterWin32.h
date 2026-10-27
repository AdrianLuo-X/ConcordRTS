#pragma once
// Private factory/backend interoperation. Window DC is borrowed only between
// BeginPaint/EndPaint on its owning UI thread. It never enters the public API.
#include "runtime/presentation/SoftwareFrame.h"
#include "ui/surface/win32/RuntimeMainBackBufferWin32.h"
namespace concord::presentation::win32 {
struct DibPresentGdiApi {
    decltype(&::BitBlt) bitBlt=&::BitBlt;
    decltype(&::GdiFlush) flush=&::GdiFlush;
};
// Native font rasterization is device-dependent even at identical pixel sizes.
// This private bridge snapshots/restores only the overlay rectangle on the
// existing UI-owned window. It never presents/counts a frame or retains pixels.
struct WindowOverlayGdiApi {
    decltype(&::GetDC) getDc=&::GetDC;
    decltype(&::ReleaseDC) releaseDc=&::ReleaseDC;
    decltype(&::BitBlt) bitBlt=&::BitBlt;
    decltype(&::GdiFlush) flush=&::GdiFlush;
};
using DrawWindowOverlay=void(*)(HDC,void*);
bool composeWindowDeviceOverlayWin32(HWND window,
    const concord::ui::win32::MainBackBufferWin32& clean,
    concord::ui::win32::MainBackBufferWin32& completed,
    concord::ui::win32::SkirmishBackBufferWin32& savedOverlay,
    RuntimeInputPixelRect bounds,DrawWindowOverlay draw,void* context,
    WindowOverlayGdiApi api={}) noexcept;
class DibFramePresenterWin32 final : public FramePresenter {
public:
    DibFramePresenterWin32(HDC target,const concord::ui::win32::MainBackBufferWin32& source,
        FramePresentStatistics& counters,DibPresentGdiApi api={}) noexcept;
private:
    bool onOwningThread() const noexcept override;
    bool submitCompleteFrame(const SoftwareFrameView&) noexcept override;
    HDC target_;
    const concord::ui::win32::MainBackBufferWin32& source_;
    DWORD ownerThread_;
    DibPresentGdiApi api_;
};
} // namespace concord::presentation::win32