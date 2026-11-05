#pragma once
// Private Main-menu compatibility policy over the single existing DIB owner.
#include "ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h"
#include <algorithm>
namespace concord::ui::win32 {
class MainMenuBackBufferWin32 {
public:
    explicit MainMenuBackBufferWin32(SkirmishBackBufferGdiApi api = {}) noexcept : storage_(api) {}
    bool ensure(HDC reference, std::int32_t width, std::int32_t height) noexcept {
        return storage_.ensure(reference, width, height);
    }
    bool ready() const noexcept { return storage_.ready(); }
    HDC dc() const noexcept { return storage_.dc(); }
    const unsigned char* pixels() const noexcept { return storage_.pixels(); }
    bool release() noexcept { return storage_.release(); }
    // UI-thread-only synchronous borrow. Main historically ignores the flush
    // result before this full copy; this adapter preserves that policy rather
    // than borrowing Skirmish's stricter upload-failure contract.
    bool uploadLegacy(ConstMenuPixelSurfaceView source) noexcept {
        if (!storage_.ready() || !source.valid()
            || source.width != storage_.width() || source.height != storage_.height()) return false;
        (void)storage_.finishGdiWrites();
        for (std::int32_t y = 0; y < source.height; ++y)
            std::copy_n(source.bytes.data() + static_cast<std::size_t>(y) * source.rowStride,
                storage_.rowStride(), storage_.writablePixels() + static_cast<std::size_t>(y) * storage_.rowStride());
        return true;
    }
private:
    SkirmishBackBufferWin32 storage_;
};
} // namespace concord::ui::win32
