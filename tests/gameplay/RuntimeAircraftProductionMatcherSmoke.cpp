#include "simulation/ai/RuntimeAircraftProductionMatcher.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Aircraft production matcher smoke failure: "
                  << message << '\n';
    }
    return condition;
}

RuntimeAircraftProductionSlot slot(
    const std::string& owner,
    int hostId,
    int dockIndex,
    std::vector<std::string> compatibleTypes,
    bool powered = true,
    bool available = true,
    bool alive = true
) {
    RuntimeAircraftProductionSlot value;
    value.canonicalOwner = owner;
    value.hostId = hostId;
    value.dockIndex = dockIndex;
    value.compatibleAircraftTypeIds = std::move(compatibleTypes);
    value.available = available;
    value.alive = alive;
    value.poweredOperational = powered;
    return value;
}

RuntimeAircraftProductionOrder order(
    std::uint64_t serial,
    const std::string& typeId,
    RuntimeAircraftProductionOrderPhase phase
) {
    RuntimeAircraftProductionOrder value;
    value.serial = serial;
    value.typeId = typeId;
    value.phase = phase;
    return value;
}

RuntimeAircraftProductionLiveOccupancy occupancy(
    const std::string& owner,
    int aircraftId,
    int hostId,
    int dockIndex,
    bool airborne
) {
    RuntimeAircraftProductionLiveOccupancy value;
    value.canonicalOwner = owner;
    value.aircraftId = aircraftId;
    value.hostId = hostId;
    value.dockIndex = dockIndex;
    value.airborne = airborne;
    return value;
}

RuntimeAircraftProductionFlexibleLiveAircraft flexibleLive(
    const std::string& owner,
    int aircraftId,
    const std::string& typeId,
    bool airborne,
    std::optional<RuntimeAircraftProductionSlotReference> priorSlot =
        std::nullopt
) {
    RuntimeAircraftProductionFlexibleLiveAircraft value;
    value.canonicalOwner = owner;
    value.aircraftId = aircraftId;
    value.typeId = typeId;
    value.airborne = airborne;
    value.priorSlot = priorSlot;
    return value;
}

const RuntimeAircraftProductionLiveAssignment* liveAssignmentFor(
    const RuntimeAircraftProductionRecoveryResult& result,
    int aircraftId
) {
    const auto found = std::find_if(
        result.liveAssignments.begin(),
        result.liveAssignments.end(),
        [aircraftId](const auto& assignment) {
            return assignment.aircraftId == aircraftId;
        }
    );
    return found == result.liveAssignments.end() ? nullptr : &*found;
}

const RuntimeAircraftProductionAssignment* retainedOrderAssignmentFor(
    const RuntimeAircraftProductionRecoveryResult& result,
    std::uint64_t serial
) {
    const auto found = std::find_if(
        result.retainedOrderAssignments.begin(),
        result.retainedOrderAssignments.end(),
        [serial](const auto& assignment) {
            return assignment.orderSerial == serial;
        }
    );
    return found == result.retainedOrderAssignments.end() ? nullptr : &*found;
}

const RuntimeAircraftProductionAssignment* assignmentFor(
    const RuntimeAircraftProductionMatchResult& result,
    std::uint64_t serial
) {
    const auto found = std::find_if(
        result.assignments.begin(),
        result.assignments.end(),
        [serial](const auto& assignment) {
            return assignment.orderSerial == serial;
        }
    );
    return found == result.assignments.end() ? nullptr : &*found;
}

bool testMaximumMatchingAndStableOrdering() {
    RuntimeAircraftProductionMatchInput input;
    input.canonicalOwner = "alpha";
    // AMRADR has the lower host id and would greedily consume ORCA first.
    // GAAIRC can take ORCA but not BEAG, so a complete solution must move
    // ORCA to GAAIRC and leave AMRADR to BEAG.
    input.slots = {
        slot("alpha", 200, 0, {"ORCA"}),
        slot("alpha", 100, 0, {"ORCA", "BEAG"})
    };
    // Reverse vector order proves that serial, not queue container order,
    // defines deterministic matching order.
    input.orders = {
        order(20, "BEAG", RuntimeAircraftProductionOrderPhase::Ready),
        order(10, "orca", RuntimeAircraftProductionOrderPhase::Active)
    };
    const RuntimeAircraftProductionMatchInput original = input;
    const RuntimeAircraftProductionMatchResult result =
        runtimeMatchAircraftProductionDocks(input);
    const RuntimeAircraftProductionAssignment* orca = assignmentFor(result, 10);
    const RuntimeAircraftProductionAssignment* blackEagle =
        assignmentFor(result, 20);
    bool success = expect(
        result.inputValid && result.feasible
            && result.veto == RuntimeAircraftProductionMatchVeto::None
            && result.assignments.size() == 2U
            && result.assignments[0].orderSerial == 10
            && result.assignments[1].orderSerial == 20
            && orca != nullptr && orca->hostId == 200
            && orca->dockIndex == 0
            && blackEagle != nullptr && blackEagle->hostId == 100
            && blackEagle->dockIndex == 0,
        "maximum matching did not resolve the ORCA/BEAG heterogeneous docks"
    );
    success &= expect(input == original, "matcher mutated its input");

    RuntimeAircraftProductionMatchInput permuted = input;
    std::reverse(permuted.slots.begin(), permuted.slots.end());
    std::reverse(permuted.orders.begin(), permuted.orders.end());
    success &= expect(
        runtimeMatchAircraftProductionDocks(permuted) == result
            && runtimeMatchAircraftProductionDocks(input) == result,
        "slot/order input permutation changed deterministic output"
    );
    return success;
}

bool testProspectiveOrder() {
    RuntimeAircraftProductionMatchInput input;
    input.canonicalOwner = "alpha";
    input.slots = {
        slot("alpha", 100, 0, {"ORCA", "BEAG"}),
        slot("alpha", 200, 0, {"ORCA"})
    };
    input.orders = {
        order(10, "ORCA", RuntimeAircraftProductionOrderPhase::Queued)
    };
    input.prospectiveOrder = order(
        20,
        "BEAG",
        RuntimeAircraftProductionOrderPhase::Prospective
    );
    const RuntimeAircraftProductionMatchResult result =
        runtimeMatchAircraftProductionDocks(input);
    const RuntimeAircraftProductionAssignment* prospective =
        assignmentFor(result, 20);
    bool success = expect(
        result.inputValid && result.feasible
            && result.assignments.size() == 2U
            && prospective != nullptr
            && prospective->phase
                == RuntimeAircraftProductionOrderPhase::Prospective
            && prospective->hostId == 100,
        "prospective order was not included atomically in matching"
    );

    RuntimeAircraftProductionMatchInput rejected;
    rejected.canonicalOwner = "alpha";
    rejected.slots = {
        slot("alpha", 100, 0, {"ORCA", "BEAG"})
    };
    rejected.orders = {
        order(10, "ORCA", RuntimeAircraftProductionOrderPhase::Active)
    };
    rejected.prospectiveOrder = order(
        20,
        "BEAG",
        RuntimeAircraftProductionOrderPhase::Prospective
    );
    const RuntimeAircraftProductionMatchResult prospectiveRejected =
        runtimeMatchAircraftProductionDocks(rejected);
    success &= expect(
        prospectiveRejected.inputValid && !prospectiveRejected.feasible
            && prospectiveRejected.assignments.empty()
            && prospectiveRejected.vetoOrderSerial == 20
            && prospectiveRejected.vetoTypeId == "BEAG",
        "prospective capacity veto did not identify the candidate order"
    );
    return success;
}

bool testOccupancyAndPowerContract() {
    bool success = true;
    RuntimeAircraftProductionMatchInput occupied;
    occupied.canonicalOwner = "alpha";
    occupied.slots = {
        slot("alpha", 300, 0, {"ORCA"}),
        slot("alpha", 300, 1, {"ORCA"}),
        slot("alpha", 300, 2, {"ORCA"})
    };
    occupied.liveOccupancies = {
        occupancy("alpha", 900, 300, 0, false),
        occupancy("alpha", 901, 300, 1, true)
    };
    occupied.orders = {
        order(1, "ORCA", RuntimeAircraftProductionOrderPhase::Queued)
    };
    const RuntimeAircraftProductionMatchResult oneFree =
        runtimeMatchAircraftProductionDocks(occupied);
    const RuntimeAircraftProductionAssignment* assignment =
        assignmentFor(oneFree, 1);
    success &= expect(
        oneFree.feasible && assignment != nullptr
            && assignment->hostId == 300 && assignment->dockIndex == 2,
        "docked and airborne live aircraft did not both reserve their slots"
    );
    RuntimeAircraftProductionMatchInput occupancyPermuted = occupied;
    std::reverse(
        occupancyPermuted.liveOccupancies.begin(),
        occupancyPermuted.liveOccupancies.end()
    );
    success &= expect(
        runtimeMatchAircraftProductionDocks(occupancyPermuted) == oneFree,
        "live occupancy input order changed deterministic output"
    );
    occupied.prospectiveOrder = order(
        2,
        "ORCA",
        RuntimeAircraftProductionOrderPhase::Prospective
    );
    const RuntimeAircraftProductionMatchResult full =
        runtimeMatchAircraftProductionDocks(occupied);
    success &= expect(
        full.inputValid && !full.feasible && full.assignments.empty()
            && full.veto
                == RuntimeAircraftProductionMatchVeto::InsufficientPhysicalSlots,
        "reserved slots did not veto excess production"
    );

    RuntimeAircraftProductionMatchInput unpowered;
    unpowered.canonicalOwner = "alpha";
    unpowered.slots = {
        slot("alpha", 400, 0, {"ORCA"}, false)
    };
    unpowered.prospectiveOrder = order(
        3,
        "ORCA",
        RuntimeAircraftProductionOrderPhase::Prospective
    );
    const RuntimeAircraftProductionMatchResult noPower =
        runtimeMatchAircraftProductionDocks(unpowered);
    success &= expect(
        noPower.feasible && noPower.assignments.size() == 1U
            && noPower.assignments[0].hostId == 400,
        "unpowered host incorrectly lost physical dock capacity"
    );

    unpowered.slots[0].available = false;
    const RuntimeAircraftProductionMatchResult unavailable =
        runtimeMatchAircraftProductionDocks(unpowered);
    success &= expect(
        unavailable.inputValid && !unavailable.feasible
            && unavailable.veto
                == RuntimeAircraftProductionMatchVeto::InsufficientPhysicalSlots,
        "unavailable slot incorrectly remained physical capacity"
    );
    unpowered.slots[0].available = true;
    unpowered.slots[0].alive = false;
    const RuntimeAircraftProductionMatchResult destroyed =
        runtimeMatchAircraftProductionDocks(unpowered);
    success &= expect(
        destroyed.inputValid && !destroyed.feasible
            && destroyed.veto
                == RuntimeAircraftProductionMatchVeto::InsufficientPhysicalSlots,
        "dead host slot incorrectly remained physical capacity"
    );
    return success;
}

bool testOwnerIsolation() {
    bool success = true;
    RuntimeAircraftProductionMatchInput foreignSlot;
    foreignSlot.canonicalOwner = "alpha";
    foreignSlot.slots = {
        slot("beta", 100, 0, {"ORCA"})
    };
    foreignSlot.orders = {
        order(1, "ORCA", RuntimeAircraftProductionOrderPhase::Queued)
    };
    const RuntimeAircraftProductionMatchResult slotResult =
        runtimeMatchAircraftProductionDocks(foreignSlot);
    success &= expect(
        !slotResult.inputValid && !slotResult.feasible
            && slotResult.assignments.empty()
            && slotResult.veto
                == RuntimeAircraftProductionMatchVeto::OwnerMismatch,
        "foreign-owner slot was not strictly rejected"
    );

    RuntimeAircraftProductionMatchInput foreignOccupancy;
    foreignOccupancy.canonicalOwner = "alpha";
    foreignOccupancy.slots = {
        slot("alpha", 100, 0, {"ORCA"})
    };
    foreignOccupancy.liveOccupancies = {
        occupancy("beta", 900, 100, 0, true)
    };
    const RuntimeAircraftProductionMatchResult occupancyResult =
        runtimeMatchAircraftProductionDocks(foreignOccupancy);
    success &= expect(
        !occupancyResult.inputValid
            && occupancyResult.veto
                == RuntimeAircraftProductionMatchVeto::OwnerMismatch,
        "foreign-owner occupancy was not strictly rejected"
    );
    return success;
}

bool testAtomicInfeasibleResult() {
    RuntimeAircraftProductionMatchInput input;
    input.canonicalOwner = "alpha";
    input.slots = {
        slot("alpha", 100, 0, {"ORCA", "BEAG"}),
        slot("alpha", 200, 0, {"ZEP"})
    };
    input.orders = {
        order(1, "ORCA", RuntimeAircraftProductionOrderPhase::Queued),
        order(2, "BEAG", RuntimeAircraftProductionOrderPhase::Ready)
    };
    const RuntimeAircraftProductionMatchInput original = input;
    const RuntimeAircraftProductionMatchResult first =
        runtimeMatchAircraftProductionDocks(input);
    const RuntimeAircraftProductionMatchResult second =
        runtimeMatchAircraftProductionDocks(input);
    return expect(
        input == original && first == second
            && first.inputValid && !first.feasible
            && first.assignments.empty()
            && first.veto
                == RuntimeAircraftProductionMatchVeto::NoFeasibleAssignment,
        "infeasible Hall conflict was not atomic and deterministic"
    );
}

bool testMaliciousAndDuplicateInputs() {
    bool success = true;
    RuntimeAircraftProductionMatchInput base;
    base.canonicalOwner = "alpha";
    base.slots = {
        slot("alpha", 100, 0, {"ORCA"}),
        slot("alpha", 100, 1, {"BEAG"})
    };
    base.orders = {
        order(1, "ORCA", RuntimeAircraftProductionOrderPhase::Queued)
    };

    auto duplicateSlot = base;
    duplicateSlot.slots.push_back(slot("alpha", 100, 0, {"BEAG"}));
    success &= expect(
        runtimeMatchAircraftProductionDocks(duplicateSlot).veto
            == RuntimeAircraftProductionMatchVeto::DuplicateSlot,
        "duplicate physical slot was accepted"
    );

    auto duplicateSerial = base;
    duplicateSerial.orders.push_back(order(
        1,
        "BEAG",
        RuntimeAircraftProductionOrderPhase::Active
    ));
    success &= expect(
        runtimeMatchAircraftProductionDocks(duplicateSerial).veto
            == RuntimeAircraftProductionMatchVeto::DuplicateOrderSerial,
        "duplicate order serial was accepted"
    );

    auto duplicateType = base;
    duplicateType.slots[0].compatibleAircraftTypeIds = {"ORCA", "orca"};
    success &= expect(
        runtimeMatchAircraftProductionDocks(duplicateType).veto
            == RuntimeAircraftProductionMatchVeto::DuplicateCompatibleType,
        "case-equivalent duplicate compatible type was accepted"
    );

    auto badOwner = base;
    badOwner.canonicalOwner = " alpha";
    success &= expect(
        runtimeMatchAircraftProductionDocks(badOwner).veto
            == RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner,
        "non-canonical owner was accepted"
    );

    auto unknownOccupancy = base;
    unknownOccupancy.liveOccupancies = {
        occupancy("alpha", 900, 999, 0, false)
    };
    success &= expect(
        runtimeMatchAircraftProductionDocks(unknownOccupancy).veto
            == RuntimeAircraftProductionMatchVeto::OccupancySlotMissing,
        "occupancy of an unknown slot was accepted"
    );

    auto duplicateOccupiedSlot = base;
    duplicateOccupiedSlot.liveOccupancies = {
        occupancy("alpha", 900, 100, 0, false),
        occupancy("alpha", 901, 100, 0, true)
    };
    success &= expect(
        runtimeMatchAircraftProductionDocks(duplicateOccupiedSlot).veto
            == RuntimeAircraftProductionMatchVeto::DuplicateOccupancySlot,
        "duplicate live slot occupancy was accepted"
    );

    auto duplicateAircraft = base;
    duplicateAircraft.liveOccupancies = {
        occupancy("alpha", 900, 100, 0, false),
        occupancy("alpha", 900, 100, 1, true)
    };
    success &= expect(
        runtimeMatchAircraftProductionDocks(duplicateAircraft).veto
            == RuntimeAircraftProductionMatchVeto::DuplicateOccupancyAircraft,
        "one aircraft occupying two slots was accepted"
    );

    auto badOrder = base;
    badOrder.orders[0].typeId = "BAD TYPE";
    success &= expect(
        runtimeMatchAircraftProductionDocks(badOrder).veto
            == RuntimeAircraftProductionMatchVeto::InvalidOrder,
        "order with an invalid type id was accepted"
    );

    auto badProspective = base;
    badProspective.prospectiveOrder = order(
        2,
        "BEAG",
        RuntimeAircraftProductionOrderPhase::Queued
    );
    success &= expect(
        runtimeMatchAircraftProductionDocks(badProspective).veto
            == RuntimeAircraftProductionMatchVeto::InvalidOrder,
        "prospective order with a non-prospective phase was accepted"
    );

    auto zeroProspective = base;
    zeroProspective.prospectiveOrder = order(
        0,
        "BEAG",
        RuntimeAircraftProductionOrderPhase::Prospective
    );
    success &= expect(
        runtimeMatchAircraftProductionDocks(zeroProspective).veto
            == RuntimeAircraftProductionMatchVeto::InvalidOrder,
        "zero-serial prospective order was accepted"
    );

    auto duplicateProspective = base;
    duplicateProspective.prospectiveOrder = order(
        1,
        "BEAG",
        RuntimeAircraftProductionOrderPhase::Prospective
    );
    success &= expect(
        runtimeMatchAircraftProductionDocks(duplicateProspective).veto
            == RuntimeAircraftProductionMatchVeto::DuplicateOrderSerial,
        "duplicate prospective serial was accepted"
    );

    auto incompatible = base;
    incompatible.orders[0].typeId = "ZEP";
    const RuntimeAircraftProductionMatchResult incompatibleResult =
        runtimeMatchAircraftProductionDocks(incompatible);
    success &= expect(
        incompatibleResult.inputValid && !incompatibleResult.feasible
            && incompatibleResult.veto
                == RuntimeAircraftProductionMatchVeto::IncompatibleAircraftType
            && incompatibleResult.vetoOrderSerial == 1,
        "incompatible order did not receive a precise capacity veto"
    );

    RuntimeAircraftProductionMatchInput oversized;
    oversized.canonicalOwner = "alpha";
    oversized.slots.resize(runtimeAircraftProductionMaximumSlots + 1U);
    success &= expect(
        runtimeMatchAircraftProductionDocks(oversized).veto
            == RuntimeAircraftProductionMatchVeto::InputLimitExceeded,
        "oversized matcher input was accepted"
    );

    auto oversizedTypes = base;
    oversizedTypes.slots[0].compatibleAircraftTypeIds.assign(
        runtimeAircraftProductionMaximumCompatibleTypesPerSlot + 1U,
        "ORCA"
    );
    success &= expect(
        runtimeMatchAircraftProductionDocks(oversizedTypes).veto
            == RuntimeAircraftProductionMatchVeto::InputLimitExceeded,
        "oversized slot compatibility list was accepted"
    );
    return success;
}

bool testRecoveryLivePriorityAndSerialRetention() {
    RuntimeAircraftProductionRecoveryInput input;
    input.canonicalOwner = "alpha";
    // FLEX starts with a valid but strategically bad reservation on the
    // shared pad. Retaining the lower-serial STRICT order requires moving the
    // live aircraft to the FLEX-only pad; a fixed occupancy mask cannot find
    // this solution.
    input.slots = {
        slot("alpha", 200, 0, {"FLEX"}),
        slot("alpha", 100, 0, {"FLEX", "STRICT"})
    };
    input.flexibleLiveAircraft = {
        flexibleLive(
            "alpha",
            900,
            "flex",
            true,
            RuntimeAircraftProductionSlotReference{100, 0}
        )
    };
    input.orders = {
        order(20, "FLEX", RuntimeAircraftProductionOrderPhase::Active),
        order(10, "STRICT", RuntimeAircraftProductionOrderPhase::Ready)
    };
    const RuntimeAircraftProductionRecoveryInput original = input;
    const RuntimeAircraftProductionRecoveryResult result =
        runtimeRecoverAircraftProductionDocks(input);
    const RuntimeAircraftProductionLiveAssignment* live =
        liveAssignmentFor(result, 900);
    const RuntimeAircraftProductionAssignment* strict =
        retainedOrderAssignmentFor(result, 10);
    bool success = expect(
        result.inputValid && !result.complete
            && result.maximumLiveMatched == 1U
            && result.liveAssignments.size() == 1U
            && result.unassignedLiveAircraftIds.empty()
            && result.retainedOrderAssignments.size() == 1U
            && result.rejectedOrderSerials
                == std::vector<std::uint64_t>{20U}
            && result.veto
                == RuntimeAircraftProductionMatchVeto::InsufficientPhysicalSlots
            && live != nullptr && live->hostId == 200 && live->dockIndex == 0
            && live->typeId == "flex" && live->airborne
            && strict != nullptr && strict->hostId == 100
            && strict->dockIndex == 0
            && strict->phase == RuntimeAircraftProductionOrderPhase::Ready,
        "recovery did not move live FLEX and retain the lower-serial STRICT order"
    );
    success &= expect(input == original, "recovery matcher mutated its input");

    RuntimeAircraftProductionRecoveryInput permuted = input;
    std::reverse(permuted.slots.begin(), permuted.slots.end());
    std::reverse(
        permuted.flexibleLiveAircraft.begin(),
        permuted.flexibleLiveAircraft.end()
    );
    std::reverse(permuted.orders.begin(), permuted.orders.end());
    success &= expect(
        runtimeRecoverAircraftProductionDocks(permuted) == result
            && runtimeRecoverAircraftProductionDocks(input) == result,
        "recovery input permutation changed deterministic output"
    );
    return success;
}

bool testRecoveryTrueLiveShortageAndHardOccupancy() {
    bool success = true;
    RuntimeAircraftProductionRecoveryInput shortage;
    shortage.canonicalOwner = "alpha";
    shortage.slots = {
        slot("alpha", 300, 0, {"ORCA"})
    };
    shortage.flexibleLiveAircraft = {
        flexibleLive("alpha", 902, "ORCA", false),
        flexibleLive("alpha", 901, "ORCA", true)
    };
    shortage.orders = {
        order(5, "ORCA", RuntimeAircraftProductionOrderPhase::Active)
    };
    const RuntimeAircraftProductionRecoveryResult result =
        runtimeRecoverAircraftProductionDocks(shortage);
    const RuntimeAircraftProductionLiveAssignment* retainedLive =
        liveAssignmentFor(result, 901);
    success &= expect(
        result.inputValid && !result.complete
            && result.maximumLiveMatched == 1U
            && retainedLive != nullptr && retainedLive->hostId == 300
            && result.unassignedLiveAircraftIds == std::vector<int>{902}
            && result.retainedOrderAssignments.empty()
            && result.rejectedOrderSerials
                == std::vector<std::uint64_t>{5U}
            && result.veto
                == RuntimeAircraftProductionMatchVeto::InsufficientPhysicalSlots,
        "recovery did not give true physical shortage to live aircraft first"
    );

    RuntimeAircraftProductionRecoveryInput hard;
    hard.canonicalOwner = "alpha";
    hard.slots = {
        slot("alpha", 400, 0, {"ORCA"}),
        slot("alpha", 400, 1, {"ORCA"})
    };
    hard.liveOccupancies = {
        occupancy("alpha", 800, 400, 0, true)
    };
    hard.flexibleLiveAircraft = {
        flexibleLive("alpha", 801, "ORCA", false)
    };
    hard.orders = {
        order(1, "ORCA", RuntimeAircraftProductionOrderPhase::Queued)
    };
    const RuntimeAircraftProductionRecoveryResult hardResult =
        runtimeRecoverAircraftProductionDocks(hard);
    const RuntimeAircraftProductionLiveAssignment* flexible =
        liveAssignmentFor(hardResult, 801);
    success &= expect(
        hardResult.inputValid && !hardResult.complete
            && hardResult.maximumLiveMatched == 1U
            && flexible != nullptr && flexible->hostId == 400
            && flexible->dockIndex == 1
            && hardResult.rejectedOrderSerials
                == std::vector<std::uint64_t>{1U},
        "hard occupancy did not remain fixed ahead of flexible recovery demand"
    );
    return success;
}

bool testRecoveryInvalidInputIsAtomic() {
    RuntimeAircraftProductionRecoveryInput invalid;
    invalid.canonicalOwner = "alpha";
    invalid.slots = {
        slot("alpha", 500, 0, {"ORCA"})
    };
    invalid.flexibleLiveAircraft = {
        flexibleLive("alpha", 900, "BAD TYPE", true)
    };
    invalid.orders = {
        order(1, "ORCA", RuntimeAircraftProductionOrderPhase::Active)
    };
    const RuntimeAircraftProductionRecoveryInput original = invalid;
    const RuntimeAircraftProductionRecoveryResult badType =
        runtimeRecoverAircraftProductionDocks(invalid);
    bool success = expect(
        invalid == original && !badType.inputValid && !badType.complete
            && badType.veto
                == RuntimeAircraftProductionMatchVeto::InvalidFlexibleLiveAircraft
            && badType.maximumLiveMatched == 0U
            && badType.liveAssignments.empty()
            && badType.unassignedLiveAircraftIds.empty()
            && badType.retainedOrderAssignments.empty()
            && badType.rejectedOrderSerials.empty(),
        "invalid flexible live input produced a partial recovery projection"
    );

    RuntimeAircraftProductionRecoveryInput duplicate = invalid;
    duplicate.flexibleLiveAircraft = {
        flexibleLive("alpha", 901, "ORCA", false),
        flexibleLive("alpha", 901, "ORCA", true)
    };
    const RuntimeAircraftProductionRecoveryResult duplicateResult =
        runtimeRecoverAircraftProductionDocks(duplicate);
    success &= expect(
        !duplicateResult.inputValid
            && duplicateResult.veto
                == RuntimeAircraftProductionMatchVeto::DuplicateLiveAircraft
            && duplicateResult.liveAssignments.empty()
            && duplicateResult.retainedOrderAssignments.empty(),
        "duplicate flexible live id was not rejected atomically"
    );

    RuntimeAircraftProductionRecoveryInput missingHardSlot;
    missingHardSlot.canonicalOwner = "alpha";
    missingHardSlot.slots = {
        slot("alpha", 500, 0, {"ORCA"})
    };
    missingHardSlot.liveOccupancies = {
        occupancy("alpha", 902, 999, 0, false)
    };
    const RuntimeAircraftProductionRecoveryResult missing =
        runtimeRecoverAircraftProductionDocks(missingHardSlot);
    success &= expect(
        !missing.inputValid
            && missing.veto
                == RuntimeAircraftProductionMatchVeto::OccupancySlotMissing
            && missing.liveAssignments.empty()
            && missing.unassignedLiveAircraftIds.empty(),
        "unknown hard occupancy slot was not rejected atomically"
    );
    return success;
}

bool testRecoveryRejectedEarlyOrderDoesNotTriggerSaturation() {
    RuntimeAircraftProductionRecoveryInput input;
    input.canonicalOwner = "alpha";
    input.slots = {
        slot("alpha", 600, 0, {"ORCA"}),
        slot("alpha", 600, 1, {"BEAG"})
    };
    input.orders = {
        order(1, "ZEP", RuntimeAircraftProductionOrderPhase::Active),
        order(2, "ORCA", RuntimeAircraftProductionOrderPhase::Queued),
        order(3, "BEAG", RuntimeAircraftProductionOrderPhase::Ready)
    };
    const RuntimeAircraftProductionRecoveryResult result =
        runtimeRecoverAircraftProductionDocks(input);
    const RuntimeAircraftProductionAssignment* orca =
        retainedOrderAssignmentFor(result, 2);
    const RuntimeAircraftProductionAssignment* beag =
        retainedOrderAssignmentFor(result, 3);
    return expect(
        result.inputValid && !result.complete
            && result.maximumLiveMatched == 0U
            && result.rejectedOrderSerials
                == std::vector<std::uint64_t>{1U}
            && result.retainedOrderAssignments.size() == 2U
            && orca != nullptr && orca->hostId == 600
            && orca->dockIndex == 0
            && beag != nullptr && beag->hostId == 600
            && beag->dockIndex == 1,
        "an incompatible early order incorrectly saturated recovery matching"
    );
}

bool testRecoveryIndividualDemandLimitIsAtomic() {
    RuntimeAircraftProductionRecoveryInput input;
    input.canonicalOwner = "alpha";
    input.orders.reserve(
        runtimeAircraftProductionMaximumOrders + 1U
    );
    for (std::size_t index = 0;
        index <= runtimeAircraftProductionMaximumOrders;
        ++index) {
        input.orders.push_back(order(
            static_cast<std::uint64_t>(index + 1U),
            "ORCA",
            RuntimeAircraftProductionOrderPhase::Queued
        ));
    }
    const RuntimeAircraftProductionRecoveryInput original = input;
    const RuntimeAircraftProductionRecoveryResult result =
        runtimeRecoverAircraftProductionDocks(input);
    bool success = expect(
        input == original && !result.inputValid && !result.complete
            && result.veto
                == RuntimeAircraftProductionMatchVeto::InputLimitExceeded
            && result.maximumLiveMatched == 0U
            && result.liveAssignments.empty()
            && result.unassignedLiveAircraftIds.empty()
            && result.retainedOrderAssignments.empty()
            && result.rejectedOrderSerials.empty(),
        "oversized recovery demand produced a partial projection"
    );

    RuntimeAircraftProductionRecoveryInput oversizedLive;
    oversizedLive.canonicalOwner = "alpha";
    oversizedLive.flexibleLiveAircraft.reserve(
        runtimeAircraftProductionMaximumFlexibleLiveAircraft + 1U
    );
    for (std::size_t index = 0;
        index <= runtimeAircraftProductionMaximumFlexibleLiveAircraft;
        ++index) {
        oversizedLive.flexibleLiveAircraft.push_back(flexibleLive(
            "alpha",
            static_cast<int>(200000U + index),
            "ORCA",
            true
        ));
    }
    const RuntimeAircraftProductionRecoveryResult oversizedLiveResult =
        runtimeRecoverAircraftProductionDocks(oversizedLive);
    success &= expect(
        !oversizedLiveResult.inputValid
            && oversizedLiveResult.veto
                == RuntimeAircraftProductionMatchVeto::InputLimitExceeded
            && oversizedLiveResult.liveAssignments.empty()
            && oversizedLiveResult.unassignedLiveAircraftIds.empty()
            && oversizedLiveResult.retainedOrderAssignments.empty()
            && oversizedLiveResult.rejectedOrderSerials.empty(),
        "oversized flexible live input was not rejected atomically"
    );
    return success;
}

bool testRecoveryIndependentMaximumDemandsAndLargeMatching() {
    RuntimeAircraftProductionRecoveryInput mixed;
    mixed.canonicalOwner = "alpha";
    mixed.slots = {slot("alpha", 700, 0, {"ORCA"})};
    mixed.flexibleLiveAircraft.reserve(
        runtimeAircraftProductionMaximumFlexibleLiveAircraft
    );
    for (std::size_t index = 0;
        index < runtimeAircraftProductionMaximumFlexibleLiveAircraft;
        ++index) {
        mixed.flexibleLiveAircraft.push_back(flexibleLive(
            "alpha",
            static_cast<int>(10000U + index),
            "NO_COMPATIBLE_PAD",
            true
        ));
    }
    mixed.orders = {
        order(1, "ORCA", RuntimeAircraftProductionOrderPhase::Active)
    };
    const RuntimeAircraftProductionRecoveryResult mixedResult =
        runtimeRecoverAircraftProductionDocks(mixed);
    bool success = expect(
        mixedResult.inputValid && !mixedResult.complete
            && mixedResult.maximumLiveMatched == 0U
            && mixedResult.unassignedLiveAircraftIds.size()
                == runtimeAircraftProductionMaximumFlexibleLiveAircraft
            && mixedResult.retainedOrderAssignments.size() == 1U
            && mixedResult.retainedOrderAssignments.front().orderSerial == 1U
            && mixedResult.rejectedOrderSerials.empty(),
        "independently legal live and order maxima hit a combined cap"
    );

    RuntimeAircraftProductionRecoveryInput denseOrders;
    denseOrders.canonicalOwner = "alpha";
    denseOrders.slots.reserve(runtimeAircraftProductionMaximumSlots);
    denseOrders.orders.reserve(runtimeAircraftProductionMaximumOrders);
    for (std::size_t index = 0;
        index < runtimeAircraftProductionMaximumSlots;
        ++index) {
        denseOrders.slots.push_back(slot(
            "alpha",
            static_cast<int>(20000U + index),
            0,
            {"COMMON"}
        ));
        denseOrders.orders.push_back(order(
            static_cast<std::uint64_t>(index + 1U),
            "COMMON",
            RuntimeAircraftProductionOrderPhase::Queued
        ));
    }
    const RuntimeAircraftProductionRecoveryResult denseResult =
        runtimeRecoverAircraftProductionDocks(denseOrders);
    success &= expect(
        denseResult.inputValid && denseResult.complete
            && denseResult.retainedOrderAssignments.size()
                == runtimeAircraftProductionMaximumOrders
            && denseResult.rejectedOrderSerials.empty(),
        "maximum same-type recovery orders did not complete"
    );

    RuntimeAircraftProductionRecoveryInput denseLive;
    denseLive.canonicalOwner = "alpha";
    denseLive.slots = denseOrders.slots;
    denseLive.flexibleLiveAircraft.reserve(
        runtimeAircraftProductionMaximumFlexibleLiveAircraft
    );
    for (std::size_t index = 0;
        index < runtimeAircraftProductionMaximumFlexibleLiveAircraft;
        ++index) {
        denseLive.flexibleLiveAircraft.push_back(flexibleLive(
            "alpha",
            static_cast<int>(400000U + index),
            "COMMON",
            true
        ));
    }
    const RuntimeAircraftProductionRecoveryResult denseLiveResult =
        runtimeRecoverAircraftProductionDocks(denseLive);
    success &= expect(
        denseLiveResult.inputValid && !denseLiveResult.complete
            && denseLiveResult.maximumLiveMatched
                == runtimeAircraftProductionMaximumSlots
            && denseLiveResult.liveAssignments.size()
                == runtimeAircraftProductionMaximumSlots
            && denseLiveResult.unassignedLiveAircraftIds.size()
                == runtimeAircraftProductionMaximumFlexibleLiveAircraft
                    - runtimeAircraftProductionMaximumSlots,
        "maximum flexible live input was not rank-compressed to pad capacity"
    );
    return success;
}

bool testRecoveryConnectedHallShortagesAtLimit() {
    constexpr std::size_t componentSlotCount = 128U;

    RuntimeAircraftProductionRecoveryInput liveShortage;
    liveShortage.canonicalOwner = "alpha";
    liveShortage.slots.reserve(runtimeAircraftProductionMaximumSlots);
    for (std::size_t index = 0;
        index < runtimeAircraftProductionMaximumSlots;
        ++index) {
        liveShortage.slots.push_back(slot(
            "alpha",
            static_cast<int>(40000U + index),
            0,
            {index < componentSlotCount ? "LIVE" : "OUT"}
        ));
    }
    liveShortage.flexibleLiveAircraft.reserve(
        runtimeAircraftProductionMaximumOccupancies
    );
    for (std::size_t index = 0;
        index < runtimeAircraftProductionMaximumOccupancies;
        ++index) {
        liveShortage.flexibleLiveAircraft.push_back(flexibleLive(
            "alpha",
            static_cast<int>(50000U + index),
            "LIVE",
            true
        ));
    }
    const RuntimeAircraftProductionRecoveryResult liveResult =
        runtimeRecoverAircraftProductionDocks(liveShortage);
    bool success = expect(
        liveResult.inputValid && !liveResult.complete
            && liveResult.maximumLiveMatched == componentSlotCount
            && liveResult.liveAssignments.size() == componentSlotCount
            && liveResult.unassignedLiveAircraftIds.size()
                == runtimeAircraftProductionMaximumOccupancies
                    - componentSlotCount,
        "maximum connected live Hall shortage was not bounded or maximal"
    );

    // Saturate one connected component while leaving unrelated slots free.
    // The later orders all have distinct type ids, so a root-only rejection
    // cache would repeatedly traverse the same exhausted alternating tree.
    RuntimeAircraftProductionRecoveryInput orderShortage;
    orderShortage.canonicalOwner = "alpha";
    orderShortage.slots.reserve(runtimeAircraftProductionMaximumSlots);
    const std::size_t futureOrderCount =
        runtimeAircraftProductionMaximumOrders - componentSlotCount;
    for (std::size_t slotIndex = 0;
        slotIndex < componentSlotCount;
        ++slotIndex) {
        std::vector<std::string> compatibleTypes;
        compatibleTypes.reserve(
            componentSlotCount
                + (futureOrderCount + componentSlotCount - 1U)
                    / componentSlotCount
        );
        for (std::size_t typeIndex = 0;
            typeIndex < componentSlotCount;
            ++typeIndex) {
            compatibleTypes.push_back("MATCHED_" + std::to_string(typeIndex));
        }
        for (std::size_t futureIndex = slotIndex;
            futureIndex < futureOrderCount;
            futureIndex += componentSlotCount) {
            compatibleTypes.push_back("FUTURE_" + std::to_string(futureIndex));
        }
        orderShortage.slots.push_back(slot(
            "alpha",
            static_cast<int>(60000U + slotIndex),
            0,
            std::move(compatibleTypes)
        ));
    }
    for (std::size_t slotIndex = componentSlotCount;
        slotIndex < runtimeAircraftProductionMaximumSlots;
        ++slotIndex) {
        orderShortage.slots.push_back(slot(
            "alpha",
            static_cast<int>(60000U + slotIndex),
            0,
            {"OUT"}
        ));
    }
    orderShortage.orders.reserve(runtimeAircraftProductionMaximumOrders);
    for (std::size_t index = 0; index < componentSlotCount; ++index) {
        orderShortage.orders.push_back(order(
            static_cast<std::uint64_t>(index + 1U),
            "MATCHED_" + std::to_string(index),
            RuntimeAircraftProductionOrderPhase::Queued
        ));
    }
    for (std::size_t index = 0; index < futureOrderCount; ++index) {
        orderShortage.orders.push_back(order(
            static_cast<std::uint64_t>(componentSlotCount + index + 1U),
            "FUTURE_" + std::to_string(index),
            RuntimeAircraftProductionOrderPhase::Queued
        ));
    }
    const RuntimeAircraftProductionRecoveryResult orderResult =
        runtimeRecoverAircraftProductionDocks(orderShortage);
    success &= expect(
        orderResult.inputValid && !orderResult.complete
            && orderResult.retainedOrderAssignments.size()
                == componentSlotCount
            && orderResult.rejectedOrderSerials.size() == futureOrderCount
            && !orderResult.rejectedOrderSerials.empty()
            && orderResult.rejectedOrderSerials.front()
                == componentSlotCount + 1U
            && orderResult.rejectedOrderSerials.back()
                == runtimeAircraftProductionMaximumOrders,
        "distinct rejected types repeatedly escaped a saturated Hall closure"
    );
    return success;
}

bool testPrimaryAirportPreference() {
    RuntimeAircraftProductionMatchInput input;
    input.canonicalOwner = "alpha";
    input.slots = {slot("alpha", 10, 0, {"ORCA"}), slot("alpha", 90, 0, {"ORCA"})};
    input.slots[1].preferredPrimary = true;
    input.orders = {order(1, "ORCA", RuntimeAircraftProductionOrderPhase::Active)};
    const auto preferred = runtimeMatchAircraftProductionDocks(input);
    bool success = expect(preferred.feasible && preferred.assignments.size() == 1
        && preferred.assignments[0].hostId == 90, "primary airport did not win a feasible equivalent slot");
    std::reverse(input.slots.begin(), input.slots.end());
    success &= expect(runtimeMatchAircraftProductionDocks(input) == preferred,
        "primary airport choice depends on input order");
    input.liveOccupancies = {occupancy("alpha", 501, 90, 0, true)};
    const auto occupied = runtimeMatchAircraftProductionDocks(input);
    success &= expect(occupied.feasible && occupied.assignments.size() == 1
        && occupied.assignments[0].hostId == 10, "primary preference displaced a live aircraft reservation");
    RuntimeAircraftProductionRecoveryInput recovery;
    recovery.canonicalOwner = input.canonicalOwner;
    recovery.slots = input.slots;
    recovery.orders = input.orders;
    const auto recovered = runtimeRecoverAircraftProductionDocks(recovery);
    success &= expect(recovered.complete && recovered.retainedOrderAssignments.size() == 1
        && recovered.retainedOrderAssignments[0].hostId == 90,
        "recovery ignored the primary airport for a retained order");
    return success;
}

bool testPrimaryAirportPowerFallback() {
    RuntimeAircraftProductionMatchInput input;
    input.canonicalOwner = "alpha";
    input.slots = {slot("alpha", 10, 0, {"ORCA"}, false),
        slot("alpha", 90, 0, {"ORCA"})};
    // The lower-id primary must not strand an active order while another
    // compatible, powered airport can produce it. A stale preferred flag
    // supplied by a caller must not override the power preference either.
    input.slots[0].preferredPrimary = true;
    input.orders = {order(1, "ORCA", RuntimeAircraftProductionOrderPhase::Active)};
    const auto fallback = runtimeMatchAircraftProductionDocks(input);
    bool success = expect(fallback.feasible && fallback.assignments.size() == 1
        && fallback.assignments[0].hostId == 90,
        "unpowered primary stranded an order instead of using a powered airport");
    std::reverse(input.slots.begin(), input.slots.end());
    success &= expect(runtimeMatchAircraftProductionDocks(input) == fallback,
        "powered fallback depends on slot input order");

    RuntimeAircraftProductionRecoveryInput recovery;
    recovery.canonicalOwner = input.canonicalOwner;
    recovery.slots = input.slots;
    recovery.orders = input.orders;
    const auto recovered = runtimeRecoverAircraftProductionDocks(recovery);
    success &= expect(recovered.complete && recovered.retainedOrderAssignments.size() == 1
        && recovered.retainedOrderAssignments[0].hostId == 90,
        "recovery stranded an order on an unpowered primary");

    // Power is a preference, not a capacity veto. The live reservation is
    // never displaced, and the second physical pad can retain a queued order.
    input.liveOccupancies = {occupancy("alpha", 501, 90, 0, true)};
    const auto capacity = runtimeMatchAircraftProductionDocks(input);
    success &= expect(capacity.feasible && capacity.assignments.size() == 1
        && capacity.assignments[0].hostId == 10,
        "power fallback displaced a live reservation or removed physical capacity");
    recovery.liveOccupancies = input.liveOccupancies;
    const auto recoveredCapacity = runtimeRecoverAircraftProductionDocks(recovery);
    success &= expect(recoveredCapacity.complete && recoveredCapacity.retainedOrderAssignments.size() == 1
        && recoveredCapacity.retainedOrderAssignments[0].hostId == 10,
        "recovery removed unpowered physical capacity");

    input.liveOccupancies.clear();
    for (auto& pad : input.slots) pad.poweredOperational = false;
    const auto blackout = runtimeMatchAircraftProductionDocks(input);
    success &= expect(blackout.feasible && blackout.assignments.size() == 1
        && blackout.assignments[0].hostId == 10,
        "all-airport blackout lost capacity or the deterministic primary preference");
    return success;
}

} // namespace

int main() {
    bool success = true;
    success &= testMaximumMatchingAndStableOrdering();
    success &= testPrimaryAirportPreference();
    success &= testPrimaryAirportPowerFallback();
    success &= testProspectiveOrder();
    success &= testOccupancyAndPowerContract();
    success &= testOwnerIsolation();
    success &= testAtomicInfeasibleResult();
    success &= testMaliciousAndDuplicateInputs();
    success &= testRecoveryLivePriorityAndSerialRetention();
    success &= testRecoveryTrueLiveShortageAndHardOccupancy();
    success &= testRecoveryInvalidInputIsAtomic();
    success &= testRecoveryRejectedEarlyOrderDoesNotTriggerSaturation();
    success &= testRecoveryIndividualDemandLimitIsAtomic();
    success &= testRecoveryIndependentMaximumDemandsAndLargeMatching();
    success &= testRecoveryConnectedHallShortagesAtLimit();
    RuntimeAircraftProductionMatchInput empty;
    empty.canonicalOwner = "alpha";
    const RuntimeAircraftProductionMatchResult emptyResult =
        runtimeMatchAircraftProductionDocks(empty);
    success &= expect(
        emptyResult.inputValid && emptyResult.feasible
            && emptyResult.veto == RuntimeAircraftProductionMatchVeto::None
            && emptyResult.assignments.empty(),
        "empty canonical input was not a feasible no-op"
    );
    success &= expect(
        std::string(runtimeAircraftProductionMatchVetoName(
            RuntimeAircraftProductionMatchVeto::NoFeasibleAssignment
        )) == "NoFeasibleAssignment",
        "veto diagnostic name drifted"
    );
    return success ? 0 : 1;
}
