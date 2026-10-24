#include "audio/menu/RuntimeMenuMusicDevice.h"
namespace concord::audio {
namespace {
class UnsupportedMusic final : public MenuMusicDevice {
public:
    bool supported() const noexcept override { return false; }
    bool open(const std::filesystem::path&) override { return false; }
    bool playFromStart() override { return false; }
    std::optional<bool> isPlaying() override { return std::nullopt; }
    void stop() override {}
    void close() override {}
};
}
std::unique_ptr<MenuMusicDevice> makeUnsupportedMenuMusicDevice() {
    return std::make_unique<UnsupportedMusic>();
}
} // namespace concord::audio
