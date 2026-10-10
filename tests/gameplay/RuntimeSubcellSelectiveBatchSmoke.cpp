#include "simulation/navigation/occupancy/RuntimeSubcellSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>

namespace {

WorldObjectInstance makeObject(int id, int x, int y, bool infantry = true) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = infantry ? WorldObjectKind::Infantry : WorldObjectKind::Unit;
    object.type = infantry ? "E1" : "MTNK";
    object.owner = "Americans";
    object.x = x;
    object.y = y;
    const auto encoded = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(y) * 100;
    object.cell = encoded <= std::numeric_limits<int>::max() ? static_cast<int>(encoded) : -1;
    object.subCell = RuntimeSubcellSystemState::activeSubCells()[id % 3];
    object.alive = true;
    object.health = 100;
    object.mission = "Guard";
    return object;
}

std::vector<RuntimeSubcellLocation> neighbors(int x, int y, bool bridge = false) {
    std::vector<RuntimeSubcellLocation> result;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx != 0 || dy != 0)
                result.push_back({x + dx, y + dy, -1, bridge});
        }
    }
    return result;
}

bool compare(const RuntimeSubcellSystemState& state,
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrain,
    const std::vector<RuntimeSubcellLocation>& destinations,
    const std::unordered_set<int>& ignored, const RuntimeSubcellContext& context,
    int fixture, std::size_t& checks) {
    const auto before = state.stateHash(objects, terrain);
    const auto reference = state.availableSubCellsBatch(
        objects, terrain, destinations, ignored, context, true);
    const auto selective = state.availableSubCellsBatch(
        objects, terrain, destinations, ignored, context, false);
    if (reference != selective || before != state.stateHash(objects, terrain)) {
        std::cerr << "selective batch mismatch fixture=" << fixture
            << " destinations=" << destinations.size() << '\n';
        return false;
    }
    for (std::size_t i = 0; i < destinations.size(); ++i) {
        if (selective[i] != state.availableSubCells(
                objects, terrain, destinations[i], ignored, context)) {
            std::cerr << "scalar mismatch fixture=" << fixture << " index=" << i << '\n';
            return false;
        }
    }
    checks += destinations.size();
    return true;
}

bool contracts() {
    std::mt19937 random(0x21a045abU);
    std::size_t checks = 0;
    for (int fixture = 0; fixture < 120; ++fixture) {
        RuntimeSubcellSystemState state;
        RuntimeSubcellContext context;
        std::vector<WorldObjectInstance> objects;
        std::vector<WorldTerrainObject> terrain;
        for (int i = 0; i < 96; ++i) {
            auto object = makeObject(i + 1, random() % 16, random() % 16, i % 3 != 0);
            object.onBridge = random() % 4 == 0;
            object.subCell = static_cast<int>(random() % 8) - 1;
            if (i % 7 == 0) object.kind = WorldObjectKind::Structure;
            if (i % 11 == 0) context.ignoredWholeCellObjectIds.insert(object.instanceId);
            if (i % 13 == 0) object.airborne = true;
            if (i % 17 == 0) object.health = 0;
            if (i % 19 == 0) object.alive = false;
            if (i % 23 == 0) object.garrisonHostObjectId = 300;
            if (i % 5 == 0) object.mission = "iNsIdEtRaNsPoRt";
            if (i % 29 == 0) { object.x = -1; object.y = -2; object.cell = 777; }
            if (i % 31 == 0) object.transportPassengerIds = {2, 7, 22, 33};
            objects.push_back(std::move(object));
        }
        // Duplicate IDs deliberately share neither coordinates nor layer.
        objects[92].instanceId = objects[1].instanceId;
        objects[93].instanceId = objects[2].instanceId;
        for (int i = 0; i < 16; ++i) {
            WorldTerrainObject object;
            object.x = random() % 16;
            object.y = random() % 16;
            object.cell = 777;
            object.footprintWidth = static_cast<int>(random() % 5) - 1;
            object.footprintHeight = static_cast<int>(random() % 5) - 1;
            object.alive = i % 7 != 0;
            object.health = i % 11 == 0 ? 0 : 100;
            object.occupationBits = static_cast<int>(random() % 24) - 12;
            object.occupiedSubCells.clear();
            for (int j = 0; j < 5; ++j) {
                if (random() % 2 == 0) object.occupiedSubCells.push_back(j);
            }
            object.occupiedSubCells.push_back(99);
            if (i == 3) { object.x = -1; object.y = 8; }
            terrain.push_back(std::move(object));
        }
        for (int i = 0; i < 8; ++i) {
            objects.push_back(makeObject(1000 + i, 50 + i, 50));
            if (!state.reserveForDestination(objects, terrain, 1000 + i,
                    {static_cast<int>(random() % 16), static_cast<int>(random() % 16),
                        -1, i % 2 == 0}).reserved()) {
                state.reserveForDestination(objects, terrain, 1000 + i,
                    {20 + i, 20, -1, false});
            }
        }
        std::vector<RuntimeSubcellLocation> targets = neighbors(7, 7, fixture % 2 == 0);
        targets.push_back({-1, -1, 777, false});
        targets.push_back({-1, -1, 777, true});
        targets.push_back({-1, -1, -1, false});
        targets.push_back({7, 7, 12345, false});
        targets.push_back(targets.front());
        targets.push_back({-1, 5, 777, false});
        targets.push_back({15, 15, -1, false});
        targets.push_back({0, 0, 777, false});
        std::unordered_set<int> ignored = fixture % 2 == 0
            ? std::unordered_set<int>{2, 3, 1000, 1003} : std::unordered_set<int>{};
        for (std::size_t count : {0U, 1U, 8U, 15U, 16U, 17U}) {
            auto subset = targets;
            subset.resize(count, {20, 20, -1, false});
            if (!compare(state, objects, terrain, subset, ignored, context, fixture, checks))
                return false;
        }
        // Fresh calls must observe an in-place landing/mission/owner-neutral
        // occupancy change; there is no persistent cache in the batch API.
        objects[1].airborne = false;
        objects[1].mission = "Guard";
        objects[1].garrisonHostObjectId = -1;
        objects[1].x = 7;
        objects[1].y = 7;
        objects[1].onBridge = false;
        state.release(1003);
        if (!compare(state, objects, terrain, targets, {}, {}, fixture, checks)) return false;
    }
    {
        RuntimeSubcellSystemState state;
        constexpr int high = std::numeric_limits<int>::max();
        std::vector<WorldObjectInstance> objects = {
            makeObject(1, high - 1, high - 1), makeObject(2, 1000, 1000, false)};
        WorldTerrainObject terrain;
        terrain.x = high - 2;
        terrain.y = high - 2;
        terrain.footprintWidth = 3;
        terrain.footprintHeight = 3;
        terrain.alive = true;
        terrain.health = 100;
        terrain.occupationBits = -8;
        terrain.occupiedSubCells = {4, 99, 4};
        if (!compare(state, objects, {terrain},
                {{high, high, -1, false}, {high - 1, high - 1, 1, false},
                 {high, high, -1, true}, {0, 0, 1, false}}, {}, {}, 121, checks)) return false;
        terrain.x = 100;
        terrain.y = 100;
        terrain.footprintWidth = 64;
        terrain.footprintHeight = 64;
        if (!compare(state, objects, {terrain},
                {{99, 100, -1, false}, {100, 100, -1, false}, {163, 163, -1, false},
                 {164, 164, -1, false}, {110, 110, -1, true}}, {}, {}, 122, checks)) return false;
        const auto referenceEnabled = [](const char* name) {
            const char* value = std::getenv(name);
            return value != nullptr && std::string(value) == "1";
        };
        if (!referenceEnabled("CONCORD_MOVEMENT_REFERENCE")
            && !referenceEnabled("CONCORD_FRAME_SCALING_REFERENCE")) {
        // Extremely large, valid footprints never expand on the selective path.
        terrain.footprintWidth = 1000000000;
        terrain.footprintHeight = 1000000000;
        const auto huge = state.availableSubCellsBatch(objects, {terrain},
            {{100, 100, -1, false}, {999999999, 999999999, -1, false},
             {1000000100, 1000000100, -1, false}});
        if (huge != std::vector<std::vector<int>>{{2, 3}, {2, 3}, {2, 4, 3}}) return false;
        }
    }
    std::cout << "selective batch contract passed cells=" << checks << '\n';
    return true;
}

void benchmark(int population, int terrainCount, bool dense) {
    RuntimeSubcellSystemState state;
    std::vector<WorldObjectInstance> objects;
    std::vector<WorldTerrainObject> terrain;
    for (int i = 0; i < population; ++i) {
        objects.push_back(makeObject(i + 1, 10 + i % 16,
            10 + (dense ? (i / 16) % 16 : i / 16), i % 2 == 0));
    }
    for (int i = 0; i < terrainCount; ++i) {
        WorldTerrainObject object;
        object.x = i % 32;
        object.y = i / 32;
        object.alive = true;
        object.health = 100;
        object.occupationBits = i % 8;
        object.occupiedSubCells = {i % 3 + 2};
        terrain.push_back(std::move(object));
    }
    for (int i = 0; i < 16; ++i) {
        auto object = makeObject(population + 1 + i, 40 + i, 40, false);
        object.kind = WorldObjectKind::Structure;
        objects.push_back(std::move(object));
    }
    std::vector<std::vector<RuntimeSubcellLocation>> targets;
    for (int i = 0; i < 32; ++i) targets.push_back(neighbors(10 + i % 16, 10 + i / 16));
    std::uint64_t checksum = 0;
    constexpr int iterations = 3000;
    for (int mode : {1, 2, 0, 2, 1}) {
        const auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) {
            const auto& cells = targets[i % targets.size()];
            if (mode == 0) {
                for (const auto& cell : cells) {
                    const auto result = state.availableSubCells(objects, terrain, cell, {i % population + 1});
                    checksum += result.size() + (result.empty() ? 0 : result.front());
                }
            } else {
                const auto result = state.availableSubCellsBatch(objects, terrain, cells,
                    {i % population + 1}, {}, mode == 1);
                for (const auto& slots : result)
                    checksum += slots.size() + (slots.empty() ? 0 : slots.front());
            }
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        std::cout << "subcell micro population=" << population << " terrain=" << terrainCount
            << " dense=" << dense << " mode=" << (mode == 0 ? "scalar8" : mode == 1 ? "world-batch" : "selective8")
            << " batches=" << iterations << " ms=" << elapsed << " checksum=" << checksum << '\n';
    }
}
} // namespace

int main(int argc, char**) {
    if (!contracts()) return 1;
    if (argc > 1) {
        benchmark(256, 0, false);
        benchmark(256, 128, true);
        benchmark(1024, 128, true);
    }
    return 0;
}
