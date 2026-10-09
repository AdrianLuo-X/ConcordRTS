bool runRuntimeSpatialAudioSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    try {
        const auto require = [](bool value, const std::string& why) { if (!value) throw std::runtime_error(why); };
        const char* output = std::getenv("CONCORD_SPATIAL_AUDIO_DIR");
        const std::string root = output && *output ? output : "build/smoke/feature-checks/20-spatial-audio/checks";
        std::filesystem::create_directories(root);
        auto base = std::make_unique<RuntimeWindowState>(); base->config = config;
        base->config.defaultMapPath = "local_data/resources/mix_exports/multimd/2peaks.map";
        base->config.computerAiEnabled = base->config.noCooldown = base->config.multiplayerActive = false;
        base->config.playerSlots.clear(); base->config.gameModeRulesOverride.clear();
        base->report = buildRuntimeStartupReport(base->config, &base->world, &base->theater, &base->art, &base->rules, &base->mapFacts);
        require(base->report.success, "original resources");
        const auto make = [&](int hz) {
            auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
            s.config = base->config; s.config.fixedTickHz = hz; s.config.crates = s.config.shortGame = false;
            s.report = base->report; s.world = base->world; s.rules = base->rules; s.art = base->art; s.theater = base->theater;
            s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
            s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.visionEnabled = false;
            s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
            s.runtimeSoundEffectsEnabled = true; s.runtimeSoundVolume = 100;
            s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = "Americans";
            s.economy.credits = 10000; s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
            initializeRuntimeLocalProductionLanes(s.economy); initializeRuntimeNavigation(s); loadRuntimeSoundDatabase(s);
            return box;
        };
        const auto camera = [&](RuntimeWindowState& s, RuntimeSpatialAudioPosition source, int screenX, int screenY, int w = 640, int h = 400) {
            s.cameraClampViewport = {13, 27, 13 + w, 27 + h}; s.camera.initialized = true;
            const auto p = runtimeProjectCellXY(source.x, source.y, source.level, s.camera);
            s.camera.worldOffsetX = p.x - screenX; s.camera.worldOffsetY = p.y - screenY;
        };
        int shots = 0, impacts = 0, deaths = 0, saves = 0, boundaries = 0;
        std::ofstream csv(root + "/spatial-events.csv");
        csv << "hz,case,event,spatial,trackCamera,x,y,level,screenX,screenY,width,height,range,local,global,shroud,minVolume,spatialGain,pan,gain,authored,master,sample,path,status\n";
        const auto log = [&](const RuntimeWindowState& s, const std::string& label, const RuntimeSoundPlaybackRecord& record) {
            const auto point = runtimeProjectCellXY(record.sourcePosition.x, record.sourcePosition.y, record.sourcePosition.level, s.camera);
            const auto rules = runtimeSpatialAudioRules(s.soundIni, record.section);
            csv << s.config.fixedTickHz << ',' << label << ',' << record.section << ',' << record.spatial << ',' << record.trackCamera
                << ',' << std::setprecision(17) << record.sourcePosition.x << ',' << record.sourcePosition.y << ',' << record.sourcePosition.level
                << ',' << point.x - s.camera.worldOffsetX << ',' << point.y - s.camera.worldOffsetY
                << ',' << s.cameraClampViewport.right-s.cameraClampViewport.left << ',' << s.cameraClampViewport.bottom-s.cameraClampViewport.top
                << ',' << rules.rangeCells << ',' << rules.local << ',' << rules.global << ',' << rules.shroud << ',' << rules.minimumVolumePercent
                << ',' << record.spatialGain16384 << ',' << record.pan16384 << ',' << record.gain16384 << ',' << record.authoredGainPermille
                << ',' << s.runtimeSoundVolume << ',' << record.sample << ',' << std::quoted(record.path) << ',' << record.status << '\n';
        };
        for (int hz : {15, 30, 60}) {
            auto box = make(hz); auto& s = *box; const auto center = runtimeWorldAudioContext(s).listenerCell;
            auto* shooter = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "MTNK", {center.x, center.y});
            require(shooter, "shooter placement"); const int shooterId = shooter->instanceId;
            auto* target = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "HTNK", {center.x + 3, center.y});
            require(target, "target placement"); const int targetId = target->instanceId; target->owner = "Russians";
            const auto position = runtimeSpatialAudioPositionForObject(*findObjectById(s, shooterId));
            for (const auto& [label, x] : std::vector<std::pair<std::string, int>>{{"center",320},{"left",0},{"right",640},{"outside",940},{"far",1500}}) {
                camera(s, position, x, 200); s.runtimeSoundPlaybackHistory.clear();
                const auto* a = findObjectById(s, shooterId); const auto* b = findObjectById(s, targetId);
                const auto profile = runtimeWeaponProfileForObject(s, *a, b); require(profile.weapon, "actual weapon profile");
                spawnRuntimeProjectile(s, *a, *b, *profile.weapon, profile, profile.weapon->damage);
                require(!s.projectiles.empty(), "real projectile creation");
                const auto& shot = s.projectiles.back();
                const auto found = std::find_if(s.runtimeSoundPlaybackHistory.rbegin(), s.runtimeSoundPlaybackHistory.rend(),
                    [](const auto& r) { return r.intent.rfind("weapon:",0)==0; });
                require(found != s.runtimeSoundPlaybackHistory.rend() && found->spatial && !found->trackCamera
                    && found->sourcePosition.x == shot.sourceX && found->sourcePosition.y == shot.sourceY && found->sourcePosition.level == shot.sourceZ,
                    "weapon Report lost exact muzzle position");
                if (label == "left") require(found->pan16384 < 8192, "left shot pan");
                if (label == "right") require(found->pan16384 > 8192, "right shot pan");
                if (label == "far") require(found->suppressed && found->spatialGain16384 == 0, "distant shot audible");
                else require(found->playable && found->spatialGain16384 > 0, "near shot missing sample or attenuation");
                log(s,label,*found); ++shots; s.projectiles.clear();
            }
            camera(s, position, 320, 200);
            // Conventional impact resolves the live weapon's authored damage.
            // Use a wounded real target instead of a fake projectile override.
            findMutableObjectById(s,targetId)->health = 1;
            const auto* a = findObjectById(s, shooterId); const auto* b = findObjectById(s, targetId);
            const auto profile = runtimeWeaponProfileForObject(s,*a,b);
            spawnRuntimeProjectile(s,*a,*b,*profile.weapon,profile,profile.weapon->damage);
            const auto shot = s.projectiles.back(); s.runtimeSoundPlaybackHistory.clear();
            require(applyRuntimeProjectileImpact(s,shot), "real lethal impact");
            for (const auto& record : s.runtimeSoundPlaybackHistory) {
                if (!record.spatial) continue;
                require(record.playable && record.spatialGain16384 > 0, "impact/death sound missing");
                if (record.intent == "sound:GenVehicleDie") {
                    require(!record.trackCamera, "ordinary death sound unexpectedly follows camera"); ++deaths;
                } else {
                    require(record.trackCamera, "animation Report lost listener updates"); ++impacts;
                }
                log(s,"lethal-hit",record);
            }
            (void)drainRuntimeObjectExitQueue(s);
            const auto* killed = findObjectById(s,targetId);
            require(!killed || !killed->alive, "target survived lethal shot"); s.projectiles.clear();
            const auto save = root + "/spatial-" + std::to_string(hz) + ".ini";
            require(saveRuntimeState(s,save,error), "save " + error);
            auto restored = make(hz); require(loadRuntimeState(*restored,save,error), "load " + error);
            require(!restored->runtimeVoiceAudio && restored->runtimeWorldAudioBackendBindings.empty()
                && std::none_of(restored->runtimeSfxMciChannelActive.begin(),restored->runtimeSfxMciChannelActive.end(),[](bool b){return b;}),
                "native voice survived save/load"); ++saves;
        }
        require(impacts >= 3 && deaths >= 3, "real impacts and deaths not exercised");
        auto box = make(30); auto& s = *box; const auto cell = runtimeWorldAudioContext(s).listenerCell;
        RuntimeSpatialAudioPosition position{double(cell.x),double(cell.y),double(cell.level)};
        IniFile extra;
        extra.loadFromString("[SoundList]\n15000=SpatialScreen\n15001=SpatialLocal\n15002=SpatialGlobal\n15003=SpatialShroud\n"
            "[SpatialScreen]\nSounds=bgralo1\nType=NORMAL SCREEN UNSHROUD\nVolume=100\nRange=10\nFShift=0\nVShift=0\n"
            "[SpatialLocal]\nSounds=bgralo1\nType=LOCAL\nVolume=100\nRange=10\nFShift=0\nVShift=0\n"
            "[SpatialGlobal]\nSounds=bgralo1\nType=GLOBAL\nVolume=100\nMinVolume=50\nRange=10\nFShift=0\nVShift=0\n"
            "[SpatialShroud]\nSounds=bgralo1\nType=SHROUD SCREEN\nVolume=100\nRange=10\nFShift=0\nVShift=0\n");
        s.soundIni.appendFrom(extra); require(s.worldAudioCatalog.loadFromIni(s.soundIni), "spatial fixture catalog");
        for (const auto& event : {"SpatialScreen","SpatialLocal","SpatialGlobal"}) {
            for (const auto& size : {std::pair{640,400},std::pair{1280,720},std::pair{801,601}}) {
                camera(s,position,size.first+300,size.second/2,size.first,size.second);
                emitRuntimeSound(s,"sound:"+std::string(event),position); const auto& r=s.runtimeSoundPlaybackHistory.back();
                require(r.spatial && r.pan16384 > 8192, "wide viewport positional routing"); log(s,"viewport",r); ++boundaries;
            }
        }
        camera(s,position,320,200);
        for (double dz : {-20.0, 20.0}) {
            auto elevated = position; elevated.level += dz;
            emitRuntimeSound(s,"sound:SpatialScreen",elevated);
            const auto& record = s.runtimeSoundPlaybackHistory.back();
            require(record.spatialGain16384 > 0 && record.spatialGain16384 < 16384,
                "height did not affect screen-space attenuation");
            log(s,"height",record); ++boundaries;
        }
        camera(s,position,320,200); s.visionEnabled=true; s.vision.reset();
        s.vision.update(s.world,&s.rules,{{"Americans",{}}},s.tick);
        emitRuntimeSound(s,"sound:SpatialShroud",position);
        require(s.runtimeSoundPlaybackHistory.back().suppressed, "SHROUD unknown cell"); ++boundaries;
        emitRuntimeSound(s,"sound:SpatialScreen",position);
        require(!s.runtimeSoundPlaybackHistory.back().suppressed, "UNSHROUD unknown cell"); ++boundaries;
        s.visionEnabled=false; s.remoteHumanHouseScopeActive=true;
        const auto count=s.runtimeSoundPlaybackHistory.size(); emitRuntimeSound(s,"sound:SpatialScreen");
        require(s.runtimeSoundPlaybackHistory.size()==count,"remote command leaked");
        emitRuntimeSound(s,"sound:SpatialScreen",position);
        require(s.runtimeSoundPlaybackHistory.back().spatial && !s.runtimeSoundPlaybackHistory.back().suppressed,"remote physical sound suppressed");
        s.remoteHumanHouseScopeActive=false; ++boundaries;
        s.runtimeVoiceAudio=std::make_unique<RuntimeVoiceAudio>(true); require(s.runtimeVoiceAudio->ready(),"muted host graph");
        s.runtimeSoundsEnabled=true;
        camera(s,position,0,200); emitRuntimeSound(s,"sound:SpatialScreen",position);
        const auto lane=(s.runtimeNextSfxChannel+runtimeSfxChannelCount-1)%runtimeSfxChannelCount;
        const auto handle=runtimeSfxVoiceHandle(lane); const auto before=s.runtimeVoiceAudio->probe(handle);
        require(before.available && before.masterVolume==0 && before.matrix.size()==2 && before.matrix[0]>before.matrix[1],"real left source mix");
        require(before.sampleRate==22050 && before.bitsPerSample==16 && before.frameBytes==2,
            "original mono source format changed");
        require(before.masteringLimiterEnabled,"stereo peak limiter absent or disabled"); ++boundaries;
        camera(s,position,640,200); advanceRuntimeSpatialAudioMixes(s);
        require(s.runtimeVoiceAudio->probe(handle).matrix==before.matrix,"ordinary one-shot followed camera"); ++boundaries;
        emitRuntimeSound(s,"sound:SpatialScreen",position,true);
        const auto trackedLane=(s.runtimeNextSfxChannel+runtimeSfxChannelCount-1)%runtimeSfxChannelCount;
        camera(s,position,0,200); advanceRuntimeSpatialAudioMixes(s);
        const auto tracked=s.runtimeVoiceAudio->probe(runtimeSfxVoiceHandle(trackedLane));
        require(tracked.matrix.size()==2 && tracked.matrix[0]>tracked.matrix[1],"managed fixed-coordinate sound lost camera updates"); ++boundaries;
        s.runtimeSoundVolume=25; advanceRuntimeSpatialAudioMixes(s);
        require(s.runtimeVoiceAudio->probe(handle).volume < before.volume,"master setting did not reach live SFX");
        s.runtimeSoundEffectsEnabled=false; advanceRuntimeSpatialAudioMixes(s);
        require(s.runtimeVoiceAudio->probe(handle).volume==0,"effects mute did not reach voice"); ++boundaries;
        s.runtimeSoundEffectsEnabled=true;s.runtimeSoundVolume=100;
        camera(s,position,1500,200); advanceRuntimeSpatialAudioMixes(s);
        require(!s.runtimeSfxMciChannelActive[trackedLane]
            && !s.runtimeVoiceAudio->isPlaying(runtimeSfxVoiceHandle(trackedLane)).value_or(true),
            "managed one-shot was not stopped outside hearing range");
        camera(s,position,320,200); advanceRuntimeSpatialAudioMixes(s);
        require(!s.runtimeVoiceAudio->isPlaying(runtimeSfxVoiceHandle(trackedLane)).value_or(true),
            "managed one-shot restarted on camera return"); ++boundaries;
        RuntimeWorldAudioAction action; action.handleId=0x7000000000000001ULL;
        action.eventId="SpatialScreen";action.sampleName="bgralo1";action.cell={cell.x,cell.y,cell.level};
        action.authoredGainPermille=1000;action.gainPermille=1000;action.backendLoopIterations=-1;
        camera(s,position,320,200);applyRuntimeWorldAudioBackendAction(s,action);
        require(s.runtimeWorldAudioBackendBindings.size()==1,"world loop backend");
        camera(s,position,640,200);advanceRuntimeSpatialAudioMixes(s);
        auto loop=s.runtimeVoiceAudio->probe(action.handleId);
        require(loop.matrix.size()==2 && loop.matrix[0]<loop.matrix[1],"world loop pan update with unchanged gain");
        camera(s,position,1500,200);advanceRuntimeSpatialAudioMixes(s);
        require(!s.runtimeVoiceAudio->isPlaying(action.handleId).value_or(true),"distant loop did not stop");
        camera(s,position,320,200);advanceRuntimeSpatialAudioMixes(s);
        require(s.runtimeVoiceAudio->isPlaying(action.handleId).value_or(false),"loop reentry failed"); ++boundaries;
        stopRuntimeSfxPlayback(s);stopRuntimeWorldAudioBackend(s);
        require(s.runtimeVoiceAudio->voiceCount()==0,"shutdown leaked voices"); ++boundaries;
        s.runtimeSoundsEnabled=false;
        const auto hash=runtimeStandaloneSimulationStateHash(s,error); camera(s,position,10,10,1024,768);
        emitRuntimeSound(s,"sound:SpatialScreen",position);
        require(hash && runtimeStandaloneSimulationStateHash(s,error)==hash,"local spatial sound/camera changed gameplay hash"); ++boundaries;
        csv.close();
        std::cout << "PASS spatial-audio shots="<<shots<<" impacts="<<impacts<<" deaths="<<deaths<<" saves="<<saves
            <<" boundaries="<<boundaries<<" nativeOutput=muted\n";
        return true;
    } catch(const std::exception& ex) {error=std::string("spatial audio: ")+ex.what();return false;}
}
