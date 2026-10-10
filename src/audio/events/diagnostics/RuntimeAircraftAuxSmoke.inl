bool runRuntimeAircraftAuxSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    try {
        const auto require = [](bool condition, const std::string& message) {
            if (!condition) throw std::runtime_error(message);
        };
        const char* output = std::getenv("CONCORD_AIRCRAFT_AUX_DIR");
        const std::string root = output && *output ? output : "build/smoke/feature-checks/18-aircraft-aux/checks";
        std::filesystem::create_directories(root);
        auto base = std::make_unique<RuntimeWindowState>(); base->config = config;
        base->config.defaultMapPath = "local_data/resources/mix_exports/multimd/2peaks.map";
        base->config.computerAiEnabled = base->config.noCooldown = base->config.multiplayerActive = false;
        base->config.playerSlots.clear(); base->config.gameModeRulesOverride.clear();
        base->report = buildRuntimeStartupReport(base->config, &base->world, &base->theater, &base->art, &base->rules, &base->mapFacts);
        require(base->report.success, "original resources");
        IniFile original; require(original.loadFromFile("local_data/rulesmd.ini"), "original rules");
        int rows = 0, configured = 0, airportSorties = 0, carrierSorties = 0, silent = 0, boundaries = 0, saves = 0, waves = 0, plays = 0;
        const auto audit = [&](const auto& types) {
            for (const auto& type : types) {
                ++rows;
                require(type.auxSound1 == trimAsciiLocal(original.getValue(type.id, "AuxSound1", ""))
                    && type.auxSound2 == trimAsciiLocal(original.getValue(type.id, "AuxSound2", "")), "loaded AuxSound fields: " + type.id);
                if (!type.auxSound1.empty() || !type.auxSound2.empty()) ++configured;
            }
        };
        audit(base->rules.getInfantryTypes()); audit(base->rules.getVehicleTypes()); audit(base->rules.getAircraftTypes());
        require(configured == 8, "stock configured object count");
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
        std::ofstream eventLog(root + "/aux-events.csv");
        eventLog << "hz,type,event,sample,frequency,milliseconds,dueMilliseconds,sourcePath,playbackPath\n";
        std::ofstream flightLog(root + "/aux-flights.csv");
        flightLog << "hz,type,sortie,takeoffRequests,landingRequests,played,returned,rearmed\n";
        const auto capture = [&](RuntimeWindowState& s, const std::string& type) {
            for (const auto& action : s.runtimeWorldAudioBackendActionHistory) {
                if (action.kind != RuntimeWorldAudioActionKind::Play || (action.handleId >> 60) != 1) continue;
                ++plays;
                const auto found = std::find_if(s.aircraftAuxAudio.pending.begin(), s.aircraftAuxAudio.pending.end(), [&](const auto& p) {return p.action.handleId == action.handleId;});
                require(found != s.aircraftAuxAudio.pending.end() && found->playing, "play without active entry");
                const auto path = findRuntimeSoundSamplePath(s, action.sampleName);
                require(path.has_value(), "missing playback sample " + action.sampleName);
                const auto playback = runtimeAudioPitchVariantPath(*path, std::filesystem::path(s.config.localDataRootPath) / "runtime_audio_cache", action.frequencyPermille);
                const int ms = runtimeWavDurationTicks(playback, 1000);
                require(found->endMilliseconds - s.aircraftAuxAudio.lastMilliseconds == ms && ms > 1, "playback duration differs from file");
                require(action.backendLoopIterations == 0, "aircraft event unexpectedly loops");
                eventLog << s.config.fixedTickHz << ',' << type << ',' << action.eventId << ',' << action.sampleName << ',' << action.frequencyPermille << ',' << ms << ','
                    << found->dueMilliseconds << ',' << std::quoted(*path) << ',' << std::quoted(playback) << '\n';
            }
        };
        const auto flush = [&](RuntimeWindowState& s, std::uint64_t& now, const std::string& type) {
            s.runtimeWorldAudioBackendActionHistory.clear(); advanceRuntimeAircraftAuxAudio(s, now); capture(s, type);
            int steps = 0;
            while (!s.aircraftAuxAudio.pending.empty()) {
                require(++steps < 600, "one-shot did not terminate");
                std::uint64_t next = (std::numeric_limits<std::uint64_t>::max)();
                for (const auto& p : s.aircraftAuxAudio.pending) next = std::min(next, p.playing ? p.endMilliseconds : p.dueMilliseconds);
                now = std::max(now + 1, next);
                s.runtimeWorldAudioBackendActionHistory.clear(); advanceRuntimeAircraftAuxAudio(s, now); capture(s, type);
            }
            for (int i = 0; i < 10; ++i) {
                s.runtimeWorldAudioBackendActionHistory.clear(); advanceRuntimeAircraftAuxAudio(s, ++now);
                require(s.runtimeWorldAudioBackendActionHistory.empty(), "finished one-shot restarted");
            }
        };
        const auto requests = [](const RuntimeWindowState& s, bool takeoff) {
            int count = 0;
            for (const auto& object : s.world.getObjects()) for (const auto& cue : object.pendingAircraftAuxSounds) if (cue.takingOff == takeoff) ++count;
            return count;
        };
        for (int hz : {15, 30, 60}) for (const std::string type : {"ORCA", "BEAG"}) {
            auto box = make(hz); auto& s = *box; std::uint64_t now = 0;
            const auto center = runtimeWorldAudioContext(s).listenerCell;
            require(addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "GAPOWR", {center.x - 4, center.y}) != nullptr, "airfield power supply");
            auto* host = addRuntimeObjectInstance(s, RuntimeBuildKind::Building, "GAAIRC", {center.x, center.y});
            require(host != nullptr, "airfield placement"); const int hostId = host->instanceId;
            RuntimeBuildCatalogItem item; item.kind = RuntimeBuildKind::Unit; item.typeId = type; item.requiredFactory = "AircraftType";
            require(completeRuntimeProductionOrder(s, item), type + " actual production");
            int planeId = -1;
            for (const auto& object : s.world.getObjects()) if (object.type == type) planeId = object.instanceId;
            require(planeId >= 0 && requests(s, true) == 0 && requests(s, false) == 0, "new parked aircraft emitted landing sound");
            refreshRuntimeAircraftDockAssignments(s); flush(s, now, type);
            for (int sortie = 0; sortie < (type == "ORCA" ? 3 : 1); ++sortie) {
                applySelection(s, {planeId}, false, false); issueRuntimeMoveCommand(s, {center.x + 6, center.y});
                auto* plane = findMutableObjectById(s, planeId);
                require(s.lastCommand.acceptedCount == 1 && plane && plane->airborne, type + " actual takeoff command");
                const int expected = type == "ORCA" ? 1 : 0;
                require(requests(s, true) == expected && requests(s, false) == 0, type + " takeoff request count");
                // An airborne destination edit does not constitute another takeoff.
                issueRuntimeMoveCommand(s, {center.x + 7, center.y});
                require(requests(s, true) == expected, type + " destination edit repeated takeoff");
                plane = findMutableObjectById(s, planeId); plane->currentAmmo = 0;
                if (type == "ORCA" && sortie == 0) {
                    // Save in the real airborne phase with an unconsumed takeoff
                    // notification, then finish the restored return mission.
                    const auto path = root + "/aux-airborne-" + std::to_string(hz) + ".ini";
                    require(saveRuntimeState(s, path, error), "airborne save " + error);
                    auto restored = make(hz); require(loadRuntimeState(*restored, path, error), "airborne restore " + error);
                    require(findObjectById(*restored, planeId)->airborne && requests(*restored, true) == 0, "airborne restore replayed takeoff");
                    const int beforeRestore = plays; flush(*restored, now, "ORCA-restored");
                    require(plays == beforeRestore, "old airborne phase produced new backend audio");
                    bool docked = false;
                    for (int step = 0; step < hz * 30 && !docked; ++step) {
                        ++restored->tick; advanceRuntimeSimulation(*restored);
                        const auto* restoredPlane = findObjectById(*restored, planeId);
                        docked = restoredPlane && !restoredPlane->airborne && restoredPlane->mission == "Dock";
                    }
                    require(docked && requests(*restored, false) == 1 && requests(*restored, true) == 0, "restored flight did not produce exactly one future landing");
                    flush(*restored, now, "ORCA-restored"); require(plays == beforeRestore + 1, "restored landing playback");
                    ++saves;
                }
                const int liveBeforePlays = plays;
                flush(s, now, type);
                bool returned = false, moved = false;
                for (int step = 0; step < std::max(600, hz * 30) && !returned; ++step) {
                    ++s.tick; advanceRuntimeSimulation(s);
                    plane = findMutableObjectById(s, planeId); require(plane && plane->alive, type + " aircraft disappeared");
                    moved = moved || plane->x >= center.x + 5;
                    returned = !plane->airborne && plane->mission == "Dock" && plane->dockedToObjectId == hostId;
                }
                require(returned && moved, type + " physical round trip failed at " + std::to_string(hz));
                require(requests(s, true) == 0 && requests(s, false) == expected, type + " landing request count");
                const int landingCount = requests(s, false); flush(s, now, type);
                const int fullAmmo = findRuntimeUnitType(s.rules, *plane)->ammo;
                for (int step = 0; step < hz * 30 && findObjectById(s, planeId)->currentAmmo != fullAmmo; ++step) {
                    ++s.tick; advanceRuntimeSimulation(s);
                }
                require(findObjectById(s, planeId)->currentAmmo == fullAmmo, type + " actual empty magazine rearm");
                for (int step = 0; step < 12; ++step) {++s.tick; advanceRuntimeSimulation(s);}
                refreshRuntimeAircraftDockAssignments(s);
                require(requests(s, true) == 0 && requests(s, false) == 0, "idle pad/reconciliation repeated landing");
                require(plays - liveBeforePlays == expected * 2, type + " missing/extra backend playback");
                flightLog << hz << ',' << type << ',' << sortie << ',' << expected << ',' << landingCount << ',' << plays - liveBeforePlays << ",yes,yes\n";
                ++airportSorties; if (!expected) ++silent;
            }
            if (type == "ORCA") {
                auto* plane = findMutableObjectById(s, planeId);
                const auto beforeHash = runtimeStandaloneSimulationStateHash(s, error); require(beforeHash != 0 && error.empty(), "pre-save hash " + error);
                queueRuntimeAircraftAuxSound(s, *plane, true);
                require(runtimeStandaloneSimulationStateHash(s, error) == beforeHash && error.empty(), "unplayed sound changed authoritative hash");
                const auto path = root + "/aux-save-" + std::to_string(hz) + ".ini";
                require(saveRuntimeState(s, path, error), "save with pending cue " + error);
                auto restored = make(hz);
                require(loadRuntimeState(*restored, path, error), "restore " + error);
                require(requests(*restored, true) == 0 && restored->aircraftAuxAudio.pending.empty(), "pending/device audio persisted");
                flush(*restored, now, type);
                applySelection(*restored, {planeId}, false, false); issueRuntimeMoveCommand(*restored, {center.x + 6, center.y});
                require(restored->lastCommand.acceptedCount == 1 && requests(*restored, true) == 1, "restored aircraft lost future takeoff sound");
                ++saves;
            }
        }
        for (int hz : {15, 30, 60}) for (const std::string parentType : {"DEST", "CARRIER"}) {
            auto box = make(hz); auto& s = *box; std::uint64_t now = 0;
            const auto center = runtimeWorldAudioContext(s).listenerCell;
            auto* parent = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, parentType, {center.x, center.y});
            require(parent != nullptr, "carrier placement"); const int parentId = parent->instanceId;
            auto* target = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, parentType == "DEST" ? "SUB" : "MTNK", {center.x + 4, center.y});
            require(target != nullptr, "carrier target placement"); const int targetId = target->instanceId;
            target->owner = "Russians"; target->cloaked = false;
            ensureRuntimeSpecialMechanismsInitialized(s); advanceRuntimeSpecialMechanisms(s);
            std::vector<int> children;
            for (const auto& [id, child] : s.specialMechanisms.carrierChildren()) if (child.parentObjectId == parentId) children.push_back(id);
            const int expected = parentType == "DEST" ? 1 : 3;
            require(children.size() == expected && requests(s, false) == 0 && requests(s, true) == 0, parentType + " original capacity / initial dock");
            RuntimeObjectBehavior attack; attack.kind = RuntimeBehaviorKind::Attack; attack.state = RuntimeBehaviorState::Queued;
            attack.targetObjectId = targetId; attack.forceFire = true; s.behaviors[parentId] = attack;
            ++s.tick; advanceRuntimeSpecialMechanisms(s);
            require(requests(s, true) == expected, parentType + " actual launch count=" + std::to_string(requests(s, true)));
            const std::string childType = findObjectById(s, children.front())->type;
            const int beforePlays = plays; flush(s, now, childType);
            s.behaviors.erase(parentId);
            for (const int id : children) {
                auto* child = findMutableObjectById(s, id); require(child && child->airborne, "launched child still docked");
                child->x = center.x + 7; child->cell = runtimeCellKey(child->x, child->y); child->currentAmmo = 0; clearRuntimeRenderPosition(*child);
                require(s.specialMechanisms.notifyCarrierChildFired(id, s.tick, runtimeSpecialEventSink(s)), "return notification");
            }
            parent = findMutableObjectById(s, parentId);
            ++parent->x; ++parent->y; parent->cell = runtimeCellKey(parent->x, parent->y); clearRuntimeRenderPosition(*parent);
            bool returned = false;
            for (int step = 0; step < hz * 30 && !returned; ++step) {
                ++s.tick; advanceRuntimeSpecialMechanisms(s);
                returned = std::all_of(children.begin(), children.end(), [&](int id) {return s.specialMechanisms.isCarrierChildDocked(id);});
            }
            require(returned && requests(s, false) == expected && requests(s, true) == 0, parentType + " actual return/landing count");
            parent = findMutableObjectById(s, parentId);
            for (int id : children) {
                const auto* child = findObjectById(s, id); require(child != nullptr, "returned child missing");
                const auto* type = findRuntimeUnitType(s.rules, *child);
                require(type && !child->airborne && child->dockedToObjectId == parentId && child->currentAmmo == type->ammo, parentType + " rearm/dock");
                require(child->pendingAircraftAuxSounds.size() == 1 && child->pendingAircraftAuxSounds.front().x == parent->x
                    && child->pendingAircraftAuxSounds.front().y == parent->y, "landing sound did not follow moved parent");
            }
            flush(s, now, childType);
            for (int step = 0; step < 12; ++step) {++s.tick; advanceRuntimeSpecialMechanisms(s);}
            require(requests(s, false) == 0, "carrier dock sync repeated landing");
            flightLog << hz << ',' << childType << ",0," << expected << ',' << expected << ',' << plays - beforePlays << ",yes,yes\n";
            ++carrierSorties;
        }
        auto box = make(30); auto& s = *box; std::uint64_t now = 0;
        const auto center = runtimeWorldAudioContext(s).listenerCell;
        auto* aircraft = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, "ORCA", {center.x, center.y});
        require(aircraft != nullptr, "boundary aircraft"); const int id = aircraft->instanceId;
        aircraft->airborne = false;
        auto detached = *aircraft; departRuntimeAircraftDock(s, detached);
        require(detached.pendingAircraftAuxSounds.size() == 1 && requests(s, true) == 0 && s.aircraftAuxAudio.pending.empty(), "detached command preparation emitted live audio"); ++boundaries;
        *findMutableObjectById(s, id) = detached;
        require(requests(s, true) == 1, "committed world discarded notification"); flush(s, now, "ORCA"); ++boundaries;
        aircraft = findMutableObjectById(s, id);
        queueRuntimeAircraftAuxSound(s, *aircraft, false, 300); require(requests(s, false) == 0, "landing threshold is inclusive");
        queueRuntimeAircraftAuxSound(s, *aircraft, false, 299); require(requests(s, false) == 1, "below landing threshold suppressed"); flush(s, now, "ORCA"); ++boundaries;
        for (const std::string type : {"BEAG", "APACHE", "HIND", "ZEP", "V3ROCKET", "DMISL", "CMISL"}) {
            WorldObjectInstance object = *aircraft; object.type = type; object.pendingAircraftAuxSounds.clear();
            queueRuntimeAircraftAuxSound(s, object, true); queueRuntimeAircraftAuxSound(s, object, false);
            require(object.pendingAircraftAuxSounds.empty(), type + " invented / duplicate AuxSound"); ++silent;
        }
        // An accepted one-shot survives a later object state change or death.
        queueRuntimeAircraftAuxSound(s, *aircraft, true); aircraft->alive = false; aircraft->health = 0;
        const int beforeDeath = plays; flush(s, now, "ORCA"); require(plays == beforeDeath + 1, "accepted one-shot cancelled on death"); ++boundaries;
        queueRuntimeAircraftAuxSound(s, *aircraft, false); require(requests(s, false) == 0, "dead aircraft generated new landing sound"); ++boundaries;
        aircraft->alive = true; aircraft->health = 200; aircraft->owner = "Russians";
        queueRuntimeAircraftAuxSound(s, *aircraft, false); const int beforeEnemy = plays; flush(s, now, "ORCA");
        require(plays == beforeEnemy + 1, "audible enemy aircraft filtered by local command ownership"); ++boundaries;
        queueRuntimeAircraftAuxSound(s, *aircraft, true); s.runtimeSoundEffectsEnabled = false; advanceRuntimeAircraftAuxAudio(s, now);
        require(requests(s, true) == 0 && s.aircraftAuxAudio.pending.empty(), "mute retained pending audio");
        s.runtimeSoundEffectsEnabled = true; const int beforeMute = plays; flush(s, now, "ORCA"); require(plays == beforeMute, "unmute replayed discarded cue"); ++boundaries;
        queueRuntimeAircraftAuxSound(s, *aircraft, false); advanceRuntimeAircraftAuxAudio(s, ++now);
        require(!s.aircraftAuxAudio.pending.empty(), "immediate landing event missing");
        s.runtimeSoundVolume = 0; advanceRuntimeAircraftAuxAudio(s, now);
        require(s.aircraftAuxAudio.pending.empty() && s.runtimeWorldAudioBackendActionHistory.back().kind == RuntimeWorldAudioActionKind::Stop, "volume zero failed to stop playback"); ++boundaries;
        s.runtimeSoundVolume = 100;
        queueRuntimeAircraftAuxSound(s, *aircraft, true); stopRuntimeWorldAudioBackend(s);
        require(requests(s, true) == 0 && s.aircraftAuxAudio.pending.empty(), "backend reset retained event"); ++boundaries;
        {
            RuntimeAircraftAuxAudioState local; const auto* spec = s.worldAudioCatalog.find("IntruderTakeOff"); require(spec && spec->simultaneousLimit == 2, "original takeoff Limit");
            WorldAircraftAuxSoundEvent cue{"IntruderTakeOff", 1, center.x, center.y, center.level, true};
            require(local.enqueue(*spec, cue, 1, 0, 300) && local.enqueue(*spec, cue, 2, 0, 300) && !local.enqueue(*spec, cue, 3, 0, 300), "Limit failed during predelay");
            for (const auto& p : local.pending) require(p.dueMilliseconds <= 500 && (p.dueMilliseconds == 0 || p.dueMilliseconds >= 33), "original predelay bounds"); ++boundaries;
            const auto duration = [](const auto&, int) {return 1000;}; local.advance(500, duration);
            require(!local.enqueue(*spec, cue, 3, 500, 300), "Limit released before sample completion");
            local.advance(1500, duration); require(local.enqueue(*spec, cue, 3, 1500, 300), "Limit did not release completed clip"); ++boundaries;
            local.advance(2000, duration); const auto reset = local.advance(1, duration);
            require(!reset.empty() && reset.front().kind == RuntimeWorldAudioActionKind::Stop && local.pending.empty(), "clock reset retained playback"); ++boundaries;
        }
        const auto priorRulesHash = s.rules.getDetailRulesHash(); IniFile extra;
        extra.loadFromString("[ORCA]\nAuxSound1=HornetTakeoff\nAuxSound2=HornetLanding\n"); original.appendFrom(extra); s.rules.loadFromIni(original);
        require(s.rules.getDetailRulesHash() != priorRulesHash && s.rules.findUnitType("AircraftType", "ORCA")->auxSound2 == "HornetLanding", "AuxSound load/fingerprint"); ++boundaries;
        std::ofstream audio(root + "/aux-audio.csv"); audio << "event,sample,milliseconds,bytes,path\n";
        std::set<std::string> samples;
        for (const std::string event : {"IntruderTakeOff", "IntruderLanding", "OspreyTakeOff", "OspreyLanding", "HornetTakeoff", "HornetLanding"}) {
            const auto* sound = s.worldAudioCatalog.find(event); require(sound && !sound->samples.empty(), event + " absent catalog");
            for (const auto& sample : sound->samples) if (samples.insert(sample).second) {
                const auto path = findRuntimeSoundSamplePath(s, sample);
                require(path && runtimeWavDurationTicks(*path, 1000) > 1 && std::filesystem::file_size(*path) > 44, "missing original audio " + sample);
                audio << event << ',' << sample << ',' << runtimeWavDurationTicks(*path, 1000) << ',' << std::filesystem::file_size(*path) << ',' << std::quoted(*path) << '\n'; ++waves;
            }
        }
        require(waves == 8, "original auxiliary sample coverage");
        std::cout << "AircraftAux PASS rows=" << rows << " configured=" << configured << " airportSorties=" << airportSorties << " carrierSorties=" << carrierSorties
            << " silent=" << silent << " saves=" << saves << " boundaries=" << boundaries << " waves=" << waves << " plays=" << plays << '\n';
        return true;
    } catch (const std::exception& e) {error = std::string("AircraftAux: ") + e.what(); return false;}
}
