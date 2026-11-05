#pragma once
#include "audio/menu/RuntimeCampaignMapAudioDevice.h"
#include <cstdint>
namespace concord::audio {
std::unique_ptr<CampaignMapAudioDevice> createCampaignMapAudioDeviceWin32(CampaignMapAudioChannel);
using CampaignMapAudioNativeCommand = std::uint32_t(*)(const wchar_t*, wchar_t*, std::uint32_t);
// Private real-native verification: only specified failing calls are injected.
std::unique_ptr<CampaignMapAudioDevice> createCampaignMapAudioDeviceWin32ForDiagnostics(
    CampaignMapAudioChannel, CampaignMapAudioNativeCommand);
} // namespace concord::audio
