struct RuntimeMenuSoundMciBackend {
    std::unique_ptr<concord::audio::MenuSoundDevice> device=makeRuntimeMenuSoundDevice();
    std::optional<bool> isPlaying(std::size_t channel) const {return device->isPlaying(channel);}
    bool open(std::size_t channel,const std::wstring& path) const {return device->open(channel,path);}
    bool playFromStart(std::size_t channel) const {return device->playFromStart(channel);}
    void close(std::size_t channel) const {device->close(channel);}
};

struct RuntimeMenuSoundPlayback {
    RuntimeMenuSoundPool<32> pool;
    RuntimeMenuSoundMciBackend backend;
    // Borrowed provider outlives this playback owner; the default is immutable.
    const RuntimeUiClock* clock = &runtimeWin32UiClock();
    std::uint64_t nextCollect = 0;

    ~RuntimeMenuSoundPlayback() { pool.closeAll(backend); }
    void play(const std::wstring& path) { (void)pool.play(path, backend); }
    void collect() {
        const auto now = clock->nowMs();
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
