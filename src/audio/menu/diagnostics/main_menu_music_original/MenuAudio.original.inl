struct OriginalMainMenuAudio {
    // Diagnostic-only per-instance source; no original method statement changed.
    const RuntimeUiClock* fixtureClock = &runtimeWin32UiClock();
    ULONGLONG GetTickCount64() const noexcept { return fixtureClock->nowMs(); }
    bool enabled = true;
    int musicVolumePercent = 100;
    fs::path assetRoot;
    fs::path cacheRoot;
    std::unordered_map<std::string, std::string> samples{
        {"MenuClick", "umenucl1"},
        {"MenuSlideIn", "uslide1"},
        {"MenuSlideOut", "uslide2"}
    };
    std::unordered_map<std::string, std::optional<std::wstring>> waves;
    bool musicOpen = false;
    fs::path musicPath;
    ULONGLONG nextMusicProbe = 0;

    ~OriginalMainMenuAudio() { stopMusic(); }

    void initialize(const fs::path& soundIni) {
        IniFile ini;
        if (!soundIni.empty() && ini.loadFromPath(soundIni)) {
            for (auto& [eventName, sample] : samples) {
                std::string configured = trimAscii(
                    iniValueNoCase(ini, eventName, "Sounds", sample)
                );
                const std::size_t separator = configured.find_first_of(" ,\t");
                if (separator != std::string::npos) configured.resize(separator);
                while (!configured.empty()
                    && (configured.front() == '$' || configured.front() == '#')) {
                    configured.erase(configured.begin());
                }
                if (!configured.empty()) sample = configured;
            }
        }
    }

    void play(const std::string& eventName) {
        if (!enabled) return;
        auto wave = waves.find(eventName);
        if (wave == waves.end()) {
            std::optional<std::wstring> path;
            const auto sample = samples.find(eventName);
            if (sample != samples.end()) {
                const AudioSampleResolveResult resolved = resolveAudioSampleToWaveFromPaths(
                    {assetRoot, cacheRoot}, sample->second
                );
                if (resolved.success && !resolved.wavFilePath.empty()) {
                    path = resolved.wavFilePath.wstring();
                }
            }
            wave = waves.emplace(eventName, std::move(path)).first;
        }
        if (wave->second) {
            PlaySoundW(
                wave->second->c_str(), nullptr,
                SND_FILENAME | SND_ASYNC | SND_NODEFAULT
            );
        }
    }

    void startMusic(const fs::path& path) {
        if (!enabled || musicVolumePercent <= 0 || !regularFile(path)) return;
        if (musicOpen && musicPath == path) return;
        if (musicOpen) stopMusic();
        musicPath = path;
        nextMusicProbe = GetTickCount64() + 1000ULL;
        mciSendStringW(L"close concord_runtime_intro", nullptr, 0, nullptr);
        const std::wstring command = L"open \"" + path.native()
            + L"\" type waveaudio alias concord_runtime_intro";
        const MCIERROR openError =
            mciSendStringW(command.c_str(), nullptr, 0, nullptr);
        if (openError == 0) {
            const std::wstring volumeCommand =
                L"setaudio concord_runtime_intro volume to "
                + std::to_wstring(
                    std::clamp(musicVolumePercent, 0, 100) * 10
                );
            (void)mciSendStringW(
                volumeCommand.c_str(),
                nullptr,
                0,
                nullptr
            );
        }
        // The Windows waveaudio MCI driver rejects the string-command
        // `repeat` flag.  Play once and let ensureMusicPlaying restart the
        // track after it reaches the stopped state.
        const MCIERROR playError = openError == 0
            ? mciSendStringW(
                L"play concord_runtime_intro from 0",
                nullptr,
                0,
                nullptr
            )
            : openError;
        if (openError == 0 && playError == 0) {
            musicOpen = true;
        } else {
            mciSendStringW(L"close concord_runtime_intro", nullptr, 0, nullptr);
            musicOpen = false;
        }
    }

    void ensureMusicPlaying() {
        if (!enabled || musicPath.empty()) return;
        const ULONGLONG now = GetTickCount64();
        if (now < nextMusicProbe) return;
        nextMusicProbe = now + 1000ULL;

        if (!musicOpen) {
            const fs::path requestedPath = musicPath;
            startMusic(requestedPath);
            return;
        }

        wchar_t status[32]{};
        const MCIERROR statusError = mciSendStringW(
            L"status concord_runtime_intro mode",
            status,
            static_cast<UINT>(sizeof(status) / sizeof(status[0])),
            nullptr
        );
        if (statusError == 0 && std::wstring(status) == L"playing") return;

        const MCIERROR playError = mciSendStringW(
            L"play concord_runtime_intro from 0",
            nullptr,
            0,
            nullptr
        );
        if (playError == 0) return;

        const fs::path requestedPath = musicPath;
        mciSendStringW(L"close concord_runtime_intro", nullptr, 0, nullptr);
        musicOpen = false;
        startMusic(requestedPath);
    }

    void stopMusic() {
        mciSendStringW(L"stop concord_runtime_intro", nullptr, 0, nullptr);
        mciSendStringW(L"close concord_runtime_intro", nullptr, 0, nullptr);
        musicOpen = false;
        musicPath.clear();
        nextMusicProbe = 0;
    }
};
