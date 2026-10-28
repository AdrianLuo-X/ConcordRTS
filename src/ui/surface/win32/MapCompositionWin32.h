#pragma once
#include "ui/surface/win32/MapNativeOverlayWin32.h"
namespace concord::ui::win32 {
// UI-thread packet owner. Native replay is a CPU-native producer; no device,
// World, geometry query or simulation state is owned here.
class MapCompositionWin32 {
public:
    bool ready(int width,int height) const noexcept {return ready_&&width_==width&&height_==height;}
    HDC begin(HDC reference,const std::uint32_t* pixels,int width,int height) noexcept;
    HDC beginWorld(HDC,const std::uint32_t*,int,int) noexcept;
    bool finish() noexcept;
    bool prepare() noexcept;
    bool appendOpaque(RuntimeInputPixelRect,const std::uint32_t*,int stride) noexcept;
    void cancel() noexcept {native_.cancel();ready_=false;}
    void release() noexcept {cancel();native_.release();try{scene_.reset();frame_.reset();}catch(...){}}
    MapNativeOverlayWin32& native() noexcept {return native_;}
    const concord::render::MapDrawList& frame() const noexcept {return frame_;}
private:
    concord::render::MapDrawList scene_,frame_;
    MapNativeOverlayWin32 native_;
    int width_=0,height_=0;
    bool ready_=false;
};
class MapCompositionRequestWin32 {
public:
    MapCompositionRequestWin32(MapCompositionWin32&,HDC,const std::uint32_t*,int,int) noexcept;
    ~MapCompositionRequestWin32();
    MapCompositionRequestWin32(const MapCompositionRequestWin32&)=delete;
    MapCompositionRequestWin32& operator=(const MapCompositionRequestWin32&)=delete;
    bool targets(HDC dc,const std::uint32_t* pixels,int width,int height) const noexcept {
        return dc==reference_&&pixels==pixels_&&width==width_&&height==height_;
    }
    HDC start() noexcept;
    static MapCompositionRequestWin32* current() noexcept;
    static HDC capture(HDC,const std::uint32_t*,int,int) noexcept;
    static HDC captureWorld(HDC,const std::uint32_t*,int,int) noexcept;
private:
    MapCompositionWin32& owner_;HDC reference_=nullptr;const std::uint32_t* pixels_=nullptr;
    int width_=0,height_=0;bool attempted_=false;MapCompositionRequestWin32* previous_=nullptr;
};
} // namespace concord::ui::win32
