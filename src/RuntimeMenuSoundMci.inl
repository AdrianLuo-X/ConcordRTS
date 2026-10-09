struct RuntimeMenuSoundMciBackend {
    static std::wstring alias(std::size_t channel) {
        return L"concord_menu_effect_" + std::to_wstring(channel);
    }
    std::optional<bool> isPlaying(std::size_t channel) const {
        wchar_t status[32]{};
        const auto command = L"status " + alias(channel) + L" mode";
        if (mciSendStringW(command.c_str(), status, static_cast<UINT>(std::size(status)), nullptr) != 0)
            return std::nullopt;
        return std::wstring(status) == L"playing";
    }
    bool open(std::size_t channel, const std::wstring& path) const {
        const auto command = L"open \"" + path + L"\" type waveaudio alias " + alias(channel);
        const auto shareable = command + L" shareable";
        return mciSendStringW(shareable.c_str(), nullptr, 0, nullptr) == 0
            || mciSendStringW(command.c_str(), nullptr, 0, nullptr) == 0;
    }
    bool playFromStart(std::size_t channel) const {
        const auto command = L"play " + alias(channel) + L" from 0";
        return mciSendStringW(command.c_str(), nullptr, 0, nullptr) == 0;
    }
    void close(std::size_t channel) const {
        const auto command = L"close " + alias(channel);
        (void)mciSendStringW(command.c_str(), nullptr, 0, nullptr);
    }
};

struct RuntimeMenuSoundPlayback {
    RuntimeMenuSoundPool<32> pool;
    RuntimeMenuSoundMciBackend backend;
    ULONGLONG nextCollect = 0;

    ~RuntimeMenuSoundPlayback() { pool.closeAll(backend); }
    void play(const std::wstring& path) { (void)pool.play(path, backend); }
    void collect() {
        const auto now = GetTickCount64();
        if (now < nextCollect) return;
        nextCollect = now + 100;
        pool.collect(backend);
    }
};

RuntimeMenuSoundPlayback& runtimeMenuSoundPlayback() {
    // The shell's shared audio lifetime spans lobby/map pages and menu reentry.
    static RuntimeMenuSoundPlayback playback;
    return playback;
}
