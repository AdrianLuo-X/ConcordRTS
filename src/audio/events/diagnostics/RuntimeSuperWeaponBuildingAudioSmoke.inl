bool runRuntimeSuperWeaponBuildingAudioSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& message){error="Superweapon building audio: "+message;return false;};
    struct Expected {const char* type;const char* opening;const char* loop;};
    const Expected expected[]{
        {"GACSPH","ChronosphereOpen","ChronosphereReadyLoop"},
        {"GAWEAT","WeatherControlReady","WeatherControlReadyLoop"},
        {"NAIRON","IronCurtainReady","IronCurtainReadyLoop"},
        {"NAMISL","NuclearSiloReady",""},
        {"YAGNTC","GeneticMutatorOpen","GeneticMutatorReadyLoop"},
        {"YAPPET","PsychicDominatorOpen","PsychicDominatorReadyLoop"}};
    auto base=std::make_unique<RuntimeWindowState>(); base->config=config;
    base->report=buildRuntimeStartupReport(config,&base->world,&base->theater,&base->art,&base->rules,&base->mapFacts);
    if(!base->report.success)return fail("original resources missing");
    int cases=0;
    for(int hz:{30,60}) for(const auto& wanted:expected) {
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=config;s.config.fixedTickHz=hz;s.config.computerAiEnabled=false;s.config.superweapons=true;
        s.config.noCooldown=false;s.config.multiplayerActive=false;
        s.report=base->report;s.rules=base->rules;s.art=base->art;s.theater=base->theater;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.economy.owner="Commander";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=true;s.runtimeSoundVolume=100;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimePreferencePersistenceEnabled=false;
        if(!loadRuntimeSoundDatabase(s))return fail("original sound catalog missing");
        const auto* building=s.rules.findBuildingType(wanted.type);
        if(!building)return fail(wanted.type);
        TerrainCell center;center.x=center.y=18;center.cellIndex512=runtimeCellKey(18,18);
        s.world.accessTerrainCells().push_back(center);
        for(int y=1;y<=32;++y)for(int x=1;x<=32;++x)if(x!=18 || y!=18) {
            auto cell=center;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);
            s.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance host;host.instanceId=1;host.type=wanted.type;host.kind=WorldObjectKind::Structure;
        host.owner="Commander";host.alive=true;host.health=building->strength;host.poweredOperational=true;
        host.x=host.y=18;host.cell=runtimeCellKey(18,18);host.mission="Guard";
        auto power=host;power.instanceId=2;power.type="NANRCT";power.health=1000;
        power.x=power.y=3;power.cell=runtimeCellKey(3,3);
        s.world.accessObjects()={host,power};initializeRuntimeNavigation(s);ensureRuntimeSuperWeaponsInitialized(s);
        std::uint64_t milliseconds=0;
        const auto observe=[&](){advanceRuntimeSuperWeaponBuildingAudio(s,milliseconds);};
        const auto step=[&](){++s.tick;milliseconds=std::uint64_t(s.tick)*1000/hz;
            auto context=runtimeSuperWeaponContext(s);s.superWeapons.tick(context);observe();};
        const auto count=[&](const std::string& event){return std::count_if(s.runtimeWorldAudioBackendActionHistory.begin(),
            s.runtimeWorldAudioBackendActionHistory.end(),[&](const auto& a){return a.kind==RuntimeWorldAudioActionKind::Play && a.eventId==event;});};
        step();
        for(int i=0;i<hz*1200;++i) {
            const auto* weapon=s.superWeapons.findSidebarSnapshot("Commander",building->superWeapon);
            if(weapon && weapon->remainingTicks<=hz*60+1)break;
            step();
        }
        if(count(wanted.opening)!=0)return fail(std::string(wanted.type)+" premature opening sound");
        step();
        if(count(wanted.opening)!=1)return fail(std::string(wanted.type)+" missing/duplicate opening sound");
        const auto* spec=s.worldAudioCatalog.find(wanted.opening);
        if(!spec || spec->samples.empty())return fail("opening catalog entry missing");
        for(const auto& sample:spec->samples) {
            const auto path=findRuntimeSoundSamplePath(s,sample);
            if(!path || runtimeWavDurationTicks(*path,1000)<=0)return fail("opening audio cannot decode: "+sample);
        }
        for(int i=0;i<8;++i)observe(); // Paused/render-only frames must not retrigger the opening.
        s.world.accessObjects()[0].health=building->strength/3;observe();
        if(count(wanted.opening)!=1)return fail("pause/damage repeated opening sound");
        s.world.accessObjects()[0].health=building->strength;
        // Reconstruct a saved opening phase: no one-shot should be replayed.
        std::ostringstream saved;s.superWeapons.save(saved);IniFile ini;ini.loadFromString(saved.str());
        if(!s.superWeapons.load(ini,&error))return false;
        s.superWeaponBuildingAudio={};s.runtimeWorldAudioBackendActionHistory.clear();observe();
        if(count(wanted.opening)!=0)return fail("load replayed opening sound");
        for(int i=0;i<hz*8;++i)step();
        auto& source=s.superWeaponBuildingAudio.sources.at(1);
        if(source.stage!="SuperAnimThree")return fail("audio did not follow end of opening animation");
        const auto* weapon=s.superWeapons.findSidebarSnapshot("Commander",building->superWeapon);
        if(!weapon || weapon->remainingTicks<=0)return fail("ready-loop test started after actual readiness");
        if(*wanted.loop) {
            if(source.event!=wanted.loop || !source.active || count(wanted.loop)==0)return fail("ready hum missing before weapon readiness");
            const auto* loop=s.worldAudioCatalog.find(wanted.loop);
            const auto controls=toLowerAsciiLocal(s.soundIni.getValue(wanted.loop,"Control",""));
            if(controls.find("attack")!=std::string::npos) {
                const auto first=std::find_if(s.runtimeWorldAudioBackendActionHistory.begin(),
                    s.runtimeWorldAudioBackendActionHistory.end(),[&](const auto& action) {
                        return action.kind==RuntimeWorldAudioActionKind::Play && action.eventId==wanted.loop;
                    });
                if(first==s.runtimeWorldAudioBackendActionHistory.end() || first->sampleName!=loop->samples.front())
                    return fail("ready hum did not start with original attack sample");
            }
            for(const auto& sample:loop->samples) {
                const auto path=findRuntimeSoundSamplePath(s,sample);
                if(!path || runtimeWavDurationTicks(*path,1000)<=0)return fail("loop audio cannot decode: "+sample);
            }
            const auto before=count(wanted.loop);
            observe();observe();if(count(wanted.loop)!=before)return fail("render-only frame stacked loop sound");
            s.world.accessObjects()[0].poweredOperational=false;observe();
            if(s.superWeaponBuildingAudio.sources.at(1).active)return fail("power loss left hum active");
            s.world.accessObjects()[0].poweredOperational=true;observe();
            if(!s.superWeaponBuildingAudio.sources.at(1).active || count(wanted.opening)!=0)return fail("power restore replayed opening or lost hum");
            s.runtimeSoundVolume=0;observe();
            if(s.superWeaponBuildingAudio.sources.at(1).active)return fail("mute left hum active");
            s.runtimeSoundVolume=100;observe();
            // Rehydrate the loop without playing its attack or opening again.
            s.superWeaponBuildingAudio={};s.runtimeWorldAudioBackendActionHistory.clear();observe();
            if(count(wanted.loop)!=1 || count(wanted.opening)!=0)return fail("ready-loop rehydration");
            if(controls.find("attack")!=std::string::npos
                && s.runtimeWorldAudioBackendActionHistory.back().sampleName==loop->samples.front())return fail("load repeated loop attack");
        } else if(source.active || !source.event.empty())return fail("silo invented a ready loop");
        const auto authoritative=s.superWeapons.stateHash();const auto worldAudioHash=s.worldAudio.stateHash();
        milliseconds+=5000;observe();
        if(s.superWeapons.stateHash()!=authoritative || s.worldAudio.stateHash()!=worldAudioHash)return fail("local presentation changed saved authority");
        s.superWeapons.forceReady("Commander",building->superWeapon);
        auto context=runtimeSuperWeaponContext(s);s.superWeapons.tick(context);
        if(!s.superWeapons.beginTargetSelection("Commander",building->superWeapon,context,&error)
            || !s.superWeapons.submitTarget("Commander",building->superWeapon,{{24,24},-1},context,&error))return fail(error);
        if(std::string(wanted.type)=="GACSPH"
            && !s.superWeapons.submitTarget("Commander",building->superWeapon,{{28,28},-1},context,&error))return fail(error);
        observe();
        if(s.superWeaponBuildingAudio.sources.at(1).active)return fail("launch left ready hum active");
        if(*wanted.loop) {
            if(!std::any_of(s.runtimeWorldAudioBackendActionHistory.begin(),s.runtimeWorldAudioBackendActionHistory.end(),
                    [&](const auto& action){return action.kind==RuntimeWorldAudioActionKind::Stop
                        && action.handleId==runtimeSuperWeaponBuildingSoundHandle(1,1);}))return fail("launch did not stop backend hum");
            const auto controls=toLowerAsciiLocal(s.soundIni.getValue(wanted.loop,"Control",""));
            if(controls.find("decay")!=std::string::npos) {
                const auto* loop=s.worldAudioCatalog.find(wanted.loop);
                const auto& last=s.runtimeWorldAudioBackendActionHistory.back();
                if(last.kind!=RuntimeWorldAudioActionKind::Play || last.eventId!=wanted.loop
                    || last.sampleName!=loop->samples.back())return fail("launch omitted original decay sample");
            }
        }
        s.world.accessObjects()[0].alive=false;observe();
        if(s.superWeaponBuildingAudio.sources.contains(1))return fail("destroyed source remained in audio state");
        ++cases;std::cout<<"Superweapon building audio "<<wanted.type<<" "<<hz<<" Hz: native opening/loop, phase sync, pause/damage/power/mute/load/launch/removal PASS\n";
    }
    std::cout<<"Superweapon building audio passed: "<<cases<<" original building/rate cases; presentation-only state\n";
    return true;
}
