#include "RuntimeTemporalAttackPolicy.h"
#include "world/model/GameWorld.h"
#include "simulation/movement/RuntimeObjectBehavior.h"
#include "simulation/combat/targeting/RuntimeCombatDetails.h"
#include "data/rules/RulesDatabase.h"
#include "runtime/timing/RuntimeRuleTime.h"
#include <algorithm>
#include <cstdint>
#include <limits>
namespace concord::simulation::abilities {
namespace {
class WeaponScope {
public:
    explicit WeaponScope(RuntimeTemporalAttackQueries& q) noexcept : queries(q) {}
    ~WeaponScope() { queries.finishWeaponScope(); }
    WeaponScope(const WeaponScope&) = delete;
    WeaponScope& operator=(const WeaponScope&) = delete;
private:
    RuntimeTemporalAttackQueries& queries;
};
}
bool advanceRuntimeTemporalAttacks(const RuntimeTemporalAttackInputs& inputs,
    RuntimeTemporalAttackQueries& queries, RuntimeTemporalAttackEffects& effects) {
    if (inputs.targets.empty()) return false;
    // Original snapshot ownership/order: completion and interruption mutate it.
    const auto targets = inputs.targets;
    const auto sink = effects.eventSink();
    bool changed = false;
    bool playerObjectLost = false;
    for (const auto& [targetId, temporal] : targets) {
        const int previousTick = temporal.lastAdvancedTick < 0
            ? std::max(0, inputs.tick - 1) : temporal.lastAdvancedTick;
        if (!effects.beginTick(targetId, inputs.tick)) continue;
        const auto frames = runtimeRuleFrameAtTick(inputs.tick, inputs.fixedTickHz)
            - runtimeRuleFrameAtTick(previousTick, inputs.fixedTickHz);
        for (const auto& [attackerId, unusedProgress] : temporal.progressByAttacker) {
            const auto* target = queries.target(targetId);
            const auto* source = queries.sourceIncludingInactive(attackerId);
            const WorldObjectInstance* host = nullptr;
            const auto* explicitOrder = queries.order(attackerId);
            const bool forceFire = explicitOrder != nullptr
                && explicitOrder->kind == RuntimeBehaviorKind::Attack
                && explicitOrder->targetObjectId == targetId && explicitOrder->forceFire;
            bool valid = source && target && target->health > 0 && queries.canAct(*source)
                && !queries.deckProtected(*target)
                && (forceFire || !queries.allied(source->owner, target->owner));
            if (valid && !source->alive) {
                const auto* cursor = queries.cursor(attackerId);
                if (cursor != nullptr && cursor->lastTargetObjectId == targetId)
                    host = queries.target(cursor->sourceAnchorObjectId);
                valid = host && queries.validPassenger(*source, *host) && queries.canAct(*host);
            } else if (valid) {
                const auto* order = queries.order(attackerId);
                const auto* cursor = queries.cursor(attackerId);
                const auto* unit = queries.unitType(*source);
                const bool independentMoveFire = unit && unit->opportunityFire && !unit->noMovingFire
                    && order != nullptr && order->kind == RuntimeBehaviorKind::Move
                    && cursor != nullptr && cursor->lastTargetObjectId == targetId;
                valid = order != nullptr && order->kind == RuntimeBehaviorKind::Attack
                    && order->targetObjectId == targetId;
                valid = valid || independentMoveFire;
            }
            const auto* hostOrder = host == nullptr ? nullptr : queries.order(host->instanceId);
            const bool profileForceFire = forceFire
                || (hostOrder != nullptr && hostOrder->kind == RuntimeBehaviorKind::Attack
                    && hostOrder->targetObjectId == targetId && hostOrder->forceFire);
            const auto profile = queries.selectWeapon(valid, source, target, host, profileForceFire);
            const WeaponScope scope(queries);
            valid = valid && profile.weapon && profile.warhead && profile.warhead->temporal;
            if (valid) {
                const auto pose = queries.firingPose(*source, host);
                const double range = host ? queries.openToppedWarpDistance()
                    : queries.effectiveWeaponRange(pose, *target, *profile.weapon);
                const double distance = host ? queries.cellDistance(*host, *target)
                    : queries.weaponDistance(pose, *target, *profile.weapon);
                valid = distance <= range && distance + 0.001 >= profile.weapon->minimumRange
                    && queries.visible(source->owner, *target)
                    && queries.lineOfSight(pose, *target, *profile.weapon);
            }
            if (!valid) {
                effects.clearAttacker(attackerId, targetId, inputs.tick, sink,
                    "temporal source or target no longer available");
                changed = true;
                continue;
            }
            if (frames <= 0) continue;
            const int progress = static_cast<int>(std::min<std::int64_t>(
                std::numeric_limits<int>::max(), std::max(0, profile.weapon->damage) * frames));
            bool completed = false;
            std::string reason;
            changed = effects.applyHit(attackerId, targetId, progress,
                temporal.requiredProgress, inputs.tick, sink, completed, reason, forceFire) || changed;
            if (completed) {
                playerObjectLost = playerObjectLost || (target->kind != WorldObjectKind::Structure
                    && target->owner == inputs.economyOwner);
                effects.queueTemporalExit(targetId, attackerId, *source);
                break;
            }
        }
    }
    changed = effects.drainExitQueue() || changed;
    if (playerObjectLost && inputs.tick - inputs.lastUnitLostSoundTick
            >= std::max(1, inputs.fixedTickHz * 2)) {
        effects.emitUnitLostSound();
        inputs.lastUnitLostSoundTick = inputs.tick;
    }
    return changed;
}
void clearRuntimeTemporalAttack(const bool& initialized, int attackerObjectId,
    const RuntimeObjectBehavior& behavior, const int& tick,
    RuntimeTemporalAttackEffects& effects, const std::string& reason) {
    if (!initialized || behavior.kind != RuntimeBehaviorKind::Attack
        || behavior.targetObjectId < 0) return;
    effects.clearAttacker(attackerObjectId, behavior.targetObjectId, tick,
        effects.eventSink(), reason);
}
}
