bool runRuntimeObjectAmbientSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) {error = "Object ambient: " + why; return false;};
    const char* output = std::getenv("CONCORD_OBJECT_AMBIENT_DIR");
    const std::string root = output && *output ? output : "build/smoke/feature-checks/17-object-ambient/checks";
    std::filesystem::create_directories(root);
    auto base = std::make_unique<RuntimeWindowState>(); base->config = config;
    base->config.defaultMapPath = "local_data/resources/mix_exports/multimd/2peaks.map";
    base->config.computerAiEnabled = base->config.noCooldown = base->config.multiplayerActive = false;
    base->config.playerSlots.clear(); base->config.gameModeRulesOverride.clear();
    base->report = buildRuntimeStartupReport(base->config, &base->world, &base->theater, &base->art, &base->rules, &base->mapFacts);
    if (!base->report.success) return fail("original resources");
    IniFile original; if (!original.loadFromFile("local_data/rulesmd.ini")) return fail("original rules");
    int rows = 0, configured = 0, scenes = 0, silent = 0, deaths = 0, saves = 0, boundaries = 0, waves = 0;
    std::set<std::string> configuredIds;
    const auto audit = [&](const auto& types) {
        for (const auto& type : types) {
            ++rows;
            if (type.ambientSound != trimAsciiLocal(original.getValue(type.id, "AmbientSound", ""))) return false;
            if (!type.ambientSound.empty()) {++configured; configuredIds.insert(type.id);}
        }
        return true;
    };
    if (!audit(base->rules.getInfantryTypes()) || !audit(base->rules.getVehicleTypes()) || !audit(base->rules.getAircraftTypes())
        || !audit(base->rules.getBuildingTypes()) || !audit(base->rules.getTerrainTypes()) || configuredIds.size() != 14) return fail("all rule categories");
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
    const auto add = [&](RuntimeWindowState& s, const std::string& id, int objectId) {
        const auto center = runtimeWorldAudioContext(s).listenerCell;
        WorldObjectInstance object; object.instanceId = objectId; object.type = id; object.owner = "Americans";
        object.x = center.x; object.y = center.y; object.cell = runtimeCellKey(object.x, object.y); object.level = center.level;
        object.kind = s.rules.findBuildingType(id) ? WorldObjectKind::Structure
            : s.rules.findUnitType("InfantryType", id) ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
        object.health = object.kind == WorldObjectKind::Structure ? s.rules.findBuildingType(id)->strength : findRuntimeUnitType(s.rules, object)->strength;
        object.alive = true; object.mission = "Guard"; object.currentAmmo = -1;
        s.world.accessObjects().push_back(object);
        assignRuntimeInfantrySubcellAtCurrentLocation(s, s.world.accessObjects().back());
        refreshRuntimeObjectOccupancyAfterLocalChange(s);
    };
    const auto start = [&](RuntimeWindowState& s, std::uint64_t now) {
        advanceRuntimeObjectAmbientAudio(s, now);
        if (!s.objectAmbientAudio.bindings.empty() && !s.objectAmbientAudio.bindings.begin()->second.active) {
            now = std::max(now, s.objectAmbientAudio.bindings.begin()->second.nextMilliseconds);
            advanceRuntimeObjectAmbientAudio(s, now);
        }
        return now;
    };
    std::ofstream events(root + "/ambient-events.csv"); events << "hz,type,event,sample,handle,moved,limbo,range,exit,frequency,milliseconds,sourcePath,playbackPath\n";
    for (int hz : {15, 30, 60}) for (const auto& id : configuredIds) {
        auto box = make(hz); auto& s = *box; add(s, id, 1700);
        auto now = start(s, 0); ++scenes;
        if (id == "PROPA" || id == "CAMSC06") {
            if (!s.objectAmbientAudio.bindings.empty() || !s.runtimeWorldAudioBackendActionHistory.empty()) return fail(id + " invented missing sample");
            ++silent; events << hz << ',' << id << ",silent,,,,,,\n"; continue;
        }
        if (s.objectAmbientAudio.bindings.size() != 1) return fail(id + " missing controller");
        auto binding = s.objectAmbientAudio.bindings.begin()->second;
        if (!binding.active || binding.ordinal != 1) return fail(id + " first playback ordinal=" + std::to_string(binding.ordinal));
        const auto firstAction = s.runtimeWorldAudioBackendActionHistory.back();
        const auto firstPath = findRuntimeSoundSamplePath(s, binding.sample);
        if (!firstPath) return fail(id + " first sample path");
        const auto playbackPath = runtimeAudioPitchVariantPath(*firstPath,
            std::filesystem::path(s.config.localDataRootPath) / "runtime_audio_cache", firstAction.frequencyPermille);
        const auto milliseconds = runtimeWavDurationTicks(playbackPath, 1000);
        if (binding.endMilliseconds - now != milliseconds) return fail(id + " sample schedule differs from playback file");
        for (int i = 0; i < 30; ++i) advanceRuntimeObjectAmbientAudio(s, now);
        if (s.objectAmbientAudio.bindings.begin()->second.ordinal != 1) return fail(id + " repeated same-frame restart");
        auto& object = s.world.accessObjects().front(); const int x = object.x;
        ++object.x; object.cell = runtimeCellKey(object.x, object.y); advanceRuntimeObjectAmbientAudio(s, now);
        if (s.objectAmbientAudio.bindings.begin()->second.handle != binding.handle
            || s.objectAmbientAudio.bindings.begin()->second.source.cell.x != object.x
            || s.objectAmbientAudio.bindings.begin()->second.ordinal != 1) return fail(id + " moving controller restarted");
        object.cloaked = true; object.poweredOperational = false; advanceRuntimeObjectAmbientAudio(s, now);
        if (s.objectAmbientAudio.bindings.empty()) return fail(id + " blanket cloak/power suppression");
        object.cloaked = false; object.poweredOperational = true; object.mission = "InsideTransport"; advanceRuntimeObjectAmbientAudio(s, now);
        if (!s.objectAmbientAudio.bindings.empty()) return fail(id + " limbo sound retained");
        object.mission = "Guard"; now = start(s, now + 1);
        object.x = x + 200; advanceRuntimeObjectAmbientAudio(s, now);
        if (!s.objectAmbientAudio.bindings.empty()) return fail(id + " out-of-range sound retained");
        object.x = x; object.cell = runtimeCellKey(x, object.y); now = start(s, now + 1);
        s.runtimeSoundEffectsEnabled = false; advanceRuntimeObjectAmbientAudio(s, now);
        if (!s.objectAmbientAudio.bindings.empty()) return fail(id + " mute retained controller");
        s.runtimeSoundEffectsEnabled = true; now = start(s, now + 1);
        const auto finalBinding = s.objectAmbientAudio.bindings.begin()->second;
        if (hz == 30) {
            const auto path = root + "/ambient-" + id + ".ini";
            if (!saveRuntimeState(s, path, error)) return fail("save " + error);
            auto restored = make(hz);
            if (!loadRuntimeState(*restored, path, error)) return fail("load " + error);
            if (!restored->objectAmbientAudio.bindings.empty()) return fail("audio device/controller persisted");
            start(*restored, now);
            if (restored->objectAmbientAudio.bindings.size() != 1) return fail("restored live object did not recreate sound");
            ++saves;
        }
        RuntimeObjectExitRequest exit; exit.objectId = 1700; exit.tick = s.tick;
        exit.kind = object.kind == WorldObjectKind::Structure && hz == 60 ? RuntimeObjectExitKind::Sold : RuntimeObjectExitKind::Destroyed;
        exit.suppressCrew = exit.suppressDeathWeapon = true;
        if (!finalizeRuntimeObjectExit(s, exit).accepted) return fail(id + " actual exit");
        advanceRuntimeObjectAmbientAudio(s, now);
        if (!s.objectAmbientAudio.bindings.empty()) return fail(id + " departed object audio survived");
        const auto& last = s.runtimeWorldAudioBackendActionHistory.back();
        if (last.kind != RuntimeWorldAudioActionKind::Stop || last.handleId != finalBinding.handle) return fail(id + " targeted stop");
        ++deaths;
        events << hz << ',' << id << ',' << binding.source.event << ',' << binding.sample << ',' << binding.handle << ",yes,yes,yes,yes," << firstAction.frequencyPermille << ',' << milliseconds
            << ',' << std::quoted(*firstPath) << ',' << std::quoted(playbackPath) << '\n';
    }
    auto box = make(30); auto& s = *box; add(s, "CAWASH18", 1700); auto now = start(s, 0);
    std::set<std::string> fountainSamples;
    for (int i = 0; i < 3; ++i) {
        fountainSamples.insert(s.objectAmbientAudio.bindings.begin()->second.sample);
        now = s.objectAmbientAudio.bindings.begin()->second.nextMilliseconds;
        advanceRuntimeObjectAmbientAudio(s, now);
    }
    if (fountainSamples.size() != 3) return fail("random All repeated before visiting fountain samples"); ++boundaries;
    const auto originalHandle = s.objectAmbientAudio.bindings.begin()->second.handle;
    add(s, "CAWASH18", 1701); start(s, now);
    s.world.accessObjects().erase(s.world.accessObjects().begin()); advanceRuntimeObjectAmbientAudio(s, now);
    if (s.objectAmbientAudio.bindings.size() != 1 || s.objectAmbientAudio.bindings.begin()->second.handle == originalHandle) return fail("one object's removal stopped its neighbor"); ++boundaries;
    s.world.accessObjects().front().type = "COP";
    s.world.accessObjects().front().kind = WorldObjectKind::Unit; now = start(s, now + 1);
    if (s.objectAmbientAudio.bindings.begin()->second.sample != "vcoplo1") return fail("police attack intro");
    for (int i = 0; i < 8; ++i) {
        now = s.objectAmbientAudio.bindings.begin()->second.nextMilliseconds; advanceRuntimeObjectAmbientAudio(s, now);
        if (s.objectAmbientAudio.bindings.begin()->second.sample == "vcoplo1") return fail("police intro repeated in body loop");
    } ++boundaries;
    stopRuntimeWorldAudioBackend(s);
    if (!s.objectAmbientAudio.bindings.empty() || !s.runtimeWorldAudioBackendBindings.empty()) return fail("backend shutdown"); ++boundaries;
    now = start(s, now); loadRuntimeSoundDatabase(s);
    if (!s.objectAmbientAudio.bindings.empty()) return fail("sound catalog reload"); ++boundaries;
    // An event limit must release its slot when the bound object disappears.
    s.world.accessObjects().clear(); add(s, "TUG", 1700); add(s, "TUG", 1701); now = start(s, now);
    if (s.objectAmbientAudio.bindings.size() != 1) return fail("original Limit=1");
    const auto limited = s.objectAmbientAudio.bindings.begin()->first;
    s.world.accessObjects().erase(s.world.accessObjects().begin()); now = start(s, now + 1);
    if (s.objectAmbientAudio.bindings.size() != 1 || s.objectAmbientAudio.bindings.begin()->first == limited) return fail("limit slot not released"); ++boundaries;
    s.world.accessObjects().clear(); add(s, "NAPSYA", 1700); s.world.accessObjects().front().owner = "Special";
    s.visionEnabled = true; s.vision.reset();
    s.vision.update(s.world, &s.rules, {{"Americans", {}}}, s.tick);
    advanceRuntimeObjectAmbientAudio(s, now);
    if (!s.objectAmbientAudio.bindings.empty()) return fail("Type=shroud audible in unexplored cell");
    add(s, "E1", 1701); s.vision.update(s.world, &s.rules, {{"Americans", {}}}, s.tick + 1);
    now = start(s, now + 1);
    if (s.objectAmbientAudio.bindings.size() != 1) return fail("shroud source did not enter audible state"); ++boundaries;
    s.world.accessObjects().clear(); add(s, "COW", 1700); s.world.accessObjects().front().owner = "Special";
    s.vision.reset(); s.vision.update(s.world, &s.rules, {{"Americans", {}}}, s.tick + 2);
    now = start(s, now + 1);
    if (s.objectAmbientAudio.bindings.size() != 1) return fail("unshroud source incorrectly suppressed"); ++boundaries;
    s.visionEnabled = false;
    {
        RuntimeObjectAmbientAudioState controller; RuntimeObjectAmbientSpec one;
        one.sound.eventId = "loop"; one.sound.samples = {"sample"}; one.sound.volumePercent = 80;
        one.literalLoop = one.sound.loop = true; controller.specs["loop"] = one;
        RuntimeObjectAmbientSource source{"source", "type", "loop", {1, 2, 0}, 800};
        const auto duration = [](const auto&, int) {return 500;};
        const auto first = controller.advance({source}, 0, 0, duration);
        if (first.size() != 1 || first[0].backendLoopIterations != -1) return fail("indefinite device loop");
        if (!controller.advance({source}, 100000, 60, duration).empty()) return fail("device loop restarted each tick"); ++boundaries;
        source.gainPermille = 400; source.cell.x = 3;
        const auto moved = controller.advance({source}, 100001, 60, duration);
        if (moved.size() != 1 || moved[0].kind != RuntimeWorldAudioActionKind::SetGain || moved[0].gainPermille != 400) return fail("loop gain update"); ++boundaries;
        if (controller.advance({}, 100002, 60, duration).size() != 1 || !controller.bindings.empty()) return fail("loop stop"); ++boundaries;
        controller.advance({source}, 100010, 61, duration);
        const auto reset = controller.advance({source}, 10, 0, duration);
        if (reset.size() != 2 || reset[0].kind != RuntimeWorldAudioActionKind::Stop) return fail("clock reset retained old loop"); ++boundaries;
    }
    // Physical sample availability, not just labels in soundmd.ini.
    std::ofstream audio(root + "/ambient-audio.csv"); audio << "event,sample,milliseconds,bytes,path\n";
    std::set<std::string> samples;
    for (const auto& [event, spec] : s.objectAmbientAudio.specs) for (const auto& sample : spec.sound.samples) if (samples.insert(sample).second) {
        const auto path = findRuntimeSoundSamplePath(s, sample);
        if (!path || runtimeWavDurationTicks(*path, 1000) <= 1 || std::filesystem::file_size(*path) <= 44) return fail("missing original sample " + sample);
        audio << event << ',' << sample << ',' << runtimeWavDurationTicks(*path, 1000) << ',' << std::filesystem::file_size(*path) << ',' << std::quoted(*path) << '\n'; ++waves;
    }
    // ObjectType also covers terrain. A mod-supplied terrain sound uses an
    // independent identity and vanishes when that terrain entry disappears.
    const auto priorHash = s.rules.getDetailRulesHash(); IniFile extra;
    extra.loadFromString("[TerrainTypes]\n999=AMBIENTTEST\n[AMBIENTTEST]\nStrength=50\nAmbientSound=FountainLoop\n");
    original.appendFrom(extra); s.rules.loadFromIni(original);
    if (s.rules.getDetailRulesHash() == priorHash || !s.rules.findTerrainType("AMBIENTTEST")) return fail("terrain AmbientSound load/hash"); ++boundaries;
    s.world.accessObjects().clear();
    const auto center = runtimeWorldAudioContext(s).listenerCell; WorldTerrainObject terrain;
    terrain.mapId = "ambient-test"; terrain.type = "AMBIENTTEST"; terrain.x = center.x; terrain.y = center.y;
    terrain.cell = runtimeCellKey(terrain.x, terrain.y); terrain.health = terrain.strength = 50;
    s.world.accessTerrainObjects().push_back(terrain); now = start(s, now + 1);
    if (s.objectAmbientAudio.bindings.size() != 1 || !s.objectAmbientAudio.bindings.begin()->first.starts_with("terrain:")) return fail("terrain source");
    s.world.accessTerrainObjects().clear(); advanceRuntimeObjectAmbientAudio(s, now);
    if (!s.objectAmbientAudio.bindings.empty()) return fail("terrain source removal"); ++boundaries;
    std::cout << "ObjectAmbient PASS rows=" << rows << " configuredRows=" << configured << " unique=" << configuredIds.size()
        << " scenes=" << scenes << " silent=" << silent << " exits=" << deaths << " saves=" << saves
        << " boundaries=" << boundaries << " waves=" << waves << '\n'; return true;
}
