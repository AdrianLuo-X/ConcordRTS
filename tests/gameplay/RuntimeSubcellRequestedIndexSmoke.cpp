#include "simulation/navigation/occupancy/RuntimeSubcellSystem.h"
#include "world/queries/RuntimeHistoryQuery.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <tuple>
#include <vector>

namespace {

RuntimeSubcellLocation at(int x, int y, bool bridge = false) {
    return {x, y, x >= 0 && y >= 0 ? y * 1000 + x : -1, bridge};
}

WorldObjectInstance object(int id, WorldObjectKind kind, int x, int y) {
    WorldObjectInstance result;
    result.instanceId = id; result.kind = kind; result.type = kind == WorldObjectKind::Infantry ? "E1" : "MTNK";
    result.owner = "Player"; result.alive = true; result.health = 100;
    result.x = x; result.y = y; result.cell = y * 1000 + x;
    result.subCell = 2; result.mission = "Guard";
    return result;
}

auto resultFields(const RuntimeSubcellReservationResult& result) {
    return std::tie(result.status, result.moverObjectId, result.destination.x, result.destination.y,
        result.destination.cell, result.destination.onBridge, result.subCell);
}

auto objectFields(const WorldObjectInstance& value) {
    return std::tie(value.instanceId, value.kind, value.type, value.owner, value.alive, value.health,
        value.x, value.y, value.cell, value.subCell, value.onBridge, value.airborne,
        value.garrisonHostObjectId, value.mission, value.transportPassengerIds);
}

WorldObjectInstance* first(std::vector<WorldObjectInstance>& objects, int id) {
    const auto found = std::find_if(objects.begin(), objects.end(), [=](const auto& item) {
        return item.instanceId == id;
    });
    return found == objects.end() ? nullptr : &*found;
}

struct Fixture {
    std::vector<WorldObjectInstance> objects;
    std::vector<WorldTerrainObject> terrain;
    RuntimeSubcellContext context;
};

Fixture fixture(int history, int order) {
    Fixture value;
    // Prefix history exercises the long scan without allocating an index node
    // for every dead record; suffix history exercises finding every mover early.
    for (int index = 0; index < history; ++index) {
        auto dead = object(100000 + index, WorldObjectKind::Unit, index % 100, index / 100);
        dead.alive = false; dead.health = 0;
        value.objects.push_back(std::move(dead));
    }
    auto deadFirst = object(3, WorldObjectKind::Infantry, 3, 0);
    deadFirst.alive = false; deadFirst.health = 0;
    value.objects.push_back(deadFirst);
    value.objects.push_back(object(6, WorldObjectKind::Unit, 6, 0));
    for (int id = 1; id <= 16; ++id) value.objects.push_back(object(id, WorldObjectKind::Infantry, id, 0));
    first(value.objects, 4)->health = 0;
    first(value.objects, 5)->airborne = true;
    first(value.objects, 8)->mission = "LiMbO";
    first(value.objects, 9)->garrisonHostObjectId = 999;
    first(value.objects, 10)->mission = "InsideTransport";
    first(value.objects, 11)->alive = false;
    first(value.objects, 12)->kind = WorldObjectKind::Unit;
    first(value.objects, 13)->subCell = 99;
    for (int id : {-7, 0, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()})
        value.objects.push_back(object(id, WorldObjectKind::Infantry, 7, 3));
    auto transport = object(999, WorldObjectKind::Unit, 20, 20);
    transport.alive = false; transport.health = 0; transport.transportPassengerIds = {7};
    value.objects.push_back(std::move(transport));
    value.objects.push_back(object(800, WorldObjectKind::Structure, 13, 10));
    value.objects.push_back(object(801, WorldObjectKind::Unit, 14, 10));
    value.context.ignoredWholeCellObjectIds = {800, 801}; // only a structure is transparent
    for (int id = 802; id <= 806; ++id) {
        auto resident = object(id, WorldObjectKind::Infantry, id < 804 ? 15 : 16, 10);
        resident.subCell = RuntimeSubcellSystemState::activeSubCells()[(id - 802) % 3];
        value.objects.push_back(resident);
    }
    auto bridgeResident = object(807, WorldObjectKind::Infantry, 16, 10);
    bridgeResident.onBridge = true;
    value.objects.push_back(bridgeResident);
    const auto terrain = [&](int x, int y, int bits, std::vector<int> cells, int width = 1) {
        WorldTerrainObject item; item.x = x; item.y = y; item.cell = y * 1000 + x;
        item.alive = true; item.health = 100; item.occupationBits = bits;
        item.occupiedSubCells = std::move(cells); item.footprintWidth = width;
        value.terrain.push_back(std::move(item));
    };
    terrain(10, 10, 1, {4, 3});
    terrain(12, 10, 7, {2, 4, 3});
    terrain(18, 10, 1, {2}, 2);
    if (order == 1) std::reverse(value.objects.begin(), value.objects.end());
    if (order == 2) std::rotate(value.objects.begin(), value.objects.begin() + value.objects.size() / 2,
        value.objects.end());
    return value;
}

bool equalReservations(const RuntimeSubcellSystemState& a, const RuntimeSubcellSystemState& b) {
    if (a.reservations().size() != b.reservations().size()) return false;
    for (const auto& [id, left] : a.reservations()) {
        const auto found = b.reservations().find(id);
        if (found == b.reservations().end()) return false;
        const auto& right = found->second;
        if (std::tie(left.moverObjectId, left.destination.x, left.destination.y, left.destination.cell,
                left.destination.onBridge, left.subCell)
            != std::tie(right.moverObjectId, right.destination.x, right.destination.y, right.destination.cell,
                right.destination.onBridge, right.subCell)) return false;
    }
    return true;
}

auto reconcileActionFields(const RuntimeSubcellReconcileAction& action) {
    return std::tie(action.kind, action.objectId, action.location.x, action.location.y,
        action.location.cell, action.location.onBridge, action.previousSubCell, action.subCell, action.reason);
}

bool equalReconcileReports(const RuntimeSubcellReconcileReport& a, const RuntimeSubcellReconcileReport& b) {
    if (std::tie(a.retainedInfantry, a.reassignedInfantry, a.needsMoveOut, a.droppedReservations)
        != std::tie(b.retainedInfantry, b.reassignedInfantry, b.needsMoveOut, b.droppedReservations)
        || a.actions.size() != b.actions.size()) return false;
    for (std::size_t index = 0; index < a.actions.size(); ++index)
        if (reconcileActionFields(a.actions[index]) != reconcileActionFields(b.actions[index])) return false;
    return true;
}

bool runReconcile(std::string& error) {
    const auto fail = [&](const std::string& why) { error = why; return false; };
    std::size_t reconciliations = 0;
    for (int history : {0, 1000, 10000}) for (int order = 0; order < 4; ++order) {
        std::vector<WorldObjectInstance> baseline;
        for (int index = 0; index < history; ++index) {
            auto dead = object(100000 + index, WorldObjectKind::Unit, 90, 90);
            dead.alive = false; dead.health = 0; baseline.push_back(std::move(dead));
        }
        const std::vector<int> movers{-7, 0, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20000, -500};
        for (std::size_t index = 0; index < movers.size(); ++index)
            baseline.push_back(object(movers[index], WorldObjectKind::Infantry, 20 + static_cast<int>(index), 1));
        auto carrier = object(999, WorldObjectKind::Unit, 90, 1);
        carrier.alive = false; carrier.health = 0; baseline.push_back(std::move(carrier));
        baseline.push_back(object(50000, WorldObjectKind::Unit, 60, 60));
        // Full terrain occupation is a whole-cell HardBlocked result. A
        // partial terrain slot plus two residents produces NoAvailableSubcell
        // and exercises that distinct failure's restoration of the old slot.
        for (int slot : {4, 3}) {
            auto resident = object(60010 + slot, WorldObjectKind::Infantry, 61, 60);
            resident.subCell = slot; baseline.push_back(std::move(resident));
        }
        if (order == 1) std::reverse(baseline.begin(), baseline.end());
        if (order == 2) std::rotate(baseline.begin(), baseline.begin() + baseline.size() / 2, baseline.end());
        if (order == 3) std::sort(baseline.begin(), baseline.end(), [](const auto& a, const auto& b) {
            return a.instanceId < b.instanceId;
        });
        WorldTerrainObject blocked; blocked.x = 61; blocked.y = 60; blocked.cell = 60061;
        blocked.alive = true; blocked.health = 100; blocked.occupationBits = 1;
        blocked.occupiedSubCells = {2};
        const std::vector<WorldTerrainObject> baseTerrain{blocked};
        RuntimeSubcellSystemState seeded;
        for (std::size_t index = 0; index < movers.size(); ++index)
            if (!seeded.reserveForDestination(baseline, baseTerrain, movers[index],
                    at(20 + static_cast<int>(index), 30, movers[index] == 15)).reserved())
                return fail("reconcile fixture could not seed all reservations");
        for (int phase = 0; phase < 12; ++phase) {
            auto objects = baseline;
            auto terrain = baseTerrain;
            auto left = seeded, right = seeded;
            const auto originalTwo = seeded.reservations().at(2).destination;
            const auto failedOld = [&] { RuntimeHistoryQueryReferenceScope ref(true);
                return left.reserveBatch(objects, terrain, {{2, at(60, 60)}, {14, at(61, 60)}}); }();
            const auto failedNew = [&] { RuntimeHistoryQueryReferenceScope opt(false);
                return right.reserveBatch(objects, terrain, {{2, at(60, 60)}, {14, at(61, 60)}}); }();
            if (failedOld.size() != 2 || failedNew.size() != 2
                || resultFields(failedOld[0]) != resultFields(failedNew[0])
                || resultFields(failedOld[1]) != resultFields(failedNew[1])
                || failedNew[0].status != RuntimeSubcellReservationStatus::HardBlocked
                || failedNew[1].status != RuntimeSubcellReservationStatus::NoAvailableSubcell
                || !equalReservations(left, right)
                || !equalReservations(seeded, left) || !equalReservations(seeded, right)
                || right.reservations().at(2).destination.x != originalTwo.x)
                return fail("reconcile fixture lost failed reservation restoration history=" + std::to_string(history)
                    + " order=" + std::to_string(order) + " phase=" + std::to_string(phase)
                    + " statuses=" + std::to_string(failedNew.empty() ? -1 : static_cast<int>(failedNew[0].status))
                    + "," + std::to_string(failedNew.size() < 2 ? -1 : static_cast<int>(failedNew[1].status)));
            if (phase == 1) {
                first(objects, 3)->alive = false; first(objects, 4)->kind = WorldObjectKind::Unit;
                first(objects, 5)->health = 0; first(objects, 6)->airborne = true;
                first(objects, 7)->mission = "iNsIdEtRaNsPoRt"; first(objects, 8)->garrisonHostObjectId = 999;
                first(objects, 999)->transportPassengerIds = {9}; // dead carrier manifests still count
                auto* invalid = first(objects, 10); invalid->x = invalid->y = invalid->cell = -1;
            }
            if (phase == 2) std::erase_if(objects, [](const auto& value) {
                return value.instanceId == 20000 || value.instanceId == -500;
            });
            if (phase == 3) {
                const auto destination = left.reservations().at(1).destination;
                objects.push_back(object(60000, WorldObjectKind::Infantry, destination.x, destination.y));
                auto partial = blocked; partial.x = originalTwo.x; partial.y = originalTwo.y;
                partial.cell = originalTwo.cell; partial.occupationBits = 1; partial.occupiedSubCells = {2};
                terrain.push_back(std::move(partial));
            }
            if (phase == 4) {
                auto deadFirst = *first(objects, 3); deadFirst.alive = false; deadFirst.health = 0;
                objects.insert(objects.begin(), std::move(deadFirst));
            }
            if (phase == 5) {
                auto unrelated = object(7777, WorldObjectKind::Unit, 90, 90);
                unrelated.alive = false; unrelated.health = 0;
                objects.push_back(unrelated); objects.push_back(std::move(unrelated));
            }
            if (phase == 6) first(objects, 3)->alive = false;
            if (phase == 7) {
                std::reverse(objects.begin(), objects.end());
                objects.push_back(object(60001, WorldObjectKind::Infantry, 70, 70));
            }
            if (phase == 8) objects.clear();
            if (phase == 9) {
                first(objects, 15)->onBridge = true;
                const auto destination = left.reservations().at(15).destination;
                auto resident = object(60002, WorldObjectKind::Infantry, destination.x, destination.y);
                resident.onBridge = true; objects.push_back(std::move(resident));
            }
            if (phase == 10) std::erase_if(objects, [](const auto& value) {
                return value.instanceId == std::numeric_limits<int>::min();
            });
            if (phase == 11) { left.clearReservations(); right.clearReservations(); }
            auto oldObjects = objects, newObjects = objects;
            const std::string context = "reconcile history=" + std::to_string(history)
                + " order=" + std::to_string(order) + " phase=" + std::to_string(phase);
            for (int pass = 0; pass < 2; ++pass) {
                const auto a = [&] { RuntimeHistoryQueryReferenceScope ref(true);
                    return left.reconcile(oldObjects, terrain); }();
                const auto b = [&] { RuntimeHistoryQueryReferenceScope opt(false);
                    return right.reconcile(newObjects, terrain); }();
                if (!equalReconcileReports(a, b) || !equalReservations(left, right)
                    || left.stateHash(oldObjects, terrain) != right.stateHash(newObjects, terrain))
                    return fail(context + " complete report/reservation/hash");
                for (std::size_t index = 0; index < oldObjects.size(); ++index)
                    if (objectFields(oldObjects[index]) != objectFields(newObjects[index]))
                        return fail(context + " committed world object fields");
                if (pass == 0 && ((phase == 0 && b.droppedReservations != 0)
                    || (phase == 1 && b.droppedReservations != 8)
                    || (phase == 3 && (right.reservations().contains(1) || right.reservations().contains(2)))
                    || ((phase == 4 || phase == 6) && right.reservations().contains(3))
                    || (phase == 8 && (b.droppedReservations != movers.size() || !right.reservations().empty()))))
                    return fail(context + " original cleanup outcome");
                if (pass == 1 && b.droppedReservations != 0) return fail(context + " repeated failed cleanup");
                if (phase == 6 && pass == 0) {
                    first(oldObjects, 3)->alive = true; first(newObjects, 3)->alive = true;
                }
                if (phase == 6 && pass == 1 && right.reservations().contains(3))
                    return fail(context + " revival resurrected dropped reservation");
                ++reconciliations;
            }
        }
    }
    std::cout << "Subcell reservation reconcile index: exactReports=" << reconciliations
        << " history=0/1000/10000 ordered/reverse/rotate/strictIncreasing duplicateAny/deadFirst/missing/extreme/cargo/failedCleanup/revive=yes\n";
    return true;
}

bool run(std::string& error) {
    const auto fail = [&](const std::string& why) { error = why; return false; };
    std::size_t batches = 0, resultsCompared = 0, commitsCompared = 0;
    for (int history : {0, 1000, 10000}) for (int order = 0; order < 3; ++order) {
        auto data = fixture(history, order);
        for (const auto destination : {at(15, 10), at(16, 10), at(13, 10), at(14, 10), at(16, 10, true)}) {
            RuntimeSubcellSystemState oldProbe, newProbe;
            const auto a = [&] { RuntimeHistoryQueryReferenceScope ref(true);
                return oldProbe.reserveBatch(data.objects, data.terrain, {{16, destination}}, data.context); }();
            const auto b = [&] { RuntimeHistoryQueryReferenceScope opt(false);
                return newProbe.reserveBatch(data.objects, data.terrain, {{16, destination}}, data.context); }();
            if (a.size() != 1 || b.size() != 1 || resultFields(a.front()) != resultFields(b.front())
                || oldProbe.stateHash(data.objects, data.terrain) != newProbe.stateHash(data.objects, data.terrain))
                return fail("resident/structure/vehicle/bridge qualification equivalence");
            if ((destination.x == 15 && (!b.front().reserved() || b.front().subCell != 3))
                || (destination.x == 16 && !destination.onBridge
                    && b.front().status != RuntimeSubcellReservationStatus::NoAvailableSubcell)
                || (destination.x == 13 && !b.front().reserved())
                || (destination.x == 14 && b.front().status != RuntimeSubcellReservationStatus::HardBlocked)
                || (destination.x == 16 && destination.onBridge && (!b.front().reserved() || b.front().subCell != 4)))
                return fail("resident capacity/authorized structure/vehicle/bridge original outcome");
            ++batches; ++resultsCompared;
        }
        RuntimeSubcellSystemState left, right;
        if (!left.reserveForDestination(data.objects, data.terrain, 2, at(10, 10), data.context).reserved()
            || !left.reserveForDestination(data.objects, data.terrain, 14, at(11, 10), data.context).reserved()
            || !left.reserveForDestination(data.objects, data.terrain, 15, at(10, 10, true), data.context).reserved())
            return fail("fixture could not prime old and bridge reservations");
        right = left;
        std::mt19937 random(0x51429U + static_cast<unsigned int>(history + order));
        for (int round = 0; round < 7; ++round) {
            std::vector<RuntimeSubcellMoveRequest> requests;
            if (round == 0) {
                // Failure restores the old slot before later arrivals. This
                // known outcome is independent of either index implementation.
                requests = {{16, at(11, 10)}, {14, at(10, 10)}, {2, at(12, 10)}};
            } else if (round == 1) {
                for (int id = 1; id <= 16; ++id) requests.push_back({id, at(24 + id, 10)});
                requests.push_back({-7, at(20, 11)}); requests.push_back({0, at(21, 11)});
                requests.push_back({std::numeric_limits<int>::min(), at(22, 11)});
                requests.push_back({std::numeric_limits<int>::max(), at(23, 11)});
                requests.push_back({20000, at(24, 11)}); requests.push_back({-500, at(25, 11)});
            } else {
                for (int id : {-7, 0, 1, 2, 3, 6, 7, 13, 14, 15, 16, 20000, -500}) {
                    const int lane = static_cast<int>(random() % 10U);
                    auto goal = at(10 + lane, 10, random() % 4U == 0);
                    if (round == 2 && id == 13) goal = {-1, -1, 500, true};
                    if (round == 3 && id == 14) goal = {-1, -1, -1, false};
                    requests.push_back({id, goal});
                    if (id == 1 || (round == 4 && id == -500)) requests.push_back({id, at(21, 10)});
                }
                if (round == 6) requests.clear();
            }
            std::shuffle(requests.begin(), requests.end(), random);
            const auto before = RuntimeSubcellSystemState{}.stateHash(data.objects, data.terrain);
            const auto oldResults = [&] {
                RuntimeHistoryQueryReferenceScope reference(true);
                return left.reserveBatch(data.objects, data.terrain, requests, data.context);
            }();
            const auto newResults = [&] {
                RuntimeHistoryQueryReferenceScope optimized(false);
                return right.reserveBatch(data.objects, data.terrain, requests, data.context);
            }();
            const std::string context = "history=" + std::to_string(history) + " order=" + std::to_string(order)
                + " round=" + std::to_string(round);
            if (oldResults.size() != newResults.size()) return fail(context + " result count");
            for (std::size_t index = 0; index < oldResults.size(); ++index) {
                if (resultFields(oldResults[index]) != resultFields(newResults[index]))
                    return fail(context + " result at " + std::to_string(index));
                ++resultsCompared;
            }
            if (!equalReservations(left, right) || left.stateHash(data.objects, data.terrain)
                    != right.stateHash(data.objects, data.terrain)
                || before != RuntimeSubcellSystemState{}.stateHash(data.objects, data.terrain))
                return fail(context + " reservation/world occupancy hash");
            if (round == 0 && (newResults.size() != 3 || newResults[0].status != RuntimeSubcellReservationStatus::HardBlocked
                || newResults[1].status != RuntimeSubcellReservationStatus::NoAvailableSubcell
                || !newResults[2].reserved() || newResults[2].subCell != 4
                || right.reservations().at(2).destination.x != 10 || right.reservations().at(14).destination.x != 11
                || !right.reservations().at(15).destination.onBridge)) return fail(context + " original failed-reservation order");
            if (round == 1 && order == 0) {
                const auto status = [&](int id) {
                    const auto found = std::find_if(newResults.begin(), newResults.end(), [=](const auto& item) {
                        return item.moverObjectId == id;
                    });
                    return found == newResults.end() ? RuntimeSubcellReservationStatus::HardBlocked : found->status;
                };
                if (status(3) != RuntimeSubcellReservationStatus::InactiveMover
                    || status(6) != RuntimeSubcellReservationStatus::MoverNotInfantry
                    || status(7) != RuntimeSubcellReservationStatus::InactiveMover
                    || status(20000) != RuntimeSubcellReservationStatus::MissingMover
                    || status(-500) != RuntimeSubcellReservationStatus::MissingMover
                    || status(-7) != RuntimeSubcellReservationStatus::Reserved
                    || status(0) != RuntimeSubcellReservationStatus::Reserved
                    || status(std::numeric_limits<int>::min()) != RuntimeSubcellReservationStatus::Reserved
                    || status(std::numeric_limits<int>::max()) != RuntimeSubcellReservationStatus::Reserved)
                    return fail(context + " first dead/kind/transport/negative/missing/extreme ID semantics");
            }
            // Commit and release remain the original scalar operations; their
            // outcomes prove that resulting reservations stay interchangeable.
            auto oldObjects = data.objects, newObjects = data.objects;
            for (int id : {-7, 0, 2, 13, 16, 20000}) {
                const auto a = left.commit(oldObjects, data.terrain, id, data.context);
                const auto b = right.commit(newObjects, data.terrain, id, data.context);
                if (std::tie(a.status, a.moverObjectId, a.subCell) != std::tie(b.status, b.moverObjectId, b.subCell))
                    return fail(context + " commit result");
                ++commitsCompared;
            }
            for (std::size_t index = 0; index < oldObjects.size(); ++index)
                if (objectFields(oldObjects[index]) != objectFields(newObjects[index])) return fail(context + " committed object fields");
            if (!equalReservations(left, right) || left.stateHash(oldObjects, data.terrain) != right.stateHash(newObjects, data.terrain))
                return fail(context + " committed occupancy hash");
            if (left.release(14) != right.release(14)) return fail(context + " release result");
            data.objects = std::move(newObjects);
            if (round == 1) {
                auto* dead = first(data.objects, 3); dead->alive = true; dead->health = 100;
                first(data.objects, 6)->kind = WorldObjectKind::Infantry;
                auto replacement = object(20000, WorldObjectKind::Infantry, 5, 5);
                data.objects.push_back(std::move(replacement));
            }
            if (round == 2) first(data.objects, 999)->transportPassengerIds.clear();
            if (round == 3) {
                first(data.objects, 15)->alive = false;
                std::reverse(data.objects.begin(), data.objects.end());
                data.context.ignoredWholeCellObjectIds.clear();
            }
            ++batches;
        }
    }
    // Single requested mover, all found before long suffix history, no resident
    // filtering shortcut and no need to index unrelated object records.
    for (int history : {0, 10000}) {
        std::vector<WorldObjectInstance> objects{object(1, WorldObjectKind::Infantry, 1, 1)};
        for (int i = 0; i < history; ++i) {
            auto dead = object(1000 + i, WorldObjectKind::Unit, 2, 2); dead.alive = false;
            objects.push_back(std::move(dead));
        }
        RuntimeSubcellSystemState left, right;
        const auto a = [&] { RuntimeHistoryQueryReferenceScope ref(true); return left.reserveBatch(objects, {}, {{1, at(3, 3)}}); }();
        const auto b = [&] { RuntimeHistoryQueryReferenceScope opt(false); return right.reserveBatch(objects, {}, {{1, at(3, 3)}}); }();
        if (a.size() != 1 || b.size() != 1 || resultFields(a.front()) != resultFields(b.front())
            || !b.front().reserved() || left.stateHash(objects, {}) != right.stateHash(objects, {}))
            return fail("early-found mover and suffix history");
        ++batches; ++resultsCompared;
    }
    std::cout << "Subcell requested-only object index: batches=" << batches << " results=" << resultsCompared
        << " commits=" << commitsCompared
        << " exactReservationHash/firstDeadKind/duplicateNegativeMissing/extremeIDs/transport/resident/restore/reorder=yes\n";
    return true;
}

bool runReadOnlyScope(std::string& error) {
    const auto fail = [&](const std::string& why) { error = "readonly scope: " + why; return false; };
    std::size_t queriesCompared = 0, reservationsCompared = 0;
    for (int history : {0, 1000, 10000}) for (int order = 0; order < 4; ++order)
        for (int phase = 0; phase < 7; ++phase) {
        auto data = fixture(history, order == 3 ? 0 : order);
        auto legacy = object(901, WorldObjectKind::Infantry, 0, 0);
        legacy.x = legacy.y = -1; legacy.cell = 500;
        data.objects.push_back(legacy);
        legacy.instanceId = 902; legacy.y = 2; legacy.cell = 501; legacy.subCell = 4;
        data.objects.push_back(legacy);
        legacy.instanceId = 903; legacy.x = legacy.y = legacy.cell = -1;
        data.objects.push_back(legacy);
        auto extreme = object(904, WorldObjectKind::Infantry, 0, 0);
        extreme.x = extreme.y = std::numeric_limits<int>::max(); extreme.cell = -1;
        data.objects.push_back(extreme);
        extreme.instanceId = 905; extreme.y = 1; extreme.subCell = 3;
        data.objects.push_back(extreme);
        auto coordinate = object(906, WorldObjectKind::Infantry, 0, 0);
        coordinate.cell = std::numeric_limits<int>::max(); coordinate.subCell = 4;
        data.objects.push_back(coordinate);
        coordinate.instanceId = 907; coordinate.onBridge = true; coordinate.subCell = 3;
        data.objects.push_back(coordinate);
        auto submerged = object(908, WorldObjectKind::Infantry, 40, 40);
        submerged.submerged = true; submerged.subCell = 4;
        data.objects.push_back(submerged); // original ground predicate includes this record
        data.objects.push_back(object(9000, WorldObjectKind::Infantry, 0, 50));
        auto duplicate = object(800, WorldObjectKind::Infantry, 13, 10);
        duplicate.subCell = 4; data.objects.push_back(duplicate);
        WorldTerrainObject legacyTerrain;
        legacyTerrain.x = legacyTerrain.y = -1; legacyTerrain.cell = 600;
        legacyTerrain.alive = true; legacyTerrain.health = 100;
        legacyTerrain.occupationBits = 1; legacyTerrain.occupiedSubCells = {3};
        legacyTerrain.footprintWidth = 4; legacyTerrain.footprintHeight = 3;
        data.terrain.push_back(legacyTerrain);
        if (phase == 1) {
            first(data.objects, 3)->alive = true; first(data.objects, 3)->health = 100;
            first(data.objects, 4)->health = 100; first(data.objects, 5)->airborne = false;
        } else if (phase == 2) {
            first(data.objects, 999)->transportPassengerIds.clear();
            first(data.objects, 8)->mission = "Guard";
            first(data.objects, 9)->garrisonHostObjectId = -1;
            first(data.objects, 10)->mission = "Guard";
        } else if (phase == 3) {
            first(data.objects, 802)->subCell = 99;
            first(data.objects, 807)->onBridge = false;
            data.context.ignoredWholeCellObjectIds.clear();
        } else if (phase == 4) {
            first(data.objects, 804)->alive = false;
            first(data.objects, 805)->health = 0;
            first(data.objects, 806)->mission = "OcCuPiEd";
            first(data.objects, 908)->airborne = true;
        } else if (phase == 5) {
            first(data.objects, 901)->subCell = 3;
            first(data.objects, 902)->onBridge = true;
            auto sameId = object(901, WorldObjectKind::Infantry, 15, 10);
            sameId.subCell = 2; data.objects.push_back(std::move(sameId));
            std::reverse(data.objects.begin(), data.objects.end());
        }
        if (order == 3) std::sort(data.objects.begin(), data.objects.end(), [](const auto& a, const auto& b) {
            return a.instanceId < b.instanceId;
        });
        const std::string detail = "history=" + std::to_string(history) + " order=" + std::to_string(order)
            + " phase=" + std::to_string(phase);
        RuntimeSubcellSystemState left, right;
        const auto seeded = left.reserveForDestination(data.objects, data.terrain, 9000, at(41, 41));
        if (!seeded.reserved()) return fail(detail + " seed reservation");
        right = left;
        const auto initialHash = left.stateHash(data.objects, data.terrain);
        RuntimeHistoryQueryReferenceScope optimized(false);
        RuntimeSubcellReadOnlyQueryScope scope(data.objects);
        if (!scope.accepts(data.objects)
            || !RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, at(15, 10), {}, {}))
            return fail(detail + " production scope did not activate");
        const std::vector<RuntimeSubcellLocation> destinations{
            at(15, 10), at(16, 10), at(13, 10), at(14, 10), at(16, 10, true),
            at(10, 10), at(12, 10), at(18, 10), at(19, 10), at(7, 3), at(7, 0), at(3, 0),
            {-1, -1, 500, false}, {-1, 2, 501, false}, {-1, 9, 501, false},
            {-1, -1, 500, true}, {-1, -1, 600, false}, {-1, -1, -1, false},
            {std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), -1, false},
            {std::numeric_limits<int>::max(), 1, 0, false}, at(0, 0), at(0, 0, true), at(40, 40), at(41, 41)};
        const auto compare = [&](const RuntimeSubcellLocation& destination,
                                 const std::unordered_set<int>& ignored,
                                 const RuntimeSubcellContext& context) {
            const auto oldSlots = [&] { RuntimeHistoryQueryReferenceScope reference(true);
                return left.availableSubCells(data.objects, data.terrain, destination, ignored, context); }();
            const auto newSlots = right.availableSubCells(data.objects, data.terrain, destination, ignored, context);
            ++queriesCompared;
            return oldSlots == newSlots;
        };
        for (int variant = 0; variant < 3; ++variant) {
            std::unordered_set<int> ignored;
            auto context = data.context;
            if (variant == 1) ignored = {3, 7, 800, 807, 901, std::numeric_limits<int>::min()};
            if (variant == 2) {
                ignored = {9000, 802, 804};
                context.ignoredWholeCellObjectIds = {800, 801, 901};
            }
            for (const auto& destination : destinations)
                if (!compare(destination, ignored, context)) return fail(detail + " complete slots/order");
        }
        if (!equalReservations(left, right) || left.stateHash(data.objects, data.terrain) != initialHash
            || right.stateHash(data.objects, data.terrain) != initialHash)
            return fail(detail + " readonly reservation/occupancy hash");
        if (phase == 0) {
            // These goldens distinguish union-of-duplicates from first-id
            // indexing, dead-host cargo, bridge layers and legacy cell keys.
            if (right.availableSubCells(data.objects, data.terrain, at(13, 10), {}, data.context)
                    != std::vector<int>({2, 3})
                || right.availableSubCells(data.objects, data.terrain, at(14, 10), {}, data.context)
                    != std::vector<int>{}
                || right.availableSubCells(data.objects, data.terrain, at(7, 0))
                    != std::vector<int>({2, 4, 3})
                || right.availableSubCells(data.objects, data.terrain, at(16, 10, true))
                    != std::vector<int>({4, 3})
                || right.availableSubCells(data.objects, data.terrain, {-1, 9, 501, false})
                    != std::vector<int>({2, 3})
                || right.availableSubCells(data.objects, data.terrain, at(40, 40))
                    != std::vector<int>({2, 3})) return fail(detail + " original resident goldens");
        }
        // Reservations, terrain and authorization context are intentionally
        // absent from the index and must refresh on each production query.
        if (phase == 6) for (int step = 0; step < 4; ++step) {
            if (left.release(9000) != right.release(9000)) return fail(detail + " release");
            data.terrain.front().occupiedSubCells = step % 2 == 0 ? std::vector<int>{2} : std::vector<int>{4, 3};
            auto context = data.context;
            if (step % 2 == 0) context.ignoredWholeCellObjectIds.clear();
            const auto destination = at(41 + step % 2, 41);
            const auto a = [&] { RuntimeHistoryQueryReferenceScope reference(true);
                return left.reserveForDestination(data.objects, data.terrain, 9000, destination, context); }();
            const auto b = right.reserveForDestination(data.objects, data.terrain, 9000, destination, context);
            if (resultFields(a) != resultFields(b) || !b.reserved() || !equalReservations(left, right))
                return fail(detail + " live reservation result");
            ++reservationsCompared;
            for (const auto& probe : {at(10, 10), at(13, 10), at(41, 41), at(42, 41)})
                if (!compare(probe, {}, context)) return fail(detail + " refreshed terrain/context/reservation");
            if (left.stateHash(data.objects, data.terrain) != right.stateHash(data.objects, data.terrain))
                return fail(detail + " refreshed system hash");
        }
        auto borrowed = data.objects;
        if (scope.accepts(borrowed)
            || RuntimeSubcellReadOnlyQueryScope::queryResidents(borrowed, at(13, 10), {}, {}))
            return fail(detail + " borrowed vector identity");
        {
            RuntimeSubcellReadOnlyQueryScope disabled(data.objects, false);
            if (RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, at(13, 10), {}, {})
                || !compare(at(13, 10), {}, data.context)) return fail(detail + " disabled nested scope");
        }
        if (!RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, at(13, 10), {}, {}))
            return fail(detail + " disabled scope restoration");
        {
            RuntimeSubcellReadOnlyQueryScope nested(borrowed);
            if (!nested.accepts(borrowed)
                || RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, at(13, 10), {}, {})
                || !RuntimeSubcellReadOnlyQueryScope::queryResidents(borrowed, at(13, 10), {}, {}))
                return fail(detail + " nested distinct vector");
            RuntimeHistoryQueryReferenceScope reference(true);
            if (RuntimeSubcellReadOnlyQueryScope::queryResidents(borrowed, at(13, 10), {}, {}))
                return fail(detail + " active scope reference override");
        }
        if (!RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, at(13, 10), {}, {}))
            return fail(detail + " nested vector restoration");
        scope.invalidate();
        first(data.objects, 901)->subCell = 3;
        first(data.objects, 999)->transportPassengerIds.clear();
        if (scope.accepts(data.objects)
            || RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, {-1, -1, 500, false}, {}, {})
            || !compare({-1, -1, 500, false}, {}, data.context)
            || !compare(at(7, 0), {}, data.context)) return fail(detail + " invalidated eligibility/cargo fallback");
        {
            RuntimeSubcellReadOnlyQueryScope fresh(data.objects);
            if (!RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, at(7, 0), {}, {})
                || !compare(at(7, 0), {}, data.context)) return fail(detail + " fresh scope after mutation");
        }
        if (RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, at(7, 0), {}, {}))
            return fail(detail + " invalid outer scope restored");
        // Deliberately exercise the defensive shape checks even if a caller
        // forgot its required invalidation before structural world changes.
        {
            RuntimeSubcellReadOnlyQueryScope shape(data.objects);
            data.objects.push_back(object(99001, WorldObjectKind::Infantry, 43, 43));
            if (shape.accepts(data.objects)
                || RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, at(43, 43), {}, {})
                || !compare(at(43, 43), {}, data.context)) return fail(detail + " appended vector fallback");
        }
        {
            RuntimeSubcellReadOnlyQueryScope shape(data.objects);
            data.objects.reserve(data.objects.capacity() + 1);
            if (shape.accepts(data.objects)
                || RuntimeSubcellReadOnlyQueryScope::queryResidents(data.objects, at(43, 43), {}, {})
                || !compare(at(43, 43), {}, data.context)) return fail(detail + " relocated vector fallback");
        }
    }
    std::vector<WorldObjectInstance> empty;
    RuntimeHistoryQueryReferenceScope optimized(false);
    RuntimeSubcellReadOnlyQueryScope scope(empty);
    const auto resident = RuntimeSubcellReadOnlyQueryScope::queryResidents(empty, at(1, 1), {}, {});
    if (!resident || resident->hardBlocked
        || RuntimeSubcellSystemState{}.availableSubCells(empty, {}, at(1, 1)) != std::vector<int>({2, 4, 3}))
        return fail("empty world scope");
    std::cout << "Subcell readonly resident scope: exactQueries=" << queriesCompared
        << " liveReservationResults=" << reservationsCompared
        << " history0/1000/10000/deadCargo/duplicateUnion/layers/legacyExtreme/nestedIdentity/invalidate/refresh/hash=yes\n";
    return true;
}

} // namespace

int main() {
    const char* movementReference = std::getenv("CONCORD_MOVEMENT_REFERENCE");
    if (movementReference && std::string_view(movementReference) == "1") {
        std::cerr << "Requested-index contract requires CONCORD_MOVEMENT_REFERENCE=0 to exercise both index branches\n";
        return 2;
    }
    std::string error;
    if (!run(error) || !runReconcile(error) || !runReadOnlyScope(error)) {
        std::cerr << "Subcell requested index contract: " << error << '\n'; return 1;
    }
    return 0;
}
