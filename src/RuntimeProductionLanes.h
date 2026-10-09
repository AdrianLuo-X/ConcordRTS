#pragma once

#include "RulesDatabase.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Numeric values follow the original production queue contract.  Keep them
// stable because later runtime integration will persist lanes by name/value.
enum class RuntimeProductionLane : std::uint8_t {
    Structures = 0,
    Armory = 1,
    Infantry = 2,
    Vehicles = 3,
    Aircraft = 4,
    Ships = 5
};

inline constexpr std::array<RuntimeProductionLane, 6>
    runtimeProductionLanes = {
        RuntimeProductionLane::Structures,
        RuntimeProductionLane::Armory,
        RuntimeProductionLane::Infantry,
        RuntimeProductionLane::Vehicles,
        RuntimeProductionLane::Aircraft,
        RuntimeProductionLane::Ships
    };

// Map insertion order in the reference runtime is gameplay-significant when
// multiple lanes compete for the last credits on the same tick.  Preserve the
// public enum values above, but advance Ships before Aircraft.
inline constexpr std::array<RuntimeProductionLane, 6>
    runtimeProductionLaneAdvanceOrder = {
        RuntimeProductionLane::Structures,
        RuntimeProductionLane::Armory,
        RuntimeProductionLane::Infantry,
        RuntimeProductionLane::Vehicles,
        RuntimeProductionLane::Ships,
        RuntimeProductionLane::Aircraft
    };

// UnitType itself does not retain which top-level rules list supplied it, so
// callers pass that source classification explicitly.
enum class RuntimeProductionUnitClass : std::uint8_t {
    Infantry,
    Vehicle,
    Aircraft
};

std::string_view runtimeProductionLaneName(RuntimeProductionLane lane);

std::optional<RuntimeProductionLane> runtimeProductionLaneFromName(
    std::string_view name
);

RuntimeProductionLane runtimeProductionLaneForBuilding(
    const BuildingType& building
);

RuntimeProductionLane runtimeProductionLaneForUnit(
    const UnitType& unit,
    RuntimeProductionUnitClass unitClass
);

// Factory=BuildingType services both building lanes.  A stock naval factory
// uses Factory=UnitType with Naval=yes, which must not service land vehicles.
bool runtimeProductionFactorySupportsLane(
    const BuildingType& factory,
    RuntimeProductionLane lane
);

struct RuntimeProductionLaneOrder {
    std::uint64_t serial = 0;
    std::string itemKey;
    int cost = 0;
    int totalTicks = 1;
    int remainingTicks = 1;
    int creditsSpent = 0;
    std::uint32_t creditRemainderMicros = 0;
    std::uint32_t zeroCostProgressMicros = 0;
    // Preserve the remainder of the per-tick rate division separately from
    // whole microcredits.  Without this carry, any cost/build-time pair that
    // does not divide evenly finishes one tick late even with full funding.
    std::uint64_t rateDivisionRemainder = 0;
    std::uint32_t rateDivisionDenominator = 0;
    bool requiresPlacement = false;
};

struct RuntimeProductionLaneState {
    std::deque<RuntimeProductionLaneOrder> queued;
    std::optional<RuntimeProductionLaneOrder> active;
    std::optional<RuntimeProductionLaneOrder> ready;
    bool onHold = false;
};

struct RuntimeProductionLanesState {
    std::array<RuntimeProductionLaneState, runtimeProductionLanes.size()> lanes;
    std::uint64_t nextSerial = 1;
};

enum class RuntimeProductionQueueDecision : std::uint8_t {
    Accepted,
    InvalidOrder,
    LaneFull
};

enum class RuntimeProductionInsertMode : std::uint8_t {
    Back,
    AfterActive
};

struct RuntimeProductionQueueResult {
    RuntimeProductionQueueDecision decision =
        RuntimeProductionQueueDecision::InvalidOrder;
    std::uint64_t serial = 0;
};

enum class RuntimeProductionAdvanceKind : std::uint8_t {
    Started,
    Progressed,
    WaitingForCredits,
    Blocked,
    Held,
    PlacementReady,
    Completed,
    CompletionDeferred
};

struct RuntimeProductionAdvanceEvent {
    RuntimeProductionLane lane = RuntimeProductionLane::Structures;
    RuntimeProductionAdvanceKind kind =
        RuntimeProductionAdvanceKind::Blocked;
    std::uint64_t serial = 0;
    std::string itemKey;
    int remainingTicks = 0;
    int creditsSpent = 0;
    // Exact credits consumed by this event's advance.  Shell integrations
    // use this delta to commit their richer economy ledgers transactionally
    // instead of handing the module a live credit account directly.
    int creditsDebited = 0;
};

struct RuntimeProductionRate {
    std::uint32_t numerator = 1;
    std::uint32_t denominator = 1;
};

struct RuntimeProductionAdvanceHooks {
    std::function<bool(
        RuntimeProductionLane,
        const RuntimeProductionLaneOrder&
    )> canStart;
    std::function<bool(
        RuntimeProductionLane,
        const RuntimeProductionLaneOrder&
    )> canContinue;
    std::function<RuntimeProductionRate(
        RuntimeProductionLane,
        const RuntimeProductionLaneOrder&
    )> productionRate;
    // Skip time only. All gates, payments and host delivery still apply.
    std::function<bool(RuntimeProductionLane,
        const RuntimeProductionLaneOrder&)> completeImmediately;
};

struct RuntimeProductionCancelResult {
    bool removed = false;
    int refund = 0;
    RuntimeProductionLaneOrder order;
};

struct RuntimeProductionReconcileResult {
    std::vector<RuntimeProductionLaneOrder> removed;
    int refund = 0;
};

struct RuntimeProductionCompletedOrder {
    RuntimeProductionLane lane = RuntimeProductionLane::Structures;
    RuntimeProductionLaneOrder order;
};

std::size_t runtimeProductionLaneIndex(RuntimeProductionLane lane);

// Structures and Armory retain the original single-item contract.  Ground
// unit lanes count queued + active + ready against MaximumQueuedObjects + 1.
// Aircraft capacity is the number of usable docks supplied by the host.
int runtimeProductionLaneCapacity(
    RuntimeProductionLane lane,
    int maximumQueuedObjects,
    int aircraftDockCapacity
);

int runtimeProductionLaneOccupancy(
    const RuntimeProductionLanesState& state,
    RuntimeProductionLane lane
);

RuntimeProductionQueueResult runtimeQueueProductionOrder(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    RuntimeProductionLaneOrder order,
    int maximumQueuedObjects,
    int aircraftDockCapacity,
    RuntimeProductionInsertMode insertMode =
        RuntimeProductionInsertMode::Back
);

bool runtimeSetProductionHold(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    bool onHold
);

RuntimeProductionCancelResult runtimeCancelProductionOrder(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    std::string_view itemKey
);

RuntimeProductionCancelResult runtimeCancelProductionSerial(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    std::uint64_t serial
);

std::vector<RuntimeProductionAdvanceEvent> runtimeAdvanceProductionLanes(
    RuntimeProductionLanesState& state,
    int& credits,
    const RuntimeProductionAdvanceHooks& hooks = {}
);

RuntimeProductionReconcileResult runtimeReconcileProductionLaneCapacity(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    int maximumQueuedObjects,
    int aircraftDockCapacity
);

// Completed non-placement orders remain in the active slot until the host
// has successfully committed the corresponding world mutation.  Peeking and
// committing by serial prevents a blocked factory exit from losing an order
// or spawning it twice.
std::optional<RuntimeProductionLaneOrder> runtimePeekCompletedProduction(
    const RuntimeProductionLanesState& state,
    RuntimeProductionLane lane
);

std::vector<RuntimeProductionCompletedOrder>
runtimeEnumerateCompletedProduction(
    const RuntimeProductionLanesState& state
);

std::optional<RuntimeProductionLaneOrder> runtimeCommitCompletedProduction(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    std::uint64_t serial
);

std::optional<RuntimeProductionLaneOrder> runtimeConsumeReadyProduction(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    std::uint64_t serial
);

std::string runtimeSerializeProductionLanes(
    const RuntimeProductionLanesState& state
);

bool runtimeDeserializeProductionLanes(
    std::string_view serialized,
    RuntimeProductionLanesState& state,
    std::string* error = nullptr
);

std::uint64_t runtimeProductionLanesStateHash(
    const RuntimeProductionLanesState& state
);
