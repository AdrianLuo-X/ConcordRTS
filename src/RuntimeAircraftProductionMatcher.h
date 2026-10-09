#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

inline constexpr std::size_t runtimeAircraftProductionMaximumSlots = 4096;
inline constexpr std::size_t runtimeAircraftProductionMaximumOrders = 4096;
inline constexpr std::size_t runtimeAircraftProductionMaximumOccupancies =
    4096;
inline constexpr std::size_t
    runtimeAircraftProductionMaximumFlexibleLiveAircraft = 100000;
inline constexpr std::size_t
    runtimeAircraftProductionMaximumCompatibleTypesPerSlot = 256;

enum class RuntimeAircraftProductionOrderPhase : std::uint8_t {
    Queued,
    Active,
    Ready,
    Prospective
};

struct RuntimeAircraftProductionSlot {
    std::string canonicalOwner;
    int hostId = -1;
    int dockIndex = -1;
    std::vector<std::string> compatibleAircraftTypeIds;
    bool available = true;
    bool alive = true;
    // Power affects production progress, not the existence of a physical pad.
    // Powered pads are preferred so a disabled primary can fall back, but
    // unpowered slots remain physical capacity when no powered match exists.
    bool poweredOperational = true;
    // User-selected primary airport wins equivalent feasible assignments.
    // This preference never removes capacity or displaces live reservations.
    bool preferredPrimary = false;

    bool operator==(const RuntimeAircraftProductionSlot&) const = default;
};

struct RuntimeAircraftProductionOrder {
    std::uint64_t serial = 0;
    std::string typeId;
    RuntimeAircraftProductionOrderPhase phase =
        RuntimeAircraftProductionOrderPhase::Queued;

    bool operator==(const RuntimeAircraftProductionOrder&) const = default;
};

// Presence in this list means that a live aircraft reserves the slot. An
// airborne sortie therefore keeps its support pad exactly like a docked plane.
struct RuntimeAircraftProductionLiveOccupancy {
    std::string canonicalOwner;
    int aircraftId = -1;
    int hostId = -1;
    int dockIndex = -1;
    bool airborne = false;

    bool operator==(
        const RuntimeAircraftProductionLiveOccupancy&
    ) const = default;
};

struct RuntimeAircraftProductionSlotReference {
    int hostId = -1;
    int dockIndex = -1;

    bool operator==(
        const RuntimeAircraftProductionSlotReference&
    ) const = default;
};

// A live PadAircraft whose support reservation may be reassigned during
// host-loss recovery. priorSlot is only a deterministic preference; a stale,
// unavailable, occupied, or incompatible prior slot does not make the input
// invalid and does not pin the aircraft to that slot.
struct RuntimeAircraftProductionFlexibleLiveAircraft {
    std::string canonicalOwner;
    int aircraftId = -1;
    std::string typeId;
    bool airborne = false;
    std::optional<RuntimeAircraftProductionSlotReference> priorSlot;

    bool operator==(
        const RuntimeAircraftProductionFlexibleLiveAircraft&
    ) const = default;
};

// One request is an atomic capacity query for one already-canonical owner.
// orders must contain every queued, active, and ready aircraft order.
struct RuntimeAircraftProductionMatchInput {
    std::string canonicalOwner;
    std::vector<RuntimeAircraftProductionSlot> slots;
    std::vector<RuntimeAircraftProductionOrder> orders;
    std::vector<RuntimeAircraftProductionLiveOccupancy> liveOccupancies;
    std::optional<RuntimeAircraftProductionOrder> prospectiveOrder;

    bool operator==(
        const RuntimeAircraftProductionMatchInput&
    ) const = default;
};

struct RuntimeAircraftProductionAssignment {
    std::string canonicalOwner;
    std::uint64_t orderSerial = 0;
    std::string typeId;
    RuntimeAircraftProductionOrderPhase phase =
        RuntimeAircraftProductionOrderPhase::Queued;
    int hostId = -1;
    int dockIndex = -1;

    bool operator==(
        const RuntimeAircraftProductionAssignment&
    ) const = default;
};

struct RuntimeAircraftProductionLiveAssignment {
    std::string canonicalOwner;
    int aircraftId = -1;
    std::string typeId;
    bool airborne = false;
    int hostId = -1;
    int dockIndex = -1;

    bool operator==(
        const RuntimeAircraftProductionLiveAssignment&
    ) const = default;
};

enum class RuntimeAircraftProductionMatchVeto : std::uint8_t {
    None,
    InputLimitExceeded,
    InvalidCanonicalOwner,
    OwnerMismatch,
    InvalidSlot,
    DuplicateSlot,
    InvalidCompatibleType,
    DuplicateCompatibleType,
    InvalidOrder,
    DuplicateOrderSerial,
    InvalidOccupancy,
    DuplicateOccupancySlot,
    DuplicateOccupancyAircraft,
    OccupancySlotMissing,
    InsufficientPhysicalSlots,
    IncompatibleAircraftType,
    NoFeasibleAssignment,
    InvalidFlexibleLiveAircraft,
    DuplicateLiveAircraft
};

struct RuntimeAircraftProductionMatchResult {
    bool inputValid = false;
    bool feasible = false;
    RuntimeAircraftProductionMatchVeto veto =
        RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner;
    std::uint64_t vetoOrderSerial = 0;
    std::string vetoTypeId;
    std::vector<RuntimeAircraftProductionAssignment> assignments;
    std::string reason;

    bool operator==(
        const RuntimeAircraftProductionMatchResult&
    ) const = default;
};

// Host-loss recovery keeps hard liveOccupancies fixed, reassigns every
// flexible live aircraft as one joint compatibility graph, and then retains
// as many existing orders as possible without reducing the maximum live
// match count. Equal-cardinality order subsets prefer lower serials.
struct RuntimeAircraftProductionRecoveryInput {
    std::string canonicalOwner;
    std::vector<RuntimeAircraftProductionSlot> slots;
    std::vector<RuntimeAircraftProductionLiveOccupancy> liveOccupancies;
    std::vector<RuntimeAircraftProductionFlexibleLiveAircraft>
        flexibleLiveAircraft;
    std::vector<RuntimeAircraftProductionOrder> orders;

    bool operator==(
        const RuntimeAircraftProductionRecoveryInput&
    ) const = default;
};

struct RuntimeAircraftProductionRecoveryResult {
    bool inputValid = false;
    bool complete = false;
    RuntimeAircraftProductionMatchVeto veto =
        RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner;
    std::size_t maximumLiveMatched = 0;
    std::vector<RuntimeAircraftProductionLiveAssignment> liveAssignments;
    std::vector<int> unassignedLiveAircraftIds;
    std::vector<RuntimeAircraftProductionAssignment>
        retainedOrderAssignments;
    std::vector<std::uint64_t> rejectedOrderSerials;
    std::string reason;

    bool operator==(
        const RuntimeAircraftProductionRecoveryResult&
    ) const = default;
};

// Returns assignments ordered by serial. Slots are considered in
// powered state, primary preference, then hostId/dockIndex order and matched with a deterministic maximum bipartite
// matcher; no input value is mutated and no state is retained.
RuntimeAircraftProductionMatchResult runtimeMatchAircraftProductionDocks(
    const RuntimeAircraftProductionMatchInput& input
);

// Pure value recovery query. Output live assignments are ordered by
// aircraftId, retained order assignments and rejected serials by order serial,
// and unassigned live ids by aircraftId. No input value is mutated.
RuntimeAircraftProductionRecoveryResult
runtimeRecoverAircraftProductionDocks(
    const RuntimeAircraftProductionRecoveryInput& input
);

const char* runtimeAircraftProductionMatchVetoName(
    RuntimeAircraftProductionMatchVeto veto
);
