#include "RuntimeIvanBombIcon.h"

bool runRuntimeIvanDamageAudioSmoke(RuntimeWindowState& stock, std::string& error) {
    const auto fail=[&](const std::string& why){error="Ivan damage audio: "+why;return false;};
    IniFile ini;RulesDatabase rules;
    if(!ini.loadFromFile(stock.report.rulesPath)||!rules.loadFromIni(ini))return fail("stock rules unavailable");
    int cases=0,hits=0,voices=0;
    for(int hz:{15,30,60})for(int mode=0;mode<4;++mode) {
        auto storage=std::make_unique<RuntimeWindowState>();auto& s=*storage;
        s.config=stock.config;s.config.fixedTickHz=hz;s.config.playerSlots.clear();
        s.config.multiplayerActive=false;s.config.computerAiEnabled=false;
        s.rules=rules;s.rulesLoaded=true;s.art=stock.art;s.artLoaded=true;
        s.runtimeSoundsEnabled=false;s.runtimeSoundEffectsEnabled=true;s.runtimeSoundVolume=100;
        s.runtimeSuppressPlayerAudioFeedback=false;s.runtimePreferencePersistenceEnabled=false;
        s.economy.owner="Viewer";s.economy.skirmishStartApplied=true;s.triggers.bootstrapComplete=true;
        const std::string type=mode==1?"CIVAN":mode==2?"E1":"IVAN";
        auto* unit=const_cast<UnitType*>(s.rules.findUnitType("InfantryType",type));
        if(!unit || (mode!=2 && (!unit->ivan||unit->voiceFeedback!="CrazyIvanFear")))
            return fail("native voice declarations changed: "+type);
        if(mode==3)unit->voiceFeedback="CrazyIvanFear,GIFear";
        if(!loadRuntimeSoundDatabase(s))return fail("original sound bank unavailable");
        for(int y=1;y<16;++y)for(int x=1;x<16;++x) {
            TerrainCell c;c.x=x;c.y=y;c.cellIndex512=runtimeCellKey(x,y);c.tileIndex=0;
            s.world.accessTerrainCells().push_back(c);
        }
        auto* added=addRuntimeObjectInstance(s,RuntimeBuildKind::Infantry,type,{8,8});
        if(!added)return fail("native infantry unavailable");
        const int id=added->instanceId;added->mission="Guard";
        initializeRuntimeNavigation(s);
        s.runtimeSoundPlaybackHistory.clear();
        const auto count=[&](const std::string& section) {
            return std::count_if(s.runtimeSoundPlaybackHistory.begin(),s.runtimeSoundPlaybackHistory.end(),
                [&](const auto& r){return r.section==section&&r.playable&&!r.sample.empty()&&r.status=="resolved";});
        };
        const auto hit=[&](int damage) {
            RuntimeProjectileEffect p;p.actorId=100+hits;p.owner="Enemy";p.targetObjectId=id;
            p.toCell=p.currentCell={8,8};p.damage=damage;p.warheadId="SA";
            p.ag=p.affectsEnemies=true;
            ++hits;return applyRuntimeProjectileImpact(s,p);
        };
        const std::string feedback=mode==3?"GIFear":unit->voiceFeedback;
        for(int seconds:{0,1,3}) {
            s.tick=100+seconds*hz;
            const auto* before=findObjectById(s,id);if(!before)return fail("victim disappeared");
            const int health=before->health;
            if(!hit(5))return fail("actual nonlethal impact failed");
            const auto* after=findObjectById(s,id);
            if(!after||after->health>=health)return fail("actual hit did not reduce health: "+type);
            const int expected=(mode==2||mode==3)?(seconds==3?2:1):0;
            if(count(feedback)!=expected || (mode!=2&&count("CrazyIvanFear")!=0))
                return fail(type+" feedback/cooldown at "+std::to_string(hz)+" Hz / second="+std::to_string(seconds));
        }
        s.selectedObjectIds={id};
        for(const auto& [kind,field]:std::vector<std::pair<std::string,std::string>>{
                {"select",unit->voiceSelect},{"move",unit->voiceMove},{"attack",unit->voiceAttack}}) {
            const auto voice=runtimeVoiceEventForSelectedUnits(s,kind);
            if(!voice||*voice!=field)return fail("normal command voice changed: "+type+"/"+kind);
            const auto before=count(field);emitRuntimeSound(s,*voice);
            if(count(field)!=before+1)return fail("normal command voice not playable: "+type+"/"+kind);
            ++voices;
        }
        const std::string death=unit->dieSound;const auto beforeDeath=count(death);
        if(!hit(10000)||findObjectById(s,id)||count(death)!=beforeDeath+1)
            return fail("lethal hit lost native death voice: "+type);
        if(mode!=2&&count("CrazyIvanFear")!=0)return fail("death emitted selection laughter");
        std::cout<<"Ivan audio scene: hz="<<hz<<" mode="<<mode<<" type="<<type
            <<" actual hurt/death, feedback cooldown, command samples PASS\n";
        ++cases;
    }
    std::cout<<"Ivan damage audio: passed cases="<<cases<<" impacts="<<hits<<" commandVoices="<<voices
        <<"; IVAN/CIVAN hurt laughter suppressed, GI/custom hurt and native death retained\n";
    return true;
}

bool runRuntimeIvanBombIconSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout<<std::unitbuf;
    const auto fail=[&](const std::string& message){error="Ivan icon: "+message;return false;};
    auto stock=std::make_unique<RuntimeWindowState>();stock->config=config;
    stock->report=buildRuntimeStartupReport(config,&stock->world,&stock->theater,&stock->art,&stock->rules,&stock->mapFacts);
    if(!stock->report.success) return fail("original resources unavailable");
    if(!runRuntimeIvanDamageAudioSmoke(*stock,error))return false;
    const auto& special=stock->rules.getSpecialMechanismRules();
    if(special.ivanIconFlickerRateFrames!=8 || special.ivanTimedDelayFrames!=450) return fail("native icon rate / bomb duration");
    RuntimeMapRenderContext context;context.rules=&stock->rules;context.art=&stock->art;
    context.assetRootPath=runtimeAssetRootPath(config);
    context.palettePath=runtimePalettePath(config,stock->report.map);
    context.structurePalettePath=runtimeStructurePalettePath(config,stock->report.map);
    if(!runRuntimeIvanBombRenderContractCheck(context,error)) return false;
    std::cout<<"Ivan icon: 13 native frames, stage/flicker boundaries, 15/30/60 Hz, configured rate, paused pixels, moving host, shroud and removed overlay passed\n";
    const auto* ivan=stock->rules.findUnitType("InfantryType","IVAN");
    const auto* tank=stock->rules.findUnitType("UnitType","MTNK");
    const UnitType* engineer=nullptr;
    const WarheadType* disarm=nullptr;
    for(const auto& unit:stock->rules.getInfantryTypes()) if(unit.bombSight>0) {
        for(const auto& weaponId:{unit.primaryWeapon,unit.secondaryWeapon}) {
            const auto* weapon=stock->rules.findWeaponType(weaponId);
            const auto* warhead=weapon?stock->rules.findWarheadType(weapon->warhead):nullptr;
            if(warhead && warhead->bombDisarm){engineer=&unit;disarm=warhead;break;}
        }
        if(engineer)break;
    }
    if(!ivan || !tank || !engineer || !disarm) return fail("native IVAN/tank/BombSight/defuse types");
    for(int hz:{15,30,60}) {
        auto s=std::make_unique<RuntimeWindowState>();s->rules=stock->rules;s->rulesLoaded=true;
        s->config.fixedTickHz=hz;s->config.playerSlots.clear();s->config.computerAiEnabled=false;
        s->economy.owner="Viewer";s->runtimeSoundsEnabled=false;s->tick=100;
        const auto add=[&](int id,const UnitType& type,WorldObjectKind kind,const char* owner,int x){
            WorldObjectInstance object;object.instanceId=id;object.type=type.id;object.kind=kind;object.owner=owner;
            object.x=x;object.y=10;object.cell=runtimeCellKey(x,10);object.alive=true;object.health=type.strength;
            object.mission="Guard";s->world.accessObjects().push_back(object);
        };
        add(1,*ivan,WorldObjectKind::Infantry,"Planter",8);
        add(2,*tank,WorldObjectKind::Unit,"Viewer",10);
        add(3,*engineer,WorldObjectKind::Infantry,"Viewer",10+engineer->bombSight+1);
        ensureRuntimeSpecialMechanismsInitialized(*s);
        std::string reason;
        if(!s->specialMechanisms.tryAttachIvanBomb(s->world.getObjects(),s->rules,1,2,s->tick,hz,{},reason)) return fail("native plant: "+reason);
        const auto hasIcon=[&](){refreshRuntimeSpecialMechanismRenderSnapshots(*s);
            return std::any_of(s->specialMechanismRenderSnapshots.begin(),s->specialMechanismRenderSnapshots.end(),
                [](const auto& snapshot){return snapshot.kind=="timedBomb" && snapshot.targetObjectId==2;});};
        if(hasIcon()) return fail("victim saw concealed enemy bomb without BombSight");
        s->economy.owner="Planter";
        if(!hasIcon()) return fail("planter could not see planted bomb");
        s->economy.owner="Viewer";auto* detector=findMutableObjectById(*s,3);
        detector->x=10+engineer->bombSight;detector->cell=runtimeCellKey(detector->x,10);
        if(!hasIcon()) return fail("BombSight boundary did not reveal icon");
        s->tick+=37*hz/15;
        const auto snapshot=s->specialMechanisms.renderSnapshots(s->tick,hz).front();
        const auto phase=runtimeIvanBombIconFrame(snapshot.progress,snapshot.requiredProgress,s->tick,hz,8,13);
        const auto savedHash=s->specialMechanisms.stateHash();std::ostringstream save;s->specialMechanisms.save(save);
        IniFile ini;RuntimeSpecialMechanismState restored;restored.reset(special);
        if(!ini.loadFromString(save.str()) || !restored.load(ini,error) || restored.stateHash()!=savedHash) return fail("bomb save roundtrip: "+error);
        const auto loaded=restored.renderSnapshots(s->tick,hz).front();
        if(runtimeIvanBombIconFrame(loaded.progress,loaded.requiredProgress,s->tick,hz,8,13)!=phase
            || loaded.expireTick!=snapshot.expireTick || loaded.progress!=snapshot.progress)
            return fail("restore restarted icon phase or bomb timer");
        s->specialMechanisms=std::move(restored);
        if(!s->specialMechanisms.tryDisarmTimedBomb(s->world.getObjects(),s->rules,3,2,disarm->id,s->tick,{},reason)
            || hasIcon()) return fail("native disarm left an icon: "+reason);
        if(!s->specialMechanisms.tryAttachIvanBomb(s->world.getObjects(),s->rules,1,2,s->tick,hz,{},reason))
            return fail("replant after disarm");
        const int deadline=s->specialMechanisms.timedBombs().at(2).detonateTick;
        if(!s->specialMechanisms.tickTimedBombs(deadline-1,{}).empty()) return fail("icon change detonated a bomb early");
        const auto actions=s->specialMechanisms.tickTimedBombs(deadline,{});
        if(actions.size()!=1 || !s->specialMechanisms.timedBombs().empty() || hasIcon()) return fail("expired bomb remained visible");
    }
    std::cout<<"Ivan icon: passed; native planting, owner/BombSight visibility, save phase, disarm and exact detonation boundary verified\n";
    return true;
}
