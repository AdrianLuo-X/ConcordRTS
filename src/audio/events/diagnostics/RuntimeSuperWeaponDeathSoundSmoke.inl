    if (std::getenv("CONCORD_COMMAND_SMOKE_SUPERWEAPON_DEATH_SOUND_ONLY")) {
        std::cout << std::unitbuf;
        const auto prepareAudio = [&](RuntimeWindowState& s, bool listenerFarAway = false) {
            s.runtimeSoundsEnabled = false; // Resolve real samples without playing test audio.
            s.runtimeSoundEffectsEnabled = true;
            s.runtimeSoundVolume = 100;
            s.visionEnabled = false;
            loadRuntimeSoundDatabase(s);
            s.cameraClampViewport = {0, 0, 640, 400};
            s.camera.initialized = true;
            const auto center = runtimeProjectCellXY(90, 90, 0, s.camera);
            s.camera.worldOffsetX = center.x - (listenerFarAway ? 5000 : 320);
            s.camera.worldOffsetY = center.y - 200;
            s.runtimeSoundIntentHistory.clear();
            s.runtimeSoundPlaybackHistory.clear();
        };
        struct DeathCase {
            const char* power;
            const char* type;
            WorldObjectKind kind;
            const char* sound;
            bool radiation = false;
            bool moddedDominator = false;
        };
        const std::vector<DeathCase> cases{
            {"NukeSpecial", "E1", WorldObjectKind::Infantry, "sound:GIDie"},
            {"NukeSpecial", "MTNK", WorldObjectKind::Unit, "sound:GenVehicleDie"},
            {"NukeSpecial", "ORCA", WorldObjectKind::Unit, "sound:IntruderDie"},
            {"NukeSpecial", "JUMPJET", WorldObjectKind::Infantry, "sound:RocketeerDie"},
            {"LightningStormSpecial", "E1", WorldObjectKind::Infantry, "sound:GIDie"},
            {"LightningStormSpecial", "MTNK", WorldObjectKind::Unit, "sound:GenVehicleDie"},
            {"IronCurtainSpecial", "E1", WorldObjectKind::Infantry, "sound:GIDie"},
            {"IronCurtainSpecial", "DOG", WorldObjectKind::Infantry, "sound:DogDie"},
            {"IronCurtainSpecial", "DNOA", WorldObjectKind::Infantry, "sound:TrexDie"},
            {"ChronoSphereSpecial", "E1", WorldObjectKind::Infantry, "sound:GIDie"},
            {"ChronoSphereSpecial", "DOG", WorldObjectKind::Infantry, "sound:DogDie"},
            {"GeneticConverterSpecial", "DOG", WorldObjectKind::Infantry, "sound:DogDie"},
            {"NukeSpecial", "E1", WorldObjectKind::Infantry, "sound:GIDie", true},
            // Stock DominatorWH damages buildings only. A legal rules override
            // proves the same lifecycle works when a mod lets it kill infantry.
            {"PsychicDominatorSpecial", "E1", WorldObjectKind::Infantry, "sound:GIDie", false, true}
        };
        int deaths = 0;
        std::vector<std::string> failures;
        for (int hz : {30, 60}) for (const auto& test : cases) {
            const bool passed = [&]() -> bool {
            auto storage = fixture("Americans", hz); auto& s = *storage;
            if (test.moddedDominator) {
                IniFile custom = stockIni, overrideIni;
                overrideIni.loadFromString("[DominatorWH]\nVerses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n");
                custom.appendFrom(overrideIni);
                if (!s.rules.loadFromIni(custom)) return fail("Dominator test rules");
                s.superWeaponsInitialized = false;
                ensureRuntimeSuperWeaponsInitialized(s);
                auto context = runtimeSuperWeaponContext(s); s.superWeapons.tick(context);
            }
            prepareAudio(s);
            s.config.gameMode = "BATTLE"; s.skirmishModeRulesLoaded = true;
            s.skirmishModeAlliesAllowed = true;
            s.skirmishTeamByOwner = {{"commander", "1"}, {"ally", "1"}, {"enemy", "2"}};
            if (test.radiation) {
                if (!fire(s, test.power, {{90, 90}, -1})) return fail(error);
                for (int i = 0; i < hz * 30 && s.superWeapons.renderSnapshots().empty(); ++i) step(s);
                // Wait for the residual field, then introduce wounded victims.
                bool field = false;
                for (int i = 0; i < hz * 30 && !field; ++i) {
                    step(s);
                    for (const auto& fx : s.superWeapons.renderSnapshots())
                        field = field || fx.kind == "superWeaponRadiation";
                }
                if (!field) return fail("nuclear residual field missing");
            }
            std::vector<int> victims;
            std::vector<RuntimeSpatialAudioPosition> positions;
            const std::array<std::string, 3> owners{"Commander", "Ally", "Enemy"};
            for (int i = 0; i < 3; ++i) {
                const int id = addUnit(s, test.type, test.kind, 90 + i % 2, 90 + i / 2, owners[i], 1);
                auto* object = findMutableObjectById(s, id);
                object->hasRenderPosition = true;
                object->renderX = object->x + .125;
                object->renderY = object->y + .25;
                object->renderLevel = object->level;
                if (std::string(test.type) == "ORCA" || std::string(test.type) == "JUMPJET") object->airborne = true;
                victims.push_back(id);
                positions.push_back(runtimeSpatialAudioPositionForObject(*object));
            }
            std::vector<RuntimeSoundPlaybackRecord> records;
            const auto collect = [&]() {
                for (const auto& record : s.runtimeSoundPlaybackHistory)
                    if (record.intent == test.sound || record.section == "IntruderVoiceDie") records.push_back(record);
                s.runtimeSoundPlaybackHistory.clear();
            };
            s.runtimeSoundIntentHistory.clear(); s.runtimeSoundPlaybackHistory.clear();
            if (!test.radiation) {
                const bool chrono = std::string(test.power) == "ChronoSphereSpecial";
                if (!fire(s, test.power, {{90, 90}, victims.front()},
                        chrono ? std::optional<SuperWeaponTarget>{{{104, 100}, -1}} : std::nullopt)) return fail(error);
            }
            collect();
            for (int tick = 0; tick < hz * 45; ++tick) {
                step(s); collect();
                if (std::all_of(victims.begin(), victims.end(), [&](int id) { return !findObjectById(s, id); })) break;
            }
            if (std::any_of(victims.begin(), victims.end(), [&](int id) { return findObjectById(s, id) != nullptr; }))
                return fail(std::string(test.power) + "/" + test.type + " did not kill all three houses");
            for (const auto& position : positions) {
                const auto matches = [&](const auto& record) {
                    const bool crashCue = std::string(test.type) == "JUMPJET" || std::string(test.type) == "ORCA";
                    return record.intent == test.sound && record.spatial && record.trackCamera == crashCue
                        && std::abs(record.sourcePosition.x - position.x) < 1e-8
                        && std::abs(record.sourcePosition.y - position.y) < 1e-8
                        && std::abs(record.sourcePosition.level - position.level) < 1e-8;
                };
                const auto it = std::find_if(records.begin(), records.end(), matches);
                if (std::count_if(records.begin(), records.end(), matches) != 1 || it == records.end()
                        || !it->playable || it->spatialGain16384 <= 0)
                    return fail(std::string(test.power) + "/" + test.type + " death sample missing, repeated or misplaced");
            }
            if (std::string(test.type) == "ORCA"
                    && std::count_if(records.begin(), records.end(), [](const auto& r) { return r.section == "IntruderVoiceDie"; }) != 3)
                return fail("aircraft VoiceCrashing missing/repeated");
            const int before = s.runtimeSoundEvents;
            auto context = runtimeSuperWeaponContext(s);
            for (int id : victims) {
                SuperWeaponObjectLifecycleEvent duplicate;
                duplicate.kind = SuperWeaponObjectLifecycleKind::Removed;
                duplicate.objectId = id; duplicate.healthBefore = 1;
                duplicate.removalDisposition = SuperWeaponObjectRemovalDisposition::Destroyed;
                context.onObjectRemoved(std::move(duplicate));
            }
            (void)drainRuntimeObjectExitQueue(s);
            if (!s.pendingObjectExits.empty() || s.runtimeSoundEvents != before)
                return fail("repeated superweapon Removed replayed death audio");
            deaths += 3;
            std::cout << "Superweapon death audio: " << hz << "Hz " << test.power << '/' << test.type
                << (test.radiation ? "/residual" : "") << " 3 houses/sample/spatial/once PASS\n";
            return true;
            }();
            if (!passed) { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
        }
        for (int hz : {30, 60}) {
            const bool passed = [&]() -> bool {
            // Transformation and ordinary transport cleanup are successful exits,
            // not deaths; protection and nonlethal hits must also stay silent.
            auto storage = fixture("Americans", hz); auto& s = *storage; prepareAudio(s);
            const int transformed = addUnit(s, "E1", WorldObjectKind::Infantry, 90, 90);
            const int protectedTank = addUnit(s, "MTNK", WorldObjectKind::Unit, 104, 100);
            if (!fire(s, "GeneticConverterSpecial", {{90, 90}, transformed})) return fail(error);
            step(s, 2);
            if (findObjectById(s, transformed) || !findObjectById(s, protectedTank)) return fail("mutation fixture");
            if (!fire(s, "IronCurtainSpecial", {{104, 100}, protectedTank})
                    || !fire(s, "ChronoSphereSpecial", {{104, 100}, protectedTank}, SuperWeaponTarget{{108, 104}, -1})) return fail(error);
            step(s, 2);
            auto context = runtimeSuperWeaponContext(s);
            const int transport = addUnit(s, "ORCA", WorldObjectKind::Unit, 110, 108);
            SuperWeaponObjectLifecycleEvent cleanup;
            cleanup.kind = SuperWeaponObjectLifecycleKind::Removed; cleanup.objectId = transport;
            cleanup.healthBefore = 1; cleanup.removalDisposition = SuperWeaponObjectRemovalDisposition::Consumed;
            context.onObjectRemoved(std::move(cleanup)); drainRuntimeObjectExitQueue(s);
            const int survivor = addUnit(s, "E1", WorldObjectKind::Infantry, 110, 108, "Enemy", 100000);
            if (!fire(s, "LightningStormSpecial", {{110, 108}, survivor})) return fail(error);
            for (int i = 0; i < hz * 30 && findObjectById(s, survivor)->health == 100000; ++i) step(s);
            if (!findObjectById(s, survivor) || findObjectById(s, survivor)->health == 100000)
                return fail("nonlethal lightning fixture");
            for (const auto& record : s.runtimeSoundPlaybackHistory)
                if (record.intent == "sound:GIDie" || record.intent == "sound:GenVehicleDie" || record.intent == "IntruderVoiceDie")
                    return fail("transform/protected/transport exit emitted death sound");
            // The physical enemy death sound still passes the local listener
            // rules while simulating a remote human house.
            auto distant = fixture("Americans", hz); prepareAudio(*distant, true);
            distant->remoteHumanHouseScopeActive = true;
            const int farVictim = addUnit(*distant, "E1", WorldObjectKind::Infantry, 90, 90);
            if (!fire(*distant, "IronCurtainSpecial", {{90, 90}, farVictim})) return fail(error);
            step(*distant, 2);
            const auto record = std::find_if(distant->runtimeSoundPlaybackHistory.begin(), distant->runtimeSoundPlaybackHistory.end(),
                [](const auto& r) { return r.intent == "sound:GIDie"; });
            if (record == distant->runtimeSoundPlaybackHistory.end() || !record->spatial
                    || !record->suppressed || record->spatialGain16384 != 0 || record->status != "spatial-inaudible")
                return fail("remote-house distant death bypassed spatial suppression");
            auto audible = fixture("Americans", hz), muted = fixture("Americans", hz);
            prepareAudio(*audible); prepareAudio(*muted);
            muted->runtimeSoundEffectsEnabled = false; muted->runtimeSoundVolume = 0;
            muted->runtimeSoundEvents = 47;
            const int first = addUnit(*audible, "E1", WorldObjectKind::Infantry, 90, 90);
            const int second = addUnit(*muted, "E1", WorldObjectKind::Infantry, 90, 90);
            if (!fire(*audible, "IronCurtainSpecial", {{90, 90}, first})
                    || !fire(*muted, "IronCurtainSpecial", {{90, 90}, second})) return fail(error);
            step(*audible, 2); step(*muted, 2);
            completeRuntimeWorldRefresh(*audible); completeRuntimeWorldRefresh(*muted);
            std::string firstError, secondError;
            const auto firstHash = runtimeWorldSimulationHash(*audible, firstError, false);
            const auto secondHash = runtimeWorldSimulationHash(*muted, secondError, false);
            if (!firstError.empty() || !secondError.empty() || firstHash != secondHash)
                return fail("local audio changed authoritative death state: " + firstError + secondError);
            auto airport = fixture("Americans", hz); prepareAudio(*airport);
            const int host = addUnit(*airport, "GAAIRC", WorldObjectKind::Structure, 90, 90, "Enemy", 1);
            const int parked = addUnit(*airport, "ORCA", WorldObjectKind::Unit, 110, 108, "Enemy");
            auto* plane = findMutableObjectById(*airport, parked);
            plane->dockedToObjectId = host; plane->airborne = false;
            if (!fire(*airport, "PsychicDominatorSpecial", {{90, 90}, host})) return fail(error);
            for (int i = 0; i < hz * 45 && findObjectById(*airport, parked); ++i) step(*airport);
            if (findObjectById(*airport, host) || findObjectById(*airport, parked)
                    || std::count_if(airport->runtimeSoundPlaybackHistory.begin(), airport->runtimeSoundPlaybackHistory.end(),
                        [](const auto& r) { return r.intent == "IntruderVoiceDie"; }) != 1)
                return fail("superweapon airport loss omitted/repeated parked aircraft death voice");
            auto cargo = fixture("Americans", hz); prepareAudio(*cargo);
            const int vehicle = addUnit(*cargo, "HTK", WorldObjectKind::Unit, 90, 90, "Commander", 1);
            const int passenger = addUnit(*cargo, "E1", WorldObjectKind::Infantry, 90, 90, "Commander");
            findMutableObjectById(*cargo, vehicle)->transportPassengerIds.push_back(passenger);
            findMutableObjectById(*cargo, passenger)->mission = "Passenger";
            if (!fire(*cargo, "ChronoSphereSpecial", {{90, 90}, vehicle}, SuperWeaponTarget{{104, 100}, -1})) return fail(error);
            step(*cargo, 2);
            const auto* moved = findObjectById(*cargo, vehicle);
            if (!moved || moved->x != 104 || moved->y != 100) return fail("chrono transport fixture");
            const auto physical = runtimeSpatialAudioPositionForObject(*moved);
            cargo->runtimeSoundPlaybackHistory.clear();
            if (!fire(*cargo, "NukeSpecial", {{104, 100}, vehicle})) return fail(error);
            for (int i = 0; i < hz * 45 && findObjectById(*cargo, passenger); ++i) step(*cargo);
            const auto passengerSound = std::find_if(cargo->runtimeSoundPlaybackHistory.begin(), cargo->runtimeSoundPlaybackHistory.end(),
                [](const auto& r) { return r.intent == "sound:GIDie"; });
            if (findObjectById(*cargo, passenger) || passengerSound == cargo->runtimeSoundPlaybackHistory.end()
                    || !passengerSound->spatial || passengerSound->sourcePosition.x != physical.x
                    || passengerSound->sourcePosition.y != physical.y)
                return fail("teleported transport passenger death sound remained at source cell");
            std::cout << "Superweapon death audio boundaries: " << hz << "Hz transform/protection/consumed/nonlethal/remote-distance/authority PASS\n";
            return true;
            }();
            if (!passed) { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
        }
        if (!failures.empty()) return fail("death audio failed " + std::to_string(failures.size())
            + " scenarios; first: " + failures.front());
        std::cout << "Superweapon death audio PASS: " << deaths << " unit deaths, 30/60Hz, all stock lethal paths and rules-driven Dominator damage\n";
        return true;
    }
