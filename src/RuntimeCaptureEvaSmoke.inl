bool runRuntimeCaptureEvaSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail=[&](const std::string& why){error="Capture EVA: "+why;return false;};
    const char* requested=std::getenv("CONCORD_CAPTURE_EVA_DIR");
    const std::string root=requested&&*requested?requested:"build/smoke/feature-checks/16-capture-eva/checks";
    std::filesystem::create_directories(root);
    auto stock=std::make_unique<RuntimeWindowState>(); stock->config=config;
    stock->config.defaultMapPath="local_data/resources/mix_exports/multimd/2peaks.map";
    stock->config.computerAiEnabled=stock->config.noCooldown=stock->config.multiplayerActive=false;
    stock->config.playerSlots.clear();stock->config.gameModeRulesOverride.clear();
    stock->report=buildRuntimeStartupReport(stock->config,&stock->world,&stock->theater,&stock->art,&stock->rules,&stock->mapFacts);
    stock->rulesLoaded=stock->artLoaded=stock->theaterLoaded=true;
    stock->runtimePreferencePersistenceEnabled=stock->runtimeSoundsEnabled=false;
    if(!stock->report.success || !loadRuntimeSoundDatabase(*stock))return fail("original resources");
    IniFile source;if(!source.loadFromFile("local_data/rulesmd.ini"))return fail("stock rules");
    int rows=0,configured=0,captures=0,repeats=0,silent=0,losses=0,boundaries=0,waves=0;
    for(const auto& type:stock->rules.getBuildingTypes()) {
        if(type.captureEvaEvent!=trimAsciiLocal(source.getValue(type.id,"CaptureEvaEvent","")))return fail(type.id+" loader");
        ++rows;if(!type.captureEvaEvent.empty())++configured;
    }
    if(rows!=399 || configured!=8)return fail("building registry coverage");
    const std::vector<std::pair<std::string,std::string>> buildings{
        {"CAOUTP","EVA_RepairFacilityCaptured"},{"CAAIRP","EVA_AirfieldCaptured"},
        {"CAOILD","EVA_OilRefineryCaptured"},{"CAPOWR","EVA_TechBuildingCaptured"},
        {"CAMACH","EVA_MachineShopCaptured"},{"CATHOSP","EVA_HospitalCaptured"},
        {"CAHOSP","EVA_HospitalCaptured"},{"CASLAB","EVA_SecretLabCaptured"},
        {"GAPOWR","EVA_BuildingCaptured"}};
    const std::array<const char*,3> owners{"Americans","Russians","YuriCountry"};
    const std::array<const char*,3> engineers{"ENGINEER","SENGINEER","YENGINEER"};
    const auto make=[&](int side) {
        auto box=std::make_unique<RuntimeWindowState>();auto& s=*box;
        s.config=stock->config;s.config.playerCountry=owners[side];s.config.crates=false;s.config.shortGame=false;
        s.rules=stock->rules;s.art=stock->art;s.theater=stock->theater;s.report=stock->report;s.world=stock->world;
        s.world.accessObjects().clear();s.world.accessTerrainCells().clear();s.world.accessTerrainObjects().clear();s.world.accessSmudges().clear();
        s.rulesLoaded=s.artLoaded=s.theaterLoaded=true;s.visionEnabled=false;
        s.evaIni=stock->evaIni;s.soundIni=stock->soundIni;s.evaLoaded=stock->evaLoaded;s.soundLoaded=stock->soundLoaded;
        s.runtimePreferencePersistenceEnabled=s.runtimeSoundsEnabled=false;s.runtimeSuppressPlayerAudioFeedback=true;
        s.runtimeVoiceVolume=100;s.runtimeSoundSamplePathCache=stock->runtimeSoundSamplePathCache;
        s.economy.owner=owners[side];s.economy.credits=10000;s.economy.techLevel=10;s.economy.skirmishStartApplied=true;
        initializeRuntimeLocalProductionLanes(s.economy);s.triggers.bootstrapComplete=true;
        for(int y=1;y<=32;++y)for(int x=1;x<=32;++x) {
            TerrainCell t;t.x=x;t.y=y;t.cellIndex512=runtimeCellKey(x,y);t.tileIndex=t.subTileIndex=0;
            s.world.accessTerrainCells().push_back(t);
        }
        initializeRuntimeNavigation(s);return box;
    };
    const auto setup=[&](RuntimeWindowState& s,int side,const std::string& id,const std::string& oldOwner,const std::string& newOwner) {
        const auto* type=findRuntimeBuildingTypeById(s.rules,id);
        WorldObjectInstance building;building.instanceId=1600;building.kind=WorldObjectKind::Structure;
        building.type=id;building.owner=oldOwner;building.health=type->strength;building.alive=true;
        building.x=building.y=16;building.cell=runtimeCellKey(16,16);building.mission="Guard";
        WorldObjectInstance engineer;engineer.instanceId=1601;engineer.kind=WorldObjectKind::Infantry;
        engineer.type=engineers[side];engineer.owner=newOwner;engineer.health=75;engineer.alive=true;
        engineer.x=engineer.y=16;engineer.cell=building.cell;engineer.mission="Enter";
        s.world.accessObjects()={building,engineer};refreshRuntimeObjectOccupancyAfterLocalChange(s);
        s.pendingEngineerCaptures[1601]=1600;s.pendingEngineerActions[1601]=RuntimeEngineerAction::Capture;
    };
    const auto isCapture=[](const RuntimeSoundPlaybackRecord& record) {
        const auto key=toLowerAsciiLocal(record.section);
        return key.find("captured")!=std::string::npos || key=="eva_techbuildinglost";
    };
    std::ofstream cases(root+"/capture-events.csv");
    cases<<"side,building,mode,event,sample,subtitle,status,path\n";
    for(int side=0;side<3;++side)for(const auto& [id,event]:buildings)for(int mode=0;mode<4;++mode) {
        auto box=make(side);auto& s=*box;
        const std::string oldOwner=mode==2?owners[side]:mode==1?"CaptureOpponent":"Special";
        const std::string newOwner=mode<2?owners[side]:"CaptureOpponent";
        setup(s,side,id,oldOwner,newOwner);
        if(!completeRuntimeEngineerCapture(s,1601))return fail(id+" actual capture rejected: "+s.simulationStatus);
        const auto* target=findObjectById(s,1600);
        if(!target || target->owner!=newOwner || findObjectById(s,1601))return fail(id+" capture/consumption state");
        std::vector<RuntimeSoundPlaybackRecord> found;
        for(const auto& record:s.runtimeSoundPlaybackHistory)if(isCapture(record))found.push_back(record);
        const std::string expected=mode==3?"":mode==2&&id!="GAPOWR"?"EVA_TechBuildingLost":event;
        if(found.size()!=(expected.empty()?0U:1U))return fail(id+" duplicate/missing capture event mode="+std::to_string(mode));
        if(!expected.empty()) {
            const auto& record=found.front();const std::string voice=runtimeEvaVoiceKeyForOwner(s);
            const std::string sample=trimAsciiLocal(stock->evaIni.getValue(expected,voice,""));
            const std::string subtitle=trimAsciiLocal(stock->evaIni.getValue(expected,"Text",""));
            if(record.section!=expected || record.sample!=sample || record.subtitleText!=subtitle
                || !record.eva || !record.playable || !record.queueRequested || record.path.empty()
                || runtimeWavDurationTicks(record.path,1000)<=1)return fail(id+" faction/sample/subtitle/queue mapping");
            cases<<voice<<','<<id<<','<<mode<<','<<expected<<','<<sample<<','<<std::quoted(subtitle)<<','<<record.status<<','<<std::quoted(record.path)<<'\n';
            if(mode==2)++losses;
        } else {++silent;cases<<runtimeEvaVoiceKeyForOwner(s)<<','<<id<<','<<mode<<",,,,,\n";}
        const auto history=s.runtimeSoundPlaybackHistory.size();
        if(completeRuntimeEngineerCapture(s,1601) || history!=s.runtimeSoundPlaybackHistory.size())return fail(id+" duplicate completion replayed audio");
        ++captures;++repeats;
    }
    // Resource proof includes every faction's seven distinct special announcements,
    // generic building capture and tech loss. Decode duration from the actual WAVs.
    std::set<std::string> events{"EVA_TechBuildingLost"};for(const auto& item:buildings)events.insert(item.second);
    std::ofstream audio(root+"/capture-audio.csv");audio<<"voice,event,sample,milliseconds,bytes,path\n";
    for(int side=0;side<3;++side) {
        auto box=make(side);auto& s=*box;
        for(const auto& event:events) {
            const auto sample=resolveRuntimeSoundSampleName(s,event,event);
            const auto path=sample?findRuntimeSoundSamplePath(s,*sample):std::optional<std::string>{};
            if(!sample || !path || runtimeWavDurationTicks(*path,1000)<=1 || std::filesystem::file_size(*path)<=44)
                return fail(event+" original wave unavailable");
            audio<<runtimeEvaVoiceKeyForOwner(s)<<','<<event<<','<<*sample<<','<<runtimeWavDurationTicks(*path,1000)<<','
                <<std::filesystem::file_size(*path)<<','<<std::quoted(*path)<<'\n';++waves;
        }
    }
    auto box=make(0);auto& s=*box;BuildingType type=*findRuntimeBuildingTypeById(s.rules,"CAOILD");
    const auto resetAudio=[&]() {
        s.runtimeSoundPlaybackHistory.clear();s.runtimeSoundIntentHistory.clear();s.runtimeEvaQueue.clear();
        s.runtimeEvaBusyUntilTick=0;s.runtimeEvaMciChannelActive=false;s.runtimeEvaActivePriority=-1;clearRuntimeEvaSubtitle(s);
    };
    const auto expect=[&](const BuildingType& target,const std::string& before,const std::string& after,const std::string& wanted) {
        resetAudio();emitRuntimeEngineerCaptureEva(s,&target,before,after);
        const auto& history=s.runtimeSoundPlaybackHistory;
        ++boundaries;return wanted.empty()?history.empty():history.size()==1&&history.back().section==wanted;
    };
    for(const char* value:{"","<none>","none","-1","UnregisteredCapture"}) {
        type.captureEvaEvent=value;
        if(!expect(type,"Special",s.economy.owner,""))return fail("invalid/unset event fell back to generic");
    }
    IniFile extra;extra.loadFromString("[UnregisteredCapture]\nAllied=ceva138\nText=Unregistered\n");s.evaIni.appendFrom(extra);
    if(!expect(type,"Special",s.economy.owner,""))return fail("unregistered section bypassed DialogList");
    type.captureEvaEvent="  eva_oilrefinerycaptured  ";
    if(!expect(type,"Special",s.economy.owner,"EVA_OilRefineryCaptured"))return fail("case/whitespace event");
    type.needsEngineer=false;
    if(!expect(type,"Special",s.economy.owner,"EVA_BuildingCaptured"))return fail("ordinary building field override");
    type.needsEngineer=true;
    if(!expect(type,s.economy.owner,s.economy.owner,"") || !expect(type,s.economy.owner,"Special",""))return fail("same/neutral owner event");
    s.remoteHumanHouseScopeActive=true;
    if(!expect(type,"Special",s.economy.owner,""))return fail("remote authority leaked capture audio");
    s.remoteHumanHouseScopeActive=false;
    // Configured captions are preserved, including an intentionally empty Text.
    extra.loadFromString("[DialogList]\n9999=CaptureCustom\n[CaptureCustom]\nAllied=ceva138\nText=Facility secure.\nType=QUEUE\nPriority=NORMAL\n");
    s.evaIni.appendFrom(extra);type.captureEvaEvent="CaptureCustom";
    if(!expect(type,"Special",s.economy.owner,"CaptureCustom") || s.runtimeEvaSubtitleText!="Facility secure.")return fail("custom subtitle");
    type.captureEvaEvent="EVA_HospitalCaptured";
    if(!expect(type,"Special",s.economy.owner,"EVA_HospitalCaptured") || !s.runtimeEvaSubtitleText.empty())return fail("blank original subtitle");
    resetAudio();s.runtimeEvaBusyUntilTick=1000;s.runtimeEvaActivePriority=20;
    emitRuntimeEngineerCaptureEva(s,&type,"Special",s.economy.owner);
    if(s.runtimeEvaQueue.size()!=1 || s.runtimeEvaQueue.front().section!="EVA_HospitalCaptured")return fail("busy EVA queue");
    s.tick=1001;processRuntimeEvaQueue(s);
    if(!s.runtimeEvaQueue.empty() || s.runtimeSoundPlaybackHistory.back().section!="EVA_HospitalCaptured"
        || s.runtimeSoundPlaybackHistory.back().queued)return fail("queued EVA did not play");++boundaries;
    s.campaignMode=true;s.triggers.campaignHouseProduction["secondhouse"].playerControlled=true;
    if(!expect(type,"Special","SecondHouse","EVA_HospitalCaptured"))return fail("campaign PlayerControl house");
    s.triggers.campaignCanonicalHouseByAlias["secondalias"]="SecondHouse";
    if(!expect(type,"SecondAlias","SecondHouse",""))return fail("campaign alias repeated capture");
    s.campaignMode=false;
    // An unfinished arrival and an allied repair must not emit capture announcements.
    auto arrival=make(0);setup(*arrival,0,"CAOILD","Special",owners[0]);
    arrival->world.accessObjects()[1].x=1;arrival->world.accessObjects()[1].y=1;arrival->world.accessObjects()[1].cell=runtimeCellKey(1,1);
    if(completeRuntimeEngineerCapture(*arrival,1601) || !arrival->runtimeSoundPlaybackHistory.empty())return fail("unfinished arrival announced");++boundaries;
    auto repair=make(0);setup(*repair,0,"GAPOWR",owners[0],owners[0]);repair->world.accessObjects()[0].health=1;
    repair->pendingEngineerActions[1601]=RuntimeEngineerAction::Repair;
    if(!completeRuntimeEngineerCapture(*repair,1601))return fail("repair fixture");
    for(const auto& record:repair->runtimeSoundPlaybackHistory)if(isCapture(record))return fail("repair emitted capture EVA");++boundaries;
    IniFile hashIni;hashIni.loadFromString("[BuildingTypes]\n0=TEST\n[TEST]\nNeedsEngineer=yes\nCaptureEvaEvent=EVA_HospitalCaptured\n");
    RulesDatabase a,b;a.loadFromIni(hashIni);extra.loadFromString("[TEST]\nCaptureEvaEvent=EVA_OilRefineryCaptured\n");hashIni.appendFrom(extra);b.loadFromIni(hashIni);
    if(a.getDetailRulesHash()==b.getDetailRulesHash())return fail("CaptureEvaEvent compatibility hash");++boundaries;
    // Independent WAV headers exercise exact FACT timing, chunk order and
    // PCM/mod fallbacks; actual stock ADPCM files are also decoded externally.
    const auto waveFixture = [&](const std::string& name, int tag, int rate, int byteRate,
        int alignment, int bits, int perBlock, int dataSize, int factCount, bool factLast) {
        std::vector<unsigned char> data;
        const auto word = [&](unsigned value, int bytes) { for(int i=0;i<bytes;++i)data.push_back(static_cast<unsigned char>(value>>(i*8))); };
        const auto tag4 = [&](const char* value) { for(int i=0;i<4;++i)data.push_back(value[i]); };
        tag4("RIFF"); word(0,4); tag4("WAVE"); tag4("fmt "); word(tag==0x11?20:16,4);
        word(tag,2);word(1,2);word(rate,4);word(byteRate,4);word(alignment,2);word(bits,2);
        if(tag==0x11){word(2,2);word(perBlock,2);}
        const auto fact = [&]() { if(factCount>=0){tag4("fact");word(4,4);word(factCount,4);} };
        if(!factLast)fact();tag4("data");word(dataSize,4);data.resize(data.size()+dataSize+(dataSize&1),0);if(factLast)fact();
        const auto riffSize=static_cast<unsigned>(data.size()-8);for(int i=0;i<4;++i)data[4+i]=static_cast<unsigned char>(riffSize>>(i*8));
        const auto path=root+"/"+name+".wav";std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());out.close();return path;
    };
    const auto pcm=waveFixture("duration-pcm",1,22050,44100,2,16,0,44100,-1,false);
    if(runtimeWavDurationTicks(pcm,1000)!=1000)return fail("PCM duration regression");++boundaries;
    const auto ima=waveFixture("duration-ima",0x11,22050,11100,512,4,1017,33792,66207,false);
    if(runtimeWavDurationTicks(ima,1000)!=3003 || runtimeWavDurationTicks(ima,30)!=91)return fail("IMA FACT exact frames");++boundaries;
    const auto late=waveFixture("duration-fact-last",0x11,22050,11100,512,4,1017,33792,66207,true);
    if(runtimeWavDurationTicks(late,1000)!=3003)return fail("FACT after data");++boundaries;
    const auto noFact=waveFixture("duration-no-fact",0x11,1000,500,512,4,1017,520,-1,false);
    if(runtimeWavDurationTicks(noFact,1000)!=1026)return fail("IMA block fallback");++boundaries;
    const auto unknown=waveFixture("duration-unknown",2,1000,500,256,4,0,1000,-1,false);
    if(runtimeWavDurationTicks(unknown,1000)!=2000)return fail("compressed average rate fallback");++boundaries;
    const auto maximum=waveFixture("duration-overflow",0x11,1,1,512,4,1017,512,2147483647,false);
    if(runtimeWavDurationTicks(maximum,1000)!=(std::numeric_limits<int>::max)())return fail("duration integer overflow");++boundaries;
    std::cout<<"CaptureEva PASS buildings="<<rows<<" configured="<<configured<<" captures="<<captures<<" repeats="<<repeats
        <<" losses="<<losses<<" silent="<<silent<<" waves="<<waves<<" boundaries="<<boundaries<<'\n';return true;
}
