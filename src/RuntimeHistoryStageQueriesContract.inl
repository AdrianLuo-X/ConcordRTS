// The three actual stages run against independently initialized authorities.
bool runRuntimeHistoryStageQueriesContract(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "history stage query equivalence: " + why;
        return false;
    };
    const auto makeState = [&](RuntimeWindowState& state) {
        RuntimeSnapshotActiveStateScope active(state);
        IniFile ini;
        if (!ini.loadFromString("[General]\nPadAircraft=JET\nReloadRate=.0001\n[VehicleTypes]\n0=TANK\n1=SHARED\n2=TELE\n"
            "3=CARRIER\n4=CAMO\n5=AREA\n6=CHILD\n[InfantryTypes]\n0=SHARED\n1=MAN\n"
            "[AircraftTypes]\n0=SHARED\n1=JET\n[BuildingTypes]\n0=SHARED\n1=FIELD\n2=ARMORY\n3=HOSP\n"
            "[TANK]\nStrength=200\nSpeed=5\nAmmo=6\nInitialAmmo=2\nSpeedType=Track\nMovementZone=Normal\n"
            "[SHARED]\nStrength=200\nSpeed=5\nAmmo=3\nPrimary=STOCK\n"
            "[TELE]\nStrength=200\nSpeed=5\nTeleporter=yes\n"
            "[CARRIER]\nStrength=200\nSpeed=5\nSpawns=CHILD\nSpawnsNumber=1\n"
            "[CAMO]\nStrength=200\nSpeed=5\nCanDisguise=yes\nDisguiseWhenStill=yes\n"
            "[AREA]\nStrength=200\nSpeed=5\nDeployFire=yes\nPrimary=AREAGUN\nDeployWeapon=AREAGUN\nAmmo=-1\n"
            "[CHILD]\nStrength=50\nSpeed=5\nSpawned=yes\n"
            "[MAN]\nStrength=100\nSpeed=5\nAmmo=-1\n"
            "[JET]\nStrength=100\nSpeed=10\nSpeedType=Winged\nMovementZone=Fly\n"
            "Ammo=4\nInitialAmmo=1\nManualReload=yes\nReloadRate=.0001\nRepairable=yes\nAirportBound=yes\nDock=FIELD\nSecondary=INFINITE\n"
            "[FIELD]\nStrength=1000\nUnitReload=yes\nNumberOfDocks=1\nFactory=AircraftType\n"
            "[ARMORY]\nStrength=1000\nArmory=yes\nAmmo=-3\n"
            "[HOSP]\nStrength=1000\nHospital=yes\nAmmo=2\n"
            "[STOCK]\nDamage=10\nRange=5\nProjectile=SHOT\nWarhead=NORMAL\n"
            "[AREAGUN]\nDamage=10\nRange=5\nAreaFire=yes\nProjectile=SHOT\nWarhead=NORMAL\n"
            "[INFINITE]\nDamage=1\nRange=5\nInfiniteMindControl=yes\nProjectile=SHOT\nWarhead=MIND\n"
            "[SHOT]\nAG=yes\nAA=yes\n[NORMAL]\nCellSpread=0\n[MIND]\nMindControl=yes\n")
            || !state.rules.loadFromIni(ini)) return false;
        // Distinguish all namespace variants despite their shared INI section.
        auto* vehicle = const_cast<UnitType*>(state.rules.findUnitType("Vehicle", "SHARED"));
        auto* infantry = const_cast<UnitType*>(state.rules.findUnitType("Infantry", "SHARED"));
        auto* aircraft = const_cast<UnitType*>(state.rules.findUnitType("Aircraft", "SHARED"));
        if (!vehicle || !infantry || !aircraft) return false;
        vehicle->ammo = 3; infantry->ammo = 2; aircraft->ammo = 7;
        aircraft->weaponSet.primary = "INFINITE";
        state.rulesLoaded = true; state.tick = 10; state.config.fixedTickHz = 30;
        state.economy.owner = "Player"; state.economy.credits = 2000;
        state.runtimeSoundsEnabled = state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSuppressPlayerAudioFeedback = true; state.visionEnabled = false;
        state.structureFootprints["FIELD"] = {2, 2, true};
        for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            state.world.accessTerrainCells().push_back(cell);
            state.terrainInfoByCell.emplace(runtimeCellKey(x, y), RuntimeTerrainInfo{});
        }
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        const auto add = [&](int id, WorldObjectKind kind, const char* type, bool alive,
                             int ammo = -1, bool authoredAircraft = false, bool airborne = false) {
            WorldObjectInstance value; value.instanceId = id; value.kind = kind; value.type = type;
            value.owner = "Player"; value.alive = alive; value.health = alive ? 100 : 0;
            value.x = id % 20; value.y = (id / 20) % 20; value.cell = runtimeCellKey(value.x, value.y);
            value.currentAmmo = ammo; value.mission = "Guard"; value.poweredOperational = true;
            value.authoredAircraft = authoredAircraft; value.airborne = airborne;
            state.world.accessObjects().push_back(std::move(value));
        };
        add(1, WorldObjectKind::Unit, "TANK", true);
        add(2, WorldObjectKind::Infantry, "SHARED", true);
        add(3, WorldObjectKind::Unit, "SHARED", true, -1, true, false);
        add(4, WorldObjectKind::Unit, "SHARED", true, -1, false, true);
        add(5, WorldObjectKind::Structure, "SHARED", true);
        add(6, WorldObjectKind::Structure, "ARMORY", false);
        add(7, WorldObjectKind::Structure, "HOSP", false);
        add(8, WorldObjectKind::Unit, "TELE", false);
        add(9, WorldObjectKind::Unit, "CARRIER", false);
        add(10, WorldObjectKind::Unit, "CAMO", false);
        add(11, WorldObjectKind::Unit, "AREA", true);
        add(12, WorldObjectKind::Unit, "CAMO", true);
        add(13, WorldObjectKind::Unit, "TANK", true);
        findMutableObjectById(state, 13)->health = 0; // normalization never gates on health/alive
        add(200, WorldObjectKind::Structure, "FIELD", true, 0);
        add(201, WorldObjectKind::Unit, "JET", true, 0, true);
        auto* area = findMutableObjectById(state, 11); area->mission = "DeployedIdle"; area->deployed = true;
        const auto* host = findObjectById(state, 200);
        const auto dock = runtimeAircraftDockSpawn(state, *host, 0);
        auto* jet = findMutableObjectById(state, 201);
        jet->x = dock.cell.x; jet->y = dock.cell.y; jet->cell = runtimeCellKey(jet->x, jet->y);
        jet->level = dock.level; jet->dockedToObjectId = 200; jet->dockIndex = 0;
        jet->mission = "Dock"; jet->health = 50;
        for (int history = 0; history < 10000; ++history) {
            const char* type = history % 7 == 0 ? "MISSING" : history % 7 == 1 ? "SHARED" : "TANK";
            const WorldObjectKind kind = history % 7 == 1 && history % 2
                ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
            add(1000 + history, kind, type, false, history % 3 == 0 ? 5 : -1,
                history % 7 == 1 && history % 4 == 0, history % 7 == 1 && history % 5 == 0);
            state.world.accessObjects().back().mission = std::string(80, 'D') + "eStRoYeD";
        }
        ensureRuntimeSpecialMechanismsInitialized(state);
        state.parasiteStunUntilTickByObject[999999] = state.tick;
        // Plant real camouflage authority so death must execute its cleanup.
        state.specialMechanisms.tickCamouflage(state.world.accessObjects(), state.rules,
            state.tick, [](int) { return true; }, SpecialEventSink{});
        return state.specialMechanisms.camouflageStates().contains(12)
            && runtimeAmmoRulesForObject(state.rules, *findObjectById(state, 2))->ammo == 2
            && runtimeAmmoRulesForObject(state.rules, *findObjectById(state, 3))->ammo == 7
            && runtimeAmmoRulesForObject(state.rules, *findObjectById(state, 4))->ammo == 3
            && runtimeHasInfiniteMindControl(state.rules, *findObjectById(state, 4))
            && !runtimeHasInfiniteMindControl(state.rules, *findObjectById(state, 3));
    };
    auto oldStorage = std::make_unique<RuntimeWindowState>();
    auto newStorage = std::make_unique<RuntimeWindowState>();
    auto& oldState = *oldStorage; auto& newState = *newStorage;
    if (!makeState(oldState) || !makeState(newState)) return fail("authored namespace/camouflage/dock fixture");

    const auto hash = [](const RuntimeWindowState& state) {
        std::uint64_t result = state.world.deterministicSimulationStateHash();
        runtimeStableHashMix(result, state.specialMechanisms.stateHash());
        runtimeStableHashMix(result, state.aircraftServices.stateHash());
        runtimeStableHashMix(result, state.economy.credits);
        runtimeStableHashMix(result, state.parasiteStunUntilTickByObject.size());
        for (const auto& event : state.recentReplayEvents) {
            runtimeStableHashMix(result, event.sequence); runtimeStableHashMix(result, event.tick);
            runtimeStableHashMixString(result, event.kind); runtimeStableHashMixString(result, event.reason);
            runtimeStableHashMix(result, event.objectId); runtimeStableHashMix(result, event.targetObjectId);
            runtimeStableHashMix(result, event.commandTargetCell.x); runtimeStableHashMix(result, event.commandTargetCell.y);
            runtimeStableHashMixString(result, event.renderIntentId); runtimeStableHashMix(result, event.renderIntentHash);
        }
        return result;
    };
    // Independent scalar gate checks prove dead capable types remain eligible.
    const auto scalarGate = [](const RuntimeWindowState& state) {
        for (const auto& object : state.world.getObjects()) {
            if (object.alive && runtimeHasInfiniteMindControl(state.rules, object)) return true;
            const auto* unit = findRuntimeUnitType(state.rules, object);
            if (unit && (unit->teleporter || (unit->canDisguise && unit->disguiseWhenStill)
                || (!unit->spawns.empty() && unit->spawnsNumber > 0))) return true;
        }
        return false;
    };
    for (const char* type : {"TANK", "TELE", "CARRIER", "CAMO", "MISSING"}) {
        auto gateStorage = std::make_unique<RuntimeWindowState>();
        auto& gateState = *gateStorage; gateState.rules = oldState.rules;
        WorldObjectInstance dead; dead.instanceId = 1; dead.kind = WorldObjectKind::Unit;
        dead.type = type; dead.alive = false;
        gateState.world.accessObjects().push_back(dead);
        RuntimeHistoryQueryReferenceScope optimized(false);
        const bool expected = std::string_view(type) == "TELE" || std::string_view(type) == "CARRIER"
            || std::string_view(type) == "CAMO";
        if (runtimeHasSpecialCapabilityForHistoryPhase(gateState) != scalarGate(gateState)
            || scalarGate(gateState) != expected) return fail("dead-only capability gate " + std::string(type));
    }

    std::size_t queryComparisons = 0;
    // Exact rule queries retain unknown misses, case spelling, long strings,
    // aircraft namespace discriminators and duplicate type registration.
    {
        std::vector<WorldObjectInstance> variants;
        const std::array<std::string, 7> spellings{"SHARED", "shared", " Shared ", "MISSING", "",
            std::string("SHARED\0JET", 10), std::string(240, 'S')};
        for (const auto kind : {WorldObjectKind::Infantry, WorldObjectKind::Unit, WorldObjectKind::Structure})
            for (bool authored : {false, true}) for (bool airborne : {false, true})
                for (const auto& type : spellings) {
                    WorldObjectInstance value; value.kind = kind; value.type = type;
                    value.authoredAircraft = authored; value.airborne = airborne;
                    variants.push_back(std::move(value));
                }
        for (int missing = 0; missing < 300; ++missing) {
            WorldObjectInstance value; value.kind = WorldObjectKind::Unit;
            value.type = "UNRESOLVED_" + std::to_string(missing);
            variants.push_back(std::move(value));
        }
        RuntimeHistoryStageRuleMemo<const UnitType*> ammoMemo(false);
        RuntimeHistoryStageRuleMemo<bool> infiniteMemo(false);
        RuntimeHistoryStageRuleMemo<const BuildingType*> buildingMemo(false);
        const auto compare = [&](const WorldObjectInstance& object) {
            if (ammoMemo.query(object, [&](const auto& value) { return runtimeAmmoRulesForObject(oldState.rules, value); })
                    != runtimeAmmoRulesForObject(oldState.rules, object)
                || infiniteMemo.query(object, [&](const auto& value) { return runtimeHasInfiniteMindControl(oldState.rules, value); })
                    != runtimeHasInfiniteMindControl(oldState.rules, object)
                || buildingMemo.query(object, [&](const auto& value) {
                    return value.kind == WorldObjectKind::Structure ? findRuntimeBuildingTypeById(oldState.rules, value.type) : nullptr;
                }) != (object.kind == WorldObjectKind::Structure ? findRuntimeBuildingTypeById(oldState.rules, object.type) : nullptr))
                return fail("memo rule result at object " + std::to_string(object.instanceId));
            ++queryComparisons;
            return true;
        };
        for (const auto& object : oldState.world.getObjects()) if (!compare(object)) return false;
        for (const auto& object : variants) if (!compare(object)) return false;
        for (auto it = variants.rbegin(); it != variants.rend(); ++it) if (!compare(*it)) return false;
    }
    std::size_t checkedTicks = 0;
    for (int step = 0; step < 18; ++step) {
        std::array<std::array<bool, 3>, 2> changed{};
        for (int branch = 0; branch < 2; ++branch) {
            auto& state = branch == 0 ? oldState : newState;
            RuntimeSnapshotActiveStateScope active(state);
            RuntimeHistoryQueryReferenceScope mode(branch == 0);
            state.tick = 10 + step;
            if (step == 1) {
                state.objectIndex.find(state.world.accessObjects(), 12)->alive = false;
                findMutableObjectById(state, 200)->poweredOperational = false;
            }
            if (step == 3) findMutableObjectById(state, 200)->poweredOperational = true;
            if (step == 5) state.objectIndex.find(state.world.accessObjects(), 201)->alive = false;
            if (step == 6) state.objectIndex.find(state.world.accessObjects(), 201)->alive = true;
            if (step == 8) std::reverse(state.world.accessObjects().begin(), state.world.accessObjects().end());
            if (step == 9) {
                auto value = *findObjectById(state, 1); value.instanceId = 20000;
                value.alive = false; value.currentAmmo = -1;
                state.world.accessObjects().push_back(value);
            }
            if (step == 11) {
                // Assignment to the same RulesDatabase address must not carry
                // any cached profile from an earlier phase.
                auto replacement = state.rules;
                const_cast<UnitType*>(replacement.findUnitType("Vehicle", "TANK"))->initialAmmo = 4;
                state.rules = std::move(replacement);
                state.objectIndex.find(state.world.accessObjects(), 20000)->currentAmmo = -1;
            }
            changed[branch][0] = advanceRuntimeAircraftServices(state);
            changed[branch][1] = advanceRuntimeSpecialMechanisms(state);
            changed[branch][2] = ensureRuntimeAreaFireBehaviors(state);
        }
        if (changed[0] != changed[1] || hash(oldState) != hash(newState))
            return fail("actual systems/changed/events hash at tick " + std::to_string(step));
        if (oldState.behaviors.size() != newState.behaviors.size()) return fail("area-fire behavior count");
        for (const auto& [id, old] : oldState.behaviors) {
            const auto found = newState.behaviors.find(id);
            if (found == newState.behaviors.end()) return fail("area-fire missing behavior");
            const auto& now = found->second;
            if (old.kind != now.kind || old.state != now.state || old.targetObjectId != now.targetObjectId
                || old.autoAcquired != now.autoAcquired || old.returnToDeployed != now.returnToDeployed)
                return fail("area-fire authority");
        }
        if (!newState.parasiteStunUntilTickByObject.empty()) return fail("expired parasite stun cleanup was skipped");
        if (step >= 1 && newState.specialMechanisms.camouflageStates().contains(12))
            return fail("dead camouflage authority cleanup was skipped");
        ++checkedTicks;
    }
    if (!newState.behaviors.contains(11)) return fail("actual live area-fire fixture did not enqueue attack");
    const auto* servicedAircraft = newState.objectIndex.find(newState.world.getObjects(), 201);
    if (!servicedAircraft || !servicedAircraft->alive || servicedAircraft->currentAmmo <= 0)
        return fail("actual aircraft service fixture did not reload after power/restoration");
    const auto* deadFinite = newState.objectIndex.find(newState.world.getObjects(), 1002);
    const auto* replacementFinite = newState.objectIndex.find(newState.world.getObjects(), 20000);
    if (!deadFinite || deadFinite->alive || deadFinite->currentAmmo != 2
        || !replacementFinite || replacementFinite->currentAmmo != 4)
        return fail("dead legacy ammo/rules replacement normalization");
    const auto* deadArmory = newState.objectIndex.find(newState.world.getObjects(), 6);
    const auto* deadHospital = newState.objectIndex.find(newState.world.getObjects(), 7);
    if (!deadArmory || deadArmory->currentAmmo != 3 || !deadHospital || deadHospital->currentAmmo != 2)
        return fail("dead infantry-service ammo normalization");
    std::cout << "History stage query equivalence: ruleObjects=" << queryComparisons
        << " exactSystemHashTicks=" << checkedTicks
        << " deadAmmo/capability/cleanup/aircraftPowerDeathRevival/areaFire/rulesReplacement=yes\n";
    error.clear();
    return true;
}
