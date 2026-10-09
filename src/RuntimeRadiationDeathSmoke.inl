// Real damage entry points and stock mounted SHPs, including the last pulse
// whose radiation site has already been removed before the host sees damage.
bool runRuntimeRadiationDeathSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string scene;
    const auto fail = [&](const std::string& why) {
        error = "Radiation death: " + scene + ": " + why; return false;
    };
    IniFile stockIni; RulesDatabase stockRules;
    if (!stockIni.loadFromFile(live.report.rulesPath) || !stockRules.loadFromIni(stockIni))
        return fail("stock rules unavailable");
    for (const char* name : {"RadBeamWarhead", "RadEruptionWarhead", "RadSite"}) {
        const auto* warhead = stockRules.findWarheadType(name);
        if (!warhead || warhead->infDeath != 7 || !warhead->radiation)
            return fail(std::string(name) + " stock InfDeath=7 declaration missing");
    }
    const auto* meltArt = live.art.findArtType("NUKEDIE");
    if (stockRules.getInfantryDeathAnim(7) != "NUKEDIE" || !meltArt
        || meltArt->reportSound != "InfantryMelt") return fail("stock melt art/sound missing");
    const char* output = std::getenv("CONCORD_RADIATION_DEATH_OUTPUT");
    const std::string root = output ? output : "";
    if (!root.empty()) std::filesystem::create_directories(root);
    int cases = 0, saves = 0, captures = 0, frameChecks = 0, snapshots = 0;
    std::vector<std::string> failures;
    const auto fixture = [&](int hz) {
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
        seedRuntimeLoadScratch(s, live, false);
        s.config.fixedTickHz = hz; s.config.multiplayerActive = false;
        s.config.computerAiEnabled = false; s.config.superweapons = true;
        s.config.gameMode = "Battle"; s.campaignMode = false;
        s.rules = stockRules; s.rulesLoaded = true; s.tick = 100;
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false; s.visionEnabled = false;
        s.economy.owner = "Russians"; s.economy.skirmishStartApplied = true;
        s.triggers.bootstrapComplete = true;
        s.world.accessObjects().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.world.accessTerrainCells().clear();
        s.behaviors.clear(); s.projectiles.clear(); s.impactEffects.clear();
        s.deathWeapons.reset(); s.terrainInfoByCell.clear(); s.terrainByCell.clear();
        s.mapFacts = {}; s.mapFactsLoaded = false;
        for (int y = 44; y <= 94; ++y) for (int x = 44; x <= 94; ++x) {
            TerrainCell c; c.x = x; c.y = y; c.cellIndex512 = runtimeCellKey(x, y);
            // Clear01 is map tile 0; its TMP LandType value (13) is not a tile index.
            c.tileIndex = 0; c.subTileIndex = 0; s.world.accessTerrainCells().push_back(c);
        }
        initializeRuntimeNavigation(s); ensureRuntimeSpecialMechanismsInitialized(s);
        return box;
    };
    const auto add = [&](RuntimeWindowState& s, const std::string& type, RuntimeCellPoint cell,
                         const std::string& owner = "Americans") {
        const RuntimeBuildKind kind = type == "HTNK" ? RuntimeBuildKind::Unit
            : (type == "NANRCT" || type == "NAMISL") ? RuntimeBuildKind::Building : RuntimeBuildKind::Infantry;
        auto* o = addRuntimeObjectInstanceForOwner(s, owner, kind, type, cell);
        if (!o) return -1;
        o->mission = "Sleep"; o->weaponCooldownTicksRemaining = 100000;
        o->buildAnimTicksRemaining = o->buildAnimTicksTotal = 0;
        return o->instanceId;
    };
    const auto death = [](RuntimeWindowState& s, int id) -> RuntimeImpactEffect* {
        for (auto& fx : s.impactEffects) if (fx.death && fx.targetObjectId == id) return &fx;
        return nullptr;
    };
    const auto save = [&](RuntimeWindowState& s) -> std::unique_ptr<RuntimeWindowState> {
        std::ostringstream out;
        if (!writeRuntimeStateToStream(s, out, error)) return nullptr;
        IniFile ini; if (!ini.loadFromString(out.str())) return nullptr;
        std::ostringstream canonical;
        if (!writeRuntimeMultiplayerSnapshotCanonicalIni(ini, canonical, error)
            || canonical.str().find("[UnitDeathAnimations]") != std::string::npos) {
            error = "network projection included ordinary-save death presentation"; return nullptr;
        }
        auto loaded = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*loaded, s, false);
        if (!materializeRuntimeLoadStateFromIni(*loaded, ini, error)) return nullptr;
        ++saves; return loaded;
    };
    const auto sameDeath = [&](RuntimeWindowState& a, RuntimeWindowState& b, int id) {
        const auto* left = death(a, id); const auto* right = death(b, id);
        if (!left || !right) { error = "missing death after load"; return false; }
        if (runtimeMultiplayerSnapshotImpactRecord(*left) != runtimeMultiplayerSnapshotImpactRecord(*right)) {
            error = "timeline differs after load: " + runtimeMultiplayerSnapshotImpactRecord(*left)
                + " versus " + runtimeMultiplayerSnapshotImpactRecord(*right); return false;
        }
        return true;
    };
    const auto step = [](RuntimeWindowState& s) { ++s.tick; advanceRuntimeEffects(s); };
    const auto setPose = [&](RuntimeWindowState& s, int id) {
        auto* o = findMutableObjectById(s, id);
        o->health = 1; o->subCell = o->kind == WorldObjectKind::Infantry ? 2 : -1;
        o->facing = 96; o->hasRenderPosition = true; o->renderMoveActive = true;
        o->renderX = 66.125; o->renderY = 65.875; o->renderLevel = 0;
        s.selectedObjectIds = {id}; s.runtimeSoundIntentHistory.clear();
    };
    const auto lastPulse = [&](RuntimeWindowState& s, int id) {
        std::ostringstream text; s.specialMechanisms.save(text);
        IniFile saved, overlay; if (!saved.loadFromString(text.str())) return false;
        int index=0;
        for (const auto& [areaId, area] : s.specialMechanisms.radiationAreas()) {
            (void)area;
            if (areaId==id) {
                std::istringstream row(saved.getValue("SpecialRadiation",std::to_string(index),""));
                std::vector<std::string> fields; std::string field;
                while(std::getline(row,field,'|')) fields.push_back(field);
                if(fields.size()!=16) return false;
                fields[11]="1";
                std::ostringstream update; update<<"[SpecialRadiation]\n"<<index<<'=';
                for(std::size_t n=0;n<fields.size();++n) update<<(n?"|":"")<<fields[n];
                update<<'\n'; overlay.loadFromString(update.str()); saved.appendFrom(overlay);
                return s.specialMechanisms.load(saved,error);
            }
            ++index;
        }
        return false;
    };
    for (int hz : {15, 30, 60}) for (const std::string route : {
            "beam", "eruption", "site", "source-gone", "last-pulse", "reactor-site", "nuke-site"}) {
        for (const std::string type : {"E1", "DOG", "HTNK"}) {
            scene = route + "/" + type + "/" + std::to_string(hz) + "Hz";
            const auto checkScene = [&]() -> bool {
            auto box = fixture(hz); auto& s = *box;
            const int source = add(s, "DESO", {60, 66}, "Russians");
            const int victim = add(s, type, {66, 66});
            const int outside = add(s, "E1", {90, 90});
            if (source < 0 || victim < 0 || outside < 0) return fail("scene objects missing");
            std::string warheadId = "RadSite";
            if (route == "reactor-site") {
                findMutableObjectById(s, victim)->health = 100000;
                const int reactor = add(s, "NANRCT", {64, 64}, "Russians");
                RuntimeObjectExitRequest exit; exit.objectId = reactor; exit.tick = s.tick;
                if (!finalizeRuntimeObjectExit(s, exit).accepted) return fail("reactor death rejected");
                drainRuntimeObjectExitQueue(s);
                if (s.specialMechanisms.radiationAreas().size() != 1)
                    return fail("real reactor blast did not leave its site");
            } else if (route == "nuke-site") {
                findMutableObjectById(s, victim)->health = 100000;
                add(s, "NANRCT", {46, 46}, "Russians");
                add(s, "NAMISL", {52, 46}, "Russians");
                ensureRuntimeSuperWeaponsInitialized(s);
                auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
                s.superWeapons.tick(context);
                if (!s.superWeapons.forceReady("Russians", "NukeSpecial", &context)
                    || !s.superWeapons.beginTargetSelection("Russians", "NukeSpecial", context, &error)
                    || !s.superWeapons.submitTarget("Russians", "NukeSpecial", {{66, 66}, victim}, context, &error))
                    return fail("nuclear launch: " + error);
                for (int i = 0; i < hz * 40 && findObjectById(s, victim)
                        && findObjectById(s, victim)->health == 100000; ++i) step(s);
                if (!findObjectById(s, victim) || findObjectById(s, victim)->health >= 100000)
                    return fail("nuclear blast did not reach the survivor");
                if (!s.specialMechanisms.radiationAreas().empty())
                    return fail("nuclear fixture used projectile sites instead of superweapon fallout");
            } else if (route != "beam" && route != "eruption") {
                const int area = s.specialMechanisms.spawnRadiationArea(source, "Russians", {66, 66}, 10,
                    500, 1, "RadSite", s.tick, true, nullptr);
                if (route == "last-pulse" && !lastPulse(s,area)) return fail("final pulse lifetime fixture");
                if (route == "source-gone") {
                    RuntimeObjectExitRequest exit; exit.objectId = source; exit.tick = s.tick;
                    if (!finalizeRuntimeObjectExit(s, exit).accepted) return fail("source death rejected");
                }
            }
            findMutableObjectById(s, victim)->health = 1;
            const int outsideHealth = findObjectById(s, outside)->health;
            std::unique_ptr<RuntimeWindowState> before;
            if (type == "E1" && route != "beam" && route != "eruption") {
                before = save(s); if (!before) return fail("before death save: " + error);
                if (!findObjectById(*before, victim) || findObjectById(*before, victim)->health != 1)
                    return fail("pre-death reload lost the living victim's health");
            }
            // This isolated effects probe assigns a visible interpolation
            // without issuing a movement order. Install that synthetic pose
            // after loading; ordinary movement persistence is not its subject.
            setPose(s, victim); if (before) setPose(*before, victim);
            const WorldObjectInstance pose = *findObjectById(s, victim);
            if (route == "beam" || route == "eruption") {
                const auto* weapon = s.rules.findWeaponType(route == "beam" ? "RadBeamWeapon" : "RadEruptionWeapon");
                if (!weapon) return fail("stock weapon missing");
                RuntimeProjectileEffect hit; hit.actorId = nextRuntimeProjectileActorId(s);
                hit.attackerObjectId = source; hit.targetObjectId = victim; hit.owner = "Russians";
                hit.weaponId = weapon->id; hit.warheadId = weapon->warhead; hit.damage = weapon->damage;
                hit.fromCell = {60, 66}; hit.toCell = hit.currentCell = {66, 66};
                hit.cellSpread = s.rules.findWarheadType(weapon->warhead)->cellSpread;
                hit.affectsEnemies = true; hit.affectsAllies = true;
                warheadId = hit.warheadId;
                if (!applyRuntimeProjectileImpact(s, hit)) return fail("direct hit rejected");
                // Stock eruption has Damage=1; heavy armor rounds that
                // initial hit to zero. Its real RadSite pulse kills the tank.
                if (route == "eruption" && findObjectById(s, victim)) {
                    advanceRuntimeSpecialMechanisms(s); warheadId = "RadSite";
                }
            } else {
                for (int i = 0; i < hz * 3 && findObjectById(s, victim); ++i) {
                    if (route == "nuke-site") { step(s); if (before) step(*before); }
                    else {
                        advanceRuntimeSpecialMechanisms(s);
                        if (before) advanceRuntimeSpecialMechanisms(*before);
                        if (findObjectById(s, victim)) { ++s.tick; if (before) ++before->tick; }
                    }
                }
                if (before && !sameDeath(s, *before, victim)) return fail("pre-death reload changed presentation: " + error);
                if (route == "last-pulse" && !s.specialMechanisms.radiationAreas().empty())
                    return fail("last-pulse fixture failed to remove its area before handling death");
            }
            auto* fx = death(s, victim);
            if (findObjectById(s, victim) || !fx || !s.selectedObjectIds.empty()
                || std::count_if(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& e) {
                    return e.death && e.targetObjectId == victim;
                }) != 1) return fail("missing/duplicate death or retained live selection; health="
                    + std::to_string(findRuntimeObjectByIdIncludingInactive(s, victim)->health)
                    + " effect=" + (fx ? fx->animId : "absent"));
            const bool human = type == "E1";
            const auto offset = worldObjectSubCellOffset(pose);
            if (fx->warheadId != warheadId || fx->owner != pose.owner
                || std::abs(fx->worldX - pose.renderX - offset.x) > 0.0001
                || std::abs(fx->worldY - pose.renderY - offset.y) > 0.0001
                || std::abs(fx->worldLevel - pose.renderLevel) > 0.0001
                || (human ? fx->animId != "NUKEDIE" : fx->animId == "NUKEDIE"))
                return fail("lost warhead, native death kind, owner or moving/subcell pose: " + fx->animId);
            if (type == "DOG" && fx->renderIntent != "infantryDeath")
                return fail("non-human infantry lost its own death sequence");
            if (human && (fx->animationFrameCount != 60 || fx->animationStartFrame != 0
                || fx->animationEndFrame != fx->animationFrameCount
                || fx->animationSequenceSteps != fx->animationFrameCount
                || fx->totalTicks < fx->animationFrameCount * fx->animationFrameDelayTicks
                || std::count(s.runtimeSoundIntentHistory.begin(), s.runtimeSoundIntentHistory.end(),
                    "sound:InfantryMelt") != 1)) return fail("truncated melt frames or missing/repeated melt sound");
            if (!findObjectById(s, outside) || findObjectById(s, outside)->health != outsideHealth)
                return fail("radiation escaped its radius");
            const auto effectCount = s.impactEffects.size();
            RuntimeObjectExitRequest repeated; repeated.objectId = victim; repeated.tick = s.tick;
            if (!finalizeRuntimeObjectExit(s, repeated).duplicate || s.impactEffects.size() != effectCount)
                return fail("duplicate finalization replayed death");
            if (human) {
                const int frames = fx->animationFrameCount, duration = fx->remainingTicks;
                const int delay = fx->animationFrameDelayTicks;
                std::unique_ptr<RuntimeWindowState> middle;
                int previousFrame = -1; std::set<int> visited;
                for (int i = 0; i <= duration; ++i) {
                    auto* current = death(s, victim);
                    if (i == duration) {
                        if (current || (middle && death(*middle, victim))) return fail("melt animation never expired");
                        break;
                    }
                    if (!current) return fail("melt disappeared early");
                    const auto* actors = runtimeImpactRenderActorsForRender(s);
                    if (!actors) return fail("death render snapshot absent");
                    const auto actor = std::find_if(actors->begin(), actors->end(), [&](const auto& a) {
                        return a.targetObjectId == victim && a.animId == "NUKEDIE";
                    });
                    if (actor == actors->end() || actor->frameIndex < previousFrame || actor->frameIndex >= frames)
                        return fail("melt render frame wrapped, disappeared or exceeded the original SHP");
                    previousFrame = actor->frameIndex; visited.insert(previousFrame); ++frameChecks;
                    if (hz == 30 && route == "site" && !root.empty()
                        && (i == 0 || i == duration / 3 || i == duration * 2 / 3)) {
                        auto stats = drawRuntimeStateOffscreenAtCell(s, {66, 66}, 800, 520, error,
                            root + "/melt-" + std::to_string(i) + ".bmp", false, false);
                        if (!stats || stats->impactAnimDrawn == 0 || stats->fallbackTerrainCells != 0)
                            return fail("original death SHP or clear terrain failed to draw: " + error);
                        ++captures;
                    }
                    if (i == duration / 3) {
                        middle = save(s);
                        if (!middle || !sameDeath(s, *middle, victim)) return fail("mid-animation reload restarted/lost melt: " + error);
                        // The multiplayer wire deliberately omits cached art.
                        // Its renderer must reconstruct the same current frame.
                        RuntimeImpactEffect wire;
                        const auto saved = *current;
                        if (!parseRuntimeMultiplayerSnapshotImpactRecord(runtimeMultiplayerSnapshotImpactRecord(saved), wire))
                            return fail("death snapshot decode rejected");
                        *current = wire;
                        const auto* restoredActors = runtimeImpactRenderActorsForRender(s);
                        const bool restored = restoredActors && std::any_of(restoredActors->begin(), restoredActors->end(), [&](const auto& a) {
                            return a.targetObjectId == victim && a.animId == "NUKEDIE" && a.frameIndex == previousFrame
                                && a.renderIntent == "unitDestruction" && a.owner == saved.owner
                                && a.x == saved.worldX && a.y == saved.worldY && a.level == saved.worldLevel;
                        });
                        *current = saved;
                        if (!restored) return fail("snapshot lost melt image, current frame or pose");
                        ++snapshots;
                    }
                    step(s); if (middle) { step(*middle); if (i + 1 < duration && !sameDeath(s, *middle, victim))
                        return fail("restored melt continuation differs"); }
                }
                if (!visited.contains(frames - 1) || visited.size() < static_cast<std::size_t>(frames - 1))
                    return fail("original melt frames were skipped");
                std::cout << "Radiation death " << scene << " anim=NUKEDIE frames=" << frames
                    << " delay=" << delay << " pose/sound/once/full-playback/save PASS\n";
            } else std::cout << "Radiation death " << scene << " native-nonhuman/vehicle death PASS\n";
            ++cases;
            return true;
            };
            if (!checkScene()) { failures.push_back(error); std::cerr << error << '\n'; }
        }
    }
    // Stock additional human types, radiation immunity and metadata selection
    // in overlapping sites. A custom General mapping proves the cause is
    // carried through the existing rules, rather than hardcoding NUKEDIE.
    for (int hz : {15, 30, 60}) {
        for (const std::string type : {"E2", "ENGINEER", "TANY", "YURI", "DESO"}) {
            scene = "additional/" + type + "/" + std::to_string(hz);
            auto box = fixture(hz); auto& s = *box;
            const int id = add(s, type, {66, 66});
            if (id < 0) return fail("additional unit missing");
            setPose(s, id);
            s.specialMechanisms.spawnRadiationArea(999, "Russians", {66, 66}, 10, 500, 100, "RadSite", s.tick, true, nullptr);
            advanceRuntimeSpecialMechanisms(s);
            if (type == "DESO") {
                if (!findObjectById(s, id) || findObjectById(s, id)->health != 1 || death(s, id))
                    return fail("radiation immunity regressed");
            } else if (!death(s, id) || death(s, id)->animId != "NUKEDIE" || findObjectById(s, id))
                return fail("additional human missed melt animation");
            ++cases;
        }
        scene = "overlap-and-custom-mapping/" + std::to_string(hz);
        auto box = fixture(hz); auto& s = *box;
        IniFile customized = stockIni, overlay;
        overlay.loadFromString("[General]\nInfantryNuked=ELECTRO\n"); customized.appendFrom(overlay);
        if (!s.rules.loadFromIni(customized)) return fail("custom rule overlay failed");
        const int id = add(s, "E1", {66, 66}); setPose(s, id);
        s.specialMechanisms.spawnRadiationArea(111, "WeakOwner", {66, 66}, 10, 10, 10000, "HE", s.tick, true, nullptr);
        const int strongest = s.specialMechanisms.spawnRadiationArea(222, "Russians", {66, 66}, 10,
            500, 1, "RadSite", s.tick, true, nullptr);
        if (!lastPulse(s,strongest)) return fail("strongest final pulse lifetime fixture");
        auto metadata = s.specialMechanisms;
        const auto events = metadata.tickRadiation(s.world.accessObjects(), s.rules, s.tick,
            [](const auto& a, const auto& b) { return a == b; }, [](int) { return false; }, nullptr);
        if (events.size() != 1 || events.front().areaId != strongest || events.front().sourceObjectId != 222
            || events.front().sourceOwner != "Russians" || events.front().warheadId != "RadSite"
            || metadata.radiationAreas().contains(strongest)) return fail("last winning area lost its complete cause");
        advanceRuntimeSpecialMechanisms(s);
        if (!death(s, id) || death(s, id)->animId != "ELECTRO" || death(s, id)->warheadId != "RadSite"
            || death(s, id)->attackerObjectId != 222) return fail("overlap used wrong source or ignored custom InfantryNuked");
        std::ostringstream savedDeaths; writeRuntimeUnitDeaths(s, savedDeaths);
        IniFile malformed, extraCount, legacy;
        malformed.loadFromString(savedDeaths.str());
        extraCount.loadFromString("[UnitDeathAnimations]\nCount=2\n"); malformed.appendFrom(extraCount);
        const auto priorEffects = s.impactEffects.size();
        const auto priorDeath = runtimeMultiplayerSnapshotImpactRecord(*death(s, id));
        std::string rejection;
        if (loadRuntimeUnitDeaths(s, malformed, rejection) || rejection.empty()
            || s.impactEffects.size() != priorEffects
            || runtimeMultiplayerSnapshotImpactRecord(*death(s, id)) != priorDeath
            || !loadRuntimeUnitDeaths(s, legacy, rejection) || s.impactEffects.size() != priorEffects)
            return fail("partial invalid death save changed state or legacy save was rejected");
        ++cases;
    }
    if (!failures.empty()) return fail(std::to_string(failures.size()) + " scenes failed; first: " + failures.front());
    std::cout << "Radiation death: passed cases=" << cases << " saves=" << saves << " captures=" << captures
        << " frameChecks=" << frameChecks << " snapshots=" << snapshots << " direct/site/expired-source/final-pulse/reactor/nuclear-fallout"
        " stock-art/sound/pose/lifetime/overlap/immunity/custom PASS\n";
    return true;
}
