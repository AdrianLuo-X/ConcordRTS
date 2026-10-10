#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeIfvDeathSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "IFV death: " + why; return false; };
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) return fail("native resources");
    const std::array<std::string, 20> modes{{"", "DOG", "ENGINEER", "E1", "FLAKT", "GHOST",
        "SNIPE", "SHK", "IVAN", "YURI", "DESO", "CLEG", "TERROR", "PRES", "INIT",
        "VIRUS", "YURIPR", "GGI", "unloaded-IVAN", "empty-stale"}};
    int cases = 0, saves = 0, nuclear = 0, ordinary = 0;
    for (int hz : {15, 30, 60}) for (int rank : {0, 2}) for (const auto& mode : modes) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, *assets, false); s.config = config; s.config.fixedTickHz = hz;
        s.config.playerSlots.clear(); s.config.playerCountry = "Americans";
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.superweapons = false;
        s.config.crates = s.config.shortGame = false;
        s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = "Americans";
        s.economy.skirmishStartApplied = true; s.triggers.loaded = s.triggers.bootstrapComplete = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts = {}; s.mapFactsLoaded = false; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        initializeRuntimeNavigation(s); ensureRuntimeSpecialMechanismsInitialized(s);
        auto* vehicle = addRuntimeObjectInstanceForOwner(s, "Americans", RuntimeBuildKind::Unit, "FV", {12, 12});
        if (!vehicle) return fail("native FV"); const int id = vehicle->instanceId;
        vehicle->veterancy = rank; vehicle->mission = "Sleep"; vehicle->weaponCooldownTicksRemaining = 100000;
        int passengerId = -1;
        if (!mode.empty() && mode != "empty-stale") {
            const std::string type = mode == "unloaded-IVAN" ? "IVAN" : mode;
            auto* passenger = addRuntimeObjectInstanceForOwner(s, "Americans", RuntimeBuildKind::Infantry, type, {13, 12});
            if (!passenger) return fail("native cargo " + type); passengerId = passenger->instanceId;
            s.selectedObjectIds = {passengerId}; vehicle = findMutableObjectById(s, id);
            if (!queueRuntimeTransportTarget(s, *vehicle) || !waitForRuntimeTransportSmokeMotion(s, id, passengerId))
                return fail("actual boarding " + type);
            eraseRuntimeBehavior(s, passengerId);
            vehicle = findMutableObjectById(s, id);
            if (mode == "unloaded-IVAN" && (unloadRuntimeTransport(s, *vehicle) != 1
                || !vehicle->transportPassengerIds.empty() || vehicle->activeGunnerWeaponIndex != -1))
                return fail("actual Ivan unload");
        }
        if (mode == "empty-stale") { vehicle->activeGunnerMode = 7; vehicle->activeGunnerWeaponIndex = 7; }
        const bool roundtrip = hz == 30 && rank == 2
            && (mode.empty() || mode == "IVAN" || mode == "DESO" || mode == "unloaded-IVAN");
        const auto reload = [&]() {
            std::ostringstream out;
            if (!writeRuntimeStateToStream(s, out, error)) return false;
            IniFile ini; if (!ini.loadFromString(out.str())) return false;
            auto loaded = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded, s, false);
            if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) return false;
            box = std::move(loaded); ++saves; return true;
        };
        // Keep references local to a state. Loading replaces the owning box.
        if (roundtrip) {
            std::cout << "IFV death save checkpoint: " << (mode.empty() ? "empty" : mode) << '\n';
            if (!reload()) return fail(mode + " pre-death save: " + error);
        }
        auto& active = *box; RuntimeSnapshotActiveStateScope activeScope(active);
        RuntimeObjectExitRequest exit; exit.objectId = id; exit.tick = active.tick;
        const auto result = finalizeRuntimeObjectExit(active, exit);
        drainRuntimeObjectExitQueue(active);
        const bool expectNuclear = mode == "IVAN";
        if (!result.accepted || result.deathWeapon.fireRequest.has_value() != expectNuclear)
            return fail(mode + " " + std::to_string(hz) + "Hz death weapon gate");
        if (!expectNuclear && (!active.specialMechanisms.radiationAreas().empty()
            || std::any_of(active.projectiles.begin(), active.projectiles.end(), [](const auto& p) { return p.weaponId == "CRNuke"; })))
            return fail(mode + " ordinary death emitted radiation");
        const auto* dead = findRuntimeObjectByIdIncludingInactive(active, id);
        if (!dead || dead->alive || !dead->transportPassengerIds.empty()
            || !active.deathWeapons.hasTriggered(id)) return fail("death/cargo authority");
        if (std::none_of(active.impactEffects.begin(), active.impactEffects.end(),
            [&](const auto& fx) { return fx.targetObjectId == id && fx.death; })) return fail("ordinary explosion animation lost");
        if (expectNuclear) {
            for (int tick = 0; tick < hz * 2 && active.specialMechanisms.radiationAreas().empty(); ++tick) {
                ++active.tick; advanceRuntimeEffects(active);
            }
            if (active.specialMechanisms.radiationAreas().empty()) return fail("Ivan nuclear site lost");
        }
        const auto radiationCount = active.specialMechanisms.radiationAreas().size();
        if (!finalizeRuntimeObjectExit(active, exit).duplicate
            || active.specialMechanisms.radiationAreas().size() != radiationCount) return fail("duplicate emitted fallout");
        if (roundtrip) {
            std::ostringstream out; if (!writeRuntimeStateToStream(active, out, error)) return fail(error);
            IniFile ini; if (!ini.loadFromString(out.str())) return fail("post-death parse");
            auto loaded = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded, active, false);
            if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)
                || !loaded->deathWeapons.hasTriggered(id)
                || loaded->specialMechanisms.radiationAreas().size() != radiationCount
                || !finalizeRuntimeObjectExit(*loaded, exit).duplicate) return fail("post-death save/tombstone");
            ++saves;
        }
        ++cases; expectNuclear ? ++nuclear : ++ordinary;
    }
    std::cout << "IFV death passed: " << cases << " actual cargo/death cases, " << ordinary
        << " ordinary explosions without radiation, " << nuclear << " Ivan-mode controls, " << saves << " saves\n";
    error.clear(); return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
