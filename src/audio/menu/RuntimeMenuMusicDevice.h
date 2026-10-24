#pragma once
#include <filesystem>
#include <memory>
#include <optional>
namespace concord::audio {
// Narrow physical music session. Calls are synchronous on the UI thread.
// open replaces the backend's previous slot; the selected Win32 adapter keeps
// its original alias reset/open sequence. No native handles/errors in callers.
class MenuMusicDevice {
public:
    virtual ~MenuMusicDevice() = default;
    virtual bool supported() const noexcept = 0;
    virtual bool open(const std::filesystem::path&) = 0;
    virtual bool playFromStart() = 0;
    virtual std::optional<bool> isPlaying() = 0;
    virtual void stop() = 0;
    virtual void close() = 0;
};
// Honest capability fallback: never reports playback success.
std::unique_ptr<MenuMusicDevice> makeUnsupportedMenuMusicDevice();
} // namespace concord::audio
