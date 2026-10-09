#include "RuntimeSpySystem.h"
#include "BuildingSuperWeapons.h"

#include "IniFile.h"

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
#include <utility>

namespace {

constexpr std::size_t maximumSavedSpyRecords = 4096;
constexpr std::uint64_t fnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnvPrime = 1099511628211ULL;

char lowerAsciiCharacter(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

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
    std::string result;
    result.reserve(end - begin);
    for (std::size_t index = begin; index < end; ++index) {
        result.push_back(lowerAsciiCharacter(value[index]));
    }
    return result;
}

bool equalsAsciiIgnoreCase(
    std::string_view left,
    std::string_view right
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
    const RuntimeSpyContext& context,
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

std::string canonicalOwner(
    const RuntimeSpyContext& context,
    const std::string& owner
) {
    if (context.canonicalOwnerId) {
        const std::string canonical = context.canonicalOwnerId(owner);
        if (!canonical.empty()) {
            return canonical;
        }
    }
    return owner;
}

bool ownersAreAllied(
    const RuntimeSpyContext& context,
    const std::string& left,
    const std::string& right
) {
    if (equalsAsciiIgnoreCase(left, right)) {
        return true;
    }
    return context.areAllied && context.areAllied(left, right);
}

bool validLiveObject(const WorldObjectInstance* object) {
    return object != nullptr && object->alive && object->health > 0;
}

bool retainedObjectSnapshot(
    const RuntimeSpyContext& context,
    const WorldObjectInstance* object
) {
    return object != nullptr
        && !object->alive
        && object->health > 0
        && context.isObjectRetained
        && context.isObjectRetained(object->instanceId);
}

bool validSpyStateObject(
    const RuntimeSpyContext& context,
    const WorldObjectInstance* object
) {
    return validLiveObject(object) || retainedObjectSnapshot(context, object);
}

struct DisguiseClass {
    const UnitType* rules = nullptr;
    RuntimeSpyDisguiseIdentity identity =
        RuntimeSpyDisguiseIdentity::Infantry;
};

DisguiseClass disguiseClassFor(
    const RuntimeSpyContext& context,
    const WorldObjectInstance& object
) {
    if (context.rules == nullptr) {
        return {};
    }
    if (object.kind == WorldObjectKind::Infantry) {
        return {
            context.rules->findUnitType("InfantryType", object.type),
            RuntimeSpyDisguiseIdentity::Infantry
        };
    }
    if (object.kind == WorldObjectKind::Unit
        && !object.authoredAircraft) {
        if (const UnitType* vehicle =
                context.rules->findUnitType("UnitType", object.type)) {
            return {vehicle, RuntimeSpyDisguiseIdentity::Vehicle};
        }
    }
    return {};
}

int safeTickAdd(int tick, int duration) {
    const long long value = static_cast<long long>(tick)
        + static_cast<long long>(std::max(0, duration));
    return static_cast<int>(std::min<long long>(
        std::numeric_limits<int>::max(),
        std::max<long long>(0, value)
    ));
}

RuntimeSpyAction disguiseAction(
    int tick,
    int objectId,
    const RuntimeSpyDisguiseState& state,
    const std::string& reason
) {
    RuntimeSpyAction action;
    action.kind = RuntimeSpyActionKind::DisguiseChanged;
    action.tick = tick;
    action.sourceObjectId = objectId;
    action.targetObjectId = objectId;
    action.disguiseIdentity = state.identity;
    action.value = state.disguiseType;
    action.targetOwner = state.disguiseOwner;
    action.active = state.active;
    action.changed = true;
    action.untilTick = state.rearmUntilTick;
    action.reason = reason;
    return action;
}

RuntimeSpyDiagnostic diagnostic(
    RuntimeSpyDiagnosticKind kind,
    int objectId,
    const std::string& value,
    const std::string& message
) {
    return {kind, objectId, value, message};
}

RuntimeSpyOwnerState* findOwnerState(
    std::vector<RuntimeSpyOwnerState>& owners,
    const std::string& owner
) {
    for (RuntimeSpyOwnerState& state : owners) {
        if (equalsAsciiIgnoreCase(state.owner, owner)) {
            return &state;
        }
    }
    return nullptr;
}

const RuntimeSpyOwnerState* findOwnerState(
    const std::vector<RuntimeSpyOwnerState>& owners,
    const std::string& owner
) {
    for (const RuntimeSpyOwnerState& state : owners) {
        if (equalsAsciiIgnoreCase(state.owner, owner)) {
            return &state;
        }
    }
    return nullptr;
}

RuntimeSpyOwnerState& ensureOwnerState(
    std::vector<RuntimeSpyOwnerState>& owners,
    const std::string& owner
) {
    if (RuntimeSpyOwnerState* found = findOwnerState(owners, owner)) {
        return *found;
    }
    owners.push_back({});
    owners.back().owner = owner;
    return owners.back();
}

RuntimeSpyDisguiseState* findDisguiseState(
    std::vector<RuntimeSpyDisguiseState>& disguises,
    int objectId
) {
    for (RuntimeSpyDisguiseState& state : disguises) {
        if (state.objectId == objectId) {
            return &state;
        }
    }
    return nullptr;
}

const RuntimeSpyDisguiseState* findDisguiseState(
    const std::vector<RuntimeSpyDisguiseState>& disguises,
    int objectId
) {
    for (const RuntimeSpyDisguiseState& state : disguises) {
        if (state.objectId == objectId) {
            return &state;
        }
    }
    return nullptr;
}

bool ownerHasSpySat(
    const RuntimeSpyContext& context,
    const std::string& owner
) {
    if (context.rules == nullptr || context.objects == nullptr) {
        return false;
    }
    for (const WorldObjectInstance& object : *context.objects) {
        if (!validLiveObject(&object)
            || object.kind != WorldObjectKind::Structure
            || !equalsAsciiIgnoreCase(
                canonicalOwner(context, object.owner),
                owner
            )) {
            continue;
        }
        const BuildingType* building =
            context.rules->findBuildingType(object.type);
        if (building != nullptr && building->spySat
            && object.poweredOperational) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> combatantOwners(
    const RuntimeSpyContext& context
) {
    std::vector<std::string> result;
    const auto append = [&](const std::string& owner) {
        const std::string canonical = canonicalOwner(context, owner);
        if (canonical.empty()) {
            return;
        }
        if (std::none_of(
                result.begin(),
                result.end(),
                [&](const std::string& existing) {
                    return equalsAsciiIgnoreCase(existing, canonical);
                }
            )) {
            result.push_back(canonical);
        }
    };
    for (const std::string& owner : context.combatantOwners) {
        append(owner);
    }
    if (result.empty() && context.objects != nullptr) {
        for (const WorldObjectInstance& object : *context.objects) {
            if (object.alive) {
                append(object.owner);
            }
        }
    }
    std::sort(
        result.begin(),
        result.end(),
        [](const std::string& left, const std::string& right) {
            return normalizedToken(left) < normalizedToken(right);
        }
    );
    return result;
}

int detectorRange(
    const RuntimeSpyContext& context,
    const WorldObjectInstance& detector
) {
    if (context.rules == nullptr) {
        return 0;
    }
    if (detector.kind == WorldObjectKind::Structure) {
        const BuildingType* building =
            context.rules->findBuildingType(detector.type);
        if (building == nullptr
            || (building->powered && !detector.poweredOperational)) {
            return 0;
        }
        return std::max(0, building->detectDisguiseRange);
    }
    const UnitType* unit = detector.kind == WorldObjectKind::Infantry
        ? context.rules->findUnitType("InfantryType", detector.type)
        : context.rules->findUnitType("UnitType", detector.type);
    if (unit == nullptr && detector.kind == WorldObjectKind::Unit) {
        unit = context.rules->findUnitType("AircraftType", detector.type);
    }
    return unit == nullptr ? 0 : std::max(0, unit->detectDisguiseRange);
}

bool detectorCanPersonallySeeDisguise(
    const RuntimeSpyContext& context,
    const WorldObjectInstance& observer
) {
    if (context.rules == nullptr) {
        return false;
    }
    if (observer.kind == WorldObjectKind::Structure) {
        const BuildingType* building =
            context.rules->findBuildingType(observer.type);
        return building != nullptr
            && building->detectDisguise
            && observer.poweredOperational;
    }
    const UnitType* unit = observer.kind == WorldObjectKind::Infantry
        ? context.rules->findUnitType("InfantryType", observer.type)
        : context.rules->findUnitType("UnitType", observer.type);
    if (unit == nullptr && observer.kind == WorldObjectKind::Unit) {
        unit = context.rules->findUnitType("AircraftType", observer.type);
    }
    return unit != nullptr && unit->detectDisguise;
}

double distanceBetween(
    const RuntimeSpyContext& context,
    const WorldObjectInstance& left,
    const WorldObjectInstance& right
) {
    if (context.cellDistance) {
        return std::max(0.0, context.cellDistance(left, right));
    }
    const double dx = static_cast<double>(left.x - right.x);
    const double dy = static_cast<double>(left.y - right.y);
    return std::hypot(dx, dy);
}

std::vector<RuntimeSpyDetectionState> computeDetections(
    const RuntimeSpyContext& context,
    const std::vector<RuntimeSpyDisguiseState>& disguises
) {
    std::map<std::pair<int, std::string>, RuntimeSpyDetectionState> byPair;
    if (context.rules == nullptr || context.objects == nullptr) {
        return {};
    }
    const std::vector<std::string> observers = combatantOwners(context);
    for (const RuntimeSpyDisguiseState& disguise : disguises) {
        if (!disguise.active) {
            continue;
        }
        const WorldObjectInstance* target = findObject(
            context,
            disguise.objectId
        );
        if (!validLiveObject(target)) {
            continue;
        }
        const std::string targetOwner = canonicalOwner(context, target->owner);
        for (const WorldObjectInstance& detector : *context.objects) {
            if (!validLiveObject(&detector)
                || detector.instanceId == target->instanceId) {
                continue;
            }
            const int range = detectorRange(context, detector);
            if (range <= 0) {
                continue;
            }
            const std::string detectorOwner = canonicalOwner(
                context,
                detector.owner
            );
            if (ownersAreAllied(context, detectorOwner, targetOwner)
                || distanceBetween(context, detector, *target)
                    > static_cast<double>(range)) {
                continue;
            }
            std::vector<std::string> sharedOwners = observers;
            if (std::none_of(
                    sharedOwners.begin(),
                    sharedOwners.end(),
                    [&](const std::string& owner) {
                        return equalsAsciiIgnoreCase(owner, detectorOwner);
                    }
                )) {
                sharedOwners.push_back(detectorOwner);
            }
            for (const std::string& observer : sharedOwners) {
                if (!ownersAreAllied(context, detectorOwner, observer)) {
                    continue;
                }
                const std::pair<int, std::string> key{
                    target->instanceId,
                    normalizedToken(observer)
                };
                const auto found = byPair.find(key);
                if (found == byPair.end()
                    || detector.instanceId
                        < found->second.detectorObjectId) {
                    byPair[key] = {
                        target->instanceId,
                        observer,
                        detector.instanceId
                    };
                }
            }
        }
    }
    std::vector<RuntimeSpyDetectionState> result;
    result.reserve(byPair.size());
    for (auto& [key, state] : byPair) {
        (void)key;
        result.push_back(std::move(state));
    }
    return result;
}

bool sameDetectionPair(
    const RuntimeSpyDetectionState& left,
    const RuntimeSpyDetectionState& right
) {
    return left.targetObjectId == right.targetObjectId
        && equalsAsciiIgnoreCase(left.observerOwner, right.observerOwner);
}

std::string hexEncode(std::string_view value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 2);
    for (const unsigned char character : value) {
        result.push_back(digits[(character >> 4U) & 0x0FU]);
        result.push_back(digits[character & 0x0FU]);
    }
    return result;
}

int hexDigit(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

bool hexDecode(std::string_view value, std::string& output) {
    if ((value.size() % 2U) != 0U) {
        return false;
    }
    std::string decoded;
    decoded.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const int high = hexDigit(value[index]);
        const int low = hexDigit(value[index + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    output = std::move(decoded);
    return true;
}

bool parseIntStrict(std::string_view text, int& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc() && result.ptr == end;
}

bool parseBoolStrict(std::string_view text, bool& value) {
    const std::string normalized = normalizedToken(text);
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        value = true;
        return true;
    }
    if (normalized == "no" || normalized == "false" || normalized == "0") {
        value = false;
        return true;
    }
    return false;
}

const char* disguiseIdentityName(RuntimeSpyDisguiseIdentity identity) {
    switch (identity) {
    case RuntimeSpyDisguiseIdentity::Infantry:
        return "Infantry";
    case RuntimeSpyDisguiseIdentity::Vehicle:
        return "Vehicle";
    case RuntimeSpyDisguiseIdentity::Terrain:
        return "Terrain";
    }
    return "Infantry";
}

bool parseDisguiseIdentity(
    std::string_view value,
    RuntimeSpyDisguiseIdentity& identity
) {
    const std::string normalized = normalizedToken(value);
    if (normalized == "infantry") {
        identity = RuntimeSpyDisguiseIdentity::Infantry;
        return true;
    }
    if (normalized == "vehicle") {
        identity = RuntimeSpyDisguiseIdentity::Vehicle;
        return true;
    }
    if (normalized == "terrain") {
        identity = RuntimeSpyDisguiseIdentity::Terrain;
        return true;
    }
    return false;
}

std::uint64_t hashText(std::string_view text) {
    std::uint64_t hash = fnvOffset;
    for (const unsigned char character : text) {
        hash ^= character;
        hash *= fnvPrime;
    }
    return hash;
}

} // namespace

void RuntimeSpySystemState::reset() {
    lastAdvancedTick_ = -1;
    owners_.clear();
    disguises_.clear();
    detections_.clear();
}

RuntimeSpyInfiltrationResult RuntimeSpySystemState::infiltrate(
    const RuntimeSpyContext& context,
    const RuntimeSpyInfiltrationInput& input
) {
    RuntimeSpyInfiltrationResult result;
    if (context.rules == nullptr || context.objects == nullptr) {
        result.veto = RuntimeSpyVeto::InvalidContext;
        return result;
    }
    const WorldObjectInstance* agent = findObject(
        context,
        input.agentObjectId
    );
    if (agent == nullptr) {
        result.veto = RuntimeSpyVeto::AgentMissing;
        return result;
    }
    if (!validLiveObject(agent)) {
        result.veto = RuntimeSpyVeto::AgentUnavailable;
        return result;
    }
    if (agent->kind != WorldObjectKind::Infantry) {
        result.veto = RuntimeSpyVeto::AgentNotInfantry;
        return result;
    }
    const UnitType* agentRules = context.rules->findUnitType(
        "InfantryType",
        agent->type
    );
    if (agentRules == nullptr || !agentRules->infiltrate) {
        result.veto = RuntimeSpyVeto::AgentCannotInfiltrate;
        return result;
    }

    const WorldObjectInstance* target = findObject(
        context,
        input.targetBuildingObjectId
    );
    if (target == nullptr) {
        result.veto = RuntimeSpyVeto::TargetMissing;
        return result;
    }
    if (!validLiveObject(target) || target->rubble) {
        result.veto = RuntimeSpyVeto::TargetUnavailable;
        return result;
    }
    if (target->kind != WorldObjectKind::Structure) {
        result.veto = RuntimeSpyVeto::TargetNotBuilding;
        return result;
    }
    const BuildingType* building = context.rules->findBuildingType(
        target->type
    );
    if (building == nullptr || !building->spyable) {
        result.veto = RuntimeSpyVeto::TargetNotSpyable;
        return result;
    }

    const std::string agentOwner = canonicalOwner(context, agent->owner);
    const std::string targetOwner = canonicalOwner(context, target->owner);
    if (ownersAreAllied(context, agentOwner, targetOwner)) {
        result.veto = RuntimeSpyVeto::FriendlyTarget;
        return result;
    }

    result.accepted = true;
    RuntimeSpyAction consume;
    consume.kind = RuntimeSpyActionKind::ConsumeAgent;
    consume.tick = input.currentTick;
    consume.sourceObjectId = agent->instanceId;
    consume.targetObjectId = target->instanceId;
    consume.sourceOwner = agentOwner;
    consume.targetOwner = targetOwner;
    consume.changed = true;
    consume.reason = "successful infiltration";
    result.actions.push_back(std::move(consume));

    if (!agentRules->agent) {
        result.diagnostics.push_back(diagnostic(
            RuntimeSpyDiagnosticKind::AgentTraitMissing,
            agent->instanceId,
            agent->type,
            "Infiltrate=yes unit has no Agent=yes effect trait; only the legal entry/consumption action is emitted"
        ));
        return result;
    }

    int effectCount = 0;
    // The laboratory's authored side decides the reward even after capture.
    // PrerequisiteTech also supports renamed/modded laboratories.
    bool techCenter = false;
    for (const PrerequisiteAlias& alias : context.rules->getPrerequisiteAliases()) {
        if (normalizedToken(alias.name) != "tech") continue;
        std::istringstream entries(alias.value);
        std::string typeId;
        while (std::getline(entries, typeId, ',')) {
            techCenter = techCenter || normalizedToken(typeId) == normalizedToken(building->id);
        }
    }
    const std::string buildingId = normalizedToken(building->id);
    int technologySide = building->aiBasePlanningSide;
    if (buildingId == "gatech" || buildingId == "natech" || buildingId == "yatech") {
        techCenter = true;
        if (technologySide < 0)
            technologySide = buildingId == "gatech" ? 0 : (buildingId == "natech" ? 1 : 2);
    }
    if (techCenter && technologySide >= 0 && technologySide <= 2) {
        RuntimeSpyOwnerState& agentState = ensureOwnerState(owners_, agentOwner);
        const int technologyBit = 1 << technologySide;
        RuntimeSpyAction action;
        action.kind = RuntimeSpyActionKind::GrantStolenTechnology;
        action.tick = input.currentTick;
        action.sourceObjectId = agent->instanceId;
        action.targetObjectId = target->instanceId;
        action.sourceOwner = agentOwner;
        action.targetOwner = agentOwner;
        action.stolenTechnology = static_cast<RuntimeSpySide>(technologySide);
        action.changed = (agentState.stolenTechMask & technologyBit) == 0;
        action.reason = "spy infiltrated technology laboratory";
        agentState.stolenTechMask |= technologyBit;
        result.actions.push_back(std::move(action));
        ++effectCount;
    }
    if (building->radar && !ownerHasSpySat(context, targetOwner)) {
        RuntimeSpyAction action;
        action.kind = RuntimeSpyActionKind::ResetRadarShroud;
        action.tick = input.currentTick;
        action.sourceObjectId = agent->instanceId;
        action.targetObjectId = target->instanceId;
        action.sourceOwner = agentOwner;
        action.targetOwner = targetOwner;
        action.changed = true;
        action.reason = "spy infiltrated radar without SpySat protection";
        result.actions.push_back(std::move(action));
        ++effectCount;
    }

    if (building->power > 0) {
        const int duration = std::max(
            0,
            context.rules->getSpecialMechanismRules()
                .spyPowerBlackoutFrames
        );
        RuntimeSpyOwnerState& targetState = ensureOwnerState(
            owners_,
            targetOwner
        );
        const int previousUntil = targetState.blackoutUntilTick;
        const int requestedUntil = safeTickAdd(
            input.currentTick,
            duration
        );
        targetState.blackoutUntilTick = std::max(
            previousUntil,
            requestedUntil
        );
        RuntimeSpyAction action;
        action.kind = RuntimeSpyActionKind::SetPowerBlackout;
        action.tick = input.currentTick;
        action.sourceObjectId = agent->instanceId;
        action.targetObjectId = target->instanceId;
        action.sourceOwner = agentOwner;
        action.targetOwner = targetOwner;
        action.durationTicks = duration;
        action.untilTick = targetState.blackoutUntilTick;
        action.changed = previousUntil != targetState.blackoutUntilTick;
        action.reason = "spy infiltrated power producer";
        result.actions.push_back(std::move(action));
        ++effectCount;
    }

    const auto superWeapons = buildingSuperWeapons(*building);
    for (const std::string& superWeapon : superWeapons) {
        RuntimeSpyAction action;
        action.kind = RuntimeSpyActionKind::ResetSuperWeapon;
        action.tick = input.currentTick;
        action.sourceObjectId = agent->instanceId;
        action.targetObjectId = target->instanceId;
        action.sourceOwner = agentOwner;
        action.targetOwner = targetOwner;
        action.value = superWeapon;
        action.changed = true;
        action.reason = "spy infiltrated super weapon host";
        result.actions.push_back(std::move(action));
        ++effectCount;
    }

    if (building->storage > 0) {
        const double percent = std::clamp(
            context.rules->getSpecialMechanismRules()
                .spyMoneyStealPercent,
            0.0,
            1.0
        );
        const int available = std::max(0, input.targetOwnerCredits);
        const int stolen = static_cast<int>(std::floor(
            static_cast<double>(available) * percent
        ));
        RuntimeSpyAction action;
        action.kind = RuntimeSpyActionKind::TransferCredits;
        action.tick = input.currentTick;
        action.sourceObjectId = agent->instanceId;
        action.targetObjectId = target->instanceId;
        action.sourceOwner = targetOwner;
        action.targetOwner = agentOwner;
        action.amount = stolen;
        action.changed = stolen > 0;
        action.reason = "spy infiltrated resource storage";
        result.actions.push_back(std::move(action));
        ++effectCount;
    }

    const std::string factory = normalizedToken(building->factory);
    bool recognizedFactory = building->factory.empty();
    bool grantsVeterancy = false;
    RuntimeSpyVeteranProduction production =
        RuntimeSpyVeteranProduction::Infantry;
    if (factory == "infantrytype") {
        recognizedFactory = true;
        grantsVeterancy = true;
        production = RuntimeSpyVeteranProduction::Infantry;
    } else if (factory == "unittype"
        || factory == "vehicletype"
        || factory == "navalunittype"
        || factory == "vesseltype") {
        recognizedFactory = true;
        grantsVeterancy = true;
        production = RuntimeSpyVeteranProduction::Vehicles;
    } else if (factory == "aircrafttype") {
        recognizedFactory = true;
        grantsVeterancy = true;
        production = RuntimeSpyVeteranProduction::Aircraft;
    } else if (factory == "buildingtype") {
        // Construction factories are a known stock category with no veteran
        // production reward.
        recognizedFactory = true;
    }
    if (!recognizedFactory) {
        result.diagnostics.push_back(diagnostic(
            RuntimeSpyDiagnosticKind::UnsupportedFactoryType,
            target->instanceId,
            building->factory,
            "unknown Factory classification was left as a safe no-op"
        ));
    }
    if (grantsVeterancy) {
        RuntimeSpyOwnerState& agentState = ensureOwnerState(
            owners_,
            agentOwner
        );
        bool* marker = &agentState.veteranInfantry;
        if (production == RuntimeSpyVeteranProduction::Vehicles) {
            marker = &agentState.veteranVehicles;
        } else if (production == RuntimeSpyVeteranProduction::Aircraft) {
            marker = &agentState.veteranAircraft;
        }
        const bool changed = !*marker;
        *marker = true;
        RuntimeSpyAction action;
        action.kind = RuntimeSpyActionKind::GrantVeteranProduction;
        action.tick = input.currentTick;
        action.sourceObjectId = agent->instanceId;
        action.targetObjectId = target->instanceId;
        action.sourceOwner = agentOwner;
        action.targetOwner = agentOwner;
        action.veteranProduction = production;
        action.changed = changed;
        action.reason = "spy infiltrated production factory";
        result.actions.push_back(std::move(action));
        ++effectCount;
    }

    if (effectCount == 0 && result.diagnostics.empty()) {
        result.diagnostics.push_back(diagnostic(
            RuntimeSpyDiagnosticKind::NoKnownInfiltrationEffect,
            target->instanceId,
            target->type,
            "Spyable target has no stock infiltration capability"
        ));
    }
    sortState();
    return result;
}

RuntimeSpyDisguiseResult RuntimeSpySystemState::initializeDefaultDisguise(
    const RuntimeSpyContext& context,
    int objectId,
    RuntimeSpySide side,
    int currentTick
) {
    RuntimeSpyDisguiseResult result;
    if (context.rules == nullptr || context.objects == nullptr) {
        result.veto = RuntimeSpyVeto::InvalidContext;
        return result;
    }
    const WorldObjectInstance* object = findObject(context, objectId);
    if (!validLiveObject(object)) {
        result.veto = RuntimeSpyVeto::AgentUnavailable;
        return result;
    }
    const DisguiseClass objectClass = disguiseClassFor(context, *object);
    if (objectClass.rules == nullptr
        || objectClass.identity != RuntimeSpyDisguiseIdentity::Infantry
        || !objectClass.rules->canDisguise
        || !objectClass.rules->permaDisguise) {
        result.veto = RuntimeSpyVeto::SourceCannotDisguise;
        return result;
    }
    const SpecialMechanismRules& special =
        context.rules->getSpecialMechanismRules();
    std::string disguiseType;
    switch (side) {
    case RuntimeSpySide::Allied:
        disguiseType = special.alliedDisguise;
        break;
    case RuntimeSpySide::Soviet:
        disguiseType = special.sovietDisguise;
        break;
    case RuntimeSpySide::Third:
        disguiseType = special.thirdDisguise;
        break;
    }
    if (disguiseType.empty()
        || context.rules->findUnitType("InfantryType", disguiseType)
            == nullptr) {
        result.veto = RuntimeSpyVeto::DefaultDisguiseUnavailable;
        result.diagnostics.push_back(diagnostic(
            RuntimeSpyDiagnosticKind::InvalidDefaultDisguise,
            objectId,
            disguiseType,
            "side default disguise does not resolve to an InfantryType"
        ));
        return result;
    }

    RuntimeSpyDisguiseState* state = findDisguiseState(disguises_, objectId);
    if (state == nullptr) {
        disguises_.push_back({});
        state = &disguises_.back();
        state->objectId = objectId;
    }
    const std::string owner = canonicalOwner(context, object->owner);
    const bool changed = !state->active
        || state->identity != RuntimeSpyDisguiseIdentity::Infantry
        || !equalsAsciiIgnoreCase(state->disguiseType, disguiseType)
        || !equalsAsciiIgnoreCase(state->disguiseOwner, owner)
        || !state->observerRelative;
    state->active = true;
    state->identity = RuntimeSpyDisguiseIdentity::Infantry;
    state->disguiseType = disguiseType;
    state->disguiseOwner = owner;
    state->observerRelative = true;
    state->rearmUntilTick = std::max(0, currentTick);
    state->lastChangeTick = std::max(0, currentTick);
    result.accepted = true;
    result.changed = changed;
    if (changed) {
        result.actions.push_back(disguiseAction(
            currentTick,
            objectId,
            *state,
            "initialized permanent side disguise"
        ));
    }
    sortState();
    return result;
}

RuntimeSpyDisguiseResult RuntimeSpySystemState::applyMakesDisguise(
    const RuntimeSpyContext& context,
    int sourceObjectId,
    int targetObjectId,
    const std::string& warheadId,
    int currentTick
) {
    RuntimeSpyDisguiseResult result;
    if (context.rules == nullptr || context.objects == nullptr) {
        result.veto = RuntimeSpyVeto::InvalidContext;
        return result;
    }
    const WarheadType* warhead = context.rules->findWarheadType(warheadId);
    if (warhead == nullptr) {
        result.veto = RuntimeSpyVeto::WarheadMissing;
        return result;
    }
    if (!warhead->makesDisguise) {
        result.veto = RuntimeSpyVeto::WarheadDoesNotMakeDisguise;
        return result;
    }
    const WorldObjectInstance* source = findObject(context, sourceObjectId);
    const WorldObjectInstance* target = findObject(context, targetObjectId);
    if (!validLiveObject(source)) {
        result.veto = RuntimeSpyVeto::AgentUnavailable;
        return result;
    }
    if (!validLiveObject(target)) {
        result.veto = RuntimeSpyVeto::TargetUnavailable;
        return result;
    }
    const DisguiseClass sourceClass = disguiseClassFor(context, *source);
    if (sourceClass.rules == nullptr || !sourceClass.rules->canDisguise) {
        result.veto = RuntimeSpyVeto::SourceCannotDisguise;
        return result;
    }
    const DisguiseClass targetClass = disguiseClassFor(context, *target);
    if (targetClass.rules == nullptr) {
        result.veto = RuntimeSpyVeto::TargetCannotSupplyDisguise;
        return result;
    }
    if (sourceClass.identity != targetClass.identity) {
        result.veto = RuntimeSpyVeto::DisguiseClassMismatch;
        return result;
    }

    RuntimeSpyDisguiseState* state = findDisguiseState(
        disguises_,
        sourceObjectId
    );
    if (state == nullptr) {
        disguises_.push_back({});
        state = &disguises_.back();
        state->objectId = sourceObjectId;
    }
    const std::string owner = canonicalOwner(context, target->owner);
    const bool changed = !state->active
        || state->identity != sourceClass.identity
        || !equalsAsciiIgnoreCase(state->disguiseType, target->type)
        || !equalsAsciiIgnoreCase(state->disguiseOwner, owner)
        || state->observerRelative;
    state->active = true;
    state->identity = sourceClass.identity;
    state->disguiseType = target->type;
    state->disguiseOwner = owner;
    state->observerRelative = false;
    state->rearmUntilTick = std::max(0, currentTick);
    state->lastChangeTick = std::max(0, currentTick);
    result.accepted = true;
    result.changed = changed;
    if (changed) {
        result.actions.push_back(disguiseAction(
            currentTick,
            sourceObjectId,
            *state,
            "MakesDisguise copied target identity"
        ));
    }
    sortState();
    return result;
}

RuntimeSpyDisguiseResult RuntimeSpySystemState::notifyFired(
    const RuntimeSpyContext& context,
    int objectId,
    int currentTick
) {
    RuntimeSpyDisguiseResult result;
    if (context.rules == nullptr || context.objects == nullptr) {
        result.veto = RuntimeSpyVeto::InvalidContext;
        return result;
    }
    const WorldObjectInstance* object = findObject(context, objectId);
    if (!validLiveObject(object)) {
        result.veto = RuntimeSpyVeto::AgentUnavailable;
        return result;
    }
    const DisguiseClass objectClass = disguiseClassFor(context, *object);
    if (objectClass.rules == nullptr || !objectClass.rules->canDisguise) {
        result.veto = RuntimeSpyVeto::SourceCannotDisguise;
        return result;
    }
    result.accepted = true;
    RuntimeSpyDisguiseState* state = findDisguiseState(disguises_, objectId);
    if (state == nullptr || !state->active || objectClass.rules->permaDisguise) {
        return result;
    }
    state->active = false;
    state->lastChangeTick = std::max(0, currentTick);
    state->rearmUntilTick = safeTickAdd(
        currentTick,
        context.rules->getSpecialMechanismRules()
            .infantryBlinkDisguiseTime
    );
    result.changed = true;
    result.actions.push_back(disguiseAction(
        currentTick,
        objectId,
        *state,
        "firing revealed disguise"
    ));
    detections_.erase(
        std::remove_if(
            detections_.begin(),
            detections_.end(),
            [objectId](const RuntimeSpyDetectionState& detection) {
                return detection.targetObjectId == objectId;
            }
        ),
        detections_.end()
    );
    sortState();
    return result;
}

RuntimeSpyDisguiseResult RuntimeSpySystemState::notifyDamaged(
    const RuntimeSpyContext& context,
    int objectId,
    int currentTick
) {
    RuntimeSpyDisguiseResult result;
    if (context.rules == nullptr || context.objects == nullptr) {
        result.veto = RuntimeSpyVeto::InvalidContext;
        return result;
    }
    const WorldObjectInstance* object = findObject(context, objectId);
    if (!validLiveObject(object)) {
        result.veto = RuntimeSpyVeto::AgentUnavailable;
        return result;
    }
    const DisguiseClass objectClass = disguiseClassFor(context, *object);
    if (objectClass.rules == nullptr || !objectClass.rules->canDisguise) {
        result.veto = RuntimeSpyVeto::SourceCannotDisguise;
        return result;
    }
    result.accepted = true;
    RuntimeSpyDisguiseState* state = findDisguiseState(disguises_, objectId);
    if (state == nullptr || !state->active) {
        return result;
    }
    state->active = false;
    state->lastChangeTick = std::max(0, currentTick);
    state->rearmUntilTick = safeTickAdd(
        currentTick,
        context.rules->getSpecialMechanismRules()
            .infantryBlinkDisguiseTime
    );
    result.changed = true;
    result.actions.push_back(disguiseAction(
        currentTick,
        objectId,
        *state,
        "damage revealed disguise"
    ));
    detections_.erase(
        std::remove_if(
            detections_.begin(),
            detections_.end(),
            [objectId](const RuntimeSpyDetectionState& detection) {
                return detection.targetObjectId == objectId;
            }
        ),
        detections_.end()
    );
    sortState();
    return result;
}

RuntimeSpyDisguiseResult
RuntimeSpySystemState::updateStationaryDisguises(
    const RuntimeSpyContext& context,
    int currentTick,
    const std::function<bool(int objectId)>& isStationary
) {
    RuntimeSpyDisguiseResult result;
    if (context.rules == nullptr || context.objects == nullptr) {
        result.veto = RuntimeSpyVeto::InvalidContext;
        return result;
    }
    result.accepted = true;
    std::vector<const WorldObjectInstance*> objects;
    for (const WorldObjectInstance& object : *context.objects) {
        objects.push_back(&object);
    }
    std::sort(
        objects.begin(),
        objects.end(),
        [](const auto* left, const auto* right) {
            return left->instanceId < right->instanceId;
        }
    );
    std::set<int> deactivated;
    for (const WorldObjectInstance* object : objects) {
        if (!validLiveObject(object)) {
            continue;
        }
        const DisguiseClass objectClass = disguiseClassFor(context, *object);
        if (objectClass.rules == nullptr
            || !objectClass.rules->canDisguise
            || !objectClass.rules->disguiseWhenStill
            || objectClass.rules->permaDisguise) {
            continue;
        }
        RuntimeSpyDisguiseState* state = findDisguiseState(
            disguises_,
            object->instanceId
        );
        const bool stationary = !isStationary
            || isStationary(object->instanceId);
        if (!stationary) {
            if (state != nullptr && state->active) {
                state->active = false;
                state->lastChangeTick = std::max(0, currentTick);
                state->rearmUntilTick = safeTickAdd(
                    currentTick,
                    context.rules->getSpecialMechanismRules()
                        .infantryBlinkDisguiseTime
                );
                result.changed = true;
                result.actions.push_back(disguiseAction(
                    currentTick,
                    object->instanceId,
                    *state,
                    "movement revealed terrain disguise"
                ));
                deactivated.insert(object->instanceId);
            }
            continue;
        }
        if (state != nullptr
            && (state->active || currentTick < state->rearmUntilTick)) {
            continue;
        }
        const std::vector<std::string>& mirages =
            context.rules->getSpecialMechanismRules()
                .defaultMirageDisguises;
        if (mirages.empty()) {
            result.diagnostics.push_back(diagnostic(
                RuntimeSpyDiagnosticKind::InvalidMirageDisguise,
                object->instanceId,
                "",
                "DefaultMirageDisguises is empty; terrain disguise was not guessed"
            ));
            continue;
        }
        const std::uint64_t stableId = object->instanceId < 0
            ? static_cast<std::uint64_t>(
                -static_cast<long long>(object->instanceId)
            )
            : static_cast<std::uint64_t>(object->instanceId);
        const std::string& disguiseType = mirages[
            static_cast<std::size_t>(stableId % mirages.size())
        ];
        if (context.rules->findTerrainType(disguiseType) == nullptr) {
            result.diagnostics.push_back(diagnostic(
                RuntimeSpyDiagnosticKind::InvalidMirageDisguise,
                object->instanceId,
                disguiseType,
                "selected DefaultMirageDisguises entry is not a TerrainType"
            ));
            continue;
        }
        if (state == nullptr) {
            disguises_.push_back({});
            state = &disguises_.back();
            state->objectId = object->instanceId;
        }
        state->active = true;
        state->identity = RuntimeSpyDisguiseIdentity::Terrain;
        state->disguiseType = disguiseType;
        state->disguiseOwner.clear();
        state->observerRelative = false;
        state->lastChangeTick = std::max(0, currentTick);
        state->rearmUntilTick = std::max(0, currentTick);
        result.changed = true;
        result.actions.push_back(disguiseAction(
            currentTick,
            object->instanceId,
            *state,
            "stationary unit assumed terrain disguise"
        ));
    }
    if (!deactivated.empty()) {
        detections_.erase(
            std::remove_if(
                detections_.begin(),
                detections_.end(),
                [&](const RuntimeSpyDetectionState& detection) {
                    return deactivated.find(detection.targetObjectId)
                        != deactivated.end();
                }
            ),
            detections_.end()
        );
    }
    sortState();
    return result;
}

RuntimeSpyDetectionUpdate RuntimeSpySystemState::updateDetection(
    const RuntimeSpyContext& context,
    int currentTick
) {
    RuntimeSpyDetectionUpdate result;
    std::vector<RuntimeSpyDetectionState> next = computeDetections(
        context,
        disguises_
    );
    for (const RuntimeSpyDetectionState& previous : detections_) {
        if (std::none_of(
                next.begin(),
                next.end(),
                [&](const RuntimeSpyDetectionState& candidate) {
                    return sameDetectionPair(previous, candidate);
                }
            )) {
            RuntimeSpyAction action;
            action.kind = RuntimeSpyActionKind::ConcealDisguiseFromOwner;
            action.tick = currentTick;
            action.targetObjectId = previous.targetObjectId;
            action.detectorObjectId = previous.detectorObjectId;
            action.targetOwner = previous.observerOwner;
            action.changed = true;
            action.reason = "target left shared disguise detection";
            result.actions.push_back(std::move(action));
        }
    }
    for (const RuntimeSpyDetectionState& detected : next) {
        if (std::none_of(
                detections_.begin(),
                detections_.end(),
                [&](const RuntimeSpyDetectionState& previous) {
                    return sameDetectionPair(previous, detected);
                }
            )) {
            RuntimeSpyAction action;
            action.kind = RuntimeSpyActionKind::RevealDisguiseToOwner;
            action.tick = currentTick;
            action.sourceObjectId = detected.detectorObjectId;
            action.targetObjectId = detected.targetObjectId;
            action.detectorObjectId = detected.detectorObjectId;
            action.targetOwner = detected.observerOwner;
            action.changed = true;
            action.reason = "detector shared disguise intelligence with allies";
            result.actions.push_back(std::move(action));
        }
    }
    detections_ = std::move(next);
    sortState();
    result.detectedPairs = detections_.size();
    return result;
}

std::vector<RuntimeSpyAction> RuntimeSpySystemState::advance(
    int currentTick
) {
    std::vector<RuntimeSpyAction> actions;
    if (currentTick < 0 || currentTick <= lastAdvancedTick_) {
        return actions;
    }
    lastAdvancedTick_ = currentTick;
    for (RuntimeSpyOwnerState& owner : owners_) {
        if (owner.blackoutUntilTick <= 0
            || owner.blackoutUntilTick > currentTick) {
            continue;
        }
        RuntimeSpyAction action;
        action.kind = RuntimeSpyActionKind::ClearPowerBlackout;
        action.tick = currentTick;
        action.targetOwner = owner.owner;
        action.untilTick = owner.blackoutUntilTick;
        action.changed = true;
        action.reason = "spy power blackout expired";
        actions.push_back(std::move(action));
        owner.blackoutUntilTick = 0;
    }
    for (RuntimeSpyDisguiseState& disguise : disguises_) {
        if (disguise.active
            || disguise.identity == RuntimeSpyDisguiseIdentity::Terrain
            || disguise.disguiseType.empty()
            || disguise.rearmUntilTick > currentTick) {
            continue;
        }
        disguise.active = true;
        disguise.lastChangeTick = currentTick;
        actions.push_back(disguiseAction(
            currentTick,
            disguise.objectId,
            disguise,
            "identity disguise blink expired"
        ));
    }
    sortState();
    return actions;
}

RuntimeSpyReconcileReport RuntimeSpySystemState::reconcile(
    const RuntimeSpyContext& context,
    int currentTick
) {
    RuntimeSpyReconcileReport report;
    sortState();
    std::vector<RuntimeSpyOwnerState> retainedOwners;
    for (RuntimeSpyOwnerState owner : owners_) {
        if (owner.owner.empty()
            || (context.isKnownOwner && !context.isKnownOwner(owner.owner))) {
            ++report.removedOwners;
            report.diagnostics.push_back(diagnostic(
                RuntimeSpyDiagnosticKind::ReconciledState,
                -1,
                owner.owner,
                "removed spy owner state for unknown owner"
            ));
            continue;
        }
        if (owner.blackoutUntilTick > 0
            && owner.blackoutUntilTick <= currentTick) {
            RuntimeSpyAction action;
            action.kind = RuntimeSpyActionKind::ClearPowerBlackout;
            action.tick = currentTick;
            action.targetOwner = owner.owner;
            action.untilTick = owner.blackoutUntilTick;
            action.changed = true;
            action.reason = "expired blackout removed during reconcile";
            report.actions.push_back(std::move(action));
            owner.blackoutUntilTick = 0;
        }
        retainedOwners.push_back(std::move(owner));
        ++report.retainedOwners;
    }
    owners_ = std::move(retainedOwners);

    std::vector<RuntimeSpyDisguiseState> retainedDisguises;
    for (const RuntimeSpyDisguiseState& disguise : disguises_) {
        const WorldObjectInstance* object = findObject(
            context,
            disguise.objectId
        );
        const DisguiseClass objectClass = object == nullptr
            ? DisguiseClass{}
            : disguiseClassFor(context, *object);
        bool validIdentity = false;
        if (context.rules != nullptr && !disguise.disguiseType.empty()) {
            if (disguise.identity == RuntimeSpyDisguiseIdentity::Infantry) {
                validIdentity = objectClass.identity
                        == RuntimeSpyDisguiseIdentity::Infantry
                    && context.rules->findUnitType(
                        "InfantryType",
                        disguise.disguiseType
                    ) != nullptr;
            } else if (disguise.identity
                    == RuntimeSpyDisguiseIdentity::Vehicle) {
                validIdentity = objectClass.identity
                        == RuntimeSpyDisguiseIdentity::Vehicle
                    && context.rules->findUnitType(
                        "UnitType",
                        disguise.disguiseType
                    ) != nullptr;
            } else if (disguise.identity
                    == RuntimeSpyDisguiseIdentity::Terrain) {
                validIdentity = objectClass.rules != nullptr
                    && objectClass.rules->disguiseWhenStill
                    && context.rules->findTerrainType(disguise.disguiseType)
                        != nullptr;
            }
        }
        if (disguise.observerRelative
            && (disguise.identity != RuntimeSpyDisguiseIdentity::Infantry
                || objectClass.rules == nullptr
                || !objectClass.rules->permaDisguise)) {
            validIdentity = false;
        }
        if (!validSpyStateObject(context, object)
            || objectClass.rules == nullptr
            || !objectClass.rules->canDisguise
            || !validIdentity) {
            ++report.removedDisguises;
            report.diagnostics.push_back(diagnostic(
                RuntimeSpyDiagnosticKind::ReconciledState,
                disguise.objectId,
                disguise.disguiseType,
                "removed disguise state that no longer matches an available disguisable object"
            ));
            continue;
        }
        retainedDisguises.push_back(disguise);
        ++report.retainedDisguises;
    }
    disguises_ = std::move(retainedDisguises);

    const std::vector<RuntimeSpyDetectionState> previousDetections =
        detections_;
    RuntimeSpyDetectionUpdate detectionUpdate = updateDetection(
        context,
        currentTick
    );

    // Active objects are recomputed above.  A retained snapshot is deliberately
    // excluded from live range acquisition, but a previously valid pair must
    // survive reconciliation while either endpoint is explicitly contained.
    // Validate both snapshots and their authored relationship before retaining
    // the saved pair so a stale id/type/owner cannot be resurrected.
    for (const RuntimeSpyDetectionState& previous : previousDetections) {
        const bool alreadyRetained = std::any_of(
            detections_.begin(),
            detections_.end(),
            [&](const RuntimeSpyDetectionState& current) {
                return sameDetectionPair(previous, current);
            }
        );
        if (alreadyRetained) {
            continue;
        }
        const WorldObjectInstance* target = findObject(
            context,
            previous.targetObjectId
        );
        const WorldObjectInstance* detector = findObject(
            context,
            previous.detectorObjectId
        );
        const bool retainedEndpoint = retainedObjectSnapshot(context, target)
            || retainedObjectSnapshot(context, detector);
        const RuntimeSpyDisguiseState* disguise = findDisguiseState(
            disguises_,
            previous.targetObjectId
        );
        const std::string detectorOwner = detector == nullptr
            ? std::string()
            : canonicalOwner(context, detector->owner);
        const bool knownObserver = !context.isKnownOwner
            || context.isKnownOwner(previous.observerOwner);
        if (!retainedEndpoint
            || !validSpyStateObject(context, target)
            || !validSpyStateObject(context, detector)
            || disguise == nullptr
            || !disguise->active
            || detectorRange(context, *detector) <= 0
            || detectorOwner.empty()
            || !knownObserver
            || !ownersAreAllied(
                context,
                detectorOwner,
                previous.observerOwner
            )) {
            continue;
        }
        detections_.push_back(previous);
        detectionUpdate.actions.erase(
            std::remove_if(
                detectionUpdate.actions.begin(),
                detectionUpdate.actions.end(),
                [&](const RuntimeSpyAction& action) {
                    return action.kind
                            == RuntimeSpyActionKind::ConcealDisguiseFromOwner
                        && action.targetObjectId == previous.targetObjectId
                        && action.detectorObjectId == previous.detectorObjectId
                        && equalsAsciiIgnoreCase(
                            action.targetOwner,
                            previous.observerOwner
                        );
                }
            ),
            detectionUpdate.actions.end()
        );
    }
    sortState();
    report.actions.insert(
        report.actions.end(),
        detectionUpdate.actions.begin(),
        detectionUpdate.actions.end()
    );
    for (const RuntimeSpyDetectionState& previous : previousDetections) {
        if (std::any_of(
                detections_.begin(),
                detections_.end(),
                [&](const RuntimeSpyDetectionState& current) {
                    return sameDetectionPair(previous, current);
                }
            )) {
            ++report.retainedDetections;
        } else {
            ++report.removedDetections;
        }
    }
    sortState();
    return report;
}

RuntimeSpyObjectExitResult RuntimeSpySystemState::onObjectExited(
    int objectId,
    int currentTick
) {
    RuntimeSpyObjectExitResult result;
    if (objectId < 0) {
        return result;
    }

    sortState();
    const int tick = std::max(0, currentTick);
    const auto disguise = std::find_if(
        disguises_.begin(),
        disguises_.end(),
        [objectId](const RuntimeSpyDisguiseState& state) {
            return state.objectId == objectId;
        }
    );
    if (disguise != disguises_.end()) {
        RuntimeSpyDisguiseState removed = *disguise;
        removed.active = false;
        removed.lastChangeTick = tick;
        removed.rearmUntilTick = tick;
        result.actions.push_back(disguiseAction(
            tick,
            objectId,
            removed,
            "object exited spy lifecycle"
        ));
        disguises_.erase(disguise);
        result.removedDisguises = 1;
    }

    for (const RuntimeSpyDetectionState& detection : detections_) {
        if (detection.targetObjectId != objectId
            && detection.detectorObjectId != objectId) {
            continue;
        }
        RuntimeSpyAction action;
        action.kind = RuntimeSpyActionKind::ConcealDisguiseFromOwner;
        action.tick = tick;
        action.targetObjectId = detection.targetObjectId;
        action.detectorObjectId = detection.detectorObjectId;
        action.targetOwner = detection.observerOwner;
        action.changed = true;
        action.reason = "object exited spy detection lifecycle";
        result.actions.push_back(std::move(action));
        ++result.removedDetections;
    }
    detections_.erase(
        std::remove_if(
            detections_.begin(),
            detections_.end(),
            [objectId](const RuntimeSpyDetectionState& detection) {
                return detection.targetObjectId == objectId
                    || detection.detectorObjectId == objectId;
            }
        ),
        detections_.end()
    );
    result.changed = result.removedDisguises > 0
        || result.removedDetections > 0;
    sortState();
    return result;
}

void RuntimeSpySystemState::save(std::ostream& output) const {
    std::vector<RuntimeSpyOwnerState> owners = owners_;
    std::vector<RuntimeSpyDisguiseState> disguises = disguises_;
    std::vector<RuntimeSpyDetectionState> detections = detections_;
    std::sort(
        owners.begin(),
        owners.end(),
        [](const auto& left, const auto& right) {
            return normalizedToken(left.owner) < normalizedToken(right.owner);
        }
    );
    std::sort(
        disguises.begin(),
        disguises.end(),
        [](const auto& left, const auto& right) {
            return left.objectId < right.objectId;
        }
    );
    std::sort(
        detections.begin(),
        detections.end(),
        [](const auto& left, const auto& right) {
            return std::tie(left.targetObjectId, left.observerOwner)
                < std::tie(right.targetObjectId, right.observerOwner);
        }
    );

    // Keep legacy states byte-identical until stolen technology is present,
    // preserving the state hashes recorded in existing version-2 saves.
    const bool hasStolenTechnology = std::any_of(owners.begin(), owners.end(),
        [](const auto& owner) { return owner.stolenTechMask != 0; });
    output << "[SpyRuntime]\n";
    output << "Version=" << (hasStolenTechnology ? 3 : 2) << "\n";
    output << "LastAdvancedTick=" << lastAdvancedTick_ << "\n";
    output << "OwnerCount=" << owners.size() << "\n";
    output << "DisguiseCount=" << disguises.size() << "\n";
    output << "DetectionCount=" << detections.size() << "\n\n";
    for (std::size_t index = 0; index < owners.size(); ++index) {
        const RuntimeSpyOwnerState& owner = owners[index];
        const int veteranMask = (owner.veteranInfantry ? 1 : 0)
            | (owner.veteranVehicles ? 2 : 0)
            | (owner.veteranAircraft ? 4 : 0);
        output << "[SpyRuntime.Owner." << index << "]\n";
        output << "OwnerHex=" << hexEncode(owner.owner) << "\n";
        output << "BlackoutUntilTick=" << owner.blackoutUntilTick << "\n";
        output << "VeteranMask=" << veteranMask << "\n";
        if (hasStolenTechnology) output << "StolenTechMask=" << owner.stolenTechMask << "\n";
        output << "\n";
    }
    for (std::size_t index = 0; index < disguises.size(); ++index) {
        const RuntimeSpyDisguiseState& disguise = disguises[index];
        output << "[SpyRuntime.Disguise." << index << "]\n";
        output << "ObjectId=" << disguise.objectId << "\n";
        output << "Active=" << (disguise.active ? "yes" : "no") << "\n";
        output << "Identity=" << disguiseIdentityName(disguise.identity)
               << "\n";
        output << "TypeHex=" << hexEncode(disguise.disguiseType) << "\n";
        output << "OwnerHex=" << hexEncode(disguise.disguiseOwner) << "\n";
        output << "ObserverRelative="
               << (disguise.observerRelative ? "yes" : "no") << "\n";
        output << "RearmUntilTick=" << disguise.rearmUntilTick << "\n";
        output << "LastChangeTick=" << disguise.lastChangeTick << "\n\n";
    }
    for (std::size_t index = 0; index < detections.size(); ++index) {
        const RuntimeSpyDetectionState& detection = detections[index];
        output << "[SpyRuntime.Detection." << index << "]\n";
        output << "TargetObjectId=" << detection.targetObjectId << "\n";
        output << "ObserverOwnerHex="
               << hexEncode(detection.observerOwner) << "\n";
        output << "DetectorObjectId=" << detection.detectorObjectId
               << "\n\n";
    }
}

bool RuntimeSpySystemState::load(
    const IniFile& ini,
    const RuntimeSpyContext& context,
    int currentTick,
    std::string* errorMessage,
    RuntimeSpyReconcileReport* reconcileReport
) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (reconcileReport != nullptr) {
        *reconcileReport = {};
    }
    if (ini.getSection("SpyRuntime") == nullptr) {
        reset();
        return true;
    }
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    const auto value = [&ini](
        const std::string& section,
        const std::string& key,
        const std::string& fallback = std::string()
    ) {
        return ini.getValue(section, key, fallback);
    };

    int version = 0;
    int lastAdvancedTick = -1;
    int ownerCount = 0;
    int disguiseCount = 0;
    int detectionCount = 0;
    if (!parseIntStrict(value("SpyRuntime", "Version"), version)
        || (version != 1 && version != 2 && version != 3)
        || !parseIntStrict(
            value("SpyRuntime", "LastAdvancedTick", "-1"),
            lastAdvancedTick
        )
        || lastAdvancedTick < -1
        || !parseIntStrict(
            value("SpyRuntime", "OwnerCount"),
            ownerCount
        )
        || ownerCount < 0
        || static_cast<std::size_t>(ownerCount) > maximumSavedSpyRecords
        || !parseIntStrict(
            value("SpyRuntime", "DisguiseCount"),
            disguiseCount
        )
        || disguiseCount < 0
        || static_cast<std::size_t>(disguiseCount)
            > maximumSavedSpyRecords
        || !parseIntStrict(
            value("SpyRuntime", "DetectionCount"),
            detectionCount
        )
        || detectionCount < 0
        || static_cast<std::size_t>(detectionCount)
            > maximumSavedSpyRecords) {
        return fail("spy runtime save has invalid header values");
    }

    RuntimeSpySystemState loaded;
    loaded.lastAdvancedTick_ = lastAdvancedTick;
    std::set<std::string> ownerKeys;
    for (int index = 0; index < ownerCount; ++index) {
        const std::string section = "SpyRuntime.Owner."
            + std::to_string(index);
        RuntimeSpyOwnerState owner;
        int veteranMask = 0;
        if (ini.getSection(section) == nullptr
            || !hexDecode(value(section, "OwnerHex"), owner.owner)
            || owner.owner.empty()
            || !parseIntStrict(
                value(section, "BlackoutUntilTick"),
                owner.blackoutUntilTick
            )
            || owner.blackoutUntilTick < 0
            || !parseIntStrict(
                value(section, "VeteranMask"),
                veteranMask
            )
            || veteranMask < 0
            || veteranMask > 7
            || (version >= 3 && (!parseIntStrict(value(section, "StolenTechMask"), owner.stolenTechMask)
                || owner.stolenTechMask < 0 || owner.stolenTechMask > 7))
            || !ownerKeys.insert(normalizedToken(owner.owner)).second) {
            return fail("spy runtime save has invalid owner record ["
                + section + "]");
        }
        owner.veteranInfantry = (veteranMask & 1) != 0;
        owner.veteranVehicles = (veteranMask & 2) != 0;
        owner.veteranAircraft = (veteranMask & 4) != 0;
        loaded.owners_.push_back(std::move(owner));
    }

    std::set<int> disguiseObjectIds;
    for (int index = 0; index < disguiseCount; ++index) {
        const std::string section = "SpyRuntime.Disguise."
            + std::to_string(index);
        RuntimeSpyDisguiseState disguise;
        const std::string observerRelativeValue = value(
            section,
            "ObserverRelative"
        );
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(
                value(section, "ObjectId"),
                disguise.objectId
            )
            || disguise.objectId < 0
            || !parseBoolStrict(
                value(section, "Active"),
                disguise.active
            )
            || !parseDisguiseIdentity(
                value(section, "Identity"),
                disguise.identity
            )
            || !hexDecode(
                value(section, "TypeHex"),
                disguise.disguiseType
            )
            || disguise.disguiseType.empty()
            || !hexDecode(
                value(section, "OwnerHex"),
                disguise.disguiseOwner
            )
            || (version >= 2 && observerRelativeValue.empty())
            || (!observerRelativeValue.empty()
                && !parseBoolStrict(
                    observerRelativeValue,
                    disguise.observerRelative
                ))
            || !parseIntStrict(
                value(section, "RearmUntilTick"),
                disguise.rearmUntilTick
            )
            || disguise.rearmUntilTick < 0
            || !parseIntStrict(
                value(section, "LastChangeTick"),
                disguise.lastChangeTick
            )
            || disguise.lastChangeTick < 0
            || !disguiseObjectIds.insert(disguise.objectId).second) {
            return fail("spy runtime save has invalid disguise record ["
                + section + "]");
        }
        if (version == 1 && observerRelativeValue.empty()) {
            // Version-1 saves written before ObserverRelative existed can be
            // migrated without changing their header.  Only a permanent
            // infantry disguise matching one of the authored side defaults
            // and the source's real owner has the observer-relative meaning.
            const WorldObjectInstance* object = findObject(
                context,
                disguise.objectId
            );
            const DisguiseClass objectClass = object == nullptr
                ? DisguiseClass{}
                : disguiseClassFor(context, *object);
            const SpecialMechanismRules* special = context.rules == nullptr
                ? nullptr
                : &context.rules->getSpecialMechanismRules();
            const bool matchesSideDefault = special != nullptr
                && (equalsAsciiIgnoreCase(
                        disguise.disguiseType,
                        special->alliedDisguise
                    )
                    || equalsAsciiIgnoreCase(
                        disguise.disguiseType,
                        special->sovietDisguise
                    )
                    || equalsAsciiIgnoreCase(
                        disguise.disguiseType,
                        special->thirdDisguise
                    ));
            disguise.observerRelative = object != nullptr
                && objectClass.rules != nullptr
                && objectClass.rules->permaDisguise
                && disguise.identity
                    == RuntimeSpyDisguiseIdentity::Infantry
                && matchesSideDefault
                && equalsAsciiIgnoreCase(
                    disguise.disguiseOwner,
                    canonicalOwner(context, object->owner)
                );
        }
        loaded.disguises_.push_back(std::move(disguise));
    }

    std::set<std::pair<int, std::string>> detectionKeys;
    for (int index = 0; index < detectionCount; ++index) {
        const std::string section = "SpyRuntime.Detection."
            + std::to_string(index);
        RuntimeSpyDetectionState detection;
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(
                value(section, "TargetObjectId"),
                detection.targetObjectId
            )
            || detection.targetObjectId < 0
            || !hexDecode(
                value(section, "ObserverOwnerHex"),
                detection.observerOwner
            )
            || detection.observerOwner.empty()
            || !parseIntStrict(
                value(section, "DetectorObjectId"),
                detection.detectorObjectId
            )
            || detection.detectorObjectId < 0
            || !detectionKeys.insert({
                detection.targetObjectId,
                normalizedToken(detection.observerOwner)
            }).second) {
            return fail("spy runtime save has invalid detection record ["
                + section + "]");
        }
        loaded.detections_.push_back(std::move(detection));
    }

    loaded.sortState();
    RuntimeSpyReconcileReport report = loaded.reconcile(
        context,
        std::max(0, currentTick)
    );
    *this = std::move(loaded);
    if (reconcileReport != nullptr) {
        *reconcileReport = std::move(report);
    }
    return true;
}

std::uint64_t RuntimeSpySystemState::stateHash() const {
    std::ostringstream output;
    save(output);
    return hashText(output.str());
}

const RuntimeSpyDisguiseState* RuntimeSpySystemState::disguiseState(
    int objectId
) const {
    return findDisguiseState(disguises_, objectId);
}

bool RuntimeSpySystemState::isDisguised(int objectId) const {
    const RuntimeSpyDisguiseState* state = disguiseState(objectId);
    return state != nullptr && state->active;
}

bool RuntimeSpySystemState::isDetectedBy(
    const std::string& observerOwner,
    int targetObjectId
) const {
    return std::any_of(
        detections_.begin(),
        detections_.end(),
        [&](const RuntimeSpyDetectionState& detection) {
            return detection.targetObjectId == targetObjectId
                && equalsAsciiIgnoreCase(
                    detection.observerOwner,
                    observerOwner
                );
        }
    );
}

bool RuntimeSpySystemState::canObserverSeeThroughDisguise(
    const RuntimeSpyContext& context,
    int observerObjectId,
    int targetObjectId
) const {
    if (!isDisguised(targetObjectId)) {
        return true;
    }
    const WorldObjectInstance* observer = findObject(
        context,
        observerObjectId
    );
    const WorldObjectInstance* target = findObject(context, targetObjectId);
    if (!validLiveObject(observer) || !validLiveObject(target)) {
        return false;
    }
    const std::string observerOwner = canonicalOwner(
        context,
        observer->owner
    );
    if (ownersAreAllied(
            context,
            observerOwner,
            canonicalOwner(context, target->owner)
        )) {
        return true;
    }
    return detectorCanPersonallySeeDisguise(context, *observer)
        || isDetectedBy(observerOwner, targetObjectId);
}

bool RuntimeSpySystemState::hasVeteranProduction(
    const std::string& owner,
    RuntimeSpyVeteranProduction production
) const {
    const RuntimeSpyOwnerState* state = findOwnerState(owners_, owner);
    if (state == nullptr) {
        return false;
    }
    switch (production) {
    case RuntimeSpyVeteranProduction::Infantry:
        return state->veteranInfantry;
    case RuntimeSpyVeteranProduction::Vehicles:
        return state->veteranVehicles;
    case RuntimeSpyVeteranProduction::Aircraft:
        return state->veteranAircraft;
    }
    return false;
}

bool RuntimeSpySystemState::hasStolenTechnology(
    const std::string& owner, RuntimeSpySide side
) const {
    const RuntimeSpyOwnerState* state = findOwnerState(owners_, owner);
    const int index = static_cast<int>(side);
    return state != nullptr && index >= 0 && index <= 2
        && (state->stolenTechMask & (1 << index)) != 0;
}

bool RuntimeSpySystemState::meetsStolenTechRequirements(
    const std::string& owner, const UnitType& unit
) const {
    return (!unit.requiresStolenAlliedTech || hasStolenTechnology(owner, RuntimeSpySide::Allied))
        && (!unit.requiresStolenSovietTech || hasStolenTechnology(owner, RuntimeSpySide::Soviet))
        && (!unit.requiresStolenThirdTech || hasStolenTechnology(owner, RuntimeSpySide::Third));
}

int RuntimeSpySystemState::blackoutUntilTick(
    const std::string& owner
) const {
    const RuntimeSpyOwnerState* state = findOwnerState(owners_, owner);
    return state == nullptr ? 0 : state->blackoutUntilTick;
}

const std::vector<RuntimeSpyOwnerState>&
RuntimeSpySystemState::ownerStates() const {
    return owners_;
}

const std::vector<RuntimeSpyDisguiseState>&
RuntimeSpySystemState::disguiseStates() const {
    return disguises_;
}

const std::vector<RuntimeSpyDetectionState>&
RuntimeSpySystemState::detectionStates() const {
    return detections_;
}

int RuntimeSpySystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

void RuntimeSpySystemState::sortState() {
    std::sort(
        owners_.begin(),
        owners_.end(),
        [](const auto& left, const auto& right) {
            return normalizedToken(left.owner) < normalizedToken(right.owner);
        }
    );
    std::sort(
        disguises_.begin(),
        disguises_.end(),
        [](const auto& left, const auto& right) {
            return left.objectId < right.objectId;
        }
    );
    std::sort(
        detections_.begin(),
        detections_.end(),
        [](const auto& left, const auto& right) {
            if (left.targetObjectId != right.targetObjectId) {
                return left.targetObjectId < right.targetObjectId;
            }
            return normalizedToken(left.observerOwner)
                < normalizedToken(right.observerOwner);
        }
    );
}

const char* runtimeSpyVetoName(RuntimeSpyVeto veto) {
    switch (veto) {
    case RuntimeSpyVeto::None:
        return "None";
    case RuntimeSpyVeto::InvalidContext:
        return "InvalidContext";
    case RuntimeSpyVeto::AgentMissing:
        return "AgentMissing";
    case RuntimeSpyVeto::AgentUnavailable:
        return "AgentUnavailable";
    case RuntimeSpyVeto::AgentNotInfantry:
        return "AgentNotInfantry";
    case RuntimeSpyVeto::AgentCannotInfiltrate:
        return "AgentCannotInfiltrate";
    case RuntimeSpyVeto::TargetMissing:
        return "TargetMissing";
    case RuntimeSpyVeto::TargetUnavailable:
        return "TargetUnavailable";
    case RuntimeSpyVeto::TargetNotBuilding:
        return "TargetNotBuilding";
    case RuntimeSpyVeto::TargetNotSpyable:
        return "TargetNotSpyable";
    case RuntimeSpyVeto::FriendlyTarget:
        return "FriendlyTarget";
    case RuntimeSpyVeto::SourceCannotDisguise:
        return "SourceCannotDisguise";
    case RuntimeSpyVeto::TargetCannotSupplyDisguise:
        return "TargetCannotSupplyDisguise";
    case RuntimeSpyVeto::DisguiseClassMismatch:
        return "DisguiseClassMismatch";
    case RuntimeSpyVeto::WarheadMissing:
        return "WarheadMissing";
    case RuntimeSpyVeto::WarheadDoesNotMakeDisguise:
        return "WarheadDoesNotMakeDisguise";
    case RuntimeSpyVeto::DefaultDisguiseUnavailable:
        return "DefaultDisguiseUnavailable";
    }
    return "InvalidContext";
}

const char* runtimeSpyActionName(RuntimeSpyActionKind kind) {
    switch (kind) {
    case RuntimeSpyActionKind::ConsumeAgent:
        return "ConsumeAgent";
    case RuntimeSpyActionKind::ResetRadarShroud:
        return "ResetRadarShroud";
    case RuntimeSpyActionKind::SetPowerBlackout:
        return "SetPowerBlackout";
    case RuntimeSpyActionKind::ClearPowerBlackout:
        return "ClearPowerBlackout";
    case RuntimeSpyActionKind::ResetSuperWeapon:
        return "ResetSuperWeapon";
    case RuntimeSpyActionKind::TransferCredits:
        return "TransferCredits";
    case RuntimeSpyActionKind::GrantVeteranProduction:
        return "GrantVeteranProduction";
    case RuntimeSpyActionKind::GrantStolenTechnology:
        return "GrantStolenTechnology";
    case RuntimeSpyActionKind::DisguiseChanged:
        return "DisguiseChanged";
    case RuntimeSpyActionKind::RevealDisguiseToOwner:
        return "RevealDisguiseToOwner";
    case RuntimeSpyActionKind::ConcealDisguiseFromOwner:
        return "ConcealDisguiseFromOwner";
    }
    return "ConsumeAgent";
}

const char* runtimeSpyVeteranProductionName(
    RuntimeSpyVeteranProduction production
) {
    switch (production) {
    case RuntimeSpyVeteranProduction::Infantry:
        return "Infantry";
    case RuntimeSpyVeteranProduction::Vehicles:
        return "Vehicles";
    case RuntimeSpyVeteranProduction::Aircraft:
        return "Aircraft";
    }
    return "Infantry";
}
