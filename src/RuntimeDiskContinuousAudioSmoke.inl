bool runRuntimeDiskContinuousAudioSmoke(RuntimeWindowState& live,std::string& error) {
    const auto fail=[&](const std::string& why){error="disk continuous audio: "+why;return false;};
    const auto make=[&](int hz){
        auto p=std::make_unique<RuntimeWindowState>();auto& s=*p;
        s.config=live.config;s.config.fixedTickHz=hz;s.config.computerAiEnabled=false;s.config.playerSlots.clear();
        s.config.multiplayerActive=false;s.report=live.report;s.art=live.art;s.theater=live.theater;
        IniFile rules;if(!rules.loadFromFile(live.report.rulesPath)||!s.rules.loadFromIni(rules))return std::unique_ptr<RuntimeWindowState>{};
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;s.economy.owner="YuriCountry";
        s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=true;s.runtimeSoundVolume=100;
        s.runtimeSuppressPlayerAudioFeedback=false;s.runtimePreferencePersistenceEnabled=false;
        s.world=live.world;s.world.accessObjects().clear();s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        for(int y=40;y<=100;++y)for(int x=40;x<=100;++x){
            TerrainCell cell;cell.x=x;cell.y=y;cell.cellIndex512=runtimeCellKey(x,y);cell.tileIndex=cell.subTileIndex=0;
            s.world.accessTerrainCells().push_back(cell);s.terrainInfoByCell.emplace(cell.cellIndex512,RuntimeTerrainInfo{});
        }
        s.terrainByCell=buildRuntimeTerrainLookup(s.world);initializeRuntimeNavigation(s);
        for(int id=0;id<2;++id){auto* disk=addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,"DISK",{60+id*4,60});
            if(!disk)return std::unique_ptr<RuntimeWindowState>{};disk->mission="Guard";}
        const auto position=runtimeSpatialAudioPositionForObject(s.world.getObjects().front());
        const auto projected=runtimeProjectCellXY(position.x,position.y,position.level,s.camera);
        s.camera.initialized=true;s.cameraClampViewport={0,0,1200,800};
        s.camera.worldOffsetX=projected.x-600;s.camera.worldOffsetY=projected.y-400;
        if(!loadRuntimeSoundDatabase(s))return std::unique_ptr<RuntimeWindowState>{};
        return p;
    };
    int moves=0,saves=0,deviceBoundaries=0;
    for(int hz:{15,30,60}){
        auto p=make(hz);if(!p)return fail("native scene");auto& s=*p;RuntimeSnapshotActiveStateScope active(s);
        const int id=s.world.getObjects().front().instanceId,other=s.world.getObjects().back().instanceId;
        advanceRuntimeDiskMoveAudio(s,0);s.selectedObjectIds={id,other};issueRuntimeMoveCommand(s,{90,60});
        for(const auto& record:s.runtimeSoundPlaybackHistory)if(record.section=="FloatingDiscMoveLoop")
            return fail("movement command still played an unowned random loop fragment");
        const auto* spec=s.worldAudioCatalog.find("FloatingDiscMoveLoop");
        if(!spec||spec->samples.size()!=4||!spec->loop)return fail("native movement sound");
        std::uint64_t now=0,audioOffset=0;int starts=0;std::map<int,int> lastOrdinal;
        const auto step=[&](){
            ++s.tick;advanceRuntimeJumpjetFlights(s);processRuntimePathRequests(s);
            for(int source:{id,other}){auto* object=findMutableObjectById(s,source);auto action=s.behaviors.find(source);
                if(object&&action!=s.behaviors.end())advanceRuntimeMoveBehavior(s,*object,action->second);}
            now=std::uint64_t(s.tick)*1000/hz+audioOffset;advanceRuntimeDiskMoveAudio(s,now);
        };
        for(int tick=0;tick<hz*5;++tick){
            if(tick==hz*2)issueRuntimeMoveCommand(s,{92,60});
            step();if(s.diskMoveAudio.sources.size()!=2){
                std::ostringstream detail;detail<<"moving group lost a continuous controller hz="<<hz<<" tick="<<tick;
                for(int source:{id,other}){const auto* object=findObjectById(s,source);const auto behavior=s.behaviors.find(source);
                    detail<<" id="<<source<<" audio="<<s.diskMoveAudio.sources.contains(source);
                    if(object)detail<<" pose="<<object->x<<','<<object->y<<" renderedMoving="<<object->renderMoveActive<<" mission="<<object->mission;
                    if(behavior!=s.behaviors.end())detail<<" kind="<<int(behavior->second.kind)<<" state="<<toString(behavior->second.state)
                        <<" path="<<behavior->second.path.size();
                }
                return fail(detail.str());
            }
            for(int source:{id,other}){
                const auto& binding=s.diskMoveAudio.sources.at(source);
                if(!binding.active||binding.ordinal<lastOrdinal[source])return fail("reissued movement restarted voice");
                lastOrdinal[source]=binding.ordinal;
            }
            for(const auto& action:s.runtimeWorldAudioBackendActionHistory)
                if(action.kind==RuntimeWorldAudioActionKind::Play&&action.sampleName==spec->samples.front())++starts;
            s.runtimeWorldAudioBackendActionHistory.clear();
        }
        if(starts!=2)return fail("intro did not play exactly once per moving disk");
        for(const auto& [source,binding]:s.diskMoveAudio.sources)if(binding.ordinal<5)return fail("loop body did not continue");
        const auto hash=runtimeStandaloneSimulationStateHash(s,error);audioOffset+=1000;now+=1000;advanceRuntimeDiskMoveAudio(s,now);
        if(!hash||runtimeStandaloneSimulationStateHash(s,error)!=hash)return fail("device time changed gameplay authority");
        std::ostringstream saved;IniFile ini;auto restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
        if(!writeRuntimeStateToStream(s,saved,error)||!ini.loadFromString(saved.str())||!materializeRuntimeLoadStateFromIni(*restored,ini,error))
            return fail("moving full reload: "+error);
        advanceRuntimeDiskMoveAudio(*restored,now);
        if(restored->diskMoveAudio.sources.size()!=2)return fail("load did not recreate movement sounds");
        for(const auto& action:restored->runtimeWorldAudioBackendActionHistory)
            if(action.kind==RuntimeWorldAudioActionKind::Play&&action.sampleName==spec->samples.front())return fail("load replayed intro");
        ++saves;
        s.runtimeSoundVolume=0;advanceRuntimeDiskMoveAudio(s,now);
        if(s.diskMoveAudio.sources.at(id).active)return fail("mute retained loop");
        s.runtimeSoundVolume=100;s.runtimeWorldAudioBackendActionHistory.clear();advanceRuntimeDiskMoveAudio(s,now);
        for(const auto& action:s.runtimeWorldAudioBackendActionHistory)
            if(action.kind==RuntimeWorldAudioActionKind::Play&&action.sampleName==spec->samples.front())return fail("unmute replayed intro");
        s.selectedObjectIds={id};issueRuntimeStopCommand(s);advanceRuntimeDiskMoveAudio(s,now);
        // Stop retains the currently interpolating jumpjet segment. Its motor
        // sound ends when that physical segment finishes, not at button-down.
        for(int tick=0;tick<hz*3&&s.diskMoveAudio.sources.contains(id);++tick)step();
        if(s.diskMoveAudio.sources.contains(id)||!s.diskMoveAudio.sources.contains(other))return fail("stop closed wrong group member");
        if(std::none_of(s.runtimeWorldAudioBackendActionHistory.begin(),s.runtimeWorldAudioBackendActionHistory.end(),
            [&](const auto& a){return a.handleId==runtimeDiskMoveSoundHandle(id,1)&&a.sampleName==spec->samples.back();}))return fail("stop omitted decay");
        RuntimeObjectExitRequest exit;exit.objectId=other;exit.kind=RuntimeObjectExitKind::Destroyed;
        exit.tick=s.tick;exit.suppressCrew=exit.suppressDeathWeapon=true;
        if(!finalizeRuntimeObjectExit(s,exit).accepted)return fail("death fixture");
        advanceRuntimeDiskMoveAudio(s,now);if(!s.diskMoveAudio.sources.empty())return fail("dead disk retained loop");
        ++moves;
    }
    // Use the real audio device with only this graph muted. A single source
    // voice must cross the introduction and several body boundaries without
    // exhausting its queue or resetting its sample counter.
    for(const char* event:{"FloatingDiscMoveLoop","FloatingDiscStealLoop"}){
        auto p=make(30);if(!p)return fail("device scene");auto& s=*p;RuntimeSnapshotActiveStateScope active(s);
        s.runtimeSoundsEnabled=true;s.runtimeVoiceAudio=std::make_unique<RuntimeVoiceAudio>(true);
        if(!s.runtimeVoiceAudio->ready())return fail("muted native audio device");
        const auto* object=&s.world.getObjects().front();const auto* spec=s.worldAudioCatalog.find(event);
        if(!spec)return fail("loop specification");
        const auto handle=runtimeDiskMoveSoundHandle(object->instanceId,0);
        RuntimeSuperWeaponBuildingAudioState sequence;
        advanceRuntimeDiskLoopSources(s,sequence,{},0,runtimeDiskMoveSoundHandle);
        const std::vector<RuntimeDiskLoopSource> sources{{object,event,"continuous-device-check"}};
        const auto began=GetTickCount64();advanceRuntimeDiskLoopSources(s,sequence,sources,began,runtimeDiskMoveSoundHandle);
        if(s.runtimeVoiceAudio->queuedBuffers(handle)!=2)return fail("body was not prequeued");
        std::uint64_t lastSamples=0;
        while(GetTickCount64()-began<3600){
            Sleep(20);
            const auto probe=s.runtimeVoiceAudio->probe(handle);
            if(!probe.available||probe.buffersQueued==0||probe.samplesPlayed<lastSamples||probe.masterVolume!=0)
                return fail(std::string(event)+" native voice interrupted or verification output was audible");
            lastSamples=probe.samplesPlayed;
            advanceRuntimeDiskLoopSources(s,sequence,sources,GetTickCount64(),runtimeDiskMoveSoundHandle);
        }
        if(lastSamples<60000||sequence.sources.at(object->instanceId).ordinal<4||s.runtimeVoiceAudio->voiceCount()!=1)
            return fail("native sequence did not cross body boundaries in one voice");
        advanceRuntimeDiskLoopSources(s,sequence,{},GetTickCount64(),runtimeDiskMoveSoundHandle);
        if(s.runtimeVoiceAudio->isPlaying(handle).value_or(true)||!sequence.sources.empty())return fail("native loop stop");
        stopRuntimeWorldAudioBackend(s);if(s.runtimeVoiceAudio->voiceCount()!=0)return fail("audio cleanup");
        ++deviceBoundaries;
    }
    std::cout<<"Disk continuous audio passed: "<<moves<<" native moving-group scenes, "<<saves
        <<" full reloads, duplicate/mute/stop/death boundaries, "<<deviceBoundaries
        <<" muted device sequences crossing multiple buffers without restart, gameplay hash unchanged\n";
    return true;
}
