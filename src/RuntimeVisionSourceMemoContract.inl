// Exact old/new visibility comparison across changes that invalidate a source
// memo, including same-tick script mutation and reversible generated shroud.
static bool runRuntimeVisionSourceMemoContractCheck(std::string& errorMessage) {
    GameWorld world;
    for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) {
        TerrainCell cell;
        cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
        world.accessTerrainCells().push_back(cell);
    }
    IniFile ini;
    const std::string text =
        "[InfantryTypes]\n0=SCOUT\n[BuildingTypes]\n0=SPY\n1=GAP\n"
        "[General]\nVeteranSight=2\n[SCOUT]\nSight=3\nStrength=100\n"
        "VeteranAbilities=SIGHT\nEliteAbilities=SIGHT\n"
        "[SPY]\nSight=1\nSpySat=yes\nStrength=500\n"
        "[GAP]\nSight=1\nGapGenerator=yes\nGapRadiusInCells=4\nPowered=yes\nStrength=500\n";
    RulesDatabase rules;
    if (!ini.loadFromString(text) || !rules.loadFromIni(ini)) return false;
    for (int index = 0; index < 24; ++index) {
        WorldObjectInstance object;
        object.instanceId = index + 1;
        object.kind = WorldObjectKind::Infantry;
        object.type = "SCOUT";
        object.owner = index < 16 ? " Alpha " : "Beta";
        object.x = 5 + index % 6 * 4;
        object.y = 5 + index / 6 * 6;
        object.alive = true;
        world.accessObjects().push_back(object);
    }
    WorldObjectInstance spy = world.getObjects().front();
    spy.instanceId = 25; spy.type = "SPY"; spy.kind = WorldObjectKind::Structure;
    spy.alive = false; spy.poweredOperational = true;
    world.accessObjects().push_back(spy);
    WorldObjectInstance gap = spy;
    gap.instanceId = 26; gap.type = "GAP"; gap.owner = "Beta";
    gap.alive = true; gap.x = 20; gap.y = 20;
    world.accessObjects().push_back(gap);
    RuntimeVisionSystem memo, reference;
    reference.setSourceMemoEnabledForDiagnostics(false);
    std::vector<RuntimeVisionObserverDefinition> observers{
        {"Alpha", {"Alpha"}}, {"Beta", {"Beta"}}
    };
    const auto equal = [&](int tick) {
        const bool memoChanged = memo.update(world, &rules, observers, tick);
        const bool referenceChanged = reference.update(world, &rules, observers, tick);
        if (memoChanged != referenceChanged) return false;
        for (const std::string owner : {"Alpha", "Beta"}) {
            RuntimeVisionGridView actual, expected;
            if (!memo.gridViewFor(owner, actual) || !reference.gridViewFor(owner, expected)) return false;
            if (actual.cellCount != expected.cellCount || actual.revision != expected.revision
                || actual.layoutEpoch != expected.layoutEpoch) return false;
            if (std::memcmp(actual.validCells, expected.validCells, actual.cellCount) != 0
                || std::memcmp(actual.visibleCells, expected.visibleCells, actual.cellCount) != 0
                || std::memcmp(actual.exploredCells, expected.exploredCells, actual.cellCount) != 0
                || std::memcmp(actual.gapShroudedCells, expected.gapShroudedCells, actual.cellCount) != 0) return false;
            const auto* left = memo.statsFor(owner);
            const auto* right = reference.statsFor(owner);
            if (left->validCells != right->validCells || left->visibleCells != right->visibleCells
                || left->exploredCells != right->exploredCells || left->visionSources != right->visionSources
                || left->spySatSources != right->spySatSources || left->gapGeneratorSources != right->gapGeneratorSources
                || left->gapShroudedCells != right->gapShroudedCells || left->lastUpdateTick != right->lastUpdateTick)
                return false;
        }
        return memo.serializeFireReveals(tick) == reference.serializeFireReveals(tick);
    };
    WeaponType weapon;
    weapon.revealOnFire = true;
    for (int tick = 1; tick <= 240; ++tick) {
        auto& objects = world.accessObjects();
        if (tick % 9 == 0) {
            auto& mover = objects[static_cast<std::size_t>(tick / 9) % 24];
            mover.hasRenderPosition = true;
            mover.renderX = 4.0 + tick % 29 + 0.45;
            mover.renderY = 4.0 + tick % 23 + 0.55;
            mover.veterancy = (tick / 9) % 3;
        }
        if (tick == 20) objects[2].mission = "InsideTransport";
        if (tick == 25) objects[2].mission = "Guard";
        if (tick == 30) objects[3].transportPassengerIds = {objects[4].instanceId};
        if (tick == 35) objects[3].transportPassengerIds.clear();
        if (tick == 40 || tick == 70) {
            memo.revealArea("Alpha", 38, 38, 2);
            reference.revealArea("Alpha", 38, 38, 2);
        }
        if (tick == 45) {
            memo.resetShroudArea("Alpha", 5, 5, 4);
            reference.resetShroudArea("Alpha", 5, 5, 4);
        }
        if (tick == 50) { objects[24].alive = true; }
        if (tick == 60) { objects[24].poweredOperational = false; }
        if (tick == 65) { objects[25].poweredOperational = false; }
        if (tick == 75) { objects[25].poweredOperational = true; objects[25].x += 3; }
        if (tick == 80) { observers[0].sharedVisionOwners.push_back("Beta"); }
        if (tick == 90) { observers[0].sharedVisionOwners = {"Alpha"}; }
        if (tick == 95) {
            memo.resetShroud("Beta"); reference.resetShroud("Beta");
        }
        if (tick == 100) {
            memo.revealMap("Alpha"); reference.revealMap("Alpha");
        }
        if (tick == 105) {
            WorldObjectInstance attacker = objects[0], target = objects[16];
            attacker.hasRenderPosition = false; attacker.x = 38; attacker.y = 38;
            if (memo.notifyWeaponFired(attacker, target, weapon, tick, 15)
                != reference.notifyWeaponFired(attacker, target, weapon, tick, 15)) return false;
        }
        if (tick == 110) {
            const auto saved = memo.serializeFireReveals(tick);
            if (!memo.restoreFireReveals(saved, tick, 15)
                || !reference.restoreFireReveals(saved, tick, 15)) return false;
        }
        if (tick == 115) { std::reverse(objects.begin(), objects.end()); }
        if (tick == 120) { world.accessTerrainCells()[0].x = 41; }
        if (tick == 130) { objects[0].alive = false; }
        if (tick == 140) {
            memo.restoreExploredCellIndices("Alpha", {39 * 512 + 39});
            reference.restoreExploredCellIndices("Alpha", {39 * 512 + 39});
        }
        if (tick == 150) {
            RuntimeVisionSystem copy = memo; memo.swap(copy);
            RuntimeVisionSystem referenceCopy = reference; reference.swap(referenceCopy);
        }
        if (tick == 160) { observers.pop_back(); }
        if (tick == 170) { observers.push_back({"Beta", {"Beta"}}); }
        if (tick == 190) {
            memo.exploreArea("Alpha", 38, 2, 3);
            reference.exploreArea("Alpha", 38, 2, 3);
        }
        if (tick == 200) {
            std::string changedRules = text;
            const auto sight = changedRules.find("[SCOUT]\nSight=3");
            changedRules.replace(sight, std::strlen("[SCOUT]\nSight=3"), "[SCOUT]\nSight=5");
            if (!ini.loadFromString(changedRules) || !rules.loadFromIni(ini)) return false;
        }
        if (!equal(tick) || !equal(tick)) {
            errorMessage = "exact visibility/source memo mismatch at tick " + std::to_string(tick);
            return false;
        }
        if (tick % 13 == 0) {
            // An authoritative script/transport action can change sight after
            // the first update without advancing the simulation tick.
            world.accessObjects()[1].hasRenderPosition = false;
            world.accessObjects()[1].x = 2 + tick % 34;
            memo.resetShroudArea("Alpha", 35, 35, 2);
            reference.resetShroudArea("Alpha", 35, 35, 2);
            if (!equal(tick)) {
                errorMessage = "same-tick source/script memo mismatch at tick " + std::to_string(tick);
                return false;
            }
        }
    }
    if (memo.sourceMemoHitsForDiagnostics() == 0) {
        errorMessage = "source memo reference comparison never exercised a hit";
        return false;
    }
    errorMessage.clear();
    return true;
}
