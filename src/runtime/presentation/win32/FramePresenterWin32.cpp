#include "runtime/presentation/win32/FramePresenterWin32.h"
namespace concord::presentation::win32 {
bool composeWindowDeviceOverlayWin32(HWND window,
    const concord::ui::win32::MainBackBufferWin32& clean,
    concord::ui::win32::MainBackBufferWin32& completed,
    concord::ui::win32::SkirmishBackBufferWin32& savedOverlay,
    RuntimeInputPixelRect r,DrawWindowOverlay draw,void* context,
    WindowOverlayGdiApi api) noexcept {
    if(!window||!draw||GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()
        ||!clean.pixelView().valid()||!completed.pixelView().valid()
        ||clean.width!=completed.width||clean.height!=completed.height
        ||GetCurrentObject(clean.memoryDc,OBJ_BITMAP)!=clean.bitmap
        ||GetCurrentObject(completed.memoryDc,OBJ_BITMAP)!=completed.bitmap
        ||r.left<0||r.top<0||r.right<=r.left||r.bottom<=r.top
        ||r.right>clean.width||r.bottom>clean.height)return false;
    const int width=r.right-r.left,height=r.bottom-r.top;
    HDC native=api.getDc(window);if(!native)return false;
    const int savedState=SaveDC(native);
    if(savedState==0){(void)api.releaseDc(window,native);return false;}
    bool success=false,touched=false;
    try {
        RECT visible{};const int clip=GetClipBox(native,&visible);
        const bool wholeOverlay=clip==SIMPLEREGION&&visible.left<=r.left
            &&visible.top<=r.top&&visible.right>=r.right&&visible.bottom>=r.bottom;
        if(wholeOverlay&&savedOverlay.ensureDeviceBitmap(native,width,height)
            &&api.bitBlt(savedOverlay.dc(),0,0,width,height,native,r.left,r.top,SRCCOPY)
            &&api.flush()) {
            // Preserve exactly the original device text rasterization. The
            // old window rectangle is restored before the complete DIB leaves
            // this bridge, including every exception/readback failure path.
            touched=true;
            if(api.bitBlt(native,r.left,r.top,width,height,clean.memoryDc,r.left,r.top,SRCCOPY)) {
                draw(native,context);
                if(api.flush()&&api.bitBlt(completed.memoryDc,r.left,r.top,width,height,
                    native,r.left,r.top,SRCCOPY)&&api.flush())success=true;
            }
        }
    } catch(...) {success=false;}
    // A nested painter can throw while its clipping/viewport is changed.
    // Restore the complete original DC state before restoring the window ROI.
    if(RestoreDC(native,savedState)==FALSE)success=false;
    if(touched) {
        const bool restored=api.bitBlt(native,r.left,r.top,width,height,savedOverlay.dc(),0,0,SRCCOPY)!=FALSE;
        const bool flushed=api.flush()!=FALSE;success=restored&&flushed&&success;
    }
    const bool released=api.releaseDc(window,native)!=0;
    return released&&success;
}
DibFramePresenterWin32::DibFramePresenterWin32(HDC target,
    const concord::ui::win32::MainBackBufferWin32& source,FramePresentStatistics& counters,
    DibPresentGdiApi api) noexcept : FramePresenter(counters),target_(target),source_(source),
        ownerThread_(GetCurrentThreadId()),api_(api) {}
bool DibFramePresenterWin32::onOwningThread() const noexcept {
    return GetCurrentThreadId()==ownerThread_;
}
bool DibFramePresenterWin32::submitCompleteFrame(const SoftwareFrameView& frame) noexcept {
    // Metadata checks precede touching any potentially expired pixel storage.
    if(!target_||!source_.owns(frame.pixels)||GetCurrentObject(source_.memoryDc,OBJ_BITMAP)!=source_.bitmap)return false;
    return api_.bitBlt(target_,0,0,frame.pixels.width,frame.pixels.height,
        source_.memoryDc,0,0,SRCCOPY)!=FALSE && api_.flush()!=FALSE;
}
} // namespace concord::presentation::win32