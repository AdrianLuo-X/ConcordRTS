#ifdef _MSC_VER
#pragma optimize("", off)
#endif
// Real UI target orders and the complete simulation clock, using retail rules.
bool runRuntimeMagnetronMechanicsSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto live = std::make_unique<RuntimeWindowState>();
    live->config = config;
    live->report = buildRuntimeStartupReport(config, &live->world, &live->theater,
        &live->art, &live->rules, &live->mapFacts);
    if (!live->report.success) { error = "Magnetron mechanics: retail resources unavailable"; return false; }
    int cases = 0, failures = 0, saves = 0;
    const auto run = [&](const std::string& type, bool friendly, const std::string& ending,
                         int hz, bool noCooldown) {
        const std::string label = type + (friendly ? "-ctrl-own-" : "-enemy-") + ending
            + "-" + std::to_string(hz) + (noCooldown ? "-instant" : "-normal");
        const auto fail = [&](const std::string& why) {
            ++failures;
            if (error.empty()) error = "Magnetron mechanics: " + label + ": " + why;
            std::cout << "FAIL " << label << ": " << why << '\n';
            return false;
        };
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& s = *storage;
        seedRuntimeLoadScratch(s, *live, false);
        s.config = config; s.config.fixedTickHz = hz; s.config.noCooldown = noCooldown;
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.shortGame = false;
        s.config.playerSlots.clear(); s.config.playerCountry = "YuriCountry";
        s.economy.owner = "YuriCountry"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.visionEnabled = false;
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear();
        s.runtimePreferencePersistenceEnabled = s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        s.terrainInfoByCell.clear(); s.terrainByCell.clear();
        s.mapFacts = {}; s.mapFactsLoaded = s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        const bool naval = type != "MTNK";
        for (int y = 44; y <= 88; ++y) for (int x = 44; x <= 88; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x; cell.tileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
            RuntimeTerrainInfo terrain;
            terrain.water = naval ? x >= 64 : ending == "water-drop" && x >= 64 && x <= 65;
            s.terrainInfoByCell.emplace(runtimeCellKey(x, y), terrain);
            MapCellFacts facts; facts.x = x; facts.y = y; facts.cellIndex = cell.cellIndex512;
            facts.hasTerrain = true; facts.isWater = terrain.water;
            facts.terrainClass = terrain.water ? "water" : "land"; facts.passabilityHint = "passable";
            s.mapFacts.cellIndexLookup.emplace(facts.cellIndex, s.mapFacts.cells.size());
            s.mapFacts.cells.push_back(facts);
        }
        const auto add = [&](const std::string& id, const std::string& owner, RuntimeCellPoint cell) {
            auto* object = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, id, cell);
            if (!object) return -1;
            object->owner = owner; object->mission = "Sleep";
            object->weaponCooldownTicksRemaining = 100000;
            object->buildAnimTicksRemaining = object->buildAnimTicksTotal = 0;
            clearRuntimeRenderPosition(*object);
            return object->instanceId;
        };
        const int sourceId = add("TELE", "YuriCountry", {60, 64});
        const int targetId = add(type, friendly ? "YuriCountry" : "Americans", {68, 64});
        if (sourceId < 0 || targetId < 0) return fail("retail objects missing");
        auto* source = findMutableObjectById(s, sourceId);
        source->mission = "Guard"; source->weaponCooldownTicksRemaining = 0;
        source->facing = source->turretFacing = runtimeFacingFromDelta(8, 0);
        const int originalHealth = findObjectById(s, targetId)->health;
        initializeRuntimeNavigation(s); ensureRuntimeSpecialMechanismsInitialized(s);
        RuntimeSnapshotActiveStateScope active(s);
        s.selectedObjectIds = {sourceId};
        if (friendly) {
            issueRuntimeAttackCommand(s, *findObjectById(s, targetId));
            if (s.lastCommand.acceptedCount != 0) return fail("ordinary attack accepted a friendly target");
        }
        if (!dispatchRuntimeTargetOrderFromUi(s, friendly
                ? RuntimeMultiplayerGameplayTargetOrderKind::Attack
                : RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {68, 64}, findObjectById(s, targetId))
            || s.lastCommand.acceptedCount != 1 || !s.behaviors.contains(sourceId)
            || s.behaviors.at(sourceId).forceFire != friendly)
            return fail("UI attack order was not preserved");
        const auto step = [](RuntimeWindowState& world) {
            RuntimeSnapshotActiveStateScope scope(world);
            ++world.tick; advanceRuntimeSimulation(world);
        };
        bool lifted = false;
        for (int tick = 0; tick < hz * 5; ++tick) {
            step(s);
            const auto* binding = runtimeMagBeamBindingForTarget(s, targetId);
            if (binding && binding->kinematics.worldZ >= 2.9
                && binding->kinematics.worldX < (ending == "water-drop" ? 65.75 : 67.0)
                // The native release freezes XY. Start the water-drop case
                // while the current landing cell is actually water.
                && (ending != "water-drop" || binding->kinematics.location.water)) {
                lifted = true; break;
            }
        }
        const auto* target = findObjectById(s, targetId);
        if (!lifted || !target || target->health != originalHealth || !target->airborne)
            return fail("no intact suspended target: " + s.simulationStatus);
        if (friendly) {
            s.selectedObjectIds = {targetId}; issueRuntimeMoveCommand(s, {68, 68});
            if (s.lastCommand.acceptedCount != 0 || s.behaviors.contains(targetId))
                return fail("the suspended unit accepted a competing movement order");
            s.selectedObjectIds = {sourceId};
        }
        refreshRuntimeSpecialMechanismRenderSnapshots(s);
        if (std::none_of(s.specialMechanismRenderSnapshots.begin(), s.specialMechanismRenderSnapshots.end(),
                [&](const auto& beam) { return beam.kind == "magBeam" && beam.hasBeamEndpoints
                    && beam.targetLevel >= 2.9; })) return fail("suspension has no visible beam");
        if (ending == "aa") {
            const int flakId = add("HTK", "YuriCountry", {63, 65});
            auto* flak = findMutableObjectById(s, flakId);
            if (!flak) return fail("anti-air unit missing");
            flak->weaponCooldownTicksRemaining = 0;
            flak->facing = flak->turretFacing = runtimeFacingFromDelta(3, -1);
            target = findObjectById(s, targetId);
            const auto profile = runtimeWeaponProfileForObject(s, *flak, target);
            const auto* groundGun = s.rules.findWeaponType("120mm");
            if (!profile.projectile || !profile.projectile->aa || !groundGun
                || runtimeWeaponCanTargetObject(s, *groundGun, *target))
                return fail("airborne vehicle did not switch from ground fire to anti-air fire");
            s.selectedObjectIds = {flakId}; issueRuntimeAttackCommand(s, *target);
            bool hit = false;
            for (int tick = 0; tick < hz * 3; ++tick) {
                step(s); target = findObjectById(s, targetId);
                if (target && target->health < originalHealth && runtimeObjectOffGroundByMagBeam(s, targetId)) {
                    hit = true; break;
                }
            }
            if (!hit) return fail("anti-air attack did not damage the suspended vehicle");
        } else {
            std::ostringstream saved; IniFile ini; std::string saveError;
            auto copy = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*copy, s, false);
            if (!writeRuntimeStateToStream(s, saved, saveError) || !ini.loadFromString(saved.str())
                || !materializeRuntimeLoadStateFromIni(*copy, ini, saveError)) return fail("suspended save: " + saveError);
            ++saves;
            const auto release = [&](RuntimeWindowState& world) {
                RuntimeSnapshotActiveStateScope scope(world); world.selectedObjectIds = {sourceId};
                if (ending == "stop" || ending == "water-drop") issueRuntimeStopCommand(world);
                if (ending == "move") issueRuntimeMoveCommand(world, {58, 64});
            };
            release(s); release(*copy);
            bool landed = false;
            const auto hp = [](const RuntimeWindowState& world, int id) {
                const auto* object = findObjectById(world, id);
                return object && object->alive ? object->health : 0;
            };
            for (int tick = 0; tick < hz * 15; ++tick) {
                step(s); step(*copy);
                if (s.magBeams.stateHash() != copy->magBeams.stateHash()
                    || hp(s, targetId) != hp(*copy, targetId)) return fail("save continuation diverged");
                if (!runtimeMagBeamBindingForTarget(s, targetId)) { landed = true; break; }
            }
            if (!landed) return fail("target never landed: " + s.simulationStatus);
            target = findObjectById(s, targetId); source = findMutableObjectById(s, sourceId);
            if ((ending == "automatic" && naval) || ending == "water-drop") {
                if (hp(s, targetId) != 0) return fail("unit survived an invalid water/land landing");
            } else if (!target || target->airborne || target->health != originalHealth
                || target->mission == "MagBeamSuspended") return fail("legal landing did not restore intact unit");
            if (!source || source->health != 150) return fail("landing collided with the magnetron");
            if (ending == "automatic" && (source->x != 60 || source->y != 64
                || (!naval && (target->x < 61 || target->x > 63))))
                return fail("magnetron retreated from its own tractor beam or did not drop the target nearby");
        }
        ++cases; std::cout << "PASS " << label << '\n';
        return true;
    };
    for (int hz : {30, 60}) {
        for (bool instant : {false, true}) for (const std::string type : {"MTNK", "DEST", "CARRIER"}) {
            run(type, true, instant ? "move" : "stop", hz, instant);
        }
        for (bool friendly : {false, true}) for (const std::string type : {"MTNK", "DEST"})
            run(type, friendly, "automatic", hz, false);
        run("MTNK", false, "aa", hz, false);
        run("MTNK", true, "water-drop", hz, false);
    }
    std::cout << "Magnetron mechanics: passed=" << cases << " failed=" << failures
        << " suspended saves=" << saves << '\n';
    return failures == 0;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif
