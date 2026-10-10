#pragma once
#include "simulation/navigation/occupancy/RuntimeSubcellSystem.h"
#include "world/queries/RuntimeHistoryQuery.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
namespace RuntimeSimulationStageAdmissionLegacySubcell {
class RuntimeSubcellSystemState {
public:
    void swap(RuntimeSubcellSystemState& other) noexcept;

    static const std::array<int, 3>& activeSubCells();
    static bool isActiveSubCell(int subCell);

    // ignoredObjectIds is intended for movers leaving their current cells.
    // It ignores both their committed occupancy and their old reservations.
    std::vector<int> availableSubCells(
        const std::vector<WorldObjectInstance>& objects,
        const std::vector<WorldTerrainObject>& terrainObjects,
        const RuntimeSubcellLocation& destination,
        const std::unordered_set<int>& ignoredObjectIds = {},
        const RuntimeSubcellContext& context = {}
    ) const;

    // Matches successive availableSubCells calls in destination order. The
    // occupancy and reservation snapshot lives only for this const call.
    std::vector<std::vector<int>> availableSubCellsBatch(
        const std::vector<WorldObjectInstance>& objects,
        const std::vector<WorldTerrainObject>& terrainObjects,
        const std::vector<RuntimeSubcellLocation>& destinations,
        const std::unordered_set<int>& ignoredObjectIds = {},
        const RuntimeSubcellContext& context = {},
        bool forceReference = false
    ) const;

    RuntimeSubcellReservationResult reserveForDestination(
        const std::vector<WorldObjectInstance>& objects,
        const std::vector<WorldTerrainObject>& terrainObjects,
        int moverObjectId,
        const RuntimeSubcellLocation& destination,
        const RuntimeSubcellContext& context = {}
    );

    // Results are sorted by mover id.  Allocation is therefore independent
    // of command insertion order and stable for lockstep/replay use.
    std::vector<RuntimeSubcellReservationResult> reserveBatch(
        const std::vector<WorldObjectInstance>& objects,
        const std::vector<WorldTerrainObject>& terrainObjects,
        const std::vector<RuntimeSubcellMoveRequest>& requests,
        const RuntimeSubcellContext& context = {}
    );

    RuntimeSubcellCommitResult commit(
        std::vector<WorldObjectInstance>& objects,
        const std::vector<WorldTerrainObject>& terrainObjects,
        int moverObjectId,
        const RuntimeSubcellContext& context = {}
    );
    bool release(int moverObjectId);
    void clearReservations();

    RuntimeSubcellReconcileReport reconcile(
        std::vector<WorldObjectInstance>& objects,
        const std::vector<WorldTerrainObject>& terrainObjects,
        const RuntimeSubcellContext& context = {}
    );

    // Subcell is already serialized in WorldObjectInstance.  This hash covers
    // committed occupancy plus transient reservations; no second save format
    // is required.
    std::uint64_t stateHash(
        const std::vector<WorldObjectInstance>& objects,
        const std::vector<WorldTerrainObject>& terrainObjects
    ) const;

    const std::map<int, RuntimeSubcellReservation>& reservations() const;

private:
    // Original per-reservation validation used by movement diagnostics and
    // duplicate-id compatibility fallback; committed reconciliation is shared.
    void reconcileReservationsReference(
        const std::vector<WorldObjectInstance>& objects,
        const std::vector<WorldTerrainObject>& terrainObjects,
        const RuntimeSubcellContext& context,
        RuntimeSubcellReconcileReport& report
    );
    // Original scalar allocation retained for movement performance/equivalence
    // diagnostics selected by CONCORD_MOVEMENT_REFERENCE=1 at process startup.
    std::vector<RuntimeSubcellReservationResult> reserveBatchReference(
        const std::vector<WorldObjectInstance>& objects,
        const std::vector<WorldTerrainObject>& terrainObjects,
        const std::vector<RuntimeSubcellMoveRequest>& requests,
        const RuntimeSubcellContext& context
    );
    std::map<int, RuntimeSubcellReservation> reservationsByMoverId_;
};
namespace {

constexpr std::array<int, 3> kActiveSubCells = {2, 4, 3};
constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

struct CellLayerKey {
    // Coordinates are preferred because terrain footprints can be expanded
    // without knowing the map's authored cell encoding.  Cell is the fallback
    // for old/synthetic records that do not carry coordinates.
    bool usesCoordinates = false;
    int x = -1;
    int y = -1;
    int cell = -1;
    bool onBridge = false;

    auto tie() const {
        return std::tie(usesCoordinates, x, y, cell, onBridge);
    }

    bool operator<(const CellLayerKey& other) const {
        return tie() < other.tie();
    }

    bool operator==(const CellLayerKey& other) const {
        return tie() == other.tie();
    }
};

struct OccupancySnapshot {
    std::set<CellLayerKey> hardBlocked;
    std::map<CellLayerKey, std::set<int>> blockedSubCells;
};

struct OccupancyQuery {
    bool validDestination = false;
    bool hardBlocked = false;
    std::set<int> blockedSubCells;
};

bool locationIsValid(const RuntimeSubcellLocation& location) {
    return (location.x >= 0 && location.y >= 0) || location.cell >= 0;
}

CellLayerKey keyForLocation(const RuntimeSubcellLocation& location) {
    CellLayerKey key;
    key.usesCoordinates = location.x >= 0 && location.y >= 0;
    key.x = key.usesCoordinates ? location.x : -1;
    key.y = key.usesCoordinates ? location.y : -1;
    key.cell = key.usesCoordinates ? -1 : location.cell;
    key.onBridge = location.onBridge;
    return key;
}

RuntimeSubcellLocation locationForObject(
    const WorldObjectInstance& object
) {
    return {object.x, object.y, object.cell, object.onBridge};
}

RuntimeSubcellLocation locationForTerrain(
    const WorldTerrainObject& object,
    int offsetX,
    int offsetY
) {
    RuntimeSubcellLocation location;
    if (object.x >= 0 && object.y >= 0) {
        location.x = object.x + offsetX;
        location.y = object.y + offsetY;
    } else if (offsetX == 0 && offsetY == 0) {
        location.cell = object.cell;
    }
    location.onBridge = false;
    return location;
}

const WorldObjectInstance* findObject(
    const std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    const auto found = std::find_if(
        objects.begin(),
        objects.end(),
        [objectId](const WorldObjectInstance& object) {
            return object.instanceId == objectId;
        }
    );
    return found == objects.end() ? nullptr : &*found;
}

WorldObjectInstance* findObject(
    std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    const auto found = std::find_if(
        objects.begin(),
        objects.end(),
        [objectId](const WorldObjectInstance& object) {
            return object.instanceId == objectId;
        }
    );
    return found == objects.end() ? nullptr : &*found;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

bool missionIsInactive(const std::string& mission) {
    const std::string normalized = lowerAscii(mission);
    return normalized == "limbo"
        || normalized == "insidetransport"
        || normalized == "garrisoned"
        || normalized == "occupied";
}

std::unordered_set<int> transportedObjectIds(
    const std::vector<WorldObjectInstance>& objects
) {
    std::unordered_set<int> transported;
    for (const WorldObjectInstance& object : objects) {
        for (const int passengerId : object.transportPassengerIds) {
            transported.insert(passengerId);
        }
    }
    return transported;
}

bool objectOccupiesGround(
    const WorldObjectInstance& object,
    const std::unordered_set<int>& transported
) {
    return object.alive
        && object.health > 0
        && !object.airborne
        && object.garrisonHostObjectId < 0
        && transported.find(object.instanceId) == transported.end()
        && !missionIsInactive(object.mission)
        && locationIsValid(locationForObject(object));
}

int normalizedOccupationBits(int bits) {
    constexpr int bitCount = 8;
    return ((bits % bitCount) + bitCount) % bitCount;
}

bool terrainFullyBlocksInfantry(const WorldTerrainObject& terrain) {
    if (normalizedOccupationBits(terrain.occupationBits) == 7) {
        return true;
    }
    return std::all_of(
        kActiveSubCells.begin(),
        kActiveSubCells.end(),
        [&terrain](int subCell) {
            return std::find(
                terrain.occupiedSubCells.begin(),
                terrain.occupiedSubCells.end(),
                subCell
            ) != terrain.occupiedSubCells.end();
        }
    );
}

OccupancySnapshot buildBaseOccupancy(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const std::unordered_set<int>& ignoredObjectIds,
    const std::unordered_set<int>& ignoredWholeCellObjectIds,
    bool includeInfantry
) {
    OccupancySnapshot snapshot;
    const std::unordered_set<int> transported = transportedObjectIds(objects);
    for (const WorldTerrainObject& terrain : terrainObjects) {
        if (!terrain.alive || terrain.health <= 0) {
            continue;
        }
        const bool fullyBlocked = terrainFullyBlocksInfantry(terrain);
        const int width = std::max(1, terrain.footprintWidth);
        const int height = std::max(1, terrain.footprintHeight);
        for (int offsetY = 0; offsetY < height; ++offsetY) {
            for (int offsetX = 0; offsetX < width; ++offsetX) {
                const RuntimeSubcellLocation location = locationForTerrain(
                    terrain,
                    offsetX,
                    offsetY
                );
                if (!locationIsValid(location)) {
                    continue;
                }
                const CellLayerKey key = keyForLocation(location);
                if (fullyBlocked) {
                    snapshot.hardBlocked.insert(key);
                    continue;
                }
                std::set<int>& blocked = snapshot.blockedSubCells[key];
                for (const int subCell : terrain.occupiedSubCells) {
                    if (RuntimeSubcellSystemState::isActiveSubCell(subCell)) {
                        blocked.insert(subCell);
                    }
                }
            }
        }
    }

    for (const WorldObjectInstance& object : objects) {
        if (ignoredObjectIds.find(object.instanceId)
                != ignoredObjectIds.end()
            || !objectOccupiesGround(object, transported)) {
            continue;
        }
        const CellLayerKey key = keyForLocation(locationForObject(object));
        if (object.kind == WorldObjectKind::Infantry) {
            if (includeInfantry
                && RuntimeSubcellSystemState::isActiveSubCell(
                    object.subCell
                )) {
                snapshot.blockedSubCells[key].insert(object.subCell);
            }
        } else {
            // Gates are structures.  Restrict this host-authorized exception
            // to structures so a malformed context can never make a vehicle
            // (or another non-structure whole-cell occupant) transparent.
            if (object.kind == WorldObjectKind::Structure
                && ignoredWholeCellObjectIds.find(object.instanceId)
                    != ignoredWholeCellObjectIds.end()) {
                continue;
            }
            // Vehicles and structures occupy their whole navigation cell.
            snapshot.hardBlocked.insert(key);
        }
    }
    return snapshot;
}

OccupancyQuery queryOccupancyReference(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const std::map<int, RuntimeSubcellReservation>& reservations,
    const RuntimeSubcellLocation& destination,
    const std::unordered_set<int>& ignoredObjectIds,
    const std::unordered_set<int>& ignoredReservationIds,
    const std::unordered_set<int>& ignoredWholeCellObjectIds
) {
    OccupancyQuery query;
    query.validDestination = locationIsValid(destination);
    if (!query.validDestination) {
        return query;
    }
    const CellLayerKey destinationKey = keyForLocation(destination);
    const OccupancySnapshot snapshot = buildBaseOccupancy(
        objects,
        terrainObjects,
        ignoredObjectIds,
        ignoredWholeCellObjectIds,
        true
    );
    query.hardBlocked = snapshot.hardBlocked.find(destinationKey)
        != snapshot.hardBlocked.end();
    const auto blocked = snapshot.blockedSubCells.find(destinationKey);
    if (blocked != snapshot.blockedSubCells.end()) {
        query.blockedSubCells = blocked->second;
    }
    for (const auto& [moverId, reservation] : reservations) {
        if (ignoredReservationIds.find(moverId)
                != ignoredReservationIds.end()
            || !RuntimeSubcellSystemState::isActiveSubCell(
                reservation.subCell
            )
            || !locationIsValid(reservation.destination)
            || !(keyForLocation(reservation.destination) == destinationKey)) {
            continue;
        }
        query.blockedSubCells.insert(reservation.subCell);
    }
    return query;
}

OccupancyQuery queryOccupancy(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const std::map<int, RuntimeSubcellReservation>& reservations,
    const RuntimeSubcellLocation& destination,
    const std::unordered_set<int>& ignoredObjectIds,
    const std::unordered_set<int>& ignoredReservationIds,
    const std::unordered_set<int>& ignoredWholeCellObjectIds
) {
    static const bool useReference = [] {
        const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    if (useReference) return queryOccupancyReference(objects, terrainObjects,
        reservations, destination, ignoredObjectIds, ignoredReservationIds,
        ignoredWholeCellObjectIds);
    OccupancyQuery query;
    query.validDestination = locationIsValid(destination);
    if (!query.validDestination) return query;
    const CellLayerKey destinationKey = keyForLocation(destination);
    // A scalar query consumes only one cell/layer of the original snapshot.
    // Accumulate that same union directly instead of allocating tree nodes
    // for every unrelated world and terrain cell on each infantry step.
    if (!destination.onBridge) {
        for (const WorldTerrainObject& terrain : terrainObjects) {
            if (!terrain.alive || terrain.health <= 0) continue;
            bool coversDestination = false;
            if (terrain.x >= 0 && terrain.y >= 0) {
                if (!destinationKey.usesCoordinates) continue;
                const std::int64_t right = static_cast<std::int64_t>(terrain.x)
                    + std::max(1, terrain.footprintWidth);
                const std::int64_t bottom = static_cast<std::int64_t>(terrain.y)
                    + std::max(1, terrain.footprintHeight);
                if (right - 1 > std::numeric_limits<int>::max()
                    || bottom - 1 > std::numeric_limits<int>::max()) {
                    return queryOccupancyReference(objects, terrainObjects,
                        reservations, destination, ignoredObjectIds,
                        ignoredReservationIds, ignoredWholeCellObjectIds);
                }
                coversDestination = destination.x >= terrain.x
                    && destination.y >= terrain.y
                    && destination.x < right && destination.y < bottom;
            } else {
                // Legacy terrain without coordinates contributes only its
                // authored cell at offset (0,0), regardless of footprint.
                const auto location = locationForTerrain(terrain, 0, 0);
                coversDestination = locationIsValid(location)
                    && keyForLocation(location) == destinationKey;
            }
            if (!coversDestination) continue;
            if (terrainFullyBlocksInfantry(terrain)) {
                query.hardBlocked = true;
            } else {
                for (int subCell : terrain.occupiedSubCells)
                    if (RuntimeSubcellSystemState::isActiveSubCell(subCell))
                        query.blockedSubCells.insert(subCell);
            }
        }
    }
    if (const auto residents = RuntimeSubcellReadOnlyQueryScope::queryResidents(
            objects, destination, ignoredObjectIds, ignoredWholeCellObjectIds)) {
        query.hardBlocked |= residents->hardBlocked;
        for (std::size_t index = 0; index < kActiveSubCells.size(); ++index)
            if (residents->blockedSubCells[index])
                query.blockedSubCells.insert(kActiveSubCells[index]);
    } else {
        const auto transported = transportedObjectIds(objects);
        for (const WorldObjectInstance& object : objects) {
            if (!(keyForLocation(locationForObject(object)) == destinationKey)
                || ignoredObjectIds.find(object.instanceId) != ignoredObjectIds.end()
                || !objectOccupiesGround(object, transported)) continue;
            if (object.kind == WorldObjectKind::Infantry) {
                if (RuntimeSubcellSystemState::isActiveSubCell(object.subCell))
                    query.blockedSubCells.insert(object.subCell);
            } else if (object.kind != WorldObjectKind::Structure
                || ignoredWholeCellObjectIds.find(object.instanceId)
                    == ignoredWholeCellObjectIds.end()) {
                query.hardBlocked = true;
            }
        }
    }
    for (const auto& [moverId, reservation] : reservations) {
        if (ignoredReservationIds.find(moverId) != ignoredReservationIds.end()
            || !RuntimeSubcellSystemState::isActiveSubCell(reservation.subCell)
            || !locationIsValid(reservation.destination)
            || !(keyForLocation(reservation.destination) == destinationKey)) continue;
        query.blockedSubCells.insert(reservation.subCell);
    }
    return query;
}

std::vector<int> availableFromQuery(const OccupancyQuery& query) {
    std::vector<int> available;
    if (!query.validDestination || query.hardBlocked) {
        return available;
    }
    for (const int subCell : kActiveSubCells) {
        if (query.blockedSubCells.find(subCell)
            == query.blockedSubCells.end()) {
            available.push_back(subCell);
        }
    }
    return available;
}

// A local avoidance query needs only a few cells. Accumulate their exact
// occupancy union without constructing map/set nodes for the entire world.
std::optional<std::vector<std::vector<int>>> selectiveAvailableSubCells(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const std::map<int, RuntimeSubcellReservation>& reservations,
    const std::vector<RuntimeSubcellLocation>& destinations,
    const std::unordered_set<int>& ignoredObjectIds,
    const RuntimeSubcellContext& context
) {
    constexpr std::size_t capacity = 16;
    if (destinations.size() > capacity) return std::nullopt;
    struct Query {
        CellLayerKey key;
        bool valid = false;
        bool hardBlocked = false;
        unsigned int blocked = 0;
    };
    std::array<Query, capacity> queries{};
    bool anyValid = false;
    bool anyGround = false;
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = -1;
    int maxY = -1;
    for (std::size_t i = 0; i < destinations.size(); ++i) {
        Query& query = queries[i];
        query.valid = locationIsValid(destinations[i]);
        if (!query.valid) continue;
        anyValid = true;
        query.key = keyForLocation(destinations[i]);
        anyGround |= !query.key.onBridge;
        if (query.key.usesCoordinates) {
            minX = std::min(minX, query.key.x);
            minY = std::min(minY, query.key.y);
            maxX = std::max(maxX, query.key.x);
            maxY = std::max(maxY, query.key.y);
        }
    }
    const auto matchingQueries = [&](const CellLayerKey& key) {
        unsigned int matches = 0;
        if (key.usesCoordinates && (key.x < minX || key.x > maxX
                || key.y < minY || key.y > maxY)) return matches;
        for (std::size_t i = 0; i < destinations.size(); ++i) {
            if (queries[i].valid && queries[i].key == key)
                matches |= 1U << i;
        }
        return matches;
    };
    const auto block = [&](unsigned int matches, bool wholeCell,
                           unsigned int slots) {
        for (std::size_t i = 0; i < destinations.size(); ++i) {
            if ((matches & (1U << i)) == 0) continue;
            queries[i].hardBlocked |= wholeCell;
            queries[i].blocked |= slots;
        }
    };
    if (anyGround) {
        for (const WorldTerrainObject& terrain : terrainObjects) {
            if (!terrain.alive || terrain.health <= 0) continue;
            unsigned int matches = 0;
            if (terrain.x >= 0 && terrain.y >= 0) {
                const std::int64_t right = static_cast<std::int64_t>(terrain.x)
                    + std::max(1, terrain.footprintWidth);
                const std::int64_t bottom = static_cast<std::int64_t>(terrain.y)
                    + std::max(1, terrain.footprintHeight);
                // Retain the original expansion for malformed footprints
                // whose integer coordinates would overflow.
                if (right - 1 > std::numeric_limits<int>::max()
                    || bottom - 1 > std::numeric_limits<int>::max())
                    return std::nullopt;
                if (right <= minX || terrain.x > maxX
                    || bottom <= minY || terrain.y > maxY) continue;
                for (std::size_t i = 0; i < destinations.size(); ++i) {
                    const Query& query = queries[i];
                    if (query.valid && query.key.usesCoordinates
                        && !query.key.onBridge && query.key.x >= terrain.x
                        && query.key.y >= terrain.y && query.key.x < right
                        && query.key.y < bottom) matches |= 1U << i;
                }
            } else {
                // Legacy records contribute only their authored cell at
                // offset (0,0), even when their declared footprint is larger.
                const auto location = locationForTerrain(terrain, 0, 0);
                if (locationIsValid(location))
                    matches = matchingQueries(keyForLocation(location));
            }
            if (matches == 0) continue;
            unsigned int slots = 0;
            for (int subCell : terrain.occupiedSubCells) {
                if (RuntimeSubcellSystemState::isActiveSubCell(subCell))
                    slots |= 1U << subCell;
            }
            block(matches, terrainFullyBlocksInfantry(terrain), slots);
        }
    }
    if (anyValid) {
        const auto transported = transportedObjectIds(objects);
        for (const WorldObjectInstance& object : objects) {
            const unsigned int matches = matchingQueries(
                keyForLocation(locationForObject(object)));
            if (matches == 0
                || ignoredObjectIds.find(object.instanceId) != ignoredObjectIds.end()
                || !objectOccupiesGround(object, transported)) continue;
            if (object.kind == WorldObjectKind::Infantry) {
                if (RuntimeSubcellSystemState::isActiveSubCell(object.subCell))
                    block(matches, false, 1U << object.subCell);
            } else if (object.kind != WorldObjectKind::Structure
                || context.ignoredWholeCellObjectIds.find(object.instanceId)
                    == context.ignoredWholeCellObjectIds.end()) {
                block(matches, true, 0);
            }
        }
        for (const auto& [moverId, reservation] : reservations) {
            if (ignoredObjectIds.find(moverId) != ignoredObjectIds.end()
                || !RuntimeSubcellSystemState::isActiveSubCell(reservation.subCell)
                || !locationIsValid(reservation.destination)) continue;
            block(matchingQueries(keyForLocation(reservation.destination)),
                false, 1U << reservation.subCell);
        }
    }
    std::vector<std::vector<int>> available(destinations.size());
    for (std::size_t i = 0; i < destinations.size(); ++i) {
        const Query& query = queries[i];
        if (!query.valid || query.hardBlocked) continue;
        available[i].reserve(kActiveSubCells.size());
        for (int subCell : kActiveSubCells) {
            if ((query.blocked & (1U << subCell)) == 0)
                available[i].push_back(subCell);
        }
    }
    return available;
}


RuntimeSubcellReservationStatus validateMover(
    const std::vector<WorldObjectInstance>& objects,
    int moverObjectId
) {
    const WorldObjectInstance* mover = findObject(objects, moverObjectId);
    if (mover == nullptr) {
        return RuntimeSubcellReservationStatus::MissingMover;
    }
    if (mover->kind != WorldObjectKind::Infantry) {
        return RuntimeSubcellReservationStatus::MoverNotInfantry;
    }
    const std::unordered_set<int> transported = transportedObjectIds(objects);
    if (!objectOccupiesGround(*mover, transported)) {
        return RuntimeSubcellReservationStatus::InactiveMover;
    }
    return RuntimeSubcellReservationStatus::Reserved;
}

bool containsSubCell(const std::vector<int>& values, int subCell) {
    return std::find(values.begin(), values.end(), subCell) != values.end();
}

std::uint64_t mixUint64(std::uint64_t hash, std::uint64_t value) {
    for (int byteIndex = 0; byteIndex < 8; ++byteIndex) {
        hash ^= static_cast<unsigned char>(value & 0xffU);
        hash *= kFnvPrime;
        value >>= 8U;
    }
    return hash;
}

std::uint64_t mixInt(std::uint64_t hash, int value) {
    return mixUint64(
        hash,
        static_cast<std::uint64_t>(static_cast<std::int64_t>(value))
    );
}

std::uint64_t mixString(std::uint64_t hash, const std::string& value) {
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= kFnvPrime;
    }
    return mixUint64(hash, value.size());
}

void mixLocation(std::uint64_t& hash, const RuntimeSubcellLocation& location) {
    hash = mixInt(hash, location.x);
    hash = mixInt(hash, location.y);
    hash = mixInt(hash, location.cell);
    hash = mixInt(hash, location.onBridge ? 1 : 0);
}

} // namespace

// The original generic resident scope remains the existing global API.
void RuntimeSubcellSystemState::swap(
    RuntimeSubcellSystemState& other
) noexcept {
    static_assert(std::is_nothrow_swappable_v<
        std::map<int, RuntimeSubcellReservation>
    >);
    reservationsByMoverId_.swap(other.reservationsByMoverId_);
}

void swap(
    RuntimeSubcellSystemState& left,
    RuntimeSubcellSystemState& right
) noexcept {
    left.swap(right);
}

const std::array<int, 3>& RuntimeSubcellSystemState::activeSubCells() {
    return kActiveSubCells;
}

bool RuntimeSubcellSystemState::isActiveSubCell(int subCell) {
    return std::find(
        kActiveSubCells.begin(),
        kActiveSubCells.end(),
        subCell
    ) != kActiveSubCells.end();
}

std::vector<int> RuntimeSubcellSystemState::availableSubCells(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const RuntimeSubcellLocation& destination,
    const std::unordered_set<int>& ignoredObjectIds,
    const RuntimeSubcellContext& context
) const {
    return availableFromQuery(queryOccupancy(
        objects,
        terrainObjects,
        reservationsByMoverId_,
        destination,
        ignoredObjectIds,
        ignoredObjectIds,
        context.ignoredWholeCellObjectIds
    ));
}

std::vector<std::vector<int>> RuntimeSubcellSystemState::availableSubCellsBatch(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const std::vector<RuntimeSubcellLocation>& destinations,
    const std::unordered_set<int>& ignoredObjectIds,
    const RuntimeSubcellContext& context,
    bool forceReference
) const {
    static const bool useReference = [] {
        for (const char* name : {"CONCORD_MOVEMENT_REFERENCE",
                                "CONCORD_FRAME_SCALING_REFERENCE"}) {
            const char* value = std::getenv(name);
            if (value != nullptr && value[0] == '1' && value[1] == '\0')
                return true;
        }
        return false;
    }();
    if (!forceReference && !useReference) {
        if (auto selective = selectiveAvailableSubCells(objects, terrainObjects,
                reservationsByMoverId_, destinations, ignoredObjectIds, context))
            return std::move(*selective);
    }
    std::vector<std::vector<int>> available;
    available.reserve(destinations.size());
    if (destinations.empty()) {
        return available;
    }

    OccupancySnapshot snapshot = buildBaseOccupancy(
        objects,
        terrainObjects,
        ignoredObjectIds,
        context.ignoredWholeCellObjectIds,
        true
    );
    for (const auto& [moverId, reservation] : reservationsByMoverId_) {
        if (ignoredObjectIds.find(moverId) != ignoredObjectIds.end()
            || !isActiveSubCell(reservation.subCell)
            || !locationIsValid(reservation.destination)) {
            continue;
        }
        snapshot.blockedSubCells[keyForLocation(reservation.destination)]
            .insert(reservation.subCell);
    }

    for (const RuntimeSubcellLocation& destination : destinations) {
        OccupancyQuery query;
        query.validDestination = locationIsValid(destination);
        if (query.validDestination) {
            const CellLayerKey key = keyForLocation(destination);
            query.hardBlocked = snapshot.hardBlocked.find(key)
                != snapshot.hardBlocked.end();
            const auto blocked = snapshot.blockedSubCells.find(key);
            if (blocked != snapshot.blockedSubCells.end()) {
                query.blockedSubCells = blocked->second;
            }
        }
        available.push_back(availableFromQuery(query));
    }
    return available;
}

RuntimeSubcellReservationResult
RuntimeSubcellSystemState::reserveForDestination(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    int moverObjectId,
    const RuntimeSubcellLocation& destination,
    const RuntimeSubcellContext& context
) {
    RuntimeSubcellReservationResult result;
    result.moverObjectId = moverObjectId;
    result.destination = destination;
    result.status = validateMover(objects, moverObjectId);
    if (result.status != RuntimeSubcellReservationStatus::Reserved) {
        return result;
    }
    if (!locationIsValid(destination)) {
        result.status = RuntimeSubcellReservationStatus::InvalidDestination;
        return result;
    }

    const auto previous = reservationsByMoverId_.find(moverObjectId);
    std::optional<RuntimeSubcellReservation> previousReservation;
    if (previous != reservationsByMoverId_.end()) {
        previousReservation = previous->second;
        reservationsByMoverId_.erase(previous);
    }
    const std::unordered_set<int> ignoredObjects = {moverObjectId};
    const OccupancyQuery query = queryOccupancy(
        objects,
        terrainObjects,
        reservationsByMoverId_,
        destination,
        ignoredObjects,
        {},
        context.ignoredWholeCellObjectIds
    );
    const std::vector<int> available = availableFromQuery(query);
    if (available.empty()) {
        if (previousReservation.has_value()) {
            reservationsByMoverId_[moverObjectId] = *previousReservation;
        }
        result.status = query.hardBlocked
            ? RuntimeSubcellReservationStatus::HardBlocked
            : RuntimeSubcellReservationStatus::NoAvailableSubcell;
        return result;
    }
    result.subCell = available.front();
    reservationsByMoverId_[moverObjectId] = {
        moverObjectId,
        destination,
        result.subCell
    };
    return result;
}

std::vector<RuntimeSubcellReservationResult>
RuntimeSubcellSystemState::reserveBatchReference(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const std::vector<RuntimeSubcellMoveRequest>& requests,
    const RuntimeSubcellContext& context
) {
    std::vector<RuntimeSubcellMoveRequest> sortedRequests = requests;
    std::stable_sort(
        sortedRequests.begin(),
        sortedRequests.end(),
        [](const RuntimeSubcellMoveRequest& left,
           const RuntimeSubcellMoveRequest& right) {
            return std::tie(
                left.moverObjectId,
                left.destination.x,
                left.destination.y,
                left.destination.cell,
                left.destination.onBridge
            ) < std::tie(
                right.moverObjectId,
                right.destination.x,
                right.destination.y,
                right.destination.cell,
                right.destination.onBridge
            );
        }
    );

    std::unordered_map<int, std::size_t> requestCountByMover;
    for (const RuntimeSubcellMoveRequest& request : sortedRequests) {
        ++requestCountByMover[request.moverObjectId];
    }
    std::unordered_set<int> validBatchMovers;
    for (const RuntimeSubcellMoveRequest& request : sortedRequests) {
        if (requestCountByMover[request.moverObjectId] == 1U
            && validateMover(objects, request.moverObjectId)
                == RuntimeSubcellReservationStatus::Reserved
            && locationIsValid(request.destination)) {
            validBatchMovers.insert(request.moverObjectId);
        }
    }

    std::map<int, RuntimeSubcellReservation> previousReservations;
    for (const int moverId : validBatchMovers) {
        const auto previous = reservationsByMoverId_.find(moverId);
        if (previous != reservationsByMoverId_.end()) {
            previousReservations.emplace(moverId, previous->second);
            reservationsByMoverId_.erase(previous);
        }
    }

    std::vector<RuntimeSubcellReservationResult> results;
    results.reserve(sortedRequests.size());
    for (const RuntimeSubcellMoveRequest& request : sortedRequests) {
        RuntimeSubcellReservationResult result;
        result.moverObjectId = request.moverObjectId;
        result.destination = request.destination;
        if (requestCountByMover[request.moverObjectId] != 1U) {
            result.status =
                RuntimeSubcellReservationStatus::DuplicateMoverRequest;
            results.push_back(result);
            continue;
        }
        result.status = validateMover(objects, request.moverObjectId);
        if (result.status != RuntimeSubcellReservationStatus::Reserved) {
            results.push_back(result);
            continue;
        }
        if (!locationIsValid(request.destination)) {
            result.status = RuntimeSubcellReservationStatus::InvalidDestination;
            results.push_back(result);
            continue;
        }
        const OccupancyQuery query = queryOccupancy(
            objects,
            terrainObjects,
            reservationsByMoverId_,
            request.destination,
            validBatchMovers,
            {},
            context.ignoredWholeCellObjectIds
        );
        const std::vector<int> available = availableFromQuery(query);
        if (available.empty()) {
            result.status = query.hardBlocked
                ? RuntimeSubcellReservationStatus::HardBlocked
                : RuntimeSubcellReservationStatus::NoAvailableSubcell;
            const auto previous = previousReservations.find(
                request.moverObjectId
            );
            if (previous != previousReservations.end()) {
                reservationsByMoverId_[request.moverObjectId] =
                    previous->second;
            }
            results.push_back(result);
            continue;
        }
        result.subCell = available.front();
        reservationsByMoverId_[request.moverObjectId] = {
            request.moverObjectId,
            request.destination,
            result.subCell
        };
        results.push_back(result);
    }
    return results;
}

std::vector<RuntimeSubcellReservationResult>
RuntimeSubcellSystemState::reserveBatch(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const std::vector<RuntimeSubcellMoveRequest>& requests,
    const RuntimeSubcellContext& context
) {
    static const bool useReference = [] {
        const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    if (useReference) {
        return reserveBatchReference(objects, terrainObjects, requests, context);
    }
    if (requests.empty()) {
        return {};
    }
    std::vector<RuntimeSubcellMoveRequest> sortedRequests = requests;
    std::stable_sort(
        sortedRequests.begin(),
        sortedRequests.end(),
        [](const RuntimeSubcellMoveRequest& left,
           const RuntimeSubcellMoveRequest& right) {
            return std::tie(
                left.moverObjectId,
                left.destination.x,
                left.destination.y,
                left.destination.cell,
                left.destination.onBridge
            ) < std::tie(
                right.moverObjectId,
                right.destination.x,
                right.destination.y,
                right.destination.cell,
                right.destination.onBridge
            );
        }
    );

    std::unordered_map<int, std::size_t> requestCountByMover;
    for (const RuntimeSubcellMoveRequest& request : sortedRequests) {
        ++requestCountByMover[request.moverObjectId];
    }
    // Neither objects nor cargo membership changes during this allocation.
    // Preserve findObject's first match when synthetic data repeats an id.
    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    if (runtimeHistoryQueryReference()) {
        objectsById.reserve(objects.size());
        for (const WorldObjectInstance& object : objects)
            objectsById.emplace(object.instanceId, &object);
    } else {
        // Only these IDs are consulted below. Avoid allocating one node per
        // historical object; cargo/resident scans still consume the complete
        // world and retain their original occupancy rules.
        objectsById.reserve(requestCountByMover.size());
        int firstRequestedId = std::numeric_limits<int>::max();
        int lastRequestedId = std::numeric_limits<int>::min();
        for (const auto& [moverId, count] : requestCountByMover) {
            (void)count;
            objectsById.emplace(moverId, nullptr);
            firstRequestedId = std::min(firstRequestedId, moverId);
            lastRequestedId = std::max(lastRequestedId, moverId);
        }
        std::size_t missing = objectsById.size();
        for (const WorldObjectInstance& object : objects) {
            if (object.instanceId < firstRequestedId || object.instanceId > lastRequestedId) continue;
            const auto requested = objectsById.find(object.instanceId);
            if (requested == objectsById.end() || requested->second != nullptr) continue;
            // The first record wins even when dead or non-infantry. Do not
            // replace it with a later live duplicate or a different kind.
            requested->second = &object;
            if (--missing == 0) break;
        }
    }
    const std::unordered_set<int> transported = transportedObjectIds(objects);
    std::unordered_map<int, RuntimeSubcellReservationStatus> moverStatuses;
    moverStatuses.reserve(requestCountByMover.size());
    for (const auto& [moverId, count] : requestCountByMover) {
        (void)count;
        RuntimeSubcellReservationStatus status =
            RuntimeSubcellReservationStatus::MissingMover;
        const auto found = objectsById.find(moverId);
        if (found != objectsById.end() && found->second != nullptr) {
            const WorldObjectInstance& mover = *found->second;
            status = mover.kind != WorldObjectKind::Infantry
                ? RuntimeSubcellReservationStatus::MoverNotInfantry
                : !objectOccupiesGround(mover, transported)
                    ? RuntimeSubcellReservationStatus::InactiveMover
                    : RuntimeSubcellReservationStatus::Reserved;
        }
        moverStatuses.emplace(moverId, status);
    }
    std::unordered_set<int> validBatchMovers;
    for (const RuntimeSubcellMoveRequest& request : sortedRequests) {
        if (requestCountByMover[request.moverObjectId] == 1U
            && moverStatuses.at(request.moverObjectId)
                == RuntimeSubcellReservationStatus::Reserved
            && locationIsValid(request.destination)) {
            validBatchMovers.insert(request.moverObjectId);
        }
    }

    std::map<int, RuntimeSubcellReservation> previousReservations;
    for (const int moverId : validBatchMovers) {
        const auto previous = reservationsByMoverId_.find(moverId);
        if (previous != reservationsByMoverId_.end()) {
            previousReservations.emplace(moverId, previous->second);
            reservationsByMoverId_.erase(previous);
        }
    }

    // Every valid batch mover has now vacated its previous reservation.
    // From here reservations only accumulate: a success adds its new slot,
    // while a failed request restores its old slot before the next mover.
    // Thus one union snapshot exactly matches the original per-request scan,
    // including competing arrivals and failed reservations restored in order.
    OccupancySnapshot occupancy = buildBaseOccupancy(
        objects, terrainObjects, validBatchMovers,
        context.ignoredWholeCellObjectIds, true
    );
    const auto includeReservation = [&occupancy](
        const RuntimeSubcellReservation& reservation
    ) {
        if (isActiveSubCell(reservation.subCell)
            && locationIsValid(reservation.destination)) {
            occupancy.blockedSubCells[keyForLocation(reservation.destination)]
                .insert(reservation.subCell);
        }
    };
    for (const auto& [moverId, reservation] : reservationsByMoverId_) {
        (void)moverId;
        includeReservation(reservation);
    }

    std::vector<RuntimeSubcellReservationResult> results;
    results.reserve(sortedRequests.size());
    for (const RuntimeSubcellMoveRequest& request : sortedRequests) {
        RuntimeSubcellReservationResult result;
        result.moverObjectId = request.moverObjectId;
        result.destination = request.destination;
        if (requestCountByMover[request.moverObjectId] != 1U) {
            result.status =
                RuntimeSubcellReservationStatus::DuplicateMoverRequest;
            results.push_back(result);
            continue;
        }
        result.status = moverStatuses.at(request.moverObjectId);
        if (result.status != RuntimeSubcellReservationStatus::Reserved) {
            results.push_back(result);
            continue;
        }
        if (!locationIsValid(request.destination)) {
            result.status = RuntimeSubcellReservationStatus::InvalidDestination;
            results.push_back(result);
            continue;
        }
        OccupancyQuery query;
        query.validDestination = true;
        const CellLayerKey destinationKey = keyForLocation(request.destination);
        query.hardBlocked = occupancy.hardBlocked.find(destinationKey)
            != occupancy.hardBlocked.end();
        const auto blocked = occupancy.blockedSubCells.find(destinationKey);
        if (blocked != occupancy.blockedSubCells.end()) {
            query.blockedSubCells = blocked->second;
        }
        const std::vector<int> available = availableFromQuery(query);
        if (available.empty()) {
            result.status = query.hardBlocked
                ? RuntimeSubcellReservationStatus::HardBlocked
                : RuntimeSubcellReservationStatus::NoAvailableSubcell;
            const auto previous = previousReservations.find(
                request.moverObjectId
            );
            if (previous != previousReservations.end()) {
                reservationsByMoverId_[request.moverObjectId] =
                    previous->second;
                includeReservation(previous->second);
            }
            results.push_back(result);
            continue;
        }
        result.subCell = available.front();
        reservationsByMoverId_[request.moverObjectId] = {
            request.moverObjectId,
            request.destination,
            result.subCell
        };
        includeReservation(reservationsByMoverId_.at(request.moverObjectId));
        results.push_back(result);
    }
    return results;
}

RuntimeSubcellCommitResult RuntimeSubcellSystemState::commit(
    std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    int moverObjectId,
    const RuntimeSubcellContext& context
) {
    RuntimeSubcellCommitResult result;
    result.moverObjectId = moverObjectId;
    const auto reserved = reservationsByMoverId_.find(moverObjectId);
    if (reserved == reservationsByMoverId_.end()) {
        result.status = RuntimeSubcellCommitStatus::MissingReservation;
        return result;
    }
    result.subCell = reserved->second.subCell;
    WorldObjectInstance* mover = findObject(objects, moverObjectId);
    if (mover == nullptr) {
        result.status = RuntimeSubcellCommitStatus::MissingMover;
        return result;
    }
    const std::unordered_set<int> transported = transportedObjectIds(objects);
    if (mover->kind != WorldObjectKind::Infantry
        || !objectOccupiesGround(*mover, transported)) {
        result.status = RuntimeSubcellCommitStatus::InactiveMover;
        return result;
    }
    const std::unordered_set<int> ignored = {moverObjectId};
    const OccupancyQuery query = queryOccupancy(
        objects,
        terrainObjects,
        reservationsByMoverId_,
        reserved->second.destination,
        ignored,
        ignored,
        context.ignoredWholeCellObjectIds
    );
    if (!query.validDestination
        || query.hardBlocked
        || query.blockedSubCells.find(reserved->second.subCell)
            != query.blockedSubCells.end()) {
        result.status = RuntimeSubcellCommitStatus::DestinationBlocked;
        return result;
    }
    const RuntimeSubcellLocation destination = reserved->second.destination;
    if (destination.x >= 0 && destination.y >= 0) {
        mover->x = destination.x;
        mover->y = destination.y;
    }
    if (destination.cell >= 0) {
        mover->cell = destination.cell;
    }
    mover->onBridge = destination.onBridge;
    mover->subCell = reserved->second.subCell;
    reservationsByMoverId_.erase(reserved);
    result.status = RuntimeSubcellCommitStatus::Committed;
    return result;
}

bool RuntimeSubcellSystemState::release(int moverObjectId) {
    return reservationsByMoverId_.erase(moverObjectId) != 0U;
}

void RuntimeSubcellSystemState::clearReservations() {
    reservationsByMoverId_.clear();
}

RuntimeSubcellReconcileReport RuntimeSubcellSystemState::reconcile(
    std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const RuntimeSubcellContext& context
) {
    RuntimeSubcellReconcileReport report;
    const OccupancySnapshot base = buildBaseOccupancy(
        objects,
        terrainObjects,
        {},
        context.ignoredWholeCellObjectIds,
        false
    );
    const std::unordered_set<int> transported = transportedObjectIds(objects);
    std::map<CellLayerKey, std::vector<WorldObjectInstance*>> infantryByCell;
    for (WorldObjectInstance& object : objects) {
        if (object.kind == WorldObjectKind::Infantry
            && objectOccupiesGround(object, transported)) {
            infantryByCell[keyForLocation(locationForObject(object))]
                .push_back(&object);
        }
    }

    for (auto& [cellKey, infantry] : infantryByCell) {
        std::stable_sort(
            infantry.begin(),
            infantry.end(),
            [](const WorldObjectInstance* left,
               const WorldObjectInstance* right) {
                return left->instanceId < right->instanceId;
            }
        );
        const bool hardBlocked = base.hardBlocked.find(cellKey)
            != base.hardBlocked.end();
        std::set<int> terrainBlocked;
        const auto terrainIt = base.blockedSubCells.find(cellKey);
        if (terrainIt != base.blockedSubCells.end()) {
            terrainBlocked = terrainIt->second;
        }

        std::map<int, std::vector<WorldObjectInstance*>> candidatesBySubCell;
        if (!hardBlocked) {
            for (WorldObjectInstance* object : infantry) {
                if (isActiveSubCell(object->subCell)
                    && terrainBlocked.find(object->subCell)
                        == terrainBlocked.end()) {
                    candidatesBySubCell[object->subCell].push_back(object);
                }
            }
        }

        std::unordered_set<int> retainedObjectIds;
        std::set<int> occupiedSubCells = terrainBlocked;
        for (const int subCell : kActiveSubCells) {
            const auto candidates = candidatesBySubCell.find(subCell);
            if (candidates == candidatesBySubCell.end()
                || candidates->second.empty()) {
                continue;
            }
            WorldObjectInstance* retained = *std::min_element(
                candidates->second.begin(),
                candidates->second.end(),
                [](const WorldObjectInstance* left,
                   const WorldObjectInstance* right) {
                    return left->instanceId < right->instanceId;
                }
            );
            retainedObjectIds.insert(retained->instanceId);
            occupiedSubCells.insert(subCell);
            ++report.retainedInfantry;
        }

        for (WorldObjectInstance* object : infantry) {
            if (retainedObjectIds.find(object->instanceId)
                != retainedObjectIds.end()) {
                continue;
            }
            const int previousSubCell = object->subCell;
            int replacement = -1;
            if (!hardBlocked) {
                for (const int subCell : kActiveSubCells) {
                    if (occupiedSubCells.find(subCell)
                        == occupiedSubCells.end()) {
                        replacement = subCell;
                        break;
                    }
                }
            }
            if (replacement >= 0) {
                object->subCell = replacement;
                occupiedSubCells.insert(replacement);
                ++report.reassignedInfantry;
                report.actions.push_back({
                    RuntimeSubcellReconcileActionKind::Reassigned,
                    object->instanceId,
                    locationForObject(*object),
                    previousSubCell,
                    replacement,
                    "reassigned invalid or duplicate infantry subcell"
                });
                continue;
            }
            object->subCell = -1;
            ++report.needsMoveOut;
            report.actions.push_back({
                RuntimeSubcellReconcileActionKind::NeedsMoveOut,
                object->instanceId,
                locationForObject(*object),
                previousSubCell,
                -1,
                hardBlocked
                    ? "cell has a whole-cell blocker"
                    : "cell has no free infantry subcell"
            });
        }
    }

    if (reservationsByMoverId_.empty()) return report;
    static const bool useReference = [] {
        const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    if (useReference) {
        reconcileReservationsReference(objects, terrainObjects, context, report);
        return report;
    }

    // The committed positions are fixed during reservation validation. Count
    // infantry slots once after reconciliation, then subtract only the mover's
    // own committed slot for each query. Terrain, overlapping occupants and
    // reservations accepted earlier in mover-id order continue to block it.
    std::unordered_map<int, const WorldObjectInstance*> objectsById;
    const bool historyReference = runtimeHistoryQueryReference();
    std::size_t missingMovers = 0;
    if (historyReference) {
        objectsById.reserve(objects.size());
    } else {
        objectsById.reserve(reservationsByMoverId_.size());
        for (const auto& [moverId, reservation] : reservationsByMoverId_) {
            (void)reservation;
            objectsById.emplace(moverId, nullptr);
        }
        missingMovers = objectsById.size();
    }
    const int firstReservedId = reservationsByMoverId_.begin()->first;
    const int lastReservedId = reservationsByMoverId_.rbegin()->first;
    bool havePreviousId = false;
    bool strictlyIncreasingIds = true;
    int previousId = 0;
    std::map<CellLayerKey, std::array<std::size_t, 3>> infantryCounts;
    for (const WorldObjectInstance& object : objects) {
        if (historyReference) {
            if (!objectsById.emplace(object.instanceId, &object).second) {
                reconcileReservationsReference(objects, terrainObjects, context, report);
                return report;
            }
        } else {
            // First match wins even if dead, contained or non-infantry. Only
            // reservation movers are read from this map during validation.
            if (missingMovers != 0 && object.instanceId >= firstReservedId
                && object.instanceId <= lastReservedId) {
                const auto requested = objectsById.find(object.instanceId);
                if (requested != objectsById.end() && requested->second == nullptr) {
                    requested->second = &object;
                    --missingMovers;
                }
            }
            if (havePreviousId && object.instanceId <= previousId)
                strictlyIncreasingIds = false;
            previousId = object.instanceId;
            havePreviousId = true;
        }
        if (object.kind == WorldObjectKind::Infantry
            && objectOccupiesGround(object, transported)
            && isActiveSubCell(object.subCell)) {
            ++infantryCounts[keyForLocation(locationForObject(object))]
                [static_cast<std::size_t>(object.subCell - 2)];
        }
    }
    if (!historyReference && !strictlyIncreasingIds) {
        // Preserve the original fallback for ANY duplicate, including an
        // unrelated dead record. A contiguous ID vector avoids historical
        // hash-node allocations; strictly increasing worlds need no vector.
        std::vector<int> objectIds;
        objectIds.reserve(objects.size());
        for (const auto& object : objects) objectIds.push_back(object.instanceId);
        std::sort(objectIds.begin(), objectIds.end());
        if (std::adjacent_find(objectIds.begin(), objectIds.end()) != objectIds.end()) {
            reconcileReservationsReference(objects, terrainObjects, context, report);
            return report;
        }
    }
    const auto oldReservations = std::move(reservationsByMoverId_);
    reservationsByMoverId_.clear();
    std::map<CellLayerKey, std::set<int>> acceptedSlots;
    for (const auto& [moverId, reservation] : oldReservations) {
        const auto found = objectsById.find(moverId);
        const WorldObjectInstance* mover = found == objectsById.end()
            ? nullptr : found->second;
        bool available = mover != nullptr
            && mover->kind == WorldObjectKind::Infantry
            && objectOccupiesGround(*mover, transported)
            && isActiveSubCell(reservation.subCell)
            && locationIsValid(reservation.destination);
        if (available) {
            const CellLayerKey key = keyForLocation(reservation.destination);
            const auto terrain = base.blockedSubCells.find(key);
            const auto accepted = acceptedSlots.find(key);
            available = base.hardBlocked.find(key) == base.hardBlocked.end()
                && (terrain == base.blockedSubCells.end()
                    || terrain->second.find(reservation.subCell) == terrain->second.end())
                && (accepted == acceptedSlots.end()
                    || accepted->second.find(reservation.subCell) == accepted->second.end());
            const auto infantry = infantryCounts.find(key);
            if (available && infantry != infantryCounts.end()) {
                const std::size_t ignoredSelf = mover->subCell == reservation.subCell
                    && keyForLocation(locationForObject(*mover)) == key ? 1U : 0U;
                available = infantry->second[
                    static_cast<std::size_t>(reservation.subCell - 2)] <= ignoredSelf;
            }
            if (available) {
                reservationsByMoverId_.emplace(moverId, reservation);
                acceptedSlots[key].insert(reservation.subCell);
                continue;
            }
        }
        ++report.droppedReservations;
        report.actions.push_back({
            RuntimeSubcellReconcileActionKind::DroppedReservation,
            moverId,
            reservation.destination,
            reservation.subCell,
            -1,
            "reservation no longer has a valid mover or destination subcell"
        });
    }
    return report;
}

void RuntimeSubcellSystemState::reconcileReservationsReference(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects,
    const RuntimeSubcellContext& context,
    RuntimeSubcellReconcileReport& report
) {
    const std::map<int, RuntimeSubcellReservation> oldReservations =
        reservationsByMoverId_;
    reservationsByMoverId_.clear();
    for (const auto& [moverId, reservation] : oldReservations) {
        const RuntimeSubcellReservationStatus moverStatus = validateMover(
            objects,
            moverId
        );
        const std::unordered_set<int> ignoredObjectIds = {moverId};
        const OccupancyQuery query = queryOccupancy(
            objects,
            terrainObjects,
            reservationsByMoverId_,
            reservation.destination,
            ignoredObjectIds,
            {},
            context.ignoredWholeCellObjectIds
        );
        const std::vector<int> available = availableFromQuery(query);
        if (moverStatus == RuntimeSubcellReservationStatus::Reserved
            && isActiveSubCell(reservation.subCell)
            && containsSubCell(available, reservation.subCell)) {
            reservationsByMoverId_.emplace(moverId, reservation);
            continue;
        }
        ++report.droppedReservations;
        report.actions.push_back({
            RuntimeSubcellReconcileActionKind::DroppedReservation,
            moverId,
            reservation.destination,
            reservation.subCell,
            -1,
            "reservation no longer has a valid mover or destination subcell"
        });
    }
}

std::uint64_t RuntimeSubcellSystemState::stateHash(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<WorldTerrainObject>& terrainObjects
) const {
    std::uint64_t hash = kFnvOffset;
    const std::unordered_set<int> transported = transportedObjectIds(objects);
    std::vector<const WorldObjectInstance*> sortedObjects;
    sortedObjects.reserve(objects.size());
    for (const WorldObjectInstance& object : objects) {
        if (objectOccupiesGround(object, transported)) {
            sortedObjects.push_back(&object);
        }
    }
    std::stable_sort(
        sortedObjects.begin(),
        sortedObjects.end(),
        [](const WorldObjectInstance* left, const WorldObjectInstance* right) {
            return left->instanceId < right->instanceId;
        }
    );
    hash = mixUint64(hash, sortedObjects.size());
    for (const WorldObjectInstance* object : sortedObjects) {
        hash = mixInt(hash, object->instanceId);
        hash = mixInt(hash, static_cast<int>(object->kind));
        mixLocation(hash, locationForObject(*object));
        hash = mixInt(hash, object->subCell);
    }

    std::vector<const WorldTerrainObject*> sortedTerrain;
    for (const WorldTerrainObject& terrain : terrainObjects) {
        if (terrain.alive && terrain.health > 0) {
            sortedTerrain.push_back(&terrain);
        }
    }
    std::stable_sort(
        sortedTerrain.begin(),
        sortedTerrain.end(),
        [](const WorldTerrainObject* left, const WorldTerrainObject* right) {
            return std::tie(
                left->mapId,
                left->x,
                left->y,
                left->cell,
                left->type
            ) < std::tie(
                right->mapId,
                right->x,
                right->y,
                right->cell,
                right->type
            );
        }
    );
    hash = mixUint64(hash, sortedTerrain.size());
    for (const WorldTerrainObject* terrain : sortedTerrain) {
        hash = mixString(hash, terrain->mapId);
        hash = mixString(hash, terrain->type);
        hash = mixInt(hash, terrain->x);
        hash = mixInt(hash, terrain->y);
        hash = mixInt(hash, terrain->cell);
        hash = mixInt(hash, terrain->occupationBits);
        hash = mixInt(hash, terrain->footprintWidth);
        hash = mixInt(hash, terrain->footprintHeight);
        for (const int subCell : terrain->occupiedSubCells) {
            hash = mixInt(hash, subCell);
        }
        hash = mixInt(hash, -99);
    }
    hash = mixUint64(hash, reservationsByMoverId_.size());
    for (const auto& [moverId, reservation] : reservationsByMoverId_) {
        hash = mixInt(hash, moverId);
        mixLocation(hash, reservation.destination);
        hash = mixInt(hash, reservation.subCell);
    }
    return hash;
}

const std::map<int, RuntimeSubcellReservation>&
RuntimeSubcellSystemState::reservations() const {
    return reservationsByMoverId_;
}

}
