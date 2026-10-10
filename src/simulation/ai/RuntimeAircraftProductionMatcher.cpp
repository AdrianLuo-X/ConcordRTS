#include "simulation/ai/RuntimeAircraftProductionMatcher.h"

#include <algorithm>
#include <iterator>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t maximumCanonicalOwnerBytes = 256;
constexpr std::size_t maximumAircraftTypeIdBytes = 256;
constexpr int maximumDockIndex = 65535;

using SlotKey = std::pair<int, int>;

struct PreparedSlot {
    RuntimeAircraftProductionSlot slot;
    std::vector<std::string> compatibleTypeKeys;
};

struct PreparedOrder {
    RuntimeAircraftProductionOrder order;
    std::string typeKey;
};

struct PreparedFlexibleLiveAircraft {
    RuntimeAircraftProductionFlexibleLiveAircraft aircraft;
    std::string typeKey;
};

bool asciiWhitespace(unsigned char value) {
    return value == ' '
        || value == '\t'
        || value == '\r'
        || value == '\n'
        || value == '\f'
        || value == '\v';
}

bool asciiControl(unsigned char value) {
    return value < 0x20U || value == 0x7fU;
}

bool validCanonicalOwner(const std::string& owner) {
    if (owner.empty()
        || owner.size() > maximumCanonicalOwnerBytes
        || asciiWhitespace(static_cast<unsigned char>(owner.front()))
        || asciiWhitespace(static_cast<unsigned char>(owner.back()))) {
        return false;
    }
    return std::none_of(
        owner.begin(),
        owner.end(),
        [](unsigned char character) { return asciiControl(character); }
    );
}

bool validAircraftTypeId(const std::string& typeId) {
    if (typeId.empty() || typeId.size() > maximumAircraftTypeIdBytes) {
        return false;
    }
    return std::none_of(
        typeId.begin(),
        typeId.end(),
        [](unsigned char character) {
            return asciiControl(character) || asciiWhitespace(character);
        }
    );
}

std::string aircraftTypeKey(std::string typeId) {
    std::transform(
        typeId.begin(),
        typeId.end(),
        typeId.begin(),
        [](unsigned char character) {
            return character >= 'a' && character <= 'z'
                ? static_cast<char>(character - 'a' + 'A')
                : static_cast<char>(character);
        }
    );
    return typeId;
}

bool validExistingOrderPhase(RuntimeAircraftProductionOrderPhase phase) {
    return phase == RuntimeAircraftProductionOrderPhase::Queued
        || phase == RuntimeAircraftProductionOrderPhase::Active
        || phase == RuntimeAircraftProductionOrderPhase::Ready;
}

RuntimeAircraftProductionMatchResult invalidResult(
    RuntimeAircraftProductionMatchVeto veto,
    std::string reason,
    std::uint64_t orderSerial = 0,
    std::string typeId = {}
) {
    RuntimeAircraftProductionMatchResult result;
    result.veto = veto;
    result.vetoOrderSerial = orderSerial;
    result.vetoTypeId = std::move(typeId);
    result.reason = std::move(reason);
    return result;
}

RuntimeAircraftProductionMatchResult infeasibleResult(
    RuntimeAircraftProductionMatchVeto veto,
    std::string reason,
    const PreparedOrder& order
) {
    RuntimeAircraftProductionMatchResult result;
    result.inputValid = true;
    result.veto = veto;
    result.vetoOrderSerial = order.order.serial;
    result.vetoTypeId = order.order.typeId;
    result.reason = std::move(reason);
    return result;
}

RuntimeAircraftProductionRecoveryResult invalidRecoveryResult(
    RuntimeAircraftProductionMatchVeto veto,
    std::string reason
) {
    RuntimeAircraftProductionRecoveryResult result;
    result.veto = veto;
    result.reason = std::move(reason);
    return result;
}

} // namespace

RuntimeAircraftProductionMatchResult runtimeMatchAircraftProductionDocks(
    const RuntimeAircraftProductionMatchInput& input
) {
    if (input.slots.size() > runtimeAircraftProductionMaximumSlots
        || input.orders.size() > runtimeAircraftProductionMaximumOrders
        || input.liveOccupancies.size()
            > runtimeAircraftProductionMaximumOccupancies
        || (input.prospectiveOrder.has_value()
            && input.orders.size()
                == runtimeAircraftProductionMaximumOrders)) {
        return invalidResult(
            RuntimeAircraftProductionMatchVeto::InputLimitExceeded,
            "aircraft production matcher input exceeds a size limit"
        );
    }
    if (!validCanonicalOwner(input.canonicalOwner)) {
        return invalidResult(
            RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner,
            "aircraft production matcher owner is not canonical"
        );
    }

    std::vector<RuntimeAircraftProductionSlot> sortedSlots = input.slots;
    std::sort(
        sortedSlots.begin(),
        sortedSlots.end(),
        [](const auto& left, const auto& right) {
            if (left.poweredOperational != right.poweredOperational)
                return left.poweredOperational;
            if (left.preferredPrimary != right.preferredPrimary)
                return left.preferredPrimary;
            return std::tie(
                left.hostId,
                left.dockIndex,
                left.canonicalOwner
            ) < std::tie(
                right.hostId,
                right.dockIndex,
                right.canonicalOwner
            );
        }
    );

    std::set<SlotKey> allSlotKeys;
    std::vector<PreparedSlot> preparedSlots;
    preparedSlots.reserve(sortedSlots.size());
    for (RuntimeAircraftProductionSlot& slot : sortedSlots) {
        if (!validCanonicalOwner(slot.canonicalOwner)) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner,
                "aircraft production slot owner is not canonical"
            );
        }
        if (slot.canonicalOwner != input.canonicalOwner) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::OwnerMismatch,
                "aircraft production slot belongs to another owner"
            );
        }
        if (slot.compatibleAircraftTypeIds.size()
            > runtimeAircraftProductionMaximumCompatibleTypesPerSlot) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::InputLimitExceeded,
                "aircraft production slot compatibility list exceeds "
                    "the size limit"
            );
        }
        if (slot.hostId < 0
            || slot.dockIndex < 0
            || slot.dockIndex > maximumDockIndex
            || slot.compatibleAircraftTypeIds.empty()) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::InvalidSlot,
                "aircraft production slot has invalid fields"
            );
        }
        const SlotKey slotKey{slot.hostId, slot.dockIndex};
        if (!allSlotKeys.insert(slotKey).second) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::DuplicateSlot,
                "aircraft production input repeats slot host="
                    + std::to_string(slot.hostId) + " dock="
                    + std::to_string(slot.dockIndex)
            );
        }

        std::set<std::string> uniqueTypeKeys;
        std::vector<std::string> compatibleTypeKeys;
        compatibleTypeKeys.reserve(slot.compatibleAircraftTypeIds.size());
        for (const std::string& typeId : slot.compatibleAircraftTypeIds) {
            if (!validAircraftTypeId(typeId)) {
                return invalidResult(
                    RuntimeAircraftProductionMatchVeto::InvalidCompatibleType,
                    "aircraft production slot has an invalid compatible type"
                );
            }
            std::string key = aircraftTypeKey(typeId);
            if (!uniqueTypeKeys.insert(key).second) {
                return invalidResult(
                    RuntimeAircraftProductionMatchVeto::DuplicateCompatibleType,
                    "aircraft production slot repeats compatible type "
                        + typeId
                );
            }
            compatibleTypeKeys.push_back(std::move(key));
        }
        std::sort(compatibleTypeKeys.begin(), compatibleTypeKeys.end());
        preparedSlots.push_back({std::move(slot), std::move(compatibleTypeKeys)});
    }

    std::vector<RuntimeAircraftProductionLiveOccupancy> sortedOccupancies =
        input.liveOccupancies;
    std::sort(
        sortedOccupancies.begin(),
        sortedOccupancies.end(),
        [](const auto& left, const auto& right) {
            return std::tie(left.hostId, left.dockIndex, left.aircraftId)
                < std::tie(right.hostId, right.dockIndex, right.aircraftId);
        }
    );
    std::set<SlotKey> occupiedSlots;
    std::set<int> occupyingAircraft;
    for (const RuntimeAircraftProductionLiveOccupancy& occupancy :
        sortedOccupancies) {
        if (!validCanonicalOwner(occupancy.canonicalOwner)) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner,
                "aircraft production occupancy owner is not canonical"
            );
        }
        if (occupancy.canonicalOwner != input.canonicalOwner) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::OwnerMismatch,
                "aircraft production occupancy belongs to another owner"
            );
        }
        if (occupancy.aircraftId < 0
            || occupancy.hostId < 0
            || occupancy.aircraftId == occupancy.hostId
            || occupancy.dockIndex < 0
            || occupancy.dockIndex > maximumDockIndex) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::InvalidOccupancy,
                "aircraft production live occupancy has invalid fields"
            );
        }
        const SlotKey slotKey{occupancy.hostId, occupancy.dockIndex};
        if (allSlotKeys.find(slotKey) == allSlotKeys.end()) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::OccupancySlotMissing,
                "aircraft production occupancy references an unknown slot"
            );
        }
        if (!occupiedSlots.insert(slotKey).second) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::DuplicateOccupancySlot,
                "aircraft production input repeats an occupied slot"
            );
        }
        if (!occupyingAircraft.insert(occupancy.aircraftId).second) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::DuplicateOccupancyAircraft,
                "aircraft production input repeats an occupying aircraft"
            );
        }
    }

    std::vector<PreparedOrder> preparedOrders;
    preparedOrders.reserve(
        input.orders.size() + (input.prospectiveOrder.has_value() ? 1U : 0U)
    );
    std::set<std::uint64_t> orderSerials;
    const auto appendOrder = [&](
        const RuntimeAircraftProductionOrder& order,
        bool prospective
    ) -> std::optional<RuntimeAircraftProductionMatchResult> {
        const bool validPhase = prospective
            ? order.phase == RuntimeAircraftProductionOrderPhase::Prospective
            : validExistingOrderPhase(order.phase);
        if (order.serial == 0
            || !validAircraftTypeId(order.typeId)
            || !validPhase) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::InvalidOrder,
                "aircraft production order has invalid fields",
                order.serial,
                order.typeId
            );
        }
        if (!orderSerials.insert(order.serial).second) {
            return invalidResult(
                RuntimeAircraftProductionMatchVeto::DuplicateOrderSerial,
                "aircraft production input repeats order serial "
                    + std::to_string(order.serial),
                order.serial,
                order.typeId
            );
        }
        preparedOrders.push_back({order, aircraftTypeKey(order.typeId)});
        return std::nullopt;
    };
    for (const RuntimeAircraftProductionOrder& order : input.orders) {
        if (const auto error = appendOrder(order, false)) {
            return *error;
        }
    }
    if (input.prospectiveOrder.has_value()) {
        if (const auto error = appendOrder(*input.prospectiveOrder, true)) {
            return *error;
        }
    }
    std::sort(
        preparedOrders.begin(),
        preparedOrders.end(),
        [](const auto& left, const auto& right) {
            return left.order.serial < right.order.serial;
        }
    );

    std::vector<const PreparedSlot*> usableSlots;
    usableSlots.reserve(preparedSlots.size());
    for (const PreparedSlot& slot : preparedSlots) {
        const SlotKey key{slot.slot.hostId, slot.slot.dockIndex};
        if (slot.slot.alive
            && slot.slot.available
            && occupiedSlots.find(key) == occupiedSlots.end()) {
            // poweredOperational is deliberately not consulted here.
            usableSlots.push_back(&slot);
        }
    }

    if (preparedOrders.size() > usableSlots.size()) {
        const PreparedOrder& blocked =
            preparedOrders[usableSlots.size()];
        return infeasibleResult(
            RuntimeAircraftProductionMatchVeto::InsufficientPhysicalSlots,
            "not enough unoccupied live aircraft production slots",
            blocked
        );
    }

    std::map<std::string, std::vector<int>> slotsByType;
    for (std::size_t slotIndex = 0;
        slotIndex < usableSlots.size();
        ++slotIndex) {
        for (const std::string& typeKey :
            usableSlots[slotIndex]->compatibleTypeKeys) {
            slotsByType[typeKey].push_back(static_cast<int>(slotIndex));
        }
    }

    std::vector<std::vector<int>> adjacency;
    adjacency.reserve(preparedOrders.size());
    for (const PreparedOrder& order : preparedOrders) {
        const auto found = slotsByType.find(order.typeKey);
        if (found == slotsByType.end()) {
            return infeasibleResult(
                RuntimeAircraftProductionMatchVeto::IncompatibleAircraftType,
                "no usable aircraft production slot accepts order type "
                    + order.order.typeId,
                order
            );
        }
        adjacency.push_back(found->second);
    }

    const int orderCount = static_cast<int>(preparedOrders.size());
    const int slotCount = static_cast<int>(usableSlots.size());
    std::vector<int> orderToSlot(static_cast<std::size_t>(orderCount), -1);
    std::vector<int> slotToOrder(static_cast<std::size_t>(slotCount), -1);
    std::vector<int> distance(static_cast<std::size_t>(orderCount), -1);
    const auto buildLayers = [&]() {
        std::queue<int> pending;
        for (int orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
            if (orderToSlot[static_cast<std::size_t>(orderIndex)] < 0) {
                distance[static_cast<std::size_t>(orderIndex)] = 0;
                pending.push(orderIndex);
            } else {
                distance[static_cast<std::size_t>(orderIndex)] = -1;
            }
        }

        bool reachesFreeSlot = false;
        while (!pending.empty()) {
            const int orderIndex = pending.front();
            pending.pop();
            for (const int slotIndex :
                adjacency[static_cast<std::size_t>(orderIndex)]) {
                const int matchedOrder =
                    slotToOrder[static_cast<std::size_t>(slotIndex)];
                if (matchedOrder < 0) {
                    reachesFreeSlot = true;
                } else if (distance[static_cast<std::size_t>(matchedOrder)]
                        < 0) {
                    distance[static_cast<std::size_t>(matchedOrder)] =
                        distance[static_cast<std::size_t>(orderIndex)] + 1;
                    pending.push(matchedOrder);
                }
            }
        }
        return reachesFreeSlot;
    };

    int matchedCount = 0;
    while (buildLayers()) {
        std::vector<std::size_t> nextEdge(
            static_cast<std::size_t>(orderCount),
            0U
        );
        const auto augment = [&](int rootOrder) {
            std::vector<int> pathOrders{rootOrder};
            std::vector<int> pathSlots;
            while (!pathOrders.empty()) {
                const int orderIndex = pathOrders.back();
                std::size_t& edgeIndex =
                    nextEdge[static_cast<std::size_t>(orderIndex)];
                bool descended = false;
                while (edgeIndex
                    < adjacency[static_cast<std::size_t>(orderIndex)].size()) {
                    const int slotIndex =
                        adjacency[static_cast<std::size_t>(orderIndex)]
                            [edgeIndex++];
                    const int matchedOrder =
                        slotToOrder[static_cast<std::size_t>(slotIndex)];
                    if (matchedOrder < 0) {
                        pathSlots.push_back(slotIndex);
                        for (std::size_t pathIndex = pathOrders.size();
                            pathIndex-- > 0U;) {
                            const int pathOrder = pathOrders[pathIndex];
                            const int pathSlot = pathSlots[pathIndex];
                            orderToSlot[static_cast<std::size_t>(pathOrder)] =
                                pathSlot;
                            slotToOrder[static_cast<std::size_t>(pathSlot)] =
                                pathOrder;
                        }
                        return true;
                    }
                    if (distance[static_cast<std::size_t>(matchedOrder)]
                        == distance[static_cast<std::size_t>(orderIndex)] + 1) {
                        pathSlots.push_back(slotIndex);
                        pathOrders.push_back(matchedOrder);
                        descended = true;
                        break;
                    }
                }
                if (descended) {
                    continue;
                }

                distance[static_cast<std::size_t>(orderIndex)] = -1;
                pathOrders.pop_back();
                if (pathSlots.size() == pathOrders.size()
                    && !pathSlots.empty()) {
                    pathSlots.pop_back();
                }
            }
            return false;
        };
        for (int orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
            if (orderToSlot[static_cast<std::size_t>(orderIndex)] < 0
                && augment(orderIndex)) {
                ++matchedCount;
            }
        }
    }
    if (matchedCount != orderCount) {
        const auto blocked = std::find(orderToSlot.begin(), orderToSlot.end(), -1);
        const std::size_t blockedIndex = static_cast<std::size_t>(
            std::distance(orderToSlot.begin(), blocked)
        );
        return infeasibleResult(
            RuntimeAircraftProductionMatchVeto::NoFeasibleAssignment,
            "aircraft production orders have no complete compatible "
                "slot assignment",
            preparedOrders[blockedIndex]
        );
    }

    RuntimeAircraftProductionMatchResult result;
    result.inputValid = true;
    result.feasible = true;
    result.veto = RuntimeAircraftProductionMatchVeto::None;
    result.reason = "aircraft production orders have a complete slot assignment";
    result.assignments.reserve(preparedOrders.size());
    for (int orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
        const PreparedOrder& order =
            preparedOrders[static_cast<std::size_t>(orderIndex)];
        const PreparedSlot& slot = *usableSlots[static_cast<std::size_t>(
            orderToSlot[static_cast<std::size_t>(orderIndex)]
        )];
        result.assignments.push_back({
            input.canonicalOwner,
            order.order.serial,
            order.order.typeId,
            order.order.phase,
            slot.slot.hostId,
            slot.slot.dockIndex
        });
    }
    return result;
}

RuntimeAircraftProductionRecoveryResult
runtimeRecoverAircraftProductionDocks(
    const RuntimeAircraftProductionRecoveryInput& input
) {
    if (input.slots.size() > runtimeAircraftProductionMaximumSlots
        || input.orders.size() > runtimeAircraftProductionMaximumOrders
        || input.liveOccupancies.size()
            > runtimeAircraftProductionMaximumOccupancies
        || input.flexibleLiveAircraft.size()
            > runtimeAircraftProductionMaximumFlexibleLiveAircraft) {
        return invalidRecoveryResult(
            RuntimeAircraftProductionMatchVeto::InputLimitExceeded,
            "aircraft production recovery input exceeds a size limit"
        );
    }
    if (!validCanonicalOwner(input.canonicalOwner)) {
        return invalidRecoveryResult(
            RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner,
            "aircraft production recovery owner is not canonical"
        );
    }

    std::vector<RuntimeAircraftProductionSlot> sortedSlots = input.slots;
    std::sort(
        sortedSlots.begin(),
        sortedSlots.end(),
        [](const auto& left, const auto& right) {
            if (left.poweredOperational != right.poweredOperational)
                return left.poweredOperational;
            if (left.preferredPrimary != right.preferredPrimary)
                return left.preferredPrimary;
            return std::tie(
                left.hostId,
                left.dockIndex,
                left.canonicalOwner
            ) < std::tie(
                right.hostId,
                right.dockIndex,
                right.canonicalOwner
            );
        }
    );

    std::set<SlotKey> allSlotKeys;
    std::vector<PreparedSlot> preparedSlots;
    preparedSlots.reserve(sortedSlots.size());
    for (RuntimeAircraftProductionSlot& slot : sortedSlots) {
        if (!validCanonicalOwner(slot.canonicalOwner)) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner,
                "aircraft production recovery slot owner is not canonical"
            );
        }
        if (slot.canonicalOwner != input.canonicalOwner) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::OwnerMismatch,
                "aircraft production recovery slot belongs to another owner"
            );
        }
        if (slot.compatibleAircraftTypeIds.size()
            > runtimeAircraftProductionMaximumCompatibleTypesPerSlot) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::InputLimitExceeded,
                "aircraft production recovery slot compatibility list "
                    "exceeds the size limit"
            );
        }
        if (slot.hostId < 0
            || slot.dockIndex < 0
            || slot.dockIndex > maximumDockIndex
            || slot.compatibleAircraftTypeIds.empty()) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::InvalidSlot,
                "aircraft production recovery slot has invalid fields"
            );
        }
        const SlotKey slotKey{slot.hostId, slot.dockIndex};
        if (!allSlotKeys.insert(slotKey).second) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::DuplicateSlot,
                "aircraft production recovery input repeats slot host="
                    + std::to_string(slot.hostId) + " dock="
                    + std::to_string(slot.dockIndex)
            );
        }

        std::set<std::string> uniqueTypeKeys;
        std::vector<std::string> compatibleTypeKeys;
        compatibleTypeKeys.reserve(slot.compatibleAircraftTypeIds.size());
        for (const std::string& typeId : slot.compatibleAircraftTypeIds) {
            if (!validAircraftTypeId(typeId)) {
                return invalidRecoveryResult(
                    RuntimeAircraftProductionMatchVeto::InvalidCompatibleType,
                    "aircraft production recovery slot has an invalid "
                        "compatible type"
                );
            }
            std::string typeKey = aircraftTypeKey(typeId);
            if (!uniqueTypeKeys.insert(typeKey).second) {
                return invalidRecoveryResult(
                    RuntimeAircraftProductionMatchVeto::DuplicateCompatibleType,
                    "aircraft production recovery slot repeats compatible "
                        "type " + typeId
                );
            }
            compatibleTypeKeys.push_back(std::move(typeKey));
        }
        std::sort(compatibleTypeKeys.begin(), compatibleTypeKeys.end());
        preparedSlots.push_back({std::move(slot), std::move(compatibleTypeKeys)});
    }

    std::vector<RuntimeAircraftProductionLiveOccupancy> sortedOccupancies =
        input.liveOccupancies;
    std::sort(
        sortedOccupancies.begin(),
        sortedOccupancies.end(),
        [](const auto& left, const auto& right) {
            return std::tie(left.hostId, left.dockIndex, left.aircraftId)
                < std::tie(right.hostId, right.dockIndex, right.aircraftId);
        }
    );
    std::set<SlotKey> occupiedSlots;
    std::set<int> liveAircraftIds;
    for (const RuntimeAircraftProductionLiveOccupancy& occupancy :
        sortedOccupancies) {
        if (!validCanonicalOwner(occupancy.canonicalOwner)) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner,
                "aircraft production recovery occupancy owner is not canonical"
            );
        }
        if (occupancy.canonicalOwner != input.canonicalOwner) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::OwnerMismatch,
                "aircraft production recovery occupancy belongs to another "
                    "owner"
            );
        }
        if (occupancy.aircraftId < 0
            || occupancy.hostId < 0
            || occupancy.aircraftId == occupancy.hostId
            || occupancy.dockIndex < 0
            || occupancy.dockIndex > maximumDockIndex) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::InvalidOccupancy,
                "aircraft production recovery hard occupancy has invalid "
                    "fields"
            );
        }
        const SlotKey slotKey{occupancy.hostId, occupancy.dockIndex};
        if (allSlotKeys.find(slotKey) == allSlotKeys.end()) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::OccupancySlotMissing,
                "aircraft production recovery occupancy references an "
                    "unknown slot"
            );
        }
        if (!occupiedSlots.insert(slotKey).second) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::DuplicateOccupancySlot,
                "aircraft production recovery repeats a hard occupied slot"
            );
        }
        if (!liveAircraftIds.insert(occupancy.aircraftId).second) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::DuplicateOccupancyAircraft,
                "aircraft production recovery repeats a hard occupying "
                    "aircraft"
            );
        }
    }

    std::vector<PreparedFlexibleLiveAircraft> preparedLive;
    preparedLive.reserve(input.flexibleLiveAircraft.size());
    for (const RuntimeAircraftProductionFlexibleLiveAircraft& aircraft :
        input.flexibleLiveAircraft) {
        if (!validCanonicalOwner(aircraft.canonicalOwner)) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner,
                "aircraft production recovery flexible live owner is not "
                    "canonical"
            );
        }
        if (aircraft.canonicalOwner != input.canonicalOwner) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::OwnerMismatch,
                "aircraft production recovery flexible live aircraft belongs "
                    "to another owner"
            );
        }
        const bool invalidPrior = aircraft.priorSlot.has_value()
            && (aircraft.priorSlot->hostId < 0
                || aircraft.priorSlot->dockIndex < 0
                || aircraft.priorSlot->dockIndex > maximumDockIndex
                || aircraft.priorSlot->hostId == aircraft.aircraftId);
        if (aircraft.aircraftId < 0
            || !validAircraftTypeId(aircraft.typeId)
            || invalidPrior) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::InvalidFlexibleLiveAircraft,
                "aircraft production recovery flexible live aircraft has "
                    "invalid fields"
            );
        }
        if (!liveAircraftIds.insert(aircraft.aircraftId).second) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::DuplicateLiveAircraft,
                "aircraft production recovery repeats a live aircraft id"
            );
        }
        preparedLive.push_back({aircraft, aircraftTypeKey(aircraft.typeId)});
    }
    std::sort(
        preparedLive.begin(),
        preparedLive.end(),
        [](const auto& left, const auto& right) {
            return left.aircraft.aircraftId < right.aircraft.aircraftId;
        }
    );

    std::vector<PreparedOrder> preparedOrders;
    preparedOrders.reserve(input.orders.size());
    std::set<std::uint64_t> orderSerials;
    for (const RuntimeAircraftProductionOrder& order : input.orders) {
        if (order.serial == 0
            || !validAircraftTypeId(order.typeId)
            || !validExistingOrderPhase(order.phase)) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::InvalidOrder,
                "aircraft production recovery order has invalid fields"
            );
        }
        if (!orderSerials.insert(order.serial).second) {
            return invalidRecoveryResult(
                RuntimeAircraftProductionMatchVeto::DuplicateOrderSerial,
                "aircraft production recovery repeats order serial "
                    + std::to_string(order.serial)
            );
        }
        preparedOrders.push_back({order, aircraftTypeKey(order.typeId)});
    }
    std::sort(
        preparedOrders.begin(),
        preparedOrders.end(),
        [](const auto& left, const auto& right) {
            return left.order.serial < right.order.serial;
        }
    );

    std::vector<const PreparedSlot*> usableSlots;
    usableSlots.reserve(preparedSlots.size());
    for (const PreparedSlot& slot : preparedSlots) {
        const SlotKey slotKey{slot.slot.hostId, slot.slot.dockIndex};
        if (slot.slot.alive
            && slot.slot.available
            && occupiedSlots.find(slotKey) == occupiedSlots.end()) {
            // As in the complete matcher, power gates progress rather than
            // the existence of physical recovery capacity.
            usableSlots.push_back(&slot);
        }
    }

    std::map<std::string, std::vector<int>> slotsByType;
    std::map<SlotKey, int> usableSlotIndexByKey;
    for (std::size_t slotIndex = 0;
        slotIndex < usableSlots.size();
        ++slotIndex) {
        const PreparedSlot& slot = *usableSlots[slotIndex];
        usableSlotIndexByKey.emplace(
            SlotKey{slot.slot.hostId, slot.slot.dockIndex},
            static_cast<int>(slotIndex)
        );
        for (const std::string& typeKey : slot.compatibleTypeKeys) {
            slotsByType[typeKey].push_back(static_cast<int>(slotIndex));
        }
    }

    // More world objects may be persistable than there can ever be physical
    // pads. Keep every live id for deterministic output, but only materialize
    // a maximum independent subset of flexible live demands into the explicit
    // slot graph. Omitted aircraft are in the closure of that basis: once the
    // basis remains fully matched, no omitted aircraft can consume a free
    // compatible slot, and adding retained orders can only reduce capacity.
    std::vector<std::size_t> activeLiveIndices;
    activeLiveIndices.reserve(std::min(
        preparedLive.size(),
        usableSlots.size()
    ));
    std::vector<int> basisLeftToSlot;
    std::vector<int> basisSlotToLeft(usableSlots.size(), -1);
    std::set<std::string> basisRejectedTypeKeys;
    const auto augmentBasis = [&](std::size_t sourceLiveIndex) {
        const std::string& sourceType =
            preparedLive[sourceLiveIndex].typeKey;
        if (basisRejectedTypeKeys.contains(sourceType)) {
            return false;
        }
        const auto compatible = slotsByType.find(
            sourceType
        );
        if (compatible == slotsByType.end()) {
            basisRejectedTypeKeys.insert(sourceType);
            return false;
        }
        std::vector<int> parentSlot(usableSlots.size(), -1);
        std::vector<bool> visitedLeft(activeLiveIndices.size(), false);
        std::set<std::string> expandedTypeKeys{sourceType};
        std::queue<int> pending;
        constexpr int rootMarker = -2;
        for (const int slotIndex : compatible->second) {
            parentSlot[static_cast<std::size_t>(slotIndex)] = rootMarker;
            pending.push(slotIndex);
        }
        int freeSlot = -1;
        while (!pending.empty() && freeSlot < 0) {
            const int slotIndex = pending.front();
            pending.pop();
            const int matchedLeft =
                basisSlotToLeft[static_cast<std::size_t>(slotIndex)];
            if (matchedLeft < 0) {
                freeSlot = slotIndex;
                break;
            }
            if (visitedLeft[static_cast<std::size_t>(matchedLeft)]) {
                continue;
            }
            visitedLeft[static_cast<std::size_t>(matchedLeft)] = true;
            const std::size_t matchedSourceIndex =
                activeLiveIndices[static_cast<std::size_t>(matchedLeft)];
            const std::string& matchedType =
                preparedLive[matchedSourceIndex].typeKey;
            if (!expandedTypeKeys.insert(matchedType).second) {
                continue;
            }
            const auto matchedCompatible = slotsByType.find(matchedType);
            if (matchedCompatible == slotsByType.end()) {
                continue;
            }
            for (const int nextSlot : matchedCompatible->second) {
                if (parentSlot[static_cast<std::size_t>(nextSlot)] == -1) {
                    parentSlot[static_cast<std::size_t>(nextSlot)] =
                        matchedLeft;
                    pending.push(nextSlot);
                }
            }
        }
        if (freeSlot < 0) {
            basisRejectedTypeKeys.insert(
                expandedTypeKeys.begin(),
                expandedTypeKeys.end()
            );
            return false;
        }
        const int newLeft = static_cast<int>(activeLiveIndices.size());
        int slotIndex = freeSlot;
        while (true) {
            const int parentLeft =
                parentSlot[static_cast<std::size_t>(slotIndex)];
            if (parentLeft == rootMarker) {
                basisSlotToLeft[static_cast<std::size_t>(slotIndex)] =
                    newLeft;
                activeLiveIndices.push_back(sourceLiveIndex);
                basisLeftToSlot.push_back(slotIndex);
                break;
            }
            const int previousSlot = basisLeftToSlot[
                static_cast<std::size_t>(parentLeft)
            ];
            basisLeftToSlot[static_cast<std::size_t>(parentLeft)] =
                slotIndex;
            basisSlotToLeft[static_cast<std::size_t>(slotIndex)] =
                parentLeft;
            slotIndex = previousSlot;
        }
        return true;
    };
    for (std::size_t sourceLiveIndex = 0;
        sourceLiveIndex < preparedLive.size()
            && activeLiveIndices.size() < usableSlots.size();
        ++sourceLiveIndex) {
        (void)augmentBasis(sourceLiveIndex);
    }

    const std::size_t liveCount = activeLiveIndices.size();
    const std::size_t orderCount = preparedOrders.size();
    std::vector<std::vector<int>> adjacency(liveCount + orderCount);
    for (std::size_t liveIndex = 0; liveIndex < liveCount; ++liveIndex) {
        const PreparedFlexibleLiveAircraft& aircraft = preparedLive[
            activeLiveIndices[liveIndex]
        ];
        const auto compatible = slotsByType.find(aircraft.typeKey);
        if (compatible != slotsByType.end()) {
            adjacency[liveIndex] = compatible->second;
        }
        if (!aircraft.aircraft.priorSlot.has_value()) {
            continue;
        }
        const auto prior = usableSlotIndexByKey.find({
            aircraft.aircraft.priorSlot->hostId,
            aircraft.aircraft.priorSlot->dockIndex
        });
        if (prior == usableSlotIndexByKey.end()) {
            continue;
        }
        auto& edges = adjacency[liveIndex];
        const auto priorEdge = std::find(
            edges.begin(),
            edges.end(),
            prior->second
        );
        if (priorEdge != edges.end()) {
            std::rotate(edges.begin(), priorEdge, std::next(priorEdge));
        }
    }
    for (std::size_t orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
        const auto compatible = slotsByType.find(
            preparedOrders[orderIndex].typeKey
        );
        if (compatible != slotsByType.end()) {
            adjacency[liveCount + orderIndex] = compatible->second;
        }
    }

    std::vector<int> leftToSlot(adjacency.size(), -1);
    std::vector<int> slotToLeft(usableSlots.size(), -1);
    const auto demandTypeKey = [
        &preparedLive,
        &preparedOrders,
        &activeLiveIndices,
        liveCount
    ](
        std::size_t leftIndex
    ) -> const std::string& {
        return leftIndex < liveCount
            ? preparedLive[activeLiveIndices[leftIndex]].typeKey
            : preparedOrders[leftIndex - liveCount].typeKey;
    };
    std::set<std::string> rejectedTypeKeys;
    const auto augment = [&](
        std::size_t rootLeft,
        std::set<std::string>& expandedTypeKeys
    ) {
        std::vector<int> parentSlot(usableSlots.size(), -1);
        std::vector<bool> visitedLeft(adjacency.size(), false);
        std::queue<int> pending;
        visitedLeft[rootLeft] = true;
        pending.push(static_cast<int>(rootLeft));
        int freeSlot = -1;
        while (!pending.empty() && freeSlot < 0) {
            const int leftIndex = pending.front();
            pending.pop();
            if (!expandedTypeKeys.insert(demandTypeKey(
                    static_cast<std::size_t>(leftIndex)
                )).second) {
                continue;
            }
            for (const int slotIndex :
                adjacency[static_cast<std::size_t>(leftIndex)]) {
                if (parentSlot[static_cast<std::size_t>(slotIndex)] >= 0) {
                    continue;
                }
                parentSlot[static_cast<std::size_t>(slotIndex)] = leftIndex;
                const int matchedLeft =
                    slotToLeft[static_cast<std::size_t>(slotIndex)];
                if (matchedLeft < 0) {
                    freeSlot = slotIndex;
                    break;
                }
                if (rejectedTypeKeys.contains(demandTypeKey(
                        static_cast<std::size_t>(matchedLeft)
                    ))) {
                    // A previously exhausted type remains in the closure as
                    // the accepted independent set grows. Its alternating
                    // subtree therefore cannot reach a free slot and need
                    // not be expanded again for a different root type.
                    continue;
                }
                if (!visitedLeft[static_cast<std::size_t>(matchedLeft)]) {
                    visitedLeft[static_cast<std::size_t>(matchedLeft)] = true;
                    pending.push(matchedLeft);
                }
            }
        }
        if (freeSlot < 0) {
            return false;
        }

        int slotIndex = freeSlot;
        while (slotIndex >= 0) {
            const int leftIndex =
                parentSlot[static_cast<std::size_t>(slotIndex)];
            const int previousSlot =
                leftToSlot[static_cast<std::size_t>(leftIndex)];
            leftToSlot[static_cast<std::size_t>(leftIndex)] = slotIndex;
            slotToLeft[static_cast<std::size_t>(slotIndex)] = leftIndex;
            slotIndex = previousSlot;
        }
        return true;
    };

    // Live aircraft are the highest-priority partition. Compute their maximum
    // cardinality with Hopcroft-Karp so a large Hall shortage does not repeat
    // one full alternating-tree search per unmatched aircraft.
    std::vector<int> liveDistance(liveCount, -1);
    const auto buildLiveLayers = [&]() {
        std::queue<int> pending;
        for (std::size_t liveIndex = 0; liveIndex < liveCount; ++liveIndex) {
            if (leftToSlot[liveIndex] < 0) {
                liveDistance[liveIndex] = 0;
                pending.push(static_cast<int>(liveIndex));
            } else {
                liveDistance[liveIndex] = -1;
            }
        }
        bool reachesFreeSlot = false;
        while (!pending.empty()) {
            const int liveIndex = pending.front();
            pending.pop();
            for (const int slotIndex :
                    adjacency[static_cast<std::size_t>(liveIndex)]) {
                const int matchedLeft =
                    slotToLeft[static_cast<std::size_t>(slotIndex)];
                if (matchedLeft < 0) {
                    reachesFreeSlot = true;
                } else if (matchedLeft < static_cast<int>(liveCount)
                    && liveDistance[static_cast<std::size_t>(matchedLeft)]
                        < 0) {
                    liveDistance[static_cast<std::size_t>(matchedLeft)] =
                        liveDistance[static_cast<std::size_t>(liveIndex)] + 1;
                    pending.push(matchedLeft);
                }
            }
        }
        return reachesFreeSlot;
    };
    std::vector<std::size_t> nextLiveEdge(liveCount, 0U);
    const auto augmentLive = [&](int rootLive) {
        std::vector<int> pathLive{rootLive};
        std::vector<int> pathSlots;
        while (!pathLive.empty()) {
            const int liveIndex = pathLive.back();
            std::size_t& edgeIndex =
                nextLiveEdge[static_cast<std::size_t>(liveIndex)];
            bool descended = false;
            while (edgeIndex
                < adjacency[static_cast<std::size_t>(liveIndex)].size()) {
                const int slotIndex =
                    adjacency[static_cast<std::size_t>(liveIndex)]
                        [edgeIndex++];
                const int matchedLeft =
                    slotToLeft[static_cast<std::size_t>(slotIndex)];
                if (matchedLeft < 0) {
                    pathSlots.push_back(slotIndex);
                    for (std::size_t pathIndex = pathLive.size();
                        pathIndex-- > 0U;) {
                        const int pathLeft = pathLive[pathIndex];
                        const int pathSlot = pathSlots[pathIndex];
                        leftToSlot[static_cast<std::size_t>(pathLeft)] =
                            pathSlot;
                        slotToLeft[static_cast<std::size_t>(pathSlot)] =
                            pathLeft;
                    }
                    return true;
                }
                if (matchedLeft < static_cast<int>(liveCount)
                    && liveDistance[static_cast<std::size_t>(matchedLeft)]
                        == liveDistance[static_cast<std::size_t>(liveIndex)]
                            + 1) {
                    pathSlots.push_back(slotIndex);
                    pathLive.push_back(matchedLeft);
                    descended = true;
                    break;
                }
            }
            if (descended) {
                continue;
            }
            liveDistance[static_cast<std::size_t>(liveIndex)] = -1;
            pathLive.pop_back();
            if (pathSlots.size() == pathLive.size()
                && !pathSlots.empty()) {
                pathSlots.pop_back();
            }
        }
        return false;
    };

    std::size_t matchedLiveCount = 0U;
    while (buildLiveLayers()) {
        std::fill(nextLiveEdge.begin(), nextLiveEdge.end(), 0U);
        for (std::size_t liveIndex = 0; liveIndex < liveCount; ++liveIndex) {
            if (leftToSlot[liveIndex] < 0
                && augmentLive(static_cast<int>(liveIndex))) {
                ++matchedLiveCount;
            }
        }
    }
    if (matchedLiveCount != activeLiveIndices.size()) {
        return invalidRecoveryResult(
            RuntimeAircraftProductionMatchVeto::NoFeasibleAssignment,
            "aircraft production recovery lost its prepared live basis"
        );
    }
    if (matchedLiveCount < usableSlots.size()) {
        std::size_t matchedOrderCount = 0;
        for (std::size_t orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
            const std::string& typeKey = preparedOrders[orderIndex].typeKey;
            if (rejectedTypeKeys.contains(typeKey)) {
                continue;
            }
            std::set<std::string> expandedTypeKeys;
            if (!augment(liveCount + orderIndex, expandedTypeKeys)) {
                // Accepted demands only grow. In the transversal matroid, a
                // type reached by a complete failed alternating search stays
                // in the closure of every later superset. Cache the entire
                // exhausted component, not only the root type, so distinct
                // later types do not repeatedly rescan the same saturated
                // Hall component while unrelated slots remain free.
                rejectedTypeKeys.insert(
                    expandedTypeKeys.begin(),
                    expandedTypeKeys.end()
                );
                continue;
            }
            ++matchedOrderCount;
            if (matchedLiveCount + matchedOrderCount == usableSlots.size()) {
                // Every usable slot is now occupied. A later order cannot be
                // retained without displacing an earlier (lower-serial)
                // retained order or a live aircraft, so avoid traversing a
                // fully saturated dense graph once per rejected order.
                break;
            }
        }
    }

    RuntimeAircraftProductionRecoveryResult result;
    result.inputValid = true;
    result.maximumLiveMatched = matchedLiveCount;
    result.liveAssignments.reserve(matchedLiveCount);
    result.unassignedLiveAircraftIds.reserve(
        preparedLive.size() - matchedLiveCount
    );
    std::vector<int> sourceToActiveLeft(preparedLive.size(), -1);
    for (std::size_t liveIndex = 0; liveIndex < liveCount; ++liveIndex) {
        sourceToActiveLeft[activeLiveIndices[liveIndex]] =
            static_cast<int>(liveIndex);
    }
    for (std::size_t sourceLiveIndex = 0;
        sourceLiveIndex < preparedLive.size();
        ++sourceLiveIndex) {
        const PreparedFlexibleLiveAircraft& aircraft =
            preparedLive[sourceLiveIndex];
        const int activeLeft = sourceToActiveLeft[sourceLiveIndex];
        const int slotIndex = activeLeft < 0
            ? -1
            : leftToSlot[static_cast<std::size_t>(activeLeft)];
        if (slotIndex < 0) {
            result.unassignedLiveAircraftIds.push_back(
                aircraft.aircraft.aircraftId
            );
            continue;
        }
        const RuntimeAircraftProductionSlot& slot =
            usableSlots[static_cast<std::size_t>(slotIndex)]->slot;
        result.liveAssignments.push_back({
            input.canonicalOwner,
            aircraft.aircraft.aircraftId,
            aircraft.aircraft.typeId,
            aircraft.aircraft.airborne,
            slot.hostId,
            slot.dockIndex
        });
    }
    result.retainedOrderAssignments.reserve(orderCount);
    result.rejectedOrderSerials.reserve(orderCount);
    for (std::size_t orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
        const PreparedOrder& order = preparedOrders[orderIndex];
        const int slotIndex = leftToSlot[liveCount + orderIndex];
        if (slotIndex < 0) {
            result.rejectedOrderSerials.push_back(order.order.serial);
            continue;
        }
        const RuntimeAircraftProductionSlot& slot =
            usableSlots[static_cast<std::size_t>(slotIndex)]->slot;
        result.retainedOrderAssignments.push_back({
            input.canonicalOwner,
            order.order.serial,
            order.order.typeId,
            order.order.phase,
            slot.hostId,
            slot.dockIndex
        });
    }

    result.complete = result.unassignedLiveAircraftIds.empty()
        && result.rejectedOrderSerials.empty();
    if (result.complete) {
        result.veto = RuntimeAircraftProductionMatchVeto::None;
        result.reason =
            "aircraft production recovery assigned all live aircraft and "
            "orders";
        return result;
    }

    bool incompatible = false;
    for (std::size_t sourceLiveIndex = 0;
        sourceLiveIndex < preparedLive.size();
        ++sourceLiveIndex) {
        const int activeLeft = sourceToActiveLeft[sourceLiveIndex];
        incompatible = incompatible
            || (activeLeft < 0
                && slotsByType.find(preparedLive[sourceLiveIndex].typeKey)
                    == slotsByType.end())
            || (activeLeft >= 0
                && leftToSlot[static_cast<std::size_t>(activeLeft)] < 0
                && adjacency[static_cast<std::size_t>(activeLeft)].empty());
    }
    for (std::size_t orderIndex = 0; orderIndex < orderCount; ++orderIndex) {
        incompatible = incompatible
            || (leftToSlot[liveCount + orderIndex] < 0
                && adjacency[liveCount + orderIndex].empty());
    }
    if (incompatible) {
        result.veto =
            RuntimeAircraftProductionMatchVeto::IncompatibleAircraftType;
        result.reason =
            "aircraft production recovery has unmatched incompatible demand";
    } else if (preparedLive.size() + orderCount > usableSlots.size()) {
        result.veto =
            RuntimeAircraftProductionMatchVeto::InsufficientPhysicalSlots;
        result.reason =
            "aircraft production recovery retained the maximum live-first "
            "subset within physical slot capacity";
    } else {
        result.veto = RuntimeAircraftProductionMatchVeto::NoFeasibleAssignment;
        result.reason =
            "aircraft production recovery retained the maximum live-first "
            "compatible subset";
    }
    return result;
}

const char* runtimeAircraftProductionMatchVetoName(
    RuntimeAircraftProductionMatchVeto veto
) {
    switch (veto) {
    case RuntimeAircraftProductionMatchVeto::None:
        return "None";
    case RuntimeAircraftProductionMatchVeto::InputLimitExceeded:
        return "InputLimitExceeded";
    case RuntimeAircraftProductionMatchVeto::InvalidCanonicalOwner:
        return "InvalidCanonicalOwner";
    case RuntimeAircraftProductionMatchVeto::OwnerMismatch:
        return "OwnerMismatch";
    case RuntimeAircraftProductionMatchVeto::InvalidSlot:
        return "InvalidSlot";
    case RuntimeAircraftProductionMatchVeto::DuplicateSlot:
        return "DuplicateSlot";
    case RuntimeAircraftProductionMatchVeto::InvalidCompatibleType:
        return "InvalidCompatibleType";
    case RuntimeAircraftProductionMatchVeto::DuplicateCompatibleType:
        return "DuplicateCompatibleType";
    case RuntimeAircraftProductionMatchVeto::InvalidOrder:
        return "InvalidOrder";
    case RuntimeAircraftProductionMatchVeto::DuplicateOrderSerial:
        return "DuplicateOrderSerial";
    case RuntimeAircraftProductionMatchVeto::InvalidOccupancy:
        return "InvalidOccupancy";
    case RuntimeAircraftProductionMatchVeto::DuplicateOccupancySlot:
        return "DuplicateOccupancySlot";
    case RuntimeAircraftProductionMatchVeto::DuplicateOccupancyAircraft:
        return "DuplicateOccupancyAircraft";
    case RuntimeAircraftProductionMatchVeto::OccupancySlotMissing:
        return "OccupancySlotMissing";
    case RuntimeAircraftProductionMatchVeto::InsufficientPhysicalSlots:
        return "InsufficientPhysicalSlots";
    case RuntimeAircraftProductionMatchVeto::IncompatibleAircraftType:
        return "IncompatibleAircraftType";
    case RuntimeAircraftProductionMatchVeto::NoFeasibleAssignment:
        return "NoFeasibleAssignment";
    case RuntimeAircraftProductionMatchVeto::InvalidFlexibleLiveAircraft:
        return "InvalidFlexibleLiveAircraft";
    case RuntimeAircraftProductionMatchVeto::DuplicateLiveAircraft:
        return "DuplicateLiveAircraft";
    }
    return "Unknown";
}
