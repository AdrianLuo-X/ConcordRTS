#pragma once
// P06 W018-01/W019-01 private adapter. The existing menu owner is also the
// shared native DIB implementation; these fields are UI-thread borrows only.
#include "ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h"
namespace concord::ui::win32 {
class MainBackBufferWin32 {
public:
    explicit MainBackBufferWin32(SkirmishBackBufferGdiApi api={}) noexcept : storage_(api) {}
    MainBackBufferWin32(const MainBackBufferWin32&)=delete;
    MainBackBufferWin32& operator=(const MainBackBufferWin32&)=delete;
    HDC memoryDc=nullptr;
    HBITMAP bitmap=nullptr;
    void* pixels=nullptr;
    int width=0,height=0;
    bool topDownDib=false;
    bool ensure(HDC reference,int w,int h) noexcept {
        if(w<=0||h<=0||!reference){release();return false;}
        if(borrowed_)return false; // Explicit external lifetime cannot be adopted.
        const bool reuse=storage_.ready()&&width==w&&height==h;
        if(!reuse)bumpGeneration();
        const bool success=storage_.ensure(reference,w,h);
        clearAliases();
        if(success){memoryDc=storage_.dc();bitmap=storage_.bitmap();
            pixels=storage_.writablePixels();width=w;height=h;topDownDib=true;}
        return success;
    }
    bool release() noexcept {
        const bool success=borrowed_||storage_.release();
        borrowed_=false;clearAliases();bumpGeneration();return success;
    }
    bool borrow(HDC dc,HBITMAP dib,void* bytes,int w,int h,bool topDown=false) noexcept {
        if(storage_.dc()||storage_.bitmap()||borrowed_)return false;
        memoryDc=dc;bitmap=dib;pixels=bytes;width=w;height=h;topDownDib=topDown;
        borrowed_=true;bumpGeneration();return true;
    }
    void detachBorrowed() noexcept {
        if(borrowed_){borrowed_=false;clearAliases();bumpGeneration();}
    }
    ConstMenuPixelSurfaceView pixelView() const noexcept {
        if(!topDownDib||!pixels||width<=0||height<=0)return {};
        if(!borrowed_&&(!storage_.ready()||storage_.dc()!=memoryDc||storage_.bitmap()!=bitmap
            ||static_cast<const void*>(storage_.pixels())!=pixels||storage_.width()!=width||storage_.height()!=height))return {};
        const auto stride=static_cast<std::size_t>(width)*4;
        if(static_cast<std::size_t>(height)>std::numeric_limits<std::size_t>::max()/stride)return {};
        return {{static_cast<const unsigned char*>(pixels),stride*static_cast<std::size_t>(height)},width,height,stride,generation_};
    }
    void beginWrite() noexcept { bumpGeneration(); }
    void swap(MainBackBufferWin32& other) noexcept {
        if(this==&other)return;
        auto generation=generation_;
        do {if(++generation==0)++generation;}while(generation==generation_||generation==other.generation_);
        storage_.swap(other.storage_);using std::swap;
        swap(memoryDc,other.memoryDc);swap(bitmap,other.bitmap);
        swap(pixels,other.pixels);swap(width,other.width);swap(height,other.height);
        swap(topDownDib,other.topDownDib);swap(borrowed_,other.borrowed_);
        generation_=other.generation_=generation;
    }
    // Working borrow for the original synchronous raster producer. Caller
    // starts a write epoch first; completedPixels is the separate read handoff.
    MenuPixelSurfaceView writablePixelView() noexcept {
        const auto metadata=pixelView();
        if(!owns(metadata))return {};
        return {{storage_.writablePixels(),metadata.bytes.size()},metadata.width,
            metadata.height,metadata.rowStride,metadata.generation};
    }
    bool nativeResourcesReleased() const noexcept {
        return !storage_.dc()&&!storage_.bitmap()&&storage_.selectionRestored();
    }
    bool owns(ConstMenuPixelSurfaceView view) const noexcept {
        const auto current=pixelView();
        return !borrowed_&&storage_.ready()&&view.valid()&&view.bytes.data()==current.bytes.data()
            &&view.bytes.size()==current.bytes.size()&&view.width==current.width&&view.height==current.height
            &&view.rowStride==current.rowStride&&view.generation==current.generation;
    }
    // This is the only publication operation. No reader is allowed while GDI
    // writes are pending. The lease ends before the next UI write/resize/release.
    ConstMenuPixelSurfaceView completedPixels() const noexcept {
        const auto view=pixelView();
        return owns(view)&&GetCurrentObject(memoryDc,OBJ_BITMAP)==bitmap
            &&storage_.finishGdiWrites()?view:ConstMenuPixelSurfaceView{};
    }
private:
    void clearAliases() noexcept {memoryDc=nullptr;bitmap=nullptr;pixels=nullptr;width=height=0;topDownDib=false;}
    void bumpGeneration() noexcept {if(++generation_==0)++generation_;}
    SkirmishBackBufferWin32 storage_;
    std::uint64_t generation_=1;
    bool borrowed_=false;
};
// Diagnostic dependency only, borrowed for one UI session. Never serialized,
// hashed, globally installed, or passed to worker threads.
struct MainFrameProbeWin32 {
    virtual ~MainFrameProbeWin32()=default;
    bool fixedClock=false;
    double seconds=0;
    bool failSceneAllocation=false;
    bool failCompletedAllocation=false;
    virtual void observe(HDC,int,int,bool,bool,ConstMenuPixelSurfaceView) = 0;
};
} // namespace concord::ui::win32