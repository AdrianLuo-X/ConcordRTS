bool runRuntimeSpawnedMissileCompatibilitySmokeProbe(RuntimeWindowState& live, std::string& error) {
    for (const std::string type : {"V3", "BSUB"}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config; probe.report = live.report;
        probe.rules = live.rules; probe.rulesLoaded = true;
        probe.art = live.art; probe.artLoaded = true;
        probe.theater = live.theater; probe.theaterLoaded = live.theaterLoaded;
        probe.world = live.world; probe.world.accessTerrainCells().clear();
        probe.world.accessTerrainObjects().clear(); probe.world.accessSmudges().clear();
        probe.economy.owner = "Americans"; probe.runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 1; y <= 28; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell;
            cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = 0; cell.subTileIndex = 0;
            probe.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance parent;
        parent.instanceId = 1; parent.type = type; parent.kind = WorldObjectKind::Unit;
        parent.owner = "Russians"; parent.alive = true; parent.health = 200;
        parent.x = 8; parent.y = 10; parent.cell = runtimeCellKey(8,10);
        parent.facing = parent.turretFacing = runtimeFacingFromDelta(1,0);
        parent.mission = "Attack";
        WorldObjectInstance target = parent;
        target.instanceId = 2; target.type = "CAEAST01"; target.kind = WorldObjectKind::Structure;
        target.owner = "Americans"; target.health = 3000; target.x = 20;
        target.cell = runtimeCellKey(20,10); target.mission = "Guard";
        probe.world.accessObjects() = {parent, target};
        initializeRuntimeNavigation(probe);
        const std::string root = std::getenv("CONCORD_MISSILE_OUTPUT") ? std::getenv("CONCORD_MISSILE_OUTPUT") : "";
        const auto capture = [&](const char* phase) {
            return root.empty() || drawRuntimeStateOffscreenAtCell(probe, {12,10}, 680,420,error,
                root + "/" + type + "-" + phase + ".bmp", false).has_value();
        };
        RuntimeObjectBehavior attack;
        attack.kind = RuntimeBehaviorKind::Attack; attack.state = RuntimeBehaviorState::Queued;
        attack.targetObjectId = 2; probe.behaviors[1] = attack;
        advanceRuntimeSpecialMechanisms(probe);
        refreshRuntimeSpecialMechanismRenderSnapshots(probe);
        const auto dockedCount = std::count_if(probe.specialMechanismRenderSnapshots.begin(),
            probe.specialMechanismRenderSnapshots.end(), [](const auto& snapshot) {
                return snapshot.kind == "spawnedMissile";
            });
        if ((type == "BSUB" && dockedCount != 0) || (type == "V3" && dockedCount != 1)) {
            error = type + " docked missile visibility is incorrect";
            return false;
        }
        if (!capture("docked")) return false;
        for (int i = 0; i < 80 && probe.specialMechanisms.spawnedMissiles().empty(); ++i) {
            auto* object = findMutableObjectById(probe, 1);
            (void)advanceRuntimeAttackBehavior(probe, *object, probe.behaviors.at(1));
            ++probe.tick;
            advanceRuntimeEffects(probe);
        }
        if (probe.specialMechanisms.spawnedMissiles().empty()) {
            error = type + " did not start its real missile volley: " + probe.simulationStatus;
            return false;
        }
        const std::size_t count = probe.specialMechanisms.spawnedMissiles().size();
        refreshRuntimeSpecialMechanismRenderSnapshots(probe);
        if (type == "BSUB" && std::any_of(probe.specialMechanismRenderSnapshots.begin(),
                probe.specialMechanismRenderSnapshots.end(), [](const auto& snapshot) {
                    return snapshot.kind == "spawnedMissile";
                })) {
            error = "BSUB exposed a missile before release";
            return false;
        }
        if (!capture("preparing")) return false;
        if (type == "BSUB") {
            const auto first = std::find_if(probe.specialMechanisms.spawnedMissiles().begin(),
                probe.specialMechanisms.spawnedMissiles().end(), [](const auto& entry) { return entry.second.slot == 0; });
            const auto second = std::find_if(probe.specialMechanisms.spawnedMissiles().begin(),
                probe.specialMechanisms.spawnedMissiles().end(), [](const auto& entry) { return entry.second.slot == 1; });
            if (count != 2 || first == probe.specialMechanisms.spawnedMissiles().end()
                || second == probe.specialMechanisms.spawnedMissiles().end()
                || std::abs(first->second.x - parent.x) > 0.00001
                || std::abs(first->second.y - parent.y) > 0.00001
                || std::abs(std::hypot(second->second.x - first->second.x,
                    second->second.y - first->second.y) - 70.0/256.0) > 0.00001) {
                error = "BSUB did not use its missile bay and SecondSpawnOffset";
                return false;
            }
        }
        bool rendered = false;
        for (int i = 0; i < probe.config.fixedTickHz * 85 && !probe.specialMechanisms.spawnedMissiles().empty(); ++i) {
            ++probe.tick;
            advanceRuntimeEffects(probe);
            const auto& missiles = probe.specialMechanisms.spawnedMissiles();
            if (type == "BSUB") {
                refreshRuntimeSpecialMechanismRenderSnapshots(probe);
                for (const auto& snapshot : probe.specialMechanismRenderSnapshots) {
                    if (snapshot.kind == "spawnedMissile"
                        && (snapshot.phase != "Flight" || !missiles.at(snapshot.objectId).released)) {
                        error = "BSUB exposed its second missile while awaiting release";
                        return false;
                    }
                }
            }
            const bool flying = !missiles.empty() && std::all_of(missiles.begin(), missiles.end(), [](const auto& entry) {
                return entry.second.released;
            });
            if (!rendered && flying) {
                refreshRuntimeSpecialMechanismRenderSnapshots(probe);
                const std::string expectedImage = type == "BSUB" ? "BSUBMISL" : "V3ROCKET";
                std::size_t matching = 0;
                for (const auto& snapshot : probe.specialMechanismRenderSnapshots)
                    if (snapshot.kind == "spawnedMissile" && snapshot.resourceId == expectedImage) ++matching;
                const auto stats = drawRuntimeStateOffscreenAtCell(probe, {12,10}, 680,420,error,
                    root.empty() ? "" : root + "/" + type + "-flight.bmp", false);
                if (matching != count || !stats || stats->specialMechanismRenderActors < static_cast<int>(count)) {
                    error = type + " lost its missile image/alias: " + error;
                    return false;
                }
                rendered = true;
            }
        }
        if (!rendered || !probe.specialMechanisms.spawnedMissiles().empty()
            || findObjectById(probe, 2)->health >= 3000) {
            error = type + " missile flight failed to reach and damage its target";
            return false;
        }
        std::cout << "Spawned missile compatibility: " << type << " count=" << count
            << " docked/preparing-visibility=ok image=ok flight=ok impact=ok\n";
    }
    return true;
}
