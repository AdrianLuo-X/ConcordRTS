#include "audio/menu/RuntimeCampaignMapAudioDevice.h"
namespace concord::audio {
namespace {
class UnsupportedCampaignMapAudio final : public CampaignMapAudioDevice {
public:
    bool supported() const noexcept override { return false; }
    bool play(const std::filesystem::path&, int) override { return false; }
    void stopAndClose() override {}
};
}
std::unique_ptr<CampaignMapAudioDevice> makeUnsupportedCampaignMapAudioDevice() {
    return std::make_unique<UnsupportedCampaignMapAudio>();
}
} // namespace concord::audio
