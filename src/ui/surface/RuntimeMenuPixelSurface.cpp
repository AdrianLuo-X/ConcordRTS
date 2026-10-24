#include "ui/surface/RuntimeMenuPixelSurface.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace concord::ui {
bool MenuPixelBuffer::reset(std::int32_t w,std::int32_t h,std::size_t stride) noexcept {
    if(w<=0 || h<=0 || generation_==std::numeric_limits<std::uint64_t>::max()) return false;
    const auto width=static_cast<std::size_t>(w),height=static_cast<std::size_t>(h);
    if(width>std::numeric_limits<std::size_t>::max()/4) return false;
    const auto row=width*4;
    if(stride==0) stride=row;
    if(stride<row || height>std::numeric_limits<std::size_t>::max()/stride) return false;
    const auto size=height*stride;
    try {
        auto p=allocate_ ? allocate_(size) : std::vector<unsigned char>(size,0);
        if(p.size()!=size) return false;
        pixels_.swap(p);width_=w;height_=h;stride_=stride;++generation_;return true;
    } catch(const std::bad_alloc&) {return false;
    } catch(const std::length_error&) {return false;}
}
void MenuPixelBuffer::release() noexcept {
    std::vector<unsigned char>().swap(pixels_);width_=height_=0;stride_=0;
    if(generation_!=std::numeric_limits<std::uint64_t>::max()) ++generation_;
}
bool MenuPixelBuffer::owns(MenuPixelSurfaceView b) const noexcept {
    // Test generation before comparing a stale pointer; never dereference it.
    return b.generation==generation_ && !pixels_.empty() && b.bytes.data()==pixels_.data()
        && b.bytes.size()==pixels_.size() && b.width==width_ && b.height==height_ && b.rowStride==stride_;
}
void clearMenuPixels(MenuPixelSurfaceView s) {
    if(!s.valid()) return;
    for(std::int32_t y=0;y<s.height;++y) for(std::int32_t x=0;x<s.width;++x) {
        const auto at=static_cast<std::size_t>(y)*s.rowStride+static_cast<std::size_t>(x)*4;
        s.bytes[at]=s.bytes[at+1]=s.bytes[at+2]=0;s.bytes[at+3]=255;
    }
}
void fillMenuPixels(MenuPixelSurfaceView s,const RuntimeInputPixelRect& r,RuntimeRgbColorValue c) {
    if(!s.valid()) return;
    for(auto y=std::clamp(r.top,0,s.height);y<std::clamp(r.bottom,0,s.height);++y)
        for(auto x=std::clamp(r.left,0,s.width);x<std::clamp(r.right,0,s.width);++x) {
            const auto at=static_cast<std::size_t>(y)*s.rowStride+static_cast<std::size_t>(x)*4;
            s.bytes[at]=c.blue();s.bytes[at+1]=c.green();s.bytes[at+2]=c.red();s.bytes[at+3]=255;
        }
}
void darkenMenuPixels(MenuPixelSurfaceView s,const RuntimeInputPixelRect& r,unsigned int opacity) {
    if(!s.valid()) return;const auto keep=255U-std::min(255U,opacity);
    for(auto y=std::clamp(r.top,0,s.height);y<std::clamp(r.bottom,0,s.height);++y)
        for(auto x=std::clamp(r.left,0,s.width);x<std::clamp(r.right,0,s.width);++x) {
            const auto at=static_cast<std::size_t>(y)*s.rowStride+static_cast<std::size_t>(x)*4;
            for(std::size_t ch=0;ch<3;++ch)
                s.bytes[at+ch]=static_cast<unsigned char>(static_cast<unsigned int>(s.bytes[at+ch])*keep/255U);
            s.bytes[at+3]=255;
        }
}
void frameMenuPixels(MenuPixelSurfaceView s,const RuntimeInputPixelRect& r,RuntimeRgbColorValue c) {
    fillMenuPixels(s,{r.left,r.top,r.right,r.top+1},c);
    fillMenuPixels(s,{r.left,r.bottom-1,r.right,r.bottom},c);
    fillMenuPixels(s,{r.left,r.top,r.left+1,r.bottom},c);
    fillMenuPixels(s,{r.right-1,r.top,r.right,r.bottom},c);
}
void blitMenuPixels(MenuPixelSurfaceView d,ConstMenuPixelSurfaceView s,
    std::int32_t left,std::int32_t top,std::int32_t clipHeight) {
    if(!d.valid()||!s.valid()) return;const auto rows=std::min(s.height,clipHeight);
    for(std::int32_t y=0;y<rows;++y) {
        const auto dy=static_cast<std::int64_t>(top)+y;if(dy<0||dy>=d.height) continue;
        for(std::int32_t x=0;x<s.width;++x) {
            const auto dx=static_cast<std::int64_t>(left)+x;if(dx<0||dx>=d.width) continue;
            const auto src=static_cast<std::size_t>(y)*s.rowStride+static_cast<std::size_t>(x)*4;
            const auto alpha=s.bytes[src+3];if(alpha==0) continue;
            const auto dst=static_cast<std::size_t>(dy)*d.rowStride+static_cast<std::size_t>(dx)*4;
            if(alpha==255) {std::copy_n(s.bytes.data()+src,4,d.bytes.data()+dst);d.bytes[dst+3]=255;}
            else for(std::size_t ch=0;ch<3;++ch)
                d.bytes[dst+ch]=static_cast<unsigned char>((static_cast<unsigned int>(s.bytes[src+ch])*alpha
                    +static_cast<unsigned int>(d.bytes[dst+ch])*(255U-alpha))/255U);
        }
    }
}
void scaleMenuPixels(MenuPixelSurfaceView d,ConstMenuPixelSurfaceView s,const RuntimeInputPixelRect& r) {
    if(!d.valid()||!s.valid()) return;
    const auto w=static_cast<std::int64_t>(r.right)-r.left,h=static_cast<std::int64_t>(r.bottom)-r.top;
    if(w<=0||h<=0) return;
    for(std::int64_t y=0;y<h;++y) {
        const auto dy=r.top+y;if(dy<0||dy>=d.height) continue;const auto sy=y*s.height/h;
        for(std::int64_t x=0;x<w;++x) {
            const auto dx=r.left+x;if(dx<0||dx>=d.width) continue;const auto sx=x*s.width/w;
            const auto src=static_cast<std::size_t>(sy)*s.rowStride+static_cast<std::size_t>(sx)*4;
            const auto dst=static_cast<std::size_t>(dy)*d.rowStride+static_cast<std::size_t>(dx)*4;
            std::copy_n(s.bytes.data()+src,4,d.bytes.data()+dst);d.bytes[dst+3]=255;
        }
    }
}
} // namespace concord::ui
