bool runRuntimeTurretSoundSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    try {
        const auto require = [](bool value, const std::string& why) {if (!value) throw std::runtime_error(why);};
        const char* output = std::getenv("CONCORD_TURRET_SOUND_DIR");
        const std::string root = output && *output ? output : "build/smoke/feature-checks/19-turret-sound/checks";
        std::filesystem::create_directories(root);
        auto base = std::make_unique<RuntimeWindowState>(); base->config = config;
        base->config.defaultMapPath = "local_data/resources/mix_exports/multimd/2peaks.map";
        base->config.computerAiEnabled = base->config.noCooldown = base->config.multiplayerActive = false;
        base->config.playerSlots.clear(); base->config.gameModeRulesOverride.clear();
        base->report = buildRuntimeStartupReport(base->config, &base->world, &base->theater, &base->art, &base->rules, &base->mapFacts);
        require(base->report.success, "original resources");
        IniFile original; require(original.loadFromFile("local_data/rulesmd.ini"), "original rules");
        int rows = 0, configured = 0, scenes = 0, silent = 0, saves = 0, exits = 0, boundaries = 0, waves = 0, plays = 0;
        const auto audit = [&](const auto& types) {
            for (const auto& type : types) {
                ++rows;
                require(type.turretRotateSound == trimAsciiLocal(original.getValue(type.id, "TurretRotateSound", "")), "loaded field " + type.id);
                if (!type.turretRotateSound.empty()) ++configured;
            }
        };
        audit(base->rules.getInfantryTypes()); audit(base->rules.getVehicleTypes()); audit(base->rules.getAircraftTypes()); audit(base->rules.getBuildingTypes());
        require(configured == 2, "original two configured types");
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
        const auto add = [&](RuntimeWindowState& s, const std::string& type) {
            const auto center = runtimeWorldAudioContext(s).listenerCell;
            require(addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "GAPOWR", {center.x - 5, center.y}) != nullptr, "power plant");
            const auto* object = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, type, {center.x, center.y});
            require(object != nullptr, type + " actual placement");
            const int id = object->instanceId; rebuildRuntimeEconomyPower(s);
            auto* turret = findMutableObjectById(s, id); turret->facing = turret->turretFacing = 0;
            require(runtimeObjectHasOperationalPower(s, *turret), type + " initial power");
            return id;
        };
        std::ofstream events(root + "/turret-events.csv");
        events << "hz,type,event,sample,phase,handle,ordinal,frequency,milliseconds,now,sourcePath,playbackPath\n";
        const auto audio = [&](RuntimeWindowState& s, std::uint64_t now, const std::string& type) {
            s.runtimeWorldAudioBackendActionHistory.clear(); advanceRuntimeTurretSoundAudio(s, now);
            for (const auto& action : s.runtimeWorldAudioBackendActionHistory) {
                if ((action.handleId >> 60) != 3 || (action.kind != RuntimeWorldAudioActionKind::Play && action.kind != RuntimeWorldAudioActionKind::Replay)) continue;
                const auto path = findRuntimeSoundSamplePath(s, action.sampleName); require(path.has_value(), "sample " + action.sampleName);
                const auto playback = runtimeAudioPitchVariantPath(*path, std::filesystem::path(s.config.localDataRootPath) / "runtime_audio_cache", action.frequencyPermille);
                const int ms = runtimeWavDurationTicks(playback, 1000); require(ms > 1 && action.backendLoopIterations == 0, "clip duration or infinite backend loop");
                const auto checkVoice = [&](const RuntimeTurretSoundVoice& voice) {
                    if (voice.handle == action.handleId && voice.ordinal == action.playbackOrdinal + 1)
                        require(voice.endMilliseconds == now + ms, "clip schedule differs from WAV");
                };
                for (const auto& [id, voice] : s.turretSoundAudio.active) checkVoice(voice);
                for (const auto& voice : s.turretSoundAudio.endings) checkVoice(voice);
                events << s.config.fixedTickHz << ',' << type << ',' << action.eventId << ',' << action.sampleName << ',' << action.reason.substr(16)
                    << ',' << action.handleId << ',' << action.playbackOrdinal << ',' << action.frequencyPermille << ',' << ms << ',' << now
                    << ',' << std::quoted(*path) << ',' << std::quoted(playback) << '\n'; ++plays;
            }
        };
        const auto finish = [&](RuntimeWindowState& s, std::uint64_t& now, const std::string& type) {
            ++s.tick; ++now; finishRuntimeTurretSoundActivities(s, s.world); audio(s, now, type);
            require(s.turretSoundAudio.active.empty(), "stationary turret kept looping");
            for (int i = 0; i < 12 && !s.turretSoundAudio.endings.empty(); ++i) {
                std::uint64_t next = now + 1; for (const auto& voice : s.turretSoundAudio.endings) next = std::max(next, voice.endMilliseconds);
                now = next; audio(s, now, type);
            }
            require(s.turretSoundAudio.endings.empty(), "decay failed to terminate");
            const int before = plays; audio(s, ++now, type); require(before == plays, "completed turret event restarted");
        };
        std::ofstream sceneLog(root + "/turret-scenes.csv"); sceneLog << "hz,type,tracking,bodySamples,idleHeld,overlap,range,save,exit\n";
        for (int hz : {15, 30, 60}) for (const std::string type : {"GTGCAN", "CAEAST02"}) {
            auto box = make(hz); auto& s = *box; const int id = add(s, type); std::uint64_t now = 0;
            audio(s, now, type); require(s.turretSoundAudio.active.empty(), "placement played rotation sound"); ++silent;
            const std::string event = runtimeTurretSoundForObject(s, *findObjectById(s, id));
            const auto& spec = s.turretSoundAudio.specs.at(event);
            require(spec.attack && spec.decay && spec.all && spec.literalLoop && spec.sound.simultaneousLimit == 5, "stock control flags");
            std::vector<std::string> bodySamples;
            WorldObjectInstance target; target.type = "MTNK"; target.kind = WorldObjectKind::Unit; target.alive = true; target.health = 400;
            for (int step = 0; step < hz * 20; ++step) {
                ++s.tick; now = static_cast<std::uint64_t>(s.tick) * 1000 / hz;
                auto* object = findMutableObjectById(s, id);
                // Keep a moving target at least a quarter-turn away. A
                // fixed eight-tick toggle can legitimately finish early.
                constexpr std::array<RuntimeCellPoint,4> offsets{{{-7,-7},{7,-7},{7,7},{-7,7}}};
                const auto offset=offsets[((object->turretFacing+160)/64)%4];
                target.x=object->x+offset.x; target.y=object->y+offset.y;
                target.cell = runtimeCellKey(target.x, target.y);
                updateRuntimeWeaponFacingToward(s, *object, target); finishRuntimeTurretSoundActivities(s, s.world);
                require(object->turretSoundActivityTick == s.tick, "tracking failed to rotate: "+type+" Hz="+std::to_string(hz)+" step="+std::to_string(step));
                const auto hash = step == 0 ? runtimeStandaloneSimulationStateHash(s, error) : 0;
                audio(s, now, type);
                if (step == 0) require(hash != 0 && runtimeStandaloneSimulationStateHash(s, error) == hash && error.empty(), "local audio changed gameplay hash");
                require(s.turretSoundAudio.active.size() == 1, "tracking controller missing");
                for (const auto& action : s.runtimeWorldAudioBackendActionHistory)
                    if (action.reason == "turret rotation body" && action.kind == RuntimeWorldAudioActionKind::Replay) bodySamples.push_back(action.sampleName);
            }
            require(s.projectiles.empty(), "aim-only fixture fired");
            const int bodyCount = static_cast<int>(spec.sound.samples.size()) - 2;
            require(bodySamples.size() >= bodyCount * 2, "insufficient completed loop pools");
            for (std::size_t offset = 0; offset + bodyCount <= bodySamples.size(); offset += bodyCount) {
                std::set<std::string> pool(bodySamples.begin() + offset, bodySamples.begin() + offset + bodyCount);
                require(pool.size() == bodyCount && !pool.contains(spec.sound.samples.front()) && !pool.contains(spec.sound.samples.back()), "All body pool or intro/decay membership");
            }
            const auto old = s.turretSoundAudio.active.at(id); ++s.tick; ++now;
            finishRuntimeTurretSoundActivities(s, s.world); audio(s, now, type);
            require(s.turretSoundAudio.active.empty() && s.turretSoundAudio.endings.size() == 1
                && s.turretSoundAudio.endings.front().handle == old.handle && s.turretSoundAudio.endings.front().sample == spec.sound.samples.back(), "stop did not switch immediately to decay");
            ++s.tick; ++now; auto* object = findMutableObjectById(s, id);
            target.x = object->x - (target.x - object->x); target.y = object->y - 2; target.cell = runtimeCellKey(target.x, target.y);
            updateRuntimeWeaponFacingToward(s, *object, target); finishRuntimeTurretSoundActivities(s, s.world); audio(s, now, type);
            require(s.turretSoundAudio.active.at(id).handle != old.handle && s.turretSoundAudio.endings.size() == 1, "new turn killed old detached decay");
            const int sourceX = object->x; object->x += 200; audio(s, ++now, type);
            require(!s.turretSoundAudio.active.at(id).playing, "out-of-range loop kept playing");
            object->x = sourceX; audio(s, ++now, type);
            require(s.turretSoundAudio.active.at(id).playing && s.turretSoundAudio.active.at(id).phase == "attack", "range reentry did not rebuild controller");
            finish(s, now, type);
            object = findMutableObjectById(s, id); object->facing = 64; object->turretFacing = 0;
            ++s.tick; advanceRuntimeIdleWeaponFacings(s); finishRuntimeTurretSoundActivities(s, s.world); audio(s, ++now, type);
            require(s.turretSoundAudio.active.empty() && object->turretFacing == 0,
                "idle defensive turret returned or requested rotation sound");
            const auto savePath = root + "/turret-" + type + "-" + std::to_string(hz) + ".ini";
            require(saveRuntimeState(s, savePath, error), "save " + error); auto restored = make(hz);
            require(loadRuntimeState(*restored, savePath, error), "load " + error);
            const auto* savedObject = findObjectById(*restored, id);
            require(savedObject && savedObject->activeTurretSoundEvent.empty() && savedObject->pendingTurretSounds.empty()
                && restored->turretSoundAudio.active.empty() && restored->turretSoundAudio.endings.empty(), "local voice/activity persisted");
            const int beforeRestore = plays; audio(*restored, now, type); require(beforeRestore == plays, "load replayed old cue");
            ++restored->tick; advanceRuntimeIdleWeaponFacings(*restored); finishRuntimeTurretSoundActivities(*restored, restored->world); audio(*restored, ++now, type);
            require(restored->turretSoundAudio.active.empty() && findObjectById(*restored,id)->turretFacing == 0,
                "restored idle defense resumed a return turn"); ++saves;
            auto* restoredObject = findMutableObjectById(*restored,id);
            target.x=restoredObject->x+4; target.y=restoredObject->y+2;
            ++restored->tick; updateRuntimeWeaponFacingToward(*restored,*restoredObject,target);
            finishRuntimeTurretSoundActivities(*restored,restored->world); audio(*restored,++now,type);
            require(restored->turretSoundAudio.active.size()==1, "restored new aim lost future sound");
            bool aimed = false;
            for (int i = 0; i < runtimeRuleFramesToTicks(256, hz) + 1 && !aimed; ++i) {
                ++s.tick; aimed=updateRuntimeWeaponFacingToward(s,*object,target);
                finishRuntimeTurretSoundActivities(s, s.world); audio(s, ++now, type);
            }
            require(aimed && findObjectById(s,id)->turretFacing == runtimeFacingFromDelta(4,2), "aim turn did not finish");
            require(s.turretSoundAudio.active.empty(),"completed FacingClass timer kept rotating audio for an extra tick");
            finish(s, now, type);
            object = findMutableObjectById(s, id); target.x=object->x-4; target.y=object->y-2;
            ++s.tick; updateRuntimeWeaponFacingToward(s,*object,target);
            finishRuntimeTurretSoundActivities(s, s.world); audio(s, ++now, type);
            RuntimeObjectExitRequest request; request.objectId = id; request.tick = s.tick;
            request.kind = hz == 60 ? RuntimeObjectExitKind::Sold : RuntimeObjectExitKind::Destroyed;
            request.suppressCrew = request.suppressDeathWeapon = true;
            require(finalizeRuntimeObjectExit(s, request).accepted, "actual object exit"); audio(s, ++now, type);
            require(s.turretSoundAudio.active.empty() && !s.turretSoundAudio.endings.empty(), "object exit did not EndLooping"); finish(s, now, type); ++exits;
            sceneLog << hz << ',' << type << ",yes," << bodySamples.size() << ",yes,yes,yes,yes,yes\n"; ++scenes;
        }
        // Real player attack and the full simulation dispatch, without a direct sound request.
        for (int hz : {15, 30, 60}) {
            auto box = make(hz); auto& s = *box; const int id = add(s, "GTGCAN"); std::uint64_t now = 0;
            const auto center = runtimeWorldAudioContext(s).listenerCell;
            auto* target = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "MTNK", {center.x + 7, center.y + 2}); require(target, "attack target");
            const int targetId = target->instanceId; target->owner = "Russians";
            applySelection(s, {id}, false, false); issueRuntimeAttackCommand(s, *target, true);
            require(s.lastCommand.acceptedCount == 1, "player attack rejected");
            bool heard = false;
            for (int i = 0; i < hz * 3 && (!heard || findObjectById(s, id)->turretFacing == 0); ++i) {
                ++s.tick; advanceRuntimeSimulation(s); now += 1000 / hz; audio(s, now, "GTGCAN-command"); heard = !s.turretSoundAudio.active.empty();
            }
            require(heard && findObjectById(s, id)->turretFacing != 0, "full simulation attack did not rotate/play"); ++boundaries;
            // Keep the accepted attack, remove actual grid supply, and step the same dispatcher.
            for (auto& object : s.world.accessObjects()) if (object.type == "GAPOWR") object.alive = false;
            rebuildRuntimeEconomyPower(s); const int facing = findObjectById(s, id)->turretFacing;
            ++s.tick; advanceRuntimeSimulation(s); audio(s, ++now, "GTGCAN-power");
            require(!runtimeObjectHasOperationalPower(s, *findObjectById(s, id)) && findObjectById(s, id)->turretFacing == facing
                && s.turretSoundAudio.active.empty(), "grid failure kept rotating/looping"); ++boundaries;
            for (auto& object : s.world.accessObjects()) if (object.type == "GAPOWR") object.alive = true;
            rebuildRuntimeEconomyPower(s);
            ++s.tick; advanceRuntimeSimulation(s); audio(s, ++now, "GTGCAN-power");
            require(!s.turretSoundAudio.active.empty(), "power recovery lost sound"); ++boundaries;
            auto* chronoman = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "CLEG", {center.x + 3, center.y - 3}); require(chronoman, "temporal attacker");
            chronoman->owner = "Russians"; const int attackerId = chronoman->instanceId;
            ensureRuntimeSpecialMechanismsInitialized(s); bool completed = false; std::string reason;
            require(s.specialMechanisms.applyTemporalHit(s.world.accessObjects(), s.rules, attackerId, id, 1, 10000, s.tick,
                [](const auto& a, const auto& b) {return a == b;}, runtimeSpecialEventSink(s), completed, reason), "temporal hit " + reason);
            const int frozenFacing = findObjectById(s, id)->turretFacing;
            auto& behavior = s.behaviors.at(id); ++s.tick; advanceRuntimeAttackBehavior(s, *findMutableObjectById(s, id), behavior);
            finishRuntimeTurretSoundActivities(s, s.world); audio(s, ++now, "GTGCAN-temporal");
            require(s.specialMechanisms.isTemporalFrozen(id) && findObjectById(s, id)->turretFacing == frozenFacing && s.turretSoundAudio.active.empty(), "temporal stopped attack kept looping"); ++boundaries;
            (void)targetId;
        }
        auto box = make(30); auto& s = *box; const int id = add(s, "GTGCAN"); std::uint64_t now = 0;
        auto* object = findMutableObjectById(s, id); object->facing = 128;
        auto detached = *object;
        WorldObjectInstance detachedTarget; detachedTarget.x=object->x+4; detachedTarget.y=object->y+2;
        ++s.tick; updateRuntimeWeaponFacingToward(s, detached, detachedTarget);
        require(!detached.pendingTurretSounds.empty() && object->pendingTurretSounds.empty() && s.turretSoundAudio.active.empty(), "uncommitted object emitted live sound"); ++boundaries;
        *object = detached; audio(s, ++now, "GTGCAN-boundary"); require(s.turretSoundAudio.active.size() == 1, "committed turn lost event");
        const auto initial = s.turretSoundAudio.active.at(id);
        for (int i = 0; i < 20; ++i) audio(s, now, "GTGCAN-boundary");
        require(s.turretSoundAudio.active.at(id).ordinal == initial.ordinal, "presentation cadence restarted clip"); ++boundaries;
        const auto hash = runtimeStandaloneSimulationStateHash(s, error); const auto cues = object->pendingTurretSounds;
        object->pendingTurretSounds.push_back({"GrandCannonRotate", s.tick, object->x, object->y, object->level, false});
        require(hash != 0 && runtimeStandaloneSimulationStateHash(s, error) == hash, "pending transitions changed hash"); object->pendingTurretSounds = cues; ++boundaries;
        ++object->x; object->cell = runtimeCellKey(object->x, object->y); audio(s, ++now, "GTGCAN-boundary");
        require(s.turretSoundAudio.active.at(id).handle == initial.handle && s.turretSoundAudio.active.at(id).source.source.cell.x == object->x, "position update restarted sound"); ++boundaries;
        object->owner = "Russians"; object->cloaked = true; s.visionEnabled = true; s.vision.reset();
        s.vision.update(s.world, &s.rules, {{"Americans", {}}}, s.tick); audio(s, ++now, "GTGCAN-boundary");
        require(s.turretSoundAudio.active.at(id).playing, "original UNSHROUD/enemy sound suppressed"); ++boundaries;
        s.runtimeSoundEffectsEnabled = false; audio(s, ++now, "GTGCAN-boundary");
        require(s.turretSoundAudio.active.empty() && s.turretSoundAudio.endings.empty(), "mute left audio alive"); ++boundaries;
        s.runtimeSoundEffectsEnabled = true; audio(s, ++now, "GTGCAN-boundary"); require(s.turretSoundAudio.active.size() == 1, "unmute failed during ongoing turn");
        s.runtimeSoundVolume = 0; audio(s, ++now, "GTGCAN-boundary"); require(s.turretSoundAudio.active.empty(), "zero volume retained audio"); ++boundaries;
        s.runtimeSoundVolume = 100; audio(s, ++now, "GTGCAN-boundary");
        object->mission = "InsideTransport"; audio(s, ++now, "GTGCAN-boundary"); require(s.turretSoundAudio.active.empty() && !s.turretSoundAudio.endings.empty(), "limbo did not EndLooping"); ++boundaries;
        object->mission = "Guard"; audio(s, ++now, "GTGCAN-boundary"); stopRuntimeWorldAudioBackend(s);
        require(s.turretSoundAudio.active.empty() && s.turretSoundAudio.endings.empty() && s.runtimeWorldAudioBackendBindings.empty(), "backend shutdown retained controller"); ++boundaries;
        audio(s, ++now, "GTGCAN-boundary"); loadRuntimeSoundDatabase(s); require(s.turretSoundAudio.active.empty(), "catalog reset retained controller"); ++boundaries;
        const auto oldHash = s.rules.getDetailRulesHash(); IniFile extra;
        extra.loadFromString("[MTNK]\nTurretRotateSound=GrandCannonRotate\n[GTGCAN]\nTurretRotateSound=YuriBustRotate\n"); original.appendFrom(extra); s.rules.loadFromIni(original);
        require(s.rules.getDetailRulesHash() != oldHash && s.rules.findUnitType("UnitType", "MTNK")->turretRotateSound == "GrandCannonRotate", "unit/common field or fingerprint"); ++boundaries;
        {
            RuntimeTurretSoundAudioState local; RuntimeObjectAmbientSpec spec;
            spec.sound = *s.worldAudioCatalog.find("GrandCannonRotate"); spec.attack = spec.decay = spec.all = spec.literalLoop = true;
            local.specs[spec.sound.eventId] = spec; const auto duration = [](const auto&, int) {return 500;};
            std::vector<RuntimeTurretSoundSource> sources;
            for (int i = 0; i < 6; ++i) sources.push_back({i, {"object:" + std::to_string(i), "GTGCAN", spec.sound.eventId, {1, 2, 0}, 300}});
            local.advance(sources, {}, 0, 0, duration);
            const auto countPlaying = [&]() {int count = 0; for (const auto& [key, voice] : local.active) if (voice.playing) ++count; return count;};
            require(countPlaying() == 5, "Limit=5 admission"); sources.erase(sources.begin()); local.advance(sources, {}, 1, 1, duration);
            require(countPlaying() == 4 && local.endings.size() == 1, "detached decay stopped counting toward Limit");
            local.advance(sources, {}, 501, 2, duration); require(countPlaying() == 5 && local.endings.empty(), "finished decay did not release Limit"); ++boundaries;
            const auto actions = local.advance({}, {}, 1, 3, duration);
            require(local.active.empty() && local.endings.empty() && !actions.empty(), "clock rollback retained old audio"); ++boundaries;
            RuntimeTurretSoundSource one = sources.front();
            const auto shortTurn = local.advance({}, {{one, true}, {one, false}}, 2, 4, duration);
            require(shortTurn.size() == 3 && shortTurn[0].sampleName == spec.sound.samples.front() && shortTurn[2].sampleName == spec.sound.samples.back()
                && local.active.empty() && local.endings.size() == 1, "short committed turn lost attack/decay"); ++boundaries;
        }
        std::ofstream files(root + "/turret-audio.csv"); files << "event,sample,milliseconds,bytes,path\n";
        for (const std::string event : {"GrandCannonRotate", "YuriBustRotate"}) {
            const auto* spec = s.worldAudioCatalog.find(event); require(spec, "original event missing");
            for (const auto& sample : spec->samples) {
                const auto path = findRuntimeSoundSamplePath(s, sample); require(path && runtimeWavDurationTicks(*path, 1000) > 1, "original WAV missing");
                files << event << ',' << sample << ',' << runtimeWavDurationTicks(*path, 1000) << ',' << std::filesystem::file_size(*path) << ',' << std::quoted(*path) << '\n'; ++waves;
            }
        }
        require(waves == 9, "original nine sample coverage");
        std::cout << "TurretSound PASS rows=" << rows << " configured=" << configured << " scenes=" << scenes << " silent=" << silent
            << " saves=" << saves << " exits=" << exits << " boundaries=" << boundaries << " waves=" << waves << " plays=" << plays << '\n';
        return true;
    } catch (const std::exception& e) {error = std::string("TurretSound: ") + e.what(); return false;}
}
