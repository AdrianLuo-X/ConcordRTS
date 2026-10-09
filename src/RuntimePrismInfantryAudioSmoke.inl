bool runRuntimePrismInfantryAudioSmoke(RuntimeWindowState& live, std::string& error) {
    std::string scene;
    const auto fail=[&](const std::string& why){error="Prism infantry audio "+scene+": "+why;return false;};
    IniFile native; RulesDatabase rules;
    if (!native.loadFromFile(live.report.rulesPath) || !rules.loadFromIni(native)) return fail("native rules");
    const char* output=std::getenv("CONCORD_PRISM_INFANTRY_AUDIO_OUTPUT");
    const std::string root=output?output:"build/smoke/feature-set-14/35-prism-infantry-audio/checks";
    std::filesystem::create_directories(root);
    std::ofstream records(root+"/prism-infantry-audio.csv");
    records << "type,hz,tick,section,sample,playable\n";
    int cases=0,voices=0,zaps=0,controls=0;
    const auto run=[&](const std::string& type,int hz,bool tankTarget,bool cannonSource,bool nonlethal) {
        scene=type+" hz="+std::to_string(hz)+(cannonSource?" cannon":" prism")+(nonlethal?" nonlethal":" lethal");
        auto box=std::make_unique<RuntimeWindowState>(); auto& s=*box;
        s.config=live.config; s.config.fixedTickHz=hz; s.config.noCooldown=false;
        s.config.computerAiEnabled=s.config.multiplayerActive=false; s.config.playerSlots.clear();
        s.config.gameMode="Battle"; s.config.gameModeRulesOverride.clear();
        s.rules=rules; s.art=live.art; s.theater=live.theater; s.report=live.report;
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true; s.visionEnabled=false;
        s.economy.owner="Americans"; s.economy.skirmishStartApplied=true; s.triggers.bootstrapComplete=true;
        s.runtimeSoundsEnabled=false; s.runtimeSuppressPlayerAudioFeedback=true; s.runtimePreferencePersistenceEnabled=false;
        s.world=live.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for(int y=48;y<=80;++y)for(int x=48;x<=80;++x){
            TerrainCell c;c.x=x;c.y=y;c.cellIndex512=y*512+x;c.tileIndex=c.subTileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        WorldObjectInstance source;source.instanceId=1;source.type=cannonSource?"MTNK":"ATESLA";
        source.kind=cannonSource?WorldObjectKind::Unit:WorldObjectKind::Structure;
        source.owner=s.economy.owner;source.alive=true;source.currentAmmo=-1;source.x=source.y=64;
        source.cell=runtimeCellKey(64,64);source.health=runtimeMaxHealthForObject(s,source);source.mission="Guard";
        auto target=source;target.instanceId=2;target.type=type;
        target.kind=tankTarget?WorldObjectKind::Unit:WorldObjectKind::Infantry;
        target.owner="Russians";target.x=68;target.y=65;target.cell=runtimeCellKey(68,65);
        target.health=nonlethal?10000:(tankTarget||cannonSource?1:runtimeMaxHealthForObject(s,target));
        target.mission="Sleep";target.subCell=tankTarget?-1:2;
        auto power=source;power.instanceId=3;power.type="NANRCT";power.kind=WorldObjectKind::Structure;
        power.x=54;power.cell=runtimeCellKey(54,64);power.health=runtimeMaxHealthForObject(s,power);
        s.world.accessObjects()={source,target,power};initializeRuntimeNavigation(s);
        s.structureFootprints=buildRuntimeStructureFootprints(s);rebuildRuntimeEconomyPower(s);
        if(!loadRuntimeSoundDatabase(s))return fail("native sound catalog");
        RuntimeSnapshotActiveStateScope active(s);
        const auto profile=runtimeWeaponProfileForObject(s,*findObjectById(s,1),findObjectById(s,2));
        if(!profile.weapon||!profile.warhead||(!cannonSource&&(profile.weapon->id!="PrismShot"
            ||profile.warhead->infDeath!=5||!profile.warhead->animList.empty())))return fail("native weapon route");
        s.selectedObjectIds={1};issueRuntimeAttackCommand(s,*findObjectById(s,2));
        if(!s.behaviors.contains(1))return fail("real attack order rejected");
        s.runtimeSoundPlaybackHistory.clear();s.runtimeSoundIntentHistory.clear();
        int explosions=0,zap=0,reports=0,deathTick=-1,firstHit=-1;
        bool electro=false;
        const auto collect=[&]() {
            for(const auto& sound:s.runtimeSoundPlaybackHistory){
                records<<type<<','<<hz<<','<<s.tick<<','<<sound.section<<','<<sound.sample<<','<<sound.playable<<'\n';
                const auto name=toLowerAsciiLocal(sound.section);
                if(name.rfind("explosion",0)==0||name=="genvehicledie"||name=="buildinggenericdie")++explosions;
                if(name=="infantryzap"){
                    if(!sound.playable||toLowerAsciiLocal(sound.sample)!="igenzapa")return false;
                    ++zap;
                }
                if(name==(cannonSource?"grizzlytankattack":"prismtowerattack"))++reports;
                ++voices;
            }
            s.runtimeSoundPlaybackHistory.clear();s.runtimeSoundIntentHistory.clear();
            return true;
        };
        for(int tick=0;tick<hz*15;++tick){
            ++s.tick;advanceRuntimeWeaponCooldowns(s);
            if(deathTick<0&&firstHit<0&&s.behaviors.contains(1))
                advanceRuntimeAttackBehavior(s,*findMutableObjectById(s,1),s.behaviors.at(1));
            advanceRuntimeEffects(s);
            if(!collect())return fail("native zap sample resolution");
            const auto* alive=findObjectById(s,2);
            if(!alive&&deathTick<0)deathTick=s.tick;
            if(nonlethal&&alive&&alive->health<target.health&&firstHit<0){firstHit=s.tick;s.behaviors.erase(1);}
            for(const auto& effect:s.impactEffects)if(effect.targetObjectId==2&&effect.animId=="ELECTRO")electro=true;
            const int finished=nonlethal?firstHit:deathTick;
            if(finished>=0&&s.tick-finished>=hz*3)break;
        }
        const auto* unit=findRuntimeUnitTypeById(s.rules,type);
        if(nonlethal){
            if(firstHit<0||!findObjectById(s,2)||zap||explosions||electro)return fail("nonlethal hit gained a death/explosion sound");
        }else{
            if(deathTick<0||findObjectById(s,2))return fail("target never died");
            if(!tankTarget&&!cannonSource){
                if(explosions)return fail("extra explosion sound after prism infantry death");
                const bool human=unit&&!unit->notHuman;
                if(zap!=(human?1:0)||electro!=human)return fail("authored human/nonhuman death route");
            }else if(!explosions)return fail("authored tank/cannon explosion was muted");
        }
        if(reports<1)return fail("attack report disappeared");
        zaps+=zap;++cases;if(tankTarget||cannonSource||nonlethal)++controls;
        std::cout<<"Prism infantry audio: "<<scene<<" reports="<<reports<<" zap="<<zap<<" explosions="<<explosions<<" PASS\n";
        return true;
    };
    for(int hz:{15,30,60}){
        for(const auto& unit:rules.getInfantryTypes()){
            if(unit.techLevel<0||unit.owner.empty()||unit.jumpJet||unit.balloonHover||!unit.deathWeapon.empty())continue;
            if(!run(unit.id,hz,false,false,false))return false;
        }
        if(!run("E1",hz,false,false,true)||!run("HTNK",hz,true,false,false)||!run("E1",hz,false,true,false))return false;
    }
    std::cout<<"Prism infantry audio passed: "<<cases<<" real attacks, "<<controls<<" nonlethal/tank/cannon controls, "
        <<zaps<<" native electrocution samples, "<<voices<<" sound records; no extra infantry explosion\n";
    return true;
}
