bool runRuntimeGarrisonCommandAudioSmoke(RuntimeWindowState& live, std::string& error) {
    const auto fail=[&](const std::string& why){error="Garrison command audio: "+why;return false;};
    int cases=0;
    for(const char* type:{"E1","E2","INIT"}) for(bool mixed:{false,true}) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=live.config;s.config.multiplayerActive=false;s.config.computerAiEnabled=false;
        s.rules=live.rules;s.rulesLoaded=true;s.art=live.art;s.artLoaded=true;
        s.economy.owner="Americans";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=true;s.runtimeSoundVolume=100;
        s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=false;
        if(!loadRuntimeSoundDatabase(s))return fail("sound bank missing");
        for(int y=1;y<=28;++y)for(int x=1;x<=28;++x){TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=0;s.world.accessTerrainCells().push_back(c);}
        const auto add=[&](RuntimeBuildKind kind,const char* id,RuntimeCellPoint cell){
            auto* object=addRuntimeObjectInstance(s,kind,id,cell);if(!object)return -1;
            object->mission="Guard";object->buildAnimTicksTotal=object->buildAnimTicksRemaining=0;
            clearRuntimeRenderPosition(*object);return object->instanceId;
        };
        const int hostId=add(RuntimeBuildKind::Building,"CAPRS03",{12,12});
        const int tankId=add(RuntimeBuildKind::Unit,"MTNK",{7,6});
        const int unitId=add(RuntimeBuildKind::Infantry,type,{6,6});
        const int secondId=add(RuntimeBuildKind::Infantry,type,{5,6});
        if(hostId<0||tankId<0||unitId<0||secondId<0)return fail("stock fixture missing");
        auto* host=findMutableObjectById(s,hostId);host->owner="Neutral";
        const int hostHealth=host->health;
        initializeRuntimeNavigation(s);ensureRuntimeGarrisonsInitialized(s);
        const auto* profile=findRuntimeUnitType(s.rules,*findObjectById(s,unitId));
        if(!profile||!trimAsciiLocal(profile->voiceEnter).empty()||profile->voiceMove.empty())return fail("stock voice assumptions changed");
        applySelection(s,mixed?std::vector<int>{tankId,unitId,secondId}:std::vector<int>{unitId},false,false);
        const int before=s.runtimeSoundEvents;
        if(!mixed && runtimeTargetCursorIntent(s,*host)!=RuntimeTargetCursorIntent::Enter)
            return fail("stock infantry lost its entry cursor");
        // Context abilities publish pending intents rather than lastCommand's
        // move/attack result, so inspect the actual committed entry below.
        (void)dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,runtimeCellPointForObject(*host),host);
        if(!s.pendingOccupyTargets.contains(unitId)||s.pendingOccupyTargets.contains(tankId)
            || (mixed&&!s.pendingOccupyTargets.contains(secondId)))return fail("accepted-unit set incorrect");
        if(s.runtimeSoundEvents!=before+1||s.runtimeSoundPlaybackHistory.empty())return fail("group did not emit exactly one response");
        const auto& record=s.runtimeSoundPlaybackHistory.back();
        if(record.section!=profile->voiceMove||!record.playable||record.sample.empty()||record.eva||record.status!="resolved")
            return fail(std::string(type)+" did not resolve its own native move voice: "+record.section+" / "+record.status);
        // A rejected entry must not fabricate the new acknowledgement.
        const int accepted=s.runtimeSoundEvents;host->health=1;
        if(queueRuntimeOccupyTarget(s,*host)||s.runtimeSoundEvents!=accepted)return fail("rejected entry emitted a success voice");
        host->health=hostHealth;
        s.runtimeSuppressPlayerAudioFeedback=true;
        if(!queueRuntimeOccupyTarget(s,*host)||s.runtimeSoundEvents!=accepted)return fail("suppressed order leaked local speech");
        ++cases;
    }
    std::cout<<"Garrison command audio: "<<cases<<" native infantry/group UI routes; one accepted speaker, playable samples, rejected/suppressed orders=ok\n";
    return true;
}
