bool runRuntimeDeathCratesSmoke(const RuntimeConfig& config, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "Death crates: " + why; return false; };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& state = *storage;
    state.config = config;
    state.config.crates = false;
    state.config.computerAiEnabled = false;
    state.config.playerSlots.clear();
    state.config.gameMode = "Battle";
    state.config.gameModeRulesOverride.clear();
    state.runtimePreferencePersistenceEnabled = false;
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.runtimeSoundsEnabled = false;
    state.report = buildRuntimeStartupReport(state.config, &state.world,
        &state.theater, &state.art, &state.rules, &state.mapFacts);
    if (!state.report.success) return fail("original rules/assets unavailable");
    state.rulesLoaded = state.report.rulesLoaded;
    state.artLoaded = state.report.artLoaded;
    state.theaterLoaded = state.report.theaterLoaded;
    state.mapFactsLoaded = false;
    state.visionEnabled = false;
    state.triggers.bootstrapComplete = true;
    state.economy.owner = "Americans";
    state.economy.credits = 100;
    state.world.accessObjects().clear();
    state.world.accessTerrainObjects().clear();
    state.world.accessTerrainCells().clear();
    state.world.accessSmudges().clear();
    for (int y = 64; y < 104; ++y) for (int x = 64; x < 104; ++x) {
        TerrainCell cell; cell.x = x; cell.y = y;
        cell.cellIndex512 = runtimeCellKey(x, y); cell.tileIndex = 0;
        state.world.accessTerrainCells().push_back(cell);
    }
    initializeRuntimeNavigation(state);
    initializeRuntimeTriggers(state);
    if (state.triggers.truckCrate || state.triggers.trainCrate)
        return fail("stock Dry Heat Basic crate switches were not loaded as disabled");
    state.triggers.bootstrapComplete = true;
    int nextId = 1;
    const auto add = [&](const std::string& type, bool building, int x = 80, int y = 80) {
        WorldObjectInstance object;
        object.instanceId = nextId++; object.mapId = "DeathCrate" + std::to_string(object.instanceId);
        object.type = type; object.owner = building ? "Neutral" : "Americans";
        object.kind = building ? WorldObjectKind::Structure : WorldObjectKind::Unit;
        object.alive = true; object.health = 1000; object.x = x; object.y = y;
        object.cell = runtimeCellKey(x, y); object.mission = "Guard";
        state.world.accessObjects().push_back(object);
        invalidateRuntimeObjectProjectionCache(state);
        invalidateRuntimeTickOccupancyCache(state);
        return object.instanceId;
    };
    const auto exit = [&](int id, RuntimeObjectExitKind kind = RuntimeObjectExitKind::Destroyed) {
        RuntimeObjectExitRequest request; request.objectId = id; request.kind = kind;
        request.tick = ++state.tick; request.suppressCrew = true; request.suppressDeathWeapon = true;
        return finalizeRuntimeObjectExit(state, request);
    };
    const auto* truck = state.rules.findUnitType("UnitType", "TRUCKB");
    if (!truck || !truck->carriesCrate) return fail("stock loaded truck flag missing");
    {
        IniFile trainIni;
        trainIni.loadFromString("[VehicleTypes]\n0=LOOTTRAIN\n[LOOTTRAIN]\nStrength=100\nCarriesCrate=yes\nIsTrain=yes\n");
        RulesDatabase trainRules;
        trainRules.loadFromIni(trainIni);
        std::swap(state.rules, trainRules);
        WorldObjectInstance train; train.alive = true; train.kind = WorldObjectKind::Unit;
        train.type = "LOOTTRAIN"; train.x = train.y = 80;
        RuntimeObjectExitRequest request;
        state.triggers.truckCrate = true;
        const bool initiallyDisabled = !prepareRuntimeDeathCrate(state, train, request).has_value();
        RuntimeTriggerDefinition trigger; RuntimeTriggerAction action; action.type = 97;
        const bool toggledOn = applyRuntimeCampaignAction(state, trigger, action)
            && prepareRuntimeDeathCrate(state, train, request).has_value();
        const bool toggledOff = applyRuntimeCampaignAction(state, trigger, action)
            && !prepareRuntimeDeathCrate(state, train, request).has_value();
        state.triggers.truckCrate = false;
        std::swap(state.rules, trainRules);
        if (!initiallyDisabled || !toggledOn || !toggledOff)
            return fail("train rule and original action 97 toggle did not agree");
    }
    int landmarks = 0, moneyLandmarks = 0;
    for (const auto& building : state.rules.getBuildingTypes()) {
        if (!building.crateBeneath) continue;
        state.skirmishCrates.clear();
        const int id = add(building.id, true);
        if (!exit(id).accepted || state.skirmishCrates.size() != 1)
            return fail(building.id + " did not drop exactly one crate");
        const auto& crate = state.skirmishCrates.front();
        if (!crate.active || !crate.authored || crate.respawnTick <= state.tick || crate.imageId.empty()
            || crate.powerupType.empty() || std::abs(crate.cell.x - 80) > 6 || std::abs(crate.cell.y - 80) > 6)
            return fail(building.id + " drop is not nearby or not an authored reward");
        if (building.crateBeneathIsMoney) {
            ++moneyLandmarks;
            if (crate.powerupType != "Money") return fail(building.id + " lost its fixed money reward");
        }
        if (!exit(id).duplicate || state.skirmishCrates.size() != 1)
            return fail(building.id + " repeated destruction duplicated loot");
        ++landmarks;
    }
    if (landmarks < 10 || moneyLandmarks < 8) return fail("too few original landmark rules loaded");
    state.skirmishCrates.clear();
    for (const auto& [type, building] : std::array<std::pair<const char*, bool>, 4>{
            {{"MTNK", false}, {"TRUCKA", false}, {"GAPOWR", true}, {"GACNST", true}}}) {
        if (!exit(add(type, building)).accepted || !state.skirmishCrates.empty())
            return fail(std::string(type) + " incorrectly dropped loot");
    }
    for (auto kind : {RuntimeObjectExitKind::Sold, RuntimeObjectExitKind::TemporalDestroyed,
            RuntimeObjectExitKind::Consumed, RuntimeObjectExitKind::TransformSource, RuntimeObjectExitKind::ScriptRemoved}) {
        if (!exit(add("TRUCKB", false), kind).accepted || !state.skirmishCrates.empty())
            return fail("non-destructive vehicle exit dropped loot");
        if (!exit(add("CANEWY04", true), kind).accepted || !state.skirmishCrates.empty())
            return fail("non-destructive building exit dropped loot");
    }
    const int truckId = add("TRUCKB", false);
    if (!exit(truckId).accepted || !state.skirmishCrates.empty())
        return fail("map TruckCrate=no was ignored");
    state.triggers.truckCrate = true;
    if (!exit(add("TRUCKB", false)).accepted || state.skirmishCrates.size() != 1
        || !state.skirmishCrates.front().authored || state.skirmishCrates.front().powerupType.empty())
        return fail("loaded truck did not drop a random reward with random crates disabled");
    const auto truckCell = state.skirmishCrates.front().cell;
    // Keep the vehicle drop active while collecting the landmark's cash crate.
    if (!exit(add("CANEWY04", true, 88, 88)).accepted || state.skirmishCrates.size() != 2)
        return fail("statue drop missing");
    auto& cash = state.skirmishCrates.back();
    const auto cashCell = cash.cell;
    const int collectorId = add("MTNK", false, cashCell.x, cashCell.y);
    if (!advanceRuntimeSkirmishCrates(state) || state.skirmishCrates.back().active
        || state.economy.credits < 2100 || state.economy.credits > 3000)
        return fail("disabled random crates prevented the dropped money pickup");
    const int collectedBalance = state.economy.credits;
    state.tick += 100000;
    if (advanceRuntimeSkirmishCrates(state) || state.skirmishCrates.size() != 2
        || state.skirmishCrates.back().active || state.skirmishCrates.front().cell != truckCell
        || state.economy.credits != collectedBalance)
        return fail("loot with random crates disabled regenerated or moved after collection/expiry");

    std::ostringstream saved;
    if (!writeRuntimeStateToStream(state, saved, error)) return false;
    IniFile ini;
    if (!ini.loadFromString(saved.str())) return fail("could not read saved crate state");
    const auto beforeHash = runtimePersistentTransientStateHash(state);
    state.skirmishCrates.clear();
    state.triggers.truckCrate = false;
    if (!restoreRuntimeCratesFromSave(state, ini, error) || state.skirmishCrates.size() != 2
        || !state.triggers.truckCrate || state.triggers.trainCrate
        || !state.skirmishCrates.front().authored || !state.skirmishCrates.back().authored
        || state.skirmishCrates.back().active || runtimePersistentTransientStateHash(state) != beforeHash)
        return fail("save/load lost active or collected destruction drops");
    // The new lifecycle flag is simulation authority, not merely presentation.
    std::string hashError;
    completeRuntimeWorldRefresh(state);
    state.config.multiplayerActive = true;
    state.config.multiplayerSetupHash = 1234;
    state.config.multiplayerCanonicalMapId = "death-crates-fixture";
    state.config.multiplayerMapContentFingerprint = 5678;
    state.config.simulationRandomSeed = 815;
    RuntimeHumanHouseAuthority local;
    local.owner = state.economy.owner;
    local.canonicalOwner = runtimeCanonicalHumanHouseOwner(local.owner);
    local.country = "Americans"; local.localFacade = true;
    state.humanHouseAuthorities = {local};
    const auto authorityBefore = runtimeMultiplayerAuthoritativeStateHash(state, hashError);
    if (!authorityBefore) return fail(hashError);
    state.skirmishCrates.front().authored = false;
    if (runtimeMultiplayerAuthoritativeStateHash(state, hashError) == authorityBefore)
        return fail("network authority ignores authored crate state");
    state.skirmishCrates.front().authored = true;
    state.config.multiplayerActive = false;
    state.humanHouseAuthorities.clear();
    state.config.crates = false;
    state.campaignMode = true;
    state.skirmishCrates.clear();
    if (!exit(add("CANEWY04", true, 88, 88)).accepted || state.skirmishCrates.size() != 1)
        return fail("campaign landmark did not drop loot");
    auto* collector = findMutableObjectById(state, collectorId);
    collector->x = state.skirmishCrates.front().cell.x; collector->y = state.skirmishCrates.front().cell.y;
    collector->cell = runtimeCellKey(collector->x, collector->y);
    if (!advanceRuntimeSkirmishCrates(state)
        || state.economy.credits != collectedBalance + state.rules.getCrateRules().soloMoney)
        return fail("campaign money drop ignored SoloCrateMoney");
    state.campaignMode = false;
    state.economy.credits = 100;
    state.skirmishCrates.clear();
    const int explosiveTruckId = add("TRUCKB", false, 96, 96);
    RuntimeCrateState explosive;
    explosive.cell = {96, 96}; explosive.authored = true;
    explosive.powerupType = "Explosion"; explosive.powerupData = 5000.0;
    state.skirmishCrates.push_back(explosive);
    if (!advanceRuntimeSkirmishCrates(state) || state.skirmishCrates.size() != 2
        || state.skirmishCrates.front().active || !state.skirmishCrates.back().active
        || findMutableRuntimeObjectByIdIncludingInactive(state, explosiveTruckId)->alive)
        return fail("lethal pickup did not safely drop loot through the actual damage path");
    // Retail Map::AI and Cell::CollectCrate replace any crate only when the
    // multiplayer random-crate option is on, including a destruction drop.
    state.skirmishCrates.clear();
    state.config.crates = true;
    if (!exit(add("CANEWY04", true, 88, 88)).accepted || state.skirmishCrates.size() != 1)
        return fail("random-on landmark drop missing");
    const auto expiry = state.skirmishCrates.front().respawnTick;
    state.tick = expiry;
    if (!advanceRuntimeSkirmishCrates(state) || !state.skirmishCrates.front().active
        || state.skirmishCrates.front().authored || state.skirmishCrates.front().respawnTick <= state.tick)
        return fail("random-on destruction drop did not enter the original expiry/replacement cycle");
    const auto replacementCell = state.skirmishCrates.front().cell;
    auto* replacementCollector = findMutableObjectById(state, collectorId);
    replacementCollector->x = replacementCell.x; replacementCollector->y = replacementCell.y;
    replacementCollector->cell = runtimeCellKey(replacementCell.x, replacementCell.y);
    state.skirmishCrates.front().powerupType = "Money";
    state.skirmishCrates.front().powerupData = 2000.0;
    if (!advanceRuntimeSkirmishCrates(state) || state.skirmishCrates.front().active)
        return fail("random-on replacement was not collectable");
    ++state.tick;
    if (!advanceRuntimeSkirmishCrates(state) || !state.skirmishCrates.front().active)
        return fail("random-on collected loot did not replenish");
    std::cout << "Death crates: " << landmarks << " stock landmarks (" << moneyLandmarks
        << " fixed cash), loaded truck, map switches, exclusions, duplicate deaths, pickup with random crates off, "
        << "original option-dependent lifetime, save/load, network authority, campaign cash and lethal explosion pickup passed\n";
    return true;
}
