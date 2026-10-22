#include "simulation/superweapons/SuperWeaponSystem.h"
#include "simulation/superweapons/RuntimeChronosphereRelocation.h"
#include "simulation/superweapons/SuperWeaponNuclearFlight.h"
#include "simulation/superweapons/BuildingSuperWeapons.h"
#include "simulation/mod_extensions/RuntimeModExtensions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <iterator>
#include <set>
#include <sstream>
#include <type_traits>
#include <tuple>
#include <unordered_set>

namespace {

std::string trimAscii(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const std::size_t begin = value.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return {};
    }
    const std::size_t end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

std::vector<std::string> splitValues(const std::string& value) {
    std::vector<std::string> result;
    std::string current;
    for (const char character : value) {
        if (character == ',' || character == ';' || character == '|') {
            const std::string token = trimAscii(current);
            if (!token.empty()) {
                result.push_back(token);
            }
            current.clear();
        } else {
            current.push_back(character);
        }
    }
    const std::string token = trimAscii(current);
    if (!token.empty()) {
        result.push_back(token);
    }
    return result;
}

#include "simulation/superweapons/SuperWeaponDamage.inl"

double veteranArmorMultiplier(
    const WorldObjectInstance& object,
    const RulesDatabase* rules
) {
    if (rules == nullptr || object.veterancy <= 0) {
        return 1.0;
    }
    const VeterancyProfile* profile = nullptr;
    if (object.kind == WorldObjectKind::Structure) {
        if (const BuildingType* building =
                rules->findBuildingType(object.type)) {
            profile = &building->veterancy;
        }
    } else if (object.kind == WorldObjectKind::Infantry) {
        if (const UnitType* unit =
                rules->findUnitType("InfantryType", object.type)) {
            profile = &unit->veterancy;
        }
    } else {
        const UnitType* unit = rules->findUnitType("UnitType", object.type);
        if (unit == nullptr) {
            unit = rules->findUnitType("AircraftType", object.type);
        }
        if (unit != nullptr) {
            profile = &unit->veterancy;
        }
    }
    if (profile == nullptr) {
        return 1.0;
    }
    const std::vector<VeteranAbility>& abilities = object.veterancy >= 2
        ? profile->eliteAbilities
        : profile->veteranAbilities;
    if (std::find(
            abilities.begin(),
            abilities.end(),
            VeteranAbility::Stronger
        ) == abilities.end()) {
        return 1.0;
    }
    return std::max(0.01, rules->getVeterancyRules().veteranArmor);
}

int parseIntValue(const std::string& value, int fallback = 0) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        return consumed == trimAscii(value).size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::uint32_t parseUInt32Value(
    const std::string& value,
    std::uint32_t fallback = 0
) {
    try {
        const std::string normalized = trimAscii(value);
        std::size_t consumed = 0;
        const unsigned long parsed = std::stoul(normalized, &consumed);
        return consumed == normalized.size()
            ? static_cast<std::uint32_t>(parsed)
            : fallback;
    } catch (...) {
        return fallback;
    }
}

bool parseBoolValue(const std::string& value, bool fallback = false) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const char character : value) {
        normalized.push_back(
            character >= 'A' && character <= 'Z'
                ? static_cast<char>(character - 'A' + 'a')
                : character
        );
    }
    normalized = trimAscii(normalized);
    if (normalized == "yes" || normalized == "true" || normalized == "1" || normalized == "on") {
        return true;
    }
    if (normalized == "no" || normalized == "false" || normalized == "0" || normalized == "off") {
        return false;
    }
    return fallback;
}

std::string joinInts(const std::vector<int>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << values[index];
    }
    return output.str();
}

std::vector<int> parseInts(const std::string& value) {
    std::vector<int> result;
    for (const std::string& token : splitValues(value)) {
        result.push_back(parseIntValue(token));
    }
    return result;
}

std::string cellText(const SuperWeaponCell& cell) {
    return std::to_string(cell.x) + "," + std::to_string(cell.y);
}

SuperWeaponCell parseCell(
    const std::string& xValue,
    const std::string& yValue
) {
    return {
        parseIntValue(xValue, -1),
        parseIntValue(yValue, -1)
    };
}

bool isActiveState(SuperWeaponRuntimeState state) {
    return state == SuperWeaponRuntimeState::Launching
        || state == SuperWeaponRuntimeState::Active
        || state == SuperWeaponRuntimeState::Cooldown;
}

bool isInfantry(const WorldObjectInstance& object) {
    return object.kind == WorldObjectKind::Infantry;
}

bool isStructure(const WorldObjectInstance& object) {
    return object.kind == WorldObjectKind::Structure;
}

bool isUnit(const WorldObjectInstance& object) {
    return object.kind == WorldObjectKind::Unit
        || object.kind == WorldObjectKind::Infantry;
}

std::string superWeaponTypeKey(const SuperWeaponRule& rule) {
    std::string value = rule.type + " " + rule.action;
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

bool isLightningLifecycle(const SuperWeaponRule& rule) {
    const std::string type = superWeaponTypeKey(rule);
    return type.find("lightning") != std::string::npos;
}

bool isDominatorLifecycle(const SuperWeaponRule& rule) {
    const std::string type = superWeaponTypeKey(rule);
    return type.find("psychicdominator") != std::string::npos
        || type.find("dominator") != std::string::npos;
}

bool hasTrackedActiveLifecycle(const SuperWeaponRule& rule) {
    return isLightningLifecycle(rule) || isDominatorLifecycle(rule);
}

} // namespace

bool operator==(const SuperWeaponCell& left, const SuperWeaponCell& right) {
    return left.x == right.x && left.y == right.y;
}

bool operator!=(const SuperWeaponCell& left, const SuperWeaponCell& right) {
    return !(left == right);
}

void SuperWeaponSystem::swap(SuperWeaponSystem& other) noexcept {
    static_assert(std::is_nothrow_swappable_v<decltype(rules_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(ruleIndexByKey_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(instances_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(protections_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(radiation_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(ownerLockouts_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(sidebarSnapshots_)>);
    static_assert(std::is_nothrow_swappable_v<decltype(renderSnapshots_)>);
    rules_.swap(other.rules_);
    ruleIndexByKey_.swap(other.ruleIndexByKey_);
    instances_.swap(other.instances_);
    protections_.swap(other.protections_);
    radiation_.swap(other.radiation_);
    ownerLockouts_.swap(other.ownerLockouts_);
    sidebarSnapshots_.swap(other.sidebarSnapshots_);
    renderSnapshots_.swap(other.renderSnapshots_);
    using std::swap;
    swap(nextInstanceId_, other.nextInstanceId_);
    swap(nextActiveToken_, other.nextActiveToken_);
    swap(lastTick_, other.lastTick_);
}

void swap(SuperWeaponSystem& left, SuperWeaponSystem& right) noexcept {
    left.swap(right);
}

void SuperWeaponSystem::reset(const RulesDatabase& rules) {
    reset(rules.getSuperWeaponTypes());
}

void SuperWeaponSystem::reset(const std::vector<SuperWeaponRule>& rules) {
    rules_ = rules;
    ruleIndexByKey_.clear();
    for (std::size_t index = 0; index < rules_.size(); ++index) {
        ruleIndexByKey_[lowerAscii(rules_[index].id)] = index;
    }
    instances_.clear();
    protections_.clear();
    radiation_.clear();
    ownerLockouts_.clear();
    sidebarSnapshots_.clear();
    renderSnapshots_.clear();
    nextInstanceId_ = 1;
    nextActiveToken_ = 1;
    lastTick_ = 0;
}

std::string SuperWeaponSystem::lowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

std::string SuperWeaponSystem::keyFor(
    const std::string& owner,
    const std::string& superWeaponId
) {
    return lowerAscii(owner) + "|" + lowerAscii(superWeaponId);
}

std::string SuperWeaponSystem::stateName(SuperWeaponRuntimeState state) {
    switch (state) {
    case SuperWeaponRuntimeState::Unavailable: return "Unavailable";
    case SuperWeaponRuntimeState::Charging: return "Charging";
    case SuperWeaponRuntimeState::Suspended: return "Suspended";
    case SuperWeaponRuntimeState::Ready: return "Ready";
    case SuperWeaponRuntimeState::SelectingTarget: return "SelectingTarget";
    case SuperWeaponRuntimeState::AwaitingSecondTarget: return "AwaitingSecondTarget";
    case SuperWeaponRuntimeState::Launching: return "Launching";
    case SuperWeaponRuntimeState::Active: return "Active";
    case SuperWeaponRuntimeState::Cooldown: return "Cooldown";
    }
    return "Unavailable";
}

SuperWeaponRuntimeState SuperWeaponSystem::stateFromName(
    const std::string& value
) {
    const std::string normalized = lowerAscii(trimAscii(value));
    if (normalized == "charging") return SuperWeaponRuntimeState::Charging;
    if (normalized == "suspended") return SuperWeaponRuntimeState::Suspended;
    if (normalized == "ready") return SuperWeaponRuntimeState::Ready;
    if (normalized == "selectingtarget") return SuperWeaponRuntimeState::SelectingTarget;
    if (normalized == "awaitingsecondtarget") return SuperWeaponRuntimeState::AwaitingSecondTarget;
    if (normalized == "launching") return SuperWeaponRuntimeState::Launching;
    if (normalized == "active") return SuperWeaponRuntimeState::Active;
    if (normalized == "cooldown") return SuperWeaponRuntimeState::Cooldown;
    return SuperWeaponRuntimeState::Unavailable;
}

SuperWeaponTargetMode SuperWeaponSystem::targetModeFromName(
    const std::string& value
) {
    const std::string normalized = lowerAscii(trimAscii(value));
    if (normalized == "none" || normalized == "notarget") {
        return SuperWeaponTargetMode::None;
    }
    if (normalized == "area" || normalized == "radius") {
        return SuperWeaponTargetMode::Area;
    }
    if (normalized == "wholemap" || normalized == "map") {
        return SuperWeaponTargetMode::WholeMap;
    }
    if (normalized == "twostage" || normalized == "two-stage") {
        return SuperWeaponTargetMode::TwoStage;
    }
    return SuperWeaponTargetMode::Cell;
}

std::string SuperWeaponSystem::targetModeName(SuperWeaponTargetMode mode) {
    switch (mode) {
    case SuperWeaponTargetMode::None: return "None";
    case SuperWeaponTargetMode::Cell: return "Cell";
    case SuperWeaponTargetMode::Area: return "Area";
    case SuperWeaponTargetMode::WholeMap: return "WholeMap";
    case SuperWeaponTargetMode::TwoStage: return "TwoStage";
    }
    return "Cell";
}

int SuperWeaponSystem::convertRuleFrames(int frames, int fixedTickHz) {
    return std::max(
        0,
        static_cast<int>(std::llround(
            static_cast<double>(frames) * std::max(1, fixedTickHz) / 15.0
        ))
    );
}

int SuperWeaponSystem::cellDistance(
    const SuperWeaponCell& left,
    const SuperWeaponCell& right
) {
    return std::max(std::abs(left.x - right.x), std::abs(left.y - right.y));
}

std::uint64_t SuperWeaponSystem::hashString(
    std::uint64_t hash,
    const std::string& value
) {
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

SuperWeaponObjectLifecycleEvent SuperWeaponSystem::lifecycleEvent(
    SuperWeaponObjectLifecycleKind kind,
    const Instance& instance,
    int tick,
    int objectId
) {
    SuperWeaponObjectLifecycleEvent event;
    event.kind = kind;
    event.tick = tick;
    event.objectId = objectId;
    event.superWeaponInstanceId = instance.instanceId;
    event.sourceOwner = instance.owner;
    event.superWeaponId = instance.definitionId;
    return event;
}

const SuperWeaponRule* SuperWeaponSystem::findRule(
    const std::string& id
) const {
    const auto it = ruleIndexByKey_.find(lowerAscii(id));
    return it == ruleIndexByKey_.end() ? nullptr : &rules_[it->second];
}

SuperWeaponRule* SuperWeaponSystem::findRule(const std::string& id) {
    const auto it = ruleIndexByKey_.find(lowerAscii(id));
    return it == ruleIndexByKey_.end() ? nullptr : &rules_[it->second];
}

const SuperWeaponRule* SuperWeaponSystem::findPostClickHelper(
    const SuperWeaponRule& primary
) const {
    const std::string primaryId = lowerAscii(trimAscii(primary.id));
    const std::string primaryType = lowerAscii(trimAscii(primary.type));
    const std::string primaryAction = lowerAscii(trimAscii(primary.action));
    for (const SuperWeaponRule& candidate : rules_) {
        if (!candidate.postClick || candidate.preDependent.empty()) {
            continue;
        }
        const std::string dependency = lowerAscii(
            trimAscii(candidate.preDependent)
        );
        const bool matches = dependency == primaryId
            || dependency == primaryType
            || dependency == primaryAction
            || (!dependency.empty() && primaryId == dependency + "special")
            || (!primaryId.empty() && dependency == primaryId + "special");
        if (matches) {
            return &candidate;
        }
    }
    return nullptr;
}

const SuperWeaponRule* SuperWeaponSystem::targetRuleForInstance(
    const Instance& instance,
    bool secondStage
) const {
    const SuperWeaponRule* primary = findRule(instance.definitionId);
    if (!secondStage || primary == nullptr) {
        return primary;
    }
    const SuperWeaponRule* helper = findPostClickHelper(*primary);
    return helper == nullptr ? primary : helper;
}

SuperWeaponSystem::Instance* SuperWeaponSystem::findInstance(
    const std::string& owner,
    const std::string& superWeaponId
) {
    const std::string key = keyFor(owner, superWeaponId);
    for (Instance& instance : instances_) {
        if (!instance.effectOnly && keyFor(instance.owner, instance.definitionId) == key) {
            return &instance;
        }
    }
    return nullptr;
}

const SuperWeaponSystem::Instance* SuperWeaponSystem::findInstance(
    const std::string& owner,
    const std::string& superWeaponId
) const {
    const std::string key = keyFor(owner, superWeaponId);
    for (const Instance& instance : instances_) {
        if (!instance.effectOnly && keyFor(instance.owner, instance.definitionId) == key) {
            return &instance;
        }
    }
    return nullptr;
}

SuperWeaponSystem::Instance* SuperWeaponSystem::findInstanceById(int instanceId) {
    for (Instance& instance : instances_) {
        if (instance.instanceId == instanceId) {
            return &instance;
        }
    }
    return nullptr;
}

const SuperWeaponSystem::Instance* SuperWeaponSystem::findInstanceById(
    int instanceId
) const {
    for (const Instance& instance : instances_) {
        if (instance.instanceId == instanceId) {
            return &instance;
        }
    }
    return nullptr;
}

SuperWeaponSystem::Instance& SuperWeaponSystem::ensureInstance(
    const std::string& owner,
    const std::string& superWeaponId
) {
    if (Instance* existing = findInstance(owner, superWeaponId)) {
        return *existing;
    }
    Instance instance;
    instance.instanceId = nextInstanceId_++;
    instance.owner = owner;
    instance.definitionId = superWeaponId;
    instance.targetMode = targetModeFromName(
        findRule(superWeaponId) == nullptr
            ? "Cell"
            : findRule(superWeaponId)->targetMode
    );
    instance.rechargeTicks = 1;
    instances_.push_back(std::move(instance));
    return instances_.back();
}

void SuperWeaponSystem::emit(
    const SuperWeaponWorldContext& context,
    const Instance& instance,
    const std::string& kind,
    const std::string& reason,
    const SuperWeaponTarget* target,
    const SuperWeaponTarget* target2,
    int objectId,
    int targetObjectId,
    int value,
    const std::string& soundIntent,
    const std::string& evaIntent,
    const std::string& animationIntent,
    int soundSourceObjectId
) const {
    if (!context.emit) {
        return;
    }
    SuperWeaponEvent event;
    event.tick = context.tick;
    event.instanceId = instance.instanceId;
    event.activeToken = instance.activeToken;
    event.sourceBuildingId = instance.sourceBuildingIds.empty()
        ? -1
        : instance.sourceBuildingIds.front();
    event.objectId = objectId;
    event.targetObjectId = targetObjectId;
    event.value = value;
    event.owner = instance.owner;
    event.superWeaponId = instance.definitionId;
    event.kind = kind;
    event.state = stateName(instance.state);
    event.reason = reason;
    event.commandVerb = kind == "SuperWeaponCommandAccepted"
        ? "ActivateSuperWeapon"
        : "";
    if (target != nullptr) {
        event.targetCell = target->cell;
        if (event.targetObjectId < 0) {
            event.targetObjectId = target->objectId;
        }
    }
    if (target2 != nullptr) {
        event.targetCell2 = target2->cell;
    }
    event.soundSourceObjectId = soundSourceObjectId;
    event.soundIntent = soundIntent;
    event.evaIntent = evaIntent;
    event.animationIntent = animationIntent;
    context.emit(event);
}

void SuperWeaponSystem::emitImpact(
    const SuperWeaponWorldContext& context,
    const Instance& instance,
    const std::string& kind,
    const SuperWeaponCell& cell,
    int radius,
    int damage,
    const std::string& warheadId,
    const std::string& animationId,
    const std::string& secondaryAnimationId
) const {
    if (!context.onSuperWeaponImpact) {
        return;
    }
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    SuperWeaponImpactEvent event;
    event.tick = context.tick;
    event.instanceId = instance.instanceId;
    event.activeToken = instance.activeToken;
    event.kind = kind;
    event.superWeaponId = instance.definitionId;
    event.superWeaponType = rule == nullptr ? "" : rule->type;
    event.sourceOwner = instance.owner;
    event.sourceObjectId = instance.sourceBuildingIds.empty()
        ? -1
        : instance.sourceBuildingIds.front();
    event.cell = cell;
    event.radius = std::max(0, radius);
    event.damage = std::max(0, damage);
    event.warheadId = warheadId;
    event.animationId = animationId;
    event.secondaryAnimationId = secondaryAnimationId;
    context.onSuperWeaponImpact(std::move(event));
}

void SuperWeaponSystem::beginActiveLifecycle(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    if (instance.activeToken >= 0) {
        return;
    }
    instance.activeToken = nextActiveToken_++;
    instance.activeStartedTick = context.tick;
    if (!context.onSuperWeaponActiveLifecycle) {
        return;
    }
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    SuperWeaponActiveLifecycleEvent event;
    event.phase = SuperWeaponActiveLifecyclePhase::Started;
    event.tick = context.tick;
    event.instanceId = instance.instanceId;
    event.activeToken = instance.activeToken;
    event.superWeaponId = instance.definitionId;
    event.superWeaponType = rule == nullptr ? "" : rule->type;
    event.sourceOwner = instance.owner;
    event.sourceObjectId = instance.sourceBuildingIds.empty()
        ? -1
        : instance.sourceBuildingIds.front();
    event.cell = instance.firstTarget.cell;
    event.startedTick = instance.activeStartedTick;
    event.endsTick = instance.activeUntilTick;
    event.durationTicks = instance.activeUntilTick < instance.activeStartedTick
        ? 0
        : instance.activeUntilTick - instance.activeStartedTick;
    context.onSuperWeaponActiveLifecycle(std::move(event));
}

void SuperWeaponSystem::endActiveLifecycle(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    if (instance.activeToken < 0) {
        return;
    }
    if (context.onSuperWeaponActiveLifecycle) {
        const SuperWeaponRule* rule = findRule(instance.definitionId);
        SuperWeaponActiveLifecycleEvent event;
        event.phase = SuperWeaponActiveLifecyclePhase::Ended;
        event.tick = context.tick;
        event.instanceId = instance.instanceId;
        event.activeToken = instance.activeToken;
        event.superWeaponId = instance.definitionId;
        event.superWeaponType = rule == nullptr ? "" : rule->type;
        event.sourceOwner = instance.owner;
        event.sourceObjectId = instance.sourceBuildingIds.empty()
            ? -1
            : instance.sourceBuildingIds.front();
        event.cell = instance.firstTarget.cell;
        event.startedTick = instance.activeStartedTick;
        event.endsTick = instance.activeUntilTick;
        event.durationTicks = instance.activeUntilTick < instance.activeStartedTick
            ? 0
            : instance.activeUntilTick - instance.activeStartedTick;
        context.onSuperWeaponActiveLifecycle(std::move(event));
    }
    instance.activeToken = -1;
    instance.activeStartedTick = -1;
}

void SuperWeaponSystem::cancelActiveLifecycle(
    Instance& instance,
    const SuperWeaponWorldContext* context
) {
    if (instance.activeToken >= 0) {
        if (context != nullptr) {
            if (const SuperWeaponRule* rule = findRule(instance.definitionId)) {
                emit(
                    *context,
                    instance,
                    isDominatorLifecycle(*rule)
                        ? "PsychicDominatorEnded"
                        : "LightningStormEnded",
                    "interrupted"
                );
            }
            endActiveLifecycle(instance, *context);
        } else {
            instance.activeToken = -1;
            instance.activeStartedTick = -1;
        }
    }
    instance.activeUntilTick = -1;
    instance.nextEffectTick = -1;
}

bool SuperWeaponSystem::instanceHasLiveSource(
    const Instance& instance,
    const SuperWeaponWorldContext& context,
    bool* powered
) const {
    bool found = false;
    bool foundPowered = false;
    for (const int sourceId : instance.sourceBuildingIds) {
        if (sourceId < 0) {
            found = true;
            foundPowered = true;
            continue;
        }
        if (context.objects == nullptr) {
            continue;
        }
        for (const WorldObjectInstance& object : *context.objects) {
            if (object.instanceId != sourceId
                || object.kind != WorldObjectKind::Structure
                || !object.alive
                || object.health == 0) {
                continue;
            }
            found = true;
            if (object.poweredOperational) {
                foundPowered = true;
            }
            break;
        }
    }
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    if (rule != nullptr && rule->isPowered && context.ownerPowered
        && !context.ownerPowered(instance.owner)) {
        foundPowered = false;
    }
    // The local no-cooldown option permits repeated support powers through
    // power loss (including Force Shield's own blackout). It still needs a
    // live grant; the host's building/unit power simulation is unchanged.
    if (found && context.noCooldown && context.noCooldown(instance.owner))
        foundPowered = true;
    if (powered != nullptr) {
        *powered = foundPowered;
    }
    return found;
}

void SuperWeaponSystem::reconcileSources(
    const SuperWeaponWorldContext& context
) {
    std::unordered_map<std::string, std::vector<int>> discovered;
    if (context.objects != nullptr && context.rules != nullptr) {
        for (const WorldObjectInstance& object : *context.objects) {
            if (object.kind != WorldObjectKind::Structure
                || !object.alive
                || object.health == 0
                || object.owner.empty()) {
                continue;
            }
            const BuildingType* building = context.rules->findBuildingType(object.type);
            if (building == nullptr) {
                continue;
            }
            const auto definitions = buildingSuperWeapons(*building);
            for (const std::string& definition : definitions) {
                const SuperWeaponRule* rule = findRule(definition);
                if (definition.empty() || rule == nullptr || rule->postClick) {
                    continue;
                }
                discovered[keyFor(object.owner, definition)].push_back(
                    object.instanceId
                );
            }
        }
    }

    for (Instance& instance : instances_) {
        // An accepted payload retains its original launcher/owner even if
        // buildings are sold, captured, or another cast becomes ready.
        if (instance.effectOnly) continue;
        if (instance.sourceBuildingIds.size() == 1U
            && instance.sourceBuildingIds.front() < 0) {
            continue;
        }
        const auto found = discovered.find(
            keyFor(instance.owner, instance.definitionId)
        );
        if (found == discovered.end()) {
            instance.sourceBuildingIds.clear();
        } else {
            instance.sourceBuildingIds = found->second;
        }
    }

    for (const auto& [key, sourceIds] : discovered) {
        const std::size_t separator = key.find('|');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string owner = key.substr(0, separator);
        const std::string definitionId = key.substr(separator + 1);
        const auto* source = sourceIds.empty() ? nullptr : findObject(sourceIds.front(), context);
        const auto* rule = findRule(definitionId);
        Instance& instance = ensureInstance(source ? source->owner : owner,
            rule ? rule->id : definitionId);
        // Lookup keys are case-insensitive; world owner and resource IDs are
        // not lookup keys. Preserve their authored identity for spawned units
        // and the host's ownership/command systems.
        if (source) instance.owner = source->owner;
        if (rule) instance.definitionId = rule->id;
        instance.sourceBuildingIds = sourceIds;
    }

    std::sort(
        instances_.begin(),
        instances_.end(),
        [](const Instance& left, const Instance& right) {
            const std::string leftKey = keyFor(left.owner, left.definitionId);
            const std::string rightKey = keyFor(right.owner, right.definitionId);
            return leftKey == rightKey
                ? left.instanceId < right.instanceId
                : leftKey < rightKey;
        }
    );

    // An ownership transaction can reconcile sources between simulation
    // ticks. Invalidate selection immediately; launched effects retain their
    // own lifecycle and are never advanced by source reconciliation.
    for (Instance& instance : instances_) {
        if (instance.effectOnly) continue;
        if (!isActiveState(instance.state) && !instanceHasLiveSource(instance, context, nullptr)) {
            if (instance.state != SuperWeaponRuntimeState::Unavailable)
                emit(context, instance, "SuperWeaponSourceLost");
            instance.state = SuperWeaponRuntimeState::Unavailable;
            instance.firstTarget = {};
            instance.secondTarget = {};
        }
    }
    rebuildSnapshots(context.tick);
}

const WorldObjectInstance* SuperWeaponSystem::findObject(
    int objectId,
    const SuperWeaponWorldContext& context
) const {
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

WorldObjectInstance* SuperWeaponSystem::findObject(
    int objectId,
    const SuperWeaponWorldContext& context
) {
    if (context.objects == nullptr) {
        return nullptr;
    }
    for (WorldObjectInstance& object : *context.objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

bool SuperWeaponSystem::ownerAllied(
    const std::string& left,
    const std::string& right,
    const SuperWeaponWorldContext& context
) const {
    if (lowerAscii(left) == lowerAscii(right)) {
        return true;
    }
    return context.areAllied && context.areAllied(left, right);
}

bool SuperWeaponSystem::objectImmune(
    const WorldObjectInstance& object,
    const std::string& mechanism,
    const SuperWeaponWorldContext& context
) const {
    if (context.rules == nullptr) {
        return false;
    }
    const UnitType* unit = nullptr;
    if (object.kind == WorldObjectKind::Infantry) {
        unit = context.rules->findUnitType("Infantry", object.type);
    } else if (object.kind == WorldObjectKind::Unit) {
        unit = context.rules->findUnitType("Vehicle", object.type);
        if (unit == nullptr) {
            unit = context.rules->findUnitType("Aircraft", object.type);
        }
    }
    if (unit != nullptr) {
        if (mechanism == "radiation") return unit->immuneToRadiation;
        if (mechanism == "temporal") return unit->immuneToTemporal;
        if (mechanism == "psionics") return unit->immuneToPsionics;
        if (mechanism == "electric") return unit->immuneToElectric;
    }
    if (object.kind == WorldObjectKind::Structure) {
        const BuildingType* building = context.rules->findBuildingType(object.type);
        if (building != nullptr) {
            if (mechanism == "radiation") return building->immuneToRadiation;
            if (mechanism == "temporal") return building->immuneToTemporal;
            if (mechanism == "psionics") return building->immuneToPsionics;
            if (mechanism == "electric") return building->immuneToElectric;
        }
    }
    return false;
}

bool SuperWeaponSystem::targetIsAllowed(
    const Instance& instance,
    const WorldObjectInstance& object,
    const SuperWeaponWorldContext& context,
    const SuperWeaponRule* targetRule
) const {
    const SuperWeaponRule* rule = targetRule == nullptr
        ? findRule(instance.definitionId)
        : targetRule;
    if (rule == nullptr || !object.alive || object.health == 0) {
        return false;
    }
    const std::string family = lowerAscii(rule->type + " " + rule->action);
    const bool forceShield = family.find("forceshield") != std::string::npos;
    const bool geneticMutator = family.find("genetic") != std::string::npos;
    if (forceShield && !isStructure(object)) {
        return false;
    }
    if (geneticMutator && !isInfantry(object)) {
        return false;
    }
    const bool allied = ownerAllied(instance.owner, object.owner, context);
    // Stock Genetic Mutator can be aimed at either side and affects both.
    if (!geneticMutator) {
        if (allied && !rule->targetFriendly) {
            return false;
        }
        if (!allied && !rule->targetEnemy) {
            return false;
        }
    }
    if (object.kind == WorldObjectKind::Infantry && !rule->targetInfantry) {
        return false;
    }
    if (object.kind == WorldObjectKind::Unit && !rule->targetUnits) {
        return false;
    }
    if (object.kind == WorldObjectKind::Structure && !rule->targetBuildings) {
        return false;
    }
    return true;
}

bool SuperWeaponSystem::validateTarget(
    const Instance& instance,
    const SuperWeaponTarget& target,
    const SuperWeaponWorldContext& context,
    std::string* reason,
    const SuperWeaponRule* targetRule
) const {
    auto reject = [&](const std::string& message) {
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };
    const SuperWeaponRule* rule = targetRule == nullptr
        ? findRule(instance.definitionId)
        : targetRule;
    if (rule == nullptr) {
        return reject("missing definition");
    }
    const std::string family = lowerAscii(rule->type + " " + rule->action);
    const bool forceShield = family.find("forceshield") != std::string::npos;
    if (instance.disabled
        || (!context.superWeaponsAllowed && rule->disableableFromShell)) {
        return reject("disabled by game option");
    }
    if (instance.targetMode == SuperWeaponTargetMode::None) {
        return true;
    }
    if (target.cell.x < 0 || target.cell.y < 0
        || (context.mapWidth > 0 && target.cell.x >= context.mapWidth)
        || (context.mapHeight > 0 && target.cell.y >= context.mapHeight)) {
        return reject("outside map");
    }
    if (context.cellLegal
        && !context.cellLegal(target.cell.x, target.cell.y, instance.targetMode)) {
        return reject("illegal target cell");
    }
    if (rule->exploredOnly && context.explored
        && !context.explored(instance.owner, target.cell.x, target.cell.y)) {
        return reject("target is unexplored");
    }
    const SuperWeaponRule* primary = findRule(instance.definitionId);
    const bool chronoGround = primary != nullptr
        && (lowerAscii(trimAscii(primary->type)) == "chronosphere"
            || lowerAscii(trimAscii(primary->action)) == "chronosphere");
    // Force Shield and Chronosphere choose ground areas. A hovered object
    // does not constrain either Chronosphere click; applyChrono independently
    // selects eligible units in the source area and checks their destinations.
    if (target.objectId >= 0 && !forceShield && !chronoGround) {
        const WorldObjectInstance* object = findObject(target.objectId, context);
        // Damage-area clicks may point at any side. The dominator's capture
        // eligibility remains separate from its building-damage footprint.
        const bool damageArea = family.find("nuke") != std::string::npos
            || family.find("multimissile") != std::string::npos
            || family.find("lightning") != std::string::npos
            || family.find("dominator") != std::string::npos;
        const auto* unit = object ? superWeaponUnitType(*object, context.rules) : nullptr;
        const bool ironOrganic = family.find("ironcurtain") != std::string::npos
            && object && (isInfantry(*object) || (unit && unit->organic));
        if (object == nullptr
            || !object->alive || object->health == 0
            || (!(damageArea || ironOrganic)
                && !targetIsAllowed(instance, *object, context, rule))) {
            return reject("target object is not legal");
        }
    }
    return true;
}

bool SuperWeaponSystem::previewTarget(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponTarget& target,
    const SuperWeaponWorldContext& context,
    std::string* reason
) const {
    const Instance* instance = findInstance(owner, superWeaponId);
    if (instance == nullptr) {
        if (reason != nullptr) *reason = "super weapon is unavailable";
        return false;
    }
    return validateTarget(
        *instance,
        target,
        context,
        reason,
        targetRuleForInstance(
            *instance,
            instance->state == SuperWeaponRuntimeState::AwaitingSecondTarget
                || instance->targetStage == 1
        )
    );
}

bool SuperWeaponSystem::beginTargetSelection(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponWorldContext& context,
    std::string* reason
) {
    Instance* instance = findInstance(owner, superWeaponId);
    if (instance == nullptr) {
        if (reason != nullptr) *reason = "super weapon is unavailable";
        return false;
    }
    if (instance->state != SuperWeaponRuntimeState::Ready) {
        if (reason != nullptr) *reason = "super weapon is not ready";
        if (instance->state == SuperWeaponRuntimeState::Charging
            || instance->state == SuperWeaponRuntimeState::Suspended) {
            const SuperWeaponRule* rule = findRule(instance->definitionId);
            emit(
                context,
                *instance,
                "SuperWeaponImpatient",
                "super weapon is not ready",
                nullptr,
                nullptr,
                -1,
                -1,
                0,
                "",
                rule == nullptr ? "" : rule->impatientVoice
            );
        }
        return false;
    }
    instance->targetStage = 0;
    instance->state = SuperWeaponRuntimeState::SelectingTarget;
    instance->stateStartTick = context.tick;
    emit(context, *instance, "SuperWeaponTargetSelectionStarted");
    rebuildSnapshots(context.tick);
    return true;
}

bool SuperWeaponSystem::cancelTargetSelection(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponWorldContext& context,
    const std::string& reason
) {
    Instance* instance = findInstance(owner, superWeaponId);
    if (instance == nullptr
        || (instance->state != SuperWeaponRuntimeState::SelectingTarget
            && instance->state != SuperWeaponRuntimeState::AwaitingSecondTarget)) {
        return false;
    }
    instance->targetStage = 0;
    instance->state = SuperWeaponRuntimeState::Ready;
    emit(context, *instance, "SuperWeaponTargetSelectionCancelled", reason);
    rebuildSnapshots(context.tick);
    return true;
}

bool SuperWeaponSystem::submitTarget(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponTarget& target,
    const SuperWeaponWorldContext& context,
    std::string* reason
) {
    Instance* instance = findInstance(owner, superWeaponId);
    if (instance == nullptr) {
        if (reason != nullptr) *reason = "super weapon is unavailable";
        return false;
    }
    if (instance->state != SuperWeaponRuntimeState::SelectingTarget
        && instance->state != SuperWeaponRuntimeState::AwaitingSecondTarget) {
        if (reason != nullptr) *reason = "target selection is not active";
        return false;
    }
    const bool secondStage =
        instance->state == SuperWeaponRuntimeState::AwaitingSecondTarget
        || instance->targetStage == 1;
    if (!validateTarget(
            *instance,
            target,
            context,
            reason,
            targetRuleForInstance(*instance, secondStage)
        )) {
        emit(context, *instance, "SuperWeaponCommandRejected", reason == nullptr ? "invalid target" : *reason, &target);
        return false;
    }
    if (instance->targetMode == SuperWeaponTargetMode::TwoStage) {
        if (instance->targetStage == 0) {
            instance->firstTarget = target;
            instance->targetStage = 1;
            instance->state = SuperWeaponRuntimeState::AwaitingSecondTarget;
            instance->stateStartTick = context.tick;
            emit(context, *instance, "SuperWeaponFirstTargetAccepted", "", &target);
            rebuildSnapshots(context.tick);
            return true;
        }
        instance->secondTarget = target;
        instance->targetStage = 2;
    } else {
        instance->firstTarget = target;
        instance->targetStage = 2;
    }
    instance->launchTick = context.tick + 1;
    instance->chargeTicks = 0;
    instance->state = SuperWeaponRuntimeState::Launching;
    instance->stateStartTick = context.tick;
    emit(
        context,
        *instance,
        "SuperWeaponCommandAccepted",
        "",
        &instance->firstTarget,
        instance->targetMode == SuperWeaponTargetMode::TwoStage
            ? &instance->secondTarget
            : nullptr
    );
    if (!instance->oneShot) {
        const auto* rule = findRule(instance->definitionId);
        emit(context, *instance, "SuperWeaponChargeStarted", "", nullptr, nullptr,
            -1, -1, 0, "", rule == nullptr ? "" : rule->chargingVoice);
    }
    detachNoCooldownCast(*instance, context);
    rebuildSnapshots(context.tick);
    return true;
}

bool SuperWeaponSystem::submitTwoStageTarget(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponTarget& first,
    const SuperWeaponTarget& second,
    const SuperWeaponWorldContext& context,
    std::string* reason
) {
    Instance* instance = findInstance(owner, superWeaponId);
    if (instance == nullptr) {
        if (reason != nullptr) *reason = "super weapon is unavailable";
        return false;
    }
    if (instance->state == SuperWeaponRuntimeState::Ready) {
        instance->state = SuperWeaponRuntimeState::SelectingTarget;
    }
    if (instance->state != SuperWeaponRuntimeState::SelectingTarget
        && instance->state != SuperWeaponRuntimeState::AwaitingSecondTarget) {
        if (reason != nullptr) *reason = "target selection is not active";
        return false;
    }
    if (!validateTarget(
            *instance,
            first,
            context,
            reason,
            targetRuleForInstance(*instance, false)
        )
        || !validateTarget(
            *instance,
            second,
            context,
            reason,
            targetRuleForInstance(*instance, true)
        )) {
        return false;
    }
    instance->firstTarget = first;
    instance->secondTarget = second;
    instance->targetStage = 2;
    instance->launchTick = context.tick + 1;
    instance->chargeTicks = 0;
    instance->state = SuperWeaponRuntimeState::Launching;
    instance->stateStartTick = context.tick;
    emit(context, *instance, "SuperWeaponCommandAccepted", "", &first, &second);
    if (!instance->oneShot) {
        const auto* rule = findRule(instance->definitionId);
        emit(context, *instance, "SuperWeaponChargeStarted", "", nullptr, nullptr,
            -1, -1, 0, "", rule == nullptr ? "" : rule->chargingVoice);
    }
    detachNoCooldownCast(*instance, context);
    rebuildSnapshots(context.tick);
    return true;
}

bool SuperWeaponSystem::grant(
    const std::string& owner,
    const std::string& superWeaponId,
    int sourceBuildingId,
    const SuperWeaponWorldContext* context
) {
    if (findRule(superWeaponId) == nullptr || owner.empty()) {
        return false;
    }
    const SuperWeaponRule* rule = findRule(superWeaponId);
    Instance& instance = ensureInstance(owner, superWeaponId);
    if (sourceBuildingId >= 0) {
        if (std::find(
                instance.sourceBuildingIds.begin(),
                instance.sourceBuildingIds.end(),
                sourceBuildingId
            ) == instance.sourceBuildingIds.end()) {
            instance.sourceBuildingIds.push_back(sourceBuildingId);
        }
    } else if (instance.sourceBuildingIds.empty()) {
        // A virtual source is used by trigger grants and keeps the charge
        // alive until the host later reconciles real source buildings.
        instance.sourceBuildingIds.push_back(-1);
    }
    const bool startedCharging =
        instance.state == SuperWeaponRuntimeState::Unavailable;
    if (startedCharging) {
        instance.state = SuperWeaponRuntimeState::Charging;
        instance.stateStartTick = context == nullptr ? lastTick_ : context->tick;
    }
    if (context != nullptr) {
        emit(
            *context,
            instance,
            "SuperWeaponGranted",
            "",
            nullptr,
            nullptr,
            -1,
            -1,
            0,
            "",
            startedCharging && rule != nullptr ? rule->chargingVoice : ""
        );
    }
    return true;
}

bool SuperWeaponSystem::grantOneShot(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponWorldContext* context
) {
    if (owner.empty() || findRule(superWeaponId) == nullptr) {
        return false;
    }

    Instance* instance = findInstance(owner, superWeaponId);
    if (instance != nullptr && context != nullptr
        && context->objects != nullptr) {
        instance->sourceBuildingIds.erase(
            std::remove_if(
                instance->sourceBuildingIds.begin(),
                instance->sourceBuildingIds.end(),
                [&](int sourceId) {
                    if (sourceId < 0) {
                        return false;
                    }
                    return std::none_of(
                        context->objects->begin(),
                        context->objects->end(),
                        [&](const WorldObjectInstance& object) {
                            return object.instanceId == sourceId
                                && object.kind == WorldObjectKind::Structure
                                && object.alive
                                && object.health != 0;
                        }
                    );
                }
            ),
            instance->sourceBuildingIds.end()
        );
    }

    // Any surviving concrete source already grants the ordinary superweapon.
    // Likewise, an existing virtual source owns its current lifetime (one-shot
    // or repeating). Treat both cases as success without changing that policy
    // or emitting a duplicate grant event.
    if (instance != nullptr && !instance->sourceBuildingIds.empty()) {
        return true;
    }

    if (!grant(owner, superWeaponId, -1, context)) {
        return false;
    }
    instance = findInstance(owner, superWeaponId);
    if (instance == nullptr) {
        return false;
    }
    instance->oneShot = true;
    return true;
}

bool SuperWeaponSystem::forceReady(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponWorldContext* context
) {
    const Instance* before = findInstance(owner, superWeaponId);
    const bool wasReady = before != nullptr
        && before->state == SuperWeaponRuntimeState::Ready;
    if (!grant(owner, superWeaponId, -1, context)) {
        return false;
    }
    Instance& instance = *findInstance(owner, superWeaponId);
    cancelActiveLifecycle(instance, context);
    const int tick = context == nullptr ? lastTick_ : context->tick;
    instance.chargeTicks = instance.rechargeTicks;
    instance.state = SuperWeaponRuntimeState::Ready;
    if (!wasReady) {
        instance.stateStartTick = tick;
    }
    if (context != nullptr) {
        const SuperWeaponRule* rule = findRule(superWeaponId);
        emit(
            *context,
            instance,
            "SuperWeaponReady",
            "forced",
            nullptr,
            nullptr,
            -1,
            -1,
            0,
            "",
            !wasReady && rule != nullptr ? rule->rechargeVoice : ""
        );
    }
    return true;
}

bool SuperWeaponSystem::resetCharge(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponWorldContext* context
) {
    Instance* instance = findInstance(owner, superWeaponId);
    if (instance == nullptr) {
        return false;
    }
    cancelActiveLifecycle(*instance, context);
    instance->chargeTicks = 0;
    instance->targetStage = 0;
    instance->state = SuperWeaponRuntimeState::Charging;
    instance->stateStartTick = context == nullptr ? lastTick_ : context->tick;
    if (context != nullptr) {
        const SuperWeaponRule* rule = findRule(superWeaponId);
        emit(
            *context,
            *instance,
            "SuperWeaponChargeReset",
            "",
            nullptr,
            nullptr,
            -1,
            -1,
            0,
            "",
            rule == nullptr ? "" : rule->chargingVoice
        );
    }
    return true;
}

bool SuperWeaponSystem::setChargePercent(
    const std::string& owner,
    const std::string& superWeaponId,
    int percent,
    const SuperWeaponWorldContext* context
) {
    if (!grant(owner, superWeaponId, -1, context)) {
        return false;
    }
    Instance& instance = *findInstance(owner, superWeaponId);
    const SuperWeaponRuntimeState previousState = instance.state;
    cancelActiveLifecycle(instance, context);
    const int clampedPercent = std::clamp(percent, 0, 100);
    instance.chargeTicks = static_cast<int>(
        (static_cast<long long>(instance.rechargeTicks) * clampedPercent)
        / 100
    );
    instance.targetStage = 0;
    instance.state = clampedPercent >= 100
        ? SuperWeaponRuntimeState::Ready
        : SuperWeaponRuntimeState::Charging;
    instance.stateStartTick = context == nullptr ? lastTick_ : context->tick;
    if (context != nullptr) {
        const SuperWeaponRule* rule = findRule(superWeaponId);
        const std::string voice = rule == nullptr
            ? ""
            : instance.state == SuperWeaponRuntimeState::Ready
                    && previousState != SuperWeaponRuntimeState::Ready
                ? rule->rechargeVoice
                : instance.state == SuperWeaponRuntimeState::Charging
                        && previousState != SuperWeaponRuntimeState::Charging
                    ? rule->chargingVoice
                    : "";
        emit(
            *context,
            instance,
            clampedPercent >= 100
                ? "SuperWeaponReady"
                : "SuperWeaponChargeSet",
            "",
            nullptr,
            nullptr,
            -1,
            -1,
            0,
            "",
            voice
        );
    }
    return true;
}

bool SuperWeaponSystem::setRechargeTicks(
    const std::string& owner,
    const std::string& superWeaponId,
    int ticks,
    const SuperWeaponWorldContext* context
) {
    if (!grant(owner, superWeaponId, -1, context)) {
        return false;
    }
    Instance& instance = *findInstance(owner, superWeaponId);
    const SuperWeaponRuntimeState previousState = instance.state;
    cancelActiveLifecycle(instance, context);
    instance.rechargeTicks = std::max(1, ticks);
    instance.chargeTicks = std::clamp(
        instance.chargeTicks,
        0,
        instance.rechargeTicks
    );
    if (instance.chargeTicks >= instance.rechargeTicks) {
        instance.state = SuperWeaponRuntimeState::Ready;
    } else if (instance.state != SuperWeaponRuntimeState::Unavailable) {
        instance.state = SuperWeaponRuntimeState::Charging;
    }
    instance.stateStartTick = context == nullptr ? lastTick_ : context->tick;
    if (context != nullptr) {
        const SuperWeaponRule* rule = findRule(superWeaponId);
        const std::string voice = rule == nullptr
            ? ""
            : instance.state == SuperWeaponRuntimeState::Ready
                    && previousState != SuperWeaponRuntimeState::Ready
                ? rule->rechargeVoice
                : instance.state == SuperWeaponRuntimeState::Charging
                        && previousState != SuperWeaponRuntimeState::Charging
                    ? rule->chargingVoice
                    : "";
        emit(
            *context,
            instance,
            "SuperWeaponRechargeSet",
            "",
            nullptr,
            nullptr,
            -1,
            -1,
            0,
            "",
            voice
        );
    }
    return true;
}

bool SuperWeaponSystem::disable(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponWorldContext* context
) {
    Instance* instance = findInstance(owner, superWeaponId);
    if (instance == nullptr) {
        return false;
    }
    const std::string key = keyFor(owner, superWeaponId);
    for (Instance& affected : instances_) {
        if (keyFor(affected.owner, affected.definitionId) != key) continue;
        cancelActiveLifecycle(affected, context);
        affected.disabled = true;
        affected.state = SuperWeaponRuntimeState::Unavailable;
    }
    if (context != nullptr) emit(*context, *instance, "SuperWeaponDisabled");
    return true;
}

bool SuperWeaponSystem::remove(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponWorldContext* context
) {
    const Instance* found = findInstance(owner, superWeaponId);
    if (found == nullptr) return false;
    if (context != nullptr) emit(*context, *found, "SuperWeaponRemoved");
    const std::string key = keyFor(owner, superWeaponId);
    for (Instance& affected : instances_) {
        if (keyFor(affected.owner, affected.definitionId) == key)
            cancelActiveLifecycle(affected, context);
    }
    std::erase_if(instances_, [&](const Instance& instance) {
        return keyFor(instance.owner, instance.definitionId) == key;
    });
    return true;
}

bool SuperWeaponSystem::applyTriggerAction(
    const std::string& owner,
    const std::string& superWeaponId,
    const std::string& action,
    const SuperWeaponTarget& target,
    const SuperWeaponWorldContext& context,
    std::string* reason
) {
    const std::string normalized = lowerAscii(trimAscii(action));
    if (normalized == "addonetimesuperweapon") {
        return grantOneShot(owner, superWeaponId, &context);
    }
    if (normalized == "33"
        || normalized == "34"
        || normalized == "grant"
        || normalized == "addrepeatingsuperweapon") {
        return grant(owner, superWeaponId, -1, &context);
    }
    if (normalized == "startcharge" || normalized == "start") {
        return resetCharge(owner, superWeaponId, &context);
    }
    if (normalized == "reset" || normalized == "resetcharge") {
        return resetCharge(owner, superWeaponId, &context);
    }
    if (normalized == "forceready" || normalized == "ready") {
        return forceReady(owner, superWeaponId, &context);
    }
    if (normalized == "disable") {
        return disable(owner, superWeaponId, &context);
    }
    if (normalized == "remove") {
        return remove(owner, superWeaponId, &context);
    }
    if (normalized == "95" || normalized == "102" || normalized == "109"
        || normalized == "fire" || normalized == "fireat" || normalized == "launch") {
        Instance* instance = findInstance(owner, superWeaponId);
        if (instance == nullptr) {
            if (reason != nullptr) *reason = "super weapon is unavailable";
            return false;
        }
        if (instance->state == SuperWeaponRuntimeState::Unavailable
            || instance->state == SuperWeaponRuntimeState::Charging) {
            forceReady(owner, superWeaponId, &context);
        }
        if (!beginTargetSelection(owner, superWeaponId, context, reason)) {
            return false;
        }
        return submitTarget(owner, superWeaponId, target, context, reason);
    }
    if (reason != nullptr) *reason = "unknown trigger action";
    return false;
}

bool SuperWeaponSystem::fireAi(
    const std::string& owner,
    const std::string& superWeaponId,
    const SuperWeaponWorldContext& context,
    std::string* reason
) {
    Instance* instance = findInstance(owner, superWeaponId);
    if (instance == nullptr) {
        if (reason != nullptr) *reason = "super weapon is unavailable";
        return false;
    }
    if (instance->state != SuperWeaponRuntimeState::Ready) {
        if (reason != nullptr) *reason = "super weapon is not ready";
        return false;
    }
    SuperWeaponTarget best;
    int bestScore = std::numeric_limits<int>::min();
    const auto aiCanSee = [this, &context, &owner](
        const WorldObjectInstance& object
    ) {
        if (context.aiTargetVisible) {
            return context.aiTargetVisible(owner, object);
        }
        return !object.cloaked
            || ownerAllied(owner, object.owner, context);
    };
    if (context.objects != nullptr) {
        for (const WorldObjectInstance& object : *context.objects) {
            if (!targetIsAllowed(*instance, object, context)
                || !aiCanSee(object)) {
                continue;
            }
            const int structureBonus = isStructure(object) ? 1000 : 0;
            const int score = structureBonus + std::max(0, object.health);
            if (score > bestScore) {
                bestScore = score;
                best.cell = {object.x, object.y};
                best.objectId = object.instanceId;
            }
        }
    }
    if (bestScore == std::numeric_limits<int>::min()) {
        if (context.mapWidth <= 0 || context.mapHeight <= 0) {
            if (reason != nullptr) *reason = "no legal AI target";
            return false;
        }
        best.cell = {context.mapWidth / 2, context.mapHeight / 2};
    }
    if (!beginTargetSelection(owner, superWeaponId, context, reason)) {
        return false;
    }
    if (instance->targetMode != SuperWeaponTargetMode::TwoStage) {
        return submitTarget(owner, superWeaponId, best, context, reason);
    }

    // Chronosphere-style weapons require a source and destination.  Choose a
    // deterministic empty destination near the strongest enemy formation;
    // falling back to a map-centre spiral guarantees that an AI weapon never
    // gets stuck indefinitely in AwaitingSecondTarget.
    SuperWeaponTarget destination;
    int destinationScore = std::numeric_limits<int>::min();
    if (context.objects != nullptr) {
        for (const WorldObjectInstance& object : *context.objects) {
            if (!object.alive
                || ownerAllied(owner, object.owner, context)
                || !aiCanSee(object)) {
                continue;
            }
            const int score = (isStructure(object) ? 1000 : 0)
                + std::max(0, object.health);
            if (score > destinationScore) {
                destinationScore = score;
                destination.cell = {object.x, object.y};
            }
        }
    }
    if (destinationScore == std::numeric_limits<int>::min()) {
        destination.cell = {
            std::max(0, context.mapWidth / 2),
            std::max(0, context.mapHeight / 2)
        };
    }

    const SuperWeaponCell preferred = destination.cell;
    bool foundDestination = false;
    const int maxRadius = std::max(context.mapWidth, context.mapHeight);
    for (int radius = 0; radius <= maxRadius && !foundDestination; ++radius) {
        for (int dy = -radius; dy <= radius && !foundDestination; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (radius > 0
                    && std::max(std::abs(dx), std::abs(dy)) != radius) {
                    continue;
                }
                const int x = preferred.x + dx;
                const int y = preferred.y + dy;
                if (x < 0 || y < 0
                    || (context.mapWidth > 0 && x >= context.mapWidth)
                    || (context.mapHeight > 0 && y >= context.mapHeight)
                    || (context.cellLegal
                        && !context.cellLegal(
                            x,
                            y,
                            SuperWeaponTargetMode::TwoStage
                        ))
                    || (context.cellOccupied && context.cellOccupied(x, y))) {
                    continue;
                }
                destination.cell = {x, y};
                destination.objectId = -1;
                foundDestination = true;
                break;
            }
        }
    }
    if (!foundDestination) {
        cancelTargetSelection(owner, superWeaponId, context, "no legal AI destination");
        if (reason != nullptr) *reason = "no legal AI destination";
        return false;
    }
    return submitTwoStageTarget(
        owner,
        superWeaponId,
        best,
        destination,
        context,
        reason
    );
}

bool SuperWeaponSystem::fireAiDefense(
    const std::string& defenderOwner,
    const SuperWeaponCell& baseCenter,
    int probabilityPercent,
    int alertFrames,
    int distanceCells,
    const SuperWeaponWorldContext& context,
    std::string* reason
) {
    const auto reject = [&](const std::string& message) {
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };
    if (trimAscii(defenderOwner).empty()) {
        return reject("AI defender owner is empty");
    }
    const int probability = std::clamp(probabilityPercent, 0, 100);
    if (probability <= 0) {
        return reject("AI super defense probability rejected");
    }
    const int alertTicks = convertRuleFrames(
        std::max(0, alertFrames),
        context.fixedTickHz
    );
    const int maximumDistance = std::max(0, distanceCells);

    const Instance* selectedThreat = nullptr;
    const SuperWeaponRule* selectedThreatRule = nullptr;
    int selectedDistance = std::numeric_limits<int>::max();
    for (const Instance& threat : instances_) {
        const SuperWeaponRule* threatRule = findRule(threat.definitionId);
        if (threatRule == nullptr
            || !threatRule->aiDefendAgainst
            || (threat.state != SuperWeaponRuntimeState::Launching
                && threat.state != SuperWeaponRuntimeState::Active)
            || ownerAllied(defenderOwner, threat.owner, context)
            || threat.firstTarget.cell.x < 0
            || threat.firstTarget.cell.y < 0) {
            continue;
        }
        const int age = context.tick - threat.stateStartTick;
        if (age < 0 || age > alertTicks) {
            continue;
        }
        const int distance = cellDistance(baseCenter, threat.firstTarget.cell);
        if (distance > maximumDistance) {
            continue;
        }
        if (selectedThreat == nullptr
            || distance < selectedDistance
            || (distance == selectedDistance
                && threat.instanceId < selectedThreat->instanceId)) {
            selectedThreat = &threat;
            selectedThreatRule = threatRule;
            selectedDistance = distance;
        }
    }
    if (selectedThreat == nullptr) {
        return reject("no hostile super weapon threat in defense window");
    }

    // Roll once for the selected threat.  Checking each threat separately
    // would make the configured probability increase with threat count.
    std::uint64_t roll = 1469598103934665603ULL;
    roll = hashString(roll, lowerAscii(trimAscii(defenderOwner)));
    roll = hashString(roll, lowerAscii(trimAscii(selectedThreat->owner)));
    roll = hashString(
        roll,
        lowerAscii(trimAscii(selectedThreat->definitionId))
    );
    roll = hashString(roll, std::to_string(selectedThreat->instanceId));
    roll = hashString(roll, std::to_string(selectedThreat->launchTick));
    roll = hashString(roll, cellText(selectedThreat->firstTarget.cell));
    if (probability < 100
        && static_cast<int>(roll % 100ULL) >= probability) {
        return reject("AI super defense probability rejected");
    }

    Instance* defense = nullptr;
    const SuperWeaponRule* defenseRule = nullptr;
    for (Instance& candidate : instances_) {
        const SuperWeaponRule* candidateRule = findRule(candidate.definitionId);
        if (candidateRule == nullptr
            || candidateRule->postClick
            || lowerAscii(trimAscii(candidate.owner))
                != lowerAscii(trimAscii(defenderOwner))
            || candidate.state != SuperWeaponRuntimeState::Ready
            || superWeaponTypeKey(*candidateRule).find("forceshield")
                == std::string::npos) {
            continue;
        }
        if (defense == nullptr || candidate.instanceId < defense->instanceId) {
            defense = &candidate;
            defenseRule = candidateRule;
        }
    }
    if (defense == nullptr || defenseRule == nullptr) {
        return reject("no ready Force Shield for AI super defense");
    }

    const WorldObjectInstance* protectedStructure = nullptr;
    int protectedDistance = std::numeric_limits<int>::max();
    if (context.objects != nullptr) {
        for (const WorldObjectInstance& object : *context.objects) {
            if (!object.alive
                || object.health == 0
                || !isStructure(object)
                || !ownerAllied(defenderOwner, object.owner, context)
                || !targetIsAllowed(*defense, object, context, defenseRule)) {
                continue;
            }
            const int distance = cellDistance(
                selectedThreat->firstTarget.cell,
                {object.x, object.y}
            );
            if (protectedStructure == nullptr
                || distance < protectedDistance
                || (distance == protectedDistance
                    && object.instanceId < protectedStructure->instanceId)) {
                protectedStructure = &object;
                protectedDistance = distance;
            }
        }
    }
    if (protectedStructure == nullptr) {
        return reject("no friendly structure for AI super defense");
    }

    const SuperWeaponTarget defenseTarget{
        {protectedStructure->x, protectedStructure->y},
        protectedStructure->instanceId
    };
    // Submission may detach the cast and append/sort instances. Preserve
    // identities before it invalidates pointers into the instance vector.
    const int defenseInstanceId = defense->instanceId;
    const std::string defenseDefinitionId = defense->definitionId;
    const int threatInstanceId = selectedThreat->instanceId;
    const SuperWeaponTarget threatTarget = selectedThreat->firstTarget;
    if (!beginTargetSelection(
            defenderOwner,
            defenseDefinitionId,
            context,
            reason
        )) {
        return false;
    }
    if (!submitTarget(
            defenderOwner,
            defenseDefinitionId,
            defenseTarget,
            context,
            reason
        )) {
        cancelTargetSelection(
            defenderOwner,
            defenseDefinitionId,
            context,
            "AI super defense target rejected"
        );
        return false;
    }
    defense = findInstanceById(defenseInstanceId);
    if (defense == nullptr) {
        return reject("accepted AI defense cast is missing");
    }
    emit(
        context,
        *defense,
        "SuperWeaponAIDefenseTriggered",
        selectedThreatRule == nullptr ? "" : selectedThreatRule->id,
        &defenseTarget,
        &threatTarget,
        threatInstanceId,
        defenseTarget.objectId,
        selectedDistance
    );
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

void SuperWeaponSystem::tick(const SuperWeaponWorldContext& context) {
    lastTick_ = context.tick;
    reconcileSources(context);
    cleanupEffects(context.tick);
    advanceRadiationEffects(context);

    const std::vector<int> instanceIds = [&]() {
        std::vector<int> ids;
        ids.reserve(instances_.size());
        for (const Instance& instance : instances_) {
            ids.push_back(instance.instanceId);
        }
        return ids;
    }();
    for (const int instanceId : instanceIds) {
        Instance* instance = findInstanceById(instanceId);
        if (instance != nullptr) {
            advanceInstance(*instance, context);
            // This also releases a pre-existing active cast when no cooldown
            // is enabled, or after loading an older save with that option.
            instance = findInstanceById(instanceId);
            if (instance != nullptr) detachNoCooldownCast(*instance, context);
        }
    }
    std::erase_if(instances_, [&](const Instance& instance) {
        if (!instance.effectOnly || instance.state != SuperWeaponRuntimeState::Unavailable)
            return false;
        // Nuclear radiation still needs its cast's owner/warhead authority
        // after the missile animation has finished.
        return std::none_of(radiation_.begin(), radiation_.end(), [&](const RadiationEffect& effect) {
            return effect.instanceId == instance.instanceId;
        });
    });
    rebuildSnapshots(context.tick);
}

void SuperWeaponSystem::detachNoCooldownCast(
    Instance& instance, const SuperWeaponWorldContext& context
) {
    if (instance.effectOnly || instance.oneShot || instance.disabled
        || !isActiveState(instance.state) || !context.noCooldown
        || !context.noCooldown(instance.owner)) return;
    const auto* rule = findRule(instance.definitionId);
    bool powered = false;
    if (!rule || (!context.superWeaponsAllowed && rule->disableableFromShell)
        || !instanceHasLiveSource(instance, context, &powered)
        || (rule->isPowered && !powered)) return;

    // Keep the accepted command's ID, targets, lifecycle token, aircraft,
    // radiation and random stream intact. Only the reusable grant is new.
    Instance ready;
    ready.instanceId = nextInstanceId_++;
    ready.owner = instance.owner;
    ready.definitionId = instance.definitionId;
    ready.sourceBuildingIds = instance.sourceBuildingIds;
    ready.targetMode = instance.targetMode;
    ready.powered = powered;
    ready.state = SuperWeaponRuntimeState::Ready;
    ready.stateStartTick = context.tick;
    ready.chargeTicks = ready.rechargeTicks = 1;
    ready.rngState = instance.rngState * 1664525U + 1013904223U;
    instance.effectOnly = true;
    instances_.push_back(std::move(ready));
    emit(context, instances_.back(), "SuperWeaponReady", "", nullptr, nullptr,
        -1, -1, 0, "", rule->rechargeVoice, "");
    std::sort(instances_.begin(), instances_.end(), [](const Instance& left, const Instance& right) {
        const auto leftKey = keyFor(left.owner, left.definitionId);
        const auto rightKey = keyFor(right.owner, right.definitionId);
        return leftKey == rightKey ? left.instanceId < right.instanceId : leftKey < rightKey;
    });
}

void SuperWeaponSystem::advanceInstance(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    // A completed cast may remain solely as the source of residual radiation.
    // It must never enter the reusable grant's charging state machine again.
    if (instance.effectOnly && instance.state == SuperWeaponRuntimeState::Unavailable) return;
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    if (rule == nullptr) {
        cancelActiveLifecycle(instance, &context);
        instance.state = SuperWeaponRuntimeState::Unavailable;
        return;
    }
    if (!rule->preDependent.empty() && !isActiveState(instance.state)) {
        const std::string dependencyKey = lowerAscii(
            trimAscii(rule->preDependent)
        );
        const Instance* dependency = findInstance(
            instance.owner,
            rule->preDependent
        );
        if (dependency == nullptr) {
            for (const SuperWeaponRule& candidateRule : rules_) {
                const std::string candidateKey = lowerAscii(candidateRule.id);
                if (candidateKey == dependencyKey
                    || candidateKey == dependencyKey + "special"
                    || candidateKey.rfind(dependencyKey, 0) == 0) {
                    dependency = findInstance(
                        instance.owner,
                        candidateRule.id
                    );
                    break;
                }
            }
        }
        if (dependency == nullptr
            || dependency->disabled
            || dependency->state == SuperWeaponRuntimeState::Unavailable) {
            if (instance.state != SuperWeaponRuntimeState::Unavailable) {
                emit(
                    context,
                    instance,
                    "SuperWeaponDependencyUnavailable",
                    rule->preDependent
                );
            }
            instance.state = SuperWeaponRuntimeState::Unavailable;
            return;
        }
    }
    instance.rechargeTicks = std::max(
        1,
        static_cast<int>(std::llround(
            rule->rechargeTimeMinutes * 60.0
                * std::max(1, context.fixedTickHz)
        ))
    );

    if (context.noCooldown && context.noCooldown(instance.owner)) {
        instance.rechargeTicks = 1;
        instance.chargeTicks = std::min(instance.chargeTicks, instance.rechargeTicks);
    }
    bool powered = false;
    const bool hasSource = instanceHasLiveSource(instance, context, &powered);
    const bool wasPowered = instance.powered;
    instance.powered = powered;
    const bool disabledByShell = !context.superWeaponsAllowed
        && rule->disableableFromShell;

    if (!hasSource) instance.chargeTicks = 0;
    if (!hasSource && !isActiveState(instance.state)) {
        instance.chargeTicks = 0;
        instance.targetStage = 0;
        if (instance.state != SuperWeaponRuntimeState::Unavailable) {
            emit(context, instance, "SuperWeaponSourceLost");
        }
        instance.state = SuperWeaponRuntimeState::Unavailable;
        return;
    }
    if (instance.disabled || disabledByShell) {
        cancelActiveLifecycle(instance, &context);
        instance.state = SuperWeaponRuntimeState::Unavailable;
        return;
    }

    // The effect owns its launch/impact/aircraft lifecycle, but recharging
    // starts at command acceptance and runs concurrently with that lifecycle.
    if (isActiveState(instance.state) && !instance.effectOnly && !instance.oneShot && hasSource) {
        if (!rule->isPowered || powered) {
            instance.chargeTicks = std::min(instance.rechargeTicks, instance.chargeTicks + 1);
        }
        if (rule->isPowered && wasPowered != powered) {
            emit(context, instance, powered ? "SuperWeaponChargeResumed" : "SuperWeaponChargeSuspended",
                powered ? "" : "insufficient power", nullptr, nullptr, -1, -1, 0, "",
                powered ? rule->chargingVoice : rule->suspendVoice);
        }
    }

    if (instance.state == SuperWeaponRuntimeState::Unavailable) {
        instance.state = powered || !rule->isPowered
            ? SuperWeaponRuntimeState::Charging
            : SuperWeaponRuntimeState::Suspended;
        instance.stateStartTick = context.tick;
        emit(
            context,
            instance,
            "SuperWeaponSourceAvailable",
            "",
            nullptr,
            nullptr,
            -1,
            -1,
            0,
            "",
            instance.state == SuperWeaponRuntimeState::Charging
                ? rule->chargingVoice
                : ""
        );
        if (instance.state == SuperWeaponRuntimeState::Suspended) {
            emit(
                context,
                instance,
                "SuperWeaponChargeSuspended",
                "insufficient power",
                nullptr,
                nullptr,
                -1,
                -1,
                0,
                "",
                rule->suspendVoice
            );
        }
    }

    if (instance.state == SuperWeaponRuntimeState::Charging) {
        if (rule->isPowered && !powered) {
            instance.state = SuperWeaponRuntimeState::Suspended;
            instance.stateStartTick = context.tick;
            emit(
                context,
                instance,
                "SuperWeaponChargeSuspended",
                "insufficient power",
                nullptr,
                nullptr,
                -1,
                -1,
                0,
                "",
                rule->suspendVoice
            );
            return;
        }
        instance.chargeTicks = std::min(
            instance.rechargeTicks,
            instance.chargeTicks + 1
        );
        if (instance.chargeTicks >= instance.rechargeTicks) {
            instance.state = SuperWeaponRuntimeState::Ready;
            instance.stateStartTick = context.tick;
            emit(
                context,
                instance,
                "SuperWeaponReady",
                "",
                nullptr,
                nullptr,
                -1,
                -1,
                0,
                "",
                rule->rechargeVoice,
                ""
            );
        }
        return;
    }
    if (instance.state == SuperWeaponRuntimeState::Suspended) {
        if (!rule->isPowered || powered) {
            instance.state = SuperWeaponRuntimeState::Charging;
            instance.stateStartTick = context.tick;
            emit(
                context,
                instance,
                "SuperWeaponChargeResumed",
                "",
                nullptr,
                nullptr,
                -1,
                -1,
                0,
                "",
                rule->chargingVoice
            );
        }
        return;
    }
    if (instance.state == SuperWeaponRuntimeState::Launching) {
        if (context.tick >= instance.launchTick) {
            launchInstance(instance, context);
        }
        return;
    }
    if (instance.state == SuperWeaponRuntimeState::Active) {
        const std::string type = lowerAscii(rule->type + " " + rule->action);
        if (type.find("forceshield") != std::string::npos && !instance.effectApplied
            && rule->fadeSoundFrames > 0 && rule->fadeSoundFrames < rule->durationFrames
            && context.tick >= instance.activeUntilTick
                - convertRuleFrames(rule->fadeSoundFrames, context.fixedTickHz)) {
            instance.effectApplied = true;
            emit(context, instance, "ForceShieldFading", "protection expiring",
                &instance.firstTarget, nullptr, -1, -1, 0, rule->specialSound);
        }
        if ((type.find("multimissile") != std::string::npos
                || type.find("nuke") != std::string::npos)
            && !instance.effectApplied && instance.nextEffectTick >= 0
            && context.tick >= instance.nextEffectTick) {
            instance.effectApplied = true;
            emitImpact(context, instance, "NukePreImpact", instance.firstTarget.cell,
                0, 0, "", rule->preImpactAnim);
        }
        if ((type.find("multimissile") != std::string::npos
                || type.find("nuke") != std::string::npos)
            && instance.nextImpactTick >= 0
            && context.tick >= instance.nextImpactTick) {
            applyNuke(instance, context);
            return;
        }
        if (type.find("lightning") != std::string::npos) {
            advanceStorm(instance, context);
        } else if (isDominatorLifecycle(*rule) && !instance.effectApplied
            && instance.nextImpactTick >= 0 && context.tick >= instance.nextImpactTick) {
            applyDominator(instance, context);
        } else if (type.find("para") != std::string::npos) {
            advanceSupportAircraft(instance, context, false);
        } else if (type.find("spy") != std::string::npos) {
            advanceSupportAircraft(instance, context, true);
        }
        if (instance.activeUntilTick >= 0
            && context.tick >= instance.activeUntilTick) {
            if (isLightningLifecycle(*rule)) {
                emit(context, instance, "LightningStormEnded");
                endActiveLifecycle(instance, context);
            } else if (isDominatorLifecycle(*rule)) {
                emit(context, instance, "PsychicDominatorEnded");
                endActiveLifecycle(instance, context);
            } else {
                emit(context, instance, "SuperWeaponEffectEnded");
            }
            instance.state = SuperWeaponRuntimeState::Cooldown;
            instance.cooldownUntilTick = context.tick + 1;
        }
        return;
    }
    if (instance.state == SuperWeaponRuntimeState::Cooldown
        && context.tick >= instance.cooldownUntilTick) {
        finishCooldown(instance, context);
    }
}

void SuperWeaponSystem::finishCooldown(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    instance.targetStage = 0;
    instance.launchTick = -1;
    instance.activeToken = -1;
    instance.activeStartedTick = -1;
    instance.activeUntilTick = -1;
    instance.cooldownUntilTick = -1;
    instance.nextEffectTick = -1;
    instance.nextImpactTick = -1;
    instance.effectApplied = false;
    instance.activeObjectIds.clear();
    instance.stormClouds.clear();
    if (instance.oneShot) {
        instance.sourceBuildingIds.erase(
            std::remove_if(
                instance.sourceBuildingIds.begin(),
                instance.sourceBuildingIds.end(),
                [](int sourceId) { return sourceId < 0; }
            ),
            instance.sourceBuildingIds.end()
        );
        instance.oneShot = false;
    }
    if (instance.effectOnly) {
        instance.state = SuperWeaponRuntimeState::Unavailable;
        instance.stateStartTick = context.tick;
        emit(context, instance, "SuperWeaponCooldownEnded");
        return;
    }
    bool powered = false;
    if (instanceHasLiveSource(instance, context, &powered)) {
        instance.powered = powered;
        instance.state = instance.powered || findRule(instance.definitionId)->isPowered == false
            ? (instance.chargeTicks >= instance.rechargeTicks
                ? SuperWeaponRuntimeState::Ready : SuperWeaponRuntimeState::Charging)
            : SuperWeaponRuntimeState::Suspended;
    } else {
        instance.state = SuperWeaponRuntimeState::Unavailable;
    }
    instance.stateStartTick = context.tick;
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    emit(
        context,
        instance,
        "SuperWeaponCooldownEnded",
        "",
        nullptr,
        nullptr,
        -1,
        -1,
        0,
        "",
        ""
    );
    if (instance.state == SuperWeaponRuntimeState::Ready && rule != nullptr) {
        emit(context, instance, "SuperWeaponReady", "", nullptr, nullptr,
            -1, -1, 0, "", rule->rechargeVoice, "");
    }
}

void SuperWeaponSystem::launchInstance(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    if (rule == nullptr) {
        instance.state = SuperWeaponRuntimeState::Cooldown;
        instance.cooldownUntilTick = context.tick + 1;
        return;
    }
    instance.state = SuperWeaponRuntimeState::Active;
    instance.stateStartTick = context.tick;
    instance.effectApplied = false;
    const std::string type = lowerAscii(rule->type + " " + rule->action);
    emit(
        context,
        instance,
        "SuperWeaponLaunched",
        "",
        &instance.firstTarget,
        instance.targetMode == SuperWeaponTargetMode::TwoStage
            ? &instance.secondTarget
            : nullptr,
        -1,
        instance.firstTarget.objectId,
        0,
        type.find("spy") != std::string::npos || type.find("lightning") != std::string::npos
            ? "" : rule->startSound,
        "",
        rule->launchAnim.empty() ? rule->invokeAnim : rule->launchAnim
    );

    if (type.find("multimissile") != std::string::npos
        || type.find("nuke") != std::string::npos) {
        instance.nextImpactTick = context.tick
            + std::max(1, convertRuleFrames(45, context.fixedTickHz));
        instance.activeUntilTick = instance.nextImpactTick;
        instance.nuclearLaunchOrigin = {};
        instance.nuclearLaunchLevel = 0;
        for (const int sourceId : instance.sourceBuildingIds) {
            const auto* source = findObject(sourceId, context);
            if (source == nullptr || !source->alive) continue;
            instance.nuclearLaunchOrigin.objectId = sourceId;
            instance.nuclearLaunchOrigin.cell = context.nuclearLaunchCell
                ? context.nuclearLaunchCell(*source)
                : SuperWeaponCell{source->x, source->y};
            instance.nuclearLaunchLevel = source->level;
            break;
        }
        if (rule->nuclearAltitudeLeptons > 0) {
            instance.nextEffectTick = context.tick + convertRuleFrames(
                superWeaponNuclearFlightFrames(*rule, instance.nuclearLaunchOrigin.objectId >= 0),
                context.fixedTickHz);
            const int buildup = context.animationDurationTicks
                ? context.animationDurationTicks(rule->preImpactAnim) : 0;
            instance.nextImpactTick = instance.nextEffectTick + std::max(1, buildup);
            instance.activeUntilTick = instance.nextImpactTick;
        }
        emit(
            context,
            instance,
            "NukeProjectileLaunched",
            "delayed payload",
            &instance.firstTarget,
            nullptr,
            -1,
            -1,
            0,
            "",
            "",
            rule->launchAnim,
            instance.nuclearLaunchOrigin.objectId
        );
        return;
    }
    if (type.find("lightning") != std::string::npos) {
        instance.nextEffectTick = context.tick
            + std::max(1, convertRuleFrames(rule->defermentFrames, context.fixedTickHz));
        instance.nextImpactTick = instance.nextEffectTick
            + std::max(1, convertRuleFrames(rule->durationFrames, context.fixedTickHz));
        int cloudDuration = 0;
        for (const auto& id : splitValues(rule->cloudAnim)) {
            if (context.animationDurationTicks)
                cloudDuration = std::max(cloudDuration, context.animationDurationTicks(id));
        }
        if (cloudDuration <= 0) cloudDuration = std::max(2, convertRuleFrames(30, context.fixedTickHz));
        instance.activeUntilTick = instance.nextImpactTick + cloudDuration;
        beginActiveLifecycle(instance, context);
        emit(
            context,
            instance,
            "LightningStormStarted",
            "deferment",
            &instance.firstTarget,
            nullptr,
            -1,
            -1,
            rule->defermentFrames,
            "",
            "",
            rule->cloudAnim
        );
        return;
    }
    if (type.find("ironcurtain") != std::string::npos) {
        applyProtection(instance, context, false);
        instance.activeUntilTick = context.tick
            + std::max(1, convertRuleFrames(rule->durationFrames, context.fixedTickHz));
        return;
    }
    if (type.find("forceshield") != std::string::npos) {
        applyProtection(instance, context, true);
        instance.activeUntilTick = context.tick
            + std::max(1, convertRuleFrames(rule->durationFrames, context.fixedTickHz));
        return;
    }
    if (type.find("chrono") != std::string::npos) {
        applyChrono(instance, context);
        instance.state = SuperWeaponRuntimeState::Cooldown;
        instance.cooldownUntilTick = context.tick + 1;
        return;
    }
    if (type.find("psychicdominator") != std::string::npos
        || type.find("dominator") != std::string::npos) {
        const int authoredDuration = context.animationDurationTicks
            ? context.animationDurationTicks(rule->firstAnim) : 0;
        const int firstDuration = authoredDuration > 0 ? authoredDuration
            : std::max(1, convertRuleFrames(rule->durationFrames, context.fixedTickHz));
        const int fireDelay = static_cast<int>((static_cast<std::int64_t>(firstDuration)
            * std::clamp(rule->fireAtPercentage, 0, 100) + 99) / 100);
        instance.nextImpactTick = context.tick + fireDelay;
        instance.activeUntilTick = context.tick + std::max(firstDuration,
            std::max(1, convertRuleFrames(rule->durationFrames, context.fixedTickHz)));
        beginActiveLifecycle(instance, context);
        emit(
            context,
            instance,
            "PsychicDominatorStarted",
            "active",
            &instance.firstTarget,
            nullptr,
            -1,
            -1,
            instance.activeUntilTick - instance.activeStartedTick,
            "",
            "",
            rule->firstAnim
        );
        if (fireDelay == 0) applyDominator(instance, context);
        return;
    }
    if (type.find("genetic") != std::string::npos
        || type.find("mutator") != std::string::npos) {
        applyMutator(instance, context);
        instance.state = SuperWeaponRuntimeState::Cooldown;
        instance.cooldownUntilTick = context.tick + 1;
        return;
    }
    if (type.find("para") != std::string::npos) {
        launchParadrop(instance, context);
        return;
    }
    if (type.find("spy") != std::string::npos
        || type.find("reveal") != std::string::npos) {
        launchReveal(instance, context);
        return;
    }
    emit(context, instance, "SuperWeaponEffectApplied", "generic rule type");
    instance.state = SuperWeaponRuntimeState::Cooldown;
    instance.cooldownUntilTick = context.tick + 1;
}

void SuperWeaponSystem::applyRadialDamage(
    const Instance& instance,
    const SuperWeaponWorldContext& context,
    const SuperWeaponCell& cell,
    int radius,
    int damage,
    const std::string& warheadId,
    const std::string& eventKind
) {
    if (context.objects == nullptr) {
        return;
    }
    const WarheadType* warhead = context.rules == nullptr
        ? nullptr
        : context.rules->findWarheadType(warheadId);
    const int effectiveRadius = std::max(
        0,
        radius > 0
            ? radius
            : (warhead == nullptr ? 0 : static_cast<int>(std::ceil(warhead->cellSpread)))
    );
    for (WorldObjectInstance& object : *context.objects) {
        if (!object.alive || object.health == 0 || !superWeaponExposed(object, context)) {
            continue;
        }
        if (superWeaponCellSpreadDistance(object.x - cell.x, object.y - cell.y) > effectiveRadius) {
            continue;
        }
        if (context.damageImmune && context.damageImmune(object)) continue;
        if (context.rules) {
            runtimeInitializeModExtensions(object, *context.rules, context.fixedTickHz);
            if (warhead) runtimeAttachEffect(object, *context.rules, warhead->id, true,
                context.fixedTickHz, isObjectProtected(object.instanceId, context.tick));
        }
        // Superweapon blast and residual radiation damage every exposed
        // house in their area. Alliance restrictions belong to targeting /
        // control; armor, range, immunity and active protection still apply.
        if (objectImmune(object, "radiation", context)
            && (eventKind.find("Radiation") != std::string::npos)) {
            emit(
                context,
                instance,
                "SuperWeaponDamageImmune",
                "radiation immunity",
                &instance.firstTarget,
                nullptr,
                object.instanceId,
                object.instanceId
            );
            continue;
        }
        if (isObjectProtected(object.instanceId, context.tick)) {
            emit(
                context,
                instance,
                "SuperWeaponDamageBlocked",
                "active protection",
                &instance.firstTarget,
                nullptr,
                object.instanceId,
                object.instanceId
            );
            continue;
        }
        const double verses = superWeaponVerses(object, context, warhead);
        if (verses <= 0.0) continue;
        const double distance = std::hypot(double(object.x-cell.x), double(object.y-cell.y));
        const double fraction = std::clamp(distance / std::max(1.0, double(effectiveRadius)), 0.0, 1.0);
        const double falloff = eventKind == "RadiationDamage"
            ? std::max(0.0, 1.0 - distance / std::max(1.0, double(effectiveRadius))) : warhead
            ? 1.0 + (warhead->percentAtMax - 1.0) * fraction
            : std::max(1.0, effectiveRadius + 1.0 - cellDistance(cell, {object.x, object.y})) / std::max(1, effectiveRadius + 1);
        if (falloff <= 0.0) continue;
        const auto* shield = context.rules ? runtimeActiveShield(object, *context.rules, warhead) : nullptr;
        const double armorBonus = veteranArmorMultiplier(object, context.rules)
            * std::max(0.01, object.crateArmorMultiplier)
            * (context.rules ? runtimeAttachMultiplier(object, *context.rules, &AttachEffectRule::armor) : 1.0);
        const auto amount = [&](double ratio, double bonus) {
            if (ratio <= 0.0) return 0;
            return static_cast<int>(std::clamp(std::round(damage * std::max(0.0, falloff) * ratio / bonus), 1.0, double(INT_MAX)));
        };
        int scaledDamage = amount(verses, shield && !shield->applyArmorMult ? 1.0 : armorBonus);
        if (shield) scaledDamage = runtimeAbsorbShieldDamage(object, *context.rules, warhead, scaledDamage,
            amount(superWeaponVerses(object, context, warhead, true), armorBonus), context.fixedTickHz);
        const int healthBeforeDamage = object.health;
        object.health = std::max(0, object.health - scaledDamage);
        if (object.health < healthBeforeDamage && context.onObjectDamaged) {
            SuperWeaponObjectLifecycleEvent event = lifecycleEvent(
                SuperWeaponObjectLifecycleKind::Damaged,
                instance,
                context.tick,
                object.instanceId
            );
            event.healthBefore = healthBeforeDamage;
            event.healthAfter = object.health;
            event.warheadId = warheadId;
            event.reason = "superweapon radial damage";
            context.onObjectDamaged(std::move(event));
        }
        if (object.health == 0) {
            object.alive = false;
            if (context.onObjectRemoved) {
                SuperWeaponObjectLifecycleEvent event = lifecycleEvent(
                    SuperWeaponObjectLifecycleKind::Removed,
                    instance,
                    context.tick,
                    object.instanceId
                );
                event.healthBefore = healthBeforeDamage;
                event.healthAfter = object.health;
                event.warheadId = warheadId;
                event.reason = "superweapon radial damage";
                event.removalDisposition =
                    SuperWeaponObjectRemovalDisposition::Destroyed;
                context.onObjectRemoved(std::move(event));
            }
        }
        emit(
            context,
            instance,
            eventKind,
            "",
            &instance.firstTarget,
            nullptr,
            object.instanceId,
            object.instanceId,
            scaledDamage
        );
    }
}

void SuperWeaponSystem::applyNuke(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    if (rule == nullptr) {
        return;
    }
    const WeaponType* payload = context.rules == nullptr
        ? nullptr
        : context.rules->findWeaponType(rule->payloadWeapon);
    const std::string warheadId = !rule->payloadWarhead.empty()
        ? rule->payloadWarhead
        : payload == nullptr ? rule->warhead : payload->warhead;
    const int damage = rule->damage > 0
        ? rule->damage
        : payload == nullptr ? 600 : std::max(1, payload->damage);
    int radius = rule->radius;
    if (radius <= 0 && context.rules != nullptr) {
        const WarheadType* warhead = context.rules->findWarheadType(warheadId);
        if (warhead != nullptr) {
            radius = static_cast<int>(std::ceil(warhead->cellSpread));
        }
    }
    radius = std::max(1, radius);
    emitImpact(
        context,
        instance,
        "NukeImpact",
        instance.firstTarget.cell,
        radius,
        damage,
        warheadId,
        rule->impactAnim
    );
    applyRadialDamage(
        instance,
        context,
        instance.firstTarget.cell,
        radius,
        damage,
        warheadId,
        "NukeDamage"
    );
    RadiationEffect radiation;
    radiation.owner = instance.owner;
    radiation.instanceId = instance.instanceId;
    radiation.cell = instance.firstTarget.cell;
    radiation.radius = radius;
    const SpecialMechanismRules* mechanisms = context.rules == nullptr
        ? nullptr
        : &context.rules->getSpecialMechanismRules();
    radiation.intensity = std::max(
        1,
        mechanisms == nullptr
            ? damage
            : std::min(
                std::max(1, mechanisms->radiationLevelMax),
                payload ? std::max(0, payload->radLevel) : damage
            )
    );
    const int durationFrames = radiation.intensity * std::max(
        1,
        mechanisms == nullptr ? 1 : mechanisms->radiationDurationMultiple
    );
    radiation.untilTick = context.tick + std::max(
        1,
        convertRuleFrames(durationFrames, context.fixedTickHz)
    );
    radiation.nextDamageTick = context.tick + 1;
    if (!payload || payload->radLevel > 0) radiation_.push_back(std::move(radiation));
    emit(
        context,
        instance,
        "NukeImpact",
        "payload detonated",
        &instance.firstTarget,
        nullptr,
        -1,
        -1,
        damage,
        "",
        "",
        rule->impactAnim
    );
    instance.state = SuperWeaponRuntimeState::Cooldown;
    instance.cooldownUntilTick = context.tick + 1;
}

#include "simulation/superweapons/SuperWeaponStorm.inl"

void SuperWeaponSystem::applyProtection(
    Instance& instance,
    const SuperWeaponWorldContext& context,
    bool forceShield
) {
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    if (rule == nullptr || context.objects == nullptr) {
        return;
    }
    const SuperWeaponCell center = instance.firstTarget.cell;
    const int radius = std::max(
        1,
        rule->radius > 0
            ? rule->radius
            : static_cast<int>(std::ceil(std::max(1.0, rule->range)))
    );
    const int duration = std::max(
        1,
        convertRuleFrames(rule->durationFrames, context.fixedTickHz)
    );
    for (WorldObjectInstance& object : *context.objects) {
        const auto* organicType = superWeaponUnitType(object, context.rules);
        const bool organicDamage = !forceShield
            && (isInfantry(object) || (organicType && organicType->organic));
        if (!object.alive || object.health == 0 || object.airborne
            || !superWeaponExposed(object, context)
            || (forceShield ? superWeaponCellSpreadDistance(object.x-center.x, object.y-center.y)
                : cellDistance(center, {object.x, object.y})) > radius
            || (forceShield && !ownerAllied(instance.owner, object.owner, context))
            || (forceShield && !isStructure(object))) {
            continue;
        }
        if (organicDamage) {
            // The original Iron Curtain does not make organic infantry
            // invulnerable.  Keep this branch explicit and replay-visible.
            const int healthBeforeDamage = object.health;
            object.health = 0;
            object.alive = false;
            if (healthBeforeDamage > 0 && context.onObjectDamaged) {
                SuperWeaponObjectLifecycleEvent event = lifecycleEvent(
                    SuperWeaponObjectLifecycleKind::Damaged,
                    instance,
                    context.tick,
                    object.instanceId
                );
                event.healthBefore = healthBeforeDamage;
                event.healthAfter = 0;
                event.warheadId = rule->warhead;
                event.reason = "iron curtain organic destruction";
                context.onObjectDamaged(std::move(event));
            }
            if (context.onObjectRemoved) {
                SuperWeaponObjectLifecycleEvent event = lifecycleEvent(
                    SuperWeaponObjectLifecycleKind::Removed,
                    instance,
                    context.tick,
                    object.instanceId
                );
                event.healthBefore = healthBeforeDamage;
                event.healthAfter = 0;
                event.warheadId = rule->warhead;
                event.reason = "iron curtain organic destruction";
                event.removalDisposition =
                    SuperWeaponObjectRemovalDisposition::Destroyed;
                context.onObjectRemoved(std::move(event));
            }
            emit(
                context,
                instance,
                "IronCurtainOrganicDestroyed",
                "organic target",
                &instance.firstTarget,
                nullptr,
                object.instanceId,
                object.instanceId,
                0,
                "",
                "",
                rule->invokeAnim
            );
            continue;
        }
        protections_.push_back({
            object.instanceId,
            context.tick + duration,
            forceShield
        });
        emit(
            context,
            instance,
            forceShield ? "ForceShieldProtected" : "IronCurtainProtected",
            "",
            &instance.firstTarget,
            nullptr,
            object.instanceId,
            object.instanceId,
            duration,
            "",
            "",
            rule->invokeAnim
        );
    }
    if (forceShield) {
        OwnerLockout& lockout = ownerLockouts_[lowerAscii(instance.owner)];
        lockout.blackoutUntil = context.tick
            + std::max(
                duration,
                convertRuleFrames(rule->blackoutDurationFrames, context.fixedTickHz)
            );
        lockout.penaltyUntil = lockout.blackoutUntil;
        lockout.penalty = rule->powerPenalty;
        emit(
            context,
            instance,
            "ForceShieldBlackoutStarted",
            "power blackout",
            &instance.firstTarget,
            nullptr,
            -1,
            -1,
            lockout.blackoutUntil,
            "",
            "",
            rule->invokeAnim
        );
    }
}

void SuperWeaponSystem::applyChrono(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    if (rule == nullptr || context.objects == nullptr) {
        return;
    }
    // The adapter borrows only this synchronous invocation. Lifecycle payloads
    // remain the original owned events queued by the host's existing callback.
    struct RelocationPort final : RuntimeChronosphereRelocationPort {
        const Instance& instance;
        const SuperWeaponWorldContext& context;
        RelocationPort(const Instance& i, const SuperWeaponWorldContext& c)
            : instance(i), context(c) {}
        bool hasTerrainLevel() const noexcept override {
            return static_cast<bool>(context.terrainLevel);
        }
        int terrainLevel(int x, int y) override { return context.terrainLevel(x, y); }
        void onRelocated(const RuntimeChronosphereRelocationResult& result) override {
            if (!context.onObjectRelocated) return;
            SuperWeaponObjectLifecycleEvent event = SuperWeaponSystem::lifecycleEvent(
                SuperWeaponObjectLifecycleKind::Relocated, instance, context.tick,
                result.request.objectId);
            event.healthBefore = event.healthAfter = result.healthAtNotification;
            event.oldCell = {result.request.oldCellX, result.request.oldCellY};
            event.newCell = {result.request.targetX, result.request.targetY};
            event.reason = result.request.arrival == RuntimeChronosphereArrival::FatalWater
                ? "chronosphere fatal water transfer" : "chronosphere formation transfer";
            context.onObjectRelocated(std::move(event));
        }
    } relocationPort(instance, context);
    const SuperWeaponCell source = instance.firstTarget.cell;
    const SuperWeaponCell destination = instance.secondTarget.cell;
    const int radius = std::max(
        1,
        rule->radius > 0
            ? rule->radius
            : static_cast<int>(std::ceil(std::max(1.0, rule->range)))
    );
    std::vector<int> affected;
    for (const WorldObjectInstance& object : *context.objects) {
        if (object.alive && object.health != 0
            && isUnit(object)
            && cellDistance(source, {object.x, object.y}) <= radius
            && !object.airborne && object.tankBunkerHostId < 0
            && !isObjectProtected(object.instanceId, context.tick)
            && superWeaponExposed(object, context)
            && targetIsAllowed(instance, object, context)) {
            affected.push_back(object.instanceId);
        }
    }
    std::sort(affected.begin(), affected.end());
    for (const int objectId : affected) {
        WorldObjectInstance* object = findObject(objectId, context);
        if (object == nullptr || !object->alive) {
            continue;
        }
        const SuperWeaponCell oldCell{object->x, object->y};
        const SuperWeaponCell offset{
            object->x - source.x,
            object->y - source.y
        };
        const SuperWeaponCell target{
            destination.x + offset.x,
            destination.y + offset.y
        };
        const bool legal = target.x >= 0 && target.y >= 0
            && (context.mapWidth <= 0 || target.x < context.mapWidth)
            && (context.mapHeight <= 0 || target.y < context.mapHeight)
            && (!context.cellLegal
                || context.cellLegal(target.x, target.y, SuperWeaponTargetMode::Cell));
        bool occupied = context.canPlaceObject
            ? !context.canPlaceObject(*object, target.x, target.y)
            : context.cellOccupied && context.cellOccupied(target.x, target.y);
        if (occupied && target == source) {
            occupied = false;
        }
        const auto* unitType = superWeaponUnitType(*object, context.rules);
        const bool organic = unitType && unitType->organic && !unitType->teleporter;
        const bool fatalWater = legal && !organic
            && context.chronoDestinationIsFatalWater
            && context.chronoDestinationIsFatalWater(*object, target.x, target.y);
        if (fatalWater) {
            // Water-domain failure is a real arrival. Expose the destination
            // while still alive so host relocation and death presentation join
            // the transfer instead of destroying the unit at its source.
            (void)commitRuntimeChronosphereRelocation(object,
                {objectId, oldCell.x, oldCell.y, target.x, target.y,
                    superWeaponObjectCellKey(target.x, target.y),
                    RuntimeChronosphereArrival::FatalWater}, relocationPort);
        }
        if (!legal || occupied || organic || fatalWater) {
            const int healthBeforeRemoval = object->health;
            object->alive = false;
            object->health = 0;
            if (context.onObjectRemoved) {
                SuperWeaponObjectLifecycleEvent event = lifecycleEvent(
                    SuperWeaponObjectLifecycleKind::Removed,
                    instance,
                    context.tick,
                    objectId
                );
                event.healthBefore = healthBeforeRemoval;
                event.healthAfter = 0;
                event.oldCell = oldCell;
                event.newCell = target;
                event.reason = organic ? "chronosphere organic target" : !legal
                    ? "chronosphere illegal destination"
                    : fatalWater ? "chronosphere fatal water destination"
                    : "chronosphere blocked destination";
                event.removalDisposition =
                    SuperWeaponObjectRemovalDisposition::Destroyed;
                context.onObjectRemoved(std::move(event));
            }
            emit(
                context,
                instance,
                "ChronoWarpAway",
                organic ? "organic target" : !legal ? "illegal destination"
                    : fatalWater ? "fatal water destination" : "blocked destination",
                &instance.firstTarget,
                &instance.secondTarget,
                objectId,
                objectId,
                0,
                "",
                "",
                rule->launchAnim
            );
            continue;
        }
        (void)commitRuntimeChronosphereRelocation(object,
            {objectId, oldCell.x, oldCell.y, target.x, target.y,
                superWeaponObjectCellKey(target.x, target.y),
                RuntimeChronosphereArrival::Formation}, relocationPort);
        emit(
            context,
            instance,
            "ChronoWarpIn",
            "formation preserved",
            &instance.firstTarget,
            &instance.secondTarget,
            objectId,
            objectId,
            0,
            "", // WarpIn is an animation; its AnimType owns any sound.
            "",
            rule->impactAnim
        );
    }
    emit(
        context,
        instance,
        "ChronoSphereApplied",
        "two-stage formation transfer",
        &instance.firstTarget,
        &instance.secondTarget,
        -1,
        -1,
        static_cast<int>(affected.size()),
        "",
        "",
        rule->firstAnim + ";" + rule->secondAnim
    );
}

void SuperWeaponSystem::applyDominator(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    if (rule == nullptr) {
        return;
    }
    instance.effectApplied = true;
    instance.nextImpactTick = -1;
    const int radius = std::max(
        1,
        rule->captureRange > 0
            ? rule->captureRange
            : static_cast<int>(std::ceil(std::max(1.0, rule->range)))
    );
    const auto* damageWarhead = context.rules
        ? context.rules->findWarheadType(rule->payloadWarhead) : nullptr;
    if (damageWarhead) {
        applyRadialDamage(instance, context, instance.firstTarget.cell,
            static_cast<int>(std::ceil(damageWarhead->cellSpread)), rule->damage,
            rule->payloadWarhead, "PsychicDominatorDamage");
    }
    emitImpact(
        context,
        instance,
        "PsychicDominatorImpact",
        instance.firstTarget.cell,
        radius,
        std::max(0, rule->damage),
        rule->payloadWarhead.empty() ? rule->warhead : rule->payloadWarhead,
        rule->impactAnim.empty() ? rule->firstAnim : rule->impactAnim
    );
    if (context.objects == nullptr) {
        return;
    }
    for (WorldObjectInstance& object : *context.objects) {
        if (!object.alive || object.health == 0 || object.airborne
            || !superWeaponExposed(object, context)
            || isObjectProtected(object.instanceId, context.tick)
            || superWeaponCellSpreadDistance(object.x-instance.firstTarget.cell.x,
                object.y-instance.firstTarget.cell.y) > radius
            || !targetIsAllowed(instance, object, context)
            || objectImmune(object, "psionics", context)) {
            continue;
        }
        const std::string oldOwner = object.owner;
        if (rule->damage > 0 && !damageWarhead) {
            const int healthBeforeDamage = object.health;
            const int appliedDamage = std::max(
                1,
                static_cast<int>(std::lround(
                    static_cast<double>(rule->damage)
                        / veteranArmorMultiplier(object, context.rules)
                ))
            );
            object.health = std::max(1, object.health - appliedDamage);
            if (object.health < healthBeforeDamage
                && context.onObjectDamaged) {
                SuperWeaponObjectLifecycleEvent event = lifecycleEvent(
                    SuperWeaponObjectLifecycleKind::Damaged,
                    instance,
                    context.tick,
                    object.instanceId
                );
                event.healthBefore = healthBeforeDamage;
                event.healthAfter = object.health;
                event.warheadId = rule->warhead;
                event.reason = "psychic dominator damage";
                context.onObjectDamaged(std::move(event));
            }
        }
        object.owner = instance.owner;
        object.mission = "Guard";
        if (context.onObjectOwnerChanged) {
            SuperWeaponObjectLifecycleEvent event = lifecycleEvent(
                SuperWeaponObjectLifecycleKind::OwnerChanged,
                instance,
                context.tick,
                object.instanceId
            );
            event.healthBefore = object.health;
            event.healthAfter = object.health;
            event.oldCell = {object.x, object.y};
            event.newCell = event.oldCell;
            event.oldOwner = oldOwner;
            event.newOwner = object.owner;
            event.warheadId = rule->warhead;
            event.reason = "psychic dominator control";
            context.onObjectOwnerChanged(std::move(event));
        }
        emit(
            context,
            instance,
            "PsychicDominatorControlled",
            "permanent control",
            &instance.firstTarget,
            nullptr,
            object.instanceId,
            object.instanceId,
            rule->damage
        );
    }
}

void SuperWeaponSystem::applyMutator(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    if (rule == nullptr || context.objects == nullptr) {
        return;
    }
    const int radius = std::max(
        1,
        rule->radius > 0
            ? rule->radius
            : static_cast<int>(std::ceil(std::max(1.0, rule->range)))
    );
    const std::string mutationType = trimAscii(rule->mutationType);
    std::vector<int> affected;
    for (const WorldObjectInstance& object : *context.objects) {
        if (!object.alive || object.health == 0 || !isInfantry(object)
            || !superWeaponExposed(object, context)
            || isObjectProtected(object.instanceId, context.tick)
            || object.airborne
            || (rule->mutateExplosion
                ? superWeaponCellSpreadDistance(object.x-instance.firstTarget.cell.x,
                    object.y-instance.firstTarget.cell.y)
                : cellDistance(instance.firstTarget.cell, {object.x, object.y})) > radius
            || (context.damageImmune && context.damageImmune(object))) {
            continue;
        }
        // The stock effect is not owner-polarized: Yuri may turn friendly,
        // hostile, allied, or neutral human infantry into his own Brutes.
        // Type data is required so NotHuman/Organic are never guessed from an
        // object id or sprite name.
        const UnitType* infantry = context.rules == nullptr
            ? nullptr
            : context.rules->findUnitType("InfantryType", object.type);
        // InfDeath=9 is damage, not mind control. Psychic immunity does not
        // protect human infantry; NotHuman victims die without a Brute.
        if (infantry == nullptr || !infantry->organic) {
            continue;
        }
        if (context.geneticMutatorEligible
            && !context.geneticMutatorEligible(object)) {
            continue;
        }
        affected.push_back(object.instanceId);
    }
    std::sort(affected.begin(), affected.end());
    if (mutationType.empty()) {
        emit(
            context,
            instance,
            "GeneticMutationResourceMissing",
            "General.SmallVisceroid is empty",
            &instance.firstTarget,
            nullptr,
            -1,
            -1,
            static_cast<int>(affected.size()),
            rule->startSound,
            "",
            rule->impactAnim
        );
        return;
    }
    int fallbackObjectId = 0;
    for (const WorldObjectInstance& object : *context.objects) {
        fallbackObjectId = std::max(fallbackObjectId, object.instanceId);
    }
    for (const int objectId : affected) {
        WorldObjectInstance* object = findObject(objectId, context);
        if (object == nullptr || !object->alive) {
            continue;
        }
        const auto* victimType = superWeaponUnitType(*object, context.rules);
        const auto* mutateWarhead = context.rules
            ? context.rules->findWarheadType(rule->mutateExplosion ? rule->warhead : rule->payloadWarhead) : nullptr;
        if (superWeaponVerses(*object, context, mutateWarhead) <= 0.0) continue;
        if (victimType && victimType->notHuman) {
            auto event = lifecycleEvent(SuperWeaponObjectLifecycleKind::Removed,
                instance, context.tick, objectId);
            event.healthBefore = object->health;
            event.oldCell = event.newCell = {object->x, object->y};
            event.warheadId = rule->warhead;
            event.reason = "mutation explosion non-human destruction";
            event.removalDisposition = SuperWeaponObjectRemovalDisposition::Destroyed;
            object->health = 0; object->alive = false;
            if (context.onObjectRemoved) context.onObjectRemoved(std::move(event));
            continue;
        }
        const int oldHealth = object->health;
        const std::string oldType = object->type;
        const std::string oldOwner = object->owner;
        WorldObjectInstance brute;
        brute.instanceId = context.allocateObjectId
            ? context.allocateObjectId()
            : ++fallbackObjectId;
        brute.kind = WorldObjectKind::Infantry;
        brute.mapId = object->mapId;
        brute.owner = instance.owner;
        brute.type = mutationType;
        const auto* replacementType = context.rules
            ? context.rules->findUnitType("InfantryType", mutationType) : nullptr;
        brute.health = std::max(1, context.geneticMutationMaxHealth
            ? context.geneticMutationMaxHealth(brute)
            : replacementType ? replacementType->strength : oldHealth);
        brute.x = object->x;
        brute.y = object->y;
        brute.cell = object->cell;
        brute.level = object->level;
        brute.facing = object->facing;
        brute.subCell = object->subCell;
        brute.mission = "Guard";
        brute.onBridge = object->onBridge;
        brute.alive = true;

        object->health = 0;
        object->alive = false;
        object->mission = "GeneticMutation";
        if (context.onObjectRemoved) {
            SuperWeaponObjectLifecycleEvent event = lifecycleEvent(
                SuperWeaponObjectLifecycleKind::Removed,
                instance,
                context.tick,
                objectId
            );
            event.healthBefore = oldHealth;
            event.healthAfter = 0;
            event.oldCell = {object->x, object->y};
            event.newCell = event.oldCell;
            event.oldOwner = oldOwner;
            event.newOwner = instance.owner;
            event.warheadId = rule->warhead;
            event.reason = "genetic mutation replacement";
            event.removalDisposition =
                SuperWeaponObjectRemovalDisposition::TransformSource;
            context.onObjectRemoved(std::move(event));
        }
        context.objects->push_back(std::move(brute));
        WorldObjectInstance& created = context.objects->back();
        const int createdId = created.instanceId;
        if (context.onObjectCreated) {
            context.onObjectCreated(created);
        }
        emit(
            context,
            instance,
            "GeneticMutationApplied",
            "infantry-only transformation",
            &instance.firstTarget,
            nullptr,
            createdId,
            objectId,
            oldHealth,
            "",
            "",
            rule->impactAnim + ";old=" + oldType + ";oldOwner=" + oldOwner
        );
    }
}

#include "simulation/superweapons/SuperWeaponSupportAircraft.inl"

void SuperWeaponSystem::launchParadrop(Instance& instance,
    const SuperWeaponWorldContext& context) {
    launchSupportAircraft(instance, context, false);
}

void SuperWeaponSystem::launchReveal(
    Instance& instance,
    const SuperWeaponWorldContext& context
) {
    const SuperWeaponRule* rule = findRule(instance.definitionId);
    if (rule == nullptr) {
        return;
    }
    if (superWeaponTypeKey(*rule).find("spy") != std::string::npos) {
        launchSupportAircraft(instance, context, true);
        return;
    }
    instance.activeUntilTick = context.tick + std::max(1, context.fixedTickHz * 2);
    emit(
        context,
        instance,
        lowerAscii(rule->type).find("spy") != std::string::npos
            ? "SpyPlaneRevealArea"
            : "PsychicRevealArea",
        "non-damaging reconnaissance",
        &instance.firstTarget,
        nullptr,
        -1,
        -1,
        rule->radius,
        "",
        "",
        rule->activeAnim
    );
}

void SuperWeaponSystem::cleanupEffects(int tick) {
    protections_.erase(
        std::remove_if(
            protections_.begin(),
            protections_.end(),
            [tick](const ProtectionEffect& effect) {
                return effect.untilTick >= 0 && effect.untilTick <= tick;
            }
        ),
        protections_.end()
    );
    radiation_.erase(
        std::remove_if(
            radiation_.begin(),
            radiation_.end(),
            [tick](const RadiationEffect& effect) {
                return effect.untilTick >= 0 && effect.untilTick <= tick;
            }
        ),
        radiation_.end()
    );
    for (auto it = ownerLockouts_.begin(); it != ownerLockouts_.end();) {
        if (it->second.blackoutUntil >= 0 && it->second.blackoutUntil <= tick
            && it->second.penaltyUntil >= 0 && it->second.penaltyUntil <= tick) {
            it = ownerLockouts_.erase(it);
        } else {
            ++it;
        }
    }
}

void SuperWeaponSystem::advanceRadiationEffects(
    const SuperWeaponWorldContext& context
) {
    if (context.objects == nullptr || context.rules == nullptr) {
        return;
    }
    const SpecialMechanismRules& mechanisms =
        context.rules->getSpecialMechanismRules();
    const int intervalTicks = std::max(
        1,
        convertRuleFrames(
            std::max(1, mechanisms.radiationApplicationDelay),
            context.fixedTickHz
        )
    );
    for (RadiationEffect& effect : radiation_) {
        if (effect.nextDamageTick < 0 || context.tick < effect.nextDamageTick) {
            continue;
        }
        effect.nextDamageTick = context.tick + intervalTicks;
        Instance* source = findInstanceById(effect.instanceId);
        if (source == nullptr) {
            continue;
        }
        const int remainingTicks = effect.untilTick < 0
            ? 1
            : std::max(1, effect.untilTick - context.tick);
        const int totalTicks = std::max(
            1,
            convertRuleFrames(
                effect.intensity * std::max(
                    1,
                    mechanisms.radiationDurationMultiple
                ),
                context.fixedTickHz
            )
        );
        const double decay = std::clamp(
            static_cast<double>(remainingTicks)
                / static_cast<double>(totalTicks),
            0.0,
            1.0
        );
        const int pulseDamage = std::max(
            1,
            static_cast<int>(std::lround(
                static_cast<double>(effect.intensity)
                    * mechanisms.radiationLevelFactor * decay
            ))
        );
        const SuperWeaponTarget savedTarget = source->firstTarget;
        source->firstTarget.cell = effect.cell;
        source->firstTarget.objectId = -1;
        applyRadialDamage(
            *source,
            context,
            effect.cell,
            effect.radius,
            pulseDamage,
            mechanisms.radiationSiteWarhead,
            "RadiationDamage"
        );
        source->firstTarget = savedTarget;
    }
}

bool SuperWeaponSystem::isOwnerBlackout(
    const std::string& owner,
    int tick
) const {
    const auto found = ownerLockouts_.find(lowerAscii(owner));
    return found != ownerLockouts_.end()
        && found->second.blackoutUntil > tick;
}

bool SuperWeaponSystem::isObjectProtected(int objectId, int tick) const {
    for (const ProtectionEffect& effect : protections_) {
        if (effect.objectId == objectId && effect.untilTick > tick) {
            return true;
        }
    }
    return false;
}

void SuperWeaponSystem::rebuildSnapshots(int tick) {
    sidebarSnapshots_.clear();
    renderSnapshots_.clear();
    for (const Instance& instance : instances_) {
        const SuperWeaponRule* rule = findRule(instance.definitionId);
        if (rule == nullptr || rule->postClick) {
            continue;
        }
        const bool secondStage =
            instance.state == SuperWeaponRuntimeState::AwaitingSecondTarget
            || (instance.state == SuperWeaponRuntimeState::SelectingTarget
                && instance.targetStage == 1);
        const SuperWeaponRule* targetRule = targetRuleForInstance(
            instance,
            secondStage
        );
        SuperWeaponSidebarSnapshot sidebar;
        sidebar.instanceId = instance.instanceId;
        sidebar.owner = instance.owner;
        sidebar.superWeaponId = instance.definitionId;
        sidebar.uiName = rule->uiName;
        sidebar.name = rule->name;
        sidebar.sidebarImage = rule->sidebarImage;
        sidebar.state = instance.state;
        sidebar.rechargeTicks = instance.rechargeTicks;
        sidebar.stateStartTick = instance.stateStartTick;
        sidebar.launchTick = instance.launchTick;
        sidebar.flashSidebarTabFrames = rule->flashSidebarTabFrames;
        sidebar.targetRange = targetRule == nullptr ? 0.0 : targetRule->range;
        sidebar.targetLineMultiplier = targetRule == nullptr
            ? 0
            : targetRule->lineMultiplier;
        sidebar.showTimer = rule->showTimer;
        sidebar.remainingTicks = instance.state == SuperWeaponRuntimeState::Ready
            ? 0
            : std::max(0, instance.rechargeTicks - instance.chargeTicks);
        sidebar.progress = instance.rechargeTicks <= 0
            ? 0.0
            : std::clamp(
                static_cast<double>(instance.chargeTicks)
                    / static_cast<double>(instance.rechargeTicks),
                0.0,
                1.0
            );
        sidebar.targetSelectionActive =
            instance.state == SuperWeaponRuntimeState::SelectingTarget
            || instance.state == SuperWeaponRuntimeState::AwaitingSecondTarget;
        sidebar.firstTargetAccepted = instance.targetStage >= 1;
        sidebar.firstTargetCell = instance.firstTarget.cell;
        sidebar.chargePaused = instance.state == SuperWeaponRuntimeState::Suspended
            || (isActiveState(instance.state) && rule->isPowered && !instance.powered);
        if (!instance.effectOnly) sidebarSnapshots_.push_back(std::move(sidebar));

        if (instance.state == SuperWeaponRuntimeState::Launching
            || instance.state == SuperWeaponRuntimeState::Active) {
            SuperWeaponRenderSnapshot snapshot;
            snapshot.instanceId = instance.instanceId;
            snapshot.tick = tick;
            snapshot.owner = instance.owner;
            snapshot.superWeaponId = instance.definitionId;
            snapshot.cell = instance.firstTarget.cell;
            snapshot.cell2 = instance.secondTarget.cell;
            snapshot.sourceObjectId = instance.sourceBuildingIds.empty()
                ? -1
                : instance.sourceBuildingIds.front();
            snapshot.phase = stateName(instance.state);
            const std::string type = lowerAscii(rule->type + " " + rule->action);
            if (type.find("multimissile") != std::string::npos
                || type.find("nuke") != std::string::npos) {
                snapshot.kind = "superWeaponProjectile";
                const int contactTick = instance.nextEffectTick >= 0
                    ? instance.nextEffectTick : instance.nextImpactTick;
                if (instance.state != SuperWeaponRuntimeState::Active
                    || contactTick <= tick) continue;
                snapshot.progress = std::max(0, tick - instance.stateStartTick);
                snapshot.requiredProgress = std::max(1,
                    contactTick - instance.stateStartTick);
                const double progress = double(snapshot.progress) / snapshot.requiredProgress;
                snapshot.phase = progress < 0.4 ? "Rising"
                    : progress < 0.6 ? "HighFlight" : "Falling";
                const bool hasSource = instance.nuclearLaunchOrigin.objectId >= 0;
                const bool rising = rule->nuclearAltitudeLeptons > 0
                    ? progress * superWeaponNuclearFlightFrames(*rule, hasSource)
                        < superWeaponNuclearAscentFrames(*rule, hasSource)
                    : progress < 0.6;
                if (rule->nuclearAltitudeLeptons > 0) snapshot.phase = rising ? "Rising" : "Falling";
                snapshot.sourceObjectId = instance.nuclearLaunchOrigin.objectId;
                snapshot.cell2 = instance.nuclearLaunchOrigin.cell;
                snapshot.sourceLevel = instance.nuclearLaunchLevel;
                snapshot.resourceId = rising ? rule->projectile
                    : rule->payloadProjectile.empty() ? rule->projectile : rule->payloadProjectile;
                snapshot.expireTick = instance.nextImpactTick;
            } else if (type.find("lightning") != std::string::npos) {
                snapshot.kind = "superWeaponArea";
                snapshot.resourceId = rule->cloudAnim;
                snapshot.radius = std::max(1, rule->cellSpread);
                snapshot.intensity = rule->damage;
                snapshot.expireTick = instance.activeUntilTick;
            } else if (type.find("ironcurtain") != std::string::npos
                || type.find("forceshield") != std::string::npos) {
                snapshot.kind = "superWeaponProtection";
                snapshot.resourceId = rule->invokeAnim;
                snapshot.radius = std::max(1, rule->radius);
                snapshot.expireTick = instance.activeUntilTick;
            } else if (type.find("chrono") != std::string::npos) {
                snapshot.kind = "superWeaponChrono";
                snapshot.resourceId = rule->firstAnim;
                snapshot.radius = std::max(1, rule->radius);
            } else if (type.find("psychic") != std::string::npos
                || type.find("dominator") != std::string::npos
                || type.find("genetic") != std::string::npos) {
                snapshot.kind = "superWeaponArea";
                snapshot.resourceId = rule->firstAnim.empty()
                    ? rule->impactAnim
                    : rule->firstAnim;
                snapshot.radius = std::max(1, rule->radius);
            }
            renderSnapshots_.push_back(std::move(snapshot));
        }
    }
    ensureOwnerSnapshotOrder();
    emitEffectSnapshots(tick);
}

void SuperWeaponSystem::emitEffectSnapshots(int tick) {
    // The affected-object records outlive the invocation animation and are
    // restored from saves. Derive the visual directly from that protection.
    for (const ProtectionEffect& effect : protections_) {
        if (effect.untilTick <= tick) continue;
        SuperWeaponRenderSnapshot snapshot;
        snapshot.kind = effect.forceShield ? "forceShieldTint" : "ironCurtainTint";
        snapshot.instanceId = effect.objectId;
        snapshot.sourceObjectId = effect.objectId;
        snapshot.targetObjectId = effect.objectId;
        snapshot.tick = tick;
        snapshot.expireTick = effect.untilTick;
        renderSnapshots_.push_back(std::move(snapshot));
    }
    for (const RadiationEffect& effect : radiation_) {
        SuperWeaponRenderSnapshot snapshot;
        snapshot.instanceId = effect.instanceId;
        snapshot.tick = tick;
        snapshot.owner = effect.owner;
        snapshot.kind = "superWeaponRadiation";
        snapshot.superWeaponId = "NukeSpecial";
        snapshot.resourceId = "NUKEANIM";
        snapshot.cell = effect.cell;
        snapshot.radius = effect.radius;
        snapshot.intensity = effect.intensity;
        snapshot.expireTick = effect.untilTick;
        renderSnapshots_.push_back(std::move(snapshot));
    }
}

void SuperWeaponSystem::ensureOwnerSnapshotOrder() {
    std::sort(
        sidebarSnapshots_.begin(),
        sidebarSnapshots_.end(),
        [](const SuperWeaponSidebarSnapshot& left, const SuperWeaponSidebarSnapshot& right) {
            const std::string leftKey = keyFor(left.owner, left.superWeaponId);
            const std::string rightKey = keyFor(right.owner, right.superWeaponId);
            return leftKey == rightKey
                ? left.instanceId < right.instanceId
                : leftKey < rightKey;
        }
    );
    std::sort(
        renderSnapshots_.begin(),
        renderSnapshots_.end(),
        [](const SuperWeaponRenderSnapshot& left, const SuperWeaponRenderSnapshot& right) {
            return std::tie(
                left.tick,
                left.instanceId,
                left.kind,
                left.cell.x,
                left.cell.y
            ) < std::tie(
                right.tick,
                right.instanceId,
                right.kind,
                right.cell.x,
                right.cell.y
            );
        }
    );
}

const std::vector<SuperWeaponSidebarSnapshot>& SuperWeaponSystem::sidebarSnapshots(
    const std::string& owner
) const {
    if (owner.empty()) {
        return sidebarSnapshots_;
    }
    // Sidebar callers normally use an owner-specific view.  Keep the backing
    // vector immutable and return the full deterministic view when there is
    // no matching owner; the host can filter without losing state authority.
    return sidebarSnapshots_;
}

const std::vector<SuperWeaponRenderSnapshot>& SuperWeaponSystem::renderSnapshots() const {
    return renderSnapshots_;
}

std::vector<SuperWeaponActiveLifecycleSnapshot>
SuperWeaponSystem::activeLifecycleSnapshots() const {
    std::vector<SuperWeaponActiveLifecycleSnapshot> snapshots;
    snapshots.reserve(instances_.size());
    for (const Instance& instance : instances_) {
        if (instance.state != SuperWeaponRuntimeState::Active
            || instance.activeToken <= 0) {
            continue;
        }
        const SuperWeaponRule* rule = findRule(instance.definitionId);
        if (rule == nullptr || !hasTrackedActiveLifecycle(*rule)) {
            continue;
        }
        SuperWeaponActiveLifecycleSnapshot snapshot;
        snapshot.instanceId = instance.instanceId;
        snapshot.activeToken = instance.activeToken;
        snapshot.superWeaponId = instance.definitionId;
        snapshot.superWeaponType = rule->type;
        snapshot.sourceOwner = instance.owner;
        snapshot.sourceObjectId = instance.sourceBuildingIds.empty()
            ? -1
            : instance.sourceBuildingIds.front();
        snapshot.cell = instance.firstTarget.cell;
        snapshot.startedTick = instance.activeStartedTick;
        snapshot.activeUntilTick = instance.activeUntilTick;
        snapshots.push_back(std::move(snapshot));
    }
    std::sort(
        snapshots.begin(),
        snapshots.end(),
        [](const SuperWeaponActiveLifecycleSnapshot& left,
           const SuperWeaponActiveLifecycleSnapshot& right) {
            return std::tie(left.activeToken, left.instanceId)
                < std::tie(right.activeToken, right.instanceId);
        }
    );
    return snapshots;
}

const SuperWeaponSidebarSnapshot* SuperWeaponSystem::findSidebarSnapshot(
    const std::string& owner,
    const std::string& superWeaponId
) const {
    for (const SuperWeaponSidebarSnapshot& snapshot : sidebarSnapshots_) {
        if (keyFor(snapshot.owner, snapshot.superWeaponId)
            == keyFor(owner, superWeaponId)) {
            return &snapshot;
        }
    }
    return nullptr;
}

int SuperWeaponSystem::activeLaunchTick(const std::string& owner, const std::string& superWeaponId) const {
    const auto key = keyFor(owner, superWeaponId);
    int tick = -1;
    for (const auto& instance : instances_)
        if (isActiveState(instance.state) && keyFor(instance.owner, instance.definitionId) == key)
            tick = std::max(tick, instance.launchTick);
    return tick;
}

SuperWeaponRuntimeState SuperWeaponSystem::stateOf(
    const std::string& owner,
    const std::string& superWeaponId
) const {
    const Instance* instance = findInstance(owner, superWeaponId);
    return instance == nullptr
        ? SuperWeaponRuntimeState::Unavailable
        : instance->state;
}

std::uint64_t SuperWeaponSystem::stateHash() const {
    std::uint64_t hash = 1469598103934665603ULL;
    hash = hashString(hash, "superweapon-state-v3");
    const auto mixInt = [&hash](int value) {
        hash ^= static_cast<std::uint64_t>(static_cast<std::int64_t>(value));
        hash *= 1099511628211ULL;
    };
    const auto mixBool = [&hash](bool value) {
        hash ^= value ? 1ULL : 0ULL;
        hash *= 1099511628211ULL;
    };
    mixInt(nextInstanceId_);
    mixInt(nextActiveToken_);
    mixInt(lastTick_);
    for (const Instance& instance : instances_) {
        hash = hashString(hash, instance.owner);
        hash = hashString(hash, instance.definitionId);
        hash = hashString(hash, stateName(instance.state));
        hash = hashString(hash, targetModeName(instance.targetMode));
        mixInt(instance.instanceId);
        mixBool(instance.disabled);
        mixBool(instance.powered);
        mixBool(instance.oneShot);
        if (instance.effectOnly) hash = hashString(hash, "detached-cast/v1");
        mixInt(instance.chargeTicks);
        mixInt(instance.rechargeTicks);
        mixInt(instance.stateStartTick);
        mixInt(instance.launchTick);
        mixInt(instance.activeToken);
        mixInt(instance.activeStartedTick);
        mixInt(instance.activeUntilTick);
        mixInt(instance.cooldownUntilTick);
        mixInt(instance.nextEffectTick);
        mixInt(instance.nextImpactTick);
        mixInt(instance.targetStage);
        mixBool(instance.effectApplied);
        hash ^= static_cast<std::uint64_t>(instance.rngState);
        hash *= 1099511628211ULL;
        hash = hashString(hash, cellText(instance.firstTarget.cell));
        mixInt(instance.firstTarget.objectId);
        hash = hashString(hash, cellText(instance.secondTarget.cell));
        mixInt(instance.secondTarget.objectId);
        if (instance.nuclearLaunchOrigin.objectId >= 0) {
            hash = hashString(hash, "NuclearLaunchOrigin/v1");
            mixInt(instance.nuclearLaunchOrigin.objectId);
            mixInt(instance.nuclearLaunchOrigin.cell.x);
            mixInt(instance.nuclearLaunchOrigin.cell.y);
            mixInt(instance.nuclearLaunchLevel);
        }
        std::vector<int> sources = instance.sourceBuildingIds;
        std::sort(sources.begin(), sources.end());
        hash = hashString(hash, joinInts(sources));
        mixInt(static_cast<int>(instance.stormClouds.size()));
        for (const auto& cloud : instance.stormClouds) {
            mixInt(cloud.cell.x);
            mixInt(cloud.cell.y);
            mixInt(cloud.untilTick);
            mixInt(cloud.impactTick);
            mixInt(cloud.boltIndex);
            mixInt(cloud.direct ? 1 : 0);
        }
        for (const int objectId : instance.activeObjectIds) {
            hash ^= static_cast<std::uint64_t>(objectId + 1);
            hash *= 1099511628211ULL;
        }
    }
    for (const ProtectionEffect& effect : protections_) {
        hash ^= static_cast<std::uint64_t>(effect.objectId + 1);
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(effect.untilTick + 1);
        hash *= 1099511628211ULL;
        hash ^= effect.forceShield ? 1ULL : 0ULL;
        hash *= 1099511628211ULL;
    }
    for (const RadiationEffect& effect : radiation_) {
        hash = hashString(hash, "radiation");
        hash = hashString(hash, effect.owner);
        hash ^= static_cast<std::uint64_t>(
            static_cast<std::int64_t>(effect.instanceId)
        );
        hash *= 1099511628211ULL;
        hash = hashString(hash, cellText(effect.cell));
        hash ^= static_cast<std::uint64_t>(
            static_cast<std::int64_t>(effect.radius)
        );
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(effect.untilTick + 1);
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(
            static_cast<std::int64_t>(effect.intensity)
        );
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(
            static_cast<std::int64_t>(effect.nextDamageTick)
        );
        hash *= 1099511628211ULL;
    }
    std::vector<std::tuple<std::string, int, int, int>> lockouts;
    lockouts.reserve(ownerLockouts_.size());
    for (const auto& entry : ownerLockouts_) {
        lockouts.emplace_back(
            lowerAscii(entry.first),
            entry.second.blackoutUntil,
            entry.second.penaltyUntil,
            entry.second.penalty
        );
    }
    std::sort(lockouts.begin(), lockouts.end());
    for (const auto& lockout : lockouts) {
        hash = hashString(hash, "lockout");
        hash = hashString(hash, std::get<0>(lockout));
        hash ^= static_cast<std::uint64_t>(
            static_cast<std::int64_t>(std::get<1>(lockout))
        );
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(
            static_cast<std::int64_t>(std::get<2>(lockout))
        );
        hash *= 1099511628211ULL;
        hash ^= static_cast<std::uint64_t>(
            static_cast<std::int64_t>(std::get<3>(lockout))
        );
        hash *= 1099511628211ULL;
    }
    return hash;
}

int SuperWeaponSystem::lastTick() const {
    return lastTick_;
}

bool SuperWeaponSystem::alignEmptyStateTick(int tick) {
    if (tick < 0
        || !instances_.empty()
        || !protections_.empty()
        || !radiation_.empty()
        || !ownerLockouts_.empty()) {
        return false;
    }
    lastTick_ = tick;
    rebuildSnapshots(tick);
    return true;
}

void SuperWeaponSystem::save(std::ostream& output) const {
    output << "[SuperWeapons]\n";
    output << "Version=8\n";
    output << "Count=" << instances_.size() << "\n";
    output << "NextInstanceId=" << nextInstanceId_ << "\n";
    output << "NextActiveToken=" << nextActiveToken_ << "\n";
    output << "LastTick=" << lastTick_ << "\n\n";
    for (std::size_t index = 0; index < instances_.size(); ++index) {
        const Instance& instance = instances_[index];
        output << "[SuperWeapon." << index << "]\n";
        output << "InstanceId=" << instance.instanceId << "\n";
        output << "Owner=" << instance.owner << "\n";
        output << "Definition=" << instance.definitionId << "\n";
        output << "Sources=" << joinInts(instance.sourceBuildingIds) << "\n";
        output << "State=" << stateName(instance.state) << "\n";
        output << "TargetMode=" << targetModeName(instance.targetMode) << "\n";
        output << "Disabled=" << (instance.disabled ? "yes" : "no") << "\n";
        output << "Powered=" << (instance.powered ? "yes" : "no") << "\n";
        output << "OneShot=" << (instance.oneShot ? "yes" : "no") << "\n";
        output << "EffectOnly=" << (instance.effectOnly ? "yes" : "no") << "\n";
        output << "ChargeTicks=" << instance.chargeTicks << "\n";
        output << "RechargeTicks=" << instance.rechargeTicks << "\n";
        output << "StateStartTick=" << instance.stateStartTick << "\n";
        output << "LaunchTick=" << instance.launchTick << "\n";
        output << "ActiveToken=" << instance.activeToken << "\n";
        output << "ActiveStartedTick=" << instance.activeStartedTick << "\n";
        output << "ActiveUntilTick=" << instance.activeUntilTick << "\n";
        output << "CooldownUntilTick=" << instance.cooldownUntilTick << "\n";
        output << "NextEffectTick=" << instance.nextEffectTick << "\n";
        output << "NextImpactTick=" << instance.nextImpactTick << "\n";
        output << "TargetStage=" << instance.targetStage << "\n";
        output << "EffectApplied=" << (instance.effectApplied ? "yes" : "no") << "\n";
        output << "RngState=" << instance.rngState << "\n";
        output << "FirstX=" << instance.firstTarget.cell.x << "\n";
        output << "FirstY=" << instance.firstTarget.cell.y << "\n";
        output << "FirstObjectId=" << instance.firstTarget.objectId << "\n";
        output << "SecondX=" << instance.secondTarget.cell.x << "\n";
        output << "SecondY=" << instance.secondTarget.cell.y << "\n";
        output << "SecondObjectId=" << instance.secondTarget.objectId << "\n";
        output << "NuclearSourceId=" << instance.nuclearLaunchOrigin.objectId << "\n";
        output << "NuclearSourceX=" << instance.nuclearLaunchOrigin.cell.x << "\n";
        output << "NuclearSourceY=" << instance.nuclearLaunchOrigin.cell.y << "\n";
        output << "NuclearSourceLevel=" << instance.nuclearLaunchLevel << "\n";
        output << "ActiveObjects=" << joinInts(instance.activeObjectIds) << "\n";
        output << "StormCloudCount=" << instance.stormClouds.size() << "\n";
        for (std::size_t cloudIndex = 0; cloudIndex < instance.stormClouds.size(); ++cloudIndex) {
            const auto& cloud = instance.stormClouds[cloudIndex];
            output << "StormCloud" << cloudIndex << "=" << cloud.cell.x << ","
                << cloud.cell.y << "," << cloud.untilTick << "," << cloud.impactTick
                << "," << cloud.boltIndex << "," << (cloud.direct ? 1 : 0) << "\n";
        }
        output << "\n";
    }
    output << "[SuperWeaponProtections]\n";
    output << "Count=" << protections_.size() << "\n";
    for (std::size_t index = 0; index < protections_.size(); ++index) {
        const ProtectionEffect& effect = protections_[index];
        output << "Object" << index << "=" << effect.objectId << ","
               << effect.untilTick << "," << (effect.forceShield ? 1 : 0)
               << "\n";
    }
    output << "\n[SuperWeaponRadiation]\n";
    output << "Count=" << radiation_.size() << "\n";
    for (std::size_t index = 0; index < radiation_.size(); ++index) {
        const RadiationEffect& effect = radiation_[index];
        output << "Effect" << index << "=" << effect.owner << "|"
               << effect.instanceId << "|" << effect.cell.x << "|"
               << effect.cell.y << "|" << effect.radius << "|"
               << effect.intensity << "|" << effect.untilTick << "|"
               << effect.nextDamageTick << "\n";
    }
    output << "\n[SuperWeaponLockouts]\n";
    output << "Count=" << ownerLockouts_.size() << "\n";
    std::vector<std::pair<std::string, OwnerLockout>> lockouts(
        ownerLockouts_.begin(),
        ownerLockouts_.end()
    );
    std::sort(
        lockouts.begin(),
        lockouts.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; }
    );
    for (std::size_t index = 0; index < lockouts.size(); ++index) {
        output << "Owner" << index << "=" << lockouts[index].first << "|"
               << lockouts[index].second.blackoutUntil << "|"
               << lockouts[index].second.penaltyUntil << "|"
               << lockouts[index].second.penalty << "\n";
    }
}

bool SuperWeaponSystem::load(
    const IniFile& ini,
    std::string* errorMessage
) {
    const IniFile::Section* section = ini.getSection("SuperWeapons");
    if (section == nullptr) {
        return true;
    }
    auto value = [&](const std::string& sectionName, const std::string& key, const std::string& fallback = std::string()) {
        return ini.getValue(sectionName, key, fallback);
    };
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    const auto strictInt = [](const std::string& text, int& result) {
        try {
            const std::string normalized = trimAscii(text);
            if (normalized.empty()) {
                return false;
            }
            std::size_t consumed = 0;
            const long long parsed = std::stoll(normalized, &consumed);
            if (consumed != normalized.size()
                || parsed < std::numeric_limits<int>::min()
                || parsed > std::numeric_limits<int>::max()) {
                return false;
            }
            result = static_cast<int>(parsed);
            return true;
        } catch (...) {
            return false;
        }
    };
    int version = 0;
    if (!strictInt(value("SuperWeapons", "Version", "1"), version)
        || (version < 1 || version > 8)) {
        return fail(
            "unsupported SuperWeapon save version "
            + value("SuperWeapons", "Version", "1")
        );
    }

    // Parse into a detached candidate. No malformed section may partially
    // clear or replace the live deterministic state.
    SuperWeaponSystem candidate = *this;
    candidate.instances_.clear();
    candidate.protections_.clear();
    candidate.radiation_.clear();
    candidate.ownerLockouts_.clear();
    candidate.sidebarSnapshots_.clear();
    candidate.renderSnapshots_.clear();

    int count = 0;
    int headerNextInstanceId = 1;
    int headerNextActiveToken = 1;
    if (!strictInt(value("SuperWeapons", "Count", "0"), count)
        || count < 0 || count > 4096) {
        return fail("invalid SuperWeapon instance count");
    }
    if (!strictInt(
            value("SuperWeapons", "NextInstanceId", "1"),
            headerNextInstanceId
        ) || headerNextInstanceId < 1) {
        return fail("invalid SuperWeapon NextInstanceId");
    }
    if (version >= 2
        && (!strictInt(
                value("SuperWeapons", "NextActiveToken", "1"),
                headerNextActiveToken
            ) || headerNextActiveToken < 1)) {
        return fail("invalid SuperWeapon NextActiveToken");
    }
    if (!strictInt(value("SuperWeapons", "LastTick", "0"), candidate.lastTick_)) {
        return fail("invalid SuperWeapon LastTick");
    }
    candidate.nextInstanceId_ = headerNextInstanceId;
    candidate.nextActiveToken_ = version >= 2 ? headerNextActiveToken : 1;

    std::unordered_set<int> instanceIds;
    std::unordered_set<int> activeTokens;
    std::unordered_set<std::string> instanceKeys;
    int maximumActiveToken = 0;
    for (int index = 0; index < count; ++index) {
        const std::string sectionName = "SuperWeapon." + std::to_string(index);
        const std::string owner = value(sectionName, "Owner");
        const std::string definition = value(sectionName, "Definition");
        const SuperWeaponRule* definitionRule = candidate.findRule(definition);
        if (owner.empty() || definitionRule == nullptr) {
            return fail(
                "save contains unknown super weapon definition ["
                + definition + "]"
            );
        }
        Instance instance;
        const auto readInt = [&](const std::string& key, const std::string& fallback, int& output) {
            return strictInt(value(sectionName, key, fallback), output);
        };
        if (!readInt("InstanceId", "-1", instance.instanceId)
            || instance.instanceId <= 0
            || !instanceIds.insert(instance.instanceId).second) {
            return fail("invalid or duplicate SuperWeapon InstanceId");
        }
        instance.owner = owner;
        instance.definitionId = definition;
        if (version >= 8) {
            const auto effectOnly = lowerAscii(trimAscii(value(sectionName, "EffectOnly", "no")));
            if (effectOnly != "yes" && effectOnly != "no") return fail("invalid SuperWeapon EffectOnly");
            instance.effectOnly = effectOnly == "yes";
        }
        if (!instance.effectOnly && !instanceKeys.insert(keyFor(owner, definition)).second) {
            return fail("duplicate SuperWeapon owner/definition instance");
        }
        instance.sourceBuildingIds = parseInts(value(sectionName, "Sources"));
        const std::string stateText = trimAscii(
            value(sectionName, "State", "Unavailable")
        );
        instance.state = stateFromName(stateText);
        if (lowerAscii(stateText) != lowerAscii(stateName(instance.state))) {
            return fail("invalid SuperWeapon state [" + stateText + "]");
        }
        instance.targetMode = targetModeFromName(
            value(sectionName, "TargetMode", definitionRule->targetMode)
        );
        instance.disabled = parseBoolValue(value(sectionName, "Disabled", "no"));
        instance.powered = parseBoolValue(value(sectionName, "Powered", "no"));
        instance.oneShot = version >= 3
            && parseBoolValue(value(sectionName, "OneShot", "no"));
        if (!readInt("ChargeTicks", "0", instance.chargeTicks)
            || !readInt("RechargeTicks", "1", instance.rechargeTicks)
            || instance.rechargeTicks < 1
            || !readInt("StateStartTick", "0", instance.stateStartTick)
            || !readInt("LaunchTick", "-1", instance.launchTick)
            || !readInt("ActiveUntilTick", "-1", instance.activeUntilTick)
            || !readInt("CooldownUntilTick", "-1", instance.cooldownUntilTick)
            || !readInt("NextEffectTick", "-1", instance.nextEffectTick)
            || !readInt("NextImpactTick", "-1", instance.nextImpactTick)
            || !readInt("TargetStage", "0", instance.targetStage)) {
            return fail("invalid numeric SuperWeapon instance field");
        }
        if (version >= 2) {
            if (!readInt("ActiveToken", "-1", instance.activeToken)
                || !readInt(
                    "ActiveStartedTick",
                    "-1",
                    instance.activeStartedTick
                )) {
                return fail("invalid SuperWeapon active lifecycle field");
            }
        }
        instance.effectApplied = parseBoolValue(value(sectionName, "EffectApplied", "no"));
        instance.rngState = parseUInt32Value(value(sectionName, "RngState", "1"), 1U);
        instance.firstTarget = {
            parseCell(value(sectionName, "FirstX", "-1"), value(sectionName, "FirstY", "-1")),
            parseIntValue(value(sectionName, "FirstObjectId", "-1"), -1)
        };
        instance.secondTarget = {
            parseCell(value(sectionName, "SecondX", "-1"), value(sectionName, "SecondY", "-1")),
            parseIntValue(value(sectionName, "SecondObjectId", "-1"), -1)
        };
        instance.activeObjectIds = parseInts(value(sectionName, "ActiveObjects"));
        if (version >= 6) {
            if (!readInt("NuclearSourceId", "-1", instance.nuclearLaunchOrigin.objectId)
                || !readInt("NuclearSourceX", "-1", instance.nuclearLaunchOrigin.cell.x)
                || !readInt("NuclearSourceY", "-1", instance.nuclearLaunchOrigin.cell.y)
                || !readInt("NuclearSourceLevel", "0", instance.nuclearLaunchLevel)
                || instance.nuclearLaunchOrigin.objectId < -1
                || (instance.nuclearLaunchOrigin.objectId >= 0
                    && (instance.nuclearLaunchOrigin.cell.x < 0
                        || instance.nuclearLaunchOrigin.cell.y < 0)))
                return fail("invalid nuclear launch origin");
        }
        if (version >= 4) {
            int cloudCount = 0;
            if (!readInt("StormCloudCount", "0", cloudCount)
                || cloudCount < 0 || cloudCount > 16384) return fail("invalid storm cloud count");
            for (int cloudIndex = 0; cloudIndex < cloudCount; ++cloudIndex) {
                const auto values = splitValues(value(sectionName,
                    "StormCloud" + std::to_string(cloudIndex)));
                StormCloud cloud;
                if (values.size() != (version >= 7 ? 6U : 3U) || !strictInt(values[0], cloud.cell.x)
                    || !strictInt(values[1], cloud.cell.y) || !strictInt(values[2], cloud.untilTick)
                    || cloud.cell.x < 0 || cloud.cell.y < 0 || cloud.untilTick < 0)
                    return fail("invalid storm cloud record");
                if (version >= 7) {
                    int direct = 0;
                    if (!strictInt(values[3], cloud.impactTick) || cloud.impactTick < -1
                        || cloud.impactTick >= cloud.untilTick
                        || !strictInt(values[4], cloud.boltIndex) || cloud.boltIndex < 0
                        || !strictInt(values[5], direct) || direct < 0 || direct > 1)
                        return fail("invalid pending storm strike");
                    cloud.direct = direct != 0;
                }
                instance.stormClouds.push_back(cloud);
            }
        } else if (isDominatorLifecycle(*definitionRule)
            && instance.state == SuperWeaponRuntimeState::Active) {
            // Legacy versions applied the payload at launch. Do not replay
            // damage/control when migrating an already active dominator.
            instance.effectApplied = true;
            instance.nextImpactTick = -1;
        }

        const bool trackedLifecycle = hasTrackedActiveLifecycle(*definitionRule);
        if (version == 1
            && trackedLifecycle
            && instance.state == SuperWeaponRuntimeState::Active) {
            instance.activeToken = instance.instanceId;
            instance.activeStartedTick = instance.stateStartTick;
        }
        if (instance.activeToken >= 0) {
            if (instance.activeToken == 0
                || instance.state != SuperWeaponRuntimeState::Active
                || !trackedLifecycle
                || instance.activeStartedTick < 0
                || instance.activeUntilTick < instance.activeStartedTick
                || !activeTokens.insert(instance.activeToken).second) {
                return fail("invalid or duplicate SuperWeapon active lifecycle");
            }
            maximumActiveToken = std::max(
                maximumActiveToken,
                instance.activeToken
            );
        } else if (instance.activeToken != -1
            || instance.activeStartedTick != -1
            || (trackedLifecycle
                && instance.state == SuperWeaponRuntimeState::Active)) {
            return fail("incomplete SuperWeapon active lifecycle");
        }

        if (instance.effectOnly && (instance.oneShot
            || (!isActiveState(instance.state) && instance.state != SuperWeaponRuntimeState::Unavailable)))
            return fail("invalid detached SuperWeapon cast state");
        if (version < 5 && isActiveState(instance.state)) {
            // Older saves kept a full charge while the effect was running.
            // Do not turn that consumed charge into another ready launch.
            instance.chargeTicks = 0;
        }
        candidate.instances_.push_back(std::move(instance));
        candidate.nextInstanceId_ = std::max(
            candidate.nextInstanceId_,
            candidate.instances_.back().instanceId + 1
        );
    }
    for (const auto& instance : candidate.instances_)
        if (instance.effectOnly && !instanceKeys.contains(keyFor(instance.owner, instance.definitionId)))
            return fail("detached SuperWeapon cast has no reusable grant");
    if (version >= 2 && headerNextActiveToken <= maximumActiveToken) {
        return fail("SuperWeapon NextActiveToken does not follow active tokens");
    }
    candidate.nextActiveToken_ = std::max(
        candidate.nextActiveToken_,
        maximumActiveToken + 1
    );

    int protectionCount = 0;
    if (!strictInt(
            value("SuperWeaponProtections", "Count", "0"),
            protectionCount
        ) || protectionCount < 0 || protectionCount > 16384) {
        return fail("invalid SuperWeapon protection count");
    }
    for (int index = 0; index < protectionCount; ++index) {
        const std::vector<std::string> values = splitValues(
            value("SuperWeaponProtections", "Object" + std::to_string(index))
        );
        int objectId = 0;
        int untilTick = 0;
        int forceShield = 0;
        if (values.size() != 3U
            || !strictInt(values[0], objectId)
            || !strictInt(values[1], untilTick)
            || !strictInt(values[2], forceShield)) {
            return fail("invalid SuperWeapon protection record");
        }
        candidate.protections_.push_back({objectId, untilTick, forceShield != 0});
    }
    int radiationCount = 0;
    if (!strictInt(
            value("SuperWeaponRadiation", "Count", "0"),
            radiationCount
        ) || radiationCount < 0 || radiationCount > 16384) {
        return fail("invalid SuperWeapon radiation count");
    }
    for (int index = 0; index < radiationCount; ++index) {
        const std::vector<std::string> values = splitValues(
            value("SuperWeaponRadiation", "Effect" + std::to_string(index))
        );
        RadiationEffect effect;
        if (values.size() != 8U || values[0].empty()
            || !strictInt(values[1], effect.instanceId)
            || !strictInt(values[2], effect.cell.x)
            || !strictInt(values[3], effect.cell.y)
            || !strictInt(values[4], effect.radius)
            || !strictInt(values[5], effect.intensity)
            || !strictInt(values[6], effect.untilTick)
            || !strictInt(values[7], effect.nextDamageTick)) {
            return fail("invalid SuperWeapon radiation record");
        }
        effect.owner = values[0];
        candidate.radiation_.push_back(std::move(effect));
    }
    int lockoutCount = 0;
    if (!strictInt(
            value("SuperWeaponLockouts", "Count", "0"),
            lockoutCount
        ) || lockoutCount < 0 || lockoutCount > 4096) {
        return fail("invalid SuperWeapon lockout count");
    }
    for (int index = 0; index < lockoutCount; ++index) {
        const std::vector<std::string> values = splitValues(
            value("SuperWeaponLockouts", "Owner" + std::to_string(index))
        );
        OwnerLockout lockout;
        if (values.size() != 4U || values[0].empty()
            || !strictInt(values[1], lockout.blackoutUntil)
            || !strictInt(values[2], lockout.penaltyUntil)
            || !strictInt(values[3], lockout.penalty)) {
            return fail("invalid SuperWeapon lockout record");
        }
        candidate.ownerLockouts_[values[0]] = lockout;
    }
    std::sort(
        candidate.instances_.begin(),
        candidate.instances_.end(),
        [](const Instance& left, const Instance& right) {
            const auto leftKey = keyFor(left.owner, left.definitionId);
            const auto rightKey = keyFor(right.owner, right.definitionId);
            return leftKey == rightKey ? left.instanceId < right.instanceId : leftKey < rightKey;
        }
    );
    candidate.rebuildSnapshots(candidate.lastTick_);
    *this = std::move(candidate);
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

int SuperWeaponSystem::runOneShotSmokeTest() {
    SuperWeaponRule rule;
    rule.id = "CrateNukeSpecial";
    rule.name = rule.id;
    rule.type = "MultiMissile";
    rule.action = "Nuke";
    rule.targetMode = "Area";
    rule.rechargeTimeMinutes = 1.0 / 60.0;
    rule.damage = 600;
    rule.radius = 2;
    rule.targetEnemy = true;
    rule.targetFriendly = true;
    rule.targetInfantry = true;
    rule.targetUnits = true;
    rule.targetBuildings = true;

    SuperWeaponSystem system;
    system.reset(std::vector<SuperWeaponRule>{rule});
    SuperWeaponWorldContext context;
    context.tick = 100;
    context.fixedTickHz = 30;
    context.mapWidth = 16;
    context.mapHeight = 16;
    context.superWeaponsAllowed = true;
    context.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
        return x >= 0 && y >= 0 && x < 16 && y < 16;
    };
    int grantedEventCount = 0;
    context.emit = [&](const SuperWeaponEvent& event) {
        if (event.kind == "SuperWeaponGranted") {
            ++grantedEventCount;
        }
    };

    if (!system.grantOneShot("player", rule.id, &context)) {
        std::cerr << "superweapon one-shot smoke: grant failed\n";
        return 1;
    }
    const Instance* virtualInstance = system.findInstance("player", rule.id);
    if (virtualInstance == nullptr
        || !virtualInstance->oneShot
        || virtualInstance->sourceBuildingIds.size() != 1U
        || virtualInstance->sourceBuildingIds.front() != -1
        || grantedEventCount != 1) {
        std::cerr << "superweapon one-shot smoke: invalid virtual source\n";
        return 1;
    }
    const std::uint64_t beforeDuplicateGrant = system.stateHash();
    if (!system.grantOneShot("player", rule.id, &context)
        || system.stateHash() != beforeDuplicateGrant
        || grantedEventCount != 1) {
        std::cerr << "superweapon one-shot smoke: duplicate virtual grant\n";
        return 1;
    }
    if (!system.forceReady("player", rule.id, &context)) {
        std::cerr << "superweapon one-shot smoke: ready failed\n";
        return 1;
    }

    WorldObjectInstance liveSilo;
    liveSilo.instanceId = 7001;
    liveSilo.kind = WorldObjectKind::Structure;
    liveSilo.owner = "player";
    liveSilo.type = "NAMISL";
    liveSilo.health = 100;
    liveSilo.alive = true;
    liveSilo.poweredOperational = true;
    std::vector<WorldObjectInstance> liveObjects{liveSilo};
    SuperWeaponWorldContext liveContext = context;
    liveContext.objects = &liveObjects;
    liveContext.emit = {};
    SuperWeaponSystem liveSourceSystem;
    liveSourceSystem.reset(std::vector<SuperWeaponRule>{rule});
    if (!liveSourceSystem.grant(
            "player",
            rule.id,
            liveSilo.instanceId,
            &liveContext
        )
        || !liveSourceSystem.grantOneShot("player", rule.id, &liveContext)) {
        std::cerr << "superweapon one-shot smoke: live source grant failed\n";
        return 1;
    }
    const Instance* liveSource = liveSourceSystem.findInstance(
        "player",
        rule.id
    );
    if (liveSource == nullptr
        || liveSource->oneShot
        || liveSource->sourceBuildingIds.size() != 1U
        || liveSource->sourceBuildingIds.front() != liveSilo.instanceId) {
        std::cerr << "superweapon one-shot smoke: live source lifetime changed\n";
        return 1;
    }

    SuperWeaponSystem repeatingSourceSystem;
    repeatingSourceSystem.reset(std::vector<SuperWeaponRule>{rule});
    if (!repeatingSourceSystem.grant("player", rule.id, -1, &liveContext)) {
        std::cerr << "superweapon one-shot smoke: repeating setup failed\n";
        return 1;
    }
    const std::uint64_t beforeRepeatingGrant =
        repeatingSourceSystem.stateHash();
    if (!repeatingSourceSystem.grantOneShot("player", rule.id, &liveContext)
        || repeatingSourceSystem.stateHash() != beforeRepeatingGrant) {
        std::cerr << "superweapon one-shot smoke: repeating lifetime changed\n";
        return 1;
    }

    std::vector<WorldObjectInstance> staleObjects{liveSilo};
    SuperWeaponWorldContext staleContext = context;
    staleContext.objects = &staleObjects;
    staleContext.emit = {};
    SuperWeaponSystem staleSourceSystem;
    staleSourceSystem.reset(std::vector<SuperWeaponRule>{rule});
    if (!staleSourceSystem.grant(
            "player",
            rule.id,
            liveSilo.instanceId,
            &staleContext
        )
        || !staleSourceSystem.forceReady("player", rule.id, &staleContext)) {
        std::cerr << "superweapon one-shot smoke: stale source setup failed\n";
        return 1;
    }
    staleObjects.front().alive = false;
    staleObjects.front().health = 0;
    if (!staleSourceSystem.grantOneShot("player", rule.id, &staleContext)) {
        std::cerr << "superweapon one-shot smoke: stale source grant failed\n";
        return 1;
    }
    const Instance* staleSource = staleSourceSystem.findInstance(
        "player",
        rule.id
    );
    if (staleSource == nullptr
        || !staleSource->oneShot
        || staleSource->sourceBuildingIds.size() != 1U
        || staleSource->sourceBuildingIds.front() != -1) {
        std::cerr << "superweapon one-shot smoke: stale source survived\n";
        return 1;
    }
    std::string staleReason;
    if (!staleSourceSystem.beginTargetSelection(
            "player",
            rule.id,
            staleContext,
            &staleReason
        )
        || !staleSourceSystem.submitTarget(
            "player",
            rule.id,
            {{4, 4}, -1},
            staleContext,
            &staleReason
        )) {
        std::cerr << "superweapon one-shot smoke: stale source launch failed: "
                  << staleReason << "\n";
        return 1;
    }
    for (int tick = 101; tick <= 220; ++tick) {
        staleContext.tick = tick;
        staleSourceSystem.tick(staleContext);
    }
    if (staleSourceSystem.stateOf("player", rule.id)
            != SuperWeaponRuntimeState::Unavailable) {
        std::cerr << "superweapon one-shot smoke: stale source recharged\n";
        return 1;
    }

    std::ostringstream encoded;
    system.save(encoded);
    IniFile ini;
    SuperWeaponSystem restored;
    restored.reset(std::vector<SuperWeaponRule>{rule});
    std::string loadError;
    if (!ini.loadFromString(encoded.str())
        || !restored.load(ini, &loadError)
        || restored.stateHash() != system.stateHash()) {
        std::cerr << "superweapon one-shot smoke: roundtrip failed: "
                  << loadError << "\n";
        return 1;
    }

    std::string reason;
    if (!restored.beginTargetSelection(
            "player",
            rule.id,
            context,
            &reason
        )
        || !restored.submitTarget(
            "player",
            rule.id,
            {{4, 4}, -1},
            context,
            &reason
        )) {
        std::cerr << "superweapon one-shot smoke: launch failed: "
                  << reason << "\n";
        return 1;
    }
    for (int tick = 101; tick <= 220; ++tick) {
        context.tick = tick;
        restored.tick(context);
    }
    if (restored.stateOf("player", rule.id)
            != SuperWeaponRuntimeState::Unavailable) {
        std::cerr << "superweapon one-shot smoke: virtual source recharged\n";
        return 1;
    }
    std::cout << "superweapon one-shot smoke: passed\n";
    return 0;
}

#include "simulation/superweapons/diagnostics/SuperWeaponDetailSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponRechargeSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponNoCooldownSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponForceShieldGroundSmoke.inl"
#include "simulation/superweapons/diagnostics/SuperWeaponChronosphereGroundSmoke.inl"

int SuperWeaponSystem::runSmokeTest() {
    if (!smokeSuperWeaponChronosphereGround()) return 1;
    if (!smokeSuperWeaponForceShieldGround()) return 1;
    if (!smokeSuperWeaponConcurrentRecharge()) return 1;
    if (!smokeSuperWeaponNoCooldown()) return 1;
    if (!smokeSuperWeaponDetails()) {
        std::cerr << "superweapon detail timing/cloud/save-resume FAIL\n";
        return 1;
    }
    std::vector<SuperWeaponRule> rules;
    auto addRule = [&](const std::string& id, const std::string& type, const std::string& mode) {
        SuperWeaponRule rule;
        rule.id = id;
        rule.name = id;
        rule.type = type;
        rule.action = type;
        rule.targetMode = mode;
        rule.rechargeTimeMinutes = 1.0 / 60.0;
        rule.targetEnemy = true;
        rule.targetFriendly = false;
        rule.targetInfantry = true;
        rule.targetUnits = true;
        rule.targetBuildings = true;
        rules.push_back(std::move(rule));
    };
    addRule("NukeSpecial", "MultiMissile", "Area");
    rules.back().damage = 600;
    rules.back().radius = 2;
    rules.back().payloadWarhead = "NUKE";
    rules.back().payloadProjectile = "NukeDown";
    rules.back().impactAnim = "NUKEANIM";
    addRule("LightningStormSpecial", "LightningStorm", "Area");
    rules.back().damage = 30;
    rules.back().payloadWarhead = "BOLTWH";
    rules.back().boltAnim = "BOLTANIM";
    rules.back().cloudAnim = "CLOUDANIM";
    rules.back().cellSpread = 2;
    rules.back().defermentFrames = 2;
    rules.back().durationFrames = 20;
    rules.back().scatterDelayFrames = 2;
    rules.back().hitDelayFrames = 4;
    addRule("IronCurtainSpecial", "IronCurtain", "Area");
    rules.back().targetEnemy = false;
    rules.back().targetFriendly = true;
    rules.back().radius = 2;
    rules.back().durationFrames = 30;
    addRule("ForceShieldSpecial", "ForceShield", "Area");
    rules.back().targetEnemy = false;
    rules.back().targetFriendly = true;
    rules.back().targetInfantry = false;
    rules.back().targetUnits = false;
    rules.back().targetBuildings = true;
    rules.back().radius = 2;
    rules.back().durationFrames = 30;
    rules.back().blackoutDurationFrames = 60;
    addRule("ChronoSphereSpecial", "ChronoSphere", "TwoStage");
    rules.back().targetEnemy = false;
    rules.back().targetFriendly = true;
    rules.back().radius = 1;
    addRule("PsychicDominatorSpecial", "PsychicDominator", "Area");
    rules.back().captureRange = 1;
    rules.back().durationFrames = 15;
    rules.back().warhead = "PSYWH";
    rules.back().firstAnim = "PDFXCLD";
    rules.back().secondAnim = "PDFXEND";
    addRule("GeneticConverterSpecial", "GeneticConverter", "Area");
    rules.back().range = 2;
    rules.back().targetFriendly = true;
    rules.back().mutationType = "BRUTE";
    addRule("ParaDropSpecial", "ParaDrop", "Cell");
    rules.back().planeType = "DROPPLANE";
    rules.back().infantryTypes = "E1";
    rules.back().infantryCounts = "2";
    addRule("SpyPlaneSpecial", "SpyPlane", "Cell");
    rules.back().radius = 4;
    rules.back().planeType = "DROPPLANE";
    addRule("PsychicRevealSpecial", "PsychicReveal", "Cell");
    rules.back().radius = 4;

    IniFile semanticIni;
    if (!semanticIni.loadFromString(R"ini(
[InfantryTypes]
0=E1
1=PSIIMMUNE
2=DOG
3=ROBOT
4=BRUTE

[VehicleTypes]
0=TANK

[AircraftTypes]
0=DROPPLANE

[BuildingTypes]
0=ALLYHQ

[General]
AISuperDefenseProbability=120,50,-10
AISuperDefenseFrames=25
AISuperDefenseDistance=-4

[SuperWeaponTypes]
0=ForceShieldSpecial
1=GeneticConverterSpecial
2=ChronoPrimarySpecial
3=ChronoPostSpecial

[E1]
Strength=100

[PSIIMMUNE]
Strength=100
ImmuneToPsionics=yes

[DOG]
Strength=100
NotHuman=yes

[ROBOT]
Strength=100
Organic=no

[BRUTE]
Strength=200
ImmuneToPsionics=yes

[TANK]
Strength=300

[DROPPLANE]
Strength=300
Speed=15

[ALLYHQ]
Strength=1000

[ForceShieldSpecial]
Type=ForceShield
Action=ForceShield
Range=3.4

[GeneticConverterSpecial]
Type=GeneticConverter
Action=GeneticConverter
Range=5

[ChronoPrimarySpecial]
UIName=Name:ChronoPrimary
Name=Chrono Primary
Type=ChronoPrimary
Action=ChronoPrimary
RechargeTime=.25
RechargeVoice=ReadyVoice
ChargingVoice=ChargingVoice
ImpatientVoice=ImpatientVoice
SuspendVoice=SuspendVoice
Range=1.5
LineMultiplier=0
PreClick=yes
ShowTimer=yes
AIDefendAgainst=yes

[ChronoPostSpecial]
Type=ChronoWarp
Action=ChronoWarp
PostClick=yes
PreDependent=ChronoPrimary
Range=8
LineMultiplier=4
FlashSidebarTabFrames=0
)ini")) {
        std::cerr << "superweapon smoke: semantic fixture parse failed\n";
        return 1;
    }
    RulesDatabase semanticRules;
    if (!semanticRules.loadFromIni(semanticIni)) {
        std::cerr << "superweapon smoke: semantic rules load failed\n";
        return 1;
    }
    const UnitType* humanType = semanticRules.findUnitType("InfantryType", "E1");
    const UnitType* dogType = semanticRules.findUnitType("InfantryType", "DOG");
    const UnitType* robotType = semanticRules.findUnitType("InfantryType", "ROBOT");
    const SuperWeaponRule* parsedForce =
        semanticRules.findSuperWeaponType("ForceShieldSpecial");
    const SuperWeaponRule* parsedMutator =
        semanticRules.findSuperWeaponType("GeneticConverterSpecial");
    const SuperWeaponRule* parsedPrimary =
        semanticRules.findSuperWeaponType("ChronoPrimarySpecial");
    const SuperWeaponRule* parsedPost =
        semanticRules.findSuperWeaponType("ChronoPostSpecial");
    const AiSuperDefenseRules& parsedAiDefense =
        semanticRules.getSpecialMechanismRules().aiSuperDefense;
    if (humanType == nullptr || !humanType->organic || humanType->notHuman
        || dogType == nullptr || !dogType->notHuman
        || robotType == nullptr || robotType->organic
        || parsedForce == nullptr || parsedForce->targetInfantry
        || parsedForce->targetUnits || !parsedForce->targetBuildings
        || parsedMutator == nullptr || parsedMutator->mutationType != "BRUTE"
        || !parsedMutator->targetFriendly || !parsedMutator->targetEnemy
        || parsedPrimary == nullptr || !parsedPrimary->preClick
        || parsedPrimary->postClick
        || parsedPrimary->targetMode != "TwoStage"
        || std::abs(parsedPrimary->rechargeTimeMinutes - 0.25) > 0.0001
        || parsedPrimary->lineMultiplier != 0
        || parsedPrimary->flashSidebarTabFrames != -1
        || !parsedPrimary->showTimer || !parsedPrimary->aiDefendAgainst
        || parsedPost == nullptr || !parsedPost->postClick
        || parsedPost->targetMode != "Cell"
        || parsedPost->flashSidebarTabFrames != 0
        || parsedAiDefense.probabilityPercent
            != std::array<int, 3>{{100, 50, 0}}
        || parsedAiDefense.alertFrames != 25
        || parsedAiDefense.distanceCells != 0
        || std::abs(parsedForce->rechargeTimeMinutes - 5.0) > 0.0001) {
        std::cerr << "superweapon smoke: stock target/type parsing mismatch\n";
        return 1;
    }

    SuperWeaponSystem system;
    system.reset(rules);
    std::vector<WorldObjectInstance> objects;
    objects.push_back({100, WorldObjectKind::Unit, "", "enemy", "TANK", 100, 10, 10, 330, 0});
    objects.push_back({101, WorldObjectKind::Infantry, "", "enemy", "E1", 100, 12, 12, 396, 0});
    objects.push_back({102, WorldObjectKind::Unit, "", "player", "TANK", 100, 4, 4, 132, 0});
    objects.push_back({103, WorldObjectKind::Infantry, "", "enemy", "E1", 73, 20, 8, 276, 0});
    objects.push_back({104, WorldObjectKind::Structure, "", "player", "ALLYHQ", 1000, 4, 5, 164, 0});
    objects.push_back({105, WorldObjectKind::Infantry, "", "player", "E1", 61, 20, 9, 308, 0});
    objects.push_back({106, WorldObjectKind::Infantry, "", "enemy", "PSIIMMUNE", 100, 19, 8, 275, 0});
    objects.push_back({107, WorldObjectKind::Infantry, "", "enemy", "DOG", 100, 19, 9, 307, 0});
    objects.push_back({108, WorldObjectKind::Infantry, "", "enemy", "ROBOT", 100, 21, 8, 277, 0});
    objects.push_back({109, WorldObjectKind::Infantry, "", "enemy", "E1", 100, 21, 9, 309, 0});
    objects.back().airborne = true;
    objects.push_back({110, WorldObjectKind::Infantry, "", "enemy", "E1", 100, 20, 7, 244, 0});
    objects.push_back({111, WorldObjectKind::Structure, "", "ally", "ALLYHQ", 1000, 5, 5, 165, 0});
    objects.push_back({112, WorldObjectKind::Unit, "", "enemy", "TANK", 1000, 9, 10, 329, 0});
    objects.push_back({113, WorldObjectKind::Infantry, "", "enemy", "E1", 20, 15, 15, 495, 0});
    objects.push_back({114, WorldObjectKind::Infantry, "", "player", "E1", 100, 2, 4, 130, 0});
    objects.push_back({115, WorldObjectKind::Unit, "", "player", "TANK", 100, 5, 4, 133, 0});
    objects.push_back({116, WorldObjectKind::Unit, "", "enemy", "TANK", 1000, 21, 20, 661, 0});

    std::vector<SuperWeaponEvent> events;
    int nextObjectId = 1000;
    SuperWeaponWorldContext context;
    context.fixedTickHz = 30;
    context.mapWidth = 32;
    context.mapHeight = 32;
    context.objects = &objects;
    context.rules = &semanticRules;
    context.superWeaponsAllowed = true;
    context.areAllied = [](const std::string& left, const std::string& right) {
        const std::string leftKey = lowerAscii(left);
        const std::string rightKey = lowerAscii(right);
        return leftKey == rightKey
            || (leftKey == "player" && rightKey == "ally")
            || (leftKey == "ally" && rightKey == "player");
    };
    context.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
        return x >= 0 && y >= 0 && x < 32 && y < 32;
    };
    context.cellOccupied = [&](int x, int y) {
        for (const WorldObjectInstance& object : objects) {
            if (object.alive && object.x == x && object.y == y) {
                return true;
            }
        }
        return false;
    };
    context.ownerPowered = [](const std::string&) { return true; };
    context.geneticMutatorEligible = [](const WorldObjectInstance& object) {
        return object.instanceId != 110;
    };
    context.allocateObjectId = [&]() { return ++nextObjectId; };
    std::vector<std::pair<int, std::string>> createdObjects;
    std::vector<SuperWeaponObjectLifecycleEvent> damagedObjects;
    std::vector<SuperWeaponObjectLifecycleEvent> removedObjects;
    std::vector<SuperWeaponObjectLifecycleEvent> relocatedObjects;
    std::vector<SuperWeaponObjectLifecycleEvent> ownerChangedObjects;
    std::vector<SuperWeaponImpactEvent> impactEvents;
    std::vector<SuperWeaponActiveLifecycleEvent> activeLifecycleEvents;
    std::vector<std::pair<std::string, int>> lifecycleOrder;
    int nextCallbackSentinelId = -1000;
    context.onObjectCreated = [&](WorldObjectInstance& object) {
        const int createdId = object.instanceId;
        const std::string createdType = object.type;
        createdObjects.emplace_back(createdId, createdType);

        // Deliberately make back() refer to a different object and permit a
        // vector reallocation.  Creation paths must retain the created ID
        // before invoking this callback.
        WorldObjectInstance sentinel;
        sentinel.instanceId = nextCallbackSentinelId--;
        sentinel.type = "CALLBACK_SENTINEL";
        sentinel.health = 0;
        sentinel.alive = false;
        objects.push_back(std::move(sentinel));
    };
    context.onObjectDamaged = [&](SuperWeaponObjectLifecycleEvent event) {
        damagedObjects.push_back(std::move(event));
    };
    context.onObjectRemoved = [&](SuperWeaponObjectLifecycleEvent event) {
        removedObjects.push_back(std::move(event));
    };
    context.onObjectRelocated = [&](SuperWeaponObjectLifecycleEvent event) {
        lifecycleOrder.emplace_back("Relocated", event.objectId);
        relocatedObjects.push_back(std::move(event));
    };
    context.onObjectOwnerChanged = [&](SuperWeaponObjectLifecycleEvent event) {
        lifecycleOrder.emplace_back("OwnerChanged", event.objectId);
        ownerChangedObjects.push_back(std::move(event));
    };
    context.onSuperWeaponImpact = [&](SuperWeaponImpactEvent event) {
        impactEvents.push_back(std::move(event));
    };
    context.onSuperWeaponActiveLifecycle = [
        &activeLifecycleEvents
    ](SuperWeaponActiveLifecycleEvent event) {
        activeLifecycleEvents.push_back(std::move(event));
    };
    context.emit = [&](const SuperWeaponEvent& event) {
        if (event.kind == "ChronoWarpIn"
            || event.kind == "PsychicDominatorControlled") {
            lifecycleOrder.emplace_back(event.kind, event.objectId);
        }
        events.push_back(event);
    };

    {
        SuperWeaponRule voiceRule;
        voiceRule.id = "VoiceSpecial";
        voiceRule.uiName = "Name:VoiceSpecial";
        voiceRule.name = "Voice Test";
        voiceRule.type = "MultiMissile";
        voiceRule.action = "MultiMissile";
        voiceRule.targetMode = "Area";
        voiceRule.rechargeTimeMinutes = 2.0 / (60.0 * 30.0);
        voiceRule.rechargeVoice = "ReadyVoice";
        voiceRule.chargingVoice = "ChargingVoice";
        voiceRule.impatientVoice = "ImpatientVoice";
        voiceRule.suspendVoice = "SuspendVoice";
        voiceRule.range = 9.0;
        voiceRule.lineMultiplier = 0;
        voiceRule.flashSidebarTabFrames = 17;
        voiceRule.showTimer = true;

        SuperWeaponSystem voiceSystem;
        voiceSystem.reset(std::vector<SuperWeaponRule>{voiceRule});
        std::vector<SuperWeaponEvent> voiceEvents;
        std::vector<WorldObjectInstance> noVoiceTargets;
        bool voicePowered = true;
        SuperWeaponWorldContext voiceContext;
        voiceContext.fixedTickHz = 30;
        voiceContext.mapWidth = 16;
        voiceContext.mapHeight = 16;
        voiceContext.objects = &noVoiceTargets;
        voiceContext.ownerPowered = [&](const std::string&) {
            return voicePowered;
        };
        voiceContext.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
            return x >= 0 && y >= 0 && x < 16 && y < 16;
        };
        voiceContext.emit = [&](const SuperWeaponEvent& event) {
            voiceEvents.push_back(event);
        };

        std::string voiceReason;
        voiceContext.tick = 0;
        if (!voiceSystem.grant(
                "voice",
                "VoiceSpecial",
                -1,
                &voiceContext
            )) {
            std::cerr << "superweapon smoke: voice grant failed\n";
            return 1;
        }
        voiceSystem.tick(voiceContext);
        const SuperWeaponSidebarSnapshot* chargingSnapshot =
            voiceSystem.findSidebarSnapshot("voice", "VoiceSpecial");
        if (chargingSnapshot == nullptr
            || chargingSnapshot->uiName != "Name:VoiceSpecial"
            || chargingSnapshot->name != "Voice Test"
            || chargingSnapshot->state != SuperWeaponRuntimeState::Charging
            || chargingSnapshot->rechargeTicks != 2
            || chargingSnapshot->remainingTicks != 1
            || chargingSnapshot->stateStartTick != 0
            || chargingSnapshot->flashSidebarTabFrames != 17
            || std::abs(chargingSnapshot->targetRange - 9.0) > 0.0001
            || chargingSnapshot->targetLineMultiplier != 0
            || !chargingSnapshot->showTimer
            || chargingSnapshot->firstTargetAccepted) {
            std::cerr << "superweapon smoke: sidebar timer metadata mismatch\n";
            return 1;
        }
        if (voiceSystem.beginTargetSelection(
                "voice",
                "VoiceSpecial",
                voiceContext,
                &voiceReason
            )) {
            std::cerr << "superweapon smoke: charging click was accepted\n";
            return 1;
        }

        voicePowered = false;
        voiceContext.tick = 1;
        voiceSystem.tick(voiceContext);
        voiceContext.tick = 2;
        voiceSystem.tick(voiceContext);
        voicePowered = true;
        voiceContext.tick = 3;
        voiceSystem.tick(voiceContext);
        voiceContext.tick = 4;
        voiceSystem.tick(voiceContext);
        if (!voiceSystem.beginTargetSelection(
                "voice",
                "VoiceSpecial",
                voiceContext,
                &voiceReason
            )
            || !voiceSystem.submitTarget(
                "voice",
                "VoiceSpecial",
                {{3, 3}, -1},
                voiceContext,
                &voiceReason
            )) {
            std::cerr << "superweapon smoke: ready voice launch failed: "
                      << voiceReason << "\n";
            return 1;
        }
        const int impatientBeforeBlockedStates = static_cast<int>(
            std::count_if(
                voiceEvents.begin(),
                voiceEvents.end(),
                [](const SuperWeaponEvent& event) {
                    return event.kind == "SuperWeaponImpatient";
                }
            )
        );
        voiceContext.tick = 5;
        voiceSystem.tick(voiceContext);
        if (voiceSystem.beginTargetSelection(
                "voice",
                "VoiceSpecial",
                voiceContext,
                &voiceReason
            )
            || !voiceSystem.disable(
                "voice",
                "VoiceSpecial",
                &voiceContext
            )
            || voiceSystem.beginTargetSelection(
                "voice",
                "VoiceSpecial",
                voiceContext,
                &voiceReason
            )) {
            std::cerr << "superweapon smoke: active/unavailable click edge mismatch\n";
            return 1;
        }

        const auto voiceCount = [&](const std::string& intent) {
            return static_cast<int>(std::count_if(
                voiceEvents.begin(),
                voiceEvents.end(),
                [&](const SuperWeaponEvent& event) {
                    return event.evaIntent == intent;
                }
            ));
        };
        const int impatientAfterBlockedStates = static_cast<int>(
            std::count_if(
                voiceEvents.begin(),
                voiceEvents.end(),
                [](const SuperWeaponEvent& event) {
                    return event.kind == "SuperWeaponImpatient";
                }
            )
        );
        const bool voiceLeakedToSound = std::any_of(
            voiceEvents.begin(),
            voiceEvents.end(),
            [](const SuperWeaponEvent& event) {
                return event.soundIntent == "ChargingVoice"
                    || event.soundIntent == "SuspendVoice"
                    || event.soundIntent == "ReadyVoice"
                    || event.soundIntent == "ImpatientVoice";
            }
        );
        if (voiceCount("ChargingVoice") != 3
            || voiceCount("SuspendVoice") != 1
            || voiceCount("ReadyVoice") != 1
            || voiceCount("ImpatientVoice") != 1
            || impatientBeforeBlockedStates != 1
            || impatientAfterBlockedStates != impatientBeforeBlockedStates
            || voiceLeakedToSound) {
            std::cerr << "superweapon smoke: voice edge/dedup mismatch\n";
            return 1;
        }
    }

    {
        SuperWeaponRule primary;
        primary.id = "ChronoPrimarySpecial";
        primary.uiName = "Name:ChronoPrimary";
        primary.name = "Chrono Primary";
        primary.type = "ChronoPrimary";
        primary.action = "ChronoPrimary";
        primary.targetMode = "TwoStage";
        primary.preClick = true;
        primary.targetEnemy = false;
        primary.targetFriendly = true;
        primary.targetInfantry = true;
        primary.targetUnits = true;
        primary.targetBuildings = false;
        primary.range = 1.5;
        primary.lineMultiplier = 0;
        primary.showTimer = true;

        SuperWeaponRule helper;
        helper.id = "ChronoPostSpecial";
        helper.type = "ChronoWarp";
        helper.action = "ChronoWarp";
        helper.targetMode = "Cell";
        helper.postClick = true;
        // Match by the primary Type rather than its section ID.
        helper.preDependent = "ChronoPrimary";
        helper.targetEnemy = true;
        helper.targetFriendly = false;
        helper.targetInfantry = false;
        helper.targetUnits = false;
        helper.targetBuildings = true;
        helper.range = 8.0;
        helper.lineMultiplier = 4;

        std::vector<WorldObjectInstance> pairTargets;
        WorldObjectInstance firstTarget;
        firstTarget.instanceId = 701;
        firstTarget.kind = WorldObjectKind::Unit;
        firstTarget.owner = "player";
        firstTarget.type = "TANK";
        firstTarget.health = 100;
        firstTarget.x = 2;
        firstTarget.y = 2;
        firstTarget.alive = true;
        pairTargets.push_back(firstTarget);
        WorldObjectInstance secondTarget;
        secondTarget.instanceId = 702;
        secondTarget.kind = WorldObjectKind::Structure;
        secondTarget.owner = "enemy";
        secondTarget.type = "ENEMYHQ";
        secondTarget.health = 1000;
        secondTarget.x = 10;
        secondTarget.y = 10;
        secondTarget.alive = true;
        pairTargets.push_back(secondTarget);

        SuperWeaponWorldContext pairContext;
        pairContext.fixedTickHz = 30;
        pairContext.mapWidth = 16;
        pairContext.mapHeight = 16;
        pairContext.objects = &pairTargets;
        pairContext.areAllied = [](
            const std::string& left,
            const std::string& right
        ) {
            return lowerAscii(left) == lowerAscii(right);
        };
        pairContext.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
            return x >= 0 && y >= 0 && x < 16 && y < 16;
        };

        SuperWeaponSystem pairSystem;
        pairSystem.reset(std::vector<SuperWeaponRule>{primary, helper});
        if (!pairSystem.grant("player", primary.id)
            || !pairSystem.grant("player", helper.id)
            || !pairSystem.forceReady("player", primary.id)
            || !pairSystem.forceReady("player", helper.id)) {
            std::cerr << "superweapon smoke: pre/post grant failed\n";
            return 1;
        }
        pairSystem.tick(pairContext);
        if (pairSystem.sidebarSnapshots("player").size() != 1U
            || pairSystem.findSidebarSnapshot("player", helper.id) != nullptr) {
            std::cerr << "superweapon smoke: post-click helper leaked to sidebar\n";
            return 1;
        }

        std::string pairReason;
        if (!pairSystem.beginTargetSelection(
                "player",
                primary.id,
                pairContext,
                &pairReason
            )
            || !pairSystem.submitTarget(
                "player",
                primary.id,
                {{2, 2}, 701},
                pairContext,
                &pairReason
            )) {
            std::cerr << "superweapon smoke: first pre-click target failed: "
                      << pairReason << "\n";
            return 1;
        }
        const SuperWeaponSidebarSnapshot* secondStageSnapshot =
            pairSystem.findSidebarSnapshot("player", primary.id);
        if (secondStageSnapshot == nullptr
            || !secondStageSnapshot->firstTargetAccepted
            || secondStageSnapshot->firstTargetCell.x != 2
            || secondStageSnapshot->firstTargetCell.y != 2
            || std::abs(secondStageSnapshot->targetRange - 8.0) > 0.0001
            || secondStageSnapshot->targetLineMultiplier != 4
            || pairSystem.previewTarget(
                "player",
                primary.id,
                {{2, 2}, 701},
                pairContext,
                nullptr
            )
            || !pairSystem.submitTarget(
                "player",
                primary.id,
                {{10, 10}, 702},
                pairContext,
                &pairReason
            )) {
            std::cerr << "superweapon smoke: post-click helper targeting mismatch: "
                      << pairReason << "\n";
            return 1;
        }

        SuperWeaponRule explicitTwoStage = primary;
        explicitTwoStage.id = "ExplicitTwoStage";
        explicitTwoStage.type = "CustomTwoStage";
        explicitTwoStage.action = "CustomTwoStage";
        explicitTwoStage.preClick = false;
        SuperWeaponSystem fallbackSystem;
        fallbackSystem.reset(
            std::vector<SuperWeaponRule>{explicitTwoStage}
        );
        if (!fallbackSystem.grant("player", explicitTwoStage.id)
            || !fallbackSystem.forceReady("player", explicitTwoStage.id)
            || !fallbackSystem.beginTargetSelection(
                "player",
                explicitTwoStage.id,
                pairContext,
                &pairReason
            )
            || !fallbackSystem.submitTarget(
                "player",
                explicitTwoStage.id,
                {{2, 2}, 701},
                pairContext,
                &pairReason
            )
            || !fallbackSystem.submitTarget(
                "player",
                explicitTwoStage.id,
                {{2, 2}, 701},
                pairContext,
                &pairReason
            )) {
            std::cerr << "superweapon smoke: explicit two-stage fallback failed: "
                      << pairReason << "\n";
            return 1;
        }
    }

    {
        SuperWeaponRule threatRule;
        threatRule.id = "DefenseThreat";
        threatRule.type = "MultiMissile";
        threatRule.action = "MultiMissile";
        threatRule.targetMode = "Area";
        threatRule.aiDefendAgainst = true;
        threatRule.targetEnemy = true;
        threatRule.targetFriendly = false;

        SuperWeaponRule shieldRule;
        shieldRule.id = "DefenseShield";
        shieldRule.type = "ForceShield";
        shieldRule.action = "ForceShield";
        shieldRule.targetMode = "Area";
        shieldRule.targetEnemy = false;
        shieldRule.targetFriendly = true;
        shieldRule.targetInfantry = false;
        shieldRule.targetUnits = false;
        shieldRule.targetBuildings = true;

        std::vector<WorldObjectInstance> defenseTargets;
        WorldObjectInstance distantStructure;
        distantStructure.instanceId = 801;
        distantStructure.kind = WorldObjectKind::Structure;
        distantStructure.owner = "defender";
        distantStructure.type = "HQ";
        distantStructure.health = 1000;
        distantStructure.x = 2;
        distantStructure.y = 2;
        distantStructure.alive = true;
        defenseTargets.push_back(distantStructure);
        WorldObjectInstance nearestAlliedStructure = distantStructure;
        nearestAlliedStructure.instanceId = 802;
        nearestAlliedStructure.owner = "ally";
        nearestAlliedStructure.x = 12;
        nearestAlliedStructure.y = 10;
        defenseTargets.push_back(nearestAlliedStructure);
        WorldObjectInstance hostileStructure = distantStructure;
        hostileStructure.instanceId = 803;
        hostileStructure.owner = "attacker";
        hostileStructure.x = 13;
        hostileStructure.y = 10;
        defenseTargets.push_back(hostileStructure);

        std::vector<SuperWeaponEvent> defenseEvents;
        SuperWeaponWorldContext defenseContext;
        defenseContext.tick = 100;
        defenseContext.fixedTickHz = 30;
        defenseContext.mapWidth = 32;
        defenseContext.mapHeight = 32;
        defenseContext.objects = &defenseTargets;
        defenseContext.areAllied = [](
            const std::string& left,
            const std::string& right
        ) {
            const std::string leftKey = lowerAscii(left);
            const std::string rightKey = lowerAscii(right);
            return leftKey == rightKey
                || (leftKey == "defender" && rightKey == "ally")
                || (leftKey == "ally" && rightKey == "defender");
        };
        defenseContext.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
            return x >= 0 && y >= 0 && x < 32 && y < 32;
        };
        defenseContext.emit = [&](const SuperWeaponEvent& event) {
            defenseEvents.push_back(event);
        };

        SuperWeaponSystem defenseBase;
        defenseBase.reset(
            std::vector<SuperWeaponRule>{threatRule, shieldRule}
        );
        std::string defenseReason;
        if (!defenseBase.grant("attacker", threatRule.id)
            || !defenseBase.forceReady("attacker", threatRule.id)
            || !defenseBase.grant("defender", shieldRule.id)
            || !defenseBase.forceReady("defender", shieldRule.id)
            || !defenseBase.beginTargetSelection(
                "attacker",
                threatRule.id,
                defenseContext,
                &defenseReason
            )
            || !defenseBase.submitTarget(
                "attacker",
                threatRule.id,
                {{13, 10}, -1},
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense threat setup failed: "
                      << defenseReason << "\n";
            return 1;
        }

        SuperWeaponSystem zeroProbability = defenseBase;
        defenseEvents.clear();
        if (zeroProbability.fireAiDefense(
                "defender",
                {10, 10},
                0,
                50,
                12,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense accepted zero percent\n";
            return 1;
        }

        SuperWeaponSystem outsideDistance = defenseBase;
        defenseEvents.clear();
        if (outsideDistance.fireAiDefense(
                "defender",
                {10, 10},
                100,
                50,
                2,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense ignored distance\n";
            return 1;
        }

        SuperWeaponSystem boundaryWindow = defenseBase;
        defenseEvents.clear();
        defenseContext.tick = 200;
        if (!boundaryWindow.fireAiDefense(
                "defender",
                {10, 10},
                100,
                50,
                12,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense 15fps window failed: "
                      << defenseReason << "\n";
            return 1;
        }

        SuperWeaponSystem expiredWindow = defenseBase;
        defenseEvents.clear();
        defenseContext.tick = 201;
        if (expiredWindow.fireAiDefense(
                "defender",
                {10, 10},
                100,
                50,
                12,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: AI defense accepted expired alert\n";
            return 1;
        }

        SuperWeaponSystem deterministicLeft = defenseBase;
        SuperWeaponSystem deterministicRight = defenseBase;
        defenseContext.tick = 100;
        defenseEvents.clear();
        const bool leftRoll = deterministicLeft.fireAiDefense(
            "defender",
            {10, 10},
            37,
            50,
            12,
            defenseContext,
            &defenseReason
        );
        defenseEvents.clear();
        const bool rightRoll = deterministicRight.fireAiDefense(
            "defender",
            {10, 10},
            37,
            50,
            12,
            defenseContext,
            &defenseReason
        );
        if (leftRoll != rightRoll) {
            std::cerr << "superweapon smoke: AI defense roll was not deterministic\n";
            return 1;
        }

        SuperWeaponSystem activeThreat = defenseBase;
        defenseEvents.clear();
        defenseContext.tick = 101;
        activeThreat.tick(defenseContext);
        defenseEvents.clear();
        if (!activeThreat.fireAiDefense(
                "defender",
                {10, 10},
                100,
                50,
                12,
                defenseContext,
                &defenseReason
            )) {
            std::cerr << "superweapon smoke: active AI defense failed: "
                      << defenseReason << "\n";
            return 1;
        }
        const auto defenseTriggered = std::find_if(
            defenseEvents.begin(),
            defenseEvents.end(),
            [](const SuperWeaponEvent& event) {
                return event.kind == "SuperWeaponAIDefenseTriggered";
            }
        );
        const SuperWeaponSidebarSnapshot* defenseSnapshot =
            activeThreat.findSidebarSnapshot("defender", shieldRule.id);
        if (defenseTriggered == defenseEvents.end()
            || defenseTriggered->targetObjectId != 802
            || defenseTriggered->targetCell.x != 12
            || defenseTriggered->targetCell.y != 10
            || defenseSnapshot == nullptr
            || defenseSnapshot->state != SuperWeaponRuntimeState::Launching) {
            std::cerr << "superweapon smoke: AI defense target/event mismatch\n";
            return 1;
        }

        // No-cooldown defense submissions reallocate/sort the instance
        // vector; both event identities must still refer to the actual cast.
        SuperWeaponSystem repeatDefense = defenseBase;
        defenseContext.noCooldown = [](const std::string& owner) {
            return owner == "defender";
        };
        std::set<int> defenseCastIds;
        for (int use = 0; use < 3; ++use) {
            defenseEvents.clear();
            if (!repeatDefense.fireAiDefense("defender", {10, 10}, 100, 50,
                    12, defenseContext, &defenseReason)) {
                std::cerr << "superweapon smoke: repeat AI defense failed: "
                          << defenseReason << "\n";
                return 1;
            }
            const auto event = std::find_if(defenseEvents.begin(), defenseEvents.end(),
                [](const auto& entry) { return entry.kind == "SuperWeaponAIDefenseTriggered"; });
            if (event == defenseEvents.end() || event->owner != "defender"
                || event->superWeaponId != shieldRule.id || event->objectId != 1
                || event->targetObjectId != 802 || event->targetCell.x != 12
                || event->targetCell2.x != 13 || !defenseCastIds.insert(event->instanceId).second
                || repeatDefense.stateOf("defender", shieldRule.id) != SuperWeaponRuntimeState::Ready) {
                std::cerr << "superweapon smoke: repeat AI defense identity mismatch\n";
                return 1;
            }
        }
    }

    {
        SuperWeaponSystem stealthAi;
        stealthAi.reset(std::vector<SuperWeaponRule>{rules.front()});
        std::vector<WorldObjectInstance> stealthTargets;
        WorldObjectInstance visibleTarget;
        visibleTarget.instanceId = 900;
        visibleTarget.kind = WorldObjectKind::Unit;
        visibleTarget.owner = "player";
        visibleTarget.type = "TANK";
        visibleTarget.health = 100;
        visibleTarget.x = 2;
        visibleTarget.y = 2;
        visibleTarget.alive = true;
        stealthTargets.push_back(visibleTarget);
        WorldObjectInstance hiddenTarget;
        hiddenTarget.instanceId = 901;
        hiddenTarget.kind = WorldObjectKind::Structure;
        hiddenTarget.owner = "player";
        hiddenTarget.type = "HIDDENHQ";
        hiddenTarget.health = 2000;
        hiddenTarget.x = 20;
        hiddenTarget.y = 20;
        hiddenTarget.alive = true;
        stealthTargets.push_back(hiddenTarget);
        WorldObjectInstance cloakedTarget = hiddenTarget;
        cloakedTarget.instanceId = 902;
        cloakedTarget.type = "CLOAKEDHQ";
        cloakedTarget.health = 3000;
        cloakedTarget.x = 25;
        cloakedTarget.y = 25;
        cloakedTarget.cloaked = true;
        stealthTargets.push_back(cloakedTarget);

        std::vector<SuperWeaponEvent> stealthEvents;
        SuperWeaponWorldContext stealthContext = context;
        stealthContext.objects = &stealthTargets;
        stealthContext.aiTargetVisible = [](
            const std::string&,
            const WorldObjectInstance& object
        ) {
            return object.instanceId != 901 && !object.cloaked;
        };
        stealthContext.emit = [&stealthEvents](const SuperWeaponEvent& event) {
            stealthEvents.push_back(event);
        };
        std::string reason;
        if (!stealthAi.grant("enemy", "NukeSpecial")
            || !stealthAi.forceReady("enemy", "NukeSpecial")
            || !stealthAi.fireAi(
                "enemy",
                "NukeSpecial",
                stealthContext,
                &reason
            )) {
            std::cerr << "superweapon smoke: stealth AI setup failed: "
                      << reason << "\n";
            return 1;
        }
        const auto accepted = std::find_if(
            stealthEvents.rbegin(),
            stealthEvents.rend(),
            [](const SuperWeaponEvent& event) {
                return event.kind == "SuperWeaponCommandAccepted";
            }
        );
        if (accepted == stealthEvents.rend()
            || accepted->targetObjectId != visibleTarget.instanceId
            || accepted->targetCell.x != visibleTarget.x
            || accepted->targetCell.y != visibleTarget.y) {
            std::cerr << "superweapon smoke: AI targeted a cloaked hostile\n";
            return 1;
        }
    }

    for (const SuperWeaponRule& rule : rules) {
        if (!system.grant("player", rule.id)) {
            std::cerr << "superweapon smoke: grant failed for " << rule.id << "\n";
            return 1;
        }
        if (!system.forceReady("player", rule.id)) {
            std::cerr << "superweapon smoke: ready failed for " << rule.id << "\n";
            return 1;
        }
    }
    {
        std::string reason;
        if (!system.beginTargetSelection(
                "player",
                "SpyPlaneSpecial",
                context,
                &reason
            )) {
            std::cerr << "superweapon smoke: immediate selection failed: "
                      << reason << "\n";
            return 1;
        }
        const SuperWeaponSidebarSnapshot* selected =
            system.findSidebarSnapshot("player", "SpyPlaneSpecial");
        if (selected == nullptr || !selected->targetSelectionActive) {
            std::cerr << "superweapon smoke: selection snapshot was stale\n";
            return 1;
        }
        if (!system.cancelTargetSelection(
                "player",
                "SpyPlaneSpecial",
                context,
                "smoke right-click"
            )) {
            std::cerr << "superweapon smoke: immediate cancel failed\n";
            return 1;
        }
        const SuperWeaponSidebarSnapshot* canceled =
            system.findSidebarSnapshot("player", "SpyPlaneSpecial");
        if (canceled == nullptr
            || canceled->targetSelectionActive
            || canceled->state != SuperWeaponRuntimeState::Ready) {
            std::cerr << "superweapon smoke: cancel snapshot was stale\n";
            return 1;
        }
    }
    {
        std::string reason;
        if (!system.previewTarget(
                "player",
                "ForceShieldSpecial",
                {{4, 4}, 102},
                context,
                &reason
            )
            || !system.previewTarget(
                "player",
                "ForceShieldSpecial",
                {{4, 5}, -1},
                context,
                &reason
            )
            || !system.previewTarget(
                "player",
                "ForceShieldSpecial",
                {{4, 5}, 104},
                context,
                &reason
            )
            || !system.previewTarget(
                "player",
                "GeneticConverterSpecial",
                {{20, 9}, 105},
                context,
                &reason
            )) {
            std::cerr << "superweapon smoke: stock target validation mismatch: "
                      << reason << "\n";
            return 1;
        }
    }
    auto fire = [&](const std::string& id, const SuperWeaponTarget& target) {
        std::string reason;
        if (!system.beginTargetSelection("player", id, context, &reason)) {
            std::cerr << "superweapon smoke: fire failed for " << id
                      << ": " << reason << "\n";
            return false;
        }
        const SuperWeaponSidebarSnapshot* selected =
            system.findSidebarSnapshot("player", id);
        if (selected == nullptr || !selected->targetSelectionActive) {
            std::cerr << "superweapon smoke: active snapshot missing for "
                      << id << "\n";
            return false;
        }
        if (!system.submitTarget("player", id, target, context, &reason)) {
            std::cerr << "superweapon smoke: fire failed for " << id
                      << ": " << reason << "\n";
            return false;
        }
        const SuperWeaponSidebarSnapshot* submitted =
            system.findSidebarSnapshot("player", id);
        if (submitted == nullptr || submitted->targetSelectionActive) {
            std::cerr << "superweapon smoke: submitted snapshot stale for "
                      << id << "\n";
            return false;
        }
        return true;
    };
    if (!fire("NukeSpecial", {{10, 10}, 100})
        || !fire("LightningStormSpecial", {{15, 15}, 113})
        || !fire("IronCurtainSpecial", {{4, 4}, 102})
        || !fire("ForceShieldSpecial", {{4, 5}, 104})
        || !fire("PsychicDominatorSpecial", {{10, 10}, 100})
        || !fire("GeneticConverterSpecial", {{20, 8}, 103})
        || !fire("ParaDropSpecial", {{16, 16}, -1})
        || !fire("SpyPlaneSpecial", {{16, 16}, -1})
        || !fire("PsychicRevealSpecial", {{16, 16}, -1})) {
        return 1;
    }
    {
        std::string reason;
        if (!system.beginTargetSelection("player", "ChronoSphereSpecial", context, &reason)
            || !system.submitTarget("player", "ChronoSphereSpecial", {{4, 4}, 102}, context, &reason)
            || !system.submitTarget("player", "ChronoSphereSpecial", {{20, 20}, -1}, context, &reason)) {
            std::cerr << "superweapon smoke: chrono failed: " << reason << "\n";
            return 1;
        }
    }
    for (int tick = 1; tick <= 140; ++tick) {
        context.tick = tick;
        system.tick(context);
    }
    const auto objectById = [&](int objectId) -> const WorldObjectInstance* {
        const auto found = std::find_if(
            objects.begin(),
            objects.end(),
            [objectId](const WorldObjectInstance& object) {
                return object.instanceId == objectId;
            }
        );
        return found == objects.end() ? nullptr : &*found;
    };
    const WorldObjectInstance* oldEnemyHuman = objectById(103);
    const WorldObjectInstance* oldFriendlyHuman = objectById(105);
    const WorldObjectInstance* immuneInfantry = objectById(106);
    const WorldObjectInstance* notHumanInfantry = objectById(107);
    const WorldObjectInstance* nonOrganicInfantry = objectById(108);
    const WorldObjectInstance* airborneInfantry = objectById(109);
    const WorldObjectInstance* vetoedInfantry = objectById(110);
    const WorldObjectInstance* radiationTarget = objectById(112);
    std::vector<const WorldObjectInstance*> createdBrutes;
    for (const WorldObjectInstance& object : objects) {
        if (object.alive && object.type == "BRUTE") {
            createdBrutes.push_back(&object);
        }
    }
    std::sort(
        createdBrutes.begin(),
        createdBrutes.end(),
        [](const WorldObjectInstance* left, const WorldObjectInstance* right) {
            return left->health < right->health;
        }
    );
    if (objects[0].alive || objects[2].x != 20 || objects[2].y != 20
        || oldEnemyHuman == nullptr || oldEnemyHuman->alive
        || oldFriendlyHuman == nullptr || oldFriendlyHuman->alive
        || createdBrutes.size() != 3
        || createdBrutes[0]->owner != "player" || createdBrutes[0]->health != 200
        || createdBrutes[1]->owner != "player" || createdBrutes[1]->health != 200
        || createdBrutes[2]->owner != "player" || createdBrutes[2]->health != 200
        || immuneInfantry == nullptr || immuneInfantry->alive
        || notHumanInfantry == nullptr || notHumanInfantry->alive
        || nonOrganicInfantry == nullptr || !nonOrganicInfantry->alive
        || airborneInfantry == nullptr || !airborneInfantry->alive
        || vetoedInfantry == nullptr || !vetoedInfantry->alive
        || radiationTarget == nullptr || !radiationTarget->alive
        || radiationTarget->health >= 800) {
        std::cerr << "superweapon smoke: effect assertion failed"
                  << " nukeAlive=" << objects[0].alive
                  << " chrono=" << objects[2].x << "," << objects[2].y
                  << " brutes=" << createdBrutes.size() << "\n";
        return 1;
    }
    bool sawStorm = false;
    bool sawDrop = false;
    bool sawReveal = false;
    bool protectedPlayerBuilding = false;
    bool protectedAlliedBuilding = false;
    bool protectedNonBuilding = false;
    int mutationEvents = 0;
    int radiationDamageEvents = 0;
    for (const SuperWeaponEvent& event : events) {
        sawStorm = sawStorm || event.kind == "LightningBolt";
        sawDrop = sawDrop || event.kind == "ParadropInfantryDropped";
        sawReveal = sawReveal || event.kind == "SpyPlaneRevealArea"
            || event.kind == "PsychicRevealArea";
        if (event.kind == "ForceShieldProtected") {
            protectedPlayerBuilding = protectedPlayerBuilding
                || event.objectId == 104;
            protectedAlliedBuilding = protectedAlliedBuilding
                || event.objectId == 111;
            protectedNonBuilding = protectedNonBuilding
                || event.objectId == 102;
        }
        if (event.kind == "GeneticMutationApplied") {
            ++mutationEvents;
        }
        if (event.kind == "RadiationDamage" && event.objectId == 112) {
            ++radiationDamageEvents;
        }
    }
    const bool sawParadropPlaneLifecycle = std::any_of(
        createdObjects.begin(),
        createdObjects.end(),
        [](const auto& created) { return created.second == "DROPPLANE"; }
    );
    const auto createdPlane = std::find_if(
        createdObjects.begin(),
        createdObjects.end(),
        [](const auto& created) { return created.second == "DROPPLANE"; }
    );
    const bool stableParadropPlaneId = createdPlane != createdObjects.end()
        && std::any_of(
            events.begin(),
            events.end(),
            [&createdPlane](const SuperWeaponEvent& event) {
                return event.kind == "ParadropAircraftLaunched"
                    && event.objectId == createdPlane->first;
            }
        );
    int stableParadropInfantryIds = 0;
    for (const SuperWeaponEvent& event : events) {
        if (event.kind != "ParadropInfantryDropped") {
            continue;
        }
        const bool matchesCreatedInfantry = std::any_of(
            createdObjects.begin(),
            createdObjects.end(),
            [&event](const auto& created) {
                return created.first == event.objectId && created.second == "E1";
            }
        );
        stableParadropInfantryIds += matchesCreatedInfantry ? 1 : 0;
    }
    const bool sawDamageLifecycle = std::any_of(
        damagedObjects.begin(),
        damagedObjects.end(),
        [](const SuperWeaponObjectLifecycleEvent& damaged) {
            return damaged.kind == SuperWeaponObjectLifecycleKind::Damaged
                && damaged.objectId == 100
                && damaged.healthBefore > damaged.healthAfter
                && damaged.sourceOwner == "player"
                && damaged.superWeaponId == "NukeSpecial"
                && damaged.superWeaponInstanceId > 0
                && damaged.warheadId == "NUKE"
                && damaged.reason == "superweapon radial damage";
        }
    );
    const auto sawRemoval = [&removedObjects](
        int objectId,
        const std::string& superWeaponId,
        SuperWeaponObjectRemovalDisposition removalDisposition,
        const std::string& reason
    ) {
        return std::any_of(
            removedObjects.begin(),
            removedObjects.end(),
            [objectId, &superWeaponId, removalDisposition, &reason](
                const SuperWeaponObjectLifecycleEvent& removed
            ) {
                return removed.kind == SuperWeaponObjectLifecycleKind::Removed
                    && removed.removalDisposition == removalDisposition
                    && removed.objectId == objectId
                    && removed.healthBefore > removed.healthAfter
                    && removed.healthAfter == 0
                    && removed.sourceOwner == "player"
                    && removed.superWeaponId == superWeaponId
                    && removed.superWeaponInstanceId > 0
                    && removed.reason == reason;
            }
        );
    };
    const auto allHaveNoRemovalDisposition = [](
        const std::vector<SuperWeaponObjectLifecycleEvent>& lifecycleEvents
    ) {
        return std::all_of(
            lifecycleEvents.begin(),
            lifecycleEvents.end(),
            [](const SuperWeaponObjectLifecycleEvent& event) {
                return event.kind != SuperWeaponObjectLifecycleKind::Removed
                    && event.removalDisposition
                        == SuperWeaponObjectRemovalDisposition::None;
            }
        );
    };
    const bool lifecycleDispositionsAreExplicit = std::all_of(
            removedObjects.begin(),
            removedObjects.end(),
            [](const SuperWeaponObjectLifecycleEvent& event) {
                return event.kind == SuperWeaponObjectLifecycleKind::Removed
                    && event.removalDisposition
                        != SuperWeaponObjectRemovalDisposition::None;
            }
        )
        && allHaveNoRemovalDisposition(damagedObjects)
        && allHaveNoRemovalDisposition(relocatedObjects)
        && allHaveNoRemovalDisposition(ownerChangedObjects);
    const bool sawRelocationLifecycle = std::any_of(
        relocatedObjects.begin(),
        relocatedObjects.end(),
        [](const SuperWeaponObjectLifecycleEvent& relocated) {
            return relocated.kind == SuperWeaponObjectLifecycleKind::Relocated
                && relocated.objectId == 102
                && relocated.oldCell == SuperWeaponCell{4, 4}
                && relocated.newCell == SuperWeaponCell{20, 20}
                && relocated.sourceOwner == "player"
                && relocated.superWeaponId == "ChronoSphereSpecial"
                && relocated.superWeaponInstanceId > 0
                && relocated.reason == "chronosphere formation transfer";
        }
    );
    const bool sawOwnerLifecycle = std::any_of(
        ownerChangedObjects.begin(),
        ownerChangedObjects.end(),
        [](const SuperWeaponObjectLifecycleEvent& changed) {
            return changed.kind == SuperWeaponObjectLifecycleKind::OwnerChanged
                && changed.objectId == 100
                && changed.oldOwner == "enemy"
                && changed.newOwner == "player"
                && changed.sourceOwner == "player"
                && changed.superWeaponId == "PsychicDominatorSpecial"
                && changed.superWeaponInstanceId > 0
                && changed.reason == "psychic dominator control";
        }
    );
    const auto lifecycleIndex = [&lifecycleOrder](
        const std::string& kind,
        int objectId
    ) {
        const auto found = std::find(
            lifecycleOrder.begin(),
            lifecycleOrder.end(),
            std::make_pair(kind, objectId)
        );
        return static_cast<std::size_t>(
            std::distance(lifecycleOrder.begin(), found)
        );
    };
    const std::size_t relocationCallbackIndex = lifecycleIndex("Relocated", 102);
    const std::size_t relocationEventIndex = lifecycleIndex("ChronoWarpIn", 102);
    const std::size_t ownerCallbackIndex = lifecycleIndex("OwnerChanged", 100);
    const std::size_t ownerEventIndex = lifecycleIndex(
        "PsychicDominatorControlled",
        100
    );
    const bool lifecycleCallbackOrder =
        relocationCallbackIndex < relocationEventIndex
        && relocationEventIndex < lifecycleOrder.size()
        && ownerCallbackIndex < ownerEventIndex
        && ownerEventIndex < lifecycleOrder.size();
    const auto impactCount = [&impactEvents](const std::string& kind) {
        return static_cast<int>(std::count_if(
            impactEvents.begin(),
            impactEvents.end(),
            [&kind](const SuperWeaponImpactEvent& event) {
                return event.kind == kind;
            }
        ));
    };
    const auto nukeImpact = std::find_if(
        impactEvents.begin(),
        impactEvents.end(),
        [](const SuperWeaponImpactEvent& event) {
            return event.kind == "NukeImpact";
        }
    );
    const auto dominatorImpact = std::find_if(
        impactEvents.begin(),
        impactEvents.end(),
        [](const SuperWeaponImpactEvent& event) {
            return event.kind == "PsychicDominatorImpact";
        }
    );
    const int lightningBoltEvents = static_cast<int>(std::count_if(
        events.begin(),
        events.end(),
        [](const SuperWeaponEvent& event) {
            return event.kind == "LightningBolt";
        }
    ));
    const bool impactPayloadsAreStable = impactCount("NukeImpact") == 1
        && nukeImpact != impactEvents.end()
        && nukeImpact->instanceId > 0
        && nukeImpact->superWeaponId == "NukeSpecial"
        && nukeImpact->superWeaponType == "MultiMissile"
        && nukeImpact->sourceOwner == "player"
        && nukeImpact->sourceObjectId == -1
        && nukeImpact->cell == SuperWeaponCell{10, 10}
        && nukeImpact->radius == 2
        && nukeImpact->damage == 600
        && nukeImpact->warheadId == "NUKE"
        && nukeImpact->animationId == "NUKEANIM"
        && impactCount("LightningBolt") == lightningBoltEvents
        && lightningBoltEvents > 0
        && impactCount("PsychicDominatorImpact") == 1
        && dominatorImpact != impactEvents.end()
        && dominatorImpact->activeToken > 0
        && dominatorImpact->warheadId == "PSYWH"
        && dominatorImpact->animationId == "PDFXCLD";
    const bool perTargetIntentsAreClean = std::all_of(
        events.begin(),
        events.end(),
        [](const SuperWeaponEvent& event) {
            if (event.kind != "NukeDamage"
                && event.kind != "LightningBoltDamage"
                && event.kind != "RadiationDamage"
                && event.kind != "PsychicDominatorControlled") {
                return true;
            }
            return event.soundIntent.empty() && event.animationIntent.empty();
        }
    );
    const auto lifecyclePairIsStable = [
        &activeLifecycleEvents,
        &events
    ](const std::string& id, const std::string& startKind, const std::string& endKind) {
        const auto started = std::find_if(
            activeLifecycleEvents.begin(),
            activeLifecycleEvents.end(),
            [&id](const SuperWeaponActiveLifecycleEvent& event) {
                return event.superWeaponId == id
                    && event.phase == SuperWeaponActiveLifecyclePhase::Started;
            }
        );
        const auto ended = std::find_if(
            activeLifecycleEvents.begin(),
            activeLifecycleEvents.end(),
            [&id](const SuperWeaponActiveLifecycleEvent& event) {
                return event.superWeaponId == id
                    && event.phase == SuperWeaponActiveLifecyclePhase::Ended;
            }
        );
        if (started == activeLifecycleEvents.end()
            || ended == activeLifecycleEvents.end()
            || started->activeToken <= 0
            || started->activeToken != ended->activeToken
            || started->instanceId != ended->instanceId
            || started->startedTick != ended->startedTick
            || started->endsTick != ended->endsTick
            || started->durationTicks <= 0
            || ended->tick != ended->endsTick) {
            return false;
        }
        const bool genericStarted = std::any_of(
            events.begin(),
            events.end(),
            [&](const SuperWeaponEvent& event) {
                return event.kind == startKind
                    && event.instanceId == started->instanceId
                    && event.activeToken == started->activeToken;
            }
        );
        const bool genericEnded = std::any_of(
            events.begin(),
            events.end(),
            [&](const SuperWeaponEvent& event) {
                return event.kind == endKind
                    && event.instanceId == ended->instanceId
                    && event.activeToken == ended->activeToken;
            }
        );
        return genericStarted && genericEnded;
    };
    const bool stableActiveLifecycles = lifecyclePairIsStable(
            "LightningStormSpecial",
            "LightningStormStarted",
            "LightningStormEnded"
        )
        && lifecyclePairIsStable(
            "PsychicDominatorSpecial",
            "PsychicDominatorStarted",
            "PsychicDominatorEnded"
        );
    if (!sawStorm || !sawDrop || !sawReveal
        || !protectedPlayerBuilding || !protectedAlliedBuilding
        || protectedNonBuilding || mutationEvents != 3
        || radiationDamageEvents <= 0
        || !sawParadropPlaneLifecycle || !stableParadropPlaneId
        || stableParadropInfantryIds != 2
        || !sawDamageLifecycle
        || !sawRemoval(
            100,
            "NukeSpecial",
            SuperWeaponObjectRemovalDisposition::Destroyed,
            "superweapon radial damage"
        )
        || !sawRemoval(
            113,
            "LightningStormSpecial",
            SuperWeaponObjectRemovalDisposition::Destroyed,
            "superweapon radial damage"
        )
        || !sawRemoval(
            114,
            "IronCurtainSpecial",
            SuperWeaponObjectRemovalDisposition::Destroyed,
            "iron curtain organic destruction"
        )
        || !sawRemoval(
            115,
            "ChronoSphereSpecial",
            SuperWeaponObjectRemovalDisposition::Destroyed,
            "chronosphere blocked destination"
        )
        || !sawRemoval(
            103,
            "GeneticConverterSpecial",
            SuperWeaponObjectRemovalDisposition::TransformSource,
            "genetic mutation replacement"
        )
        || !sawRemoval(
            105,
            "GeneticConverterSpecial",
            SuperWeaponObjectRemovalDisposition::TransformSource,
            "genetic mutation replacement"
        )
        || !lifecycleDispositionsAreExplicit
        || !sawRelocationLifecycle || !sawOwnerLifecycle
        || !lifecycleCallbackOrder
        || !impactPayloadsAreStable || !perTargetIntentsAreClean
        || !stableActiveLifecycles) {
        std::cerr << "superweapon smoke: missing independent effect events\n";
        return 1;
    }
    const std::uint64_t beforeSaveHash = system.stateHash();
    std::ostringstream saved;
    system.save(saved);
    IniFile savedIni;
    if (!savedIni.loadFromString(saved.str())) {
        std::cerr << "superweapon smoke: save parse failed\n";
        return 1;
    }
    SuperWeaponSystem restored;
    restored.reset(rules);
    std::string loadError;
    if (!restored.load(savedIni, &loadError)
        || restored.stateHash() != beforeSaveHash) {
        std::cerr << "superweapon smoke: save/load mismatch " << loadError << "\n";
        return 1;
    }

    // A Dominator lifecycle exists independently of captured targets. Two
    // owners may overlap, each activation must keep a distinct stable token,
    // and a mid-active save must resume with only the pending Ended callback.
    {
        const auto dominatorRule = std::find_if(
            rules.begin(),
            rules.end(),
            [](const SuperWeaponRule& rule) {
                return rule.id == "PsychicDominatorSpecial";
            }
        );
        if (dominatorRule == rules.end()) {
            std::cerr << "superweapon smoke: dominator rule missing\n";
            return 1;
        }
        std::vector<SuperWeaponRule> lifecycleRules{*dominatorRule};
        lifecycleRules.front().durationFrames = 15;
        SuperWeaponSystem lifecycleSystem;
        lifecycleSystem.reset(lifecycleRules);
        std::vector<WorldObjectInstance> noTargets;
        std::vector<SuperWeaponImpactEvent> lifecycleImpacts;
        std::vector<SuperWeaponActiveLifecycleEvent> lifecycleCallbacks;
        std::vector<SuperWeaponEvent> lifecycleGenericEvents;
        SuperWeaponWorldContext lifecycleContext;
        lifecycleContext.fixedTickHz = 30;
        lifecycleContext.mapWidth = 32;
        lifecycleContext.mapHeight = 32;
        lifecycleContext.objects = &noTargets;
        lifecycleContext.cellLegal = [](int x, int y, SuperWeaponTargetMode) {
            return x >= 0 && y >= 0 && x < 32 && y < 32;
        };
        lifecycleContext.areAllied = [](
            const std::string& left,
            const std::string& right
        ) { return lowerAscii(left) == lowerAscii(right); };
        lifecycleContext.ownerPowered = [](const std::string&) { return true; };
        lifecycleContext.onSuperWeaponImpact = [
            &lifecycleImpacts
        ](SuperWeaponImpactEvent event) {
            lifecycleImpacts.push_back(std::move(event));
        };
        lifecycleContext.onSuperWeaponActiveLifecycle = [
            &lifecycleCallbacks
        ](SuperWeaponActiveLifecycleEvent event) {
            lifecycleCallbacks.push_back(std::move(event));
        };
        lifecycleContext.emit = [
            &lifecycleGenericEvents
        ](const SuperWeaponEvent& event) {
            lifecycleGenericEvents.push_back(event);
        };
        const auto armDominator = [
            &lifecycleSystem,
            &lifecycleContext
        ](const std::string& owner, const SuperWeaponCell& cell) {
            std::string reason;
            return lifecycleSystem.grant(
                    owner,
                    "PsychicDominatorSpecial"
                )
                && lifecycleSystem.forceReady(
                    owner,
                    "PsychicDominatorSpecial"
                )
                && lifecycleSystem.beginTargetSelection(
                    owner,
                    "PsychicDominatorSpecial",
                    lifecycleContext,
                    &reason
                )
                && lifecycleSystem.submitTarget(
                    owner,
                    "PsychicDominatorSpecial",
                    {cell, -1},
                    lifecycleContext,
                    &reason
                );
        };
        if (!armDominator("alpha", {6, 6})
            || !armDominator("beta", {20, 20})) {
            std::cerr << "superweapon smoke: overlapping dominator setup failed\n";
            return 1;
        }
        lifecycleContext.tick = 1;
        lifecycleSystem.tick(lifecycleContext);
        std::vector<int> startedTokens;
        std::vector<int> startedInstances;
        for (const SuperWeaponActiveLifecycleEvent& event : lifecycleCallbacks) {
            if (event.phase != SuperWeaponActiveLifecyclePhase::Started) {
                continue;
            }
            startedTokens.push_back(event.activeToken);
            startedInstances.push_back(event.instanceId);
            if (event.startedTick != 1 || event.endsTick != 31
                || event.durationTicks != 30
                || event.sourceObjectId != -1
                || event.superWeaponType != "PsychicDominator") {
                std::cerr << "superweapon smoke: dominator lifecycle payload mismatch\n";
                return 1;
            }
        }
        std::sort(startedTokens.begin(), startedTokens.end());
        std::sort(startedInstances.begin(), startedInstances.end());
        if (!noTargets.empty() || !lifecycleImpacts.empty()
            || startedTokens.size() != 2U
            || startedTokens[0] <= 0 || startedTokens[0] == startedTokens[1]
            || startedInstances[0] <= 0
            || startedInstances[0] == startedInstances[1]
            || lifecycleSystem.stateOf(
                "alpha",
                "PsychicDominatorSpecial"
            ) != SuperWeaponRuntimeState::Active
            || lifecycleSystem.stateOf(
                "beta",
                "PsychicDominatorSpecial"
            ) != SuperWeaponRuntimeState::Active) {
            std::cerr << "superweapon smoke: zero-target/overlap lifecycle mismatch\n";
            return 1;
        }
        const auto sameActiveSnapshots = [](
            const std::vector<SuperWeaponActiveLifecycleSnapshot>& left,
            const std::vector<SuperWeaponActiveLifecycleSnapshot>& right
        ) {
            if (left.size() != right.size()) {
                return false;
            }
            for (std::size_t index = 0; index < left.size(); ++index) {
                const SuperWeaponActiveLifecycleSnapshot& a = left[index];
                const SuperWeaponActiveLifecycleSnapshot& b = right[index];
                if (a.instanceId != b.instanceId
                    || a.activeToken != b.activeToken
                    || a.superWeaponId != b.superWeaponId
                    || a.superWeaponType != b.superWeaponType
                    || a.sourceOwner != b.sourceOwner
                    || a.sourceObjectId != b.sourceObjectId
                    || a.cell != b.cell
                    || a.startedTick != b.startedTick
                    || a.activeUntilTick != b.activeUntilTick) {
                    return false;
                }
            }
            return true;
        };
        const std::uint64_t beforeSnapshotHash = lifecycleSystem.stateHash();
        const auto activeSnapshots = lifecycleSystem.activeLifecycleSnapshots();
        bool snapshotPayloadsMatch = activeSnapshots.size() == 2U;
        for (std::size_t index = 0;
             snapshotPayloadsMatch && index < activeSnapshots.size();
             ++index) {
            const SuperWeaponActiveLifecycleSnapshot& snapshot =
                activeSnapshots[index];
            snapshotPayloadsMatch = snapshot.activeToken == startedTokens[index]
                && snapshot.instanceId > 0
                && snapshot.superWeaponId == "PsychicDominatorSpecial"
                && snapshot.superWeaponType == "PsychicDominator"
                && (snapshot.sourceOwner == "alpha"
                    || snapshot.sourceOwner == "beta")
                && snapshot.sourceObjectId == -1
                && snapshot.startedTick == 1
                && snapshot.activeUntilTick == 31;
        }
        auto detachedSnapshots = activeSnapshots;
        if (!detachedSnapshots.empty()) {
            detachedSnapshots.front().activeToken = -1;
            detachedSnapshots.front().superWeaponType.clear();
        }
        const auto activeSnapshotsAgain =
            lifecycleSystem.activeLifecycleSnapshots();
        if (!snapshotPayloadsMatch
            || !sameActiveSnapshots(activeSnapshots, activeSnapshotsAgain)
            || lifecycleSystem.stateHash() != beforeSnapshotHash) {
            std::cerr << "superweapon smoke: active snapshot value semantics mismatch\n";
            return 1;
        }
        for (int tick = 2; tick <= 5; ++tick) {
            lifecycleContext.tick = tick;
            lifecycleSystem.tick(lifecycleContext);
        }
        const std::uint64_t midActiveHash = lifecycleSystem.stateHash();
        std::ostringstream midActiveSave;
        lifecycleSystem.save(midActiveSave);
        IniFile midActiveIni;
        if (!midActiveIni.loadFromString(midActiveSave.str())) {
            std::cerr << "superweapon smoke: mid-active save parse failed\n";
            return 1;
        }
        SuperWeaponSystem lifecycleRestored;
        lifecycleRestored.reset(lifecycleRules);
        if (!lifecycleRestored.load(midActiveIni, &loadError)
            || lifecycleRestored.stateHash() != midActiveHash
            || !sameActiveSnapshots(
                activeSnapshots,
                lifecycleRestored.activeLifecycleSnapshots()
            )) {
            std::cerr << "superweapon smoke: mid-active roundtrip mismatch "
                      << loadError << "\n";
            return 1;
        }

        // Versions 1-3 applied Dominator payloads at launch, without setting
        // EffectApplied. They cannot represent this v4 pre-impact state.
        // Migration must preserve the active visual lifetime/tokens while
        // marking that historical payload consumed, rather than firing again.
        SuperWeaponSystem expectedLegacy = lifecycleSystem;
        for (Instance& instance : expectedLegacy.instances_) {
            instance.chargeTicks = 0;
            instance.effectApplied = true;
            instance.nextImpactTick = -1;
        }
        const std::uint64_t expectedLegacyHash = expectedLegacy.stateHash();
        for (int legacyVersion = 1; legacyVersion <= 3; ++legacyVersion) {
            std::istringstream currentLines(midActiveSave.str());
            std::ostringstream legacySave;
            std::string saveLine;
            while (std::getline(currentLines, saveLine)) {
                if ((legacyVersion == 1
                        && (saveLine.rfind("NextActiveToken=", 0) == 0
                            || saveLine.rfind("ActiveToken=", 0) == 0
                            || saveLine.rfind("ActiveStartedTick=", 0) == 0))
                    || (legacyVersion < 3 && saveLine.rfind("OneShot=", 0) == 0)
                    || saveLine.rfind("StormCloud", 0) == 0) {
                    continue;
                }
                if (saveLine == "Version=8") {
                    saveLine = "Version=" + std::to_string(legacyVersion);
                } else if (saveLine.rfind("NextImpactTick=", 0) == 0) {
                    saveLine = "NextImpactTick=-1";
                }
                legacySave << saveLine << '\n';
            }
            IniFile legacyIni;
            SuperWeaponSystem legacyRestored;
            legacyRestored.reset(lifecycleRules);
            if (!legacyIni.loadFromString(legacySave.str())
                || !legacyRestored.load(legacyIni, &loadError)
                || legacyRestored.stateHash() != expectedLegacyHash
                || legacyRestored.stateHash() == midActiveHash
                || !sameActiveSnapshots(activeSnapshots,
                    legacyRestored.activeLifecycleSnapshots())) {
                std::cerr << "superweapon smoke: v" << legacyVersion
                          << " lifecycle migration failed " << loadError << "\n";
                return 1;
            }
            SuperWeaponWorldContext legacyContext = lifecycleContext;
            std::vector<SuperWeaponActiveLifecycleEvent> legacyCallbacks;
            int legacyImpacts = 0;
            legacyContext.emit = {};
            legacyContext.onSuperWeaponImpact = [&](SuperWeaponImpactEvent) {
                ++legacyImpacts;
            };
            legacyContext.onSuperWeaponActiveLifecycle = [&](SuperWeaponActiveLifecycleEvent event) {
                legacyCallbacks.push_back(std::move(event));
            };
            for (int tick = 6; tick <= 32; ++tick) {
                legacyContext.tick = tick;
                legacyRestored.tick(legacyContext);
            }
            std::vector<int> legacyEndedTokens;
            for (const auto& event : legacyCallbacks) {
                if (event.phase != SuperWeaponActiveLifecyclePhase::Ended
                    || event.tick != 31 || event.endsTick != 31
                    || event.durationTicks != 30) {
                    std::cerr << "superweapon smoke: legacy lifecycle replay/timing mismatch\n";
                    return 1;
                }
                legacyEndedTokens.push_back(event.activeToken);
            }
            std::sort(legacyEndedTokens.begin(), legacyEndedTokens.end());
            if (legacyImpacts != 0 || legacyEndedTokens != startedTokens
                || !legacyRestored.activeLifecycleSnapshots().empty()) {
                std::cerr << "superweapon smoke: legacy payload replay or missing lifecycle end\n";
                return 1;
            }
        }

        std::string malformedSave = midActiveSave.str();
        const std::size_t tokenLine = malformedSave.find("\nActiveToken=");
        const std::size_t tokenEnd = tokenLine == std::string::npos
            ? std::string::npos
            : malformedSave.find('\n', tokenLine + 1);
        if (tokenLine == std::string::npos || tokenEnd == std::string::npos) {
            std::cerr << "superweapon smoke: lifecycle token save field missing\n";
            return 1;
        }
        malformedSave.replace(
            tokenLine + 1,
            tokenEnd - tokenLine - 1,
            "ActiveToken=0"
        );
        IniFile malformedIni;
        const std::uint64_t beforeMalformedLoad = lifecycleRestored.stateHash();
        if (!malformedIni.loadFromString(malformedSave)
            || lifecycleRestored.load(malformedIni, &loadError)
            || lifecycleRestored.stateHash() != beforeMalformedLoad) {
            std::cerr << "superweapon smoke: bad save was not atomic\n";
            return 1;
        }

        std::vector<SuperWeaponActiveLifecycleEvent> resumedCallbacks;
        std::vector<SuperWeaponEvent> resumedGenericEvents;
        lifecycleContext.onSuperWeaponActiveLifecycle = [
            &resumedCallbacks
        ](SuperWeaponActiveLifecycleEvent event) {
            resumedCallbacks.push_back(std::move(event));
        };
        lifecycleContext.emit = [
            &resumedGenericEvents
        ](const SuperWeaponEvent& event) {
            resumedGenericEvents.push_back(event);
        };
        for (int tick = 6; tick <= 32; ++tick) {
            lifecycleContext.tick = tick;
            lifecycleRestored.tick(lifecycleContext);
        }
        std::vector<int> endedTokens;
        for (const SuperWeaponActiveLifecycleEvent& event : resumedCallbacks) {
            if (event.phase == SuperWeaponActiveLifecyclePhase::Ended) {
                endedTokens.push_back(event.activeToken);
                if (event.tick != 31 || event.endsTick != 31
                    || event.durationTicks != 30) {
                    std::cerr << "superweapon smoke: resumed lifecycle timing mismatch\n";
                    return 1;
                }
            }
        }
        std::sort(endedTokens.begin(), endedTokens.end());
        const int resumedEnds = static_cast<int>(std::count_if(
            resumedGenericEvents.begin(),
            resumedGenericEvents.end(),
            [](const SuperWeaponEvent& event) {
                return event.kind == "PsychicDominatorEnded"
                    && event.activeToken > 0;
            }
        ));
        if (endedTokens != startedTokens || resumedEnds != 2
            || lifecycleImpacts.size() != 2U
            || std::any_of(lifecycleImpacts.begin(), lifecycleImpacts.end(),
                [](const auto& impact) { return impact.tick != 7; })
            || !lifecycleRestored.activeLifecycleSnapshots().empty()) {
            std::cerr << "superweapon smoke: resumed lifecycle token mismatch\n";
            return 1;
        }
    }

    const auto fixtureHash = [&rules](
        const std::vector<std::string>& radiation,
        const std::vector<std::string>& lockouts,
        std::uint64_t& result
    ) {
        std::ostringstream fixture;
        fixture << "[SuperWeapons]\n"
                << "Version=1\n"
                << "Count=0\n"
                << "NextInstanceId=1\n"
                << "LastTick=40\n\n"
                << "[SuperWeaponProtections]\n"
                << "Count=0\n\n"
                << "[SuperWeaponRadiation]\n"
                << "Count=" << radiation.size() << "\n";
        for (std::size_t index = 0; index < radiation.size(); ++index) {
            fixture << "Effect" << index << "=" << radiation[index] << "\n";
        }
        fixture << "\n[SuperWeaponLockouts]\n"
                << "Count=" << lockouts.size() << "\n";
        for (std::size_t index = 0; index < lockouts.size(); ++index) {
            fixture << "Owner" << index << "=" << lockouts[index] << "\n";
        }

        IniFile fixtureIni;
        if (!fixtureIni.loadFromString(fixture.str())) {
            return false;
        }
        SuperWeaponSystem fixtureSystem;
        fixtureSystem.reset(rules);
        std::string fixtureError;
        if (!fixtureSystem.load(fixtureIni, &fixtureError)) {
            return false;
        }
        result = fixtureSystem.stateHash();
        return true;
    };
    std::uint64_t radiationBaseHash = 0;
    std::uint64_t radiationInstanceHash = 0;
    std::uint64_t radiationRadiusHash = 0;
    std::uint64_t radiationNextTickHash = 0;
    std::uint64_t lockoutBaseHash = 0;
    std::uint64_t lockoutOwnerHash = 0;
    std::uint64_t lockoutBlackoutHash = 0;
    std::uint64_t lockoutPenaltyUntilHash = 0;
    std::uint64_t lockoutPenaltyHash = 0;
    std::uint64_t lockoutCanonicalOrderHash = 0;
    std::uint64_t lockoutCanonicalReverseHash = 0;
    if (!fixtureHash(
            {"player|7|3|4|5|6|200|50"},
            {},
            radiationBaseHash
        )
        || !fixtureHash(
            {"player|8|3|4|5|6|200|50"},
            {},
            radiationInstanceHash
        )
        || !fixtureHash(
            {"player|7|3|4|6|6|200|50"},
            {},
            radiationRadiusHash
        )
        || !fixtureHash(
            {"player|7|3|4|5|6|200|51"},
            {},
            radiationNextTickHash
        )
        || !fixtureHash({}, {"player|100|110|-50"}, lockoutBaseHash)
        || !fixtureHash({}, {"enemy|100|110|-50"}, lockoutOwnerHash)
        || !fixtureHash({}, {"player|101|110|-50"}, lockoutBlackoutHash)
        || !fixtureHash(
            {},
            {"player|100|111|-50"},
            lockoutPenaltyUntilHash
        )
        || !fixtureHash({}, {"player|100|110|-51"}, lockoutPenaltyHash)
        || !fixtureHash(
            {},
            {"Zulu|100|110|-50", "alpha|120|130|-30"},
            lockoutCanonicalOrderHash
        )
        || !fixtureHash(
            {},
            {"ALPHA|120|130|-30", "zULU|100|110|-50"},
            lockoutCanonicalReverseHash
        )) {
        std::cerr << "superweapon smoke: hash fixture load failed\n";
        return 1;
    }
    if (radiationBaseHash == radiationInstanceHash
        || radiationBaseHash == radiationRadiusHash
        || radiationBaseHash == radiationNextTickHash
        || lockoutBaseHash == lockoutOwnerHash
        || lockoutBaseHash == lockoutBlackoutHash
        || lockoutBaseHash == lockoutPenaltyUntilHash
        || lockoutBaseHash == lockoutPenaltyHash
        || lockoutCanonicalOrderHash != lockoutCanonicalReverseHash) {
        std::cerr << "superweapon smoke: state hash coverage mismatch\n";
        return 1;
    }
    if (!system.applyTriggerAction(
            "player",
            "NukeSpecial",
            "95",
            {{2, 2}, -1},
            context
        )) {
        // The nuke is cooling down in this probe; the action is expected to
        // be rejected rather than silently firing without a charge.
        if (system.stateOf("player", "NukeSpecial") != SuperWeaponRuntimeState::Cooldown
            && system.stateOf("player", "NukeSpecial") != SuperWeaponRuntimeState::Charging) {
            return 1;
        }
    }

    if (!system.grant("enemy", "ChronoSphereSpecial")
        || !system.forceReady("enemy", "ChronoSphereSpecial")) {
        std::cerr << "superweapon smoke: AI chrono setup failed\n";
        return 1;
    }
    {
        std::string reason;
        if (!system.fireAi(
                "enemy",
                "ChronoSphereSpecial",
                context,
                &reason
            )
            || system.stateOf("enemy", "ChronoSphereSpecial")
                != SuperWeaponRuntimeState::Launching) {
            std::cerr << "superweapon smoke: AI chrono did not complete "
                         "two-stage targeting: " << reason << "\n";
            return 1;
        }
    }
    if (!system.grant("scripted", "NukeSpecial")
        || !system.setRechargeTicks(
            "scripted",
            "NukeSpecial",
            240,
            &context
        )
        || !system.setChargePercent(
            "scripted",
            "NukeSpecial",
            50,
            &context
        )
        || system.stateOf("scripted", "NukeSpecial")
            != SuperWeaponRuntimeState::Charging
        || !system.setChargePercent(
            "scripted",
            "NukeSpecial",
            100,
            &context
        )
        || system.stateOf("scripted", "NukeSpecial")
            != SuperWeaponRuntimeState::Ready) {
        std::cerr << "superweapon smoke: scripted charge control failed\n";
        return 1;
    }

    std::cout << "superweapon smoke: passed definitions=" << rules.size()
              << " events=" << events.size()
              << " hash=" << beforeSaveHash << "\n";
    return 0;
}
