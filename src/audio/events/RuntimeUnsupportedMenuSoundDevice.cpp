#include "audio/events/RuntimeMenuSoundDevice.h"
namespace concord::audio {
namespace {
class UnsupportedMenuSoundDevice final : public MenuSoundDevice {
public:
    bool supported() const noexcept override { return false; }
    std::optional<bool> isPlaying(std::size_t) const override { return std::nullopt; }
    bool open(std::size_t, const std::wstring&) override { return false; }
    bool playFromStart(std::size_t) override { return false; }
    void close(std::size_t) override {}
};
}
std::unique_ptr<MenuSoundDevice> makeUnsupportedMenuSoundDevice() {
    return std::make_unique<UnsupportedMenuSoundDevice>();
}
}
