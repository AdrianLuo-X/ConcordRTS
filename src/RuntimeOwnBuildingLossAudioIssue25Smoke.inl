// Issue25/7: original announcements, actual projectile damage and sustained Temporal producers.
#ifdef _MSC_VER
#pragma optimize("", off)
#endif
bool runRuntimeOwnBuildingLossAudioIssue25SmokeProbe(RuntimeWindowState& live,std::string& error) {
    std::cout<<std::unitbuf;std::string scene="original-audio";
    const auto fail=[&](const std::string& why){error="Issue25 own building loss audio: "+scene+": "+why;return false;};
    const char* output=std::getenv("CONCORD_OWN_BUILDING_LOSS_AUDIO_ISSUE25_OUTPUT");
    const std::string root=output&&*output?output:pathString(std::filesystem::path(live.config.localDataRootPath)/"issue25-own-building-loss-audio");
    std::filesystem::create_directories(root);
    const auto quote=[](const std::string& v){std::string out="\"";for(char c:v){if(c=='"')out+='"';out+=c;}return out+'"';};
    const auto hash=[](std::span<const std::uint8_t> bytes){const auto h=runtimeMultiplayerSnapshotSha256(bytes);std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto b:h)out<<std::setw(2)<<unsigned(b);return out.str();};
    const auto read=[](const std::string& path){std::ifstream in(path,std::ios::binary);return std::vector<std::uint8_t>{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};};
    const auto assets=std::filesystem::path(runtimeAssetRootPath(live.config));
    const auto evaPath=pathString(assets/"ra2md/localmd/evamd.ini"),rulesPath=pathString(assets/"ra2md/localmd/rulesmd.ini");
    IniFile originalEva,originalRules;
    if(hash(read(evaPath))!="e4df441ac619ba3b68e84ceee55e3eaf7c64c0410176cf2ea442085da9feeadc"
        ||hash(read(rulesPath))!="06761dd7f714e7d9400216ec3c06109ec5c1461f6a0727be7401eb9d8b0f6d05"
        ||!originalEva.loadFromFile(evaPath)||!originalRules.loadFromFile(rulesPath))return fail("original EVA/rules bytes");
    if(originalEva.getValue("DialogList","64")!="EVA_UnitLost"||originalEva.getValue("EVA_UnitLost","Text")!="Unit lost."
        ||originalEva.getValue("EVA_UnitLost","Priority")!="IMPORTANT"||!originalEva.getValue("EVA_UnitLost","Type").empty())
        return fail("original registered UnitLost64/Text/IMPORTANT/no-explicit-Type");
    struct Voice {const char* owner;const char* key;const char* sample;const char* rawHash;const char* pcmHash;int frames;};
    const std::array<Voice,3> voices{{
        {"Americans","Allied","ceva064","3b6926bfb8454e0151b84967dfc3eccbacc0ee47115280e280f76928cfe12e5f","4d5d0c1b8c4ebb6d3d2a949dfef01de3b1dbe1cafc160f3aa215d01b9a485eb0",18559},
        {"Russians","Russian","csof064","1984d70947b3e284420c2d85b36596525d23626ee9a485e194d363d1080447ea","c172b6317b2bb894b1b028c2fd1e3616dbc1aea2aebaefa6d91c51d5034e821e",25243},
        {"YuriCountry","Yuri","cyur064","aa378e504d455caf1cb3e24a37ed048ee66752f712a99ffbd7b162a87ef4ce49","0afb565b4079544223ef38694363ac1564b0f5a90c303f47bc3541a9441dae6e",22474}}};
    auto stock=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*stock,live,false);
    stock->runtimeSoundsEnabled=stock->runtimePreferencePersistenceEnabled=false;
    if(!loadRuntimeSoundDatabase(*stock))return fail("real original sound bank");
    std::ofstream audio(root+"/audio.csv"),cases(root+"/cases.csv"),events(root+"/events.csv"),temporal(root+"/temporal.csv"),sources(root+"/sources.txt");
    if(!audio||!cases||!events||!temporal||!sources)return fail("evidence output");
    audio<<"side,key,original_sample,raw_sha,pcm_sha,frames,rate,decoded_bytes,path\n";
    cases<<"scene,side,producer,victim,type,owner,input_hp,actual_hp,actual_live,expected_unitlost,actual_intents,actual_records,clock_before,clock_after,finalized,passed\n";
    events<<"scene,tick,intent,section,sample,eva,playable,queued,path,status\n";
    temporal<<"scene,tick,source,target,expected_progress,required_progress,actual_progress,alive,health,unitlost_intents,unitlost_records\n";
    int waves=0,fixtures=0,ordinaryStructures=0,ordinaryUnits=0,nonlocal=0,mixed=0,sequence=0,throttle=0,continuous=0,checkpointTail=0;
    for(const auto& voice:voices) {
        if(originalEva.getValue("EVA_UnitLost",voice.key)!=voice.sample)return fail("original faction064 mapping");
        const auto resolved=resolveAudioSampleToWave(runtimeAssetRootPath(stock->config),pathString(std::filesystem::path(stock->config.localDataRootPath)/"runtime_audio_cache"),voice.sample);
        if(!resolved.success||resolved.sourcePath.empty()||resolved.wavPath.empty())return fail("original064 sample resolution: "+resolved.errorMessage);
        const auto bytes=read(resolved.wavPath);MixArchive archive;std::vector<std::uint8_t> raw;
        if(!archive.loadFromFile(resolved.sourcePath))return fail("original064 MIX source");
        const auto* entry=archive.findEntryByName(std::string(voice.sample)+".wav");
        if(!entry||!archive.readEntryData(*entry,raw,error)||raw!=bytes||hash(raw)!=voice.rawHash)return fail("original064 raw slice SHA/cached equality");
        const auto u16=[&](std::size_t p){return unsigned(bytes[p])|(unsigned(bytes[p+1])<<8);};
        const auto u32=[&](std::size_t p){return std::uint32_t(bytes[p])|(std::uint32_t(bytes[p+1])<<8)|(std::uint32_t(bytes[p+2])<<16)|(std::uint32_t(bytes[p+3])<<24);};
        if(bytes.size()<12||!isWaveFileBytes(bytes)||std::uint64_t(u32(4))+8!=bytes.size())return fail("original064 RIFF bounds");
        unsigned format=0,channels=0,rate=0,block=0,bits=0,frames=0;std::vector<std::uint8_t> compressed;
        for(std::size_t p=12;p+8<=bytes.size();) {
            const auto n=u32(p+4);const auto end=std::uint64_t(p)+8+n;if(end>bytes.size())return fail("original064 chunk bounds");
            const std::string id(reinterpret_cast<const char*>(bytes.data()+p),4);
            if(id=="fmt "){if(n<20)return fail("original064 short IMA fmt");format=u16(p+8);channels=u16(p+10);rate=u32(p+12);block=u16(p+20);bits=u16(p+22);}
            else if(id=="fact"){if(n<4)return fail("original064 short FACT");frames=u32(p+8);}
            else if(id=="data")compressed.assign(bytes.begin()+p+8,bytes.begin()+static_cast<std::size_t>(end));
            p=static_cast<std::size_t>(end+(n&1));
        }
        if(format!=0x11||channels!=1||rate!=22050||block!=512||bits!=4||frames!=unsigned(voice.frames)||compressed.empty())return fail("original064 IMA/FACT parameters");
        const auto decoded=decodeImaAdpcmBagSampleToWave(compressed,rate,channels,block);
        std::string expectedPcm=voice.pcmHash;std::erase(expectedPcm,' ');
        if(!decoded.success||decoded.pcmBytes.size()<std::size_t(frames)*2
            ||hash(std::span<const std::uint8_t>(decoded.pcmBytes.data(),std::size_t(frames)*2))!=expectedPcm)
            return fail("original064 independently pinned decoded PCM: "+decoded.errorMessage);
        audio<<voice.owner<<','<<voice.key<<','<<voice.sample<<','<<hash(raw)<<','<<expectedPcm<<','<<frames<<','<<rate<<','<<decoded.pcmBytes.size()<<','<<quote(resolved.wavPath)<<'\n';++waves;
    }
    for(const char* id:{"120mm","Medusa","Demobomb","NeutronRifle"}) {
        const auto* weapon=live.rules.findWeaponType(id);
        if(!weapon||weapon->damage!=std::stoi(originalRules.getValue(id,"Damage"))||weapon->warhead!=originalRules.getValue(id,"Warhead")
            ||weapon->projectile!=originalRules.getValue(id,"Projectile"))return fail(std::string(id)+" original weapon fields");
    }
    if(originalRules.getValue("DemobombWH","CellSpread")!="8"||originalRules.getValue("ChronoBeam","Temporal")!="yes"
        ||originalRules.getValue("NeutronRifle","Damage")!="8")return fail("original explosion/Temporal fields");
    sources<<"Original evamd SHA e4df441ac619ba3b68e84ceee55e3eaf7c64c0410176cf2ea442085da9feeadc\n"
        <<"Original UnitLost DialogList64, Allied/Russian/Yuri064, Text Unit lost., IMPORTANT, Type omitted.\n"
        <<"Independent original ADPCM/FACT PCM hashes from raw MIX decoding; backend decoding must reproduce exact PCM.\n"
        <<"ordinary-projectile = actual damage producer at original impact with original weapon/projectile/warhead, not claimed UI-fired shot.\n"
        <<"input_hp1 = lawful pre-wounded object; stock Strength/weapon damage remain unchanged.\n"
        <<"continuous-Temporal = actual enemy CLEG Attack plus full simulation, no seeded temporal relationship/progress.\n"
        <<"checkpoint-TemporalRemoved = legal mechanism-completed input before projectile tail, explicitly not natural full-simulation timing.\n";
    auto tileCache=std::make_unique<RuntimeMapRenderCache>();const auto metadata=runtimeTerrainMetadataContext(live);
    std::optional<TerrainCell> land,water;
    for(const auto& set:live.theater.getTileSets())for(int t=set.startTileIndex;t<set.startTileIndex+set.tilesInSet&&(!land||!water);++t)
        for(int sub=0;sub<16;++sub){TerrainCell cell;cell.tileIndex=t;cell.subTileIndex=sub;const auto info=runtimeTerrainInfoForCell(live,cell);
            if(!info.passable||info.bridge||info.shore||info.cliff||resolveRuntimeTerrainSubTileRampType(cell,metadata,*tileCache)!=0)continue;
            if(!land&&!info.water)land=cell;if(!water&&info.water&&resolveRuntimeTerrainSubTileLandType(cell,metadata,*tileCache)==RuntimeTerrainLandType::Water)water=cell;}
    if(!land||!water)return fail("original flat Clear/Water terrain");
    const auto make=[&](int side){
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;seedRuntimeLoadScratch(s,*stock,false);
        s.config.fixedTickHz=30;s.config.playerCountry=voices[side].owner;s.config.playerSlots.clear();s.config.gameModeRulesOverride.clear();
        s.config.computerAiEnabled=s.config.multiplayerActive=s.config.crates=s.config.superweapons=s.config.noCooldown=s.config.shortGame=false;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.mapFacts={};s.mapFactsLoaded=false;s.report.map.size.clear();s.report.map.localSize.clear();s.report.map.width=-1;
        s.terrainByCell.clear();s.terrainInfoByCell.clear();s.structureFootprints.clear();
        s.skirmishCountryByOwner.clear();s.skirmishTeamByOwner.clear();s.skirmishDifficultyByOwner.clear();
        s.humanHouseAuthorities.clear();s.computerAiByOwner.clear();s.computerMiningByOwner.clear();s.triggers={};s.triggers.bootstrapComplete=true;
        s.campaignMode=s.skirmishMatchFinished=false;s.economy={};s.economy.owner=voices[side].owner;s.economy.skirmishStartApplied=true;s.economy.credits=10000;
        s.initialOreFieldsSeeded=true;s.tick=100;s.visionEnabled=s.runtimeSoundsEnabled=s.runtimePreferencePersistenceEnabled=false;
        s.runtimeSuppressPlayerAudioFeedback=true;s.runtimeSoundEffectsEnabled=true;s.runtimeVoiceVolume=s.runtimeSoundVolume=100;
        s.evaIni=stock->evaIni;s.soundIni=stock->soundIni;s.evaLoaded=stock->evaLoaded;s.soundLoaded=stock->soundLoaded;
        s.runtimeSoundSamplePathCache=stock->runtimeSoundSamplePathCache;
        for(int y=1;y<=40;++y)for(int x=1;x<=40;++x){auto cell=x>=26?*water:*land;cell.x=x;cell.y=y;cell.cellIndex512=y*512+x;cell.level=0;s.world.accessTerrainCells().push_back(cell);}
        initializeRuntimeNavigation(s);ensureRuntimeSpecialMechanismsInitialized(s);ensureRuntimeGarrisonsInitialized(s);ensureRuntimeSubcellsInitialized(s);
        s.cameraClampViewport={0,0,800,600};s.camera.initialized=true;const auto center=runtimeProjectCellXY(12,12,0,s.camera);
        s.camera.worldOffsetX=center.x-400;s.camera.worldOffsetY=center.y-300;return box;
    };    const auto add=[&](RuntimeWindowState& s,const std::string& type,int id,int x,int y,const std::string& owner,int inputHp=1){
        WorldObjectInstance o;o.instanceId=id;o.mapId="Issue25Loss"+std::to_string(id);o.type=type;o.owner=owner;
        o.kind=s.rules.findBuildingType(type)?WorldObjectKind::Structure:s.rules.findUnitType("InfantryType",type)?WorldObjectKind::Infantry:WorldObjectKind::Unit;
        o.authoredAircraft=s.rules.findUnitType("AircraftType",type)!=nullptr;
        o.x=x;o.y=y;o.cell=runtimeCellKey(x,y);o.alive=o.poweredOperational=true;o.currentAmmo=-1;o.mission="Sleep";o.facing=o.turretFacing=64;
        o.health=inputHp<0?std::max(1,runtimeMaxHealthForObject(s,o)):inputHp;
        if(o.authoredAircraft){o.airborne=true;o.level=o.flightLevel=4;}
        s.world.accessObjects().push_back(o);invalidateRuntimeObjectProjectionCache(s);invalidateRuntimeTickOccupancyCache(s);return id;
    };
    const auto intents=[](const RuntimeWindowState& s){return int(std::count(s.runtimeSoundIntentHistory.begin(),s.runtimeSoundIntentHistory.end(),runtimeSoundIntentUnitLost));};
    const auto records=[](const RuntimeWindowState& s){return int(std::count_if(s.runtimeSoundPlaybackHistory.begin(),s.runtimeSoundPlaybackHistory.end(),[](const auto& r){return r.section=="EVA_UnitLost";}));};
    const auto dump=[&](const RuntimeWindowState& s){
        for(const auto& r:s.runtimeSoundPlaybackHistory)events<<quote(scene)<<','<<r.tick<<','<<quote(r.intent)<<','<<quote(r.section)<<','<<quote(r.sample)<<','<<r.eva<<','<<r.playable<<','<<r.queued<<','<<quote(r.path)<<','<<quote(r.status)<<'\n';
        events.flush();
    };
    const auto soundCheck=[&](const RuntimeWindowState& s,int side,int expected,int expectedClock){
        if(intents(s)!=expected||records(s)!=expected||s.runtimeLastUnitLostSoundTick!=expectedClock)
            return fail("UnitLost intent/record/clock expected="+std::to_string(expected)+" actual="+std::to_string(intents(s))+"/"+std::to_string(records(s))+" clock="+std::to_string(s.runtimeLastUnitLostSoundTick)+" expectedClock="+std::to_string(expectedClock));
        for(const auto& r:s.runtimeSoundPlaybackHistory)if(r.section=="EVA_UnitLost"){
            if(r.intent!=runtimeSoundIntentUnitLost||!r.semantic||!r.eva||!r.playable||r.path.empty()||r.sample!=voices[side].sample||r.priority!=2||r.subtitleText!=originalEva.getValue("EVA_UnitLost","Text")||r.queueRequested||r.queued)
                return fail("original064 event mapping/playable: "+r.sample+" "+r.status);
        }
        for(const auto& r:s.runtimeEvaQueue)if(r.section=="EVA_UnitLost")return fail("unexpected UnitLost queue");
        return true;
    };
    std::string impactDiagnostic;
    const auto impact=[&](RuntimeWindowState& s,int targetId,const std::string& selectedWeapon=""){
        const auto* target=findObjectById(s,targetId);if(!target)return fail("impact target missing");
        const bool air=runtimeObjectIsAirTarget(s,*target);
        const auto* weapon=s.rules.findWeaponType(selectedWeapon.empty()?(air?"Medusa":"120mm"):selectedWeapon);
        const auto* projectile=weapon?s.rules.findProjectileType(weapon->projectile):nullptr;
        const auto* wh=weapon?s.rules.findWarheadType(weapon->warhead):nullptr;
        if(!weapon||!projectile||!wh)return fail("actual original weapon/projectile/warhead missing");
        const auto p=runtimeProjectileTargetPosition(s,*target);RuntimeProjectileEffect hit;
        hit.actorId=nextRuntimeProjectileActorId(s);hit.targetObjectId=targetId;hit.owner=s.economy.owner=="Russians"?"Americans":"Russians";
        hit.toCell=hit.currentCell=p.cell;hit.toLevel=hit.currentLevel=p.level;hit.currentX=p.x;hit.currentY=p.y;hit.currentZ=p.level;
        hit.hasTargetPosition=true;hit.targetX=p.x;hit.targetY=p.y;hit.targetZ=p.level;
        hit.weaponId=weapon->id;hit.projectileId=projectile->id;hit.warheadId=wh->id;hit.damage=weapon->damage;
        hit.ag=projectile->ag;hit.aa=projectile->aa;hit.cellSpread=wh->cellSpread;hit.percentAtMax=wh->percentAtMax;
        hit.affectsAllies=wh->affectsAllies;hit.affectsEnemies=wh->affectsEnemies;
        const bool canHit=runtimeProjectileCanHitTarget(s,hit,*target);
        std::ostringstream why;why<<"weapon="<<hit.weaponId<<" projectile="<<hit.projectileId<<" warhead="<<hit.warheadId<<" damage="<<hit.damage<<" AA="<<hit.aa<<" AG="<<hit.ag<<" air="<<air<<" hit="<<canHit;
        impactDiagnostic=why.str();sources<<scene<<": "<<impactDiagnostic<<'\n';sources.flush();
        if(!canHit||!applyRuntimeProjectileImpact(s,hit))return fail("original projectile producer rejected: "+impactDiagnostic);
        return true;
    };
    const auto dead=[&](RuntimeWindowState& s,int id){
        const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);if(!o||o->alive||o->health!=0)return false;
        RuntimeObjectExitRequest again;again.objectId=id;again.tick=s.tick;
        return s.pendingObjectExits.empty()&&s.pendingObjectExitIds.empty()&&queueRuntimeObjectExit(s,again)==RuntimeObjectExitQueueResult::AlreadyFinalized;
    };
    const auto row=[&](RuntimeWindowState& s,int side,int id,const std::string& producer,int expected,int before,int expectedClock,bool ordinaryBuilding=false){
        const auto* o=findRuntimeObjectByIdIncludingInactive(s,id);const bool finalized=dead(s,id);
        const bool soundOkay=soundCheck(s,side,expected,expectedClock);
        bool presentation=true;
        if(ordinaryBuilding&&o){
            const auto* b=s.rules.findBuildingType(o->type);const auto die=b?trimAsciiLocal(b->dieSound):std::string();
            const auto intent=die.empty()?std::string("BuildingDestroyed"):"sound:"+die;
            presentation=std::find(s.runtimeSoundIntentHistory.begin(),s.runtimeSoundIntentHistory.end(),intent)!=s.runtimeSoundIntentHistory.end()
                &&std::any_of(s.impactEffects.begin(),s.impactEffects.end(),[&](const auto& e){return e.targetObjectId==id&&e.renderIntent=="buildingDestruction";});
            if(o->owner==s.economy.owner)presentation=presentation&&s.runtimeLastBaseUnderAttackSoundTick==s.tick
                &&std::find(s.runtimeSoundIntentHistory.begin(),s.runtimeSoundIntentHistory.end(),runtimeSoundIntentBaseUnderAttack)!=s.runtimeSoundIntentHistory.end();
        }
        const bool okay=finalized&&soundOkay&&presentation;
        cases<<quote(scene)<<','<<voices[side].key<<','<<producer<<','<<id<<','<<(o?o->type:"")<<','<<(o?o->owner:"")<<",1,"<<(o?o->health:-1)<<','<<(o&&o->alive)<<','<<expected<<','<<intents(s)<<','<<records(s)<<','<<before<<','<<s.runtimeLastUnitLostSoundTick<<','<<finalized<<','<<okay<<'\n';cases.flush();dump(s);
        if(!okay)return fail(error.empty()?"actual death/exit/building presentation failed":error+"; finalized="+std::to_string(finalized)+" presentation="+std::to_string(presentation));
        return true;
    };
    const auto step=[](RuntimeWindowState& s){RuntimeSnapshotActiveStateScope active(s);++s.tick;advanceRuntimeSimulation(s,false);completeRuntimeWorldRefresh(s);};
    const auto done=[&](){++fixtures;std::cout<<"Issue25 own building loss audio: "<<scene<<" PASS\n";};    const std::array<const char*,3> powers{{"GAPOWR","NAPOWR","YAPOWR"}},yards{{"GAYARD","NAYARD","YAYARD"}};
    // Start with an actual own power-plant death, so the unchanged producer
    // provides a short, reviewable negative baseline before the three guards.
    for(int side=0;side<3;++side)for(const auto& type:std::array<std::string,3>{{powers[side],"CAOILD",yards[side]}}){
        scene=std::string(voices[side].key)+"-own-"+type;
        auto box=make(side);auto& s=*box;const auto* b=s.rules.findBuildingType(type);
        if(!b||b->strength!=std::stoi(originalRules.getValue(type,"Strength")))return fail("original structure Strength");
        const bool yard=type==yards[side];const int id=add(s,type,1,yard?28:12,12,s.economy.owner);const int before=s.runtimeLastUnitLostSoundTick;
        if(!impact(s,id)||!row(s,side,id,"ordinary-projectile",0,before,before,true))return false;
        ++ordinaryStructures;done();
    }
    for(int side=0;side<3;++side)for(const char* type:{"E1","MTNK","ORCA","DEST"}){
        scene=std::string(voices[side].key)+"-own-"+type;
        auto box=make(side);auto& s=*box;
        // VehicleTypes are queried through the existing UnitType API category.
        const auto* u=s.rules.findUnitType(type==std::string("E1")?"InfantryType":type==std::string("ORCA")?"AircraftType":"UnitType",type);
        const int originalStrength=std::stoi(originalRules.getValue(type,"Strength"));
        if(!u||u->strength!=originalStrength)return fail("original mobile Strength loaded="+(u?std::to_string(u->strength):std::string("missing type"))+" original="+std::to_string(originalStrength));
        const int id=add(s,type,1,type==std::string("DEST")?28:12,12,s.economy.owner),before=s.runtimeLastUnitLostSoundTick;
        if(!impact(s,id)||!row(s,side,id,"ordinary-projectile",1,before,s.tick))return false;
        ++ordinaryUnits;done();
    }
    for(bool allied:{false,true})for(bool building:{true,false}){
        scene=std::string(allied?"allied":"enemy")+"-nonlocal-"+(building?"structure":"unit");
        auto box=make(0);auto& s=*box;const std::string owner=allied?"British":"Russians";
        if(allied){s.playerCommands.alliances[{"americans","british"}]=true;s.playerCommands.alliances[{"british","americans"}]=true;}
        if(runtimeOwnersAreAllied(s,s.economy.owner,owner)!=allied)return fail("actual diplomacy input");
        const int id=add(s,building?"GAPOWR":"E1",1,12,12,owner),before=s.runtimeLastUnitLostSoundTick;
        // For enemy victims use a local original weapon; for allied victims
        // use a hostile original weapon. No friendly-fire override is needed.
        if(!allied){s.economy.owner="Americans";}
        const auto* victim=findObjectById(s,id);
        const auto* weapon=s.rules.findWeaponType("120mm");const auto* projectile=weapon?s.rules.findProjectileType(weapon->projectile):nullptr;const auto* wh=weapon?s.rules.findWarheadType(weapon->warhead):nullptr;
        if(!victim||!weapon||!projectile||!wh)return fail("nonlocal original producer");
        const auto p=runtimeProjectileTargetPosition(s,*victim);RuntimeProjectileEffect hit;hit.actorId=nextRuntimeProjectileActorId(s);hit.targetObjectId=id;
        hit.owner=allied?"Russians":"Americans";hit.toCell=hit.currentCell=p.cell;hit.toLevel=hit.currentLevel=p.level;hit.currentX=p.x;hit.currentY=p.y;hit.currentZ=p.level;
        hit.hasTargetPosition=true;hit.targetX=p.x;hit.targetY=p.y;hit.targetZ=p.level;hit.weaponId=weapon->id;hit.projectileId=projectile->id;hit.warheadId=wh->id;hit.damage=weapon->damage;
        hit.ag=projectile->ag;hit.aa=projectile->aa;hit.cellSpread=wh->cellSpread;hit.percentAtMax=wh->percentAtMax;hit.affectsAllies=wh->affectsAllies;hit.affectsEnemies=wh->affectsEnemies;
        if(!runtimeProjectileCanHitTarget(s,hit,*victim)||!applyRuntimeProjectileImpact(s,hit)||!row(s,0,id,"ordinary-projectile/nonlocal",0,before,before,building))return false;
        ++nonlocal;done();
    }
    {
        scene="mixed-one-original-Demobomb";auto box=make(0);auto& s=*box;const int before=s.runtimeLastUnitLostSoundTick;
        add(s,"GAPOWR",1,12,12,s.economy.owner);add(s,"E1",2,16,12,s.economy.owner);
        if(!impact(s,1,"Demobomb")||!row(s,0,1,"original-Demobomb/one-blast",1,before,s.tick,true)
            ||!row(s,0,2,"original-Demobomb/one-blast",1,before,s.tick))return false;
        ++mixed;done();
    }
    {
        scene="own-building-then-unit-same-tick";auto box=make(0);auto& s=*box;const int before=s.runtimeLastUnitLostSoundTick;
        add(s,"GAPOWR",1,12,12,s.economy.owner);add(s,"E1",2,20,20,s.economy.owner);
        if(!impact(s,1)||!row(s,0,1,"ordinary-projectile/first-same-tick",0,before,before,true)
            ||!impact(s,2)||!row(s,0,2,"ordinary-projectile/second-same-tick",1,before,s.tick))return false;
        ++sequence;done();
    }
    {
        scene="unit-building-unit-original-two-second-throttle";auto box=make(0);auto& s=*box;const int before=s.runtimeLastUnitLostSoundTick;
        add(s,"E1",1,10,10,s.economy.owner);add(s,"GAPOWR",2,14,14,s.economy.owner);add(s,"E1",3,20,20,s.economy.owner);add(s,"E1",4,22,22,s.economy.owner);
        const int firstTick=s.tick;
        if(!impact(s,1)||!row(s,0,1,"ordinary-projectile/first",1,before,firstTick))return false;
        // Advance only the producer clock; no real-time or scheduler-rate
        // assumption is used for the unchanged two-second notification gate.
        s.tick=firstTick+29;
        if(!impact(s,2)||!row(s,0,2,"ordinary-projectile/building-within-cooldown",1,firstTick,firstTick,true))return false;
        s.tick=firstTick+59;
        if(!impact(s,3)||!row(s,0,3,"ordinary-projectile/one-tick-before-two-seconds",1,firstTick,firstTick))return false;
        s.tick=firstTick+60;
        if(!impact(s,4)||!row(s,0,4,"ordinary-projectile/exact-two-seconds",2,firstTick,s.tick))return false;
        ++throttle;done();
    }    for(bool building:{true,false}){
        scene=std::string("continuous-real-CLEG-Temporal-")+(building?"structure":"unit");auto box=make(0);auto& s=*box;
        add(s,"CLEG",1,8,8,"Russians",-1);add(s,building?"GAPOWR":"MTNK",2,10,8,s.economy.owner);
        auto* attacker=findMutableObjectById(s,1);const auto* target=findObjectById(s,2);
        const int required=std::stoi(originalRules.getValue(target->type,"Strength"))*10,before=s.runtimeLastUnitLostSoundTick;
        const auto profile=runtimeWeaponProfileForObject(s,*attacker,target);
        if(!profile.weapon||profile.weapon->id!="NeutronRifle"||profile.weapon->damage!=8||!profile.warhead||!profile.warhead->temporal
            ||!runtimeComputerAiIssueAttack(s,*attacker,*target,false,0))return fail("original enemy CLEG attack unavailable");
        for(int wait=0;wait<300&&!s.specialMechanisms.hasTemporalAttacker(1,2);++wait)step(s);
        const auto initial=s.specialMechanisms.temporalTargets().find(2);
        if(initial==s.specialMechanisms.temporalTargets().end()||initial->second.requiredProgress!=required
            ||initial->second.progressByAttacker.at(1)!=0)return fail("real original ray did not attach with zero progress");
        int expected=0;bool completed=false;
        for(int elapsed=0;elapsed<(required+7)/8*2+4;++elapsed){
            const int tickBefore=s.tick;step(s);expected+=(s.tick*15/30-tickBefore*15/30)*8;
            const auto found=s.specialMechanisms.temporalTargets().find(2);const auto* actual=findRuntimeObjectByIdIncludingInactive(s,2);
            const int actualProgress=found==s.specialMechanisms.temporalTargets().end()?-1:found->second.progressByAttacker.at(1);
            temporal<<quote(scene)<<','<<s.tick<<",1,2,"<<expected<<','<<required<<','<<actualProgress<<','<<(actual&&actual->alive)<<','<<(actual?actual->health:-1)<<','<<intents(s)<<','<<records(s)<<'\n';
            if(expected>=required){
                completed=actual&&!actual->alive&&actual->health==0&&found==s.specialMechanisms.temporalTargets().end();break;
            }
            if(!actual||!actual->alive||actual->health!=1||found==s.specialMechanisms.temporalTargets().end()
                ||actualProgress!=expected||found->second.requiredProgress!=required
                ||!soundCheck(s,0,0,before)){temporal.flush();return fail("continuous authored Damage/rule-frame or early loss mismatch");}
        }
        temporal.flush();
        if(!completed||!row(s,0,2,"continuous-original-Temporal/full-simulation",building?0:1,before,building?before:s.tick))return false;
        ++continuous;done();
    }
    for(bool building:{true,false}){
        scene=std::string("checkpoint-TemporalRemoved-projectile-tail-")+(building?"structure":"unit");auto box=make(0);auto& s=*box;
        add(s,"CLEG",1,8,8,"Russians",-1);add(s,building?"GAPOWR":"MTNK",2,10,8,s.economy.owner);add(s,"E1",3,22,22,"British");
        const auto* target=findObjectById(s,2);const int required=std::stoi(originalRules.getValue(target->type,"Strength"))*10,before=s.runtimeLastUnitLostSoundTick;
        bool completed=false;std::string reason;
        const auto allies=[&](const std::string& a,const std::string& b){return runtimeOwnersAreAllied(s,a,b);};
        // This is an explicitly labelled lawful restored mechanism boundary,
        // not a claim that a natural attack completed in one simulation tick.
        if(!s.specialMechanisms.applyTemporalHit(s.world.accessObjects(),s.rules,1,2,0,required,s.tick,allies,runtimeSpecialEventSink(s),completed,reason)
            ||completed||!s.specialMechanisms.applyTemporalHit(s.world.accessObjects(),s.rules,1,2,required,required,s.tick,allies,runtimeSpecialEventSink(s),completed,reason)
            ||!completed)return fail("legal original TemporalCompleted input unavailable: "+reason);
        target=findObjectById(s,2);
        if(!target||!target->alive||target->health!=0||target->mission!="TemporalRemoved"||!soundCheck(s,0,0,before))return fail("checkpoint must precede exit/UnitLost tail");
        sources<<scene<<": checkpoint-input original NeutronRifle Damage8; Strength*10="<<required<<"; original mechanism completed, alive/HP0/TemporalRemoved before unrelated original projectile.\n";sources.flush();
        if(!impact(s,3)||!dead(s,3)||!row(s,0,2,"checkpoint-input/actual-post-projectile-TemporalRemoved-tail",building?0:1,before,building?before:s.tick))return false;
        ++checkpointTail;done();
    }
    if(waves!=3||fixtures!=32||ordinaryStructures!=9||ordinaryUnits!=12||nonlocal!=4||mixed!=1||sequence!=1||throttle!=1||continuous!=2||checkpointTail!=2)
        return fail("terminal focused fixture guardcounts");
    std::ofstream result(root+"/result.txt");
    result<<"PASS\nfixtures="<<fixtures<<"\noriginal_voices="<<waves<<"\nordinary_structures="<<ordinaryStructures<<"\nordinary_units="<<ordinaryUnits
        <<"\nnonlocal="<<nonlocal<<"\nmixed="<<mixed<<"\nsame_tick_sequence="<<sequence<<"\noriginal_2s_throttle="<<throttle
        <<"\ncontinuous_temporal="<<continuous<<"\ncheckpoint_temporal_tail="<<checkpointTail<<"\n";
    std::cout<<"Issue25 own building loss audio: 32 fixtures, 3 original decoded faction voices PASS\n";return true;
}
#ifdef _MSC_VER
#pragma optimize("", on)
#endif