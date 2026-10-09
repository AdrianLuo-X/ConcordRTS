// Shared, opt-in battle fixture. Included after the ordinary movement fixture
// and its timing helpers. All orders use production AI command helpers; world
// hydration, HP, weapons, sight, damage, deaths and corpse removal are ordinary.
struct RuntimeBenchmarkProjectileTracker {
    struct Observed {
        int actorId = 0, spawnTick = 0, side = 0;
    };
    // Keep only the previous/current active + recent-expired populations.
    // Expired projectiles remain observable continuously in the production
    // queue; every completed tick must be sampled. Include spawnTick in the
    // identity because production actor IDs can wrap and later be reused.
    std::vector<Observed> previous, current;
    std::size_t peakObservations = 0;
    void reserve(std::size_t count) { previous.reserve(count); current.reserve(count); }
    std::size_t capacity() const { return previous.capacity() + current.capacity(); }
    template<class SideForProjectile>
    std::array<std::uint64_t, 2> sample(const RuntimeWindowState& state,
        int firstSampleTick, SideForProjectile sideForProjectile) {
        current.clear();
        const auto observe = [&](const RuntimeProjectileEffect& projectile) {
            const int side = sideForProjectile(projectile);
            if (side < 0 || side > 1 || projectile.spawnTick < firstSampleTick
                || projectile.actorId < 0) return;
            current.push_back({projectile.actorId, projectile.spawnTick, side});
        };
        for (const auto& projectile : state.projectiles) observe(projectile);
        for (const auto& projectile : state.recentExpiredProjectiles) observe(projectile);
        const auto less = [](const Observed& a, const Observed& b) {
            return a.actorId < b.actorId || (a.actorId == b.actorId && a.spawnTick < b.spawnTick);
        };
        std::sort(current.begin(), current.end(), less);
        current.erase(std::unique(current.begin(), current.end(),
            [](const Observed& a, const Observed& b) {
                return a.actorId == b.actorId && a.spawnTick == b.spawnTick;
            }), current.end());
        peakObservations = std::max(peakObservations, current.size());
        std::array<std::uint64_t, 2> shots{};
        for (const auto& projectile : current)
            if (!std::binary_search(previous.begin(), previous.end(), projectile, less))
                ++shots[projectile.side];
        previous.swap(current);
        return shots;
    }
};

struct RuntimeMassBattleSample {
    static constexpr int segmentTicks = 120;
    struct Segment {
        int samples = 0;
        std::array<int, 2> aliveBegin{}, aliveEnd{}, buildingsEnd{};
        std::array<long long, 2> shots{}, damage{};
        long long attacks = 0, movingTotal = 0;
        int visibleMobilePeak = 0, visibleBuildingsPeak = 0;
        double simulationMs = 0.0;
    };
    std::vector<int> unitIds;
    std::map<int, int> sides;
    std::map<int, RuntimeCellPoint> starts;
    std::map<int, int> previousHealth;
    std::vector<Segment> segments;
    RuntimeBenchmarkProjectileTracker projectileTracker;
    std::unordered_set<int> moved;
    std::array<int, 2> alive{}, buildings{}, firstFireSample{{-1, -1}}, lastFireSample{{-1, -1}};
    std::array<long long, 2> shots{}, damage{};
    int count = 0, samples = 0, firstTick = -1, active = 0, peakMoving = 0;
    int peakVisibleMobile = 0, peakVisibleBuildings = 0, bothArmiesAliveSamples = 0;
    long long movingTotal = 0, attacks = 0, acceptedMoves = 0, acceptedAttacks = 0;
    int lastAttackSample = -1;
    std::uint64_t trajectory = 1469598103934665603ULL;
    bool valid = true;

    void mix(std::uint64_t value) { trajectory ^= value; trajectory *= 1099511628211ULL; }
    bool prepare(RuntimeWindowState& state, int units, std::string& error) {
        count = units;
        // Reserve the maximum live soak's scalar summaries before timing
        // starts, avoiding an allocation staircase in memory measurements.
        segments.reserve((216000 + segmentTicks - 1) / segmentTicks);
        projectileTracker.reserve(4096);
        if (!prepareRuntimeMassMovementWorld(state, units, unitIds, error, false, true, true))
            return false;
        for (const auto& object : state.world.getObjects()) {
            const int side = object.owner == "Americans" ? 0 : object.owner == "Russians" ? 1 : -1;
            if (side < 0 || !object.alive || object.health != runtimeMaxHealthForObject(state, object)) {
                error = "battle fixture requires two owners and full authored HP: id="
                    + std::to_string(object.instanceId) + " type=" + object.type
                    + " owner=" + object.owner + " health=" + std::to_string(object.health)
                    + " expected=" + std::to_string(runtimeMaxHealthForObject(state, object));
                return false;
            }
            sides.emplace(object.instanceId, side);
            previousHealth.emplace(object.instanceId, object.health);
            if (object.kind == WorldObjectKind::Structure) ++buildings[side];
            else {
                ++alive[side];
                starts.emplace(object.instanceId, runtimeCellPointForObject(object));
            }
        }
        if (alive != std::array<int, 2>{units / 2, units / 2}
            || buildings != std::array<int, 2>{4, 4}
            || runtimeOwnersAreAllied(state, "Americans", "Russians")) {
            error = "battle fixture did not hydrate equal hostile armies and four buildings per side";
            return false;
        }
        error.clear();
        return true;
    }

    // Commands depend only on completed ticks and legally visible targets.
    // A live Attack or Move is retained, so the driver never resets its path
    // merely because another 30-tick decision interval elapsed.
    void issueOrders(RuntimeWindowState& state, int sampleIndex) {
        if (sampleIndex % 30 != 0) return;
        RuntimeAutonomousOrderScope orderScope(state.runtimeIssuingAutonomousOrder);
        std::array<std::unordered_set<int>, 2> groups;
        for (int id : unitIds) {
            const auto* object = findObjectById(state, id);
            if (object && object->alive && object->health > 0) groups[sides.at(id)].insert(id);
        }
        for (int id : unitIds) {
            auto* object = findMutableObjectById(state, id);
            if (!object || !object->alive || object->health <= 0) continue;
            const auto current = state.behaviors.find(id);
            if (current != state.behaviors.end()
                && current->second.kind == RuntimeBehaviorKind::Attack
                && current->second.state != RuntimeBehaviorState::Complete) {
                const auto* target = findObjectById(state, current->second.targetObjectId);
                if (target && target->alive && target->health > 0
                    && runtimeObjectVisibleToOwner(state, object->owner, *target)) continue;
            }
            const WorldObjectInstance* target = nullptr;
            long long bestDistance = std::numeric_limits<long long>::max();
            for (const auto& candidate : state.world.getObjects()) {
                if (!candidate.alive || candidate.health <= 0
                    || !runtimeOwnersAreHostileForAutoTarget(state, object->owner, candidate.owner)
                    || !runtimeObjectVisibleToOwner(state, object->owner, candidate)) continue;
                const auto cell = runtimeCellPointForObject(candidate);
                const long long dx = static_cast<long long>(cell.x) - object->x;
                const long long dy = static_cast<long long>(cell.y) - object->y;
                const long long distance = dx * dx + dy * dy;
                if (!target || distance < bestDistance
                    || (distance == bestDistance && candidate.instanceId < target->instanceId)) {
                    target = &candidate;
                    bestDistance = distance;
                }
            }
            if (target && runtimeComputerAiIssueAttack(state, *object, *target, false, id % 16)) {
                ++acceptedAttacks;
                continue;
            }
            if (current != state.behaviors.end()
                && current->second.kind == RuntimeBehaviorKind::Move
                && current->second.state != RuntimeBehaviorState::Complete) continue;
            const auto start = starts.at(id);
            // Opposing marching lanes reach contact through normal sight.
            // The known destination is a map cell, never a hidden object ID.
            const RuntimeCellPoint destination{44 - start.x, start.y};
            if (runtimeCellPointForObject(*object) != destination
                && runtimeComputerAiIssueMove(state, *object, destination, groups[sides.at(id)]))
                ++acceptedMoves;
        }
    }

    void sample(const RuntimeWindowState& state, double simulationMs) {
        if (firstTick < 0) firstTick = state.tick;
        if (state.tick != firstTick + samples) valid = false;
        const auto before = alive;
        alive = {}; buildings = {}; active = 0;
        if (samples % segmentTicks == 0) {
            segments.emplace_back();
            segments.back().aliveBegin = before;
        }
        auto& segment = segments.back();
        int visibleMobile = 0, visibleBuildings = 0;
        mix(static_cast<std::uint64_t>(state.tick));
        for (const auto& [id, side] : sides) {
            const auto* object = findObjectById(state, id);
            const bool living = object && object->alive && object->health > 0;
            const int health = living ? object->health : 0;
            const int lost = std::max(0, previousHealth.at(id) - health);
            damage[side] += lost;
            segment.damage[side] += lost;
            previousHealth.at(id) = health;
            const bool mobile = starts.contains(id);
            if (living) {
                if (mobile) {
                    ++alive[side];
                    if (!state.terrainByCell.contains(runtimeCellKey(object->x, object->y))) valid = false;
                    if (runtimeCellPointForObject(*object) != starts.at(id)) moved.insert(id);
                    active += object->renderMoveActive ? 1 : 0;
                } else ++buildings[side];
                if (runtimeObjectVisibleToOwner(state, state.economy.owner, *object)) {
                    if (mobile) ++visibleMobile;
                    else ++visibleBuildings;
                }
            }
            mix(static_cast<std::uint64_t>(id));
            mix(living ? 1ULL : 0ULL);
            mix(static_cast<std::uint64_t>(health));
            if (object) {
                mix(static_cast<std::uint64_t>(object->x));
                mix(static_cast<std::uint64_t>(object->y));
                mix(static_cast<std::uint64_t>(object->facing));
            }
        }
        const auto newShots = projectileTracker.sample(state, firstTick,
            [&](const RuntimeProjectileEffect& projectile) {
                const auto owner = sides.find(projectile.attackerObjectId);
                return owner == sides.end() ? -1 : owner->second;
            });
        for (int side = 0; side < 2; ++side) {
            if (newShots[side] == 0) continue;
            shots[side] += static_cast<long long>(newShots[side]);
            segment.shots[side] += static_cast<long long>(newShots[side]);
            if (firstFireSample[side] < 0) firstFireSample[side] = samples + 1;
            lastFireSample[side] = samples + 1;
        }
        attacks += state.actionSummary.attacksThisTick;
        segment.attacks += state.actionSummary.attacksThisTick;
        if (state.actionSummary.attacksThisTick > 0) lastAttackSample = samples + 1;
        bothArmiesAliveSamples += alive[0] > 0 && alive[1] > 0 ? 1 : 0;
        movingTotal += active;
        peakMoving = std::max(peakMoving, active);
        peakVisibleMobile = std::max(peakVisibleMobile, visibleMobile);
        peakVisibleBuildings = std::max(peakVisibleBuildings, visibleBuildings);
        segment.movingTotal += active;
        segment.aliveEnd = alive;
        segment.buildingsEnd = buildings;
        segment.visibleMobilePeak = std::max(segment.visibleMobilePeak, visibleMobile);
        segment.visibleBuildingsPeak = std::max(segment.visibleBuildingsPeak, visibleBuildings);
        segment.simulationMs += simulationMs;
        ++segment.samples; ++samples;
    }

    bool succeeded() const {
        return valid && samples >= 120 && acceptedMoves > 0 && acceptedAttacks > 0
            && peakMoving > 0 && !moved.empty() && bothArmiesAliveSamples > 0
            && shots[0] > 0 && shots[1] > 0 && damage[0] > 0 && damage[1] > 0
            && alive[0] + alive[1] < count && peakVisibleBuildings >= 4;
    }
    void print(const char* runner) const {
        std::cout << runner << " battle result: passed=" << (succeeded() ? "yes" : "no")
            << " units=" << count << " buildings=8 samples=" << samples
            << " acceptedMoves=" << acceptedMoves << " acceptedAttacks=" << acceptedAttacks
            << " aliveA=" << alive[0] << " aliveB=" << alive[1]
            << " deathsA=" << count / 2 - alive[0] << " deathsB=" << count / 2 - alive[1]
            << " buildingsA=" << buildings[0] << " buildingsB=" << buildings[1]
            << " shotsA=" << shots[0] << " shotsB=" << shots[1]
            << " damageA=" << damage[0] << " damageB=" << damage[1]
            << " firstFireA=" << firstFireSample[0] << " firstFireB=" << firstFireSample[1]
            << " lastFireA=" << lastFireSample[0] << " lastFireB=" << lastFireSample[1]
            << " lastAttackSample=" << lastAttackSample << " attacks=" << attacks
            << " shotTrackingPeakObservations=" << projectileTracker.peakObservations
            << " shotTrackingCapacity=" << projectileTracker.capacity()
            << " movedUnique=" << moved.size() << " peakMoving=" << peakMoving
            << " meanMoving=" << (samples ? static_cast<double>(movingTotal) / samples : 0.0)
            << " bothArmiesAliveSamples=" << bothArmiesAliveSamples
            << " visibleMobilePeak=" << peakVisibleMobile << " visibleBuildingsPeak=" << peakVisibleBuildings
            << " trajectoryHash=" << trajectory << '\n';
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const auto& segment = segments[i];
            std::cout << runner << " battle segment: units=" << count
                << " firstSample=" << i * segmentTicks + 1 << " samples=" << segment.samples
                << " aliveBeginA=" << segment.aliveBegin[0] << " aliveBeginB=" << segment.aliveBegin[1]
                << " aliveEndA=" << segment.aliveEnd[0] << " aliveEndB=" << segment.aliveEnd[1]
                << " buildingsA=" << segment.buildingsEnd[0] << " buildingsB=" << segment.buildingsEnd[1]
                << " shotsA=" << segment.shots[0] << " shotsB=" << segment.shots[1]
                << " damageA=" << segment.damage[0] << " damageB=" << segment.damage[1]
                << " attacks=" << segment.attacks
                << " meanMoving=" << static_cast<double>(segment.movingTotal) / segment.samples
                << " simMeanMs=" << segment.simulationMs / segment.samples
                << " visibleMobilePeak=" << segment.visibleMobilePeak
                << " visibleBuildingsPeak=" << segment.visibleBuildingsPeak << '\n';
        }
    }
};

bool runRuntimeMassBattleSample(RuntimeWindowState& state, int unitCount, int ticks,
    std::array<std::uint64_t, 3>& hashes, std::string& error) {
    RuntimeMassBattleSample battle;
    if (!battle.prepare(state, unitCount, error)) return false;
    for (int i = 0; i < 8; ++i) { ++state.tick; (void)advanceRuntimeSimulation(state); }
    hashes[0] = runtimeStandaloneSimulationStateHash(state, error);
    if (hashes[0] == 0) return false;
    hashes[1] = runtimeStableHashString("MassBattleTrajectory/v1");
    runtimeMultiplayerAuthoritativeMixUnsigned(hashes[1], hashes[0]);
    hashes[2] = hashes[0];
    std::array<std::vector<double>, 10> timings;
    std::vector<double> commandTimings;
    for (auto& values : timings) values.reserve(ticks);
    for (int step = 0; step < ticks; ++step) {
        ++state.tick;
        const auto started = runtimeSmokeNow();
        if (step % 30 == 0) {
            const auto commandStart = runtimeSmokeNow();
            battle.issueOrders(state, step);
            commandTimings.push_back(runtimeSmokeElapsedMs(commandStart));
        }
        const auto simulationStart = runtimeSmokeNow();
        (void)advanceRuntimeSimulation(state);
        const double simulationMs = runtimeSmokeElapsedMs(simulationStart);
        const std::array<double, 10> sample{runtimeSmokeElapsedMs(started), simulationMs,
            state.perf.lastSimEffectsMs, state.perf.lastSimEconomyMs, state.perf.lastSimTriggersMs,
            state.perf.lastSimBuildMs, state.perf.lastSimGuardMs, state.perf.lastSimMoveMs,
            state.perf.lastSimAttackMs, state.perf.lastSimPathMs};
        for (std::size_t i = 0; i < sample.size(); ++i) timings[i].push_back(sample[i]);
        battle.sample(state, simulationMs);
        hashes[2] = runtimeStandaloneSimulationStateHash(state, error);
        if (hashes[2] == 0) return false;
        runtimeMultiplayerAuthoritativeMixUnsigned(hashes[1], hashes[2]);
        if ((step + 1) % RuntimeMassBattleSample::segmentTicks == 0 || step + 1 == ticks)
            std::cout << "Mass battle progress: units=" << unitCount << " sample=" << step + 1
                << " aliveA=" << battle.alive[0] << " aliveB=" << battle.alive[1]
                << " shotsA=" << battle.shots[0] << " shotsB=" << battle.shots[1]
                << " moving=" << battle.active << " stateHash=" << hashes[2] << '\n' << std::flush;
    }
    constexpr std::array<const char*, 10> names{"tick-with-command", "simulation", "effects",
        "economy", "triggers", "build", "guard", "move-including-path", "attack", "path-subset"};
    const auto printTiming = [&](const char* name, const std::vector<double>& values) {
        const auto summary = RuntimeMassMovementBenchmarkDetail::summarize(values);
        std::cout << "Mass movement timing: scenario=mixed-battle units=" << unitCount
            << " stage=" << name << " samples=" << values.size() << " meanMs=" << summary.mean
            << " p95Ms=" << summary.p95 << " maxMs=" << summary.maximum << '\n';
    };
    for (std::size_t i = 0; i < names.size(); ++i) printTiming(names[i], timings[i]);
    printTiming("command-only", commandTimings);
    battle.print("Mass");
    std::cout << "Mass movement result: scenario=mixed-battle units=" << unitCount << " ticks=" << ticks
        << " initialHash=" << hashes[0] << " trajectoryHash=" << hashes[1] << " finalHash=" << hashes[2]
        << '\n' << std::flush;
    if (!battle.succeeded()) {
        error = "battle requires both armies firing and taking damage, real deaths, movement and visible buildings";
        return false;
    }
    error.clear();
    return true;
}
