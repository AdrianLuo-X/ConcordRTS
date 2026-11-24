#pragma once
#include "simulation/abilities/parasites/RuntimeParasiteAtomicPolicy.h"
#include <algorithm>
#include <map>
#include <string>
namespace concord::simulation::abilities {
// Live synchronous borrows; the preflight path needs no production host ports.
class RuntimeParasiteHostEventQueries {
public:
 virtual ~RuntimeParasiteHostEventQueries() = default;
 virtual const std::map<int, ParasiteLinkState>& links() const = 0;
 virtual const int& liveTick() const noexcept = 0;
 virtual const std::unordered_set<int>& pendingExitIds() const noexcept = 0;
 virtual const RuntimeParasiteActionQueries& actions() const noexcept = 0;
 virtual RuntimeCellPoint objectCell(const WorldObjectInstance&) const = 0;
};
template<class OwnedCandidateFactory>
bool handleRuntimeParasiteHostEvent(
 RuntimeParasiteHostEventQueries& queries, RuntimeParasiteAtomicPorts& atomicPorts,
 OwnedCandidateFactory& factory, std::string& status,
 int targetObjectId, ParasiteHostEventKind event, int stunTicks) {
 const auto link = queries.links().find(targetObjectId);
 if (link == queries.links().end()) return true;
 const int sourceObjectId = link->second.sourceObjectId;
 const WorldObjectInstance* target = queries.actions().objectIncludingInactive(targetObjectId);
 const WorldObjectInstance* source = queries.actions().objectIncludingInactive(sourceObjectId);
 const RuntimeCellPoint targetCell = target == nullptr ? RuntimeCellPoint{} : queries.objectCell(*target);
 const std::optional<RuntimeCellPoint> ejectCell = source == nullptr ? std::nullopt
     : queries.actions().placementCell(*source, {targetCell.x, targetCell.y}, nullptr, -1);
 ParasiteHostEventRequest request;
 request.currentTick = queries.liveTick();
 request.targetObjectId = targetObjectId;
 request.event = event;
 request.ejectCellAvailable = ejectCell.has_value();
 if (ejectCell.has_value()) request.ejectCell = {ejectCell->x, ejectCell->y};
 request.stunTicks = std::max(0, stunTicks);
 auto specialCandidate = factory.copyCandidate();
 const ParasiteOperationResult handled = specialCandidate.handle(request);
 const std::vector<SpecialMechanismAction> prepared =
     prepareRuntimeParasiteActions(handled.actions, queries.actions());
 if (!handled.accepted || !validateRuntimeParasiteActionBatch(prepared, queries.pendingExitIds(), queries.actions())) {
  status = handled.reason.empty() ? "parasite host event rejected atomically"
      : "parasite host event rejected: " + handled.reason;
  return false;
 }
 factory.commitCandidate(specialCandidate);
 const RuntimeParasiteActionApplyResult applied = applyRuntimeParasiteActionsAtomically(
     atomicPorts, factory, prepared, {}, sourceObjectId, false);
 if (!applied.accepted) {
  status = "parasite host event commit rejected unexpectedly";
  return false;
 }
 return true;
}
template<class OwnedCandidateFactory>
bool preflightRuntimeParasiteHostEvent(
 RuntimeParasiteHostEventQueries& queries, OwnedCandidateFactory& factory,
 int targetObjectId, ParasiteHostEventKind event, int stunTicks) {
 const auto link = queries.links().find(targetObjectId);
 if (link == queries.links().end()) return true;
 const int sourceObjectId = link->second.sourceObjectId;
 const WorldObjectInstance* target = queries.actions().objectIncludingInactive(targetObjectId);
 const WorldObjectInstance* source = queries.actions().objectIncludingInactive(sourceObjectId);
 const RuntimeCellPoint targetCell = target == nullptr ? RuntimeCellPoint{} : queries.objectCell(*target);
 const std::optional<RuntimeCellPoint> ejectCell = source == nullptr ? std::nullopt
     : queries.actions().placementCell(*source, {targetCell.x, targetCell.y}, nullptr, -1);
 ParasiteHostEventRequest request;
 request.currentTick = queries.liveTick();
 request.targetObjectId = targetObjectId;
 request.event = event;
 request.ejectCellAvailable = ejectCell.has_value();
 if (ejectCell.has_value()) request.ejectCell = {ejectCell->x, ejectCell->y};
 request.stunTicks = std::max(0, stunTicks);
 auto specialCandidate = factory.copyCandidate();
 const ParasiteOperationResult handled = specialCandidate.handle(request);
 const std::vector<SpecialMechanismAction> prepared =
     prepareRuntimeParasiteActions(handled.actions, queries.actions());
 return handled.accepted && validateRuntimeParasiteActionBatch(prepared, queries.pendingExitIds(), queries.actions());
}
}
