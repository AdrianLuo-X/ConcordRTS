#include "world/model/GameWorld.h"
#include "render/shadows/RuntimeWaterWakeHistoryMaintenance.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace RuntimeWaterWakeHistoryContract {

struct Actor {
    int objectId = 0, x = 0, y = 0, spawnTick = 0;
    std::string image = "WAKE";
    bool operator==(const Actor& other) const {
        return objectId == other.objectId && x == other.x && y == other.y
            && spawnTick == other.spawnTick && image == other.image;
    }
};

struct ReferenceWakeState {
    std::unordered_map<int, int> lastSpawn;
    std::vector<Actor> actors;
    std::uint64_t emissions = 0;
    int lastUpdateTick = -1;

    void reset() {
        lastSpawn.clear(); actors.clear(); lastUpdateTick = -1;
    }

    // Same lifetime and exact-tick dedup equations as the production updater.
    void emit(const WorldObjectInstance& object, int tick) {
        if (!object.alive || !object.renderMoveActive || object.submerged
            || object.onBridge || object.mission == "InsideTransport"
            || tick < 0 || tick % 10 != 0) return;
        const auto previous = lastSpawn.find(object.instanceId);
        if (previous != lastSpawn.end() && previous->second == tick) return;
        actors.push_back({object.instanceId, object.x, object.y, tick, "WAKE"});
        lastSpawn[object.instanceId] = tick;
        ++emissions;
    }

    void advance(const GameWorld& world, int tick) {
        if (tick < 0) return;
        if (lastUpdateTick >= 0 && tick < lastUpdateTick) reset();
        if (lastUpdateTick == tick) return;
        actors.erase(std::remove_if(actors.begin(), actors.end(), [tick](const Actor& actor) {
            const int age = tick - actor.spawnTick;
            return age < 0 || age / 5 >= 7;
        }), actors.end());
        for (const auto& object : world.getObjects()) emit(object, tick);
        lastUpdateTick = tick;
    }
};

WorldObjectInstance object(int id, bool alive, bool moving = true) {
    WorldObjectInstance result;
    result.instanceId = id; result.type = "BOAT"; result.owner = "Americans";
    result.kind = WorldObjectKind::Unit; result.alive = alive;
    result.health = alive ? 100 : 0; result.x = id % 31; result.y = id % 23;
    result.cell = result.x + 100 * result.y; result.mission = "Guard";
    result.renderMoveActive = moving;
    return result;
}

bool same(const ReferenceWakeState& oldState, const ReferenceWakeState& newState) {
    return oldState.actors == newState.actors && oldState.emissions == newState.emissions
        && oldState.lastUpdateTick == newState.lastUpdateTick;
}

} // namespace RuntimeWaterWakeHistoryContract

bool runRuntimeWaterWakeHistoryMaintenanceContract(std::string& error) {
    using namespace RuntimeWaterWakeHistoryContract;
    const auto fail = [&](const std::string& why) {
        error = "water wake history maintenance: " + why;
        return false;
    };
    std::uint64_t tickComparisons = 0, hashChecks = 0, removals = 0;
    const auto maintenance = [&](const GameWorld& world, int tick,
                                 ReferenceWakeState& optimized) -> bool {
        const auto beforeHash = world.deterministicSimulationStateHash();
        const auto before = optimized.lastSpawn;
        const auto oldActors = optimized.actors;
        const auto oldEmissions = optimized.emissions;
        const auto removed = pruneRuntimeWaterWakeSpawnHistory(
            optimized.lastSpawn, world.getObjects(), tick);
        ++hashChecks; removals += removed;
        if (beforeHash != world.deterministicSimulationStateHash()
            || optimized.actors != oldActors || optimized.emissions != oldEmissions
            || before.size() != optimized.lastSpawn.size() + removed) return false;
        std::unordered_map<int, bool> alive;
        for (const auto& item : world.getObjects()) alive[item.instanceId] = alive[item.instanceId] || item.alive;
        for (const auto& [id, stamp] : before) {
            const auto retained = optimized.lastSpawn.find(id);
            if (retained != optimized.lastSpawn.end()) {
                if (retained->second != stamp) return false;
            } else if (tick < 0 || stamp >= tick || alive[id]) return false;
        }
        for (const auto& [id, stamp] : optimized.lastSpawn) {
            const auto prior = before.find(id);
            if (prior == before.end() || prior->second != stamp) return false;
        }
        return true;
    };

    GameWorld world;
    auto& objects = world.accessObjects();
    for (int id = 1; id <= 6; ++id) objects.push_back(object(id, true, id == 1 || id == 6));
    objects[2].submerged = true;
    objects[3].mission = "InsideTransport"; // Alive but hidden/paused remains tracked.
    objects[4].alive = false; objects[4].health = 100;
    objects[4].mission = "InsideTransport";
    constexpr int historicCount = 10000;
    for (int id = 1000; id < 1000 + historicCount; ++id) objects.push_back(object(id, false));
    ReferenceWakeState reference, optimized;
    for (const auto& item : objects) reference.lastSpawn[item.instanceId] = -20;
    reference.lastSpawn[50000] = -20; // Missing historical ID.
    reference.lastSpawn[50001] = 900; // Future timestamp must remain untouched.
    optimized = reference;
    for (int tick = 1; tick <= 650; ++tick) {
        if (tick == 300) {
            objects[5].alive = false; objects[5].health = 0;
            reference.lastSpawn[6] = optimized.lastSpawn[6] = tick;
        }
        if (tick == 301) {
            objects[5].alive = true; objects[5].health = 100;
        }
        if (tick == 310) {
            objects[1].renderMoveActive = true;
            objects[2].submerged = false; objects[2].renderMoveActive = true;
            objects[3].mission = "Guard"; objects[3].renderMoveActive = true;
            objects[4].alive = true; objects[4].mission = "Guard"; objects[4].renderMoveActive = true;
        }
        if (tick == 320) objects[0].alive = false;
        if (tick == 321) objects[0].alive = true;
        if (tick == 450) objects.push_back(object(20000, true));
        reference.advance(world, tick); optimized.advance(world, tick);
        if (tick % 300 == 0 && !maintenance(world, tick, optimized)) return fail("maintenance changed a living/current/future entry or world hash");
        // Exact-tick repeated renders and direct object-level callers cannot emit twice.
        reference.advance(world, tick); optimized.advance(world, tick);
        for (const auto& item : objects) { reference.emit(item, tick); optimized.emit(item, tick); }
        if (!same(reference, optimized)) return fail("emissions/actors differ tick=" + std::to_string(tick));
        ++tickComparisons;
        if (tick == 300) {
            if (optimized.lastSpawn.size() > 7 || optimized.lastSpawn.count(2) != 1
                || optimized.lastSpawn.count(3) != 1 || optimized.lastSpawn.count(4) != 1
                || optimized.lastSpawn.count(6) != 1 || optimized.lastSpawn.count(50001) != 1)
                return fail("historic cleanup removed paused/submerged/transport/current/future stamp");
            // Death -> maintenance -> resurrection within the same tick retains dedup.
            objects[5].alive = true; objects[5].health = 100;
            reference.emit(objects[5], tick); optimized.emit(objects[5], tick);
            if (!same(reference, optimized)) return fail("same-tick resurrection emitted a duplicate");
        }
    }
    // A dead source's existing wake is retained until authored expiry.
    GameWorld deadSource;
    deadSource.accessObjects().push_back(object(1, true));
    ReferenceWakeState oldDead, newDead;
    oldDead.advance(deadSource, 300); newDead.advance(deadSource, 300);
    deadSource.accessObjects()[0].alive = false;
    if (!maintenance(deadSource, 301, newDead) || newDead.actors.empty()) return fail("source death removed an existing wake");
    for (int tick = 301; tick <= 335; ++tick) {
        oldDead.advance(deadSource, tick); newDead.advance(deadSource, tick);
        if (!same(oldDead, newDead) || (tick < 335 && newDead.actors.empty())) return fail("dead-source wake lifetime differs");
        ++tickComparisons;
    }
    if (!newDead.actors.empty()) return fail("dead-source wake did not expire naturally");
    deadSource.accessObjects()[0].alive = true;
    oldDead.advance(deadSource, 340); newDead.advance(deadSource, 340);
    if (!same(oldDead, newDead)) return fail("later resurrection emission differs");

    GameWorld empty;
    ReferenceWakeState oldEmpty, newEmpty;
    oldEmpty.lastSpawn = {{1, 299}, {2, 300}, {3, 301}, {4, -1}};
    newEmpty = oldEmpty;
    if (!maintenance(empty, 300, newEmpty) || newEmpty.lastSpawn.size() != 2)
        return fail("empty world current/future protection");
    empty.accessObjects().push_back(object(1, true));
    empty.accessObjects().push_back(object(20, true));
    for (int tick : {300, 301, 310, 320}) {
        oldEmpty.advance(empty, tick); newEmpty.advance(empty, tick);
        if (!same(oldEmpty, newEmpty)) return fail("missing/recreated/new id emission differs");
        ++tickComparisons;
    }
    if (!maintenance(empty, -1, newEmpty)) return fail("negative tick altered bookkeeping");
    // Reload/configuration resets remain the original full presentation reset.
    oldEmpty.reset(); newEmpty.reset();
    oldEmpty.advance(empty, 20); newEmpty.advance(empty, 20);
    oldEmpty.advance(empty, 10); newEmpty.advance(empty, 10); // Time rollback resets too.
    if (!same(oldEmpty, newEmpty) || oldEmpty.lastSpawn != newEmpty.lastSpawn)
        return fail("reload/configuration/rollback reset differs");

    // Random small populations include reordered records and duplicate IDs:
    // any alive record protects a tracked ID, regardless of motion/type/health.
    std::mt19937 random(0x57414b45U);
    for (int fixture = 0; fixture < 96; ++fixture) {
        GameWorld randomWorld;
        ReferenceWakeState oldRandom, newRandom;
        for (int i = 0; i < 70; ++i) {
            auto item = object(1 + static_cast<int>(random() % 50), random() % 3 != 0, random() % 2 != 0);
            item.health = random() % 5 == 0 ? 0 : 100;
            item.submerged = random() % 4 == 0; item.onBridge = random() % 6 == 0;
            if (random() % 7 == 0) item.mission = "InsideTransport";
            randomWorld.accessObjects().push_back(std::move(item));
        }
        for (int id = 1; id <= 80; ++id) oldRandom.lastSpawn[id] = 280 + static_cast<int>(random() % 31);
        newRandom = oldRandom;
        oldRandom.advance(randomWorld, 300); newRandom.advance(randomWorld, 300);
        if (!maintenance(randomWorld, 300, newRandom)) return fail("random preserved-entry/hash contract");
        std::reverse(randomWorld.accessObjects().begin(), randomWorld.accessObjects().end());
        for (int tick = 300; tick <= 340; ++tick) {
            oldRandom.advance(randomWorld, tick); newRandom.advance(randomWorld, tick);
            if (!same(oldRandom, newRandom)) return fail("random/reordered emissions/actor lifecycle differs");
            ++tickComparisons;
        }
    }
    std::cout << "Water wake history equivalence passed: tickComparisons=" << tickComparisons
        << " worldHashChecks=" << hashChecks << " historicObjects=" << historicCount
        << " removedObsoleteStamps=" << removals
        << " alive/paused/submerged/transport/current/future/death/resurrection/newId/reset/actorLifetime=yes\n";
    error.clear();
    return true;
}
