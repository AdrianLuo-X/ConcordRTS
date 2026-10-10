// Include after the native economy/navigation functions and before
// RuntimeLiveMovementBenchmark.inl. This is an opt-in native mining fixture.
// Opt in separately from PlayerScene; prepare only after the ordinary player
// fixture and before appendHistory. Keep the army IDs unchanged. sample must
// run after EVERY completed tick, including the first 60 warmup ticks.
// This fixture seeds ore once and never commands, refills or repairs the miner.
class RuntimePlayerMiningBenchmark {
public:
    bool prepare(RuntimeWindowState& state, const std::vector<int>& armyIds,
                 std::string& error) {
        *this = RuntimePlayerMiningBenchmark{};
        phaseFirstTicks_.fill(-1);
        const auto fail = [&](const char* reason) {
            failed_ = true;
            failure_ = reason;
            error = std::string("player mining fixture: ") + reason;
            return false;
        };
        if (state.tick != 0 || state.campaignMode || state.config.multiplayerActive
            || state.config.playerCountry != "Americans" || !state.config.playerSlots.empty()
            || state.economy.owner != "Americans" || !state.visionEnabled
            || runtimeLocalObserver(state.config) || !runtimeRadarAvailable(state)
            || state.report.map.path != "MassMovement-v1.map"
            || state.report.map.structures != 9 || state.report.map.width < 96
            || state.report.map.width > 256
            || state.report.map.width != state.report.map.height
            || state.world.getTerrainCells().size() != static_cast<std::size_t>(
                2 * state.report.map.width * state.report.map.height - state.report.map.width)
            || state.world.getObjects().size() != armyIds.size() + 9
            || state.economy.simplifiedHarvesterIncomeEnabled
            || state.economy.harvesters != 0 || !state.harvesterTasks.empty())
            return fail("requires_fresh_native_player_scene_before_history");
        if (armyIds.size() != 48 && armyIds.size() != 128 && armyIds.size() != 256)
            return fail("unexpected_army_population");
        std::unordered_set<int> armySet(armyIds.begin(), armyIds.end());
        if (armySet.size() != armyIds.size()) return fail("duplicate_army_id");
        constexpr std::array<const char*, 8> buildingTypes{
            "GACNST", "GAPOWR", "GAREFN", "GAPILE", "GAWEAP", "AMRADR", "GATECH", "GADEPT"};
        constexpr std::array<int, 8> expectedBuildings{1, 2, 1, 1, 1, 1, 1, 1};
        std::array<int, 8> buildings{};
        RuntimeCellPoint refineryCell;
        for (const auto& object : state.world.getObjects()) {
            if (!object.alive || object.health <= 0 || object.owner != "Americans"
                || object.health != runtimeMaxHealthForObject(state, object))
                return fail("player_scene_population_is_not_full_health_native");
            if (object.kind == WorldObjectKind::Structure) {
                const auto type = std::find(buildingTypes.begin(), buildingTypes.end(), object.type);
                if (type == buildingTypes.end() || object.buildAnimTicksRemaining != 0)
                    return fail("unexpected_or_unfinished_player_building");
                ++buildings[static_cast<std::size_t>(type - buildingTypes.begin())];
                if (object.type == "GAREFN") {
                    refineryId_ = object.instanceId;
                    refineryCell = runtimeCellPointForObject(object);
                }
            } else if (!armySet.contains(object.instanceId)
                || (object.kind != WorldObjectKind::Unit && object.kind != WorldObjectKind::Infantry)
                || findRuntimeUnitType(state.rules, object) == nullptr) {
                return fail("unexpected_player_scene_unit");
            }
        }
        if (buildings != expectedBuildings || refineryId_ < 0)
            return fail("native_player_building_census_mismatch");
        const auto* type = state.rules.findUnitType("UnitType", "CMIN");
        if (!state.rulesLoaded || type == nullptr || !type->harvester
            || type->storage != 20 || type->speed != 4 || type->strength <= 0)
            return fail("stock_cmin_profile_missing");
        capacity_ = type->storage;
        sourceRulesHash_ = state.rules.getDetailRulesHash();
        for (const auto& terrain : state.world.getTerrainCells())
            if (terrain.overlayIsOre || terrain.overlayIsGem)
                return fail("requires_empty_resource_scene_before_native_seed");

        // Copy coordinates/IDs, never keep a refinery pointer across push_back.
        // North/west of the authored refinery avoids the starting army and the
        // 3x3 building cluster. Every exact cell is checked by native passability.
        minerStart_ = {refineryCell.x - 8, refineryCell.y - 12};
        oreCells_ = {{{refineryCell.x - 8, refineryCell.y - 10},
                      {refineryCell.x - 7, refineryCell.y - 10},
                      {refineryCell.x - 8, refineryCell.y - 9},
                      {refineryCell.x - 7, refineryCell.y - 9}}};
        WorldObjectInstance probe;
        probe.instanceId = -1;
        probe.kind = WorldObjectKind::Unit;
        probe.type = "CMIN";
        probe.owner = "Americans";
        probe.health = type->strength;
        probe.alive = true;
        const auto legalEmptyCell = [&](const RuntimeCellPoint& cell) {
            const auto* terrain = findRuntimeTerrainCellAt(state, cell);
            const auto passable = findRuntimePassableCellNear(state, probe, cell, 0);
            return terrain != nullptr && !terrain->hasOverlay
                && passable.has_value() && *passable == cell;
        };
        if (!legalEmptyCell(minerStart_)) return fail("native_miner_spawn_is_not_legal");
        for (const auto& cell : oreCells_)
            if (!legalEmptyCell(cell)) return fail("native_ore_seed_cell_is_not_legal");
        for (const auto& cell : oreCells_)
            if (!growRuntimeOreAtCell(state, cell, runtimeOreIdentityForCell(cell, "TIB", 12), 12))
                return fail("native_ore_seed_failed");
        auto* miner = addRuntimeObjectInstanceForOwner(state, "Americans",
            RuntimeBuildKind::Unit, "CMIN", minerStart_);
        if (miner == nullptr) return fail("native_cmin_spawn_failed");
        minerId_ = miner->instanceId;
        miner->harvestAutoSearchSuspended = false;
        // No rules, mission, cargo, task phase, tick rate or speed assignment.
        if (armySet.contains(minerId_)) return fail("miner_was_included_in_army_ids");
        rebuildRuntimeEconomyPower(state);
        (void)completeRuntimeWorldRefresh(state);
        (void)updateRuntimeVisionState(state);
        // Both pointers are reacquired after the vector append and refresh.
        const auto* currentMiner = findObjectById(state, minerId_);
        const auto* currentRefinery = currentMiner == nullptr ? nullptr
            : findRuntimeHarvesterRefineryById(state, *currentMiner, refineryId_);
        if (currentMiner == nullptr || currentRefinery == nullptr
            || state.economy.harvesters != 1 || state.economy.refineries != 1
            || state.economy.simplifiedHarvesterIncomeEnabled)
            return fail("native_miner_refinery_economy_not_ready");
        ++state.report.map.units;
        state.report.map.overlayCells += static_cast<int>(oreCells_.size());
        state.report.world = state.world.getSummary();
        initialOreBails_ = oreBails(state);
        if (initialOreBails_ != 48) return fail("native_seed_did_not_contain_48_bails");
        finalOreBails_ = minimumOreBails_ = initialOreBails_;
        initialCredits_ = finalCredits_ = previousCredits_ = state.economy.credits;
        initialTrips_ = finalTrips_ = previousTrips_ = state.economy.resourceTrips;
        initialTeleports_ = finalTeleports_ = state.economy.harvesterTeleports;
        minerFinal_ = runtimeCellPointForObject(*currentMiner);
        minerAlive_ = true;
        startTick_ = lastTick_ = state.tick;
        prepared_ = true;
        error.clear();
        return true;
    }

    // Constant bounded work: one object lookup, one task lookup and four terrain
    // lookups. This never writes game state or scans the entire map per tick.
    void sample(const RuntimeWindowState& state) {
        if (!prepared_ || state.tick == lastTick_) return;
        if (state.tick != lastTick_ + 1) failSample("missing_or_reordered_completed_tick");
        lastTick_ = state.tick;
        ++samples_;
        finalCredits_ = state.economy.credits;
        finalTrips_ = state.economy.resourceTrips;
        finalTeleports_ = state.economy.harvesterTeleports;
        finalOreBails_ = oreBails(state);
        if (finalOreBails_ < 0) failSample("seeded_terrain_cell_disappeared");
        else minimumOreBails_ = std::min(minimumOreBails_, finalOreBails_);
        simplifiedIncomeEverEnabled_ = simplifiedIncomeEverEnabled_
            || state.economy.simplifiedHarvesterIncomeEnabled;
        if (state.economy.simplifiedHarvesterIncomeEnabled || state.economy.harvesters != 1)
            failSample("native_economy_invariant_changed");
        const auto* miner = findObjectById(state, minerId_);
        minerAlive_ = miner != nullptr && miner->alive && miner->health > 0;
        if (!minerAlive_) { failSample("miner_died_or_disappeared"); return; }
        if (miner->type != "CMIN" || miner->owner != "Americans"
            || miner->harvestAutoSearchSuspended)
            failSample("miner_identity_or_auto_harvest_changed");
        minerFinal_ = runtimeCellPointForObject(*miner);
        const auto taskIt = state.harvesterTasks.find(minerId_);
        const auto creditDelta = static_cast<long long>(finalCredits_) - previousCredits_;
        const auto tripDelta = static_cast<long long>(finalTrips_) - previousTrips_;
        if (taskIt != state.harvesterTasks.end()) {
            const auto& task = taskIt->second;
            const int phase = phaseIndex(task.phase);
            if (phase < 0 || task.cargoCapacity != capacity_ || task.cargoUnits < 0
                || task.cargoUnits > capacity_ || task.cargoValue < 0 || task.cargoGemUnits != 0)
                failSample("invalid_native_task_or_cargo");
            if (phase >= 0) {
                ++phaseTicks_[static_cast<std::size_t>(phase)];
                if (phaseFirstTicks_[static_cast<std::size_t>(phase)] < 0)
                    phaseFirstTicks_[static_cast<std::size_t>(phase)] = state.tick;
            }
            peakCargoUnits_ = std::max(peakCargoUnits_, task.cargoUnits);
            peakCargoValue_ = std::max(peakCargoValue_, task.cargoValue);
            if (task.cargoUnits > previousCargoUnits_) {
                const int nativeBails = state.economy.lastHarvesterGatherBeforeBails
                    - state.economy.lastHarvesterGatherAfterBails;
                if (nativeBails != task.cargoUnits - previousCargoUnits_ || nativeBails <= 0)
                    failSample("cargo_increase_without_native_ore_pickup");
                else {
                    nativePickups_ += static_cast<std::uint64_t>(nativeBails);
                    // Native growth/spread may later create ore outside the four
                    // seeded cells. Observe it; never disable normal world rules.
                    if (std::find(oreCells_.begin(), oreCells_.end(),
                            state.economy.lastHarvesterGatherCell) != oreCells_.end())
                        seededNativePickups_ += static_cast<std::uint64_t>(nativeBails);
                    sawCargo_ = true;
                }
            }
            // Pure ore unloads atomically: PreparingToUnload -> SeekingOre in
            // one advance. An after-tick observer need not see Unloading itself.
            const bool nativeUnload = tripDelta == 1 && previousTaskSeen_
                && (previousPhase_ == RuntimeHarvesterPhase::PreparingToUnload
                    || previousPhase_ == RuntimeHarvesterPhase::Unloading)
                && previousAtDock_ && previousRefineryId_ == refineryId_
                && previousCargoUnits_ > 0 && previousCargoValue_ > 0
                && task.cargoUnits == 0 && task.cargoValue == 0
                && task.phase == RuntimeHarvesterPhase::SeekingOre
                && state.economy.lastResourceBaseValue == previousCargoValue_
                && creditDelta == static_cast<long long>(previousCargoValue_)
                    + state.economy.lastResourcePurifierBonus
                && creditDelta > 0
                && findRuntimeHarvesterRefineryById(state, *miner, refineryId_) != nullptr;
            if (nativeUnload) { ++nativeUnloads_; observedUnloadCredits_ += creditDelta; }
            else if (tripDelta != 0 || creditDelta != 0)
                failSample("trip_or_credit_change_without_native_unload");
            previousTaskSeen_ = true;
            previousPhase_ = task.phase;
            previousCargoUnits_ = task.cargoUnits;
            previousCargoValue_ = task.cargoValue;
            previousRefineryId_ = task.targetRefineryId;
            previousAtDock_ = runtimeHarvesterIsAtDock(*miner, task);
            mix(static_cast<std::uint64_t>(phase));
            mix(static_cast<std::uint64_t>(task.cargoUnits));
            mix(static_cast<std::uint64_t>(task.cargoValue));
        } else if (previousTaskSeen_ || tripDelta != 0 || creditDelta != 0) {
            failSample("native_harvester_task_disappeared");
        }
        previousCredits_ = finalCredits_;
        previousTrips_ = finalTrips_;
        const double economyMs = state.perf.lastSimEconomyMs;
        if (std::isfinite(economyMs) && economyMs >= 0) {
            economyTotalMs_ += economyMs;
            economyMaxMs_ = std::max(economyMaxMs_, economyMs);
        }
        mix(static_cast<std::uint64_t>(state.tick));
        mix(static_cast<std::uint64_t>(minerFinal_.x));
        mix(static_cast<std::uint64_t>(minerFinal_.y));
        mix(static_cast<std::uint64_t>(miner->health));
        mix(static_cast<std::uint64_t>(finalOreBails_));
        mix(static_cast<std::uint64_t>(finalCredits_));
        mix(static_cast<std::uint64_t>(finalTrips_));
    }

    bool succeeded() const {
        return prepared_ && !failed_ && minerAlive_ && samples_ > 0 && sawCargo_
            && minimumOreBails_ < initialOreBails_ && finalOreBails_ < initialOreBails_
            && peakCargoUnits_ == capacity_ && seededNativePickups_ >= static_cast<std::uint64_t>(capacity_)
            && phaseTicks_[3] > 0 && (phaseTicks_[4] > 0 || phaseTicks_[6] > 0)
            && phaseTicks_[7] > 0 && nativeUnloads_ > 0 && observedUnloadCredits_ > 0
            && static_cast<long long>(finalTrips_) - initialTrips_ == static_cast<long long>(nativeUnloads_)
            && static_cast<long long>(finalCredits_) - initialCredits_ == observedUnloadCredits_;
    }

    int harvesterId() const { return minerId_; }
    void print() const {
        std::cout << "Live player mining benchmark: passed=" << (succeeded() ? "yes" : "no")
            << " nativeApiFixture=yes seededOnce=" << (prepared_ ? "yes" : "no")
            << " simplifiedIncomeEverEnabled=" << (simplifiedIncomeEverEnabled_ ? "yes" : "no")
            << " oreScope=seeded4Cells"
            << " harvesterId=" << minerId_ << " refineryId=" << refineryId_
            << " startTick=" << startTick_ << " endTick=" << lastTick_ << " samples=" << samples_
            << " minerAlive=" << (minerAlive_ ? "yes" : "no")
            << " minerStart=" << minerStart_.x << ',' << minerStart_.y
            << " minerFinal=" << minerFinal_.x << ',' << minerFinal_.y
            << " oreInitialBails=" << initialOreBails_ << " oreFinalBails=" << finalOreBails_
            << " oreMinimumBails=" << minimumOreBails_ << " nativePickups=" << nativePickups_
            << " seededNativePickups=" << seededNativePickups_
            << " capacity=" << capacity_ << " peakCargoUnits=" << peakCargoUnits_
            << " peakCargoValue=" << peakCargoValue_ << " creditsInitial=" << initialCredits_
            << " creditsFinal=" << finalCredits_ << " nativeUnloadCredits=" << observedUnloadCredits_
            << " tripsInitial=" << initialTrips_ << " tripsFinal=" << finalTrips_
            << " nativeUnloads=" << nativeUnloads_ << " teleportsInitial=" << initialTeleports_
            << " teleportsFinal=" << finalTeleports_ << " economyTimingScope=wholeEconomy"
            << " economyWholeMeanMs=" << (samples_ ? economyTotalMs_ / samples_ : 0.0)
            << " economyWholeMaxMs=" << economyMaxMs_ << " sourceRulesHash=" << sourceRulesHash_
            << " trajectoryHash=" << trajectory_ << " failure=" << failure_ << '\n';
        constexpr std::array<const char*, 9> phases{"Idle", "SeekingOre", "MovingToOre", "Loading",
            "ReturningToRefinery", "WaitingForRefineryDock", "Docking", "PreparingToUnload", "Unloading"};
        std::cout << "Live player mining phases: atomicOreUnload=yes";
        for (std::size_t i = 0; i < phases.size(); ++i)
            std::cout << ' ' << phases[i] << "Ticks=" << phaseTicks_[i]
                << ' ' << phases[i] << "FirstTick=" << phaseFirstTicks_[i];
        std::cout << " oreCells=";
        for (std::size_t i = 0; i < oreCells_.size(); ++i)
            std::cout << (i ? ";" : "") << oreCells_[i].x << ',' << oreCells_[i].y;
        std::cout << '\n';
    }
private:
    static int phaseIndex(RuntimeHarvesterPhase phase) {
        switch (phase) {
        case RuntimeHarvesterPhase::Idle: return 0;
        case RuntimeHarvesterPhase::SeekingOre: return 1;
        case RuntimeHarvesterPhase::MovingToOre: return 2;
        case RuntimeHarvesterPhase::Loading: return 3;
        case RuntimeHarvesterPhase::ReturningToRefinery: return 4;
        case RuntimeHarvesterPhase::WaitingForRefineryDock: return 5;
        case RuntimeHarvesterPhase::Docking: return 6;
        case RuntimeHarvesterPhase::PreparingToUnload: return 7;
        case RuntimeHarvesterPhase::Unloading: return 8;
        }
        return -1;
    }
    int oreBails(const RuntimeWindowState& state) const {
        int result = 0;
        for (const auto& cell : oreCells_) {
            const auto* terrain = findRuntimeTerrainCellAt(state, cell);
            if (terrain == nullptr) return -1;
            if (terrain->overlayIsOre || terrain->overlayIsGem)
                result += runtimeResourceBailCountForTerrain(*terrain);
        }
        return result;
    }
    void failSample(const char* reason) {
        failed_ = true;
        if (failure_ == "none") failure_ = reason;
    }
    void mix(std::uint64_t value) { trajectory_ = (trajectory_ ^ value) * 1099511628211ULL; }
    bool prepared_ = false, failed_ = false, minerAlive_ = false, sawCargo_ = false;
    bool previousTaskSeen_ = false, previousAtDock_ = false, simplifiedIncomeEverEnabled_ = false;
    std::string failure_ = "none";
    int minerId_ = -1, refineryId_ = -1, capacity_ = 0, startTick_ = 0, lastTick_ = 0;
    RuntimeCellPoint minerStart_, minerFinal_;
    std::array<RuntimeCellPoint, 4> oreCells_{};
    std::array<std::uint64_t, 9> phaseTicks_{};
    std::array<int, 9> phaseFirstTicks_{};
    RuntimeHarvesterPhase previousPhase_ = RuntimeHarvesterPhase::Idle;
    int previousCargoUnits_ = 0, previousCargoValue_ = 0, previousRefineryId_ = -1;
    int initialOreBails_ = 0, finalOreBails_ = 0, minimumOreBails_ = 0;
    int initialCredits_ = 0, finalCredits_ = 0, previousCredits_ = 0;
    int initialTrips_ = 0, finalTrips_ = 0, previousTrips_ = 0;
    int initialTeleports_ = 0, finalTeleports_ = 0, peakCargoUnits_ = 0, peakCargoValue_ = 0;
    std::uint64_t samples_ = 0, nativePickups_ = 0, seededNativePickups_ = 0;
    std::uint64_t nativeUnloads_ = 0, sourceRulesHash_ = 0;
    std::uint64_t trajectory_ = 1469598103934665603ULL;
    long long observedUnloadCredits_ = 0;
    double economyTotalMs_ = 0, economyMaxMs_ = 0;
};
