bool runRuntimeGuardRotationSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& reason) { error = "Guard rotation: " + reason; return false; };
    auto stock = std::make_unique<RuntimeWindowState>(); stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original rules unavailable");
    const auto make = [&](bool pressure, bool dense = false) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = config; s.config.playerSlots.clear(); s.config.computerAiEnabled = false;
        s.config.multiplayerActive = false; s.config.shortGame = false;
        s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater; s.report = stock->report;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = "Americans"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true; s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 64; ++y) for (int x = 1; x <= 64; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        for (int i = 0; i < 80; ++i)
            addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{1+i%10,1+i/10});
        addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{50,50});
        addRuntimeObjectInstanceForOwner(s,"Russians",RuntimeBuildKind::Unit,"HTNK",dense ? RuntimeCellPoint{5,5} : RuntimeCellPoint{54,50});
        for (auto& object : s.world.accessObjects()) object.mission = object.owner == "Americans" ? "Guard" : "Sleep";
        if (pressure) {
            RuntimeObjectBehavior move; move.kind=RuntimeBehaviorKind::Move;
            move.state=RuntimeBehaviorState::Moving; move.moveTarget={12,1};
            s.behaviors[s.world.getObjects().front().instanceId]=move;
            s.world.accessObjects().front().mission="Moving";
        }
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s);
        return box;
    };
    const auto scan = [](RuntimeWindowState& s) { s.tick += 8; advanceRuntimeGuardBehavior(s); };
    int cases = 0;
    for (bool pressure : {false,true}) for (bool reverse : {false,true}) {
        auto box=make(pressure);auto& s=*box;
        const int front=s.world.getObjects()[80].instanceId, target=s.world.getObjects()[81].instanceId;
        const int limit=pressure ? 11 : 3;
        if(reverse)std::reverse(s.world.accessObjects().begin(),s.world.accessObjects().end());
        int scans=0;
        for(;scans<limit && !s.behaviors.contains(front);++scans)scan(s);
        if(!s.behaviors.contains(front) || s.behaviors.at(front).targetObjectId!=target)
            return fail("late-created front tank starved at budget " + std::to_string(pressure?8:32));
        if(scans!=limit && !pressure) return fail("32-slot scan did not retain its budget");
        ++cases;
        std::cout<<"Guard rotation: budget="<<(pressure?8:32)<<" reverse="<<reverse<<" front acquired after "<<scans<<" scans\n";
    }
    for(bool pressure : {false,true}) {
        auto box=make(pressure,true);auto& s=*box;
        scan(s);
        int attacks=0;
        for(const auto& [id,behavior]:s.behaviors)
            if(behavior.kind==RuntimeBehaviorKind::Attack && behavior.autoAcquired)++attacks;
        if(attacks<1 || attacks>(pressure?8:32))return fail("mobile scan cap exceeded or no close target acquired");
        ++cases;
    }
    {
        auto box=make(true);auto& s=*box;
        const int front=s.world.getObjects()[80].instanceId;
        for(int i=0;i<40;++i) {
            const int id=s.world.getObjects()[i].instanceId;
            s.playerCommands.travelling[id]={"americans",{12,12},
                RuntimeMultiplayerGameplayTargetOrderKind::AttackMove,false,0};
        }
        for(int rounds=0;rounds<6 && !s.behaviors.contains(front);++rounds)scan(s);
        if(!s.behaviors.contains(front))return fail("travelling units consumed ordinary mobile slots");
        ++cases;
    }
    {
        auto box=make(true);auto& s=*box;
        const int front=s.world.getObjects()[80].instanceId;
        scan(s);
        const int removed=s.combatDetails.guardScanAfterObjectId;
        auto& objects=s.world.accessObjects();
        objects.erase(std::remove_if(objects.begin(),objects.end(),[&](const auto& object){return object.instanceId==removed;}),objects.end());
        std::reverse(objects.begin(),objects.end());initializeRuntimeNavigation(s);
        int rounds=0;
        for(;rounds<11 && !s.behaviors.contains(front);++rounds) {
            addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"MTNK",{12,12});
            scan(s);
        }
        if(!s.behaviors.contains(front))return fail("removal, reordering and new units starved the old front tank");
        ++cases;
    }
    {
        auto box=make(true);auto& s=*box;scan(s);
        std::ostringstream saved;s.combatDetails.save(saved);IniFile ini;ini.loadFromString(saved.str());
        auto restored=make(true);
        if(!restored->combatDetails.load(ini,error)
            || restored->combatDetails.deterministicHash()!=s.combatDetails.deterministicHash())return fail("cursor save/load: "+error);
        restored->tick=s.tick;std::reverse(restored->world.accessObjects().begin(),restored->world.accessObjects().end());
        scan(s);scan(*restored);
        if(restored->combatDetails.guardScanAfterObjectId!=s.combatDetails.guardScanAfterObjectId)
            return fail("restored cursor restarted or depended on object storage order");
        const auto before=restored->combatDetails.deterministicHash();
        auto text=saved.str();const auto at=text.find("GuardScanAfterObjectId=");
        const auto end=text.find('\n',at);text.replace(at,end-at,"GuardScanAfterObjectId=-2");
        IniFile bad;bad.loadFromString(text);
        if(restored->combatDetails.load(bad,error) || restored->combatDetails.deterministicHash()!=before)
            return fail("invalid cursor load was not rejected atomically");
        text=saved.str();text.erase(at,end-at+1);IniFile legacy;legacy.loadFromString(text);
        if(!restored->combatDetails.load(legacy,error) || restored->combatDetails.guardScanAfterObjectId!=-1)
            return fail("old save did not use initial cursor");
        error.clear();++cases;
    }
    {
        auto box=make(true);auto& s=*box;
        auto* defense=addRuntimeObjectInstance(s,RuntimeBuildKind::Defense,"GAPILL",{52,50});
        if(!defense)return fail("defense fixture");const int id=defense->instanceId;
        defense->buildAnimTicksRemaining=defense->buildAnimTicksTotal=0;defense->mission="Guard";
        initializeRuntimeNavigation(s);scan(s);
        if(!s.behaviors.contains(id))return fail("defense was delayed by mobile budget");
        ++cases;
    }
    std::cout<<"Guard rotation passed: "<<cases<<" cases; bounded service, 8/32 budgets, reorder, removal, insertion, save, legacy and defense priority\n";
    return true;
}
