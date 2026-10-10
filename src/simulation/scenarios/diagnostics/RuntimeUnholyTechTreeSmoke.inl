bool runRuntimeUnholyTechTreeSmoke(RuntimeWindowState& state, std::string& error) {
    const std::string country = runtimeCountryForOwner(state, state.economy.owner);
    const auto fail = [&](const std::string& reason) {
        error = country + " technology: " + reason;
        return false;
    };
    const auto itemById = [&](const std::string& id) {
        return findRuntimeGameplayCatalogItem(state, state.economy.owner, id);
    };
    const auto expect = [&](const std::string& id, bool unlocked) {
        const auto* item = itemById(id);
        if (item == nullptr) return fail("catalog is missing " + id);
        const auto decision = evaluateRuntimeTechTreeDecision(state, *item);
        if (decision.canQueue != unlocked
            || runtimeBuildItemUnlockedForSidebar(state, *item) != unlocked)
            return fail(id + (unlocked ? " must unlock: " : " unlocked early: ")
                + decision.reason);
        return true;
    };
    const auto build = [&](const std::string& id) {
        const auto* catalogItem = itemById(id);
        if (catalogItem == nullptr) return fail("catalog is missing " + id);
        const RuntimeBuildCatalogItem item = *catalogItem;
        if (!queueRuntimeBuildOrder(state, item)) return fail(state.economy.status);
        ++state.tick;
        advanceRuntimeProductionEconomy(state);
        if (!completeRuntimeActiveProductionForProbe(state, item))
            return fail(id + " did not start production");
        ++state.tick;
        advanceRuntimeProductionEconomy(state);
        const auto ready = runtimeOldestReadyPlacement(state);
        if (!ready.has_value() || ready->item == nullptr || ready->item->typeId != id)
            return fail(id + " did not become ready to place");
        if (runtimeSelectedPlacement(state).has_value()
            || !beginRuntimePlacementFromUi(state, *ready, true))
            return fail(id + " was auto-selected or sidebar selection failed");
        const auto cell = findRuntimePlacementCellNearBase(state, item);
        if (!cell.has_value() || !placeRuntimeReadyBuilding(state, *cell))
            return fail(id + " could not be placed: " + state.economy.status);
        for (int tick = 0; tick < 240; ++tick) {
            ++state.tick;
            advanceRuntimeBuildAnimations(state);
        }
        return runtimeHasOwnedBuilding(state, id) || fail(id + " was not created");
    };
    struct Chain {
        const char* yard;
        const char* power;
        const char* refinery;
        const char* barracks;
        const char* factory;
        const char* radar;
        const char* lab;
        const char* infantry;
        const char* tank;
        const char* advanced;
        const char* superweapon;
    };
    const Chain chains[] = {
        {"GACNST", "GAPOWR", "GAREFN", "GAPILE", "GAWEAP",
            country == "Americans" ? "AMRADR" : "GAAIRC", "GATECH",
            "E1", "MTNK", "SREF", "GAWEAT"},
        {"NACNST", "NAPOWR", "NAREFN", "NAHAND", "NAWEAP", "NARADR", "NATECH",
            "E2", "HTNK", "APOC", "NAMISL"},
        {"YACNST", "YAPOWR", "YAREFN", "YABRCK", "YAWEAP", "NAPSIS", "YATECH",
            "INIT", "LTNK", "MIND", "YAPPET"}
    };
    for (const auto& chain : chains) {
        if (!expect(chain.power, runtimeHasOwnedBuilding(state, chain.yard))) return false;
        for (const char* id : {chain.refinery, chain.barracks, chain.factory,
                chain.lab, chain.infantry, chain.tank, chain.advanced, chain.superweapon})
            if (!expect(id, false)) return false;
    }
    // These ordinary Allied types are absent from MPUnholyMD.ini. The full
    // technology tree must still contain them for Soviet and Yuri players.
    for (const char* id : {"FV", "CLEG", "DEST"})
        if (!expect(id, false)) return false;

    std::vector<int> mcvIds;
    for (const auto& object : state.world.getObjects()) {
        if (object.alive && object.owner == state.economy.owner
            && object.kind == WorldObjectKind::Unit) {
            const auto* unit = findRuntimeUnitType(state.rules, object);
            if (unit != nullptr && runtimeUnitTypeIsMcv(state, *unit))
                mcvIds.push_back(object.instanceId);
        }
    }
    for (int id : mcvIds)
        if (!deployRuntimeMcv(state, id, false)) return fail(state.economy.status);
    for (const auto& chain : chains)
        if (!expect(chain.power, true)) return false;
    for (const auto& chain : chains)
        if (!build(chain.power)) return false;
    for (const auto& chain : chains) {
        if (!expect(chain.refinery, true) || !expect(chain.barracks, true)
            || !expect(chain.factory, false) || !build(chain.refinery)) return false;
    }
    for (const auto& chain : chains) {
        if (!build(chain.barracks) || !expect(chain.infantry, true)
            || !expect(chain.factory, true) || !expect(chain.tank, false)) return false;
    }
    for (const auto& chain : chains) {
        if (!build(chain.factory) || !expect(chain.tank, true)
            || !expect(chain.advanced, false) || !build(chain.radar)) return false;
    }
    if (!expect("JUMPJET", true) || !expect("GHOST", true)) return false;
    for (const auto& chain : chains)
        if (!expect(chain.lab, true) || !expect(chain.advanced, false)
            || !expect(chain.superweapon, false)) return false;
    for (std::size_t side = 0; side < std::size(chains); ++side) {
        const auto& chain = chains[side];
        if (!build(chain.lab)) return false;
        for (std::size_t other = 0; other < std::size(chains); ++other) {
            if (!expect(chains[other].advanced, other <= side)
                || !expect(chains[other].superweapon, other <= side)) return false;
        }
        const auto* advanced = itemById(chain.advanced);
        const auto factories = findRuntimeFactoryAnchorCandidatesForOwner(
            state, *advanced, state.economy.owner, false);
        if (factories.empty() || std::any_of(factories.begin(), factories.end(),
                [&](const auto* factory) { return factory->type != chain.factory; }))
            return fail(std::string(chain.advanced) + " selected the wrong faction factory");
        for (auto& object : state.world.accessObjects()) {
            if (object.alive && object.owner == state.economy.owner && object.type == chain.lab) {
                const std::string owner = object.owner;
                object.owner = runtimeSkirmishOwnerForSlot(1);
                const bool relocked = expect(chain.advanced, false)
                    && expect(chain.superweapon, false);
                object.owner = owner;
                if (!relocked || !expect(chain.advanced, true)) return false;
                break;
            }
        }
    }
    if (!expect("FV", true) || !expect("CLEG", true)) return false;
    std::cout << "Unholy technology: " << country
        << "; three deployed yards; 18 buildings queued/placed; separate labs,"
           " correct factories, lost prerequisites and complete catalog passed\n";
    return true;
}
