#pragma once
#include <filesystem>
#include <memory>
namespace concord::audio {
enum class CampaignMapAudioChannel { Voice, Effect };
// The original two UI-owned map voice/effect slots. No playlist or event logic.
class CampaignMapAudioDevice {
public:
    virtual ~CampaignMapAudioDevice() = default;
    virtual bool supported() const noexcept = 0;
    // Replaces this channel; original native volume errors remain ignored.
    virtual bool play(const std::filesystem::path& path, int volumePercent) = 0;
    virtual void stopAndClose() = 0;
};
std::unique_ptr<CampaignMapAudioDevice> makeUnsupportedCampaignMapAudioDevice();
} // namespace concord::audio
