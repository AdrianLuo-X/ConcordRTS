bool runRuntimeModeNavalProductionSmoke(RuntimeWindowState& state, std::string& error) {
    struct RestoreAiFlag {
        bool& flag;
        bool previous;
        ~RestoreAiFlag() { flag = previous; }
    } restoreAi{state.config.computerAiEnabled, state.config.computerAiEnabled};
    state.config.computerAiEnabled = false;
    RuntimeComputerAiState ai;
    ai.initialized = true;
    ai.owner = state.economy.owner;
    ai.country = runtimeCountryForOwner(state, ai.owner);
    ai.baseCenter = state.economy.skirmishStartCell;
    ai.credits = 200000;
    const auto profile = runtimeComputerAiProfile(state, ai);
    const auto shipCount = [&] {
        int ships = 0;
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || object.owner != ai.owner || object.kind != WorldObjectKind::Unit) continue;
            const auto* unit = findRuntimeUnitType(state.rules, object);
            if (unit && unit->naval && !unit->primaryWeapon.empty()) ++ships;
        }
        return ships;
    };
    // The authored defense plan can contain 20-25 structures plus power and
    // refinery expansion. A fixed 28 decisions assumed the old three defenses.
    const auto baseTargets = runtimeComputerAiBaseTargets(state, ai);
    const int maximumDecisions = 32 + baseTargets.defenses * 2 + baseTargets.refineries * 3;
    for (int step = 0; step < maximumDecisions && shipCount() == 0; ++step) {
        state.tick = std::max(state.tick + 1, ai.nextBuildTick);
        const auto metrics = runtimeComputerAiMetrics(state, ai);
        advanceRuntimeComputerAiProduction(state, ai, profile, metrics);
        for (int tick = 0; tick < 240; ++tick) {
            ++state.tick;
            advanceRuntimeSimulation(state);
        }
    }
    const auto metrics = runtimeComputerAiMetrics(state, ai);
    if (metrics.shipyards == 0 || shipCount() == 0 || ai.credits >= 200000) {
        error = "naval AI failed to build/pay for its shipyard and fleet: " + ai.status
            + " yards=" + std::to_string(metrics.shipyards)
            + " miners=" + std::to_string(metrics.harvesters)
            + " refineries=" + std::to_string(metrics.refineries);
        return false;
    }
    const auto ship = runtimeComputerAiSelectUnitForRole(state, ai, RuntimeComputerAiUnitRole::Naval);
    const auto* anchor = ship ? runtimeComputerAiFactoryAnchor(state, ai, *ship) : nullptr;
    const auto* building = anchor ? findRuntimeBuildingTypeById(state.rules, anchor->type) : nullptr;
    if (!building || !building->naval) {
        error = "naval AI selected a land factory for a ship";
        return false;
    }
    std::cout << "Naval computer built shipyard and " << shipCount() << " ship(s): " << ai.country << '\n';
    return true;
}

bool runRuntimeModeOilEconomySmoke(RuntimeWindowState& state, std::string& error) {
    ensureRuntimeComputerAiStates(state);
    auto& ai = state.computerAiByOwner.at("skirmishplayer2");
    const auto selected = state.selectedObjectIds;
    const auto uiStatus = state.economy.status;
    if (!advanceRuntimeComputerAiOilCapture(state, ai)
        || state.selectedObjectIds != selected || state.economy.status != uiStatus) {
        error = "Megawealth AI did not send its starting engineers without changing player controls";
        return false;
    }
    int engineerId = -1;
    int targetId = -1;
    for (const auto& [id, target] : state.pendingEngineerCaptures) {
        const auto* engineer = findObjectById(state, id);
        if (engineer && engineer->owner == ai.owner) { engineerId = id; targetId = target; break; }
    }
    auto* engineer = findMutableObjectById(state, engineerId);
    const auto* target = findObjectById(state, targetId);
    const auto* oil = target ? findRuntimeBuildingTypeById(state.rules, target->type) : nullptr;
    if (!engineer || !target || !oil) { error = "missing oil capture order"; return false; }
    const std::string aiOwner = ai.owner;
    const int startingCash = ai.credits;
    const int captureCash = oil->produceCashStartup;
    const int periodicCash = oil->produceCashAmount;
    // The walking/entry path has its own integration coverage. Complete this
    // retained real AI order at the entrance to exercise ownership and money.
    const auto entrance = state.behaviors.at(engineerId).moveTarget;
    engineer->x = entrance.x;
    engineer->y = entrance.y;
    engineer->cell = runtimeCellKey(entrance.x, entrance.y);
    engineer->hasRenderPosition = false;
    engineer->renderMoveActive = false;
    if (!completeRuntimeEngineerCapture(state, engineerId)) {
        error = "AI engineer could not finish its real capture order";
        return false;
    }
    // Owner transitions can replace backing state. Reacquire all references.
    const auto* captured = findObjectById(state, targetId);
    if (!captured || captured->owner != aiOwner
        || state.computerAiByOwner.at("skirmishplayer2").credits != startingCash + captureCash) {
        error = "oil capture ownership or initial payment went to the wrong player";
        return false;
    }
    const int nextPayment = captured->specialCashNextTick;
    auto& liveAi = state.computerAiByOwner.at("skirmishplayer2");
    const int beforeIncome = liveAi.credits;
    advanceRuntimeComputerMining(state);
    if (state.computerAiByOwner.at("skirmishplayer2").credits != beforeIncome || nextPayment <= state.tick) {
        error = "Megawealth gained ordinary mine income or lost its oil timer";
        return false;
    }
    state.tick = nextPayment;
    advanceRuntimeSpecialBuildings(state);
    if (state.computerAiByOwner.at("skirmishplayer2").credits != beforeIncome + periodicCash) {
        error = "Megawealth oil did not pay recurring income";
        return false;
    }
    std::cout << "Megawealth computer captured oil; initial and recurring credits passed\n";
    return true;
}
