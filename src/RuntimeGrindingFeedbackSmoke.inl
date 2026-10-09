bool runRuntimeGrindingFeedbackSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& why) { error = "grinder feedback: " + why; return false; };
    const auto make = [&](const char* type, int hz) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = live.config; s.config.multiplayerActive = false; s.config.computerAiEnabled = false;
        s.config.fixedTickHz = hz; s.config.playerSlots.clear(); s.config.crates = false;
        s.report = live.report; s.art = live.art; s.theater = live.theater;
        IniFile rules; if (!rules.loadFromFile(live.report.rulesPath) || !s.rules.loadFromIni(rules)) return std::unique_ptr<RuntimeWindowState>{};
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.world = live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.economy.owner = "YuriCountry"; s.economy.credits = 1000; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.visionEnabled = false;
        s.runtimeSoundsEnabled = false; s.runtimeSoundEffectsEnabled = true; s.runtimeSoundVolume = 100;
        s.runtimePreferencePersistenceEnabled = false; s.runtimeSuppressPlayerAudioFeedback = false;
        for (int y = 1; y <= 26; ++y) for (int x = 1; x <= 26; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(cell.cellIndex512, RuntimeTerrainInfo{});
        }
        WorldObjectInstance host; host.instanceId = 1; host.kind = WorldObjectKind::Structure; host.type = "YAGRND";
        host.owner = s.economy.owner; host.alive = true; host.health = 900; host.poweredOperational = true;
        host.mission = "Guard"; host.x = host.y = 10; host.cell = runtimeCellKey(10,10);
        auto actor = host; actor.instanceId = 2; actor.type = type;
        actor.kind = std::string(type) == "E1" || std::string(type) == "ENGINEER" ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        actor.authoredAircraft = std::string(type) == "ORCA";
        actor.airborne = actor.authoredAircraft; actor.currentAmmo = -1;
        actor.health = runtimeMaxHealthForObject(s, actor); actor.x = 7; actor.y = 13; actor.cell = runtimeCellKey(7,13);
        auto power = host; power.instanceId = 100; power.type = "YAPOWR";
        power.x = power.y = 20; power.cell = runtimeCellKey(20,20); power.health = 700;
        s.world.accessObjects() = {host, actor, power};
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        ensureRuntimeSpecialMechanismsInitialized(s);
        if (!loadRuntimeSoundDatabase(s)) return std::unique_ptr<RuntimeWindowState>{};
        s.selectedObjectIds = {2}; return storage;
    };
    const auto countSound = [](const RuntimeWindowState& s, const std::string& section) {
        return std::count_if(s.runtimeSoundPlaybackHistory.begin(), s.runtimeSoundPlaybackHistory.end(),
            [&](const auto& record) { return record.section == section; });
    };
    const auto hasScold = [](const RuntimeWindowState& s) {
        return std::any_of(s.runtimeSoundIntentHistory.begin(), s.runtimeSoundIntentHistory.end(),
            [](const std::string& intent) { return intent == "blocked" || intent == runtimeSoundIntentSidebarScold; });
    };
    const auto click = [](RuntimeWindowState& s) {
        const auto* host = findObjectById(s, 1);
        return dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            runtimeCellPointForObject(*host), host);
    };
    int cases = 0, saves = 0;
    for (int hz : {15,30,60}) for (const char* type : {"E1","HTNK"}) for (bool routed : {false,true}) {
        auto storage = make(type,hz); if (!storage) return fail("stock resources"); auto& s = *storage;
        RuntimeSnapshotActiveStateScope active(s);
        int captures = 0;
        if (routed) {
            // Exercise the same UI capture/result path as offline recording;
            // full file recording/reopening is covered by the replay smoke.
            initializeRuntimeOfflineGameplayRouting(s);
            s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command, std::string&) {
                ++captures;
                return applyRuntimePlayerTargetOrder(s, s.economy.owner,
                    std::get<RuntimeMultiplayerGameplayTargetOrderCommand>(command.body), true);
            });
        }
        if (runtimeTargetCursorIntent(s,*findObjectById(s,1)) != RuntimeTargetCursorIntent::Enter)
            return fail("initial entry cursor");
        if (!click(s) || s.lastCommand.verb != "grind" || s.lastCommand.acceptedCount != 1
            || hasScold(s) || countSound(s,"GrinderGrinding") != 0) return fail("successful entry was rejected or sounded too early");
        const auto authority = s.grinding.stateHash();
        for (int i = 0; i < 3; ++i) {
            if (runtimeTargetCursorIntent(s,*findObjectById(s,1)) != RuntimeTargetCursorIntent::Enter
                || !click(s) || s.grinding.stateHash() != authority || hasScold(s))
                return fail("queued cursor/repeated click changed authority or scolded");
        }
        if (routed && captures != 4) return fail("capture routing");
        s.offlineGameplayIntentRouting=false;
        std::ostringstream saved; IniFile ini; auto restored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*restored,s,false);
        if (!writeRuntimeStateToStream(s,saved,error) || !ini.loadFromString(saved.str())
            || !materializeRuntimeLoadStateFromIni(*restored,ini,error)) return fail("queued reload: " + error);
        restored->selectedObjectIds = {2};
        if (runtimeTargetCursorIntent(*restored,*findObjectById(*restored,1)) != RuntimeTargetCursorIntent::Enter
            || countSound(*restored,"GrinderGrinding") != 0) return fail("reload changed cursor or replayed audio");
        ++saves;
        // Observe the transient entering state independently of whether the
        // full simulation consumes it in the same tick as its arrival.
        auto* arriving = findMutableObjectById(*restored,2);
        const auto entry = restored->behaviors.at(2).moveTarget;
        arriving->x = entry.x; arriving->y = entry.y; arriving->cell = runtimeCellKey(entry.x,entry.y);
        clearRuntimeRenderPosition(*arriving);
        if (!completeRuntimeGrindingEntry(*restored,2)
            || runtimeTargetCursorIntent(*restored,*findObjectById(*restored,1)) != RuntimeTargetCursorIntent::Enter
            || !click(*restored)) return fail("entering cursor/repeated order");
        const auto* actor = findObjectById(s,2);
        const int expectedRefund = runtimeGrindingExpectedRefund(s,*actor,runtimeVeterancyObjectCost(s,*actor));
        for (int step = 0; step < hz*20 && findObjectById(s,2); ++step) {
            const auto* relation = runtimeGrindingRelationForSource(s.grinding,2);
            if (relation && relation->phase == RuntimeGrindingRelationPhase::Entering) {
                if (runtimeTargetCursorIntent(s,*findObjectById(s,1)) != RuntimeTargetCursorIntent::Enter
                    || !click(s)) return fail("entering cursor/repeated order");
            }
            ++s.tick; advanceRuntimeSimulation(s);
        }
        if (findObjectById(s,2) || s.economy.credits != 1000 + expectedRefund
            || countSound(s,"GrinderGrinding") != 1 || hasScold(s)) return fail(std::string(type)
                + " consumption/refund/one completion sound: hz=" + std::to_string(hz)
                + " credits=" + std::to_string(s.economy.credits) + " expected=" + std::to_string(1000+expectedRefund)
                + " sounds=" + std::to_string(countSound(s,"GrinderGrinding")));
        for (const auto& record : s.runtimeSoundPlaybackHistory) if (record.section == "GrinderGrinding") {
            if (!record.playable || record.sample.empty() || !record.spatial)
                return fail("native grinding sample or positional playback");
        }
        for (int step = 0; step < hz*2; ++step) { ++s.tick; advanceRuntimeGrinding(s); }
        if (countSound(s,"GrinderGrinding") != 1) return fail("completion sound repeated");
        ++cases;
        std::cout << "Grinder feedback " << type << " hz=" << hz << " routed=" << routed
            << " entry/repeat/cursor/load/refund/native-audio=ok\n";
    }
    // Invalid entries stay invalid, including after a stale successful command.
    for (const char* type : {"ENGINEER","ORCA","SUB","HTNK"}) {
        auto storage = make(type,30); if (!storage) return fail("negative resources"); auto& s = *storage;
        if (std::string(type) == "HTNK") findMutableObjectById(s,1)->poweredOperational = false;
        s.lastCommand.valid = true; s.lastCommand.acceptedCount = 1;
        if (runtimeTargetCursorIntent(s,*findObjectById(s,1)) != RuntimeTargetCursorIntent::NoEnter
            || click(s) || s.lastCommand.acceptedCount != 0 || !s.grinding.relations().empty()
            || countSound(s,"GrinderGrinding") != 0) return fail(std::string(type) + " invalid entry accepted");
    }
    // A mixed selection publishes the actual accepted count without a scold.
    auto group = make("HTNK",30); if (!group) return fail("group resources");
    RuntimeSnapshotActiveStateScope groupActive(*group);
    auto other = *findObjectById(*group,2); other.instanceId = 3; other.type = "E1";
    other.kind = WorldObjectKind::Infantry; other.health = runtimeMaxHealthForObject(*group,other);
    other.x = 6; other.cell = runtimeCellKey(6,13);
    group->world.accessObjects().push_back(other);
    other.instanceId = 4; other.type = "ENGINEER"; other.x = 5; other.cell = runtimeCellKey(5,13);
    other.health = runtimeMaxHealthForObject(*group,other);
    group->world.accessObjects().push_back(other); initializeRuntimeNavigation(*group);
    group->selectedObjectIds = {2,3,4};
    if (!click(*group) || group->lastCommand.acceptedCount != 2 || group->lastCommand.rejectedCount != 1
        || group->grinding.relations().size() != 2 || hasScold(*group)) return fail("mixed group acknowledgement");
    int groupSounds = 0;
    for (int step = 0; step < 600 && !group->grinding.relations().empty(); ++step) {
        ++group->tick; advanceRuntimeSimulation(*group);
        const auto context = makeRuntimeGrindingContext(*group);
        for (const auto& entry : group->grinding.relations()) {
            if (entry.phase == RuntimeGrindingRelationPhase::Entering
                && !context.isInsideHostFootprint(entry.hostId,entry.sourceId))
                return fail("group marked as entering outside the factory");
        }
        // Playback history retains only 32 records; count events as they occur.
        for (const auto& record : group->runtimeSoundPlaybackHistory)
            if (record.tick == group->tick && record.section == "GrinderGrinding") ++groupSounds;
    }
    if (groupSounds != 2 || !group->grinding.relations().empty() || !findObjectById(*group,4)) {
        for (int id : {2,3,4}) if (const auto* actor = findObjectById(*group,id))
            std::cout << "Grinder group remaining id=" << id << " type=" << actor->type
                << " cell=" << actor->x << ',' << actor->y << " mission=" << actor->mission << '\n';
        for (const auto& [id, behavior] : group->behaviors)
            std::cout << "Grinder group path id=" << id << " goal=" << behavior.moveTarget.x << ',' << behavior.moveTarget.y
                << " pending=" << behavior.pathRequestPending << " blocked=" << behavior.blockedTicks
                << " allowed=" << behavior.pendingAllowedOccupiedObjectId << " nearby=" << behavior.pendingAllowNearbyTarget
                << " pathSize=" << behavior.path.size() << " status=" << group->simulationStatus << '\n';
        return fail("mixed group recycling count=" + std::to_string(groupSounds)
            + " retainedHistory=" + std::to_string(countSound(*group,"GrinderGrinding"))
            + " remaining=" + std::to_string(group->grinding.relations().size())
            + " tankAlive=" + std::to_string(findObjectById(*group,2)!=nullptr)
            + " infantryAlive=" + std::to_string(findObjectById(*group,3)!=nullptr)
            + " engineerAlive=" + std::to_string(findObjectById(*group,4)!=nullptr));
    }
    std::cout << "Grinder mixed group: events=" << groupSounds
        << " retainedHistory=" << countSound(*group,"GrinderGrinding") << " passed\n";
    std::cout << "Grinder feedback: " << cases << " native command/cursor/audio cases; " << saves
        << " queued reloads; repeat clicks, rejected units/power and mixed group passed\n";
    return true;
}
