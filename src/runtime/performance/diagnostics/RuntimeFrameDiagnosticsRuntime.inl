// Included by RuntimeShell.cpp after RuntimeWindowState is complete. All work in
// a normal paint is bounded; filesystem and report formatting run only at exit.

RuntimeFrameCacheCounters runtimeDiagnosticCacheCounters(
    const RuntimeWindowState& state
) noexcept {
    const auto& cache = state.renderCache;
    // Renderer counters currently have signed int storage. Convert separately,
    // before addition, and reject a wrapped/negative counter rather than publish
    // a huge unsigned value. These count cache lookups, not distinct assets.
    const int counters[] = {
        cache.sequenceFrameCacheMisses,
        cache.projectilePaletteCacheMisses,
        cache.auxiliaryPaletteCacheMisses,
        cache.auxiliaryShpBytesCacheMisses,
        cache.auxiliaryShpFrameCacheMisses,
        cache.voxelCacheMisses,
        cache.buildingAnimationLayerCacheMisses,
        cache.terrainLayerCacheMisses,
        cache.projectileTypeCacheMisses,
        cache.auxiliaryEffectKindCacheMisses,
    };
    RuntimeFrameCacheCounters result;
    for (const int count : counters) {
        if (count < 0) return result;
        result.totalMisses += static_cast<std::uint64_t>(count);
    }
    result.measured = true;
    result.voxelMisses = static_cast<std::uint64_t>(cache.voxelCacheMisses);
    result.spriteMisses = static_cast<std::uint64_t>(cache.sequenceFrameCacheMisses)
        + static_cast<std::uint64_t>(cache.auxiliaryShpBytesCacheMisses)
        + static_cast<std::uint64_t>(cache.auxiliaryShpFrameCacheMisses)
        + static_cast<std::uint64_t>(cache.buildingAnimationLayerCacheMisses);
    result.paletteMisses = static_cast<std::uint64_t>(cache.projectilePaletteCacheMisses)
        + static_cast<std::uint64_t>(cache.auxiliaryPaletteCacheMisses);
    return result;
}

void resetRuntimeFrameDiagnosticsInterval(RuntimeWindowState& state) noexcept {
    state.diagnosticLastPaint = {};
    state.diagnosticPendingSimulationMs = 0.0;
    state.diagnosticPendingPathMs = 0.0;
    state.diagnosticPendingStages = {};
    state.diagnosticPendingMoveCommands.reset();
    state.diagnosticPendingNestedStages.reset();
    state.diagnosticPendingSimulationTicks = 0;
    state.diagnosticLastColdResourceSequence = state.renderCache.lastColdResourceSequence;
    if (state.frameDiagnostics != nullptr) {
        state.frameDiagnostics->primeCacheCounters(runtimeDiagnosticCacheCounters(state));
    }
}

// This scope changes only presentation diagnostics. With diagnostics absent or
// disabled it does not read a clock, allocate, or inspect the world objects.
class RuntimeMoveCommandDiagnosticScope {
    RuntimeWindowState* state_ = nullptr;
    const RuntimeFrameDiagnostics* diagnostics_ = nullptr;
    RuntimeFrameMoveCommandInterval::ScopeToken token_;
    std::chrono::steady_clock::time_point started_{};
public:
    explicit RuntimeMoveCommandDiagnosticScope(RuntimeWindowState& state) noexcept {
        if (state.frameDiagnostics == nullptr || !state.frameDiagnostics->enabled()) return;
        if (!state.world.getSummary().loaded || state.runtimeMenuOpen || state.skirmishMatchFinished) return;
        state_ = &state;
        diagnostics_ = state.frameDiagnostics.get();
        token_ = state.diagnosticPendingMoveCommands.enter();
        if (token_.ownsTimer) started_ = std::chrono::steady_clock::now();
    }
    ~RuntimeMoveCommandDiagnosticScope() {
        if (state_ == nullptr) return;
        const bool valid = token_.ownsTimer
            && state_->frameDiagnostics.get() == diagnostics_
            && state_->frameDiagnostics->enabled()
            && state_->world.getSummary().loaded && !state_->runtimeMenuOpen
            && !state_->skirmishMatchFinished
            && state_->diagnosticPendingMoveCommands.generation() == token_.generation;
        const double elapsed = valid ? std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started_).count() : 0.0;
        state_->diagnosticPendingMoveCommands.leave(token_, elapsed, valid);
    }
    RuntimeMoveCommandDiagnosticScope(const RuntimeMoveCommandDiagnosticScope&) = delete;
    RuntimeMoveCommandDiagnosticScope& operator=(const RuntimeMoveCommandDiagnosticScope&) = delete;
};

// Only three coarse nested stages use this hook. It never visits terrain or
// objects and takes no timestamp when diagnostics are absent/disabled. The
// accumulator generation rejects work crossing a discarded paint interval.
class RuntimeNestedStageDiagnosticScope {
    RuntimeWindowState* state_ = nullptr;
    const RuntimeFrameDiagnostics* diagnostics_ = nullptr;
    RuntimeFrameNestedStageInterval::ScopeToken token_;
    std::chrono::steady_clock::time_point started_{};
public:
    RuntimeNestedStageDiagnosticScope(RuntimeWindowState& state, std::uint32_t stage) noexcept {
        if (state.frameDiagnostics == nullptr || !state.frameDiagnostics->enabled()) return;
        if (!state.world.getSummary().loaded || state.runtimeMenuOpen || state.skirmishMatchFinished) return;
        state_ = &state;
        diagnostics_ = state.frameDiagnostics.get();
        token_ = state.diagnosticPendingNestedStages.enter(stage);
        if (token_.ownsTimer) started_ = std::chrono::steady_clock::now();
    }
    ~RuntimeNestedStageDiagnosticScope() {
        if (state_ == nullptr) return;
        const bool valid = token_.ownsTimer
            && state_->frameDiagnostics.get() == diagnostics_
            && state_->frameDiagnostics->enabled()
            && state_->world.getSummary().loaded && !state_->runtimeMenuOpen
            && !state_->skirmishMatchFinished
            && state_->diagnosticPendingNestedStages.generation() == token_.generation;
        const double elapsed = valid ? std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started_).count() : 0.0;
        state_->diagnosticPendingNestedStages.leave(token_, elapsed, valid);
    }
    RuntimeNestedStageDiagnosticScope(const RuntimeNestedStageDiagnosticScope&) = delete;
    RuntimeNestedStageDiagnosticScope& operator=(const RuntimeNestedStageDiagnosticScope&) = delete;
};

void accumulateRuntimeFrameSimulationDiagnostics(RuntimeWindowState& state) noexcept {
    if (state.frameDiagnostics == nullptr || !state.frameDiagnostics->enabled()) return;
    if (!state.world.getSummary().loaded || state.runtimeMenuOpen
        || state.skirmishMatchFinished) {
        resetRuntimeFrameDiagnosticsInterval(state);
        return;
    }
    // These hooks run only after a timed simulation callback. Every tick resets
    // its perf fields, so no stage from a previous tick is counted twice.
    const auto addMeasured = [](double& total, double value) noexcept {
        if (std::isfinite(value) && value >= 0.0) total += value;
    };
    addMeasured(state.diagnosticPendingSimulationMs, state.perf.lastSimulationMs);
    addMeasured(state.diagnosticPendingPathMs, state.perf.lastSimPathMs);
    auto& pending = state.diagnosticPendingStages;
    addMeasured(pending.simVisionMs, state.perf.lastSimVisionMs);
    addMeasured(pending.simMoveMs, state.perf.lastSimMoveMs);
    addMeasured(pending.simAttackMs, state.perf.lastSimAttackMs);
    addMeasured(pending.simEconomyMs, state.perf.lastSimEconomyMs);
    addMeasured(pending.simEffectsMs, state.perf.lastSimEffectsMs);
    addMeasured(pending.simGuardMs, state.perf.lastSimGuardMs);
    pending.measuredStages |= RuntimeFrameSimulation | RuntimeFramePathfinding
        | RuntimeFrameSimVision | RuntimeFrameSimMove | RuntimeFrameSimAttack
        | RuntimeFrameSimEconomy | RuntimeFrameSimEffects | RuntimeFrameSimGuard;
    ++state.diagnosticPendingSimulationTicks;
}

void recordRuntimeCompletedFrameDiagnostics(
    RuntimeWindowState& state,
    int width,
    int height,
    bool reused,
    bool submitted
) noexcept {
    if (state.frameDiagnostics == nullptr || !state.frameDiagnostics->enabled()) return;
    // submitted already incorporates the actual window visibility/minimization
    // check, with an explicit exception for the hidden live-frame benchmark.
    if (!submitted || width <= 0 || height <= 0
        || !state.world.getSummary().loaded || state.runtimeMenuOpen
        || state.skirmishMatchFinished) {
        resetRuntimeFrameDiagnosticsInterval(state);
        return;
    }
    const auto completed = std::chrono::steady_clock::now();
    if (state.diagnosticLastPaint == std::chrono::steady_clock::time_point{}) {
        resetRuntimeFrameDiagnosticsInterval(state);
        state.diagnosticLastPaint = completed;
        return;
    }

    RuntimeFrameSample sample = state.diagnosticPendingStages;
    state.diagnosticPendingMoveCommands.writeTo(sample);
    state.diagnosticPendingNestedStages.writeTo(sample);
    sample.frameGapMs = std::chrono::duration<double, std::milli>(
        completed - state.diagnosticLastPaint).count();
    sample.simulationMs = state.diagnosticPendingSimulationMs;
    sample.pathfindingMs = state.diagnosticPendingPathMs;
    sample.simTicks = state.diagnosticPendingSimulationTicks;
    // Even a reused map incurs paint/submission work, which remains part of the
    // measured whole-paint cost while the skipped nested drawing stages are 0.
    sample.renderMs = state.perf.lastPaintMs;
    sample.mapMs = reused ? 0.0 : state.perf.lastMapDrawMs;
    sample.overlayMs = reused ? 0.0 : state.perf.lastOverlayDrawMs;
    sample.sidebarMs = reused ? 0.0 : state.perf.lastSidebarDrawMs;
    sample.measuredStages |= RuntimeFrameRender | RuntimeFrameMap
        | RuntimeFrameOverlay | RuntimeFrameSidebar;
    if (sample.simTicks == 0) {
        // No callback ran in this interval: these stages are known zero, rather
        // than unmeasured. An interval with ticks retains its measured flags.
        sample.measuredStages |= RuntimeFrameSimulation | RuntimeFramePathfinding
            | RuntimeFrameSimVision | RuntimeFrameSimMove | RuntimeFrameSimAttack
            | RuntimeFrameSimEconomy | RuntimeFrameSimEffects | RuntimeFrameSimGuard;
    }
    if (reused || (state.renderStats.rendered
            && state.renderStats.coarseStageTimingEnabled)) {
        sample.fogMs = reused ? 0.0 : state.renderStats.visionMs;
        sample.terrainMs = reused ? 0.0 : state.renderStats.terrainMs;
        sample.worldObjectsMs = reused ? 0.0 : state.renderStats.worldObjectsMs;
        sample.lightingMs = reused ? 0.0 : state.renderStats.lightingMs;
        sample.measuredStages |= RuntimeFrameFog | RuntimeFrameTerrain
            | RuntimeFrameWorldObjects | RuntimeFrameLighting;
    }
    // Resource misses are evidence of cold work; available miss timers cover
    // only selected caches, so RuntimeFrameResource is intentionally unset.
    sample.cacheCounters = runtimeDiagnosticCacheCounters(state);
    if (state.renderCache.lastColdResourceSequence
        != state.diagnosticLastColdResourceSequence) {
        sample.setSuspectResource(state.renderCache.lastColdResourceType,
            state.renderCache.lastColdResourceName);
    }
    sample.frameNumber = ++state.diagnosticFrameNumber;
    sample.tick = static_cast<std::uint64_t>(std::max(0, state.tick));
    sample.totalObjects = state.world.getObjects().size();
    sample.visibleObjects = static_cast<std::uint64_t>(
        std::max(0, state.renderStats.visibleWorldObjects));
    sample.gameSpeed = state.config.gameSpeed;
    sample.width = width;
    sample.height = height;
    sample.hiddenWindow = state.liveBenchmark != nullptr;
    // No whole-world scan in ordinary frames. Only retained slow samples need
    // the live count, and even those retain no IDs, owner data or world state.
    if (sample.frameGapMs >= 50.0) {
        for (const auto& object : state.world.getObjects()) {
            if (object.alive) ++sample.aliveObjects;
        }
    }
    state.frameDiagnostics->observe(sample);
    state.diagnosticLastPaint = completed;
    state.diagnosticPendingSimulationMs = 0.0;
    state.diagnosticPendingPathMs = 0.0;
    state.diagnosticPendingStages = {};
    state.diagnosticPendingMoveCommands.reset();
    state.diagnosticPendingNestedStages.reset();
    state.diagnosticPendingSimulationTicks = 0;
    state.diagnosticLastColdResourceSequence = state.renderCache.lastColdResourceSequence;
}

void writeRuntimeFrameDiagnosticsReport(RuntimeWindowState& state) noexcept {
    if (state.frameDiagnostics == nullptr || !state.frameDiagnostics->enabled()) return;
    try {
        // Opening menus without a completed gameplay interval produces no log.
        if (state.frameDiagnostics->snapshot().sessionFrames == 0) return;
        const char* overrideDirectory = std::getenv("CONCORD_FRAME_DIAGNOSTICS_DIR");
        const auto directory = overrideDirectory != nullptr && *overrideDirectory != '\0'
            ? std::filesystem::u8path(overrideDirectory)
            : (state.config.localDataRootPath.empty()
                ? std::filesystem::path("local_data")
                : std::filesystem::path(state.config.localDataRootPath)) / "performance";
        SYSTEMTIME utc{};
        GetSystemTime(&utc);
        RuntimeFrameSessionMetadata metadata;
        RuntimeFrameSessionMetadata::setText(metadata.finalMapName, state.report.map.name);
        RuntimeFrameSessionMetadata::setText(metadata.finalMapFile, state.report.map.path);
        RuntimeFrameSessionMetadata::setText(metadata.finalMapSize, state.report.map.size);
        RuntimeFrameSessionMetadata::setText(metadata.finalMapLocalSize, state.report.map.localSize);
        metadata.finalTick = static_cast<std::uint64_t>(std::max(0, state.tick));
        metadata.terrainCells = state.world.getTerrainCells().size();
        std::ostringstream captured;
        captured << std::setfill('0') << std::setw(4) << utc.wYear << '-'
            << std::setw(2) << utc.wMonth << '-' << std::setw(2) << utc.wDay << 'T'
            << std::setw(2) << utc.wHour << ':' << std::setw(2) << utc.wMinute << ':'
            << std::setw(2) << utc.wSecond << '.' << std::setw(3) << utc.wMilliseconds << 'Z';
        RuntimeFrameSessionMetadata::setText(metadata.capturedUtc, captured.str());
        // Once at exit, not per frame. Sort/deduplicate a private ID copy so
        // metadata never cleans/reorders the actual presentation selection.
        auto selected = state.selectedObjectIds;
        std::sort(selected.begin(), selected.end());
        selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
        metadata.finalSelected = selected.size();
        for (int id : selected) {
            const auto* object = state.objectIndex.find(state.world.getObjects(), id);
            if (!object) { ++metadata.selectedUnresolved; continue; }
            const char* kind = object->kind == WorldObjectKind::Structure ? "Structure"
                : object->kind == WorldObjectKind::Infantry ? "Infantry" : "Unit";
            metadata.addSelection(object->type, kind, object->alive);
        }
        static std::atomic<std::uint64_t> sessionSequence{0};
        std::ostringstream name;
        name << "session-" << std::setfill('0') << std::setw(4) << utc.wYear
            << std::setw(2) << utc.wMonth << std::setw(2) << utc.wDay << 'T'
            << std::setw(2) << utc.wHour << std::setw(2) << utc.wMinute
            << std::setw(2) << utc.wSecond << '-' << std::setw(3) << utc.wMilliseconds
            << "Z-p" << GetCurrentProcessId() << '-'
            << sessionSequence.fetch_add(1, std::memory_order_relaxed) << ".txt";
        auto sessionPath = directory / name.str();
        // Preserve an existing report even if wall time/PID happen to repeat.
        for (std::uint64_t suffix = 1; std::filesystem::exists(sessionPath); ++suffix) {
            sessionPath = directory / (name.str() + "." + std::to_string(suffix));
        }
        std::string error;
        if (!state.frameDiagnostics->writeSessionReport(sessionPath, error, &metadata)) {
            std::cerr << "Frame diagnostics report failed: " << error << '\n';
            return;
        }
        if (!state.frameDiagnostics->writeSessionReport(directory / "latest.txt", error, &metadata)) {
            std::cerr << "Frame diagnostics latest report failed: " << error << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Frame diagnostics report failed: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Frame diagnostics report failed: unknown error\n";
    }
}
