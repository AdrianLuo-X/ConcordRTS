#include "audio/menu/RuntimeMainMenuMusicDevice.h"
namespace concord::audio {
namespace {
class UnsupportedMainMusic final : public MainMenuMusicDevice {
public:
    bool supported() const noexcept override { return false; }
    bool open(const std::filesystem::path&) override { return false; }
    bool playFromStart() override { return false; }
    std::optional<bool> isPlaying() override { return std::nullopt; }
    void setVolumePercent(int) override {}
    void stop() override {}
    void close() override {}
};
}
std::unique_ptr<MainMenuMusicDevice> makeUnsupportedMainMenuMusicDevice() {
    return std::make_unique<UnsupportedMainMusic>();
}
} // namespace concord::audio
