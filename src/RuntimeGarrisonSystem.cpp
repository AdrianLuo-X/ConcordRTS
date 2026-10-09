#include "RuntimeGarrisonSystem.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

constexpr double kAuthoredTickHz = 15.0;
constexpr std::size_t kMaximumSavedRelations = 100000U;
constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

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

bool containsId(const std::vector<int>& values, int objectId) {
    return std::find(values.begin(), values.end(), objectId) != values.end();
}

bool isInTransport(
    const std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    return std::any_of(
        objects.begin(),
        objects.end(),
        [objectId](const WorldObjectInstance& object) {
            return containsId(object.transportPassengerIds, objectId);
        }
    );
}

bool isInAnyGarrisonManifest(
    const std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    return std::any_of(
        objects.begin(),
        objects.end(),
        [objectId](const WorldObjectInstance& object) {
            return containsId(object.garrisonOccupantIds, objectId);
        }
    );
}

const UnitType* infantryType(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    if (object.kind != WorldObjectKind::Infantry) {
        return nullptr;
    }
    return rules.findUnitType("infantry", object.type);
}

const BuildingType* buildingType(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    if (object.kind != WorldObjectKind::Structure) {
        return nullptr;
    }
    return rules.findBuildingType(object.type);
}

bool occupantRecordIsUsable(
    const RulesDatabase& rules,
    const WorldObjectInstance& occupant
) {
    const UnitType* type = infantryType(rules, occupant);
    return type != nullptr && type->occupier && occupant.health > 0;
}

bool hostRecordIsUsable(
    const RulesDatabase& rules,
    const WorldObjectInstance& host,
    const RuntimeGarrisonConfig& config
) {
    const BuildingType* type = buildingType(rules, host);
    if (type == nullptr
        || !host.alive
        || host.health <= 0
        || !type->canBeOccupied
        || type->maxNumberOccupants <= 0) {
        return false;
    }
    const int maximumHealth = std::max(1, type->strength);
    return static_cast<double>(host.health)
        > static_cast<double>(maximumHealth) * config.conditionRedFraction;
}

void syncOccupantToHost(
    WorldObjectInstance& occupant,
    const WorldObjectInstance& host
) {
    occupant.x = host.x;
    occupant.y = host.y;
    occupant.cell = host.cell;
    occupant.level = host.level;
    occupant.onBridge = host.onBridge;
    occupant.hasRenderPosition = false;
    occupant.renderMoveActive = false;
    occupant.movementAnimationDistance = 0.0;
}

void activateAtPlacement(
    WorldObjectInstance& occupant,
    const RuntimeGarrisonExitPlacement& placement
) {
    occupant.x = placement.x;
    occupant.y = placement.y;
    occupant.cell = placement.cell;
    occupant.level = placement.level;
    occupant.onBridge = placement.onBridge;
    occupant.hasRenderPosition = false;
    occupant.renderMoveActive = false;
    occupant.movementAnimationDistance = 0.0;
    occupant.garrisonHostObjectId = -1;
    occupant.alive = occupant.health > 0;
    occupant.mission = occupant.alive ? "Guard" : "Destroyed";
}

bool validPlacement(const RuntimeGarrisonExitPlacement& placement) {
    return placement.x >= 0 && placement.y >= 0 && placement.cell >= 0;
}

void normalizeCursor(WorldObjectInstance& host) {
    if (host.garrisonOccupantIds.empty()) {
        host.garrisonFireCursor = 0;
        return;
    }
    const int count = static_cast<int>(host.garrisonOccupantIds.size());
    host.garrisonFireCursor %= count;
    if (host.garrisonFireCursor < 0) {
        host.garrisonFireCursor += count;
    }
}

bool removeOccupantFromHost(
    WorldObjectInstance& host,
    int occupantId
) {
    const auto found = std::find(
        host.garrisonOccupantIds.begin(),
        host.garrisonOccupantIds.end(),
        occupantId
    );
    if (found == host.garrisonOccupantIds.end()) {
        return false;
    }
    const int removedIndex = static_cast<int>(
        std::distance(host.garrisonOccupantIds.begin(), found)
    );
    host.garrisonOccupantIds.erase(found);
    if (removedIndex < host.garrisonFireCursor) {
        --host.garrisonFireCursor;
    }
    normalizeCursor(host);
    return true;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            if (character >= 'A' && character <= 'Z') {
                return static_cast<char>(character - 'A' + 'a');
            }
            return static_cast<char>(character);
        }
    );
    return value;
}

bool isLegacyGarrisonMission(const std::string& mission) {
    const std::string normalized = lowerAscii(mission);
    return normalized == "occupied" || normalized == "garrisoned";
}

bool coLocated(
    const WorldObjectInstance& occupant,
    const WorldObjectInstance& host
) {
    if (occupant.cell >= 0 && host.cell >= 0) {
        return occupant.cell == host.cell;
    }
    return occupant.x == host.x && occupant.y == host.y;
}

std::vector<std::size_t> indicesByObjectId(
    const std::vector<WorldObjectInstance>& objects
) {
    std::vector<std::size_t> indices(objects.size());
    for (std::size_t index = 0; index < objects.size(); ++index) {
        indices[index] = index;
    }
    std::stable_sort(
        indices.begin(),
        indices.end(),
        [&objects](std::size_t left, std::size_t right) {
            return objects[left].instanceId < objects[right].instanceId;
        }
    );
    return indices;
}

std::optional<std::pair<std::string, const WeaponType*>> occupantWeapon(
    const RulesDatabase& rules,
    const UnitType& occupantType,
    int veterancy
) {
    std::vector<std::string> candidates;
    if (veterancy >= 2) {
        candidates = {
            occupantType.eliteOccupyWeapon,
            occupantType.occupyWeapon,
            occupantType.elitePrimaryWeapon,
            occupantType.primaryWeapon
        };
    } else {
        candidates = {
            occupantType.occupyWeapon,
            occupantType.primaryWeapon
        };
    }
    std::unordered_set<std::string> visited;
    for (const std::string& candidate : candidates) {
        const std::string key = lowerAscii(candidate);
        if (candidate.empty() || !visited.insert(key).second) {
            continue;
        }
        const WeaponType* weapon = rules.findWeaponType(candidate);
        if (weapon != nullptr && !weapon->neverUse) {
            return std::make_pair(candidate, weapon);
        }
    }
    return std::nullopt;
}

RuntimeGarrisonConfig normalizedConfig(RuntimeGarrisonConfig config) {
    if (!std::isfinite(config.conditionRedFraction)) {
        config.conditionRedFraction = 0.25;
    }
    config.conditionRedFraction = std::clamp(
        config.conditionRedFraction,
        0.0,
        1.0
    );
    if (!std::isfinite(config.occupyDamageMultiplier)
        || config.occupyDamageMultiplier <= 0.0) {
        config.occupyDamageMultiplier = 1.0;
    }
    if (!std::isfinite(config.occupyRofMultiplier)
        || config.occupyRofMultiplier <= 0.0) {
        config.occupyRofMultiplier = 1.0;
    }
    if (!std::isfinite(config.occupyWeaponRange)
        || config.occupyWeaponRange < 0.0) {
        config.occupyWeaponRange = 5.0;
    }
    if (config.fixedTickHz <= 0) {
        config.fixedTickHz = 30;
    }
    return config;
}

bool parseIntStrict(const std::string& text, int& value) {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc() && parsed.ptr == end;
}

bool parseDoubleStrict(const std::string& text, double& value) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    input >> value;
    if (input.fail() || !std::isfinite(value)) {
        return false;
    }
    input >> std::ws;
    return input.eof();
}

bool parseIdList(const std::string& text, std::vector<int>& values) {
    values.clear();
    if (text.empty()) {
        return true;
    }
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string::npos
            ? text.size()
            : comma;
        const std::string token = text.substr(begin, end - begin);
        int value = -1;
        if (!parseIntStrict(token, value)) {
            return false;
        }
        values.push_back(value);
        if (comma == std::string::npos) {
            break;
        }
        begin = comma + 1;
    }
    return true;
}

std::uint64_t hashText(std::uint64_t hash, const std::string& value) {
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= kFnvPrime;
    }
    return hash;
}

} // namespace

RuntimeGarrisonSystemState::RuntimeGarrisonSystemState(
    RuntimeGarrisonConfig config
) {
    reset(config);
}

void RuntimeGarrisonSystemState::swap(
    RuntimeGarrisonSystemState& other
) noexcept {
    static_assert(std::is_nothrow_swappable_v<RuntimeGarrisonConfig>);
    using std::swap;
    swap(config_, other.config_);
    nextFireTickByHostId_.swap(other.nextFireTickByHostId_);
}

void swap(
    RuntimeGarrisonSystemState& left,
    RuntimeGarrisonSystemState& right
) noexcept {
    left.swap(right);
}

void RuntimeGarrisonSystemState::reset(RuntimeGarrisonConfig config) {
    config_ = normalizedConfig(config);
    nextFireTickByHostId_.clear();
}

const RuntimeGarrisonConfig& RuntimeGarrisonSystemState::config() const {
    return config_;
}

RuntimeGarrisonEntryResult RuntimeGarrisonSystemState::tryEnter(
    const RulesDatabase& rules,
    std::vector<WorldObjectInstance>& objects,
    int occupantObjectId,
    int hostObjectId,
    const RuntimeGarrisonEntryVeto& veto
) {
    RuntimeGarrisonEntryResult result;
    WorldObjectInstance* occupant = findObject(objects, occupantObjectId);
    if (occupant == nullptr) {
        result.status = RuntimeGarrisonEntryStatus::MissingOccupant;
        result.reason = "occupant object does not exist";
        return result;
    }
    WorldObjectInstance* host = findObject(objects, hostObjectId);
    if (host == nullptr) {
        result.status = RuntimeGarrisonEntryStatus::MissingHost;
        result.reason = "host object does not exist";
        return result;
    }
    if (occupantObjectId == hostObjectId) {
        result.status = RuntimeGarrisonEntryStatus::SameObject;
        result.reason = "an object cannot garrison itself";
        return result;
    }
    if (!occupant->alive || occupant->health <= 0) {
        result.status = RuntimeGarrisonEntryStatus::OccupantNotAlive;
        result.reason = "occupant must be alive";
        return result;
    }
    if (occupant->kind != WorldObjectKind::Infantry) {
        result.status = RuntimeGarrisonEntryStatus::OccupantNotInfantry;
        result.reason = "only infantry can enter a garrison";
        return result;
    }
    const UnitType* occupantRules = infantryType(rules, *occupant);
    if (occupantRules == nullptr) {
        result.status = RuntimeGarrisonEntryStatus::MissingOccupantType;
        result.reason = "occupant infantry type is missing";
        return result;
    }
    if (!occupantRules->occupier) {
        result.status = RuntimeGarrisonEntryStatus::OccupantNotOccupier;
        result.reason = "occupant type does not have Occupier=yes";
        return result;
    }
    if (!host->alive || host->health <= 0) {
        result.status = RuntimeGarrisonEntryStatus::HostNotAlive;
        result.reason = "host structure must be alive";
        return result;
    }
    if (host->kind != WorldObjectKind::Structure) {
        result.status = RuntimeGarrisonEntryStatus::HostNotStructure;
        result.reason = "garrison host must be a structure";
        return result;
    }
    const BuildingType* hostRules = buildingType(rules, *host);
    if (hostRules == nullptr) {
        result.status = RuntimeGarrisonEntryStatus::MissingHostType;
        result.reason = "host building type is missing";
        return result;
    }
    if (!hostRules->canBeOccupied) {
        result.status = RuntimeGarrisonEntryStatus::HostNotOccupiable;
        result.reason = "host does not have CanBeOccupied=yes";
        return result;
    }
    if (hostRules->maxNumberOccupants <= 0) {
        result.status = RuntimeGarrisonEntryStatus::HostHasNoCapacity;
        result.reason = "host has no positive occupant capacity";
        return result;
    }
    if (occupant->owner != host->owner) {
        result.status = RuntimeGarrisonEntryStatus::DifferentOwner;
        result.reason = "occupant and host must have the same owner";
        return result;
    }
    const int maximumHealth = std::max(1, hostRules->strength);
    if (static_cast<double>(host->health)
        <= static_cast<double>(maximumHealth) * config_.conditionRedFraction) {
        result.status = RuntimeGarrisonEntryStatus::HostAtConditionRed;
        result.reason = "host health is at or below ConditionRed";
        return result;
    }
    if (isInTransport(objects, occupantObjectId)) {
        result.status = RuntimeGarrisonEntryStatus::OccupantInTransport;
        result.reason = "occupant is inside a transport";
        return result;
    }
    if (occupant->garrisonHostObjectId >= 0
        || isInAnyGarrisonManifest(objects, occupantObjectId)) {
        result.status = RuntimeGarrisonEntryStatus::OccupantAlreadyGarrisoned;
        result.reason = "occupant already belongs to a garrison";
        return result;
    }
    if (host->garrisonOccupantIds.size()
        >= static_cast<std::size_t>(hostRules->maxNumberOccupants)) {
        result.status = RuntimeGarrisonEntryStatus::HostAtCapacity;
        result.reason = "host garrison is at capacity";
        return result;
    }
    if (veto) {
        // Pass value snapshots so a host-owned callback cannot invalidate the
        // references being inspected.  Re-run every invariant afterwards in
        // case the callback changed world state through another subsystem.
        const WorldObjectInstance occupantSnapshot = *occupant;
        const WorldObjectInstance hostSnapshot = *host;
        const std::string vetoReason = veto(occupantSnapshot, hostSnapshot);
        if (!vetoReason.empty()) {
            result.status = RuntimeGarrisonEntryStatus::Vetoed;
            result.reason = vetoReason;
            return result;
        }
        return tryEnter(
            rules,
            objects,
            occupantObjectId,
            hostObjectId,
            RuntimeGarrisonEntryVeto()
        );
    }

    const bool hostWasEmpty = host->garrisonOccupantIds.empty();
    host->garrisonOccupantIds.push_back(occupantObjectId);
    normalizeCursor(*host);
    occupant->garrisonHostObjectId = hostObjectId;
    occupant->alive = false;
    occupant->mission = "Garrisoned";
    syncOccupantToHost(*occupant, *host);
    if (hostWasEmpty) {
        nextFireTickByHostId_.erase(hostObjectId);
    }
    result.status = RuntimeGarrisonEntryStatus::Entered;
    return result;
}

void RuntimeGarrisonSystemState::syncHostOccupants(
    std::vector<WorldObjectInstance>& objects,
    int hostObjectId
) const {
    WorldObjectInstance* host = findObject(objects, hostObjectId);
    if (host == nullptr) {
        return;
    }
    const std::vector<int> occupantIds = host->garrisonOccupantIds;
    for (const int occupantId : occupantIds) {
        WorldObjectInstance* occupant = findObject(objects, occupantId);
        host = findObject(objects, hostObjectId);
        if (occupant != nullptr
            && host != nullptr
            && occupant->garrisonHostObjectId == hostObjectId) {
            syncOccupantToHost(*occupant, *host);
        }
    }
}

std::optional<RuntimeGarrisonFirePlan>
RuntimeGarrisonSystemState::takeFirePlan(
    const RulesDatabase& rules,
    std::vector<WorldObjectInstance>& objects,
    int hostObjectId,
    int targetObjectId,
    int currentTick,
    const RuntimeGarrisonFirePredicate& canFire
) {
    if (currentTick < 0) {
        return std::nullopt;
    }
    WorldObjectInstance* host = findObject(objects, hostObjectId);
    if (host == nullptr || !hostRecordIsUsable(rules, *host, config_)) {
        return std::nullopt;
    }
    const BuildingType* hostRules = buildingType(rules, *host);
    if (hostRules == nullptr
        || !hostRules->canOccupyFire
        || host->garrisonOccupantIds.empty()) {
        return std::nullopt;
    }
    const auto hostCooldown = nextFireTickByHostId_.find(hostObjectId);
    if (hostCooldown != nextFireTickByHostId_.end()
        && currentTick < hostCooldown->second) {
        return std::nullopt;
    }
    normalizeCursor(*host);
    const int occupantCount = static_cast<int>(host->garrisonOccupantIds.size());
    const int initialCursor = host->garrisonFireCursor;
    for (int offset = 0; offset < occupantCount; ++offset) {
        const int manifestIndex = (initialCursor + offset) % occupantCount;
        const int occupantId = host->garrisonOccupantIds[manifestIndex];
        WorldObjectInstance* occupant = findObject(objects, occupantId);
        if (occupant == nullptr
            || occupant->garrisonHostObjectId != hostObjectId
            || occupant->alive
            || occupant->health <= 0
            || occupant->owner != host->owner) {
            continue;
        }
        const UnitType* type = infantryType(rules, *occupant);
        if (type == nullptr || !type->occupier) {
            continue;
        }
        const auto selected = occupantWeapon(rules, *type, occupant->veterancy);
        if (!selected.has_value()) {
            continue;
        }
        const WeaponType& weapon = *selected->second;
        if (canFire && !canFire(occupantId, weapon)) {
            continue;
        }
        const int cooldownTicks = convertRuleFrames(
            static_cast<double>(std::max(0, weapon.rof))
                / static_cast<double>(occupantCount)
                / config_.occupyRofMultiplier
        );
        const int nextTick = currentTick + cooldownTicks;
        nextFireTickByHostId_[hostObjectId] = nextTick;
        host->garrisonFireCursor = (manifestIndex + 1) % occupantCount;

        RuntimeGarrisonFirePlan plan;
        plan.hostObjectId = hostObjectId;
        plan.occupantObjectId = occupantId;
        plan.occupantIndex = manifestIndex;
        plan.targetObjectId = targetObjectId;
        plan.weaponId = selected->first;
        plan.projectileId = weapon.projectile;
        plan.warheadId = weapon.warhead;
        plan.baseDamage = weapon.damage;
        plan.damage = static_cast<int>(std::lround(
            static_cast<double>(weapon.damage)
                * config_.occupyDamageMultiplier
        ));
        plan.baseRof = weapon.rof;
        plan.cooldownTicks = cooldownTicks;
        plan.nextEligibleTick = nextTick;
        // Stock explicitly uses OccupyWeaponRange instead of the selected
        // weapon's range so mixed occupants do not stop the building firing.
        plan.range = config_.occupyWeaponRange;
        plan.minimumRange = weapon.minimumRange;
        plan.burst = std::max(1, weapon.burst);
        return plan;
    }
    return std::nullopt;
}

RuntimeGarrisonExitResult RuntimeGarrisonSystemState::exitOccupant(
    std::vector<WorldObjectInstance>& objects,
    int occupantObjectId,
    const std::optional<RuntimeGarrisonExitPlacement>& placement
) {
    RuntimeGarrisonExitResult result;
    result.occupantId = occupantObjectId;
    WorldObjectInstance* occupant = findObject(objects, occupantObjectId);
    if (occupant == nullptr) {
        result.status = RuntimeGarrisonExitStatus::MissingOccupant;
        return result;
    }
    result.hostId = occupant->garrisonHostObjectId;
    if (occupant->garrisonHostObjectId < 0) {
        result.status = RuntimeGarrisonExitStatus::NotGarrisoned;
        return result;
    }
    WorldObjectInstance* host = findObject(
        objects,
        occupant->garrisonHostObjectId
    );
    if (host == nullptr) {
        result.status = RuntimeGarrisonExitStatus::MissingHost;
        return result;
    }
    if (!containsId(host->garrisonOccupantIds, occupantObjectId)) {
        result.status = RuntimeGarrisonExitStatus::NotGarrisoned;
        return result;
    }
    if (!placement.has_value() || !validPlacement(*placement)) {
        result.status = RuntimeGarrisonExitStatus::NoExitPlacement;
        return result;
    }
    removeOccupantFromHost(*host, occupantObjectId);
    activateAtPlacement(*occupant, *placement);
    if (host->garrisonOccupantIds.empty()) {
        nextFireTickByHostId_.erase(host->instanceId);
    }
    result.status = RuntimeGarrisonExitStatus::Exited;
    return result;
}

std::vector<RuntimeGarrisonEvacuationAction>
RuntimeGarrisonSystemState::evacuateValidatedOccupants(
    std::vector<WorldObjectInstance>& objects,
    int hostObjectId,
    const std::vector<int>& occupantObjectIds,
    RuntimeGarrisonEvacuationReason reason,
    const RuntimeGarrisonExitResolver& resolver,
    bool destroyWhenBlocked
) {
    std::vector<RuntimeGarrisonEvacuationAction> actions;
    WorldObjectInstance* host = findObject(objects, hostObjectId);
    if (host == nullptr) {
        return actions;
    }
    const std::vector<int> occupantIds = host->garrisonOccupantIds;
    const std::unordered_set<int> selectedOccupantIds(
        occupantObjectIds.begin(),
        occupantObjectIds.end()
    );
    actions.reserve(selectedOccupantIds.size());
    for (std::size_t index = 0; index < occupantIds.size(); ++index) {
        const int occupantId = occupantIds[index];
        if (selectedOccupantIds.find(occupantId)
            == selectedOccupantIds.end()) {
            continue;
        }
        WorldObjectInstance* occupant = findObject(objects, occupantId);
        host = findObject(objects, hostObjectId);
        if (host == nullptr) {
            break;
        }
        if (occupant == nullptr) {
            removeOccupantFromHost(*host, occupantId);
            if (host->garrisonOccupantIds.empty()) {
                nextFireTickByHostId_.erase(hostObjectId);
            }
            continue;
        }
        std::optional<RuntimeGarrisonExitPlacement> placement;
        if (resolver) {
            const WorldObjectInstance occupantSnapshot = *occupant;
            const WorldObjectInstance hostSnapshot = *host;
            placement = resolver(occupantSnapshot, hostSnapshot, reason, index);
        }
        occupant = findObject(objects, occupantId);
        host = findObject(objects, hostObjectId);
        if (occupant == nullptr || host == nullptr
            || occupant->garrisonHostObjectId != hostObjectId
            || !containsId(host->garrisonOccupantIds, occupantId)) {
            continue;
        }
        if (placement.has_value() && validPlacement(*placement)) {
            const RuntimeGarrisonExitResult exited = exitOccupant(
                objects,
                occupantId,
                placement
            );
            if (exited.exited()) {
                actions.push_back({
                    occupantId,
                    hostObjectId,
                    RuntimeGarrisonEvacuationDisposition::Exited
                });
                continue;
            }
        }
        if (!destroyWhenBlocked) {
            actions.push_back({
                occupantId,
                hostObjectId,
                RuntimeGarrisonEvacuationDisposition::KeptInside
            });
            continue;
        }
        removeOccupantFromHost(*host, occupantId);
        occupant->garrisonHostObjectId = -1;
        occupant->alive = false;
        occupant->health = 0;
        occupant->mission = "DestroyedWhileEvacuating";
        if (host->garrisonOccupantIds.empty()) {
            nextFireTickByHostId_.erase(hostObjectId);
        }
        actions.push_back({
            occupantId,
            hostObjectId,
            RuntimeGarrisonEvacuationDisposition::DestroyedNoExit
        });
    }
    return actions;
}

std::vector<RuntimeGarrisonEvacuationAction>
RuntimeGarrisonSystemState::forceEvacuate(
    std::vector<WorldObjectInstance>& objects,
    int hostObjectId,
    RuntimeGarrisonEvacuationReason reason,
    const RuntimeGarrisonExitResolver& resolver,
    bool destroyWhenBlocked
) {
    const WorldObjectInstance* host = findObject(objects, hostObjectId);
    if (host == nullptr) {
        return {};
    }
    return evacuateValidatedOccupants(
        objects,
        hostObjectId,
        host->garrisonOccupantIds,
        reason,
        resolver,
        destroyWhenBlocked
    );
}

RuntimeGarrisonSelectiveEvacuationResult
RuntimeGarrisonSystemState::forceEvacuateOccupants(
    std::vector<WorldObjectInstance>& objects,
    int hostObjectId,
    const std::vector<int>& occupantObjectIds,
    RuntimeGarrisonEvacuationReason reason,
    const RuntimeGarrisonExitResolver& resolver,
    bool destroyWhenBlocked
) {
    RuntimeGarrisonSelectiveEvacuationResult result;
    const WorldObjectInstance* host = findObject(objects, hostObjectId);
    if (host == nullptr) {
        result.status = RuntimeGarrisonSelectiveEvacuationStatus::InvalidHost;
        return result;
    }

    std::unordered_set<int> uniqueOccupantIds;
    uniqueOccupantIds.reserve(occupantObjectIds.size());
    for (const int occupantId : occupantObjectIds) {
        if (!uniqueOccupantIds.insert(occupantId).second) {
            result.status =
                RuntimeGarrisonSelectiveEvacuationStatus::DuplicateOccupant;
            result.rejectedOccupantId = occupantId;
            return result;
        }
        const WorldObjectInstance* occupant = findObject(objects, occupantId);
        if (occupant == nullptr
            || occupant->garrisonHostObjectId != hostObjectId
            || !containsId(host->garrisonOccupantIds, occupantId)) {
            result.status =
                RuntimeGarrisonSelectiveEvacuationStatus::OccupantNotInHost;
            result.rejectedOccupantId = occupantId;
            return result;
        }
    }

    result.actions = evacuateValidatedOccupants(
        objects,
        hostObjectId,
        occupantObjectIds,
        reason,
        resolver,
        destroyWhenBlocked
    );
    result.status = RuntimeGarrisonSelectiveEvacuationStatus::Applied;
    return result;
}

std::vector<RuntimeGarrisonEvacuationAction>
RuntimeGarrisonSystemState::evacuateIfConditionRed(
    const RulesDatabase& rules,
    std::vector<WorldObjectInstance>& objects,
    int hostObjectId,
    const RuntimeGarrisonExitResolver& resolver,
    bool destroyWhenBlocked
) {
    WorldObjectInstance* host = findObject(objects, hostObjectId);
    if (host == nullptr || host->kind != WorldObjectKind::Structure) {
        return {};
    }
    const BuildingType* type = buildingType(rules, *host);
    if (type == nullptr) {
        return {};
    }
    const int maximumHealth = std::max(1, type->strength);
    if (static_cast<double>(host->health)
        > static_cast<double>(maximumHealth) * config_.conditionRedFraction) {
        return {};
    }
    return forceEvacuate(
        objects,
        hostObjectId,
        RuntimeGarrisonEvacuationReason::ConditionRed,
        resolver,
        destroyWhenBlocked
    );
}

std::vector<RuntimeGarrisonEvacuationAction>
RuntimeGarrisonSystemState::handleHostDestruction(
    std::vector<WorldObjectInstance>& objects,
    int hostObjectId,
    bool immediate,
    const RuntimeGarrisonExitResolver& resolver
) {
    if (!immediate) {
        return forceEvacuate(
            objects,
            hostObjectId,
            RuntimeGarrisonEvacuationReason::HostDestroyed,
            resolver,
            true
        );
    }
    std::vector<RuntimeGarrisonEvacuationAction> actions;
    WorldObjectInstance* host = findObject(objects, hostObjectId);
    if (host == nullptr) {
        return actions;
    }
    const std::vector<int> occupantIds = host->garrisonOccupantIds;
    for (const int occupantId : occupantIds) {
        WorldObjectInstance* occupant = findObject(objects, occupantId);
        host = findObject(objects, hostObjectId);
        if (host == nullptr) {
            break;
        }
        removeOccupantFromHost(*host, occupantId);
        if (host->garrisonOccupantIds.empty()) {
            nextFireTickByHostId_.erase(hostObjectId);
        }
        if (occupant == nullptr) {
            continue;
        }
        occupant->garrisonHostObjectId = -1;
        occupant->alive = false;
        occupant->health = 0;
        occupant->mission = "DestroyedWithGarrison";
        actions.push_back({
            occupantId,
            hostObjectId,
            RuntimeGarrisonEvacuationDisposition::DestroyedWithHost
        });
    }
    return actions;
}

RuntimeGarrisonReconcileReport RuntimeGarrisonSystemState::reconcile(
    const RulesDatabase& rules,
    std::vector<WorldObjectInstance>& objects
) {
    RuntimeGarrisonReconcileReport report;
    const std::vector<std::size_t> sortedIndices = indicesByObjectId(objects);
    std::unordered_map<int, int> originalReverse;
    std::unordered_set<int> transported;
    std::unordered_set<int> rejectedCandidates;
    for (const WorldObjectInstance& object : objects) {
        originalReverse[object.instanceId] = object.garrisonHostObjectId;
        for (const int passengerId : object.transportPassengerIds) {
            transported.insert(passengerId);
        }
    }
    for (WorldObjectInstance& object : objects) {
        object.garrisonHostObjectId = -1;
    }

    std::unordered_set<int> claimed;
    for (const std::size_t objectIndex : sortedIndices) {
        WorldObjectInstance& host = objects[objectIndex];
        const std::vector<int> authoredManifest = host.garrisonOccupantIds;
        host.garrisonOccupantIds.clear();
        host.garrisonFireCursor = std::max(0, host.garrisonFireCursor);
        const BuildingType* hostRules = buildingType(rules, host);
        const bool validHost = hostRecordIsUsable(rules, host, config_);
        const int capacity = hostRules == nullptr
            ? 0
            : std::max(0, hostRules->maxNumberOccupants);
        for (const int occupantId : authoredManifest) {
            WorldObjectInstance* occupant = findObject(objects, occupantId);
            const bool relationValid = validHost
                && occupant != nullptr
                && occupantId != host.instanceId
                && occupantRecordIsUsable(rules, *occupant)
                && occupant->owner == host.owner
                && transported.find(occupantId) == transported.end()
                && claimed.find(occupantId) == claimed.end()
                && host.garrisonOccupantIds.size()
                    < static_cast<std::size_t>(capacity);
            if (!relationValid) {
                ++report.removedRelations;
                rejectedCandidates.insert(occupantId);
                report.actions.push_back({
                    RuntimeGarrisonRecoveryKind::DroppedCorruptRelation,
                    occupantId,
                    host.instanceId,
                    "host manifest relation failed validation"
                });
                continue;
            }
            host.garrisonOccupantIds.push_back(occupantId);
            claimed.insert(occupantId);
            occupant->garrisonHostObjectId = host.instanceId;
            occupant->alive = false;
            occupant->mission = "Garrisoned";
            syncOccupantToHost(*occupant, host);
            ++report.retainedRelations;
        }
        normalizeCursor(host);
    }

    // A reverse-only link is recoverable when every ordinary entry invariant
    // still holds.  This handles partial/older saves without guessing a host.
    for (const std::size_t objectIndex : sortedIndices) {
        WorldObjectInstance& occupant = objects[objectIndex];
        if (claimed.find(occupant.instanceId) != claimed.end()) {
            continue;
        }
        const auto original = originalReverse.find(occupant.instanceId);
        const int hostId = original == originalReverse.end()
            ? -1
            : original->second;
        if (hostId < 0) {
            continue;
        }
        WorldObjectInstance* host = findObject(objects, hostId);
        const BuildingType* hostRules = host == nullptr
            ? nullptr
            : buildingType(rules, *host);
        const bool canRecover = host != nullptr
            && hostRecordIsUsable(rules, *host, config_)
            && hostRules != nullptr
            && occupantRecordIsUsable(rules, occupant)
            && occupant.owner == host->owner
            && transported.find(occupant.instanceId) == transported.end()
            && host->garrisonOccupantIds.size()
                < static_cast<std::size_t>(hostRules->maxNumberOccupants);
        if (canRecover) {
            host->garrisonOccupantIds.push_back(occupant.instanceId);
            normalizeCursor(*host);
            occupant.garrisonHostObjectId = hostId;
            occupant.alive = false;
            occupant.mission = "Garrisoned";
            syncOccupantToHost(occupant, *host);
            claimed.insert(occupant.instanceId);
            ++report.recoveredRelations;
            report.actions.push_back({
                RuntimeGarrisonRecoveryKind::RecoveredReverseOnly,
                occupant.instanceId,
                hostId,
                "recovered valid reverse-only garrison relation"
            });
            continue;
        }
        ++report.removedRelations;
        if (!occupant.alive
            && occupant.health > 0
            && isLegacyGarrisonMission(occupant.mission)) {
            report.actions.push_back({
                RuntimeGarrisonRecoveryKind::NeedsExitPlacement,
                occupant.instanceId,
                hostId,
                "orphaned inactive occupant needs a safe exit placement"
            });
        } else {
            report.actions.push_back({
                RuntimeGarrisonRecoveryKind::DroppedCorruptRelation,
                occupant.instanceId,
                hostId,
                "invalid reverse garrison relation was cleared"
            });
        }
    }

    // Legacy runtime saves only marked occupants inactive with mission
    // Occupied.  Recover those only when one and only one eligible colocated
    // host exists; ambiguity is surfaced to the caller as an explicit action.
    for (const std::size_t objectIndex : sortedIndices) {
        WorldObjectInstance& occupant = objects[objectIndex];
        if (claimed.find(occupant.instanceId) != claimed.end()
            || occupant.alive
            || occupant.health <= 0
            || !isLegacyGarrisonMission(occupant.mission)
            || !occupantRecordIsUsable(rules, occupant)) {
            continue;
        }
        std::vector<int> candidates;
        for (const std::size_t hostIndex : sortedIndices) {
            WorldObjectInstance& host = objects[hostIndex];
            const BuildingType* type = buildingType(rules, host);
            if (hostRecordIsUsable(rules, host, config_)
                && type != nullptr
                && host.owner == occupant.owner
                && coLocated(occupant, host)
                && host.garrisonOccupantIds.size()
                    < static_cast<std::size_t>(type->maxNumberOccupants)) {
                candidates.push_back(host.instanceId);
            }
        }
        if (candidates.size() == 1U) {
            WorldObjectInstance* host = findObject(objects, candidates.front());
            if (host != nullptr) {
                host->garrisonOccupantIds.push_back(occupant.instanceId);
                normalizeCursor(*host);
                occupant.garrisonHostObjectId = host->instanceId;
                occupant.mission = "Garrisoned";
                syncOccupantToHost(occupant, *host);
                claimed.insert(occupant.instanceId);
                ++report.recoveredRelations;
                report.actions.push_back({
                    RuntimeGarrisonRecoveryKind::RecoveredLegacyOccupant,
                    occupant.instanceId,
                    host->instanceId,
                    "recovered unique colocated legacy occupant"
                });
                continue;
            }
        }
        if (rejectedCandidates.find(occupant.instanceId)
                != rejectedCandidates.end()
            || candidates.size() != 1U) {
            report.actions.push_back({
                RuntimeGarrisonRecoveryKind::NeedsExitPlacement,
                occupant.instanceId,
                candidates.empty() ? -1 : candidates.front(),
                candidates.empty()
                    ? "legacy occupant has no eligible host"
                    : "legacy occupant host is ambiguous"
            });
        }
    }

    std::unordered_set<int> occupiedHostIds;
    for (const WorldObjectInstance& object : objects) {
        if (!object.garrisonOccupantIds.empty()) {
            occupiedHostIds.insert(object.instanceId);
        }
    }
    for (auto iterator = nextFireTickByHostId_.begin();
         iterator != nextFireTickByHostId_.end();) {
        if (occupiedHostIds.find(iterator->first) == occupiedHostIds.end()
            || iterator->second < 0) {
            iterator = nextFireTickByHostId_.erase(iterator);
            ++report.removedCooldowns;
        } else {
            ++iterator;
        }
    }
    return report;
}

void RuntimeGarrisonSystemState::save(
    const std::vector<WorldObjectInstance>& objects,
    std::ostream& output
) const {
    std::vector<const WorldObjectInstance*> hosts;
    for (const WorldObjectInstance& object : objects) {
        if (!object.garrisonOccupantIds.empty()) {
            hosts.push_back(&object);
        }
    }
    std::stable_sort(
        hosts.begin(),
        hosts.end(),
        [](const WorldObjectInstance* left, const WorldObjectInstance* right) {
            return left->instanceId < right->instanceId;
        }
    );

    std::ostringstream saved;
    saved.imbue(std::locale::classic());
    saved << std::setprecision(std::numeric_limits<double>::max_digits10);
    saved << "[GarrisonRuntime]\n";
    saved << "Version=1\n";
    saved << "FixedTickHz=" << config_.fixedTickHz << "\n";
    saved << "ConditionRed=" << config_.conditionRedFraction << "\n";
    saved << "DamageMultiplier=" << config_.occupyDamageMultiplier << "\n";
    saved << "ROFMultiplier=" << config_.occupyRofMultiplier << "\n";
    saved << "WeaponRange=" << config_.occupyWeaponRange << "\n";
    saved << "HostCount=" << hosts.size() << "\n";
    saved << "CooldownCount=" << nextFireTickByHostId_.size() << "\n\n";
    for (std::size_t index = 0; index < hosts.size(); ++index) {
        const WorldObjectInstance& host = *hosts[index];
        saved << "[GarrisonRuntime.Host." << index << "]\n";
        saved << "HostId=" << host.instanceId << "\n";
        saved << "Cursor=" << host.garrisonFireCursor << "\n";
        saved << "Occupants=";
        for (std::size_t occupantIndex = 0;
             occupantIndex < host.garrisonOccupantIds.size();
             ++occupantIndex) {
            if (occupantIndex != 0U) {
                saved << ',';
            }
            saved << host.garrisonOccupantIds[occupantIndex];
        }
        saved << "\n\n";
    }
    std::size_t cooldownIndex = 0;
    for (const auto& [hostId, nextTick] : nextFireTickByHostId_) {
        saved << "[GarrisonRuntime.Cooldown." << cooldownIndex++ << "]\n";
        saved << "HostId=" << hostId << "\n";
        saved << "NextTick=" << nextTick << "\n\n";
    }
    output << saved.str();
}

bool RuntimeGarrisonSystemState::load(
    const IniFile& ini,
    const RulesDatabase& rules,
    std::vector<WorldObjectInstance>& objects,
    std::string* errorMessage,
    RuntimeGarrisonReconcileReport* reconcileReport
) {
    if (ini.getSection("GarrisonRuntime") == nullptr) {
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
    int hostCount = 0;
    int cooldownCount = 0;
    RuntimeGarrisonConfig loadedConfig;
    if (!parseIntStrict(value("GarrisonRuntime", "Version", "0"), version)
        || version != 1
        || !parseIntStrict(
            value("GarrisonRuntime", "FixedTickHz", "30"),
            loadedConfig.fixedTickHz
        )
        || loadedConfig.fixedTickHz <= 0
        || !parseDoubleStrict(
            value("GarrisonRuntime", "ConditionRed", "0.25"),
            loadedConfig.conditionRedFraction
        )
        || loadedConfig.conditionRedFraction < 0.0
        || loadedConfig.conditionRedFraction > 1.0
        || !parseDoubleStrict(
            value("GarrisonRuntime", "DamageMultiplier", "1.2"),
            loadedConfig.occupyDamageMultiplier
        )
        || loadedConfig.occupyDamageMultiplier <= 0.0
        || !parseDoubleStrict(
            value("GarrisonRuntime", "ROFMultiplier", "1.2"),
            loadedConfig.occupyRofMultiplier
        )
        || loadedConfig.occupyRofMultiplier <= 0.0
        || !parseDoubleStrict(
            value("GarrisonRuntime", "WeaponRange", "5"),
            loadedConfig.occupyWeaponRange
        )
        || loadedConfig.occupyWeaponRange < 0.0
        || !parseIntStrict(
            value("GarrisonRuntime", "HostCount", "0"),
            hostCount
        )
        || hostCount < 0
        || static_cast<std::size_t>(hostCount) > kMaximumSavedRelations
        || !parseIntStrict(
            value("GarrisonRuntime", "CooldownCount", "0"),
            cooldownCount
        )
        || cooldownCount < 0
        || static_cast<std::size_t>(cooldownCount) > kMaximumSavedRelations) {
        return fail("garrison runtime save has invalid header values");
    }

    struct SavedHost {
        int hostId = -1;
        int cursor = 0;
        std::vector<int> occupants;
    };
    std::vector<SavedHost> savedHosts;
    savedHosts.reserve(static_cast<std::size_t>(hostCount));
    for (int index = 0; index < hostCount; ++index) {
        const std::string section = "GarrisonRuntime.Host."
            + std::to_string(index);
        SavedHost host;
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(value(section, "HostId"), host.hostId)
            || host.hostId < 0
            || !parseIntStrict(value(section, "Cursor", "0"), host.cursor)
            || host.cursor < 0
            || !parseIdList(value(section, "Occupants", ""), host.occupants)
            || host.occupants.size() > kMaximumSavedRelations) {
            return fail("garrison runtime save has invalid host section ["
                + section + "]");
        }
        savedHosts.push_back(std::move(host));
    }

    std::map<int, int> savedCooldowns;
    for (int index = 0; index < cooldownCount; ++index) {
        const std::string section = "GarrisonRuntime.Cooldown."
            + std::to_string(index);
        int hostId = -1;
        int nextTick = -1;
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(value(section, "HostId"), hostId)
            || hostId < 0
            || !parseIntStrict(value(section, "NextTick"), nextTick)
            || nextTick < 0) {
            return fail("garrison runtime save has invalid cooldown section ["
                + section + "]");
        }
        savedCooldowns.emplace(hostId, nextTick);
    }

    RuntimeGarrisonSystemState loaded(loadedConfig);
    loaded.nextFireTickByHostId_ = std::move(savedCooldowns);
    std::vector<WorldObjectInstance> loadedObjects = objects;
    for (WorldObjectInstance& object : loadedObjects) {
        object.garrisonHostObjectId = -1;
        object.garrisonOccupantIds.clear();
        object.garrisonFireCursor = 0;
    }
    RuntimeGarrisonReconcileReport preReport;
    for (const SavedHost& savedHost : savedHosts) {
        WorldObjectInstance* host = findObject(loadedObjects, savedHost.hostId);
        if (host == nullptr) {
            preReport.removedRelations += savedHost.occupants.size();
            for (const int occupantId : savedHost.occupants) {
                preReport.actions.push_back({
                    RuntimeGarrisonRecoveryKind::DroppedCorruptRelation,
                    occupantId,
                    savedHost.hostId,
                    "saved host object does not exist"
                });
            }
            continue;
        }
        if (host->garrisonOccupantIds.empty()) {
            host->garrisonFireCursor = savedHost.cursor;
        }
        host->garrisonOccupantIds.insert(
            host->garrisonOccupantIds.end(),
            savedHost.occupants.begin(),
            savedHost.occupants.end()
        );
        for (const int occupantId : savedHost.occupants) {
            WorldObjectInstance* occupant = findObject(loadedObjects, occupantId);
            if (occupant != nullptr && occupant->garrisonHostObjectId < 0) {
                occupant->garrisonHostObjectId = savedHost.hostId;
            }
        }
    }
    RuntimeGarrisonReconcileReport report = loaded.reconcile(
        rules,
        loadedObjects
    );
    report.removedRelations += preReport.removedRelations;
    report.actions.insert(
        report.actions.begin(),
        preReport.actions.begin(),
        preReport.actions.end()
    );
    *this = std::move(loaded);
    objects = std::move(loadedObjects);
    if (reconcileReport != nullptr) {
        *reconcileReport = std::move(report);
    }
    return true;
}

std::uint64_t RuntimeGarrisonSystemState::stateHash(
    const std::vector<WorldObjectInstance>& objects
) const {
    std::ostringstream saved;
    save(objects, saved);
    return hashText(kFnvOffset, saved.str());
}

int RuntimeGarrisonSystemState::nextEligibleFireTick(
    int hostObjectId
) const {
    const auto found = nextFireTickByHostId_.find(hostObjectId);
    return found == nextFireTickByHostId_.end() ? 0 : found->second;
}

int RuntimeGarrisonSystemState::convertRuleFrames(
    double authoredFrames
) const {
    if (!std::isfinite(authoredFrames) || authoredFrames <= 0.0) {
        return 1;
    }
    return std::max(
        1,
        static_cast<int>(std::ceil(
            authoredFrames * static_cast<double>(config_.fixedTickHz)
                / kAuthoredTickHz - 1.0e-9
        ))
    );
}
