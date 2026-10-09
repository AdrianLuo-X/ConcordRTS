// Every actual barracks completion owes one original Cloning Vat delivery.
// The held scene pauses a naturally generated clone through the real UI;
// neither a production completion nor a clone, lease or timer is fabricated.
bool runRuntimeCloningBatchSmoke(RuntimeWindowState& live, std::string& error) {
    std::cout << std::unitbuf;
    std::string label, stage;
    const auto fail = [&](const std::string& why) {
        error = "Cloning batch " + label + " [" + stage + "]: " + why; return false;
    };
    const char* output = std::getenv("CONCORD_CLONING_BATCH_OUTPUT");
    const std::filesystem::path root = output && *output ? output : "build/issue22_12_cloning_batch";
    const char* requested = std::getenv("CONCORD_CLONING_BATCH_CASE");
    const std::string filter = requested ? requested : "";
    std::filesystem::create_directories(root);
    std::ofstream cases(root / "cases.csv"), orders(root / "orders.csv"), events(root / "events.csv"),
        movement(root / "movement.csv"), pending(root / "pending.csv"), clocks(root / "clocks.csv"), checkpoints(root / "checkpoints.csv");
    if (!cases || !orders || !events || !movement || !pending || !clocks || !checkpoints) return fail("output unavailable");
    cases << "case,hz,mode,queued,produced,cloned,original_cost,credits_before,credits_after,max_pending,blocked_ticks,ordinary14,canonical_extension,result\n";
    orders << "case,ordinal,serial,type,cost,build_ticks,queued_tick\n";
    events << "case,stage,tick,event,ordinal,serial,factory,object,type,credits,pending,lease,animation_end\n";
    movement << std::setprecision(17) << "case,stage,tick,object,role,type,owner,x,y,z,body_x,body_y,body_z,hp,subcell,mission,moving\n";
    pending << "case,stage,tick,index,vat,owner,type\n";
    clocks << "case,stage,tick,completed,original_ids,clone_ids,pending,lease,animation_start,animation_end,credits,queued,active_serial,active_remaining,active_paid\n";
    checkpoints << "case,stage,format,tick,hz,completed,clones,pending,credits,lease,particle_hash,result\n";
    IniFile raw, rawArt;
    if (!raw.loadFromFile(live.report.rulesPath) || !rawArt.loadFromFile(live.report.artPath)) return fail("original rules/art unavailable");
    const auto* vatType = live.rules.findBuildingType("NACLON");
    const auto* barracksType = live.rules.findBuildingType("YABRCK");
    const auto* blockerType = live.rules.findUnitType("vehicle", "MTNK");
    if (!vatType || !barracksType || !vatType->cloning || barracksType->factory != "InfantryType"
        || !blockerType || blockerType->strength != 300 || raw.getValue("MTNK", "Strength", "") != "300"
        || raw.getValue("NACLON", "Cloning", "") != "yes" || raw.getValue("NACLON", "Strength", "") != "1000"
        || rawArt.getValue("NACLON", "Foundation", "") != "2x2" || rawArt.getValue("YABRCK", "Foundation", "") != "2x3")
        return fail("registered stock NACLON/YABRCK declarations differ");
    const std::array<std::string, 5> sequence{{"INIT", "YENGINEER", "INIT", "BRUTE", "INIT"}};
    const std::map<std::string, std::pair<int, int>> originals{{"INIT", {100, 200}}, {"YENGINEER", {75, 500}}, {"BRUTE", {200, 500}}};
    for (const auto& [type, values] : originals) {
        const auto* unit = live.rules.findUnitType("infantry", type);
        if (!unit || unit->strength != values.first || unit->cost != values.second
            || std::stoi(raw.getValue(type, "Strength", "0")) != values.first || std::stoi(raw.getValue(type, "Cost", "0")) != values.second)
            return fail("original infantry registration/HP/cost: " + type);
    }
    const std::uint64_t rulesHash = live.rules.getDetailRulesHash();
    std::ostringstream resourceMode;
    for (const auto& t : live.rules.getTiberiumTypes()) resourceMode << '[' << t.id << "]\nGrowthPercentage=0\nSpreadPercentage=0\n";
    const auto resourceValid = [&](const RuntimeWindowState& s) {
        if (!s.rulesLoaded || s.rules.getDetailRulesHash() != rulesHash || !s.skirmishModeRulesLoaded
            || s.config.noCooldown || s.particles.fixedTickHz() != s.config.fixedTickHz) return false;
        for (const auto& t : live.rules.getTiberiumTypes())
            if (runtimeOreRuleProbability(s, t, false) != 0.0 || runtimeOreRuleProbability(s, t, true) != 0.0) return false;
        return true;
    };
    const auto describe = [](const RuntimeWindowState& s, int id) {
        const auto* o = findRuntimeObjectByIdIncludingInactive(s, id);
        if (!o) return std::string("missing#") + std::to_string(id);
        std::ostringstream d; d << std::setprecision(17) << o->type << '#' << id << " owner=" << o->owner << " HP=" << o->health
            << " alive=" << o->alive << " XYZ=" << o->x << ',' << o->y << ',' << o->level
            << " body=" << runtimeObjectRenderX(*o) << ',' << runtimeObjectRenderY(*o) << ',' << runtimeDrawLevelForObject(*o)
            << " mission=" << o->mission << " subcell=" << o->subCell << " lease=" << o->specialCloneDeliveryObjectId;
        if (const auto b = s.behaviors.find(id); b != s.behaviors.end()) d << " behavior=" << int(b->second.kind) << ':' << int(b->second.state)
            << " target=" << b->second.moveTarget.x << ',' << b->second.moveTarget.y << " cooldown=" << b->second.moveCooldownTicks
            << " path=" << b->second.path.size() << " pathPending=" << b->second.pathRequestPending;
        return d.str();
    };
    const auto nativeStep = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    int passed = 0, producedTotal = 0, cloneTotal = 0, ordinaryTotal = 0, canonicalTotal = 0;
    for (int hz : {30, 60}) for (bool held : {false, true}) {
        label = std::string(held ? "held-exit-" : "open-") + std::to_string(hz);
        if (!filter.empty() && filter != label) continue;
        stage = "original fixture";
        auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box; seedRuntimeLoadScratch(s, live, false);
        s.config.fixedTickHz = hz; s.config.playerCountry = "YuriCountry"; s.config.techLevel = 10; s.config.gameMode = "Battle";
        s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.shortGame = s.config.crates = s.config.superweapons = s.config.noCooldown = false;
        s.config.startingUnits = 0; s.tick = 200; s.campaignMode = false;
        RuntimeConfig::PlayerSlot local; local.enabled = local.human = local.local = true; local.name = local.country = "YuriCountry"; local.color = "Gold";
        RuntimeConfig::PlayerSlot other; other.enabled = true; other.country = "Americans"; other.color = "Red";
        s.config.playerSlots = {local, other};
        s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        s.triggers = {}; s.computerAiByOwner.clear(); s.computerMiningByOwner.clear();
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true; s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear(); s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.terrainByCell.clear(); s.terrainInfoByCell.clear(); s.mapFacts = {}; s.mapFactsLoaded = false;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = s.report.map.height = -1;
        s.behaviors.clear(); s.projectiles.clear(); s.recentExpiredProjectiles.clear(); s.impactEffects.clear();
        s.pendingCloneDeliveries.clear(); s.pendingTransportTargets.clear(); s.selectedObjectIds.clear();
        s.humanHouseAuthorities.clear(); s.specialMechanismsInitialized = s.superWeaponsInitialized = s.subcellsInitialized = false;
        for (int y = 44; y <= 85; ++y) for (int x = 44; x <= 85; ++x) {
            TerrainCell c; c.x = x; c.y = y; c.cellIndex512 = y * 512 + x; c.tileIndex = c.subTileIndex = 0; s.world.accessTerrainCells().push_back(c);
        }
        RuntimeSnapshotActiveStateScope active(s);
        initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s);
        const auto add = [&](const char* type, RuntimeCellPoint point) {
            auto* o = addRuntimeObjectInstanceForOwner(s, "YuriCountry", RuntimeBuildKind::Building, type, point);
            if (!o) return -1; o->mission = "Sleep"; o->buildAnimTicksTotal = o->buildAnimTicksRemaining = 0; return o->instanceId;
        };
        const int factory = add("YABRCK", {52, 62}), vat = add("NACLON", {70, 62});
        if (factory < 0 || vat < 0 || add("YACNST", {47, 47}) < 0) return fail("stock buildings unavailable");
        for (int x : {46, 52, 58, 64}) if (add("YAPOWR", {x, 76}) < 0) return fail("stock power plants unavailable");
        // A weaponless original opponent power plant prevents the normal
        // Battle outcome check from ending this production-only match. It is
        // over twenty cells from the clone exit and every selected rally.
        auto* opposition = addRuntimeObjectInstanceForOwner(s, "SkirmishPlayer2", RuntimeBuildKind::Building, "GAPOWR", {80, 46});
        if (!opposition || opposition->health != 750) return fail("original distant outcome control");
        opposition->mission = "Sleep"; opposition->buildAnimTicksTotal = opposition->buildAnimTicksRemaining = 0;
        const int oppositionId = opposition->instanceId;
        // A real full-health tank occupies only the outward exit cell. Thus
        // the clone is born inside the delivery area without committing the
        // next logical cell before this test can issue its real Stop. Both
        // actors are later released through UI orders; no lease is fabricated.
        int blockerId = -1;
        if (held) {
            auto* blocker = addRuntimeObjectInstanceForOwner(s, "YuriCountry", RuntimeBuildKind::Unit, "MTNK", {71, 65});
            if (!blocker || blocker->health != blockerType->strength) return fail("original occupied-exit tank unavailable");
            blockerId = blocker->instanceId; blocker->mission = "Sleep";
        }
        s.structureFootprints = buildRuntimeStructureFootprints(s); refreshRuntimeObjectOccupancyAfterLocalChange(s);
        initializeRuntimeEconomy(s); s.economy.owner = "YuriCountry"; s.economy.credits = 50000; s.economy.techLevel = 10; s.economy.skirmishStartApplied = true;
        s.skirmishCountryByOwner = {{"yuricountry", "YuriCountry"}, {"skirmishplayer2", "Americans"}};
        s.skirmishTeamByOwner.clear();
        s.triggers.loaded = s.triggers.bootstrapComplete = true;
        s.skirmishModeRulesLoaded = s.skirmishModeRules.loadFromString(resourceMode.str()); s.skirmishModeAlliesAllowed = true;
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority authority;
        authority.owner = authority.country = "YuriCountry"; authority.canonicalOwner = "yuricountry"; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority)); initializeRuntimeOfflineGameplayRouting(s);
        s.particles.reset(hz); s.particlesInitialized = s.particles.alignEmptyStateTick(s.tick);
        ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeSuperWeaponsInitialized(s); ensureRuntimeLightingInitialized(s); rebuildRuntimeEconomyPower(s);
        if (!resourceValid(s) || !s.particlesInitialized || s.economy.completedInfantry != 0 || s.economy.powerProduced < s.economy.powerUsed
            || !findObjectById(s, factory) || !findObjectById(s, vat) || findObjectById(s, vat)->health != 1000)
            return fail("stock clock/power/empty production initialization");
        std::vector<RuntimeBuildCatalogItem> items; int originalCost = 0, manufactureTicks = 0;
        for (const auto& type : sequence) {
            const auto found = std::find_if(s.economy.catalog.begin(), s.economy.catalog.end(), [&](const auto& item) {
                return item.kind == RuntimeBuildKind::Infantry && item.typeId == type;
            });
            if (found == s.economy.catalog.end() || found->cost != originals.at(type).second || found->buildTicks <= 0
                || !runtimeBuildAvailability(s, *found).canQueue) return fail("real Yuri infantry sidebar unavailable: " + type);
            items.push_back(*found); originalCost += found->cost; manufactureTicks += found->buildTicks;
        }
        const int initialCredits = s.economy.credits;
        std::vector<std::uint64_t> serials;
        stage = "real repeated sidebar";
        for (std::size_t n = 0; n < items.size(); ++n) {
            const auto serial = s.economy.localProductionLanes.nextSerial;
            if (!dispatchRuntimeQueueProductionFromUi(s, items[n]) || s.economy.localProductionLanes.nextSerial != serial + 1)
                return fail("sidebar did not commit a distinct paid order: " + s.interactionStatus);
            serials.push_back(serial); orders << label << ',' << n << ',' << serial << ',' << items[n].typeId << ',' << items[n].cost << ',' << items[n].buildTicks << ',' << s.tick << '\n';
        }
        std::vector<int> originalIds, cloneIds; std::set<int> seen, exitMoveOrdered;
        for (const auto& o : s.world.getObjects()) seen.insert(o.instanceId);
        std::unique_ptr<RuntimeWindowState> ordinary, canonical;
        int caseOrdinary = 0, caseCanonical = 0, maxPending = 0, blockedTicks = 0; bool stopped = false, released = false;
        const auto pendingText = [](const RuntimeWindowState& q) {
            std::ostringstream out; for (const auto& p : q.pendingCloneDeliveries) out << p.sourceObjectId << '|' << p.owner << '|' << p.infantryTypeId << ';'; return out.str();
        };
        const auto equal = [&]() {
            for (const auto& pair : {std::pair<const char*, RuntimeWindowState*>{"ordinary14", ordinary.get()}, {"canonical-extension", canonical.get()}}) {
                if (!pair.second) continue; const auto& q = *pair.second;
                if (!resourceValid(q) || q.tick != s.tick || q.economy.credits != s.economy.credits || q.economy.completedInfantry != s.economy.completedInfantry
                    || q.particles.stateHash() != s.particles.stateHash() || pendingText(q) != pendingText(s)
                    || runtimeClonePendingDeliveriesHash(q) != runtimeClonePendingDeliveriesHash(s)
                    || runtimeProductionLanesStateHash(q.economy.localProductionLanes) != runtimeProductionLanesStateHash(s.economy.localProductionLanes)
                    || q.world.getObjects().size() != s.world.getObjects().size()) return fail(std::string(pair.first) + " production/payment/pending/clock differs");
                for (const auto& a : s.world.getObjects()) {
                    const auto* b = findRuntimeObjectByIdIncludingInactive(q, a.instanceId);
                    if (!b || a.type != b->type || a.owner != b->owner || a.health != b->health || a.alive != b->alive || a.x != b->x || a.y != b->y || a.level != b->level
                        || a.subCell != b->subCell || a.mission != b->mission || a.facing != b->facing || a.veterancy != b->veterancy
                        || a.specialCloneDeliveryObjectId != b->specialCloneDeliveryObjectId || a.factoryExitAnimationStartTick != b->factoryExitAnimationStartTick
                        || a.factoryExitAnimationEndTick != b->factoryExitAnimationEndTick || a.renderMoveActive != b->renderMoveActive
                        || std::abs(runtimeObjectRenderX(a) - runtimeObjectRenderX(*b)) > 1e-8 || std::abs(runtimeObjectRenderY(a) - runtimeObjectRenderY(*b)) > 1e-8
                        || std::abs(runtimeDrawLevelForObject(a) - runtimeDrawLevelForObject(*b)) > 1e-8)
                        return fail(std::string(pair.first) + " object differs original=" + describe(s, a.instanceId) + " restored=" + describe(q, a.instanceId));
                }
            }
            return true;
        };
        const auto replicas = [&]() { return std::array<RuntimeWindowState*, 3>{{&s, ordinary.get(), canonical.get()}}; };
        const auto command = [&](int id, bool stop, RuntimeCellPoint destination) {
            for (auto* q : replicas()) if (q) {
                RuntimeSnapshotActiveStateScope scope(*q); q->selectedObjectIds = {id};
                const bool accepted = stop ? dispatchRuntimeSelectionCommandFromUi(*q, RuntimeMultiplayerGameplayCommandType::Stop)
                    : dispatchRuntimeTargetOrderFromUi(*q, RuntimeMultiplayerGameplayTargetOrderKind::Move, destination);
                if (!accepted || q->lastCommand.acceptedCount != 1) return fail("real exit actor UI order rejected: " + describe(*q, id));
            }
            return equal();
        };
        const auto insideDeliveryArea = [&](const WorldObjectInstance& o) { return o.x >= 69 && o.x <= 72 && o.y >= 61 && o.y <= 64; };
        const auto cloneParking = [](std::size_t ordinal) { return RuntimeCellPoint{78 + int(ordinal % 3), 68 + int(ordinal / 3)}; };
        const auto settled = [&](const RuntimeWindowState& q) {
            for (const auto& o : q.world.getObjects()) if ((o.kind == WorldObjectKind::Infantry || o.instanceId == blockerId)
                && (o.renderMoveActive || q.behaviors.contains(o.instanceId))) return false;
            return true;
        };
        const auto step = [&]() {
            const int beforeCompleted = s.economy.completedInfantry;
            std::map<int, std::pair<double, double>> previous;
            for (const auto& o : s.world.getObjects()) if (o.kind == WorldObjectKind::Infantry) previous[o.instanceId] = {runtimeObjectRenderX(o), runtimeObjectRenderY(o)};
            nativeStep(s); if (ordinary) nativeStep(*ordinary); if (canonical) nativeStep(*canonical);
            std::vector<int> newOriginals, newClones;
            for (const auto& o : s.world.getObjects()) if (!seen.contains(o.instanceId)) {
                seen.insert(o.instanceId);
                if (o.kind != WorldObjectKind::Infantry || o.owner != "YuriCountry" || !o.alive || !originals.contains(o.type) || o.health != originals.at(o.type).first)
                    return fail("unexpected or non-stock new object: " + describe(s, o.instanceId));
                // Raw foundations are 2x3 and 2x2. Their exit search footprints
                // are separated by eighteen cells, so birthplace identifies
                // the two real producers without asking the clone helper.
                const int factoryDistance = std::max({52 - o.x, o.x - 53, 62 - o.y, o.y - 64, 0});
                const int vatDistance = std::max({70 - o.x, o.x - 71, 62 - o.y, o.y - 63, 0});
                if (factoryDistance <= 8 && vatDistance > 8) newOriginals.push_back(o.instanceId);
                else if (vatDistance <= 8 && factoryDistance > 8) newClones.push_back(o.instanceId);
                else return fail("ambiguous real birthplace: " + describe(s, o.instanceId));
            }
            if (s.economy.completedInfantry - beforeCompleted != int(newOriginals.size()) || newOriginals.size() > 1 || newClones.size() > 1)
                return fail("actual completion/new original/new clone per-tick mismatch");
            const auto logEvent = [&](const char* kind, int ordinal, int id, int host) {
                const auto* o = findObjectById(s, id); const auto* v = findObjectById(s, vat);
                events << label << ',' << stage << ',' << s.tick << ',' << kind << ',' << ordinal << ',' << serials[ordinal] << ',' << host << ',' << id << ',' << o->type
                    << ',' << s.economy.credits << ',' << s.pendingCloneDeliveries.size() << ',' << v->specialCloneDeliveryObjectId << ',' << v->factoryExitAnimationEndTick << '\n';
            };
            for (int id : newOriginals) {
                const int ordinal = int(originalIds.size());
                if (ordinal >= 5 || findObjectById(s, id)->type != sequence[ordinal]) return fail("original sidebar FIFO/type differs");
                for (const auto& lane : s.economy.localProductionLanes.lanes) {
                    if ((lane.active && lane.active->serial == serials[ordinal]) || (lane.ready && lane.ready->serial == serials[ordinal])) return fail("completed serial still active/ready");
                    for (const auto& order : lane.queued) if (order.serial == serials[ordinal]) return fail("completed serial remains queued");
                }
                originalIds.push_back(id); logEvent("original-completion", ordinal, id, factory);
            }
            for (int id : newClones) {
                const int ordinal = int(cloneIds.size());
                if (ordinal >= int(originalIds.size()) || ordinal >= 5 || findObjectById(s, id)->type != sequence[ordinal]
                    || std::find(originalIds.begin(), originalIds.end(), id) != originalIds.end()) return fail("clone FIFO/type/source ID reuse differs");
                cloneIds.push_back(id); logEvent("clone-delivery", ordinal, id, vat);
            }
            if (s.pendingCloneDeliveries.size() != originalIds.size() - cloneIds.size()) return fail("every real completion did not retain exactly one clone obligation");
            for (std::size_t n = 0; n < s.pendingCloneDeliveries.size(); ++n) {
                const auto& p = s.pendingCloneDeliveries[n];
                if (p.sourceObjectId != vat || p.owner != "YuriCountry" || p.infantryTypeId != sequence[cloneIds.size() + n]) return fail("pending vat/type/owner FIFO differs");
                pending << label << ',' << stage << ',' << s.tick << ',' << n << ',' << p.sourceObjectId << ',' << p.owner << ',' << p.infantryTypeId << '\n';
            }
            maxPending = std::max(maxPending, int(s.pendingCloneDeliveries.size()));
            if (held && !released && !cloneIds.empty()) {
                if (!stopped) { if (!command(cloneIds.front(), true, {})) return false; stopped = true; }
                const auto* first = findObjectById(s, cloneIds.front()); const auto* v = findObjectById(s, vat);
                if (cloneIds.size() != 1 || !first || !insideDeliveryArea(*first) || first->renderMoveActive || s.behaviors.contains(first->instanceId)
                    || !v || v->specialCloneDeliveryObjectId != first->instanceId) return fail("real Stop did not hold the original occupied clone exit");
                if (!s.pendingCloneDeliveries.empty()) ++blockedTicks;
            }
            // The stock vat's automatic exit is only one cell long. Without
            // player orders, three stationary infantry can fill that cell
            // and legitimately block later exits. First prove the native
            // exit finished, then move each settled actor once through the
            // real UI to a different safe cell. The held first actor is
            // exempt until its explicit player release below.
            if (!held || released) for (std::size_t n = 0; n < cloneIds.size(); ++n) {
                const int id = cloneIds[n]; const auto* clone = findObjectById(s, id);
                if (!clone || exitMoveOrdered.contains(id) || insideDeliveryArea(*clone)
                    || clone->renderMoveActive || s.behaviors.contains(id)) continue;
                if (!command(id, false, cloneParking(n))) return false;
                exitMoveOrdered.insert(id); logEvent("real-post-exit-ui-move", int(n), id, vat);
            }
            for (const auto& o : s.world.getObjects()) if (o.kind == WorldObjectKind::Infantry) {
                if (!o.alive || !originals.contains(o.type) || o.health != originals.at(o.type).first || o.owner != "YuriCountry") return fail("generated original health/identity changed");
                const auto found = previous.find(o.instanceId); const auto* t = s.rules.findUnitType("infantry", o.type);
                if (found != previous.end() && t) {
                    const double distance = std::hypot(runtimeObjectRenderX(o) - found->second.first, runtimeObjectRenderY(o) - found->second.second);
                    const double originalStep = 1.25 * std::min(255, t->speed * 256 / 100) * 15.0 / (256.0 * hz);
                    if (!std::isfinite(distance) || distance > originalStep + 1e-6) return fail("noncontinuous original infantry body: " + describe(s, o.instanceId));
                }
                const bool copy = std::find(cloneIds.begin(), cloneIds.end(), o.instanceId) != cloneIds.end();
                movement << label << ',' << stage << ',' << s.tick << ',' << o.instanceId << ',' << (copy ? "clone" : "original") << ',' << o.type << ',' << o.owner
                    << ',' << o.x << ',' << o.y << ',' << o.level << ',' << runtimeObjectRenderX(o) << ',' << runtimeObjectRenderY(o) << ',' << runtimeDrawLevelForObject(o)
                    << ',' << o.health << ',' << o.subCell << ',' << o.mission << ',' << o.renderMoveActive << '\n';
            }
            const auto* v = findObjectById(s, vat); const auto& lane = s.economy.localProductionLanes.lanes[runtimeProductionLaneIndex(RuntimeProductionLane::Infantry)];
            if (held) {
                const auto* blocker = findObjectById(s, blockerId); const auto* opponent = findObjectById(s, oppositionId);
                if (!blocker || blocker->health != blockerType->strength || !opponent || opponent->health != 750
                    || runtimeCellDistanceToObjectFootprint(s, runtimeCellPointForObject(*blocker), *opponent)
                        <= runtimeCachedCapabilityProfileForObject(s, *blocker).guardScanRange + 0.001)
                    return fail("original blocker health or base/elite guard separation: " + describe(s, blockerId));
                movement << label << ',' << stage << ',' << s.tick << ',' << blocker->instanceId << ",exit-blocker," << blocker->type << ',' << blocker->owner
                    << ',' << blocker->x << ',' << blocker->y << ',' << blocker->level << ',' << runtimeObjectRenderX(*blocker) << ',' << runtimeObjectRenderY(*blocker)
                    << ',' << runtimeDrawLevelForObject(*blocker) << ',' << blocker->health << ',' << blocker->subCell << ',' << blocker->mission << ',' << blocker->renderMoveActive << '\n';
            }
            if (!v || v->health != 1000 || !resourceValid(s) || !s.projectiles.empty() || s.skirmishMatchFinished)
                return fail("vat/stock context, unrelated combat or outcome changed: " + s.skirmishResult);
            clocks << label << ',' << stage << ',' << s.tick << ',' << s.economy.completedInfantry << ',' << originalIds.size() << ',' << cloneIds.size() << ',' << s.pendingCloneDeliveries.size()
                << ',' << v->specialCloneDeliveryObjectId << ',' << v->factoryExitAnimationStartTick << ',' << v->factoryExitAnimationEndTick << ',' << s.economy.credits
                << ',' << lane.queued.size() << ',' << (lane.active ? lane.active->serial : 0) << ',' << (lane.active ? lane.active->remainingTicks : 0) << ',' << (lane.active ? lane.active->creditsSpent : 0) << '\n';
            return equal();
        };
        const auto wait = [&](int budget, const auto& ready, const std::string& why) {
            for (int n = 0; n < budget; ++n) { if (ready()) return true; if (!step()) return false; }
            return ready() ? true : fail(why + " completed=" + std::to_string(s.economy.completedInfantry) + " clones=" + std::to_string(cloneIds.size())
                + " pending=" + std::to_string(s.pendingCloneDeliveries.size()) + " status=" + s.economy.status + " vat=" + describe(s, vat));
        };
        const auto checkpoint = [&]() {
            if (!settled(s)) return fail("checkpoint is not a naturally settled clone/barracks boundary");
            for (bool exact : {false, true}) {
                std::ostringstream payload, normalized; IniFile base, extension;
                if (!writeRuntimeStateToStream(s, payload, error) || !base.loadFromString(payload.str()) || base.getValue("RuntimeSave", "Version", "") != "14")
                    return fail("ordinary14 checkpoint: " + error);
                const std::string suffix = exact ? "canonical-extension" : "ordinary14";
                if (exact && (!writeRuntimeMultiplayerSnapshotExtension(s, payload, error) || !extension.loadFromString(payload.str())
                    || !writeRuntimeMultiplayerSnapshotCanonicalIni(extension, normalized, error) || !extension.loadFromString(normalized.str()))) return fail("canonical extension: " + error);
                const auto savedPath = root / (label + '-' + stage + '-' + suffix + ".ini");
                std::ofstream saved(savedPath, std::ios::binary); saved << (exact ? normalized.str() : payload.str()); saved.close();
                if (!saved) return fail("checkpoint output write failed");
                auto copy = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*copy, s, false); RuntimeSnapshotActiveStateScope destination(*copy);
                // Ordinary copies use the same full transactional loader as
                // the load menu, including its final state commit. The
                // canonical document remains a contextual extension of v14.
                if ((!exact && !loadRuntimeState(*copy, savedPath.string(), error))
                    || (exact && (!materializeRuntimeLoadStateFromIni(*copy, base, error)
                        || !materializeRuntimeMultiplayerSnapshotExtension(*copy, extension, error)))) return fail("strict restore: " + error);
                copy->runtimeSoundsEnabled = copy->runtimePreferencePersistenceEnabled = copy->visionEnabled = false; initializeRuntimeOfflineGameplayRouting(*copy);
                const auto* v = findObjectById(*copy, vat);
                if (!v || !resourceValid(*copy)) return fail("restored raw rules/vat clock context differs");
                checkpoints << label << ',' << stage << ',' << (exact ? "ordinary14+canonical-extension" : "ordinary14") << ',' << copy->tick << ',' << hz
                    << ',' << copy->economy.completedInfantry << ',' << cloneIds.size() << ',' << copy->pendingCloneDeliveries.size() << ',' << copy->economy.credits
                    << ',' << v->specialCloneDeliveryObjectId << ',' << copy->particles.stateHash() << ",PASS\n";
                if (exact) { canonical = std::move(copy); ++caseCanonical; ++canonicalTotal; }
                else { ordinary = std::move(copy); ++caseOrdinary; ++ordinaryTotal; }
            }
            return equal();
        };
        stage = held ? "held original clone exit" : "natural continuous production";
        if (!wait(manufactureTicks + hz * 90, [&]() { return originalIds.size() == 5 && (held ? cloneIds.size() == 1 : cloneIds.size() == 5) && settled(s); }, "original five paid orders never naturally completed")) return false;
        if (s.economy.completedInfantry != 5 || s.economy.credits != initialCredits - originalCost) return fail("original count/payment is not exactly five paid units");
        if (held) {
            stage = "blocked-obligations";
            for (int n = 0; n < hz * 3; ++n) if (!step()) return false;
            if (!stopped || cloneIds.size() != 1 || s.pendingCloneDeliveries.size() != 4 || maxPending != 4 || blockedTicks <= 0) return fail("occupied original exit did not retain all four obligations");
            if (!checkpoint()) return false;
            // The vehicle must physically clear the exit before the first
            // clone is released. Moving both at once can make the original
            // dynamic path fallback stop a later clone inside the door.
            // Keep the real stopped clone and all four obligations intact
            // during this genuine vehicle movement in all three replicas.
            stage = "real UI blocker clearance";
            if (!command(blockerId, false, {76, 60})) return false;
            if (!wait(hz * 90, [&]() {
                const auto* blocker = findObjectById(s, blockerId);
                return blocker && blocker->x == 76 && blocker->y == 60 && !blocker->renderMoveActive && !s.behaviors.contains(blockerId)
                    && std::abs(runtimeObjectRenderX(*blocker) - 76.0) <= 1e-8 && std::abs(runtimeObjectRenderY(*blocker) - 60.0) <= 1e-8;
            }, "real occupied-exit vehicle never physically cleared")) return false;
            if (cloneIds.size() != 1 || s.pendingCloneDeliveries.size() != 4 || findObjectById(s, vat)->specialCloneDeliveryObjectId != cloneIds.front())
                return fail("real blocker clearance did not preserve one stopped clone and four FIFO obligations");
            stage = "real UI first clone release"; released = true;
            if (!command(cloneIds.front(), false, cloneParking(0))) return false;
            exitMoveOrdered.insert(cloneIds.front());
            events << label << ',' << stage << ',' << s.tick << ",real-held-release-ui-move,0," << serials.front() << ',' << vat << ',' << cloneIds.front()
                << ',' << sequence.front() << ',' << s.economy.credits << ',' << s.pendingCloneDeliveries.size() << ',' << findObjectById(s, vat)->specialCloneDeliveryObjectId
                << ',' << findObjectById(s, vat)->factoryExitAnimationEndTick << '\n';
        }
        stage = "natural FIFO delivery completion";
        if (!wait(hz * 90, [&]() { const auto* v = findObjectById(s, vat); return cloneIds.size() == 5 && s.pendingCloneDeliveries.empty()
                && v && v->specialCloneDeliveryObjectId < 0 && settled(s); }, "five clone obligations never naturally drained")) return false;
        if (originalIds.size() != 5 || cloneIds.size() != 5 || s.economy.completedInfantry != 5 || s.economy.credits != initialCredits - originalCost) return fail("final count/free clone/payment differs");
        if (exitMoveOrdered.size() != 5) return fail("not every actual clone completed its one real UI dispersal");
        for (std::size_t n = 0; n < cloneIds.size(); ++n) {
            const auto* clone = findObjectById(s, cloneIds[n]); const auto destination = cloneParking(n);
            if (!clone || clone->x != destination.x || clone->y != destination.y || clone->renderMoveActive || s.behaviors.contains(cloneIds[n])
                || std::abs(runtimeObjectRenderX(*clone) - destination.x) > 1e-8 || std::abs(runtimeObjectRenderY(*clone) - destination.y) > 1e-8)
                return fail("natural clone dispersal arrival differs: " + describe(s, cloneIds[n]));
        }
        const std::set<int> sourceIds(originalIds.begin(), originalIds.end()), copyIds(cloneIds.begin(), cloneIds.end());
        if (sourceIds.size() != 5 || copyIds.size() != 5) return fail("original or clone ID reused");
        for (int id : sourceIds) if (copyIds.contains(id)) return fail("clone reused an original ID");
        stage = "delivered-save"; if (held && !checkpoint()) return false;
        stage = "no duplicate hold"; const auto objectCount = s.world.getObjects().size();
        for (int n = 0; n < hz * 6; ++n) if (!step()) return false;
        if (s.world.getObjects().size() != objectCount || originalIds.size() != 5 || cloneIds.size() != 5 || !s.pendingCloneDeliveries.empty()
            || findObjectById(s, vat)->specialCloneDeliveryObjectId >= 0 || s.economy.credits != initialCredits - originalCost) return fail("completed/loaded vat replayed a clone or charged again");
        cases << label << ',' << hz << ',' << (held ? "held-exit" : "open") << ",5," << originalIds.size() << ',' << cloneIds.size() << ',' << originalCost
            << ',' << initialCredits << ',' << s.economy.credits << ',' << maxPending << ',' << blockedTicks << ',' << caseOrdinary << ',' << caseCanonical << ",PASS\n";
        ++passed; producedTotal += int(originalIds.size()); cloneTotal += int(cloneIds.size());
        std::cout << "Cloning batch " << label << " PASS realSidebar=5 original=5 clone=5 freeClone=yes blockedTicks=" << blockedTicks
            << " ordinary14=" << caseOrdinary << " canonical=" << caseCanonical << '\n';
    }
    if ((!filter.empty() && passed != 1) || (filter.empty() && (passed != 4 || producedTotal != 20 || cloneTotal != 20 || ordinaryTotal != 4 || canonicalTotal != 4)))
        return fail("exact native clone/production/save coverage count");
    std::cout << "Cloning batch passed: cases=" << passed << " originals=" << producedTotal << " clones=" << cloneTotal
        << " ordinary14=" << ordinaryTotal << " canonicalExtensions=" << canonicalTotal << " rawRulesHash=" << rulesHash << '\n';
    return true;
}
