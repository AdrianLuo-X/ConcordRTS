// Compare the complete frozen pre-S17-02 worker path with the production caller.
// The oracle below was copied before changing any production economy logic.
#include "simulation/ai/diagnostics/RuntimeSlaveWorkerEconomyOldOracle.inl"

bool runRuntimeSlaveWorkerEconomyContextSmoke(const RuntimeConfig& config, std::string& error) try {
    const char* configured = std::getenv("CONCORD_SLAVE_WORKER_ECONOMY_OUTPUT");
    if (!configured || !*configured) { error = "house economy requires isolated output"; return false; }
    const std::filesystem::path output = configured;
    std::filesystem::create_directories(output);
    auto stock = std::make_unique<RuntimeWindowState>();
    stock->config = config;
    stock->report = buildRuntimeStartupReport(config, &stock->world, &stock->theater,
        &stock->art, &stock->rules, &stock->mapFacts);
    if (!stock->report.success) { error = "house economy stock inputs unavailable"; return false; }
    int checks = 0, cases = 0, loads = 0, pairedTicks = 0; int repacks = 0, repackOwnerMask = 0, captures = 0, deaths = 0;
    int workerDeaths = 0, workerCaptures = 0, workerOwnerMask = 0;
    std::array<long long, 3> observedLoads{}, observedTrips{}, cargoObservations{}, gemCargoObservations{},
        storedObservations{}, regenObservations{}, returningObservations{}, harvestingObservations{};
    const auto need = [&](bool ok, const std::string& why) { ++checks; if (!ok) error = why; return ok; };
    struct OracleBinding {
        explicit OracleBinding(bool old) { runtimeComputerMiningDiagnosticOverride = old ? s1702OldAdvanceRuntimeComputerMining : nullptr; runtimeSlaveWorkerMiningDiagnosticOverride = old ? s1702OldAdvanceRuntimeSlaveMinerEconomy : nullptr; }
        ~OracleBinding() { runtimeComputerMiningDiagnosticOverride = nullptr; runtimeSlaveWorkerMiningDiagnosticOverride = nullptr; }
    };
    const auto trace = [](const RuntimeWindowState& s) {
        std::ostringstream out;
        out << std::setprecision(17) << s.tick << '|' << s.world.deterministicSimulationStateHash()
            << '|' << s.runtimeSoundEvents << '|' << s.runtimeEvaEvents << '|' << s.runtimeEvaSuppressedEvents
            << '|' << s.lastRuntimeSound << '|' << s.interactionStatus << '|' << s.simulationStatus
            << '|' << s.economy.owner << '|' << s.economy.credits << '|' << s.economy.resourceTrips
            << '|' << s.economy.storedResourceUnits << '|' << s.economy.storedResourceValue
            << '|' << s.economy.storedResourceGemUnits << '|' << s.economy.harvesterTeleports
            << '|' << s.economy.status << '|' << s.techTreeLoaded << '|';
        out << s.techTree.house << '|' << s.techTree.totalNodes << '|' << s.techTree.buildableNodes << '\n';
        for (const auto& intent : s.runtimeSoundIntentHistory) out << "sound=" << intent << '\n';
        std::vector<std::string> owners;
        for (const auto& [owner, ai] : s.computerAiByOwner) { (void)ai; owners.push_back(owner); }
        std::sort(owners.begin(), owners.end());
        for (const auto& owner : owners) {
            const auto& ai = s.computerAiByOwner.at(owner);
            out << owner << '|' << ai.credits << '|' << ai.incomeEarned << '|' << ai.nextThinkTick
                << '|' << ai.nextBuildTick << '|' << ai.nextAttackTick << '|' << ai.creditsSpent << '\n';
        }
        writeRuntimeComputerMining(out, s);
        for (const auto& [owner, house] : s.triggers.campaignHouseProduction)
            out << "campaign-account=" << owner << '|' << house.credits << '\n';
        std::vector<int> regenIds;
        for (const auto& [id, ticks] : s.slaveMinerRegenTicks) { (void)ticks; regenIds.push_back(id); }
        std::sort(regenIds.begin(), regenIds.end());
        for (int id : regenIds) out << "slave-regen=" << id << '|' << s.slaveMinerRegenTicks.at(id) << '\n';
        for (int slot = 0; slot < 3; ++slot) {
            const auto owner = runtimeSkirmishOwnerForSlot(slot);
            const auto key = runtimeRepairDepotCanonicalOwnerId(s, owner);
            if (slot == 0) out << "worker-owner-account=" << owner << '|' << s.economy.credits << '|' << s.economy.resourceTrips << '|' << s.economy.slaveMinerLoads << '|' << s.economy.harvesterTeleports << '\n';
            else if (const auto found = s.computerMiningByOwner.find(key); found != s.computerMiningByOwner.end())
                out << "worker-owner-account=" << owner << '|' << runtimeRepairDepotCreditBalance(s, owner) << '|' << found->second.resourceTrips << '|' << found->second.slaveMinerLoads << '|' << found->second.harvesterTeleports << '\n';
        }
        std::vector<int> workers;
        for (const auto& [id, task] : s.slaveMinerTasks) { (void)task; workers.push_back(id); }
        std::sort(workers.begin(), workers.end());
        for (int id : workers) out << "slave-task=" << id << '|' << serializeRuntimeSlaveMinerTask(s.slaveMinerTasks.at(id)) << '\n';
        for (int id : workers) {
            const auto* worker = findRuntimeObjectByIdIncludingInactive(s, id);
            const auto* parent = findRuntimeObjectByIdIncludingInactive(s, s.slaveMinerTasks.at(id).refineryId);
            out << "worker-owner-parent=" << id << '|' << (worker ? worker->owner : "missing") << '|'
                << (parent ? parent->owner : "missing") << '|' << (worker ? worker->health : -1) << '|'
                << (worker ? worker->alive : false) << '|' << (worker ? worker->mission : "missing") << '\n';
        }
        std::vector<int> ids;
        for (const auto& [id, task] : s.harvesterTasks) { (void)task; ids.push_back(id); }
        std::sort(ids.begin(), ids.end());
        for (int id : ids) out << id << '|' << serializeRuntimeHarvesterTask(s.harvesterTasks.at(id)) << '\n';
        return out.str();
    };
    const auto authorityTrace = [](const RuntimeWindowState& s) {
        std::ostringstream out; out << std::setprecision(17) << "tick=" << s.tick << '\n';
        std::vector<int> behaviorIds;
        for (const auto& [id, behavior] : s.behaviors) { (void)behavior; behaviorIds.push_back(id); }
        std::sort(behaviorIds.begin(), behaviorIds.end());
        for (int id : behaviorIds) out << "behavior=" << id << '|' << runtimeMultiplayerSnapshotBehaviorRecord(id, s.behaviors.at(id)) << '\n';
        const auto& e = s.economy;
        out << "ui=" << e.owner << '|' << e.techLevel << '|' << e.credits << '|' << e.powerProduced << '|'
            << e.powerUsed << '|' << e.harvesters << '|' << e.refineries << '|' << e.incomeCooldownTicks << '|'
            << e.storedResourceUnits << '|' << e.storedResourceValue << '|' << e.storedResourceGemUnits << '|'
            << e.resourceStorageCapacity << '|' << e.resourceTrips << '|' << e.harvesterTeleports << '|' << e.slaveMinerLoads << '|'
            << e.lastResourceStoredUnits << '|' << e.lastResourceStoredValue << '|' << e.lastResourceStoredGemUnits << '|'
            << e.lastResourceStorageOverflowUnits << '|' << e.lastResourceStorageCapacity << '|'
            << e.lastResourceBaseValue << '|' << e.lastResourcePurifierBonus << '|' << e.lastResourcePurifierCount << '|'
            << e.lastHarvesterGatherCell.x << '|' << e.lastHarvesterGatherCell.y << '|'
            << e.lastHarvesterGatherBeforeBails << '|' << e.lastHarvesterGatherAfterBails << '|'
            << e.lastHarvesterGatherOverlayBefore << '|' << e.lastHarvesterGatherOverlayAfter << '|'
            << e.lastRefinerySmokeCell.x << '|' << e.lastRefinerySmokeCell.y << '|' << e.lastRefinerySmokeEffects << '|'
            << e.lastRefinerySmokeParticleSystem << '|' << e.lastRefinerySmokeParticle << '|' << e.lastRefinerySmokeImage << '|'
            << e.lastHarvesterTooFarApproachTarget.x << '|' << e.lastHarvesterTooFarApproachTarget.y << '|'
            << e.lastHarvesterTooFarDistance << '|' << e.lastHarvesterTooFarThreshold << '|' << e.lastHarvesterTooFarChrono << '|'
            << e.catalog.size() << '|' << e.localProductionLanesInitialized << '|' << e.selectedPlacementSerial << '|'
            << e.placementCell.x << '|' << e.placementCell.y << '|' << e.placementValid << '|'
            << e.completedBuildings << '|' << e.completedUnits << '|' << e.completedInfantry << '|' << e.status << '\n';
        const auto& c = s.lastCommand;
        out << "last-command=" << c.valid << '|' << c.verb << '|' << c.tick << '|' << c.selectedCount << '|'
            << c.acceptedCount << '|' << c.rejectedCount << '|' << c.targetX << '|' << c.targetY << '|'
            << c.targetObjectId << '|' << c.targetObjectType << '|' << c.inputAckImmediate << '|' << c.feedbackLatencyTicks << '\n';
        out << "audio=" << s.runtimeSoundEvents << '|' << s.runtimeEvaEvents << '|' << s.runtimeEvaPlayableEvents << '|'
            << s.runtimeEvaSuppressedEvents << '|' << s.runtimeEvaQueuedEvents << '|' << s.runtimeEvaDequeuedEvents << '|'
            << s.runtimeEvaSubtitleEvents << '|' << s.lastRuntimeSound << '|' << s.runtimeSoundPlaybackHistory.size() << '|'
            << s.runtimeSuppressPlayerAudioFeedback << '|' << s.techTreeLoaded << '\n';
        for (const auto& intent : s.runtimeSoundIntentHistory) out << "intent=" << intent << '\n';
        for (int id : s.selectedObjectIds) out << "selected=" << id << '\n';
        for (int page : s.buildPageByTab) out << "page=" << page << '\n';
        for (int remainder : s.buildWheelRemainderByTab) out << "wheel=" << remainder << '\n';
        return out.str();
    };

    const auto save = [&](RuntimeWindowState& s, const std::string& name, std::string& ordinary) {
        RuntimeSnapshotActiveStateScope active(s);
        std::ostringstream bytes, extension;
        std::string why;
        if (!writeRuntimeStateToStream(s, bytes, why)
            || !writeRuntimeMultiplayerSnapshotExtension(s, extension, why)) { error = why; return std::string{}; }
        ordinary = bytes.str();
        std::ostringstream tech; printTechTreeReport(tech, s.techTree);
        const std::string complete = trace(s) + "\nTECH\n" + tech.str() + "\nORDINARY\n" + ordinary + "\nEXTENSION\n" + extension.str();
        std::ofstream file(output / (name + ".complete.txt"), std::ios::binary); file << complete;
        return complete;
    };
    for (const std::string scenario : {"Easy", "Medium", "Hard", "campaign", "no-ore", "no-refinery", "loss", "mixed-cargo"}) {
        ++cases;
        std::array<std::string, 2> records, authorityRecords;
        for (int route = 0; route < 2; ++route) {
            OracleBinding binding(route == 0);
            auto box = std::make_unique<RuntimeWindowState>(); auto& s = *box;
            s.config = config; s.config.fixedTickHz = 30; s.config.computerAiEnabled = true;
            s.config.multiplayerActive = false; s.config.crates = false; s.config.shortGame = false;
            s.config.superweapons = false; s.config.startingCredits = 0; s.config.playerSlots.clear();
            s.config.playerSlots.resize(3);
            for (int i = 0; i < 3; ++i) {
                auto& slot = s.config.playerSlots[i]; slot.enabled = true; slot.human = i == 0;
                slot.local = i == 0; slot.country = i == 2 ? "Russians" : "Americans";
                slot.difficulty = scenario == "Easy" ? "Easy" : scenario == "Hard" ? "Hard" : "Medium";
                slot.team = i == 2 ? "2" : "1"; slot.startPosition = i + 1;
                const auto key = toLowerAsciiLocal(runtimeSkirmishOwnerForSlot(i));
                s.skirmishCountryByOwner[key] = slot.country; s.skirmishDifficultyByOwner[key] = slot.difficulty; s.skirmishTeamByOwner[key] = slot.team;
            }
            s.rules = stock->rules; s.rulesLoaded = true; s.art = stock->art; s.artLoaded = true; s.report = stock->report;
            s.runtimePreferencePersistenceEnabled = false; s.runtimeSoundsEnabled = false;
            s.runtimeSuppressPlayerAudioFeedback = false; s.visionEnabled = false; s.triggers.bootstrapComplete = true;
            s.economy.owner = runtimeSkirmishOwnerForSlot(0); s.economy.credits = 12345;
            s.economy.skirmishStartApplied = true; s.economy.status = "local building ready";
            s.interactionStatus = "local selection"; s.techTree = analyzeTechTree(s.rules, "Americans"); s.techTreeLoaded = true;
            for (int y = 1; y <= 70; ++y) for (int x = 1; x <= 48; ++x) {
                TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
                cell.tileIndex = cell.subTileIndex = 0;
                if (scenario != "no-ore" && x >= 23 && x <= 28 && ((y >= 8 && y <= 10) || (y >= 18 && y <= 20) || (y >= 57 && y <= 59)))
                    setRuntimeResourceTerrainBails(cell, scenario == "mixed-cargo" && x % 2 == 0 ? "GEM" : "TIB", 12);
                s.world.accessTerrainCells().push_back(cell);
            }
            const auto add = [&](int slot, RuntimeBuildKind kind, const std::string& type, RuntimeCellPoint cell) {
                auto* object = addRuntimeObjectInstanceForOwner(s, runtimeSkirmishOwnerForSlot(slot), kind, type, cell);
                if (!object) return -1;
                object->buildAnimTicksTotal = object->buildAnimTicksRemaining = 0;
                object->mission = "Guard"; object->harvestAutoSearchSuspended = false; return object->instanceId;
            };
            if (!need(add(0, RuntimeBuildKind::Building, "GACNST", {40, 2}) >= 0, "local base")) return false;
            ensureRuntimeComputerAiStates(s);
            std::array<int, 2> miners{}, refineries{};
            for (int i = 0; i < 2; ++i) {
                refineries[i] = scenario == "no-refinery" ? -1 : add(i + 1, RuntimeBuildKind::Building,
                    i == 0 ? "GAREFN" : "NAREFN", {5, 5 + i * 49});
                miners[i] = add(i + 1, RuntimeBuildKind::Unit, i == 0 ? "CMIN" : "HARV", {10, 10 + i * 49});
                if (!need(miners[i] >= 0, "two AI miners")) return false;
                auto& ai = s.computerAiByOwner.at(toLowerAsciiLocal(runtimeSkirmishOwnerForSlot(i + 1)));
                ai.nextThinkTick = 100000000; ai.credits = 100 + i * 37;
            }
            if (scenario == "campaign") {
                s.campaignMode = true; s.config.campaignDifficulty = "Hard";
                s.config.gameMode = "CampaignMission";
                s.config.campaignId = "s17-02-owned-fixture"; s.config.campaignMissionId = "two-owner-mining";
                for (int i = 1; i <= 2; ++i) {
                    RuntimeCampaignHouseProductionState house; house.owner = runtimeSkirmishOwnerForSlot(i);
                    house.country = i == 1 ? "Americans" : "Russians"; house.side = i == 1 ? 1 : 2;
                    s.triggers.campaignHouseProduction[toLowerAsciiLocal(house.owner)] = house;
                }
            }
            if (scenario == "campaign") {
                const auto a = toLowerAsciiLocal(runtimeSkirmishOwnerForSlot(0));
                const auto b = toLowerAsciiLocal(runtimeSkirmishOwnerForSlot(1));
                s.triggers.campaignAlliedPairs.insert(a < b ? a + "|" + b : b + "|" + a);
            }
            // Stock SMIN for both AI houses and the local player. No rule edits.
            std::array<int, 3> mobiles{};
            for (int slot = 0; slot < 3; ++slot) {
                mobiles[slot] = add(slot, RuntimeBuildKind::Unit, "SMIN", {20, slot == 0 ? 18 : slot == 1 ? 8 : 57});
                if (!need(mobiles[slot] >= 0, "three stock mobile miners")) return false;
                if (scenario == "mixed-cargo") {
                    const int worker = add(slot, RuntimeBuildKind::Infantry, "SLAV", {20, slot == 0 ? 18 : slot == 1 ? 8 : 57});
                    if (!need(worker >= 0, "stored cargo worker")) return false;
                    auto& task = s.slaveMinerTasks[worker]; task.refineryId = mobiles[slot];
                    task.phase = RuntimeSlaveMinerPhase::Returning; task.cargoUnits = 4; task.cargoGemUnits = 2; task.cargoValue = 175;
                    findMutableObjectById(s, worker)->mission = "Return";
                }
            }
            initializeRuntimeNavigation(s); initializeRuntimeOfflineGameplayRouting(s);
            if (scenario == "mixed-cargo") for (int i = 0; i < 2; ++i) {
                auto* miner = findMutableObjectById(s, miners[i]);
                const auto* refinery = findObjectById(s, refineries[i]);
                if (!need(miner && refinery, "mixed cargo owners")) return false;
                const auto dock = runtimeRefineryDefaultDockCell(s, *refinery);
                miner->x = dock.x; miner->y = dock.y; miner->cell = runtimeCellKey(dock.x, dock.y);
                auto& task = s.harvesterTasks[miners[i]]; task.phase = RuntimeHarvesterPhase::Unloading;
                task.targetRefineryId = refineries[i]; task.dockCell = dock;
                task.cargoUnits = 5; task.cargoGemUnits = 2; task.cargoValue = 175; task.cargoHasGems = true;
            }
            std::array<int, 3> previousLoads{}, previousTrips{};
            const auto observeWorkers = [&](const RuntimeWindowState& live) {
                for (int slot = 0; slot < 3; ++slot) {
                    const auto owner = runtimeSkirmishOwnerForSlot(slot);
                    const auto key = runtimeRepairDepotCanonicalOwnerId(live, owner);
                    int loadsNow = slot == 0 ? live.economy.slaveMinerLoads : 0;
                    int tripsNow = slot == 0 ? live.economy.resourceTrips : 0;
                    if (slot != 0) if (const auto ledger = live.computerMiningByOwner.find(key); ledger != live.computerMiningByOwner.end()) {
                        loadsNow = ledger->second.slaveMinerLoads; tripsNow = ledger->second.resourceTrips;
                    }
                    observedLoads[slot] += std::max(0, loadsNow - previousLoads[slot]);
                    observedTrips[slot] += std::max(0, tripsNow - previousTrips[slot]);
                    previousLoads[slot] = loadsNow; previousTrips[slot] = tripsNow;
                    for (const auto& [id, task] : live.slaveMinerTasks) {
                        const auto* worker = findRuntimeObjectByIdIncludingInactive(live, id);
                        if (!worker || runtimeRepairDepotCanonicalOwnerId(live, worker->owner) != key) continue;
                        workerOwnerMask |= 1 << slot;
                        cargoObservations[slot] += task.cargoUnits > 0;
                        gemCargoObservations[slot] += task.cargoGemUnits > 0;
                        storedObservations[slot] += task.phase == RuntimeSlaveMinerPhase::Stored;
                        returningObservations[slot] += task.phase == RuntimeSlaveMinerPhase::Returning;
                        harvestingObservations[slot] += task.phase == RuntimeSlaveMinerPhase::Harvesting;
                    }
                    for (const auto& [id, regen] : live.slaveMinerRegenTicks) {
                        const auto* parent = findRuntimeObjectByIdIncludingInactive(live, id);
                        if (regen > 0 && parent && runtimeRepairDepotCanonicalOwnerId(live, parent->owner) == key) ++regenObservations[slot];
                    }
                }
            };
            std::ostringstream ticks, authorityTicks;
            const auto label = scenario + '-' + std::to_string(route);
            std::string ordinary;
            for (int t = 0; t <= 2400; ++t) {
                if (t == 120 && scenario == "loss") {
                    if (auto* object = findMutableObjectById(s, miners[0])) { object->alive = false; object->health = 0; }
                    if (auto* object = findMutableObjectById(s, refineries[1])) { object->alive = false; object->health = 0; }
                }
                if (t == 900 && scenario != "no-ore") {
                    for (int slot = 0; slot < 3; ++slot) {
                        int home = -1;
                        for (const auto& o : s.world.getObjects()) if (o.alive && o.owner == runtimeSkirmishOwnerForSlot(slot) && o.type == "YAREFN" && o.buildAnimTicksRemaining == 0) { home = o.instanceId; break; }
                        if (home >= 0) {
                            int repacked = -1;
                            if (slot == 0) { if (!need(undeployRuntimeStructure(s, home, &repacked, false), "local repack")) return false; }
                            else { s1702OldRuntimeComputerMiningScope scope(s, runtimeSkirmishOwnerForSlot(slot)); if (!need(undeployRuntimeStructure(s, home, &repacked, false), "AI repack")) return false; }
                            ++repacks; repackOwnerMask |= (1 << slot);
                        }
                    }
                }
                if (t == 180 && scenario == "loss") {
                    for (int slot = 0; slot < 3; ++slot) {
                        int doomed = -1;
                        for (const auto& [id, task] : s.slaveMinerTasks) {
                            (void)task; const auto* worker = findRuntimeObjectByIdIncludingInactive(s, id);
                            if (worker && worker->health > 0 && worker->owner == runtimeSkirmishOwnerForSlot(slot) && (doomed < 0 || id < doomed)) doomed = id;
                        }
                        if (doomed >= 0) {
                            RuntimeObjectExitRequest request; request.objectId = doomed; request.kind = RuntimeObjectExitKind::Destroyed;
                            request.tick = s.tick; request.reason = "S17-02 owned worker death"; request.wasAliveBeforeDamage = true;
                            const bool queued = queueRuntimeObjectExit(s, std::move(request)) == RuntimeObjectExitQueueResult::Queued;
                            const bool drained = route == 0 ? s1702OldDrainRuntimeObjectExitQueue(s) : drainRuntimeObjectExitQueue(s);
                            if (!need(queued && drained, "owned worker death drain")) return false;
                            ++workerDeaths;
                        }
                    }
                }
                if (t == 240 && scenario == "loss") {
                    int captured = -1;
                    for (const auto& [id, task] : s.slaveMinerTasks) {
                        (void)task; const auto* worker = findRuntimeObjectByIdIncludingInactive(s, id);
                        if (worker && worker->health > 0 && worker->owner == runtimeSkirmishOwnerForSlot(1) && (captured < 0 || id < captured)) captured = id;
                    }
                    if (captured >= 0) {
                        auto* worker = findMutableRuntimeObjectByIdIncludingInactive(s, captured);
                        worker->owner = runtimeSkirmishOwnerForSlot(2); worker->hasBeenCaptured = true; ++workerCaptures;
                    }
                }
                if (t == 360 && scenario == "loss") {
                    for (auto& o : s.world.accessObjects()) if (o.alive && o.owner == runtimeSkirmishOwnerForSlot(1) && (o.type == "SMIN" || o.type == "YAREFN")) { o.owner = runtimeSkirmishOwnerForSlot(2); o.hasBeenCaptured = true; ++captures; break; }
                }
                if (t == 480 && scenario == "loss") {
                    int doomed = -1;
                    for (const auto& o : s.world.getObjects()) if (o.alive && o.owner == runtimeSkirmishOwnerForSlot(2) && (o.type == "SMIN" || o.type == "YAREFN")) { doomed = o.instanceId; break; }
                    if (doomed >= 0) { RuntimeObjectExitRequest request; request.objectId = doomed; request.kind = RuntimeObjectExitKind::Destroyed; request.tick = s.tick; request.reason = "S17 owned loss fixture"; if (!need(queueRuntimeObjectExit(s, std::move(request)) == RuntimeObjectExitQueueResult::Queued && drainRuntimeObjectExitQueue(s), "owned host death")) return false; ++deaths; }
                }
                if (t > 0) { ++s.tick; advanceRuntimeSimulation(s); ++pairedTicks; }
                observeWorkers(s); ticks << trace(s); authorityTicks << authorityTrace(s);
                if (t == 0 || t == 120 || t == 720 || t == 2400) {
                    const auto bytes = save(s, label + '-' + std::to_string(t), ordinary);
                    if (!need(!bytes.empty(), label + " complete save/extension: " + error)) return false;
                    records[route] += bytes;
                }
            }
            std::ofstream tf(output / (label + ".ticks.txt"), std::ios::binary); tf << ticks.str();
            records[route] += ticks.str();
            if (!need(s.economy.credits >= 12345 && s.economy.resourceTrips >= 0
                && !s.humanHouseScopeActive && !s.remoteHumanHouseScopeActive && !s.computerMiningScopeActive,
                label + " local account/scopes preserved")) return false;
            IniFile ini;
            if (!need(ini.loadFromString(ordinary), label + " save parse")) return false;
            auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
            { RuntimeSnapshotActiveStateScope active(*restored);
                const bool materialized = materializeRuntimeLoadStateFromIni(*restored, ini, error);
                if (!need(materialized, label + " ordinary restore: " + error)) return false; }
            ++loads; restored->runtimeSoundsEnabled = false; restored->runtimePreferencePersistenceEnabled = false;
            initializeRuntimeOfflineGameplayRouting(*restored);
            for (int t = 0; t < 30; ++t) { ++restored->tick; advanceRuntimeSimulation(*restored); ++pairedTicks; observeWorkers(*restored); authorityTicks << authorityTrace(*restored); }
            const auto continued = save(*restored, label + "-continued", ordinary);
            if (!need(!continued.empty(), label + " resumed save/extension")) return false;
            records[route] += continued;
            authorityRecords[route] = authorityTicks.str();
            std::ofstream af(output / (label + ".authority.txt"), std::ios::binary); af << authorityRecords[route];
        }
        if (!need(records[0] == records[1], scenario + " complete oracle/production/ticks/UI/audio/save/restore bytes")) return false;
        if (authorityRecords[0] != authorityRecords[1]) { error = scenario + " full per-tick behavior/command/player feedback observer mismatch"; return false; }
    }
    if (!need(repackOwnerMask == 7 && repacks > 0 && captures == 2 && deaths == 2, "all three houses repacked and both routes captured/destroyed: repacks=" + std::to_string(repacks) + " owners=" + std::to_string(repackOwnerMask) + " captures=" + std::to_string(captures) + " deaths=" + std::to_string(deaths))) return false;
    if (!need(workerOwnerMask == 7 && workerDeaths == 6 && workerCaptures == 2,
        "three worker owners plus both-route worker death/capture: owners=" + std::to_string(workerOwnerMask)
        + " deaths=" + std::to_string(workerDeaths) + " captures=" + std::to_string(workerCaptures))) return false;
    for (int slot = 0; slot < 3; ++slot) {
        if (!need(cargoObservations[slot] > 0 && gemCargoObservations[slot] > 0 && storedObservations[slot] > 0
            && regenObservations[slot] > 0 && returningObservations[slot] > 0,
            "actual worker cargo/gem/store/regen/return coverage owner=" + std::to_string(slot))) return false;
        std::cout << "Slave worker owner coverage: owner=" << slot << " loads=" << observedLoads[slot]
            << " trips=" << observedTrips[slot] << " cargo=" << cargoObservations[slot] << " gemCargo=" << gemCargoObservations[slot]
            << " stored=" << storedObservations[slot] << " regen=" << regenObservations[slot]
            << " returning=" << returningObservations[slot] << " harvesting=" << harvestingObservations[slot] << '\n';
    }
    std::cout << "Slave worker economy host contract passed: cases=" << cases << " checks=" << checks
        << " ordinaryLoads=" << loads << " routeTicks=" << pairedTicks
        << " repacks=" << repacks << " repackOwners=" << repackOwnerMask << " captures=" << captures << " deaths=" << deaths << " workerDeaths=" << workerDeaths << " workerCaptures=" << workerCaptures << " workerOwners=" << workerOwnerMask << " oldLiteralOracle=1 fullBytes=1 twoAiOwners=1 localPlayer=1 physicalInput=not-run performance=not-run\n";
    return true;
} catch (const std::exception& exception) { error = std::string("house economy fixture: ") + exception.what(); return false; }
