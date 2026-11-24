#pragma once
#include "simulation/abilities/chrono/RuntimeChronoState.h"
#include "simulation/abilities/parasites/RuntimeParasiteAtomicPolicy.h"
#include <algorithm>
#include <optional>
#include <string>
#include <vector>
namespace concord::simulation::abilities {
class RuntimeParasiteChronoMoveQueries {
public:
    virtual ~RuntimeParasiteChronoMoveQueries() = default;
    virtual bool managedByAirstrike(int objectId) const = 0;
    virtual bool suspendedByMagBeam(int objectId) const = 0;
    virtual const WorldObjectInstance* canonicalObject(int objectId) const = 0;
    virtual bool allowsMoveToCell(const WorldObjectInstance&, const RuntimeCellPoint&) const = 0;
    virtual std::vector<WorldObjectInstance> copyObjects() const = 0;
    virtual const RuntimeParasiteActionQueries& actions() const noexcept = 0;
    virtual const std::unordered_set<int>& pendingExitIds() const noexcept = 0;
    virtual RuntimeCellPoint objectCell(const WorldObjectInstance&) const = 0;
    virtual const int& liveTick() const noexcept = 0;
    virtual const int& fixedTickHz() const noexcept = 0;
};
class RuntimeParasiteChronoMoveHostPorts {
public:
    virtual ~RuntimeParasiteChronoMoveHostPorts() = default;
    virtual void ensureInitialized() = 0;
    virtual WorldObjectInstance* mutableCanonicalObject(int objectId) = 0;
    // Construct the original sink before reading tick/coordinates/reason, then
    // clear render coordinates on this same borrow after its callback returns.
    virtual void warpOutAndClearBorrowed(WorldObjectInstance&, int, std::string&) = 0;
};
// The chrono candidate and the atomic candidate have distinct owners. The
// original atomic factory is reused unchanged after the original weak commits.
template<class ChronoCandidateFactory, class AtomicCandidateFactory>
bool requestRuntimeChronoMoveWithParasite(
    RuntimeParasiteChronoMoveQueries& queries,
    RuntimeParasiteChronoMoveHostPorts& hostPorts,
    RuntimeParasiteAtomicPorts& atomicPorts,
    ChronoCandidateFactory& chronoFactory,
    AtomicCandidateFactory& atomicFactory,
    int objectId, SpecialCell destination, int destinationLevel,
    const SpecialCellPredicate& cellIsLegal,
    const SpecialCellPredicate& cellIsOccupied, std::string& reason) {
    if (queries.managedByAirstrike(objectId)) {
        reason = "chrono move rejected: Airstrike owns plane";
        return false;
    }
    if (queries.suspendedByMagBeam(objectId)) {
        reason = "chrono move rejected: mag beam owns locomotor";
        return false;
    }
    const auto* mover = queries.canonicalObject(objectId);
    if (mover != nullptr && !queries.allowsMoveToCell(*mover, {destination.x, destination.y})) {
        reason = "chrono move rejected: destination is shrouded for the unit's owner";
        return false;
    }
    hostPorts.ensureInitialized();
    auto specialCandidate = chronoFactory.copyCandidate();
    std::vector<WorldObjectInstance> objectCandidate = queries.copyObjects();
    if (!chronoFactory.requestMove(specialCandidate, objectCandidate, objectId,
            destination, destinationLevel, cellIsLegal, cellIsOccupied, reason)) {
        return false;
    }
    std::vector<SpecialMechanismAction> parasiteActions;
    const auto link = specialCandidate.links().find(objectId);
    if (link != specialCandidate.links().end()) {
        const WorldObjectInstance* target = queries.canonicalObject(objectId);
        const WorldObjectInstance* source = queries.actions().objectIncludingInactive(link->second.sourceObjectId);
        const RuntimeCellPoint targetCell = target == nullptr ? RuntimeCellPoint{} : queries.objectCell(*target);
        const std::optional<RuntimeCellPoint> ejectCell = source == nullptr ? std::nullopt
            : queries.actions().placementCell(*source, {targetCell.x, targetCell.y}, nullptr, -1);
        ParasiteHostEventRequest teleporting;
        teleporting.currentTick = queries.liveTick();
        teleporting.targetObjectId = objectId;
        teleporting.event = ParasiteHostEventKind::Teleporting;
        teleporting.ejectCellAvailable = ejectCell.has_value();
        if (ejectCell.has_value()) teleporting.ejectCell = {ejectCell->x, ejectCell->y};
        teleporting.stunTicks = std::max(1, queries.fixedTickHz());
        const ParasiteOperationResult handled = specialCandidate.handle(teleporting);
        parasiteActions = prepareRuntimeParasiteActions(handled.actions, queries.actions());
        if (!handled.accepted || !validateRuntimeParasiteActionBatch(
                parasiteActions, queries.pendingExitIds(), queries.actions())) {
            reason = handled.reason.empty() ? "parasite teleport event rejected atomically" : handled.reason;
            return false;
        }
    }
    const auto shadow = std::find_if(objectCandidate.begin(), objectCandidate.end(),
        [objectId](const WorldObjectInstance& object) { return object.instanceId == objectId; });
    WorldObjectInstance* live = hostPorts.mutableCanonicalObject(objectId);
    if (shadow == objectCandidate.end() || live == nullptr || !live->alive) {
        reason = "chrono object disappeared before commit";
        return false;
    }
    chronoFactory.commitCandidate(specialCandidate);
    live->mission = shadow->mission;
    const RuntimeParasiteActionApplyResult applied = applyRuntimeParasiteActionsAtomically(
        atomicPorts, atomicFactory, parasiteActions, {}, objectId, false);
    if (!applied.accepted) {
        reason = "parasite teleport action commit rejected";
        return false;
    }
    if (auto* moved = hostPorts.mutableCanonicalObject(objectId)) {
        hostPorts.warpOutAndClearBorrowed(*moved, objectId, reason);
    }
    return true;
}
}
