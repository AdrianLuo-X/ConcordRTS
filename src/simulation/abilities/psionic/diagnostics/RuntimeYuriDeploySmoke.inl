bool runRuntimeYuriDeploySmoke(RuntimeWindowState& assets, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Yuri deploy: " + why; return false; };
    const bool diagnostic = std::getenv("CONCORD_YURI_DEPLOY_DIAGNOSTIC") != nullptr;
    const char* output = std::getenv("CONCORD_YURI_DEPLOY_OUTPUT");
    int cases = 0, saves = 0, paired = 0;
    for (const std::string type : {"YURI", "YURIPR"}) for (int hz : {15,30,60})
    for (bool cooling : {false,true}) {
        const std::string label = type + "-" + std::to_string(hz) + (cooling ? "-cooldown" : "-ready");
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, assets, false);
        s.config.fixedTickHz=hz; s.config.noCooldown=false;
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.shortGame=false;
        s.config.playerSlots.clear(); s.config.playerCountry="YuriCountry";
        s.economy.owner="YuriCountry"; s.economy.skirmishStartApplied=true;
        s.triggers.bootstrapComplete=true; s.visionEnabled=false;
        s.runtimePreferencePersistenceEnabled=s.runtimeSoundsEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.world.accessTerrainCells().clear(); s.terrainInfoByCell.clear(); s.terrainByCell.clear();
        s.mapFacts={}; s.mapFactsLoaded=false;
        for (int y=1;y<=26;++y) for(int x=1;x<=26;++x) {
            TerrainCell c; c.x=x; c.y=y; c.cellIndex512=runtimeCellKey(x,y); c.tileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        auto* source=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,type,{12,12});
        if (!source) return fail(label+" source missing"); const int id=source->instanceId;
        source->weaponCooldownTicksRemaining=cooling?hz:0;
        const auto addVictim=[&](const char* victimType,RuntimeCellPoint cell,const char* owner) {
            auto* o=addRuntimeObjectInstanceForOwner(s,owner,RuntimeBuildKind::Infantry,victimType,cell);
            if (!o) return -1;
            if (std::string(victimType)=="E1") o->health=1000;
            o->mission="Sleep";
            o->weaponCooldownTicksRemaining=100000; return o->instanceId;
        };
        const int nearId=addVictim("E1",{13,12},"Americans");
        const int farId=addVictim("E1",{20,12},"Americans");
        const int immuneId=addVictim(type.c_str(),{12,13},"YuriCountry");
        if (nearId<0 || farId<0 || immuneId<0) return fail(label+" victim missing");
        const int immuneHealth=findObjectById(s,immuneId)->health;
        initializeRuntimeNavigation(s); RuntimeSnapshotActiveStateScope scope(s);
        if (!executeRuntimeSecondaryAction(s,id)) return fail(label+" deploy rejected");
        const auto step=[](RuntimeWindowState& state) {
            RuntimeSnapshotActiveStateScope active(state); ++state.tick; advanceRuntimeSimulation(state);
        };
        const auto health=[](const RuntimeWindowState& state,int objectId) {
            const auto* o=findRuntimeObjectByIdIncludingInactive(state,objectId); return o?o->health:-1;
        };
        const auto projectiles=[](const RuntimeWindowState& state) {
            std::string text; for (const auto& p:state.projectiles) text+=serializeRuntimeProjectileEffect(p)+"\n";
            return text;
        };
        auto clone=std::unique_ptr<RuntimeWindowState>{};
        int waveTick=-1, damageTick=-1; std::set<int> pulses;
        // Keep the pulse/range check before Prime's five-second auto-undeploy:
        // later guard acquisition and mind control are separate behaviors.
        // Exact auto-undeploy boundaries are covered by the full-deploy probe.
        for(int n=0;n<hz*(diagnostic?12:4);++n) {
            step(s);
            if (clone) {
                step(*clone); ++paired;
                if (health(s,nearId)!=health(*clone,nearId) || projectiles(s)!=projectiles(*clone)
                    || findObjectById(s,id)->mission!=findObjectById(*clone,id)->mission) {
                    std::cout<<"Continuation tick="<<s.tick<<" health="<<health(s,nearId)<<'/'<<health(*clone,nearId)
                        <<" mission="<<findObjectById(s,id)->mission<<'/'<<findObjectById(*clone,id)->mission
                        <<" deployed="<<findObjectById(s,id)->deployed<<'/'<<findObjectById(*clone,id)->deployed
                        <<" cooldown="<<findObjectById(s,id)->weaponCooldownTicksRemaining<<'/'
                        <<findObjectById(*clone,id)->weaponCooldownTicksRemaining<<'\n'
                        <<"Live projectiles "<<projectiles(s)<<"Loaded projectiles "<<projectiles(*clone)<<'\n';
                    return fail(label+" save continuation diverged");
                }
            }
            for(const auto& p:s.projectiles) if(p.attackerObjectId==id
                && (p.weaponId=="PsiWave" || p.weaponId=="SuperPsiWave")) {
                pulses.insert(p.actorId); if(waveTick<0) waveTick=s.tick;
            }
            if(health(s,nearId)<1000 && damageTick<0) damageTick=s.tick;
            if (s.tick==1) {
                std::ostringstream saved; IniFile ini;
                clone=std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*clone,s,false);
                if (!writeRuntimeStateToStream(s,saved,error) || !ini.loadFromString(saved.str())
                    || !materializeRuntimeLoadStateFromIni(*clone,ini,error)) return fail(label+" save failed: "+error);
                ++saves;
            }
            if(output && !cooling && hz==30 && s.tick==waveTick) {
                std::filesystem::create_directories(output);
                const auto stats=drawRuntimeStateOffscreenAtCell(s,{12,12},640,480,error,
                    std::string(output)+"/"+type+"-pulse.bmp",false);
                if(!stats || !stats->rendered || stats->impactActorDrawn<1)
                    return fail(label+" original RING1 not drawn: "+error);
            }
        }
        std::cout<<"Yuri latency "<<label<<": wave="<<waveTick<<" damage="<<damageTick
                 <<" pulses="<<pulses.size()<<" health="<<health(s,nearId)<<'\n';
        if (!diagnostic && (waveTick<1 || damageTick<1 || (!cooling && (waveTick>1 || damageTick>2))
            || (cooling && (waveTick<hz || waveTick>hz+1 || damageTick>waveTick+1))))
            return fail(label+" extra deploy/travel delay or weapon cooldown bypass");
        if (pulses.size()!=1 || health(s,farId)!=1000 || health(s,immuneId)!=immuneHealth)
            return fail(label+" single pulse/range/type immunity; outside="+std::to_string(health(s,farId))
                +" immune="+std::to_string(health(s,immuneId))+" expected="+std::to_string(immuneHealth));
        ++cases;
    }
    std::cout<<"Yuri deploy: "<<cases<<" real simulation cases, "<<saves<<" saves, "
             <<paired<<" continued ticks; first-tick pulse/next-tick damage, ROF, range, immunity=ok\n";
    return true;
}
