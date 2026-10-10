#pragma once

#include "world/model/GameWorld.h"
#include "simulation/navigation/occupancy/RuntimeGroundPoseOccupancy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

struct RuntimeSubcellLocation {
    // Runtime callers should provide x/y whenever available.  The authored
    // cell id is retained as a fallback for legacy/synthetic records.
    int x = -1;
    int y = -1;
    int cell = -1;
    bool onBridge = false;
};

struct RuntimeSubcellMoveRequest {
    int moverObjectId = -1;
    RuntimeSubcellLocation destination;
};

// Whole-cell structure occupancy that the host has independently authorized
// as transparent (for example, the host object of a Fully Open gate).  This
// never suppresses terrain, infantry subcells/reservations, vehicles, or any
// structure whose id is not explicitly present.
struct RuntimeSubcellContext {
    std::unordered_set<int> ignoredWholeCellObjectIds;
    RuntimeGroundPoseOccupancyContext groundPoseContext;
};

struct RuntimeSubcellReadOnlyResidentResult {
    bool hardBlocked = false;
    // The same authored 2/4/3 order as activeSubCells().
    std::array<bool, 3> blockedSubCells{};
};

// Opt-in only: objects (including cargo lists and eligibility fields) must
// remain unchanged until this scope ends or invalidate() is called. Terrain,
// ignored ids, gate context and reservations are still read for every query.
// Generic callers without a matching scope retain their scalar query.
class RuntimeSubcellReadOnlyQueryScope {
public:
    explicit RuntimeSubcellReadOnlyQueryScope(
        const std::vector<WorldObjectInstance>& objects, bool enabled = true,
        RuntimeGroundPoseOccupancyContext groundPoseContext = {});
    ~RuntimeSubcellReadOnlyQueryScope();
    RuntimeSubcellReadOnlyQueryScope(const RuntimeSubcellReadOnlyQueryScope&) = delete;
    RuntimeSubcellReadOnlyQueryScope& operator=(const RuntimeSubcellReadOnlyQueryScope&) = delete;
    RuntimeSubcellReadOnlyQueryScope(RuntimeSubcellReadOnlyQueryScope&&) = delete;
    RuntimeSubcellReadOnlyQueryScope& operator=(RuntimeSubcellReadOnlyQueryScope&&) = delete;

    void invalidate() noexcept;
    bool accepts(const std::vector<WorldObjectInstance>& objects) const noexcept;
    static std::optional<RuntimeSubcellReadOnlyResidentResult> queryResidents(
        const std::vector<WorldObjectInstance>& objects,
        const RuntimeSubcellLocation& destination,
        const std::unordered_set<int>& ignoredObjectIds,
        const std::unordered_set<int>& ignoredWholeCellObjectIds);

private:
    using Key = std::tuple<bool, int, int, int, bool>;
    struct Resident {
        int objectId;
        WorldObjectKind kind;
        int subCell;
    };
    const std::vector<WorldObjectInstance>* objects_;
    const WorldObjectInstance* data_;
    std::size_t size_;
    bool valid_;
    RuntimeSubcellReadOnlyQueryScope* previous_;
    std::map<Key, std::vector<Resident>> residentsByCell_;
    static thread_local RuntimeSubcellReadOnlyQueryScope* active_;
};

struct RuntimeSubcellReservation {
    int moverObjectId = -1;
    RuntimeSubcellLocation destination;
    int subCell = -1;
};

enum class RuntimeSubcellReservationStatus {
    Reserved,
    MissingMover,
    InactiveMover,
    MoverNotInfantry,
    InvalidDestination,
    HardBlocked,
    NoAvailableSubcell,
    DuplicateMoverRequest
};

struct RuntimeSubcellReservationResult {
    RuntimeSubcellReservationStatus status =
        RuntimeSubcellReservationStatus::MissingMover;
    int moverObjectId = -1;
    RuntimeSubcellLocation destination;
    int subCell = -1;

    bool reserved() const {
        return status == RuntimeSubcellReservationStatus::Reserved;
    }
};

enum class RuntimeSubcellCommitStatus {
    Committed,
    MissingReservation,
    MissingMover,
    InactiveMover,
    DestinationBlocked
};

struct RuntimeSubcellCommitResult {
    RuntimeSubcellCommitStatus status =
        RuntimeSubcellCommitStatus::MissingReservation;
    int moverObjectId = -1;
    int subCell = -1;

    bool committed() const {
        return status == RuntimeSubcellCommitStatus::Committed;
    }
};

enum class RuntimeSubcellReconcileActionKind {
    Reassigned,
    NeedsMoveOut,
    DroppedReservation
};

struct RuntimeSubcellReconcileAction {
    RuntimeSubcellReconcileActionKind kind =
        RuntimeSubcellReconcileActionKind::NeedsMoveOut;
    int objectId = -1;
    RuntimeSubcellLocation location;
    int previousSubCell = -1;
    int subCell = -1;
    std::string reason;
};

struct RuntimeSubcellReconcileReport {
    std::size_t retainedInfantry = 0;
    std::size_t reassignedInfantry = 0;
    std::size_t needsMoveOut = 0;
    std::size_t droppedReservations = 0;
    std::vector<RuntimeSubcellReconcileAction> actions;
};

class RuntimeSubcellSystemState {
public:
    void swap(RuntimeSubcellSystemState& other) noexcept;

    static const std::array<int, 3>& activeSubCells();
    static bool isActiveSubCell(int subCell);

    // Keep the current slot when it is free; otherwise use the authored 2/4/3 order.
    static int selectAvailableSubCell(
        const std::vector<int>& available, int preferredSubCell) noexcept;

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
        const std::vector<WorldTerrainObject>& terrainObjects,
        RuntimeGroundPoseOccupancyContext groundPoseContext = {}
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

void swap(
    RuntimeSubcellSystemState& left,
    RuntimeSubcellSystemState& right
) noexcept;
