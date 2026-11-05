// W007 caller contract: exact full old functions, same real production world and complete codecs.
class RuntimeChronosphereCallerClockOriginal {
public:
    explicit RuntimeChronosphereCallerClockOriginal(const RuntimeUiClock& source):source_(source){}
    std::uint64_t GetTickCount64() const noexcept{return source_.nowMs();}
#include "audio/events/diagnostics/chronosphere_callers_original/Consumers.original.inl"
private:
    const RuntimeUiClock& source_;
};

bool runRuntimeChronosphereCallerClockSmoke(RuntimeWindowState& stock,std::ostream& output){
    struct Clock final : RuntimeUiClock {
        std::uint64_t base = 0;
        mutable std::vector<std::uint64_t> values;
        Clock() { values.reserve(4096); }
        std::uint64_t nowMs() const noexcept override {
            const auto value = base + static_cast<std::uint64_t>(values.size());
            values.push_back(value);
            return value;
        }
        void reset(std::uint64_t value) { base = value; values.clear(); }
    } currentClock, originalClock;
    RuntimeChronosphereCallerClockOriginal original(originalClock);
    struct FixedUi final:RuntimeUiClock{mutable std::size_t reads=0;std::uint64_t nowMs() const noexcept override{++reads;return 123;}}uiClock;
    std::size_t checks=0,failures=0,comparisons=0,totalReads=0,hashes=0;
    const auto expect=[&](bool value,const std::string& label){++checks;if(!value){++failures;output<<"Chronosphere caller clock failure: "<<label<<'\n';}return value;};
    const char* path=std::getenv("CONCORD_CHRONOSPHERE_CALLER_CLOCK_OUTPUT");
    if(!expect(path&&*path,"explicit owned output required"))return false;
    const std::filesystem::path directory(path);
    if(!expect(!std::filesystem::exists(directory),"all earlier evidence retained"))return false;
    std::filesystem::create_directories(directory);
    const auto make = [&](const RuntimeUiClock& clock) {
        auto p = std::make_unique<RuntimeWindowState>(); auto& s = *p;
        s.config = stock.config; s.config.fixedTickHz = 30; s.config.computerAiEnabled = false;
        s.config.multiplayerActive = false; s.config.playerSlots.clear();
        s.report = stock.report; s.rules = stock.rules; s.art = stock.art; s.theater = stock.theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.runtimeSoundsEnabled = false; // Observe the real local event/PCM algorithms, without opening a device.
        s.runtimeSoundEffectsEnabled = true; s.runtimeSoundVolume = 100;
        s.runtimeSuppressPlayerAudioFeedback = false; s.runtimePreferencePersistenceEnabled = false;
        s.runtimeMusicEnabled = false; s.visionEnabled = false;
        s.economy.owner = "Commander"; s.skirmishCountryByOwner["commander"]="Americans"; s.config.superweapons=true; s.config.noCooldown=false; s.economy.credits = 1000;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true; s.tick = 17;
        s.world = stock.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y=40; y<=100; ++y) for (int x=40; x<=100; ++x) {
            TerrainCell cell; cell.x=x; cell.y=y; cell.cellIndex512=runtimeCellKey(x,y);
            cell.tileIndex=cell.subTileIndex=0; s.world.accessTerrainCells().push_back(cell);
            s.terrainInfoByCell.emplace(cell.cellIndex512, RuntimeTerrainInfo{});
        }
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        for (const auto& item : std::array<std::pair<const char*,RuntimeCellPoint>,3>{
                 std::pair{"DISK",RuntimeCellPoint{60,60}}, {"YTNK",{62,60}}, {"HTNK",{65,60}}}) {
            if (!addRuntimeObjectInstance(s,RuntimeBuildKind::Unit,item.first,item.second)) return std::unique_ptr<RuntimeWindowState>{};
        }
        auto& target=s.world.accessObjects().back(); target.owner="Americans"; target.health=100000;
        s.camera.initialized=true; s.cameraClampViewport={0,0,1200,800};
        const auto projected=runtimeProjectCellXY(60,60,0,s.camera);
        s.camera.worldOffsetX=projected.x-600; s.camera.worldOffsetY=projected.y-400;
        s.uiClock=&uiClock; s.worldAudioClock=&clock;
        if (!loadRuntimeSoundDatabase(s)) return std::unique_ptr<RuntimeWindowState>{};
        s.runtimeSoundIntentHistory.clear(); s.runtimeWorldAudioBackendActionHistory.clear();
        int id=1;for(const auto& object:s.world.getObjects())id=std::max(id,object.instanceId+1);
        const auto building=[&](const BuildingType& rule,int x,int y){
            WorldObjectInstance object;object.instanceId=id++;object.kind=WorldObjectKind::Structure;
            object.type=rule.id;object.owner=s.economy.owner;object.alive=true;object.health=rule.strength;
            object.x=x;object.y=y;object.cell=runtimeCellKey(x,y);object.mission="Guard";s.world.accessObjects().push_back(object);
        };
        bool source=false;
        for(const auto& rule:s.rules.getBuildingTypes())if(rule.superWeapon=="ChronoSphereSpecial"){
            building(rule,45,45);source=true;break;
        }
        const auto* power=s.rules.findBuildingType("NANRCT");if(!source||!power)return std::unique_ptr<RuntimeWindowState>{};
        building(*power,50,45);building(*power,54,45);initializeRuntimeNavigation(s);
        ensureRuntimeSuperWeaponsInitialized(s);auto context=runtimeSuperWeaponContext(s);s.superWeapons.tick(context);
        if(!s.superWeapons.forceReady(s.economy.owner,"ChronoSphereSpecial",&context))return std::unique_ptr<RuntimeWindowState>{};
        context.tick=++s.tick;s.superWeapons.tick(context);(void)completeRuntimeWorldRefresh(s);
        return p;
    };
    const auto sound = [](std::ostream& stream,const RuntimeSuperWeaponBuildingSound& x) {
        stream<<std::quoted(x.stage)<<' '<<std::quoted(x.event)<<' '<<std::quoted(x.owner)<<' '<<std::quoted(x.sample)
            <<' '<<x.nextSampleMilliseconds<<' '<<x.queuedUntilMilliseconds<<' '<<x.ordinal<<' '<<x.gainPermille
            <<' '<<x.loop<<' '<<x.active<<' ';
        for(auto sample:x.remainingLoopSamples) stream<<sample<<',';
        stream<<'\n';
    };
    const auto observe = [&](const RuntimeWindowState& s) {
        std::ostringstream stream; stream<<std::hexfloat;
        const auto& c=s.creditAudio;
        stream<<c.observed<<' '<<std::quoted(c.owner)<<' '<<c.balance<<' '<<c.pendingDirection<<' '
            <<c.lastObservationMs<<' '<<c.nextSoundMs<<' '<<c.burstEndMs<<' '<<c.burstPlayed<<' '
            <<c.samplesPrepared<<' '<<std::quoted(c.configuredPair)<<' '<<std::quoted(c.sections[0])<<' '
            <<std::quoted(c.sections[1])<<' '<<c.intervalMs[0]<<' '<<c.intervalMs[1]<<'\n';
        const auto loops=[&](const RuntimeSuperWeaponBuildingAudioState& x) {
            stream<<x.primed<<'\n'; std::vector<int> ids;
            for(const auto& [id,value]:x.sources){ (void)value; ids.push_back(id); }
            std::sort(ids.begin(),ids.end());
            for(int id:ids){stream<<id<<' ';sound(stream,x.sources.at(id));}
            ids.assign(x.newlyCreatedSources.begin(),x.newlyCreatedSources.end());std::sort(ids.begin(),ids.end());
            for(int id:ids) stream<<id<<','; stream<<'\n';
        };
        loops(s.superWeaponBuildingAudio);loops(s.diskDrainAudio);loops(s.diskMoveAudio);loops(s.gattlingAudio);
        const auto& x=s.chronosphereSelectionAudio;
        stream<<std::quoted(x.owner)<<' '<<std::quoted(x.weaponId)<<' '<<x.cell.x<<' '<<x.cell.y<<' '<<x.cell.level<<' '<<x.selected<<'\n';sound(stream,x.sound);
        for(const auto& event:s.runtimeSoundIntentHistory) stream<<std::quoted(event)<<'\n';
        for(const auto& event:s.runtimeWorldAudioBackendActionHistory) {
            stream<<int(event.kind)<<' '<<event.handleId<<' '<<std::quoted(event.eventId)<<' '<<std::quoted(event.sampleName)
                <<' '<<event.waypointId<<' '<<event.cell.x<<' '<<event.cell.y<<' '<<event.cell.level<<' '
                <<event.eventTick<<' '<<event.playbackOrdinal<<' '<<event.gainPermille<<' '<<event.frequencyPermille
                <<' '<<event.local<<' '<<event.rangeCells<<' '<<event.backendLoopIterations<<' '<<std::quoted(event.reason)
                <<' '<<event.authoredGainPermille<<' '<<event.sourceObjectId<<' '<<event.trackCamera<<' '
                <<event.pan16384<<' '<<event.spatialGain16384<<' '<<event.queueAfterCurrent<<'\n';
        }
        return stream.str();
    };
    const auto stage=[&](RuntimeWindowState& s){
        auto context=runtimeSuperWeaponContext(s);context.emit=runtimeSuperWeaponEventSink(s);std::string error;
        return s.superWeapons.beginTargetSelection(s.economy.owner,"ChronoSphereSpecial",context,&error)
            &&s.superWeapons.submitTarget(s.economy.owner,"ChronoSphereSpecial",SuperWeaponTarget{{60,60},-1},context,&error);
    };
    const auto full=[&](RuntimeWindowState& s){
        RuntimeSnapshotActiveStateScope active(s);std::string error;std::ostringstream saved,extension,stream;
        expect(writeRuntimeStateToStream(s,saved,error),"actual complete ordinary save: "+error);
        expect(writeRuntimeMultiplayerSnapshotExtension(s,extension,error),"actual complete extension: "+error);
        stream<<saved.str()<<"\nFULL EXTENSION\n"<<extension.str()<<"\nAUDIO\n"<<observe(s);
        stream<<"FLAGS "<<s.tick<<' '<<s.navigationRefreshPending<<' '<<s.powerRefreshPending<<' '
            <<s.projectedObjectsCacheValid<<' '<<s.repaintRequested<<' '<<s.remoteHumanHouseScopeActive<<' '
            <<s.pendingSuperWeaponObjectEvents.size()<<' '<<s.pendingSuperWeaponImpacts.size()<<' '<<s.pendingSuperWeaponVisuals.size()<<'\n';
        stream<<"LOCAL "<<bool(s.localChronosphereMarker)<<'\n';
        if(s.localChronosphereMarker){const auto& m=*s.localChronosphereMarker;stream<<m.startedTick<<' '<<m.lastAnimationStep<<'\n'
            <<runtimeMultiplayerSnapshotImpactRecord(m.animation)<<'\n';}
        return stream.str();
    };
    const auto hash=[&](RuntimeWindowState& s){
        RuntimeSnapshotActiveStateScope active(s);std::string error;const auto h=runtimeStandaloneSimulationStateHash(s,error);
        expect(h!=0&&error.empty(),"actual complete stable authority hash: "+error);++hashes;return h;
    };
    std::ofstream currentTrace(directory/"current.txt",std::ios::binary),oldTrace(directory/"original.txt",std::ios::binary);
    const auto one=[&](int mode,std::uint64_t now,int hz){
        currentClock.reset(100);originalClock.reset(100);auto a=make(currentClock),b=make(originalClock);
        if(!expect(a&&b,"real source building/power/stock art/audio setup"))return false;
        a->config.fixedTickHz=b->config.fixedTickHz=hz;
        if(mode==4||mode==5||mode==6||mode==15){if(!expect(stage(*a)&&stage(*b),"actual first-target selection event"))return false;}
        if(mode==2||mode==3){
            RuntimeSuperWeaponVisualEvent v;v.event.kind="SuperWeaponFirstTargetAccepted";v.event.owner=a->economy.owner;
            v.event.superWeaponId=mode==2?"W007-intentionally-missing":"ChronoSphereSpecial";v.event.targetCell={60,60};
            a->pendingSuperWeaponVisuals.push_back(v);b->pendingSuperWeaponVisuals.push_back(v);
        }
        if(mode==10||mode==13||mode==14){
            beginRuntimeLocalChronosphereMarker(*a,"ChronoSphereSpecial",{60,60});
            original.beginRuntimeLocalChronosphereMarker(*b,"ChronoSphereSpecial",{60,60});
            if(!expect(a->localChronosphereMarker&&b->localChronosphereMarker,"actual nonempty marker committed"))return false;
            if(mode==13||mode==14)expect(a->chronosphereSelectionAudio.selected&&b->chronosphereSelectionAudio.selected,"actual authored nonempty audio selection");
            if(mode==14){stopRuntimeChronosphereSelectionAudio(*a,true);stopRuntimeChronosphereSelectionAudio(*b,true);
                expect(a->chronosphereSelectionAudio.sound.stage=="decay"&&a->chronosphereSelectionAudio.sound.active,"actual authored visible decay");}
        }
        if(mode==15){
            flushRuntimeSuperWeaponHostEvents(*a);original.flushRuntimeSuperWeaponHostEvents(*b);
            if(!expect(!a->impactEffects.empty()&&!b->impactEffects.empty(),"real selection impact"))return false;
            for(auto* s:{a.get(),b.get()})s->impactEffects.back().remainingTicks=s->impactEffects.back().animationFrameDelayTicks;
        }
        if(mode==4||mode==9){a->art=decltype(a->art){};b->art=decltype(b->art){};}
        // Observe and complete the real refresh boundary; never mask dirty state to obtain a hash.
        (void)completeRuntimeWorldRefresh(*a);(void)completeRuntimeWorldRefresh(*b);
        const auto beforeA=hash(*a),beforeB=hash(*b);expect(beforeA==beforeB,"same full authority before caller");
        const auto tick=a->tick;
        a->remoteHumanHouseScopeActive=b->remoteHumanHouseScopeActive=(mode==1||mode==6||mode==12);
        currentClock.reset(now);originalClock.reset(now);
        bool ca=false,cb=false;
        if(mode>=7&&mode<=12){
            const auto weapon=mode==7?"W007-intentionally-missing":mode==8?"IronCurtainSpecial":"ChronoSphereSpecial";
            {RuntimeSnapshotActiveStateScope active(*a);beginRuntimeLocalChronosphereMarker(*a,weapon,{60,60});}
            {RuntimeSnapshotActiveStateScope active(*b);original.beginRuntimeLocalChronosphereMarker(*b,weapon,{60,60});}
        }else{
            {RuntimeSnapshotActiveStateScope active(*a);ca=flushRuntimeSuperWeaponHostEvents(*a);}
            {RuntimeSnapshotActiveStateScope active(*b);cb=original.flushRuntimeSuperWeaponHostEvents(*b);}
        }
        const std::string label="mode-"+std::to_string(mode)+"-ms-"+std::to_string(now)+"-hz-"+std::to_string(hz);
        expect(ca==cb,label+" original result");expect(currentClock.values==originalClock.values,label+" all source values/order/count");
        const auto expected=mode==1||mode==7||mode==8||mode==9||mode==10?0U:mode==5||mode==13?2U:1U;
        expect(currentClock.values.size()==expected,label+" exact expected guard reads");totalReads+=currentClock.values.size();
        const auto sa=full(*a),sb=full(*b);expect(sa==sb,label+" every save/extension/audio/marker/flags field equal");
        currentTrace<<label<<'\n'<<sa;oldTrace<<label<<'\n'<<sb;
        for(const auto v:currentClock.values)currentTrace<<v<<',';for(const auto v:originalClock.values)oldTrace<<v<<',';
        currentTrace<<'\n';oldTrace<<'\n';
        expect(a->tick==tick&&b->tick==tick,label+" caller leaves authority tick unchanged");
        expect(a->remoteHumanHouseScopeActive==b->remoteHumanHouseScopeActive,label+" caller retains owned remote facade");
        a->remoteHumanHouseScopeActive=b->remoteHumanHouseScopeActive=false;
        (void)completeRuntimeWorldRefresh(*a);(void)completeRuntimeWorldRefresh(*b);
        const auto ha=hash(*a),hb=hash(*b);expect(ha==hb,label+" full closed-boundary authority equal");
        if(mode>=7&&mode<=12)expect(ha==beforeA&&hb==beforeB,label+" private local presentation never changes complete authority");
        if(mode==5)expect(a->chronosphereSelectionAudio.selected&&!a->impactEffects.empty(),label+" actual first-target/audio path reached");
        if(mode==11)expect(a->localChronosphereMarker&&a->chronosphereSelectionAudio.selected,label+" actual local animation/audio path reached");
        ++comparisons;return true;
    };
    for(int hz:{30,60}){
        for(int mode=0;mode<=15;++mode)if(mode!=5&&mode!=11){if(!one(mode,1000,hz))return false;}
        for(std::uint64_t now:std::array<std::uint64_t,9>{0,1,99,100,999,1000,0xffffffffULL,0x100000000ULL,std::numeric_limits<std::uint64_t>::max()}){
            if(!one(5,now,hz)||!one(11,now,hz))return false;
        }
        if(!one(14,0,hz)||!one(14,std::numeric_limits<std::uint64_t>::max(),hz))return false;
    }
    expect(uiClock.reads==0,"separate session UI port never read");expect(currentTrace.good()&&oldTrace.good(),"full observations retained");
    output<<"Chronosphere caller clock contract: checks="<<checks<<" comparisons="<<comparisons<<" sourceReads="<<totalReads
        <<" fullAuthorityHashes="<<hashes<<" failures="<<failures<<" skipped=0 originalConsumers=2 productionReads=3 uiReads="<<uiClock.reads
        <<" globalFakeClock=0 devicePlayback=not-run\n";
    return failures==0;
}
