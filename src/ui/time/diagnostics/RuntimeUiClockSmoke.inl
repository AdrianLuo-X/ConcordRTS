// Production integration for the borrowed game-session presentation clock.
// Included after the actual UI consumers and ordinary load transaction.
bool runRuntimeUiClockSmokeTest(RuntimeWindowState& stock, std::ostream& output) {
    struct CountingClock final : RuntimeUiClock {
        std::uint64_t value = 0;
        mutable std::size_t reads = 0;
        std::uint64_t nowMs() const noexcept override { ++reads; return value; }
    } clock;
    struct ClockBinding {
        RuntimeWindowState& state;
        const RuntimeUiClock* previous;
        ClockBinding(RuntimeWindowState& value, const RuntimeUiClock& source)
            : state(value), previous(value.uiClock) { state.uiClock = &source; }
        ~ClockBinding() { state.uiClock = previous; }
    };
    std::size_t checks = 0;
    const auto expect = [&](bool condition, const char* label) {
        ++checks;
        if (!condition) output << "UI clock integration failed: " << label << '\n';
        return condition;
    };
    if (!expect(stock.report.success && stock.world.getSummary().loaded
            && stock.rulesLoaded && stock.artLoaded && stock.theaterLoaded
            && stock.rules.findUnitType("UnitType", "AMCV"), "original map/rules/art/AMCV")) return false;
    const char* requested = std::getenv("CONCORD_UI_CLOCK_OUTPUT");
    const std::filesystem::path directory = requested && *requested
        ? std::filesystem::path(requested) : std::filesystem::path("ui-clock-smoke-output");
    std::error_code directoryError;
    std::filesystem::create_directories(directory, directoryError);
    if (!expect(!directoryError, "isolated output directory")) return false;
    const auto make = [&]() {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config = stock.config;
        state.config.configPath = pathString(directory / "private-runtime.ini");
        state.config.gameMode = "Battle";
        state.config.gameModeRulesOverride.clear();
        state.config.multiplayerActive = false;
        state.config.playerCountry = "Americans";
        state.config.computerAiEnabled = false;
        state.config.shortGame = true;
        state.config.showMainMenu = state.config.showSkirmishMenu = false;
        state.config.keyboardReferenceLayout = true;
        state.config.keyBindings.clear();
        state.config.playerSlots.assign(runtimeMultiplayerMatchMaximumSlots, {});
        state.report = stock.report;
        state.world = stock.world;
        state.theater = stock.theater;
        state.rules = stock.rules;
        state.art = stock.art;
        state.mapFacts = stock.mapFacts;
        state.rulesLoaded = state.artLoaded = state.theaterLoaded = true;
        state.mapFactsLoaded = stock.mapFactsLoaded;
        state.terrainInfoByCell = stock.terrainInfoByCell;
        state.world.accessObjects().clear();
        state.runtimeSoundsEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.runtimePreferencePersistenceEnabled = false;
        state.runtimeMusicEnabled = false;
        state.runtimeVoiceVolume = state.runtimeSoundVolume = 0;
        state.visionEnabled = false;
        state.economy.owner = runtimeSkirmishOwnerForSlot(0);
        state.economy.skirmishStartApplied = true;
        state.triggers.bootstrapComplete = true;
        state.tick = 11; // Keep outcome observation off-cycle while testing local expiry.
        for (std::size_t index = 0; index < 3; ++index) {
            auto& slot = state.config.playerSlots[index];
            slot.enabled = slot.human = true;
            slot.local = index == 0;
            slot.country = index % 2 ? "Russians" : "Americans";
            slot.name = "UI clock player " + std::to_string(index + 1);
            slot.team = "-";
            const auto owner = runtimeCanonicalHumanHouseOwner(runtimeSkirmishOwnerForSlot(index));
            state.skirmishCountryByOwner[owner] = slot.country;
            state.skirmishTeamByOwner[owner] = runtimeNormalizeSkirmishTeam(slot.team);
        }
        initializeRuntimeNavigation(state);
        return storage;
    };
    auto storage = make();
    auto& state = *storage;
    ClockBinding binding(state, clock);
    RuntimeSnapshotActiveStateScope active(state);
    const auto owner = runtimeSkirmishOwnerForSlot(0);
    const auto ownerKey = runtimeCanonicalHumanHouseOwner(owner);
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const RECT client{0, 0, 800, 600};
    RuntimePlayerDefeatNoticeSmokeSurface surface(800, 600);
    if (!expect(surface.valid(), "native diagnostic DIB")) return false;
    const auto readPixels = [&]() {
        GdiFlush();
        return std::vector<std::uint32_t>(surface.pixels, surface.pixels + 800U * 600U);
    };

    // Default arguments used to sample even before campaign/eligibility guards.
    state.campaignMode = true; clock.value = 123;
    auto reads = clock.reads;
    if (!expect(!enqueueRuntimePlayerDefeatNotice(state, owner)
            && clock.reads == reads + 1, "enqueue samples before campaign guard")) return false;
    reads = clock.reads;
    if (!expect(!observeRuntimePlayerDefeatNotices(state)
            && clock.reads == reads + 1, "observe samples before campaign guard")) return false;
    state.campaignMode = false;
    resetRuntimePlayerDefeatNotices(state);
    clock.value = 1000; reads = clock.reads;
    if (!expect(enqueueRuntimePlayerDefeatNotice(state, owner)
            && clock.reads == reads + 1 && state.playerDefeatNotices.lines.front().untilMs == 16000,
            "real notice default source and deadline")) return false;
    clock.value = 2000; reads = clock.reads;
    if (!expect(!enqueueRuntimePlayerDefeatNotice(state, owner)
            && clock.reads == reads + 1 && state.playerDefeatNotices.lines.front().untilMs == 16000,
            "duplicate owner samples but does not restart deadline")) return false;
    clock.value = 15999; reads = clock.reads;
    if (!expect(!observeRuntimePlayerDefeatNotices(state) && clock.reads == reads + 1
            && state.playerDefeatNotices.lines.size() == 1, "notice alive before equality")) return false;
    clock.value = 16000; reads = clock.reads;
    if (!expect(observeRuntimePlayerDefeatNotices(state) && clock.reads == reads + 1
            && state.playerDefeatNotices.lines.empty(), "notice expires at equality")) return false;
    reads = clock.reads;
    drawRuntimePlayerDefeatNotices(nullptr, state, client);
    if (!expect(clock.reads == reads, "empty notice painter never reads time")) return false;
    resetRuntimePlayerDefeatNotices(state); clock.value = maximum - 1;
    if (!expect(enqueueRuntimePlayerDefeatNotice(state, owner)
            && state.playerDefeatNotices.lines.front().untilMs == maximum, "production notice saturation")) return false;
    clock.value = maximum;
    if (!expect(observeRuntimePlayerDefeatNotices(state)
            && state.playerDefeatNotices.lines.empty(), "saturated deadline equality")) return false;

    // These are the actual stateful tooltip consumers, including rollback/wrap.
    resetRuntimeGameplayTooltipHover(state); clock.value = 1000; reads = clock.reads;
    if (!expect(!runtimeGameplayObjectTooltipDelayElapsed(state, 41)
            && state.runtimeGameplayTooltipHoverObjectId == 41
            && state.runtimeGameplayTooltipHoverStartedMs == 1000 && clock.reads == reads + 1,
            "object hover first observation")) return false;
    clock.value = 3999;
    if (!expect(!runtimeGameplayObjectTooltipDelayElapsed(state, 41), "object 2999ms")) return false;
    clock.value = 4000;
    if (!expect(runtimeGameplayObjectTooltipDelayElapsed(state, 41), "object 3000ms")) return false;
    clock.value = 4500;
    if (!expect(!runtimeGameplayObjectTooltipDelayElapsed(state, 42)
            && state.runtimeGameplayTooltipHoverStartedMs == 4500, "object identity resets timer")) return false;
    clock.value = 4499;
    if (!expect(runtimeGameplayObjectTooltipDelayElapsed(state, 42), "object rollback preserves unsigned underflow")) return false;
    resetRuntimeGameplayObjectTooltipHover(state); clock.value = maximum - 10;
    if (!expect(!runtimeGameplayObjectTooltipDelayElapsed(state, 43), "object wrap origin")) return false;
    clock.value = 2988;
    if (!expect(!runtimeGameplayObjectTooltipDelayElapsed(state, 43), "object wrap at 2999ms")) return false;
    clock.value = 2989;
    if (!expect(runtimeGameplayObjectTooltipDelayElapsed(state, 43), "object wrap at 3000ms")) return false;
    resetRuntimeGameplayBuildTooltipHover(state); clock.value = 2000;
    if (!expect(!runtimeGameplayBuildTooltipDelayElapsed(state, 2), "build first observation")) return false;
    clock.value = 2999;
    if (!expect(!runtimeGameplayBuildTooltipDelayElapsed(state, 2), "build 999ms")) return false;
    clock.value = 3000;
    if (!expect(runtimeGameplayBuildTooltipDelayElapsed(state, 2), "build 1000ms")) return false;
    clock.value = 4000;
    if (!expect(!runtimeGameplayBuildTooltipDelayElapsed(state, 3), "build identity resets timer")) return false;
    clock.value = 3999;
    if (!expect(runtimeGameplayBuildTooltipDelayElapsed(state, 3), "build rollback preserves unsigned underflow")) return false;
    resetRuntimeGameplayBuildTooltipHover(state); clock.value = maximum - 10;
    if (!expect(!runtimeGameplayBuildTooltipDelayElapsed(state, 4), "build wrap origin")) return false;
    clock.value = 988;
    if (!expect(!runtimeGameplayBuildTooltipDelayElapsed(state, 4), "build wrap at 999ms")) return false;
    clock.value = 989;
    if (!expect(runtimeGameplayBuildTooltipDelayElapsed(state, 4), "build wrap at 1000ms")) return false;
    state.runtimeTooltipsEnabled = false; reads = clock.reads;
    (void)runtimeGameplayTooltipAtPoint(state, client);
    if (!expect(clock.reads == reads && state.runtimeGameplayTooltipHoverObjectId == -1
            && state.runtimeGameplayTooltipHoverBuildButtonIndex == -1
            && state.runtimeGameplayTooltipHoverStartedMs == 0
            && state.runtimeGameplayTooltipHoverBuildStartedMs == 0, "disabled tooltip resets without sampling")) return false;
    state.runtimeTooltipsEnabled = true;

    reads = clock.reads; clock.value = 500;
    drawRuntimeSidebar(surface.dc, client, state, 499);
    if (!expect(clock.reads == reads, "explicit sidebar now never samples provider")) return false;
    drawRuntimeSidebar(surface.dc, client, state);
    if (!expect(clock.reads == reads + 1, "default sidebar samples exactly once")) return false;
    state.playerCommunication.diplomacyOpen = true; state.runtimeMenuOpen = true; reads = clock.reads;
    drawRuntimePlayerHotkeyOverlay(surface.dc, client, state);
    if (!expect(clock.reads == reads, "diplomacy hotkey overlay early return does not sample")) return false;
    state.playerCommunication.diplomacyOpen = false; state.runtimeMenuOpen = false;
    drawRuntimePlayerHotkeyOverlay(surface.dc, client, state);
    if (!expect(clock.reads == reads + 1, "chat stack layout samples its real provider")) return false;

    // Real result state machine, not a fixture copy; explicit-now stays explicit.
    state.skirmishMatchFinished = true; state.multiplayerLocalSpectator = true;
    state.resultPresentation = {}; clock.value = 1000; reads = clock.reads;
    advanceRuntimeUiPresentation(state);
    const auto bannerDuration = state.resultPresentation.bannerDurationMs;
    if (!expect(clock.reads == reads + 1 && state.resultPresentation.started
            && state.resultPresentation.bannerVisible && state.resultPresentation.startedAtMs == 1000,
            "loop source starts actual result banner")) return false;
    reads = clock.reads;
    advanceRuntimeResultPresentation(state, 999);
    if (!expect(clock.reads == reads && state.resultPresentation.bannerVisible, "banner rollback guard and explicit time")) return false;
    if (bannerDuration > 0) {
        advanceRuntimeResultPresentation(state, 1000 + bannerDuration - 1);
        if (!expect(state.resultPresentation.bannerVisible, "banner before duration equality")) return false;
    }
    advanceRuntimeResultPresentation(state, 1000 + bannerDuration);
    if (!expect(!state.resultPresentation.bannerVisible
            && state.campaignScore.presentationStartedAtMs == 1000 + bannerDuration,
            "banner equality enters actual score page")) return false;
    state.resultPresentation = {}; clock.value = 0;
    advanceRuntimeUiPresentation(state);
    if (!expect(state.resultPresentation.startedAtMs == 0 && state.resultPresentation.started,
            "zero source is a valid banner origin")) return false;
    state.resultPresentation.bannerDurationMs = 0;
    advanceRuntimeResultPresentation(state, 0);
    if (!expect(!state.resultPresentation.bannerVisible
            && state.campaignScore.presentationStartedAtMs == 0, "zero duration and original score zero sentinel")) return false;
    state.campaignScore.captured = true; state.campaignScore.finalScore = 73;
    clock.value = 77; reads = clock.reads; finalizeRuntimeCampaignScore(state, true);
    clock.value = 88; finalizeRuntimeCampaignScore(state, true);
    if (!expect(clock.reads == reads + 2 && state.campaignScore.presentationStartedAtMs == 88
            && state.campaignScore.finalScore == 73, "captured score still resamples animation start")) return false;

    // Actual native fallback/main painters consume their distinct elapsed policies.
    state.campaignMode = true; state.campaignScore.presentationStartedAtMs = 0;
    clock.value = 10; reads = clock.reads;
    drawRuntimeCampaignScoreFallbackOverlay(surface.dc, client, state);
    const auto fallbackSentinel = readPixels();
    if (!expect(clock.reads == reads + 1, "fallback painter samples once")) return false;
    state.campaignScore.presentationStartedAtMs = 100; clock.value = 99;
    drawRuntimeCampaignScoreFallbackOverlay(surface.dc, client, state);
    if (!expect(readPixels() == fallbackSentinel, "fallback rollback uses same 2000ms frame as zero sentinel")) return false;
    clock.value = 100;
    drawRuntimeCampaignScoreFallbackOverlay(surface.dc, client, state);
    if (!expect(readPixels() != fallbackSentinel, "fallback first frame differs from 2000ms final bars")) return false;
    state.campaignScore.presentationStartedAtMs = 0; clock.value = 10; reads = clock.reads;
    drawRuntimeCampaignScoreOverlay(surface.dc, client, state);
    const auto mainSentinel = readPixels();
    std::array<std::size_t, 2> mainSampleCounts{clock.reads - reads, 0};
    if (!expect(mainSampleCounts[0] == 1 || mainSampleCounts[0] == 2,
            "actual score entry sentinel source count")) return false;
    state.campaignScore.presentationStartedAtMs = 100; clock.value = 99; reads = clock.reads;
    drawRuntimeCampaignScoreOverlay(surface.dc, client, state);
    mainSampleCounts[1] = clock.reads - reads;
    if (!expect((mainSampleCounts[1] == 1 || mainSampleCounts[1] == 2)
            && readPixels() == mainSentinel, "main entry rollback/sentinel rendering matches")) return false;
    // Distinct 2000/3000ms constants are also checked against frozen expressions
    // by the independent contract. Missing native art is not silently called playback.

    state.campaignMode = false; state.skirmishMatchFinished = false;
    state.multiplayerLocalSpectator = false; state.resultPresentation = {};
    resetRuntimePlayerDefeatNotices(state); clock.value = 1000;
    if (!expect(enqueueRuntimePlayerDefeatNotice(state, owner), "paused notice admission")) return false;
    const auto pausedHash = state.world.deterministicSimulationStateHash();
    const auto pausedCommands = state.playerCommands.serialize();
    const int pausedTick = state.tick;
    const auto powerSteps = runtimeSidebarPowerAnimationElapsedSteps(state);
    clock.value = 16000; reads = clock.reads;
    advanceRuntimeUiPresentation(state);
    if (!expect(clock.reads == reads + 1 && state.playerDefeatNotices.lines.empty()
            && state.lastSidebarReadyBlinkPeriod == 32 && state.tick == pausedTick
            && state.world.deterministicSimulationStateHash() == pausedHash
            && state.playerCommands.serialize() == pausedCommands
            && runtimeSidebarPowerAnimationElapsedSteps(state) == powerSteps,
            "real paused UI advance preserves tick/hash/commands and tick-based power")) return false;

    // Populate the actual production world for keyboard camera/selection and load.
    const auto& terrain = state.world.getTerrainCells();
    if (!expect(!terrain.empty(), "original terrain available")) return false;
    std::vector<int> objects;
    for (std::size_t index = 0; index < 3; ++index) {
        const auto& cell = terrain[(index * 137) % terrain.size()];
        auto* object = addRuntimeObjectInstanceForOwner(state, runtimeSkirmishOwnerForSlot(index),
            RuntimeBuildKind::Unit, "AMCV", {cell.x, cell.y});
        if (!expect(object != nullptr, "actual AMCV creation")) return false;
        object->mission = "Guard";
        object->buildAnimTicksTotal = object->buildAnimTicksRemaining = 0;
        clearRuntimeRenderPosition(*object);
        objects.push_back(object->instanceId);
    }
    ensureRuntimeMenuKeyboardLoaded(state);
    // Drive the actual consumer with typed time. Native GetClientRect/GetKeyState
    // remain in their original handler and are not exercised by this fixture.
    const auto sampleKey = [&](int code, bool repeated) {
        return handleRuntimePlayerCommandCode(state, client, code, repeated, runtimeUiClockNow(state));
    };
    state.selectedObjectIds = {objects.front()}; clock.value = 1000; reads = clock.reads;
    if (!expect(sampleKey('1' | runtimePlayerControlKey, false)
            && clock.reads == reads + 1 && state.playerLocalCommands.teams[0] == state.selectedObjectIds,
            "real keyboard consumer receives sampled source for team-create")) return false;
    const auto keyHash = state.world.deterministicSimulationStateHash();
    const auto keyCommands = state.playerCommands.serialize();
    const int keyTick = state.tick;
    const auto doubleSelection = [&](std::uint64_t now, std::uint64_t last, bool centers) {
        state.playerLocalCommands.lastSelectionKey = '1';
        state.playerLocalCommands.lastSelectionAtMs = last;
        state.camera.worldOffsetX = 123456789;
        state.camera.worldOffsetY = 123456789;
        clock.value = now; const auto beforeReads = clock.reads;
        const bool accepted = sampleKey('1', false);
        const bool moved = state.camera.worldOffsetX != 123456789 || state.camera.worldOffsetY != 123456789;
        return accepted && clock.reads == beforeReads + 1 && moved == centers
            && state.playerLocalCommands.lastSelectionAtMs == now;
    };
    if (!expect(doubleSelection(2499, 2000, true), "real team 499ms centers")
        || !expect(doubleSelection(2500, 2000, true), "real team 500ms centers")
        || !expect(doubleSelection(2501, 2000, false), "real team 501ms does not center")
        || !expect(doubleSelection(1999, 2000, false), "real team rollback is not clamped")
        || !expect(doubleSelection(5, maximum - 10, true), "real team unsigned wrap remains within threshold")) return false;
    state.playerLocalCommands.lastSelectionAtMs = 4444; clock.value = 5000; reads = clock.reads;
    if (!expect(sampleKey('1', true)
            && clock.reads == reads + 1 && state.playerLocalCommands.lastSelectionAtMs == 4444,
            "repeat samples before existing controller guard but leaves selection time")) return false;
    if (!expect(state.world.deterministicSimulationStateHash() == keyHash
            && state.playerCommands.serialize() == keyCommands && state.tick == keyTick,
            "keyboard local timing never changes authority/tick/commands")) return false;

    // Use the existing ordinary save/load transaction, including its fault hook.
    resetRuntimePlayerDefeatNotices(state); state.tick = 21;
    (void)advanceRuntimeParticles(state);
    std::string error;
    const auto savePath = pathString(directory / "ui-clock-load.ini");
    if (!expect(saveRuntimeState(state, savePath, error), "actual isolated save")) {
        output << error << '\n'; return false;
    }
    auto seeded = std::make_unique<RuntimeWindowState>();
    seedRuntimeLoadScratch(*seeded, state, false);
    if (!expect(seeded->uiClock == &clock, "detached scratch inherits exact typed provider")) return false;
    IniFile invalid;
    if (!expect(invalid.loadFromString("[RuntimeSave]\nVersion=999\n"), "invalid-stage fixture parse")) return false;
    const auto loadHash = state.world.deterministicSimulationStateHash();
    const auto loadCommands = state.playerCommands.serialize();
    if (!expect(!materializeRuntimeLoadStateFromIni(*seeded, invalid, error)
            && !error.empty() && state.uiClock == &clock && state.tick == 21
            && state.world.deterministicSimulationStateHash() == loadHash
            && state.playerCommands.serialize() == loadCommands, "failed detached stage preserves live source/state")) return false;
    const auto previousFault = runtimeLoadFaultPointForSmoke;
    struct FaultRestore {
        RuntimeLoadFaultPointForSmoke previous;
        ~FaultRestore() { runtimeLoadFaultPointForSmoke = previous; }
    } restoreFault{previousFault};
    runtimeLoadFaultPointForSmoke = RuntimeLoadFaultPointForSmoke::BeforeCommit;
    const bool faultAccepted = loadRuntimeState(state, savePath, error);
    runtimeLoadFaultPointForSmoke = previousFault;
    if (!expect(!faultAccepted && error.find("load materialization failed:") == 0
            && state.uiClock == &clock && state.tick == 21
            && state.world.deterministicSimulationStateHash() == loadHash
            && state.playerCommands.serialize() == loadCommands, "actual before-commit fault keeps source and authority")) return false;
    if (!expect(loadRuntimeState(state, savePath, error) && state.uiClock == &clock && state.tick == 21,
            "successful actual load retains provider identity")) {
        output << error << '\n'; return false;
    }
    auto staged = std::make_unique<RuntimeWindowState>();
    seedRuntimeLoadScratch(*staged, state, false);
    if (!expect(materializeRuntimeLoadState(*staged, savePath, error), "actual detached successful materialization")) {
        output << error << '\n'; return false;
    }
    prepareRuntimePlayerDefeatNoticeLoadBaseline(*staged);
    CountingClock alternative;
    staged->uiClock = &alternative;
    commitRuntimeLoadedState(state, *staged);
    if (!expect(state.uiClock == &clock && staged->uiClock == &alternative,
            "real noexcept commit never swaps host provider dependency")) return false;
    // Restore the borrowed alternate before it leaves scope, even though state
    // destruction currently has no clock reads. This makes the lifetime explicit.
    staged->uiClock = &clock;
    const auto provider = state.uiClock;
    { CountingClock temporary; ClockBinding temporaryBinding(state, temporary); }
    if (!expect(state.uiClock == provider, "nested override restores session provider")) return false;

    output << "UI clock integration passed: checks=" << checks << " sourceReads=" << clock.reads
        << " noticeDefault=1 tooltipProduction=1 resultProduction=1 sidebarDefaultExplicit=1"
        << " hotkeyEarlyReturn=1 keyboardConsumer500=1 pauseAuthority=1 scratchSameSource=1"
        << " failedStage=1 failedLoad=1 successfulLoad=1 noThrowCommitSource=1"
        << " mpFullSnapshotRestore=not-run nativeVideoPlayback=not-run deviceContinuity=not-run"
        << " mainEntrySampleCounts=" << mainSampleCounts[0] << ',' << mainSampleCounts[1] << " lateFallbackBranch=not-forced nativeKeyAdapter=not-run legacyGraphOverflow=not-covered\n";
    return true;
}