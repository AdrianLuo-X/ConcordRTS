// W007-02 world audio: fixture-owned clocks and complete frozen original bodies.
// No global mutable clock, device substitution, alternate simulation or golden.
class RuntimeWorldAudioClockOriginal {
public:
    explicit RuntimeWorldAudioClockOriginal(const RuntimeUiClock& clock) : clock_(clock) {}
    std::uint64_t GetTickCount64() const noexcept { return clock_.nowMs(); }
#include "audio/events/diagnostics/world_audio_clock_original/Gattling.original.inl"
#include "audio/events/diagnostics/world_audio_clock_original/Chronosphere.original.inl"
#include "audio/events/diagnostics/world_audio_clock_original/Presentation.original.inl"
private:
    const RuntimeUiClock& clock_;
};

bool runRuntimeWorldAudioClockSmoke(RuntimeWindowState& stock, std::ostream& output) {
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
    RuntimeWorldAudioClockOriginal original(originalClock);
    struct FixedUiClock final : RuntimeUiClock {
        std::uint64_t nowMs() const noexcept override { return 123; }
    } uiClock;
    std::size_t checks = 0, comparisons = 0, authorityChecks = 0, failures = 0;
    bool success = true;
    const auto expect = [&](bool value, const std::string& label) {
        ++checks;
        if (!value) { ++failures; output << "World audio clock failure: " << label << '\n'; success = false; }
        return value;
    };
    const char* requested = std::getenv("CONCORD_WORLD_AUDIO_CLOCK_OUTPUT");
    if (!expect(requested && *requested, "explicit isolated output root required")) return false;
    const std::filesystem::path directory(requested);
    if (!expect(!std::filesystem::exists(directory), "retain prior fixture rounds")) return false;
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
        s.economy.owner = "YuriCountry"; s.economy.credits = 1000;
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
        return p;
    };
    auto current=make(currentClock), baseline=make(originalClock);
    if (!expect(current && baseline, "same original rules/art/sound/PCM and real world states")) return false;
    auto& a=*current; auto& b=*baseline;
    const auto hash = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); std::string error;
        const auto value=runtimeStandaloneSimulationStateHash(s,error);
        expect(value != 0 && error.empty(),"complete authority hash available: "+error);
        return value;
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
    std::ofstream trace(directory/"comparisons.tsv",std::ios::binary);
    const auto compare=[&](const std::string& label) {
        ++comparisons;
        expect(currentClock.values==originalClock.values,label+" exact original clock read sequence");
        expect(observe(a)==observe(b),label+" complete local timing, bags, event/action fields and order");
        const auto ha=hash(a),hb=hash(b);expect(ha==hb,label+" full authority same as original");++authorityChecks;
        trace<<label<<'\t'<<currentClock.values.size()<<'\t'<<ha<<'\t'<<hb<<'\n';
    };
    expect(observe(a)==observe(b),"identical initial local audio state");
    // Default argument semantics: a read even when the callee immediately rejects.
    RuntimeImpactEffect marker; marker.animId="CHRONOAR";marker.owner=a.economy.owner;
    marker.weaponId="ChronoSphereSpecial";marker.cell={60,60};marker.level=0;
    for(const auto now:std::array<std::uint64_t,4>{0,100,1000,std::numeric_limits<std::uint64_t>::max()}) {
        a.runtimeSuppressPlayerAudioFeedback=b.runtimeSuppressPlayerAudioFeedback=true;
        currentClock.reset(now);originalClock.reset(now);
        startRuntimeChronosphereSelectionAudio(a,marker);
        original.startRuntimeChronosphereSelectionAudio(b,marker,originalClock.nowMs());
        expect(currentClock.values.size()==1,"default time is read before a rejected guard");compare("default-rejected-"+std::to_string(now));
        currentClock.reset(now);originalClock.reset(now);
        startRuntimeChronosphereSelectionAudio(a,marker,now);
        original.startRuntimeChronosphereSelectionAudio(b,marker,now);
        expect(currentClock.values.empty(),"explicit time does not query host clock");compare("explicit-rejected-"+std::to_string(now));
    }
    a.runtimeSuppressPlayerAudioFeedback=b.runtimeSuppressPlayerAudioFeedback=false;
    currentClock.reset(100);originalClock.reset(100);
    startRuntimeChronosphereSelectionAudio(a,marker);
    original.startRuntimeChronosphereSelectionAudio(b,marker,originalClock.nowMs());
    expect(a.chronosphereSelectionAudio.selected,"authored original Chronosphere selection started");
    expect(std::any_of(a.runtimeWorldAudioBackendActionHistory.begin(),a.runtimeWorldAudioBackendActionHistory.end(),[](const auto& x){return x.kind==RuntimeWorldAudioActionKind::Play;}),"nonempty original sample/PCM actions observed");
    compare("authored-selection-default");
    // Exercise real decay and duplicate-stop guards, with the old complete body.
    currentClock.reset(200);originalClock.reset(200);
    stopRuntimeChronosphereSelectionAudio(a,true);original.stopRuntimeChronosphereSelectionAudio(b,true);
    expect(currentClock.values.size()==1,"visible authored decay reads once after its original guards");compare("decay-visible");
    currentClock.reset(300);originalClock.reset(300);
    stopRuntimeChronosphereSelectionAudio(a,true);original.stopRuntimeChronosphereSelectionAudio(b,true);
    expect(currentClock.values.empty(),"duplicate decay stop returns before clock read");compare("decay-duplicate");
    // Every consumer retains its separate read; backwards/late/mute/facade paths
    // execute the same original algorithms and exact unsigned arithmetic.
    int iteration=0;
    for(auto now:std::array<std::uint64_t,12>{0,1,79,80,99,100,999,1000,1001,5000,4999,9000}) {
        ++iteration;a.economy.credits+=17;b.economy.credits+=17;
        a.runtimeMenuOpen=b.runtimeMenuOpen=iteration==4;
        a.runtimeSuppressPlayerAudioFeedback=b.runtimeSuppressPlayerAudioFeedback=iteration==6;
        // The production authority hash only accepts a closed facade boundary.
        // Enter the remote facade solely around the consumer calls, just as the
        // host does, and verify its rejection contract before closing the scope.
        const bool remoteFacade=iteration==8;
        const auto beforeA=hash(a),beforeB=hash(b);
        a.remoteHumanHouseScopeActive=b.remoteHumanHouseScopeActive=remoteFacade;
        if(remoteFacade) {
            std::string ea,eb;
            expect(runtimeStandaloneSimulationStateHash(a,ea)==0&&ea=="simulation boundary is not stable"
                &&runtimeStandaloneSimulationStateHash(b,eb)==0&&eb==ea,
                "original production hash rejects an active remote facade");
        }
        currentClock.reset(now);originalClock.reset(now);
        {RuntimeSnapshotActiveStateScope active(a);advanceRuntimeWorldAudioPresentationClocks(a);}
        {RuntimeSnapshotActiveStateScope active(b);original.presentation(b);}
        expect(a.remoteHumanHouseScopeActive==remoteFacade&&b.remoteHumanHouseScopeActive==remoteFacade,
            "presentation preserves the caller-owned facade scope");
        a.remoteHumanHouseScopeActive=b.remoteHumanHouseScopeActive=false;
        expect(currentClock.values.size()>=9,"all nine original presentation call sites read separately");
        compare("nine-consumers-"+std::to_string(now));
        expect(hash(a)==beforeA&&hash(b)==beforeB,"presentation time changes neither tick/RNG nor complete authority");++authorityChecks;
    }
    a.runtimeMenuOpen=b.runtimeMenuOpen=false;a.runtimeSuppressPlayerAudioFeedback=b.runtimeSuppressPlayerAudioFeedback=false;
    a.remoteHumanHouseScopeActive=b.remoteHumanHouseScopeActive=false;
    const auto gat=std::find_if(a.world.getObjects().begin(),a.world.getObjects().end(),[](const auto& x){return x.type=="YTNK";});
    const auto other=std::find_if(a.world.getObjects().begin(),a.world.getObjects().end(),[](const auto& x){return x.type=="HTNK";});
    if(!expect(gat!=a.world.getObjects().end()&&other!=a.world.getObjects().end(),"real Gattling and target present")) return false;
    const int gatId=gat->instanceId,targetId=other->instanceId;
    for(int mode=0;mode<3;++mode) {
        for(auto* s:{&a,&b}) {s->selectedObjectIds={gatId};issueRuntimeAttackCommand(*s,*findObjectById(*s,targetId));}
        const auto profile=runtimeWeaponProfileForObject(a,*findObjectById(a,gatId),findObjectById(a,targetId),false);
        if(!expect(profile.weapon!=nullptr,"real YTNK weapon profile")) return false;
        WeaponType weapon=*profile.weapon;
        if(mode==1) weapon.report="FloatingDiscMoveLoop"; // Explicit stock-loop input probes the guard independently of weapon data.
        if(mode==2) weapon.report="W007-intentionally-missing";
        const auto* spec=a.worldAudioCatalog.find(weapon.report);
        const bool accepted=spec&&spec->loop&&!spec->samples.empty();
        currentClock.reset(1234);originalClock.reset(1234);
        const bool ca=manageRuntimeGattlingWeaponReport(a,*findObjectById(a,gatId),weapon);
        const bool cb=original.manageRuntimeGattlingWeaponReport(b,*findObjectById(b,gatId),weapon);
        expect(ca==cb&&ca==accepted,"original Gattling report guard and result");
        expect(currentClock.values.size()==(accepted?1U:0U),"Gattling report reads only after original capability/spec guards");
        if(mode==1) expect(accepted,"stock FloatingDisc loop guard is actually exercised");
        compare("gattling-report-"+std::to_string(mode));
    }
    std::string errorA,errorB;std::ostringstream saveA,saveB;
    expect(writeRuntimeStateToStream(a,saveA,errorA)&&writeRuntimeStateToStream(b,saveB,errorB),"complete production save codec available: "+errorA+errorB);
    expect(saveA.str()==saveB.str(),"all saved fields exactly equal to original");
    {std::ofstream f(directory/"current-save.ini",std::ios::binary);f<<saveA.str();}
    {std::ofstream f(directory/"original-save.ini",std::ios::binary);f<<saveB.str();}
    {std::ofstream f(directory/"current-audio.txt",std::ios::binary);f<<observe(a);}
    {std::ofstream f(directory/"original-audio.txt",std::ios::binary);f<<observe(b);}
    // Reuse the existing ordinary H039 transaction, not a second load path.
    const auto loadPath=pathString(directory/"audio-clock-load.ini");
    std::string loadError;
    if(!expect(saveRuntimeState(a,loadPath,loadError),"actual isolated audio-clock save: "+loadError)) return false;
    auto seeded=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*seeded,a,false);
    expect(seeded->worldAudioClock==&currentClock&&seeded->uiClock==&uiClock,"scratch retains separate audio and UI borrowed providers");
    IniFile invalid;expect(invalid.loadFromString("[RuntimeSave]\nVersion=999\n"),"invalid detached stage fixture parsed");
    const auto beforeFailedLoad=hash(a);
    expect(!materializeRuntimeLoadStateFromIni(*seeded,invalid,loadError)&&!loadError.empty()
        &&a.worldAudioClock==&currentClock&&hash(a)==beforeFailedLoad,"invalid detached load preserves audio provider and authority");
    const auto previousFault=runtimeLoadFaultPointForSmoke;
    struct RestoreFault {RuntimeLoadFaultPointForSmoke previous;~RestoreFault(){runtimeLoadFaultPointForSmoke=previous;}} restoreFault{previousFault};
    runtimeLoadFaultPointForSmoke=RuntimeLoadFaultPointForSmoke::BeforeCommit;
    const bool acceptedFault=loadRuntimeState(a,loadPath,loadError);runtimeLoadFaultPointForSmoke=previousFault;
    expect(!acceptedFault&&loadError.find("load materialization failed:")==0&&a.worldAudioClock==&currentClock
        &&a.uiClock==&uiClock&&hash(a)==beforeFailedLoad,"actual before-commit rejection retains both ports and complete authority");
    if(!expect(loadRuntimeState(a,loadPath,loadError)&&a.worldAudioClock==&currentClock&&a.uiClock==&uiClock,
        "successful ordinary load retains separate live providers: "+loadError)) return false;
    auto staged=std::make_unique<RuntimeWindowState>();seedRuntimeLoadScratch(*staged,a,false);
    if(!expect(materializeRuntimeLoadState(*staged,loadPath,loadError),"ordinary detached materialization: "+loadError)) return false;
    prepareRuntimePlayerDefeatNoticeLoadBaseline(*staged);Clock alternate;staged->worldAudioClock=&alternate;
    commitRuntimeLoadedState(a,*staged);
    expect(a.worldAudioClock==&currentClock&&a.uiClock==&uiClock&&staged->worldAudioClock==&alternate,
        "noexcept commit never swaps host audio/UI clock dependencies");
    staged->worldAudioClock=&currentClock;
    expect(trace.good(),"complete comparison trace retained");
    output<<"World audio clock production contract: checks="<<checks<<" comparisons="<<comparisons
        <<" authorityChecks="<<authorityChecks<<" failures="<<failures
        <<" skipped=0 originalBodies=2 originalPresentationCalls=9 globalFakeClock=0 devicePlayback=not-run\n";
    return success;
}