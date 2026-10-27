#pragma once
// Private Win32 backend. Only the UI thread may own/use this DC and DIB.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "ui/surface/RuntimeMenuPixelSurface.h"
#include <array>
#include <utility>
namespace concord::ui::win32 {
struct SkirmishGdiReleaseTrace {
    std::array<bool, 7> succeeded{};
    std::array<int, 7> order{};
    int count = 0;
    void record(int step, bool success) noexcept {
        if (count < 7) order[static_cast<std::size_t>(count)] = step;
        ++count;
        succeeded[static_cast<std::size_t>(step)] = success;
    }
};

// Value-copied, typed API table; default is the actual Win32 implementation.
// Explicit injection is for native failure contracts, never a global override.
struct SkirmishBackBufferGdiApi {
    decltype(&::CreateCompatibleDC) createDc = &::CreateCompatibleDC;
    decltype(&::CreateDIBSection) createDib = &::CreateDIBSection;
    decltype(&::SelectObject) select = &::SelectObject;
    decltype(&::DeleteObject) deleteObject = &::DeleteObject;
    decltype(&::DeleteDC) deleteDc = &::DeleteDC;
    decltype(&::GdiFlush) flush = &::GdiFlush;
    decltype(&::CreateCompatibleBitmap) createDeviceBitmap = &::CreateCompatibleBitmap;
};
class SkirmishBackBufferWin32 {
public:
    explicit SkirmishBackBufferWin32(SkirmishBackBufferGdiApi api = {}) noexcept : api_(api) {}
    ~SkirmishBackBufferWin32() { release(); }
    SkirmishBackBufferWin32(const SkirmishBackBufferWin32&) = delete;
    SkirmishBackBufferWin32& operator=(const SkirmishBackBufferWin32&) = delete;
    // Same dimensions reuse the owner. Resize releases first. Creation failures
    // roll back; failed cleanup retains its remaining handles for later retry.
    bool ensure(HDC reference,std::int32_t width,std::int32_t height) noexcept;
    // Native-device-only HUD; no CPU view is published for this bitmap.
    bool ensureDeviceBitmap(HDC reference,std::int32_t width,std::int32_t height) noexcept;
    // Synchronous CPU completion -> flush previous GDI access -> copy rows.
    // Failed flush never writes the DIB; caller keeps dirty and returns failure.
    bool upload(ConstMenuPixelSurfaceView) noexcept;
    // false reports real cleanup failure; remaining handles stay owned.
    bool release(SkirmishGdiReleaseTrace* trace = nullptr) noexcept;
    bool ready() const noexcept { return dc_ && bitmap_ && old_ && old_!=HGDI_ERROR && (bits_||deviceBitmap_); }
    HDC dc() const noexcept { return dc_; }
    HBITMAP bitmap() const noexcept { return bitmap_; }
    std::int32_t width() const noexcept { return width_; }
    std::int32_t height() const noexcept { return height_; }
    std::size_t rowStride() const noexcept { return stride_; }
    bool selectionRestored() const noexcept { return old_==nullptr; }
    // Explicit UI-owner exchange; no native handle is ever copied as ownership.
    void swap(SkirmishBackBufferWin32& other) noexcept {
        using std::swap;swap(api_,other.api_);swap(dc_,other.dc_);swap(bitmap_,other.bitmap_);
        swap(old_,other.old_);swap(bits_,other.bits_);swap(width_,other.width_);
        swap(height_,other.height_);swap(stride_,other.stride_);swap(deviceBitmap_,other.deviceBitmap_);
    }
    // Private backend/diagnostic borrow, invalid on ensure-resize/release.
    const unsigned char* pixels() const noexcept { return bits_; }
    // Private UI-thread mutable borrow. Complete GDI writes before any reader.
    unsigned char* writablePixels() noexcept { return bits_; }
    bool finishGdiWrites() const noexcept { return ready() && api_.flush()!=FALSE; }
private:
    SkirmishBackBufferGdiApi api_;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ old_ = nullptr;
    unsigned char* bits_ = nullptr;
    std::int32_t width_ = 0,height_ = 0;
    std::size_t stride_ = 0;
    bool deviceBitmap_ = false;
};
} // namespace concord::ui::win32
