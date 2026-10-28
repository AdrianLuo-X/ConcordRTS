#include "ui/surface/win32/MapCompositionWin32.h"
namespace concord::ui::win32 {
using namespace concord::render;
HDC MapCompositionWin32::begin(HDC reference,const std::uint32_t* pixels,int width,int height) noexcept {
    ready_=false;
    if(!reference||!pixels||width<=0||height<=0||width>MapDrawList::maxExtent||height>MapDrawList::maxExtent||!GdiFlush())return nullptr;
    try {
        scene_.reset();width_=width;height_=height;
        MapDrawCommand c;c.right=width;c.bottom=height;c.kind=MapDrawKind::Bgra;c.width=width;c.height=height;c.stride=c.width*4;
        c.offset=scene_.intern({reinterpret_cast<const unsigned char*>(pixels),static_cast<std::size_t>(c.stride)*height});scene_.push(c);
        return native_.begin(reference,width,height,scene_,pixels);
    }catch(...){native_.cancel();return nullptr;}
}
HDC MapCompositionWin32::beginWorld(HDC reference,const std::uint32_t* pixels,int width,int height) noexcept {
    ready_=false;if(!reference||!pixels||width<=0||height<=0||width>MapDrawList::maxExtent||height>MapDrawList::maxExtent||!GdiFlush())return nullptr;
    try{scene_.reset();width_=width;height_=height;scene_.fill({0,0,width,height},0);return native_.begin(reference,width,height,scene_,pixels);}
    catch(...){native_.cancel();return nullptr;}
}
bool MapCompositionWin32::finish() noexcept {ready_=native_.finish()&&scene_.valid();return ready_;}
bool MapCompositionWin32::prepare() noexcept {
    if(!ready_)return false;
    try{frame_=scene_;return true;}catch(...){return false;}
}
bool MapCompositionWin32::appendOpaque(RuntimeInputPixelRect r,const std::uint32_t* pixels,int stride) noexcept {
    if(!pixels||stride<width_||r.left<0||r.top<0||r.right>width_||r.bottom>height_||r.right<=r.left||r.bottom<=r.top)return false;
    try {
        const auto w=r.right-r.left,h=r.bottom-r.top;std::vector<std::uint32_t> copy(static_cast<std::size_t>(w)*h);
        for(int y=0;y<h;++y)std::copy_n(pixels+static_cast<std::size_t>(y+r.top)*stride+r.left,w,copy.data()+static_cast<std::size_t>(y)*w);
        MapDrawCommand c;c.left=r.left;c.top=r.top;c.right=r.right;c.bottom=r.bottom;c.kind=MapDrawKind::Bgra;
        c.originX=r.left;c.originY=r.top;c.width=w;c.height=h;c.stride=c.width*4;c.offset=frame_.internValues<std::uint32_t>(copy);frame_.push(c);return true;
    }catch(...){return false;}
}
namespace {thread_local MapCompositionRequestWin32* mapRequest=nullptr;}
MapCompositionRequestWin32::MapCompositionRequestWin32(MapCompositionWin32& owner,HDC dc,const std::uint32_t* pixels,int width,int height) noexcept
    :owner_(owner),reference_(dc),pixels_(pixels),width_(width),height_(height),previous_(mapRequest){mapRequest=this;}
MapCompositionRequestWin32::~MapCompositionRequestWin32(){mapRequest=previous_;}
HDC MapCompositionRequestWin32::start() noexcept {
    if(attempted_){return owner_.native().dc();}
    attempted_=true;return owner_.begin(reference_,pixels_,width_,height_);
}
MapCompositionRequestWin32* MapCompositionRequestWin32::current() noexcept {return mapRequest;}
HDC MapCompositionRequestWin32::capture(HDC reference,const std::uint32_t* pixels,int width,int height) noexcept {
    auto* request=current();if(!request||!request->targets(reference,pixels,width,height))return reference;
    const auto captured=request->start();return captured?captured:reference;
}
HDC MapCompositionRequestWin32::captureWorld(HDC dc,const std::uint32_t* pixels,int width,int height) noexcept {
    auto* request=current();if(!request||!request->targets(dc,pixels,width,height)||request->attempted_)return dc;
    request->attempted_=true;const auto result=request->owner_.beginWorld(dc,pixels,width,height);return result?result:dc;
}
} // namespace concord::ui::win32
