#include "simulation/production/RuntimeProductionLanes.h"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>

namespace {

std::string normalizedToken(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size()
        && (value[begin] == ' '
            || value[begin] == '\t'
            || value[begin] == '\r'
            || value[begin] == '\n')) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin
        && (value[end - 1] == ' '
            || value[end - 1] == '\t'
            || value[end - 1] == '\r'
            || value[end - 1] == '\n')) {
        --end;
    }

    std::string normalized;
    normalized.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
        const char character = value[index];
        normalized.push_back(
            character >= 'A' && character <= 'Z'
                ? static_cast<char>(character - 'A' + 'a')
                : character
        );
    }
    return normalized;
}

constexpr std::uint32_t productionMicros = 1000000;
constexpr std::size_t maximumProductionItemKeyLength = 256;

bool validProductionLane(RuntimeProductionLane lane) {
    return static_cast<std::size_t>(lane) < runtimeProductionLanes.size();
}

bool validProductionOrder(const RuntimeProductionLaneOrder& order) {
    return !order.itemKey.empty()
        && order.itemKey.size() <= maximumProductionItemKeyLength
        && order.cost >= 0
        && order.totalTicks >= 1
        && order.remainingTicks >= 0
        && order.remainingTicks <= order.totalTicks
        && order.creditsSpent >= 0
        && order.creditsSpent <= order.cost
        && order.creditRemainderMicros < productionMicros
        && order.zeroCostProgressMicros <= productionMicros
        && order.rateDivisionDenominator <= 1000
        && (order.rateDivisionDenominator == 0
            ? order.rateDivisionRemainder == 0
            : order.rateDivisionRemainder
                < static_cast<std::uint64_t>(order.totalTicks)
                    * order.rateDivisionDenominator)
        && (order.cost == 0
            ? order.creditsSpent == 0
                && order.creditRemainderMicros == 0
            : order.zeroCostProgressMicros == 0);
}

int expectedProductionRemainingTicks(
    const RuntimeProductionLaneOrder& order
) {
    if (order.cost > 0) {
        const std::uint64_t remainingCost =
            static_cast<std::uint64_t>(order.cost - order.creditsSpent);
        return static_cast<int>(
            (static_cast<std::uint64_t>(order.totalTicks) * remainingCost
                + static_cast<std::uint64_t>(order.cost) - 1)
            / static_cast<std::uint64_t>(order.cost)
        );
    }
    return static_cast<int>(
        (static_cast<std::uint64_t>(order.totalTicks)
            * (productionMicros - order.zeroCostProgressMicros)
            + productionMicros - 1)
        / productionMicros
    );
}

bool productionOrderComplete(const RuntimeProductionLaneOrder& order) {
    return order.cost > 0
        ? order.creditsSpent >= order.cost
        : order.zeroCostProgressMicros >= productionMicros;
}

bool validActiveProductionOrder(const RuntimeProductionLaneOrder& order) {
    if (order.remainingTicks != expectedProductionRemainingTicks(order)) {
        return false;
    }
    const bool complete = productionOrderComplete(order);
    if (complete) {
        return order.creditRemainderMicros == 0
            && order.rateDivisionRemainder == 0
            && order.rateDivisionDenominator == 0;
    }
    if (order.rateDivisionDenominator == 0
        && (order.creditRemainderMicros != 0
            || (order.cost == 0 && order.zeroCostProgressMicros != 0))) {
        return false;
    }
    return true;
}

void hashProductionByte(std::uint64_t& hash, std::uint8_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
}

template <typename Value>
void hashProductionValue(std::uint64_t& hash, Value value) {
    using Unsigned = std::make_unsigned_t<Value>;
    Unsigned unsignedValue = static_cast<Unsigned>(value);
    for (std::size_t index = 0; index < sizeof(Value); ++index) {
        hashProductionByte(
            hash,
            static_cast<std::uint8_t>(unsignedValue & 0xffu)
        );
        if constexpr (sizeof(Unsigned) > 1) {
            unsignedValue >>= 8u;
        }
    }
}

void hashProductionString(std::uint64_t& hash, std::string_view value) {
    hashProductionValue(hash, static_cast<std::uint64_t>(value.size()));
    for (const unsigned char character : value) {
        hashProductionByte(hash, character);
    }
}

void writeProductionOrder(
    std::ostream& output,
    char role,
    const RuntimeProductionLaneOrder& order
) {
    output << role << ' '
           << order.serial << ' '
           << order.cost << ' '
           << order.totalTicks << ' '
           << order.remainingTicks << ' '
           << order.creditsSpent << ' '
           << order.creditRemainderMicros << ' '
           << order.zeroCostProgressMicros << ' '
           << order.rateDivisionRemainder << ' '
           << order.rateDivisionDenominator << ' '
           << (order.requiresPlacement ? 1 : 0) << ' '
           << std::quoted(order.itemKey) << '\n';
}

bool readProductionOrder(
    std::istream& input,
    char expectedRole,
    RuntimeProductionLaneOrder& order
) {
    char role = '\0';
    int requiresPlacement = 0;
    if (!(input >> role
          >> order.serial
          >> order.cost
          >> order.totalTicks
          >> order.remainingTicks
          >> order.creditsSpent
          >> order.creditRemainderMicros
          >> order.zeroCostProgressMicros
          >> order.rateDivisionRemainder
          >> order.rateDivisionDenominator
          >> requiresPlacement
          >> std::quoted(order.itemKey))) {
        return false;
    }
    if (role != expectedRole
        || (requiresPlacement != 0 && requiresPlacement != 1)) {
        return false;
    }
    order.requiresPlacement = requiresPlacement != 0;
    return validProductionOrder(order) && order.serial != 0;
}

} // namespace

std::string_view runtimeProductionLaneName(RuntimeProductionLane lane) {
    switch (lane) {
    case RuntimeProductionLane::Structures:
        return "Structures";
    case RuntimeProductionLane::Armory:
        return "Armory";
    case RuntimeProductionLane::Infantry:
        return "Infantry";
    case RuntimeProductionLane::Vehicles:
        return "Vehicles";
    case RuntimeProductionLane::Aircraft:
        return "Aircraft";
    case RuntimeProductionLane::Ships:
        return "Ships";
    }
    return "Unknown";
}

std::optional<RuntimeProductionLane> runtimeProductionLaneFromName(
    std::string_view name
) {
    const std::string normalized = normalizedToken(name);
    if (normalized == "structures") {
        return RuntimeProductionLane::Structures;
    }
    if (normalized == "armory") {
        return RuntimeProductionLane::Armory;
    }
    if (normalized == "infantry") {
        return RuntimeProductionLane::Infantry;
    }
    if (normalized == "vehicles") {
        return RuntimeProductionLane::Vehicles;
    }
    if (normalized == "ships") {
        return RuntimeProductionLane::Ships;
    }
    // "Aircrafts" was exposed by the reference project's bot API.  Read it
    // for compatibility, but always serialize the stable singular name.
    if (normalized == "aircraft" || normalized == "aircrafts") {
        return RuntimeProductionLane::Aircraft;
    }
    return std::nullopt;
}

RuntimeProductionLane runtimeProductionLaneForBuilding(
    const BuildingType& building
) {
    return normalizedToken(building.buildCat) == "combat"
        ? RuntimeProductionLane::Armory
        : RuntimeProductionLane::Structures;
}

RuntimeProductionLane runtimeProductionLaneForUnit(
    const UnitType& unit,
    RuntimeProductionUnitClass unitClass
) {
    switch (unitClass) {
    case RuntimeProductionUnitClass::Infantry:
        return RuntimeProductionLane::Infantry;
    case RuntimeProductionUnitClass::Vehicle:
        return unit.naval || unit.waterBound
            ? RuntimeProductionLane::Ships
            : RuntimeProductionLane::Vehicles;
    case RuntimeProductionUnitClass::Aircraft:
        return RuntimeProductionLane::Aircraft;
    }
    return RuntimeProductionLane::Vehicles;
}

bool runtimeProductionFactorySupportsLane(
    const BuildingType& factory,
    RuntimeProductionLane lane
) {
    const std::string factoryType = normalizedToken(factory.factory);
    if (factoryType == "buildingtype") {
        return lane == RuntimeProductionLane::Structures
            || lane == RuntimeProductionLane::Armory;
    }
    if (factoryType == "infantrytype") {
        return lane == RuntimeProductionLane::Infantry;
    }
    if (factoryType == "aircrafttype") {
        return lane == RuntimeProductionLane::Aircraft;
    }
    if (factoryType == "navalunittype" || factoryType == "vesseltype") {
        return lane == RuntimeProductionLane::Ships;
    }
    if (factoryType == "unittype" || factoryType == "vehicletype") {
        return factory.naval
            ? lane == RuntimeProductionLane::Ships
            : lane == RuntimeProductionLane::Vehicles;
    }
    return false;
}

std::size_t runtimeProductionLaneIndex(RuntimeProductionLane lane) {
    const std::size_t index = static_cast<std::size_t>(lane);
    return index < runtimeProductionLanes.size()
        ? index
        : runtimeProductionLanes.size();
}

int runtimeProductionLaneCapacity(
    RuntimeProductionLane lane,
    int maximumQueuedObjects,
    int aircraftDockCapacity
) {
    switch (lane) {
    case RuntimeProductionLane::Structures:
    case RuntimeProductionLane::Armory:
        return 1;
    case RuntimeProductionLane::Aircraft:
        return std::max(0, aircraftDockCapacity);
    case RuntimeProductionLane::Infantry:
    case RuntimeProductionLane::Vehicles:
    case RuntimeProductionLane::Ships:
        return maximumQueuedObjects >= std::numeric_limits<int>::max() - 1
            ? std::numeric_limits<int>::max()
            : std::max(1, maximumQueuedObjects + 1);
    }
    return 0;
}

int runtimeProductionLaneOccupancy(
    const RuntimeProductionLanesState& state,
    RuntimeProductionLane lane
) {
    if (!validProductionLane(lane)) {
        return 0;
    }
    const RuntimeProductionLaneState& laneState =
        state.lanes[runtimeProductionLaneIndex(lane)];
    return static_cast<int>(laneState.queued.size())
        + (laneState.active.has_value() ? 1 : 0)
        + (laneState.ready.has_value() ? 1 : 0);
}

RuntimeProductionQueueResult runtimeQueueProductionOrder(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    RuntimeProductionLaneOrder order,
    int maximumQueuedObjects,
    int aircraftDockCapacity,
    RuntimeProductionInsertMode insertMode
) {
    if (!validProductionLane(lane)
        || order.itemKey.empty()
        || order.itemKey.size() > maximumProductionItemKeyLength
        || order.cost < 0
        || order.totalTicks < 1
        || (insertMode != RuntimeProductionInsertMode::Back
            && insertMode != RuntimeProductionInsertMode::AfterActive)) {
        return {};
    }
    const int capacity = runtimeProductionLaneCapacity(
        lane,
        maximumQueuedObjects,
        aircraftDockCapacity
    );
    if (runtimeProductionLaneOccupancy(state, lane) >= capacity) {
        return {RuntimeProductionQueueDecision::LaneFull, 0};
    }
    const RuntimeProductionLaneState& targetLane =
        state.lanes[runtimeProductionLaneIndex(lane)];
    int matchingItems = 0;
    for (const RuntimeProductionLaneOrder& queued : targetLane.queued) {
        matchingItems += queued.itemKey == order.itemKey ? 1 : 0;
    }
    matchingItems += targetLane.active.has_value()
            && targetLane.active->itemKey == order.itemKey
        ? 1
        : 0;
    matchingItems += targetLane.ready.has_value()
            && targetLane.ready->itemKey == order.itemKey
        ? 1
        : 0;
    const int perItemCapacity =
        lane == RuntimeProductionLane::Structures
            || lane == RuntimeProductionLane::Armory
        ? 1
        : (maximumQueuedObjects >= std::numeric_limits<int>::max() - 1
            ? std::numeric_limits<int>::max()
            : std::max(1, maximumQueuedObjects + 1));
    if (matchingItems >= perItemCapacity) {
        return {RuntimeProductionQueueDecision::LaneFull, 0};
    }

    auto serialExists = [&](std::uint64_t serial) {
        for (const RuntimeProductionLane laneToScan : runtimeProductionLanes) {
            const RuntimeProductionLaneState& laneState =
                state.lanes[runtimeProductionLaneIndex(laneToScan)];
            for (const RuntimeProductionLaneOrder& queued : laneState.queued) {
                if (queued.serial == serial) {
                    return true;
                }
            }
            if ((laneState.active.has_value()
                    && laneState.active->serial == serial)
                || (laneState.ready.has_value()
                    && laneState.ready->serial == serial)) {
                return true;
            }
        }
        return false;
    };
    if (order.serial == 0) {
        if (state.nextSerial == 0) {
            state.nextSerial = 1;
        }
        if (state.nextSerial == std::numeric_limits<std::uint64_t>::max()) {
            return {};
        }
        order.serial = state.nextSerial++;
    } else {
        if (order.serial == std::numeric_limits<std::uint64_t>::max()
            || (state.nextSerial != 0 && order.serial < state.nextSerial)
            || serialExists(order.serial)) {
            return {};
        }
        state.nextSerial = std::max(state.nextSerial, order.serial + 1);
    }
    order.remainingTicks = order.totalTicks;
    order.creditsSpent = 0;
    order.creditRemainderMicros = 0;
    order.zeroCostProgressMicros = 0;
    order.rateDivisionRemainder = 0;
    order.rateDivisionDenominator = 0;
    order.requiresPlacement =
        lane == RuntimeProductionLane::Structures
        || lane == RuntimeProductionLane::Armory;
    const std::uint64_t serial = order.serial;
    RuntimeProductionLaneState& laneState =
        state.lanes[runtimeProductionLaneIndex(lane)];
    if (insertMode == RuntimeProductionInsertMode::AfterActive) {
        laneState.queued.push_front(std::move(order));
    } else {
        laneState.queued.push_back(std::move(order));
    }
    return {RuntimeProductionQueueDecision::Accepted, serial};
}

bool runtimeSetProductionHold(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    bool onHold
) {
    if (!validProductionLane(lane)) {
        return false;
    }
    RuntimeProductionLaneState& laneState =
        state.lanes[runtimeProductionLaneIndex(lane)];
    if (laneState.ready.has_value()
        || (!laneState.active.has_value() && laneState.queued.empty())) {
        return false;
    }
    if (laneState.active.has_value()
        && productionOrderComplete(*laneState.active)) {
        if (onHold) {
            return false;
        }
        laneState.onHold = false;
        return true;
    }
    laneState.onHold = onHold;
    return true;
}

RuntimeProductionCancelResult runtimeCancelProductionOrder(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    std::string_view itemKey
) {
    if (!validProductionLane(lane)) {
        return {};
    }
    RuntimeProductionLaneState& laneState =
        state.lanes[runtimeProductionLaneIndex(lane)];
    RuntimeProductionCancelResult result;
    // Pop from the queue tail first.  Repeated UI items must not discard the
    // paid progress of the current item while an unstarted copy is behind it.
    for (auto iterator = laneState.queued.rbegin();
        iterator != laneState.queued.rend();
        ++iterator) {
        if (iterator->itemKey != itemKey) {
            continue;
        }
        result.removed = true;
        result.order = *iterator;
        result.refund = result.order.creditsSpent;
        auto eraseIterator = iterator.base();
        --eraseIterator;
        laneState.queued.erase(eraseIterator);
        if (runtimeProductionLaneOccupancy(state, lane) == 0) {
            laneState.onHold = false;
        }
        return result;
    }
    if (laneState.ready.has_value()
        && laneState.ready->itemKey == itemKey) {
        result.removed = true;
        result.order = *laneState.ready;
        result.refund = result.order.creditsSpent;
        laneState.ready.reset();
        laneState.onHold = false;
        return result;
    }
    if (laneState.active.has_value()
        && laneState.active->itemKey == itemKey) {
        result.removed = true;
        result.order = *laneState.active;
        result.refund = result.order.creditsSpent;
        laneState.active.reset();
        laneState.onHold = false;
        return result;
    }
    return result;
}

RuntimeProductionCancelResult runtimeCancelProductionSerial(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    std::uint64_t serial
) {
    if (!validProductionLane(lane) || serial == 0) {
        return {};
    }
    RuntimeProductionLaneState& laneState =
        state.lanes[runtimeProductionLaneIndex(lane)];
    RuntimeProductionCancelResult result;
    const auto queued = std::find_if(
        laneState.queued.begin(),
        laneState.queued.end(),
        [serial](const RuntimeProductionLaneOrder& order) {
            return order.serial == serial;
        }
    );
    if (queued != laneState.queued.end()) {
        result.removed = true;
        result.order = *queued;
        result.refund = result.order.creditsSpent;
        laneState.queued.erase(queued);
        if (runtimeProductionLaneOccupancy(state, lane) == 0) {
            laneState.onHold = false;
        }
        return result;
    }
    if (laneState.ready.has_value()
        && laneState.ready->serial == serial) {
        result.removed = true;
        result.order = *laneState.ready;
        result.refund = result.order.creditsSpent;
        laneState.ready.reset();
        laneState.onHold = false;
        return result;
    }
    if (laneState.active.has_value()
        && laneState.active->serial == serial) {
        result.removed = true;
        result.order = *laneState.active;
        result.refund = result.order.creditsSpent;
        laneState.active.reset();
        laneState.onHold = false;
    }
    return result;
}

std::vector<RuntimeProductionAdvanceEvent> runtimeAdvanceProductionLanes(
    RuntimeProductionLanesState& state,
    int& credits,
    const RuntimeProductionAdvanceHooks& hooks
) {
    std::vector<RuntimeProductionAdvanceEvent> events;
    credits = std::max(0, credits);
    for (const RuntimeProductionLane lane : runtimeProductionLaneAdvanceOrder) {
        RuntimeProductionLaneState& laneState =
            state.lanes[runtimeProductionLaneIndex(lane)];
        if (!laneState.active.has_value()
            && !laneState.ready.has_value()
            && !laneState.queued.empty()) {
            RuntimeProductionLaneOrder& queued = laneState.queued.front();
            const bool canStart = !hooks.canStart
                || hooks.canStart(lane, queued);
            if (!canStart) {
                events.push_back({
                    lane,
                    RuntimeProductionAdvanceKind::Blocked,
                    queued.serial,
                    queued.itemKey,
                    queued.remainingTicks,
                    queued.creditsSpent
                });
            } else {
                RuntimeProductionLaneOrder started = std::move(queued);
                laneState.queued.pop_front();
                started.remainingTicks = started.totalTicks;
                laneState.active = std::move(started);
                events.push_back({
                    lane,
                    RuntimeProductionAdvanceKind::Started,
                    laneState.active->serial,
                    laneState.active->itemKey,
                    laneState.active->remainingTicks,
                    laneState.active->creditsSpent
                });
            }
        }

        if (!laneState.active.has_value()) {
            continue;
        }
        RuntimeProductionLaneOrder& active = *laneState.active;
        if (productionOrderComplete(active)) {
            active.remainingTicks = 0;
            active.creditRemainderMicros = 0;
            active.rateDivisionRemainder = 0;
            active.rateDivisionDenominator = 0;
            laneState.onHold = false;
            if (active.requiresPlacement) {
                laneState.ready = std::move(active);
                laneState.active.reset();
                events.push_back({
                    lane,
                    RuntimeProductionAdvanceKind::PlacementReady,
                    laneState.ready->serial,
                    laneState.ready->itemKey,
                    0,
                    laneState.ready->creditsSpent
                });
            } else {
                events.push_back({
                    lane,
                    RuntimeProductionAdvanceKind::CompletionDeferred,
                    active.serial,
                    active.itemKey,
                    0,
                    active.creditsSpent
                });
            }
            continue;
        }
        if (laneState.onHold) {
            events.push_back({
                lane,
                RuntimeProductionAdvanceKind::Held,
                active.serial,
                active.itemKey,
                active.remainingTicks,
                active.creditsSpent
            });
            continue;
        }
        if (hooks.canContinue && !hooks.canContinue(lane, active)) {
            events.push_back({
                lane,
                RuntimeProductionAdvanceKind::Blocked,
                active.serial,
                active.itemKey,
                active.remainingTicks,
                active.creditsSpent
            });
            continue;
        }

        const RuntimeProductionRate rate = hooks.productionRate
            ? hooks.productionRate(lane, active)
            : RuntimeProductionRate{};
        if (rate.denominator == 0
            || rate.denominator > 1000
            || rate.numerator > 1000) {
            events.push_back({
                lane,
                RuntimeProductionAdvanceKind::Blocked,
                active.serial,
                active.itemKey,
                active.remainingTicks,
                active.creditsSpent
            });
            continue;
        }
        if (rate.numerator == 0) {
            continue;
        }

        const bool immediate = hooks.completeImmediately
            && hooks.completeImmediately(lane, active);
        bool madeProgress = false;
        bool waitingForCredits = false;
        int creditsDebited = 0;
        if (active.cost > 0
            && active.creditsSpent < active.cost
            && credits == 0) {
            events.push_back({
                lane,
                RuntimeProductionAdvanceKind::WaitingForCredits,
                active.serial,
                active.itemKey,
                active.remainingTicks,
                active.creditsSpent
            });
            continue;
        }
        auto accrueRateDivision = [&](std::uint64_t scaledNumerator) {
            if (active.rateDivisionDenominator != 0
                && active.rateDivisionDenominator != rate.denominator) {
                active.rateDivisionRemainder =
                    (active.rateDivisionRemainder * rate.denominator)
                    / active.rateDivisionDenominator;
            }
            active.rateDivisionDenominator = rate.denominator;
            const std::uint64_t divisor =
                static_cast<std::uint64_t>(active.totalTicks)
                * rate.denominator;
            const std::uint64_t accumulated =
                scaledNumerator + active.rateDivisionRemainder;
            active.rateDivisionRemainder = accumulated % divisor;
            return accumulated / divisor;
        };
        if (active.cost > 0 && active.creditsSpent < active.cost) {
            const std::uint64_t scaledCost =
                static_cast<std::uint64_t>(active.cost) * productionMicros;
            const std::uint64_t accruedMicros = immediate ? scaledCost
                : accrueRateDivision(scaledCost * rate.numerator)
                    + active.creditRemainderMicros;
            int spend = static_cast<int>(std::min<std::uint64_t>(
                accruedMicros / productionMicros,
                static_cast<std::uint64_t>(active.cost - active.creditsSpent)
            ));
            if (spend > credits) {
                spend = credits;
                active.creditRemainderMicros = 0;
                active.rateDivisionRemainder = 0;
                active.rateDivisionDenominator = 0;
            } else {
                active.creditRemainderMicros = static_cast<std::uint32_t>(
                    accruedMicros % productionMicros
                );
            }
            if (spend > 0) {
                credits -= spend;
                active.creditsSpent += spend;
                creditsDebited = spend;
                madeProgress = true;
            } else if (accruedMicros >= productionMicros && credits == 0) {
                waitingForCredits = true;
            } else {
                madeProgress = active.creditRemainderMicros != 0
                    || active.rateDivisionRemainder != 0;
            }
            const std::uint64_t remainingCost =
                static_cast<std::uint64_t>(active.cost - active.creditsSpent);
            active.remainingTicks = static_cast<int>(
                (static_cast<std::uint64_t>(active.totalTicks)
                    * remainingCost
                    + static_cast<std::uint64_t>(active.cost) - 1)
                / static_cast<std::uint64_t>(active.cost)
            );
        } else if (active.cost == 0
            && active.zeroCostProgressMicros < productionMicros) {
            const std::uint64_t increment = immediate ? productionMicros : accrueRateDivision(
                static_cast<std::uint64_t>(productionMicros)
                    * rate.numerator
            );
            active.zeroCostProgressMicros = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    productionMicros,
                    static_cast<std::uint64_t>(
                        active.zeroCostProgressMicros
                    ) + increment
                )
            );
            active.remainingTicks = static_cast<int>(
                (static_cast<std::uint64_t>(active.totalTicks)
                    * (productionMicros - active.zeroCostProgressMicros)
                    + productionMicros - 1)
                / productionMicros
            );
            madeProgress = true;
        }

        if (waitingForCredits) {
            events.push_back({
                lane,
                RuntimeProductionAdvanceKind::WaitingForCredits,
                active.serial,
                active.itemKey,
                active.remainingTicks,
                active.creditsSpent
            });
            continue;
        }
        if (madeProgress) {
            events.push_back({
                lane,
                RuntimeProductionAdvanceKind::Progressed,
                active.serial,
                active.itemKey,
                active.remainingTicks,
                active.creditsSpent,
                creditsDebited
            });
        }
        const bool productionComplete = productionOrderComplete(active);
        if (!productionComplete) {
            continue;
        }
        active.remainingTicks = 0;
        active.creditRemainderMicros = 0;
        active.rateDivisionRemainder = 0;
        active.rateDivisionDenominator = 0;

        if (active.requiresPlacement) {
            laneState.ready = std::move(active);
            laneState.active.reset();
            laneState.onHold = false;
            events.push_back({
                lane,
                RuntimeProductionAdvanceKind::PlacementReady,
                laneState.ready->serial,
                laneState.ready->itemKey,
                0,
                laneState.ready->creditsSpent
            });
            continue;
        }
        events.push_back({
            lane,
            RuntimeProductionAdvanceKind::CompletionDeferred,
            active.serial,
            active.itemKey,
            0,
            active.creditsSpent
        });
    }
    return events;
}

RuntimeProductionReconcileResult runtimeReconcileProductionLaneCapacity(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    int maximumQueuedObjects,
    int aircraftDockCapacity
) {
    RuntimeProductionReconcileResult result;
    if (!validProductionLane(lane)) {
        return result;
    }
    RuntimeProductionLaneState& laneState =
        state.lanes[runtimeProductionLaneIndex(lane)];
    auto recordRemoval = [&](RuntimeProductionLaneOrder order) {
        result.refund = static_cast<int>(std::min<long long>(
            std::numeric_limits<int>::max(),
            static_cast<long long>(result.refund) + order.creditsSpent
        ));
        result.removed.push_back(std::move(order));
    };
    auto removeQueuedBack = [&](const std::string* matchingKey) {
        for (auto iterator = laneState.queued.rbegin();
            iterator != laneState.queued.rend();
            ++iterator) {
            if (matchingKey != nullptr && iterator->itemKey != *matchingKey) {
                continue;
            }
            RuntimeProductionLaneOrder removed = *iterator;
            auto eraseIterator = iterator.base();
            --eraseIterator;
            laneState.queued.erase(eraseIterator);
            recordRemoval(std::move(removed));
            return true;
        }
        return false;
    };
    auto removeNewestMatching = [&](const std::string* matchingKey) {
        if (removeQueuedBack(matchingKey)) {
            return true;
        }
        if (laneState.ready.has_value()
            && (matchingKey == nullptr
                || laneState.ready->itemKey == *matchingKey)) {
            recordRemoval(*laneState.ready);
            laneState.ready.reset();
            return true;
        }
        if (laneState.active.has_value()
            && (matchingKey == nullptr
                || laneState.active->itemKey == *matchingKey)) {
            recordRemoval(*laneState.active);
            laneState.active.reset();
            return true;
        }
        return false;
    };

    const int capacity = runtimeProductionLaneCapacity(
        lane,
        maximumQueuedObjects,
        aircraftDockCapacity
    );
    while (runtimeProductionLaneOccupancy(state, lane) > capacity) {
        if (!removeNewestMatching(nullptr)) {
            break;
        }
    }

    const int perItemCapacity =
        lane == RuntimeProductionLane::Structures
            || lane == RuntimeProductionLane::Armory
        ? 1
        : (maximumQueuedObjects >= std::numeric_limits<int>::max() - 1
            ? std::numeric_limits<int>::max()
            : std::max(1, maximumQueuedObjects + 1));
    std::set<std::string> itemKeys;
    for (const RuntimeProductionLaneOrder& order : laneState.queued) {
        itemKeys.insert(order.itemKey);
    }
    if (laneState.active.has_value()) {
        itemKeys.insert(laneState.active->itemKey);
    }
    if (laneState.ready.has_value()) {
        itemKeys.insert(laneState.ready->itemKey);
    }
    for (const std::string& itemKey : itemKeys) {
        auto countMatching = [&]() {
            int count = 0;
            for (const RuntimeProductionLaneOrder& queued : laneState.queued) {
                count += queued.itemKey == itemKey ? 1 : 0;
            }
            count += laneState.active.has_value()
                    && laneState.active->itemKey == itemKey
                ? 1
                : 0;
            count += laneState.ready.has_value()
                    && laneState.ready->itemKey == itemKey
                ? 1
                : 0;
            return count;
        };
        while (countMatching() > perItemCapacity) {
            if (!removeNewestMatching(&itemKey)) {
                break;
            }
        }
    }
    if (runtimeProductionLaneOccupancy(state, lane) == 0
        || laneState.ready.has_value()) {
        laneState.onHold = false;
    }
    return result;
}

std::optional<RuntimeProductionLaneOrder> runtimePeekCompletedProduction(
    const RuntimeProductionLanesState& state,
    RuntimeProductionLane lane
) {
    if (!validProductionLane(lane)) {
        return std::nullopt;
    }
    const RuntimeProductionLaneState& laneState =
        state.lanes[runtimeProductionLaneIndex(lane)];
    if (!laneState.active.has_value()
        || laneState.active->requiresPlacement
        || !productionOrderComplete(*laneState.active)) {
        return std::nullopt;
    }
    return laneState.active;
}

std::vector<RuntimeProductionCompletedOrder>
runtimeEnumerateCompletedProduction(
    const RuntimeProductionLanesState& state
) {
    std::vector<RuntimeProductionCompletedOrder> completed;
    completed.reserve(runtimeProductionLanes.size());
    for (const RuntimeProductionLane lane : runtimeProductionLanes) {
        const std::optional<RuntimeProductionLaneOrder> order =
            runtimePeekCompletedProduction(state, lane);
        if (order.has_value()) {
            completed.push_back({lane, *order});
        }
    }
    std::sort(
        completed.begin(),
        completed.end(),
        [](const RuntimeProductionCompletedOrder& left,
           const RuntimeProductionCompletedOrder& right) {
            if (left.order.serial != right.order.serial) {
                return left.order.serial < right.order.serial;
            }
            return static_cast<std::uint8_t>(left.lane)
                < static_cast<std::uint8_t>(right.lane);
        }
    );
    return completed;
}

std::optional<RuntimeProductionLaneOrder> runtimeCommitCompletedProduction(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    std::uint64_t serial
) {
    if (serial == 0 || !validProductionLane(lane)) {
        return std::nullopt;
    }
    RuntimeProductionLaneState& laneState =
        state.lanes[runtimeProductionLaneIndex(lane)];
    if (!laneState.active.has_value()
        || laneState.active->serial != serial
        || laneState.active->requiresPlacement
        || !productionOrderComplete(*laneState.active)) {
        return std::nullopt;
    }
    std::optional<RuntimeProductionLaneOrder> completed =
        std::move(laneState.active);
    laneState.active.reset();
    laneState.onHold = false;
    return completed;
}

std::optional<RuntimeProductionLaneOrder> runtimeConsumeReadyProduction(
    RuntimeProductionLanesState& state,
    RuntimeProductionLane lane,
    std::uint64_t serial
) {
    if (serial == 0 || !validProductionLane(lane)) {
        return std::nullopt;
    }
    RuntimeProductionLaneState& laneState =
        state.lanes[runtimeProductionLaneIndex(lane)];
    if (!laneState.ready.has_value()
        || laneState.ready->serial != serial) {
        return std::nullopt;
    }
    std::optional<RuntimeProductionLaneOrder> ready =
        std::move(laneState.ready);
    laneState.ready.reset();
    laneState.onHold = false;
    return ready;
}

std::string runtimeSerializeProductionLanes(
    const RuntimeProductionLanesState& state
) {
    std::ostringstream output;
    output << "RPLS 2 " << state.nextSerial << '\n';
    for (const RuntimeProductionLane lane : runtimeProductionLanes) {
        const RuntimeProductionLaneState& laneState =
            state.lanes[runtimeProductionLaneIndex(lane)];
        output << "L " << static_cast<int>(lane) << ' '
               << laneState.queued.size() << ' '
               << (laneState.active.has_value() ? 1 : 0) << ' '
               << (laneState.ready.has_value() ? 1 : 0) << ' '
               << (laneState.onHold ? 1 : 0) << '\n';
        for (const RuntimeProductionLaneOrder& order : laneState.queued) {
            writeProductionOrder(output, 'Q', order);
        }
        if (laneState.active.has_value()) {
            writeProductionOrder(output, 'A', *laneState.active);
        }
        if (laneState.ready.has_value()) {
            writeProductionOrder(output, 'R', *laneState.ready);
        }
    }
    return output.str();
}

bool runtimeDeserializeProductionLanes(
    std::string_view serialized,
    RuntimeProductionLanesState& state,
    std::string* error
) {
    auto fail = [error](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    std::istringstream input{std::string(serialized)};
    std::string magic;
    int schema = 0;
    RuntimeProductionLanesState parsed;
    if (!(input >> magic >> schema >> parsed.nextSerial)
        || magic != "RPLS"
        || schema != 2
        || parsed.nextSerial == 0) {
        return fail("invalid production lanes header");
    }

    constexpr std::size_t maximumSerializedOrders = 65536;
    std::size_t totalOrders = 0;
    std::set<std::uint64_t> serials;
    std::uint64_t greatestSerial = 0;
    for (const RuntimeProductionLane expectedLane : runtimeProductionLanes) {
        char marker = '\0';
        int laneValue = -1;
        std::size_t queuedCount = 0;
        int activeCount = 0;
        int readyCount = 0;
        int onHold = 0;
        if (!(input >> marker
              >> laneValue
              >> queuedCount
              >> activeCount
              >> readyCount
              >> onHold)
            || marker != 'L'
            || laneValue != static_cast<int>(expectedLane)
            || (activeCount != 0 && activeCount != 1)
            || (readyCount != 0 && readyCount != 1)
            || (onHold != 0 && onHold != 1)
            || (activeCount != 0 && readyCount != 0)
            || queuedCount > maximumSerializedOrders
            || totalOrders + queuedCount
                + static_cast<std::size_t>(activeCount + readyCount)
                > maximumSerializedOrders) {
            return fail("invalid production lane record");
        }
        RuntimeProductionLaneState& laneState =
            parsed.lanes[runtimeProductionLaneIndex(expectedLane)];
        const bool laneRequiresPlacement =
            expectedLane == RuntimeProductionLane::Structures
            || expectedLane == RuntimeProductionLane::Armory;
        laneState.onHold = onHold != 0;
        auto acceptOrder = [&](const RuntimeProductionLaneOrder& order) {
            if (!serials.insert(order.serial).second) {
                return false;
            }
            greatestSerial = std::max(greatestSerial, order.serial);
            ++totalOrders;
            return true;
        };
        for (std::size_t index = 0; index < queuedCount; ++index) {
            RuntimeProductionLaneOrder order;
            if (!readProductionOrder(input, 'Q', order)
                || order.remainingTicks != order.totalTicks
                || order.creditsSpent != 0
                || order.creditRemainderMicros != 0
                || order.zeroCostProgressMicros != 0
                || order.rateDivisionRemainder != 0
                || order.rateDivisionDenominator != 0
                || order.requiresPlacement != laneRequiresPlacement
                || !acceptOrder(order)) {
                return fail("invalid queued production order");
            }
            laneState.queued.push_back(std::move(order));
        }
        if (activeCount != 0) {
            RuntimeProductionLaneOrder order;
            if (!readProductionOrder(input, 'A', order)
                || order.requiresPlacement != laneRequiresPlacement
                || !validActiveProductionOrder(order)
                || !acceptOrder(order)) {
                return fail("invalid active production order");
            }
            laneState.active = std::move(order);
        }
        if (readyCount != 0) {
            RuntimeProductionLaneOrder order;
            if (!readProductionOrder(input, 'R', order)
                || order.remainingTicks != 0
                || !laneRequiresPlacement
                || !order.requiresPlacement
                || order.creditRemainderMicros != 0
                || order.rateDivisionRemainder != 0
                || order.rateDivisionDenominator != 0
                || (order.cost > 0
                    ? order.creditsSpent != order.cost
                    : order.zeroCostProgressMicros != productionMicros)
                || !acceptOrder(order)) {
                return fail("invalid ready production order");
            }
            laneState.ready = std::move(order);
        }
        const int occupancy = static_cast<int>(queuedCount)
            + activeCount + readyCount;
        if ((laneRequiresPlacement && occupancy > 1)
            || (laneRequiresPlacement
                && laneState.active.has_value()
                && productionOrderComplete(*laneState.active))
            || (laneState.onHold
                && (laneState.ready.has_value()
                    || occupancy == 0
                    || (laneState.active.has_value()
                        && productionOrderComplete(
                            *laneState.active
                        ))))) {
            return fail("impossible production lane state");
        }
    }
    input >> std::ws;
    if (!input.eof()) {
        return fail("trailing production lane data");
    }
    if (parsed.nextSerial <= greatestSerial) {
        return fail("production serial counter did not follow saved orders");
    }
    state = std::move(parsed);
    if (error != nullptr) {
        error->clear();
    }
    return true;
}

std::uint64_t runtimeProductionLanesStateHash(
    const RuntimeProductionLanesState& state
) {
    std::uint64_t hash = 1469598103934665603ull;
    hashProductionValue(hash, state.nextSerial);
    for (const RuntimeProductionLane lane : runtimeProductionLanes) {
        hashProductionValue(hash, static_cast<std::uint8_t>(lane));
        const RuntimeProductionLaneState& laneState =
            state.lanes[runtimeProductionLaneIndex(lane)];
        hashProductionValue(
            hash,
            static_cast<std::uint64_t>(laneState.queued.size())
        );
        hashProductionByte(hash, laneState.onHold ? 1 : 0);
        auto hashOrder = [&](char role, const RuntimeProductionLaneOrder& order) {
            hashProductionByte(hash, static_cast<std::uint8_t>(role));
            hashProductionValue(hash, order.serial);
            hashProductionString(hash, order.itemKey);
            hashProductionValue(hash, order.cost);
            hashProductionValue(hash, order.totalTicks);
            hashProductionValue(hash, order.remainingTicks);
            hashProductionValue(hash, order.creditsSpent);
            hashProductionValue(hash, order.creditRemainderMicros);
            hashProductionValue(hash, order.zeroCostProgressMicros);
            hashProductionValue(hash, order.rateDivisionRemainder);
            hashProductionValue(hash, order.rateDivisionDenominator);
            hashProductionByte(hash, order.requiresPlacement ? 1 : 0);
        };
        for (const RuntimeProductionLaneOrder& order : laneState.queued) {
            hashOrder('Q', order);
        }
        hashProductionByte(hash, laneState.active.has_value() ? 1 : 0);
        if (laneState.active.has_value()) {
            hashOrder('A', *laneState.active);
        }
        hashProductionByte(hash, laneState.ready.has_value() ? 1 : 0);
        if (laneState.ready.has_value()) {
            hashOrder('R', *laneState.ready);
        }
    }
    return hash;
}
