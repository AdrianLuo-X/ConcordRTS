bool runRuntimeIdleVariantContract(std::string& error) {
    const auto fail = [&](const std::string& why) {
        error = "idle variant equivalence: " + why;
        return false;
    };
    const auto prepare = [&](RuntimeWindowState& state) {
        state.config.fixedTickHz = 30;
        state.runtimePreferencePersistenceEnabled = false;
        state.runtimeSoundsEnabled = false;
        state.economy.owner = "Player";
        IniFile art;
        if (!art.loadFromString("[BOTH]\nImage=BOTH\n[ONE]\nImage=ONE\n"
            "[NONE]\nImage=NONE\n[UNSAFE]\nImage=UNSAFE\n[SHORT]\nImage=SHORT\n"
            "[BOTHA]\nImage=BOTHA\n")) return false;
        if (!state.art.loadFromIni(art)) return false;
        state.artLoaded = true;
        const auto set = [&](const char* id, std::vector<ArtType::SequenceFrame> frames) {
            auto* entry = const_cast<ArtType*>(state.art.findArtType(id));
            if (!entry) return false;
            entry->sequenceFrames = std::move(frames);
            return true;
        };
        if (!set("BOTH", {{"Idle1",true,0,8,0},{"Idle2",true,16,8,0}})
            || !set("ONE", {{"iDlE1",true,0,8,0},{"Idle2",false,16,8,0}})
            || !set("NONE", {})
            || !set("UNSAFE", {{"Idle1",true,0,8,0},{"Crawl",true,4,8,0},
                {"Idle2",true,32,8,0}})
            || !set("SHORT", {{"Idle1",true,0,3,0},{"Idle2",true,16,1,0}})
            || !set("BOTHA", {{"Idle2",true,16,8,0}})) return false;
        IniFile rules;
        if (!rules.loadFromString("[InfantryTypes]\n0=ALIAS\n[ALIAS]\n"
            "Strength=100\nSpeed=5\nImage=BOTH\nAlternateArcticArt=yes\n")
            || !state.rules.loadFromIni(rules)) return false;
        state.rulesLoaded = true;
        state.report.map.theater = "TEMPERATE";
        // No TMP/resource loading: explicit cell facts exercise the existing
        // fallback for water, shore and land, independently of art identity.
        for (int x = 0; x < 8; ++x) {
            MapCellFacts fact;
            fact.x = x; fact.y = 1;
            // MapFacts uses the map's 512-column index. runtimeCellKey is the
            // separate 1000-column runtime navigation key and cannot index it.
            fact.cellIndex = fact.y * 512 + fact.x;
            fact.isWater = x == 1 || x == 2; fact.isShore = x == 2;
            state.mapFacts.cellIndexLookup.emplace(fact.cellIndex, state.mapFacts.cells.size());
            state.mapFacts.cells.push_back(fact);
        }
        const char* types[] = {"BOTH","ONE","NONE","UNSAFE","SHORT","MISSING","ALIAS"};
        for (int id = 0; id < 84; ++id) {
            WorldObjectInstance object;
            object.instanceId = id; object.type = types[id % 7];
            object.kind = id % 2 ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
            object.owner = "Player"; object.alive = true; object.health = 100;
            object.x = id % 8; object.y = 1; object.cell = runtimeCellKey(object.x, object.y);
            object.mission = id % 3 ? "Guard" : "Idle1";
            object.onBridge = id % 13 == 0; object.airborne = id % 17 == 0;
            state.world.accessObjects().push_back(object);
        }
        auto& objects = state.world.accessObjects();
        objects[1].type = "BOTH"; // water, no bridge
        objects[3].type = "BOTH"; objects[3].x = 2; // shore fallback stays on land
        objects[3].cell = runtimeCellKey(2, 1);
        objects[5].type = "BOTH"; objects[5].x = 1; objects[5].onBridge = true;
        objects[5].cell = runtimeCellKey(1, 1);
        objects[7].deployed = true;
        objects[8].hasRenderPosition = true;
        objects[9].alive = false;
        objects[10].mission = "Attack";
        objects[11].mission = "Deploying";
        state.behaviors[12].kind = RuntimeBehaviorKind::Move;
        state.behaviors[13].returnToDeployed = true;
        return true;
    };
    auto optimized = std::make_unique<RuntimeWindowState>();
    auto reference = std::make_unique<RuntimeWindowState>();
    if (!prepare(*optimized) || !prepare(*reference)) return fail("fixture failed");
    for (RuntimeWindowState* state : {optimized.get(), reference.get()}) {
        auto context = runtimeTerrainMetadataContext(*state);
        context.mapFacts = &state->mapFacts;
        const auto& objects = state->world.getObjects();
        const auto* water = MapQuery(state->mapFacts).getCell(objects[1].x, objects[1].y);
        const auto* shore = MapQuery(state->mapFacts).getCell(objects[3].x, objects[3].y);
        const auto* bridgeWater = MapQuery(state->mapFacts).getCell(objects[5].x, objects[5].y);
        if (!water || !water->isWater || water->isShore
            || !shore || !shore->isWater || !shore->isShore
            || bridgeWater != water || !objects[5].onBridge)
            return fail("water/shore/bridge MapQuery fixture classification");
        if (!resolveRuntimeInfantrySwimming(objects[1], context, state->renderCache)
            || resolveRuntimeInfantrySwimming(objects[3], context, state->renderCache)
            || resolveRuntimeInfantrySwimming(objects[5], context, state->renderCache))
            return fail("water/shore/bridge actual swimming classification");
    }
    std::size_t selections = 0, passes = 0, bridgeVariants = 0, shoreVariants = 0;
    for (int hz : {15,30,60}) {
        optimized->config.fixedTickHz = reference->config.fixedTickHz = hz;
        for (int tick : {-5,0,1,17,18,29,30,89,90,179,180,359,360,719}) {
            optimized->tick = tick;
            RuntimeIdleVariantPass pass;
            const auto before = optimized->world.deterministicSimulationStateHash();
            for (const auto& source : optimized->world.getObjects()) {
                for (int id : {-1,0,1,2,7,31,127,255}) {
                    auto object = source; object.instanceId = id;
                    const auto expected = runtimeIdleVariantMissionForObject(*optimized, object, true);
                    const auto actual = runtimeIdleVariantMissionForObject(*optimized, object, false, &pass);
                    if (expected != actual) return fail("selector hz=" + std::to_string(hz)
                        + " tick=" + std::to_string(tick) + " id=" + std::to_string(id)
                        + " type=" + object.type + " expected=" + expected + " actual=" + actual);
                    ++selections;
                }
            }
            if (before != optimized->world.deterministicSimulationStateHash())
                return fail("selector changed world");
            if (!runtimeFrameScalingReferenceMovement() && pass.safetyEvaluations > 6)
                return fail("repeated art safety evaluation within a pass");
        }
        for (int tick = 1; tick <= hz * 12; ++tick) {
            optimized->tick = reference->tick = tick;
            bool actual = false, expected = false;
            { RuntimeSnapshotActiveStateScope scope(*reference);
              expected = advanceRuntimeIdleVariants(*reference, true); }
            { RuntimeSnapshotActiveStateScope scope(*optimized);
              actual = advanceRuntimeIdleVariants(*optimized, false); }
            if (actual != expected
                || optimized->world.deterministicSimulationStateHash()
                    != reference->world.deterministicSimulationStateHash()
                || optimized->nextReplayEventSequence != reference->nextReplayEventSequence
                || optimized->recentReplayEvents.size() != reference->recentReplayEvents.size())
                return fail("update/state hz=" + std::to_string(hz) + " tick=" + std::to_string(tick));
            const auto isVariant = [](const WorldObjectInstance& object) {
                return object.mission == "Idle1" || object.mission == "Idle2";
            };
            if (isVariant(optimized->world.getObjects()[1]))
                return fail("resting water infantry played land idle at hz="
                    + std::to_string(hz) + " tick=" + std::to_string(tick));
            bridgeVariants += isVariant(optimized->world.getObjects()[5]) ? 1 : 0;
            shoreVariants += isVariant(optimized->world.getObjects()[3]) ? 1 : 0;
            for (std::size_t i = 0; i < optimized->recentReplayEvents.size(); ++i) {
                const auto& a = optimized->recentReplayEvents[i];
                const auto& b = reference->recentReplayEvents[i];
                if (a.sequence != b.sequence || a.tick != b.tick || a.kind != b.kind
                    || a.objectId != b.objectId || a.missionFrom != b.missionFrom
                    || a.missionTo != b.missionTo || a.reason != b.reason
                    || a.renderIntentId != b.renderIntentId || a.renderIntentHash != b.renderIntentHash)
                    return fail("replay transition differs");
            }
            ++passes;
        }
    }
    if (bridgeVariants == 0 || shoreVariants == 0) return fail("bridge/shore variants not exercised");
    auto probe = optimized->world.getObjects().front();
    probe.type = "BOTH"; probe.instanceId = 0; probe.deployed = false;
    optimized->tick = 60; optimized->config.fixedTickHz = 30;
    RuntimeIdleVariantPass inactive;
    if (!runtimeIdleVariantMissionForObject(*optimized, probe, false, &inactive).empty()
        || inactive.safetyEvaluations != 0) return fail("inactive phase did art safety work");
    optimized->tick = 0;
    RuntimeIdleVariantPass active;
    for (int i = 0; i < 32; ++i)
        if (runtimeIdleVariantMissionForObject(*optimized, probe, false, &active) != "Idle1")
            return fail("active phase selection");
    if (!runtimeFrameScalingReferenceMovement()
        && (active.safetyEvaluations != 1 || active.cacheHits != 31))
        return fail("shared art work bound");
    auto* art = const_cast<ArtType*>(optimized->art.findArtType("BOTH"));
    art->sequenceFrames = {{"Idle2",true,32,8,0}};
    RuntimeIdleVariantPass fresh;
    if (runtimeIdleVariantMissionForObject(*optimized, probe, false, &fresh) != "Idle2")
        return fail("new pass did not see changed sequence");
    probe.kind = WorldObjectKind::Infantry; probe.type = "ALIAS";
    optimized->report.map.theater = "SNOW";
    RuntimeIdleVariantPass winter;
    if (runtimeArtTypeForObject(*optimized, probe) != optimized->art.findArtType("BOTHA")
        || runtimeIdleVariantMissionForObject(*optimized, probe, false, &winter) != "Idle2"
        || runtimeIdleVariantMissionForObject(*optimized, probe, true) != "Idle2")
        return fail("art alias/theater selection");
    // Guard the original individual arithmetic operations, not just their sum.
    if (runtimeSafeIdleVariantPhase(0, std::numeric_limits<int>::max(), 0)
        || runtimeSafeIdleVariantPhase(0, 30, std::numeric_limits<int>::max())
        || runtimeSafeIdleVariantPhase(std::numeric_limits<int>::max(), 30, 1)
        || !runtimeSafeIdleVariantPhase(-5, 30, -1)) return fail("phase overflow guard");
    probe.type = "MISSING"; probe.instanceId = std::numeric_limits<int>::max();
    optimized->config.fixedTickHz = std::numeric_limits<int>::max();
    if (!runtimeIdleVariantMissionForObject(*optimized, probe).empty())
        return fail("abnormal input did not preserve missing-art early return");
    std::cout << "Idle variant equivalence passed: selections=" << selections
        << " updateTicks=" << passes
        << " sharedArt/phase/water/shore/bridge/deployed/replay/overflow=yes\n";
    return true;
}
