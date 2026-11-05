// Frozen original five Campaign functions, exact original bodies. Native test oracle only.
void closeCampaignMciAlias(const wchar_t* alias) {
    std::wstring stop = L"stop ";
    stop += alias;
    mciSendStringW(stop.c_str(), nullptr, 0, nullptr);
    std::wstring close = L"close ";
    close += alias;
    mciSendStringW(close.c_str(), nullptr, 0, nullptr);
}

bool playCampaignVoiceWave(const fs::path& path, int volume) {
    constexpr const wchar_t* kVoiceAlias = L"concord_campaign_map_voice";
    closeCampaignMciAlias(kVoiceAlias);
    const std::wstring command = L"open \"" + path.wstring()
        + L"\" type waveaudio alias " + kVoiceAlias;
    if (mciSendStringW(command.c_str(), nullptr, 0, nullptr) != 0) {
        return false;
    }
    const int mciVolume = std::clamp(volume, 0, 100) * 10;
    const std::wstring volumeCommand = L"setaudio "
        + std::wstring(kVoiceAlias) + L" volume to "
        + std::to_wstring(mciVolume);
    mciSendStringW(volumeCommand.c_str(), nullptr, 0, nullptr);
    const std::wstring play = L"play " + std::wstring(kVoiceAlias);
    if (mciSendStringW(play.c_str(), nullptr, 0, nullptr) != 0) {
        closeCampaignMciAlias(kVoiceAlias);
        return false;
    }
    return true;
}

bool playCampaignEffectWave(const fs::path& path, int volume) {
    constexpr const wchar_t* kEffectAlias = L"concord_campaign_map_effect";
    closeCampaignMciAlias(kEffectAlias);
    const std::wstring command = L"open \"" + path.wstring()
        + L"\" type waveaudio alias " + kEffectAlias;
    if (mciSendStringW(command.c_str(), nullptr, 0, nullptr) != 0) {
        return false;
    }
    const int mciVolume = std::clamp(volume, 0, 100) * 10;
    const std::wstring volumeCommand = L"setaudio "
        + std::wstring(kEffectAlias) + L" volume to "
        + std::to_wstring(mciVolume);
    mciSendStringW(volumeCommand.c_str(), nullptr, 0, nullptr);
    const std::wstring play = L"play " + std::wstring(kEffectAlias);
    if (mciSendStringW(play.c_str(), nullptr, 0, nullptr) != 0) {
        closeCampaignMciAlias(kEffectAlias);
        return false;
    }
    return true;
}

void stopCampaignVoice() {
    closeCampaignMciAlias(L"concord_campaign_map_voice");
}

void stopCampaignMapEffect() {
    closeCampaignMciAlias(L"concord_campaign_map_effect");
}

