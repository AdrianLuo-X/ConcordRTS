#include "simulation/abilities/parasites/RuntimeParasiteHostDeathPolicy.h"
#include "world/model/GameWorld.h"
namespace concord::simulation::abilities {
bool collectRuntimeParasiteHostDeathActions(
    const std::vector<SpecialMechanismAction>& prepared, const int& tick,
    const RuntimeParasiteHostDeathQueries& queries, RuntimeParasiteHostDeathCandidate& candidate,
    std::vector<SpecialMechanismAction>& deathActions, bool& hasHostDeath,
    RuntimeParasiteHostDeathRejection& rejection) {
    for (const SpecialMechanismAction& action : prepared) {
        if (action.kind != SpecialMechanismActionKind::ParasiteDamage
            && action.kind
                != SpecialMechanismActionKind::ParasiteKillTarget) {
            continue;
        }
        const WorldObjectInstance* target =
            queries.objectIncludingInactive(action.targetObjectId);
        if (target == nullptr || !target->alive || target->health <= 0) {
            continue;
        }
        if (action.kind == SpecialMechanismActionKind::ParasiteDamage
            && queries.damageIsTypeImmune(*target, action.objectId, action.damage)) continue;
        bool lethal = action.kind
            == SpecialMechanismActionKind::ParasiteKillTarget;
        if (!lethal) {
            const int appliedDamage = queries.authoredDamage(
                *target,
                action.damage,
                action.warheadId,
                0.0,
                1.0
            );
            lethal = appliedDamage >= target->health;
        }
        const auto link = candidate.links().find(
            action.targetObjectId
        );
        if (!lethal
            || link == candidate.links().end()) {
            continue;
        }
        const WorldObjectInstance* source =
            queries.objectIncludingInactive(link->second.sourceObjectId);
        const RuntimeCellPoint targetCell =
            queries.objectCell(*target);
        const std::optional<RuntimeCellPoint> ejectCell = source == nullptr
            ? std::nullopt
            : queries.placementCell(
                *source,
                {targetCell.x, targetCell.y},
                nullptr,
                target->instanceId
            );
        ParasiteHostEventRequest hostDeath;
        hostDeath.currentTick = tick;
        hostDeath.targetObjectId = action.targetObjectId;
        hostDeath.event = ParasiteHostEventKind::DestroyedByParasite;
        hostDeath.ejectCellAvailable = ejectCell.has_value();
        if (ejectCell.has_value()) {
            hostDeath.ejectCell = {ejectCell->x, ejectCell->y};
        }
        const ParasiteOperationResult handled =
            candidate.handle(hostDeath);
        if (!handled.accepted) {
            rejection.rejectAtomically();
            return false;
        }
        deathActions.insert(
            deathActions.end(),
            handled.actions.begin(),
            handled.actions.end()
        );
        hasHostDeath = true;
    }

    return true;
}
}
