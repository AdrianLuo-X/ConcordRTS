bool runRuntimeGrinderAnimationSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    const auto fail = [&](const std::string& why) { error = "Grinder sequence: " + why; return false; };
    std::array<std::unique_ptr<RuntimeWindowState>, 2> assets;
    for (int theater = 0; theater < 2; ++theater) {
        assets[theater] = std::make_unique<RuntimeWindowState>(); auto& base = *assets[theater];
        base.config = config; base.config.computerAiEnabled = false; base.config.playerSlots.clear();
        base.config.gameModeRulesOverride.clear(); base.config.multiplayerActive = false;
        base.config.defaultMapPath = theater ? "local_data/resources/mix_exports/mapsmd03/all07smd.map"
            : "local_data/resources/mix_exports/multimd/2peaks.map";
        base.report = buildRuntimeStartupReport(base.config, &base.world, &base.theater,
            &base.art, &base.rules, &base.mapFacts);
        if (!base.report.success || toLowerAsciiLocal(base.report.map.theater) != (theater ? "snow" : "temperate"))
            return fail("original theater resources: " + base.report.errorMessage);
    }
    const auto make = [&](int theater, int hz, bool damaged, bool twoHosts = false) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        const auto& base = *assets[theater];
        s.config = base.config; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.crates = s.config.superweapons = s.config.shortGame = false;
        s.report = base.report; s.rules = base.rules; s.art = base.art; s.theater = base.theater;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        // This fixture replaces cell topology, while keeping the map's actual
        // global lighting. Initialize it through the same startup authority.
        s.mapFacts.theater = base.mapFacts.theater;
        s.mapFacts.lighting = base.mapFacts.lighting;
        ensureRuntimeLightingInitialized(s);
        s.world = base.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.economy.owner = "YuriCountry"; s.economy.credits = 1000; s.economy.techLevel = 10;
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.runtimePreferencePersistenceEnabled = false; s.runtimeSoundsEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        initializeRuntimeLocalProductionLanes(s.economy);
        for (int y = 1; y <= 40; ++y) for (int x = 1; x <= 40; ++x) {
            TerrainCell terrain; terrain.x = x; terrain.y = y; terrain.cellIndex512 = runtimeCellKey(x, y);
            terrain.tileIndex = terrain.subTileIndex = 0; s.world.accessTerrainCells().push_back(terrain);
        }
        const auto add = [&](int id, WorldObjectKind kind, const std::string& type, int x, int y) {
            WorldObjectInstance object; object.instanceId = id; object.mapId = "GrinderSequence" + std::to_string(id);
            object.kind = kind; object.type = type; object.owner = s.economy.owner;
            object.alive = object.poweredOperational = true; object.mission = "Guard";
            object.x = x; object.y = y; object.cell = runtimeCellKey(x, y); object.currentAmmo = -1;
            object.health = runtimeMaxHealthForObject(s, object); s.world.accessObjects().push_back(object);
        };
        add(1, WorldObjectKind::Structure, "YAGRND", 10, 10);
        if (damaged) findMutableObjectById(s, 1)->health = 300;
        add(100, WorldObjectKind::Structure, "YAPOWR", 26, 26);
        add(101, WorldObjectKind::Structure, "YAPOWR", 30, 26);
        for (int id : {2, 3, 4}) add(id, WorldObjectKind::Infantry, "INIT", 5 + id, 15);
        if (twoHosts) {
            add(5, WorldObjectKind::Structure, "YAGRND", 20, 10);
            add(6, WorldObjectKind::Infantry, "INIT", 20, 16);
            add(7, WorldObjectKind::Infantry, "INIT", 21, 16);
        }
        s.terrainByCell = buildRuntimeTerrainLookup(s.world); initializeRuntimeNavigation(s);
        ensureRuntimeSpecialMechanismsInitialized(s);
        return storage;
    };
    const auto currentCycle = [](const RuntimeWindowState& s, int hostId) -> const RuntimeImpactEffect* {
        const auto found = std::find_if(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& effect) {
            return effect.lifecycleState == "GrindingAnimation" && effect.targetObjectId == hostId && effect.remainingTicks > 0;
        });
        return found == s.impactEffects.end() ? nullptr : &*found;
    };
    const auto cycleCount = [](const RuntimeWindowState& s, int hostId, bool queued) {
        return std::count_if(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& effect) {
            return effect.targetObjectId == hostId && effect.lifecycleState == (queued ? "GrindingAnimationQueued" : "GrindingAnimation");
        });
    };
    // A real command and real atomic arrival/consumption are used. Placing the
    // actor at the authored entry cell lets two arrivals share one exact tick.
    const auto arrive = [&](RuntimeWindowState& s, int hostId, int unitId) {
        if (!queueRuntimeGrindingEntry(s, hostId, unitId, false)) return false;
        auto* actor = findMutableObjectById(s, unitId);
        const auto found = s.behaviors.find(unitId);
        if (!actor || found == s.behaviors.end()) return false;
        actor->x = found->second.moveTarget.x; actor->y = found->second.moveTarget.y;
        actor->cell = runtimeCellKey(actor->x, actor->y); clearRuntimeRenderPosition(*actor);
        return completeRuntimeGrindingEntry(s, unitId);
    };
    const auto step = [](RuntimeWindowState& s) {
        ++s.tick; advanceRuntimeEffects(s); advanceRuntimeLighting(s);
        // The real simulation admits path requests after effects. Our exact
        // same-tick arrivals bypass walking, but must still run that normal
        // bounded admission, including its cleanup of consumed actors' ids.
        processRuntimePathRequests(s);
    };
    const auto visualCycle = [&](RuntimeWindowState& s, int hostId, const RuntimeImpactEffect& cycle) {
        const auto* actors = runtimeImpactRenderActorsForRender(s);
        if (!actors) return false;
        int found = 0;
        for (const auto& actor : *actors) if (actor.targetObjectId == hostId
            && (actor.renderIntent == "grinderCycle" || actor.renderIntent == "grinderCycleQueued")) {
            ++found;
            if (actor.renderIntent != "grinderCycle" || actor.animId != cycle.animId
                || actor.frameIndex != cycle.frameIndex || actor.remainingTicks != cycle.remainingTicks) return false;
        }
        return found == 1;
    };
    const auto describe = [&](const RuntimeWindowState& s) {
        std::ostringstream text;
        const auto tint = s.lighting.renderTint(); const auto& profile = s.lighting.currentProfile();
        text << "tick=" << s.tick << " theater=" << s.report.map.theater
            << " loaded=" << s.theaterLoaded << '/' << s.artLoaded << '/' << s.rulesLoaded
            << " mapFacts=" << s.mapFactsLoaded << '/' << s.mapFacts.cells.size()
            << " lighting=" << s.lightingInitialized << " tint=" << tint.red << ',' << tint.green << ',' << tint.blue
            << " profile=" << profile.ambient << ',' << profile.red << ',' << profile.green << ',' << profile.blue
            << ',' << profile.ground << ',' << profile.level << " owner=" << s.economy.owner
            << " country=" << runtimeCountryForOwner(s, s.economy.owner) << " configCountry=" << s.config.playerCountry
            << " remaps=" << s.ownerRemapColors.size() << " neutrals=" << s.neutralRemapOwners.size();
        const auto color = s.ownerRemapColors.find(toLowerAsciiLocal(s.economy.owner));
        if (color != s.ownerRemapColors.end()) text << " ownerColor=" << std::hex << color->second << std::dec;
        else text << " ownerColor=missing";
        text << " ownerNeutral=" << s.neutralRemapOwners.contains(toLowerAsciiLocal(s.economy.owner));
        if (const auto* cycle = currentCycle(s, 1)) {
            text << " cycle={anim:" << cycle->animId << ",intent:" << cycle->renderIntent
                << ",lifecycle:" << cycle->lifecycleState << ",frame:" << cycle->frameIndex
                << ",range:" << cycle->animationStartFrame << ',' << cycle->animationEndFrame
                << ",loop:" << cycle->animationLoopStartFrame << ',' << cycle->animationLoopEndFrame
                << ',' << cycle->animationLoopCount << ",steps:" << cycle->animationSequenceSteps
                << ",frameCount:" << cycle->animationFrameCount
                << ",delay:" << cycle->animationFrameDelayTicks << ",remaining:" << cycle->remainingTicks
                << ",total:" << cycle->totalTicks << ",owner:" << cycle->owner << ",cell:"
                << cycle->cell.x << ',' << cycle->cell.y << ",level:" << cycle->level
                << ",world:" << cycle->hasWorldPosition << ',' << cycle->worldX << ',' << cycle->worldY
                << ',' << cycle->worldLevel << ",queued:" << cycleCount(s, 1, true) << '}';
        } else text << " cycle=missing";
        text << " alive={";
        for (const auto& object : s.world.getObjects()) if (object.alive)
            text << object.instanceId << ':' << object.type << ":hp" << object.health
                << ":cell" << object.x << ',' << object.y << ',' << object.level
                << ":render" << object.hasRenderPosition << ',' << object.renderX << ',' << object.renderY
                << ',' << object.renderLevel << ":bridge" << object.onBridge << ',' << object.renderOnBridge
                << ":subcell" << object.subCell << ":facing" << object.facing << ',' << object.turretFacing
                << ":power" << object.poweredOperational << ":powerPause" << object.powerAnimationPausedAtTick
                << ',' << object.powerAnimationPausedTicks << ":buildup" << object.buildAnimTicksRemaining
                << ',' << object.buildAnimTicksTotal << ":mission" << object.mission << ';';
        text << '}'; return text.str();
    };
    const auto render = [&](RuntimeWindowState& s, int theater, const std::string& path,
        std::vector<std::uint32_t>& pixels, std::string* diagnostic = nullptr) {
        const auto stats = drawRuntimeStateOffscreenAtCell(s, {11, 11}, 360, 320, error,
            path, false, false, -1, &pixels);
        std::ostringstream details; details << describe(s);
        if (!stats) {
            details << " render=failed reason=" << error;
            if (diagnostic) *diagnostic = details.str();
            return false;
        }
        const auto source = toLowerAsciiLocal(stats->buildingSpecialAnimationSource);
        const std::string expected = theater ? "yagrnd_b.shp" : "yggrnd_b.shp";
        const bool valid = stats->rendered && stats->visibleStructureSprites > 0
            && stats->buildingSpecialAnimationLayers == 1 && source.find(expected) != std::string::npos;
        details << " rendered=" << stats->rendered << " structures=" << stats->visibleStructureSprites
            << " specialLayers=" << stats->buildingSpecialAnimationLayers
            << " specialImage=" << stats->buildingSpecialAnimationImage
            << " specialResolved=" << stats->buildingSpecialAnimationResolved
            << " specialSource=" << stats->buildingSpecialAnimationSource
            << " firstBuildingArtFrame=" << stats->firstBuildingArtAnimationFrame
            << " actorCount=" << stats->impactActorSnapshots
            << " actorDrawn=" << stats->impactActorDrawn << " pixels=" << pixels.size();
        if (const auto* actors = runtimeImpactRenderActorsForRender(s)) {
            details << " actors={";
            for (const auto& actor : *actors) if (actor.targetObjectId == 1)
                details << actor.renderIntent << ':' << actor.animId << ":frame" << actor.frameIndex
                    << ":remaining" << actor.remainingTicks << ":xy" << actor.x << ',' << actor.y
                    << ":level" << actor.level << ';';
            details << '}';
        }
        if (diagnostic) *diagnostic = details.str();
        return valid;
    };
    const auto describeDifference = [](const auto& expected, const auto& actual) {
        std::ostringstream text;
        std::size_t raw = 0, rgb = 0, first = expected.size();
        int left = 360, top = 320, right = -1, bottom = -1;
        const auto count = std::min(expected.size(), actual.size());
        for (std::size_t i = 0; i < count; ++i) if (expected[i] != actual[i]) {
            ++raw;
            if ((expected[i] & 0x00ffffffU) != (actual[i] & 0x00ffffffU)) {
                ++rgb; first = std::min(first, i);
                const int x = static_cast<int>(i % 360), y = static_cast<int>(i / 360);
                left = std::min(left, x); right = std::max(right, x);
                top = std::min(top, y); bottom = std::max(bottom, y);
            }
        }
        text << "sizes=" << expected.size() << ',' << actual.size() << " rawDiff=" << raw
            << " rgbDiff=" << rgb << " highByteOnly=" << raw - rgb
            << " rgbBounds=" << left << ',' << top << '-' << right << ',' << bottom;
        if (first < count) text << " first=" << first % 360 << ',' << first / 360
            << " colors=" << std::hex << expected[first] << ',' << actual[first] << std::dec;
        return text.str();
    };
    const auto restore = [&](RuntimeWindowState& s, bool snapshot) {
        if (snapshot) for (int id : s.pendingPathObjectIds) {
            const auto behavior = s.behaviors.find(id);
            if (!findObjectById(s, id) || behavior == s.behaviors.end()
                || !behavior->second.pathRequestPending) {
                error = "fixture snapshot precedes the real path-admission boundary for actor " + std::to_string(id);
                return std::unique_ptr<RuntimeWindowState>{};
            }
        }
        std::ostringstream saved; IniFile ini;
        if (!writeRuntimeStateToStream(s, saved, error)
            || (snapshot && !writeRuntimeMultiplayerSnapshotExtension(s, saved, error))
            || !ini.loadFromString(saved.str())) return std::unique_ptr<RuntimeWindowState>{};
        auto storage = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*storage, s, false);
        if (!materializeRuntimeLoadStateFromIni(*storage, ini, error)) {
            std::ofstream diagnostic("build/issue21_06_grinder_failed_restore.ini", std::ios::binary | std::ios::trunc);
            diagnostic << saved.str(); return std::unique_ptr<RuntimeWindowState>{};
        }
        initializeRuntimeNavigation(*storage); completeRuntimeWorldRefresh(*storage);
        if (snapshot && !materializeRuntimeMultiplayerSnapshotExtension(*storage, ini, error))
            return std::unique_ptr<RuntimeWindowState>{};
        return storage;
    };
    int cases = 0, saves = 0, snapshots = 0, cycles = 0;
    std::vector<std::string> failures;
    std::array<std::vector<WorldObjectInstance>, 2> changedHealthHosts;
    std::array<std::vector<std::vector<RuntimeImpactRenderActor>>, 2> originalHalfFrames;
    for (int theater : {0, 1}) for (int hz : {30, 60}) for (bool damaged : {false, true})
        for (bool queued : {false, true}) {
        const std::string scene = std::string(theater ? "snow" : "temperate") + " hz=" + std::to_string(hz)
            + " damaged=" + std::to_string(damaged) + " queued=" + std::to_string(queued);
        const auto bad = [&](const std::string& why) { return fail(scene + ": " + why); };
        const auto checkScene = [&]() -> bool {
            auto storage = make(theater, hz, damaged); auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            const auto* unit = findObjectById(s, 2);
            const int refund = runtimeGrindingExpectedRefund(s, *unit, runtimeVeterancyObjectCost(s, *unit));
            if (!arrive(s, 1, 2) || (queued && !arrive(s, 1, 3))) return bad("actual entry refused");
            ++s.tick; if (!advanceRuntimeGrinding(s)) return bad("atomic consumption refused");
            if (findObjectById(s, 2) || (queued && findObjectById(s, 3))
                || s.economy.credits != 1000 + refund * (queued ? 2 : 1)
                || cycleCount(s, 1, false) != 1 || cycleCount(s, 1, true) != (queued ? 1 : 0))
                return bad("same-tick consumption/refund lost a cycle or displayed two at once");
            const auto first = *currentCycle(s, 1);
            if (first.animationStartFrame != (damaged ? 49 : 0)
                || first.animationEndFrame != (damaged ? 96 : 48)
                || first.totalTicks != (damaged ? 47 : 48) * 2 * hz / 15
                || first.remainingTicks != first.totalTicks || !visualCycle(s, 1, first))
                return bad("first cycle has wrong native range, duration or render actor");
            const char* output = std::getenv("CONCORD_GRINDER_DIR");
            const std::string imageRoot = output && *output ? output : "build";
            const bool images = hz == 30 && queued;
            const std::string imageStem = imageRoot + "/issue21_06_grinder-" + (theater ? "snow" : "temperate")
                + (damaged ? "-damaged" : "-healthy");
            std::vector<std::uint32_t> firstPixels;
            if (!render(s, theater, images ? imageStem + "-first.bmp" : "", firstPixels))
                return bad("original theater special image missing: " + error);
            std::unique_ptr<RuntimeWindowState> loaded, network;
            bool visualFailure = false;
            std::vector<std::vector<int>> seenFrames(queued ? 3 : 1);
            std::vector<RuntimeImpactRenderActor> firstCycleFrames;
            int completed = 0, age = 0;
            int duration = first.totalTicks, start = first.animationStartFrame, end = first.animationEndFrame;
            for (int ticks = 0; ticks < hz * 30; ++ticks) {
                const auto* cycle = currentCycle(s, 1);
                if (!cycle) {
                    if (age != duration) return bad("current cycle vanished before its final frame");
                    ++completed; break;
                }
                if (age == duration) {
                    ++completed; age = 0;
                    start = damaged ? 0 : 49; end = damaged ? 48 : 96;
                    duration = (end - start) * 2 * hz / 15;
                }
                if (completed >= static_cast<int>(seenFrames.size())) return bad("animation repeated beyond consumed unit count");
                if (cycle->animationStartFrame != start || cycle->animationEndFrame != end
                    || cycle->totalTicks != duration || cycle->remainingTicks != duration - age
                    || cycle->frameIndex != start + age / (2 * hz / 15) || !visualCycle(s, 1, *cycle))
                    return bad("cycle restarted, switched its damage half or skipped a frame at age=" + std::to_string(age));
                if (seenFrames[completed].empty() || seenFrames[completed].back() != cycle->frameIndex) {
                    seenFrames[completed].push_back(cycle->frameIndex);
                    if (images && completed == 0) firstCycleFrames.push_back(makeRuntimeImpactRenderActor(*cycle));
                }
                if (queued && completed == 0 && age + 1 == duration / 3)
                    if (!arrive(s, 1, 4)) return bad("mid-cycle third actual arrival refused");
                if (completed == 0 && age == duration / 3) {
                    if (queued) {
                        if (findObjectById(s, 4) || cycleCount(s, 1, true) != 2
                            || s.economy.credits != 1000 + refund * 3)
                            return bad("mid-cycle arrival replaced or restarted the active animation");
                        findMutableObjectById(s, 1)->health = damaged ? 900 : 300;
                    }
                    loaded = restore(s, false); network = restore(s, true);
                    if (!loaded || !network) return bad("full save/snapshot restore: " + error);
                    ++saves; ++snapshots;
                    std::vector<std::uint32_t> original, diskPixels, networkPixels;
                    const std::string restoreStem = imageRoot + "/issue21_06_restore-" + (theater ? "snow" : "temperate")
                        + "-" + std::to_string(hz) + (damaged ? "-damaged" : "-healthy")
                        + (queued ? "-queue" : "-single");
                    std::string originalDetails, diskDetails, networkDetails;
                    const bool originalRendered = render(s, theater, restoreStem + "-original.bmp", original, &originalDetails);
                    const bool diskRendered = render(*loaded, theater, restoreStem + "-loaded.bmp", diskPixels, &diskDetails);
                    const bool networkRendered = render(*network, theater, restoreStem + "-network.bmp", networkPixels, &networkDetails);
                    std::cout << "Grinder restore " << scene << " original: " << originalDetails << '\n'
                        << "Grinder restore " << scene << " loaded: " << diskDetails << '\n'
                        << "Grinder restore " << scene << " network: " << networkDetails << '\n'
                        << "Grinder restore " << scene << " loadedPixels: " << describeDifference(original, diskPixels) << '\n'
                        << "Grinder restore " << scene << " networkPixels: " << describeDifference(original, networkPixels) << '\n';
                    if (!originalRendered || !diskRendered || !networkRendered)
                        return bad("restore native rendering failed at stage="
                            + std::string(!originalRendered ? "original" : !diskRendered ? "loaded" : "network")
                            + "; " + (!originalRendered ? originalDetails : !diskRendered ? diskDetails : networkDetails));
                    if (original != diskPixels || original != networkPixels) {
                        bad("restored native scene pixels differ; loaded " + describeDifference(original, diskPixels)
                            + "; network " + describeDifference(original, networkPixels));
                        failures.push_back(error); std::cerr << error << "; continuing this scene's full cycles\n";
                        visualFailure = true;
                    }
                }
                for (auto* restored : {loaded.get(), network.get()}) if (restored) {
                    const auto* restoredCycle = currentCycle(*restored, 1);
                    if (!restoredCycle || restoredCycle->animationStartFrame != cycle->animationStartFrame
                        || restoredCycle->animationEndFrame != cycle->animationEndFrame
                        || restoredCycle->frameIndex != cycle->frameIndex || restoredCycle->remainingTicks != cycle->remainingTicks
                        || cycleCount(*restored, 1, true) != cycleCount(s, 1, true)
                        || !visualCycle(*restored, 1, *restoredCycle)) return bad("save/snapshot diverged while advancing queued cycles; original "
                            + describe(s) + "; restored " + describe(*restored));
                }
                if (images && completed == 0 && age + 1 == duration) {
                    std::vector<std::uint32_t> pixels;
                    if (!render(s, theater, imageStem + "-last.bmp", pixels)) return bad("final native frame failed to render");
                }
                ++age; step(s); if (loaded) step(*loaded); if (network) step(*network);
            }
            if (completed != (queued ? 3 : 1) || currentCycle(s, 1) || cycleCount(s, 1, true))
                return bad("wrong count of complete animation cycles: " + std::to_string(completed));
            for (const auto& frames : seenFrames) if (frames.size() != 47 && frames.size() != 48)
                return bad("not every authored frame was shown exactly once");
            const int credits = s.economy.credits;
            for (int n = 0; n < hz * 10; ++n) {
                step(s);
                if (currentCycle(s, 1) || cycleCount(s, 1, true) || s.economy.credits != credits)
                    return bad("empty factory restarted recycling after the final cycle");
            }
            std::vector<std::uint32_t> idlePixels;
            const auto idle = drawRuntimeStateOffscreenAtCell(s, {11, 11}, 360, 320, error,
                images ? imageStem + "-idle.bmp" : "", false, false, -1, &idlePixels);
            if (!idle || idle->buildingSpecialAnimationLayers != 0 || idlePixels == firstPixels
                || (loaded && (currentCycle(*loaded, 1) || cycleCount(*loaded, 1, true)))
                || (network && (currentCycle(*network, 1) || cycleCount(*network, 1, true))))
                return bad("final cycle did not return to original idle presentation after reload");
            if (images) {
                changedHealthHosts[theater].push_back(*findObjectById(s, 1));
                originalHalfFrames[theater].push_back(std::move(firstCycleFrames));
            }
            if (!visualFailure) ++cases;
            cycles += completed;
            std::cout << "Grinder sequence " << scene << " completeCycles=" << completed
                << " complete-frames/ordered/save/snapshot/idle=ok pixels=" << (visualFailure ? "failed" : "ok") << '\n';
            return true;
        };
        try { if (!checkScene()) { failures.push_back(error); std::cerr << error << '\n'; } }
        catch (const std::exception& exception) {
            bad(std::string("scene exception: ") + exception.what()); failures.push_back(error); std::cerr << error << '\n';
        }
    }
    // The special-only renderer exposes its actual selected frame separately
    // from the input actor. A factory whose health crossed the threshold must
    // still draw the first cycle's original half, even though its next cycle
    // and its base model legitimately use the new health state.
    for (int theater : {0, 1}) {
        if (changedHealthHosts[theater].size() != 2) continue;
        const auto& base = *assets[theater]; RuntimeMapRenderContext context;
        context.rules = &base.rules; context.art = &base.art; context.theater = &base.theater;
        context.assetRootPath = runtimeAssetRootPath(base.config);
        context.structureRootPath = runtimeStructureRootPath(base.config, base.report.map);
        context.palettePath = runtimePalettePath(base.config, base.report.map);
        context.structurePalettePath = runtimeStructurePalettePath(base.config, base.report.map);
        if (!runRuntimeGrindingAnimationRenderContractCheck(context, changedHealthHosts[theater],
                originalHalfFrames[theater], error)) {
            failures.push_back(error); std::cerr << error << '\n';
        }
    }
    // Factories have independent queues. Destroying one during its current
    // cycle must cancel only its own queue and must not restart the other.
    for (int hz : {30, 60}) {
        const auto checkHosts = [&]() -> bool {
            auto storage = make(0, hz, false, true); auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            for (const auto pair : {std::pair<int, int>{1, 2}, {1, 3}, {5, 6}, {5, 7}})
                if (!arrive(s, pair.first, pair.second)) return fail("independent factory arrival");
            ++s.tick; if (!advanceRuntimeGrinding(s)) return fail("independent factory consumption");
            for (int n = 0; n < hz; ++n) step(s);
            const auto before = currentCycle(s, 1) ? *currentCycle(s, 1) : RuntimeImpactEffect{};
            auto* host = findMutableObjectById(s, 5); host->alive = false; host->health = 0;
            step(s);
            const auto* after = currentCycle(s, 1);
            if (!after || after->remainingTicks != before.remainingTicks - 1 || currentCycle(s, 5)
                || cycleCount(s, 5, true) || cycleCount(s, 1, true) != 1)
                return fail("destroying factory B cleared/restarted factory A or retained B's queue");
            const int duration = after->totalTicks;
            for (int n = 0; n < duration * 2; ++n) step(s);
            if (currentCycle(s, 1) || cycleCount(s, 1, true)) return fail("unrelated surviving factory did not finish exactly two cycles");
            ++cases;
            std::cout << "Grinder independent factories hz=" << hz << " destruction-isolation=ok\n";
            return true;
        };
        try { if (!checkHosts()) { failures.push_back(error); std::cerr << error << '\n'; } }
        catch (const std::exception& exception) {
            fail(std::string("factory exception: ") + exception.what()); failures.push_back(error); std::cerr << error << '\n';
        }
    }
    std::cout << "Grinder sequence: cases=" << cases << " completeCycles=" << cycles
        << " saves=" << saves << " snapshots=" << snapshots << '\n';
    if (!failures.empty()) return fail("failed scenes=" + std::to_string(failures.size()) + "; " + failures.front());
    return true;
}
