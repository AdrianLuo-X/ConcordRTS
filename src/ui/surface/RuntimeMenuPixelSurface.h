#pragma once
#include "ui/input/RuntimeInputPixelValues.h"
#include "ui/values/RuntimeColorValues.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
namespace concord::ui {
// Top-down BGRA8, rowStride includes padding. Last row needs width*4 bytes.
// Synchronous storage borrow only; reset/release invalidates every old view.
// CPU storage/raster input, not S38's completed-frame/Presenter contract.
template<class Byte> struct BasicMenuPixelSurfaceView {
    std::span<Byte> bytes;
    std::int32_t width = 0, height = 0;
    std::size_t rowStride = 0;
    std::uint64_t generation = 0; // 0 for external synchronous storage.
    bool valid() const noexcept {
        if (width <= 0 || height <= 0) return false;
        const auto w = static_cast<std::size_t>(width);
        if (w > std::numeric_limits<std::size_t>::max()/4) return false;
        const auto row = w*4, rows = static_cast<std::size_t>(height-1);
        return rowStride >= row
            && rows <= (std::numeric_limits<std::size_t>::max()-row)/rowStride
            && bytes.size() >= rows*rowStride+row;
    }
};
using MenuPixelSurfaceView = BasicMenuPixelSurfaceView<unsigned char>;
using ConstMenuPixelSurfaceView = BasicMenuPixelSurfaceView<const unsigned char>;
class MenuPixelBuffer {
public:
    // Typed allocation seam; production uses its original zero-filled vector.
    using Allocate = std::vector<unsigned char>(*)(std::size_t);
    explicit MenuPixelBuffer(Allocate allocate = nullptr) noexcept : allocate_(allocate) {}
    MenuPixelBuffer(const MenuPixelBuffer&) = delete;
    MenuPixelBuffer& operator=(const MenuPixelBuffer&) = delete;
    MenuPixelBuffer(MenuPixelBuffer&&) = delete;
    MenuPixelBuffer& operator=(MenuPixelBuffer&&) = delete;
    bool reset(std::int32_t width, std::int32_t height, std::size_t rowStride = 0) noexcept;
    void release() noexcept;
    MenuPixelSurfaceView view() noexcept { return {pixels_,width_,height_,stride_,generation_}; }
    ConstMenuPixelSurfaceView view() const noexcept { return {pixels_,width_,height_,stride_,generation_}; }
    bool owns(MenuPixelSurfaceView) const noexcept;
    const std::vector<unsigned char>& bytes() const noexcept { return pixels_; }
private:
    std::vector<unsigned char> pixels_;
    std::int32_t width_ = 0, height_ = 0;
    std::size_t stride_ = 0;
    std::uint64_t generation_ = 1;
    Allocate allocate_;
};
void clearMenuPixels(MenuPixelSurfaceView);
void fillMenuPixels(MenuPixelSurfaceView,const RuntimeInputPixelRect&,RuntimeRgbColorValue);
void darkenMenuPixels(MenuPixelSurfaceView,const RuntimeInputPixelRect&,unsigned int opacity);
// Rect sums/differences keep the original representable signed32 domain.
void frameMenuPixels(MenuPixelSurfaceView,const RuntimeInputPixelRect&,RuntimeRgbColorValue);
// Original /255 blend. Intermediate alpha preserves destination alpha.
void blitMenuPixels(MenuPixelSurfaceView,ConstMenuPixelSurfaceView,
    std::int32_t left,std::int32_t top,
    std::int32_t clipHeight = std::numeric_limits<std::int32_t>::max());
// Nearest sampling, destination clip, copied alpha forced to255.
void scaleMenuPixels(MenuPixelSurfaceView,ConstMenuPixelSurfaceView,const RuntimeInputPixelRect&);
} // namespace concord::ui
