#include "simulation/abilities/unit_special/RuntimeSpecialMechanisms.h"
#include "simulation/abilities/unit_special/RuntimeSpecialMechanismsPrivate.h"
#include "simulation/buildings/RuntimeStructureRepack.h"
#include "simulation/abilities/radiation/RuntimeRadiation.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>

bool decodeRuntimeCarrierCellTarget(int targetId, SpecialCell& cell, bool& bridge) {
    int key = -1;
    if (targetId >= -2959999 && targetId <= -1960000) {
        key = -1960000 - targetId;
        bridge = false;
    } else if (targetId >= -1959999 && targetId <= -960000) {
        key = -960000 - targetId;
        bridge = true;
    } else {
        return false;
    }
    cell = {key % 1000, key / 1000};
    return true;
}

namespace {

constexpr int kSpecialMechanismSaveVersion = 3;
using runtime_special_mechanisms_detail::kMaximumTimedBombRecords;
constexpr std::size_t kMaximumParasiteRecords = 4096;
constexpr std::size_t kMaximumCarrierRecords = 4096;
constexpr std::size_t kMaximumParasiteTextLength = 256;
constexpr int kParasiteRatioPerMyriad = 10000;

char lowerAscii(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

bool equalsAsciiIgnoreCase(
    const std::string& left,
    const std::string& right
) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lowerAscii(left[index]) != lowerAscii(right[index])) {
            return false;
        }
    }
    return true;
}

using runtime_special_mechanisms_detail::boolText;

bool parseBool(const std::string& value) {
    return value == "yes" || value == "true" || value == "1";
}

bool parseBoolStrict(const std::string& value, bool& result) {
    if (value == "yes" || value == "true" || value == "1") {
        result = true;
        return true;
    }
    if (value == "no" || value == "false" || value == "0") {
        result = false;
        return true;
    }
    return false;
}

int parseInt(const std::string& value, int fallback = 0) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

bool parseIntStrict(const std::string& value, int& result) {
    if (value.empty()) {
        return false;
    }
    try {
        std::size_t parsed = 0;
        const long long numeric = std::stoll(value, &parsed, 10);
        if (parsed != value.size()
            || numeric < std::numeric_limits<int>::min()
            || numeric > std::numeric_limits<int>::max()) {
            return false;
        }
        result = static_cast<int>(numeric);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> result;
    std::string current;
    std::istringstream stream(value);
    while (std::getline(stream, current, delimiter)) {
        result.push_back(current);
    }
    if (value.empty()) {
        result.clear();
    }
    return result;
}

std::string saveField(const std::string& value) {
    std::string result = value;
    std::size_t position = 0;
    while ((position = result.find('|', position)) != std::string::npos) {
        result.replace(position, 1, "%7C");
        position += 3;
    }
    return result;
}

std::string loadField(std::string value) {
    std::size_t position = 0;
    while ((position = value.find("%7C", position)) != std::string::npos) {
        value.replace(position, 3, "|");
        ++position;
    }
    return value;
}

const UnitType* unitTypeFor(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    if (object.kind == WorldObjectKind::Infantry) {
        return rules.findUnitType("Infantry", object.type);
    }
    if (object.kind == WorldObjectKind::Structure) {
        return nullptr;
    }
    return object.airborne
        ? rules.findUnitType("Aircraft", object.type)
        : rules.findUnitType("Vehicle", object.type);
}

const BuildingType* buildingTypeFor(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    return object.kind == WorldObjectKind::Structure
        ? rules.findBuildingType(object.type)
        : nullptr;
}

bool targetImmuneToPsionics(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    if (const BuildingType* building = buildingTypeFor(rules, object)) {
        return building->immuneToPsionics;
    }
    const UnitType* unit = unitTypeFor(rules, object);
    return unit != nullptr && unit->immuneToPsionics;
}

bool targetImmuneToRadiation(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    if (const BuildingType* building = buildingTypeFor(rules, object)) {
        return building->immuneToRadiation;
    }
    const UnitType* unit = unitTypeFor(rules, object);
    return unit != nullptr && unit->immuneToRadiation;
}

bool targetImmuneToTemporal(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    if (const BuildingType* building = buildingTypeFor(rules, object)) {
        return building->immuneToTemporal;
    }
    const UnitType* unit = unitTypeFor(rules, object);
    return unit != nullptr && unit->immuneToTemporal;
}

bool targetImmuneToElectric(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    if (const BuildingType* building = buildingTypeFor(rules, object)) {
        return building->immuneToElectric;
    }
    const UnitType* unit = unitTypeFor(rules, object);
    return unit != nullptr && unit->immuneToElectric;
}

bool canReceiveElectricCharge(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    const BuildingType* building = buildingTypeFor(rules, object);
    return building != nullptr && building->overpowerable
        && !building->immuneToElectric;
}

bool isUnitEligibleForCamouflage(
    const RulesDatabase& rules,
    const WorldObjectInstance& object
) {
    const UnitType* unit = unitTypeFor(rules, object);
    return unit != nullptr && unit->canDisguise
        && unit->disguiseWhenStill
        && !unit->permaDisguise;
}

bool specialCombatSourceAvailable(const std::vector<WorldObjectInstance>& objects,
    const WorldObjectInstance* source, const RulesDatabase* rules = nullptr,
    const SpecialAlliancePredicate* allied = nullptr) {
    if (!source || source->health <= 0) return false;
    if (source->alive) return true;
    if (source->mission != "InsideTransport") return false;
    return std::any_of(objects.begin(), objects.end(), [&](const auto& host) {
        if (!host.alive || host.health <= 0
            || std::find(host.transportPassengerIds.begin(), host.transportPassengerIds.end(),
                source->instanceId) == host.transportPassengerIds.end()) return false;
        if (rules && host.owner != source->owner
            && (!allied || !*allied || !(*allied)(host.owner, source->owner))) return false;
        const auto* type = rules ? unitTypeFor(*rules, host) : nullptr;
        return !rules || (type && type->openTopped);
    });
}

bool hasVeteranAbility(
    const WorldObjectInstance& object,
    const UnitType& unit,
    VeteranAbility ability
) {
    if (object.veterancy <= 0) {
        return false;
    }
    const std::vector<VeteranAbility>& abilities = object.veterancy >= 2
        ? unit.veterancy.eliteAbilities
        : unit.veterancy.veteranAbilities;
    return std::find(abilities.begin(), abilities.end(), ability)
        != abilities.end();
}

int convertRuleFrames(int frames, int fixedTickHz) {
    return std::max(
        1,
        static_cast<int>(std::lround(
            static_cast<double>(std::max(0, frames))
            * static_cast<double>(std::max(1, fixedTickHz))
            / 15.0
        ))
    );
}

std::string timedBombKindName(TimedBombKind kind) {
    return kind == TimedBombKind::C4 ? "C4" : "Ivan";
}

bool timedBombKindFromName(const std::string& value, TimedBombKind& kind) {
    if (value == "C4") {
        kind = TimedBombKind::C4;
        return true;
    }
    if (value == "Ivan") {
        kind = TimedBombKind::Ivan;
        return true;
    }
    return false;
}

std::string parasiteTargetKindName(ParasiteTargetKind kind) {
    return kind == ParasiteTargetKind::Infantry ? "Infantry" : "Vehicle";
}

bool parasiteTargetKindFromName(
    const std::string& value,
    ParasiteTargetKind& kind
) {
    if (value == "Infantry") {
        kind = ParasiteTargetKind::Infantry;
        return true;
    }
    if (value == "Vehicle") {
        kind = ParasiteTargetKind::Vehicle;
        return true;
    }
    return false;
}

std::string parasiteHostEventName(ParasiteHostEventKind event) {
    switch (event) {
    case ParasiteHostEventKind::Healed:
        return "healed";
    case ParasiteHostEventKind::SonicHit:
        return "sonic hit";
    case ParasiteHostEventKind::Teleporting:
        return "teleporting";
    case ParasiteHostEventKind::DestroyedExternally:
        return "destroyed externally";
    case ParasiteHostEventKind::DestroyedByParasite:
        return "destroyed by parasite";
    }
    return "unknown host event";
}

bool validParasiteText(const std::string& value) {
    return !value.empty() && value.size() <= kMaximumParasiteTextLength;
}

bool validParasiteRatio(int value) {
    return value >= 0 && value <= kParasiteRatioPerMyriad;
}

using runtime_special_mechanisms_detail::objectCell;

bool sameCell(const SpecialCell& left, const SpecialCell& right) {
    return left.x == right.x && left.y == right.y;
}

int cellDistance(const SpecialCell& left, const SpecialCell& right) {
    return std::abs(left.x - right.x) + std::abs(left.y - right.y);
}

template <typename Value>
void mixValue(std::uint64_t& hash, const Value& value) {
    const std::uint64_t numeric = static_cast<std::uint64_t>(value);
    hash ^= numeric + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
}

void mixString(std::uint64_t& hash, const std::string& value) {
    for (const unsigned char character : value) {
        mixValue(hash, character);
    }
    mixValue(hash, static_cast<std::uint64_t>(value.size()));
}

std::string phaseName(CarrierChildPhase phase) {
    switch (phase) {
    case CarrierChildPhase::Docked:
        return "Docked";
    case CarrierChildPhase::Launching:
        return "Launching";
    case CarrierChildPhase::Attacking:
        return "Attacking";
    case CarrierChildPhase::Returning:
        return "Returning";
    case CarrierChildPhase::Destroyed:
        return "Destroyed";
    }
    return "Docked";
}

bool carrierPhaseFromName(
    const std::string& value,
    CarrierChildPhase& phase
) {
    if (value == "Docked") {
        phase = CarrierChildPhase::Docked;
        return true;
    }
    if (value == "Launching") {
        phase = CarrierChildPhase::Launching;
        return true;
    }
    if (value == "Attacking") {
        phase = CarrierChildPhase::Attacking;
        return true;
    }
    if (value == "Returning") {
        phase = CarrierChildPhase::Returning;
        return true;
    }
    if (value == "Destroyed") {
        phase = CarrierChildPhase::Destroyed;
        return true;
    }
    return false;
}

std::string chronoPhaseName(ChronoPhase phase) {
    switch (phase) {
    case ChronoPhase::None:
        return "None";
    case ChronoPhase::WarpOut:
        return "WarpOut";
    case ChronoPhase::InTransit:
        return "InTransit";
    case ChronoPhase::WarpIn:
        return "WarpIn";
    case ChronoPhase::Cooldown:
        return "Cooldown";
    }
    return "None";
}


TimedBombObjectQuery timedBombObjectQuery(
    const std::vector<WorldObjectInstance>& objects
) {
    return [&objects](int id) {
        for (const auto& object : objects) {
            if (object.instanceId == id) {
                return TimedBombObjectView{
                    true, object.alive, object.health, object.owner,
                    {object.x, object.y}
                };
            }
        }
        return TimedBombObjectView{};
    };
}

TimedBombEligibilityQuery timedBombEligibilityQuery(
    const std::vector<WorldObjectInstance>& objects, const RulesDatabase& rules
) {
    return [&objects, &rules](int id, TimedBombEligibility eligibility) {
        const auto found = std::find_if(objects.begin(), objects.end(),
            [id](const auto& object) { return object.instanceId == id; });
        if (found == objects.end()) return false;
        const auto& object = *found;
        switch (eligibility) {
        case TimedBombEligibility::C4Source: {
            const auto* type = unitTypeFor(rules, object);
            return type != nullptr && (type->c4
                || hasVeteranAbility(object, *type, VeteranAbility::C4));
        }
        case TimedBombEligibility::IvanSource: {
            const auto* type = unitTypeFor(rules, object);
            return type != nullptr && type->ivan;
        }
        case TimedBombEligibility::C4Target:
            return buildingTypeFor(rules, object) != nullptr;
        case TimedBombEligibility::IvanTarget: {
            const auto* unit = unitTypeFor(rules, object);
            const auto* building = buildingTypeFor(rules, object);
            return building != nullptr || (unit != nullptr && unit->bombable);
        }
        }
        return false;
    };
}

TimedBombRules timedBombRules(const SpecialMechanismRules& rules) {
    return {rules.c4DelayMinutes, rules.c4Warhead, rules.ivanTimedDelayFrames,
        rules.ivanDamage, rules.ivanWarhead};
}

} // namespace

void RuntimeSpecialMechanismState::swap(
    RuntimeSpecialMechanismState& other
) noexcept {
    static_assert(std::is_nothrow_swappable_v<SpecialMechanismRules>);
    using std::swap;
    swap(rules_, other.rules_);
    mindControlByTarget_.swap(other.mindControlByTarget_);
    mindControlOverloadByController_.swap(other.mindControlOverloadByController_);
    camouflageByObject_.swap(other.camouflageByObject_);
    chronoByObject_.swap(other.chronoByObject_);
    temporalByTarget_.swap(other.temporalByTarget_);
    electricDisableByObject_.swap(other.electricDisableByObject_);
    electricChargeByTarget_.swap(other.electricChargeByTarget_);
    electricArcs_.swap(other.electricArcs_);
    radiationAreas_.swap(other.radiationAreas_);
    timedBombs_.swap(other.timedBombs_);
    parasitePendingByProjectile_.swap(
        other.parasitePendingByProjectile_
    );
    parasiteByTarget_.swap(other.parasiteByTarget_);
    parasiteResolvedByProjectile_.swap(
        other.parasiteResolvedByProjectile_
    );
    carrierParents_.swap(other.carrierParents_);
    carrierChildren_.swap(other.carrierChildren_);
    spawnedMissiles_.swap(other.spawnedMissiles_);
    swap(nextElectricArcId_, other.nextElectricArcId_);
    swap(nextRadiationAreaId_, other.nextRadiationAreaId_);
}

void swap(
    RuntimeSpecialMechanismState& left,
    RuntimeSpecialMechanismState& right
) noexcept {
    left.swap(right);
}

void RuntimeSpecialMechanismState::reset(
    const SpecialMechanismRules& rules
) {
    rules_ = rules;
    mindControlByTarget_.clear();
    mindControlOverloadByController_.clear();
    camouflageByObject_.clear();
    chronoByObject_.clear();
    temporalByTarget_.clear();
    electricDisableByObject_.clear();
    electricChargeByTarget_.clear();
    electricArcs_.clear();
    radiationAreas_.clear();
    timedBombs_.reset();
    parasitePendingByProjectile_.clear();
    parasiteByTarget_.clear();
    parasiteResolvedByProjectile_.clear();
    carrierParents_.clear();
    carrierChildren_.clear();
    spawnedMissiles_.clear();
    nextElectricArcId_ = 1;
    nextRadiationAreaId_ = 1;
}

const SpecialMechanismRules& RuntimeSpecialMechanismState::rules() const {
    return rules_;
}

WorldObjectInstance* RuntimeSpecialMechanismState::findObject(
    std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    for (WorldObjectInstance& object : objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

const WorldObjectInstance* RuntimeSpecialMechanismState::findObject(
    const std::vector<WorldObjectInstance>& objects,
    int objectId
) {
    for (const WorldObjectInstance& object : objects) {
        if (object.instanceId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

#include "simulation/abilities/psionic/RuntimeMindControlOverload.inl"

void RuntimeSpecialMechanismState::establishDominatorControl(int targetObjectId,
    const std::string& owner, const std::string& originalOwner, int tick) {
    MindControlLinkState link;
    link.targetObjectId = targetObjectId;
    link.controllerObjectId = -1;
    link.controllerOwner = owner;
    link.originalOwner = originalOwner;
    link.permanent = true;
    link.startedTick = tick;
    mindControlByTarget_[targetObjectId] = std::move(link);
}

bool RuntimeSpecialMechanismState::tryMindControl(
    std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int controllerObjectId,
    int targetObjectId,
    int capacity,
    bool permanent,
    bool infinite,
    bool allowStructures,
    int tick,
    const SpecialAlliancePredicate& areAllied,
    const SpecialEventSink& emit,
    std::string& reason
) {
    const auto reject = [&](const std::string& rejection) {
        reason = rejection;
        if (emit != nullptr) {
            const WorldObjectInstance* target = findObject(
                objects,
                targetObjectId
            );
            emit({
                "MindControlRejected",
                tick,
                controllerObjectId,
                targetObjectId,
                0,
                target == nullptr
                    ? SpecialCell{}
                    : SpecialCell{target->x, target->y},
                rejection
            });
        }
        return false;
    };
    WorldObjectInstance* controller = findObject(objects, controllerObjectId);
    WorldObjectInstance* target = findObject(objects, targetObjectId);
    if (controller == nullptr || target == nullptr
        || !specialCombatSourceAvailable(objects, controller, &rules, &areAllied) || !target->alive) {
        return reject("object unavailable");
    }
    if (controller->instanceId == target->instanceId) {
        return reject("self target");
    }
    if (!controller->owner.empty()
        && equalsAsciiIgnoreCase(controller->owner, target->owner)) {
        return reject("own target");
    }
    if (areAllied != nullptr
        && areAllied(controller->owner, target->owner)) {
        return reject("allied target");
    }
    if (targetImmuneToPsionics(rules, *target)
        && !(allowStructures && target->kind == WorldObjectKind::Structure)) {
        return reject("target immune to psionics");
    }

    const auto existing = mindControlByTarget_.find(targetObjectId);
    if (existing != mindControlByTarget_.end()) {
        return reject(existing->second.controllerObjectId == controllerObjectId
            ? "already controlled by attacker"
            : "target already controlled");
    }

    int linkCount = 0;
    for (const auto& [unusedTargetId, link] : mindControlByTarget_) {
        (void)unusedTargetId;
        if (link.controllerObjectId == controllerObjectId) {
            ++linkCount;
        }
    }
    if (!infinite && linkCount >= std::max(1, capacity)) {
        return reject("control capacity full");
    }

    MindControlLinkState link;
    link.controllerObjectId = controllerObjectId;
    link.targetObjectId = targetObjectId;
    link.controllerOwner = controller->owner;
    link.originalOwner = target->owner;
    link.permanent = permanent;
    link.startedTick = tick;
    mindControlByTarget_[targetObjectId] = link;
    target->hasBeenCaptured = target->hasBeenCaptured || target->owner != controller->owner;
    target->owner = controller->owner;
    target->mission = permanent ? "MindControlledPermanent" : "MindControlled";
    reason = permanent ? "permanent control" : "control established";
    if (emit != nullptr) {
        emit({
            "MindControlEstablished",
            tick,
            controllerObjectId,
            targetObjectId,
            linkCount + 1,
            {target->x, target->y},
            reason
        });
    }
    return true;
}

bool RuntimeSpecialMechanismState::releaseMindControlByTarget(
    std::vector<WorldObjectInstance>& objects,
    int targetObjectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    const auto linkIt = mindControlByTarget_.find(targetObjectId);
    if (linkIt == mindControlByTarget_.end()) {
        return false;
    }
    const MindControlLinkState link = linkIt->second;
    mindControlByTarget_.erase(linkIt);
    if (WorldObjectInstance* target = findObject(objects, targetObjectId)) {
        if (target->alive && !link.originalOwner.empty()) {
            target->hasBeenCaptured = target->hasBeenCaptured || target->owner != link.originalOwner;
            target->owner = link.originalOwner;
            target->mission = "Guard";
        }
    }
    if (emit != nullptr) {
        emit({
            "MindControlReleased",
            tick,
            link.controllerObjectId,
            targetObjectId,
            0,
            {},
            reason
        });
    }
    return true;
}

bool RuntimeSpecialMechanismState::releaseMindControlByController(
    std::vector<WorldObjectInstance>& objects,
    int controllerObjectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    std::vector<int> targets;
    for (const auto& [targetId, link] : mindControlByTarget_) {
        if (link.controllerObjectId == controllerObjectId && !link.permanent) {
            targets.push_back(targetId);
        }
    }
    bool changed = false;
    for (int targetId : targets) {
        changed = releaseMindControlByTarget(
            objects,
            targetId,
            tick,
            emit,
            reason
        ) || changed;
    }
    return changed;
}

void RuntimeSpecialMechanismState::notifyActivity(
    int objectId,
    int tick,
    const std::string& reason,
    int fakeBlinkTicks
) {
    CamouflageState& state = camouflageByObject_[objectId];
    state.objectId = objectId;
    if (reason == "fire" || reason == "attack") {
        state.fakeBlinkUntilTick = tick
            + std::max(
                0,
                fakeBlinkTicks >= 0
                    ? fakeBlinkTicks
                    : rules_.infantryBlinkDisguiseTime
            );
        // DisguiseFakeBlinkTime exposes the real sprite without changing
        // logical camouflage. Moving/deploying still breaks the disguise.
        return;
    }
    state.lastActivityTick = tick;
    state.rearmUntilTick = tick + std::max(0, rules_.disguiseRearmDelay);
    state.active = false;
}

bool RuntimeSpecialMechanismState::tickCamouflage(
    std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int tick,
    const std::function<bool(int)>& isStationary,
    const SpecialEventSink& emit
) {
    bool changed = false;
    for (WorldObjectInstance& object : objects) {
        if (!object.alive || !isUnitEligibleForCamouflage(rules, object)) {
            camouflageByObject_.erase(object.instanceId);
            continue;
        }
        CamouflageState& state = camouflageByObject_[object.instanceId];
        state.objectId = object.instanceId;
        if (state.disguiseType.empty()
            && !rules_.defaultMirageDisguises.empty()) {
            const std::size_t disguiseIndex =
                static_cast<std::size_t>(
                    std::llabs(static_cast<long long>(object.instanceId))
                ) % rules_.defaultMirageDisguises.size();
            state.disguiseType = rules_.defaultMirageDisguises[disguiseIndex];
        }
        const bool stationary = isStationary == nullptr
            ? true
            : isStationary(object.instanceId);
        if (!stationary) {
            if (state.active) {
                state.active = false;
                state.rearmUntilTick =
                    tick + std::max(0, rules_.disguiseRearmDelay);
                if (emit != nullptr) {
                    emit({
                        "CamouflageExit",
                        tick,
                        object.instanceId,
                        -1,
                        0,
                        {object.x, object.y},
                        "moving"
                    });
                }
                changed = true;
            }
            state.lastActivityTick = tick;
            continue;
        }
        if (!state.active
            && tick >= state.rearmUntilTick
            && tick - state.lastActivityTick
                >= std::max(0, rules_.disguiseDelay)) {
            state.active = true;
            if (emit != nullptr) {
                emit({
                    "CamouflageEnter",
                    tick,
                    object.instanceId,
                    -1,
                    0,
                    {object.x, object.y},
                    "stationary"
                });
            }
            changed = true;
        }
    }
    return changed;
}

bool RuntimeSpecialMechanismState::isCamouflaged(int objectId) const {
    const auto found = camouflageByObject_.find(objectId);
    return found != camouflageByObject_.end() && found->second.active;
}

bool RuntimeSpecialMechanismState::isCamouflageBlinking(
    int objectId,
    int tick
) const {
    const auto found = camouflageByObject_.find(objectId);
    return found != camouflageByObject_.end()
        && tick < found->second.fakeBlinkUntilTick;
}

void RuntimeSpecialMechanismState::beginChronosphereRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz
) {
    beginChronoArrivalRecovery(object, tick, fixedTickHz, rules_.chronoDelay);
}

void RuntimeSpecialMechanismState::beginChronoReinforcementRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz
) {
    beginChronoArrivalRecovery(object, tick, fixedTickHz, rules_.chronoReinforcementDelay);
}

void RuntimeSpecialMechanismState::beginChronoArrivalRecovery(
    WorldObjectInstance& object, int tick, int fixedTickHz, int delayFrames
) {
    if (!object.alive) return;
    const int delay = static_cast<int>(std::clamp(
        std::round(static_cast<double>(std::max(0, delayFrames))
            * std::max(1, fixedTickHz) / 15.0),
        0.0, static_cast<double>(std::numeric_limits<int>::max() / 2)));
    if (delay == 0) {
        chronoByObject_.erase(object.instanceId);
        object.mission = "Guard";
        return;
    }
    ChronoMoveState recovery;
    recovery.objectId = object.instanceId;
    recovery.phase = ChronoPhase::WarpIn;
    recovery.source = recovery.destination = {object.x, object.y};
    recovery.sourceLevel = recovery.destinationLevel = object.level;
    recovery.startTick = recovery.completeTick = tick;
    recovery.cooldownUntilTick = static_cast<int>(std::min<std::int64_t>(
        std::numeric_limits<int>::max(), static_cast<std::int64_t>(tick) + delay));
    chronoByObject_[object.instanceId] = recovery;
    object.mission = "ChronoFrozen";
}

bool RuntimeSpecialMechanismState::requestChronoMove(
    std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int objectId,
    SpecialCell destination,
    int destinationLevel,
    int tick,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied,
    const SpecialEventSink& emit,
    std::string& reason,
    int fixedTickHz
) {
    WorldObjectInstance* object = findObject(objects, objectId);
    const UnitType* unit = object == nullptr ? nullptr : unitTypeFor(rules, *object);
    if (object == nullptr || unit == nullptr || !object->alive
        || !unit->teleporter) {
        reason = "not a teleporter";
        return false;
    }
    if (object->dockedToObjectId >= 0 || !object->transportPassengerIds.empty()) {
        reason = "transport state blocks chrono";
        return false;
    }
    const auto current = chronoByObject_.find(objectId);
    if (current != chronoByObject_.end()
        && (isChronoInTransit(objectId) || tick < current->second.cooldownUntilTick)) {
        reason = "chrono cooldown or transit";
        return false;
    }
    if (cellIsLegal != nullptr
        && !cellIsLegal(destination.x, destination.y, objectId)) {
        reason = "illegal destination";
        return false;
    }
    if (cellIsOccupied != nullptr
        && cellIsOccupied(destination.x, destination.y, objectId)) {
        reason = "destination occupied";
        return false;
    }

    const SpecialCell source{object->x, object->y};
    // This is recovery AFTER arrival, not travel time. RangeMinimum is a
    // threshold for the minimum delay, not a distance to subtract from a trip.
    const double distanceLeptons = std::hypot(
        static_cast<double>(destination.x) - source.x,
        static_cast<double>(destination.y) - source.y) * 256.0;
    const double variableFrames = rules_.chronoTrigger
            && distanceLeptons > std::max(0, rules_.chronoRangeMinimum)
        ? std::floor(distanceLeptons / std::max(1, rules_.chronoDistanceFactor)) : 0.0;
    const double recoveryFrames = std::max(
        static_cast<double>(rules_.chronoMinimumDelay), variableFrames);
    const int delay = static_cast<int>(std::clamp(
        std::round(recoveryFrames * std::max(1, fixedTickHz) / 15.0),
        0.0, static_cast<double>(std::numeric_limits<int>::max() / 2)));

    ChronoMoveState state;
    state.objectId = objectId;
    state.phase = ChronoPhase::WarpOut;
    state.source = source;
    state.destination = destination;
    state.sourceLevel = object->level;
    state.destinationLevel = destinationLevel;
    state.startTick = tick;
    state.completeTick = tick + 1; // A blink takes one simulation tick at any distance.
    state.cooldownUntilTick = static_cast<int>(std::min<std::int64_t>(
        std::numeric_limits<int>::max(), static_cast<std::int64_t>(state.completeTick) + delay));
    chronoByObject_[objectId] = state;
    object->mission = "ChronoOut";
    reason = "chrono warp queued";
    if (emit != nullptr) {
        emit({
            "ChronoWarpOut",
            tick,
            objectId,
            -1,
            delay,
            source,
            reason
        });
    }
    return true;
}

bool RuntimeSpecialMechanismState::tickChronoMoves(
    std::vector<WorldObjectInstance>& objects,
    int tick,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied,
    const SpecialEventSink& emit
) {
    bool changed = false;
    std::vector<int> completed;
    for (auto& [objectId, state] : chronoByObject_) {
        WorldObjectInstance* object = findObject(objects, objectId);
        // A real passenger retains the absolute recovery deadline without
        // replaying a landing or replacing its InsideTransport mission.
        if (object && !object->alive
            && (state.phase == ChronoPhase::WarpIn || state.phase == ChronoPhase::Cooldown)
            && specialCombatSourceAvailable(objects, object)) {
            if (tick >= state.cooldownUntilTick) {
                completed.push_back(objectId);
                changed = true;
            } else if (state.phase != ChronoPhase::Cooldown) {
                state.phase = ChronoPhase::Cooldown;
                changed = true;
            }
            continue;
        }
        if (object == nullptr || !object->alive) {
            completed.push_back(objectId);
            continue;
        }
        if ((state.phase == ChronoPhase::WarpOut || state.phase == ChronoPhase::InTransit)
            && tick >= state.completeTick) {
            state.phase = ChronoPhase::InTransit;
            object->mission = "ChronoInTransit";
            if (cellIsLegal != nullptr
                && !cellIsLegal(
                    state.destination.x,
                    state.destination.y,
                    objectId
                )) {
                object->mission = "Guard";
                if (emit != nullptr) {
                    emit({
                        "ChronoRejectedAtArrival",
                        tick,
                        objectId,
                        -1,
                        0,
                        state.source,
                        "destination became illegal"
                    });
                }
                completed.push_back(objectId);
                changed = true;
                continue;
            }
            if (cellIsOccupied != nullptr
                && cellIsOccupied(
                    state.destination.x,
                    state.destination.y,
                    objectId
                )) {
                object->mission = "Guard";
                if (emit != nullptr) {
                    emit({
                        "ChronoRejectedAtArrival",
                        tick,
                        objectId,
                        -1,
                        0,
                        state.source,
                        "destination became occupied"
                    });
                }
                completed.push_back(objectId);
                changed = true;
                continue;
            }
            object->x = state.destination.x;
            object->y = state.destination.y;
            object->cell = state.destination.x + state.destination.y * 512;
            object->level = state.destinationLevel;
            object->hasRenderPosition = false;
            object->renderMoveActive = false;
            object->renderX = object->x;
            object->renderY = object->y;
            object->renderLevel = object->level;
            object->mission = "ChronoFrozen";
            const int recoveryTicks = std::max(0, state.cooldownUntilTick - state.completeTick);
            state.phase = ChronoPhase::WarpIn;
            state.completeTick = tick; // Retain actual arrival time through recovery/save/load.
            state.cooldownUntilTick = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(), static_cast<std::int64_t>(tick) + recoveryTicks));
            if (emit != nullptr) {
                emit({
                    "ChronoWarpIn",
                    tick,
                    objectId,
                    -1,
                    0,
                    state.destination,
                    "destination reached"
                });
            }
            changed = true;
        } else if (state.phase == ChronoPhase::WarpIn || state.phase == ChronoPhase::Cooldown) {
            if (tick >= state.cooldownUntilTick) {
                object->mission = "Guard";
                completed.push_back(objectId);
                if (emit != nullptr) {
                    emit({"ChronoReady", tick, objectId, -1, 0,
                        state.destination, "chrono recovery complete"});
                }
                changed = true;
            } else if (state.phase == ChronoPhase::WarpIn) {
                state.phase = ChronoPhase::Cooldown;
                object->mission = "ChronoFrozen";
                changed = true;
            }
        }
    }
    for (int objectId : completed) {
        chronoByObject_.erase(objectId);
    }
    return changed;
}

bool RuntimeSpecialMechanismState::isChronoInTransit(int objectId) const {
    const auto found = chronoByObject_.find(objectId);
    return found != chronoByObject_.end()
        && (found->second.phase == ChronoPhase::WarpOut
            || found->second.phase == ChronoPhase::InTransit);
}

bool RuntimeSpecialMechanismState::isChronoMovementLocked(int objectId, int tick) const {
    const auto found = chronoByObject_.find(objectId);
    return found != chronoByObject_.end()
        && (isChronoInTransit(objectId) || tick < found->second.cooldownUntilTick);
}

bool RuntimeSpecialMechanismState::applyTemporalHit(
    std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int attackerObjectId,
    int targetObjectId,
    int progress,
    int requiredProgress,
    int tick,
    const SpecialAlliancePredicate& areAllied,
    const SpecialEventSink& emit,
    bool& completed,
    std::string& reason,
    bool allowAlliedTarget
) {
    completed = false;
    WorldObjectInstance* attacker = findObject(objects, attackerObjectId);
    WorldObjectInstance* target = findObject(objects, targetObjectId);
    if (attacker == nullptr || target == nullptr
        || !specialCombatSourceAvailable(objects, attacker, &rules, &areAllied) || !target->alive) {
        reason = "object unavailable";
        return false;
    }
    // Ordinary acquisition keeps its alliance filter. The host may admit
    // an explicit force-fire order against a distinct friendly target.
    if (attackerObjectId == targetObjectId || (!allowAlliedTarget && areAllied != nullptr
        && areAllied(attacker->owner, target->owner))) {
        reason = "allied target";
        return false;
    }
    if (targetImmuneToTemporal(rules, *target)) {
        reason = "target immune to temporal";
        return false;
    }

    auto [targetState, inserted] = temporalByTarget_.try_emplace(targetObjectId);
    TemporalTargetState& state = targetState->second;
    if (inserted) state.lastAdvancedTick = tick;
    state.targetObjectId = targetObjectId;
    state.requiredProgress = std::max(
        state.requiredProgress,
        std::max(1, requiredProgress)
    );
    state.frozen = true;
    auto& contribution = state.progressByAttacker[attackerObjectId];
    contribution = static_cast<int>(std::min<std::int64_t>(
        std::numeric_limits<int>::max(), static_cast<std::int64_t>(contribution) + std::max(0, progress)));
    // Preserve the saved folding mission while its buildup clock is frozen.
    if (!runtimeStructureIsRepacking(*target)) target->mission = "Temporal";
    std::int64_t totalProgress = 0;
    for (const auto& [unusedAttacker, value] : state.progressByAttacker) {
        (void)unusedAttacker;
        totalProgress += value;
    }
    reason = "temporal progress";
    if (emit != nullptr) {
        emit({
            "TemporalProgress",
            tick,
            attackerObjectId,
            targetObjectId,
            static_cast<int>(std::min<std::int64_t>(totalProgress, std::numeric_limits<int>::max())),
            {target->x, target->y},
            reason
        });
    }
    if (totalProgress >= state.requiredProgress) {
        completed = true;
        target->health = 0;
        target->mission = "TemporalRemoved";
        if (emit != nullptr) {
            emit({
                "TemporalCompleted",
                tick,
                attackerObjectId,
                targetObjectId,
                static_cast<int>(std::min<std::int64_t>(totalProgress, std::numeric_limits<int>::max())),
                {target->x, target->y},
                "temporal progress complete"
            });
        }
        temporalByTarget_.erase(targetObjectId);
    }
    return true;
}

bool RuntimeSpecialMechanismState::isTemporalFrozen(int objectId) const {
    const auto found = temporalByTarget_.find(objectId);
    return found != temporalByTarget_.end() && found->second.frozen;
}

bool RuntimeSpecialMechanismState::hasTemporalAuthorityForObject(
    int objectId
) const {
    if (temporalByTarget_.find(objectId) != temporalByTarget_.end()) {
        return true;
    }
    return std::any_of(
        temporalByTarget_.begin(),
        temporalByTarget_.end(),
        [objectId](const auto& entry) {
            return entry.second.progressByAttacker.find(objectId)
                != entry.second.progressByAttacker.end();
        }
    );
}

void RuntimeSpecialMechanismState::clearTemporalAttacker(
    int attackerObjectId,
    int targetObjectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    const auto targetIt = temporalByTarget_.find(targetObjectId);
    if (targetIt == temporalByTarget_.end()) {
        return;
    }
    auto& contributors = targetIt->second.progressByAttacker;
    const auto leaving = contributors.find(attackerObjectId);
    if (leaving == contributors.end()) return;
    const int accrued = leaving->second;
    contributors.erase(leaving);
    // Erasure belongs to the target. A remaining firer inherits completed
    // work; only the final disconnected beam releases stasis and resets it.
    if (!contributors.empty()) {
        auto& retained = contributors.begin()->second;
        retained = static_cast<int>(std::min<std::int64_t>(
            std::numeric_limits<int>::max(), static_cast<std::int64_t>(retained) + accrued));
    }
    if (targetIt->second.progressByAttacker.empty()) {
        temporalByTarget_.erase(targetIt);
    }
    if (emit != nullptr) {
        emit({
            "TemporalInterrupted",
            tick,
            attackerObjectId,
            targetObjectId,
            0,
            {},
            reason
        });
    }
}

void RuntimeSpecialMechanismState::clearTemporalTarget(
    int targetObjectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    if (temporalByTarget_.erase(targetObjectId) == 0) {
        return;
    }
    if (emit != nullptr) {
        emit({
            "TemporalTargetCleared",
            tick,
            -1,
            targetObjectId,
            0,
            {},
            reason
        });
    }
}

void RuntimeSpecialMechanismState::addElectricArc(
    int sourceObjectId,
    int targetObjectId,
    SpecialWorldPoint source,
    SpecialWorldPoint target,
    const std::string& color,
    bool alternateColor,
    bool assault,
    int tick,
    int durationTicks,
    const SpecialEventSink& emit,
    int sourceScreenOffsetX,
    int sourceScreenOffsetY,
    int sourceNativeFixedX,
    int sourceNativeFixedY
) {
    ElectricArcState arc;
    arc.arcId = nextElectricArcId_++;
    arc.sourceObjectId = sourceObjectId;
    arc.targetObjectId = targetObjectId;
    arc.source = source;
    arc.target = target;
    arc.color = color;
    arc.alternateColor = alternateColor;
    arc.assault = assault;
    arc.startTick = tick;
    arc.expireTick = tick + std::max(1, durationTicks);
    arc.sourceScreenOffsetX = sourceScreenOffsetX;
    arc.sourceScreenOffsetY = sourceScreenOffsetY;
    arc.sourceNativeFixedX = sourceNativeFixedX;
    arc.sourceNativeFixedY = sourceNativeFixedY;
    electricArcs_[arc.arcId] = arc;
    if (emit != nullptr) {
        emit({
            assault ? "ElectricAssaultArc" : "ElectricArc",
            tick,
            sourceObjectId,
            targetObjectId,
            arc.arcId,
            {},
            color
        });
    }
}

void RuntimeSpecialMechanismState::disableElectricTarget(
    int objectId,
    int untilTick,
    int tick,
    const SpecialEventSink& emit
) {
    ElectricDisableState& state = electricDisableByObject_[objectId];
    state.objectId = objectId;
    state.untilTick = std::max(state.untilTick, untilTick);
    if (emit != nullptr) {
        emit({
            "ElectricTargetDisabled",
            tick,
            objectId,
            -1,
            state.untilTick,
            {},
            "electric assault"
        });
    }
}

bool RuntimeSpecialMechanismState::isElectricDisabled(
    int objectId,
    int tick
) const {
    const auto found = electricDisableByObject_.find(objectId);
    return found != electricDisableByObject_.end()
        && tick < found->second.untilTick;
}

bool RuntimeSpecialMechanismState::applyElectricCharge(
    std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int sourceObjectId,
    int targetObjectId,
    int tick,
    int durationTicks,
    int level,
    const SpecialAlliancePredicate& areAllied,
    const SpecialEventSink& emit,
    std::string& reason
) {
    const WorldObjectInstance* source = findObject(objects, sourceObjectId);
    const WorldObjectInstance* target = findObject(objects, targetObjectId);
    if (source == nullptr || target == nullptr
        || !source->alive || !target->alive) {
        reason = "electric charge object unavailable";
        return false;
    }
    if (sourceObjectId == targetObjectId) {
        reason = "electric charge self target";
        return false;
    }
    if (areAllied == nullptr || !areAllied(source->owner, target->owner)) {
        reason = "electric charge requires allied target";
        return false;
    }
    if (!canReceiveElectricCharge(rules, *target)
        || targetImmuneToElectric(rules, *target)) {
        reason = "target is not overpowerable";
        return false;
    }

    ElectricChargeState& state = electricChargeByTarget_[targetObjectId];
    const bool wasActive = state.expireTick > tick;
    state.targetObjectId = targetObjectId;
    state.sourceObjectId = sourceObjectId;
    state.startTick = wasActive ? state.startTick : tick;
    state.expireTick = std::max(
        state.expireTick,
        tick + std::max(1, durationTicks)
    );
    state.level = wasActive
        ? std::max(state.level, std::max(1, level))
        : std::max(1, level);
    reason = wasActive ? "electric charge refreshed" : "electric charge started";
    if (emit != nullptr) {
        emit({
            wasActive ? "ElectricChargeRefreshed" : "ElectricChargeStarted",
            tick,
            sourceObjectId,
            targetObjectId,
            state.level,
            {target->x, target->y},
            reason
        });
    }
    return true;
}

bool RuntimeSpecialMechanismState::isElectricCharged(
    int objectId,
    int tick
) const {
    const auto found = electricChargeByTarget_.find(objectId);
    return found != electricChargeByTarget_.end()
        && tick < found->second.expireTick;
}

void RuntimeSpecialMechanismState::clearElectricChargeByObject(
    int objectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    std::vector<int> targets;
    for (const auto& [targetId, charge] : electricChargeByTarget_) {
        if (targetId == objectId || charge.sourceObjectId == objectId) {
            targets.push_back(targetId);
        }
    }
    for (int targetId : targets) {
        const auto found = electricChargeByTarget_.find(targetId);
        if (found == electricChargeByTarget_.end()) {
            continue;
        }
        const ElectricChargeState charge = found->second;
        electricChargeByTarget_.erase(found);
        if (emit != nullptr) {
            emit({
                "ElectricChargeEnded",
                tick,
                charge.sourceObjectId,
                charge.targetObjectId,
                charge.level,
                {},
                reason
            });
        }
    }
}

bool RuntimeSpecialMechanismState::hasActiveElectricEffects() const {
    return !electricDisableByObject_.empty()
        || !electricChargeByTarget_.empty()
        || !electricArcs_.empty();
}

void RuntimeSpecialMechanismState::tickElectric(
    int tick,
    const SpecialEventSink& emit
) {
    std::vector<int> expiredArcs;
    for (const auto& [arcId, arc] : electricArcs_) {
        if (tick >= arc.expireTick) {
            expiredArcs.push_back(arcId);
        }
    }
    for (int arcId : expiredArcs) {
        electricArcs_.erase(arcId);
    }
    std::vector<int> restored;
    for (const auto& [objectId, state] : electricDisableByObject_) {
        if (tick >= state.untilTick) {
            restored.push_back(objectId);
        }
    }
    for (int objectId : restored) {
        electricDisableByObject_.erase(objectId);
        if (emit != nullptr) {
            emit({
                "ElectricTargetRestored",
                tick,
                objectId,
                -1,
                0,
                {},
                "electric duration ended"
            });
        }
    }
    std::vector<int> expiredCharges;
    for (const auto& [targetId, charge] : electricChargeByTarget_) {
        if (tick >= charge.expireTick) {
            expiredCharges.push_back(targetId);
        }
    }
    for (int targetId : expiredCharges) {
        const auto found = electricChargeByTarget_.find(targetId);
        if (found == electricChargeByTarget_.end()) {
            continue;
        }
        const ElectricChargeState charge = found->second;
        electricChargeByTarget_.erase(found);
        if (emit != nullptr) {
            emit({
                "ElectricChargeEnded",
                tick,
                charge.sourceObjectId,
                charge.targetObjectId,
                charge.level,
                {},
                "charge duration ended"
            });
        }
    }
}

int RuntimeSpecialMechanismState::spawnRadiationArea(
    int sourceObjectId,
    const std::string& owner,
    SpecialCell center,
    int radius,
    int strength,
    int damageBase,
    const std::string& warheadId,
    int tick,
    bool persistsAfterSourceDeath,
    const SpecialEventSink& emit
) {
    const int normalizedRadius = std::max(0, radius);
    const int maximumStrength = std::max(1, rules_.radiationLevelMax);
    const int normalizedStrength = std::clamp(
        strength,
        1,
        maximumStrength
    );
    const std::string normalizedWarheadId = warheadId.empty()
        ? rules_.radiationSiteWarhead
        : warheadId;
    for (auto& [areaId, existing] : radiationAreas_) {
        if (existing.sourceObjectId != sourceObjectId
            || existing.owner != owner
            || existing.center.x != center.x
            || existing.center.y != center.y
            || existing.warheadId != normalizedWarheadId) {
            continue;
        }

        // A radiation site is authoritative per source and cell. Repeated
        // AreaFire discharges may strengthen the site, but must not create a
        // stack of independently decaying areas at the same location.
        if (normalizedStrength > existing.strength) {
            existing.strength = normalizedStrength;
            existing.maximumStrength = maximumStrength;
            existing.radius = std::max(existing.radius, normalizedRadius);
            existing.damageBase = std::max(existing.damageBase, damageBase);
            existing.remainingTicks = std::max(
                existing.remainingTicks,
                normalizedStrength
                    * std::max(1, rules_.radiationDurationMultiple)
            );
            existing.persistsAfterSourceDeath =
                existing.persistsAfterSourceDeath
                || persistsAfterSourceDeath;
            if (emit != nullptr) {
                emit({
                    "RadiationAreaStrengthened",
                    tick,
                    sourceObjectId,
                    -1,
                    existing.strength,
                    center,
                    existing.warheadId
                });
            }
        }
        return areaId;
    }

    RadiationAreaState area;
    area.areaId = nextRadiationAreaId_++;
    area.sourceObjectId = sourceObjectId;
    area.owner = owner;
    area.center = center;
    area.radius = normalizedRadius;
    area.strength = normalizedStrength;
    area.maximumStrength = maximumStrength;
    area.damageBase = std::max(1, damageBase);
    area.nextApplicationTick = tick;
    area.nextDecayTick = tick + std::max(1, rules_.radiationLevelDelay);
    area.remainingTicks = std::max(
        1,
        area.strength * std::max(1, rules_.radiationDurationMultiple)
    );
    area.damageInterval = std::max(1, rules_.radiationApplicationDelay);
    area.decayAmount = std::max(
        1,
        rules_.radiationLevelDelay
            / std::max(1, rules_.radiationDurationMultiple)
    );
    area.warheadId = normalizedWarheadId;
    area.persistsAfterSourceDeath = persistsAfterSourceDeath;
    radiationAreas_[area.areaId] = area;
    if (emit != nullptr) {
        emit({
            "RadiationAreaCreated",
            tick,
            sourceObjectId,
            -1,
            area.strength,
            center,
            area.warheadId
        });
    }
    return area.areaId;
}

std::vector<RadiationDamageEvent> RuntimeSpecialMechanismState::tickRadiation(
    std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int tick,
    const SpecialAlliancePredicate& areAllied,
    const std::function<bool(int)>& sourceIsAlive,
    const SpecialEventSink& emit
) {
    // Retail cell radiation has no firing house for alliance filtering.
    // Keep the recorded source for kill/death attribution, not immunity.
    (void)areAllied;
    std::map<int, RadiationDamageEvent> mergedDamage;
    std::vector<int> expired;
    for (auto& [areaId, area] : radiationAreas_) {
        if (!area.persistsAfterSourceDeath
            && area.sourceObjectId >= 0
            && sourceIsAlive != nullptr
            && !sourceIsAlive(area.sourceObjectId)) {
            expired.push_back(areaId);
            continue;
        }
        if (area.remainingTicks <= 0 || area.strength <= 0) {
            expired.push_back(areaId);
            continue;
        }
        const WarheadType* warhead = rules.findWarheadType(area.warheadId);
        if (tick >= area.nextApplicationTick) {
            area.nextApplicationTick = tick + area.damageInterval;
            for (const WorldObjectInstance& target : objects) {
                if (!target.alive || target.health <= 0) {
                    continue;
                }
                const double falloff = runtimeRadiationFalloff(
                    static_cast<double>(target.x) - area.center.x,
                    static_cast<double>(target.y) - area.center.y, area.radius);
                if (falloff <= 0.0 || target.airborne
                    || targetImmuneToRadiation(rules, target)) {
                    continue;
                }
                if (warhead != nullptr) {
                    const bool categoryAllowed =
                        (target.kind == WorldObjectKind::Infantry
                            && warhead->profile.damagesInfantry)
                        || (target.kind == WorldObjectKind::Structure
                            && warhead->profile.damagesBuildings)
                        || (target.kind == WorldObjectKind::Unit
                            && warhead->profile.damagesVehicles);
                    if (!categoryAllowed) {
                        continue;
                    }
                }
                const int intensity = std::clamp(
                    static_cast<int>(std::lround(area.strength * falloff)),
                    0,
                    area.maximumStrength
                );
                if (intensity <= 0) continue;
                // RadEruptionWeapon.Damage is only the firing prerequisite.
                // Retail FootClass uses cell radiation * RadLevelFactor, then
                // receives the result through RadSite's normal armor rules.
                const double scaledDamage = intensity * rules_.radiationLevelFactor;
                const int damage = !std::isfinite(scaledDamage)
                        || scaledDamage >= static_cast<double>(std::numeric_limits<int>::max())
                    ? std::numeric_limits<int>::max()
                    : static_cast<int>(std::max(0.0, scaledDamage));
                if (damage <= 0) continue;
                auto found = mergedDamage.find(target.instanceId);
                if (found == mergedDamage.end()
                    || damage > found->second.damage) {
                    mergedDamage[target.instanceId] = {
                        areaId,
                        target.instanceId,
                        damage,
                        intensity,
                        area.sourceObjectId,
                        area.owner,
                        area.warheadId
                    };
                }
            }
        }
        if (tick >= area.nextDecayTick) {
            area.strength = std::max(0, area.strength - area.decayAmount);
            area.nextDecayTick = tick + std::max(1, rules_.radiationLevelDelay);
            if (emit != nullptr) {
                emit({
                    "RadiationAreaDecayed",
                    tick,
                    area.sourceObjectId,
                    -1,
                    area.strength,
                    area.center,
                    std::to_string(areaId)
                });
            }
        }
        --area.remainingTicks;
        if (area.remainingTicks <= 0 || area.strength <= 0) {
            expired.push_back(areaId);
        }
    }
    for (int areaId : expired) {
        if (emit != nullptr) {
            const auto found = radiationAreas_.find(areaId);
            if (found != radiationAreas_.end()) {
                emit({
                    "RadiationAreaRemoved",
                    tick,
                    found->second.sourceObjectId,
                    -1,
                    0,
                    found->second.center,
                    "expired"
                });
            }
        }
        radiationAreas_.erase(areaId);
    }
    std::vector<RadiationDamageEvent> result;
    result.reserve(mergedDamage.size());
    for (const auto& [unusedTarget, damage] : mergedDamage) {
        (void)unusedTarget;
        result.push_back(damage);
    }
    return result;
}

bool RuntimeSpecialMechanismState::tryPlantC4(
    const std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int sourceObjectId,
    int targetObjectId,
    int tick,
    int fixedTickHz,
    const SpecialEventSink& emit,
    std::string& reason
) {
    return timedBombs_.tryPlantC4(timedBombObjectQuery(objects), timedBombEligibilityQuery(objects, rules), timedBombRules(rules_), sourceObjectId, targetObjectId, tick, fixedTickHz, emit, reason);
}

bool RuntimeSpecialMechanismState::tryAttachIvanBomb(
    const std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int sourceObjectId,
    int targetObjectId,
    int tick,
    int fixedTickHz,
    const SpecialEventSink& emit,
    std::string& reason
) {
    return timedBombs_.tryAttachIvanBomb(timedBombObjectQuery(objects), timedBombEligibilityQuery(objects, rules), timedBombRules(rules_), sourceObjectId, targetObjectId, tick, fixedTickHz, emit, reason);
}

bool RuntimeSpecialMechanismState::tryDisarmTimedBomb(
    const std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int sourceObjectId,
    int targetObjectId,
    const std::string& warheadId,
    int tick,
    const SpecialEventSink& emit,
    std::string& reason
) {
    return timedBombs_.tryDisarmTimedBomb(timedBombObjectQuery(objects), [&rules](const std::string& id) { const auto* warhead = rules.findWarheadType(id); return warhead != nullptr && warhead->bombDisarm; }, sourceObjectId, targetObjectId, warheadId, tick, emit, reason);
}

std::vector<SpecialMechanismAction>
RuntimeSpecialMechanismState::tickTimedBombs(
    int tick,
    const SpecialEventSink& emit
) {
    return timedBombs_.tickTimedBombs(tick, emit);
}

ParasiteOperationResult RuntimeSpecialMechanismState::beginParasiteLaunch(
    const ParasiteLaunchRequest& request,
    const SpecialEventSink& emit
) {
    ParasiteOperationResult result;
    const auto reject = [&](const std::string& reason) {
        result.reason = reason;
        if (emit != nullptr) {
            emit({
                "ParasiteLaunchRejected",
                request.currentTick,
                request.sourceObjectId,
                request.targetObjectId,
                request.projectileId,
                request.targetCell,
                reason
            });
        }
        return result;
    };
    for (auto resolved = parasiteResolvedByProjectile_.begin();
         resolved != parasiteResolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < request.currentTick) {
            resolved = parasiteResolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    if (request.currentTick < 0 || request.projectileId == 0
        || request.sourceObjectId < 0 || request.targetObjectId < 0
        || request.sourceObjectId == request.targetObjectId) {
        return reject("invalid parasite launch identity");
    }
    if (!validParasiteText(request.sourceOwner)
        || !validParasiteText(request.sourceTypeId)
        || !validParasiteText(request.targetTypeId)
        || !validParasiteText(request.weaponId)
        || !validParasiteText(request.warheadId)) {
        return reject("invalid parasite launch text snapshot");
    }
    if (request.damage < 0 || request.damageIntervalTicks <= 0
        || request.paralyzes < 0 || request.targetMaximumHealth <= 0
        || !validParasiteRatio(request.normalCullingThresholdPermyriad)
        || !validParasiteRatio(request.eliteCullingThresholdPermyriad)) {
        return reject("invalid parasite launch rules snapshot");
    }
    if (request.targetKind == ParasiteTargetKind::Vehicle
        && !request.targetParasiteable) {
        return reject("vehicle target is not parasiteable");
    }
    const auto existingProjectile = parasitePendingByProjectile_.find(
        request.projectileId
    );
    if (existingProjectile != parasitePendingByProjectile_.end()) {
        const ParasitePendingLaunchState& pending = existingProjectile->second;
        if (pending.sourceObjectId == request.sourceObjectId
            && pending.targetObjectId == request.targetObjectId
            && pending.sourceTypeId == request.sourceTypeId
            && pending.targetTypeId == request.targetTypeId
            && pending.weaponId == request.weaponId
            && pending.warheadId == request.warheadId) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        return reject("projectile identity already belongs to another launch");
    }
    const auto resolved = parasiteResolvedByProjectile_.find(
        request.projectileId
    );
    if (resolved != parasiteResolvedByProjectile_.end()
        && resolved->second.resolvedTick == request.currentTick) {
        if (resolved->second.sourceObjectId == request.sourceObjectId
            && resolved->second.targetObjectId == request.targetObjectId) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        return reject("resolved projectile identity conflicts with launch");
    }
    if (parasitePendingByProjectile_.size()
            + parasiteResolvedByProjectile_.size()
        >= kMaximumParasiteRecords) {
        return reject("parasite launch/resolution capacity reached");
    }
    for (const auto& [unusedProjectileId, pending]
         : parasitePendingByProjectile_) {
        (void)unusedProjectileId;
        if (pending.sourceObjectId == request.sourceObjectId) {
            return reject("parasite source already owns an in-flight launch");
        }
        if (pending.targetObjectId == request.targetObjectId) {
            return reject("parasite target is already being boarded");
        }
    }
    for (const auto& [targetId, link] : parasiteByTarget_) {
        if (link.sourceObjectId == request.sourceObjectId) {
            return reject("parasite source is already attached");
        }
        if (targetId == request.targetObjectId) {
            return reject("parasite target already has a parasite");
        }
    }

    ParasitePendingLaunchState pending;
    pending.projectileId = request.projectileId;
    pending.sourceObjectId = request.sourceObjectId;
    pending.targetObjectId = request.targetObjectId;
    pending.owner = request.sourceOwner;
    pending.sourceTypeId = request.sourceTypeId;
    pending.targetTypeId = request.targetTypeId;
    pending.weaponId = request.weaponId;
    pending.warheadId = request.warheadId;
    pending.targetKind = request.targetKind;
    pending.targetCell = request.targetCell;
    pending.launchedTick = request.currentTick;
    pending.damage = request.damage;
    pending.damageIntervalTicks = request.damageIntervalTicks;
    pending.paralyzes = request.paralyzes;
    pending.targetMaximumHealth = request.targetMaximumHealth;
    pending.cullingThresholdPerMyriad = request.sourceElite
        ? request.eliteCullingThresholdPermyriad
        : request.normalCullingThresholdPermyriad;
    pending.targetParasiteable = request.targetParasiteable;
    pending.organic = request.sourceOrganic;
    pending.sourceElite = request.sourceElite;
    pending.culling = request.culling;
    parasitePendingByProjectile_[pending.projectileId] = pending;

    SpecialMechanismAction enterLimbo;
    enterLimbo.kind = SpecialMechanismActionKind::ParasiteEnterLimbo;
    enterLimbo.tick = request.currentTick;
    enterLimbo.projectileId = request.projectileId;
    enterLimbo.objectId = request.sourceObjectId;
    enterLimbo.targetObjectId = request.targetObjectId;
    enterLimbo.owner = request.sourceOwner;
    enterLimbo.damage = request.damage;
    enterLimbo.warheadId = request.warheadId;
    enterLimbo.paralyzes = request.paralyzes;
    enterLimbo.culling = request.culling;
    enterLimbo.cell = request.targetCell;
    enterLimbo.reason = "LimboLaunch projectile began";
    result.actions.push_back(std::move(enterLimbo));
    result.accepted = true;
    if (emit != nullptr) {
        emit({
            "ParasiteLaunchBegan",
            request.currentTick,
            request.sourceObjectId,
            request.targetObjectId,
            request.projectileId,
            request.targetCell,
            request.weaponId
        });
    }
    return result;
}

ParasiteOperationResult RuntimeSpecialMechanismState::resolveParasiteImpact(
    const ParasiteImpactRequest& request,
    const SpecialEventSink& emit
) {
    ParasiteOperationResult result;
    const auto reject = [&](const std::string& reason) {
        result.reason = reason;
        return result;
    };
    for (auto resolved = parasiteResolvedByProjectile_.begin();
         resolved != parasiteResolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < request.currentTick) {
            resolved = parasiteResolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    if (request.currentTick < 0 || request.projectileId == 0) {
        return reject("invalid parasite impact identity");
    }
    const auto pendingIt = parasitePendingByProjectile_.find(
        request.projectileId
    );
    if (pendingIt == parasitePendingByProjectile_.end()) {
        const auto resolved = parasiteResolvedByProjectile_.find(
            request.projectileId
        );
        if (resolved != parasiteResolvedByProjectile_.end()
            && resolved->second.resolvedTick == request.currentTick) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        return reject("parasite projectile has no pending launch");
    }
    const ParasitePendingLaunchState pending = pendingIt->second;
    if (request.currentTick < pending.launchedTick) {
        return reject("parasite impact precedes launch");
    }
    if (request.targetPresent
        && (!validParasiteText(request.targetTypeId)
            || request.targetMaximumHealth <= 0)) {
        return reject("invalid parasite impact target snapshot");
    }
    if (pending.damageIntervalTicks > 0
        && request.currentTick
            > std::numeric_limits<int>::max()
                - pending.damageIntervalTicks) {
        return reject("parasite impact tick would overflow bite interval");
    }

    const bool identityMatches = request.targetPresent
        && request.targetObjectId == pending.targetObjectId
        && request.targetTypeId == pending.targetTypeId
        && request.targetKind == pending.targetKind;
    const bool liveHit = request.impact == ParasiteImpactKind::Hit
        && identityMatches && request.targetAlive;
    const bool vehicleHit = liveHit
        && pending.targetKind == ParasiteTargetKind::Vehicle
        && pending.targetParasiteable && request.targetParasiteable;
    const bool infantryHit = liveHit
        && pending.targetKind == ParasiteTargetKind::Infantry;

    const auto appendReturnOrKill = [&](const std::string& reason,
                                      bool replacesTerminatingHost = false) {
        SpecialMechanismAction action;
        action.kind = request.returnCellAvailable
            ? SpecialMechanismActionKind::ParasiteReturnFromLimbo
            : SpecialMechanismActionKind::ParasiteKillParasite;
        action.tick = request.currentTick;
        action.projectileId = pending.projectileId;
        action.objectId = pending.sourceObjectId;
        action.targetObjectId = pending.targetObjectId;
        action.owner = pending.owner;
        action.paralyzes = pending.paralyzes;
        action.cell = request.returnCellAvailable
            ? request.returnCell
            : request.impactCell;
        action.suppressDeathWeapon = !request.returnCellAvailable;
        action.replacesTerminatingHost = request.returnCellAvailable
            && replacesTerminatingHost;
        action.reason = request.returnCellAvailable
            ? reason
            : reason + "; no legal return cell";
        result.actions.push_back(std::move(action));
    };

    if (vehicleHit) {
        if (parasiteByTarget_.size() >= kMaximumParasiteRecords
            || parasiteByTarget_.find(pending.targetObjectId)
                != parasiteByTarget_.end()) {
            return reject("parasite link capacity or target conflict");
        }
        ParasiteLinkState link;
        link.projectileId = pending.projectileId;
        link.targetObjectId = pending.targetObjectId;
        link.sourceObjectId = pending.sourceObjectId;
        link.owner = pending.owner;
        link.sourceTypeId = pending.sourceTypeId;
        link.targetTypeId = pending.targetTypeId;
        link.weaponId = pending.weaponId;
        link.warheadId = pending.warheadId;
        link.damage = pending.damage;
        link.attachedTick = request.currentTick;
        link.damageIntervalTicks = pending.damageIntervalTicks;
        link.nextDamageTick = pending.organic
            ? request.currentTick + pending.damageIntervalTicks
            : request.currentTick;
        link.organic = pending.organic;
        link.sourceElite = pending.sourceElite;
        link.paralyzes = pending.paralyzes;
        link.culling = pending.culling;
        link.cullingThresholdPerMyriad = pending.cullingThresholdPerMyriad;
        link.targetMaximumHealth = std::max(
            1,
            request.targetMaximumHealth
        );
        link.sourceExpectedInLimbo = true;
        parasiteByTarget_[link.targetObjectId] = link;

        SpecialMechanismAction attached;
        attached.kind = SpecialMechanismActionKind::ParasiteAttachHost;
        attached.tick = request.currentTick;
        attached.projectileId = pending.projectileId;
        attached.objectId = pending.sourceObjectId;
        attached.targetObjectId = pending.targetObjectId;
        attached.owner = pending.owner;
        attached.damage = pending.damage;
        attached.warheadId = pending.warheadId;
        attached.paralyzes = pending.paralyzes;
        attached.culling = pending.culling;
        attached.cell = request.impactCell;
        attached.reason = "LimboLaunch parasite attached to vehicle";
        result.actions.push_back(std::move(attached));
    } else if (infantryHit) {
        SpecialMechanismAction kill;
        kill.kind = SpecialMechanismActionKind::ParasiteKillTarget;
        kill.tick = request.currentTick;
        kill.projectileId = pending.projectileId;
        kill.objectId = pending.sourceObjectId;
        kill.targetObjectId = pending.targetObjectId;
        kill.owner = pending.owner;
        kill.damage = std::numeric_limits<int>::max();
        kill.warheadId = pending.warheadId;
        kill.forceDestruction = true;
        kill.cell = request.impactCell;
        kill.reason = "parasite warhead instantly kills infantry";
        result.actions.push_back(std::move(kill));
        appendReturnOrKill("parasite returned after infantry impact", true);
    } else {
        appendReturnOrKill(
            identityMatches
                ? "parasite returned after invalid or dead target"
                : "parasite returned after miss or identity mismatch"
        );
    }

    parasitePendingByProjectile_.erase(pendingIt);
    parasiteResolvedByProjectile_[request.projectileId] = {
        request.projectileId,
        pending.sourceObjectId,
        pending.targetObjectId,
        request.currentTick
    };
    result.accepted = true;
    if (emit != nullptr) {
        emit({
            vehicleHit ? "ParasiteAttached"
                : (infantryHit ? "ParasiteInfantryKilled"
                               : "ParasiteReturned"),
            request.currentTick,
            pending.sourceObjectId,
            pending.targetObjectId,
            request.projectileId,
            request.impactCell,
            pending.warheadId
        });
    }
    return result;
}

ParasiteOperationResult RuntimeSpecialMechanismState::handleParasiteHostEvent(
    const ParasiteHostEventRequest& request,
    const SpecialEventSink& emit
) {
    ParasiteOperationResult result;
    if (request.currentTick < 0 || request.targetObjectId < 0
        || request.stunTicks < 0) {
        result.reason = "invalid parasite host event";
        return result;
    }
    for (auto resolved = parasiteResolvedByProjectile_.begin();
         resolved != parasiteResolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < request.currentTick) {
            resolved = parasiteResolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    const auto linkIt = parasiteByTarget_.find(request.targetObjectId);
    if (linkIt == parasiteByTarget_.end()) {
        const auto repeated = std::find_if(
            parasiteResolvedByProjectile_.begin(),
            parasiteResolvedByProjectile_.end(),
            [&request](const auto& entry) {
                return entry.second.targetObjectId == request.targetObjectId
                    && entry.second.resolvedTick == request.currentTick
                    && entry.second.hostTerminated;
            }
        );
        if (repeated != parasiteResolvedByProjectile_.end()) {
            result.accepted = true;
            result.idempotent = true;
            return result;
        }
        result.reason = "target has no attached parasite";
        return result;
    }
    const ParasiteLinkState link = linkIt->second;
    const bool killDirectly =
        request.event == ParasiteHostEventKind::DestroyedExternally
        || (request.event == ParasiteHostEventKind::Healed && !link.organic);
    const bool canEject = !killDirectly && request.ejectCellAvailable;

    SpecialMechanismAction action;
    action.kind = canEject
        ? SpecialMechanismActionKind::ParasiteEject
        : SpecialMechanismActionKind::ParasiteKillParasite;
    action.tick = request.currentTick;
    action.projectileId = link.projectileId;
    action.objectId = link.sourceObjectId;
    action.targetObjectId = link.targetObjectId;
    action.owner = link.owner;
    action.paralyzes = link.paralyzes;
    action.cell = canEject ? request.ejectCell : SpecialCell{};
    action.suppressDeathWeapon = !canEject;
    action.replacesTerminatingHost = canEject
        && request.event == ParasiteHostEventKind::DestroyedByParasite;
    action.sourceWasAttachedToHost = true;
    const bool stunned = canEject
        && (request.event == ParasiteHostEventKind::Healed
            || request.event == ParasiteHostEventKind::SonicHit
            || request.event == ParasiteHostEventKind::Teleporting);
    action.stunTicks = stunned ? request.stunTicks : 0;
    action.reason = canEject
        ? "parasite ejected because host was "
            + parasiteHostEventName(request.event)
        : "parasite destroyed without death weapon because host was "
            + parasiteHostEventName(request.event)
            + (request.ejectCellAvailable || killDirectly
                ? std::string()
                : "; no legal eject cell");
    result.actions.push_back(std::move(action));
    parasiteByTarget_.erase(linkIt);
    parasiteResolvedByProjectile_[link.projectileId] = {
        link.projectileId,
        link.sourceObjectId,
        link.targetObjectId,
        request.currentTick,
        true
    };
    result.accepted = true;
    if (emit != nullptr) {
        emit({
            canEject ? "ParasiteEjected" : "ParasiteKilled",
            request.currentTick,
            link.sourceObjectId,
            link.targetObjectId,
            0,
            canEject ? request.ejectCell : SpecialCell{},
            parasiteHostEventName(request.event)
        });
    }
    return result;
}

std::vector<SpecialMechanismAction>
RuntimeSpecialMechanismState::reconcileParasites(
    const std::vector<WorldObjectInstance>& objects,
    const std::vector<int>& activeProjectileIds,
    int tick,
    const SpecialEventSink& emit
) {
    std::vector<SpecialMechanismAction> actions;
    const std::set<int> activeProjectiles(
        activeProjectileIds.begin(),
        activeProjectileIds.end()
    );
    for (auto pending = parasitePendingByProjectile_.begin();
         pending != parasitePendingByProjectile_.end();) {
        const ParasitePendingLaunchState state = pending->second;
        const WorldObjectInstance* source = findObject(
            objects,
            state.sourceObjectId
        );
        const WorldObjectInstance* target = findObject(
            objects,
            state.targetObjectId
        );
        const bool sourceValid = source != nullptr && source->health > 0
            && source->type == state.sourceTypeId
            && ((!source->alive && source->mission == "ParasiteLimbo")
                || (source->alive && tick == state.launchedTick));
        const bool targetValid = target != nullptr && target->alive
            && target->health > 0 && target->type == state.targetTypeId;
        const bool projectileValid = activeProjectiles.find(state.projectileId)
            != activeProjectiles.end();
        if (sourceValid && targetValid && projectileValid) {
            ++pending;
            continue;
        }
        SpecialMechanismAction action;
        action.kind = sourceValid
            ? SpecialMechanismActionKind::ParasiteReturnFromLimbo
            : SpecialMechanismActionKind::ParasiteKillParasite;
        action.tick = tick;
        action.projectileId = state.projectileId;
        action.objectId = state.sourceObjectId;
        action.targetObjectId = state.targetObjectId;
        action.owner = state.owner;
        action.paralyzes = state.paralyzes;
        action.cell = target == nullptr ? state.targetCell : objectCell(target);
        action.suppressDeathWeapon = !sourceValid;
        action.reason = !sourceValid
            ? "pending parasite source invalid during reconciliation"
            : (!targetValid
                ? "pending parasite target invalid during reconciliation"
                : "pending parasite projectile missing during reconciliation");
        actions.push_back(std::move(action));
        parasiteResolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick
        };
        if (emit != nullptr) {
            emit({
                sourceValid ? "ParasiteReturned" : "ParasiteKilled",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                state.projectileId,
                target == nullptr ? state.targetCell : objectCell(target),
                "pending launch reconciled"
            });
        }
        pending = parasitePendingByProjectile_.erase(pending);
    }
    for (auto link = parasiteByTarget_.begin();
         link != parasiteByTarget_.end();) {
        const ParasiteLinkState state = link->second;
        const WorldObjectInstance* source = findObject(
            objects,
            state.sourceObjectId
        );
        const WorldObjectInstance* target = findObject(
            objects,
            state.targetObjectId
        );
        const bool sourceValid = source != nullptr && source->health > 0
            && (state.sourceTypeId.empty()
                || source->type == state.sourceTypeId)
            && (!state.sourceExpectedInLimbo
                || (!source->alive && source->mission == "ParasiteLimbo"));
        const bool targetValid = target != nullptr && target->alive
            && target->health > 0
            && (state.targetTypeId.empty()
                || target->type == state.targetTypeId);
        if (sourceValid && targetValid) {
            ++link;
            continue;
        }
        SpecialMechanismAction action;
        action.kind = sourceValid
            ? SpecialMechanismActionKind::ParasiteKillParasite
            : SpecialMechanismActionKind::ParasiteReleaseTarget;
        action.tick = tick;
        action.projectileId = state.projectileId;
        action.objectId = state.sourceObjectId;
        action.targetObjectId = state.targetObjectId;
        action.owner = state.owner;
        action.paralyzes = state.paralyzes;
        action.cell = objectCell(target);
        action.suppressDeathWeapon = sourceValid;
        action.sourceWasAttachedToHost = sourceValid;
        action.reason = sourceValid
            ? "parasite host invalid during reconciliation"
            : "parasite source invalid during reconciliation";
        actions.push_back(std::move(action));
        parasiteResolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick,
            sourceValid
        };
        link = parasiteByTarget_.erase(link);
    }
    return actions;
}

bool RuntimeSpecialMechanismState::tryAttachParasite(
    const std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int sourceObjectId,
    int targetObjectId,
    const std::string& weaponId,
    int tick,
    int fixedTickHz,
    SpecialMechanismAction& enterLimboAction,
    const SpecialEventSink& emit,
    std::string& reason
) {
    enterLimboAction = {};
    const WorldObjectInstance* source = findObject(objects, sourceObjectId);
    const WorldObjectInstance* target = findObject(objects, targetObjectId);
    const WeaponType* weapon = rules.findWeaponType(weaponId);
    const WarheadType* warhead = weapon == nullptr
        ? nullptr
        : rules.findWarheadType(weapon->warhead);
    const UnitType* sourceType = source == nullptr
        ? nullptr
        : unitTypeFor(rules, *source);
    const UnitType* targetType = target == nullptr
        ? nullptr
        : unitTypeFor(rules, *target);
    if (source == nullptr || !source->alive || source->health <= 0
        || target == nullptr || !target->alive || target->health <= 0
        || sourceObjectId == targetObjectId || weapon == nullptr
        || !weapon->limboLaunch || warhead == nullptr || !warhead->parasite
        || targetType == nullptr
        || (target->kind != WorldObjectKind::Infantry
            && !targetType->parasiteable)) {
        reason = "invalid legacy parasite attach request";
        return false;
    }
    ParasiteLaunchRequest launch;
    launch.projectileId = -sourceObjectId - 1;
    launch.currentTick = tick;
    launch.sourceObjectId = sourceObjectId;
    launch.targetObjectId = targetObjectId;
    launch.sourceOwner = source->owner;
    launch.sourceTypeId = source->type;
    launch.targetTypeId = target->type;
    launch.weaponId = weapon->id;
    launch.warheadId = warhead->id;
    launch.targetKind = target->kind == WorldObjectKind::Infantry
        ? ParasiteTargetKind::Infantry
        : ParasiteTargetKind::Vehicle;
    launch.targetCell = objectCell(target);
    launch.damage = std::max(0, weapon->damage);
    launch.sourceOrganic = sourceType != nullptr && sourceType->organic;
    launch.sourceElite = source->veterancy >= 2;
    launch.damageIntervalTicks = convertRuleFrames(
        launch.sourceOrganic ? 36 : std::max(1, weapon->rof),
        fixedTickHz
    );
    launch.paralyzes = std::max(0, warhead->paralyzes);
    launch.targetMaximumHealth = std::max(1, targetType->strength);
    launch.targetParasiteable = targetType->parasiteable;
    launch.culling = warhead->culling;
    launch.normalCullingThresholdPermyriad = static_cast<int>(std::lround(
        std::clamp(rules_.conditionRed, 0.0, 1.0)
        * kParasiteRatioPerMyriad
    ));
    launch.eliteCullingThresholdPermyriad =
        launch.normalCullingThresholdPermyriad;
    ParasiteOperationResult began = beginParasiteLaunch(launch, emit);
    if (!began.accepted || began.actions.size() != 1) {
        reason = began.reason;
        return false;
    }
    enterLimboAction = began.actions.front();

    ParasiteImpactRequest impact;
    impact.projectileId = launch.projectileId;
    impact.currentTick = tick;
    impact.impact = ParasiteImpactKind::Hit;
    impact.targetObjectId = targetObjectId;
    impact.targetTypeId = target->type;
    impact.targetKind = launch.targetKind;
    impact.targetPresent = true;
    impact.targetAlive = true;
    impact.targetParasiteable = targetType->parasiteable;
    impact.targetMaximumHealth = targetType->strength;
    impact.impactCell = objectCell(target);
    impact.returnCellAvailable = true;
    impact.returnCell = objectCell(target);
    ParasiteOperationResult resolved = resolveParasiteImpact(impact, emit);
    if (!resolved.accepted) {
        reason = resolved.reason;
        return false;
    }
    reason.clear();
    return true;
}

std::vector<SpecialMechanismAction>
RuntimeSpecialMechanismState::tickParasites(
    const std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int tick,
    const SpecialEventSink& emit
) {
    (void)rules;
    std::vector<SpecialMechanismAction> actions;
    for (auto resolved = parasiteResolvedByProjectile_.begin();
         resolved != parasiteResolvedByProjectile_.end();) {
        if (resolved->second.resolvedTick < tick) {
            resolved = parasiteResolvedByProjectile_.erase(resolved);
        } else {
            ++resolved;
        }
    }
    for (auto entry = parasiteByTarget_.begin();
         entry != parasiteByTarget_.end();) {
        ParasiteLinkState& link = entry->second;
        const WorldObjectInstance* source = findObject(
            objects,
            link.sourceObjectId
        );
        const WorldObjectInstance* target = findObject(
            objects,
            link.targetObjectId
        );
        const bool sourceDestroyed = source == nullptr || source->health <= 0
            || (!link.sourceTypeId.empty()
                && source->type != link.sourceTypeId)
            || (!source->alive
                && (!link.sourceExpectedInLimbo
                    || source->mission != "ParasiteLimbo"));
        if (sourceDestroyed) {
            SpecialMechanismAction action;
            action.kind = SpecialMechanismActionKind::ParasiteReleaseTarget;
            action.tick = tick;
            action.projectileId = link.projectileId;
            action.objectId = link.sourceObjectId;
            action.targetObjectId = link.targetObjectId;
            action.owner = link.owner;
            action.paralyzes = link.paralyzes;
            action.cell = objectCell(target);
            action.reason = "parasite source died or left the world";
            actions.push_back(std::move(action));
            parasiteResolvedByProjectile_[link.projectileId] = {
                link.projectileId,
                link.sourceObjectId,
                link.targetObjectId,
                tick,
                false
            };
            entry = parasiteByTarget_.erase(entry);
            continue;
        }
        if (target == nullptr || !target->alive || target->health <= 0
            || target->kind != WorldObjectKind::Unit
            || (!link.targetTypeId.empty()
                && target->type != link.targetTypeId)) {
            SpecialMechanismAction action;
            action.kind = SpecialMechanismActionKind::ParasiteKillParasite;
            action.tick = tick;
            action.projectileId = link.projectileId;
            action.objectId = link.sourceObjectId;
            action.targetObjectId = link.targetObjectId;
            action.owner = link.owner;
            action.paralyzes = link.paralyzes;
            action.cell = objectCell(target);
            action.suppressDeathWeapon = true;
            action.sourceWasAttachedToHost = true;
            action.reason = "parasite host died externally or became invalid";
            actions.push_back(std::move(action));
            parasiteResolvedByProjectile_[link.projectileId] = {
                link.projectileId,
                link.sourceObjectId,
                link.targetObjectId,
                tick,
                true
            };
            entry = parasiteByTarget_.erase(entry);
            continue;
        }
        if (tick >= link.nextDamageTick) {
            const bool culled = link.culling
                && link.cullingThresholdPerMyriad > 0
                && static_cast<long long>(target->health)
                    * kParasiteRatioPerMyriad
                    <= static_cast<long long>(link.targetMaximumHealth)
                        * link.cullingThresholdPerMyriad;
            SpecialMechanismAction action;
            action.kind = culled
                ? SpecialMechanismActionKind::ParasiteKillTarget
                : SpecialMechanismActionKind::ParasiteDamage;
            action.tick = tick;
            action.projectileId = link.projectileId;
            action.objectId = link.sourceObjectId;
            action.targetObjectId = link.targetObjectId;
            action.owner = link.owner;
            action.damage = culled
                ? std::numeric_limits<int>::max()
                : link.damage;
            action.warheadId = link.warheadId;
            action.forceDestruction = culled;
            action.paralyzes = link.paralyzes;
            action.culling = link.culling;
            action.cell = objectCell(target);
            action.reason = culled
                ? (link.sourceElite ? "elite parasite culling threshold"
                                    : "parasite culling threshold")
                : "parasite damage interval";
            actions.push_back(std::move(action));
            link.nextDamageTick = tick
                    > std::numeric_limits<int>::max()
                        - std::max(1, link.damageIntervalTicks)
                ? std::numeric_limits<int>::max()
                : tick + std::max(1, link.damageIntervalTicks);
            if (emit != nullptr) {
                emit({
                    culled ? "ParasiteCulling" : "ParasiteDamage",
                    tick,
                    link.sourceObjectId,
                    link.targetObjectId,
                    culled ? target->health : link.damage,
                    objectCell(target),
                    link.warheadId
                });
            }
        }
        ++entry;
    }
    return actions;
}

bool RuntimeSpecialMechanismState::isParasiteSourceLimboed(
    int objectId
) const {
    for (const auto& [unusedProjectileId, pending]
         : parasitePendingByProjectile_) {
        (void)unusedProjectileId;
        if (pending.sourceObjectId == objectId) {
            return true;
        }
    }
    for (const auto& [unusedTargetId, link] : parasiteByTarget_) {
        (void)unusedTargetId;
        if (link.sourceObjectId == objectId) {
            return true;
        }
    }
    return false;
}

int RuntimeSpecialMechanismState::parasiteTargetForSource(
    int objectId
) const {
    for (const auto& [unusedProjectileId, pending]
         : parasitePendingByProjectile_) {
        (void)unusedProjectileId;
        if (pending.sourceObjectId == objectId) {
            return pending.targetObjectId;
        }
    }
    for (const auto& [targetId, link] : parasiteByTarget_) {
        if (link.sourceObjectId == objectId) {
            return targetId;
        }
    }
    return -1;
}

bool RuntimeSpecialMechanismState::isParasiteHostParalyzed(
    int objectId
) const {
    const auto found = parasiteByTarget_.find(objectId);
    return found != parasiteByTarget_.end() && found->second.paralyzes != 0;
}

void RuntimeSpecialMechanismState::ensureCarrierParents(
    const std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int tick
) {
    for (const WorldObjectInstance& object : objects) {
        if (!object.alive || object.kind == WorldObjectKind::Structure) {
            continue;
        }
        const UnitType* unit = unitTypeFor(rules, object);
        if (unit == nullptr || unit->spawns.empty() || unit->spawnsNumber <= 0) {
            continue;
        }
        CarrierParentState& parent = carrierParents_[object.instanceId];
        parent.parentObjectId = object.instanceId;
        parent.childType = unit->spawns;
        parent.owner = object.owner;
        parent.capacity = std::max(1, unit->spawnsNumber);
        parent.regenRate = std::max(0, unit->spawnRegenRate);
        parent.reloadRate = std::max(0, unit->spawnReloadRate);
        for (int slot = 0; slot < parent.capacity; ++slot) {
            parent.childBySlot.try_emplace(slot, -1);
            parent.slotReadyTick.try_emplace(slot, tick);
        }
    }
}

std::vector<CarrierAction> RuntimeSpecialMechanismState::tickCarriers(
    const std::vector<WorldObjectInstance>& objects,
    const RulesDatabase& rules,
    int tick,
    const std::function<int(int)>& parentTarget,
    const SpecialEventSink& emit
) {
    ensureCarrierParents(objects, rules, tick);
    std::vector<CarrierAction> actions;
    for (auto& [parentId, parent] : carrierParents_) {
        const WorldObjectInstance* parentObject = findObject(objects, parentId);
        if (parentObject == nullptr || !parentObject->alive) {
            for (const auto& [slot, childId] : parent.childBySlot) {
                if (childId >= 0) {
                    actions.push_back({
                        CarrierAction::Kind::Destroy,
                        parentId,
                        childId,
                        slot,
                        -1,
                        parent.childType,
                        parent.owner
                    });
                }
            }
            continue;
        }
        const bool continuousHornet = equalsAsciiIgnoreCase(parentObject->type, "CARRIER")
            && equalsAsciiIgnoreCase(parent.childType, "HORNET");
        const bool threeRoundSortie = continuousHornet
            || (equalsAsciiIgnoreCase(parentObject->type, "DEST")
                && equalsAsciiIgnoreCase(parent.childType, "ASW"));
        const int targetId = parentTarget == nullptr
            ? -1
            : parentTarget(parentId);
        SpecialCell targetCell;
        bool bridgeTarget = false;
        const bool hasTarget = targetId > 0
            || decodeRuntimeCarrierCellTarget(targetId, targetCell, bridgeTarget);
        for (auto& [slot, childId] : parent.childBySlot) {
            const int readyTick = parent.slotReadyTick[slot];
            if (childId < 0) {
                if (tick >= readyTick) {
                    actions.push_back({
                        CarrierAction::Kind::Spawn,
                        parentId,
                        -1,
                        slot,
                        -1,
                        parent.childType,
                        parent.owner
                    });
                    parent.slotReadyTick[slot] = std::numeric_limits<int>::max();
                }
                continue;
            }
            auto childIt = carrierChildren_.find(childId);
            if (childIt == carrierChildren_.end()) {
                childId = -1;
                parent.slotReadyTick[slot] = tick + parent.regenRate;
                continue;
            }
            CarrierChildState& child = childIt->second;
            // Missile slots are fired at the parent's validated weapon/ROF
            // boundary, then use their own launch and flight state machine.
            if (rules.findSpawnedMissileProfile(parent.childType) != nullptr) continue;
            if ((child.phase == CarrierChildPhase::Launching
                    || child.phase == CarrierChildPhase::Attacking)
                && hasTarget) {
                child.targetObjectId = targetId;
            } else if ((child.phase == CarrierChildPhase::Launching
                    || child.phase == CarrierChildPhase::Attacking)
                && !hasTarget) {
                requestCarrierChildReturn(childId, tick, emit, "parent target cleared");
            }
            if (child.phase == CarrierChildPhase::Launching
                && !continuousHornet && tick >= child.actionTick) {
                child.phase = CarrierChildPhase::Attacking;
                child.actionTick = tick;
            }
            if (child.phase == CarrierChildPhase::Docked
                && hasTarget
                && tick >= readyTick) {
                child.phase = CarrierChildPhase::Launching;
                child.targetObjectId = targetId;
                child.actionTick = tick + 1;
                // A new docked sortie refills its own budget. An already
                // airborne legacy seven-field save retains its one round.
                child.sortieShotsRemaining = threeRoundSortie ? 3 : 1;
                actions.push_back({
                    CarrierAction::Kind::Launch,
                    parentId,
                    childId,
                    slot,
                    targetId,
                    parent.childType,
                    parent.owner
                });
                if (emit != nullptr) {
                    emit({
                        "CarrierChildLaunch",
                        tick,
                        parentId,
                        childId,
                        slot,
                        parentObject->x == -1
                            ? SpecialCell{}
                            : SpecialCell{parentObject->x, parentObject->y},
                        std::to_string(targetId)
                    });
                }
            }
        }
    }
    return actions;
}

bool RuntimeSpecialMechanismState::attachCarrierChild(
    int parentObjectId,
    int slot,
    int childObjectId,
    int tick,
    const SpecialEventSink& emit
) {
    const auto parentIt = carrierParents_.find(parentObjectId);
    if (parentIt == carrierParents_.end()
        || slot < 0 || slot >= parentIt->second.capacity) {
        return false;
    }
    CarrierParentState& parent = parentIt->second;
    parent.childBySlot[slot] = childObjectId;
    parent.slotReadyTick[slot] = tick;
    carrierChildren_[childObjectId] = {
        childObjectId,
        parentObjectId,
        slot,
        CarrierChildPhase::Docked,
        -1,
        tick,
        1
    };
    if (emit != nullptr) {
        emit({
            "CarrierChildCreated",
            tick,
            parentObjectId,
            childObjectId,
            slot,
            {},
            parent.childType
        });
    }
    return true;
}

bool RuntimeSpecialMechanismState::notifyCarrierChildLaunched(
    int childObjectId, int tick, const SpecialEventSink& emit
) {
    (void)emit;
    const auto found = carrierChildren_.find(childObjectId);
    if (found == carrierChildren_.end()
        || found->second.phase != CarrierChildPhase::Launching
        || found->second.sortieShotsRemaining <= 0 || found->second.ammoRemaining != 1) return false;
    found->second.phase = CarrierChildPhase::Attacking;
    found->second.actionTick = tick;
    return true;
}

bool RuntimeSpecialMechanismState::notifyCarrierChildFired(
    int childObjectId,
    int tick,
    const SpecialEventSink& emit
) {
    const auto childIt = carrierChildren_.find(childObjectId);
    if (childIt == carrierChildren_.end()) {
        return false;
    }
    CarrierChildState& child = childIt->second;
    if (child.sortieShotsRemaining <= 0) return false;
    --child.sortieShotsRemaining;
    if (child.sortieShotsRemaining > 0) {
        child.phase = CarrierChildPhase::Attacking;
        child.ammoRemaining = 1;
        return true;
    }
    child.phase = CarrierChildPhase::Returning;
    child.ammoRemaining = 0;
    child.actionTick = tick;
    if (emit != nullptr) {
        emit({
            "CarrierChildReturn",
            tick,
            child.parentObjectId,
            childObjectId,
            child.slot,
            {},
            "ammo exhausted"
        });
    }
    return true;
}

bool RuntimeSpecialMechanismState::requestCarrierChildReturn(
    int childObjectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    const auto childIt = carrierChildren_.find(childObjectId);
    if (childIt == carrierChildren_.end()) return false;
    CarrierChildState& child = childIt->second;
    if (child.phase != CarrierChildPhase::Launching
        && child.phase != CarrierChildPhase::Attacking) return false;
    child.phase = CarrierChildPhase::Returning;
    child.targetObjectId = -1;
    child.actionTick = tick;
    // Cancelling an unfired sortie must not spend its magazine.
    if (emit != nullptr) emit({
        "CarrierChildReturn", tick, child.parentObjectId, childObjectId,
        child.slot, {}, reason
    });
    return true;
}

bool RuntimeSpecialMechanismState::notifyCarrierChildDestroyed(
    int childObjectId,
    int tick,
    const SpecialEventSink& emit
) {
    const auto childIt = carrierChildren_.find(childObjectId);
    if (childIt == carrierChildren_.end()) {
        return false;
    }
    const CarrierChildState child = childIt->second;
    const auto parentIt = carrierParents_.find(child.parentObjectId);
    if (parentIt != carrierParents_.end()) {
        parentIt->second.childBySlot[child.slot] = -1;
        parentIt->second.slotReadyTick[child.slot] =
            tick + std::max(0, parentIt->second.regenRate);
    }
    carrierChildren_.erase(childIt);
    if (emit != nullptr) {
        emit({
            "CarrierChildDestroyed",
            tick,
            child.parentObjectId,
            childObjectId,
            child.slot,
            {},
            "regeneration scheduled"
        });
    }
    return true;
}

bool RuntimeSpecialMechanismState::notifyCarrierChildDocked(
    int childObjectId,
    int tick,
    const SpecialEventSink& emit
) {
    const auto childIt = carrierChildren_.find(childObjectId);
    if (childIt == carrierChildren_.end()) {
        return false;
    }
    CarrierChildState& child = childIt->second;
    const auto parentIt = carrierParents_.find(child.parentObjectId);
    if (parentIt == carrierParents_.end()
        || parentIt->second.childBySlot[child.slot] != childObjectId) {
        return false;
    }
    if (child.phase == CarrierChildPhase::Docked) {
        return true;
    }
    child.phase = CarrierChildPhase::Docked;
    child.targetObjectId = -1;
    child.ammoRemaining = 1;
    child.sortieShotsRemaining = 1;
    child.actionTick = tick;
    parentIt->second.slotReadyTick[child.slot] =
        tick + std::max(0, parentIt->second.reloadRate);
    if (emit != nullptr) {
        emit({
            "CarrierChildDock",
            tick,
            child.parentObjectId,
            childObjectId,
            child.slot,
            {},
            "physical return completed; reload scheduled"
        });
    }
    return true;
}

bool RuntimeSpecialMechanismState::isCarrierChild(int objectId) const {
    return carrierChildren_.find(objectId) != carrierChildren_.end();
}

bool RuntimeSpecialMechanismState::isCarrierChildDocked(int objectId) const {
    const auto found = carrierChildren_.find(objectId);
    return found != carrierChildren_.end()
        && found->second.phase == CarrierChildPhase::Docked;
}

int RuntimeSpecialMechanismState::carrierParentForChild(int objectId) const {
    const auto found = carrierChildren_.find(objectId);
    return found == carrierChildren_.end() ? -1 : found->second.parentObjectId;
}

void RuntimeSpecialMechanismState::onObjectRemoved(
    std::vector<WorldObjectInstance>& objects,
    int objectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    onObjectRemoved(objects, objectId, tick, emit, reason, nullptr);
}

void RuntimeSpecialMechanismState::onObjectRemoved(
    std::vector<WorldObjectInstance>& objects,
    int objectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason,
    std::vector<SpecialMechanismAction>* actions
) {
    releaseMindControlByController(objects, objectId, tick, emit, reason);
    mindControlOverloadByController_.erase(objectId);
    releaseMindControlByTarget(objects, objectId, tick, emit, reason);
    clearTemporalTarget(objectId, tick, emit, reason);
    std::vector<int> affectedTemporalTargets;
    for (const auto& [targetId, temporal] : temporalByTarget_) {
        if (temporal.progressByAttacker.contains(objectId)) affectedTemporalTargets.push_back(targetId);
    }
    for (int targetId : affectedTemporalTargets) {
        clearTemporalAttacker(objectId, targetId, tick, emit, reason);
    }
    camouflageByObject_.erase(objectId);
    chronoByObject_.erase(objectId);
    electricDisableByObject_.erase(objectId);
    clearElectricChargeByObject(objectId, tick, emit, reason);

    timedBombs_.onObjectRemoved(timedBombObjectQuery(objects), objectId, tick, emit, reason, actions);

    for (auto pending = parasitePendingByProjectile_.begin();
         pending != parasitePendingByProjectile_.end();) {
        const ParasitePendingLaunchState state = pending->second;
        if (state.sourceObjectId != objectId
            && state.targetObjectId != objectId) {
            ++pending;
            continue;
        }
        const bool sourceRemoved = state.sourceObjectId == objectId;
        if (actions != nullptr) {
            SpecialMechanismAction action;
            action.kind = sourceRemoved
                ? SpecialMechanismActionKind::ParasiteKillParasite
                : SpecialMechanismActionKind::ParasiteReturnFromLimbo;
            action.tick = tick;
            action.projectileId = state.projectileId;
            action.objectId = state.sourceObjectId;
            action.targetObjectId = state.targetObjectId;
            action.owner = state.owner;
            action.paralyzes = state.paralyzes;
            action.cell = state.targetCell;
            action.suppressDeathWeapon = sourceRemoved;
            action.reason = sourceRemoved
                ? "pending parasite source removed"
                : "pending parasite target removed";
            actions->push_back(std::move(action));
        }
        parasiteResolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick
        };
        if (emit != nullptr) {
            emit({
                sourceRemoved ? "ParasiteKilled" : "ParasiteReturned",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                state.projectileId,
                state.targetCell,
                reason
            });
        }
        pending = parasitePendingByProjectile_.erase(pending);
    }

    for (auto parasite = parasiteByTarget_.begin();
         parasite != parasiteByTarget_.end();) {
        const ParasiteLinkState state = parasite->second;
        if (state.sourceObjectId != objectId
            && state.targetObjectId != objectId) {
            ++parasite;
            continue;
        }
        const bool sourceRemoved = state.sourceObjectId == objectId;
        if (actions != nullptr) {
            SpecialMechanismAction action;
            action.kind = sourceRemoved
                ? SpecialMechanismActionKind::ParasiteReleaseTarget
                : SpecialMechanismActionKind::ParasiteKillParasite;
            action.tick = tick;
            action.projectileId = state.projectileId;
            action.objectId = state.sourceObjectId;
            action.targetObjectId = state.targetObjectId;
            action.owner = state.owner;
            action.paralyzes = state.paralyzes;
            action.cell = objectCell(findObject(objects, state.targetObjectId));
            action.suppressDeathWeapon = !sourceRemoved;
            action.sourceWasAttachedToHost = !sourceRemoved;
            action.reason = sourceRemoved
                ? "parasite source removed"
                : "parasite host removed externally";
            actions->push_back(std::move(action));
        }
        if (emit != nullptr) {
            emit({
                sourceRemoved ? "ParasiteLinkCleared" : "ParasiteKilled",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                0,
                objectCell(findObject(objects, state.targetObjectId)),
                reason
            });
        }
        parasiteResolvedByProjectile_[state.projectileId] = {
            state.projectileId,
            state.sourceObjectId,
            state.targetObjectId,
            tick,
            !sourceRemoved
        };
        parasite = parasiteByTarget_.erase(parasite);
    }

    if (carrierParents_.find(objectId) != carrierParents_.end()) {
        CarrierParentState parent = carrierParents_.at(objectId);
        for (const auto& [slot, childId] : parent.childBySlot) {
            if (childId < 0) {
                continue;
            }
            if (WorldObjectInstance* child = findObject(objects, childId)) {
                child->health = 0;
                child->alive = false;
                child->mission = "CarrierLost";
            }
            carrierChildren_.erase(childId);
            spawnedMissiles_.erase(childId);
            if (emit != nullptr) {
                emit({
                    "CarrierParentRemoved",
                    tick,
                    objectId,
                    childId,
                    slot,
                    {},
                    reason
                });
            }
        }
        carrierParents_.erase(objectId);
    }
    const auto childIt = carrierChildren_.find(objectId);
    if (childIt != carrierChildren_.end()) {
        notifyCarrierChildDestroyed(objectId, tick, emit);
    }
    spawnedMissiles_.erase(objectId);
}

void RuntimeSpecialMechanismState::onOwnerChanged(
    std::vector<WorldObjectInstance>& objects,
    int objectId,
    const std::string& newOwner,
    int tick,
    const SpecialEventSink& emit
) {
    clearElectricChargeByObject(
        objectId,
        tick,
        emit,
        "owner changed"
    );
    const auto parentIt = carrierParents_.find(objectId);
    if (parentIt != carrierParents_.end()) {
        parentIt->second.owner = newOwner;
        for (const auto& [slot, childId] : parentIt->second.childBySlot) {
            (void)slot;
            if (WorldObjectInstance* child = findObject(objects, childId)) {
                child->owner = newOwner;
            }
        }
        if (emit != nullptr) {
            emit({
                "CarrierOwnerChanged",
                tick,
                objectId,
                -1,
                0,
                {},
                newOwner
            });
        }
    }
    const auto childIt = carrierChildren_.find(objectId);
    if (childIt != carrierChildren_.end()) {
        const auto parent = carrierParents_.find(childIt->second.parentObjectId);
        if (parent != carrierParents_.end()) {
            if (WorldObjectInstance* child = findObject(objects, objectId)) {
                child->owner = parent->second.owner;
            }
        }
    }
    for (auto& [targetId, link] : mindControlByTarget_) {
        if (link.controllerObjectId != objectId) {
            continue;
        }
        link.controllerOwner = newOwner;
        if (WorldObjectInstance* target = findObject(objects, targetId)) {
            target->hasBeenCaptured = target->hasBeenCaptured || target->owner != newOwner;
            target->owner = newOwner;
        }
    }
    timedBombs_.onOwnerChanged(objectId, newOwner);
}

void RuntimeSpecialMechanismState::reconcile(
    std::vector<WorldObjectInstance>& objects,
    int tick,
    const SpecialEventSink& emit
) {
    reconcile(objects, tick, emit, nullptr);
}

void RuntimeSpecialMechanismState::reconcile(
    std::vector<WorldObjectInstance>& objects,
    int tick,
    const SpecialEventSink& emit,
    std::vector<SpecialMechanismAction>* actions
) {
    std::vector<int> staleTargets;
    for (const auto& [targetId, link] : mindControlByTarget_) {
        const WorldObjectInstance* controller = findObject(
            objects,
            link.controllerObjectId
        );
        const WorldObjectInstance* target = findObject(objects, targetId);
        if (target == nullptr || !target->alive
            || ((!link.permanent)
                && !specialCombatSourceAvailable(objects, controller))) {
            staleTargets.push_back(targetId);
        }
    }
    for (int targetId : staleTargets) {
        releaseMindControlByTarget(
            objects,
            targetId,
            tick,
            emit,
            "relationship reconciled"
        );
    }
    for (const auto& [targetId, link] : mindControlByTarget_) {
        const WorldObjectInstance* controller = findObject(
            objects,
            link.controllerObjectId
        );
        WorldObjectInstance* target = findObject(objects, targetId);
        if (specialCombatSourceAvailable(objects, controller)
            && target != nullptr && target->alive
            && target->owner != controller->owner) {
            target->hasBeenCaptured = true;
            target->owner = controller->owner;
        }
    }
    std::erase_if(mindControlOverloadByController_, [&](const auto& entry) {
        const auto* controller = findObject(objects, entry.first);
        return !specialCombatSourceAvailable(objects, controller);
    });
    for (auto it = temporalByTarget_.begin(); it != temporalByTarget_.end();) {
        if (findObject(objects, it->first) == nullptr
            || !findObject(objects, it->first)->alive) {
            it = temporalByTarget_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = camouflageByObject_.begin(); it != camouflageByObject_.end();) {
        const WorldObjectInstance* object = findObject(objects, it->first);
        if (object == nullptr || !object->alive) {
            it = camouflageByObject_.erase(it);
        } else {
            ++it;
        }
    }
    timedBombs_.reconcile(timedBombObjectQuery(objects), tick, emit, actions);
    std::vector<int> assumedActiveProjectiles;
    assumedActiveProjectiles.reserve(parasitePendingByProjectile_.size());
    for (const auto& [projectileId, unusedPending]
         : parasitePendingByProjectile_) {
        (void)unusedPending;
        assumedActiveProjectiles.push_back(projectileId);
    }
    std::vector<SpecialMechanismAction> parasiteActions =
        reconcileParasites(objects, assumedActiveProjectiles, tick, emit);
    if (actions != nullptr) {
        actions->insert(
            actions->end(),
            std::make_move_iterator(parasiteActions.begin()),
            std::make_move_iterator(parasiteActions.end())
        );
    }
}

#include "simulation/abilities/spawning/RuntimeSpawnedMissileState.inl"

void RuntimeSpecialMechanismState::save(std::ostream& output) const {
    output << "[SpecialMechanisms]\n";
    output << "Version=" << kSpecialMechanismSaveVersion << "\n";
    output << "NextElectricArcId=" << nextElectricArcId_ << "\n";
    output << "NextRadiationAreaId=" << nextRadiationAreaId_ << "\n\n";

    output << "[SpecialMindControl]\n";
    output << "Count=" << mindControlByTarget_.size() << "\n";
    int index = 0;
    for (const auto& [targetId, link] : mindControlByTarget_) {
        output << index++ << "="
               << targetId << "|"
               << link.controllerObjectId << "|"
               << saveField(link.controllerOwner) << "|"
               << saveField(link.originalOwner) << "|"
               << boolText(link.permanent) << "|"
               << link.startedTick << "\n";
    }

    output << "\n[SpecialMindControlOverload]\nCount=" << mindControlOverloadByController_.size() << '\n';
    index = 0;
    for (const auto& [id, overload] : mindControlOverloadByController_)
        output << index++ << '=' << id << '|' << overload.nextCheckFrame << '|'
            << boolText(overload.soundPlayed) << '\n';

    output << "\n[SpecialCamouflage]\n";
    output << "Count=" << camouflageByObject_.size() << "\n";
    index = 0;
    for (const auto& [objectId, state] : camouflageByObject_) {
        output << index++ << "="
               << objectId << "|"
               << boolText(state.active) << "|"
               << saveField(state.disguiseType) << "|"
               << state.lastActivityTick << "|"
               << state.rearmUntilTick << "|"
               << state.fakeBlinkUntilTick << "\n";
    }

    output << "\n[SpecialChrono]\n";
    output << "Count=" << chronoByObject_.size() << "\n";
    index = 0;
    for (const auto& [objectId, state] : chronoByObject_) {
        output << index++ << "="
               << objectId << "|"
               << chronoPhaseName(state.phase) << "|"
               << state.source.x << "|" << state.source.y << "|"
               << state.destination.x << "|" << state.destination.y << "|"
               << state.sourceLevel << "|" << state.destinationLevel << "|"
               << state.startTick << "|" << state.completeTick << "|"
               << state.cooldownUntilTick << "\n";
    }

    output << "\n[SpecialTemporal]\n";
    output << "ProgressScale=10\n";
    output << "Count=" << temporalByTarget_.size() << "\n";
    index = 0;
    for (const auto& [targetId, state] : temporalByTarget_) {
        output << index++ << "="
               << targetId << "|" << state.requiredProgress << "|"
               << boolText(state.frozen) << "|";
        bool first = true;
        for (const auto& [attackerId, progress] : state.progressByAttacker) {
            if (!first) {
                output << "~";
            }
            first = false;
            output << attackerId << "," << progress;
        }
        output << "|" << state.lastAdvancedTick << "\n";
    }

    output << "\n[SpecialElectricDisable]\n";
    output << "Count=" << electricDisableByObject_.size() << "\n";
    index = 0;
    for (const auto& [objectId, state] : electricDisableByObject_) {
        output << index++ << "=" << objectId << "|" << state.untilTick << "\n";
    }

    output << "\n[SpecialElectricCharges]\n";
    output << "Count=" << electricChargeByTarget_.size() << "\n";
    index = 0;
    for (const auto& [targetId, charge] : electricChargeByTarget_) {
        output << index++ << "="
               << targetId << "|" << charge.sourceObjectId << "|"
               << charge.startTick << "|" << charge.expireTick << "|"
               << charge.level << "\n";
    }

    output << "\n[SpecialElectricArcs]\n";
    output << "Count=" << electricArcs_.size() << "\n";
    index = 0;
    for (const auto& [arcId, arc] : electricArcs_) {
        output << index++ << "="
               << arcId << "|" << arc.sourceObjectId << "|"
               << arc.targetObjectId << "|" << arc.source.x << "|"
               << arc.source.y << "|" << arc.source.z << "|"
               << arc.target.x << "|" << arc.target.y << "|"
               << arc.target.z << "|" << saveField(arc.color) << "|"
               << boolText(arc.alternateColor) << "|"
               << boolText(arc.assault) << "|" << arc.startTick << "|"
               << arc.expireTick << "|" << arc.sourceScreenOffsetX << "|"
               << arc.sourceScreenOffsetY << "|" << arc.sourceNativeFixedX << "|"
               << arc.sourceNativeFixedY << "\n";
    }

    output << "\n[SpecialRadiation]\n";
    output << "Count=" << radiationAreas_.size() << "\n";
    index = 0;
    for (const auto& [areaId, area] : radiationAreas_) {
        output << index++ << "="
               << areaId << "|" << area.sourceObjectId << "|"
               << saveField(area.owner) << "|" << area.center.x << "|"
               << area.center.y << "|" << area.radius << "|"
               << area.strength << "|" << area.maximumStrength << "|"
               << area.damageBase << "|" << area.nextApplicationTick << "|"
               << area.nextDecayTick << "|" << area.remainingTicks << "|"
               << area.damageInterval << "|" << area.decayAmount << "|"
               << saveField(area.warheadId) << "|"
               << boolText(area.persistsAfterSourceDeath) << "\n";
    }

    output << "\n[SpecialTimedBombs]\n";
    output << "Count=" << timedBombs_.records().size() << "\n";
    index = 0;
    for (const auto& [targetId, bomb] : timedBombs_.records()) {
        output << index++ << "="
               << targetId << "|" << bomb.sourceObjectId << "|"
               << saveField(bomb.owner) << "|"
               << timedBombKindName(bomb.kind) << "|"
               << bomb.attachedTick << "|" << bomb.detonateTick << "|"
               << bomb.damage << "|" << saveField(bomb.warheadId) << "|"
               << boolText(bomb.forceDestruction) << "\n";
    }

    output << "\n[SpecialParasitePending]\n";
    output << "Count=" << parasitePendingByProjectile_.size() << "\n";
    index = 0;
    for (const auto& [projectileId, pending]
         : parasitePendingByProjectile_) {
        output << index++ << "="
               << projectileId << "|" << pending.sourceObjectId << "|"
               << pending.targetObjectId << "|" << saveField(pending.owner)
               << "|" << saveField(pending.sourceTypeId) << "|"
               << saveField(pending.targetTypeId) << "|"
               << saveField(pending.weaponId) << "|"
               << saveField(pending.warheadId) << "|"
               << parasiteTargetKindName(pending.targetKind) << "|"
               << pending.targetCell.x << "|" << pending.targetCell.y << "|"
               << pending.launchedTick << "|" << pending.damage << "|"
               << pending.damageIntervalTicks << "|" << pending.paralyzes
               << "|" << pending.targetMaximumHealth << "|"
               << pending.cullingThresholdPerMyriad << "|"
               << boolText(pending.targetParasiteable) << "|"
               << boolText(pending.organic) << "|"
               << boolText(pending.sourceElite) << "|"
               << boolText(pending.culling) << "\n";
    }

    output << "\n[SpecialParasites]\n";
    output << "Count=" << parasiteByTarget_.size() << "\n";
    index = 0;
    for (const auto& [targetId, link] : parasiteByTarget_) {
        output << index++ << "="
               << targetId << "|" << link.sourceObjectId << "|"
               << saveField(link.owner) << "|"
               << saveField(link.weaponId) << "|"
               << saveField(link.warheadId) << "|"
               << link.damage << "|" << link.attachedTick << "|"
               << link.nextDamageTick << "|"
               << link.damageIntervalTicks << "|"
               << boolText(link.organic) << "|" << link.paralyzes << "|"
               << boolText(link.culling) << "|"
               << boolText(link.sourceExpectedInLimbo) << "|"
               << link.projectileId << "|" << saveField(link.sourceTypeId)
               << "|" << saveField(link.targetTypeId) << "|"
               << boolText(link.sourceElite) << "|"
               << link.cullingThresholdPerMyriad << "|"
               << link.targetMaximumHealth << "\n";
    }

    output << "\n[SpecialParasiteResolved]\n";
    output << "Count=" << parasiteResolvedByProjectile_.size() << "\n";
    index = 0;
    for (const auto& [projectileId, resolved]
         : parasiteResolvedByProjectile_) {
        output << index++ << "=" << projectileId << "|"
               << resolved.sourceObjectId << "|" << resolved.targetObjectId
               << "|" << resolved.resolvedTick << "|"
               << boolText(resolved.hostTerminated) << "\n";
    }

    output << "\n[SpecialCarrierParents]\n";
    output << "Count=" << carrierParents_.size() << "\n";
    index = 0;
    for (const auto& [parentId, parent] : carrierParents_) {
        output << index++ << "="
               << parentId << "|" << saveField(parent.childType) << "|"
               << saveField(parent.owner) << "|" << parent.capacity << "|"
               << parent.regenRate << "|" << parent.reloadRate << "|";
        bool first = true;
        for (const auto& [slot, childId] : parent.childBySlot) {
            if (!first) {
                output << "~";
            }
            first = false;
            output << slot << "," << childId << "," << parent.slotReadyTick.at(slot);
        }
        output << "\n";
    }

    output << "\n[SpecialCarrierChildren]\n";
    output << "Count=" << carrierChildren_.size() << "\n";
    index = 0;
    for (const auto& [childId, child] : carrierChildren_) {
        output << index++ << "="
               << childId << "|" << child.parentObjectId << "|"
               << child.slot << "|" << phaseName(child.phase) << "|"
               << child.targetObjectId << "|" << child.actionTick << "|"
               << child.ammoRemaining << "|" << child.sortieShotsRemaining << "\n";
    }
    output << "\n[SpecialSpawnedMissiles]\nVersion=4\nCount=" << spawnedMissiles_.size() << "\n";
    index = 0;
    for (const auto& [id, missile] : spawnedMissiles_)
        output << index++ << '=' << saveSpawnedMissileRow(missile) << '\n';
}

bool RuntimeSpecialMechanismState::load(
    const IniFile& ini,
    std::string& errorMessage
) {
    errorMessage.clear();
    struct LoadTransaction {
        RuntimeSpecialMechanismState* target = nullptr;
        RuntimeSpecialMechanismState backup;
        bool committed = false;

        ~LoadTransaction() {
            if (!committed && target != nullptr) {
                target->swap(backup);
            }
        }
    } transaction{this, *this, false};
    try {
    int saveVersion = 1;
    if (!parseIntStrict(
            ini.getValue("SpecialMechanisms", "Version", "1"),
            saveVersion
        ) || saveVersion < 1
        || saveVersion > kSpecialMechanismSaveVersion) {
        errorMessage = "unsupported SpecialMechanisms save version";
        return false;
    }
    mindControlByTarget_.clear();
    mindControlOverloadByController_.clear();
    camouflageByObject_.clear();
    chronoByObject_.clear();
    temporalByTarget_.clear();
    electricDisableByObject_.clear();
    electricChargeByTarget_.clear();
    electricArcs_.clear();
    radiationAreas_.clear();
    timedBombs_.reset();
    parasitePendingByProjectile_.clear();
    parasiteByTarget_.clear();
    parasiteResolvedByProjectile_.clear();
    carrierParents_.clear();
    carrierChildren_.clear();
    spawnedMissiles_.clear();
    nextElectricArcId_ = std::max(
        1,
        parseInt(ini.getValue("SpecialMechanisms", "NextElectricArcId", "1"), 1)
    );
    nextRadiationAreaId_ = std::max(
        1,
        parseInt(ini.getValue("SpecialMechanisms", "NextRadiationAreaId", "1"), 1)
    );

    const auto countFor = [&ini](const std::string& section) {
        return std::max(0, parseInt(ini.getValue(section, "Count", "0"), 0));
    };
    for (int index = 0; index < countFor("SpecialMindControl"); ++index) {
        const std::vector<std::string> values = split(
            ini.getValue("SpecialMindControl", std::to_string(index), ""),
            '|'
        );
        if (values.size() < 5) {
            errorMessage = "invalid SpecialMindControl entry";
            return false;
        }
        MindControlLinkState link;
        link.targetObjectId = parseInt(values[0], -1);
        link.controllerObjectId = parseInt(values[1], -1);
        if (values.size() >= 6) {
            link.controllerOwner = loadField(values[2]);
            link.originalOwner = loadField(values[3]);
            link.permanent = parseBool(values[4]);
            link.startedTick = parseInt(values[5], 0);
        } else {
            link.originalOwner = loadField(values[2]);
            link.permanent = parseBool(values[3]);
            link.startedTick = parseInt(values[4], 0);
        }
        mindControlByTarget_[link.targetObjectId] = std::move(link);
    }
    int overloadCount = 0;
    if (!parseIntStrict(ini.getValue("SpecialMindControlOverload", "Count", "0"), overloadCount)
        || overloadCount < 0 || overloadCount > 4096) {
        errorMessage = "invalid mind-control overload count";
        return false;
    }
    for (int index = 0; index < overloadCount; ++index) {
        const auto values = split(ini.getValue("SpecialMindControlOverload", std::to_string(index), ""), '|');
        int id = -1;
        MindControlOverloadState overload;
        if (values.size() != 3 || !parseIntStrict(values[0], id) || id < 0
            || !parseIntStrict(values[1], overload.nextCheckFrame) || overload.nextCheckFrame < 0
            || !parseBoolStrict(values[2], overload.soundPlayed)
            || !mindControlOverloadByController_.emplace(id, overload).second) {
            errorMessage = "invalid mind-control overload entry";
            return false;
        }
    }
    for (int index = 0; index < countFor("SpecialCamouflage"); ++index) {
        const auto values = split(
            ini.getValue("SpecialCamouflage", std::to_string(index), ""),
            '|'
        );
        if (values.size() < 5) {
            errorMessage = "invalid SpecialCamouflage entry";
            return false;
        }
        CamouflageState state;
        state.objectId = parseInt(values[0], -1);
        state.active = parseBool(values[1]);
        if (values.size() >= 6) {
            state.disguiseType = loadField(values[2]);
            state.lastActivityTick = parseInt(values[3], 0);
            state.rearmUntilTick = parseInt(values[4], 0);
            state.fakeBlinkUntilTick = parseInt(values[5], 0);
        } else {
            state.lastActivityTick = parseInt(values[2], 0);
            state.rearmUntilTick = parseInt(values[3], 0);
            state.fakeBlinkUntilTick = parseInt(values[4], 0);
        }
        camouflageByObject_[state.objectId] = std::move(state);
    }
    for (int index = 0; index < countFor("SpecialChrono"); ++index) {
        const auto values = split(
            ini.getValue("SpecialChrono", std::to_string(index), ""),
            '|'
        );
        if (values.size() < 11) {
            errorMessage = "invalid SpecialChrono entry";
            return false;
        }
        ChronoMoveState state;
        state.objectId = parseInt(values[0], -1);
        const std::string phase = values[1];
        state.phase = phase == "WarpOut"
            ? ChronoPhase::WarpOut
            : (phase == "InTransit"
                ? ChronoPhase::InTransit
                : (phase == "WarpIn"
                    ? ChronoPhase::WarpIn
                    : (phase == "Cooldown"
                        ? ChronoPhase::Cooldown
                        : ChronoPhase::None)));
        state.source = {parseInt(values[2], -1), parseInt(values[3], -1)};
        state.destination = {parseInt(values[4], -1), parseInt(values[5], -1)};
        state.sourceLevel = parseInt(values[6]);
        state.destinationLevel = parseInt(values[7]);
        state.startTick = parseInt(values[8]);
        state.completeTick = parseInt(values[9]);
        state.cooldownUntilTick = parseInt(values[10]);
        chronoByObject_[state.objectId] = state;
    }
    for (int index = 0; index < countFor("SpecialTemporal"); ++index) {
        const auto values = split(
            ini.getValue("SpecialTemporal", std::to_string(index), ""),
            '|'
        );
        if (values.size() < 4) {
            errorMessage = "invalid SpecialTemporal entry";
            return false;
        }
        TemporalTargetState state;
        state.targetObjectId = parseInt(values[0], -1);
        state.requiredProgress = parseInt(values[1]);
        state.frozen = parseBool(values[2]);
        state.lastAdvancedTick = values.size() >= 5 ? parseInt(values[4], -1) : -1;
        const char temporalDelimiter = values[3].find('~')
            == std::string::npos ? ';' : '~';
        for (const std::string& token : split(values[3], temporalDelimiter)) {
            const auto pair = split(token, ',');
            if (pair.size() >= 2) {
                state.progressByAttacker[parseInt(pair[0], -1)] = parseInt(pair[1]);
            }
        }
        // Saves from the shot-based implementation used one tenth the
        // progress units. Preserve their completion percentage on migration.
        if (parseInt(ini.getValue("SpecialTemporal", "ProgressScale", "1")) == 1) {
            const auto rescale = [](int value) {
                return static_cast<int>(std::clamp<std::int64_t>(
                    static_cast<std::int64_t>(value) * 10, 0, std::numeric_limits<int>::max()));
            };
            state.requiredProgress = rescale(state.requiredProgress);
            for (auto& [id, progress] : state.progressByAttacker) progress = rescale(progress);
        }
        temporalByTarget_[state.targetObjectId] = state;
    }
    for (int index = 0; index < countFor("SpecialElectricDisable"); ++index) {
        const auto values = split(
            ini.getValue("SpecialElectricDisable", std::to_string(index), ""),
            '|'
        );
        if (values.size() < 2) {
            errorMessage = "invalid SpecialElectricDisable entry";
            return false;
        }
        electricDisableByObject_[parseInt(values[0], -1)] = {
            parseInt(values[0], -1),
            parseInt(values[1])
        };
    }
    for (int index = 0; index < countFor("SpecialElectricCharges"); ++index) {
        const auto values = split(
            ini.getValue("SpecialElectricCharges", std::to_string(index), ""),
            '|'
        );
        if (values.size() < 5) {
            errorMessage = "invalid SpecialElectricCharges entry";
            return false;
        }
        ElectricChargeState charge;
        charge.targetObjectId = parseInt(values[0], -1);
        charge.sourceObjectId = parseInt(values[1], -1);
        charge.startTick = parseInt(values[2]);
        charge.expireTick = parseInt(values[3]);
        charge.level = parseInt(values[4], 1);
        electricChargeByTarget_[charge.targetObjectId] = charge;
    }
    for (int index = 0; index < countFor("SpecialElectricArcs"); ++index) {
        const auto values = split(
            ini.getValue("SpecialElectricArcs", std::to_string(index), ""),
            '|'
        );
        if (values.size() != 14U && values.size() != 16U && values.size() != 18U) {
            errorMessage = "invalid SpecialElectricArcs entry";
            return false;
        }
        ElectricArcState arc;
        arc.arcId = parseInt(values[0], -1);
        arc.sourceObjectId = parseInt(values[1], -1);
        arc.targetObjectId = parseInt(values[2], -1);
        arc.source = {
            std::stod(values[3]),
            std::stod(values[4]),
            std::stod(values[5])
        };
        arc.target = {
            std::stod(values[6]),
            std::stod(values[7]),
            std::stod(values[8])
        };
        arc.color = loadField(values[9]);
        arc.alternateColor = parseBool(values[10]);
        arc.assault = parseBool(values[11]);
        arc.startTick = parseInt(values[12]);
        arc.expireTick = parseInt(values[13]);
        if (values.size() >= 16U
            && (!parseIntStrict(values[14], arc.sourceScreenOffsetX)
                || !parseIntStrict(values[15], arc.sourceScreenOffsetY)
                || arc.sourceScreenOffsetX < -65536 || arc.sourceScreenOffsetX > 65536
                || arc.sourceScreenOffsetY < -65536 || arc.sourceScreenOffsetY > 65536)) {
            errorMessage = "invalid SpecialElectricArcs presentation offset";
            return false;
        }
        if (values.size() == 18U
            && (!parseIntStrict(values[16], arc.sourceNativeFixedX)
                || !parseIntStrict(values[17], arc.sourceNativeFixedY)
                || arc.sourceNativeFixedX < -65536 || arc.sourceNativeFixedX > 65536
                || arc.sourceNativeFixedY < -65536 || arc.sourceNativeFixedY > 65536)) {
            errorMessage = "invalid SpecialElectricArcs native presentation offset";
            return false;
        }
        electricArcs_[arc.arcId] = arc;
        nextElectricArcId_ = std::max(nextElectricArcId_, arc.arcId + 1);
    }
    for (int index = 0; index < countFor("SpecialRadiation"); ++index) {
        const auto values = split(
            ini.getValue("SpecialRadiation", std::to_string(index), ""),
            '|'
        );
        if (values.size() < 16) {
            errorMessage = "invalid SpecialRadiation entry";
            return false;
        }
        RadiationAreaState area;
        area.areaId = parseInt(values[0], -1);
        area.sourceObjectId = parseInt(values[1], -1);
        area.owner = loadField(values[2]);
        area.center = {parseInt(values[3], -1), parseInt(values[4], -1)};
        area.radius = parseInt(values[5]);
        area.strength = parseInt(values[6]);
        area.maximumStrength = parseInt(values[7], 1);
        area.damageBase = parseInt(values[8], 1);
        area.nextApplicationTick = parseInt(values[9]);
        area.nextDecayTick = parseInt(values[10]);
        area.remainingTicks = parseInt(values[11]);
        area.damageInterval = parseInt(values[12], 1);
        area.decayAmount = parseInt(values[13], 1);
        area.warheadId = loadField(values[14]);
        area.persistsAfterSourceDeath = parseBool(values[15]);
        radiationAreas_[area.areaId] = area;
        nextRadiationAreaId_ = std::max(nextRadiationAreaId_, area.areaId + 1);
    }
    int timedBombCount = 0;
    if (ini.getSection("SpecialTimedBombs") != nullptr
        && (!parseIntStrict(
                ini.getValue("SpecialTimedBombs", "Count", ""),
                timedBombCount
            )
            || timedBombCount < 0
            || timedBombCount
                > static_cast<int>(kMaximumTimedBombRecords))) {
        errorMessage = "invalid SpecialTimedBombs count";
        return false;
    }
    for (int index = 0; index < timedBombCount; ++index) {
        const auto values = split(
            ini.getValue("SpecialTimedBombs", std::to_string(index), ""),
            '|'
        );
        TimedBombState bomb;
        bool forceDestruction = false;
        if (values.size() != 9
            || !parseIntStrict(values[0], bomb.targetObjectId)
            || !parseIntStrict(values[1], bomb.sourceObjectId)
            || !timedBombKindFromName(values[3], bomb.kind)
            || !parseIntStrict(values[4], bomb.attachedTick)
            || !parseIntStrict(values[5], bomb.detonateTick)
            || !parseIntStrict(values[6], bomb.damage)
            || !parseBoolStrict(values[8], forceDestruction)) {
            errorMessage = "invalid SpecialTimedBombs entry";
            return false;
        }
        bomb.owner = loadField(values[2]);
        bomb.warheadId = loadField(values[7]);
        bomb.forceDestruction = forceDestruction;
        const bool kindFieldsValid = bomb.kind == TimedBombKind::C4
            ? bomb.forceDestruction
            : !bomb.forceDestruction;
        if (bomb.targetObjectId < 0 || bomb.sourceObjectId < 0
            || bomb.targetObjectId == bomb.sourceObjectId
            || bomb.owner.empty() || bomb.warheadId.empty()
            || bomb.attachedTick < 0
            || bomb.detonateTick < bomb.attachedTick
            || bomb.damage < 0 || !kindFieldsValid
            || timedBombs_.byTarget_.find(bomb.targetObjectId)
                != timedBombs_.byTarget_.end()) {
            errorMessage = "invalid or conflicting timed-bomb identity";
            return false;
        }
        timedBombs_.byTarget_[bomb.targetObjectId] = std::move(bomb);
    }
    const auto readParasiteCount = [
        &ini,
        saveVersion
    ](const std::string& section, int& count) {
        if (saveVersion == 1) {
            count = std::min(
                std::max(
                    0,
                    parseInt(ini.getValue(section, "Count", "0"), 0)
                ),
                static_cast<int>(kMaximumParasiteRecords)
            );
            return true;
        }
        if (!parseIntStrict(ini.getValue(section, "Count", ""), count)
            || count < 0
            || count > static_cast<int>(kMaximumParasiteRecords)) {
            return false;
        }
        return true;
    };
    const auto sourceOrTargetReserved = [this](int sourceId, int targetId) {
        for (const auto& [unusedProjectileId, pending]
             : parasitePendingByProjectile_) {
            (void)unusedProjectileId;
            if (pending.sourceObjectId == sourceId
                || pending.targetObjectId == targetId) {
                return true;
            }
        }
        for (const auto& [existingTargetId, link] : parasiteByTarget_) {
            if (link.sourceObjectId == sourceId
                || existingTargetId == targetId) {
                return true;
            }
        }
        return false;
    };

    int pendingParasiteCount = 0;
    if (saveVersion >= 2
        && !readParasiteCount(
            "SpecialParasitePending",
            pendingParasiteCount
        )) {
        errorMessage = "invalid SpecialParasitePending count";
        return false;
    }
    for (int index = 0; index < pendingParasiteCount; ++index) {
        const auto values = split(
            ini.getValue(
                "SpecialParasitePending",
                std::to_string(index),
                ""
            ),
            '|'
        );
        ParasitePendingLaunchState pending;
        bool targetParasiteable = false;
        bool organic = false;
        bool sourceElite = false;
        bool culling = false;
        if (values.size() != 21
            || !parseIntStrict(values[0], pending.projectileId)
            || !parseIntStrict(values[1], pending.sourceObjectId)
            || !parseIntStrict(values[2], pending.targetObjectId)
            || !parasiteTargetKindFromName(values[8], pending.targetKind)
            || !parseIntStrict(values[9], pending.targetCell.x)
            || !parseIntStrict(values[10], pending.targetCell.y)
            || !parseIntStrict(values[11], pending.launchedTick)
            || !parseIntStrict(values[12], pending.damage)
            || !parseIntStrict(values[13], pending.damageIntervalTicks)
            || !parseIntStrict(values[14], pending.paralyzes)
            || !parseIntStrict(values[15], pending.targetMaximumHealth)
            || !parseIntStrict(
                values[16],
                pending.cullingThresholdPerMyriad
            )
            || !parseBoolStrict(values[17], targetParasiteable)
            || !parseBoolStrict(values[18], organic)
            || !parseBoolStrict(values[19], sourceElite)
            || !parseBoolStrict(values[20], culling)) {
            errorMessage = "invalid SpecialParasitePending entry";
            return false;
        }
        pending.owner = loadField(values[3]);
        pending.sourceTypeId = loadField(values[4]);
        pending.targetTypeId = loadField(values[5]);
        pending.weaponId = loadField(values[6]);
        pending.warheadId = loadField(values[7]);
        pending.targetParasiteable = targetParasiteable;
        pending.organic = organic;
        pending.sourceElite = sourceElite;
        pending.culling = culling;
        if (pending.projectileId == 0 || pending.sourceObjectId < 0
            || pending.targetObjectId < 0
            || pending.sourceObjectId == pending.targetObjectId
            || pending.launchedTick < 0 || pending.damage < 0
            || pending.damageIntervalTicks <= 0 || pending.paralyzes < 0
            || pending.targetMaximumHealth <= 0
            || !validParasiteRatio(pending.cullingThresholdPerMyriad)
            || !validParasiteText(pending.owner)
            || !validParasiteText(pending.sourceTypeId)
            || !validParasiteText(pending.targetTypeId)
            || !validParasiteText(pending.weaponId)
            || !validParasiteText(pending.warheadId)
            || (pending.targetKind == ParasiteTargetKind::Vehicle
                && !pending.targetParasiteable)
            || parasitePendingByProjectile_.find(pending.projectileId)
                != parasitePendingByProjectile_.end()
            || sourceOrTargetReserved(
                pending.sourceObjectId,
                pending.targetObjectId
            )) {
            errorMessage = "invalid or conflicting pending parasite state";
            return false;
        }
        parasitePendingByProjectile_[pending.projectileId] =
            std::move(pending);
    }

    int parasiteCount = 0;
    if (!readParasiteCount("SpecialParasites", parasiteCount)) {
        errorMessage = "invalid SpecialParasites count";
        return false;
    }
    for (int index = 0; index < parasiteCount; ++index) {
        const auto values = split(
            ini.getValue("SpecialParasites", std::to_string(index), ""),
            '|'
        );
        if ((saveVersion == 1 && values.size() < 12)
            || (saveVersion >= 2 && values.size() != 19)) {
            if (saveVersion >= 2) {
                errorMessage = "invalid SpecialParasites entry width";
                return false;
            }
            continue;
        }
        ParasiteLinkState link;
        bool organic = false;
        bool culling = false;
        bool sourceExpectedInLimbo = true;
        if (!parseIntStrict(values[0], link.targetObjectId)
            || !parseIntStrict(values[1], link.sourceObjectId)
            || !parseIntStrict(values[5], link.damage)
            || !parseIntStrict(values[6], link.attachedTick)
            || !parseIntStrict(values[7], link.nextDamageTick)
            || !parseIntStrict(values[8], link.damageIntervalTicks)
            || !parseBoolStrict(values[9], organic)
            || !parseIntStrict(values[10], link.paralyzes)
            || !parseBoolStrict(values[11], culling)
            || (values.size() >= 13
                && !parseBoolStrict(values[12], sourceExpectedInLimbo))) {
            if (saveVersion >= 2) {
                errorMessage = "invalid SpecialParasites fields";
                return false;
            }
            continue;
        }
        link.owner = loadField(values[2]);
        link.weaponId = loadField(values[3]);
        link.warheadId = loadField(values[4]);
        link.organic = organic;
        link.culling = culling;
        link.sourceExpectedInLimbo = sourceExpectedInLimbo;
        if (saveVersion >= 2) {
            bool sourceElite = false;
            if (!parseIntStrict(values[13], link.projectileId)
                || !parseBoolStrict(values[16], sourceElite)
                || !parseIntStrict(
                    values[17],
                    link.cullingThresholdPerMyriad
                )
                || !parseIntStrict(values[18], link.targetMaximumHealth)) {
                errorMessage = "invalid expanded SpecialParasites fields";
                return false;
            }
            link.sourceTypeId = loadField(values[14]);
            link.targetTypeId = loadField(values[15]);
            link.sourceElite = sourceElite;
        } else {
            link.projectileId = -link.sourceObjectId - 1;
            link.cullingThresholdPerMyriad = 0;
            link.targetMaximumHealth = 1;
        }
        if (link.targetObjectId < 0 || link.sourceObjectId < 0
            || link.targetObjectId == link.sourceObjectId
            || link.owner.empty() || link.weaponId.empty()
            || link.warheadId.empty() || link.damage < 0
            || link.attachedTick < 0
            || link.nextDamageTick < link.attachedTick
            || link.damageIntervalTicks <= 0 || link.paralyzes < 0
            || (saveVersion >= 2
                && (link.projectileId == 0
                    || !validParasiteText(link.sourceTypeId)
                    || !validParasiteText(link.targetTypeId)
                    || !validParasiteRatio(
                        link.cullingThresholdPerMyriad
                    )
                    || link.targetMaximumHealth <= 0))
            || sourceOrTargetReserved(
                link.sourceObjectId,
                link.targetObjectId
            )
            || parasiteByTarget_.find(link.targetObjectId)
                != parasiteByTarget_.end()) {
            if (saveVersion >= 2) {
                errorMessage = "invalid or conflicting parasite link";
                return false;
            }
            continue;
        }
        parasiteByTarget_[link.targetObjectId] = std::move(link);
    }

    int resolvedParasiteCount = 0;
    if (saveVersion >= 2
        && !readParasiteCount(
            "SpecialParasiteResolved",
            resolvedParasiteCount
        )) {
        errorMessage = "invalid SpecialParasiteResolved count";
        return false;
    }
    if (parasitePendingByProjectile_.size()
            + static_cast<std::size_t>(resolvedParasiteCount)
        > kMaximumParasiteRecords) {
        errorMessage = "combined parasite launch/resolution cap exceeded";
        return false;
    }
    for (int index = 0; index < resolvedParasiteCount; ++index) {
        const auto values = split(
            ini.getValue(
                "SpecialParasiteResolved",
                std::to_string(index),
                ""
            ),
            '|'
        );
        ParasiteResolvedLaunchState resolved;
        bool hostTerminated = false;
        if (values.size() != 5
            || !parseIntStrict(values[0], resolved.projectileId)
            || !parseIntStrict(values[1], resolved.sourceObjectId)
            || !parseIntStrict(values[2], resolved.targetObjectId)
            || !parseIntStrict(values[3], resolved.resolvedTick)
            || !parseBoolStrict(values[4], hostTerminated)
            || resolved.projectileId == 0 || resolved.sourceObjectId < 0
            || resolved.targetObjectId < 0
            || resolved.sourceObjectId == resolved.targetObjectId
            || resolved.resolvedTick < 0
            || parasitePendingByProjectile_.find(resolved.projectileId)
                != parasitePendingByProjectile_.end()
            || parasiteResolvedByProjectile_.find(resolved.projectileId)
                != parasiteResolvedByProjectile_.end()) {
            errorMessage = "invalid SpecialParasiteResolved entry";
            return false;
        }
        resolved.hostTerminated = hostTerminated;
        const auto link = parasiteByTarget_.find(resolved.targetObjectId);
        if (link != parasiteByTarget_.end()
            && (link->second.projectileId != resolved.projectileId
                || link->second.sourceObjectId != resolved.sourceObjectId)) {
            errorMessage = "resolved parasite identity conflicts with link";
            return false;
        }
        parasiteResolvedByProjectile_[resolved.projectileId] = resolved;
    }
    const auto readCarrierCount = [
        &ini
    ](const std::string& sectionName, int& count) {
        const IniFile::Section* section = ini.getSection(sectionName);
        if (section == nullptr) {
            count = 0;
            return true;
        }
        if (!parseIntStrict(ini.getValue(sectionName, "Count", ""), count)
            || count < 0
            || count > static_cast<int>(kMaximumCarrierRecords)) {
            return false;
        }
        int countFields = 0;
        std::set<int> entryIndexes;
        for (const auto& [key, unusedValue] : *section) {
            (void)unusedValue;
            if (equalsAsciiIgnoreCase(key, "Count")) {
                ++countFields;
                continue;
            }
            int entryIndex = -1;
            if (!parseIntStrict(key, entryIndex)
                || entryIndex < 0 || entryIndex >= count
                || key != std::to_string(entryIndex)
                || !entryIndexes.insert(entryIndex).second) {
                return false;
            }
        }
        return countFields == 1
            && entryIndexes.size() == static_cast<std::size_t>(count);
    };

    int carrierParentCount = 0;
    int carrierChildCount = 0;
    if (!readCarrierCount("SpecialCarrierParents", carrierParentCount)) {
        errorMessage = "invalid SpecialCarrierParents count";
        return false;
    }
    if (!readCarrierCount("SpecialCarrierChildren", carrierChildCount)) {
        errorMessage = "invalid SpecialCarrierChildren count";
        return false;
    }

    std::set<int> referencedCarrierChildren;
    std::size_t totalCarrierSlots = 0;
    for (int index = 0; index < carrierParentCount; ++index) {
        const auto values = split(
            ini.getValue("SpecialCarrierParents", std::to_string(index), ""),
            '|'
        );
        CarrierParentState parent;
        if (values.size() != 7
            || !parseIntStrict(values[0], parent.parentObjectId)
            || !parseIntStrict(values[3], parent.capacity)
            || !parseIntStrict(values[4], parent.regenRate)
            || !parseIntStrict(values[5], parent.reloadRate)) {
            errorMessage = "invalid SpecialCarrierParents entry";
            return false;
        }
        parent.childType = loadField(values[1]);
        parent.owner = loadField(values[2]);
        if (parent.parentObjectId <= 0 || parent.childType.empty()
            || parent.owner.empty() || parent.capacity <= 0
            || parent.capacity
                > static_cast<int>(kMaximumCarrierRecords)
            || parent.regenRate < 0 || parent.reloadRate < 0
            || carrierParents_.find(parent.parentObjectId)
                != carrierParents_.end()) {
            errorMessage = "invalid or duplicate carrier parent";
            return false;
        }
        totalCarrierSlots += static_cast<std::size_t>(parent.capacity);
        if (totalCarrierSlots > kMaximumCarrierRecords) {
            errorMessage = "carrier slot cap exceeded";
            return false;
        }
        const char carrierDelimiter = values[6].find('~')
            == std::string::npos ? ';' : '~';
        const std::vector<std::string> slotTokens = split(
            values[6],
            carrierDelimiter
        );
        if (slotTokens.size() != static_cast<std::size_t>(parent.capacity)) {
            errorMessage = "carrier parent slots do not match capacity";
            return false;
        }
        for (const std::string& token : slotTokens) {
            const auto slotValues = split(token, ',');
            int slot = -1;
            int childId = -1;
            int readyTick = -1;
            if (slotValues.size() != 3
                || !parseIntStrict(slotValues[0], slot)
                || !parseIntStrict(slotValues[1], childId)
                || !parseIntStrict(slotValues[2], readyTick)
                || slot < 0 || slot >= parent.capacity
                || (childId != -1 && childId <= 0) || readyTick < 0
                || parent.childBySlot.find(slot)
                    != parent.childBySlot.end()) {
                errorMessage = "invalid carrier parent slot";
                return false;
            }
            parent.childBySlot[slot] = childId;
            parent.slotReadyTick[slot] = readyTick;
            if (childId > 0
                && !referencedCarrierChildren.insert(childId).second) {
                errorMessage = "carrier child is referenced more than once";
                return false;
            }
        }
        for (int slot = 0; slot < parent.capacity; ++slot) {
            if (parent.childBySlot.find(slot) == parent.childBySlot.end()
                || parent.slotReadyTick.find(slot)
                    == parent.slotReadyTick.end()) {
                errorMessage = "carrier parent slots are not contiguous";
                return false;
            }
        }
        carrierParents_[parent.parentObjectId] = std::move(parent);
    }
    for (int index = 0; index < carrierChildCount; ++index) {
        const auto values = split(
            ini.getValue("SpecialCarrierChildren", std::to_string(index), ""),
            '|'
        );
        CarrierChildState child;
        if ((values.size() != 7 && values.size() != 8)
            || !parseIntStrict(values[0], child.childObjectId)
            || !parseIntStrict(values[1], child.parentObjectId)
            || !parseIntStrict(values[2], child.slot)
            || !carrierPhaseFromName(values[3], child.phase)
            || !parseIntStrict(values[4], child.targetObjectId)
            || !parseIntStrict(values[5], child.actionTick)
            || !parseIntStrict(values[6], child.ammoRemaining)
            || (values.size() == 8 && !parseIntStrict(values[7], child.sortieShotsRemaining))) {
            errorMessage = "invalid SpecialCarrierChildren entry";
            return false;
        }
        // Legacy already-fired records retain zero; armed records retain a
        // one-shot budget. Reading a save never creates two extra rounds.
        if (values.size() == 7) child.sortieShotsRemaining = child.ammoRemaining;
        const auto parent = carrierParents_.find(child.parentObjectId);
        // Parent records contain the child type, but not the parent's world
        // type. The world validator checks the exact CARRIER/HORNET or
        // DEST/ASW pair before applying these records to loaded objects.
        const bool threeRoundChildRecord = parent != carrierParents_.end()
            && (equalsAsciiIgnoreCase(parent->second.childType, "HORNET")
                || equalsAsciiIgnoreCase(parent->second.childType, "ASW"));
        SpecialCell targetCell;
        bool bridgeTarget = false;
        const bool hasTarget = child.targetObjectId > 0
            || decodeRuntimeCarrierCellTarget(child.targetObjectId, targetCell, bridgeTarget);
        const bool phaseFieldsValid = [&child, hasTarget]() {
            switch (child.phase) {
            case CarrierChildPhase::Docked:
                return child.targetObjectId == -1 && child.ammoRemaining == 1
                    && (child.sortieShotsRemaining == 1 || child.sortieShotsRemaining == 3);
            case CarrierChildPhase::Launching:
            case CarrierChildPhase::Attacking:
                return hasTarget && child.ammoRemaining == 1
                    && child.sortieShotsRemaining >= 1;
            case CarrierChildPhase::Returning:
                return (child.targetObjectId == -1 || hasTarget)
                    && ((child.ammoRemaining == 0 && child.sortieShotsRemaining == 0)
                        || (child.ammoRemaining == 1 && child.sortieShotsRemaining >= 1));
            case CarrierChildPhase::Destroyed:
                return false;
            }
            return false;
        }();
        if (child.childObjectId <= 0 || child.parentObjectId <= 0
            || child.childObjectId == child.parentObjectId
            || (child.targetObjectId != -1 && !hasTarget)
            || child.actionTick < 0
            || child.ammoRemaining < 0 || child.ammoRemaining > 1
            || child.sortieShotsRemaining < 0 || child.sortieShotsRemaining > (threeRoundChildRecord ? 3 : 1)
            || !phaseFieldsValid
            || carrierChildren_.find(child.childObjectId)
                != carrierChildren_.end()
            || parent == carrierParents_.end()
            || child.slot < 0 || child.slot >= parent->second.capacity
            || parent->second.childBySlot.at(child.slot)
                != child.childObjectId) {
            errorMessage = "invalid or conflicting carrier child";
            return false;
        }
        carrierChildren_[child.childObjectId] = child;
    }
    if (carrierChildren_.size() != referencedCarrierChildren.size()) {
        errorMessage = "carrier parent/child links are not bidirectional";
        return false;
    }
    for (const int childId : referencedCarrierChildren) {
        if (carrierChildren_.find(childId) == carrierChildren_.end()) {
            errorMessage = "carrier parent references a missing child";
            return false;
        }
    }
    for (const auto& [parentId, unusedParent] : carrierParents_) {
        (void)unusedParent;
        std::set<int> visited;
        int objectId = parentId;
        while (true) {
            if (!visited.insert(objectId).second) {
                errorMessage = "carrier parent/child links contain a cycle";
                return false;
            }
            const auto child = carrierChildren_.find(objectId);
            if (child == carrierChildren_.end()) {
                break;
            }
            objectId = child->second.parentObjectId;
        }
    }
    if (!loadSpawnedMissiles(ini, spawnedMissiles_, errorMessage)) return false;
    for (const auto& [id, missile] : spawnedMissiles_) {
        const auto child = carrierChildren_.find(id);
        if ((missile.released && child != carrierChildren_.end())
            || (!missile.released && (child == carrierChildren_.end()
                || child->second.parentObjectId != missile.parentObjectId
                || child->second.slot != missile.slot
                || child->second.phase != CarrierChildPhase::Docked))) {
            errorMessage = "spawned missile disagrees with carrier slot";
            return false;
        }
    }
    transaction.committed = true;
    return true;
    } catch (const std::exception& exception) {
        errorMessage = std::string("invalid SpecialMechanisms save: ")
            + exception.what();
        return false;
    } catch (...) {
        errorMessage = "invalid SpecialMechanisms save";
        return false;
    }
}

std::uint64_t RuntimeSpecialMechanismState::stateHash() const {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto& [id, overload] : mindControlOverloadByController_) {
        mixValue(hash, id);
        mixValue(hash, overload.nextCheckFrame);
        mixValue(hash, overload.soundPlayed);
    }
    for (const auto& [targetId, link] : mindControlByTarget_) {
        mixValue(hash, targetId);
        mixValue(hash, link.controllerObjectId);
        mixString(hash, link.controllerOwner);
        mixString(hash, link.originalOwner);
        mixValue(hash, link.permanent);
        mixValue(hash, link.startedTick);
    }
    for (const auto& [objectId, state] : camouflageByObject_) {
        mixValue(hash, objectId);
        mixValue(hash, state.active);
        mixString(hash, state.disguiseType);
        mixValue(hash, state.lastActivityTick);
        mixValue(hash, state.rearmUntilTick);
        mixValue(hash, state.fakeBlinkUntilTick);
    }
    for (const auto& [objectId, state] : chronoByObject_) {
        mixValue(hash, objectId);
        mixString(hash, chronoPhaseName(state.phase));
        mixValue(hash, state.source.x);
        mixValue(hash, state.source.y);
        mixValue(hash, state.destination.x);
        mixValue(hash, state.destination.y);
        mixValue(hash, state.sourceLevel);
        mixValue(hash, state.destinationLevel);
        mixValue(hash, state.startTick);
        mixValue(hash, state.completeTick);
        mixValue(hash, state.cooldownUntilTick);
    }
    for (const auto& [targetId, state] : temporalByTarget_) {
        mixValue(hash, targetId);
        mixValue(hash, state.requiredProgress);
        mixValue(hash, state.frozen);
        mixValue(hash, state.lastAdvancedTick);
        for (const auto& [attackerId, progress] : state.progressByAttacker) {
            mixValue(hash, attackerId);
            mixValue(hash, progress);
        }
    }
    for (const auto& [objectId, state] : electricDisableByObject_) {
        mixValue(hash, objectId);
        mixValue(hash, state.untilTick);
    }
    for (const auto& [targetId, charge] : electricChargeByTarget_) {
        mixValue(hash, targetId);
        mixValue(hash, charge.sourceObjectId);
        mixValue(hash, charge.startTick);
        mixValue(hash, charge.expireTick);
        mixValue(hash, charge.level);
    }
    for (const auto& [arcId, arc] : electricArcs_) {
        mixValue(hash, arcId);
        mixValue(hash, arc.sourceObjectId);
        mixValue(hash, arc.targetObjectId);
        mixString(hash, arc.color);
        mixValue(hash, arc.expireTick);
        // Both sourceScreenOffset and sourceNativeFixed are local presentation.
    }
    for (const auto& [areaId, area] : radiationAreas_) {
        mixValue(hash, areaId);
        mixValue(hash, area.sourceObjectId);
        mixString(hash, area.owner);
        mixValue(hash, area.center.x);
        mixValue(hash, area.center.y);
        mixValue(hash, area.strength);
        mixValue(hash, area.remainingTicks);
        mixString(hash, area.warheadId);
    }
    for (const auto& [targetId, bomb] : timedBombs_.records()) {
        mixValue(hash, targetId);
        mixValue(hash, bomb.sourceObjectId);
        mixString(hash, bomb.owner);
        mixString(hash, timedBombKindName(bomb.kind));
        mixValue(hash, bomb.attachedTick);
        mixValue(hash, bomb.detonateTick);
        mixValue(hash, bomb.damage);
        mixString(hash, bomb.warheadId);
        mixValue(hash, bomb.forceDestruction);
    }
    for (const auto& [projectileId, pending]
         : parasitePendingByProjectile_) {
        mixValue(hash, projectileId);
        mixValue(hash, pending.sourceObjectId);
        mixValue(hash, pending.targetObjectId);
        mixString(hash, pending.owner);
        mixString(hash, pending.sourceTypeId);
        mixString(hash, pending.targetTypeId);
        mixString(hash, pending.weaponId);
        mixString(hash, pending.warheadId);
        mixString(hash, parasiteTargetKindName(pending.targetKind));
        mixValue(hash, pending.targetCell.x);
        mixValue(hash, pending.targetCell.y);
        mixValue(hash, pending.launchedTick);
        mixValue(hash, pending.damage);
        mixValue(hash, pending.damageIntervalTicks);
        mixValue(hash, pending.paralyzes);
        mixValue(hash, pending.targetMaximumHealth);
        mixValue(hash, pending.cullingThresholdPerMyriad);
        mixValue(hash, pending.targetParasiteable);
        mixValue(hash, pending.organic);
        mixValue(hash, pending.sourceElite);
        mixValue(hash, pending.culling);
    }
    for (const auto& [targetId, link] : parasiteByTarget_) {
        mixValue(hash, targetId);
        mixValue(hash, link.projectileId);
        mixValue(hash, link.sourceObjectId);
        mixString(hash, link.owner);
        mixString(hash, link.sourceTypeId);
        mixString(hash, link.targetTypeId);
        mixString(hash, link.weaponId);
        mixString(hash, link.warheadId);
        mixValue(hash, link.damage);
        mixValue(hash, link.attachedTick);
        mixValue(hash, link.nextDamageTick);
        mixValue(hash, link.damageIntervalTicks);
        mixValue(hash, link.organic);
        mixValue(hash, link.sourceElite);
        mixValue(hash, link.paralyzes);
        mixValue(hash, link.culling);
        mixValue(hash, link.cullingThresholdPerMyriad);
        mixValue(hash, link.targetMaximumHealth);
        mixValue(hash, link.sourceExpectedInLimbo);
    }
    for (const auto& [projectileId, resolved]
         : parasiteResolvedByProjectile_) {
        mixValue(hash, projectileId);
        mixValue(hash, resolved.sourceObjectId);
        mixValue(hash, resolved.targetObjectId);
        mixValue(hash, resolved.resolvedTick);
        mixValue(hash, resolved.hostTerminated);
    }
    for (const auto& [parentId, parent] : carrierParents_) {
        mixValue(hash, parentId);
        mixString(hash, parent.childType);
        mixString(hash, parent.owner);
        mixValue(hash, parent.capacity);
        mixValue(hash, parent.regenRate);
        mixValue(hash, parent.reloadRate);
        for (const auto& [slot, childId] : parent.childBySlot) {
            mixValue(hash, slot);
            mixValue(hash, childId);
            mixValue(hash, parent.slotReadyTick.at(slot));
        }
    }
    for (const auto& [childId, child] : carrierChildren_) {
        mixValue(hash, childId);
        mixValue(hash, child.parentObjectId);
        mixValue(hash, child.slot);
        mixString(hash, phaseName(child.phase));
        mixValue(hash, child.targetObjectId);
        mixValue(hash, child.actionTick);
        mixValue(hash, child.ammoRemaining);
        mixValue(hash, child.sortieShotsRemaining);
    }
    for (const auto& [id, missile] : spawnedMissiles_)
        mixString(hash, saveSpawnedMissileRow(missile, false));
    return hash;
}

std::vector<SpecialMechanismRenderSnapshot>
RuntimeSpecialMechanismState::renderSnapshots(int tick, int fixedTickHz) const {
    std::vector<SpecialMechanismRenderSnapshot> snapshots;
    for (const auto& [targetId, link] : mindControlByTarget_) {
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "mindControl";
        snapshot.objectId = link.controllerObjectId;
        snapshot.targetObjectId = targetId;
        // YURICNTL is the weapon/warhead's finite casting effect. The linked
        // target keeps its separate control ring until this relationship ends.
        snapshot.resourceId = link.permanent
            ? rules_.permaControlledAnimationType
            : rules_.controlledAnimationType;
        snapshot.owner = link.controllerOwner;
        snapshot.phase = link.permanent ? "permanent" : "controlled";
        snapshot.progress = static_cast<int>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(tick) - link.startedTick,
            0, std::numeric_limits<int>::max()));
        snapshot.requiredProgress = static_cast<int>(std::clamp<std::int64_t>(
            (static_cast<std::int64_t>(rules_.mindControlAttackLineFrames)
                * std::max(1, fixedTickHz) + 7) / 15,
            0, std::numeric_limits<int>::max()));
        snapshot.expireTick = static_cast<int>(std::clamp<std::int64_t>(
            static_cast<std::int64_t>(link.startedTick) + snapshot.requiredProgress,
            0, std::numeric_limits<int>::max()));
        snapshots.push_back(std::move(snapshot));
    }
    for (const auto& [objectId, state] : camouflageByObject_) {
        // The renderer hides the ordinary body whenever a camouflage
        // snapshot exists. Omitting it during a fake blink reveals the real
        // sprite while isCamouflaged() continues to protect its identity.
        if (!state.active || isCamouflageBlinking(objectId, tick)) {
            continue;
        }
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "camouflage";
        snapshot.objectId = objectId;
        snapshot.resourceId = state.disguiseType.empty()
            ? (rules_.defaultMirageDisguises.empty()
                ? std::string()
                : rules_.defaultMirageDisguises.front())
            : state.disguiseType;
        snapshot.expireTick = state.fakeBlinkUntilTick;
        snapshots.push_back(std::move(snapshot));
    }
    for (const auto& [objectId, state] : chronoByObject_) {
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "chrono";
        snapshot.objectId = objectId;
        snapshot.sourceCell = state.source;
        snapshot.targetCell = state.destination;
        snapshot.phase = chronoPhaseName(state.phase);
        // Departure/arrival SHPs have independent finite lifetimes in the host.
        // This attached sparkle only represents the immobilized recovery state.
        snapshot.resourceId = state.phase == ChronoPhase::WarpIn || state.phase == ChronoPhase::Cooldown
            ? rules_.chronoSparkleAnim : std::string();
        snapshot.progress = std::max(0, tick - state.completeTick);
        snapshot.requiredProgress = std::max(0, state.cooldownUntilTick - state.completeTick);
        snapshot.expireTick = state.cooldownUntilTick;
        snapshots.push_back(std::move(snapshot));
    }
    for (const auto& [targetId, state] : temporalByTarget_) {
        int progress = 0;
        for (const auto& [unusedAttacker, value] : state.progressByAttacker) {
            (void)unusedAttacker;
            progress += value;
        }
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "temporal";
        snapshot.targetObjectId = targetId;
        snapshot.progress = progress;
        snapshot.requiredProgress = state.requiredProgress;
        snapshots.push_back(snapshot);
        // One persistent ray per attacker, including during weapon cooldown.
        // WarpAway belongs to the completion event, not this frozen status.
        for (const auto& [attackerId, contribution] : state.progressByAttacker) {
            (void)contribution;
            snapshot.kind = "temporalBeam";
            snapshot.objectId = attackerId;
            snapshots.push_back(snapshot);
        }
    }
    for (const auto& [arcId, arc] : electricArcs_) {
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = arc.assault ? "electricAssault" : "electricArc";
        snapshot.objectId = arc.sourceObjectId;
        snapshot.targetObjectId = arc.targetObjectId;
        snapshot.sourceX = arc.source.x;
        snapshot.sourceY = arc.source.y;
        snapshot.sourceLevel = arc.source.z;
        snapshot.sourceScreenOffsetX = arc.sourceScreenOffsetX;
        snapshot.sourceScreenOffsetY = arc.sourceScreenOffsetY;
        snapshot.sourceNativeFixedX = arc.sourceNativeFixedX;
        snapshot.sourceNativeFixedY = arc.sourceNativeFixedY;
        snapshot.targetX = arc.target.x;
        snapshot.targetY = arc.target.y;
        snapshot.targetLevel = arc.target.z;
        snapshot.hasBeamEndpoints = true;
        snapshot.instanceId = arcId;
        snapshot.progress = std::max(0, tick - arc.startTick);
        snapshot.phase = arc.alternateColor ? "Alternate" : "Normal";
        snapshot.color = arc.color;
        snapshot.expireTick = arc.expireTick;
        snapshot.resourceId = std::to_string(arcId);
        snapshots.push_back(std::move(snapshot));
    }
    for (const auto& [targetId, charge] : electricChargeByTarget_) {
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "electricCharge";
        snapshot.objectId = charge.sourceObjectId;
        snapshot.targetObjectId = targetId;
        snapshot.expireTick = charge.expireTick;
        snapshot.progress = charge.level;
        snapshot.color.clear();
        snapshot.resourceId = "charge";
        snapshots.push_back(std::move(snapshot));
    }
    for (const auto& [areaId, area] : radiationAreas_) {
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "radiation";
        snapshot.objectId = area.sourceObjectId;
        snapshot.cell = area.center;
        snapshot.radius = area.radius;
        snapshot.intensity = area.strength;
        snapshot.maximumIntensity = area.maximumStrength;
        snapshot.owner = area.owner;
        snapshot.color = rules_.radiationColor;
        snapshot.expireTick = tick + area.remainingTicks;
        snapshot.resourceId = area.warheadId;
        snapshots.push_back(std::move(snapshot));
    }
    for (const auto& [targetId, bomb] : timedBombs_.records()) {
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "timedBomb";
        snapshot.objectId = bomb.sourceObjectId;
        snapshot.targetObjectId = targetId;
        snapshot.owner = bomb.owner;
        snapshot.phase = timedBombKindName(bomb.kind);
        snapshot.expireTick = bomb.detonateTick;
        snapshot.progress = std::max(0, tick - bomb.attachedTick);
        snapshot.requiredProgress = std::max(
            1,
            bomb.detonateTick - bomb.attachedTick
        );
        snapshot.resourceId = bomb.warheadId;
        snapshots.push_back(std::move(snapshot));
    }
    for (const auto& [targetId, link] : parasiteByTarget_) {
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "parasite";
        snapshot.objectId = link.sourceObjectId;
        snapshot.targetObjectId = targetId;
        snapshot.owner = link.owner;
        snapshot.phase = link.organic ? "organic" : "mechanical";
        snapshot.progress = std::max(0, tick - link.attachedTick);
        snapshot.requiredProgress = link.damageIntervalTicks;
        snapshot.expireTick = link.nextDamageTick;
        snapshot.intensity = link.damage;
        snapshot.resourceId = link.warheadId;
        snapshots.push_back(std::move(snapshot));
    }
    for (const auto& [childId, child] : carrierChildren_) {
        if (child.phase != CarrierChildPhase::Docked) {
            continue;
        }
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "carrierChildDocked";
        snapshot.objectId = childId;
        snapshot.targetObjectId = child.parentObjectId;
        snapshot.resourceId = "docked:" + std::to_string(child.slot);
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

const std::map<int, MindControlLinkState>&
RuntimeSpecialMechanismState::mindControlLinks() const {
    return mindControlByTarget_;
}

const std::map<int, CamouflageState>&
RuntimeSpecialMechanismState::camouflageStates() const {
    return camouflageByObject_;
}

const std::map<int, RadiationAreaState>&
RuntimeSpecialMechanismState::radiationAreas() const {
    return radiationAreas_;
}

const std::map<int, CarrierParentState>&
RuntimeSpecialMechanismState::carrierParents() const {
    return carrierParents_;
}

const std::map<int, CarrierChildState>&
RuntimeSpecialMechanismState::carrierChildren() const {
    return carrierChildren_;
}

const std::map<int, ElectricChargeState>&
RuntimeSpecialMechanismState::electricCharges() const {
    return electricChargeByTarget_;
}

const std::map<int, TimedBombState>&
RuntimeSpecialMechanismState::timedBombs() const {
    return timedBombs_.records();
}

const std::map<int, ParasitePendingLaunchState>&
RuntimeSpecialMechanismState::pendingParasiteLaunches() const {
    return parasitePendingByProjectile_;
}

const std::map<int, ParasiteLinkState>&
RuntimeSpecialMechanismState::parasiteLinks() const {
    return parasiteByTarget_;
}

