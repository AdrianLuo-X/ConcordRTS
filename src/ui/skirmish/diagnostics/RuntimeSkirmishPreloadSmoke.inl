// Exercises the real asynchronous prepare and modal entry/exit paths using a
// hidden host. No audio or foreground changes; timing is diagnostic only.
bool runSkirmishPreloadSmoke(const RuntimeConfig& config, const fs::path& outputRoot) {
    using Clock = std::chrono::steady_clock;
    const auto milliseconds = [](Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };
    fs::create_directories(outputRoot);
    HWND host = CreateWindowExW(0, L"STATIC", L"Skirmish preload smoke",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, kBaseWidth, kBaseHeight,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (host == nullptr) return false;
    struct HostGuard {
        HWND window;
        ~HostGuard() { if (IsWindow(window)) DestroyWindow(window); }
    } hostGuard{host};

    // An unfinished future must never be joined by the readiness poll. A
    // gate makes this deterministic even on very fast machines.
    std::promise<void> gate;
    std::future<void> released = gate.get_future();
    pendingMenuState = std::async(std::launch::async,
        [config, released = std::move(released)]() mutable {
            released.wait();
            auto state = std::make_unique<MenuState>();
            initializeState(*state, config, true);
            return state;
        });
    const auto coldStarted = Clock::now();
    const bool prematureReady = runtimeSkirmishMenuReady(config);
    prewarmRuntimeSkirmishMenu(config); // Must not replace/join the active job.
    const double pendingPollMs = milliseconds(coldStarted);
    gate.set_value();
    if (prematureReady) return false;

    int heartbeatCount = 0;
    SetWindowLongPtrW(host, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&heartbeatCount));
    SetTimer(host, 71, 1, [](HWND window, UINT, UINT_PTR, DWORD) {
        ++*reinterpret_cast<int*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    });
    while (!runtimeSkirmishMenuReady(config)) {
        if (milliseconds(coldStarted) > 30000.0) return false;
        MsgWaitForMultipleObjectsEx(0, nullptr, 5, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    KillTimer(host, 71);
    const double coldMs = milliseconds(coldStarted);
    if (prewarmedMenuState == nullptr || !prewarmedMenuState->initialized
        || prewarmedMenuState->hwnd != nullptr
        || !prewarmedMenuState->resourceWatch->unchanged(config)) return false;

    MenuState* const resourceOwner = prewarmedMenuState.get();
    const auto* const maps = resourceOwner->maps.data();
    const auto* const shellPixels = resourceOwner->background.bgra.data();
    const fs::path preferencePath = outputRoot / "preload_preferences.ini";
    RuntimeConfig entryConfig = config;
    entryConfig.configPath = pathString(preferencePath);
    entryConfig.playerSlots.clear();
    entryConfig.gameMode = "Battle";
    entryConfig.playerCountry = "Americans";
    entryConfig.playerColor = "Gold";

    struct EntryProbe {
        RuntimeConfig config;
        bool crates = false;
        bool called = false;
        bool valid = false;
    } probe;
    double maxEntryMs = 0;
    for (int visit = 0; visit < 4; ++visit) {
        entryConfig.gameSpeed = visit + 1;
        entryConfig.startingCredits = 10000 + 5000 * visit;
        entryConfig.computerAiEnabled = visit % 2 != 0;
        const bool crates = visit % 2 == 0;
        {
            std::ofstream preferences(preferencePath);
            preferences << "[Skirmish]\nCrates=" << (crates ? "yes" : "no") << '\n';
            if (!preferences.good()) return false;
        }
        // Dirty state left by a canceled map picker/accepted match must not
        // leak into a subsequent visit even though the resources are reused.
        resourceOwner->page = SkirmishPage::MapSelection;
        resourceOwner->selection.accepted = true;
        resourceOwner->closing = true;
        resourceOwner->openDropdown = DropdownKind::Country;
        resourceOwner->startingSpots.fill(8);
        probe = EntryProbe{entryConfig, crates};
        SetWindowLongPtrW(host, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&probe));
        SetTimer(host, 72, 1, [](HWND window, UINT, UINT_PTR id, DWORD) {
            HWND child = FindWindowExW(window, nullptr,
                L"ConcordRTS.RuntimeSkirmishShell", nullptr);
            if (child == nullptr) return;
            KillTimer(window, id);
            auto& probe = *reinterpret_cast<EntryProbe*>(
                GetWindowLongPtrW(window, GWLP_USERDATA));
            auto& state = *reinterpret_cast<MenuState*>(
                GetWindowLongPtrW(child, GWLP_USERDATA));
            probe.called = true;
            probe.valid = state.page == SkirmishPage::Lobby
                && !state.closing && !state.selection.accepted
                && state.openDropdown == DropdownKind::None
                && state.selection.gameSpeed == probe.config.gameSpeed
                && state.selection.startingCredits == probe.config.startingCredits
                && state.selection.computerAiEnabled == probe.config.computerAiEnabled
                && state.selection.crates == probe.crates
                && state.selection.playerCountry == "Americans"
                && state.selection.playerColor == "Gold"
                && std::all_of(state.startingSpots.begin(), state.startingSpots.end(),
                    [](int spot) { return spot == 0; })
                && GetWindowThreadProcessId(child, nullptr) == GetCurrentThreadId();
            state.phase = ShellMenuPhase::Idle;
            completeMenu(state, false);
            updateMenuTransition(state, GetTickCount64() + 1000);
        });
        const auto entryStarted = Clock::now();
        prewarmRuntimeSkirmishMenu(entryConfig);
        if (!runtimeSkirmishMenuReady(entryConfig)) return false;
        const RuntimeSkirmishSelection result = runRuntimeSkirmishMenu(entryConfig, host);
        const double entryMs = milliseconds(entryStarted);
        maxEntryMs = std::max(maxEntryMs, entryMs);
        if (!probe.called || !probe.valid || result.accepted || !IsWindow(host)
            || prewarmedMenuState.get() != resourceOwner || resourceOwner->hwnd != nullptr
            || resourceOwner->maps.data() != maps
            || resourceOwner->background.bgra.data() != shellPixels) return false;
    }

    // Use isolated directories to verify freshness without touching the
    // player's maps: adding a map, editing rules, and switching roots all
    // invalidate reuse, while saving ordinary preferences does not.
    const fs::path fixtureRoot = outputRoot / "watch_fixture";
    RuntimeConfig watchConfig = config;
    watchConfig.resourceRootPath = pathString(fixtureRoot / "resources");
    watchConfig.localDataRootPath = pathString(fixtureRoot / "local");
    fs::create_directories(fs::path(watchConfig.resourceRootPath));
    const fs::path exportRoot = fs::path(watchConfig.localDataRootPath)
        / "resources" / "mix_exports";
    fs::create_directories(exportRoot / "multimd");
    RuntimeMenuResourceWatch mapWatch(watchConfig);
    if (!mapWatch.unchanged(watchConfig)) return false;
    {
        std::ofstream map(exportRoot / "multimd" / "added.map", std::ios::app);
        map << "\n[Map]\nSize=0,0,32,32\n";
    }
    bool mapInvalidated = false;
    for (int attempt = 0; attempt < 100 && !mapInvalidated; ++attempt) {
        mapInvalidated = !mapWatch.unchanged(watchConfig);
        if (!mapInvalidated) Sleep(1);
    }
    RuntimeMenuResourceWatch rulesWatch(watchConfig);
    {
        std::ofstream rules(fs::path(watchConfig.localDataRootPath) / "rulesmd.ini",
            std::ios::app);
        rules << "\n[Countries]\n0=Americans\n";
    }
    const bool rulesInvalidated = !rulesWatch.unchanged(watchConfig);
    RuntimeMenuResourceWatch preferenceWatch(watchConfig);
    {
        std::ofstream preferences(fs::path(watchConfig.localDataRootPath) / "runtime_config.ini");
        preferences << "[Skirmish]\nCrates=yes\n";
    }
    RuntimeConfig otherRoot = watchConfig;
    otherRoot.resourceRootPath += "_other";
    const bool freshnessValid = mapInvalidated && rulesInvalidated
        && preferenceWatch.unchanged(watchConfig) && !preferenceWatch.unchanged(otherRoot);
    prewarmedMenuState.reset();

    // Closing a hosted direct launch while its worker is pending must exit,
    // without reopening the lobby as a new standalone window afterwards.
    std::promise<void> closeGate;
    pendingMenuState = std::async(std::launch::async,
        [config, released = closeGate.get_future()]() mutable {
            released.wait();
            auto state = std::make_unique<MenuState>();
            state->config = config;
            return state;
        });
    PostMessageW(host, WM_CLOSE, 0, 0);
    const RuntimeSkirmishSelection closed = runRuntimeSkirmishMenu(config, host);
    closeGate.set_value();
    pendingMenuState.get();
    const bool closedWhilePending = !closed.accepted && !IsWindow(host);
    std::cout << "Skirmish preload: pendingPollMs=" << pendingPollMs
        << " coldWorkerMs=" << coldMs << " responsiveHeartbeats=" << heartbeatCount
        << " visits=4 maxEntryMs=" << maxEntryMs
        << " resourcesReused=yes settingsRefreshed=yes freshness=" << freshnessValid
        << " closeWhilePending=" << closedWhilePending << '\n';
    return freshnessValid && closedWhilePending;
}
