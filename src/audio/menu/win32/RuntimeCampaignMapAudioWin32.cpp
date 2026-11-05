#include "audio/menu/win32/RuntimeCampaignMapAudioWin32.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <string>
namespace concord::audio {
namespace {
struct CampaignAudioLease {};
struct CampaignAudioSlot { std::weak_ptr<CampaignAudioLease> owner; };
std::shared_ptr<CampaignAudioSlot> campaignAudioSlot(CampaignMapAudioChannel channel) {
    static const std::array slots{std::make_shared<CampaignAudioSlot>(), std::make_shared<CampaignAudioSlot>()};
    return slots[channel == CampaignMapAudioChannel::Voice ? 0 : 1];
}
std::uint32_t nativeCampaignAudioCommand(const wchar_t* command, wchar_t* output, std::uint32_t capacity) {
    return mciSendStringW(command, output, static_cast<UINT>(capacity), nullptr);
}
class CampaignMapAudioWin32 final : public CampaignMapAudioDevice {
public:
    CampaignMapAudioWin32(CampaignMapAudioChannel channel, CampaignMapAudioNativeCommand send)
        : send_(send), slot_(campaignAudioSlot(channel)), alias_(channel == CampaignMapAudioChannel::Voice
            ? L"concord_campaign_map_voice" : L"concord_campaign_map_effect") {}
    ~CampaignMapAudioWin32() override {
        if (owns_ && slot_->owner.lock() == lease_) stopAndClose();
    }
    bool supported() const noexcept override { return true; }
    bool play(const std::filesystem::path& path, int volumePercent) override {
        const auto attempt = std::make_shared<CampaignAudioLease>();
        forceStopAndClose();
        // Exactly original Campaign: path.wstring(), no shareable, play with
        // no 'from 0'. Failure of open returns directly; play failure closes.
        const std::wstring command = L"open \"" + path.wstring()
            + L"\" type waveaudio alias " + alias_;
        if (send_(command.c_str(), nullptr, 0) != 0) {
            retainActual(attempt);
            return false;
        }
        const int volume = std::clamp(volumePercent, 0, 100) * 10;
        const std::wstring volumeCommand = L"setaudio " + alias_ + L" volume to " + std::to_wstring(volume);
        (void)send_(volumeCommand.c_str(), nullptr, 0);
        const std::wstring playCommand = L"play " + alias_;
        if (send_(playCommand.c_str(), nullptr, 0) != 0) {
            forceStopAndClose();
            retainActual(attempt);
            return false;
        }
        retainActual(attempt);
        return true;
    }
    void stopAndClose() override {
        if (!canAddress()) { owns_ = false; lease_.reset(); return; }
        const std::wstring stop = L"stop " + alias_; (void)send_(stop.c_str(), nullptr, 0);
        const std::wstring close = L"close " + alias_; const auto error = send_(close.c_str(), nullptr, 0);
        if (error == 0 || mciGetDeviceIDW(alias_.c_str()) == 0) {
            if (slot_->owner.lock() == lease_) slot_->owner.reset();
            owns_ = false; lease_.reset();
        }
    }
private:
    bool canAddress() const { const auto owner = slot_->owner.lock(); return !owner || owner == lease_; }
    void forceStopAndClose() {
        const std::wstring stop = L"stop " + alias_; (void)send_(stop.c_str(), nullptr, 0);
        const std::wstring close = L"close " + alias_; (void)send_(close.c_str(), nullptr, 0);
    }
    void retainActual(const std::shared_ptr<CampaignAudioLease>& attempt) {
        slot_->owner.reset(); lease_.reset(); owns_ = false;
        if (mciGetDeviceIDW(alias_.c_str()) != 0) { lease_ = attempt; slot_->owner = lease_; owns_ = true; }
    }
    CampaignMapAudioNativeCommand send_;
    std::shared_ptr<CampaignAudioSlot> slot_;
    std::shared_ptr<CampaignAudioLease> lease_;
    std::wstring alias_;
    bool owns_ = false;
};
}
std::unique_ptr<CampaignMapAudioDevice> createCampaignMapAudioDeviceWin32ForDiagnostics(
    CampaignMapAudioChannel channel, CampaignMapAudioNativeCommand send) {
    if (!send || (channel != CampaignMapAudioChannel::Voice && channel != CampaignMapAudioChannel::Effect))
        return makeUnsupportedCampaignMapAudioDevice();
    return std::make_unique<CampaignMapAudioWin32>(channel, send);
}
std::unique_ptr<CampaignMapAudioDevice> createCampaignMapAudioDeviceWin32(CampaignMapAudioChannel channel) {
    return createCampaignMapAudioDeviceWin32ForDiagnostics(channel, nativeCampaignAudioCommand);
}
} // namespace concord::audio
#else
namespace concord::audio {
std::unique_ptr<CampaignMapAudioDevice> createCampaignMapAudioDeviceWin32(CampaignMapAudioChannel) {
    return makeUnsupportedCampaignMapAudioDevice();
}
std::unique_ptr<CampaignMapAudioDevice> createCampaignMapAudioDeviceWin32ForDiagnostics(CampaignMapAudioChannel, CampaignMapAudioNativeCommand) {
    return makeUnsupportedCampaignMapAudioDevice();
}
}
#endif
