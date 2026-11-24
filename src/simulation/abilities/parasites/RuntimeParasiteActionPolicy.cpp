#include "RuntimeParasiteActionPolicy.h"
#include "world/model/GameWorld.h"

namespace concord::simulation::abilities {
std::vector<SpecialMechanismAction> prepareRuntimeParasiteActions(
    const std::vector<SpecialMechanismAction>& actions,
    const RuntimeParasiteActionQueries& queries) {
    std::vector<SpecialMechanismAction> prepared = actions;
    std::unordered_set<int> reservedPlacementCells;
    for (SpecialMechanismAction& action : prepared) {
        if (action.kind
                != SpecialMechanismActionKind::ParasiteReturnFromLimbo
            && action.kind != SpecialMechanismActionKind::ParasiteEject) {
            continue;
        }
        const WorldObjectInstance* source =
            queries.objectIncludingInactive(action.objectId);
        const std::optional<RuntimeCellPoint> placement =
            source == nullptr
                ? std::nullopt
                : queries.placementCell(*source,
                    action.cell,
                    &reservedPlacementCells,
                    action.replacesTerminatingHost
                        ? action.targetObjectId : -1
                );
        if (!placement.has_value()) {
            action.kind = SpecialMechanismActionKind::ParasiteKillParasite;
            action.suppressDeathWeapon = true;
            action.replacesTerminatingHost = false;
            action.stunTicks = 0;
            action.reason += "; no legal host placement";
            continue;
        }
        action.cell = {placement->x, placement->y};
        reservedPlacementCells.insert(
            queries.cellKey(placement->x, placement->y)
        );
    }
    return prepared;
}

bool validateRuntimeParasiteActionBatch(
    const std::vector<SpecialMechanismAction>& actions,
    const std::unordered_set<int>& pendingObjectExitIds,
    const RuntimeParasiteActionQueries& queries) {
    std::unordered_set<int> returningSources;
    std::unordered_set<int> terminalObjects;
    for (const SpecialMechanismAction& action : actions) {
        if (action.kind == SpecialMechanismActionKind::TimedBombDetonate) {
            continue;
        }
        if (action.tick < 0 || action.projectileId == 0
            || action.objectId < 0 || action.targetObjectId < 0) {
            return false;
        }
        const WorldObjectInstance* source =
            queries.objectIncludingInactive(action.objectId);
        const WorldObjectInstance* target =
            queries.objectIncludingInactive(action.targetObjectId
            );
        if (action.replacesTerminatingHost
            && (source == nullptr || target == nullptr
                || source->instanceId == target->instanceId
                || action.stunTicks != 0
                || !((action.kind
                        == SpecialMechanismActionKind::ParasiteReturnFromLimbo
                        && target->kind == WorldObjectKind::Infantry)
                    || (action.kind == SpecialMechanismActionKind::ParasiteEject
                        && target->kind == WorldObjectKind::Unit)))) {
            return false;
        }
        if (action.sourceWasAttachedToHost
            && action.kind != SpecialMechanismActionKind::ParasiteEject
            && action.kind != SpecialMechanismActionKind::ParasiteKillParasite) {
            return false;
        }
        if (source != nullptr && !action.owner.empty()
            && queries.canonicalOwner(source->owner)
                != queries.canonicalOwner(action.owner
                )) {
            return false;
        }
        switch (action.kind) {
        case SpecialMechanismActionKind::ParasiteEnterLimbo:
            if (source == nullptr || !source->alive || source->health <= 0
                || pendingObjectExitIds.find(source->instanceId)
                    != pendingObjectExitIds.end()
                || queries.exclusiveAuthority(*source
                )) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteAttachHost:
            if (source == nullptr || source->alive || source->health <= 0
                || source->mission != "ParasiteLimbo"
                || target == nullptr || !target->alive
                || target->health <= 0
                || queries.exclusiveAuthority(*target,
                    true
                )) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteReturnFromLimbo:
        case SpecialMechanismActionKind::ParasiteEject:
            if (source == nullptr || source->alive || source->health <= 0
                || source->mission != "ParasiteLimbo"
                || !returningSources.insert(source->instanceId).second
                || !queries.placementCell(*source,
                    action.cell,
                    nullptr,
                    action.replacesTerminatingHost
                        ? action.targetObjectId : -1
                ).has_value()) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteDamage:
            if (target == nullptr || !target->alive || target->health <= 0
                || action.damage < 0) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteKillTarget:
            if (target == nullptr || !target->alive || target->health <= 0
                || !terminalObjects.insert(target->instanceId).second
                || pendingObjectExitIds.find(target->instanceId)
                    != pendingObjectExitIds.end()) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteKillParasite:
            if (source != nullptr && source->health > 0
                && (!terminalObjects.insert(source->instanceId).second
                    || pendingObjectExitIds.find(source->instanceId)
                        != pendingObjectExitIds.end())) {
                return false;
            }
            break;
        case SpecialMechanismActionKind::ParasiteReleaseTarget:
            break;
        case SpecialMechanismActionKind::TimedBombDetonate:
            break;
        }
    }
    return true;
}
}
