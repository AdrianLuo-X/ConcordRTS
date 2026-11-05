// Private native verification of the original WorldAudio producer/device/load
// closure. Uses actual RuntimeWindowState, startup, tick, save/load and Voice.
bool runRuntimeWorldAudioDeviceLifecycleSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    unsigned checks = 0;
    const auto require = [&](bool value, const std::string& message) {
        ++checks; if (!value) throw std::runtime_error(message);
    };
    try {
        const DWORD count = GetEnvironmentVariableW(L"CONCORD_WORLD_AUDIO_DEVICE_OUTPUT", nullptr, 0);
        require(count > 1, "explicit native output directory required");
        std::vector<wchar_t> output(count);
        require(GetEnvironmentVariableW(L"CONCORD_WORLD_AUDIO_DEVICE_OUTPUT", output.data(), count) == count - 1, "owned wide output path");
        const std::filesystem::path directory(output.data());
        std::filesystem::create_directories(directory);
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = config; s.config.computerAiEnabled = s.config.multiplayerActive = false;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear();
        s.config.crates = s.config.shortGame = false;
        s.report = buildRuntimeStartupReport(s.config, &s.world, &s.theater, &s.art, &s.rules, &s.mapFacts);
        require(s.report.success, "actual original startup report: " + s.report.errorMessage);
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.mapFactsLoaded = s.report.map.loaded;
        s.runtimePreferencePersistenceEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimeSoundsEnabled = false; s.runtimeSoundEffectsEnabled = true; s.runtimeSoundVolume = 100;
        s.visionEnabled = false; s.economy.owner = "Americans"; s.economy.credits = 10000;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        initializeRuntimeLocalProductionLanes(s.economy); initializeRuntimeNavigation(s);
        ensureRuntimeRepairDepotsInitialized(s);
        require(loadRuntimeSoundDatabase(s), "original sound catalog");
        RuntimeSnapshotActiveStateScope active(s);
        const int initialTick = s.tick; ++s.tick; (void)advanceRuntimeSimulation(s);
        require(s.tick == initialTick + 1, "actual production simulation primes one complete tick");
        // The private audio fixture uses ordinary saves, which deliberately
        // omit transient ore twinkles. Retain the actual generated records and
        // require their exact unrelated kind before choosing an effects-free
        // audio baseline. Complete authority assertions below stay unchanged.
        std::ofstream primedEffects(directory / "primed-effects.trace");
        require(bool(primedEffects), "explicit primed effect records");
        primedEffects << "count=" << s.impactEffects.size() << '\n';
        for (const auto& effect : s.impactEffects) {
            primedEffects << effect.renderIntent << '|' << runtimeMultiplayerSnapshotImpactRecord(effect) << '\n';
            require(effect.renderIntent == "oreTwinkle", "audio fixture may isolate only actual unrelated ore twinkles");
        }
        s.impactEffects.clear();
        const int tick = s.tick;
        const auto authority = [&]() {
            std::string why; const auto hash = runtimeStandaloneSimulationStateHash(s, why);
            require(hash != 0, "complete actual authority hash: " + why); return hash;
        };
        const auto serialized = [&]() {
            std::ostringstream text; std::string why;
            require(writeRuntimeStateToStream(s, text, why), "complete production codec: " + why); return text.str();
        };
        const auto cell = runtimeWorldAudioContext(s).listenerCell;
        const auto listen = [&]() {
            s.cameraClampViewport = {0, 0, 640, 400}; s.camera.initialized = true;
            const auto point = runtimeProjectCellXY(cell.x, cell.y, cell.level, s.camera);
            s.camera.worldOffsetX = point.x - 320; s.camera.worldOffsetY = point.y - 200;
        };
        listen();
        const auto* spec = s.worldAudioCatalog.find("FountainLoop");
        require(spec && spec->loop && spec->loopIterations == 0 && !spec->samples.empty(), "original indefinite FountainLoop");
        s.worldAudio.reset({s.config.fixedTickHz, 4096});
        RuntimeWorldAudioPlayRequest request; request.eventId = spec->eventId;
        request.cell = cell; request.requestToken = "P08-active-loop";
        const auto play = s.worldAudio.playAt(runtimeWorldAudioContext(s), request, tick);
        require(play.accepted && play.handleId != 0, "same actual WorldAudio producer accepts original request: " + play.reason);
        const auto playActions = s.worldAudio.tick(runtimeWorldAudioContext(s), tick);
        require(!playActions.empty(), "original fixed-tick producer emits native actions after accepted request");
        const auto trace = [&](const char* name) {
            std::vector<std::pair<std::string, std::uint64_t>> rows; std::string message;
            const auto value = runtimeStandaloneSimulationStateHash(s, message, &rows);
            std::ofstream file(directory / name); require(bool(file), "explicit full authority trace output");
            for (const auto& row : rows) file << row.first << '=' << row.second << '\n';
            file << "complete=" << value << '\n'; return value;
        };
        const auto domain = s.worldAudio.stateHash(); const auto hash = authority();
        require(trace("authority-before.trace") == hash, "actual full authority trace matches baseline hash");
        s.runtimeVoiceAudio = std::make_unique<RuntimeVoiceAudio>(true);
        require(s.runtimeVoiceAudio->ready(), "actual native verification graph required, output muted");
        s.runtimeSoundsEnabled = true; auto* const session = s.runtimeVoiceAudio.get();
        applyRuntimeWorldAudioBackendActions(s, playActions);
        require(s.runtimeWorldAudioBackendBindings.size() == 1 && session->voiceCount() == 1
            && session->isPlaying(play.handleId).value_or(false), "actual producer reaches one real playing native voice");
        const auto deadline = GetTickCount64() + 3000;
        while (session->probe(play.handleId).samplesPlayed < 1024 && GetTickCount64() < deadline) Sleep(5);
        require(session->probe(play.handleId).samplesPlayed >= 1024 && session->probe(play.handleId).masterVolume == 0,
            "owned PCM really progresses with muted device output");
        const auto oneBufferSave = serialized();
        auto append = playActions.front(); append.kind = RuntimeWorldAudioActionKind::Play; append.queueAfterCurrent = true;
        for (int n = 0; n < 3; ++n) applyRuntimeWorldAudioBackendAction(s, append);
        require(session->queuedBuffers(play.handleId) == 4, "actual native owner holds four buffers before active save");
        require(serialized() == oneBufferSave && authority() == hash && s.worldAudio.stateHash() == domain,
            "native PCM queue/resources do not change complete save bytes or authority");
        const auto savePath = pathString(directory / "active-world.ini"); std::string why;
        require(saveRuntimeState(s, savePath, why), "real active native save: " + why);
        std::vector<std::filesystem::path> waves;
        const auto addWave = [&](const RuntimeWorldAudioAction& action) {
            const auto original = findRuntimeSoundSamplePath(s, action.sampleName);
            require(original.has_value(), "same production sample selected");
            const auto variant = runtimeAudioPitchVariantPath(*original,
                std::filesystem::path(s.config.localDataRootPath) / "runtime_audio_cache", action.frequencyPermille);
            require(!variant.empty(), "same production pitch variant");
            const std::filesystem::path wave(variant);
            if (std::find(waves.begin(), waves.end(), wave) == waves.end()) waves.push_back(wave);
        };
        for (const auto& action : playActions) addWave(action);
        for (const auto& action : s.worldAudio.rehydrateActions(runtimeWorldAudioContext(s), tick)) addWave(action);
        struct ReadLocks {
            ReadLocks() = default;
            ReadLocks(const ReadLocks&) = delete;
            ReadLocks& operator=(const ReadLocks&) = delete;
            std::vector<HANDLE> handles;
            ~ReadLocks() { for (const auto handle : handles) CloseHandle(handle); }
        };
        unsigned sharingDenials = 0;
        {
            ReadLocks locks;
            for (const auto& wave : waves) {
                const auto handle = CreateFileW(wave.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                require(handle != INVALID_HANDLE_VALUE, "actual read-only exclusive cached PCM lock"); locks.handles.push_back(handle);
                const auto denied = CreateFileW(wave.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                const auto code = GetLastError(); if (denied != INVALID_HANDLE_VALUE) CloseHandle(denied);
                require(denied == INVALID_HANDLE_VALUE && code == ERROR_SHARING_VIOLATION, "actual Windows read denial, no ACL/file-byte changes"); ++sharingDenials;
            }
            const auto previous = runtimeLoadFaultPointForSmoke;
            struct FaultRestore { RuntimeLoadFaultPointForSmoke previous; ~FaultRestore() { runtimeLoadFaultPointForSmoke = previous; } } restore{previous};
            runtimeLoadFaultPointForSmoke = RuntimeLoadFaultPointForSmoke::BeforeCommit;
            const bool loaded = loadRuntimeState(s, savePath, why); runtimeLoadFaultPointForSmoke = previous;
            require(!loaded && why.find("load materialization failed:") == 0 && authority() == hash
                && s.tick == tick && s.worldAudio.stateHash() == domain && s.runtimeVoiceAudio.get() == session
                && session->queuedBuffers(play.handleId) == 4 && session->isPlaying(play.handleId).value_or(false),
                "actual before-commit rejection preserves complete authority and all live owned PCM");
            require(loadRuntimeState(s, savePath, why), "real valid load must commit despite post-commit audio resource failure: " + why);
            const auto committedHash = trace("authority-postcommit.trace");
            std::ofstream committedState(directory / "postcommit-world.ini");
            require(writeRuntimeStateToStream(s, committedState, why), "complete postcommit diagnostic state: " + why);
            std::cout << "World postcommit detail: authority=" << committedHash << " expected=" << hash
                << " domain=" << s.worldAudio.stateHash() << " expectedDomain=" << domain
                << " tick=" << s.tick << " expectedTick=" << tick << " sameSession=" << (s.runtimeVoiceAudio.get() == session)
                << " nativeVoices=" << session->voiceCount() << " bindings=" << s.runtimeWorldAudioBackendBindings.size() << '\n';
            require(s.runtimeVoiceAudio.get() == session && session->voiceCount() == 0
                && s.runtimeWorldAudioBackendBindings.empty(), "postcommit resource failure releases original native PCM");
            require(s.tick == tick && s.worldAudio.stateHash() == domain, "postcommit original World audio domain unchanged");
            require(committedHash == hash, "postcommit complete authority unchanged; compare explicit full traces");
        }
        require(loadRuntimeState(s, savePath, why), "ordinary load after sharing denial released: " + why);
        require(s.runtimeVoiceAudio.get() == session && session->voiceCount() == 1
            && s.runtimeWorldAudioBackendBindings.size() == 1 && session->queuedBuffers(play.handleId) <= 1,
            "same original session recreates loop; original four-buffer owner was destroyed");
        listen(); advanceRuntimeSpatialAudioMixes(s);
        require(session->isPlaying(play.handleId).value_or(false) && session->queuedBuffers(play.handleId) == 1
            && authority() == hash && s.tick == tick && s.worldAudio.stateHash() == domain,
            "restored real native loop follows original listener policy without advancing authority");
        RuntimeWorldAudioStopRequest stop; stop.handleId = play.handleId; stop.requestToken = "P08-explicit-stop";
        const auto stopped = s.worldAudio.stopAt(runtimeWorldAudioContext(s), stop, tick);
        require(stopped.accepted && !stopped.actions.empty(), "same real domain stop request");
        applyRuntimeWorldAudioBackendActions(s, stopped.actions);
        require(session->voiceCount() == 0 && s.runtimeWorldAudioBackendBindings.empty(), "domain stop disposes all native loop PCM");
        const RuntimeWorldAudioSoundSpec* finite = nullptr;
        for (const auto& value : s.worldAudioCatalog.sounds()) if (!value.loop && !value.predelay
            && value.delayMinMilliseconds == 0 && value.delayMaxMilliseconds == 0
            && std::find(value.samples.begin(), value.samples.end(), "vgriatta") != value.samples.end()) { finite = &value; break; }
        require(finite != nullptr, "original finite Grizzly report event exists");
        RuntimeWorldAudioPlayRequest shot; shot.eventId = finite->eventId; shot.cell = cell; shot.requestToken = "P08-finite";
        const auto finitePlay = s.worldAudio.playAt(runtimeWorldAudioContext(s), shot, tick);
        require(finitePlay.accepted && finitePlay.handleId != 0, "same actual finite producer accepts original request: " + finitePlay.reason);
        ++s.tick; (void)advanceRuntimeSimulation(s);
        require(s.tick == tick + 1, "actual next full production tick dispatches finite native action");
        require(session->voiceCount() == 1 && session->isPlaying(finitePlay.handleId).value_or(false), "real finite native voice started");
        const auto finiteHash = authority(); const auto end = GetTickCount64() + 15000;
        while (session->isPlaying(finitePlay.handleId).value_or(false) && GetTickCount64() < end) Sleep(10);
        require(!session->isPlaying(finitePlay.handleId).value_or(true), "original finite WAV ends naturally, no forced stop");
        reapRuntimeWorldAudioBackendBindings(s);
        require(session->voiceCount() == 0 && s.runtimeWorldAudioBackendBindings.empty() && authority() == finiteHash,
            "original reap disposes finite PCM without authority mutation");
        stopRuntimeSfxPlayback(s); stopRuntimeWorldAudioBackend(s); stopRuntimeWorldAudioBackend(s);
        require(session->voiceCount() == 0 && s.runtimeWorldAudioBackendBindings.empty(), "repeat shutdown is empty");
        s.runtimeVoiceAudio.reset(); s.runtimeVoiceAudio = std::make_unique<RuntimeVoiceAudio>(true);
        require(s.runtimeVoiceAudio->ready() && s.runtimeVoiceAudio->voiceCount() == 0, "real native session destructor/reentry");
        std::cout << "World audio device lifecycle: checks=" << checks << " failures=0 skipped=0 productionPrimeTicks=1 tick=" << tick
            << " activeSave=1 nativeQueueBefore=4 beforeCommitRollback=1 postCommitResourceFailure=1 sharingDenials=" << sharingDenials
            << " activeRehydrate=1 nativeQueueAfter=1 naturalFiniteReap=1 stopClose=1 nativeReentry=1 authority=" << hash
            << " output=muted noFakeWorld=yes\n";
        return true;
    } catch (const std::exception& exception) { error = std::string("world audio device lifecycle: ") + exception.what(); return false; }
}
