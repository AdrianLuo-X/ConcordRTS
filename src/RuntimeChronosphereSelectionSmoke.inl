    if (std::getenv("CONCORD_COMMAND_SMOKE_CHRONOSPHERE_SELECTION_ONLY")) {
        std::array<int, 2> timedFrames{};
        int rateIndex = 0;
        for (const int hz : {30, 60}) for (const bool routed : {true, false}) {
            auto storage = fixture("Americans", hz); auto& s = *storage;
            s.config.noCooldown = false; s.config.multiplayerActive = false;
            s.runtimeSoundsEnabled = false; s.visionEnabled = false;
            s.runtimeSuppressPlayerAudioFeedback = false;
            const int tank = addUnit(s, "MTNK", WorldObjectKind::Unit, 90, 90, "Commander");
            initializeRuntimeNavigation(s);
            RuntimeHumanHouseAuthority authority;
            authority.owner = "Commander"; authority.canonicalOwner = "commander";
            authority.country = "Americans"; authority.localFacade = true;
            s.humanHouseAuthorities.push_back(std::move(authority));
            bool reject = false;
            int commands = 0;
            if (routed) {
                initializeRuntimeOfflineGameplayRouting(s);
                s.multiplayerIntentRouter.setCaptureCallback([&](const auto& command, std::string& reason) {
                    const auto* body = std::get_if<RuntimeMultiplayerGameplayFireSuperWeaponCommand>(&command.body);
                    if (!body || reject) { reason = "selection probe rejected capture"; return false; }
                    ++commands;
                    return applyRuntimeGameplaySuperWeaponCommand(s, s.economy.owner, *body);
                });
            }
            RuntimeSnapshotActiveStateScope activeScope(s);
            const RECT client{0, 0, 1024, 768};
            const RECT viewport = runtimeMapViewport(client);
            centerRuntimeCameraOnCell(s, client, {93, 93});
            ensureRuntimeCameraForViewport(s, viewport);
            const auto click = [&](RuntimeCellPoint cell) {
                return submitRuntimeSuperWeaponTargetAtPoint(s, viewport,
                    runtimeScreenPointForCell(s, viewport, cell,
                        terrainLevelAtRuntimeCell(s, cell.x, cell.y)));
            };
            const auto selection = [&]() {
                const auto* actors = runtimeImpactRenderActorsForRender(s);
                return std::count_if(actors->begin(), actors->end(), [](const auto& actor) {
                    return actor.animId == "CHRONOAR";
                });
            };
            const auto sourceActor = [&]() -> RuntimeImpactRenderActor {
                const auto* actors = runtimeImpactRenderActorsForRender(s);
                const auto it = std::find_if(actors->begin(), actors->end(), [](const auto& actor) {
                    return actor.animId == "CHRONOAR";
                });
                return it == actors->end() ? RuntimeImpactRenderActor{} : *it;
            };
            const auto begin = [&]() {
                auto context = runtimeSuperWeaponContext(s); context.emit = runtimeSuperWeaponEventSink(s);
                if (!s.superWeapons.forceReady("Commander", "ChronoSphereSpecial", &context)) return false;
                // forceReady changes the instance; a normal tick publishes
                // the Ready sidebar snapshot used by routed input/rendering.
                context.tick = ++s.tick;
                s.superWeapons.tick(context);
                if (routed) return s.multiplayerIntentRouter.beginSuperWeapon("ChronoSphereSpecial", 2, error);
                return s.superWeapons.beginTargetSelection("Commander", "ChronoSphereSpecial", context, &error);
            };
            if (!begin()) return fail("chrono selection setup: " + error);
            const auto before = s.superWeapons.stateHash();
            s.runtimeSoundIntentHistory.clear();
            if (!click({90, 90})) return fail("chrono source UI click");
            if (!routed) flushRuntimeSuperWeaponHostEvents(s);
            if (selection() != 1) return fail("chrono source was not visible immediately after first UI click");
            const auto first = sourceActor();
            if (first.cellX != 90 || first.cellY != 90) return fail("chrono source anchor");
            if (routed) {
                if (commands != 0 || s.superWeapons.stateHash() != before
                    || std::any_of(s.impactEffects.begin(), s.impactEffects.end(), [](const auto& effect) {
                        return effect.animId == "CHRONOAR";
                    })) return fail("local chrono selection changed authoritative effects");
                std::ostringstream saved;
                if (!writeRuntimeStateToStream(s, saved, error)
                    || saved.str().find("CHRONOAR") != std::string::npos)
                    return fail("local chrono selection leaked into saved state: " + error);
            }
            s.mouseClientPointValid = false; s.rightDragActive = true;
            s.repaintRequested = false;
            step(s, hz * 20);
            if (selection() != 1 || (routed && !s.repaintRequested))
                return fail("chrono source disappeared or stopped repainting while waiting or panning");
            const auto later = sourceActor();
            if (later.cellX != first.cellX || later.cellY != first.cellY
                || later.frameIndex < 11 || later.frameIndex >= 17)
                return fail("chrono selection did not stay at source in authored loop");
            if (routed) timedFrames[rateIndex++] = later.frameIndex;
            if (routed) {
                s.tick += hz * 60 * 30;
                if (selection() != 1 || sourceActor().frameIndex < 11 || sourceActor().frameIndex >= 17)
                    return fail("chrono source expired after a long local selection");
            }
            if (!capture(s, "chrono-selection-" + std::to_string(hz) + (routed ? "-routed" : "-direct")))
                return fail(error);
            const auto soundCount = std::count(s.runtimeSoundIntentHistory.begin(),
                s.runtimeSoundIntentHistory.end(), "sound:ChronosphereSelect");
            if (soundCount != 1) return fail("chrono source select sound missing or repeated");
            for (const std::string viewer : {"Ally", "Enemy"}) {
                s.economy.owner = viewer;
                if (selection() != 0) return fail("chrono source visible to another viewer");
            }
            s.economy.owner = "Commander";
            if (selection() != 1) return fail("chrono source lost after owner-only render query");
            s.rightDragActive = false;
            if (routed) {
                reject = true;
                click({103, 100});
                if (commands != 0 || selection() != 1
                    || !s.multiplayerIntentRouter.superWeapon()
                    || s.multiplayerIntentRouter.superWeapon()->targets.size() != 2)
                    return fail("rejected second chrono click cleared source");
                reject = false;
                if (!s.multiplayerIntentRouter.confirmSuperWeapon(error) || commands != 1)
                    return fail("chrono second-click retry: " + error);
            } else if (!click({103, 100})) return fail("chrono destination UI click");
            step(s, 2);
            if (selection() != 0) return fail("chrono source survived accepted second click");
            const auto countAnim = [&](const std::string& id) {
                return std::count_if(s.impactEffects.begin(), s.impactEffects.end(), [&](const auto& effect) {
                    return effect.animId == id;
                });
            };
            if (countAnim("CHRONOTG") != 1 || countAnim("CHRONOFD") != 1
                || findObjectById(s, tank)->x != 103 || findObjectById(s, tank)->y != 100)
                return fail("chrono target animation or actual transfer missing");
            const std::string destinationImage = root.empty() ? "" : root + "/chrono-destination-"
                + std::to_string(hz) + (routed ? "-routed" : "-direct") + ".bmp";
            refreshRuntimeSpecialMechanismRenderSnapshots(s);
            if (!drawRuntimeStateOffscreenAtCell(s, {103, 100}, 960, 660, error, destinationImage, false))
                return fail(error);
            step(s, hz * 10);
            if (countAnim("CHRONOTG") || countAnim("CHRONOFD"))
                return fail("chrono destination animation did not finish");
            if (!begin() || !click({90, 90})) return fail("chrono cancel setup");
            if (!routed) flushRuntimeSuperWeaponHostEvents(s);
            if (selection() != 1) return fail("chrono cancel source was not visible after reselect");
            if (!cancelRuntimeActiveSuperWeaponTargetSelection(s, "probe"))
                return fail("chrono cancel input was not handled");
            flushRuntimeSuperWeaponHostEvents(s);
            if (selection() != 0) return fail("chrono source survived cancel");
            if (!begin() || !click({90, 90})) return fail("chrono source-loss setup");
            if (!routed) flushRuntimeSuperWeaponHostEvents(s);
            for (auto& object : s.world.accessObjects()) {
                const auto* building = s.rules.findBuildingType(object.type);
                if (building && building->superWeapon == "ChronoSphereSpecial") {
                    object.health = 0; object.alive = false;
                }
            }
            step(s);
            if (selection() != 0) return fail("chrono source survived loss of all source buildings");
            std::cout << "Chronosphere selection hz=" << hz << " routed=" << routed
                << " immediate/loop/private/reject/transfer/expire/cancel PASS\n";
        }
        if (timedFrames[0] != timedFrames[1]) return fail("chrono loop animation time differs at 30/60 Hz");
        return true;
    }
