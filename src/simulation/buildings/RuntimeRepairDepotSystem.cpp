#include "simulation/buildings/RuntimeRepairDepotSystem.h"

#include "core/io/IniFile.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace {

constexpr std::size_t maximumSavedRelations = 4096;
constexpr int maximumQueueSlotsPerHost = 64;
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

char lowerAsciiCharacter(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](char character) {
            return lowerAsciiCharacter(character);
        }
    );
    return value;
}

bool equalsAsciiIgnoreCase(
    const std::string& left,
    const std::string& right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAsciiCharacter(left[index])
            != lowerAsciiCharacter(right[index])) {
            return false;
        }
    }
    return true;
}

const WorldObjectInstance* findObject(
    const RuntimeRepairDepotContext& context,
    int objectId
) {
    if (context.objects == nullptr) {
        return nullptr;
    }
    for (const WorldObjectInstance& object : *context.objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

bool ownersAreAllied(
    const RuntimeRepairDepotContext& context,
    const std::string& left,
    const std::string& right
) {
    if (equalsAsciiIgnoreCase(left, right)) {
        return true;
    }
    return context.areAllied && context.areAllied(left, right);
}

std::string economyOwnerId(
    const RuntimeRepairDepotContext& context,
    const std::string& owner
) {
    if (context.canonicalOwnerId) {
        const std::string canonical = context.canonicalOwnerId(owner);
        if (!canonical.empty()) {
            return canonical;
        }
    }
    return lowerAscii(owner);
}

bool objectIsTransported(
    const RuntimeRepairDepotContext& context,
    const WorldObjectInstance& object
) {
    if (lowerAscii(object.mission) == "insidetransport") {
        return true;
    }
    return context.isTransported && context.isTransported(object.instanceId);
}

bool validCell(const RuntimeRepairDepotCell& cell) {
    return cell.x >= 0 && cell.y >= 0;
}

struct PairValidation {
    RuntimeRepairDepotVeto veto = RuntimeRepairDepotVeto::None;
    const WorldObjectInstance* host = nullptr;
    const WorldObjectInstance* unit = nullptr;
    const BuildingType* hostRules = nullptr;
    const UnitType* unitRules = nullptr;
    RuntimeRepairDepotDomain domain = RuntimeRepairDepotDomain::Ground;
};

struct SlotSelection {
    int index = -1;
    RuntimeRepairDepotCell cell;
    RuntimeRepairDepotVeto veto = RuntimeRepairDepotVeto::None;

    bool selected() const {
        return index >= 0 && validCell(cell);
    }
};

SlotSelection selectAvailableDock(
    const RuntimeRepairDepotContext& context,
    const std::vector<RuntimeRepairDepotRelation>& relations,
    int hostId,
    int unitId,
    int numberOfDocks
) {
    bool foundFreeSlot = false;
    bool foundResolvedCell = false;
    for (int candidate = 0; candidate < numberOfDocks; ++candidate) {
        const bool usedByState = std::any_of(
            relations.begin(),
            relations.end(),
            [hostId, candidate](const RuntimeRepairDepotRelation& relation) {
                return relation.phase != RuntimeRepairDepotRelationPhase::Queued
                    && relation.hostId == hostId
                    && relation.dockIndex == candidate;
            }
        );
        const bool usedExternally = context.isDockOccupied
            && context.isDockOccupied(hostId, candidate);
        if (usedByState || usedExternally) {
            continue;
        }
        foundFreeSlot = true;
        if (!context.resolveDockCell) {
            continue;
        }
        const std::optional<RuntimeRepairDepotCell> cell =
            context.resolveDockCell(hostId, candidate, unitId);
        if (!cell.has_value() || !validCell(*cell)) {
            continue;
        }
        foundResolvedCell = true;
        if (!context.canPathTo || !context.canPathTo(unitId, *cell)) {
            continue;
        }
        return {candidate, *cell, RuntimeRepairDepotVeto::None};
    }

    SlotSelection result;
    result.veto = !foundFreeSlot
        ? RuntimeRepairDepotVeto::CapacityFull
        : !foundResolvedCell
            ? RuntimeRepairDepotVeto::DockCellUnavailable
            : RuntimeRepairDepotVeto::PathUnavailable;
    return result;
}

SlotSelection selectAvailableQueueCell(
    const RuntimeRepairDepotContext& context,
    const std::vector<RuntimeRepairDepotRelation>& relations,
    int hostId,
    int unitId
) {
    if (!context.resolveQueueCell) {
        return {-1, {}, RuntimeRepairDepotVeto::CapacityFull};
    }

    bool foundUnusedSlot = false;
    bool foundUniqueCell = false;
    for (int candidate = 0;
         candidate < maximumQueueSlotsPerHost;
         ++candidate) {
        const bool indexInUse = std::any_of(
            relations.begin(),
            relations.end(),
            [hostId, candidate](const RuntimeRepairDepotRelation& relation) {
                return relation.phase == RuntimeRepairDepotRelationPhase::Queued
                    && relation.hostId == hostId
                    && relation.queueIndex == candidate;
            }
        );
        if (indexInUse) {
            continue;
        }
        foundUnusedSlot = true;
        const std::optional<RuntimeRepairDepotCell> cell =
            context.resolveQueueCell(hostId, candidate, unitId);
        if (!cell.has_value() || !validCell(*cell)) {
            continue;
        }
        const bool cellInUse = std::any_of(
            relations.begin(),
            relations.end(),
            [&cell](const RuntimeRepairDepotRelation& relation) {
                return relation.phase == RuntimeRepairDepotRelationPhase::Queued
                    && relation.queueCell == *cell;
            }
        );
        if (cellInUse) {
            continue;
        }
        foundUniqueCell = true;
        if (!context.canPathTo || !context.canPathTo(unitId, *cell)) {
            continue;
        }
        return {candidate, *cell, RuntimeRepairDepotVeto::None};
    }

    SlotSelection result;
    result.veto = !foundUnusedSlot
        ? RuntimeRepairDepotVeto::CapacityFull
        : !foundUniqueCell
            ? RuntimeRepairDepotVeto::QueueCellUnavailable
            : RuntimeRepairDepotVeto::PathUnavailable;
    return result;
}

PairValidation validatePair(
    const RuntimeRepairDepotContext& context,
    int hostId,
    int unitId,
    bool allowFullHealth,
    bool allowExpectedDock,
    int expectedDockIndex
) {
    PairValidation result;
    if (context.rules == nullptr || context.objects == nullptr) {
        result.veto = RuntimeRepairDepotVeto::InvalidContext;
        return result;
    }
    result.host = findObject(context, hostId);
    if (result.host == nullptr) {
        result.veto = RuntimeRepairDepotVeto::HostMissing;
        return result;
    }
    if (!result.host->alive
        || result.host->health <= 0
        || result.host->kind != WorldObjectKind::Structure) {
        result.veto = RuntimeRepairDepotVeto::HostUnavailable;
        return result;
    }
    result.hostRules = context.rules->findBuildingType(result.host->type);
    if (result.hostRules == nullptr
        || !result.hostRules->unitRepair
        || result.hostRules->numberOfDocks <= 0) {
        result.veto = RuntimeRepairDepotVeto::HostNotRepairDepot;
        return result;
    }

    result.unit = findObject(context, unitId);
    if (result.unit == nullptr) {
        result.veto = RuntimeRepairDepotVeto::UnitMissing;
        return result;
    }
    if (!result.unit->alive
        || result.unit->health <= 0
        || result.unit->kind != WorldObjectKind::Unit
        || result.unit->deployed) {
        result.veto = RuntimeRepairDepotVeto::UnitUnavailable;
        return result;
    }
    result.unitRules = context.rules->findUnitType("Vehicle", result.unit->type);
    if (result.unitRules == nullptr
        || result.unit->airborne
        || result.unit->authoredAircraft
        || result.unitRules->consideredAircraft
        || result.unitRules->jumpJet) {
        result.veto = RuntimeRepairDepotVeto::UnitNotGroundVehicle;
        return result;
    }
    if (!context.resolveHostDomain || !context.resolveUnitDomain) {
        result.veto = RuntimeRepairDepotVeto::DomainUnavailable;
        return result;
    }
    const std::optional<RuntimeRepairDepotDomain> hostDomain =
        context.resolveHostDomain(hostId);
    const std::optional<RuntimeRepairDepotDomain> unitDomain =
        context.resolveUnitDomain(unitId);
    if (!hostDomain.has_value() || !unitDomain.has_value()) {
        result.veto = RuntimeRepairDepotVeto::DomainUnavailable;
        return result;
    }
    if (*hostDomain != *unitDomain) {
        result.veto = RuntimeRepairDepotVeto::DomainMismatch;
        return result;
    }
    result.domain = *hostDomain;
    if (!result.unitRules->repairable) {
        result.veto = RuntimeRepairDepotVeto::UnitNotRepairable;
        return result;
    }
    if (result.unit->garrisonHostObjectId >= 0) {
        result.veto = RuntimeRepairDepotVeto::UnitGarrisoned;
        return result;
    }
    if (objectIsTransported(context, *result.unit)) {
        result.veto = RuntimeRepairDepotVeto::UnitTransported;
        return result;
    }
    const bool exactlyExpectedDock = allowExpectedDock
        && result.unit->dockedToObjectId == hostId
        && result.unit->dockIndex == expectedDockIndex;
    if (!exactlyExpectedDock
        && (result.unit->dockedToObjectId >= 0
            || result.unit->dockIndex >= 0)) {
        result.veto = RuntimeRepairDepotVeto::UnitAlreadyDocked;
        return result;
    }
    if (!ownersAreAllied(context, result.host->owner, result.unit->owner)) {
        result.veto = RuntimeRepairDepotVeto::NotAllied;
        return result;
    }
    if (!allowFullHealth
        && result.unit->health >= std::max(1, result.unitRules->strength)) {
        result.veto = RuntimeRepairDepotVeto::UnitAlreadyFull;
        return result;
    }
    return result;
}

std::string vetoReason(RuntimeRepairDepotVeto veto) {
    return runtimeRepairDepotVetoName(veto);
}

RuntimeRepairDepotAction baseAction(
    RuntimeRepairDepotActionKind kind,
    const RuntimeRepairDepotRelation& relation,
    const std::string& reason
) {
    RuntimeRepairDepotAction action;
    action.kind = kind;
    action.hostId = relation.hostId;
    action.unitId = relation.unitId;
    action.dockIndex = relation.dockIndex;
    action.queueIndex = relation.queueIndex;
    action.relationPhase = relation.phase;
    action.owner = relation.unitOwner;
    action.reason = reason;
    action.domain = relation.domain;
    action.expectedOwner = relation.unitOwner;
    return action;
}

void addRallyTarget(
    RuntimeRepairDepotAction& action,
    const RuntimeRepairDepotContext& context,
    const RuntimeRepairDepotRelation& relation
) {
    if (!context.resolveRallyCell) {
        return;
    }
    const std::optional<RuntimeRepairDepotCell> rally =
        context.resolveRallyCell(
            relation.hostId,
            relation.dockIndex,
            relation.unitId
        );
    if (!rally.has_value()) {
        return;
    }
    if (!validCell(*rally)) {
        return;
    }
    if (context.canPathTo
        && !context.canPathTo(relation.unitId, *rally)) {
        return;
    }
    action.hasTargetCell = true;
    action.targetCell = *rally;
}

RuntimeRepairDepotAction releaseAction(
    const RuntimeRepairDepotContext& context,
    const RuntimeRepairDepotRelation& relation,
    RuntimeRepairDepotActionKind kind,
    const std::string& reason
) {
    RuntimeRepairDepotAction action = baseAction(kind, relation, reason);
    if (relation.phase == RuntimeRepairDepotRelationPhase::Docked) {
        addRallyTarget(action, context, relation);
    }
    return action;
}

int serviceIntervalTicks(
    int fixedTickHz,
    const SpecialMechanismRules& rules
) {
    const double minutes = rules.unitRepairRateMinutes;
    if (!std::isfinite(minutes) || minutes <= 0.0) {
        return 1;
    }
    const long double ticks = static_cast<long double>(minutes)
        * 60.0L
        * static_cast<long double>(std::max(1, fixedTickHz));
    if (ticks >= static_cast<long double>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return std::max(
        1,
        static_cast<int>(std::ceil(ticks - 1.0e-12L))
    );
}

struct RepairCharge {
    int healedHitPoints = 0;
    int creditCost = 0;
};

RepairCharge calculateRepairCharge(
    const UnitType& unit,
    const SpecialMechanismRules& rules,
    int currentHealth,
    int availableCredits
) {
    RepairCharge result;
    const int maxHealth = std::max(1, unit.strength);
    const int missingHealth = std::max(0, maxHealth - currentHealth);
    const int repairStep = std::min(
        missingHealth,
        std::max(0, rules.repairStep)
    );
    if (repairStep <= 0) {
        return result;
    }
    if (!std::isfinite(rules.repairPercent)
        || rules.repairPercent <= 0.0
        || unit.cost <= 0) {
        result.healedHitPoints = repairStep;
        return result;
    }

    const long double costPerHitPoint =
        static_cast<long double>(rules.repairPercent)
        * static_cast<long double>(unit.cost)
        / static_cast<long double>(maxHealth);
    if (!std::isfinite(static_cast<double>(costPerHitPoint))
        || costPerHitPoint <= 0.0L
        || availableCredits <= 0) {
        return result;
    }

    const long double authoredStepCost = costPerHitPoint
        * static_cast<long double>(std::max(0, rules.repairStep));
    long long roundedStepCost = static_cast<long long>(std::floor(
        std::min(
            authoredStepCost,
            static_cast<long double>(std::numeric_limits<int>::max())
        )
    ));
    roundedStepCost = std::max<long long>(1, roundedStepCost);
    const int paidCost = std::min(
        std::max(0, availableCredits),
        static_cast<int>(roundedStepCost)
    );
    if (paidCost <= 0) {
        return result;
    }
    const long double affordable = static_cast<long double>(paidCost)
        / costPerHitPoint;
    const int affordableHitPoints = affordable
            >= static_cast<long double>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(std::floor(affordable + 1.0e-12L));
    result.healedHitPoints = std::min(repairStep, affordableHitPoints);
    if (result.healedHitPoints <= 0) {
        return {};
    }
    // Westwood-compatible rounding charges the capped step budget.  It can
    // intentionally exceed costPerHP * the final partial heal.
    result.creditCost = paidCost;
    return result;
}

bool hostSupportsRepairAnimation(
    const RuntimeRepairDepotContext& context,
    int hostId
) {
    const WorldObjectInstance* host = findObject(context, hostId);
    const BuildingType* building = host == nullptr || context.rules == nullptr
        ? nullptr : context.rules->findBuildingType(host->type);
    return host != nullptr && host->alive && host->health > 0
        && host->kind == WorldObjectKind::Structure && building != nullptr
        && building->unitRepair
        && context.resolveHostDomain
        && context.resolveHostDomain(hostId)
            == std::optional<RuntimeRepairDepotDomain>(
                building->naval || building->waterBound
                    ? RuntimeRepairDepotDomain::Naval
                    : RuntimeRepairDepotDomain::Ground);
}

bool hostRepairOperational(
    const RuntimeRepairDepotContext& context,
    const WorldObjectInstance& host
) {
    if (host.poweredOperational) return true;
    const BuildingType* building = context.rules == nullptr
        ? nullptr : context.rules->findBuildingType(host.type);
    // Unpowered naval repair hosts remain operational during a grid deficit.
    // Ground depots retain their existing raw-power requirement.
    return building != nullptr && building->unitRepair
        && (building->naval || building->waterBound) && !building->powered
        && hostSupportsRepairAnimation(context, host.instanceId);
}

const char* animationPhaseName(RuntimeRepairDepotAnimationPhase phase) {
    switch (phase) {
    case RuntimeRepairDepotAnimationPhase::Extending: return "Extending";
    case RuntimeRepairDepotAnimationPhase::Repairing: return "Repairing";
    case RuntimeRepairDepotAnimationPhase::Retracting: return "Retracting";
    }
    return "Extending";
}

std::string hexEncode(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(value.size() * 2);
    for (const unsigned char character : value) {
        output.push_back(digits[character >> 4]);
        output.push_back(digits[character & 0x0f]);
    }
    return output;
}

int hexNibble(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool hexDecode(const std::string& value, std::string& output) {
    if ((value.size() % 2U) != 0U || value.size() > 8192U) {
        return false;
    }
    output.clear();
    output.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const int high = hexNibble(value[index]);
        const int low = hexNibble(value[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        output.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

bool parseIntStrict(const std::string& value, int& output) {
    if (value.empty()) {
        return false;
    }
    const char* begin = value.data();
    const char* end = begin + value.size();
    int parsed = 0;
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    output = parsed;
    return true;
}

bool parseBoolStrict(const std::string& value, bool& output) {
    const std::string lowered = lowerAscii(value);
    if (lowered == "yes" || lowered == "true" || lowered == "1") {
        output = true;
        return true;
    }
    if (lowered == "no" || lowered == "false" || lowered == "0") {
        output = false;
        return true;
    }
    return false;
}

bool parseDomainStrict(
    const std::string& value,
    RuntimeRepairDepotDomain& output
) {
    const std::string lowered = lowerAscii(value);
    if (lowered == "ground") {
        output = RuntimeRepairDepotDomain::Ground;
        return true;
    }
    if (lowered == "naval") {
        output = RuntimeRepairDepotDomain::Naval;
        return true;
    }
    return false;
}

std::uint64_t hashText(const std::string_view text) {
    std::uint64_t hash = fnvOffset;
    for (const unsigned char character : text) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= fnvPrime;
    }
    return hash;
}

} // namespace

const char* runtimeRepairDepotVetoName(RuntimeRepairDepotVeto veto) {
    switch (veto) {
    case RuntimeRepairDepotVeto::None: return "none";
    case RuntimeRepairDepotVeto::InvalidContext: return "invalid context";
    case RuntimeRepairDepotVeto::HostMissing: return "host missing";
    case RuntimeRepairDepotVeto::HostUnavailable: return "host unavailable";
    case RuntimeRepairDepotVeto::HostNotRepairDepot: return "host is not a repair depot";
    case RuntimeRepairDepotVeto::UnitMissing: return "unit missing";
    case RuntimeRepairDepotVeto::UnitUnavailable: return "unit unavailable";
    case RuntimeRepairDepotVeto::UnitNotGroundVehicle: return "unit is not a ground vehicle";
    case RuntimeRepairDepotVeto::UnitNotRepairable: return "unit is not repairable";
    case RuntimeRepairDepotVeto::UnitAlreadyManaged: return "unit already has a repair relation";
    case RuntimeRepairDepotVeto::UnitAlreadyDocked: return "unit already has a dock relation";
    case RuntimeRepairDepotVeto::UnitTransported: return "unit is transported";
    case RuntimeRepairDepotVeto::UnitGarrisoned: return "unit is garrisoned";
    case RuntimeRepairDepotVeto::NotAllied: return "unit and depot are not allied";
    case RuntimeRepairDepotVeto::UnitAlreadyFull: return "unit is already at full health";
    case RuntimeRepairDepotVeto::CapacityFull: return "repair depot capacity is full";
    case RuntimeRepairDepotVeto::QueueCellUnavailable: return "repair queue cell is unavailable";
    case RuntimeRepairDepotVeto::DockCellUnavailable: return "dock cell is unavailable";
    case RuntimeRepairDepotVeto::PathUnavailable: return "unit cannot path to dock cell";
    case RuntimeRepairDepotVeto::ReservationMissing: return "repair reservation is missing";
    case RuntimeRepairDepotVeto::ReservationMismatch: return "repair reservation does not match host";
    case RuntimeRepairDepotVeto::UnitNotAtDock: return "unit has not reached the reserved dock cell";
    case RuntimeRepairDepotVeto::DomainUnavailable: return "repair domain is unavailable";
    case RuntimeRepairDepotVeto::DomainMismatch: return "repair host and unit domains do not match";
    case RuntimeRepairDepotVeto::InvalidTransition: return "repair host transition is invalid";
    case RuntimeRepairDepotVeto::RefundUnavailable: return "repair unit refund is unavailable";
    }
    return "unknown repair veto";
}

const char* runtimeRepairDepotActionName(RuntimeRepairDepotActionKind kind) {
    switch (kind) {
    case RuntimeRepairDepotActionKind::MoveToQueue: return "move-to-queue";
    case RuntimeRepairDepotActionKind::MoveToDock: return "move-to-dock";
    case RuntimeRepairDepotActionKind::Attach: return "attach";
    case RuntimeRepairDepotActionKind::Repair: return "repair";
    case RuntimeRepairDepotActionKind::UndockToRally: return "undock-to-rally";
    case RuntimeRepairDepotActionKind::Release: return "release";
    case RuntimeRepairDepotActionKind::DestroyDockedUnit: return "destroy-docked-unit";
    case RuntimeRepairDepotActionKind::SellDockedUnit: return "sell-docked-unit";
    case RuntimeRepairDepotActionKind::TransferDockedUnitOwner: return "transfer-docked-unit-owner";
    }
    return "unknown repair action";
}

const char* runtimeRepairDepotDomainName(RuntimeRepairDepotDomain domain) {
    switch (domain) {
    case RuntimeRepairDepotDomain::Ground: return "Ground";
    case RuntimeRepairDepotDomain::Naval: return "Naval";
    }
    return "Unknown";
}

RuntimeRepairDepotSystemState::RuntimeRepairDepotSystemState(int fixedTickHz) {
    reset(fixedTickHz);
}

void RuntimeRepairDepotSystemState::reset(int fixedTickHz) {
    fixedTickHz_ = std::max(1, fixedTickHz);
    lastAdvancedTick_ = -1;
    relations_.clear();
    hostTimers_.clear();
    animations_.clear();
}

int RuntimeRepairDepotSystemState::fixedTickHz() const {
    return fixedTickHz_;
}

RuntimeRepairDepotCommandResult RuntimeRepairDepotSystemState::reserveDock(
    const RuntimeRepairDepotContext& context,
    int hostId,
    int unitId,
    int currentTick
) {
    RuntimeRepairDepotCommandResult result;
    result.hostId = hostId;
    result.unitId = unitId;
    if (std::any_of(
            relations_.begin(),
            relations_.end(),
            [unitId](const RuntimeRepairDepotRelation& relation) {
                return relation.unitId == unitId;
            }
        )) {
        result.veto = RuntimeRepairDepotVeto::UnitAlreadyManaged;
        result.reason = vetoReason(result.veto);
        return result;
    }
    if (relations_.size() >= maximumSavedRelations) {
        result.veto = RuntimeRepairDepotVeto::CapacityFull;
        result.reason = "repair depot runtime relation limit reached";
        return result;
    }

    const PairValidation pair = validatePair(
        context,
        hostId,
        unitId,
        false,
        false,
        -1
    );
    if (pair.veto != RuntimeRepairDepotVeto::None) {
        result.veto = pair.veto;
        result.reason = vetoReason(result.veto);
        return result;
    }

    const SlotSelection dock = selectAvailableDock(
        context,
        relations_,
        hostId,
        unitId,
        pair.hostRules->numberOfDocks
    );
    if (!dock.selected()
        && dock.veto != RuntimeRepairDepotVeto::CapacityFull) {
        result.veto = dock.veto;
        result.reason = vetoReason(result.veto);
        return result;
    }

    SlotSelection queue;
    if (!dock.selected()) {
        queue = selectAvailableQueueCell(
            context,
            relations_,
            hostId,
            unitId
        );
        if (!queue.selected()) {
            result.veto = queue.veto;
            result.reason = vetoReason(result.veto);
            return result;
        }
    }

    RuntimeRepairDepotRelation relation;
    relation.hostId = hostId;
    relation.unitId = unitId;
    relation.dockIndex = dock.selected() ? dock.index : -1;
    relation.queueIndex = queue.selected() ? queue.index : -1;
    relation.phase = dock.selected()
        ? RuntimeRepairDepotRelationPhase::Reserved
        : RuntimeRepairDepotRelationPhase::Queued;
    relation.dockCell = dock.selected()
        ? dock.cell
        : RuntimeRepairDepotCell{};
    relation.queueCell = queue.selected()
        ? queue.cell
        : RuntimeRepairDepotCell{};
    relation.hostOwner = pair.host->owner;
    relation.unitOwner = pair.unit->owner;
    relation.hostType = pair.host->type;
    relation.unitType = pair.unit->type;
    relation.reservedTick = std::max(0, currentTick);
    relation.domain = pair.domain;
    relations_.push_back(relation);
    sortState();

    RuntimeRepairDepotAction action = baseAction(
        dock.selected()
            ? RuntimeRepairDepotActionKind::MoveToDock
            : RuntimeRepairDepotActionKind::MoveToQueue,
        relation,
        dock.selected()
            ? "repair dock reserved"
            : "repair queue position reserved"
    );
    action.hasTargetCell = true;
    action.targetCell = dock.selected() ? dock.cell : queue.cell;
    result.accepted = true;
    result.veto = RuntimeRepairDepotVeto::None;
    result.dockIndex = relation.dockIndex;
    result.queueIndex = relation.queueIndex;
    result.reason = dock.selected()
        ? "repair dock reserved"
        : "repair queue position reserved";
    result.actions.push_back(std::move(action));
    return result;
}

RuntimeRepairDepotCommandResult RuntimeRepairDepotSystemState::attachAtDock(
    const RuntimeRepairDepotContext& context,
    int hostId,
    int unitId,
    const RuntimeRepairDepotCell& actualCell,
    int currentTick
) {
    RuntimeRepairDepotCommandResult result;
    result.hostId = hostId;
    result.unitId = unitId;
    auto relationIt = std::find_if(
        relations_.begin(),
        relations_.end(),
        [unitId](const RuntimeRepairDepotRelation& relation) {
            return relation.unitId == unitId;
        }
    );
    if (relationIt == relations_.end()) {
        result.veto = RuntimeRepairDepotVeto::ReservationMissing;
        result.reason = vetoReason(result.veto);
        return result;
    }
    if (relationIt->hostId != hostId
        || relationIt->phase != RuntimeRepairDepotRelationPhase::Reserved) {
        result.veto = RuntimeRepairDepotVeto::ReservationMismatch;
        result.reason = vetoReason(result.veto);
        return result;
    }
    result.dockIndex = relationIt->dockIndex;
    if (!validCell(actualCell) || !(actualCell == relationIt->dockCell)) {
        result.veto = RuntimeRepairDepotVeto::UnitNotAtDock;
        result.reason = vetoReason(result.veto);
        return result;
    }
    RuntimeRepairDepotRelation arrivalRelation = *relationIt;
    const WorldObjectInstance* observedUnit = findObject(context, unitId);
    if (observedUnit != nullptr
        && observedUnit->x == relationIt->dockCell.x
        && observedUnit->y == relationIt->dockCell.y
        && observedUnit->onBridge == relationIt->dockCell.onBridge) {
        arrivalRelation.phase = RuntimeRepairDepotRelationPhase::Docked;
    }

    const PairValidation pair = validatePair(
        context,
        hostId,
        unitId,
        true,
        false,
        -1
    );
    if (pair.veto != RuntimeRepairDepotVeto::None
        || pair.host->owner != relationIt->hostOwner
        || pair.unit->owner != relationIt->unitOwner
        || !equalsAsciiIgnoreCase(pair.host->type, relationIt->hostType)
        || !equalsAsciiIgnoreCase(pair.unit->type, relationIt->unitType)
        || pair.domain != relationIt->domain) {
        result.veto = pair.veto == RuntimeRepairDepotVeto::None
            ? RuntimeRepairDepotVeto::ReservationMismatch
            : pair.veto;
        result.reason = vetoReason(result.veto);
        result.actions.push_back(releaseAction(
            context,
            arrivalRelation,
            RuntimeRepairDepotActionKind::Release,
            result.reason
        ));
        relations_.erase(relationIt);
        sortState();
        return result;
    }
    if (arrivalRelation.phase != RuntimeRepairDepotRelationPhase::Docked) {
        result.veto = RuntimeRepairDepotVeto::UnitNotAtDock;
        result.reason = vetoReason(result.veto);
        return result;
    }
    if (context.isDockOccupied
        && context.isDockOccupied(hostId, relationIt->dockIndex)) {
        result.veto = RuntimeRepairDepotVeto::CapacityFull;
        result.reason = "repair dock became occupied before arrival";
        result.actions.push_back(releaseAction(
            context,
            arrivalRelation,
            RuntimeRepairDepotActionKind::Release,
            result.reason
        ));
        relations_.erase(relationIt);
        sortState();
        return result;
    }
    if (pair.unit->health >= std::max(1, pair.unitRules->strength)) {
        result.veto = RuntimeRepairDepotVeto::UnitAlreadyFull;
        result.reason = vetoReason(result.veto);
        result.actions.push_back(releaseAction(
            context,
            arrivalRelation,
            RuntimeRepairDepotActionKind::UndockToRally,
            result.reason
        ));
        relations_.erase(relationIt);
        sortState();
        return result;
    }

    relationIt->phase = RuntimeRepairDepotRelationPhase::Docked;
    relationIt->attachedTick = std::max(0, currentTick);
    RuntimeRepairDepotAction action = baseAction(
        RuntimeRepairDepotActionKind::Attach,
        *relationIt,
        "unit attached to repair dock"
    );
    action.hasTargetCell = true;
    action.targetCell = relationIt->dockCell;
    if (std::none_of(
            hostTimers_.begin(),
            hostTimers_.end(),
            [hostId](const HostTimer& timer) {
                return timer.hostId == hostId;
            }
        )) {
        hostTimers_.push_back({hostId, 0});
    }
    if (hostSupportsRepairAnimation(context, hostId)) {
        startAnimation(hostId);
    }
    sortState();

    result.accepted = true;
    result.reason = "unit attached to repair dock";
    result.actions.push_back(std::move(action));
    return result;
}

std::vector<RuntimeRepairDepotAction>
RuntimeRepairDepotSystemState::releaseUnit(
    const RuntimeRepairDepotContext& context,
    int unitId,
    const std::string& reason
) {
    std::vector<RuntimeRepairDepotAction> actions;
    auto relationIt = std::find_if(
        relations_.begin(),
        relations_.end(),
        [unitId](const RuntimeRepairDepotRelation& relation) {
            return relation.unitId == unitId;
        }
    );
    if (relationIt == relations_.end()) {
        return actions;
    }
    actions.push_back(releaseAction(
        context,
        *relationIt,
        RuntimeRepairDepotActionKind::Release,
        reason
    ));
    relations_.erase(relationIt);
    sortState();
    const RuntimeRepairDepotReconcileReport report = reconcile(context);
    updateAnimationRequests(context, actions);
    actions.insert(
        actions.end(),
        report.actions.begin(),
        report.actions.end()
    );
    return actions;
}

RuntimeRepairDepotHostTransitionResult
RuntimeRepairDepotSystemState::transitionHost(
    const RuntimeRepairDepotContext& context,
    const RuntimeRepairDepotHostTransitionRequest& request
) {
    RuntimeRepairDepotHostTransitionResult result;
    result.hostId = request.hostId;
    const auto fail = [&](RuntimeRepairDepotVeto veto,
                          const std::string& reason) {
        result.accepted = false;
        result.veto = veto;
        result.reason = reason;
        result.actions.clear();
        return result;
    };

    if (context.rules == nullptr || context.objects == nullptr) {
        return fail(
            RuntimeRepairDepotVeto::InvalidContext,
            vetoReason(RuntimeRepairDepotVeto::InvalidContext)
        );
    }
    if (request.hostId < 0
        || request.expectedOwner.empty()
        || (request.temporalRemoval
            && request.kind
                != RuntimeRepairDepotHostTransitionKind::Destroyed)
        || (request.kind == RuntimeRepairDepotHostTransitionKind::Captured
            && request.newOwner.empty())) {
        return fail(
            RuntimeRepairDepotVeto::InvalidTransition,
            vetoReason(RuntimeRepairDepotVeto::InvalidTransition)
        );
    }

    const WorldObjectInstance* host = findObject(context, request.hostId);
    if (host == nullptr) {
        return fail(
            RuntimeRepairDepotVeto::HostMissing,
            vetoReason(RuntimeRepairDepotVeto::HostMissing)
        );
    }
    if (host->kind != WorldObjectKind::Structure
        || !equalsAsciiIgnoreCase(host->owner, request.expectedOwner)
        || (request.kind == RuntimeRepairDepotHostTransitionKind::Captured
            && (!host->alive || host->health <= 0))) {
        return fail(
            RuntimeRepairDepotVeto::HostUnavailable,
            "repair host transition no longer matches the expected host"
        );
    }
    const BuildingType* hostRules =
        context.rules->findBuildingType(host->type);
    if (hostRules == nullptr
        || !hostRules->unitRepair
        || hostRules->numberOfDocks <= 0) {
        return fail(
            RuntimeRepairDepotVeto::HostNotRepairDepot,
            vetoReason(RuntimeRepairDepotVeto::HostNotRepairDepot)
        );
    }
    if (!context.resolveHostDomain || !context.resolveUnitDomain) {
        return fail(
            RuntimeRepairDepotVeto::DomainUnavailable,
            vetoReason(RuntimeRepairDepotVeto::DomainUnavailable)
        );
    }
    const std::optional<RuntimeRepairDepotDomain> hostDomain =
        context.resolveHostDomain(request.hostId);
    if (!hostDomain.has_value()) {
        return fail(
            RuntimeRepairDepotVeto::DomainUnavailable,
            vetoReason(RuntimeRepairDepotVeto::DomainUnavailable)
        );
    }

    std::vector<RuntimeRepairDepotRelation> candidates;
    for (const RuntimeRepairDepotRelation& relation : relations_) {
        if (relation.hostId == request.hostId) {
            candidates.push_back(relation);
        }
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const auto& left, const auto& right) {
            if (left.phase != right.phase) {
                return static_cast<int>(left.phase)
                    < static_cast<int>(right.phase);
            }
            if (left.dockIndex != right.dockIndex) {
                return left.dockIndex < right.dockIndex;
            }
            return left.unitId < right.unitId;
        }
    );

    std::vector<RuntimeRepairDepotAction> actions;
    actions.reserve(candidates.size());
    for (const RuntimeRepairDepotRelation& relation : candidates) {
        const WorldObjectInstance* unit = findObject(context, relation.unitId);
        const std::optional<RuntimeRepairDepotDomain> unitDomain =
            context.resolveUnitDomain(relation.unitId);
        const bool validQueued =
            relation.phase == RuntimeRepairDepotRelationPhase::Queued
            && relation.dockIndex == -1
            && !validCell(relation.dockCell)
            && relation.queueIndex >= 0
            && relation.queueIndex < maximumQueueSlotsPerHost
            && validCell(relation.queueCell);
        const bool validDock =
            relation.phase != RuntimeRepairDepotRelationPhase::Queued
            && relation.dockIndex >= 0
            && relation.dockIndex < hostRules->numberOfDocks
            && validCell(relation.dockCell)
            && relation.queueIndex == -1
            && !validCell(relation.queueCell);
        const bool validDockMirror =
            relation.phase != RuntimeRepairDepotRelationPhase::Docked
            || (unit != nullptr
                && unit->dockedToObjectId == relation.hostId
                && unit->dockIndex == relation.dockIndex
                && unit->x == relation.dockCell.x
                && unit->y == relation.dockCell.y
                && unit->onBridge == relation.dockCell.onBridge);
        if (relation.domain != *hostDomain
            || !unitDomain.has_value()
            || *unitDomain != relation.domain) {
            return fail(
                RuntimeRepairDepotVeto::DomainMismatch,
                "repair host transition relation domain mismatch"
            );
        }
        if (unit == nullptr
            || !unit->alive
            || unit->health <= 0
            || unit->kind != WorldObjectKind::Unit
            || !equalsAsciiIgnoreCase(host->type, relation.hostType)
            || !equalsAsciiIgnoreCase(unit->type, relation.unitType)
            || !equalsAsciiIgnoreCase(
                relation.hostOwner,
                request.expectedOwner
            )
            || !equalsAsciiIgnoreCase(unit->owner, relation.unitOwner)
            || (!validQueued && !validDock)
            || !validDockMirror) {
            return fail(
                RuntimeRepairDepotVeto::InvalidTransition,
                "repair host transition relation preflight failed"
            );
        }

        RuntimeRepairDepotAction action;
        if (relation.phase != RuntimeRepairDepotRelationPhase::Docked) {
            action = releaseAction(
                context,
                relation,
                RuntimeRepairDepotActionKind::Release,
                "repair host transition released waiting unit"
            );
        } else if (request.kind
                == RuntimeRepairDepotHostTransitionKind::Captured) {
            action = baseAction(
                RuntimeRepairDepotActionKind::TransferDockedUnitOwner,
                relation,
                "repair host capture transferred docked unit"
            );
            action.newOwner = request.newOwner;
        } else if (request.temporalRemoval) {
            action = releaseAction(
                context,
                relation,
                RuntimeRepairDepotActionKind::Release,
                "temporal repair host removal released docked unit"
            );
        } else if (relation.domain == RuntimeRepairDepotDomain::Naval) {
            action = releaseAction(
                context,
                relation,
                RuntimeRepairDepotActionKind::UndockToRally,
                request.kind == RuntimeRepairDepotHostTransitionKind::Sold
                    ? "sold naval repair host released docked unit"
                    : "destroyed naval repair host released docked unit"
            );
            if (!action.hasTargetCell) {
                action.kind = RuntimeRepairDepotActionKind::Release;
            }
        } else if (request.kind
                == RuntimeRepairDepotHostTransitionKind::Sold) {
            if (!context.refundValue) {
                return fail(
                    RuntimeRepairDepotVeto::RefundUnavailable,
                    vetoReason(RuntimeRepairDepotVeto::RefundUnavailable)
                );
            }
            const std::optional<int> refund =
                context.refundValue(relation.unitId);
            if (!refund.has_value() || *refund < 0) {
                return fail(
                    RuntimeRepairDepotVeto::RefundUnavailable,
                    vetoReason(RuntimeRepairDepotVeto::RefundUnavailable)
                );
            }
            action = baseAction(
                RuntimeRepairDepotActionKind::SellDockedUnit,
                relation,
                "sold repair host sold docked ground unit"
            );
            action.owner = economyOwnerId(context, relation.unitOwner);
            action.refundValue = *refund;
        } else {
            action = baseAction(
                RuntimeRepairDepotActionKind::DestroyDockedUnit,
                relation,
                "destroyed repair host destroyed docked ground unit"
            );
        }
        actions.push_back(std::move(action));
    }

    if (request.kind == RuntimeRepairDepotHostTransitionKind::Captured) {
        relations_.erase(
            std::remove_if(
                relations_.begin(),
                relations_.end(),
                [&](const RuntimeRepairDepotRelation& relation) {
                    return relation.hostId == request.hostId
                        && relation.phase
                            != RuntimeRepairDepotRelationPhase::Docked;
                }
            ),
            relations_.end()
        );
        for (RuntimeRepairDepotRelation& relation : relations_) {
            if (relation.hostId == request.hostId
                && relation.phase
                    == RuntimeRepairDepotRelationPhase::Docked) {
                relation.hostOwner = request.newOwner;
                relation.unitOwner = request.newOwner;
            }
        }
    } else {
        relations_.erase(
            std::remove_if(
                relations_.begin(),
                relations_.end(),
                [&](const RuntimeRepairDepotRelation& relation) {
                    return relation.hostId == request.hostId;
                }
            ),
            relations_.end()
        );
    }

    const bool hasDockedRelation = std::any_of(
        relations_.begin(),
        relations_.end(),
        [&](const RuntimeRepairDepotRelation& relation) {
            return relation.hostId == request.hostId
                && relation.phase == RuntimeRepairDepotRelationPhase::Docked;
        }
    );
    if (!hasDockedRelation) {
        hostTimers_.erase(
            std::remove_if(
                hostTimers_.begin(),
                hostTimers_.end(),
                [&](const HostTimer& timer) {
                    return timer.hostId == request.hostId;
                }
            ),
            hostTimers_.end()
        );
    }
    if (request.kind != RuntimeRepairDepotHostTransitionKind::Captured) {
        std::erase_if(animations_, [&](const auto& animation) {
            return animation.hostId == request.hostId;
        });
    }
    sortState();
    result.accepted = true;
    result.veto = RuntimeRepairDepotVeto::None;
    result.reason = request.kind
            == RuntimeRepairDepotHostTransitionKind::Captured
        ? "repair host capture transitioned"
        : request.kind == RuntimeRepairDepotHostTransitionKind::Sold
            ? "repair host sale transitioned"
            : "repair host destruction transitioned";
    result.actions = std::move(actions);
    return result;
}

bool RuntimeRepairDepotSystemState::claimsDock(
    int hostId,
    int dockIndex
) const {
    if (hostId < 0 || dockIndex < 0) {
        return false;
    }
    return std::any_of(
        relations_.begin(),
        relations_.end(),
        [hostId, dockIndex](const RuntimeRepairDepotRelation& relation) {
            return relation.hostId == hostId
                && relation.dockIndex == dockIndex
                && relation.phase
                    != RuntimeRepairDepotRelationPhase::Queued;
        }
    );
}

void RuntimeRepairDepotSystemState::startAnimation(int hostId) {
    const auto animation = std::find_if(
        animations_.begin(), animations_.end(), [hostId](const auto& entry) {
            return entry.hostId == hostId;
        });
    if (animation == animations_.end()) {
        if (animations_.size() < maximumSavedRelations) {
            animations_.push_back({hostId,
                RuntimeRepairDepotAnimationPhase::Extending, 0});
        }
    } else if (animation->phase == RuntimeRepairDepotAnimationPhase::Retracting) {
        animation->phase = RuntimeRepairDepotAnimationPhase::Extending;
        animation->ageTicks = 0;
    }
}

void RuntimeRepairDepotSystemState::reconcileAnimations(
    const RuntimeRepairDepotContext& context
) {
    std::erase_if(animations_, [&](const auto& animation) {
        return !hostSupportsRepairAnimation(context, animation.hostId);
    });
}

void RuntimeRepairDepotSystemState::advanceAnimations(
    const RuntimeRepairDepotContext& context,
    int elapsedTicks
) {
    std::set<int> completed;
    for (auto& animation : animations_) {
        const auto* host = findObject(context, animation.hostId);
        if (host == nullptr || !hostRepairOperational(context, *host)) {
            continue;
        }
        const long long age = static_cast<long long>(animation.ageTicks)
            + std::max(0, elapsedTicks);
        if (animation.phase == RuntimeRepairDepotAnimationPhase::Repairing) {
            animation.ageTicks = static_cast<int>(std::min<long long>(
                age, std::numeric_limits<int>::max()));
            continue;
        }
        const int duration = std::max(1, context.animationDurationTicks
            ? context.animationDurationTicks(animation.hostId, animation.phase)
            : std::max(1, fixedTickHz_ / 2));
        if (age < duration) {
            animation.ageTicks = static_cast<int>(age);
            continue;
        }
        if (animation.phase == RuntimeRepairDepotAnimationPhase::Retracting) {
            completed.insert(animation.hostId);
            continue;
        }
        animation.phase = RuntimeRepairDepotAnimationPhase::Repairing;
        animation.ageTicks = static_cast<int>(std::min<long long>(
            age - duration, std::numeric_limits<int>::max()));
    }
    std::erase_if(animations_, [&](const auto& animation) {
        return completed.contains(animation.hostId);
    });
}

void RuntimeRepairDepotSystemState::updateAnimationRequests(
    const RuntimeRepairDepotContext& context,
    const std::vector<RuntimeRepairDepotAction>& actions
) {
    if (context.rules == nullptr) {
        return;
    }
    // Repair actions have not reached the world yet. Use their resulting HP
    // and remaining account balance so the last paid pulse stops the arm at
    // the same simulation boundary without changing the repair calculation.
    std::map<int, int> healthAfter;
    std::map<std::string, int> spentCredits;
    for (const auto& action : actions) {
        if (action.kind == RuntimeRepairDepotActionKind::Repair) {
            healthAfter[action.unitId] = action.healthAfter;
            const std::string owner = economyOwnerId(context, action.owner);
            spentCredits[owner] = static_cast<int>(std::min<long long>(
                static_cast<long long>(spentCredits[owner]) + action.creditCost,
                std::numeric_limits<int>::max()));
        }
    }
    std::set<int> activeHosts;
    for (const auto& relation : relations_) {
        if (relation.phase != RuntimeRepairDepotRelationPhase::Docked
            || !hostSupportsRepairAnimation(context, relation.hostId)) {
            continue;
        }
        const auto* host = findObject(context, relation.hostId);
        const auto* unit = findObject(context, relation.unitId);
        const auto* unitRules = unit == nullptr ? nullptr
            : context.rules->findUnitType("Vehicle", unit->type);
        if (host == nullptr || !hostRepairOperational(context, *host)
            || unit == nullptr || unitRules == nullptr) {
            continue;
        }
        const std::string owner = economyOwnerId(context, relation.unitOwner);
        const int balance = context.creditBalance
            ? std::max(0, context.creditBalance(owner)) : 0;
        const auto changedHealth = healthAfter.find(unit->instanceId);
        const int health = changedHealth == healthAfter.end()
            ? unit->health : changedHealth->second;
        if (calculateRepairCharge(*unitRules,
                context.rules->getSpecialMechanismRules(), health,
                std::max(0, balance - spentCredits[owner])).healedHitPoints > 0) {
            activeHosts.insert(relation.hostId);
        }
    }
    for (const int hostId : activeHosts) {
        startAnimation(hostId);
    }
    for (auto& animation : animations_) {
        const auto* host = findObject(context, animation.hostId);
        if (host != nullptr && hostRepairOperational(context, *host)
            && !activeHosts.contains(animation.hostId)
            && animation.phase != RuntimeRepairDepotAnimationPhase::Retracting) {
            animation.phase = RuntimeRepairDepotAnimationPhase::Retracting;
            animation.ageTicks = 0;
        }
    }
}

RuntimeRepairDepotReconcileReport RuntimeRepairDepotSystemState::reconcile(
    const RuntimeRepairDepotContext& context
) {
    std::size_t remainingQueuePromotions = context.allowQueuePromotion
        ? context.maximumQueuePromotions
        : 0;
    return reconcileWithPromotionBudget(
        context,
        remainingQueuePromotions
    );
}

RuntimeRepairDepotReconcileReport
RuntimeRepairDepotSystemState::reconcileWithPromotionBudget(
    const RuntimeRepairDepotContext& context,
    std::size_t& remainingQueuePromotions
) {
    RuntimeRepairDepotReconcileReport report;
    sortState();
    std::vector<RuntimeRepairDepotRelation> retained;
    retained.reserve(relations_.size());
    std::set<std::pair<int, int>> occupiedDocks;
    std::set<std::pair<int, int>> occupiedQueueSlots;
    std::set<std::tuple<int, int, bool>> occupiedQueueCells;
    std::set<int> managedUnits;

    for (const RuntimeRepairDepotRelation& relation : relations_) {
        std::string invalidReason;
        const PairValidation pair = validatePair(
            context,
            relation.hostId,
            relation.unitId,
            true,
            relation.phase == RuntimeRepairDepotRelationPhase::Docked,
            relation.dockIndex
        );
        if (pair.veto != RuntimeRepairDepotVeto::None) {
            invalidReason = vetoReason(pair.veto);
        } else if (pair.host->owner != relation.hostOwner) {
            invalidReason = "repair host owner changed";
        } else if (pair.unit->owner != relation.unitOwner) {
            invalidReason = "repair unit owner changed";
        } else if (!equalsAsciiIgnoreCase(pair.host->type, relation.hostType)
            || !equalsAsciiIgnoreCase(pair.unit->type, relation.unitType)) {
            invalidReason = "repair relation object type changed";
        } else if (pair.domain != relation.domain) {
            invalidReason = "repair relation domain changed";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Queued
            && (relation.dockIndex != -1
                || validCell(relation.dockCell)
                || relation.queueIndex < 0
                || relation.queueIndex >= maximumQueueSlotsPerHost
                || !validCell(relation.queueCell))) {
            invalidReason = "repair queue relation has invalid slots";
        } else if (relation.phase != RuntimeRepairDepotRelationPhase::Queued
            && (relation.dockIndex < 0
                || relation.dockIndex >= pair.hostRules->numberOfDocks
                || !validCell(relation.dockCell)
                || relation.queueIndex != -1
                || validCell(relation.queueCell))) {
            invalidReason = "repair dock index is no longer valid";
        } else if (relation.phase != RuntimeRepairDepotRelationPhase::Docked
            && (pair.unit->dockedToObjectId >= 0
                || pair.unit->dockIndex >= 0)) {
            invalidReason = "waiting repair unit acquired another dock";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Queued
            && (!context.resolveQueueCell
                || context.resolveQueueCell(
                    relation.hostId,
                    relation.queueIndex,
                    relation.unitId
                ) != std::optional<RuntimeRepairDepotCell>(
                    relation.queueCell
                ))) {
            invalidReason = "repair queue cell is no longer available";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Queued
            && context.isReservationActive
            && !context.isReservationActive(
                relation.hostId,
                relation.queueIndex,
                relation.unitId
            )) {
            invalidReason = "repair queue command was cancelled";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Queued
            && (!context.canPathTo
                || !context.canPathTo(relation.unitId, relation.queueCell))) {
            invalidReason = "repair queue path is no longer available";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Reserved
            && context.isReservationActive
            && !context.isReservationActive(
                relation.hostId,
                relation.dockIndex,
                relation.unitId
            )) {
            invalidReason = "repair move command was cancelled";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Reserved
            && context.isDockOccupied
            && context.isDockOccupied(
                relation.hostId,
                relation.dockIndex
            )) {
            invalidReason = "reserved repair dock became occupied";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Reserved
            && (!context.canPathTo
                || !context.canPathTo(relation.unitId, relation.dockCell))) {
            invalidReason = "reserved repair path is no longer available";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Docked
            && (pair.unit->dockedToObjectId != relation.hostId
                || pair.unit->dockIndex != relation.dockIndex)) {
            invalidReason = "docked repair relation is not mirrored by object state";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Docked
            && (pair.unit->x != relation.dockCell.x
                || pair.unit->y != relation.dockCell.y
                || pair.unit->onBridge != relation.dockCell.onBridge)) {
            invalidReason = "docked repair unit left its repair cell";
        } else if (!managedUnits.insert(relation.unitId).second) {
            invalidReason = "unit has duplicate repair relations";
        } else if (relation.phase == RuntimeRepairDepotRelationPhase::Queued
            && (!occupiedQueueSlots.insert({
                    relation.hostId,
                    relation.queueIndex
                }).second
                || !occupiedQueueCells.insert({
                    relation.queueCell.x,
                    relation.queueCell.y,
                    relation.queueCell.onBridge
                }).second)) {
            invalidReason = "repair queue has duplicate positions";
        } else if (relation.phase != RuntimeRepairDepotRelationPhase::Queued
            && !occupiedDocks.insert({
                relation.hostId,
                relation.dockIndex
            }).second) {
            invalidReason = "repair dock has duplicate relations";
        }

        if (!invalidReason.empty()) {
            ++report.releasedRelations;
            report.actions.push_back(releaseAction(
                context,
                relation,
                RuntimeRepairDepotActionKind::Release,
                invalidReason
            ));
            continue;
        }
        retained.push_back(relation);
        ++report.retainedRelations;
    }
    relations_ = std::move(retained);

    std::vector<int> queuedUnitIds;
    for (const RuntimeRepairDepotRelation& relation : relations_) {
        if (relation.phase == RuntimeRepairDepotRelationPhase::Queued) {
            queuedUnitIds.push_back(relation.unitId);
        }
    }
    std::sort(
        queuedUnitIds.begin(),
        queuedUnitIds.end(),
        [&](int leftUnitId, int rightUnitId) {
            const auto findQueued = [&](int unitId) {
                return std::find_if(
                    relations_.begin(),
                    relations_.end(),
                    [unitId](const RuntimeRepairDepotRelation& relation) {
                        return relation.unitId == unitId;
                    }
                );
            };
            const auto left = findQueued(leftUnitId);
            const auto right = findQueued(rightUnitId);
            if (left == relations_.end() || right == relations_.end()) {
                return leftUnitId < rightUnitId;
            }
            if (left->hostId != right->hostId) {
                return left->hostId < right->hostId;
            }
            if (left->reservedTick != right->reservedTick) {
                return left->reservedTick < right->reservedTick;
            }
            return left->unitId < right->unitId;
        }
    );

    std::vector<int> promotionFailures;
    for (const int unitId : queuedUnitIds) {
        if (remainingQueuePromotions == 0) {
            break;
        }
        auto relation = std::find_if(
            relations_.begin(),
            relations_.end(),
            [unitId](const RuntimeRepairDepotRelation& candidate) {
                return candidate.unitId == unitId
                    && candidate.phase
                        == RuntimeRepairDepotRelationPhase::Queued;
            }
        );
        if (relation == relations_.end()) {
            continue;
        }
        const PairValidation pair = validatePair(
            context,
            relation->hostId,
            relation->unitId,
            true,
            false,
            -1
        );
        if (pair.veto != RuntimeRepairDepotVeto::None) {
            promotionFailures.push_back(relation->unitId);
            report.actions.push_back(releaseAction(
                context,
                *relation,
                RuntimeRepairDepotActionKind::Release,
                vetoReason(pair.veto)
            ));
            continue;
        }
        const SlotSelection dock = selectAvailableDock(
            context,
            relations_,
            relation->hostId,
            relation->unitId,
            pair.hostRules->numberOfDocks
        );
        if (!dock.selected()) {
            if (dock.veto == RuntimeRepairDepotVeto::CapacityFull
                || dock.veto
                    == RuntimeRepairDepotVeto::DockCellUnavailable) {
                // A vehicle that has just completed service may still be
                // physically clearing the authored dock. Keep the queue
                // reservation until the exact cell becomes available again.
                continue;
            }
            promotionFailures.push_back(relation->unitId);
            report.actions.push_back(releaseAction(
                context,
                *relation,
                RuntimeRepairDepotActionKind::Release,
                vetoReason(dock.veto)
            ));
            continue;
        }

        relation->phase = RuntimeRepairDepotRelationPhase::Reserved;
        relation->dockIndex = dock.index;
        relation->dockCell = dock.cell;
        relation->queueIndex = -1;
        relation->queueCell = {};
        RuntimeRepairDepotAction action = baseAction(
            RuntimeRepairDepotActionKind::MoveToDock,
            *relation,
            "repair queue promoted to dock"
        );
        action.hasTargetCell = true;
        action.targetCell = dock.cell;
        report.actions.push_back(std::move(action));
        --remainingQueuePromotions;
    }
    if (!promotionFailures.empty()) {
        std::sort(promotionFailures.begin(), promotionFailures.end());
        relations_.erase(
            std::remove_if(
                relations_.begin(),
                relations_.end(),
                [&](const RuntimeRepairDepotRelation& relation) {
                    return std::binary_search(
                        promotionFailures.begin(),
                        promotionFailures.end(),
                        relation.unitId
                    );
                }
            ),
            relations_.end()
        );
        report.releasedRelations += promotionFailures.size();
        report.retainedRelations -= std::min(
            report.retainedRelations,
            promotionFailures.size()
        );
    }

    std::set<int> hostsWithDockedUnits;
    for (const RuntimeRepairDepotRelation& relation : relations_) {
        if (relation.phase == RuntimeRepairDepotRelationPhase::Docked) {
            hostsWithDockedUnits.insert(relation.hostId);
        }
    }
    hostTimers_.erase(
        std::remove_if(
            hostTimers_.begin(),
            hostTimers_.end(),
            [&](const HostTimer& timer) {
                if (hostsWithDockedUnits.find(timer.hostId)
                    != hostsWithDockedUnits.end()) {
                    return false;
                }
                ++report.removedHostTimers;
                return true;
            }
        ),
        hostTimers_.end()
    );
    for (const int hostId : hostsWithDockedUnits) {
        if (std::none_of(
                hostTimers_.begin(),
                hostTimers_.end(),
                [hostId](const HostTimer& timer) {
                    return timer.hostId == hostId;
                }
            )) {
            hostTimers_.push_back({hostId, 0});
        }
    }
    const std::size_t animationCountBefore = animations_.size();
    reconcileAnimations(context);
    report.removedHostAnimations = animationCountBefore - animations_.size();
    sortState();
    return report;
}

std::vector<RuntimeRepairDepotAction> RuntimeRepairDepotSystemState::tick(
    const RuntimeRepairDepotContext& context,
    int currentTick
) {
    std::size_t remainingQueuePromotions = context.allowQueuePromotion
        ? context.maximumQueuePromotions
        : 0;
    RuntimeRepairDepotReconcileReport reconcileReport =
        reconcileWithPromotionBudget(context, remainingQueuePromotions);
    std::vector<RuntimeRepairDepotAction> actions =
        std::move(reconcileReport.actions);
    if (context.rules == nullptr || context.objects == nullptr) {
        return actions;
    }
    if (currentTick < 0 || currentTick <= lastAdvancedTick_) {
        return actions;
    }
    const int elapsedTicks = lastAdvancedTick_ < 0
        ? 1
        : std::max(1, currentTick - lastAdvancedTick_);
    lastAdvancedTick_ = currentTick;
    advanceAnimations(context, elapsedTicks);

    std::map<std::string, int> creditLedger;
    const auto availableCredits = [&](const std::string& owner) {
        const std::string ownerKey = economyOwnerId(context, owner);
        const auto found = creditLedger.find(ownerKey);
        if (found != creditLedger.end()) {
            return found->second;
        }
        const int balance = context.creditBalance
            ? std::max(0, context.creditBalance(ownerKey))
            : 0;
        creditLedger.emplace(ownerKey, balance);
        return balance;
    };

    std::vector<int> completedUnits;
    for (HostTimer& timer : hostTimers_) {
        const WorldObjectInstance* host = findObject(context, timer.hostId);
        const BuildingType* building = host == nullptr
            ? nullptr
            : context.rules->findBuildingType(host->type);
        if (host == nullptr
            || building == nullptr
            || !host->alive
            || !hostRepairOperational(context, *host)) {
            continue;
        }
        if (timer.remainingTicks > 0) {
            timer.remainingTicks -= std::min(
                timer.remainingTicks,
                elapsedTicks
            );
            if (timer.remainingTicks > 0) {
                continue;
            }
        }
        timer.remainingTicks = serviceIntervalTicks(
            fixedTickHz_,
            context.rules->getSpecialMechanismRules()
        );

        std::vector<RuntimeRepairDepotRelation*> docked;
        for (RuntimeRepairDepotRelation& relation : relations_) {
            if (relation.hostId == timer.hostId
                && relation.phase == RuntimeRepairDepotRelationPhase::Docked) {
                docked.push_back(&relation);
            }
        }
        std::sort(
            docked.begin(),
            docked.end(),
            [](const auto* left, const auto* right) {
                return left->unitId < right->unitId;
            }
        );

        for (RuntimeRepairDepotRelation* relation : docked) {
            const WorldObjectInstance* unit = findObject(
                context,
                relation->unitId
            );
            const UnitType* unitRules = unit == nullptr
                ? nullptr
                : context.rules->findUnitType("Vehicle", unit->type);
            if (unit == nullptr || unitRules == nullptr) {
                continue;
            }
            const int maxHealth = std::max(1, unitRules->strength);
            const bool completionNeedsRallyTarget = context.resolveRallyCell
                && relation->domain == RuntimeRepairDepotDomain::Ground
                && (equalsAsciiIgnoreCase(host->type, "GADEPT")
                    || equalsAsciiIgnoreCase(host->type, "NADEPT"));
            if (unit->health >= maxHealth) {
                RuntimeRepairDepotAction complete = releaseAction(
                    context,
                    *relation,
                    RuntimeRepairDepotActionKind::UndockToRally,
                    "repair complete"
                );
                complete.healthBefore = unit->health;
                complete.healthAfter = unit->health;
                if (!completionNeedsRallyTarget || complete.hasTargetCell) {
                    actions.push_back(std::move(complete));
                    completedUnits.push_back(relation->unitId);
                }
                continue;
            }

            const int balance = availableCredits(relation->unitOwner);
            const RepairCharge charge = calculateRepairCharge(
                *unitRules,
                context.rules->getSpecialMechanismRules(),
                unit->health,
                balance
            );
            if (charge.healedHitPoints <= 0) {
                continue;
            }
            RuntimeRepairDepotAction repair = baseAction(
                RuntimeRepairDepotActionKind::Repair,
                *relation,
                "repair pulse"
            );
            repair.healthBefore = unit->health;
            repair.healthAfter = std::min(
                maxHealth,
                unit->health + charge.healedHitPoints
            );
            repair.healedHitPoints =
                repair.healthAfter - repair.healthBefore;
            repair.creditCost = charge.creditCost;
            repair.owner = economyOwnerId(context, relation->unitOwner);
            repair.firstRepairPulse = !relation->hasRepaired;
            relation->hasRepaired = true;
            creditLedger[repair.owner] =
                std::max(0, balance - repair.creditCost);
            actions.push_back(repair);

            if (repair.healthAfter >= maxHealth) {
                RuntimeRepairDepotAction complete = releaseAction(
                    context,
                    *relation,
                    RuntimeRepairDepotActionKind::UndockToRally,
                    "repair complete"
                );
                complete.healthBefore = repair.healthAfter;
                complete.healthAfter = repair.healthAfter;
                // A full repair is already paid even when every native exit
                // is blocked. Retain the dock and retry on the service clock;
                // the full-health branch above never charges another pulse.
                if (!completionNeedsRallyTarget || complete.hasTargetCell) {
                    actions.push_back(std::move(complete));
                    completedUnits.push_back(relation->unitId);
                }
            }
        }
    }

    if (!completedUnits.empty()) {
        std::sort(completedUnits.begin(), completedUnits.end());
        completedUnits.erase(
            std::unique(completedUnits.begin(), completedUnits.end()),
            completedUnits.end()
        );
        relations_.erase(
            std::remove_if(
                relations_.begin(),
                relations_.end(),
                [&](const RuntimeRepairDepotRelation& relation) {
                    return std::binary_search(
                        completedUnits.begin(),
                        completedUnits.end(),
                        relation.unitId
                    );
                }
            ),
            relations_.end()
        );
        RuntimeRepairDepotReconcileReport completionReconcile =
            reconcileWithPromotionBudget(
                context,
                remainingQueuePromotions
            );
        actions.insert(
            actions.end(),
            std::make_move_iterator(
                completionReconcile.actions.begin()
            ),
            std::make_move_iterator(
                completionReconcile.actions.end()
            )
        );
    }
    updateAnimationRequests(context, actions);
    sortState();
    return actions;
}

void RuntimeRepairDepotSystemState::save(std::ostream& output) const {
    std::vector<RuntimeRepairDepotRelation> relations = relations_;
    std::vector<HostTimer> timers = hostTimers_;
    std::vector<RuntimeRepairDepotAnimationState> animations = animations_;
    std::sort(animations.begin(), animations.end(),
        [](const auto& left, const auto& right) {
            return left.hostId < right.hostId;
        });
    std::sort(
        relations.begin(),
        relations.end(),
        [](const auto& left, const auto& right) {
            if (left.hostId != right.hostId) {
                return left.hostId < right.hostId;
            }
            if (left.unitId != right.unitId) {
                return left.unitId < right.unitId;
            }
            return left.dockIndex < right.dockIndex;
        }
    );
    std::sort(
        timers.begin(),
        timers.end(),
        [](const HostTimer& left, const HostTimer& right) {
            return left.hostId < right.hostId;
        }
    );

    output << "[RepairDepotRuntime]\n";
    output << "Version=4\n";
    output << "FixedTickHz=" << fixedTickHz_ << "\n";
    output << "LastAdvancedTick=" << lastAdvancedTick_ << "\n";
    output << "RelationCount=" << relations.size() << "\n";
    output << "HostTimerCount=" << timers.size() << "\n";
    output << "AnimationCount=" << animations.size() << "\n\n";
    for (std::size_t index = 0; index < relations.size(); ++index) {
        const RuntimeRepairDepotRelation& relation = relations[index];
        output << "[RepairDepotRuntime.Relation." << index << "]\n";
        output << "HostId=" << relation.hostId << "\n";
        output << "UnitId=" << relation.unitId << "\n";
        output << "DockIndex=" << relation.dockIndex << "\n";
        output << "Phase="
               << (relation.phase == RuntimeRepairDepotRelationPhase::Queued
                       ? "Queued"
                       : relation.phase
                                == RuntimeRepairDepotRelationPhase::Docked
                           ? "Docked"
                           : "Reserved")
               << "\n";
        output << "DockX=" << relation.dockCell.x << "\n";
        output << "DockY=" << relation.dockCell.y << "\n";
        output << "DockBridge="
               << (relation.dockCell.onBridge ? "yes" : "no") << "\n";
        output << "QueueIndex=" << relation.queueIndex << "\n";
        output << "QueueX=" << relation.queueCell.x << "\n";
        output << "QueueY=" << relation.queueCell.y << "\n";
        output << "QueueBridge="
               << (relation.queueCell.onBridge ? "yes" : "no") << "\n";
        output << "HostOwnerHex=" << hexEncode(relation.hostOwner) << "\n";
        output << "UnitOwnerHex=" << hexEncode(relation.unitOwner) << "\n";
        output << "HostTypeHex=" << hexEncode(relation.hostType) << "\n";
        output << "UnitTypeHex=" << hexEncode(relation.unitType) << "\n";
        output << "ReservedTick=" << relation.reservedTick << "\n";
        output << "AttachedTick=" << relation.attachedTick << "\n";
        output << "HasRepaired="
               << (relation.hasRepaired ? "yes" : "no") << "\n";
        output << "Domain="
               << runtimeRepairDepotDomainName(relation.domain)
               << "\n\n";
    }
    for (std::size_t index = 0; index < timers.size(); ++index) {
        output << "[RepairDepotRuntime.Timer." << index << "]\n";
        output << "HostId=" << timers[index].hostId << "\n";
        output << "RemainingTicks=" << timers[index].remainingTicks << "\n\n";
    }
    for (std::size_t index = 0; index < animations.size(); ++index) {
        output << "[RepairDepotRuntime.Animation." << index << "]\n";
        output << "HostId=" << animations[index].hostId << "\n";
        output << "Phase=" << animationPhaseName(animations[index].phase) << "\n";
        output << "AgeTicks=" << animations[index].ageTicks << "\n\n";
    }
}

bool RuntimeRepairDepotSystemState::load(
    const IniFile& ini,
    const RuntimeRepairDepotContext& context,
    std::string* errorMessage,
    RuntimeRepairDepotReconcileReport* reconcileReport
) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (reconcileReport != nullptr) {
        *reconcileReport = {};
    }
    if (ini.getSection("RepairDepotRuntime") == nullptr) {
        relations_.clear();
        hostTimers_.clear();
        animations_.clear();
        lastAdvancedTick_ = -1;
        if (reconcileReport != nullptr) {
            *reconcileReport = {};
        }
        return true;
    }
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    const auto value = [&](
        const std::string& section,
        const std::string& key,
        const std::string& fallback = std::string()
    ) {
        return ini.getValue(section, key, fallback);
    };

    int version = 0;
    int fixedTickHz = 0;
    int lastAdvancedTick = -1;
    int relationCount = 0;
    int timerCount = 0;
    int animationCount = 0;
    if (!parseIntStrict(value("RepairDepotRuntime", "Version"), version)
        || (version < 1 || version > 4)
        || !parseIntStrict(
            value("RepairDepotRuntime", "FixedTickHz"),
            fixedTickHz
        )
        || fixedTickHz <= 0
        || fixedTickHz > 1000
        || !parseIntStrict(
            value("RepairDepotRuntime", "LastAdvancedTick", "-1"),
            lastAdvancedTick
        )
        || lastAdvancedTick < -1
        || !parseIntStrict(
            value("RepairDepotRuntime", "RelationCount"),
            relationCount
        )
        || relationCount < 0
        || static_cast<std::size_t>(relationCount) > maximumSavedRelations
        || !parseIntStrict(
            value("RepairDepotRuntime", "HostTimerCount"),
            timerCount
        )
        || timerCount < 0
        || static_cast<std::size_t>(timerCount) > maximumSavedRelations
        || (version >= 4 && (!parseIntStrict(
                value("RepairDepotRuntime", "AnimationCount"), animationCount)
            || animationCount < 0
            || static_cast<std::size_t>(animationCount) > maximumSavedRelations))) {
        return fail("repair depot runtime save has invalid header values");
    }

    RuntimeRepairDepotSystemState loaded(fixedTickHz);
    loaded.lastAdvancedTick_ = lastAdvancedTick;
    std::set<int> unitIds;
    std::set<std::pair<int, int>> dockIds;
    std::set<std::pair<int, int>> queueIds;
    std::set<std::tuple<int, int, bool>> queueCells;
    for (int index = 0; index < relationCount; ++index) {
        const std::string section = "RepairDepotRuntime.Relation."
            + std::to_string(index);
        RuntimeRepairDepotRelation relation;
        bool dockBridge = false;
        bool queueBridge = false;
        const std::string phase = lowerAscii(value(section, "Phase"));
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(value(section, "HostId"), relation.hostId)
            || relation.hostId < 0
            || !parseIntStrict(value(section, "UnitId"), relation.unitId)
            || relation.unitId < 0
            || relation.unitId == relation.hostId
            || !parseIntStrict(value(section, "DockIndex"), relation.dockIndex)
            || !parseIntStrict(value(section, "DockX"), relation.dockCell.x)
            || !parseIntStrict(value(section, "DockY"), relation.dockCell.y)
            || !parseBoolStrict(value(section, "DockBridge"), dockBridge)
            || !hexDecode(value(section, "HostOwnerHex"), relation.hostOwner)
            || relation.hostOwner.empty()
            || !hexDecode(value(section, "UnitOwnerHex"), relation.unitOwner)
            || relation.unitOwner.empty()
            || !hexDecode(value(section, "HostTypeHex"), relation.hostType)
            || relation.hostType.empty()
            || !hexDecode(value(section, "UnitTypeHex"), relation.unitType)
            || relation.unitType.empty()
            || !parseIntStrict(
                value(section, "ReservedTick"),
                relation.reservedTick
            )
            || relation.reservedTick < 0
            || !parseIntStrict(
                value(section, "AttachedTick"),
                relation.attachedTick
            )
            || !parseBoolStrict(
                value(section, "HasRepaired"),
                relation.hasRepaired
            )) {
            return fail("repair depot runtime save has invalid relation ["
                + section + "]");
        }
        relation.dockCell.onBridge = dockBridge;
        if (version >= 3
            && !parseDomainStrict(
                value(section, "Domain"),
                relation.domain
            )) {
            return fail("repair depot runtime save has invalid domain ["
                + section + "]");
        }
        if (version >= 2) {
            if (!parseIntStrict(
                    value(section, "QueueIndex"),
                    relation.queueIndex
                )
                || !parseIntStrict(
                    value(section, "QueueX"),
                    relation.queueCell.x
                )
                || !parseIntStrict(
                    value(section, "QueueY"),
                    relation.queueCell.y
                )
                || !parseBoolStrict(
                    value(section, "QueueBridge"),
                    queueBridge
                )) {
                return fail("repair depot runtime save has invalid queue fields ["
                    + section + "]");
            }
            relation.queueCell.onBridge = queueBridge;
        }
        if (phase == "queued" && version >= 2) {
            relation.phase = RuntimeRepairDepotRelationPhase::Queued;
            if (relation.dockIndex != -1
                || validCell(relation.dockCell)
                || relation.queueIndex < 0
                || relation.queueIndex >= maximumQueueSlotsPerHost
                || !validCell(relation.queueCell)
                || relation.attachedTick != -1
                || relation.hasRepaired) {
                return fail("queued repair relation has invalid state");
            }
        } else if (phase == "reserved") {
            relation.phase = RuntimeRepairDepotRelationPhase::Reserved;
            if (relation.dockIndex < 0
                || !validCell(relation.dockCell)
                || relation.queueIndex != -1
                || validCell(relation.queueCell)
                || relation.attachedTick != -1
                || relation.hasRepaired) {
                return fail("reserved repair relation has an attached tick");
            }
        } else if (phase == "docked") {
            relation.phase = RuntimeRepairDepotRelationPhase::Docked;
            if (relation.dockIndex < 0
                || !validCell(relation.dockCell)
                || relation.queueIndex != -1
                || validCell(relation.queueCell)
                || relation.attachedTick < relation.reservedTick) {
                return fail("docked repair relation has invalid tick ordering");
            }
        } else {
            return fail("repair depot runtime save has invalid relation phase");
        }
        const bool duplicateSlot =
            relation.phase == RuntimeRepairDepotRelationPhase::Queued
                ? !queueIds.insert({
                        relation.hostId,
                        relation.queueIndex
                    }).second
                    || !queueCells.insert({
                        relation.queueCell.x,
                        relation.queueCell.y,
                        relation.queueCell.onBridge
                    }).second
                : !dockIds.insert({
                        relation.hostId,
                        relation.dockIndex
                    }).second;
        if (!unitIds.insert(relation.unitId).second || duplicateSlot) {
            return fail("repair depot runtime save has duplicate relations");
        }
        loaded.relations_.push_back(std::move(relation));
    }

    std::set<int> timerHostIds;
    for (int index = 0; index < timerCount; ++index) {
        const std::string section = "RepairDepotRuntime.Timer."
            + std::to_string(index);
        HostTimer timer;
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(value(section, "HostId"), timer.hostId)
            || timer.hostId < 0
            || !parseIntStrict(
                value(section, "RemainingTicks"),
                timer.remainingTicks
            )
            || timer.remainingTicks < 0
            || !timerHostIds.insert(timer.hostId).second) {
            return fail("repair depot runtime save has invalid host timer ["
                + section + "]");
        }
        loaded.hostTimers_.push_back(timer);
    }

    std::set<int> animationHostIds;
    for (int index = 0; index < animationCount; ++index) {
        const std::string section = "RepairDepotRuntime.Animation."
            + std::to_string(index);
        RuntimeRepairDepotAnimationState animation;
        const std::string phase = lowerAscii(value(section, "Phase"));
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(value(section, "HostId"), animation.hostId)
            || animation.hostId < 0
            || !parseIntStrict(value(section, "AgeTicks"), animation.ageTicks)
            || animation.ageTicks < 0
            || !animationHostIds.insert(animation.hostId).second) {
            return fail("repair depot runtime save has invalid animation ["
                + section + "]");
        }
        if (phase == "extending") {
            animation.phase = RuntimeRepairDepotAnimationPhase::Extending;
        } else if (phase == "repairing") {
            animation.phase = RuntimeRepairDepotAnimationPhase::Repairing;
        } else if (phase == "retracting") {
            animation.phase = RuntimeRepairDepotAnimationPhase::Retracting;
        } else {
            return fail("repair depot runtime save has invalid animation phase");
        }
        loaded.animations_.push_back(animation);
    }

    loaded.sortState();
    RuntimeRepairDepotReconcileReport report = loaded.reconcile(context);
    *this = std::move(loaded);
    if (reconcileReport != nullptr) {
        *reconcileReport = std::move(report);
    }
    return true;
}

std::uint64_t RuntimeRepairDepotSystemState::stateHash() const {
    std::ostringstream stream;
    save(stream);
    return hashText(
        std::string("RuntimeRepairDepotSystemState/v4\n") + stream.str()
    );
}

const std::vector<RuntimeRepairDepotRelation>&
RuntimeRepairDepotSystemState::relations() const {
    return relations_;
}

const std::vector<RuntimeRepairDepotAnimationState>&
RuntimeRepairDepotSystemState::animations() const {
    return animations_;
}

int RuntimeRepairDepotSystemState::remainingServiceTicks(int hostId) const {
    const auto found = std::find_if(
        hostTimers_.begin(),
        hostTimers_.end(),
        [hostId](const HostTimer& timer) {
            return timer.hostId == hostId;
        }
    );
    return found == hostTimers_.end() ? -1 : found->remainingTicks;
}

int RuntimeRepairDepotSystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

void RuntimeRepairDepotSystemState::sortState() {
    std::sort(animations_.begin(), animations_.end(),
        [](const auto& left, const auto& right) {
            return left.hostId < right.hostId;
        });
    std::sort(
        relations_.begin(),
        relations_.end(),
        [](const RuntimeRepairDepotRelation& left,
           const RuntimeRepairDepotRelation& right) {
            if (left.hostId != right.hostId) {
                return left.hostId < right.hostId;
            }
            if (left.unitId != right.unitId) {
                return left.unitId < right.unitId;
            }
            return left.dockIndex < right.dockIndex;
        }
    );
    std::sort(
        hostTimers_.begin(),
        hostTimers_.end(),
        [](const HostTimer& left, const HostTimer& right) {
            return left.hostId < right.hostId;
        }
    );
}
