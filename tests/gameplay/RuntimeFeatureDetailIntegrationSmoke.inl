bool runRuntimeFeatureDetailIntegrationSmokeProbe(const RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "feature detail integration: " + message;
        return false;
    };
    const auto setup = [&]() {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& state = *storage;
        state.config.fixedTickHz = 30;
        state.rules = live.rules;
        state.art = live.art;
        state.rulesLoaded = true;
        state.artLoaded = live.artLoaded;
        state.runtimeSuppressPlayerAudioFeedback = true;
        state.economy.owner = "ProbeA";
        initializeRuntimeLocalProductionLanes(state.economy);
        for (int y = 1; y <= 25; ++y) {
            for (int x = 1; x <= 25; ++x) {
                TerrainCell cell;
                cell.x = x;
                cell.y = y;
                cell.cellIndex512 = runtimeCellKey(x, y);
                state.world.accessTerrainCells().push_back(cell);
                state.terrainInfoByCell.emplace(cell.cellIndex512, RuntimeTerrainInfo{});
            }
        }
        state.terrainByCell = buildRuntimeTerrainLookup(state.world);
        return storage;
    };
    const auto add = [](RuntimeWindowState& state, int id, WorldObjectKind kind,
        const std::string& type, RuntimeCellPoint cell) {
        WorldObjectInstance object;
        object.instanceId = id;
        object.kind = kind;
        object.type = type;
        object.owner = state.economy.owner;
        object.health = 100;
        object.alive = true;
        object.poweredOperational = true;
        object.x = cell.x;
        object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y);
        object.mission = "Guard";
        state.world.accessObjects().push_back(object);
        invalidateRuntimeObjectProjectionCache(state);
    };
    auto repair = setup();
    const auto* power = live.rules.findBuildingType("GAPOWR");
    const auto* forbidden = live.rules.findBuildingType("YAREFN");
    if (!power || power->strength < 4 || !power->clickRepairable
        || !forbidden || forbidden->clickRepairable) return fail("stock repair fixtures unavailable");
    add(*repair, 1, WorldObjectKind::Structure, "GAPOWR", {8, 8});
    auto* object = findMutableObjectById(*repair, 1);
    object->health = power->strength / 2;
    const int initialHealth = object->health;
    repair->economy.credits = 1000;
    if (!repairRuntimeStructure(*repair, 1) || !repair->repairingStructureTicks.contains(1)
        || !repairRuntimeStructure(*repair, 1) || repair->repairingStructureTicks.contains(1))
        return fail("second wrench click did not cancel repair");
    repair->economy.credits = 0;
    if (!repairRuntimeStructure(*repair, 1)) return fail("cashless wrench did not retain a pending repair");
    const int interval = runtimeBuildingRepairInterval(repair->rules.getSpecialMechanismRules(), 30);
    for (int tick = 0; tick <= interval + 1; ++tick) {
        ++repair->tick;
        advanceRuntimeStructureRepairs(*repair);
    }
    if (findObjectById(*repair, 1)->health != initialHealth
        || !repair->repairingStructureTicks.contains(1) || repair->economy.credits != 0)
        return fail("cashless repair healed for free or discarded its pending state");
    depositRuntimeCredits(*repair, 100);
    ++repair->tick;
    advanceRuntimeStructureRepairs(*repair);
    if (findObjectById(*repair, 1)->health <= initialHealth || repair->economy.credits >= 100)
        return fail("repair did not resume and charge after receiving credits");
    if (!repairRuntimeStructure(*repair, 1) || repair->repairingStructureTicks.contains(1))
        return fail("resumed repair could not be cancelled");
    add(*repair, 2, WorldObjectKind::Structure, "YAREFN", {18, 18});
    if (runtimeStructureCanStartRepair(*repair, *findObjectById(*repair, 2))
        || repairRuntimeStructure(*repair, 2) || repair->repairingStructureTicks.contains(2))
        return fail("ClickRepairable=no was accepted by wrench validation");

    auto gift = setup();
    const auto* refinery = live.rules.findBuildingType("GAREFN");
    if (!refinery || refinery->freeUnit.empty()
        || !findRuntimeUnitTypeById(live.rules, refinery->freeUnit))
        return fail("stock refinery free-unit fixture unavailable");
    add(*gift, 1, WorldObjectKind::Structure, refinery->id, {10, 10});
    const auto footprint = runtimeStructureFootprintCellsForObject(*gift, *findObjectById(*gift, 1));
    int blockerId = 10;
    for (int y = 1; y <= 25; ++y) {
        for (int x = 1; x <= 25; ++x) {
            if (std::find(footprint.begin(), footprint.end(), RuntimeCellPoint{x, y}) != footprint.end()) continue;
            add(*gift, blockerId++, WorldObjectKind::Unit, "MTNK", {x, y});
        }
    }
    gift->structureFootprints = buildRuntimeStructureFootprints(*gift);
    refreshRuntimeObjectOccupancyAfterLocalChange(*gift);
    // Seed the receipt produced by the building-placement transaction; all
    // subsequent checks exercise the production runtime and host save validator.
    if (!gift->factoryDeliveries.deferFreeUnit({1, gift->economy.owner, refinery->id, refinery->freeUnit}))
        return fail("free-unit receipt was not accepted");
    const auto initialObjects = gift->world.getObjects().size();
    advanceRuntimeFreeUnitDeliveries(*gift);
    if (gift->world.getObjects().size() != initialObjects || gift->factoryDeliveries.pendingFreeUnits().size() != 1)
        return fail("sealed refinery overlapped a blocker or discarded its gift");
    std::ostringstream saved;
    gift->factoryDeliveries.save(saved);
    saved << "\n[Behaviors]\nCount=0\n";
    IniFile ini;
    RuntimeSavedProductionCandidate production;
    production.lanes = gift->economy.localProductionLanes;
    RuntimeFactoryDeliveryState restored;
    if (!ini.loadFromString(saved.str()) || !validateRuntimeSavedFactoryDeliveries(*gift, ini,
        14, gift->tick, production, gift->world.getObjects(), gift->repairDepots, restored, error)
        || restored.stateHash() != gift->factoryDeliveries.stateHash())
        return fail("blocked gift failed host save/load validation: " + error);
    gift->factoryDeliveries = std::move(restored);
    for (auto& blocker : gift->world.accessObjects()) {
        if (blocker.instanceId >= 10) {
            blocker.alive = false;
            blocker.health = 0;
        }
    }
    refreshRuntimeObjectOccupancyAfterLocalChange(*gift);
    const int cadence = std::max(1, gift->config.fixedTickHz / 4);
    gift->tick += cadence;
    advanceRuntimeFreeUnitDeliveries(*gift);
    const auto countGifts = [&]() {
        return std::count_if(gift->world.getObjects().begin(), gift->world.getObjects().end(),
            [&](const auto& candidate) {
                return candidate.alive && candidate.owner == gift->economy.owner
                    && candidate.type == refinery->freeUnit;
            });
    };
    if (countGifts() != 1 || !gift->factoryDeliveries.pendingFreeUnits().empty())
        return fail("unblocked restored refinery did not deliver exactly one unit");
    for (int repeat = 0; repeat < 4; ++repeat) {
        gift->tick += cadence;
        advanceRuntimeFreeUnitDeliveries(*gift);
    }
    if (countGifts() != 1) return fail("completed gift was delivered twice");
    std::cout << "Feature detail integration: repairCancel=yes cashPauseResume=yes clickRepairable=yes "
        "blockedGiftSaveLoad=yes singleDelivery=yes\n";
    return true;
}
