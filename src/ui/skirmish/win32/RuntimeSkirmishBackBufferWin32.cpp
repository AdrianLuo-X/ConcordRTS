#include "ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h"
#include <algorithm>
#include <limits>
namespace concord::ui::win32 {
bool SkirmishBackBufferWin32::release(SkirmishGdiReleaseTrace* trace) noexcept {
    bool success=true;
    if(dc_ && old_ && old_!=HGDI_ERROR) {
        const bool restored=api_.select(dc_,old_)==bitmap_;
        if(trace)trace->record(0,restored);
        if(restored)old_=nullptr;
        success=restored&&success;
    }
    if(bitmap_) {
        const bool disposed=api_.deleteObject(bitmap_)!=FALSE;
        if(trace)trace->record(1,disposed);
        if(disposed){bitmap_=nullptr;bits_=nullptr;}
        success=disposed&&success;
    }
    if(dc_) {
        const bool disposed=api_.deleteDc(dc_)!=FALSE;
        if(trace)trace->record(2,disposed);
        if(disposed){dc_=nullptr;old_=nullptr;}
        success=disposed&&success;
    }
    // Failed deletion retains the precise remaining handle for a later retry.
    // A live DC no longer selects our bitmap after successful restoration.
    if(!dc_&&!bitmap_){width_=height_=0;stride_=0;}
    return success;
}
bool SkirmishBackBufferWin32::ensure(HDC reference,std::int32_t w,std::int32_t h) noexcept {
    if(ready()&&width_==w&&height_==h)return true;
    if(!release()||w<=0||h<=0)return false;
    const auto width=static_cast<std::size_t>(w),height=static_cast<std::size_t>(h);
    if(width>std::numeric_limits<std::size_t>::max()/4
        ||height>std::numeric_limits<std::size_t>::max()/(width*4))return false;
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;
    info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    info.bmiHeader.biCompression=BI_RGB;
    dc_=api_.createDc(reference);if(!dc_)return false;
    void* bits=nullptr;
    bitmap_=api_.createDib(reference,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    bits_=static_cast<unsigned char*>(bits);
    if(!bitmap_||!bits_){release();return false;}
    old_=api_.select(dc_,bitmap_);
    if(!old_||old_==HGDI_ERROR){old_=nullptr;release();return false;}
    width_=w;height_=h;stride_=width*4;return true;
}
bool SkirmishBackBufferWin32::upload(ConstMenuPixelSurfaceView source) noexcept {
    if(!ready()||!source.valid()||source.width!=width_||source.height!=height_)return false;
    if(api_.flush()==FALSE)return false;
    for(std::int32_t y=0;y<height_;++y)
        std::copy_n(source.bytes.data()+static_cast<std::size_t>(y)*source.rowStride,
            stride_,bits_+static_cast<std::size_t>(y)*stride_);
    return true;
}
} // namespace concord::ui::win32
