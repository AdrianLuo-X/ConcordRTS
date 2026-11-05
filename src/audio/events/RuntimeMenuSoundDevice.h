#pragma once
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
namespace concord::audio {
// Existing shared menu sound pool owns policy; this owns its device sessions.
class MenuSoundDevice {
public:
    virtual ~MenuSoundDevice() = default;
    virtual bool supported() const noexcept = 0;
    virtual std::optional<bool> isPlaying(std::size_t channel) const = 0;
    virtual bool open(std::size_t channel, const std::wstring& nativePath) = 0;
    virtual bool playFromStart(std::size_t channel) = 0;
    virtual void close(std::size_t channel) = 0;
};
std::unique_ptr<MenuSoundDevice> makeUnsupportedMenuSoundDevice();
}
