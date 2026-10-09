bool runRuntimeChronoMinerMovementSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "chrono miner: " + message; return false; };
    auto storage = std::make_unique<RuntimeWindowState>();
    auto& probe = *storage;
    probe.config = live.config;
    probe.config.fixedTickHz = 30;
    probe.rules = live.rules;
    probe.art = live.art;
    probe.rulesLoaded = true;
    probe.artLoaded = true;
    probe.runtimeSuppressPlayerAudioFeedback = true;
    probe.economy.owner = "Americans";
    probe.economy.harvesters = 1;
    probe.economy.refineries = 1;
    const RuntimeCellPoint oreCell{24, 8};
    for (int y = 1; y <= 24; ++y) for (int x = 1; x <= 28; ++x) {
        TerrainCell terrain;
        terrain.x = x;
        terrain.y = y;
        terrain.cellIndex512 = runtimeCellKey(x, y);
        terrain.tileIndex = 0;
        terrain.subTileIndex = 0;
        if (RuntimeCellPoint{x, y} == oreCell) {
            terrain.hasOverlay = true;
            terrain.overlayIndex = 102;
            terrain.overlayFrame = 11;
            terrain.overlayType = "TIB01";
            terrain.overlayImage = "TIB01";
            terrain.overlayIsOre = true;
        }
        probe.world.accessTerrainCells().push_back(terrain);
    }
    WorldObjectInstance miner;
    miner.instanceId = 1;
    miner.kind = WorldObjectKind::Unit;
    miner.type = "CMIN";
    miner.owner = probe.economy.owner;
    miner.health = 1000;
    miner.x = 8;
    miner.y = 8;
    miner.cell = runtimeCellKey(miner.x, miner.y);
    miner.facing = 64;
    miner.mission = "Guard";
    auto refinery = miner;
    refinery.instanceId = 2;
    refinery.kind = WorldObjectKind::Structure;
    refinery.type = "GAREFN";
    refinery.x = 4;
    refinery.y = 14;
    refinery.cell = runtimeCellKey(refinery.x, refinery.y);
    probe.world.accessObjects() = {miner, refinery};
    initializeRuntimeNavigation(probe);
    probe.selectedObjectIds = {miner.instanceId};
    auto* moving = findMutableObjectById(probe, miner.instanceId);
    bool interpolated = false;
    const auto finishDrive = [&](const RuntimeCellPoint destination, int expectedTeleports, bool allowOreApproach = false) {
        const auto warpCount = [&]() {
            return std::count_if(probe.impactEffects.begin(), probe.impactEffects.end(), [](const auto& effect) {
                return effect.lifecycleState == "ChronoWarpOut" || effect.lifecycleState == "ChronoWarpIn";
            });
        };
        const auto initialWarps = warpCount();
        for (int i = 0; i < 1200; ++i) {
            ++probe.tick;
            processRuntimePathRequests(probe);
            auto behavior = probe.behaviors.find(miner.instanceId);
            if (behavior == probe.behaviors.end() || behavior->second.kind != RuntimeBehaviorKind::Move)
                return fail("drive did not retain a movement path");
            const auto before = runtimeCellPointForObject(*moving);
            advanceRuntimeMoveBehavior(probe, *moving, behavior->second);
            const auto after = runtimeCellPointForObject(*moving);
            interpolated = interpolated || moving->hasRenderPosition;
            if (std::abs(after.x - before.x) > 1 || std::abs(after.y - before.y) > 1
                || probe.specialMechanisms.isChronoMovementLocked(miner.instanceId, probe.tick)
                || probe.economy.harvesterTeleports != expectedTeleports || warpCount() != initialWarps)
                return fail("ordinary movement blinked or entered chrono recovery");
            if (behavior->second.state == RuntimeBehaviorState::Complete) {
                // Automatic harvesting permits a nearby approach square; manual
                // commands still have to reach the exact clicked destination.
                const bool arrived = after == destination || (allowOreApproach
                    && after == behavior->second.moveTarget
                    && std::abs(after.x - destination.x) <= 3
                    && std::abs(after.y - destination.y) <= 3);
                eraseRuntimeBehavior(probe, miner.instanceId);
                return arrived ? true : fail("drive stopped short of its destination: "
                    + std::to_string(after.x) + "," + std::to_string(after.y));
            }
        }
        return fail("drive did not finish");
    };
    for (const RuntimeCellPoint destination : {RuntimeCellPoint{16, 8}, oreCell}) {
        const auto before = runtimeCellPointForObject(*moving);
        issueRuntimeCommand(probe, destination, nullptr);
        if (probe.lastCommand.acceptedCount != 1 || runtimeCellPointForObject(*moving) != before
            || !probe.impactEffects.empty() || probe.specialMechanisms.isChronoInTransit(miner.instanceId))
            return fail("manual ground/ore command triggered a blink");
        if (destination == oreCell && (probe.lastCommand.verb != "harvest"
                || probe.harvesterTasks.at(miner.instanceId).phase != RuntimeHarvesterPhase::MovingToOre))
            return fail("ore click did not queue harvesting");
        if (!finishDrive(destination, 0)) return false;
    }
    if (!interpolated) return fail("driving never rendered intermediate positions");

    // Start a loaded return through the actual economy task, then let unloading
    // and the next automatic trip to ore run through their normal transitions.
    auto& task = probe.harvesterTasks.at(miner.instanceId);
    task.cargoUnits = runtimeHarvesterCargoCapacity(probe, *moving);
    task.cargoValue = 1000;
    task.phase = RuntimeHarvesterPhase::ReturningToRefinery;
    task.targetRefineryId = refinery.instanceId;
    task.nextThinkTick = probe.tick;
    const auto beforeReturn = runtimeCellPointForObject(*moving);
    advanceRuntimeHarvesterEconomy(probe);
    if (probe.economy.harvesterTeleports != 1 || task.phase != RuntimeHarvesterPhase::Docking
        || !runtimeHarvesterIsAtDock(*moving, task)
        || runtimeCellDistance(beforeReturn, runtimeCellPointForObject(*moving)) <= 1.0
        || probe.impactEffects.size() != 2
        || probe.impactEffects[0].lifecycleState != "ChronoWarpOut"
        || probe.impactEffects[1].lifecycleState != "ChronoWarpIn"
        || probe.impactEffects[0].animId != "WARPOUT"
        || probe.impactEffects[1].animId != "WARPOUT")
        return fail("loaded return did not blink into the refinery with both warp effects");
    const int creditsBefore = probe.economy.credits;
    for (int i = 0; i < 600 && probe.economy.resourceTrips == 0; ++i) {
        ++probe.tick;
        advanceRuntimeHarvesterEconomy(probe);
    }
    if (probe.economy.resourceTrips != 1 || probe.economy.credits != creditsBefore + 1000
        || task.cargoUnits != 0 || task.cargoValue != 0)
        return fail("refinery return did not unload its cargo");
    const auto dock = runtimeCellPointForObject(*moving);
    ++probe.tick;
    advanceRuntimeHarvesterEconomy(probe);
    if (task.phase != RuntimeHarvesterPhase::MovingToOre || runtimeCellPointForObject(*moving) != dock
        || task.targetOre != oreCell) return fail("automatic departure did not queue a trip to ore");
    if (!finishDrive(oreCell, 1, true)) return false;
    if (runtimeCellDistance(runtimeCellPointForObject(*moving), oreCell) >= runtimeCellDistance(dock, oreCell))
        return fail("automatic departure did not drive toward the ore");
    std::cout << "Chrono miner: manualDrive=ok oreClickDrive=ok queuedDrive=ok interpolation=ok "
        "refineryBlink=ok warpEffects=ok unload=ok automaticOreDrive=ok\n";
    return true;
}

bool runRuntimeChronoMovementSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) { error = "chrono: " + message; return false; };
    const auto& stockVisuals = live.rules.getSpecialMechanismRules();
    if (stockVisuals.warpOutAnim != "WARPOUT" || stockVisuals.warpInAnim != "WARPIN"
        || stockVisuals.chronoSparkleAnim != "CHRONOSK")
        return fail("stock General animation fields were not loaded");
    IniFile customVisualIni;
    customVisualIni.loadFromString("[General]\nWarpOut=CUSTOMOUT\nChronoSparkle1=CUSTOMSPARK\n"
        "[AudioVisual]\nWarpOut=OLDOUT\nWarpIn=LEGACYIN\n");
    RulesDatabase customVisualRules;
    customVisualRules.loadFromIni(customVisualIni);
    const auto& customVisuals = customVisualRules.getSpecialMechanismRules();
    if (customVisuals.warpOutAnim != "CUSTOMOUT" || customVisuals.warpInAnim != "LEGACYIN"
        || customVisuals.chronoSparkleAnim != "CUSTOMSPARK") return fail("custom chrono animation fields were ignored");
    WorldObjectInstance original;
    original.instanceId = 1;
    original.kind = WorldObjectKind::Infantry;
    original.type = "CLEG";
    original.owner = "Americans";
    original.health = 125;
    original.x = 8;
    original.y = 8;
    original.cell = runtimeCellKey(8, 8);
    original.facing = 64;
    original.mission = "Guard";
    const SpecialCellPredicate legal = [](int x, int y, int) { return x > 0 && y > 0; };
    const SpecialCellPredicate free = [](int, int, int) { return false; };
    int timingCases = 0;
    auto timing = [&](SpecialMechanismRules settings, int cells, int hz, int expected) {
        RuntimeSpecialMechanismState mechanism;
        mechanism.reset(settings);
        std::vector<WorldObjectInstance> objects{original};
        std::vector<SpecialMechanismEvent> events;
        const SpecialEventSink sink = [&](const SpecialMechanismEvent& event) { events.push_back(event); };
        std::string reason;
        if (!mechanism.requestChronoMove(objects, live.rules, 1, {8 + cells, 8}, 2,
                100, legal, free, sink, reason, hz)) return fail(reason);
        if (objects.front().x != 8 || !mechanism.isChronoInTransit(1)
            || events.size() != 1 || events.front().kind != "ChronoWarpOut"
            || events.front().cell.x != 8) return fail("departure did not stay at its source");
        mechanism.tickChronoMoves(objects, 101, legal, free, sink);
        if (objects.front().x != 8 + cells || objects.front().level != 2
            || objects.front().hasRenderPosition || objects.front().renderMoveActive
            || mechanism.isChronoInTransit(1)) return fail("blink waited for distance or hid the arrived unit");
        const auto snapshots = mechanism.renderSnapshots(101, hz);
        if (snapshots.size() != 1 || snapshots.front().requiredProgress != expected
            || snapshots.front().progress != 0 || snapshots.front().expireTick != 101 + expected)
            return fail("incorrect distance recovery: cells=" + std::to_string(cells));
        if (expected > 0) {
            if (!mechanism.isChronoMovementLocked(1, 101 + expected - 1)
                || mechanism.requestChronoMove(objects, live.rules, 1, {20, 10}, 0,
                    101, legal, free, sink, reason, hz)) return fail("another order bypassed recovery");
            const int middleTick = 101 + expected / 2;
            mechanism.tickChronoMoves(objects, middleTick, legal, free, sink);
            std::ostringstream saved;
            mechanism.save(saved);
            IniFile ini;
            ini.loadFromString(saved.str());
            RuntimeSpecialMechanismState restored;
            restored.reset(settings);
            if (!restored.load(ini, reason) || restored.stateHash() != mechanism.stateHash())
                return fail("recovery save/load changed state");
            const auto restoredSnapshots = restored.renderSnapshots(middleTick, hz);
            if (restoredSnapshots.front().expireTick != 101 + expected
                || restoredSnapshots.front().progress != expected / 2)
                return fail("loading restarted recovery or its sparkle");
            mechanism = std::move(restored);
        }
        mechanism.tickChronoMoves(objects, 101 + expected, legal, free, sink);
        if (mechanism.isChronoMovementLocked(1, 101 + expected)
            || !mechanism.renderSnapshots(101 + expected, hz).empty()
            || objects.front().mission != "Guard") return fail("recovery did not end at its deadline");
        if (std::count_if(events.begin(), events.end(), [](const auto& e) { return e.kind == "ChronoWarpIn"; }) != 1)
            return fail("arrival animation repeated during recovery");
        ++timingCases;
        return true;
    };
    auto settings = live.rules.getSpecialMechanismRules();
    settings.chronoTrigger = true;
    settings.chronoDistanceFactor = 48;
    settings.chronoMinimumDelay = 16;
    settings.chronoRangeMinimum = 0;
    if (!timing(settings, 2, 30, 32) || !timing(settings, 8, 30, 84)
        || !timing(settings, 16, 30, 170) || !timing(settings, 8, 60, 168)) return false;
    settings.chronoTrigger = false;
    if (!timing(settings, 16, 30, 32)) return false;
    settings.chronoTrigger = true;
    settings.chronoMinimumDelay = 1;
    settings.chronoDistanceFactor = 256;
    settings.chronoRangeMinimum = 512;
    if (!timing(settings, 2, 30, 2) || !timing(settings, 3, 30, 6)) return false;
    settings.chronoTrigger = false;
    settings.chronoMinimumDelay = 0;
    if (!timing(settings, 8, 30, 0)) return false;

    RuntimeSpecialMechanismState blocked;
    blocked.reset(live.rules.getSpecialMechanismRules());
    std::vector<WorldObjectInstance> blockedObjects{original};
    std::string reason;
    if (!blocked.requestChronoMove(blockedObjects, live.rules, 1, {16, 8}, 0,
            0, legal, free, nullptr, reason)) return fail(reason);
    blocked.tickChronoMoves(blockedObjects, 1, legal, [](int, int, int) { return true; }, nullptr);
    if (blockedObjects.front().x != 8 || blocked.isChronoMovementLocked(1, 1)
        || !blocked.renderSnapshots(1).empty()) return fail("blocked arrival lost its unit");

    int unitCases = 0;
    for (const std::string type : {"CLEG", "CCOMAND", "CIVAN"}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.config.fixedTickHz = 30;
        probe.rules = live.rules;
        probe.art = live.art;
        probe.rulesLoaded = true;
        probe.artLoaded = true;
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.tick = 200;
        probe.economy.owner = original.owner;
        auto object = original;
        object.type = type;
        object.kind = WorldObjectKind::Infantry;
        WorldObjectInstance target = original;
        target.instanceId = 2;
        target.type = "E2";
        target.owner = "Russians";
        target.x = 17;
        target.cell = runtimeCellKey(17, 8);
        probe.world.accessObjects() = {object, target};
        for (int y = 1; y <= 24; ++y) for (int x = 1; x <= 28; ++x) {
            TerrainCell terrain;
            terrain.x = x;
            terrain.y = y;
            terrain.cellIndex512 = runtimeCellKey(x, y);
            terrain.level = x >= 16 ? 1 : 0;
            probe.world.accessTerrainCells().push_back(terrain);
            probe.terrainInfoByCell.emplace(terrain.cellIndex512, RuntimeTerrainInfo{});
        }
        probe.terrainByCell = buildRuntimeTerrainLookup(probe.world);
        probe.selectedObjectIds = {1};
        issueRuntimeMoveCommand(probe, {16, 8});
        if (probe.lastCommand.acceptedCount != 1 || probe.impactEffects.size() != 1
            || probe.impactEffects.front().lifecycleState != "ChronoWarpOut")
            return fail(type + " manual command missed departure: accepted="
                + std::to_string(probe.lastCommand.acceptedCount) + " effects="
                + std::to_string(probe.impactEffects.size()) + " status=" + probe.simulationStatus);
        ++probe.tick;
        advanceRuntimeEffects(probe);
        auto* moved = findMutableObjectById(probe, 1);
        if (moved == nullptr || moved->x != 16 || moved->y != 8 || moved->level != 1
            || moved->hasRenderPosition || probe.impactEffects.size() != 2
            || runtimeObjectCanMove(probe, *moved)) return fail(type + " did not arrive frozen on the correct height");
        const auto snapshot = probe.specialMechanisms.renderSnapshots(probe.tick).front();
        if (snapshot.requiredProgress != 84 || snapshot.resourceId != "CHRONOSK")
            return fail(type + " lost its distance delay/sparkle");
        issueRuntimeMoveCommand(probe, {20, 8});
        if (probe.lastCommand.acceptedCount != 0 || probe.lastCommand.rejectedCount != 1
            || probe.impactEffects.size() != 2) return fail(type + " accepted movement while frozen");
        RuntimeObjectBehavior queued;
        queued.kind = RuntimeBehaviorKind::Move;
        queued.moveTarget = {20, 8};
        queued.path = {{17, 8}, {18, 8}, {20, 8}};
        queued.moveCooldownTicks = 2;
        if (advanceRuntimeMoveBehavior(probe, *moved, queued) || moved->x != 16 || moved->hasRenderPosition)
            return fail(type + " interpolated or walked during recovery");
        queued.kind = RuntimeBehaviorKind::Attack;
        queued.targetObjectId = 2;
        if (advanceRuntimeAttackBehavior(probe, *moved, queued) || !probe.projectiles.empty())
            return fail(type + " attacked during recovery");
        int animationDuration = 0;
        for (const auto& effect : probe.impactEffects) {
            if (effect.animId != "WARPOUT") return fail(type + " did not use WarpOut at both ends");
            if (effect.animationFrameCount <= 1 || effect.frameIndex < 0
                || effect.animationSequenceSteps != effect.animationFrameCount
                || effect.totalTicks != effect.animationFrameCount * effect.animationFrameDelayTicks
                || !makeRuntimeImpactRenderActor(effect).centerOnAnchor)
                return fail(type + " truncated a warp SHP");
            animationDuration = std::max(animationDuration, effect.totalTicks);
        }
        for (const auto& actor : *runtimeImpactRenderActorsForRender(probe)) {
            if (actor.animId != "WARPOUT" || !actor.centerOnAnchor)
                return fail(type + " cached renderer changed personal teleport animation");
        }
        // Network snapshots omit cached image IDs. Rebuild the right animation
        // from its persisted lifecycle, without restarting the authored frames.
        auto transferred = std::make_unique<RuntimeWindowState>();
        transferred->rules = probe.rules;
        for (const auto& effect : probe.impactEffects) {
            RuntimeImpactEffect copy;
            if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(effect), copy)
                || !copy.animId.empty() || copy.remainingTicks != effect.remainingTicks)
                return fail(type + " warp snapshot transfer failed");
            transferred->impactEffects.push_back(std::move(copy));
        }
        for (const auto& actor : *runtimeImpactRenderActorsForRender(*transferred)) {
            if (actor.animId != "WARPOUT" || !actor.centerOnAnchor)
                return fail(type + " snapshot renderer used the red arrival animation");
        }
        probe.tick = snapshot.expireTick - 1;
        advanceRuntimeSpecialMechanisms(probe);
        if (runtimeObjectCanMove(probe, *moved)) return fail(type + " thawed early");
        ++probe.tick;
        advanceRuntimeSpecialMechanisms(probe);
        if (!runtimeObjectCanMove(probe, *moved)) return fail(type + " stayed frozen");
        issueRuntimeMoveCommand(probe, {20, 8});
        if (probe.lastCommand.acceptedCount != 1) return fail(type + " could not blink again after thawing");
        for (int i = 0; i <= animationDuration + 100; ++i) {
            ++probe.tick;
            advanceRuntimeEffects(probe);
        }
        if (!probe.impactEffects.empty()) return fail(type + " left an endless warp effect");
        if (type == "CLEG" || type == "CCOMAND") {
            auto* enemy = findMutableObjectById(probe, 2);
            enemy->x = 27;
            enemy->cell = runtimeCellKey(27, 8);
            refreshRuntimeObjectOccupancyAfterLocalChange(probe);
            RuntimeObjectBehavior chase;
            chase.kind = RuntimeBehaviorKind::Attack;
            chase.state = RuntimeBehaviorState::Chasing;
            chase.targetObjectId = 2;
            chase.path = {{21, 8}, {22, 8}};
            if (!advanceRuntimeAttackBehavior(probe, *moved, chase)
                || !probe.specialMechanisms.isChronoInTransit(1) || moved->x != 20
                || moved->hasRenderPosition || !chase.path.empty())
                return fail(type + " walked instead of blinking when chasing");
            ++probe.tick;
            advanceRuntimeEffects(probe);
            if (moved->x != 22) return fail(type + " attack blink missed its approach cell");
        }
        if (type == "CIVAN") {
            for (int id : {3, 4, 5, 6}) {
                auto contender = original;
                contender.instanceId = id;
                contender.y = 10 + id;
                contender.cell = runtimeCellKey(contender.x, contender.y);
                probe.world.accessObjects().push_back(contender);
            }
            refreshRuntimeObjectOccupancyAfterLocalChange(probe);
            for (int id : {3, 4, 5, 6}) {
                if (!requestRuntimeChronoMoveWithParasite(probe, id, {24, 12}, 1, legal, free, reason))
                    return fail("shared arrival setup: " + reason);
            }
            ++probe.tick;
            advanceRuntimeEffects(probe);
            int arrived = 0;
            std::set<int> arrivedSlots;
            for (int id : {3, 4, 5, 6}) {
                const auto* contender = findObjectById(probe, id);
                if (contender->x == 24 && contender->y == 12) {
                    ++arrived;
                    if(!RuntimeSubcellSystemState::isActiveSubCell(contender->subCell)
                        ||!arrivedSlots.insert(contender->subCell).second)
                        return fail("simultaneous infantry blinks overlapped a subcell");
                }
            }
            if (arrived != 3) return fail("simultaneous blinks did not enforce three infantry per cell");
        }
        ++unitCases;
    }
    if (!runRuntimeChronoMinerMovementSmokeProbe(live, error)) return false;
    std::cout << "Chrono movement: timingCases=" << timingCases << " unitTypes=" << unitCases
        << " oneTickBlink=ok frozenMoveAttack=ok height=ok fullWarpFrames=ok thaw=ok saveLoad=ok\n";
    return true;
}

// Issue25/6: real UI transport orders for stock personal teleporters.
// Expected displacement/recovery uses pinned original rules, independently of transport/chrono helpers.
bool runRuntimeChronoTransportIssue25SmokeProbe(RuntimeWindowState& live,std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& message) { error="Issue25 chrono transport: "+scene+": "+message;return false; };
    const char* output=std::getenv("CONCORD_CHRONO_TRANSPORT_ISSUE25_OUTPUT");
    const std::string root=output&&*output?output:pathString(std::filesystem::path(live.config.localDataRootPath)/"issue25-chrono-transport");
    std::filesystem::create_directories(root);
    const auto digest=[](std::span<const std::uint8_t> bytes) {
        const auto hash=runtimeMultiplayerSnapshotSha256(bytes);std::ostringstream out;out<<std::hex<<std::setfill('0');
        for(auto b:hash)out<<std::setw(2)<<static_cast<unsigned int>(b);return out.str();
    };
    const auto quote=[](const std::string& value) {std::string out="\"";for(char c:value){if(c=='"')out+='"';out+=c;}return out+'"';};
    const auto rulesPath=pathString(std::filesystem::path(runtimeAssetRootPath(live.config))/"ra2md/localmd/rulesmd.ini");
    std::ifstream rawRules(rulesPath,std::ios::binary);
    const std::vector<std::uint8_t> rulesBytes{std::istreambuf_iterator<char>(rawRules),std::istreambuf_iterator<char>()};
    IniFile authored;
    if(digest(rulesBytes)!="06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05"||!authored.loadFromFile(rulesPath))
        return fail("original rules SHA/parse");
    const auto& special=live.rules.getSpecialMechanismRules();
    if(authored.getValue("General","ChronoDistanceFactor")!="48"||authored.getValue("General","ChronoMinimumDelay")!="16"
        ||authored.getValue("General","ChronoRangeMinimum")!="0"||authored.getValue("General","ChronoTrigger")!="yes"
        ||special.chronoDistanceFactor!=48||special.chronoMinimumDelay!=16||special.chronoRangeMinimum!=0||!special.chronoTrigger
        ||special.warpOutAnim!="WARPOUT")return fail("original personal-chrono rules; ChronoDelay is not personal recovery");
    std::ofstream catalog(root+"/catalog.csv"),states(root+"/states.csv"),cases(root+"/cases.csv");
    if(!catalog||!states||!cases)return fail("evidence files");
    catalog<<"kind,type,original_passengers,original_size_limit,original_size,original_teleporter,loaded_passengers,loaded_size_limit,loaded_size,loaded_teleporter\n";
    const std::map<std::string,std::pair<int,int>> transports{{"FV",{1,1}},{"BFRT",{5,2}},{"HTK",{5,2}},
        {"BUS",{5,2}},{"DDBX",{5,2}},{"PROPA",{5,2}},{"LCRF",{12,6}},{"SAPC",{12,6}},{"YHVR",{12,6}},
        {"SHAD",{5,2}},{"HIND",{10,2}}};
    std::set<std::string> loadedHosts;
    for(const auto& unit:live.rules.getVehicleTypes())if(unit.passengers>0) {
        const auto expected=transports.find(unit.id);
        if(expected==transports.end()||std::pair{unit.passengers,unit.sizeLimit}!=expected->second
            ||authored.getValue(unit.id,"Passengers")!=std::to_string(expected->second.first)
            ||authored.getValue(unit.id,"SizeLimit")!=std::to_string(expected->second.second))return fail(unit.id+" original transport catalog");
        loadedHosts.insert(unit.id);
        catalog<<"Unit,"<<unit.id<<','<<expected->second.first<<','<<expected->second.second<<",,,"<<unit.passengers<<','<<unit.sizeLimit<<','<<unit.size<<','<<unit.teleporter<<'\n';
    }
    if(loadedHosts.size()!=11)return fail("all eleven original transport types");
    for(const std::string type:{"CLEG","CCOMAND","CIVAN"}) {
        const auto* unit=findRuntimeUnitTypeById(live.rules,type);
        const auto rawSize=authored.getValue(type,"Size");
        const int expectedLoadedSize=type=="CLEG"?1:0;
        if(!unit||!unit->teleporter||unit->harvester||unit->size!=expectedLoadedSize
            ||rawSize!=(type=="CLEG"?"1":"")||unit->locomotor!="{4A582747-9839-11d1-B709-00A024DDAFD1}"
            ||authored.getValue(type,"Teleporter")!="yes"||authored.getValue(type,"Locomotor")!="{4A582747-9839-11d1-B709-00A024DDAFD1}")
            return fail(type+" original personal infantry teleporter");
        catalog<<"Infantry,"<<type<<",,,"<<(rawSize.empty()?"omitted":rawSize)<<",yes,"<<unit->passengers<<','<<unit->sizeLimit<<','<<unit->size<<','<<unit->teleporter<<'\n';
    }
    const auto* legion=findRuntimeUnitTypeById(live.rules,"CLEG");
    if(!legion||legion->ifvMode!=10||authored.getValue("CLEG","IFVMode")!="10")return fail("original CLEG IFVMode10");
    std::ofstream sources(root+"/sources.txt");sources<<"rulesmd.ini SHA256 "<<digest(rulesBytes)
        <<"\nOriginal personal recovery: round(max(16,floor(hypot(dx,dy)*256/48))*fixedHz/15)."
        <<"\nOriginal 11 transport capacities and three personal Teleporter Infantry are pinned separately from runtime command results. CLEG explicit Size=1; CCOMAND/CIVAN omit Size and load zero (one transport space minimum)."
        <<"\nCIVAN retains friendly IvanBomb Primary priority; its two extra family controls use actual Primary/Stop followed by original team-script action14 explicit Load. No manifest, interpolation pose, ChronoMoveState or entry permission is seeded.\n";
    auto tileCache=std::make_unique<RuntimeMapRenderCache>();const auto metadata=runtimeTerrainMetadataContext(live);
    std::optional<TerrainCell> clear,water;
    for(const auto& set:live.theater.getTileSets())for(int tile=set.startTileIndex;tile<set.startTileIndex+set.tilesInSet&&(!clear||!water);++tile)
        for(int sub=0;sub<16;++sub) {
            TerrainCell c;c.tileIndex=tile;c.subTileIndex=sub;
            const auto info=runtimeTerrainInfoForCell(live,c);
            if(!info.passable||info.bridge||info.cliff||info.shore||resolveRuntimeTerrainSubTileRampType(c,metadata,*tileCache)!=0)continue;
            const auto land=resolveRuntimeTerrainSubTileLandType(c,metadata,*tileCache);
            if(!clear&&!info.water&&land==RuntimeTerrainLandType::Clear)clear=c;
            if(!water&&info.water&&land==RuntimeTerrainLandType::Water)water=c;
        }
    if(!clear||!water)return fail("real original flat Clear/Water TMP subtiles");
    states<<"scene,tick,event,actor_id,host_id,alive,mission,cell_x,cell_y,foot_x,foot_y,render_active,contained,pending_host,behavior_kind,allowed_host,transfer_kind,path_size,chrono_phase,chrono_start,chrono_arrival,chrono_deadline,host_x,host_y,host_alive,host_owner,host_active,host_airborne,warp_effects,host_foot_x,host_foot_y,host_facing,host_move_kind,host_move_state,host_goal_x,host_goal_y,host_path_size,host_path_pending,host_move_cooldown,host_next_repath,host_render_remaining,host_render_total,command_valid,command_verb,command_accepted,command_rejected,simulation_status,interaction_status,match_finished\n";
    cases<<"scene,type,host,actor_id,host_id,tick,expected_delay,deadline,warp_count,contained,accepted,rejected,entry_command,passed\n";
    int fixtureCount=0,coreCount=0,rejectionCount=0,ordinaryControls=0,minimumControls=0;
    int saveFiles=0,pairedTicks=0,captures=0,unloadControls=0;
    struct Fixture {std::unique_ptr<RuntimeWindowState> state;int actor=-1,host=-1;std::string type,hostType;RuntimeCellPoint source{56,64},hull{64,64};};
    const auto make=[&](const std::string& type,const std::string& hostType,int distance=8,bool onWater=false) {
        Fixture f;f.state=std::make_unique<RuntimeWindowState>();auto& s=*f.state;seedRuntimeLoadScratch(s,live,false);
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.noCooldown=s.config.shortGame=s.config.crates=s.config.superweapons=false;
        s.config.fixedTickHz=30;s.config.playerSlots.clear();s.config.playerCountry="Americans";s.config.startingUnits=0;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.campaignMode=s.skirmishMatchFinished=false;s.computerAiByOwner.clear();s.computerMiningByOwner.clear();s.humanHouseAuthorities.clear();
        s.triggers={};s.triggers.bootstrapComplete=true;s.economy={};s.economy.owner="Americans";s.economy.credits=1000;s.economy.skirmishStartApplied=true;
        s.initialOreFieldsSeeded=true;s.visionEnabled=false;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();s.mapFacts={};s.mapFactsLoaded=false;
        s.terrainByCell.clear();s.terrainInfoByCell.clear();s.structureFootprints.clear();
        s.report.map.size.clear();s.report.map.localSize.clear();
        for(int y=44;y<=88;++y)for(int x=44;x<=88;++x) {
            auto c=onWater&&x>=64&&x<=68&&y>=60&&y<=68?*water:*clear;
            c.x=x;c.y=y;c.cellIndex512=y*512+x;c.level=0;s.world.accessTerrainCells().push_back(c);
        }
        f.type=type;f.hostType=hostType;f.source={64-distance,64};
        auto* host=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,hostType,f.hull);
        if(!host)return f;f.host=host->instanceId;host->owner=s.economy.owner;host->mission="Guard";
        auto* actor=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,type,f.source);
        if(!actor)return f;f.actor=actor->instanceId;actor->owner=s.economy.owner;actor->mission="Guard";
        initializeRuntimeNavigation(s);rebuildRuntimeEconomyPower(s);initializeRuntimeOfflineGameplayRouting(s);s.selectedObjectIds={f.actor};
        return f;
    };
    const auto step=[](RuntimeWindowState& s) {RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    const auto effectCount=[](const RuntimeWindowState& s,int id) {return std::count_if(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& e){return e.targetObjectId==id&&(e.lifecycleState=="ChronoWarpOut"||e.lifecycleState=="ChronoWarpIn");});};
    const auto contained=[](const RuntimeWindowState& s,int host,int actor) {
        const auto* h=findRuntimeObjectByIdIncludingInactive(s,host);
        return h&&std::find(h->transportPassengerIds.begin(),h->transportPassengerIds.end(),actor)!=h->transportPassengerIds.end();
    };
    const auto sample=[&](Fixture& f,const std::string& event) {
        const auto& s=*f.state;const auto* p=findRuntimeObjectByIdIncludingInactive(s,f.actor);const auto* h=findRuntimeObjectByIdIncludingInactive(s,f.host);
        if(!p||!h)return;
        const auto slot=worldObjectSubCellOffset(*p);const auto pending=s.pendingTransportTargets.find(f.actor);const auto move=s.behaviors.find(f.actor);
        const auto hostMove=s.behaviors.find(f.host);
        const auto* clock=s.specialMechanisms.chronoMoveState(f.actor);
        states<<quote(scene)<<','<<s.tick<<','<<quote(event)<<','<<f.actor<<','<<f.host<<','<<p->alive<<','<<quote(p->mission)<<','<<p->x<<','<<p->y
            <<','<<std::setprecision(17)<<runtimeObjectRenderX(*p)+slot.x<<','<<runtimeObjectRenderY(*p)+slot.y<<','<<p->renderMoveActive
            <<','<<contained(s,f.host,f.actor)<<','<<(pending==s.pendingTransportTargets.end()?-1:pending->second)
            <<','<<(move==s.behaviors.end()?-1:int(move->second.kind))<<','<<(move==s.behaviors.end()?-1:move->second.pendingAllowedOccupiedObjectId)
            <<','<<(move==s.behaviors.end()?-1:int(move->second.transportTransferKind))<<','<<(move==s.behaviors.end()?0:move->second.path.size())
            <<','<<(clock?int(clock->phase):-1)<<','<<(clock?clock->startTick:-1)<<','<<(clock?clock->completeTick:-1)<<','<<(clock?clock->cooldownUntilTick:-1)
            <<','<<h->x<<','<<h->y<<','<<h->alive<<','<<quote(h->owner)<<','<<h->renderMoveActive<<','<<h->airborne<<','<<effectCount(s,f.actor)
            <<','<<runtimeObjectRenderX(*h)<<','<<runtimeObjectRenderY(*h)<<','<<h->facing
            <<','<<(hostMove==s.behaviors.end()?-1:int(hostMove->second.kind))<<','<<(hostMove==s.behaviors.end()?-1:int(hostMove->second.state))
            <<','<<(hostMove==s.behaviors.end()?-1:hostMove->second.moveTarget.x)<<','<<(hostMove==s.behaviors.end()?-1:hostMove->second.moveTarget.y)
            <<','<<(hostMove==s.behaviors.end()?0:hostMove->second.path.size())<<','<<(hostMove!=s.behaviors.end()&&hostMove->second.pathRequestPending)
            <<','<<(hostMove==s.behaviors.end()?0:hostMove->second.moveCooldownTicks)<<','<<(hostMove==s.behaviors.end()?-1:hostMove->second.nextRepathTick)
            <<','<<(hostMove==s.behaviors.end()?0:hostMove->second.renderMoveTicksRemaining)<<','<<(hostMove==s.behaviors.end()?0:hostMove->second.renderMoveTicksTotal)
            <<','<<s.lastCommand.valid<<','<<quote(s.lastCommand.verb)<<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount
            <<','<<quote(s.simulationStatus)<<','<<quote(s.interactionStatus)<<','<<s.skirmishMatchFinished<<'\n';states.flush();
    };
    const auto noWalk=[&](Fixture& f) {
        const auto& s=*f.state;const auto* p=findRuntimeObjectByIdIncludingInactive(s,f.actor);const auto move=s.behaviors.find(f.actor);
        if(!p||p->renderMoveActive||(move!=s.behaviors.end()&&(move->second.transportTransferKind==RuntimeTransportTransferKind::Boarding
            ||move->second.renderMoveTicksRemaining>0||!move->second.path.empty())))return fail("personal chrono passenger used ground path/boarding interpolation");
        return true;
    };
    const auto expectedDelay=[](RuntimeCellPoint a,RuntimeCellPoint b,int hz) {
        const int frames=std::max(16,int(std::floor(std::hypot(double(b.x-a.x),double(b.y-a.y))*256.0/48.0)));
        return int(std::round(frames*hz/15.0));
    };    const auto queue=[&](Fixture& f) {
        auto& s=*f.state;const auto* h=findObjectById(s,f.host);const auto* p=findObjectById(s,f.actor);
        if(!h||!p||h->airborne||h->renderMoveActive||h->health<=0)return fail("original living/landed/stationary host prerequisite");
        s.selectedObjectIds={f.actor};
        bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*h),h);
        if(f.type=="CIVAN") {
            sample(f,"UI-Primary-original-Ivan-Attack-priority");
            const auto attack=s.behaviors.find(f.actor);
            if(authored.getValue("CIVAN","Primary")!="IvanBomber"||authored.getValue("CIVAN","AttackCursorOnFriendlies")!="yes"
                ||authored.getValue("IvanBomber","Warhead")!="IvanBomb"||authored.getValue("IvanBomb","IvanBomb")!="yes"
                ||!dispatched||!s.lastCommand.valid||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0
                ||attack==s.behaviors.end()||attack->second.kind!=RuntimeBehaviorKind::Attack||attack->second.targetObjectId!=f.host
                ||s.pendingTransportTargets.contains(f.actor)||s.specialMechanisms.chronoMoveState(f.actor))
                return fail("original CIVAN friendly IvanBomb Primary priority changed");
            if(!dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop))
                return fail("actual CIVAN Stop did not cancel original Ivan attack");
            sample(f,"UI-Stop-cancels-Ivan-Attack");
            if(s.behaviors.contains(f.actor)||s.pendingTransportTargets.contains(f.actor)||s.specialMechanisms.chronoMoveState(f.actor))
                return fail("CIVAN Stop retained old Ivan attack/transport/chrono intent");
            RuntimeTeamMissionState mission;mission.objectIds={f.host,f.actor};
            const auto issued=issueRuntimeTeamScriptStep(s,mission,{14,0});
            dispatched=issued==RuntimeTeamStepDisposition::Waiting;
            sample(f,"team-script-load14-explicit-transport-queued");
        } else sample(f,"UI-Primary-queued");
        const auto pending=s.pendingTransportTargets.find(f.actor);const auto move=s.behaviors.find(f.actor);
        if(pending==s.pendingTransportTargets.end()||pending->second!=f.host||move==s.behaviors.end()
            ||move->second.kind!=RuntimeBehaviorKind::Move||move->second.pendingAllowedOccupiedObjectId!=f.host
            ||contained(s,f.host,f.actor)||s.specialMechanisms.chronoMoveState(f.actor)||!noWalk(f)) {
            std::ostringstream detail;detail<<"actual entry queue missing/early warp dispatched="<<dispatched
                <<" valid="<<s.lastCommand.valid<<" accepted="<<s.lastCommand.acceptedCount<<" rejected="<<s.lastCommand.rejectedCount;
            return fail(detail.str());
        }
        return true;
    };
    const auto startWarp=[&](Fixture& f,int& deadline) {
        auto& s=*f.state;step(s);sample(f,"first-tick-WarpOut");
        const auto* p=findObjectById(s,f.actor);const auto* clock=s.specialMechanisms.chronoMoveState(f.actor);
        const int delay=expectedDelay(f.source,f.hull,s.config.fixedTickHz);
        if(!p||p->x!=f.source.x||p->y!=f.source.y||contained(s,f.host,f.actor)||!clock
            ||clock->phase!=ChronoPhase::WarpOut||clock->source.x!=f.source.x||clock->source.y!=f.source.y
            ||clock->destination.x!=f.hull.x||clock->destination.y!=f.hull.y
            ||clock->startTick!=s.tick||clock->completeTick!=s.tick+1||clock->cooldownUntilTick!=s.tick+1+delay
            ||effectCount(s,f.actor)!=1||!noWalk(f))return fail("first full tick did not start one source-to-hull warp with original absolute recovery");
        deadline=clock->cooldownUntilTick;
        const auto slot=worldObjectSubCellOffset(*p);
        const auto effect=std::find_if(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& e){return e.targetObjectId==f.actor&&e.lifecycleState=="ChronoWarpOut";});
        if(effect==s.impactEffects.end()||effect->animId!="WARPOUT"||!effect->hasWorldPosition
            ||effect->cell.x!=f.source.x||effect->cell.y!=f.source.y
            ||std::abs(effect->worldX-(f.source.x+slot.x))>1e-9||std::abs(effect->worldY-(f.source.y+slot.y))>1e-9)
            return fail("original departure WARPOUT is not at actual source subcell");
        return true;
    };
    const auto aboard=[&](Fixture& f,int deadline) {
        auto& s=*f.state;const auto* p=findRuntimeObjectByIdIncludingInactive(s,f.actor);const auto* h=findObjectById(s,f.host);
        const auto* clock=s.specialMechanisms.chronoMoveState(f.actor);
        if(!p||!h||p->alive||p->mission!="InsideTransport"||h->transportPassengerIds!=std::vector<int>{f.actor}
            ||s.pendingTransportTargets.contains(f.actor)||s.behaviors.contains(f.actor)||!clock
            ||clock->cooldownUntilTick!=deadline||clock->completeTick!=clock->startTick+1
            ||s.tick!=clock->startTick+1||effectCount(s,f.actor)!=2||!noWalk(f))return fail("next chrono tick did not commit exact manifest/retained deadline without ground body");
        const auto effect=std::find_if(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& e){return e.targetObjectId==f.actor&&e.lifecycleState=="ChronoWarpIn";});
        if(effect==s.impactEffects.end()||effect->animId!="WARPOUT"||!effect->hasWorldPosition
            ||effect->cell.x!=f.hull.x||effect->cell.y!=f.hull.y
            ||std::abs(effect->worldX-f.hull.x)>1e-9||std::abs(effect->worldY-f.hull.y)>1e-9)
            return fail("original arrival WARPOUT is not at hull center");
        if(f.type=="CLEG"&&f.hostType=="FV"&&(h->activeGunnerMode!=10||h->activeGunnerWeaponIndex!=10))
            return fail("CLEG actual FV gunner mode is not original10");
        return true;
    };
    const auto capture=[&](Fixture& f,const std::string& event) {
        if(!drawRuntimeStateOffscreenAtCell(*f.state,{60,64},800,600,error,root+"/"+scene+"-"+event+".bmp",false))
            return fail("original scene capture: "+error);
        ++captures;return true;
    };
    const auto clockEqual=[](const ChronoMoveState* a,const ChronoMoveState* b) {
        if(!a||!b)return a==b;
        return a->objectId==b->objectId&&a->phase==b->phase&&a->source.x==b->source.x&&a->source.y==b->source.y
            &&a->destination.x==b->destination.x&&a->destination.y==b->destination.y&&a->sourceLevel==b->sourceLevel
            &&a->destinationLevel==b->destinationLevel&&a->startTick==b->startTick&&a->completeTick==b->completeTick
            &&a->cooldownUntilTick==b->cooldownUntilTick;
    };
    const auto same=[&](const RuntimeWindowState& s,const RuntimeWindowState& copy,int host,int actor) {
        const auto* a=findRuntimeObjectByIdIncludingInactive(s,actor);const auto* b=findRuntimeObjectByIdIncludingInactive(copy,actor);
        const auto* h=findRuntimeObjectByIdIncludingInactive(s,host);const auto* k=findRuntimeObjectByIdIncludingInactive(copy,host);
        if(!a||!b||!h||!k||s.tick!=copy.tick||a->alive!=b->alive||a->health!=b->health||a->mission!=b->mission
            ||a->x!=b->x||a->y!=b->y||a->subCell!=b->subCell||a->renderMoveActive!=b->renderMoveActive
            ||std::abs(runtimeObjectRenderX(*a)-runtimeObjectRenderX(*b))>1e-9
            ||std::abs(runtimeObjectRenderY(*a)-runtimeObjectRenderY(*b))>1e-9
            ||h->transportPassengerIds!=k->transportPassengerIds
            ||!clockEqual(s.specialMechanisms.chronoMoveState(actor),copy.specialMechanisms.chronoMoveState(actor)))
            return fail("complete saved-vs-live chrono body/manifest/absolute clock differs");
        const auto p=s.pendingTransportTargets.find(actor),q=copy.pendingTransportTargets.find(actor);
        const auto m=s.behaviors.find(actor),n=copy.behaviors.find(actor);
        if((p==s.pendingTransportTargets.end())!=(q==copy.pendingTransportTargets.end())
            ||(p!=s.pendingTransportTargets.end()&&p->second!=q->second)
            ||(m==s.behaviors.end())!=(n==copy.behaviors.end())
            ||(m!=s.behaviors.end()&&(m->second.kind!=n->second.kind||m->second.pendingAllowedOccupiedObjectId!=n->second.pendingAllowedOccupiedObjectId
                ||m->second.moveTarget!=n->second.moveTarget||m->second.transportTransferKind!=n->second.transportTransferKind)))
            return fail("complete saved-vs-live exact transport intent/permission differs");
        return true;
    };
    struct Replicas {std::unique_ptr<RuntimeWindowState> ordinary,canonical;};
    const auto checkpoint=[&](Fixture& f,const std::string& phase) {
        auto copies=std::make_unique<Replicas>();auto& s=*f.state;
        const auto ordinaryPath=root+"/"+scene+"-"+phase+"-ordinary.ini";
        if(!saveRuntimeState(s,ordinaryPath,error))return std::unique_ptr<Replicas>{};
        copies->ordinary=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copies->ordinary,s,false);
        if(!loadRuntimeState(*copies->ordinary,ordinaryPath,error))return std::unique_ptr<Replicas>{};
        initializeRuntimeOfflineGameplayRouting(*copies->ordinary);
        std::ifstream input(ordinaryPath,std::ios::binary);const std::string bytes{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
        std::ostringstream extension,normalized;extension<<bytes;IniFile snapshot;
        if(!writeRuntimeMultiplayerSnapshotExtension(s,extension,error)||!snapshot.loadFromString(extension.str())
            ||!writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot,normalized,error))return std::unique_ptr<Replicas>{};
        const auto imagePath=root+"/"+scene+"-"+phase+"-canonical.ini";
        {std::ofstream image(imagePath,std::ios::binary);image<<normalized.str();image.close();if(!image.good()){error="canonical complete image disk";return std::unique_ptr<Replicas>{};}}
        IniFile image;if(!image.loadFromFile(imagePath))return std::unique_ptr<Replicas>{};
        copies->canonical=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*copies->canonical,s,false);
        if(!loadRuntimeState(*copies->canonical,ordinaryPath,error))return std::unique_ptr<Replicas>{};
        {RuntimeSnapshotActiveStateScope active(*copies->canonical);if(!materializeRuntimeMultiplayerSnapshotExtension(*copies->canonical,image,error))return std::unique_ptr<Replicas>{};}
        initializeRuntimeOfflineGameplayRouting(*copies->canonical);
        if(!same(s,*copies->ordinary,f.host,f.actor)||!same(s,*copies->canonical,f.host,f.actor))return std::unique_ptr<Replicas>{};
        saveFiles+=2;return copies;
    };
    const auto pairStep=[&](Fixture& f,Replicas* copies) {
        step(*f.state);sample(f,"simulation");
        if(copies)for(auto* copy:{copies->ordinary.get(),copies->canonical.get()}) {
            step(*copy);if(!same(*f.state,*copy,f.host,f.actor))return false;++pairedTicks;
        }
        return noWalk(f);
    };
    const auto row=[&](Fixture& f,int deadline) {
        auto& s=*f.state;cases<<quote(scene)<<','<<f.type<<','<<f.hostType<<','<<f.actor<<','<<f.host<<','<<s.tick<<','
            <<expectedDelay(f.source,f.hull,s.config.fixedTickHz)<<','<<deadline<<','<<effectCount(s,f.actor)<<','<<contained(s,f.host,f.actor)
            <<','<<s.lastCommand.acceptedCount<<','<<s.lastCommand.rejectedCount<<','<<(f.type=="CIVAN"?"team-script-load14-after-Primary-Ivan-Stop":"UI-Primary")<<",1\n";cases.flush();++fixtureCount;
    };    const auto core=[&](const std::string& type,const std::string& hostType,int distance,bool waterHost,bool saveDeparture,bool saveCargo) {
        scene=type+"-"+hostType+(distance==1?"-near":"-far")+(waterHost?"-shore":"");
        auto f=make(type,hostType,distance,waterHost);auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        if(f.actor<0||f.host<0)return fail("stock actors");
        if(hostType=="SHAD") {
            bool landed=false;for(int n=0;n<600;++n) {
                const auto* h=findObjectById(s,f.host);if(h&&!h->airborne&&!h->renderMoveActive){landed=true;break;}step(s);
            }
            if(!landed)return fail("original SHAD never reached real landed/static pose");
        }
        if(!queue(f))return false;int deadline=0;if(!startWarp(f,deadline))return false;
        if(saveDeparture) {
            const auto before=s.specialMechanisms.chronoMoveState(f.actor);const ChronoMoveState held=*before;
            const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{60,68});
            sample(f,"WarpOut-NewMove-rejected");
            const auto pending=s.pendingTransportTargets.find(f.actor);
            if(dispatched||s.lastCommand.acceptedCount!=0||s.lastCommand.rejectedCount!=1||pending==s.pendingTransportTargets.end()
                ||pending->second!=f.host||!clockEqual(&held,s.specialMechanisms.chronoMoveState(f.actor))||!noWalk(f))
                return fail("locked WarpOut NewMove bypassed recovery or stole original boarding reservation");
        }
        if((saveDeparture||waterHost)&&!capture(f,"WarpOut"))return false;
        std::unique_ptr<Replicas> copies;
        if(saveDeparture) {copies=checkpoint(f,"WarpOut");if(!copies)return fail("full departure checkpoint: "+error);}
        if(!pairStep(f,copies.get())||!aboard(f,deadline))return false;sample(f,"next-tick-manifest");
        if((saveDeparture||waterHost)&&!capture(f,"aboard"))return false;
        if(saveCargo) {
            for(int n=0;n<6;++n){if(!pairStep(f,nullptr))return false;const auto* p=findRuntimeObjectByIdIncludingInactive(s,f.actor);
                if(!p||p->mission!="InsideTransport")return fail("cargo recovery changed contained mission");}
            copies=checkpoint(f,"contained-recovery");if(!copies)return fail("full cargo recovery checkpoint: "+error);
        }
        for(int n=0;n<(copies?12:3);++n) {
            if(!pairStep(f,copies.get()))return false;
            const auto* p=findRuntimeObjectByIdIncludingInactive(s,f.actor);const auto* clock=s.specialMechanisms.chronoMoveState(f.actor);
            if(!p||p->alive||p->mission!="InsideTransport"||!clock||clock->cooldownUntilTick!=deadline)
                return fail("cargo lost original absolute recovery clock or InsideTransport mission");
        }
        if(distance==1) {
            // Unload through the actual Secondary, while original personal recovery still has time remaining.
            if(s.tick>=deadline)return fail("near unload did not exercise remaining recovery");
            s.selectedObjectIds={f.host};
            if(!dispatchRuntimeSecondaryActionFromUi(s,f.host))return fail("real early unload Secondary rejected");
            sample(f,"unload-during-recovery");
            const auto* clock=s.specialMechanisms.chronoMoveState(f.actor);
            if(!clock||clock->cooldownUntilTick!=deadline)return fail("unload restarted/dropped original absolute recovery");
            const auto* released=findObjectById(s,f.actor);
            if(!released||contained(s,f.host,f.actor)||runtimeObjectCanMove(s,*released))return fail("unloaded passenger bypassed remaining recovery lock");
            s.selectedObjectIds={f.actor};const auto retained=s.specialMechanisms.chronoMoveState(f.actor)->cooldownUntilTick;
            (void)dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{60,68});
            sample(f,"unloaded-frozen-Move-rejected");
            if(s.lastCommand.acceptedCount!=0||s.lastCommand.rejectedCount!=1
                ||!s.specialMechanisms.chronoMoveState(f.actor)||s.specialMechanisms.chronoMoveState(f.actor)->cooldownUntilTick!=retained)
                return fail("unloaded recovery accepted a fresh destination or changed deadline");
            while(s.tick<deadline-1) {
                step(s);sample(f,"remaining-recovery");const auto* p=findObjectById(s,f.actor);const auto* state=s.specialMechanisms.chronoMoveState(f.actor);
                if(!p||!state||state->cooldownUntilTick!=deadline||runtimeObjectCanMove(s,*p))return fail("unloaded original recovery thawed early");
            }
            step(s);sample(f,"original-recovery-deadline");
            released=findObjectById(s,f.actor);
            if(!released||s.specialMechanisms.isChronoMovementLocked(f.actor,s.tick))return fail("unloaded original recovery missed exact deadline");
            ++unloadControls;++minimumControls;
        } else ++coreCount;
        row(f,deadline);return true;
    };
    for(const std::string type:{"CLEG","CCOMAND","CIVAN"})for(const std::string host:{"FV","BFRT"})
        if(!core(type,host,8,false,type=="CLEG"&&host=="FV",type=="CLEG"&&host=="BFRT"))return false;
    if(!core("CLEG","FV",1,false,false,false)||!core("CLEG","LCRF",8,true,false,false)
        ||!core("CLEG","SHAD",8,false,false,false))return false;
    {
        scene="CLEG-FV-one-slot-reservation-and-full";auto f=make("CLEG","FV");auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        if(f.actor<0||f.host<0||!queue(f))return false;
        auto* extra=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"CCOMAND",{56,66});
        if(!extra)return fail("second original teleporter");const int other=extra->instanceId;
        extra->owner=s.economy.owner;extra->mission="Guard";refreshRuntimeObjectOccupancyAfterLocalChange(s);
        const auto rejectOther=[&](const char* event) {
            s.selectedObjectIds={other};const auto* h=findObjectById(s,f.host);
            const bool accepted=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*h),h);
            sample(f,event);
            if(accepted||s.lastCommand.acceptedCount!=0||s.lastCommand.rejectedCount!=1||s.pendingTransportTargets.contains(other)
                ||s.specialMechanisms.chronoMoveState(other)||!findObjectById(s,other))return fail("second teleporter stole reserved/full original FV slot");
            return true;
        };
        if(!rejectOther("second-Primary-reservation-rejected"))return false;
        int deadline=0;if(!startWarp(f,deadline))return false;step(s);sample(f,"reserved-winner-manifest");
        if(!aboard(f,deadline)||!rejectOther("second-Primary-full-rejected"))return false;
        row(f,deadline);
    }
    {
        scene="CLEG-FV-Stop-in-WarpOut";auto f=make("CLEG","FV");auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        if(f.actor<0||f.host<0||!queue(f))return false;int deadline=0;if(!startWarp(f,deadline))return false;
        s.selectedObjectIds={f.actor};const bool stopped=dispatchRuntimeSelectionCommandFromUi(s,RuntimeMultiplayerGameplayCommandType::Stop);
        sample(f,"actual-Stop-WarpOut");
        if(!stopped||s.pendingTransportTargets.contains(f.actor)||s.specialMechanisms.chronoMoveState(f.actor)||!noWalk(f))
            return fail("Stop did not release exact reservation and in-transit warp while retaining source");
        step(s);sample(f,"Stop-arrival-rejected");const auto* p=findObjectById(s,f.actor);
        if(!p||p->x!=f.source.x||p->y!=f.source.y||contained(s,f.host,f.actor)||s.pendingTransportTargets.contains(f.actor)
            ||effectCount(s,f.actor)!=1||!noWalk(f))return fail("canceled old-host warp moved/boarded or leaked reservation");
        row(f,deadline);++rejectionCount;
    }
    {
        scene="CLEG-FV-NewMove-before-WarpOut";auto f=make("CLEG","FV");auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        if(f.actor<0||f.host<0||!queue(f))return false;
        const RuntimeCellPoint destination{54,68};s.selectedObjectIds={f.actor};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,destination))return fail("queued entry NewMove command rejected");
        sample(f,"actual-NewMove-replaced-entry");
        const auto* clock=s.specialMechanisms.chronoMoveState(f.actor);
        if(s.pendingTransportTargets.contains(f.actor)||!clock||clock->destination.x!=destination.x||clock->destination.y!=destination.y
            ||clock->completeTick!=s.tick+1||clock->cooldownUntilTick!=s.tick+1+expectedDelay(f.source,destination,s.config.fixedTickHz)||!noWalk(f))
            return fail("NewMove did not cancel old entry and retain ordinary original blink timing");
        const int deadline=clock->cooldownUntilTick;
        const auto rejectLockedPrimary=[&](const char* event) {
            const ChronoMoveState original=*s.specialMechanisms.chronoMoveState(f.actor);
            const auto behavior=s.behaviors.find(f.actor);
            const std::optional<RuntimeObjectBehavior> held=behavior==s.behaviors.end()?std::nullopt:std::optional<RuntimeObjectBehavior>{behavior->second};
            const auto* host=findObjectById(s,f.host);s.selectedObjectIds={f.actor};
            const bool dispatched=dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*host),host);
            sample(f,event);const auto retained=s.behaviors.find(f.actor);
            if(dispatched||s.lastCommand.acceptedCount!=0||s.lastCommand.rejectedCount!=1||s.pendingTransportTargets.contains(f.actor)
                ||!clockEqual(&original,s.specialMechanisms.chronoMoveState(f.actor))
                ||held.has_value()!=(retained!=s.behaviors.end())
                ||(held&&(held->kind!=retained->second.kind||held->moveTarget!=retained->second.moveTarget
                    ||held->pendingAllowedOccupiedObjectId!=retained->second.pendingAllowedOccupiedObjectId||held->path!=retained->second.path)))
                return fail("locked ordinary warp Primary stole destination/absolute clock or installed transport intent");
            return noWalk(f);
        };
        if(!rejectLockedPrimary("ordinary-WarpOut-host-Primary-rejected"))return false;
        step(s);sample(f,"new-ground-destination-arrival");const auto* p=findObjectById(s,f.actor);
        if(!p||p->x!=destination.x||p->y!=destination.y||contained(s,f.host,f.actor)||s.pendingTransportTargets.contains(f.actor)||!noWalk(f))
            return fail("NewMove replacement still entered old host or walked");
        if(!rejectLockedPrimary("ordinary-recovery-host-Primary-rejected"))return false;
        row(f,deadline);++rejectionCount;
    }    {
        scene="CLEG-FV-host-real-Move";auto f=make("CLEG","FV");auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        if(f.actor<0||f.host<0||!queue(f))return false;
        int deadline=0;if(!startWarp(f,deadline))return false;
        s.selectedObjectIds={f.host};
        if(!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{68,64}))return fail("host ordinary Move rejected");
        sample(f,"host-real-Move-cancels-WarpOut-entry");
        const auto hostOrder=s.behaviors.find(f.host);
        if(!s.lastCommand.valid||s.lastCommand.verb!="move"||s.lastCommand.acceptedCount!=1||s.lastCommand.rejectedCount!=0
            ||hostOrder==s.behaviors.end()||hostOrder->second.kind!=RuntimeBehaviorKind::Move||hostOrder->second.moveTarget!=RuntimeCellPoint{68,64})
            return fail("real host Move did not accept exactly one actual destination");
        if(s.pendingTransportTargets.contains(f.actor)||s.specialMechanisms.chronoMoveState(f.actor)||!noWalk(f))
            return fail("host accepted Move retained stale personal boarding intent");
        bool realMotion=false;
        // Original FV ROT=5: initial facing0 -> +x facing96 needs19 rule frames (38 ticks at30Hz), before body motion.
        for(int n=0;n<std::max(1,s.config.fixedTickHz)*3&&!realMotion;++n) {
            step(s);sample(f,"host-moving-source-held");const auto* p=findObjectById(s,f.actor);const auto* h=findObjectById(s,f.host);
            if(!p||!h||p->x!=f.source.x||p->y!=f.source.y||contained(s,f.host,f.actor)
                ||s.pendingTransportTargets.contains(f.actor)||s.specialMechanisms.chronoMoveState(f.actor)||effectCount(s,f.actor)!=1||!noWalk(f))
                return fail("moving host caused stale landing/cargo or walking");
            realMotion=h->renderMoveActive&&(std::abs(runtimeObjectRenderX(*h)-f.hull.x)>1e-9||std::abs(runtimeObjectRenderY(*h)-f.hull.y)>1e-9);
        }
        if(!realMotion)return fail("host Move never produced actual continuous motion");
        row(f,deadline);++rejectionCount;
    }
    for(const bool changeOwner:{false,true}) {
        scene=changeOwner?"CLEG-FV-host-owner-transition-in-WarpOut":"CLEG-FV-host-destroyed-in-WarpOut";
        auto f=make("CLEG","FV");auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        if(f.actor<0||f.host<0||!queue(f))return false;int deadline=0;if(!startWarp(f,deadline))return false;
        if(changeOwner) {
            const auto changed=applyRuntimeOwnerTransitionBatch(s,{{f.host,"Americans","Russians"}},RuntimeOwnerTransitionRepairPolicy::ReleaseRelations,"issue25-chrono-transport-owner-control");
            const auto* h=findObjectById(s,f.host);
            if(!changed.applied||!h||h->owner!="Russians")return fail("real host owner transition failed");
        } else {
            auto* h=findMutableObjectById(s,f.host);if(!h)return fail("host before real destruction");
            destroyRuntimeCampaignObject(s,*h,"Russians","issue25-chrono-transport-death-control");
            if(findObjectById(s,f.host))return fail("real host lifecycle did not destroy live host");
        }
        sample(f,"host-invalidates-exact-WarpOut-entry");
        if(s.pendingTransportTargets.contains(f.actor)||s.specialMechanisms.chronoMoveState(f.actor)||contained(s,f.host,f.actor))
            return fail("host lifecycle retained exact pending/warp/cargo");
        step(s);sample(f,"invalid-host-arrival-source-held");const auto* p=findObjectById(s,f.actor);
        if(!p||p->owner!="Americans"||p->x!=f.source.x||p->y!=f.source.y||contained(s,f.host,f.actor)
            ||s.pendingTransportTargets.contains(f.actor)||s.specialMechanisms.chronoMoveState(f.actor)||effectCount(s,f.actor)!=1||!noWalk(f))
            return fail("invalid former host caused stale landing/ownership/cargo or walking");
        row(f,deadline);++rejectionCount;
    }
    {
        scene="E1-FV-original-walking-positive";auto f=make("E1","FV");auto& s=*f.state;RuntimeSnapshotActiveStateScope active(s);
        if(f.actor<0||f.host<0)return fail("original E1/FV actors");
        const auto* h=findObjectById(s,f.host);s.selectedObjectIds={f.actor};
        (void)dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*h),h);
        sample(f,"ordinary-Primary-queued");
        const auto pending=s.pendingTransportTargets.find(f.actor);const auto intent=s.behaviors.find(f.actor);
        if(pending==s.pendingTransportTargets.end()||pending->second!=f.host||intent==s.behaviors.end()
            ||intent->second.kind!=RuntimeBehaviorKind::Move||intent->second.pendingAllowedOccupiedObjectId!=f.host)
            return fail("ordinary E1 actual entry command unavailable");
        bool groundMotion=false,boardingMotion=false,complete=false;
        for(int n=0;n<1800;++n) {
            step(s);sample(f,"ordinary-ground-and-Boarding");const auto* p=findRuntimeObjectByIdIncludingInactive(s,f.actor);
            const auto move=s.behaviors.find(f.actor);
            if(!p||s.specialMechanisms.chronoMoveState(f.actor)||effectCount(s,f.actor)!=0)return fail("ordinary E1 acquired chrono transfer");
            if(p->alive&&p->renderMoveActive) {
                if(move!=s.behaviors.end()&&move->second.transportTransferKind==RuntimeTransportTransferKind::Boarding)boardingMotion=true;
                else if(std::abs(runtimeObjectRenderX(*p)-f.source.x)>1e-9||std::abs(runtimeObjectRenderY(*p)-f.source.y)>1e-9)groundMotion=true;
            }
            if(contained(s,f.host,f.actor)) {complete=true;break;}
        }
        const auto* p=findRuntimeObjectByIdIncludingInactive(s,f.actor);
        if(!complete||!groundMotion||!boardingMotion||!p||p->alive||p->mission!="InsideTransport"
            ||s.pendingTransportTargets.contains(f.actor)||s.behaviors.contains(f.actor))
            return fail("ordinary E1 did not retain actual ground route/final Boarding curve and final manifest");
        row(f,0);++ordinaryControls;
    }
    if(fixtureCount!=16||coreCount!=8||minimumControls!=1||unloadControls!=1||rejectionCount!=5||ordinaryControls!=1
        ||saveFiles!=4||pairedTicks!=50||captures!=4)return fail("focused fixture/save/paired-tick/bitmap coverage count");
    std::ofstream result(root+"/result.txt");
    result<<"PASS\nfixtures="<<fixtureCount<<"\ncore="<<coreCount<<"\nnear_minimum="<<minimumControls
        <<"\nreservation_and_full=1\nsource_retention_controls="<<rejectionCount<<"\nordinary_walk="<<ordinaryControls
        <<"\nactual_early_unload="<<unloadControls<<"\nsave_files="<<saveFiles<<"\npaired_replica_ticks="<<pairedTicks
        <<"\nbitmaps="<<captures<<"\noriginal_transport_catalog=11\npersonal_infantry_catalog=3\n";
    if(!result.good())return fail("terminal evidence write");
    std::cout<<"Issue25 chrono transport PASS fixtures="<<fixtureCount<<" core="<<coreCount<<" source_controls="<<rejectionCount
        <<" save="<<saveFiles<<" paired="<<pairedTicks<<" bitmap="<<captures<<'\n';
    return true;
}