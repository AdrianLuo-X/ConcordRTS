bool runRuntimeDiskGroundSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& message) { error = "Disk ground: " + message; return false; };
    int cases = 0, saves = 0, continuedTicks = 0;
    for (int hz : {15, 30, 60}) for (bool elite : {false, true})
    for (int distance : {4, 16}) for (const std::string mode : {"empty", "enemy", "friendly"}) {
        const std::string label = std::to_string(hz) + (elite ? "-elite-" : "-normal-")
            + std::to_string(distance) + "-" + mode;
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, live, false);
        s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.shortGame = false;
        s.config.playerSlots.clear(); s.config.playerCountry = "YuriCountry";
        s.economy.owner = "YuriCountry"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.visionEnabled = false;
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear(); s.terrainInfoByCell.clear(); s.terrainByCell.clear();
        s.mapFacts = {}; s.mapFactsLoaded = true;
        for (int y = 1; y <= 32; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = 0; s.world.accessTerrainCells().push_back(cell);
            MapCellFacts facts; facts.x = x; facts.y = y; facts.cellIndex = cell.cellIndex512;
            facts.hasTerrain = true; facts.terrainClass = "land"; facts.passabilityHint = "passable";
            s.mapFacts.cellIndexLookup.emplace(facts.cellIndex, s.mapFacts.cells.size());
            s.mapFacts.cells.push_back(facts);
        }
        const RuntimeCellPoint goal{6 + distance, 8};
        auto* disk = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "DISK", {6,8});
        if (!disk) return fail(label + " source missing");
        const int id = disk->instanceId; disk->veterancy = elite ? 2 : 0;
        initializeRuntimeNavigation(s); RuntimeSnapshotActiveStateScope active(s);
        s.selectedObjectIds = {id};
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell, goal)
            || s.lastCommand.acceptedCount != 1 || !s.behaviors.at(id).attackGround)
            return fail(label + " ground order rejected");
        const auto step = [](RuntimeWindowState& state) {
            RuntimeSnapshotActiveStateScope scope(state); ++state.tick; advanceRuntimeSimulation(state);
        };
        const auto charge = [&](const RuntimeWindowState& state) -> const RuntimeProjectileEffect* {
            for (const auto& shot : state.projectiles)
                if (shot.attackerObjectId == id && shot.renderIntent == "diskLaserCharge") return &shot;
            return nullptr;
        };
        for (int i = 0; i < hz * 20 && !charge(s); ++i) step(s);
        if (!charge(s)) return fail(label + " did not approach and start charge: " + s.simulationStatus);
        const int started = s.tick;
        const int duration = charge(s)->totalTicks;
        if (charge(s)->targetObjectId >= 0 || charge(s)->toCell != goal
            || charge(s)->weaponId != (elite ? "DiskLaserE" : "DiskLaser"))
            return fail(label + " charge did not retain ground point/weapon");
        step(s);
        if (!charge(s)) return fail(label + " ground charge was cancelled after its first frame");
        int victimId = -1;
        if (mode != "empty") {
            auto* victim = addRuntimeObjectInstanceForOwner(s, mode == "friendly" ? "YuriCountry" : "Americans",
                RuntimeBuildKind::Unit, "MTNK", goal);
            if (!victim) return fail(label + " entering victim missing");
            victimId = victim->instanceId; victim->health = 2000; victim->mission = "Sleep";
            victim->weaponCooldownTicksRemaining = 100000;
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
        }
        std::ostringstream saved; IniFile ini;
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
        if (!writeRuntimeStateToStream(s, saved, error) || !ini.loadFromString(saved.str())
            || !materializeRuntimeLoadStateFromIni(*restored, ini, error))
            return fail(label + " full save failed: " + error);
        ++saves;
        const auto hp = [&](const RuntimeWindowState& state) {
            const auto* victim = findObjectById(state, victimId); return victim ? victim->health : -1;
        };
        const auto actors = [](const RuntimeWindowState& state) {
            std::string result;
            for (const auto& shot : state.projectiles) result += serializeRuntimeProjectileEffect(shot) + "\n";
            return result;
        };
        std::set<int> beams;
        bool capturedCharge = false, capturedBeam = false;
        for (int i = 0; i < hz * 10; ++i) {
            step(s); step(*restored); ++continuedTicks;
            if (hp(s) != hp(*restored) || actors(s) != actors(*restored))
                return fail(label + " saved charge/beam or damage diverged");
            if (s.tick < started + duration && hp(s) != (victimId < 0 ? -1 : 2000))
                return fail(label + " damage before charge completed");
            bool beam = false;
            for (const auto& shot : s.projectiles) if (shot.renderIntent == "diskLaserBeam") {
                beams.insert(shot.actorId); beam = true;
                if (shot.toCell != goal || std::abs(shot.targetX - goal.x) > .001
                    || std::abs(shot.targetY - goal.y) > .001 || std::abs(shot.targetZ) > .001)
                    return fail(label + " beam missed its fixed ground endpoint");
            }
            if (hz == 30 && !elite && distance == 4 && mode == "empty") {
                const bool captureCharge = !capturedCharge && charge(s) && charge(s)->elapsedTicks >= hz / 2;
                const bool captureBeam = !capturedBeam && beam;
                if (const char* output = std::getenv("CONCORD_DISK_GROUND_OUTPUT"); output && (captureCharge || captureBeam)) {
                    std::filesystem::create_directories(output);
                    const auto stats = drawRuntimeStateOffscreenAtCell(s, {8,8}, 640, 480, error,
                        std::string(output) + (captureBeam ? "/ground-beam.bmp" : "/ground-charge.bmp"), false);
                    if (!stats || stats->projectileActorTravelDrawn < 1)
                        return fail(label + " native ground effect missing: " + error);
                    capturedCharge |= captureCharge; capturedBeam |= captureBeam;
                }
            }
        }
        if (beams.size() < 2 || (victimId >= 0 && hp(s) >= 2000)
            || !s.behaviors.contains(id) || !s.behaviors.at(id).attackGround)
            return fail(label + " repeated ground fire/damage failed");
        // Stop must cease ground fire; remove enemy acquisition from this check.
        if (auto* victim = findMutableObjectById(s, victimId)) victim->owner = "YuriCountry";
        s.selectedObjectIds = {id}; issueRuntimeStopCommand(s);
        for (int i = 0; i < hz * 3; ++i) step(s);
        if (charge(s) || std::any_of(s.projectiles.begin(), s.projectiles.end(),
                [](const auto& shot) { return shot.renderIntent == "diskLaserBeam"; }))
            return fail(label + " stopped attack continued firing");
        ++cases; std::cout << "Disk ground: " << label << " approach/charge/beam/damage/save/stop passed\n";
    }
    std::cout << "Disk ground: " << cases << " cases, " << saves << " full saves, "
        << continuedTicks << " continuation ticks passed\n";
    return true;
}
