bool runRuntimeMagnetronSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& text) { error = "Magnetron: " + text; return false; };
    auto live = std::make_unique<RuntimeWindowState>();
    live->config = config;
    live->report = buildRuntimeStartupReport(config, &live->world, &live->theater,
        &live->art, &live->rules, &live->mapFacts);
    if (!live->report.success) return fail("original resources unavailable");
    const auto make = [&](const std::string& type, WorldObjectKind kind, bool water, int hz) {
        auto p = std::make_unique<RuntimeWindowState>();
        auto& s = *p;
        s.config = config; s.config.fixedTickHz = hz; s.config.computerAiEnabled = false;
        s.rules = live->rules; s.art = live->art; s.theater = live->theater; s.report = live->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = s.mapFactsLoaded = true;
        s.economy.owner = "YuriCountry"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.visionEnabled = false;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false;
        s.world = live->world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 1; y <= 26; ++y) for (int x = 1; x <= 30; ++x) {
            const int key = runtimeCellKey(x, y);
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = key;
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
            RuntimeTerrainInfo info; info.water = water && x >= 8; s.terrainInfoByCell.emplace(key, info);
            MapCellFacts facts; facts.x = x; facts.y = y; facts.cellIndex = y * 512 + x;
            facts.hasTerrain = true; facts.isWater = info.water; facts.heightLevel = 0;
            facts.terrainClass = info.water ? "water" : "land"; facts.passabilityHint = "passable";
            s.mapFacts.cellIndexLookup.emplace(facts.cellIndex, s.mapFacts.cells.size());
            s.mapFacts.cells.push_back(facts);
        }
        WorldObjectInstance source;
        source.instanceId = 1; source.kind = WorldObjectKind::Unit; source.type = "TELE";
        source.owner = "YuriCountry"; source.alive = true; source.health = 150;
        source.x = 4; source.y = 14; source.cell = runtimeCellKey(4,14);
        source.mission = "Guard"; source.currentAmmo = -1; source.poweredOperational = true;
        source.facing = source.turretFacing = runtimeFacingFromDelta(8, 0);
        auto target = source; target.instanceId = 2; target.kind = kind; target.type = type;
        target.owner = "Americans"; target.x = 12; target.cell = runtimeCellKey(12,14);
        target.mission = "Sleep"; target.health = runtimeMaxHealthForObject(s, target);
        target.weaponCooldownTicksRemaining = 100000;
        s.world.accessObjects() = {source, target};
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        ensureRuntimeSpecialMechanismsInitialized(s);
        // Establish real ship parent/child slots before acquiring the target.
        advanceRuntimeSpecialMechanisms(s);
        s.selectedObjectIds = {1}; issueRuntimeAttackCommand(s, *findObjectById(s, 2));
        return p;
    };
    const auto step = [](RuntimeWindowState& s) {
        ++s.tick; advanceRuntimeWeaponCooldowns(s); advanceRuntimeCombatBursts(s);
        const auto b = s.behaviors.find(1);
        if (b != s.behaviors.end() && b->second.kind == RuntimeBehaviorKind::Attack)
            advanceRuntimeAttackBehavior(s, *findMutableObjectById(s, 1), b->second);
        advanceRuntimeEffects(s);
    };
    std::vector<WorldObjectInstance> previewObjects;
    std::vector<SpecialMechanismRenderSnapshot> previewBeams;
    int cases = 0, continuousFrames = 0, continuousSaves = 0, clearedLinks = 0;
    std::string collisionFailure;
    for (int hz : {30, 60}) for (const std::string type : {
            "HTNK", "DEST", "DRED", "CARRIER", "BSUB", "SUB", "AEGIS", "LCRF", "DLPH", "SQD"}) {
        auto p = make(type, WorldObjectKind::Unit, type != "HTNK", hz);
        auto& s = *p;
        if (s.lastCommand.acceptedCount != 1) return fail(type + " order rejected: " + s.simulationStatus);
        const int health = findObjectById(s, 2)->health;
        bool lifted = false;
        for (int i = 0; i < hz * 5; ++i) {
            step(s);
            const auto* binding = runtimeMagBeamBindingForTarget(s, 2);
            if (binding && binding->kinematics.worldZ >= 2.9
                && binding->kinematics.worldX < 11.0) { lifted = true; break; }
        }
        if (!lifted) {
            std::ostringstream diagnostic; s.magBeams.save(diagnostic);
            const auto* object = findRuntimeObjectByIdIncludingInactive(s, 2);
            const auto behavior = s.behaviors.find(1);
            diagnostic << "\nHP=" << (object ? object->health : -1)
                << " submerged=" << (object && object->submerged) << " cloaked=" << (object && object->cloaked)
                << " targetMission=" << (object ? object->mission : "missing")
                << " attackerBehavior=" << (behavior == s.behaviors.end() ? -1 : static_cast<int>(behavior->second.state));
            return fail(type + " did not lift and move: " + s.simulationStatus + diagnostic.str());
        }
        const auto* target = findObjectById(s, 2);
        if (!target || target->health != health || !target->airborne || target->cloaked
            || target->submerged || s.behaviors.contains(2))
            return fail(type + " lift changed health or kept a competing move order");
        refreshRuntimeSpecialMechanismRenderSnapshots(s);
        const auto beam = std::find_if(s.specialMechanismRenderSnapshots.begin(), s.specialMechanismRenderSnapshots.end(),
            [](const auto& snapshot) { return snapshot.kind == "magBeam"; });
        if (beam == s.specialMechanismRenderSnapshots.end() || !beam->hasBeamEndpoints
            || beam->targetLevel < 2.9) return fail(type + " beam did not follow lifted pose");
        if (hz == 30 && (type == "HTNK" || type == "DRED" || type == "CARRIER")) {
            previewObjects.push_back(*findObjectById(s, 1)); previewObjects.push_back(*target);
            previewBeams.push_back(*beam);
        }
        const auto beforeSave = s.magBeams.stateHash();
        std::ostringstream save; s.magBeams.save(save);
        IniFile saved; RuntimeMagBeamSystemState restored;
        if (!saved.loadFromString(save.str()) || !restored.load(saved, &error)
            || restored.stateHash() != beforeSave) return fail(type + " carry save/resume failed: " + error);
        s.magBeams = std::move(restored);
        issueRuntimeStopCommand(s);
        for (int i = 0; i < hz * 4 && !s.magBeams.bindings().empty(); ++i) step(s);
        target = findObjectById(s, 2);
        if (!s.magBeams.bindings().empty() || !target || !target->alive || target->health != health
            || target->airborne || target->x >= 12 || target->mission == "MagBeamSuspended")
            return fail(type + " legal empty landing killed or stranded the unit: " + s.simulationStatus);
        refreshRuntimeSpecialMechanismRenderSnapshots(s);
        if (std::any_of(s.specialMechanismRenderSnapshots.begin(), s.specialMechanismRenderSnapshots.end(),
            [](const auto& v) { return v.kind == "magBeam"; })) return fail("released beam persisted");
        if (const auto* unit = s.rules.findUnitType("Vehicle", type); unit && !unit->spawns.empty()) {
            const auto parent = s.specialMechanisms.carrierParents().find(2);
            if (parent == s.specialMechanisms.carrierParents().end()) return fail(type + " lost its children on landing");
        }
        std::cout << "Magnetron " << type << " hz=" << hz << " lift/move/empty-land/HP/children/save=ok\n";
        ++cases;
    }
    for (int hz : {15,30,60}) {
        auto p = make("HTNK", WorldObjectKind::Unit, false, hz);
        auto& s = *p;
        for (int i=0; i<hz*5; ++i) {
            step(s);
            const auto* binding = runtimeMagBeamBindingForTarget(s,2);
            if (binding && binding->kinematics.worldZ >= 2.9) break;
        }
        const auto* held = findObjectById(s, 2);
        if (!held || !runtimeMagBeamBindingForTarget(s,2)) return fail("collision lift failed");
        WorldObjectInstance building = *held;
        building.instanceId = 100; building.type = "NAPOWR"; building.kind = WorldObjectKind::Structure;
        building.x = held->x - 1; building.y = held->y - 1;
        building.cell = runtimeCellKey(building.x, building.y); building.level = 0;
        building.airborne = false; building.flightLevel = 0; building.hasRenderPosition = building.renderMoveActive = false;
        building.mission = "Guard"; building.health = runtimeMaxHealthForObject(s, building);
        s.world.accessObjects().push_back(building); invalidateRuntimeTickOccupancyCache(s);
        issueRuntimeStopCommand(s);
        for(int i=0; i<hz*4 && !s.magBeams.bindings().empty(); ++i) step(s);
        const auto* crushed = findRuntimeObjectByIdIncludingInactive(s,2);
        const auto* damaged = findRuntimeObjectByIdIncludingInactive(s,100);
        if (!s.magBeams.bindings().empty() || !crushed || crushed->alive
            || !damaged || damaged->health >= building.health) {
            collisionFailure = "building collision failed at hz=" + std::to_string(hz)
                + " remaining=" + std::to_string(s.magBeams.bindings().size())
                + " tankHP=" + std::to_string(crushed ? crushed->health : -1)
                + " tankAlive=" + std::to_string(crushed && crushed->alive)
                + " land=" + std::to_string(crushed ? crushed->x : -1) + "," + std::to_string(crushed ? crushed->y : -1)
                + " building=" + std::to_string(building.x) + "," + std::to_string(building.y)
                + " buildingHP=" + std::to_string(damaged ? damaged->health : -1) + "/" + std::to_string(building.health);
            std::cout << collisionFailure << '\n';
        }
        ++cases;
        auto b = make("NAPOWR", WorldObjectKind::Structure, false, hz);
        auto& direct = *b;
        RuntimeSnapshotActiveStateScope active(direct);
        const int hp = findObjectById(direct,2)->health;
        bool hit = false;
        for(int i=0; i<hz*4; ++i) {
            step(direct);
            if (findObjectById(direct,2)->health < hp) { hit = true; break; }
        }
        if (!hit || !direct.magBeams.bindings().empty()) return fail("building did not take normal damage");
        const auto visual = std::find_if(direct.projectiles.begin(), direct.projectiles.end(),
            [](const auto& v) { return v.renderIntent == "magBeamVisual"; });
        if (visual == direct.projectiles.end()) return fail("building hit lacked its purple beam");
        const int linkId = visual->actorId;
        const int firstDamage = hp - findObjectById(direct,2)->health;
        const auto* shake = direct.rules.findWeaponType("MagneShake");
        if (!shake || firstDamage <= 0) return fail("native building damage setup");
        const int period = runtimeWeaponCooldownTicks(direct,*findObjectById(direct,1),*shake);
        int lastHit = direct.tick, damageCount = 1, previousHealth = findObjectById(direct,2)->health;
        std::unique_ptr<RuntimeWindowState> resumed;
        const auto link = [](const RuntimeWindowState& s) -> const RuntimeProjectileEffect* {
            const RuntimeProjectileEffect* result = nullptr;
            for (const auto& actor : s.projectiles) if (actor.renderIntent == "magBeamVisual") {
                if (result) return nullptr;
                result = &actor;
            }
            return result;
        };
        for (int tick = 1; tick <= period*2+hz; ++tick) {
            step(direct);
            const auto* actor = link(direct); const auto* target = findObjectById(direct,2);
            if (!actor || actor->actorId != linkId || actor->damage != 0 || !target)
                return fail("continuous link vanished/duplicated hz=" + std::to_string(hz)
                    + " tick=" + std::to_string(direct.tick) + " actors=" + std::to_string(direct.projectiles.size()));
            const auto* render = runtimeProjectileRenderActorsForRender(direct);
            if (!render || std::count_if(render->begin(),render->end(),[](const auto& v) {
                return v.renderIntent == "magBeamVisual" && v.laser;
            }) != 1) return fail("active link absent from renderer");
            if (target->health != previousHealth) {
                if (previousHealth-target->health != firstDamage || direct.tick-lastHit < period)
                    return fail("visual changed damage amount/ROF hz=" + std::to_string(hz));
                previousHealth = target->health; lastHit = direct.tick; ++damageCount;
            }
            ++continuousFrames;
            if (resumed) {
                RuntimeSnapshotActiveStateScope restoredActive(*resumed); step(*resumed);
                const auto* copy = link(*resumed); const auto* restoredTarget = findObjectById(*resumed,2);
                if (!copy || !restoredTarget || restoredTarget->health != target->health
                    || serializeRuntimeProjectileEffect(*copy) != serializeRuntimeProjectileEffect(*actor))
                    return fail("continuous link/reload damage diverged hz=" + std::to_string(hz)
                        + " tick=" + std::to_string(direct.tick));
            } else if (tick == hz) {
                std::ostringstream saved; IniFile ini; resumed = std::make_unique<RuntimeWindowState>();
                seedRuntimeLoadScratch(*resumed,direct,false);
                if (!writeRuntimeStateToStream(direct,saved,error) || !ini.loadFromString(saved.str())
                    || !materializeRuntimeLoadStateFromIni(*resumed,ini,error)) return fail("continuous full reload: " + error);
                if (!link(*resumed) || link(*resumed)->actorId != linkId) return fail("reload lost active link");
                ++continuousSaves;
            }
        }
        if (damageCount != 3) return fail("native repeated building damage count=" + std::to_string(damageCount));
        std::ostringstream endSave; IniFile endIni;
        if (!writeRuntimeStateToStream(direct,endSave,error) || !endIni.loadFromString(endSave.str()))
            return fail("end save: " + error);
        for (const std::string action : {"stop","move","retarget","target-lost","source-lost","out-of-range","owner-change"}) {
            auto cancelled = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*cancelled,direct,false);
            if (!materializeRuntimeLoadStateFromIni(*cancelled,endIni,error)) return fail(action + " restore: " + error);
            auto& s = *cancelled; RuntimeSnapshotActiveStateScope cancelActive(s); s.selectedObjectIds = {1};
            if (action == "stop") issueRuntimeStopCommand(s);
            else if (action == "move") issueRuntimeMoveCommand(s,{3,14});
            else if (action == "retarget") {
                auto other = *findObjectById(s,2); other.instanceId = 3;
                other.y = 18; other.cell = runtimeCellKey(other.x,other.y);
                s.world.accessObjects().push_back(other); initializeRuntimeNavigation(s);
                issueRuntimeAttackCommand(s,*findObjectById(s,3));
            } else if (action == "owner-change") findMutableObjectById(s,1)->owner = "Americans";
            else if (action == "out-of-range") {
                auto* target = findMutableObjectById(s,2); target->x = 28; target->cell = runtimeCellKey(28,target->y);
                clearRuntimeRenderPosition(*target); invalidateRuntimeTickOccupancyCache(s);
            } else {
                auto* gone = findMutableObjectById(s,action == "source-lost" ? 1 : 2);
                gone->alive = false; gone->health = 0;
            }
            ++s.tick; advanceRuntimeEffects(s);
            if (link(s)) return fail(action + " left the old beam visible");
            ++clearedLinks;
        }
        std::cout << "Magnetron continuous hz=" << hz << " frames=" << period*2+hz
            << " shots=" << damageCount << " link=one reload=ok cancellation=7\n";
        const int afterHit = findObjectById(direct,2)->health;
        issueRuntimeStopCommand(direct);
        for(int i=0; i<hz; ++i) step(direct);
        if (findObjectById(direct,2)->health != afterHit || !direct.projectiles.empty())
            return fail("building visual repeated damage or did not expire");
        ++cases;
        auto infantry = make("E1", WorldObjectKind::Infantry, false, hz);
        if (infantry->lastCommand.acceptedCount != 0
            || runtimeWeaponProfileForObject(*infantry, *findObjectById(*infantry,1), findObjectById(*infantry,2)).weapon)
            return fail("infantry was accepted as a lift target");
        ++cases;
    }
    if (!runRuntimeMagBeamHostSmokeProbe(error)) return false;
    RuntimeMapRenderContext context;
    context.rules = &live->rules; context.art = &live->art; context.theater = &live->theater;
    context.assetRootPath = runtimeAssetRootPath(config);
    context.structureRootPath = runtimeStructureRootPath(config,live->report.map);
    context.palettePath = runtimePalettePath(config,live->report.map);
    context.structurePalettePath = runtimeStructurePalettePath(config,live->report.map);
    if (!runRuntimeMagnetronRenderContractCheck(context, previewObjects, previewBeams, error)) return false;
    if (!collisionFailure.empty()) return fail(collisionFailure);
    std::cout << "Magnetron passed: " << cases << " stock combat cases; vehicles/navy, empty land, building impact,"
        << " direct building damage, infantry exclusion, carrier children, save phases and native purple beam; continuousFrames="
        << continuousFrames << " continuousReloads=" << continuousSaves << " clearedLinks=" << clearedLinks << '\n';
    return true;
}
