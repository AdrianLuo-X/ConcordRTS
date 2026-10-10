#include "simulation/buildings/RuntimeGrindingSystem.h"

#include "core/io/IniFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>

namespace {

constexpr std::size_t maximumGrindingRelations = 4096;
constexpr std::size_t maximumGrindingPassengers = 128;
constexpr std::size_t maximumGrindingActionsPerTick = 8192;

std::string trimAscii(std::string value) {
    const auto notSpace = [](unsigned char character) {
        return !std::isspace(character);
    };
    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), notSpace)
    );
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end()
    );
    return value;
}

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

std::string canonicalOwner(
    const RuntimeGrindingContext& context,
    const std::string& owner
) {
    if (context.canonicalOwnerId) {
        return lowerAscii(trimAscii(context.canonicalOwnerId(owner)));
    }
    return lowerAscii(trimAscii(owner));
}

const WorldObjectInstance* findObject(
    const RuntimeGrindingContext& context,
    int objectId
) {
    if (context.objects == nullptr) {
        return nullptr;
    }
    const auto found = std::find_if(
        context.objects->begin(),
        context.objects->end(),
        [objectId](const WorldObjectInstance& object) {
            return object.instanceId == objectId;
        }
    );
    return found == context.objects->end() ? nullptr : &*found;
}

const UnitType* unitRulesForObject(
    const RuntimeGrindingContext& context,
    const WorldObjectInstance& object
) {
    if (context.rules == nullptr) {
        return nullptr;
    }
    if (object.kind == WorldObjectKind::Infantry) {
        return context.rules->findUnitType("InfantryType", object.type);
    }
    if (object.kind == WorldObjectKind::Unit) {
        return context.rules->findUnitType("UnitType", object.type);
    }
    return nullptr;
}

bool isChronoLocomotor(const UnitType& rules) {
    const std::string locomotor = lowerAscii(trimAscii(rules.locomotor));
    return rules.teleporter
        || locomotor.find("4a582747-9839-11d1-b709-00a024ddafd1")
            != std::string::npos;
}

bool isFlyingSource(
    const WorldObjectInstance& object,
    const UnitType& rules
) {
    const std::string movementZone =
        lowerAscii(trimAscii(rules.movementZone));
    const std::string speedType = lowerAscii(trimAscii(rules.speedType));
    return object.authoredAircraft
        || object.airborne
        || rules.consideredAircraft
        || rules.jumpJet
        || rules.balloonHover
        || movementZone == "fly"
        || speedType == "winged";
}

bool objectIsTransportedBy(
    const RuntimeGrindingContext& context,
    int objectId,
    int allowedTransportId
) {
    if (context.objects == nullptr) {
        return false;
    }
    for (const WorldObjectInstance& candidate : *context.objects) {
        if (!candidate.alive || candidate.instanceId == allowedTransportId) {
            continue;
        }
        if (std::find(
                candidate.transportPassengerIds.begin(),
                candidate.transportPassengerIds.end(),
                objectId
            ) != candidate.transportPassengerIds.end()) {
            return true;
        }
    }
    return false;
}

bool isRetainedTransportPassenger(const WorldObjectInstance& object) {
    return !object.alive
        && object.health > 0
        && lowerAscii(trimAscii(object.mission)) == "insidetransport";
}

bool isExactLiveTransportManifestMember(
    const RuntimeGrindingContext& context,
    const WorldObjectInstance& object,
    int transportId
) {
    if (transportId < 0) {
        return false;
    }
    const WorldObjectInstance* transport = findObject(context, transportId);
    return transport != nullptr
        && transport->alive
        && transport->instanceId != object.instanceId
        && std::count(
            transport->transportPassengerIds.begin(),
            transport->transportPassengerIds.end(),
            object.instanceId
        ) == 1;
}

bool snapshotPassengerIds(
    const WorldObjectInstance& source,
    std::vector<int>& passengerIds
) {
    passengerIds = source.transportPassengerIds;
    if (passengerIds.size() > maximumGrindingPassengers) {
        return false;
    }
    std::sort(passengerIds.begin(), passengerIds.end());
    if (std::adjacent_find(passengerIds.begin(), passengerIds.end())
            != passengerIds.end()
        || std::any_of(
            passengerIds.begin(),
            passengerIds.end(),
            [&source](int passengerId) {
                return passengerId < 0 || passengerId == source.instanceId;
            }
        )) {
        return false;
    }
    return true;
}

int clampedContextValue(
    const std::function<int(const WorldObjectInstance&)>& lookup,
    const WorldObjectInstance& object
) {
    return lookup ? std::max(0, lookup(object)) : 0;
}

int refundForObject(
    const RuntimeGrindingContext& context,
    const UnitType& rules,
    int purchaseValue
) {
    if (rules.soylent > 0) {
        return rules.soylent;
    }
    const long double raw = static_cast<long double>(purchaseValue)
        * static_cast<long double>(context.rules->getRefundPercent());
    if (!std::isfinite(static_cast<double>(raw)) || raw <= 0.0L) {
        return 0;
    }
    if (raw >= static_cast<long double>(
            std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(std::floor(raw));
}

RuntimeGrindingVeto ownerVeto(
    const RuntimeGrindingContext& context,
    const std::string& hostOwner,
    const std::string& sourceOwner
) {
    const std::string hostKey = canonicalOwner(context, hostOwner);
    const std::string sourceKey = canonicalOwner(context, sourceOwner);
    if (!hostKey.empty() && hostKey == sourceKey) {
        return RuntimeGrindingVeto::None;
    }
    if (context.areAllied && context.areAllied(hostOwner, sourceOwner)) {
        return RuntimeGrindingVeto::SourceAlliedNotOwned;
    }
    return RuntimeGrindingVeto::SourceEnemy;
}

struct PairValidation {
    RuntimeGrindingVeto veto = RuntimeGrindingVeto::InvalidContext;
    const WorldObjectInstance* host = nullptr;
    const BuildingType* hostRules = nullptr;
    const WorldObjectInstance* source = nullptr;
    const UnitType* sourceRules = nullptr;
    std::vector<int> passengerIds;
};

RuntimeGrindingVeto validateConsumableObject(
    const RuntimeGrindingContext& context,
    const WorldObjectInstance& host,
    const WorldObjectInstance& object,
    int allowedTransportId,
    bool requirePositiveRefund
) {
    // A manifest passenger is deliberately absent from the active world but
    // remains a value-bearing TechnoClass record.  Reject a direct attempt to
    // grind that record as a root before the ordinary alive check so callers
    // receive the precise transported-source veto.
    if (allowedTransportId < 0
        && objectIsTransportedBy(context, object.instanceId, -1)) {
        return RuntimeGrindingVeto::SourceTransported;
    }
    const bool exactManifestMember = allowedTransportId >= 0
        && isExactLiveTransportManifestMember(
            context,
            object,
            allowedTransportId
        );
    if (allowedTransportId >= 0 && !exactManifestMember) {
        return RuntimeGrindingVeto::PassengerManifestInvalid;
    }
    const bool retainedPassenger = exactManifestMember
        && isRetainedTransportPassenger(object);
    if (!object.alive && !retainedPassenger) {
        return RuntimeGrindingVeto::SourceUnavailable;
    }
    const UnitType* rules = unitRulesForObject(context, object);
    if (rules == nullptr
        || (object.kind != WorldObjectKind::Infantry
            && object.kind != WorldObjectKind::Unit)) {
        return RuntimeGrindingVeto::SourceNotGroundTechno;
    }
    const RuntimeGrindingVeto ownership = ownerVeto(
        context,
        host.owner,
        object.owner
    );
    if (ownership != RuntimeGrindingVeto::None) {
        return ownership;
    }
    if (rules->engineer) {
        return RuntimeGrindingVeto::SourceEngineer;
    }
    if (isFlyingSource(object, *rules)) {
        return RuntimeGrindingVeto::SourceFlying;
    }
    if (isChronoLocomotor(*rules)) {
        return RuntimeGrindingVeto::SourceChrono;
    }
    if (rules->naval
        || rules->waterBound
        || rules->underwater
        || object.submerged) {
        return RuntimeGrindingVeto::SourceNaval;
    }
    if (object.garrisonHostObjectId >= 0) {
        return RuntimeGrindingVeto::SourceGarrisoned;
    }
    if (objectIsTransportedBy(
            context,
            object.instanceId,
            allowedTransportId
        )) {
        return RuntimeGrindingVeto::SourceTransported;
    }
    if (allowedTransportId >= 0
        && !object.transportPassengerIds.empty()) {
        return RuntimeGrindingVeto::PassengerManifestInvalid;
    }
    if (requirePositiveRefund) {
        const int purchaseValue = clampedContextValue(
            context.purchaseValue,
            object
        );
        if (refundForObject(context, *rules, purchaseValue) <= 0) {
            return RuntimeGrindingVeto::SourceHasNoRefund;
        }
    }
    return RuntimeGrindingVeto::None;
}

PairValidation validatePair(
    const RuntimeGrindingContext& context,
    int hostId,
    int sourceId
) {
    PairValidation result;
    if (context.rules == nullptr
        || context.objects == nullptr
        || !context.purchaseValue
        || !context.currentValue
        || !context.isInsideHostFootprint) {
        result.veto = RuntimeGrindingVeto::InvalidContext;
        return result;
    }
    result.host = findObject(context, hostId);
    if (result.host == nullptr) {
        result.veto = RuntimeGrindingVeto::HostMissing;
        return result;
    }
    if (hostId == sourceId
        || !result.host->alive
        || result.host->rubble
        || result.host->kind != WorldObjectKind::Structure) {
        result.veto = RuntimeGrindingVeto::HostUnavailable;
        return result;
    }
    result.hostRules = context.rules->findBuildingType(result.host->type);
    if (result.hostRules == nullptr || !result.hostRules->grinding) {
        result.veto = RuntimeGrindingVeto::HostNotGrinder;
        return result;
    }
    if (!result.host->poweredOperational) {
        result.veto = RuntimeGrindingVeto::HostUnpowered;
        return result;
    }
    result.source = findObject(context, sourceId);
    if (result.source == nullptr) {
        result.veto = RuntimeGrindingVeto::SourceMissing;
        return result;
    }
    result.sourceRules = unitRulesForObject(context, *result.source);
    result.veto = validateConsumableObject(
        context,
        *result.host,
        *result.source,
        -1,
        true
    );
    if (result.veto != RuntimeGrindingVeto::None) {
        return result;
    }
    if (!snapshotPassengerIds(*result.source, result.passengerIds)) {
        result.veto = RuntimeGrindingVeto::PassengerManifestInvalid;
        return result;
    }
    for (const int passengerId : result.passengerIds) {
        if (passengerId == hostId) {
            result.veto = RuntimeGrindingVeto::PassengerManifestInvalid;
            return result;
        }
        const WorldObjectInstance* passenger = findObject(
            context,
            passengerId
        );
        if (passenger == nullptr) {
            result.veto = RuntimeGrindingVeto::PassengerManifestInvalid;
            return result;
        }
        result.veto = validateConsumableObject(
            context,
            *result.host,
            *passenger,
            sourceId,
            false
        );
        if (result.veto != RuntimeGrindingVeto::None) {
            result.veto = RuntimeGrindingVeto::PassengerManifestInvalid;
            return result;
        }
    }
    result.veto = RuntimeGrindingVeto::None;
    return result;
}

std::string vetoReason(RuntimeGrindingVeto veto) {
    return std::string(runtimeGrindingVetoName(veto));
}

RuntimeGrindingAction relationAction(
    RuntimeGrindingActionKind kind,
    const RuntimeGrindingRelation& relation,
    const std::string& reason
) {
    RuntimeGrindingAction action;
    action.kind = kind;
    action.hostId = relation.hostId;
    action.rootSourceId = relation.sourceId;
    action.objectId = relation.sourceId;
    action.relationPhase = relation.phase;
    action.reason = reason;
    return action;
}

bool relationMetadataMatches(
    const RuntimeGrindingContext& context,
    const RuntimeGrindingRelation& relation,
    const PairValidation& pair
) {
    return pair.host != nullptr
        && pair.source != nullptr
        && lowerAscii(trimAscii(pair.host->type))
            == lowerAscii(trimAscii(relation.hostType))
        && lowerAscii(trimAscii(pair.source->type))
            == lowerAscii(trimAscii(relation.sourceType))
        && canonicalOwner(context, pair.host->owner)
            == canonicalOwner(context, relation.hostOwner)
        && canonicalOwner(context, pair.source->owner)
            == canonicalOwner(context, relation.sourceOwner)
        && pair.passengerIds == relation.passengerIds;
}

std::vector<RuntimeGrindingAction> removalBatch(
    const RuntimeGrindingContext& context,
    const RuntimeGrindingRelation& relation
) {
    std::vector<int> objectIds = relation.passengerIds;
    objectIds.push_back(relation.sourceId);
    std::vector<RuntimeGrindingAction> actions;
    actions.reserve(objectIds.size());
    long long credited = 0;
    const int batchSize = static_cast<int>(objectIds.size());
    for (std::size_t index = 0; index < objectIds.size(); ++index) {
        const WorldObjectInstance* object = findObject(
            context,
            objectIds[index]
        );
        const UnitType* rules = object == nullptr
            ? nullptr
            : unitRulesForObject(context, *object);
        if (object == nullptr || rules == nullptr) {
            return {};
        }
        const int purchaseValue = clampedContextValue(
            context.purchaseValue,
            *object
        );
        const int currentValue = clampedContextValue(
            context.currentValue,
            *object
        );
        const int calculated = refundForObject(
            context,
            *rules,
            purchaseValue
        );
        const int remainingCredit = static_cast<int>(std::max(
            0LL,
            static_cast<long long>(std::numeric_limits<int>::max())
                - credited
        ));
        const int refund = std::min(calculated, remainingCredit);
        credited += refund;

        RuntimeGrindingAction action;
        action.kind = RuntimeGrindingActionKind::RemoveUnit;
        action.removalRole = object->instanceId == relation.sourceId
            ? RuntimeGrindingRemovalRole::Root
            : RuntimeGrindingRemovalRole::Passenger;
        action.terminalDisposition =
            RuntimeGrindingTerminalDisposition::ConsumeInsideGrinder;
        action.hostId = relation.hostId;
        action.rootSourceId = relation.sourceId;
        action.objectId = object->instanceId;
        action.batchIndex = static_cast<int>(index);
        action.batchSize = batchSize;
        action.relationPhase = relation.phase;
        action.owner = canonicalOwner(context, object->owner);
        action.objectType = object->type;
        action.purchaseValue = purchaseValue;
        action.currentValue = currentValue;
        action.refundCredits = refund;
        action.reason = object->instanceId == relation.sourceId
            ? "grinding source"
            : "grinding passenger";
        actions.push_back(std::move(action));
    }
    return actions;
}

bool parseIntStrict(const std::string& text, int& value) {
    const std::string normalized = trimAscii(text);
    if (normalized.empty()) {
        return false;
    }
    try {
        std::size_t parsed = 0;
        const long long candidate = std::stoll(normalized, &parsed, 10);
        if (parsed != normalized.size()
            || candidate < std::numeric_limits<int>::min()
            || candidate > std::numeric_limits<int>::max()) {
            return false;
        }
        value = static_cast<int>(candidate);
        return true;
    } catch (...) {
        return false;
    }
}

std::string hexEncode(std::string_view value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 2U);
    for (const unsigned char character : value) {
        encoded.push_back(digits[(character >> 4U) & 0x0FU]);
        encoded.push_back(digits[character & 0x0FU]);
    }
    return encoded;
}

int hexDigit(char character) {
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

bool hexDecode(const std::string& encoded, std::string& value) {
    if ((encoded.size() % 2U) != 0U) {
        return false;
    }
    value.clear();
    value.reserve(encoded.size() / 2U);
    for (std::size_t index = 0; index < encoded.size(); index += 2U) {
        const int high = hexDigit(encoded[index]);
        const int low = hexDigit(encoded[index + 1U]);
        if (high < 0 || low < 0) {
            value.clear();
            return false;
        }
        value.push_back(static_cast<char>((high << 4) | low));
    }
    return true;
}

std::string joinIds(const std::vector<int>& ids) {
    std::ostringstream output;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (index > 0) {
            output << ',';
        }
        output << ids[index];
    }
    return output.str();
}

bool parseIdList(
    const std::string& text,
    int expectedCount,
    std::vector<int>& ids
) {
    ids.clear();
    if (expectedCount == 0) {
        return trimAscii(text).empty();
    }
    std::istringstream input(text);
    std::string token;
    while (std::getline(input, token, ',')) {
        int value = -1;
        if (!parseIntStrict(token, value) || value < 0) {
            return false;
        }
        ids.push_back(value);
    }
    return static_cast<int>(ids.size()) == expectedCount
        && std::is_sorted(ids.begin(), ids.end())
        && std::adjacent_find(ids.begin(), ids.end()) == ids.end();
}

std::uint64_t hashText(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char character : text) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace

void RuntimeGrindingSystemState::reset() {
    lastAdvancedTick_ = -1;
    relations_.clear();
}

bool RuntimeGrindingSystemState::hasActiveEntry(
    const RuntimeGrindingContext& context, int hostId, int sourceId
) const {
    const auto relation = std::find_if(relations_.begin(), relations_.end(),
        [=](const RuntimeGrindingRelation& entry) {
            return entry.hostId == hostId && entry.sourceId == sourceId;
        });
    if (relation == relations_.end()) return false;
    const PairValidation pair = validatePair(context, hostId, sourceId);
    return pair.veto == RuntimeGrindingVeto::None
        && relationMetadataMatches(context, *relation, pair)
        && context.isEntryActive && context.isEntryActive(hostId, sourceId);
}

RuntimeGrindingCommandResult RuntimeGrindingSystemState::queueEntry(
    const RuntimeGrindingContext& context,
    int hostId,
    int sourceId,
    int currentTick
) {
    RuntimeGrindingCommandResult result;
    result.hostId = hostId;
    result.sourceId = sourceId;
    if (currentTick < 0) {
        result.veto = RuntimeGrindingVeto::InvalidContext;
        result.reason = vetoReason(result.veto);
        return result;
    }
    if (relations_.size() >= maximumGrindingRelations) {
        result.veto = RuntimeGrindingVeto::RelationCapacityReached;
        result.reason = vetoReason(result.veto);
        return result;
    }
    const PairValidation pair = validatePair(context, hostId, sourceId);
    if (pair.veto != RuntimeGrindingVeto::None) {
        result.veto = pair.veto;
        result.reason = vetoReason(result.veto);
        return result;
    }

    std::set<int> newManaged(pair.passengerIds.begin(), pair.passengerIds.end());
    newManaged.insert(sourceId);
    for (const RuntimeGrindingRelation& relation : relations_) {
        if (relation.sourceId == sourceId) {
            result.veto = RuntimeGrindingVeto::SourceAlreadyManaged;
            result.reason = vetoReason(result.veto);
            return result;
        }
        std::vector<int> managed = relation.passengerIds;
        managed.push_back(relation.sourceId);
        if (std::any_of(
                managed.begin(),
                managed.end(),
                [&newManaged](int objectId) {
                    return newManaged.find(objectId) != newManaged.end();
                }
            )) {
            result.veto = RuntimeGrindingVeto::PassengerAlreadyManaged;
            result.reason = vetoReason(result.veto);
            return result;
        }
    }

    RuntimeGrindingRelation relation;
    relation.hostId = hostId;
    relation.sourceId = sourceId;
    relation.phase = RuntimeGrindingRelationPhase::Queued;
    relation.hostOwner = pair.host->owner;
    relation.sourceOwner = pair.source->owner;
    relation.hostType = pair.host->type;
    relation.sourceType = pair.source->type;
    relation.queuedTick = currentTick;
    relation.passengerIds = pair.passengerIds;
    relations_.push_back(relation);
    sortState();

    result.accepted = true;
    result.veto = RuntimeGrindingVeto::None;
    result.reason = "grinding entry queued";
    result.actions.push_back(relationAction(
        RuntimeGrindingActionKind::MoveToHost,
        relation,
        result.reason
    ));
    return result;
}

RuntimeGrindingCommandResult RuntimeGrindingSystemState::beginEnter(
    const RuntimeGrindingContext& context,
    int hostId,
    int sourceId,
    int currentTick
) {
    RuntimeGrindingCommandResult result;
    result.hostId = hostId;
    result.sourceId = sourceId;
    auto relation = std::find_if(
        relations_.begin(),
        relations_.end(),
        [sourceId](const RuntimeGrindingRelation& candidate) {
            return candidate.sourceId == sourceId;
        }
    );
    if (relation == relations_.end()) {
        result.veto = RuntimeGrindingVeto::RelationMissing;
        result.reason = vetoReason(result.veto);
        return result;
    }
    if (relation->hostId != hostId || currentTick < relation->queuedTick) {
        result.veto = RuntimeGrindingVeto::RelationMismatch;
        result.reason = vetoReason(result.veto);
        return result;
    }
    const PairValidation pair = validatePair(context, hostId, sourceId);
    if (pair.veto != RuntimeGrindingVeto::None
        || !relationMetadataMatches(context, *relation, pair)) {
        result.veto = pair.veto == RuntimeGrindingVeto::None
            ? RuntimeGrindingVeto::RelationMismatch
            : pair.veto;
        result.reason = vetoReason(result.veto);
        return result;
    }
    relation->phase = RuntimeGrindingRelationPhase::Entering;
    if (relation->enteringTick < 0) {
        relation->enteringTick = currentTick;
    }
    result.accepted = true;
    result.veto = RuntimeGrindingVeto::None;
    result.reason = "grinding entry active";
    sortState();
    return result;
}

std::vector<RuntimeGrindingAction> RuntimeGrindingSystemState::cancelSource(
    int sourceId,
    const std::string& reason
) {
    std::vector<RuntimeGrindingAction> actions;
    const auto relation = std::find_if(
        relations_.begin(),
        relations_.end(),
        [sourceId](const RuntimeGrindingRelation& candidate) {
            return candidate.sourceId == sourceId;
        }
    );
    if (relation == relations_.end()) {
        return actions;
    }
    actions.push_back(relationAction(
        RuntimeGrindingActionKind::Release,
        *relation,
        reason
    ));
    relations_.erase(relation);
    return actions;
}

RuntimeGrindingReconcileReport RuntimeGrindingSystemState::reconcile(
    const RuntimeGrindingContext& context
) {
    RuntimeGrindingReconcileReport report;
    if (context.rules == nullptr
        || context.objects == nullptr
        || !context.purchaseValue
        || !context.currentValue
        || !context.isInsideHostFootprint) {
        return report;
    }
    report.contextValid = true;
    sortState();

    std::set<int> managedObjects;
    std::vector<RuntimeGrindingRelation> retained;
    retained.reserve(relations_.size());
    for (const RuntimeGrindingRelation& relation : relations_) {
        const PairValidation pair = validatePair(
            context,
            relation.hostId,
            relation.sourceId
        );
        RuntimeGrindingVeto veto = pair.veto;
        if (veto == RuntimeGrindingVeto::None
            && !relationMetadataMatches(context, relation, pair)) {
            veto = RuntimeGrindingVeto::RelationMismatch;
        }
        if (veto == RuntimeGrindingVeto::None
            && context.isEntryActive
            && !context.isEntryActive(
                relation.hostId,
                relation.sourceId
            )) {
            veto = RuntimeGrindingVeto::EntryInactive;
        }
        std::vector<int> relationObjects = relation.passengerIds;
        relationObjects.push_back(relation.sourceId);
        if (veto == RuntimeGrindingVeto::None) {
            std::set<int> relationManagedObjects;
            for (const int objectId : relationObjects) {
                if (managedObjects.find(objectId) != managedObjects.end()
                    || !relationManagedObjects.insert(objectId).second) {
                    veto = RuntimeGrindingVeto::PassengerAlreadyManaged;
                    ++report.duplicateManagedObjects;
                    break;
                }
            }
            if (veto == RuntimeGrindingVeto::None) {
                managedObjects.insert(
                    relationManagedObjects.begin(),
                    relationManagedObjects.end()
                );
            }
        }
        if (veto != RuntimeGrindingVeto::None) {
            ++report.releasedRelations;
            report.actions.push_back(relationAction(
                RuntimeGrindingActionKind::Release,
                relation,
                vetoReason(veto)
            ));
            continue;
        }
        retained.push_back(relation);
        ++report.retainedRelations;
    }
    relations_ = std::move(retained);
    sortState();
    return report;
}

std::vector<RuntimeGrindingAction> RuntimeGrindingSystemState::tick(
    const RuntimeGrindingContext& context,
    int currentTick
) {
    RuntimeGrindingReconcileReport reconciled = reconcile(context);
    std::vector<RuntimeGrindingAction> actions =
        std::move(reconciled.actions);
    if (!reconciled.contextValid
        || currentTick < 0
        || currentTick <= lastAdvancedTick_) {
        return actions;
    }
    lastAdvancedTick_ = currentTick;

    std::vector<int> completedSources;
    for (const RuntimeGrindingRelation& relation : relations_) {
        if (relation.phase != RuntimeGrindingRelationPhase::Entering
            || !context.isInsideHostFootprint(
                relation.hostId,
                relation.sourceId
            )) {
            continue;
        }
        const std::size_t batchSize = relation.passengerIds.size() + 1U;
        if (actions.size() + batchSize > maximumGrindingActionsPerTick) {
            continue;
        }
        std::vector<RuntimeGrindingAction> batch = removalBatch(
            context,
            relation
        );
        if (batch.size() != batchSize) {
            continue;
        }
        actions.insert(
            actions.end(),
            std::make_move_iterator(batch.begin()),
            std::make_move_iterator(batch.end())
        );
        completedSources.push_back(relation.sourceId);
    }
    if (!completedSources.empty()) {
        std::sort(completedSources.begin(), completedSources.end());
        relations_.erase(
            std::remove_if(
                relations_.begin(),
                relations_.end(),
                [&completedSources](const RuntimeGrindingRelation& relation) {
                    return std::binary_search(
                        completedSources.begin(),
                        completedSources.end(),
                        relation.sourceId
                    );
                }
            ),
            relations_.end()
        );
    }
    sortState();
    return actions;
}

void RuntimeGrindingSystemState::save(std::ostream& output) const {
    std::vector<RuntimeGrindingRelation> relations = relations_;
    std::sort(
        relations.begin(),
        relations.end(),
        [](const RuntimeGrindingRelation& left,
           const RuntimeGrindingRelation& right) {
            if (left.sourceId != right.sourceId) {
                return left.sourceId < right.sourceId;
            }
            return left.hostId < right.hostId;
        }
    );
    output << "[GrindingRuntime]\n";
    output << "Version=1\n";
    output << "LastAdvancedTick=" << lastAdvancedTick_ << "\n";
    output << "RelationCount=" << relations.size() << "\n\n";
    for (std::size_t index = 0; index < relations.size(); ++index) {
        const RuntimeGrindingRelation& relation = relations[index];
        output << "[GrindingRuntime.Relation." << index << "]\n";
        output << "HostId=" << relation.hostId << "\n";
        output << "SourceId=" << relation.sourceId << "\n";
        output << "Phase="
               << (relation.phase == RuntimeGrindingRelationPhase::Queued
                    ? "Queued"
                    : "Entering")
               << "\n";
        output << "HostOwnerHex=" << hexEncode(relation.hostOwner) << "\n";
        output << "SourceOwnerHex=" << hexEncode(relation.sourceOwner) << "\n";
        output << "HostTypeHex=" << hexEncode(relation.hostType) << "\n";
        output << "SourceTypeHex=" << hexEncode(relation.sourceType) << "\n";
        output << "QueuedTick=" << relation.queuedTick << "\n";
        output << "EnteringTick=" << relation.enteringTick << "\n";
        output << "PassengerCount=" << relation.passengerIds.size() << "\n";
        output << "PassengerIds=" << joinIds(relation.passengerIds) << "\n\n";
    }
}

bool RuntimeGrindingSystemState::load(
    const IniFile& ini,
    const RuntimeGrindingContext& context,
    std::string* errorMessage,
    RuntimeGrindingReconcileReport* reconcileReport
) {
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (reconcileReport != nullptr) {
        *reconcileReport = {};
    }
    const auto fail = [&](const std::string& message) {
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return false;
    };
    if (ini.getSection("GrindingRuntime") == nullptr) {
        reset();
        return true;
    }
    const auto value = [&ini](
        const std::string& section,
        const std::string& key,
        const std::string& fallback = std::string()
    ) {
        return ini.getValue(section, key, fallback);
    };

    int version = 0;
    int lastAdvancedTick = -1;
    int relationCount = 0;
    if (!parseIntStrict(value("GrindingRuntime", "Version"), version)
        || version != 1
        || !parseIntStrict(
            value("GrindingRuntime", "LastAdvancedTick", "-1"),
            lastAdvancedTick
        )
        || lastAdvancedTick < -1
        || !parseIntStrict(
            value("GrindingRuntime", "RelationCount"),
            relationCount
        )
        || relationCount < 0
        || static_cast<std::size_t>(relationCount)
            > maximumGrindingRelations) {
        return fail("grinding runtime save has invalid header values");
    }

    RuntimeGrindingSystemState loaded;
    loaded.lastAdvancedTick_ = lastAdvancedTick;
    std::set<int> sourceIds;
    for (int index = 0; index < relationCount; ++index) {
        const std::string section =
            "GrindingRuntime.Relation." + std::to_string(index);
        RuntimeGrindingRelation relation;
        int passengerCount = 0;
        const std::string phase = lowerAscii(value(section, "Phase"));
        if (ini.getSection(section) == nullptr
            || !parseIntStrict(value(section, "HostId"), relation.hostId)
            || relation.hostId < 0
            || !parseIntStrict(value(section, "SourceId"), relation.sourceId)
            || relation.sourceId < 0
            || relation.sourceId == relation.hostId
            || !hexDecode(
                value(section, "HostOwnerHex"),
                relation.hostOwner
            )
            || trimAscii(relation.hostOwner).empty()
            || !hexDecode(
                value(section, "SourceOwnerHex"),
                relation.sourceOwner
            )
            || trimAscii(relation.sourceOwner).empty()
            || !hexDecode(value(section, "HostTypeHex"), relation.hostType)
            || trimAscii(relation.hostType).empty()
            || !hexDecode(
                value(section, "SourceTypeHex"),
                relation.sourceType
            )
            || trimAscii(relation.sourceType).empty()
            || !parseIntStrict(
                value(section, "QueuedTick"),
                relation.queuedTick
            )
            || relation.queuedTick < 0
            || !parseIntStrict(
                value(section, "EnteringTick"),
                relation.enteringTick
            )
            || !parseIntStrict(
                value(section, "PassengerCount"),
                passengerCount
            )
            || passengerCount < 0
            || static_cast<std::size_t>(passengerCount)
                > maximumGrindingPassengers
            || !parseIdList(
                value(section, "PassengerIds"),
                passengerCount,
                relation.passengerIds
            )
            || !sourceIds.insert(relation.sourceId).second) {
            return fail("grinding runtime save has invalid relation ["
                + section + "]");
        }
        if (phase == "queued") {
            relation.phase = RuntimeGrindingRelationPhase::Queued;
            if (relation.enteringTick != -1) {
                return fail("queued grinding relation has entering tick");
            }
        } else if (phase == "entering") {
            relation.phase = RuntimeGrindingRelationPhase::Entering;
            if (relation.enteringTick < relation.queuedTick) {
                return fail("entering grinding relation has invalid ticks");
            }
        } else {
            return fail("grinding runtime save has invalid relation phase");
        }
        if (std::binary_search(
                relation.passengerIds.begin(),
                relation.passengerIds.end(),
                relation.sourceId
            )
            || std::binary_search(
                relation.passengerIds.begin(),
                relation.passengerIds.end(),
                relation.hostId
            )) {
            return fail("grinding runtime save has recursive passenger ids");
        }
        loaded.relations_.push_back(std::move(relation));
    }

    loaded.sortState();
    RuntimeGrindingReconcileReport report = loaded.reconcile(context);
    if (!report.contextValid
        || report.releasedRelations != 0
        || report.retainedRelations
            != static_cast<std::size_t>(relationCount)) {
        return fail("grinding runtime diverged during strict reconcile");
    }
    *this = std::move(loaded);
    if (reconcileReport != nullptr) {
        *reconcileReport = std::move(report);
    }
    return true;
}

std::uint64_t RuntimeGrindingSystemState::stateHash() const {
    std::ostringstream output;
    save(output);
    return hashText(output.str());
}

const std::vector<RuntimeGrindingRelation>&
RuntimeGrindingSystemState::relations() const {
    return relations_;
}

int RuntimeGrindingSystemState::lastAdvancedTick() const {
    return lastAdvancedTick_;
}

void RuntimeGrindingSystemState::sortState() {
    std::sort(
        relations_.begin(),
        relations_.end(),
        [](const RuntimeGrindingRelation& left,
           const RuntimeGrindingRelation& right) {
            if (left.sourceId != right.sourceId) {
                return left.sourceId < right.sourceId;
            }
            return left.hostId < right.hostId;
        }
    );
}

const char* runtimeGrindingVetoName(RuntimeGrindingVeto veto) {
    switch (veto) {
    case RuntimeGrindingVeto::None: return "none";
    case RuntimeGrindingVeto::InvalidContext: return "invalid context";
    case RuntimeGrindingVeto::RelationCapacityReached:
        return "relation capacity reached";
    case RuntimeGrindingVeto::HostMissing: return "host missing";
    case RuntimeGrindingVeto::HostUnavailable: return "host unavailable";
    case RuntimeGrindingVeto::HostNotGrinder: return "host not grinder";
    case RuntimeGrindingVeto::HostUnpowered: return "host unpowered";
    case RuntimeGrindingVeto::SourceMissing: return "source missing";
    case RuntimeGrindingVeto::SourceUnavailable: return "source unavailable";
    case RuntimeGrindingVeto::SourceNotGroundTechno:
        return "source not ground techno";
    case RuntimeGrindingVeto::SourceEngineer: return "source engineer";
    case RuntimeGrindingVeto::SourceFlying: return "source flying";
    case RuntimeGrindingVeto::SourceChrono: return "source chrono";
    case RuntimeGrindingVeto::SourceNaval: return "source naval";
    case RuntimeGrindingVeto::SourceTransported:
        return "source transported";
    case RuntimeGrindingVeto::SourceGarrisoned:
        return "source garrisoned";
    case RuntimeGrindingVeto::SourceAlliedNotOwned:
        return "source allied but not owned";
    case RuntimeGrindingVeto::SourceEnemy: return "source enemy";
    case RuntimeGrindingVeto::SourceAlreadyManaged:
        return "source already managed";
    case RuntimeGrindingVeto::SourceHasNoRefund:
        return "source has no refund";
    case RuntimeGrindingVeto::PassengerManifestInvalid:
        return "passenger manifest invalid";
    case RuntimeGrindingVeto::PassengerAlreadyManaged:
        return "passenger already managed";
    case RuntimeGrindingVeto::RelationMissing: return "relation missing";
    case RuntimeGrindingVeto::RelationMismatch: return "relation mismatch";
    case RuntimeGrindingVeto::EntryInactive: return "entry inactive";
    case RuntimeGrindingVeto::SourceNotInsideFootprint:
        return "source not inside footprint";
    }
    return "unknown";
}

const char* runtimeGrindingActionName(RuntimeGrindingActionKind kind) {
    switch (kind) {
    case RuntimeGrindingActionKind::MoveToHost: return "move-to-host";
    case RuntimeGrindingActionKind::RemoveUnit: return "remove-unit";
    case RuntimeGrindingActionKind::Release: return "release";
    }
    return "unknown";
}
