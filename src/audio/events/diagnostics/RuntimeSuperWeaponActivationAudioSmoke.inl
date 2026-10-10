// Real sidebar/map input and the normal simulation clock exercise the original
// activation definitions. Audio histories are observed without clearing them.
bool runRuntimeGeneticMutationPositionSmoke(RuntimeWindowState&, std::string&);
bool runRuntimeSuperWeaponActivationAudioSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    IniFile stock, originalArt; RulesDatabase original; ArtDatabase stockArt;
    if (!stock.loadFromFile(live.report.rulesPath) || !original.loadFromIni(stock)
        || !originalArt.loadFromFile(live.report.artPath) || !stockArt.loadFromIni(originalArt)) {
        error = "SW activation audio: original rules/art unavailable"; return false;
    }
    const std::set<std::string> buttons{"NukeSpecial", "IronCurtainSpecial", "LightningStormSpecial",
        "ChronoSphereSpecial", "ParaDropSpecial", "AmericanParaDropSpecial", "PsychicDominatorSpecial",
        "SpyPlaneSpecial", "GeneticConverterSpecial", "ForceShieldSpecial", "PsychicRevealSpecial"};
    const char* output = std::getenv("CONCORD_SUPERWEAPON_ACTIVATION_AUDIO_OUTPUT");
    const std::string outputRoot = output ? output : "";
    if (!outputRoot.empty()) std::filesystem::create_directories(outputRoot);
    std::ofstream geneticHealthRows;
    if (!outputRoot.empty()) {
        geneticHealthRows.open(outputRoot + "/genetic_health.csv");
        if (!geneticHealthRows) { error = "SW activation audio: genetic health output unavailable"; return false; }
        geneticHealthRows << "case,stage,format,tick,hz,object,type,owner,hp,typed_strength\n";
    }
    const char* filterValue = std::getenv("CONCORD_SUPERWEAPON_ACTIVATION_AUDIO_CASE");
    const std::string caseFilter = filterValue ? filterValue : "";
    std::vector<std::string> failures; std::string label, stage, inputDiagnosis, fixtureFailure;
    int scenes = 0, activations = 0, decoded = 0, positionChecks = 0, saves = 0, snapshots = 0, pairedTicks = 0;
    int selectionBoundaries = 0, nativeChecks = 0;
    int supportLoopScenes = 0, supportLoopSamples = 0, supportLoopResumes = 0;
    const auto fail = [&](const std::string& reason) {
        error = "SW activation audio " + label + " [" + stage + "]: " + reason;
        if (!inputDiagnosis.empty()) error += " input=" + inputDiagnosis;
        return false;
    };
    const auto scene = [&](const std::string& name, const auto& body) {
        if (!caseFilter.empty() && name != caseFilter) return;
        label = name; stage = "setup"; inputDiagnosis.clear(); fixtureFailure.clear();
        bool passed = body();
        if (!fixtureFailure.empty()) { fail(fixtureFailure); passed = false; }
        if (passed) { ++scenes; std::cout << "SW activation audio " << name << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    const auto verifyBattleContinues = [&](const RuntimeWindowState& s) {
        if (s.skirmishMatchFinished && fixtureFailure.empty())
            fixtureFailure = "real Battle fixture ended during audio observation tick=" + std::to_string(s.tick)
                + " result=" + s.skirmishResult;
    };
    int landTile = -1;
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(live, cell);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge) { landTile = tile; break; }
    }
    if (landTile < 0) return fail("original land TMP unavailable");
    const RECT client{0, 0, 1024, 768}, viewport = runtimeMapViewport(client);
    std::unordered_map<const RuntimeWindowState*, std::uint64_t> audioWallBases;
    const auto presentationNow = [&](const RuntimeWindowState& s) {
        return audioWallBases.at(&s) + std::uint64_t(s.tick) * 1000 / std::max(1, s.config.fixedTickHz);
    };
    const auto make = [&](int hz, const std::string& country = "Americans") {
        auto result = std::make_unique<RuntimeWindowState>(); auto& s = *result;
        seedRuntimeLoadScratch(s, live, false); s.config = live.config; s.config.fixedTickHz = hz;
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = s.config.crates = s.config.shortGame = false;
        s.config.superweapons = true; s.config.gameMode = "Battle"; s.config.playerCountry = country;
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.rules = original; s.art = stockArt; s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.mapFacts = {}; s.mapFacts.theater = live.mapFacts.theater; s.mapFacts.lighting = live.mapFacts.lighting;
        // This fixture replaces DryHeat's terrain. Its old authored playable
        // rectangle must not crop the new grid's camera or real mouse input.
        // A positive original header width keeps the native DXY projection;
        // absent Size/LocalSize selects the actual decoded terrain envelope.
        s.report.map.size.clear(); s.report.map.localSize.clear();
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        for (int y = 55; y <= 124; ++y) for (int x = 55; x <= 124; ++x) {
            // Native terrain ownership uses a 512-wide index; gameplay
            // object/lookup keys below use the separate 1000-wide format.
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = landTile; cell.subTileIndex = 0; s.world.accessTerrainCells().push_back(cell);
        }
        int id = 1; std::set<std::string> sourceTypes;
        const auto building = [&](const BuildingType& type, RuntimeCellPoint cell) {
            WorldObjectInstance o; o.instanceId = id++; o.kind = WorldObjectKind::Structure;
            o.type = type.id; o.owner = "Commander"; o.x = cell.x; o.y = cell.y;
            o.cell = runtimeCellKey(o.x, o.y); o.alive = o.poweredOperational = true;
            o.health = type.strength; o.mission = "Sleep"; s.world.accessObjects().push_back(o);
        };
        for (const auto& sw : buttons) for (const auto& type : s.rules.getBuildingTypes())
            if (type.superWeapon == sw || type.superWeapon2 == sw) {
                if (sourceTypes.insert(type.id).second) {
                    const int slot = static_cast<int>(sourceTypes.size()) - 1;
                    building(type, {62 + slot % 4 * 6, 62 + slot / 4 * 7});
                }
                break;
            }
        if (const auto* power = s.rules.findBuildingType("NANRCT")) {
            building(*power, {62, 84}); building(*power, {68, 84});
        }
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSoundEffectsEnabled = true; s.runtimeSoundVolume = 100; s.runtimeVoiceVolume = 0;
        s.runtimeSuppressPlayerAudioFeedback = false; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.owner = "Commander"; s.economy.credits = 10000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        // Real Battle outcome scans configured slot owners. Keep a quiet
        // hostile stock reactor alive well away from every activation target;
        // otherwise tick 10 declares victory and legitimately stops all movers.
        // Install slots after economy startup so no extra starting army is
        // generated. Commander remains the existing local command facade.
        RuntimeConfig::PlayerSlot localSlot; localSlot.enabled = localSlot.human = localSlot.local = true;
        localSlot.name = "Commander"; localSlot.country = country; localSlot.color = "Gold";
        RuntimeConfig::PlayerSlot hostileSlot; hostileSlot.enabled = true; hostileSlot.human = hostileSlot.local = false;
        hostileSlot.name = "Quiet hostile"; hostileSlot.country = "Russians"; hostileSlot.color = "Red";
        s.config.playerSlots = {localSlot, hostileSlot};
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear(); s.playerCommands.alliances.clear();
        s.skirmishCountryByOwner["commander"] = country;
        s.skirmishCountryByOwner["skirmishplayer2"] = "Russians";
        if (const auto* power = s.rules.findBuildingType("NANRCT")) {
            building(*power, {56, 56}); s.world.accessObjects().back().owner = "SkirmishPlayer2";
        } else fixtureFailure = "original quiet hostile NANRCT unavailable";
        if (runtimeOwnersAreAllied(s, "Commander", "SkirmishPlayer2"))
            fixtureFailure = "quiet Battle fixture enemy is unexpectedly allied";
        initializeRuntimeNavigation(s);
        s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority authority; authority.owner = "Commander"; authority.canonicalOwner = "commander";
        authority.country = country; authority.localFacade = true; s.humanHouseAuthorities.push_back(authority);
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        ensureRuntimeSuperWeaponsInitialized(s); initializeRuntimeOfflineGameplayRouting(s);
        // Keep the production offline command path. A copied preview only
        // reports its precise rejection reason; it cannot commit a cast.
        s.multiplayerIntentRouter.setCaptureCallback([&s](const auto& command, std::string& reason) {
            const bool accepted = dispatchRuntimeOfflineGameplayCommand(s, command, reason);
            if (accepted) return true;
            const auto* body = std::get_if<RuntimeMultiplayerGameplayFireSuperWeaponCommand>(&command.body);
            if (!body) return false;
            auto staged = s.superWeapons; auto context = runtimeSuperWeaponContext(s); context.emit = {};
            std::string originalReason; bool preview = false;
            if (body->targets.size() == 2) {
                preview = staged.submitTwoStageTarget(s.economy.owner, body->superWeaponId,
                    runtimeGameplaySuperWeaponTarget(body->targets[0]), runtimeGameplaySuperWeaponTarget(body->targets[1]),
                    context, &originalReason);
            } else if (staged.beginTargetSelection(s.economy.owner, body->superWeaponId, context, &originalReason)) {
                preview = staged.submitTarget(s.economy.owner, body->superWeaponId,
                    body->targets.empty() ? SuperWeaponTarget{} : runtimeGameplaySuperWeaponTarget(body->targets.front()),
                    context, &originalReason);
            }
            std::ostringstream detail; detail << reason << " staged=" << preview << ':' << originalReason
                << " house=" << s.economy.owner << " scope=" << s.humanHouseScopeActive
                << " remote=" << s.remoteHumanHouseScopeActive << " roster=" << s.humanHouseAuthorities.size();
            for (const auto& house : s.humanHouseAuthorities) detail << '[' << house.owner << '/' << house.canonicalOwner
                << " local=" << house.localFacade << " remoteEconomy=" << house.remoteEconomy.has_value() << ']';
            for (const auto& target : body->targets) detail << " target=" << target.cell.x << ',' << target.cell.y
                << '/' << target.objectId.value_or(-1);
            reason = detail.str(); return false;
        });
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s);
            auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
            s.superWeapons.tick(context); completeRuntimeWorldRefresh(s); }
        verifyBattleContinues(s);
        centerRuntimeCameraOnCell(s, client, {95, 94}); ensureRuntimeCameraForViewport(s, viewport);
        audioWallBases[&s] = GetTickCount64(); advanceRuntimeDiskMoveAudio(s, presentationNow(s));
        return result;
    };
    const auto add = [&](RuntimeWindowState& s, const std::string& type, WorldObjectKind kind,
        RuntimeCellPoint cell, const std::string& owner) {
        auto context = runtimeSuperWeaponContext(s); WorldObjectInstance o;
        o.instanceId = context.allocateObjectId(); o.kind = kind; o.type = type; o.owner = owner;
        o.x = cell.x; o.y = cell.y; o.cell = runtimeCellKey(o.x, o.y); o.alive = o.poweredOperational = true;
        o.health = runtimeMaxHealthForObject(s, o); o.mission = "Sleep";
        initializeRuntimeObjectAmmo(o, runtimeAmmoRulesForObject(s.rules, o));
        applyRuntimeInitialObjectFlightState(s.rules, o); runtimeInitializeModExtensions(o, s.rules, s.config.fixedTickHz);
        const int id = o.instanceId; s.world.accessObjects().push_back(o);
        initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s); return id;
    };
    const auto step = [&](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
        verifyBattleContinues(s);
        // This presentation pass is normally called by the window after a
        // simulation frame, including while an aircraft enters hearing range.
        advanceRuntimeDiskMoveAudio(s, presentationNow(s));
    };
    // Match overlapping portions of the bounded history; never erase the
    // engine's playback records, voices, effects, or managed sequence state.
    const auto recordKey = [](const RuntimeSoundPlaybackRecord& r) {
        std::ostringstream key; key << std::setprecision(17) << r.tick << '|' << r.playbackOrder << '|' << r.intent << '|' << r.section
            << '|' << r.sample << '|' << r.status << '|' << r.spatial << '|' << r.trackCamera << '|'
            << r.sourcePosition.x << '|' << r.sourcePosition.y << '|' << r.sourcePosition.level
            << '|' << r.gain16384 << '|' << r.pan16384; return key.str();
    };
    struct SoundObserver { std::vector<std::string> previous; std::vector<RuntimeSoundPlaybackRecord> records; };
    const auto collect = [&](RuntimeWindowState& s, SoundObserver& observer) {
        std::vector<std::string> current; for (const auto& record : s.runtimeSoundPlaybackHistory) current.push_back(recordKey(record));
        std::size_t overlap = std::min(current.size(), observer.previous.size());
        while (overlap && !std::equal(observer.previous.end() - overlap, observer.previous.end(), current.begin())) --overlap;
        for (std::size_t i = overlap; i < current.size(); ++i) observer.records.push_back(s.runtimeSoundPlaybackHistory[i]);
        observer.previous = std::move(current);
    };
    const auto prime = [&](RuntimeWindowState& s, SoundObserver& observer) { collect(s, observer); observer.records.clear(); };
    const auto count = [](const SoundObserver& observer, const std::string& event) {
        return static_cast<int>(std::count_if(observer.records.begin(), observer.records.end(), [&](const auto& r) {
            return toLowerAsciiLocal(r.section) == toLowerAsciiLocal(event);
        }));
    };
    const auto describe = [](const RuntimeSoundPlaybackRecord& r) {
        std::ostringstream out; out << r.section << '/' << r.sample << " tick=" << r.tick << " status=" << r.status
            << " xyz=" << r.sourcePosition.x << ',' << r.sourcePosition.y << ',' << r.sourcePosition.level
            << " spatial=" << r.spatial << " gain=" << r.gain16384 << " spatialGain=" << r.spatialGain16384 << " pan=" << r.pan16384;
        return out.str();
    };
    const auto actionKey = [](const RuntimeWorldAudioAction& a) {
        std::ostringstream out; out << int(a.kind) << '|' << a.handleId << '|' << a.eventId << '|' << a.sampleName
            << '|' << a.eventTick << '|' << a.playbackOrdinal << '|' << a.cell.x << '|' << a.cell.y << '|' << a.cell.level
            << '|' << a.gainPermille << '|' << a.queueAfterCurrent; return out.str();
    };
    struct ActionObserver { std::vector<std::string> previous; std::vector<RuntimeWorldAudioAction> actions; };
    const auto collectActions = [&](RuntimeWindowState& s, ActionObserver& observer) {
        std::vector<std::string> current;
        for (const auto& action : s.runtimeWorldAudioBackendActionHistory) current.push_back(actionKey(action));
        std::size_t overlap = std::min(current.size(), observer.previous.size());
        while (overlap && !std::equal(observer.previous.end() - overlap, observer.previous.end(), current.begin())) --overlap;
        for (std::size_t i = overlap; i < current.size(); ++i) observer.actions.push_back(s.runtimeWorldAudioBackendActionHistory[i]);
        observer.previous = std::move(current);
    };
    struct IntentObserver { std::vector<std::string> previous, intents; };
    const auto collectIntents = [&](RuntimeWindowState& s, IntentObserver& observer) {
        std::vector<std::string> current(s.runtimeSoundIntentHistory.begin(), s.runtimeSoundIntentHistory.end());
        std::size_t overlap = std::min(current.size(), observer.previous.size());
        while (overlap && !std::equal(observer.previous.end() - overlap, observer.previous.end(), current.begin())) --overlap;
        observer.intents.insert(observer.intents.end(), current.begin() + overlap, current.end()); observer.previous = std::move(current);
    };
    // These expected slots come from original INI data, independently of the
    // new resolver. An invalid StartSound falls through to a valid Report.
    const auto authoredSound = [&](RuntimeWindowState& s, const std::string& animation) {
        for (const char* key : {"StartSound", "Report"}) {
            const auto authored = trimAsciiLocal(originalArt.getValue(animation, key, ""));
            if (const auto* spec = s.worldAudioCatalog.find(authored)) return spec->eventId;
        }
        return std::string{};
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream ordinaryText, exactText, canonical; IniFile ordinaryIni, exactIni;
        if (!writeRuntimeStateToStream(s, ordinaryText, error) || !ordinaryIni.loadFromString(ordinaryText.str()))
            return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, exactText, error)
            || !writeRuntimeMultiplayerSnapshotExtension(s, exactText, error) || !exactIni.loadFromString(exactText.str())
            || !writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni, canonical, error) || !exactIni.loadFromString(canonical.str())))
            return std::unique_ptr<RuntimeWindowState>{};
        // These two documents retain the existing v14-base plus contextual
        // canonical-extension contract; they are not a fresh save schema.
        if (!outputRoot.empty() && label.rfind("GeneticConverterSpecial-", 0) == 0) {
            std::ofstream saved(outputRoot + "/" + label + (exact ? "-canonical-extension.ini" : "-ordinary14.ini"), std::ios::binary);
            saved << (exact ? canonical.str() : ordinaryText.str()); saved.close();
            if (!saved) { error = "genetic health checkpoint output unavailable"; return std::unique_ptr<RuntimeWindowState>{}; }
        }
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
        if (!materializeRuntimeLoadStateFromIni(*restored, ordinaryIni, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*restored, exactIni, error)))
            return std::unique_ptr<RuntimeWindowState>{};
        restored->runtimeSoundsEnabled = false; restored->runtimeVoiceVolume = 0;
        centerRuntimeCameraOnCell(*restored, client, {95, 94}); ensureRuntimeCameraForViewport(*restored, viewport);
        initializeRuntimeOfflineGameplayRouting(*restored); audioWallBases[restored.get()] = audioWallBases.at(&s); return restored;
    };
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t* className = L"ConcordSuperWeaponActivationAudioSmoke";
    WNDCLASSW wc{}; wc.lpfnWndProc = runtimeWindowProc; wc.hInstance = instance; wc.lpszClassName = className;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return fail("input window class");
    struct ClassCleanup { HINSTANCE instance; const wchar_t* name; ~ClassCleanup() { UnregisterClassW(name, instance); } } classCleanup{instance, className};
    HWND window = CreateWindowExW(0, className, L"SW activation audio", WS_POPUP, 0, 0, 1024, 768,
        nullptr, nullptr, instance, nullptr);
    if (!window) return fail("input window");
    struct WindowCleanup { HWND window; ~WindowCleanup() {
        SetWindowLongPtrW(window, GWLP_USERDATA, 0); SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DefWindowProcW));
        if (GetCapture() == window) ReleaseCapture(); DestroyWindow(window);
    } } windowCleanup{window};
    const auto left = [&](RuntimeWindowState& s, POINT point) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));
        SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(point.x, point.y));
        SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(point.x, point.y));
    };
    const auto begin = [&](RuntimeWindowState& s, const std::string& id) {
        RuntimeSnapshotActiveStateScope active(s); auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
        if (!s.superWeapons.forceReady("Commander", id, &context)) return fail("forceReady " + id);
        step(s); s.activeBuildTab = RuntimeBuildKind::Defense; const RECT sidebar = runtimeSidebarRect(client);
        std::optional<POINT> button;
        for (int page = 0; page < runtimeBuildPageCount(s, sidebar) && !button; ++page) {
            runtimeSetActiveBuildPage(s, page); const int offset = static_cast<int>(runtimeVisibleBuildCatalogIndices(s, sidebar).size());
            const auto visible = runtimeVisibleSuperWeaponsOnBuildPage(s, sidebar);
            for (std::size_t n = 0; n < visible.size(); ++n) if (visible[n]->superWeaponId == id) {
                const auto rect = runtimeBuildButtonRect(sidebar, offset + static_cast<int>(n));
                button = POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}; break;
            }
        }
        if (!button) return fail("real sidebar button missing " + id);
        audioWallBases[&s] = GetTickCount64() - std::uint64_t(s.tick) * 1000 / std::max(1, s.config.fixedTickHz);
        left(s, *button); const auto& intent = s.multiplayerIntentRouter.superWeapon();
        return intent && intent->superWeaponId == id ? true : fail("sidebar did not begin actual targeting: " + s.interactionStatus);
    };
    const auto click = [&](RuntimeWindowState& s, RuntimeCellPoint cell) {
        RuntimeSnapshotActiveStateScope active(s);
        // The click belongs to the visible cell interior. A geometric
        // diamond vertex can instead be owned by its neighbouring TMP.
        centerRuntimeCameraOnCell(s, client, cell); ensureRuntimeCameraForViewport(s, viewport);
        POINT point = runtimeScreenPointForCell(s, viewport, cell, terrainLevelAtRuntimeCell(s, cell.x, cell.y));
        const POINT authored = point; POINT picked = approximateCellFromScreenPoint(s, viewport, point);
        if (picked.x != cell.x || picked.y != cell.y) {
            bool found = false;
            for (int dy = 1; dy <= std::max(1, s.camera.tileHeight / 2) && !found; ++dy) {
                for (int dx : {0, -1, 1}) {
                    const POINT candidate{authored.x + dx, authored.y + dy};
                    const POINT candidateCell = approximateCellFromScreenPoint(s, viewport, candidate);
                    if (pointInRectInclusive(viewport, candidate) && candidateCell.x == cell.x && candidateCell.y == cell.y) {
                        point = candidate; picked = candidateCell; found = true; break;
                    }
                }
            }
        }
        std::ostringstream diagnosis; diagnosis << "requested=" << cell.x << ',' << cell.y << " pixel=" << point.x << ',' << point.y
            << " picked=" << picked.x << ',' << picked.y << " inViewport=" << pointInRectInclusive(viewport, point);
        if (picked.x != cell.x || picked.y != cell.y || point.x < viewport.left || point.x >= viewport.right
            || point.y < viewport.top || point.y >= viewport.bottom) {
            inputDiagnosis = diagnosis.str() + " visible native surface does not own the requested cell";
            return;
        }
        if (const auto& intent = s.multiplayerIntentRouter.superWeapon(); intent) {
            const auto* rule = s.rules.findSuperWeaponType(intent->superWeaponId);
            if (rule && !rule->placementAnim.empty()) {
                RuntimeImpactEffect probe; probe.animId = rule->placementAnim;
                initializeRuntimeSuperWeaponAnimation(s, probe);
                diagnosis << " placement=" << probe.animId << " art=" << (s.art.findArtType(probe.animId) != nullptr)
                    << " nativeFrames=" << probe.animationFrameCount;
            }
        }
        left(s, point);
        if (s.localChronosphereMarker) diagnosis << " marker=" << s.localChronosphereMarker->animation.animId
            << " frames=" << s.localChronosphereMarker->animation.animationFrameCount;
        inputDiagnosis = diagnosis.str();
    };
    // Inventory and every sample referenced by the original power/animation
    // definitions are decoded once, rather than trusting an intent string.
    scene("stock-catalog", [&]() {
        auto storage = make(30); auto& s = *storage; std::set<std::string> registered, sidebar, eventIds, samples;
        for (const auto& sw : original.getSuperWeaponTypes()) registered.insert(sw.id);
        auto expectedRegistered = buttons; expectedRegistered.insert("ChronoWarpSpecial");
        if (registered != expectedRegistered) return fail("original twelve registered definitions changed");
        for (const auto& snapshot : s.superWeapons.sidebarSnapshots()) if (snapshot.owner == "Commander") sidebar.insert(snapshot.superWeaponId);
        if (sidebar != buttons) return fail("eleven source sidebar inventory does not match original registrations");
        const auto* helper = original.findSuperWeaponType("ChronoWarpSpecial");
        if (!helper || !helper->postClick || helper->preDependent != "ChronoSphere" || sidebar.contains(helper->id))
            return fail("hidden post-click ChronoWarp pairing");
        for (const auto& sw : original.getSuperWeaponTypes()) {
            for (const auto& id : {sw.startSound, sw.specialSound}) if (!id.empty()) eventIds.insert(id);
            for (const auto& id : {sw.placementAnim, sw.launchAnim, sw.preImpactAnim, sw.impactAnim,
                sw.invokeAnim, sw.firstAnim, sw.secondAnim, sw.boltAnim, sw.boltExplosionAnim}) {
                const auto sound = authoredSound(s, id); if (!sound.empty()) eventIds.insert(sound);
                if (!id.empty() && s.art.findArtType(id) && runtimeArtAnimationSound(s, s.art.findArtType(id)) != sound)
                    return fail("original StartSound/Report resolver mismatch " + id);
            }
            if (const auto* plane = original.findUnitType("AircraftType", sw.planeType); plane && !plane->moveSound.empty())
                eventIds.insert(plane->moveSound);
        }
        eventIds.insert(s.rules.getAudioVisualValue("ChuteSound"));
        for (const auto& id : eventIds) {
            const auto* spec = s.worldAudioCatalog.find(id); if (!spec || spec->samples.empty()) return fail("catalog entry missing " + id);
            for (const auto& sample : spec->samples) if (samples.insert(sample).second) {
                const auto path = findRuntimeSoundSamplePath(s, sample);
                if (!path || runtimeWavDurationTicks(*path, 1000) <= 0) return fail("original WAV decode failed " + id + '/' + sample);
                ++decoded;
            }
        }
        ArtType fallback; fallback.startSound = "MissingIssue12Sound"; fallback.reportSound = "IronCurtainBlast";
        if (runtimeArtAnimationSound(s, &fallback) != "IronCurtainBlast") return fail("invalid StartSound did not use valid Report");
        fallback.startSound = "ChronosphereMove";
        if (runtimeArtAnimationSound(s, &fallback) != "ChronosphereMove") return fail("valid StartSound lost priority");
        std::cout << "SW original catalog: registered=12 realButtons=11 helper=ChronoWarp samples=" << samples.size() << '\n';
        return true;
    });
    const std::set<std::string> checkpointPowers{"NukeSpecial", "LightningStormSpecial", "IronCurtainSpecial", "ForceShieldSpecial", "ChronoSphereSpecial", "GeneticConverterSpecial"};
    for (int hz : {30, 60}) for (const auto& power : buttons) scene(power + '-' + std::to_string(hz), [&]() {
        auto storage = make(hz, power == "SpyPlaneSpecial" ? "Russians" : "Americans"); auto& s = *storage;
        RuntimeSnapshotActiveStateScope active(s); const auto* rule = original.findSuperWeaponType(power);
        if (!rule || !s.worldAudioCatalogLoaded) return fail("original rule/catalog");
        const bool supportCase = power == "SpyPlaneSpecial" || power == "ParaDropSpecial" || power == "AmericanParaDropSpecial";
        const auto* supportType = supportCase ? original.findUnitType("AircraftType", rule->planeType) : nullptr;
        std::map<int, RuntimeSpatialAudioPosition> supportPositions;
        int supportId = -1;
        const auto rememberSupport = [&]() {
            if (!supportCase) return;
            for (const auto& object : s.world.getObjects()) if (object.type == rule->planeType) {
                supportId = object.instanceId; supportPositions[s.tick] = runtimeSpatialAudioPositionForObject(object); break;
            }
        };
        const RuntimeCellPoint target{94, 94}, destination{106, 100}; std::vector<int> targets;
        const bool geneticHealthCase = power == "GeneticConverterSpecial";
        std::set<int> preMutationIds, mutationResultIds;
        int expectedMutationHealth = 0;
        if (power == "IronCurtainSpecial" || power == "ChronoSphereSpecial") {
            for (const auto cell : {target, RuntimeCellPoint{95, 94}, RuntimeCellPoint{94, 95}})
                targets.push_back(add(s, "MTNK", WorldObjectKind::Unit, cell, "Commander"));
        } else if (power == "ForceShieldSpecial") targets.push_back(add(s, "GAPOWR", WorldObjectKind::Structure, target, "Commander"));
        else if (power == "GeneticConverterSpecial") {
            const auto* brute = original.findUnitType("infantry", "BRUTE");
            if (!brute || brute->strength != 200 || std::stoi(stock.getValue("BRUTE", "Strength", "0")) != 200
                || rule->mutationType != "BRUTE") return fail("original human mutation type/full Strength");
            expectedMutationHealth = brute->strength;
            struct Victim { const char* type; int health; RuntimeCellPoint cell; };
            for (const auto& victim : std::array<Victim, 5>{{
                {"E1", 1, target}, {"E1", 62, {95, 94}}, {"E1", 125, {94, 95}},
                {"ENGINEER", 75, {96, 94}}, {"INIT", 100, {94, 96}}}}) {
                const auto* type = original.findUnitType("infantry", victim.type);
                if (!type || victim.health <= 0 || victim.health > type->strength) return fail("original mutation victim HP/type");
                const int id = add(s, victim.type, WorldObjectKind::Infantry, victim.cell, "Enemy");
                auto* object = findMutableObjectById(s, id);
                if (!object || object->health != type->strength) return fail("mutation victim did not start with original Strength");
                object->health = victim.health; // Explicitly wounded live victim; original rules remain unchanged.
                targets.push_back(id);
                if (geneticHealthRows) geneticHealthRows << label << ",source-before-ui,original," << s.tick << ',' << hz << ',' << id << ',' << object->type
                    << ',' << object->owner << ',' << object->health << ',' << type->strength << '\n';
            }
            for (const auto& object : s.world.getObjects()) {
                if (object.alive && object.type == "BRUTE") return fail("pre-existing Brute would conceal a missing mutation");
                preMutationIds.insert(object.instanceId);
            }
        } else if (power == "PsychicDominatorSpecial") targets.push_back(add(s, "HTNK", WorldObjectKind::Unit, target, "Enemy"));
        const auto verifyMutationHealth = [&](const RuntimeWindowState& state, bool first, const char* recordStage, const char* format) {
            if (!geneticHealthCase) return true;
            for (int id : targets) if (findObjectById(state, id)) return fail("mutation retained old live victim #" + std::to_string(id));
            std::set<int> actualIds;
            for (const auto& object : state.world.getObjects()) if (object.alive && object.type == rule->mutationType) {
                if (object.kind != WorldObjectKind::Infantry || object.owner != "Commander" || object.health != expectedMutationHealth
                    || preMutationIds.contains(object.instanceId) || !actualIds.insert(object.instanceId).second)
                    return fail(std::string("mutation full health/identity ") + format + " #" + std::to_string(object.instanceId)
                        + " HP=" + std::to_string(object.health) + " expected=" + std::to_string(expectedMutationHealth));
                if (recordStage && geneticHealthRows) geneticHealthRows << label << ',' << recordStage << ',' << format << ',' << state.tick << ',' << hz << ',' << object.instanceId
                    << ',' << object.type << ',' << object.owner << ',' << object.health << ',' << expectedMutationHealth << '\n';
            }
            if (actualIds.size() != targets.size() || targets.size() != 5) return fail("mutation did not create exactly five independent full-health Brutes");
            if (first) mutationResultIds = std::move(actualIds);
            else if (actualIds != mutationResultIds) return fail(std::string("mutation replayed/lost generated IDs in ") + format);
            return true;
        };
        SoundObserver sounds; ActionObserver engineActions; IntentObserver intents;
        if (!begin(s, power)) return false; prime(s, sounds); collectActions(s, engineActions); engineActions.actions.clear();
        collectIntents(s, intents); intents.intents.clear();
        stage = "real map activation"; click(s, target); collect(s, sounds); collectIntents(s, intents);
        if (power == "ChronoSphereSpecial") {
            if (!s.localChronosphereMarker || !s.chronosphereSelectionAudio.selected) return fail("first click lacks private selection");
            click(s, destination); collect(s, sounds); collectIntents(s, intents);
        }
        if (s.multiplayerIntentRouter.superWeapon()) return fail("real target click did not finish routed command: " + s.interactionStatus);
        int launchTick = s.tick + 1; step(s); rememberSupport(); collect(s, sounds); collectActions(s, engineActions); collectIntents(s, intents);
        // Inspect the first cast tick, before any later SelfHealing can hide
        // inherited low health. Both rates use the real sidebar/map cast.
        if (!verifyMutationHealth(s, true, "first-transformation-tick", "original")) return false;
        if (const auto* launched = s.superWeapons.findSidebarSnapshot("Commander", power); launched && launched->launchTick >= 0)
            launchTick = launched->launchTick;
        step(s); rememberSupport(); collect(s, sounds); collectActions(s, engineActions); collectIntents(s, intents);
        if (!verifyMutationHealth(s, false, nullptr, "original")) return false;
        std::unique_ptr<RuntimeWindowState> ordinary, exact; SoundObserver ordinarySounds, exactSounds;
        ActionObserver ordinaryEngine, exactEngine;
        std::set<std::string> powerSounds;
        const auto addSound = [&](const std::string& id) { if (!id.empty()) powerSounds.insert(toLowerAsciiLocal(id)); };
        addSound(rule->startSound); addSound(rule->specialSound);
        if (supportType) addSound(supportType->moveSound);
        for (const auto& id : {rule->placementAnim, rule->launchAnim, rule->preImpactAnim, rule->impactAnim,
            rule->invokeAnim, rule->firstAnim, rule->secondAnim, rule->boltAnim, rule->boltExplosionAnim}) addSound(authoredSound(s, id));
        if (power == "ParaDropSpecial" || power == "AmericanParaDropSpecial") addSound(s.rules.getAudioVisualValue("ChuteSound"));
        const auto relevant = [&](const auto& r) { return powerSounds.contains(toLowerAsciiLocal(r.section)); };
        const auto restorePair = [&]() {
            stage = "ordinary and canonical checkpoint";
            ordinary = checkpoint(s, false); exact = checkpoint(s, true);
            if (!ordinary || !exact) return fail("checkpoint restore: " + error);
            if (ordinary->superWeapons.stateHash() != s.superWeapons.stateHash() || exact->superWeapons.stateHash() != s.superWeapons.stateHash())
                return fail("checkpoint changed active lifecycle/charge authority");
            if (!verifyMutationHealth(*ordinary, false, "restored-boundary", "ordinary14")
                || !verifyMutationHealth(*exact, false, "restored-boundary", "ordinary14+canonical-extension")) return false;
            for (auto* restored : {ordinary.get(), exact.get()}) {
                if (std::any_of(restored->runtimeSoundPlaybackHistory.begin(), restored->runtimeSoundPlaybackHistory.end(), relevant))
                    return fail("loading replayed an activation/animation cue");
                if (restored->chronosphereSelectionAudio.selected) return fail("local selected sequence leaked into completed-command save");
            }
            prime(*ordinary, ordinarySounds); prime(*exact, exactSounds);
            collectActions(*ordinary, ordinaryEngine); ordinaryEngine.actions.clear();
            collectActions(*exact, exactEngine); exactEngine.actions.clear();
            ++saves; ++snapshots; return true;
        };
        if (checkpointPowers.contains(power) && !restorePair()) return false;
        auto afterCheckpoint = sounds.records.size(); bool finished = false;
        stage = "full simulation delayed cues and natural end";
        for (int i = 0; i < hz * 130; ++i) {
            step(s); rememberSupport(); collect(s, sounds); collectActions(s, engineActions); collectIntents(s, intents);
            if (!verifyMutationHealth(s, false, nullptr, "original")) return false;
            if (ordinary) {
                step(*ordinary); collect(*ordinary, ordinarySounds); step(*exact); collect(*exact, exactSounds); ++pairedTicks;
                collectActions(*ordinary, ordinaryEngine); collectActions(*exact, exactEngine);
                if (ordinary->superWeapons.stateHash() != s.superWeapons.stateHash() || exact->superWeapons.stateHash() != s.superWeapons.stateHash())
                    return fail("restored superweapon continuation diverged at tick " + std::to_string(s.tick));
                if (!verifyMutationHealth(*ordinary, false, nullptr, "ordinary14")
                    || !verifyMutationHealth(*exact, false, nullptr, "ordinary14+canonical-extension")) return false;
            }
            // Save a real audible flight, rather than an off-screen source
            // that has not yet requested any engine buffers.
            if (supportCase && !ordinary && supportId >= 0 && s.diskMoveAudio.sources.contains(supportId)
                && s.diskMoveAudio.sources.at(supportId).active) {
                if (!restorePair()) return false; afterCheckpoint = sounds.records.size();
                stage = "full simulation delayed cues and natural end";
            }
            const auto* state = s.superWeapons.findSidebarSnapshot("Commander", power);
            const bool activePower = state && (state->state == SuperWeaponRuntimeState::Active || state->state == SuperWeaponRuntimeState::Launching);
            const bool visual = std::any_of(s.impactEffects.begin(), s.impactEffects.end(), [](const auto& e) {
                return e.renderIntent == "superWeaponEffect" && e.remainingTicks > 0;
            });
            const bool support = std::any_of(s.world.getObjects().begin(), s.world.getObjects().end(), [](const auto& o) {
                return o.alive && (o.type == "PDPLANE" || o.type == "SPYP" || (o.kind == WorldObjectKind::Infantry && o.airborne));
            });
            if (!activePower && !visual && !support && i > hz) { finished = true; break; }
        }
        if (!finished) return fail("native activation/animation/support flight never completed");
        const auto require = [&](const std::string& id, int expected = 1) {
            const int found = count(sounds, id);
            return found == expected ? true : fail("expected " + id + " requests=" + std::to_string(expected) + " actual=" + std::to_string(found));
        };
        if (power == "NukeSpecial") {
            if (!require(rule->startSound) || !require(authoredSound(s, rule->launchAnim))
                || !require(authoredSound(s, rule->preImpactAnim)) || !require("NukeExplosion")) return false;
            int intro = -1, explosion = -1;
            for (const auto& r : sounds.records) { if (r.section == "NukeIntro") intro = r.tick; if (r.section == "NukeExplosion") explosion = r.tick; }
            if (!(intro > launchTick && explosion > intro)) return fail("native flight/buildup/explosion ordering");
        } else if (power == "IronCurtainSpecial") {
            if (!require(authoredSound(s, rule->invokeAnim))) return false;
        } else if (power == "ForceShieldSpecial") {
            if (!require(rule->startSound) || !require(rule->specialSound)) return false;
            const int fadeTick = launchTick + runtimeRuleFramesToTicks(rule->durationFrames - rule->fadeSoundFrames, hz);
            for (const auto& r : sounds.records) if (r.section == rule->specialSound && std::abs(r.tick - fadeTick) > 1)
                return fail("fade cue changed native deadline: " + describe(r) + " expected=" + std::to_string(fadeTick));
        } else if (power == "ChronoSphereSpecial") {
            if (!require("ChronosphereMove") || count(sounds, "WARPIN")) return fail("destination cue missing/repeated or animation id used as sound");
            if (s.chronosphereSelectionAudio.selected || s.localChronosphereMarker) return fail("completed chrono retained private loop");
            for (int id : targets) { const auto* object = findObjectById(s, id);
                if (!object || object->x < destination.x || object->x > destination.x + 1 || object->y < destination.y || object->y > destination.y + 1)
                    return fail("actual multi-unit transfer was not applied"); }
        } else if (power == "LightningStormSpecial") {
            if (!require(rule->startSound) || count(sounds, rule->specialSound) < 1) return fail("deferred storm/real strike sound missing");
            const int earliest = launchTick + runtimeRuleFramesToTicks(rule->defermentFrames, hz);
            for (const auto& r : sounds.records) if (r.section == rule->startSound && r.tick != earliest)
                return fail("storm cue did not wait original deferment: " + describe(r));
        } else if (power == "GeneticConverterSpecial") {
            if (!require(rule->startSound, static_cast<int>(targets.size()) + 1) || count(sounds, "InfantryMelt"))
                return fail("native GENDEATH StartSound priority/activation requests");
            if (!verifyMutationHealth(s, false, "natural-end", "original")
                || !ordinary || !exact || !verifyMutationHealth(*ordinary, false, "natural-end", "ordinary14")
                || !verifyMutationHealth(*exact, false, "natural-end", "ordinary14+canonical-extension")) return false;
        } else if (power == "PsychicDominatorSpecial") {
            if (!require(rule->startSound)) return false;
            const auto* victim = findObjectById(s, targets.front()); if (!victim || victim->owner != "Commander") return fail("actual domination absent");
        } else if (power == "ParaDropSpecial" || power == "AmericanParaDropSpecial") {
            const int troops = static_cast<int>(std::count_if(s.world.getObjects().begin(), s.world.getObjects().end(), [](const auto& o) {
                return o.alive && o.owner == "Commander" && o.kind == WorldObjectKind::Infantry && !o.airborne;
            }));
            if (troops == 0 || !require(s.rules.getAudioVisualValue("ChuteSound"), troops)) return fail("actual paradrop/chute sound count");
        } else if (power == "SpyPlaneSpecial" || power == "PsychicRevealSpecial") {
            if (power == "SpyPlaneSpecial") {
                const auto* plane = findRuntimeObjectByIdIncludingInactive(s, supportId);
                const int initialAmmo = supportType ? (supportType->initialAmmo < 0 ? supportType->ammo : supportType->initialAmmo) : 0;
                const int exposures = plane ? initialAmmo - plane->currentAmmo : 0;
                if (exposures < 1 || !require(rule->startSound, exposures)) return fail("real camera exposures did not consume matching native ammo/sounds");
            } else if (!rule->startSound.empty() && !require(rule->startSound)) return false;
            if (count(sounds, "WARPIN") || count(sounds, "ChronosphereMove")) return fail("silent recon generated unrelated activation");
        }
        if (supportType && !supportType->moveSound.empty()) {
            stage = "real support aircraft engine loop";
            const auto* spec = s.worldAudioCatalog.find(supportType->moveSound);
            if (!spec || !spec->loop || count(sounds, spec->eventId) != 0)
                return fail("aircraft loop was still emitted as a generic one-shot");
            if (std::count(intents.intents.begin(), intents.intents.end(), "sound:" + spec->eventId) != 1)
                return fail("aircraft launch did not retain exactly one semantic loop start");
            const auto auditEngine = [&](RuntimeWindowState& state, const ActionObserver& observer, bool restored) {
                std::vector<std::string> samples; bool stopped = false; int lastPlayTick = -1, queued = 0;
                const auto controls = toLowerAsciiLocal(state.soundIni.getValue(spec->eventId, "Control", ""));
                const bool attack = controls.find("attack") != std::string::npos && spec->samples.size() > 2;
                for (const auto& action : observer.actions) {
                    if (action.handleId != runtimeDiskMoveSoundHandle(supportId, 0)) continue;
                    if (action.kind == RuntimeWorldAudioActionKind::Stop && action.eventTick >= lastPlayTick && !samples.empty()) stopped = true;
                    if (action.kind != RuntimeWorldAudioActionKind::Play) continue;
                    lastPlayTick = action.eventTick; stopped = false;
                    const auto expected = supportPositions.find(action.eventTick);
                    if (action.eventId != spec->eventId || action.sourceObjectId != supportId || !action.queueAfterCurrent
                        || action.gainPermille <= 0 || expected == supportPositions.end()
                        || action.cell.x != static_cast<int>(std::lround(expected->second.x))
                        || action.cell.y != static_cast<int>(std::lround(expected->second.y))
                        || action.cell.level != static_cast<int>(std::lround(expected->second.level))
                        || std::find(spec->samples.begin(), spec->samples.end(), action.sampleName) == spec->samples.end())
                        return fail("managed engine sample lost actual aircraft/source/cell/queue: " + actionKey(action));
                    const auto spatial = runtimeSpatialAudioAt(state, spec->eventId, expected->second);
                    if (action.pan16384 != spatial.pan16384 || action.spatialGain16384 != spatial.gain16384)
                        return fail("managed aircraft sound did not track actual fractional flying position: " + actionKey(action));
                    if (restored && attack && action.sampleName == spec->samples.front()) return fail("flight restore replayed engine introduction");
                    if (!attack || action.sampleName != spec->samples.front()) samples.push_back(action.sampleName);
                    ++queued; ++supportLoopSamples;
                }
                // An off-screen ingress can be heard only briefly. Require
                // its real initial two-buffer queue and natural stop here;
                // all six following-listener scenes below require complete
                // authored body bags without shortening any original WAV.
                if (samples.empty() || queued < 2 || !stopped || state.diskMoveAudio.sources.contains(supportId))
                    return fail("support engine did not queue/stop naturally: body=" + std::to_string(samples.size())
                        + " queued=" + std::to_string(queued) + " stopped=" + std::to_string(stopped));
                const std::set<std::string> expectedBag(spec->samples.begin() + (attack ? 1 : 0), spec->samples.end());
                for (std::size_t start = 0; start + expectedBag.size() <= samples.size(); start += expectedBag.size())
                    if (std::set<std::string>(samples.begin() + start, samples.begin() + start + expectedBag.size()) != expectedBag)
                        return fail("aircraft Control=all random repeated/omitted a native body sample");
                return true;
            };
            if (!ordinary || !exact) return fail("no audible flight checkpoint was exercised");
            if (!auditEngine(s, engineActions, false)
                || !auditEngine(*ordinary, ordinaryEngine, true) || !auditEngine(*exact, exactEngine, true)) return false;
            ++supportLoopScenes; supportLoopResumes += 2;
        }
        for (const auto& r : sounds.records) if (relevant(r) && r.section != "ChronosphereSelect") {
            if (!r.playable || r.path.empty() || !r.spatial || !r.trackCamera || r.status == "missing" || r.status == "no-sample")
                return fail("activation was not a real positioned sample: " + describe(r));
            if (supportCase) {
                const auto expected = supportPositions.find(r.tick);
                if (expected == supportPositions.end() || r.sourcePosition.x != expected->second.x
                    || r.sourcePosition.y != expected->second.y || r.sourcePosition.level != expected->second.level)
                    return fail("aircraft ingress/camera/chute cue did not use its actual plane XYZ: " + describe(r));
            } else if (r.sourcePosition.x < 55 || r.sourcePosition.x > 124 || r.sourcePosition.y < 55 || r.sourcePosition.y > 124)
                return fail("actual source position outside native fixture: " + describe(r));
            if (r.section == "ChronosphereMove" && (r.sourcePosition.x != destination.x || r.sourcePosition.y != destination.y))
                return fail("destination sample positioned at source instead of destination: " + describe(r));
            if (r.section == "IronCurtainBlast" && (r.sourcePosition.x != target.x || r.sourcePosition.y != target.y))
                return fail("iron invocation duplicated at victim instead of clicked center: " + describe(r));
            if ((r.section == "NukeSiren" || r.section == "NukeIntro" || r.section == "NukeExplosion"
                || r.section == "ForceShieldStarting" || r.section == "ForceShieldFading" || r.section == "PsychicDominatorActivate")
                && (r.sourcePosition.x != target.x || r.sourcePosition.y != target.y || r.sourcePosition.level != 0))
                return fail("ground activation/global cue was moved away from its real clicked cell: " + describe(r));
            if (r.section == "NukeLaunch") {
                bool sourceMatched = false;
                for (const auto& object : s.world.getObjects()) {
                    const auto* building = s.rules.findBuildingType(object.type);
                    if (!building || (building->superWeapon != power && building->superWeapon2 != power)) continue;
                    const auto cell = runtimePlacementAnchorCellForFootprint({object.x, object.y}, runtimeStructureFootprintForObject(s, object));
                    sourceMatched = sourceMatched || (r.sourcePosition.x == cell.x && r.sourcePosition.y == cell.y && r.sourcePosition.level == object.level);
                }
                if (!sourceMatched) return fail("NUKETO StartSound did not originate at actual silo anchor: " + describe(r));
            }
            ++positionChecks;
        }
        if (ordinary) {
            const auto signature = [&](const auto& records, std::size_t start) {
                std::vector<std::string> result;
                for (std::size_t i = start; i < records.size(); ++i) if (relevant(records[i])) {
                    const auto& r = records[i]; std::ostringstream value; value << std::setprecision(17) << r.tick << '|' << r.section
                        << '|' << r.spatial << '|' << r.trackCamera << '|' << r.sourcePosition.x << '|' << r.sourcePosition.y << '|' << r.sourcePosition.level;
                    result.push_back(value.str());
                }
                return result;
            };
            const auto expected = signature(sounds.records, afterCheckpoint);
            if (signature(ordinarySounds.records, 0) != expected || signature(exactSounds.records, 0) != expected)
                return fail("ordinary/canonical continuation replayed, skipped, or moved future activation sound");
        }
        if (!outputRoot.empty()) {
            std::ofstream log(outputRoot + '/' + power + '-' + std::to_string(hz) + "-sounds.txt");
            for (const auto& record : sounds.records) log << describe(record) << '\n';
        }
        ++activations; return true;
    });
    for (int hz : {30, 60}) for (const std::string power : {"ParaDropSpecial", "AmericanParaDropSpecial", "SpyPlaneSpecial"})
        scene("audible-aircraft-loop-" + power + '-' + std::to_string(hz), [&]() {
        auto storage = make(hz, power == "SpyPlaneSpecial" ? "Russians" : "Americans"); auto& s = *storage;
        RuntimeSnapshotActiveStateScope active(s); const auto* rule = original.findSuperWeaponType(power);
        const auto* unit = rule ? original.findUnitType("AircraftType", rule->planeType) : nullptr;
        const auto* spec = unit ? s.worldAudioCatalog.find(unit->moveSound) : nullptr;
        if (!rule || !unit || !spec || !spec->loop) return fail("original aircraft loop specification");
        if (!begin(s, power)) return false; click(s, {94, 94});
        // Pan before the queued launch. The actual legal map edge supplies
        // only the listener location; launch/flight authority stays untouched.
        const auto context = runtimeSuperWeaponContext(s);
        int leftEdge = 0, rightEdge = std::max(0, context.mapWidth - 1);
        if (context.cellLegal) {
            while (leftEdge < rightEdge && !context.cellLegal(leftEdge, 94, SuperWeaponTargetMode::Cell)) ++leftEdge;
            while (rightEdge > leftEdge && !context.cellLegal(rightEdge, 94, SuperWeaponTargetMode::Cell)) --rightEdge;
        }
        centerRuntimeCameraOnCell(s, client, {94 <= (leftEdge + rightEdge) / 2 ? leftEdge : rightEdge, 94});
        ensureRuntimeCameraForViewport(s, viewport);
        ActionObserver observer; collectActions(s, observer); observer.actions.clear();
        ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); verifyBattleContinues(s);
        int planeId = -1;
        for (const auto& object : s.world.getObjects()) if (object.alive && object.type == rule->planeType) { planeId = object.instanceId; break; }
        const auto* plane = findObjectById(s, planeId);
        if (!plane) return fail("real UI launch did not produce original support aircraft");
        const auto follow = [&](const WorldObjectInstance& object) {
            centerRuntimeCameraOnCell(s, client, {object.x, object.y}); ensureRuntimeCameraForViewport(s, viewport);
        };
        follow(*plane); advanceRuntimeDiskMoveAudio(s, presentationNow(s)); collectActions(s, observer);
        const auto controls = toLowerAsciiLocal(s.soundIni.getValue(spec->eventId, "Control", ""));
        const bool attack = controls.find("attack") != std::string::npos && spec->samples.size() > 2;
        const auto source = s.diskMoveAudio.sources.find(planeId);
        if (source == s.diskMoveAudio.sources.end() || !source->second.active || source->second.ordinal != 2)
            return fail("audible plane did not prequeue its native intro/body");
        int previousOrdinal = source->second.ordinal; bool departed = false;
        stage = "actual aircraft flight and complete native sample bags";
        for (int i = 0; i < hz * 20; ++i) {
            // Keep the normal simulation/presentation ordering, but capture
            // the listener used by this audio pass before the camera follows
            // the newly committed flight pose.
            ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s); verifyBattleContinues(s);
            const auto* beforeFollow = findObjectById(s, planeId);
            const int gainBeforeFollow = beforeFollow ? runtimeSpatialAudioGainPermille(s, *spec,
                runtimeSpatialAudioPositionForObject(*beforeFollow)) : -1;
            const auto previousCamera = s.camera;
            advanceRuntimeDiskMoveAudio(s, presentationNow(s)); collectActions(s, observer);
            if (const auto* current = findObjectById(s, planeId)) follow(*current);
            else { departed = true; break; }
            const auto loop = s.diskMoveAudio.sources.find(planeId);
            if (loop == s.diskMoveAudio.sources.end() || !loop->second.active || loop->second.ordinal < previousOrdinal) {
                std::ostringstream detail; detail << std::setprecision(17)
                    << "camera-follow flight lost or restarted its managed engine tick=" << s.tick
                    << " iteration=" << i << " sourceExists=" << (loop != s.diskMoveAudio.sources.end())
                    << " previousOrdinal=" << previousOrdinal << " nowMs=" << presentationNow(s)
                    << " preFollowGain=" << gainBeforeFollow
                    << " preCamera=" << previousCamera.worldOffsetX << ',' << previousCamera.worldOffsetY
                    << " postCamera=" << s.camera.worldOffsetX << ',' << s.camera.worldOffsetY
                    << " listenerViewport=" << s.cameraClampViewport.left << ',' << s.cameraClampViewport.top << ','
                    << s.cameraClampViewport.right << ',' << s.cameraClampViewport.bottom;
                if (const auto* current = findObjectById(s, planeId)) {
                    const auto pose = runtimeSpatialAudioPositionForObject(*current);
                    const auto projected = runtimeProjectCellXY(pose.x, pose.y, pose.level, s.camera);
                    detail << " type=" << current->type << " owner=" << current->owner << " mission=" << current->mission
                        << " alive=" << current->alive << " health=" << current->health
                        << " hasRender=" << current->hasRenderPosition << " moving=" << current->renderMoveActive
                        << " cellXYZ=" << current->x << ',' << current->y << ',' << current->level
                        << " renderXYZ=" << current->renderX << ',' << current->renderY << ',' << current->renderLevel
                        << " postFollowGain=" << runtimeSpatialAudioGainPermille(s, *spec, pose)
                        << " projected=" << projected.x << ',' << projected.y
                        << " screen=" << projected.x - s.camera.worldOffsetX << ',' << projected.y - s.camera.worldOffsetY
                        << " limbo=" << runtimeGateObjectIsLimbo(*current)
                        << " loopLegal=" << runtimeDiskMoveSoundIsLoop(s, *unit);
                    const auto behavior = s.behaviors.find(planeId);
                    if (behavior != s.behaviors.end()) detail << " behavior=" << int(behavior->second.kind)
                        << ':' << int(behavior->second.state) << " path=" << behavior->second.path.size();
                }
                if (loop != s.diskMoveAudio.sources.end()) detail << " active=" << loop->second.active
                    << " ordinal=" << loop->second.ordinal << " stage=" << loop->second.stage
                    << " owner=" << loop->second.owner << " event=" << loop->second.event
                    << " gain=" << loop->second.gainPermille << " nextMs=" << loop->second.nextSampleMilliseconds
                    << " queuedUntil=" << loop->second.queuedUntilMilliseconds;
                for (const auto& action : s.runtimeWorldAudioBackendActionHistory)
                    if (action.handleId == runtimeDiskMoveSoundHandle(planeId, 0)
                        || action.handleId == runtimeDiskMoveSoundHandle(planeId, 1))
                        detail << " action=[" << actionKey(action) << " source=" << action.sourceObjectId
                            << " reason=" << action.reason << ']';
                return fail(detail.str());
            }
            previousOrdinal = loop->second.ordinal;
        }
        if (!departed || s.diskMoveAudio.sources.contains(planeId)) return fail("departed support aircraft retained its engine source");
        std::vector<std::string> bodies; int attacks = 0; bool stopped = false;
        for (const auto& action : observer.actions) {
            if (action.handleId != runtimeDiskMoveSoundHandle(planeId, 0)) continue;
            if (action.kind == RuntimeWorldAudioActionKind::Stop && !bodies.empty()) stopped = true;
            if (action.kind != RuntimeWorldAudioActionKind::Play) continue;
            stopped = false;
            if (action.eventId != spec->eventId || action.sourceObjectId != planeId || !action.queueAfterCurrent || action.gainPermille <= 0)
                return fail("audible loop lost actual plane ownership/queued buffer: " + actionKey(action));
            if (attack && action.sampleName == spec->samples.front()) ++attacks;
            else bodies.push_back(action.sampleName);
            ++supportLoopSamples;
        }
        if (attacks != (attack ? 1 : 0) || bodies.size() < spec->samples.size() - (attack ? 1 : 0) || !stopped)
            return fail("native audible attack/bag/departure contract: attack=" + std::to_string(attacks)
                + " body=" + std::to_string(bodies.size()) + " stopped=" + std::to_string(stopped));
        const std::set<std::string> expected(spec->samples.begin() + (attack ? 1 : 0), spec->samples.end());
        for (std::size_t start = 0; start + expected.size() <= bodies.size(); start += expected.size())
            if (std::set<std::string>(bodies.begin() + start, bodies.begin() + start + expected.size()) != expected)
                return fail("original all/random aircraft loop bag repeated or omitted a body");
        ++supportLoopScenes; return true;
    });
    const auto selectedPlay = [](const RuntimeWorldAudioAction& a) {
        return a.kind == RuntimeWorldAudioActionKind::Play
            && (a.handleId == 0xD000000000000001ULL || a.handleId == 0xD000000000000002ULL);
    };
    for (int hz : {30, 60}) scene("private-Chronosphere-sequence-" + std::to_string(hz), [&]() {
        auto storage = make(hz); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
        add(s, "MTNK", WorldObjectKind::Unit, {94, 94}, "Commander");
        if (!begin(s, "ChronoSphereSpecial")) return false;
        ActionObserver observer; collectActions(s, observer); observer.actions.clear();
        const auto* spec = s.worldAudioCatalog.find("ChronosphereSelect");
        if (!spec || !spec->loop || spec->samples.size() != 5) return fail("original five-sample attack/all/random/loop/decay config");
        const auto control = toLowerAsciiLocal(s.soundIni.getValue(spec->eventId, "Control", ""));
        for (const auto* token : {"all", "random", "loop", "attack", "decay"})
            if (control.find(token) == std::string::npos) return fail(std::string("original Control missing ") + token);
        stage = "first real click"; click(s, {94, 94}); collectActions(s, observer);
        if (!s.localChronosphereMarker || !s.chronosphereSelectionAudio.selected || !s.chronosphereSelectionAudio.sound.active)
            return fail("private marker did not start its managed sequence");
        const auto countSample = [&](const std::string& sample) {
            return std::count_if(observer.actions.begin(), observer.actions.end(), [&](const auto& a) {
                return selectedPlay(a) && a.sampleName == sample;
            });
        };
        if (countSample(spec->samples.front()) != 1 || s.chronosphereSelectionAudio.sound.ordinal != 2)
            return fail("attack was not played once with one body already queued");
        const auto marker = s.localChronosphereMarker->animation;
        const int ordinal = s.chronosphereSelectionAudio.sound.ordinal;
        const auto queued = s.chronosphereSelectionAudio.sound.queuedUntilMilliseconds;
        startRuntimeChronosphereSelectionAudio(s, marker, queued); collectActions(s, observer);
        if (s.chronosphereSelectionAudio.sound.ordinal != ordinal || countSample(spec->samples.front()) != 1)
            return fail("duplicate first-target presentation restarted attack");
        const auto beforeHash = runtimeStandaloneSimulationStateHash(s, error);
        if (!beforeHash) return fail("pre-sequence authority hash: " + error);
        stage = "native sample clock and complete random bag";
        for (int i = 0; i < 8; ++i) {
            const auto boundary = s.chronosphereSelectionAudio.sound.nextSampleMilliseconds;
            advanceRuntimeChronosphereSelectionAudio(s, boundary); collectActions(s, observer);
        }
        if (runtimeStandaloneSimulationStateHash(s, error) != beforeHash) return fail("private wall-clock sound changed gameplay authority");
        std::vector<std::string> bodySamples;
        for (const auto& a : observer.actions) if (selectedPlay(a) && a.handleId == 0xD000000000000001ULL
            && a.sampleName != spec->samples.front()) bodySamples.push_back(a.sampleName);
        if (bodySamples.size() < 6) return fail("loop stopped after a single body segment");
        const std::set<std::string> expectedBag(spec->samples.begin() + 1, spec->samples.end() - 1);
        for (std::size_t start = 0; start + expectedBag.size() <= bodySamples.size(); start += expectedBag.size()) {
            std::set<std::string> bag(bodySamples.begin() + start, bodySamples.begin() + start + expectedBag.size());
            if (bag != expectedBag) return fail("Control=all random omitted/repeated a loop sample within its bag");
        }
        for (const auto& a : observer.actions) if (selectedPlay(a)) {
            if (a.cell.x != 94 || a.cell.y != 94 || a.cell.level != terrainLevelAtRuntimeCell(s, 94, 94)
                || a.gainPermille <= 0 || a.frequencyPermille <= 0 || !a.queueAfterCurrent)
                return fail("selected voice was not owned/positioned/prequeued at the source cell");
        }
        const auto pausedOrdinal = s.chronosphereSelectionAudio.sound.ordinal;
        const auto pausedClock = s.chronosphereSelectionAudio.sound.nextSampleMilliseconds;
        advanceRuntimeChronosphereSelectionAudio(s, pausedClock - 1); collectActions(s, observer);
        if (s.chronosphereSelectionAudio.sound.ordinal != pausedOrdinal) return fail("same sample interval or pause duplicated a buffer");
        stage = "mute and private viewer boundaries";
        activateRuntimeMenuAction(nullptr, s, RuntimeMenuAction::ToggleSound);
        advanceRuntimeChronosphereSelectionAudio(s, pausedClock); collectActions(s, observer);
        if (!s.chronosphereSelectionAudio.selected || s.chronosphereSelectionAudio.sound.active)
            return fail("mute retained active voice or lost the pending target");
        activateRuntimeMenuAction(nullptr, s, RuntimeMenuAction::ToggleSound);
        advanceRuntimeChronosphereSelectionAudio(s, pausedClock); collectActions(s, observer);
        if (!s.chronosphereSelectionAudio.sound.active || countSample(spec->samples.front()) != 1)
            return fail("unmute failed to resume body or replayed attack");
        s.economy.owner = "Ally"; advanceRuntimeChronosphereSelectionAudio(s, pausedClock); collectActions(s, observer);
        if (s.chronosphereSelectionAudio.sound.active || !s.chronosphereSelectionAudio.selected)
            return fail("another listener inherited caster-only select voice");
        s.economy.owner = "Commander"; advanceRuntimeChronosphereSelectionAudio(s, pausedClock); collectActions(s, observer);
        if (!s.chronosphereSelectionAudio.sound.active || countSample(spec->samples.front()) != 1)
            return fail("returning caster replayed attack or failed to resume loop");
        const auto remoteOrdinal = s.chronosphereSelectionAudio.sound.ordinal;
        s.remoteHumanHouseScopeActive = true; s.economy.owner = "Enemy";
        advanceRuntimeChronosphereSelectionAudio(s, pausedClock + 100000); collectActions(s, observer);
        if (s.chronosphereSelectionAudio.sound.ordinal != remoteOrdinal || !s.chronosphereSelectionAudio.sound.active)
            return fail("remote simulation scope changed local playback clock");
        s.remoteHumanHouseScopeActive = false; s.economy.owner = "Commander";
        s.runtimeSuppressPlayerAudioFeedback = true;
        advanceRuntimeChronosphereSelectionAudio(s, pausedClock); collectActions(s, observer);
        if (s.chronosphereSelectionAudio.sound.active) return fail("suppressed local feedback left private voice playing");
        s.runtimeSuppressPlayerAudioFeedback = false;
        advanceRuntimeChronosphereSelectionAudio(s, pausedClock); collectActions(s, observer);
        if (countSample(spec->samples.front()) != 1) return fail("feedback resume replayed attack");
        stage = "private selection save contract";
        auto ordinary = checkpoint(s, false), exact = checkpoint(s, true);
        if (!ordinary || !exact) return fail("selection checkpoint: " + error);
        for (auto* restored : {ordinary.get(), exact.get()}) {
            advanceRuntimeChronosphereSelectionAudio(*restored, pausedClock);
            if (restored->localChronosphereMarker || restored->chronosphereSelectionAudio.selected
                || std::any_of(restored->runtimeWorldAudioBackendActionHistory.begin(), restored->runtimeWorldAudioBackendActionHistory.end(), selectedPlay))
                return fail("unfinished private command/attack leaked through a save");
        }
        ++saves; ++snapshots;
        stage = "real right-click cancel and finite decay";
        handleRuntimeMapRightClick(s, client, runtimeScreenPointForCell(s, viewport, {98, 96}, 0));
        collectActions(s, observer);
        if (s.localChronosphereMarker || s.chronosphereSelectionAudio.selected || s.multiplayerIntentRouter.superWeapon())
            return fail("right-click did not cancel the selected target and its loop");
        if (countSample(spec->samples.back()) != 1 || s.chronosphereSelectionAudio.sound.stage != "decay")
            return fail("cancel did not request exactly one original last decay sample");
        stopRuntimeChronosphereSelectionAudio(s); collectActions(s, observer);
        if (countSample(spec->samples.back()) != 1 || !s.chronosphereSelectionAudio.sound.active)
            return fail("duplicate cancellation restarted or cut the same decay");
        advanceRuntimeChronosphereSelectionAudio(s, s.chronosphereSelectionAudio.sound.nextSampleMilliseconds);
        collectActions(s, observer);
        if (s.chronosphereSelectionAudio.sound.active || s.chronosphereSelectionAudio.selected)
            return fail("decay never ended on its actual WAV deadline");
        // Source loss is an actual authoritative exit; no audio object is
        // cleared manually. The next normal simulation tick invalidates it.
        if (!begin(s, "ChronoSphereSpecial")) return false; click(s, {94, 94});
        const auto source = std::find_if(s.world.getObjects().begin(), s.world.getObjects().end(), [&](const auto& o) {
            const auto* building = s.rules.findBuildingType(o.type);
            return o.alive && building && (building->superWeapon == "ChronoSphereSpecial" || building->superWeapon2 == "ChronoSphereSpecial");
        });
        if (source == s.world.getObjects().end()) return fail("actual chronosphere source");
        RuntimeObjectExitRequest exit; exit.objectId = source->instanceId; exit.kind = RuntimeObjectExitKind::Destroyed;
        exit.tick = s.tick; exit.suppressCrew = exit.suppressDeathWeapon = true;
        if (!finalizeRuntimeObjectExit(s, exit).accepted) return fail("source destruction exit");
        step(s); advanceRuntimeChronosphereSelectionAudio(s, GetTickCount64());
        if (s.chronosphereSelectionAudio.selected) return fail("destroyed source retained selection loop");
        selectionBoundaries += 12; return true;
    });
    // Independent original projection/range arithmetic verifies the runtime
    // backend receives authored global/local attenuation and camera pan.
    for (int hz : {30, 60}) for (const std::string event : {"ChronosphereMove", "ChronosphereSelect", "IronCurtainBlast", "ForceShieldStarting"})
        scene("spatial-" + event + '-' + std::to_string(hz), [&]() {
        auto storage = make(hz); auto& s = *storage; const auto* spec = s.worldAudioCatalog.find(event);
        if (!spec) return fail("original sound spec");
        const RuntimeSpatialAudioPosition source{94, 94, 0};
        s.cameraClampViewport = {0, 0, 640, 400}; s.camera.initialized = true;
        const auto projected = runtimeProjectCellXY(source.x, source.y, source.level, s.camera);
        for (int volume : {100, 35, 0}) for (const POINT relative : {POINT{320, 200}, POINT{20, 200}, POINT{620, 200}, POINT{4000, 200}}) {
            s.runtimeSoundVolume = volume; s.camera.worldOffsetX = projected.x - relative.x; s.camera.worldOffsetY = projected.y - relative.y;
            const std::size_t before = s.runtimeSoundPlaybackHistory.size();
            emitRuntimeSound(s, "sound:" + event, source, true);
            if (s.runtimeSoundPlaybackHistory.empty()) return fail("sound record absent");
            const auto& r = s.runtimeSoundPlaybackHistory.back();
            // NORMAL sounds have no attenuation inside the view rectangle;
            // GLOBAL sounds clamp to their authored MinVolume outside range.
            double dx = std::abs(double(relative.x) - 320), dy = std::abs(double(relative.y) - 200);
            if (!spec->spatial.local) { dx = std::max(0.0, dx - 320); dy = std::max(0.0, dy - 200); }
            dy *= 2; const double range = spec->spatial.rangeCells * 60.0 * s.camera.tileWidth / 60.0;
            double gain = range > 0 && dx < range && dy < range ? (range - std::max(dx, dy)) / range : 0;
            if (spec->spatial.global) gain = std::max(gain, spec->spatial.minimumVolumePercent / 100.0);
            if (gain < .05) gain = 0;
            const int expectedSpatial = static_cast<int>(gain * 16384);
            const int expectedPan = std::clamp(static_cast<int>(std::clamp(double(relative.x) - 320, -640.0, 640.0) * 8192 / 640 + 8192), 0, 16384);
            const std::int64_t authoredQ14 = std::clamp(r.authoredGainPermille, 0, 1000) * 16384 / 1000;
            const int userQ14 = volume * 16384 / 100;
            const int expectedGain = static_cast<int>((authoredQ14 * expectedSpatial / 16384) * userQ14 / 16384);
            if (!r.playable || !r.spatial || r.sourcePosition.x != 94 || r.sourcePosition.y != 94
                || r.spatialGain16384 != expectedSpatial || r.pan16384 != expectedPan || r.gain16384 != expectedGain)
                return fail("original source/camera/volume mismatch: " + describe(r) + " expected=" + std::to_string(expectedGain)
                    + '/' + std::to_string(expectedSpatial) + '/' + std::to_string(expectedPan) + " oldHistory=" + std::to_string(before));
            if (!spec->spatial.global && relative.x == 4000 && (!r.suppressed || r.status != "spatial-inaudible"))
                return fail("out-of-range local selection did not become silent");
            if (spec->spatial.global && relative.x == 4000 && r.spatialGain16384 <= 0)
                return fail("global SW cue lost original minimum volume");
            ++positionChecks;
        }
        return true;
    });
    for (const std::string power : {"ParaDropSpecial", "SpyPlaneSpecial"})
        scene("native-aircraft-loop-Limit3-" + power, [&]() {
        auto storage = make(30, power == "SpyPlaneSpecial" ? "Russians" : "Americans"); auto& s = *storage;
        RuntimeSnapshotActiveStateScope active(s);
        const auto* rule = s.rules.findSuperWeaponType(power);
        const auto* unit = rule ? s.rules.findUnitType("AircraftType", rule->planeType) : nullptr;
        const auto* spec = unit ? s.worldAudioCatalog.find(unit->moveSound) : nullptr;
        if (!rule || !unit || !spec || !spec->loop || spec->simultaneousLimit != 3)
            return fail("original aircraft Control=loop/Limit=3 fixture");
        // The real no-cooldown option detaches each accepted cast. It leaves
        // original aircraft, sample and flight rules intact, and prevents a
        // forceReady of an Active normal cast from cancelling its old plane.
        s.config.noCooldown = true;
        s.runtimeSoundsEnabled = true; s.runtimeVoiceAudio = std::make_unique<RuntimeVoiceAudio>(true);
        if (!s.runtimeVoiceAudio->ready()) return fail("real muted XAudio device unavailable");
        std::vector<int> planeIds; SoundObserver sounds; ActionObserver actions;
        prime(s, sounds); collectActions(s, actions); actions.actions.clear();
        const auto listenAtIngress = [&]() {
            const auto context = runtimeSuperWeaponContext(s);
            int leftEdge = 0, rightEdge = std::max(0, context.mapWidth - 1);
            if (context.cellLegal) {
                while (leftEdge < rightEdge && !context.cellLegal(leftEdge, 94, SuperWeaponTargetMode::Cell)) ++leftEdge;
                while (rightEdge > leftEdge && !context.cellLegal(rightEdge, 94, SuperWeaponTargetMode::Cell)) --rightEdge;
            }
            centerRuntimeCameraOnCell(s, client, {94 <= (leftEdge + rightEdge) / 2 ? leftEdge : rightEdge, 94});
            ensureRuntimeCameraForViewport(s, viewport);
        };
        const auto voiceSet = [&]() {
            std::set<int> playing;
            for (const auto& [handle, binding] : s.runtimeWorldAudioBackendBindings) {
                if (binding.action.eventId != spec->eventId || !s.runtimeVoiceAudio->isPlaying(handle).value_or(false)) continue;
                const auto probe = s.runtimeVoiceAudio->probe(handle);
                if (binding.action.sourceObjectId < 0 || handle != runtimeDiskMoveSoundHandle(binding.action.sourceObjectId, 0)
                    || !probe.available || probe.masterVolume != 0 || probe.buffersQueued == 0) {
                    error = "invalid actual managed engine voice/device"; return std::set<int>{-1};
                }
                playing.insert(binding.action.sourceObjectId);
            }
            return playing;
        };
        stage = "four real sidebar/map casts and native replacement";
        for (int cast = 0; cast < 4; ++cast) {
            if (!begin(s, power)) return false;
            click(s, {94, 94});
            if (s.multiplayerIntentRouter.superWeapon()) return fail("concurrent real target click was rejected: " + s.interactionStatus);
            listenAtIngress(); step(s); collect(s, sounds); collectActions(s, actions);
            std::vector<int> created;
            for (const auto& object : s.world.getObjects())
                if (object.alive && object.type == rule->planeType
                    && std::find(planeIds.begin(), planeIds.end(), object.instanceId) == planeIds.end()) created.push_back(object.instanceId);
            if (created.size() != 1) return fail("real concurrent launch did not create exactly one original plane");
            planeIds.push_back(created.front());
            const auto* plane = findObjectById(s, planeIds.back());
            if (!plane || runtimeSpatialAudioGainPermille(s, *spec, runtimeSpatialAudioPositionForObject(*plane)) <= 0)
                return fail("launch listener was outside the actual aircraft's hearing range");
            const std::set<int> expected(planeIds.begin() + std::max(0, cast - 2), planeIds.end());
            if (voiceSet() != expected) return fail("native Limit3 did not keep the newest actual aircraft voices after cast " + std::to_string(cast + 1));
        }
        if (count(sounds, spec->eventId) != 0) return fail("managed engine also created generic one-shot voices");
        const auto oldHandle = runtimeDiskMoveSoundHandle(planeIds.front(), 0);
        const auto oldSource = s.diskMoveAudio.sources.find(planeIds.front());
        if (oldSource == s.diskMoveAudio.sources.end() || !s.runtimeWorldAudioLimitEvictedHandles.contains(oldHandle)
            || s.runtimeWorldAudioBackendBindings.contains(oldHandle) || s.runtimeVoiceAudio->isPlaying(oldHandle).value_or(false))
            return fail("oldest engine was not actually evicted by the fourth voice");
        const int evictedOrdinal = oldSource->second.ordinal;
        const auto actionsBefore = actions.actions.size(); const int frozenTick = s.tick;
        stage = "evicted moving engine cannot reclaim a voice";
        for (int frame = 0; frame < 12; ++frame) {
            Sleep(20); advanceRuntimeDiskMoveAudio(s, GetTickCount64()); collectActions(s, actions);
            const auto old = s.diskMoveAudio.sources.find(planeIds.front());
            if (s.tick != frozenTick || old == s.diskMoveAudio.sources.end() || old->second.ordinal != evictedOrdinal
                || voiceSet() != std::set<int>(planeIds.begin() + 1, planeIds.end()))
                return fail("evicted source reclaimed another engine during the same real motor run");
        }
        for (std::size_t i = actionsBefore; i < actions.actions.size(); ++i)
            if (actions.actions[i].kind == RuntimeWorldAudioActionKind::Play && actions.actions[i].handleId == oldHandle)
                return fail("evicted source requested a new body buffer before leaving hearing range");
        stage = "four original flights and troops finish naturally";
        bool finished = false;
        for (int i = 0; i < s.config.fixedTickHz * 130; ++i) {
            step(s); collect(s, sounds); collectActions(s, actions);
            if (voiceSet().size() > 3 || voiceSet().contains(-1)) return fail("support engine exceeded native simultaneous voice limit");
            const bool airborne = std::any_of(s.world.getObjects().begin(), s.world.getObjects().end(), [&](const auto& object) {
                return object.alive && (std::find(planeIds.begin(), planeIds.end(), object.instanceId) != planeIds.end()
                    || (object.kind == WorldObjectKind::Infantry && object.airborne));
            });
            if (!airborne) { finished = true; break; }
        }
        if (!finished) return fail("original concurrent support flights/paratroopers did not finish");
        for (int id : planeIds) {
            const auto handle = runtimeDiskMoveSoundHandle(id, 0);
            if (s.diskMoveAudio.sources.contains(id) || s.runtimeWorldAudioBackendBindings.contains(handle)
                || s.runtimeWorldAudioLimitEvictedHandles.contains(handle) || s.runtimeVoiceAudio->isPlaying(handle).value_or(false))
                return fail("natural aircraft departure retained its engine voice/source/eviction marker");
        }
        ++nativeChecks; return true;
    });
    scene("native-GENDEATH-Limit1", [&]() {
        auto storage = make(30); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
        for (const auto cell : {RuntimeCellPoint{94, 94}, RuntimeCellPoint{95, 94}, RuntimeCellPoint{94, 95}})
            add(s, "E1", WorldObjectKind::Infantry, cell, "Enemy");
        if (!begin(s, "GeneticConverterSpecial")) return false;
        s.runtimeVoiceAudio = std::make_unique<RuntimeVoiceAudio>(true); s.runtimeSoundsEnabled = true;
        if (!s.runtimeVoiceAudio->ready()) return fail("real muted XAudio device unavailable");
        SoundObserver sounds; prime(s, sounds); click(s, {94, 94}); collect(s, sounds); step(s); collect(s, sounds);
        const auto* spec = s.worldAudioCatalog.find("GeneticMutatorActivate");
        if (!spec || spec->simultaneousLimit != 1 || count(sounds, spec->eventId) != 4)
            return fail("native power+three GENDEATH requests/Limit1 fixture");
        int activeVoices = 0, accepted = 0; RuntimeSpatialAudioPosition lastPosition{};
        for (const auto& r : sounds.records) if (r.section == spec->eventId) {
            if (!r.playable || !r.played || r.status != "played") return fail("native overflow must replace the old voice, not discard an authored request: " + describe(r));
            ++accepted; lastPosition = r.sourcePosition;
        }
        for (std::size_t channel = 0; channel < runtimeSfxChannelCount; ++channel)
            if (s.runtimeSfxSpatialRecords[channel].section == spec->eventId
                && s.runtimeVoiceAudio->isPlaying(runtimeSfxVoiceHandle(channel)).value_or(false)) {
                ++activeVoices; const auto probe = s.runtimeVoiceAudio->probe(runtimeSfxVoiceHandle(channel));
                if (!probe.available || probe.masterVolume != 0 || probe.buffersQueued == 0)
                    return fail("limit verification device was audible or not actually queued");
                const auto& r = s.runtimeSfxSpatialRecords[channel];
                if (r.sourcePosition.x != lastPosition.x || r.sourcePosition.y != lastPosition.y || r.sourcePosition.level != lastPosition.level)
                    return fail("Limit1 kept an old position instead of replacing it with the newest native request");
            }
        if (accepted != 4 || activeVoices != 1) return fail("native Limit1 accepted=" + std::to_string(accepted) + " simultaneous=" + std::to_string(activeVoices));
        // Allow the original WAV to finish; do not stop a voice to satisfy the
        // finite sound test. Only resource disposal occurs after natural end.
        const auto deadline = GetTickCount64() + 15000;
        while (GetTickCount64() < deadline) {
            bool playing = false;
            for (std::size_t channel = 0; channel < runtimeSfxChannelCount; ++channel)
                if (s.runtimeSfxSpatialRecords[channel].section == spec->eventId)
                    playing = playing || s.runtimeVoiceAudio->isPlaying(runtimeSfxVoiceHandle(channel)).value_or(false);
            if (!playing) break; Sleep(20);
        }
        for (std::size_t channel = 0; channel < runtimeSfxChannelCount; ++channel)
            if (s.runtimeSfxSpatialRecords[channel].section == spec->eventId
                && s.runtimeVoiceAudio->isPlaying(runtimeSfxVoiceHandle(channel)).value_or(false)) return fail("original finite activation WAV did not end");
        ++nativeChecks; return true;
    });
    scene("native-private-Chronosphere-continuity", [&]() {
        auto storage = make(30); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
        add(s, "MTNK", WorldObjectKind::Unit, {94, 94}, "Commander");
        if (!begin(s, "ChronoSphereSpecial")) return false;
        s.runtimeSoundsEnabled = true; s.runtimeVoiceAudio = std::make_unique<RuntimeVoiceAudio>(true);
        if (!s.runtimeVoiceAudio->ready()) return fail("real muted XAudio device unavailable");
        const auto started = GetTickCount64(); click(s, {94, 94}); const auto handle = 0xD000000000000001ULL;
        if (!s.chronosphereSelectionAudio.selected || s.runtimeVoiceAudio->queuedBuffers(handle) != 2)
            return fail("native intro/body were not prequeued in one voice");
        std::uint64_t samples = 0; const int tick = s.tick;
        while (GetTickCount64() - started < 3600) {
            Sleep(20); advanceRuntimeChronosphereSelectionAudio(s, GetTickCount64());
            const auto probe = s.runtimeVoiceAudio->probe(handle);
            if (!probe.available || probe.buffersQueued == 0 || probe.samplesPlayed < samples || probe.masterVolume != 0)
                return fail("managed attack/body sequence interrupted or produced audible verification output");
            samples = probe.samplesPlayed;
        }
        if (samples == 0 || s.tick != tick || s.chronosphereSelectionAudio.sound.ordinal <= 2)
            return fail("paused simulation did not continue the real WAV sequence");
        handleRuntimeMapRightClick(s, client, runtimeScreenPointForCell(s, viewport, {98, 96}, 0));
        if (s.runtimeVoiceAudio->isPlaying(handle).value_or(true) || s.chronosphereSelectionAudio.selected)
            return fail("real cancel did not stop body voice");
        const auto decayHandle = 0xD000000000000002ULL;
        if (!s.runtimeVoiceAudio->isPlaying(decayHandle).value_or(false)) return fail("real cancel did not play original decay voice");
        const auto deadline = GetTickCount64() + 15000;
        while (GetTickCount64() < deadline && s.chronosphereSelectionAudio.sound.active) {
            Sleep(20); advanceRuntimeChronosphereSelectionAudio(s, GetTickCount64());
        }
        if (s.chronosphereSelectionAudio.sound.active || s.runtimeVoiceAudio->isPlaying(decayHandle).value_or(false))
            return fail("original finite decay WAV did not end naturally");
        ++nativeChecks; return true;
    });
    if (caseFilter.empty() || caseFilter.rfind("GeneticPosition-", 0) == 0) {
        if (!runRuntimeGeneticMutationPositionSmoke(live, error)) return false;
        if (!caseFilter.empty()) return true;
    }
    if (!caseFilter.empty() && scenes == 0 && failures.empty()) return fail("requested case was not found: " + caseFilter);
    if (!failures.empty()) {
        std::ostringstream out; out << "SW activation audio: " << failures.size() << " failed scenes";
        for (const auto& failure : failures) out << '\n' << failure; error = out.str(); return false;
    }
    std::cout << "SW activation audio passed: " << scenes << " scenes, " << activations << " real sidebar activations, "
        << decoded << " decoded stock samples, " << positionChecks << " physical positions, " << saves << '+' << snapshots
        << " ordinary/canonical checkpoints, " << pairedTicks << " paired ticks, " << selectionBoundaries
        << " selection boundaries, " << supportLoopScenes << " real support loop scenes/" << supportLoopSamples
        << " engine buffers/" << supportLoopResumes << " restored loops, " << nativeChecks << " muted device checks\n";
    return true;
}

#include "simulation/superweapons/diagnostics/RuntimeGeneticMutationPositionSmoke.inl"
