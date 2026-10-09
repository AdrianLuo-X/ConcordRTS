bool runRuntimeDiskDrainSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& message) { error = "Disk drain: " + message; return false; };
    auto stock = std::make_unique<RuntimeWindowState>(); stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) return fail("original resources missing");
    stock->rulesLoaded = stock->artLoaded = stock->theaterLoaded = true;
    const auto* drainWeapon = stock->rules.findWeaponType("DiskDrain");
    if (!drainWeapon || !drainWeapon->drainWeapon || !drainWeapon->profile.drainWeapon
        || !stock->rules.findBuildingType("GAPOWR")->drainable
        || stock->rules.findBuildingType("GACNST")->drainable
        || stock->rules.getSpecialMechanismRules().drainMoneyFrameDelay != 30
        || stock->rules.getSpecialMechanismRules().drainMoneyAmount != 30
        || stock->rules.getSpecialMechanismRules().drainAnimationType != "DISKRAY")
        return fail("original drain flags, amounts or animation not parsed");
    std::string root = "build/smoke/feature-checks/01-disk-drain/checks";
    if (const char* output = std::getenv("CONCORD_DISK_DRAIN_DIR")) root = output;
    std::filesystem::create_directories(root);
    const auto make = [&](int hz, const std::string& type, bool distant = false) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        s.config = config; s.config.fixedTickHz = hz; s.config.computerAiEnabled = false;
        s.config.playerSlots.clear(); s.config.shortGame = false; s.config.noCooldown = false;
        s.config.multiplayerActive = false; s.config.superweapons = false;
        s.report = stock->report; s.rules = stock->rules; s.art = stock->art; s.theater = stock->theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
        s.economy.owner = "YuriCountry"; s.economy.credits = 500;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.runtimeSoundsEnabled = false; s.runtimeSoundEffectsEnabled = true; s.runtimeSoundVolume = 100;
        s.runtimeSuppressPlayerAudioFeedback = true; s.runtimePreferencePersistenceEnabled = false;
        for (int y = 1; y <= 32; ++y) for (int x = 1; x <= 32; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "DISK", {distant ? 3 : 10, 10});
        addRuntimeObjectInstanceForOwner(s, "Americans", RuntimeBuildKind::Building, type, {11,10});
        addRuntimeObjectInstanceForOwner(s, "Americans", RuntimeBuildKind::Building, "NANRCT", {20,20});
        addRuntimeObjectInstanceForOwner(s, "Americans", RuntimeBuildKind::Defense, "ATESLA", {25,25});
        for (auto& object : s.world.accessObjects()) {
            object.buildAnimTicksRemaining = object.buildAnimTicksTotal = 0;
            object.mission = object.type == "DISK" ? "Guard" : "Sleep";
        }
        auto& ai = s.computerAiByOwner["americans"];
        ai.initialized = true; ai.owner = ai.country = "Americans"; ai.credits = 1000;
        initializeRuntimeNavigation(s); rebuildRuntimeEconomyPower(s);
        return box;
    };
    const auto step = [](RuntimeWindowState& s) { ++s.tick; advanceRuntimeSimulation(s); };
    const auto begin = [&](RuntimeWindowState& s) {
        const int id = s.world.getObjects()[0].instanceId, target = s.world.getObjects()[1].instanceId;
        applySelection(s, {id}, false, false);
        issueRuntimeAttackCommand(s, *findObjectById(s,target));
        for (int i = 0; i < s.config.fixedTickHz * 20 && !s.combatDetails.diskDrains.sources.contains(id); ++i) step(s);
        return s.combatDetails.diskDrains.sources.contains(id);
    };
    int cases = 0;
    if (runtimeDiskDrainNextMoneyTick(1,30,30) != 60
        || runtimeDiskDrainNextMoneyTick(59,30,30) != 60
        || runtimeDiskDrainNextMoneyTick(60,30,30) != 120
        || runtimeDiskDrainNextMoneyTick(119,30,60) != 120)
        return fail("retail global-frame money cadence");
    for (int hz : {30,60}) for (const std::string type : {"GAPOWR","GAREFN","ATESLA"}) {
        auto box = make(hz,type); auto& s = *box;
        const int id = s.world.getObjects()[0].instanceId, targetId = s.world.getObjects()[1].instanceId;
        const int targetHealth = s.world.getObjects()[1].health;
        if (!begin(s)) return fail(type + " did not enter drain through a real attack command: " + s.simulationStatus);
        if (runtimeCellDistanceToObjectFootprint(s, runtimeCellPointForObject(*findObjectById(s,id)),
                *findObjectById(s,targetId)) > 0.001) return fail("drain started beside the building");
        completeRuntimeWorldRefresh(s);
        if (!s.projectiles.empty() || findObjectById(s,targetId)->health != targetHealth)
            return fail(type + " drain generated ordinary damage/projectile");
        const bool blackout = type == "GAPOWR";
        if (runtimeDiskDrainOwnerBlackout(s,"Americans") != blackout
            || runtimeSuperWeaponContext(s).ownerPowered("Americans") == blackout
            || runtimeObjectHasOperationalPower(s,s.world.getObjects()[3]) == blackout
            || !runtimeDiskDrainDisablesObject(s,targetId)) return fail(type + " wrong power scope");
        const auto record = s.combatDetails.diskDrains.sources.at(id);
        const int beforeSource = runtimeRepairDepotCreditBalance(s,"YuriCountry");
        const int beforeTarget = runtimeRepairDepotCreditBalance(s,"Americans");
        while (s.tick < record.nextMoneyTick - 1) step(s);
        if (runtimeRepairDepotCreditBalance(s,"YuriCountry") != beforeSource
            || runtimeRepairDepotCreditBalance(s,"Americans") != beforeTarget) return fail(type + " early transfer");
        step(s); advanceRuntimeDiskDrains(s,true); // Re-entering this tick cannot double debit.
        const int amount = type == "GAREFN" ? 30 : 0;
        if (runtimeRepairDepotCreditBalance(s,"YuriCountry") != beforeSource + amount
            || runtimeRepairDepotCreditBalance(s,"Americans") != beforeTarget - amount)
            return fail(type + " money transfer or duplicate-tick guard incorrect");
        if (findObjectById(s,targetId)->health != targetHealth) return fail(type + " drain caused damage over time");
        const auto* actors = runtimeImpactRenderActorsForRender(s);
        const auto visual = std::find_if(actors->begin(),actors->end(),[](const auto& a){return a.renderIntent=="diskDrain";});
        if (visual==actors->end() || visual->animId!="DISKRAY" || visual->targetObjectId!=id)
            return fail("native drain animation did not attach to the disc");
        if (hz == 30 && type == "GAPOWR") {
            RuntimeMapRenderContext context;
            context.rules = &s.rules; context.art = &s.art;
            context.assetRootPath = runtimeAssetRootPath(s.config);
            context.palettePath = runtimePalettePath(s.config, s.report.map);
            context.structurePalettePath = runtimeStructurePalettePath(s.config, s.report.map);
            if (!runRuntimeDiskDrainRenderContractCheck(context, s.world, *visual, root + "/drain-loop.bmp", error))
                return false;
        }
        std::ostringstream saved; s.combatDetails.save(saved); IniFile ini;
        ini.loadFromString(saved.str()); RuntimeCombatDetailsState restored;
        if (!restored.load(ini,error) || restored.deterministicHash()!=s.combatDetails.deterministicHash()
            || !restored.validateWorld(s.rules,s.world.getObjects(),s.tick,error)) return fail("relationship save: "+error);
        auto invalid = saved.str(); const auto at = invalid.find("Count=1",invalid.find("[DiskDrains]"));
        invalid.replace(at,7,"Count=2"); IniFile bad; bad.loadFromString(invalid);
        const auto hash = restored.deterministicHash();
        if (restored.load(bad,error) || restored.deterministicHash()!=hash) return fail("malformed relation load was not atomic");
        error.clear();
        if (type=="GAREFN") {
            step(s);
            const std::string path=root+"/draining-"+std::to_string(hz)+".ini";
            const auto before=s.combatDetails.deterministicHash();
            if (!saveRuntimeState(s,path,error)) return fail("full save: "+error);
            auto loaded=make(hz,type);
            if (!loadRuntimeState(*loaded,path,error) || loaded->combatDetails.deterministicHash()!=before)
                return fail("full reload: "+error);
            const int due=loaded->combatDetails.diskDrains.sources.at(id).nextMoneyTick;
            const int credit=runtimeRepairDepotCreditBalance(*loaded,"YuriCountry");
            while(loaded->tick<due)step(*loaded);
            if(runtimeRepairDepotCreditBalance(*loaded,"YuriCountry")!=credit+30)return fail("reload changed next debit");
        }
        eraseRuntimeBehavior(s,id); completeRuntimeWorldRefresh(s);
        if (!s.combatDetails.diskDrains.sources.empty() || runtimeDiskDrainOwnerBlackout(s,"Americans")
            || !runtimeObjectHasOperationalPower(s,s.world.getObjects()[3])) return fail("stop did not restore power");
        ++cases;
        std::cout<<"Disk drain case: "<<hz<<" Hz "<<type<<" command/power/credits/save/stop passed\n";
    }
    for (const std::string action : {"source-death","target-death","source-owner","target-owner","move","competing","elite","empty","far"}) {
        auto box=make(30,"GAREFN",action=="far");auto& s=*box;
        const int id=s.world.getObjects()[0].instanceId,target=s.world.getObjects()[1].instanceId;
        if(action=="elite")findMutableObjectById(s,id)->veterancy=2;
        if(!begin(s))return fail(action+" setup could not drain: "+s.simulationStatus);
        if(action=="source-death")findMutableObjectById(s,id)->alive=false;
        if(action=="target-death")findMutableObjectById(s,target)->alive=false;
        if(action=="source-owner")findMutableObjectById(s,id)->owner="Americans";
        if(action=="target-owner")findMutableObjectById(s,target)->owner="YuriCountry";
        if(action=="move") {applySelection(s,{id},false,false);issueRuntimeMoveCommand(s,{5,5});}
        if(action=="competing") {
            const auto* second=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"DISK",{10,11});
            if(!second)return fail("second disc fixture");
            const int secondId=second->instanceId;
            applySelection(s,{secondId},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,target));
            for(int i=0;i<120;++i)step(s);
            if(s.combatDetails.diskDrains.sources.size()!=1 || !s.combatDetails.diskDrains.sources.contains(id))
                return fail("two discs acquired the same building");
            eraseRuntimeBehavior(s,id);
            for(int i=0;i<120 && !s.combatDetails.diskDrains.sources.contains(secondId);++i)step(s);
            if(s.combatDetails.diskDrains.sources.size()!=1 || !s.combatDetails.diskDrains.sources.contains(secondId))
                return fail("waiting disc did not acquire released target");
        }
        if(action=="empty") {
            s.config.noCooldown=true;s.computerAiByOwner.at("americans").credits=7;
            const int credit=s.economy.credits,due=s.combatDetails.diskDrains.sources.at(id).nextMoneyTick;
            while(s.tick<due)step(s);
            if(s.economy.credits!=credit+7 || runtimeRepairDepotCreditBalance(s,"Americans")!=0)
                return fail("poor refinery minted credits or no-cooldown bypassed the interval");
        }
        advanceRuntimeDiskDrains(s,false);
        const bool ended=action=="source-death" || action=="target-death" || action=="source-owner" || action=="target-owner" || action=="move";
        if(s.combatDetails.diskDrains.sources.empty()!=ended)return fail(action+" relationship lifetime");
        ++cases;
    }
    {
        auto box=make(30,"GAPOWR"); auto& s=*box;
        if(!begin(s))return fail("two power plants setup");
        const int first=s.world.getObjects()[0].instanceId;
        const int otherPlant=s.world.getObjects()[2].instanceId;
        const auto* added=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"DISK",{19,20});
        if(!added)return fail("second power source fixture");
        const int second=added->instanceId;
        applySelection(s,{second},false,false);issueRuntimeAttackCommand(s,*findObjectById(s,otherPlant));
        for(int i=0;i<600 && !s.combatDetails.diskDrains.sources.contains(second);++i)step(s);
        if(s.combatDetails.diskDrains.sources.size()!=2)return fail("two independent power drains");
        eraseRuntimeBehavior(s,first);completeRuntimeWorldRefresh(s);
        if(!runtimeDiskDrainOwnerBlackout(s,"Americans"))return fail("first release restored power too early");
        eraseRuntimeBehavior(s,second);completeRuntimeWorldRefresh(s);
        if(runtimeDiskDrainOwnerBlackout(s,"Americans"))return fail("last release failed to restore power");
        ++cases;
    }
    {
        auto box=make(30,"GAREFN");auto& s=*box;
        if(!begin(s))return fail("human account setup");
        s.computerAiByOwner.erase("americans");
        RuntimeHumanHouseAuthority remote;
        remote.owner="Americans";remote.canonicalOwner="americans";remote.localFacade=false;
        remote.remoteEconomy.emplace();remote.remoteEconomy->owner="Americans";remote.remoteEconomy->credits=1000;
        s.humanHouseAuthorities.push_back(std::move(remote));
        const int before=s.economy.credits;
        s.tick=s.combatDetails.diskDrains.sources.begin()->second.nextMoneyTick;
        advanceRuntimeDiskDrains(s,true);
        if(s.economy.credits!=before+30 || runtimeRepairDepotCreditBalance(s,"Americans")!=970)
            return fail("other human account did not transfer exactly once");
        ++cases;
    }
    {
        auto box = make(30,"GAREFN"); auto& s = *box;
        if (!loadRuntimeSoundDatabase(s)) return fail("sound catalog missing");
        const auto* spec = s.worldAudioCatalog.find("FloatingDiscStealLoop");
        if (!spec || spec->samples.size() != 4 || !spec->loop) return fail("original drain sound definition");
        for (const auto& sample : spec->samples) {
            const auto path = findRuntimeSoundSamplePath(s,sample);
            if (!path || runtimeWavDurationTicks(*path,1000) <= 0) return fail("drain sample cannot decode: " + sample);
        }
        const auto plays = [&]() {
            std::vector<RuntimeWorldAudioAction> actions;
            for (const auto& action : s.runtimeWorldAudioBackendActionHistory)
                if (action.kind == RuntimeWorldAudioActionKind::Play && action.eventId == spec->eventId)
                    actions.push_back(action);
            return actions;
        };
        std::uint64_t milliseconds = 0;
        const auto observe = [&]() { advanceRuntimeDiskDrainAudio(s,milliseconds); };
        observe();
        if (!begin(s)) return fail("audio drain setup");
        const int id = s.world.getObjects()[0].instanceId;
        observe();
        if (plays().size() != 2 || plays().front().sampleName != spec->samples.front()
            || !plays().back().queueAfterCurrent) return fail("drain attack and prequeued loop sample");
        observe(); observe();
        if (plays().size() != 2) return fail("paused drain sound stacked");
        const auto authoritative = s.combatDetails.deterministicHash();
        for (int i = 0; i < 4; ++i) {
            milliseconds = s.diskDrainAudio.sources.at(id).nextSampleMilliseconds;
            observe();
            if (plays().back().sampleName != spec->samples[1] && plays().back().sampleName != spec->samples[2])
                return fail("drain loop used attack/decay sample");
        }
        if (s.combatDetails.deterministicHash() != authoritative) return fail("audio changed gameplay state");
        s.runtimeSoundVolume = 0; observe();
        if (s.diskDrainAudio.sources.at(id).active) return fail("mute left drain loop active");
        s.runtimeSoundVolume = 100; observe();
        if (!s.diskDrainAudio.sources.at(id).active || plays().back().sampleName == spec->samples.front())
            return fail("unmute replayed attack or lost loop");
        s.diskDrainAudio = {}; observe();
        if (plays().back().sampleName == spec->samples.front()) return fail("load replayed drain attack");
        eraseRuntimeBehavior(s,id); observe();
        if (!s.diskDrainAudio.sources.empty() || plays().back().sampleName != spec->samples.back())
            return fail("stop omitted decay or retained drain sound");
        const auto stopped = plays().size(); milliseconds += 10000; observe();
        if (plays().size() != stopped) return fail("ended drain restarted sound");
        ++cases;
        std::cout << "Disk drain audio: native attack/loop/decay, pause, mute, load and authority passed\n";
    }
    if(!runRuntimeFloatingDiskSmokeProbe(*stock,error))return false;
    std::cout<<"Disk drain passed: "<<cases<<" gameplay cases, original rules, 30/60 Hz, full save and laser regression\n";
    return true;
}
