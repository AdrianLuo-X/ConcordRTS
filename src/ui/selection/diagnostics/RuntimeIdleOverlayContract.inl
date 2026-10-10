// Include after drawRuntimeInteractionOverlay and the smoke-state helpers.
#include "ui/selection/diagnostics/RuntimeSelectedPathBatchContract.inl"
#include "ui/selection/diagnostics/RuntimeSelectedPathRasterContract.inl"
#include "ui/selection/diagnostics/RuntimeSelectionFrameRasterContract.inl"
#include "ui/sidebar/diagnostics/RuntimeHealthPipStripContract.inl"
#include "ui/selection/diagnostics/RuntimePaintTechTreeMemoContract.inl"
#include "ui/selection/diagnostics/RuntimeProjectionHistoryContract.inl"
#include "ui/selection/diagnostics/RuntimePaintProjectionSignatureContract.inl"

bool runRuntimeIdleOverlayContract(RuntimeWindowState& live, std::string& error) {
    if (!runRuntimePaintTechTreeMemoContract(live, error)) return false;
    if (!runRuntimeSelectedPathBatchPixelContract(error)) return false;
    if (!runRuntimeSelectedPathRasterPixelContract(error)) return false;
    if (!runRuntimeSelectionFrameRasterContract(error)) return false;
    if (!runRuntimeHealthPipStripPixelContract(error)) return false;
    const auto fail = [&](const std::string& reason) {
        error = "idle interaction overlay: " + reason;
        return false;
    };
    const std::uint64_t liveHash = live.world.deterministicSimulationStateHash();
    auto storage = std::make_unique<RuntimeWindowState>();
    RuntimeWindowState& probe = *storage;
    seedRuntimeLoadScratch(probe, live, false);
    RuntimeSnapshotActiveStateScope snapshotScope(probe);
    probe.runtimePreferencePersistenceEnabled = false;
    probe.runtimeSuppressPlayerAudioFeedback = true;
    probe.runtimeSoundsEnabled = false;
    probe.visionEnabled = false;
    probe.campaignMode = false;
    probe.triggers = {};
    probe.economy = {};
    probe.economy.owner = "Americans";
    probe.tick = 12;
    const auto* tankRules = probe.rules.findUnitType("UnitType", "MTNK");
    const auto* buildingRules = probe.rules.findBuildingType("GAPOWR");
    if (!probe.world.getSummary().loaded || probe.world.getTerrainCells().empty()
        || tankRules == nullptr || buildingRules == nullptr) {
        return fail("loaded terrain and stock MTNK/GAPOWR rules are required");
    }

    // Use the live map's actual central terrain, while keeping every mutation
    // confined to this private world. No navigation or simulation tick runs.
    const auto& summary = probe.world.getSummary();
    const long long centerX = (static_cast<long long>(summary.minCellX) + summary.maxCellX) / 2;
    const long long centerY = (static_cast<long long>(summary.minCellY) + summary.maxCellY) / 2;
    const auto centerCell = std::min_element(probe.world.getTerrainCells().begin(),
        probe.world.getTerrainCells().end(), [&](const TerrainCell& left, const TerrainCell& right) {
            const auto distance = [&](const TerrainCell& cell) {
                return std::abs(static_cast<long long>(cell.x) - centerX)
                    + std::abs(static_cast<long long>(cell.y) - centerY);
            };
            return distance(left) < distance(right);
        });
    WorldObjectInstance tank;
    tank.instanceId = 810001;
    tank.kind = WorldObjectKind::Unit;
    tank.type = "MTNK";
    tank.owner = probe.economy.owner;
    tank.x = centerCell->x;
    tank.y = centerCell->y;
    tank.cell = runtimeCellKey(tank.x, tank.y);
    tank.level = centerCell->level;
    tank.health = std::max(1, tankRules->strength);
    tank.mission = "Guard";
    tank.facing = tank.turretFacing = 128;
    WorldObjectInstance building;
    building.instanceId = 810002;
    building.kind = WorldObjectKind::Structure;
    building.type = "GAPOWR";
    building.owner = probe.economy.owner;
    building.x = tank.x + 6;
    building.y = tank.y;
    building.cell = runtimeCellKey(building.x, building.y);
    building.level = terrainLevelAtRuntimeCell(probe, building.x, building.y);
    building.health = std::max(1, buildingRules->strength / 2);
    building.mission = "Guard";
    probe.world.accessObjects() = {tank, building};

    constexpr int width = 960;
    constexpr int height = 640;
    const RECT clientRect{0, 0, width, height};
    const RECT viewport = runtimeMapViewport(clientRect);
    probe.camera = {};
    centerRuntimeCameraOnCell(probe, clientRect, {tank.x, tank.y});
    if (!runRuntimeProjectionHistoryContract(probe, clientRect, error)) return false;
    if (!runRuntimePaintProjectionSignatureContract(probe, clientRect, error)) return false;
    const auto& projected = runtimeProjectedObjectsForViewport(probe, viewport);
    const auto projectedTank = std::find_if(projected.begin(), projected.end(),
        [&](const RuntimeProjectedObject& object) {
            return object.object != nullptr && object.object->instanceId == tank.instanceId;
        });
    if (projectedTank == projected.end() || projected.size() != 2) {
        return fail("fixture objects did not produce body projections");
    }
    const POINT hoverPoint{(projectedTank->bounds.left + projectedTank->bounds.right) / 2,
        (projectedTank->bounds.top + projectedTank->bounds.bottom) / 2};
    const auto* hoverTarget = findProjectedObjectAtPoint(projected, hoverPoint);
    if (!pointInRectInclusive(viewport, hoverPoint) || hoverTarget == nullptr
        || hoverTarget->object->instanceId != tank.instanceId) {
        return fail("fixture unit cannot be hovered inside the viewport");
    }

    struct Buffers {
        RuntimeBackBuffer lazy;
        RuntimeBackBuffer complete;
        HDC screen = GetDC(nullptr);
        ~Buffers() {
            releaseRuntimeBackBuffer(lazy);
            releaseRuntimeBackBuffer(complete);
            if (screen != nullptr) ReleaseDC(nullptr, screen);
        }
    } buffers;
    if (buffers.screen == nullptr
        || !ensureRuntimeBackBuffer(buffers.lazy, buffers.screen, width, height)
        || !ensureRuntimeBackBuffer(buffers.complete, buffers.screen, width, height)) {
        return fail("cannot allocate the two comparison surfaces");
    }

    // An explicitly enabled diagnostic is itself an overlay demand. Exercise
    // the ordinary path independently, then restore the caller's environment.
    struct TurretDebugSetting {
        std::optional<std::string> previous;
        TurretDebugSetting() {
            if (const char* value = std::getenv("CONCORD_DEBUG_TURRET_FACING")) previous = value;
            _putenv_s("CONCORD_DEBUG_TURRET_FACING", "");
        }
        ~TurretDebugSetting() {
            _putenv_s("CONCORD_DEBUG_TURRET_FACING", previous ? previous->c_str() : "");
        }
    } turretDebugSetting;

    RuntimeMapRenderContext context;
    context.theater = probe.theaterLoaded ? &probe.theater : nullptr;
    context.art = probe.artLoaded ? &probe.art : nullptr;
    context.rules = &probe.rules;
    context.mapFacts = probe.mapFactsLoaded ? &probe.mapFacts : nullptr;
    context.assetRootPath = runtimeAssetRootPath(probe.config);
    context.tileRootPath = runtimeTileRootPath(probe.config, probe.report.map);
    context.palettePath = runtimePalettePath(probe.config, probe.report.map);
    context.structureRootPath = runtimeStructureRootPath(probe.config, probe.report.map);
    context.structurePalettePath = runtimeStructurePalettePath(probe.config, probe.report.map);
    context.animationTick = probe.tick;
    context.fixedTickHz = probe.config.fixedTickHz;
    context.stableRenderConfiguration = true;
    context.ownerRemapColors = &probe.ownerRemapColors;
    context.neutralRemapOwners = &probe.neutralRemapOwners;
    context.selectedObjectIds = &probe.selectedObjectIds;

    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    std::vector<std::uint32_t> backdrop(pixelCount);
    for (std::size_t index = 0; index < pixelCount; ++index)
        backdrop[index] = 0x00202d38U ^ static_cast<std::uint32_t>(index % 127);
    const auto counters = [&]() {
        return std::array<int, 6>{probe.lastUnitSelectionFrames,
            probe.lastStructureSelectionFrames, probe.lastSelectedHealthBars,
            probe.lastHoveredUnitHealthBars, probe.lastHoveredStructureHealthBars,
            probe.lastVeterancyResourceDraws + probe.lastVeterancyFallbackDraws};
    };
    bool coldCamera = false;
    const auto sameRgb = [](std::uint32_t left, std::uint32_t right) {
        return ((left ^ right) & 0x00ffffffU) == 0;
    };
    const auto draw = [&](RuntimeBackBuffer& target, bool forceProjection) {
        RuntimeSelectedPathRasterReferenceScope pathRasterPath(forceProjection);
        RuntimeSelectionFrameReferenceScope selectionFramePath(forceProjection);
        RuntimeHealthPipStripReferenceScope healthPipPath(forceProjection);
        if (coldCamera) probe.camera = {};
        probe.projectedObjectsCache.clear();
        invalidateRuntimeObjectProjectionCache(probe);
        GdiFlush();
        std::copy(backdrop.begin(), backdrop.end(), static_cast<std::uint32_t*>(target.pixels));
        // UI sprite paths use state.backBuffer to find the same CPU surface.
        std::swap(probe.backBuffer, target);
        context.surfacePixels = static_cast<std::uint32_t*>(probe.backBuffer.pixels);
        context.surfaceWidth = width;
        context.surfaceHeight = height;
        SetBkMode(probe.backBuffer.memoryDc, TRANSPARENT);
        drawRuntimeInteractionOverlay(probe.backBuffer.memoryDc, clientRect, probe, context,
            forceProjection, !forceProjection, !forceProjection);
        GdiFlush();
        std::swap(probe.backBuffer, target);
    };

    constexpr std::array<const char*, 10> names{
        "idle", "drag-box", "mouse-outside", "selected-unit", "hover-unit",
        "repair-building", "veteran", "elite", "selected-building", "cold-camera"};
    for (std::size_t scenario = 0; scenario < names.size(); ++scenario) {
        coldCamera = scenario == 9;
        probe.selectedObjectIds.clear();
        probe.repairingStructureTicks.clear();
        probe.mouseClientPointValid = false;
        probe.selectionDragActive = probe.selectionDragMoved = false;
        probe.world.accessObjects()[0].veterancy = 0;
        if (scenario == 1) {
            probe.selectionDragActive = probe.selectionDragMoved = true;
            probe.selectionStart = {viewport.left + 30, viewport.top + 30};
            probe.selectionCurrent = {viewport.left + 180, viewport.top + 110};
        } else if (scenario == 2) {
            probe.mouseClientPointValid = true;
            probe.mouseClientPoint = {-20, -20};
        } else if (scenario == 3) {
            probe.selectedObjectIds.push_back(tank.instanceId);
        } else if (scenario == 4) {
            probe.mouseClientPointValid = true;
            probe.mouseClientPoint = hoverPoint;
        } else if (scenario == 5) {
            probe.repairingStructureTicks.emplace(building.instanceId, probe.tick);
        } else if (scenario == 6 || scenario == 7) {
            probe.world.accessObjects()[0].veterancy = static_cast<int>(scenario - 5);
        } else if (scenario == 8) {
            probe.selectedObjectIds.push_back(building.instanceId);
        }
        const bool needsProjection = scenario >= 3 && scenario < 9;
        const std::uint64_t before = probe.world.deterministicSimulationStateHash();
        draw(buffers.lazy, false);
        if (probe.projectedObjectsCacheValid != needsProjection
            || probe.projectedObjectsCache.empty() == needsProjection) {
            return fail(std::string(names[scenario]) + " materialized the wrong projection path");
        }
        const auto lazyCounters = counters();
        if (probe.world.deterministicSimulationStateHash() != before)
            return fail(std::string(names[scenario]) + " lazy drawing changed world authority");
        draw(buffers.complete, true);
        if (!probe.projectedObjectsCacheValid || probe.projectedObjectsCache.size() != 2
            || counters() != lazyCounters)
            return fail(std::string(names[scenario]) + " full projection changed overlay counters");
        if (probe.world.deterministicSimulationStateHash() != before)
            return fail(std::string(names[scenario]) + " full drawing changed world authority");
        const auto* lazyPixels = static_cast<const std::uint32_t*>(buffers.lazy.pixels);
        const auto* completePixels = static_cast<const std::uint32_t*>(buffers.complete.pixels);
        if (!std::equal(lazyPixels, lazyPixels + pixelCount, completePixels, sameRgb))
            return fail(std::string(names[scenario]) + " differs from the full projection pixels");
        const bool changedPixels = !std::equal(backdrop.begin(), backdrop.end(), lazyPixels, sameRgb);
        if (changedPixels != (scenario != 0 && scenario != 2 && scenario != 9)
            || (scenario == 3 && (lazyCounters[0] != 1 || lazyCounters[2] != 1))
            || (scenario == 4 && lazyCounters[3] != 1)
            || ((scenario == 6 || scenario == 7) && lazyCounters[5] != 1)
            || (scenario == 8 && (lazyCounters[1] != 1 || lazyCounters[2] != 1))) {
            return fail(std::string(names[scenario]) + " did not draw its expected visible overlay");
        }
    }
    // Commands retain their selection: a moving army paints many health pips
    // even when nothing is hovered. Compare the full uncached resource lookup
    // path with this paint's memo across health, rank and observer changes.
    coldCamera = false;
    probe.world.accessObjects().clear();
    probe.selectedObjectIds.clear();
    probe.repairingStructureTicks.clear();
    probe.mouseClientPointValid = false;
    probe.runtimeTargetLinesEnabled = true;
    probe.behaviors.clear();
    for (int index = 0; index < 128; ++index) {
        auto object = tank;
        object.instanceId += index;
        object.kind = index % 2 ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        object.type = index % 2 ? "E1" : "MTNK";
        object.x += index % 12 - 6;
        object.y += index / 12 - 5;
        object.cell = runtimeCellKey(object.x, object.y);
        object.health = index % 4 == 0 ? 1 : index % 4 == 1 ? 10 : index % 4 == 2 ? 50 : tank.health;
        object.veterancy = index % 3;
        probe.world.accessObjects().push_back(object);
        probe.selectedObjectIds.push_back(object.instanceId);
        auto& behavior = probe.behaviors[object.instanceId];
        behavior.kind = index % 3 ? RuntimeBehaviorKind::Move : RuntimeBehaviorKind::Attack;
        behavior.targetObjectId = tank.instanceId;
        behavior.moveTarget = {tank.x + 3 - index % 7, tank.y + 3 - index % 5};
        // Shared centers and reversed zigzags force dots, target diamonds and
        // different units to overlap. Include empty paths and the 96-step cap.
        const int pathLength = index % 9 == 0 ? 0 : index % 13 == 0 ? 103 : 17;
        for (int step = 0; step < pathLength; ++step)
            behavior.path.push_back({tank.x + (step * (index % 2 ? -1 : 1)) % 9 - 4,
                tank.y + ((step + index) % 7) - 3});
    }
    for (const std::string owner : {"Americans", "Russians", "Americans"}) {
        probe.economy.owner = owner;
        for (auto& object : probe.world.accessObjects()) object.owner = owner;
        const auto before = probe.world.deterministicSimulationStateHash();
        draw(buffers.lazy, false);
        const auto memoCounters = counters();
        const int memoPathOrders = probe.lastTargetLineOrdersDrawn;
        const std::array<int, 4> memoPips{probe.lastHealthPipResourceDraws,
            probe.lastHealthPipFallbackDraws, probe.lastVeterancyResourceDraws,
            probe.lastVeterancyFallbackDraws};
        draw(buffers.complete, true);
        const std::array<int, 4> referencePips{probe.lastHealthPipResourceDraws,
            probe.lastHealthPipFallbackDraws, probe.lastVeterancyResourceDraws,
            probe.lastVeterancyFallbackDraws};
        const auto* memoPixels = static_cast<const std::uint32_t*>(buffers.lazy.pixels);
        const auto* referencePixels = static_cast<const std::uint32_t*>(buffers.complete.pixels);
        if (counters() != memoCounters || memoPips != referencePips
            || memoPathOrders != 128 || probe.lastTargetLineOrdersDrawn != memoPathOrders
            || !std::equal(memoPixels, memoPixels + pixelCount, referencePixels, sameRgb)
            || probe.world.deterministicSimulationStateHash() != before)
            return fail("128 selected mixed-unit UI resource batch differs for " + owner);
    }
    if (live.world.deterministicSimulationStateHash() != liveHash)
        return fail("scratch rendering modified the live world");
    std::cout << "Idle overlay contract: 10 RGB-identical cases; idle/drag/outside/cold-camera skip projection; "
        "selection/hover/repair/veterancy keep full overlays; 3 mixed selected-128 resource batches "
        "match uncached pixels, pip counts and overlapping selected paths across owner changes; world hashes unchanged.\n";
    return true;
}
