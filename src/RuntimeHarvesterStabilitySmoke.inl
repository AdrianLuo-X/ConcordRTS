#include "RuntimeOreSearchContract.inl"
#include "RuntimeHarvesterManualMoveSmoke.inl"

bool runRuntimeHarvesterStabilitySmokeProbe(RuntimeWindowState& live, std::string& error) {
    if (!runRuntimeHarvesterManualMoveSmokeProbe(live, error)) return false;
    const auto fail = [&](const std::string& message) {
        error = "harvester stability: " + message;
        return false;
    };
    for (const char* type : {"CMIN", "HARV"}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.config.fixedTickHz = 30;
        probe.rules = live.rules;
        probe.rulesLoaded = true;
        probe.art = live.art;
        probe.artLoaded = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.economy.owner = "Americans";
        probe.economy.harvesters = 1;
        const RuntimeCellPoint origin{5, 5}, selectedOre{5, 6}, otherOre{6, 5};
        for (int y = 1; y <= 12; ++y) for (int x = 1; x <= 12; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = 0;
            cell.subTileIndex = 0;
            if (RuntimeCellPoint{x, y} == selectedOre || RuntimeCellPoint{x, y} == otherOre)
                setRuntimeResourceTerrainBails(cell, "TIB", 12);
            probe.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance miner;
        miner.instanceId = 1;
        miner.type = type;
        miner.kind = WorldObjectKind::Unit;
        miner.owner = probe.economy.owner;
        miner.alive = true;
        miner.health = 1000;
        miner.x = origin.x;
        miner.y = origin.y;
        miner.cell = runtimeCellKey(origin.x, origin.y);
        miner.facing = runtimeFacingFromDelta(0, 1);
        miner.mission = "Guard";
        probe.world.accessObjects().reserve(3);
        probe.world.accessObjects().push_back(miner);
        initializeRuntimeNavigation(probe);
        auto& object = probe.world.accessObjects().front();
        auto& task = probe.harvesterTasks[miner.instanceId];
        task.phase = RuntimeHarvesterPhase::SeekingOre;
        task.targetOre = selectedOre;
        const int facing = object.facing;
        for (int tick = 1; tick <= 300 && task.cargoUnits < 3; ++tick) {
            probe.tick = tick;
            advanceRuntimeHarvesterEconomy(probe);
            if (object.facing != facing || runtimeCellPointForObject(object) != origin
                || task.targetOre != selectedOre || !probe.behaviors.empty()) {
                return fail(std::string(type) + " abandoned a nonempty ore cell while loading: tick="
                    + std::to_string(tick) + " cargo=" + std::to_string(task.cargoUnits)
                    + " facing=" + std::to_string(object.facing) + " target="
                    + std::to_string(task.targetOre.x) + "," + std::to_string(task.targetOre.y));
            }
        }
        if (task.cargoUnits != 3 || runtimeResourceDensityAtCell(probe, otherOre) != 11
            || runtimeResourceDensityAtCell(probe, selectedOre) != 8)
            return fail(std::string(type) + " did not steadily consume the selected cell: cargo="
                + std::to_string(task.cargoUnits) + " status=" + task.status);
        const auto restoredTask = parseRuntimeHarvesterTask(serializeRuntimeHarvesterTask(task));
        if (!restoredTask || restoredTask->targetOre != selectedOre
            || restoredTask->cargoUnits != task.cargoUnits || restoredTask->phase != task.phase
            || restoredTask->nextThinkTick != task.nextThinkTick)
            return fail("save/load lost the current ore target");

        // A resumed pickup must turn the hull, rather than drawing/collecting
        // ore from its flank. Old intake dust must disappear during that turn.
        object.facing = (facing + 128) % 256;
        task.phase = RuntimeHarvesterPhase::Loading;
        task.workTicksRemaining = 1;
        ++probe.tick;
        advanceRuntimeHarvesterEconomy(probe);
        advanceRuntimeEffects(probe);
        if (task.cargoUnits != 3 || task.phase != RuntimeHarvesterPhase::SeekingOre
            || std::any_of(probe.impactEffects.begin(), probe.impactEffects.end(),
                [](const auto& effect) { return effect.renderIntent == "oreGather"; }))
            return fail("sideways pickup continued or left stale intake dust");
        for (int turn = 0; turn < 256 && task.phase != RuntimeHarvesterPhase::Loading; ++turn) {
            ++probe.tick;
            advanceRuntimeHarvesterEconomy(probe);
            if (task.cargoUnits != 3) return fail("collected ore while realigning the hull");
        }
        if (object.facing != facing || task.phase != RuntimeHarvesterPhase::Loading
            || probe.impactEffects.empty() || probe.impactEffects.back().facing != facing)
            return fail("aligned hull did not restart the front intake effect");

        // A second miner can deplete the target during a pickup. Do not charge
        // that completed pickup to a different cell behind the intake.
        auto* selectedTerrain = findMutableRuntimeTerrainCellAt(probe, selectedOre);
        setRuntimeResourceTerrainBails(*selectedTerrain, "TIB", 0);
        invalidateRuntimeResourceCaches(probe);
        task.phase = RuntimeHarvesterPhase::Loading;
        task.workTicksRemaining = 0;
        ++probe.tick;
        advanceRuntimeHarvesterEconomy(probe);
        if (task.cargoUnits != 3 || task.phase != RuntimeHarvesterPhase::SeekingOre
            || runtimeResourceDensityAtCell(probe, otherOre) != 11 || object.facing != facing)
            return fail("depleted target silently gathered a different ore cell");
        for (int tick = 0; tick < 300 && task.cargoUnits < 4; ++tick) {
            ++probe.tick;
            advanceRuntimeHarvesterEconomy(probe);
        }
        if (task.cargoUnits != 4 || task.targetOre != otherOre
            || object.facing != runtimeFacingFromDelta(1, 0))
            return fail("depleted target did not realign and resume harvesting");

        // Traffic over the resource does not invalidate a stationary intake.
        WorldObjectInstance traffic = miner;
        traffic.instanceId = 2;
        traffic.x = otherOre.x;
        traffic.y = otherOre.y;
        traffic.cell = runtimeCellKey(traffic.x, traffic.y);
        probe.world.accessObjects().push_back(traffic);
        if (findHarvestableRuntimeResourceNear(probe, object, otherOre) != otherOre)
            return fail("mobile traffic changed the intake target");
        probe.world.accessObjects().back().alive = false;

        setRuntimeResourceTerrainBails(*selectedTerrain, "TIB", 12);
        selectedTerrain->level = 3;
        invalidateRuntimeResourceCaches(probe);
        if (findHarvestableRuntimeResourceNear(probe, object, selectedOre) != otherOre)
            return fail("adjacent cliff ore was gathered across a height discontinuity");
        selectedTerrain->level = 1;
        auto& slopeInfo = probe.terrainInfoByCell[runtimeCellKey(selectedOre.x, selectedOre.y)];
        slopeInfo.ramp = true;
        invalidateRuntimeResourceCaches(probe);
        if (findHarvestableRuntimeResourceNear(probe, object, selectedOre) != selectedOre)
            return fail("legal ramp ore was rejected");
        slopeInfo.ramp = false;
        selectedTerrain->level = 0;

        const RuntimeCellPoint cornerOre{6, 6};
        setRuntimeResourceTerrainBails(*findMutableRuntimeTerrainCellAt(probe, cornerOre), "TIB", 12);
        auto& eastInfo = probe.terrainInfoByCell[runtimeCellKey(otherOre.x, otherOre.y)];
        slopeInfo.passable = false;
        eastInfo.passable = false;
        invalidateRuntimeResourceCaches(probe);
        if (findHarvestableRuntimeResourceNear(probe, object, cornerOre).has_value())
            return fail("intake reached through a blocked diagonal corner");
        slopeInfo.passable = true;
        eastInfo.passable = true;
        auto& originInfo = probe.terrainInfoByCell[runtimeCellKey(origin.x, origin.y)];
        originInfo.bridge = true;
        if (findHarvestableRuntimeResourceNear(probe, object, otherOre).has_value())
            return fail("bridge deck harvested the resource underneath");
        originInfo.bridge = false;

        // Enclose an ore tile. The general movement fallback can find distant
        // ground, but that ground cannot serve as a harvesting destination.
        const RuntimeCellPoint enclosedOre{9, 9};
        setRuntimeResourceTerrainBails(*findMutableRuntimeTerrainCellAt(probe, enclosedOre), "TIB", 12);
        for (const auto& cell : runtimeNeighborsOf(enclosedOre))
            probe.terrainInfoByCell[runtimeCellKey(cell.x, cell.y)].passable = false;
        invalidateRuntimeResourceCaches(probe);
        if (findRuntimeHarvesterResourceApproach(probe, object, enclosedOre).has_value()
            || issueRuntimeHarvesterMoveToCell(probe, object, enclosedOre, "Harvest")
            || !probe.behaviors.empty())
            return fail("unreachable ore was replaced with an unusable distant approach");
        if (!checkRuntimeOreSearchContract(probe, object, error)) return false;
        std::cout << "Harvester stability: " << type
            << " sameOre=ok steadyFacing=ok loads=4 depletion=ok traffic=ok save=ok"
            << " cliff=ok ramp=ok corner=ok bridge=ok unreachable=ok\n";
    }
    return true;
}
