#pragma once
// Private Campaign/LoadGame transfer policy, over the existing bitmap owner.
#include "ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h"
namespace concord::ui::win32 {
class CampaignDeviceBitmapWin32 {
public:
    using Upload = decltype(&::SetDIBitsToDevice);
    explicit CampaignDeviceBitmapWin32(SkirmishBackBufferGdiApi api = {},
        Upload upload = &::SetDIBitsToDevice) noexcept : storage_(api), upload_(upload) {}
    bool ensure(HDC reference, std::int32_t width, std::int32_t height) noexcept {
        return storage_.ensureDeviceBitmap(reference, width, height);
    }
    HDC dc() const noexcept { return storage_.dc(); }
    bool ready() const noexcept { return storage_.ready(); }
    bool release() noexcept { return storage_.release(); }
    // Same synchronous tight top-down BGRA upload as the original shell.
    // true reports a valid transfer request; Campaign historically ignores the
    // device's SetDIBitsToDevice return value. No completion/Presenter claim.
    bool upload(ConstMenuPixelSurfaceView source) noexcept {
        if (!storage_.ready() || !source.valid()
            || source.width != storage_.width() || source.height != storage_.height()
            || source.rowStride != static_cast<std::size_t>(source.width) * 4) return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = source.width;
        info.bmiHeader.biHeight = -source.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        (void)upload_(storage_.dc(), 0, 0, static_cast<DWORD>(source.width),
            static_cast<DWORD>(source.height), 0, 0, 0,
            static_cast<UINT>(source.height), source.bytes.data(), &info, DIB_RGB_COLORS);
        return true;
    }
private:
    SkirmishBackBufferWin32 storage_;
    Upload upload_;
};
} // namespace concord::ui::win32
