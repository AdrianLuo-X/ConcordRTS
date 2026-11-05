RuntimeEconomyState makeRuntimeMultiplayerSnapshotSmokeEconomy(
    RuntimeProductionHostState& state,
    const std::string& owner,
    int credits
) {
    RuntimeEconomyState economy;
    initializeRuntimeLocalProductionLanes(economy);
    economy.owner = owner;
    economy.techLevel = 8;
    economy.credits = credits;
    economy.powerProduced = 100;
    economy.powerUsed = 25;
    economy.resourceStorageCapacity = 20;
    RuntimeBuildCatalogItem item;
    item.kind = RuntimeBuildKind::Unit;
    item.typeId = "SNAPSHOT_TANK";
    item.owner = "Americans,Russians";
    item.cost = 100;
    item.techLevel = 1;
    item.buildTicks = std::max(1, runtimeBaseBuildTimeTicks(state, item));
    item.techTreeAllowed = true;
    item.techTreeKind = "Vehicle";
    economy.catalog.push_back(item);
    RuntimeProductionLaneOrder order;
    order.serial = 1;
    order.itemKey = item.typeId;
    order.cost = item.cost;
    order.totalTicks = item.buildTicks;
    order.remainingTicks = item.buildTicks;
    economy.localProductionLanes.lanes[
        runtimeProductionLaneIndex(RuntimeProductionLane::Vehicles)
    ].queued.push_back(order);
    economy.localProductionLanes.nextSerial = 2;
    return economy;
}

bool configureRuntimeMultiplayerSnapshotSmokeState(
    RuntimeProductionHostState& state,
    bool secondLocal,
    bool reverseAuthorityInsertion,
    bool includeUnorderedBehaviors,
    double pendingRange
) {
    IniFile rulesIni;
    if (!rulesIni.loadFromString(
            "[VehicleTypes]\n"
            "0=SNAPSHOT_TANK\n"
            "[AircraftTypes]\n"
            "[InfantryTypes]\n"
            "[BuildingTypes]\n"
            "0=SNAPSHOT_LANDMARK\n"
            "[SNAPSHOT_LANDMARK]\n"
            "Strength=1000\n"
            "DestroyAnim=SNAPSHOT_COLLAPSE\n"
            "LeaveRubble=yes\n"
            "[SNAPSHOT_TANK]\n"
            "Strength=200\n"
            "Cost=100\n"
            "TechLevel=1\n"
            "Speed=6\n"
            "SpeedType=Track\n"
            "MovementZone=Normal\n"
        )
        || !state.rules.loadFromIni(rulesIni)) {
        return false;
    }
    state.rulesLoaded = true;
    state.config.defaultMapPath = "snapshot-smoke.map";
    state.config.gameMode = "Battle";
    state.config.fixedTickHz = 30;
    state.config.multiplayerActive = true;
    state.config.multiplayerSetupHash = 0x8194ab32c71d09e5ULL;
    state.config.multiplayerCanonicalMapId = "snapshot-smoke-map";
    state.config.multiplayerMapContentFingerprint =
        0x912e7a0463bd5cc1ULL;
    state.config.multiplayerTimelineEpoch = 3;
    state.config.simulationRandomSeed = 0xf01842bc997ea251ULL;
    state.config.startingCredits = 10000;
    state.config.startingUnits = 1;
    state.config.techLevel = 8;
    state.matchScores["skirmishplayer1"] = {3, 1, 1, 0, 1900, 14};
    state.matchScores["skirmishplayer2"] = {1, 0, 3, 1, 300, 9};
    state.tick = 7;
    state.config.playerSlots.resize(3);
    for (std::size_t index = 0; index < 3U; ++index) {
        RuntimeConfig::PlayerSlot& slot = state.config.playerSlots[index];
        slot.enabled = true;
        slot.startPosition = static_cast<int>(index + 1U);
        slot.team = "-";
    }
    state.config.playerSlots[0].human = true;
    state.config.playerSlots[0].local = !secondLocal;
    state.config.playerSlots[0].peerId = secondLocal
        ? "remote-peer-one"
        : "local-peer-one";
    state.config.playerSlots[0].name = "Display One";
    state.config.playerSlots[0].country = "Americans";
    state.config.playerSlots[1].human = true;
    state.config.playerSlots[1].local = secondLocal;
    state.config.playerSlots[1].peerId = secondLocal
        ? "local-peer-two"
        : "remote-peer-two";
    state.config.playerSlots[1].name = "Display Two";
    state.config.playerSlots[1].country = "Russians";
    state.config.playerSlots[2].human = false;
    state.config.playerSlots[2].country = "Africans";
    state.config.playerSlots[2].difficulty = "Hard";

    constexpr const char* ownerOne = "SkirmishPlayer1";
    constexpr const char* ownerTwo = "SkirmishPlayer2";
    constexpr const char* ownerThree = "SkirmishPlayer3";
    state.skirmishCountryByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerOne),
        "Americans"
    );
    state.skirmishCountryByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerTwo),
        "Russians"
    );
    state.skirmishCountryByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerThree),
        "Africans"
    );
    state.skirmishTeamByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerOne),
        "-"
    );
    state.skirmishTeamByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerTwo),
        "-"
    );
    state.skirmishTeamByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerThree),
        "-"
    );
    state.skirmishDifficultyByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerOne),
        "Medium"
    );
    state.skirmishDifficultyByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerTwo),
        "Medium"
    );
    state.skirmishDifficultyByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerThree),
        "Hard"
    );

    RuntimeEconomyState economyOne =
        makeRuntimeMultiplayerSnapshotSmokeEconomy(state, ownerOne, 9000);
    RuntimeEconomyState economyTwo =
        makeRuntimeMultiplayerSnapshotSmokeEconomy(state, ownerTwo, 7000);
    RuntimeHumanHouseAuthority first;
    first.slotIndex = 0;
    first.owner = ownerOne;
    first.canonicalOwner = runtimeCanonicalHumanHouseOwner(ownerOne);
    first.country = "Americans";
    first.localFacade = !secondLocal;
    RuntimeHumanHouseAuthority second;
    second.slotIndex = 1;
    second.owner = ownerTwo;
    second.canonicalOwner = runtimeCanonicalHumanHouseOwner(ownerTwo);
    second.country = "Russians";
    second.localFacade = secondLocal;
    if (first.localFacade) {
        state.economy = std::move(economyOne);
        state.techTree = TechTreeReport{};
        state.techTreeLoaded = false;
        second.remoteEconomy.emplace(std::move(economyTwo));
        second.remoteTechTree.emplace();
    } else {
        state.economy = std::move(economyTwo);
        state.techTree = TechTreeReport{};
        state.techTreeLoaded = false;
        first.remoteEconomy.emplace(std::move(economyOne));
        first.remoteTechTree.emplace();
    }
    if (reverseAuthorityInsertion) {
        state.humanHouseAuthorities.push_back(std::move(second));
        state.humanHouseAuthorities.push_back(std::move(first));
    } else {
        state.humanHouseAuthorities.push_back(std::move(first));
        state.humanHouseAuthorities.push_back(std::move(second));
    }

    RuntimeComputerAiState ai;
    ai.initialized = true;
    ai.owner = ownerThree;
    ai.country = "Africans";
    ai.difficulty = "Hard";
    ai.credits = 6500;
    ai.nextThinkTick = 11;
    ai.nextIncomeTick = 12;
    ai.nextBuildTick = 13;
    ai.nextAttackTick = 14;
    ai.nextRepairTick = 15;
    ai.productionSerial = 1;
    state.computerAiByOwner.emplace(
        runtimeCanonicalHumanHouseOwner(ownerThree),
        std::move(ai)
    );

    if (includeUnorderedBehaviors) {
        RuntimeObjectBehavior firstBehavior;
        firstBehavior.kind = RuntimeBehaviorKind::Move;
        firstBehavior.state = RuntimeBehaviorState::Queued;
        firstBehavior.moveTarget = {5, 6};
        firstBehavior.pendingRangeCells = pendingRange;
        RuntimeObjectBehavior secondBehavior = firstBehavior;
        secondBehavior.moveTarget = {7, 8};
        if (reverseAuthorityInsertion) {
            state.behaviors.emplace(42, std::move(secondBehavior));
            state.behaviors.emplace(41, std::move(firstBehavior));
        } else {
            state.behaviors.emplace(41, std::move(firstBehavior));
            state.behaviors.emplace(42, std::move(secondBehavior));
        }
    }
    WorldObjectInstance wreckObject;
    wreckObject.instanceId = 90; wreckObject.mapId = "SnapshotWreck";
    wreckObject.kind = WorldObjectKind::Unit; wreckObject.type = "SNAPSHOT_TANK";
    wreckObject.owner = ownerThree; wreckObject.x = 1; wreckObject.y = 2;
    wreckObject.cell = runtimeCellKey(1, 2); wreckObject.level = 2;
    wreckObject.alive = false; wreckObject.health = 0; wreckObject.mission = "Destroyed";
    state.world.accessObjects().push_back(wreckObject);
    RuntimeWreckState wreck; wreck.object = wreckObject; wreck.startTick = 2;
    wreck.x = 1; wreck.y = 2; wreck.startLevel = 4.0; wreck.fallSpeed = 12.0;
    wreck.jumpjet = true; wreck.impactAnim = "S_BANG48";
    state.wrecks.push_back(std::move(wreck));
    WorldObjectInstance collapseBuilding;
    collapseBuilding.instanceId = 91; collapseBuilding.mapId = "SnapshotCollapse";
    collapseBuilding.type = "SNAPSHOT_LANDMARK"; collapseBuilding.owner = ownerThree;
    collapseBuilding.kind = WorldObjectKind::Structure; collapseBuilding.x = 2; collapseBuilding.y = 2;
    collapseBuilding.cell = runtimeCellKey(2, 2); collapseBuilding.alive = false;
    collapseBuilding.health = 0; collapseBuilding.rubble = true; collapseBuilding.mission = "Destroyed";
    state.world.accessObjects().push_back(collapseBuilding);
    state.buildingCollapses.push_back({91, "SNAPSHOT_COLLAPSE", 2, 60, 200});
    state.triggers.publicBuildingLocations[999] = {"skirmishplayer3", {1, 2}, state.tick};
    state.triggers.loaded = true;
    state.triggers.bootstrapComplete = true;
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    state.particles.reset(state.config.fixedTickHz);
    if (!state.particles.alignEmptyStateTick(state.tick)) return false;
    state.particlesInitialized = true;
    state.superWeapons.reset(std::vector<SuperWeaponRule>{});
    if (!state.superWeapons.alignEmptyStateTick(state.tick)) return false;
    state.superWeaponsInitialized = true;
    return true;
}

RuntimeMultiplayerSnapshotIdentity
runtimeMultiplayerSnapshotSmokeIdentity(
    const RuntimeProductionHostState& state,
    std::uint64_t proposalId,
    const std::string& token,
    RuntimeMultiplayerSnapshotKind kind
) {
    RuntimeMultiplayerSnapshotIdentity identity;
    identity.proposalId = proposalId;
    identity.kind = kind;
    identity.sourceTimelineEpoch = state.config.multiplayerTimelineEpoch;
    identity.barrierTick = state.tick;
    identity.snapshotTick = state.tick;
    identity.snapshotToken = token;
    identity.gameplaySetupFingerprint = state.config.multiplayerSetupHash;
    std::string errorMessage;
    identity.authoritativeStateHash =
        runtimeMultiplayerAuthoritativeStateHash(state, errorMessage);
    return identity;
}

bool runtimeMultiplayerSnapshotSmokeReplaceIniValue(
    const std::vector<std::uint8_t>& bytes,
    const std::string& section,
    const std::string& key,
    const std::string& replacement,
    std::vector<std::uint8_t>& changed
) {
    std::string text(bytes.begin(), bytes.end());
    const std::string header = "[" + section + "]\n";
    const std::size_t sectionBegin = text.find(header);
    if (sectionBegin == std::string::npos) return false;
    const std::size_t nextSection = text.find("\n[", sectionBegin + 1U);
    const std::string keyPrefix = key + "=";
    const std::size_t keyBegin = text.find(
        keyPrefix,
        sectionBegin + header.size()
    );
    if (keyBegin == std::string::npos
        || (nextSection != std::string::npos && keyBegin >= nextSection)) {
        return false;
    }
    const std::size_t valueBegin = keyBegin + keyPrefix.size();
    const std::size_t valueEnd = text.find('\n', valueBegin);
    if (valueEnd == std::string::npos) return false;
    text.replace(valueBegin, valueEnd - valueBegin, replacement);
    changed.assign(text.begin(), text.end());
    return true;
}

bool configureRuntimeDetailSnapshotSmokeWorld(RuntimeProductionHostState& state,
    bool secondLocal, std::string& error) {
    RuntimeSnapshotActiveStateScope scope(state);
    if (!configureRuntimeMultiplayerSnapshotSmokeState(state, secondLocal,
            secondLocal, false, 0.0)) {
        error = "detail snapshot base fixture"; return false;
    }
    IniFile ini;
    if (!ini.loadFromString(R"DETAIL(
[VehicleTypes]
0=SNAPSHOT_TANK
[BuildingTypes]
0=DETAIL_FACTORY
1=SNAPSHOT_LANDMARK
[SNAPSHOT_LANDMARK]
Strength=1000
DestroyAnim=SNAPSHOT_COLLAPSE
LeaveRubble=yes
[SNAPSHOT_TANK]
Strength=200
Cost=100
TechLevel=1
Speed=6
SpeedType=Track
MovementZone=Normal
Primary=DETAIL_GUN
DistributedFire=yes
[DETAIL_FACTORY]
Strength=1000
Cost=100
TechLevel=1
Factory=UnitType
WeaponsFactory=yes
Foundation=1x1
[DETAIL_GUN]
Damage=10
Range=10
ROF=30
Burst=3
BurstDelay=3
OmniFire=yes
Projectile=DETAIL_SHOT
Speed=100
Warhead=DETAIL_WH
[DETAIL_SHOT]
AG=yes
[DETAIL_WH]
AffectsEnemies=yes
AffectsAllies=no
)DETAIL") || !state.rules.loadFromIni(ini)) {
        error = "detail snapshot rules fixture"; return false;
    }
    state.visionEnabled = false;
    // Terrain is constructed below; this fixture has no backing map file.
    state.config.defaultMapPath.clear();
    state.runtimeSuppressPlayerAudioFeedback = true;
    state.structureFootprints["DETAIL_FACTORY"] = {1, 1, true};
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
        TerrainCell terrain;
        terrain.x = x; terrain.y = y; terrain.cellIndex512 = y * 512 + x;
        state.world.accessTerrainCells().push_back(terrain);
        state.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
    }
    state.terrainByCell = buildRuntimeTerrainLookup(state.world);
    const auto add = [&](int id, const char* owner, WorldObjectKind kind, int x, int y) {
        WorldObjectInstance object;
        object.instanceId = id; object.mapId = "DetailSnapshot" + std::to_string(id);
        object.kind = kind; object.type = kind == WorldObjectKind::Structure
            ? "DETAIL_FACTORY" : "SNAPSHOT_TANK";
        object.owner = owner; object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
        object.health = kind == WorldObjectKind::Structure ? 1000 : 200;
        object.alive = true; object.mission = "Guard"; object.facing = 0;
        object.poweredOperational = true;
        // The authoritative world hash includes continuous coordinates even
        // for an idle actor. Match the production placement/load invariant.
        clearRuntimeRenderPosition(object);
        state.world.accessObjects().push_back(std::move(object));
    };
    add(41, "SkirmishPlayer1", WorldObjectKind::Unit, 2, 2);
    add(42, "SkirmishPlayer2", WorldObjectKind::Unit, 8, 2);
    add(43, "SkirmishPlayer1", WorldObjectKind::Unit, 3, 4);
    add(44, "SkirmishPlayer1", WorldObjectKind::Structure, 4, 8);
    add(45, "SkirmishPlayer3", WorldObjectKind::Unit, 10, 10);
    add(46, "SkirmishPlayer3", WorldObjectKind::Unit, 10, 13);
    add(47, "SkirmishPlayer3", WorldObjectKind::Structure, 8, 13);
    RuntimeTaskForce force; force.id = "SNAPSHOT_AI_FORCE"; force.members = {{3, "SNAPSHOT_TANK"}};
    state.triggers.taskForces[force.id] = force;
    RuntimeScriptType script; script.id = "SNAPSHOT_AI_SCRIPT"; script.steps = {{4, 13013}, {49, 0}};
    state.triggers.scripts[script.id] = script;
    RuntimeTeamType type; type.id = "SNAPSHOT_AI_TEAM"; type.house = "SkirmishPlayer3";
    type.taskForceId = force.id; type.scriptId = script.id; type.reinforce = true;
    state.triggers.teams[type.id] = type;
    RuntimeCampaignAiTriggerDefinition trigger; trigger.id = "SNAPSHOT_AI_TRIGGER";
    trigger.primaryTeamId = type.id; trigger.currentWeight = 40; trigger.minimumWeight = 10; trigger.maximumWeight = 80;
    trigger.enabled = trigger.valid = trigger.availableInSkirmish = true;
    state.triggers.computerAiTriggers.push_back(trigger);
    initializeRuntimeNavigation(state);
    return true;
}
