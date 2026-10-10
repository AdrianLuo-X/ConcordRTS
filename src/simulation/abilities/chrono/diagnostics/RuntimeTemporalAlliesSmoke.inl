bool runRuntimeTemporalAlliesSmoke(const RuntimeConfig& config,std::string& error) {
    std::cout<<std::unitbuf;std::string scene;
    const auto fail=[&](const std::string& why){error="Temporal allies: "+scene+": "+why;return false;};
    auto assets=std::make_unique<RuntimeWindowState>();assets->config=config;
    assets->report=buildRuntimeStartupReport(config,&assets->world,&assets->theater,&assets->art,&assets->rules,&assets->mapFacts);
    if(!assets->report.success)return fail("original resources");
    const char* output=std::getenv("CONCORD_TEMPORAL_ALLIES_OUTPUT");
    int cases=0,beamChecks=0,protectedHits=0,frames=0,saves=0,paired=0,interruptions=0,normalRejections=0;
    for(const auto* type:{"MTNK","E1"})for(int relation:{0,1,2})for(int hz:{15,30,60})for(int count:{1,3}){
        scene=std::string(type)+" relation="+std::to_string(relation)+" hz="+std::to_string(hz)+" attackers="+std::to_string(count);
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=config;s.config.fixedTickHz=hz;s.config.computerAiEnabled=s.config.shortGame=s.config.crates=false;
        s.config.noCooldown=false;s.config.startingUnits=0;s.config.playerCountry="Americans";s.config.playerSlots.clear();
        RuntimeConfig::PlayerSlot slot;slot.enabled=slot.human=slot.local=true;slot.country="Americans";slot.color="Gold";slot.startPosition=1;slot.team="A";
        s.config.playerSlots.push_back(slot);slot.local=slot.human=false;slot.country="Russians";slot.color="Red";slot.startPosition=2;slot.team="B";
        s.config.playerSlots.push_back(slot);slot.country="French";slot.color="Blue";slot.startPosition=3;slot.team="A";s.config.playerSlots.push_back(slot);
        s.rules=assets->rules;s.art=assets->art;s.theater=assets->theater;s.world=assets->world;s.world.accessObjects().clear();
        s.report=assets->report;s.mapFacts=assets->mapFacts;s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;
        s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        ensureRuntimeLightingInitialized(s);ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s);initializeRuntimeEconomy(s);initializeRuntimeSkirmishStart(s);ensureRuntimeRepairDepotsInitialized(s);
        s.triggers.loaded=s.triggers.bootstrapComplete=true;s.visionEnabled=false;initializeRuntimeOfflineGameplayRouting(s);
        std::vector<int> attackers;
        for(int n=0;n<count;++n){auto* p=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,"CLEG",{42,75+n});
            if(!p)return fail("stock CLEG creation");attackers.push_back(p->instanceId);}
        const auto owner=runtimeSkirmishOwnerForSlot(relation==0?1:relation==1?0:2);
        auto* t=addRuntimeObjectInstanceForOwner(s,owner,std::string(type)=="E1"?RuntimeBuildKind::Infantry:RuntimeBuildKind::Unit,type,{46,77},std::nullopt,false);
        if(!t)return fail("target creation");t->mission="Sleep";const int targetId=t->instanceId,health=t->health;
        if(runtimeOwnersAreAllied(s,s.economy.owner,t->owner)!=(relation!=0))return fail("alliance fixture");
        applySelection(s,attackers,false,false);
        if(relation!=0){issueRuntimeAttackCommand(s,*t);
            if(s.lastCommand.acceptedCount!=0)return fail("ordinary click acquired a friendly target");++normalRejections;}
        if(!dispatchRuntimeTargetOrderFromUi(s,relation==0?RuntimeMultiplayerGameplayTargetOrderKind::Primary:RuntimeMultiplayerGameplayTargetOrderKind::Attack,
            {t->x,t->y},t)||s.lastCommand.acceptedCount!=count)return fail("explicit attack command rejected");
        const auto step=[&](){++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
        bool connected=false;
        for(int n=0;n<hz*10;++n){step();connected=std::all_of(attackers.begin(),attackers.end(),[&](int id){return s.specialMechanisms.hasTemporalAttacker(id,targetId);});
            if(connected)break;}
        if(!connected)return fail("real attack never froze the target with all requested beams");
        const auto initial=s.specialMechanisms.temporalTargets().at(targetId);
        int expected=0;for(const auto& [id,progress]:initial.progressByAttacker){(void)id;expected+=progress;}
        const auto frozenCell=runtimeCellPointForObject(*findObjectById(s,targetId));
        if(findObjectById(s,targetId)->health!=health)return fail("beam caused ordinary HP damage");
        const bool pictured=output&&std::string(type)=="MTNK"&&relation==1&&hz==30&&count==3;
        const auto capture=[&](const char* phase){if(!pictured)return true;s.mouseClientPointValid=false;
            const auto path=std::filesystem::path(output)/(std::string("allied-")+phase+".bmp");
            if(!drawRuntimeStateOffscreenAtCell(s,{44,76},800,600,error,path.string(),false,true,-1,nullptr,true))return false;
            ++frames;return true;};
        if(!capture("connected"))return fail("connected frame");
        std::unique_ptr<RuntimeWindowState> restored;
        if(relation!=0&&count==3){
            std::ostringstream text;IniFile ini;
            if(!writeRuntimeStateToStream(s,text,error)||!writeRuntimeMultiplayerSnapshotExtension(s,text,error)||!ini.loadFromString(text.str()))return fail("friendly snapshot write");
            restored=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*restored,s,false);
            if(!materializeRuntimeLoadStateFromIni(*restored,ini,error))return fail("friendly snapshot restore: "+error);
            initializeRuntimeNavigation(*restored);completeRuntimeWorldRefresh(*restored);
            if(!materializeRuntimeMultiplayerSnapshotExtension(*restored,ini,error))return fail("friendly snapshot exact restore: "+error);
            for(int id:attackers)if(!restored->behaviors.at(id).forceFire)return fail("saved friendly attack lost explicit intent");++saves;
        }
        for(const char* warhead:{"AP","HE","RadBeamWarhead"}){
            const auto* victim=findObjectById(s,targetId);const auto center=runtimeProjectileTargetPosition(s,*victim);
            const auto before=s.specialMechanisms.stateHash();RuntimeProjectileEffect hit;
            hit.actorId=nextRuntimeProjectileActorId(s);hit.attackerObjectId=attackers.front();hit.targetObjectId=targetId;hit.owner=s.economy.owner;
            hit.toCell=hit.currentCell=center.cell;hit.toLevel=center.level;hit.damage=20000;hit.warheadId=warhead;hit.ag=true;
            hit.affectsEnemies=hit.affectsAllies=true;applyRuntimeProjectileImpact(s,hit);
            if(restored){nextRuntimeProjectileActorId(*restored);applyRuntimeProjectileImpact(*restored,hit);}
            if(!findObjectById(s,targetId)||findObjectById(s,targetId)->health!=health||s.specialMechanisms.stateHash()!=before)
                return fail("ordinary damage escaped temporal immunity");++protectedHits;
        }
        if(!runtimeSuperWeaponContext(s).damageImmune(*findObjectById(s,targetId)))return fail("support damage immunity missing");
        const int start=s.tick;bool erased=false;
        for(int n=0;n<hz*40;++n){const int before=s.tick;step();
            if(restored&&n<12){++restored->tick;advanceRuntimeSimulation(*restored,false);completeRuntimeWorldRefresh(*restored);++paired;
                const auto a=runtimeStandaloneSimulationStateHash(s,error),b=runtimeStandaloneSimulationStateHash(*restored,error);
                if(!a||a!=b)return fail("restored friendly beam diverged: "+error);}
            if(restored&&n==11){
                applySelection(*restored,{attackers.front()},false,false);issueRuntimeStopCommand(*restored);
                if(!restored->specialMechanisms.isTemporalFrozen(targetId)||restored->specialMechanisms.hasTemporalAttacker(attackers.front(),targetId))return fail("partial stop lost other beams");
                applySelection(*restored,attackers,false,false);issueRuntimeStopCommand(*restored);
                if(restored->specialMechanisms.isTemporalFrozen(targetId)||findObjectById(*restored,targetId)->health!=health)return fail("last stop did not release friendly target");
                interruptions+=2;restored.reset();}
            expected+=8*count*static_cast<int>(runtimeRuleFrameAtTick(s.tick,hz)-runtimeRuleFrameAtTick(before,hz));
            const auto* victim=findObjectById(s,targetId);
            if(expected>=initial.requiredProgress){if(victim||s.specialMechanisms.isTemporalFrozen(targetId))return fail("erasure not completed at combined damage deadline");erased=true;break;}
            if(!victim||victim->health!=health||runtimeCellPointForObject(*victim)!=frozenCell||victim->mission!="Temporal")return fail("frozen target moved or lost HP");
            const auto& status=s.specialMechanisms.temporalTargets().at(targetId);int progress=0;
            for(const auto& [id,value]:status.progressByAttacker){(void)id;progress+=value;}
            if(progress!=expected)return fail("combined continuous progress changed");
            refreshRuntimeSpecialMechanismRenderSnapshots(s);
            const auto beams=std::count_if(s.specialMechanismRenderSnapshots.begin(),s.specialMechanismRenderSnapshots.end(),[&](const auto& ray){
                return ray.kind=="temporalBeam"&&ray.targetObjectId==targetId&&ray.hasBeamEndpoints&&ray.color=="128,200,255";});
            if(beams!=count)return fail("continuous beam disappeared during reload");++beamChecks;
            if(n==hz/2&&!capture("continuous"))return fail("continuous frame");
        }
        if(!erased)return fail("target never erased");if(!capture("erased"))return fail("erasure frame");
        ++cases;std::cout<<"Temporal allies: "<<scene<<" ticks="<<s.tick-start<<" HP=unchanged continuousBeams=ok\n";
    }
    std::cout<<"Temporal allies: passed cases="<<cases<<" beamChecks="<<beamChecks<<" protectedHits="<<protectedHits<<" nativeFrames="<<frames
        <<" saves="<<saves<<" pairedTicks="<<paired<<" interruptions="<<interruptions<<" normalRejections="<<normalRejections<<'\n';return true;
}
