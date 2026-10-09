// Opt-in input driver for the existing production-loop benchmark. Commands
// depend only on the completed simulation tick, never on measured wall time.
class RuntimeLiveMovementBenchmark {
public:
    explicit RuntimeLiveMovementBenchmark(bool liveEnabled) {
        if (!liveEnabled) return;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_MOVEMENT")) {
            requested_ = true;
            count_ = std::strcmp(value, "48") == 0 ? 48
                : std::strcmp(value, "128") == 0 ? 128
                : std::strcmp(value, "256") == 0 ? 256 : 0;
        }
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_MIXED"))
            mixed_ = std::strcmp(value, "1") == 0;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_CHOKE"))
            choke_ = std::strcmp(value, "1") == 0;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_COMBAT"))
            combat_ = std::strcmp(value, "1") == 0;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_BATTLE"))
            battle_ = std::strcmp(value, "1") == 0;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_SUSTAIN_MOVEMENT"))
            sustainMovement_ = std::strcmp(value, "1") == 0;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_PLAYER_SCENE"))
            playerScene_ = std::strcmp(value, "1") == 0;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_MIXED_ROSTER"))
            mixedRoster_ = std::strcmp(value, "1") == 0;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_KEEP_AUTHORED_WORLD"))
            keepAuthoredWorld_ = std::strcmp(value, "1") == 0;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_PLAYER_MINING"))
            playerMining_ = std::strcmp(value, "1") == 0;
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_MAP_SIDE")) {
            char* end = nullptr;
            const long side = std::strtol(value, &end, 10);
            mapSideValid_ = end != value && *end == '\0' && side >= 96 && side <= 480;
            if (mapSideValid_) mapSide_ = static_cast<int>(side);
        }
        if (const char* value = std::getenv("CONCORD_LIVE_BENCHMARK_HISTORY")) {
            historyValid_ = value[0] != '\0';
            for (const char* digit = value; *digit != '\0' && historyValid_; ++digit) {
                if (*digit < '0' || *digit > '9') historyValid_ = false;
                else {
                    historyCount_ = historyCount_ * 10 + (*digit - '0');
                    if (historyCount_ > 10000) historyValid_ = false;
                }
            }
        }
        if (battle_) { mixed_ = true; combat_ = false; choke_ = false; }
    }
    bool enabled() const { return requested_; }
    bool preservesAuthoredWorld() const { return requested_ && keepAuthoredWorld_; }
    RuntimeCellPoint benchmarkCameraCell() const {
        return keepAuthoredWorld_ ? authoredCamera_ : RuntimeCellPoint{22,32};
    }
    bool prepare(RuntimeWindowState& state, std::string& error) {
        if (!requested_) return true;
        if (!historyValid_) {
            error = "live movement history must be an integer from 0 through 10000";
            return false;
        }
        if (keepAuthoredWorld_) {
            if (count_ != 48 || !mixed_ || !mixedRoster_ || historyCount_ != 0
                || !mapSideValid_ || mapSide_ != 0 || playerScene_ || playerMining_
                || battle_ || combat_ || choke_) {
                error = "keep authored world requires 48 mixed stock units, H0 and no synthetic flags";
                return false;
            }
            projectileTracker_.reserve(4096);
            if (!prepareRuntimeKeepAuthoredMovementBenchmark(state,ids_,authoredGoals_,authoredCamera_,error))
                return false;
        } else {
        if (!mapSideValid_ || (battle_ && (mapSide_ != 0 || playerScene_)) || (playerScene_ && mapSide_ == 0)
            || ((mixedRoster_ || playerMining_) && (!playerScene_ || battle_ || combat_ || choke_))) {
            error = "fixture map side must be 96 through 480 and is supported by movement only";
            return false;
        }
        if (battle_) {
            if (!battleSample_.prepare(state, count_, error)) return false;
            ids_ = battleSample_.unitIds;
            return appendHistory(state, error);
        }
        projectileTracker_.reserve(4096);
        if (!prepareRuntimeMassMovementWorld(state, count_, ids_, error, choke_, mixed_, false,
                mapSide_ == 0 ? 96 : mapSide_, mapSide_ == 0 ? 64 : mapSide_, playerScene_, mixedRoster_))
            return false;
        }
        if (playerMining_ && !miningSample_.prepare(state, ids_, error)) return false;
        if (mixedRoster_) {
            std::map<std::string, int> census;
            for (int id : ids_) {
                const auto* object = findObjectById(state, id);
                if (!object) { error = "native roster lost an army ID"; return false; }
                ++census[object->type];
            }
            if (census.size() != 12) { error = "native roster did not hydrate all 12 types"; return false; }
            std::cout << "Live movement roster fixture: units=" << ids_.size()
                << " distinctTypes=" << census.size();
            for (const auto& [type, count] : census) std::cout << ' ' << type << '=' << count;
            std::cout << '\n';
        }
        std::cout << "Live movement terrain fixture: width=" << state.report.map.width
            << " height=" << state.report.map.height
            << " terrainCells=" << state.world.getTerrainCells().size()
            << " playerScene=" << (playerScene_ ? "yes" : "no")
            << " mixedRoster=" << (mixedRoster_ ? "yes" : "no")
            << " playerMining=" << (playerMining_ ? "yes" : "no")
            << " vision=" << (state.visionEnabled ? "yes" : "no")
            << " radar=" << (runtimeRadarAvailable(state) ? "yes" : "no") << '\n';
        for (int id : ids_) {
            const auto* object = findObjectById(state, id);
            starts_.emplace(id, runtimeCellPointForObject(*object));
        }
        if (combat_) {
            if (!prepareRuntimeMassCombatTarget(state, targetId_, error,
                    &targetOriginalStrength_)) return false;
            const auto* target = findObjectById(state, targetId_);
            targetInitialHealth_ = targetFinalHealth_ = target->health;
            targetMaxHealth_ = runtimeMaxHealthForObject(state, *target);
        }
        return appendHistory(state, error);
    }
    void beforeSimulation(RuntimeWindowState& state) {
        if (!requested_ || failed_) return;
        if (battle_) {
            if (state.tick <= 60 || (state.tick - 61) % 30 != 0) return;
            const auto started = std::chrono::steady_clock::now();
            battleSample_.issueOrders(state, state.tick - 61);
            commandMaxMs_ = std::max(commandMaxMs_, std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count());
            ++commands_;
            return;
        }
        const bool sustained = sustainMovement_ && !combat_;
        if (sustained) {
            if (state.tick <= 60 || (state.tick - 61) % 240 != 0) return;
        } else if (state.tick != 61 && state.tick != 301 && state.tick != 541) return;
        const bool returning = sustained ? ((state.tick - 61) / 240) % 2 != 0 : state.tick == 301;
        const int playerOffset = playerScene_ ? state.report.map.width / 2 : 0;
        const RuntimeCellPoint goal = keepAuthoredWorld_ ? authoredGoals_[returning ? 1 : 0]
            : combat_ ? RuntimeCellPoint{28, 32}
            : returning ? RuntimeCellPoint{playerOffset + 22, playerOffset + 48}
                : RuntimeCellPoint{playerOffset + 76, playerOffset + 32};
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        const bool accepted = combat_
            ? issueRuntimeMassCombatOrder(state, ids_, targetId_, error)
            : issueRuntimeMassMovementOrder(state, ids_, goal, destinations_, error, mixedRoster_);
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        commandMaxMs_ = std::max(commandMaxMs_, elapsed);
        ++commands_;
        acceptedMovesTotal_ += state.lastCommand.acceptedCount;
        rejectedMovesTotal_ += state.lastCommand.rejectedCount;
        failed_ = failed_ || !accepted;
        std::cout << "Live movement order: tick=" << state.tick
            << " accepted=" << state.lastCommand.acceptedCount << '/' << count_
            << " rejected=" << state.lastCommand.rejectedCount
            << " goal=" << goal.x << ',' << goal.y << " commandMs=" << elapsed
            << " error=" << (error.empty() ? "none" : error) << '\n';
    }
    void sample(const RuntimeWindowState& state) {
        if (!requested_) return;
        if (playerMining_) miningSample_.sample(state);
        if (state.tick <= 60) return;
        int active = 0, alive = 0, arrived = 0;
        if (battle_) {
            battleSample_.sample(state, state.perf.lastSimulationMs);
            active = battleSample_.active;
            alive = battleSample_.alive[0] + battleSample_.alive[1];
            trajectory_ = battleSample_.trajectory;
        } else {
        mix(static_cast<std::uint64_t>(state.tick));
        for (int id : ids_) {
            const auto* object = findObjectById(state, id);
            if (!object || !object->alive) { failed_ = true; continue; }
            ++alive;
            const auto cell = runtimeCellPointForObject(*object);
            if (cell != starts_.at(id)) moved_.insert(id);
            active += object->renderMoveActive ? 1 : 0;
            const auto goal = destinations_.find(id);
            if (goal != destinations_.end() && cell == goal->second
                && !object->renderMoveActive) ++arrived;
            mix(static_cast<std::uint64_t>(id));
            mix(static_cast<std::uint64_t>(object->x));
            mix(static_cast<std::uint64_t>(object->y));
            mix(static_cast<std::uint64_t>(object->facing));
            mix(static_cast<std::uint64_t>(object->health));
        }
        }
        ++samples_;
        lastSampleTick_ = state.tick;
        peakVisible_ = std::max(peakVisible_, state.renderStats.visibleWorldObjects);
        activeTotal_ += active;
        peakActive_ = std::max(peakActive_, active);
        finalAlive_ = alive;
        finalArrived_ = arrived;
        attacks_ += state.actionSummary.attacksThisTick;
        if (state.actionSummary.attacksThisTick > 0) lastAttackTick_ = state.tick;
        if (combat_) {
            const auto* target = findObjectById(state, targetId_);
            targetAlive_ = target && target->alive && target->health > 0;
            targetFinalHealth_ = target ? target->health : 0;
            if (!targetAlive_) failed_ = true;
            mix(static_cast<std::uint64_t>(targetId_));
            mix(static_cast<std::uint64_t>(targetFinalHealth_));
            mix(targetAlive_ ? 1ULL : 0ULL);
        }
        peakProjectiles_ = std::max(peakProjectiles_, state.projectiles.size());
        peakImpacts_ = std::max(peakImpacts_, state.impactEffects.size());
        const std::array<double, 8> phases{state.perf.lastSimEffectsMs,
            state.perf.lastSimEconomyMs, state.perf.lastSimTriggersMs, state.perf.lastSimBuildMs,
            state.perf.lastSimGuardMs, state.perf.lastSimMoveMs, state.perf.lastSimAttackMs,
            state.perf.lastSimPathMs};
        for (std::size_t i = 0; i < phases.size(); ++i) {
            phaseTotals_[i] += phases[i];
            phasePeaks_[i] = std::max(phasePeaks_[i], phases[i]);
        }
        pathRequests_ += state.perf.lastSimPathRequests;
        pendingPathsPeak_ = std::max(pendingPathsPeak_, state.pendingPathObjectIds.size());
        std::uint64_t newShots = 0;
        if (battle_) {
            const auto totalShots = static_cast<std::uint64_t>(battleSample_.shots[0])
                + static_cast<std::uint64_t>(battleSample_.shots[1]);
            newShots = totalShots - previousShots_;
            previousShots_ = totalShots;
        } else {
            const auto shots = projectileTracker_.sample(state, 61,
                [&](const RuntimeProjectileEffect& projectile) {
                    return starts_.contains(projectile.attackerObjectId) ? 0 : -1;
                });
            newShots = shots[0];
        }
        if (state.liveBenchmark != nullptr)
            state.liveBenchmark->recordWorkload(state.tick, alive, active, newShots,
                static_cast<std::uint64_t>(std::max(0, state.actionSummary.attacksThisTick)),
                state.projectiles.size(), state.impactEffects.size(),
                state.particles.systems().size(), state.particles.particles().size());
    }
    bool succeeded() const {
        if (requested_ && battle_)
            return !failed_ && commands_ > 0 && battleSample_.succeeded() && peakVisible_ >= count_ / 2;
        return !requested_ || (!failed_ && commands_ > 0 && peakActive_ > 0
            && (!playerMining_ || miningSample_.succeeded())
            && peakVisible_ >= count_ / 2
            && finalAlive_ == count_ && moved_.size() >= ids_.size() * 3 / 4
            && (!combat_ || (attacks_ > 0 && peakProjectiles_ > 0 && peakImpacts_ > 0
                && targetAlive_ && targetFinalHealth_ > 0
                && targetFinalHealth_ < targetInitialHealth_
                && lastAttackTick_ >= lastSampleTick_ - 180)));
    }
    void print(const RuntimeWindowState& state) const {
        if (!requested_) return;
        std::cout << "Live terrain viewport fill: lifetimeFillCalls="
            << state.renderCache.terrainViewportFillCalls << " lifetimeElisions="
            << state.renderCache.terrainViewportFillElisions << '\n';
        std::cout << "Live path layout fixture: retainedCells="
            << state.pathTerrainCache.queueLayout.retainedCellCount()
            << " lastConstructionReused="
            << (state.pathTerrainCache.queueLayout.lastConstructionReused() ? "yes" : "no") << '\n';
        std::cout << "Live movement benchmark: passed=" << (succeeded() ? "yes" : "no")
            << " units=" << count_ << " mixed=" << (mixed_ ? "yes" : "no")
            << " acceptedMovesTotal=" << acceptedMovesTotal_ << " rejectedMovesTotal=" << rejectedMovesTotal_
            << " historyObjects=" << historyCount_ << " fixtureWorldObjects=" << fixtureWorldObjects_
            << " fixtureAliveObjects=" << fixtureAliveObjects_
            << " combat=" << (combat_ ? "yes" : "no")
            << " battle=" << (battle_ ? "yes" : "no")
            << " sustainedMovement=" << (sustainMovement_ && !combat_ && !battle_ ? "yes" : "no")
            << " choke=" << (choke_ ? "yes" : "no") << " commands=" << commands_
            << " movedUnique=" << (battle_ ? battleSample_.moved.size() : moved_.size()) << " peakMoving=" << peakActive_
            << " peakVisible=" << peakVisible_
            << " meanMoving=" << (samples_ ? static_cast<double>(activeTotal_) / samples_ : 0.0)
            << " finalAlive=" << finalAlive_ << " finalArrived=" << finalArrived_
            << " attacks=" << attacks_ << " peakProjectiles=" << peakProjectiles_
            << " peakImpacts=" << peakImpacts_
            << " shotTrackingPeakObservations=" << (battle_ ? battleSample_.projectileTracker.peakObservations : projectileTracker_.peakObservations)
            << " shotTrackingCapacity=" << (battle_ ? battleSample_.projectileTracker.capacity() : projectileTracker_.capacity())
            << " targetOriginalStrength=" << targetOriginalStrength_
            << " targetMaxHealth=" << targetMaxHealth_
            << " targetInitialHealth=" << targetInitialHealth_
            << " targetFinalHealth=" << targetFinalHealth_
            << " targetAlive=" << (combat_ ? (targetAlive_ ? "yes" : "no") : "n/a")
            << " targetDamage=" << targetInitialHealth_ - targetFinalHealth_
            << " lastAttackTick=" << lastAttackTick_
            << " commandMaxMs=" << commandMaxMs_ << " trajectoryHash=" << trajectory_ << '\n';
        constexpr std::array<const char*, 8> phaseNames{
            "effects", "economy", "triggers", "build", "guard", "move", "attack", "path"};
        std::cout << "Live movement phases: pathIncludedInMove=yes pathRequests=" << pathRequests_
            << " pendingPathsPeak=" << pendingPathsPeak_;
        for (std::size_t i = 0; i < phaseNames.size(); ++i)
            std::cout << ' ' << phaseNames[i] << "MeanMs=" << (samples_ ? phaseTotals_[i] / samples_ : 0.0)
                << ' ' << phaseNames[i] << "MaxMs=" << phasePeaks_[i];
        std::cout << '\n';
        if (battle_) battleSample_.print("Live");
        if (playerMining_) miningSample_.print();
    }
private:
    bool appendHistory(RuntimeWindowState& state, std::string& error) {
        // Add tombstones after every live actor/target has received its ID.
        // The benchmark input and trajectory hash remain identical across
        // history populations; full world hashes compare only the same fixture.
        if (historyCount_ > 0) {
            const int firstId = nextRuntimeObjectInstanceId(state);
            if (firstId <= 0 || firstId > std::numeric_limits<int>::max() - historyCount_) {
                error = "live movement history exceeds the available object ID range";
                return false;
            }
            auto& objects = state.world.accessObjects();
            objects.reserve(objects.size() + static_cast<std::size_t>(historyCount_));
            for (int history = 0; history < historyCount_; ++history) {
                WorldObjectInstance dead;
                dead.instanceId = firstId + history;
                dead.kind = WorldObjectKind::Unit; dead.type = "MTNK"; dead.owner = "Americans";
                dead.x = 2; dead.y = 2; dead.cell = runtimeCellKey(2, 2);
                dead.alive = false; dead.health = 0; dead.mission = "Destroyed";
                objects.push_back(std::move(dead));
            }
            state.objectIndex = RuntimeObjectIndex{};
            // Preserve the shared fixture's explicit ClearTile movement facts.
            // Rebuild navigation against the final world storage, then restore
            // those facts and invalidate path snapshots before the warmup.
            auto terrainInfo = std::move(state.terrainInfoByCell);
            initializeRuntimeNavigation(state);
            state.terrainInfoByCell = std::move(terrainInfo);
            state.pathTerrainCache.valid = state.pathTerrainCache.dynamicValid = false;
            invalidateRuntimeTickOccupancyCache(state);
            invalidateRuntimeObjectProjectionCache(state);
            (void)completeRuntimeWorldRefresh(state);
        }
        fixtureWorldObjects_ = state.world.getObjects().size();
        fixtureAliveObjects_ = static_cast<std::size_t>(std::count_if(
            state.world.getObjects().begin(), state.world.getObjects().end(),
            [](const auto& object) { return object.alive && object.health > 0; }));
        std::cout << "Live movement history fixture: historyObjects=" << historyCount_
            << " worldObjects=" << fixtureWorldObjects_ << " aliveObjects=" << fixtureAliveObjects_ << '\n';
        error.clear();
        return true;
    }
    void mix(std::uint64_t value) { trajectory_ ^= value; trajectory_ *= 1099511628211ULL; }
    bool requested_ = false, mixed_ = false, choke_ = false, combat_ = false, battle_ = false, failed_ = false;
    bool sustainMovement_ = false;
    bool playerScene_ = false, mapSideValid_ = true;
    bool keepAuthoredWorld_ = false;
    std::array<RuntimeCellPoint,2> authoredGoals_{};
    RuntimeCellPoint authoredCamera_{};
    bool mixedRoster_ = false, playerMining_ = false;
    RuntimePlayerMiningBenchmark miningSample_;
    int mapSide_ = 0;
    std::uint64_t acceptedMovesTotal_ = 0, rejectedMovesTotal_ = 0;
    bool historyValid_ = true;
    int historyCount_ = 0;
    std::size_t fixtureWorldObjects_ = 0, fixtureAliveObjects_ = 0;
    RuntimeMassBattleSample battleSample_;
    RuntimeBenchmarkProjectileTracker projectileTracker_;
    std::uint64_t previousShots_ = 0;
    int count_ = 0, commands_ = 0, samples_ = 0, peakActive_ = 0;
    int peakVisible_ = 0;
    int finalAlive_ = 0, finalArrived_ = 0;
    int targetId_ = -1;
    int targetOriginalStrength_ = 0, targetMaxHealth_ = 0;
    int targetInitialHealth_ = 0, targetFinalHealth_ = 0;
    int lastAttackTick_ = -1, lastSampleTick_ = 0;
    bool targetAlive_ = false;
    std::uint64_t attacks_ = 0;
    std::size_t peakProjectiles_ = 0, peakImpacts_ = 0;
    std::uint64_t pathRequests_ = 0;
    std::size_t pendingPathsPeak_ = 0;
    std::array<double, 8> phaseTotals_{}, phasePeaks_{};
    std::uint64_t activeTotal_ = 0, trajectory_ = 1469598103934665603ULL;
    double commandMaxMs_ = 0;
    std::vector<int> ids_;
    std::map<int, RuntimeCellPoint> starts_, destinations_;
    std::unordered_set<int> moved_;
};
