#include "core/io/IniFile.h"
#include "simulation/navigation/occupancy/RuntimeSubcellSystem.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "subcell smoke: " << message << "\n";
        return false;
    }
    return true;
}

int parseInt(const std::string& text, int fallback = -1) {
    std::istringstream input(text);
    int value = fallback;
    input >> value;
    return input ? value : fallback;
}

std::vector<int> parseList(const std::string& text) {
    std::vector<int> values;
    std::istringstream input(text);
    std::string token;
    while (std::getline(input, token, ',')) {
        values.push_back(parseInt(token));
    }
    return values;
}

RuntimeSubcellLocation at(
    int x,
    int y,
    int cell,
    bool onBridge = false
) {
    return {x, y, cell, onBridge};
}

WorldObjectInstance makeInfantry(
    int id,
    const RuntimeSubcellLocation& location,
    int subCell
) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = WorldObjectKind::Infantry;
    object.owner = "Allies";
    object.type = "E1";
    object.health = 100;
    object.alive = true;
    object.x = location.x;
    object.y = location.y;
    object.cell = location.cell;
    object.onBridge = location.onBridge;
    object.subCell = subCell;
    object.mission = "Guard";
    return object;
}

WorldObjectInstance makeHardObject(
    int id,
    WorldObjectKind kind,
    const RuntimeSubcellLocation& location
) {
    WorldObjectInstance object;
    object.instanceId = id;
    object.kind = kind;
    object.owner = "Allies";
    object.type = kind == WorldObjectKind::Structure ? "GAPOWR" : "MTNK";
    object.health = 500;
    object.alive = true;
    object.x = location.x;
    object.y = location.y;
    object.cell = location.cell;
    object.onBridge = location.onBridge;
    object.mission = "Guard";
    return object;
}

WorldTerrainObject makeTerrain(
    const RuntimeSubcellLocation& location,
    int occupationBits,
    std::vector<int> occupiedSubCells
) {
    WorldTerrainObject terrain;
    terrain.mapId = "terrain-" + std::to_string(location.cell);
    terrain.type = "TREE";
    terrain.x = location.x;
    terrain.y = location.y;
    terrain.cell = location.cell;
    terrain.strength = 200;
    terrain.health = 200;
    terrain.alive = true;
    terrain.occupationBits = occupationBits;
    terrain.occupiedSubCells = std::move(occupiedSubCells);
    return terrain;
}

WorldObjectInstance* objectById(
    std::vector<WorldObjectInstance>& objects,
    int id
) {
    for (WorldObjectInstance& object : objects) {
        if (object.instanceId == id) {
            return &object;
        }
    }
    return nullptr;
}

std::map<int, int> reservationMap(
    const std::vector<RuntimeSubcellReservationResult>& results
) {
    std::map<int, int> mapped;
    for (const RuntimeSubcellReservationResult& result : results) {
        if (result.reserved()) {
            mapped[result.moverObjectId] = result.subCell;
        }
    }
    return mapped;
}

bool hasAction(
    const RuntimeSubcellReconcileReport& report,
    RuntimeSubcellReconcileActionKind kind,
    int objectId
) {
    return std::any_of(
        report.actions.begin(),
        report.actions.end(),
        [kind, objectId](const RuntimeSubcellReconcileAction& action) {
            return action.kind == kind && action.objectId == objectId;
        }
    );
}

bool batchQueryContract() {
    RuntimeSubcellSystemState state;
    const RuntimeSubcellLocation ground = at(5, 6, 65);
    const RuntimeSubcellLocation bridge = at(5, 6, 65, true);
    const RuntimeSubcellLocation gate = at(8, 8, 88);
    const RuntimeSubcellLocation vehicle = at(9, 8, 89);
    std::vector<WorldObjectInstance> objects = {
        makeInfantry(1, ground, 2),
        makeInfantry(2, bridge, 4),
        makeInfantry(3, at(1, 1, 11), 2),
        makeInfantry(4, at(2, 2, 22), 2),
        makeInfantry(5, at(-1, -1, 777), 3),
        makeHardObject(900, WorldObjectKind::Structure, gate),
        makeHardObject(901, WorldObjectKind::Unit, vehicle)
    };
    std::vector<WorldTerrainObject> terrain = {
        makeTerrain(ground, 1, {4}),
        makeTerrain(at(11, 11, 121), 7, {2, 4, 3})
    };
    terrain.front().footprintWidth = 2;
    RuntimeSubcellContext openGate;
    // Ignoring whole-cell occupancy must never hide a vehicle.
    openGate.ignoredWholeCellObjectIds = {900, 901};
    const std::vector<RuntimeSubcellLocation> destinations = {
        ground, bridge, gate, vehicle, at(11, 11, 121),
        at(6, 6, 66), at(-1, -1, 777), at(0, 0, -1),
        at(-1, -1, -1), at(5, 6, 999), ground
    };
    const auto compare = [&](const std::unordered_set<int>& ignored,
                             const RuntimeSubcellContext& context) {
        const std::uint64_t before = state.stateHash(objects, terrain);
        const auto batch = state.availableSubCellsBatch(
            objects, terrain, destinations, ignored, context
        );
        std::vector<std::vector<int>> single;
        for (const auto& destination : destinations) {
            single.push_back(state.availableSubCells(
                objects, terrain, destination, ignored, context
            ));
        }
        return expect(batch == single, "batch query exactly matches successive queries")
            && expect(before == state.stateHash(objects, terrain),
                "batch query preserves committed occupancy and reservations");
    };
    bool ok = expect(state.availableSubCellsBatch(objects, terrain, {}).empty(),
        "empty batch returns no results");
    ok &= compare({}, {});
    ok &= compare({}, openGate);
    ok &= compare({1, 5}, openGate);
    ok &= expect(
        state.reserveForDestination(objects, terrain, 3, gate, openGate).reserved()
            && state.reserveForDestination(objects, terrain, 4, bridge).reserved(),
        "batch fixture reserves gate and bridge subcells"
    );
    ok &= compare({}, openGate);
    ok &= compare({3, 4}, openGate);
    ok &= compare({}, {}); // The gate may close after a reservation.
    ok &= expect(state.commit(objects, terrain, 3, openGate).committed(),
        "batch fixture commits gate reservation");
    state.release(4);
    objectById(objects, 2)->subCell = 3;
    objectById(objects, 1)->alive = false;
    terrain.front().occupiedSubCells = {2};
    ok &= compare({}, openGate); // No stale state survives another batch.
    ok &= compare({3}, openGate);
    return ok;
}

bool movingFormationBatchContract() {
    const auto started = std::chrono::steady_clock::now();
    RuntimeSubcellSystemState state;
    std::vector<WorldObjectInstance> objects;
    std::vector<WorldTerrainObject> terrain;
    RuntimeSubcellContext gates;
    constexpr int movers = 768;
    const auto destination = [](int lane, bool bridge = false) {
        return at(100 + lane % 24, 100 + lane / 24,
            10000 + lane, bridge);
    };
    for (int id = 1; id <= movers; ++id) {
        objects.push_back(makeInfantry(id, at(id % 64, id / 64,
            20000 + id), 2));
    }
    for (int lane = 0; lane < 192; ++lane) {
        if (lane % 7 == 0) {
            auto object = makeHardObject(10000 + lane,
                lane % 2 ? WorldObjectKind::Unit : WorldObjectKind::Structure,
                destination(lane));
            gates.ignoredWholeCellObjectIds.insert(object.instanceId);
            objects.push_back(std::move(object));
        }
        if (lane % 5 == 0) {
            auto item = makeTerrain(destination(lane),
                lane % 3 == 0 ? 7 : 1, lane % 3 == 0
                    ? std::vector<int>{2, 4, 3} : std::vector<int>{2});
            item.footprintWidth = lane % 2 + 1;
            terrain.push_back(std::move(item));
        }
    }
    objects.push_back(makeHardObject(9000, WorldObjectKind::Unit, at(20, 20, 2020)));
    objects.back().transportPassengerIds = {17, 117, 217};
    // Duplicate object ids deliberately keep the first object's validation.
    objects.push_back(makeHardObject(21, WorldObjectKind::Unit, at(30, 30, 3030)));
    objectById(objects, 33)->mission = "LiMbO";
    objectById(objects, 34)->garrisonHostObjectId = 9000;
    objectById(objects, 35)->airborne = true;
    objectById(objects, 36)->health = 0;
    objectById(objects, 37)->alive = false;

    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= value & 0xffU;
            hash *= 1099511628211ULL;
            value >>= 8U;
        }
    };
    bool ok = true;
    for (int round = 0; round < 6; ++round) {
        std::vector<RuntimeSubcellMoveRequest> requests;
        for (int id = 1; id <= movers; ++id) {
            auto goal = destination((id / 4 + round * 13) % 192,
                (id + round) % 11 == 0);
            if (id % 61 == 0) goal = at(-1, -1, 70000 + round);
            if (id % 67 == 0) goal = at(-1, -1, -1);
            requests.push_back({id, goal});
            if (id % 71 == 0) requests.push_back({id, destination(round)});
        }
        requests.push_back({9000, destination(round)});
        requests.push_back({-500, destination(round)});
        requests.push_back({10000, at(-1, -1, -1)});
        std::reverse(requests.begin(), requests.end());
        if (round % 2) std::rotate(requests.begin(), requests.begin() + 51, requests.end());
        const auto results = state.reserveBatch(objects, terrain, requests, gates);
        if (round == 0 || round == 1) {
            const auto allocated = [&](int id, int slot, int lane, bool bridge = false) {
                const auto found = std::find_if(results.begin(), results.end(),
                    [id](const auto& result) { return result.moverObjectId == id; });
                const auto expected = destination(lane, bridge);
                return found != results.end() && found->reserved()
                    && found->subCell == slot
                    && found->destination.x == expected.x && found->destination.y == expected.y
                    && found->destination.cell == expected.cell && found->destination.onBridge == bridge;
            };
            if (round == 0) {
                // Terrain lane155 occupies slot2 across the two-cell footprint
                // covering lane156. Mover624 therefore legitimately receives4.
                ok &= expect(allocated(624, 4, 156),
                    "moving formation first round assigns free4 beside authored terrain slot2");
            } else {
                const auto* mover = objectById(objects, 624);
                ok &= expect(mover && mover->x == 112 && mover->y == 106
                    && mover->cell == 10156 && !mover->onBridge && mover->subCell == 4,
                    "moving formation first divergence starts from actually committed slot4");
                // Only this four-mover group targets lane169 in round1. Ignore
                // its new reservations to independently observe the initial free
                // target; all its members started elsewhere, and terrain169 is empty.
                ok &= expect(state.availableSubCells(objects, terrain, destination(169),
                        {624, 625, 626, 627}, gates) == std::vector<int>({2, 4, 3}),
                    "moving formation first divergence target has all three canonical slots free");
                ok &= expect(allocated(624, 4, 169) && allocated(625, 2, 169)
                    && allocated(626, 2, 169, true) && allocated(627, 3, 169),
                    "moving formation retains624 current4, retains625 current2, isolates bridge626, and falls back627 to3");
            }
        }
        for (const auto& result : results) {
            mix(static_cast<std::uint64_t>(result.status));
            mix(result.moverObjectId); mix(result.destination.x);
            mix(result.destination.y); mix(result.destination.cell);
            mix(result.destination.onBridge); mix(result.subCell);
        }
        mix(state.stateHash(objects, terrain));
        // A retained reservation must survive failed reallocation, and the
        // next batch must observe actual commits, releases and world changes.
        for (int id = 1 + round; id <= movers; id += 89) {
            const auto result = state.commit(objects, terrain, id, gates);
            mix(static_cast<std::uint64_t>(result.status)); mix(result.subCell);
        }
        for (int id = 41; id <= movers; id += 113) mix(state.release(id));
        objects[round + 40].onBridge = !objects[round + 40].onBridge;
        terrain.front().occupiedSubCells = round % 2
            ? std::vector<int>{2} : std::vector<int>{4, 3};
        terrain.front().occupationBits = 1;
        if (round == 2) gates.ignoredWholeCellObjectIds.clear();
        mix(state.stateHash(objects, terrain));
    }
    const std::uint64_t beforeEmpty = state.stateHash(objects, terrain);
    ok &= expect(state.reserveBatch(objects, terrain, {}, gates).empty()
        && beforeEmpty == state.stateHash(objects, terrain),
        "empty moving batch preserves reservations");
    std::cout << "subcell moving formation hash=" << hash
        << " elapsedMs=" << std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count() << "\n";
    // Frozen legacy first-free-slot golden: 2665459986984541552 (issue09 history).
    // The slot-preserving policy first differs at round1 mover624: current4 is
    // legal in the empty target, so it retains4 rather than switching to2.
    // Independent source/free-target/group assertions above explain that change.
    ok &= expect(hash == 16328067613711142260ULL,
        "moving formation preserves current-slot allocation and state hash");
    return ok;
}

bool batchReservationRestoreContract() {
    const auto shared = at(10, 10, 1010);
    const auto other = at(11, 10, 1011);
    const auto bridge = at(10, 10, 1010, true);
    const auto blocked = at(12, 10, 1012);
    std::vector<WorldObjectInstance> objects;
    for (int id = 1; id <= 4; ++id)
        objects.push_back(makeInfantry(id, at(id, 0, id), 2));
    const std::vector<WorldTerrainObject> terrain = {
        makeTerrain(shared, 1, {4, 3}), makeTerrain(blocked, 7, {2, 4, 3})
    };
    RuntimeSubcellSystemState state;
    bool ok = expect(state.reserveForDestination(objects, terrain, 1, shared).reserved()
        && state.reserveForDestination(objects, terrain, 2, other).reserved()
        && state.reserveForDestination(objects, terrain, 4, bridge).reserved(),
        "restoration fixture primes old and non-batch reservations");
    const auto result = state.reserveBatch(objects, terrain,
        {{3, other}, {2, shared}, {1, blocked}});
    ok &= expect(result.size() == 3U
        && result[0].status == RuntimeSubcellReservationStatus::HardBlocked
        && result[1].status == RuntimeSubcellReservationStatus::NoAvailableSubcell
        && result[2].reserved() && result[2].subCell == 4,
        "failed old reservations immediately block later batch requests");
    ok &= expect(state.reservations().size() == 4U
        && state.reservations().at(1).destination.x == shared.x
        && state.reservations().at(2).destination.x == other.x
        && state.reservations().at(4).destination.onBridge,
        "failure restoration retains original locations and independent bridge layer");
    const auto next = state.reserveBatch(objects, terrain,
        {{2, bridge}, {3, bridge}, {1, at(-1, -1, -1)}, {2, blocked}});
    ok &= expect(next.size() == 4U
        && next[0].status == RuntimeSubcellReservationStatus::InvalidDestination
        && next[1].status == RuntimeSubcellReservationStatus::DuplicateMoverRequest
        && next[2].status == RuntimeSubcellReservationStatus::DuplicateMoverRequest
        && next[3].reserved() && next[3].subCell == 4
        && state.reservations().at(1).destination.x == shared.x
        && state.reservations().at(2).destination.x == other.x,
        "invalid and duplicate requests keep previous reservations");
    return ok;
}

bool reconcileReservationScaleContract() {
    constexpr int moverCount = 384;
    std::vector<WorldObjectInstance> seedObjects;
    std::vector<RuntimeSubcellMoveRequest> requests;
    const auto destination = [](int group, bool bridge = false) {
        const int x = 80 + group % 32;
        const int y = 40 + group / 32;
        return at(x, y, y * 512 + x, bridge);
    };
    for (int id = 1; id <= moverCount; ++id) {
        seedObjects.push_back(makeInfantry(id,
            at(id % 64, id / 64, (id / 64) * 512 + id % 64), 2));
        requests.push_back({id, destination((id - 1) / 3, id % 17 == 0)});
    }
    RuntimeSubcellSystemState seeded;
    const auto allocated = seeded.reserveBatch(seedObjects, {}, requests);
    bool ok = expect(allocated.size() == moverCount
        && seeded.reservations().size() == moverCount,
        "large reconciliation fixture primes every reservation");
    std::vector<WorldTerrainObject> seedTerrain;
    for (int cell = 0; cell < 2048; ++cell) {
        const int x = 200 + cell % 64, y = 100 + cell / 64;
        seedTerrain.push_back(makeTerrain(at(x, y, y * 512 + x),
            cell % 2 ? 7 : 1, cell % 2 ? std::vector<int>{2, 4, 3}
                : std::vector<int>{2}));
    }
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= value & 0xffU;
            hash *= 1099511628211ULL;
            value >>= 8U;
        }
    };
    double elapsedMs = 0.0;
    for (int round = 0; round < 6; ++round) {
        auto objects = seedObjects;
        auto terrain = seedTerrain;
        auto state = seeded;
        RuntimeSubcellContext gates;
        for (auto& object : objects) {
            const int id = object.instanceId;
            if ((id + round) % 7 == 0) {
                const auto& reservation = state.reservations().at(id);
                object.x = reservation.destination.x;
                object.y = reservation.destination.y;
                object.cell = reservation.destination.cell;
                object.onBridge = reservation.destination.onBridge;
                object.subCell = reservation.subCell;
            }
            if ((id + round) % 29 == 0) object.subCell = 99;
            if ((id + round) % 47 == 0) object.alive = false;
            if ((id + round) % 43 == 0) object.health = 0;
            if ((id + round) % 41 == 0) object.airborne = true;
            if ((id + round) % 37 == 0) object.mission = "LiMbO";
            if ((id + round) % 31 == 0) object.garrisonHostObjectId = 9001;
        }
        auto transport = makeHardObject(9001, WorldObjectKind::Unit, destination(19));
        transport.transportPassengerIds = {11 + round, 83 + round};
        objects.push_back(std::move(transport));
        objects.push_back(makeHardObject(9002, WorldObjectKind::Structure, destination(21)));
        objects.push_back(makeHardObject(9003, WorldObjectKind::Unit, destination(22, true)));
        if (round % 2 == 0) gates.ignoredWholeCellObjectIds.insert(9002);
        for (int group = 0; group < 128; group += 13) {
            terrain.push_back(makeTerrain(destination(group), round % 2 ? 7 : 1,
                round % 2 ? std::vector<int>{2, 4, 3} : std::vector<int>{2}));
        }
        // Missing, non-infantry and repeated physical-slot occupancy all
        // invalidate reservations without changing action order.
        objects.erase(objects.begin() + (round + 5));
        objects[round + 10].kind = WorldObjectKind::Unit;
        auto crowded = makeInfantry(9100, destination(round), 2);
        objects.push_back(crowded);
        crowded.instanceId = 9101;
        objects.push_back(crowded);
        const auto started = std::chrono::steady_clock::now();
        const auto report = state.reconcile(objects, terrain, gates);
        elapsedMs += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        mix(report.retainedInfantry); mix(report.reassignedInfantry);
        mix(report.needsMoveOut); mix(report.droppedReservations);
        for (const auto& action : report.actions) {
            mix(static_cast<unsigned>(action.kind)); mix(action.objectId);
            mix(action.location.x); mix(action.location.y);
            mix(action.location.cell); mix(action.location.onBridge);
            mix(action.previousSubCell); mix(action.subCell);
            for (unsigned char letter : action.reason) mix(letter);
        }
        mix(state.stateHash(objects, terrain));
        // Repeat against already reconciled committed slots and reservations.
        const auto second = state.reconcile(objects, terrain, gates);
        mix(second.retainedInfantry); mix(second.reassignedInfantry);
        mix(second.needsMoveOut); mix(second.droppedReservations);
        mix(state.stateHash(objects, terrain));
    }
    std::cout << "subcell reconcile reservation hash=" << hash
        << " reconcileMs=" << elapsedMs << "\n";
    // Golden captured against the original scalar reconciliation loop.
    ok &= expect(hash == 6004941793930184863ULL,
        "large reconciliation preserves scalar reports, actions and state");
    return ok;
}

bool reconcileSelfAndDuplicateContract() {
    const auto same = at(10, 10, 5130);
    bool ok = true;
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {makeInfantry(1, same, 2)};
        state.reserveForDestination(objects, {}, 1, same);
        const auto report = state.reconcile(objects, {});
        ok &= expect(report.droppedReservations == 0 && state.reservations().size() == 1,
            "own committed slot does not block its retained reservation");
        const auto blocked = state.reconcile(objects, {makeTerrain(same, 1, {2})});
        ok &= expect(blocked.reassignedInfantry == 1 && blocked.droppedReservations == 1,
            "terrain remains blocked when the mover's former own slot changes");
    }
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {makeInfantry(1, same, 2)};
        state.reserveForDestination(objects, {}, 1, at(-1, -1, 12345));
        objects[0].x = -1; objects[0].y = -1; objects[0].cell = 12345;
        const auto report = state.reconcile(objects, {});
        ok &= expect(report.droppedReservations == 0 && state.reservations().size() == 1,
            "cell-only legacy locations retain self-ignored reservations");
    }
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {makeInfantry(1, at(1, 1, 513), 2)};
        state.reserveForDestination(objects, {}, 1, same);
        objects.push_back(makeHardObject(1, WorldObjectKind::Unit, same));
        const auto report = state.reconcile(objects, {});
        ok &= expect(report.droppedReservations == 0 && state.reservations().size() == 1,
            "duplicate ids preserve scalar first-mover validation and all-id exclusion");
    }
    return ok;
}

bool targetedOccupancyContract() {
    const auto shared = at(10, 10, 5130);
    std::vector<WorldObjectInstance> objects;
    for (int id = 1; id <= 192; ++id)
        objects.push_back(makeInfantry(id, at(id % 24, id / 24, id),
            RuntimeSubcellSystemState::activeSubCells()[id % 3]));
    objects.push_back(makeInfantry(500, shared, 2));
    objects.push_back(makeInfantry(501, shared, 4));
    objects.push_back(makeInfantry(502, at(10, 10, 5130, true), 3));
    objects.push_back(makeInfantry(503, at(-1, 10, 5130), 2));
    objects.push_back(makeHardObject(600, WorldObjectKind::Structure, shared));
    objects.push_back(makeHardObject(601, WorldObjectKind::Unit, at(11, 10, 5131)));
    objects.back().transportPassengerIds = {500, 501};
    // Duplicate IDs are intentionally filtered as a group by ignored IDs.
    objects.push_back(makeHardObject(502, WorldObjectKind::Unit, at(12, 10, 5132)));
    objects[9].alive = false; objects[10].health = 0;
    objects[11].airborne = true; objects[12].mission = "GaRrIsOnEd";
    objects[13].garrisonHostObjectId = 600;
    auto large = makeTerrain(at(40, 30, 15400), 1, {2, 99, -1});
    large.footprintWidth = 64; large.footprintHeight = 48;
    auto legacy = makeTerrain(at(-1, 5, 5130), 1, {4});
    legacy.footprintWidth = 7; legacy.footprintHeight = 5;
    auto minimum = makeTerrain(at(12, 12, 6156), -1, {4});
    minimum.footprintWidth = 0; minimum.footprintHeight = -8;
    auto dead = makeTerrain(shared, 7, {2, 4, 3}); dead.alive = false;
    std::vector<WorldTerrainObject> terrain{large, legacy, minimum, dead,
        makeTerrain(at(15, 14, 7183), 0, {2, 4, 3}),
        makeTerrain(at(13, 12, 6157), 1, {3, 3, 19})};
    std::vector<RuntimeSubcellLocation> destinations;
    for (int y : {0, 4, 7, 10, 12, 14, 29, 30, 31, 77, 78})
        for (int x : {0, 5, 10, 11, 12, 13, 15, 39, 40, 41, 103, 104})
            for (bool bridge : {false, true})
                destinations.push_back(at(x, y, y * 512 + x, bridge));
    destinations.push_back(at(-1, -1, 5130));
    destinations.push_back(at(-1, 3, 5130, true));
    destinations.push_back(at(-1, -1, -1));
    RuntimeSubcellSystemState state;
    state.reserveForDestination(objects, {}, 1, at(20, 10, 5140));
    state.reserveForDestination(objects, {}, 2, at(-1, -1, 5130));
    const auto before = state.stateHash(objects, terrain);
    for (const auto& ignored : {std::unordered_set<int>{},
            std::unordered_set<int>{1, 2, 500, 502},
            std::unordered_set<int>{600, 601}}) {
        for (const auto& gates : {RuntimeSubcellContext{},
                RuntimeSubcellContext{{600, 601}}}) {
            // The batch API still builds the complete original snapshot.
            const auto expected = state.availableSubCellsBatch(
                objects, terrain, destinations, ignored, gates);
            for (std::size_t i = 0; i < destinations.size(); ++i)
                if (!expect(state.availableSubCells(objects, terrain,
                        destinations[i], ignored, gates) == expected[i],
                        "targeted occupancy matches full snapshot at case " + std::to_string(i)))
                    return false;
        }
    }
    return expect(before == state.stateHash(objects, terrain),
        "targeted occupancy queries preserve world and reservation state");
}

bool slotPreservingReservationContract() {
    bool ok = true;
    const auto source = at(1, 1, 11);
    const auto target = at(20, 20, 2020);
    const auto allocate = [&](RuntimeSubcellSystemState& state,
        const std::vector<WorldObjectInstance>& objects, int id,
        const RuntimeSubcellLocation& destination, bool batch,
        const std::vector<WorldTerrainObject>& terrain = std::vector<WorldTerrainObject>{}) {
        if (!batch) return state.reserveForDestination(objects, terrain, id, destination);
        const auto results = state.reserveBatch(objects, terrain, {{id, destination}});
        if (!expect(results.size() == 1U, "slot policy singleton result count")) {
            ok = false;
            return RuntimeSubcellReservationResult{};
        }
        return results.front();
    };
    for (bool batch : {false, true}) {
        // These are public allocation/commit outcomes, independent of the selector.
        for (int preferred : {2, 4, 3}) {
            RuntimeSubcellSystemState state;
            std::vector<WorldObjectInstance> objects{makeInfantry(1, source, preferred)};
            const auto result = allocate(state, objects, 1, target, batch);
            const auto committed = state.commit(objects, {}, 1);
            ok &= expect(result.reserved() && result.subCell == preferred
                && committed.committed() && committed.subCell == preferred
                && objects.front().cell == target.cell && objects.front().subCell == preferred
                && state.reservations().empty(), "empty destination retains each source slot through commit");
        }
        // Block each preferred slot with a real resident or another real reservation.
        // Expected fallback is fixed by the public active order, not calculated by production code.
        for (const auto [preferred, expected] : std::vector<std::pair<int, int>>{{2, 4}, {4, 2}, {3, 2}}) {
            for (bool reservedBlocker : {false, true}) {
                RuntimeSubcellSystemState state;
                std::vector<WorldObjectInstance> objects{
                    makeInfantry(1, source, preferred),
                    makeInfantry(2, reservedBlocker ? at(2, 2, 22) : target, preferred)};
                if (reservedBlocker) {
                    const auto blocker = allocate(state, objects, 2, target, batch);
                    ok &= expect(blocker.reserved() && blocker.subCell == preferred,
                        "other mover owns the exact preferred reservation");
                }
                const auto blockerBefore = objects[1];
                const auto result = allocate(state, objects, 1, target, batch);
                const auto committed = state.commit(objects, {}, 1);
                ok &= expect(result.reserved() && result.subCell == expected
                    && committed.committed() && committed.subCell == expected
                    && objects.front().cell == target.cell && objects.front().subCell == expected
                    && objects[1].cell == blockerBefore.cell && objects[1].subCell == blockerBefore.subCell
                    && (!reservedBlocker || state.reservations().at(2).subCell == preferred),
                    "occupied or reserved preferred slot falls back without displacing its owner");
            }
        }
        {
            RuntimeSubcellSystemState state;
            std::vector<WorldObjectInstance> objects{
                makeInfantry(1, source, 4), makeInfantry(2, at(2, 2, 22), 3),
                makeInfantry(3, at(3, 3, 33), 2), makeInfantry(4, at(4, 4, 44), 4)};
            std::vector<RuntimeSubcellReservationResult> results;
            if (batch) results = state.reserveBatch(objects, {}, {{4, target}, {2, target}, {3, target}, {1, target}});
            else for (int id : {1, 2, 3, 4}) results.push_back(allocate(state, objects, id, target, false));
            const std::map<int, int> expected{{1, 4}, {2, 3}, {3, 2}};
            ok &= expect(results.size() == 4U && reservationMap(results) == expected
                && results.back().status == RuntimeSubcellReservationStatus::NoAvailableSubcell,
                "mixed preferred slots fill exactly three independent reservations in mover order");
            for (const auto [id, slot] : expected) {
                const auto committed = state.commit(objects, {}, id);
                ok &= expect(committed.committed() && committed.subCell == slot
                    && objectById(objects, id)->subCell == slot, "mixed preferred slots survive actual commit");
            }
            ok &= expect(state.availableSubCells(objects, {}, target).empty()
                && !state.commit(objects, {}, 4).committed(), "fourth mover never commits a fourth slot");
        }
        for (bool reverse : {false, true}) {
            RuntimeSubcellSystemState state;
            std::vector<WorldObjectInstance> objects{
                makeInfantry(-7, source, 4), makeInfantry(-7, at(2, 2, 22), 3),
                makeInfantry(0, at(3, 3, 33), 3)};
            if (reverse) std::reverse(objects.begin(), objects.end());
            const int expected = reverse ? 3 : 4;
            const auto result = allocate(state, objects, -7, target, batch);
            const auto committed = state.commit(objects, {}, -7);
            const auto zero = allocate(state, objects, 0, at(21, 20, 2021), batch);
            ok &= expect(result.reserved() && result.subCell == expected
                && committed.committed() && objectById(objects, -7)->subCell == expected
                && zero.reserved() && zero.subCell == 3,
                "negative/zero IDs retain the first matching mover's actual preferred slot");
        }
        {
            RuntimeSubcellSystemState state;
            const auto oldDestination = at(21, 21, 2121);
            const std::vector<WorldTerrainObject> terrain{makeTerrain(oldDestination, 1, {2, 3})};
            std::vector<WorldObjectInstance> objects{
                makeInfantry(1, source, 4), makeInfantry(2, at(2, 2, 22), 2),
                makeInfantry(10, target, 2), makeInfantry(11, target, 4), makeInfantry(12, target, 3)};
            const auto old = allocate(state, objects, 1, oldDestination, batch, terrain);
            ok &= expect(old.reserved() && old.subCell == 4, "slot-4 old reservation seed");
            const auto before = state.stateHash(objects, terrain);
            const auto failed = allocate(state, objects, 1, target, batch, terrain);
            const auto blocked = allocate(state, objects, 2, oldDestination, batch, terrain);
            ok &= expect(failed.status == RuntimeSubcellReservationStatus::NoAvailableSubcell
                && before == state.stateHash(objects, terrain)
                && state.reservations().at(1).destination.x == oldDestination.x
                && state.reservations().at(1).destination.y == oldDestination.y
                && state.reservations().at(1).destination.cell == oldDestination.cell
                && state.reservations().at(1).subCell == 4
                && blocked.status == RuntimeSubcellReservationStatus::NoAvailableSubcell,
                "failed reallocation restores the exact old slot-4 reservation before a later arrival");
            ok &= expect(state.release(1), "explicit release removes the retained old reservation");
            const auto released = allocate(state, objects, 2, oldDestination, batch, terrain);
            ok &= expect(released.reserved() && released.subCell == 4,
                "later arrival uses the old slot only after its reservation is released");
        }
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: concord_subcell_system_smoke <fixture.ini>\n";
        return 2;
    }
    IniFile fixture;
    if (!fixture.loadFromFile(argv[1])) {
        std::cerr << "subcell smoke: fixture load failed\n";
        return 3;
    }
    const std::vector<int> fixtureOrder = parseList(
        fixture.getValue("SubcellSmoke", "ActiveOrder")
    );
    const RuntimeSubcellLocation target = at(
        parseInt(fixture.getValue("SubcellSmoke", "TargetX")),
        parseInt(fixture.getValue("SubcellSmoke", "TargetY")),
        parseInt(fixture.getValue("SubcellSmoke", "TargetCell"))
    );

    bool ok = batchQueryContract();
    ok &= slotPreservingReservationContract();
    ok &= movingFormationBatchContract();
    ok &= batchReservationRestoreContract();
    ok &= expect(
        fixtureOrder == std::vector<int>({2, 4, 3})
            && std::vector<int>(
                RuntimeSubcellSystemState::activeSubCells().begin(),
                RuntimeSubcellSystemState::activeSubCells().end()
            ) == fixtureOrder,
        "active subcell order is 2,4,3"
    );

    // Three infantry share a cell; the fourth cannot reserve it.
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(1, target, 2),
            makeInfantry(2, target, 4),
            makeInfantry(3, target, 3),
            makeInfantry(4, at(1, 1, 11), 2)
        };
        ok &= expect(
            state.availableSubCells(objects, {}, target).empty(),
            "three infantry fill one ordinary ground cell"
        );
        const RuntimeSubcellReservationResult fourth =
            state.reserveForDestination(objects, {}, 4, target);
        ok &= expect(
            fourth.status
                == RuntimeSubcellReservationStatus::NoAvailableSubcell,
            "fourth infantry is blocked"
        );
        objects.erase(objects.begin() + 2);
        ok &= expect(
            state.availableSubCells(objects, {}, target)
                == std::vector<int>({3}),
            "vacancy follows fixed active order"
        );
    }

    // Partial terrain only blocks the authored infantry subcells; full
    // occupation remains a whole-cell blocker.
    {
        RuntimeSubcellSystemState state;
        const WorldTerrainObject partial = makeTerrain(target, 1, {2});
        ok &= expect(
            state.availableSubCells({}, {partial}, target)
                == std::vector<int>({4, 3}),
            "partial terrain blocks only subcell 2"
        );
        WorldTerrainObject footprint = partial;
        footprint.footprintWidth = 2;
        const RuntimeSubcellLocation footprintCell = at(
            target.x + 1,
            target.y,
            target.cell + 1
        );
        ok &= expect(
            state.availableSubCells({}, {footprint}, footprintCell)
                == std::vector<int>({4, 3}),
            "terrain footprint expands partial subcell occupation"
        );
        const WorldTerrainObject full = makeTerrain(target, 7, {0, 1, 2, 3, 4});
        ok &= expect(
            state.availableSubCells({}, {full}, target).empty(),
            "full terrain is a whole-cell blocker"
        );
    }

    // Bridge and ground are independent occupancy layers.  Terrain is on the
    // ground layer unless represented by a separate bridge object.
    {
        RuntimeSubcellSystemState state;
        const RuntimeSubcellLocation bridgeTarget = at(
            target.x,
            target.y,
            target.cell,
            true
        );
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(1, target, 2),
            makeInfantry(2, bridgeTarget, 4)
        };
        const WorldTerrainObject partial = makeTerrain(target, 1, {3});
        ok &= expect(
            state.availableSubCells(objects, {partial}, target)
                == std::vector<int>({4}),
            "ground layer combines ground infantry and terrain"
        );
        ok &= expect(
            state.availableSubCells(objects, {partial}, bridgeTarget)
                == std::vector<int>({2, 3}),
            "bridge layer ignores ground infantry and terrain"
        );
    }

    // Vehicles and structures remain whole-cell blockers on their own layer.
    {
        RuntimeSubcellSystemState state;
        const RuntimeSubcellLocation bridgeTarget = at(
            target.x,
            target.y,
            target.cell,
            true
        );
        ok &= expect(
            state.availableSubCells(
                {makeHardObject(20, WorldObjectKind::Unit, target)},
                {},
                target
            ).empty(),
            "vehicle blocks the whole ground cell"
        );
        ok &= expect(
            state.availableSubCells(
                {makeHardObject(21, WorldObjectKind::Structure, target)},
                {},
                target
            ).empty(),
            "structure blocks the whole ground cell"
        );
        ok &= expect(
            state.availableSubCells(
                {makeHardObject(22, WorldObjectKind::Unit, bridgeTarget)},
                {},
                target
            ) == std::vector<int>({2, 4, 3}),
            "bridge vehicle does not block the ground layer"
        );
    }

    // A host-authorized open gate removes only that structure's whole-cell
    // blocker. Infantry still allocate the ordinary 2/4/3 slots, commits are
    // revalidated, and reconcile retains legal occupants and reservations.
    {
        constexpr int gateId = 900;
        RuntimeSubcellContext openGate;
        openGate.ignoredWholeCellObjectIds.insert(gateId);
        WorldObjectInstance gate = makeHardObject(
            gateId,
            WorldObjectKind::Structure,
            target
        );
        gate.type = "GAGATE_A";
        std::vector<WorldObjectInstance> objects = {
            gate,
            makeInfantry(1, at(1, 1, 11), 2),
            makeInfantry(2, at(2, 2, 22), 2),
            makeInfantry(3, at(3, 3, 33), 2),
            makeInfantry(4, at(4, 4, 44), 2)
        };

        RuntimeSubcellSystemState closed;
        const RuntimeSubcellReservationResult closedResult =
            closed.reserveForDestination(objects, {}, 1, target);
        ok &= expect(
            closedResult.status
                == RuntimeSubcellReservationStatus::HardBlocked,
            "closed gate host remains a whole-cell blocker"
        );

        RuntimeSubcellSystemState closedBeforeCommit;
        const RuntimeSubcellReservationResult pendingWhileOpen =
            closedBeforeCommit.reserveForDestination(
                objects,
                {},
                1,
                target,
                openGate
            );
        const RuntimeSubcellCommitResult refusedAfterClose =
            closedBeforeCommit.commit(objects, {}, 1);
        ok &= expect(
            pendingWhileOpen.reserved()
                && refusedAfterClose.status
                    == RuntimeSubcellCommitStatus::DestinationBlocked
                && objectById(objects, 1)->cell == 11,
            "commit revalidates when an open gate closes after reservation"
        );

        RuntimeSubcellSystemState state;
        const auto reserved = state.reserveBatch(
            objects,
            {},
            {{4, target}, {2, target}, {1, target}, {3, target}},
            openGate
        );
        const std::map<int, int> expected = {{1, 2}, {2, 4}, {3, 3}};
        ok &= expect(
            reservationMap(reserved) == expected
                && reserved.size() == 4U
                && reserved.back().moverObjectId == 4
                && reserved.back().status
                    == RuntimeSubcellReservationStatus::NoAvailableSubcell,
            "open gate admits exactly three infantry in stable 2,4,3 order"
        );

        const RuntimeSubcellReconcileReport reservedReport =
            state.reconcile(objects, {}, openGate);
        ok &= expect(
            reservedReport.droppedReservations == 0U
                && state.reservations().size() == 3U,
            "open gate reconcile retains legal destination reservations"
        );
        const bool committed = state.commit(objects, {}, 1, openGate).committed()
            && state.commit(objects, {}, 2, openGate).committed()
            && state.commit(objects, {}, 3, openGate).committed();
        const RuntimeSubcellReconcileReport committedReport =
            state.reconcile(objects, {}, openGate);
        ok &= expect(
            committed
                && objectById(objects, 1)->cell == target.cell
                && objectById(objects, 1)->subCell == 2
                && objectById(objects, 2)->cell == target.cell
                && objectById(objects, 2)->subCell == 4
                && objectById(objects, 3)->cell == target.cell
                && objectById(objects, 3)->subCell == 3
                && committedReport.needsMoveOut == 0U
                && committedReport.retainedInfantry == 4U,
            "open gate commit and reconcile keep three legal infantry slots"
        );
    }

    // Ignoring one gate host never suppresses terrain or an unrelated hard
    // occupant. Vehicle ids are deliberately not eligible for this exception,
    // even if a malformed host context lists one.
    {
        constexpr int gateId = 910;
        constexpr int vehicleId = 912;
        RuntimeSubcellContext openGate;
        openGate.ignoredWholeCellObjectIds = {gateId, vehicleId};
        WorldObjectInstance gate = makeHardObject(
            gateId,
            WorldObjectKind::Structure,
            target
        );
        gate.type = "GAGATE_A";
        const WorldObjectInstance otherStructure = makeHardObject(
            911,
            WorldObjectKind::Structure,
            target
        );
        const WorldObjectInstance vehicle = makeHardObject(
            vehicleId,
            WorldObjectKind::Unit,
            target
        );
        const WorldTerrainObject terrain = makeTerrain(
            target,
            7,
            {0, 1, 2, 3, 4}
        );
        RuntimeSubcellSystemState state;
        ok &= expect(
            state.availableSubCells(
                {gate, otherStructure},
                {},
                target,
                {},
                openGate
            ).empty(),
            "open gate ignore leaves another structure blocking"
        );
        ok &= expect(
            state.availableSubCells(
                {gate, vehicle},
                {},
                target,
                {},
                openGate
            ).empty(),
            "open gate ignore cannot make a vehicle transparent"
        );
        ok &= expect(
            state.availableSubCells(
                {gate},
                {terrain},
                target,
                {},
                openGate
            ).empty(),
            "open gate ignore leaves whole-cell terrain blocking"
        );
    }

    // Inactive/limbo domains never contribute a subcell occupation.
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(1, target, 2),
            makeInfantry(2, target, 4),
            makeInfantry(3, target, 3),
            makeInfantry(4, target, 2),
            makeInfantry(5, target, 4),
            makeHardObject(50, WorldObjectKind::Unit, at(1, 1, 11))
        };
        objectById(objects, 1)->garrisonHostObjectId = 100;
        objectById(objects, 2)->mission = "Limbo";
        objectById(objects, 3)->airborne = true;
        objectById(objects, 4)->alive = false;
        objectById(objects, 4)->health = 0;
        objectById(objects, 5)->mission = "InsideTransport";
        objectById(objects, 50)->transportPassengerIds = {5};
        ok &= expect(
            state.availableSubCells(objects, {}, target)
                == std::vector<int>({2, 4, 3}),
            "garrison, transport, limbo, airborne, and dead objects do not occupy"
        );
    }

    // Batch allocation is stable by mover id, independent of insertion order.
    {
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(30, at(1, 1, 11), 2),
            makeInfantry(10, at(2, 2, 22), 2),
            makeInfantry(20, at(3, 3, 33), 2)
        };
        const std::vector<RuntimeSubcellMoveRequest> forward = {
            {30, target}, {10, target}, {20, target}
        };
        const std::vector<RuntimeSubcellMoveRequest> reverse = {
            {20, target}, {10, target}, {30, target}
        };
        RuntimeSubcellSystemState left;
        RuntimeSubcellSystemState right;
        const auto leftResults = left.reserveBatch(objects, {}, forward);
        const auto rightResults = right.reserveBatch(objects, {}, reverse);
        const std::map<int, int> expected = {{10, 2}, {20, 4}, {30, 3}};
        ok &= expect(
            reservationMap(leftResults) == expected
                && reservationMap(rightResults) == expected
                && left.stateHash(objects, {}) == right.stateHash(objects, {}),
            "batch reservation is insertion-order independent"
        );
        ok &= expect(
            leftResults[0].moverObjectId == 10
                && leftResults[1].moverObjectId == 20
                && leftResults[2].moverObjectId == 30,
            "batch results are returned in stable mover order"
        );
    }

    // Reservation commit and release are explicit and transactional.
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(1, at(1, 1, 11), 2)
        };
        const auto first = state.reserveForDestination(objects, {}, 1, target);
        ok &= expect(first.reserved() && first.subCell == 2, "single reservation");
        ok &= expect(state.release(1) && state.reservations().empty(), "release");
        const auto second = state.reserveForDestination(objects, {}, 1, target);
        const RuntimeSubcellCommitResult committed = state.commit(objects, {}, 1);
        ok &= expect(
            second.reserved()
                && committed.committed()
                && objectById(objects, 1)->x == target.x
                && objectById(objects, 1)->y == target.y
                && objectById(objects, 1)->cell == target.cell
                && objectById(objects, 1)->subCell == 2
                && state.reservations().empty(),
            "commit applies destination and consumes reservation"
        );
    }

    // A reservation is intent, not proof of departure. If the other mover has
    // not committed, its old occupied slot must remain a hard safety gate.
    {
        RuntimeSubcellSystemState state;
        const RuntimeSubcellLocation left = at(1, 1, 11);
        const RuntimeSubcellLocation right = at(2, 2, 22);
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(2, right, 2),
            makeInfantry(1, left, 2)
        };
        const auto reserved = state.reserveBatch(
            objects,
            {},
            {{2, left}, {1, right}}
        );
        const RuntimeSubcellCommitResult first = state.commit(objects, {}, 1);
        const RuntimeSubcellCommitResult second = state.commit(objects, {}, 2);
        ok &= expect(
            reservationMap(reserved) == std::map<int, int>({{1, 2}, {2, 2}})
                && !first.committed()
                && !second.committed()
                && objectById(objects, 1)->cell == left.cell
                && objectById(objects, 2)->cell == right.cell,
            "uncommitted reserved occupants still block their old subcells"
        );
    }

    // A full cell with two departures admits exactly two of three competing
    // arrivals.  The remaining resident is never displaced or overbooked.
    {
        RuntimeSubcellSystemState state;
        const RuntimeSubcellLocation full = at(5, 5, 55);
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(30, full, 3),
            makeInfantry(20, full, 4),
            makeInfantry(10, full, 2),
            makeInfantry(3, at(8, 3, 83), 2),
            makeInfantry(2, at(8, 2, 82), 2),
            makeInfantry(1, at(8, 1, 81), 2)
        };
        const auto reserved = state.reserveBatch(
            objects,
            {},
            {
                {20, at(6, 2, 62)},
                {3, full},
                {10, at(6, 1, 61)},
                {2, full},
                {1, full}
            }
        );
        const std::map<int, int> allocated = reservationMap(reserved);
        const bool earlyArrivalsBlocked =
            !state.commit(objects, {}, 1).committed()
            && !state.commit(objects, {}, 2).committed();
        const bool committedDepartures =
            state.commit(objects, {}, 10).committed()
            && state.commit(objects, {}, 20).committed();
        const bool laterArrivalsCommitted =
            state.commit(objects, {}, 1).committed()
            && state.commit(objects, {}, 2).committed();
        const RuntimeSubcellCommitResult rejectedArrival =
            state.commit(objects, {}, 3);
        ok &= expect(
            allocated.find(1) != allocated.end()
                && allocated.find(2) != allocated.end()
                && allocated.find(3) == allocated.end()
                && earlyArrivalsBlocked
                && !rejectedArrival.committed()
                && committedDepartures
                && laterArrivalsCommitted
                && objectById(objects, 1)->cell == full.cell
                && objectById(objects, 2)->cell == full.cell
                && objectById(objects, 3)->cell == 83
                && objectById(objects, 30)->cell == full.cell,
            "departures unblock exactly two later arrivals without overbooking"
        );
    }

    // Reconcile keeps every existing legal unique occupation, repairs invalid
    // and duplicate records by id, and reports a fourth occupant for removal.
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(50, target, 2),
            makeInfantry(30, target, 4),
            makeInfantry(40, target, 3),
            makeInfantry(10, target, 2)
        };
        const RuntimeSubcellReconcileReport report = state.reconcile(objects, {});
        ok &= expect(
            objectById(objects, 10)->subCell == 2
                && objectById(objects, 30)->subCell == 4
                && objectById(objects, 40)->subCell == 3
                && objectById(objects, 50)->subCell == -1
                && report.retainedInfantry == 3U
                && report.needsMoveOut == 1U
                && hasAction(
                    report,
                    RuntimeSubcellReconcileActionKind::NeedsMoveOut,
                    50
                ),
            "duplicate repair retains low id and never displaces legal occupants"
        );
    }
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(9, target, 99),
            makeInfantry(20, target, 4)
        };
        const RuntimeSubcellReconcileReport report = state.reconcile(objects, {});
        ok &= expect(
            objectById(objects, 9)->subCell == 2
                && objectById(objects, 20)->subCell == 4
                && report.reassignedInfantry == 1U,
            "invalid legacy subcell migrates to first free slot"
        );
    }
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(1, target, 2),
            makeInfantry(2, target, 4)
        };
        const WorldTerrainObject partial = makeTerrain(target, 1, {2});
        state.reconcile(objects, {partial});
        ok &= expect(
            objectById(objects, 1)->subCell == 3
                && objectById(objects, 2)->subCell == 4,
            "terrain-conflicting infantry moves without stealing legal subcell"
        );
    }
    {
        std::vector<WorldObjectInstance> firstOrder = {
            makeInfantry(20, target, 2),
            makeInfantry(10, target, 2),
            makeInfantry(30, target, 4)
        };
        std::vector<WorldObjectInstance> secondOrder = {
            makeInfantry(30, target, 4),
            makeInfantry(10, target, 2),
            makeInfantry(20, target, 2)
        };
        RuntimeSubcellSystemState left;
        RuntimeSubcellSystemState right;
        left.reconcile(firstOrder, {});
        right.reconcile(secondOrder, {});
        ok &= expect(
            objectById(firstOrder, 10)->subCell == 2
                && objectById(firstOrder, 20)->subCell == 3
                && objectById(firstOrder, 30)->subCell == 4
                && objectById(secondOrder, 10)->subCell == 2
                && objectById(secondOrder, 20)->subCell == 3
                && objectById(secondOrder, 30)->subCell == 4
                && left.stateHash(firstOrder, {})
                    == right.stateHash(secondOrder, {}),
            "duplicate reconcile is object-insertion-order independent"
        );
    }
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(1, target, 2),
            makeHardObject(2, WorldObjectKind::Unit, target)
        };
        const RuntimeSubcellReconcileReport report = state.reconcile(objects, {});
        ok &= expect(
            objectById(objects, 1)->subCell == -1
                && report.needsMoveOut == 1U,
            "hard blocker reconciliation requests infantry removal"
        );
    }

    // A reservation that becomes blocked is filtered by reconcile, and hash
    // changes when committed subcell state changes.
    {
        RuntimeSubcellSystemState state;
        std::vector<WorldObjectInstance> objects = {
            makeInfantry(1, at(1, 1, 11), 2)
        };
        state.reserveForDestination(objects, {}, 1, target);
        const std::uint64_t reservedHash = state.stateHash(objects, {});
        objects.push_back(makeHardObject(2, WorldObjectKind::Structure, target));
        const RuntimeSubcellReconcileReport report = state.reconcile(objects, {});
        ok &= expect(
            state.reservations().empty()
                && report.droppedReservations == 1U,
            "reconcile drops a newly blocked reservation"
        );
        objects.pop_back();
        objectById(objects, 1)->subCell = 4;
        ok &= expect(
            reservedHash != state.stateHash(objects, {}),
            "independent hash covers reservations and committed subcells"
        );
    }

    ok &= reconcileReservationScaleContract();
    ok &= reconcileSelfAndDuplicateContract();
    ok &= targetedOccupancyContract();
    return ok ? 0 : 1;
}
