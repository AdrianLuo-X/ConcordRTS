#include "simulation/abilities/unit_special/RuntimeTimedBomb.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
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

} // namespace

void RuntimeTimedBombState::reset() { byTarget_.clear(); }
void RuntimeTimedBombState::swap(RuntimeTimedBombState& other) noexcept { byTarget_.swap(other.byTarget_); }
const std::map<int, TimedBombState>& RuntimeTimedBombState::records() const { return byTarget_; }

bool RuntimeTimedBombState::tryPlantC4(
    const TimedBombObjectQuery& objectQuery,
    const TimedBombEligibilityQuery& eligibility,
    const TimedBombRules& settings,
    int sourceObjectId,
    int targetObjectId,
    int tick,
    int fixedTickHz,
    const SpecialEventSink& emit,
    std::string& reason
) {
    const TimedBombObjectView source = objectQuery(sourceObjectId);
    const TimedBombObjectView target = objectQuery(targetObjectId);
    const auto reject = [&](const std::string& rejection) {
        reason = rejection;
        if (emit != nullptr) {
            emit({
                "TimedBombRejected",
                tick,
                sourceObjectId,
                targetObjectId,
                0,
                target.cell,
                rejection
            });
        }
        return false;
    };
    if (!source.present || !source.alive || source.health <= 0) {
        return reject("C4 source is not alive");
    }
    if (!target.present || !target.alive || target.health <= 0) {
        return reject("C4 target is not alive");
    }
    if (sourceObjectId == targetObjectId) {
        return reject("C4 source cannot be its target");
    }
    if (!eligibility(sourceObjectId, TimedBombEligibility::C4Source)) {
        return reject("source has no C4 capability");
    }
    // Every registered building accepts an explicitly ordered demolition.
    // Alliance/force-order policy belongs to the runtime command authority.
    if (!eligibility(targetObjectId, TimedBombEligibility::C4Target)) {
        return reject("target cannot receive C4");
    }
    if (byTarget_.find(targetObjectId)
        != byTarget_.end()) {
        return reject("target already has a timed bomb");
    }

    const double delayTicks = std::max(0.0, settings.c4DelayMinutes)
        * 60.0 * static_cast<double>(std::max(1, fixedTickHz));
    TimedBombState bomb;
    bomb.targetObjectId = targetObjectId;
    bomb.sourceObjectId = sourceObjectId;
    bomb.owner = source.owner;
    bomb.kind = TimedBombKind::C4;
    bomb.attachedTick = tick;
    bomb.detonateTick = tick + std::max(
        1,
        static_cast<int>(std::floor(delayTicks))
    );
    bomb.damage = std::numeric_limits<int>::max();
    bomb.warheadId = settings.c4Warhead;
    bomb.forceDestruction = true;
    byTarget_[targetObjectId] = bomb;
    reason.clear();
    if (emit != nullptr) {
        emit({
            "C4Placed",
            tick,
            sourceObjectId,
            targetObjectId,
            bomb.detonateTick,
            target.cell,
            bomb.warheadId
        });
    }
    return true;
}

bool RuntimeTimedBombState::tryAttachIvanBomb(
    const TimedBombObjectQuery& objectQuery,
    const TimedBombEligibilityQuery& eligibility,
    const TimedBombRules& settings,
    int sourceObjectId,
    int targetObjectId,
    int tick,
    int fixedTickHz,
    const SpecialEventSink& emit,
    std::string& reason
) {
    const TimedBombObjectView source = objectQuery(sourceObjectId);
    const TimedBombObjectView target = objectQuery(targetObjectId);
    const auto reject = [&](const std::string& rejection) {
        reason = rejection;
        if (emit != nullptr) {
            emit({
                "TimedBombRejected",
                tick,
                sourceObjectId,
                targetObjectId,
                0,
                target.cell,
                rejection
            });
        }
        return false;
    };
    if (!source.present || !source.alive || source.health <= 0) {
        return reject("Ivan source is not alive");
    }
    if (!target.present || !target.alive || target.health <= 0) {
        return reject("Ivan target is not alive");
    }
    if (sourceObjectId == targetObjectId) {
        return reject("Ivan source cannot be its target");
    }
    if (!eligibility(sourceObjectId, TimedBombEligibility::IvanSource)) {
        return reject("source has no Ivan capability");
    }
    if (!eligibility(targetObjectId, TimedBombEligibility::IvanTarget)) {
        return reject("target is not bombable");
    }
    if (byTarget_.find(targetObjectId)
        != byTarget_.end()) {
        return reject("target already has a timed bomb");
    }

    TimedBombState bomb;
    bomb.targetObjectId = targetObjectId;
    bomb.sourceObjectId = sourceObjectId;
    bomb.owner = source.owner;
    bomb.kind = TimedBombKind::Ivan;
    bomb.attachedTick = tick;
    bomb.detonateTick = tick + convertRuleFrames(
        settings.ivanTimedDelayFrames,
        fixedTickHz
    );
    bomb.damage = std::max(0, settings.ivanDamage);
    bomb.warheadId = settings.ivanWarhead;
    bomb.forceDestruction = false;
    byTarget_[targetObjectId] = bomb;
    reason.clear();
    if (emit != nullptr) {
        emit({
            "IvanBombAttached",
            tick,
            sourceObjectId,
            targetObjectId,
            bomb.detonateTick,
            target.cell,
            bomb.warheadId
        });
    }
    return true;
}

bool RuntimeTimedBombState::tryDisarmTimedBomb(
    const TimedBombObjectQuery& objectQuery,
    const TimedBombDisarmQuery& disarms,
    int sourceObjectId,
    int targetObjectId,
    const std::string& warheadId,
    int tick,
    const SpecialEventSink& emit,
    std::string& reason
) {
    const TimedBombObjectView source = objectQuery(sourceObjectId);
    const TimedBombObjectView target = objectQuery(targetObjectId);
    const bool warheadDisarms = disarms(warheadId);
    if (!source.present || !source.alive || source.health <= 0) {
        reason = "bomb-disarm source is not alive";
        return false;
    }
    if (!target.present || !target.alive || target.health <= 0) {
        reason = "bomb-disarm target is not alive";
        return false;
    }
    if (!warheadDisarms) {
        reason = "warhead does not disarm bombs";
        return false;
    }
    const auto bomb = byTarget_.find(targetObjectId);
    if (bomb == byTarget_.end()) {
        reason = "target has no timed bomb";
        return false;
    }
    const TimedBombKind kind = bomb->second.kind;
    byTarget_.erase(bomb);
    reason.clear();
    if (emit != nullptr) {
        emit({
            "TimedBombDisarmed",
            tick,
            sourceObjectId,
            targetObjectId,
            0,
            target.cell,
            timedBombKindName(kind)
        });
    }
    return true;
}

std::vector<SpecialMechanismAction>
RuntimeTimedBombState::tickTimedBombs(
    int tick,
    const SpecialEventSink& emit
) {
    std::vector<SpecialMechanismAction> actions;
    for (auto bomb = byTarget_.begin();
         bomb != byTarget_.end();) {
        if (tick < bomb->second.detonateTick) {
            ++bomb;
            continue;
        }
        const TimedBombState state = bomb->second;
        SpecialMechanismAction action;
        action.kind = SpecialMechanismActionKind::TimedBombDetonate;
        action.tick = tick;
        action.objectId = state.sourceObjectId;
        action.targetObjectId = state.targetObjectId;
        action.owner = state.owner;
        action.damage = state.damage;
        action.warheadId = state.warheadId;
        action.forceDestruction = state.forceDestruction;
        action.reason = timedBombKindName(state.kind) + " timer expired";
        actions.push_back(std::move(action));
        if (emit != nullptr) {
            emit({
                "TimedBombDetonated",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                state.damage,
                {},
                timedBombKindName(state.kind)
            });
        }
        bomb = byTarget_.erase(bomb);
    }
    return actions;
}

void RuntimeTimedBombState::onObjectRemoved(
    const TimedBombObjectQuery& objectQuery, int objectId, int tick,
    const SpecialEventSink& emit, const std::string& reason,
    std::vector<SpecialMechanismAction>* actions
) {
    const auto bomb = byTarget_.find(objectId);
    if (bomb != byTarget_.end()) {
        const TimedBombState state = bomb->second;
        if (state.kind == TimedBombKind::Ivan && actions != nullptr) {
            SpecialMechanismAction action;
            action.kind = SpecialMechanismActionKind::TimedBombDetonate;
            action.tick = tick;
            action.objectId = state.sourceObjectId;
            action.targetObjectId = state.targetObjectId;
            action.owner = state.owner;
            action.damage = state.damage;
            action.warheadId = state.warheadId;
            action.cell = objectQuery(objectId).cell;
            action.reason = "Ivan bomb target removed";
            actions->push_back(std::move(action));
        }
        if (emit != nullptr) {
            emit({
                state.kind == TimedBombKind::Ivan
                    ? "IvanBombTargetRemoved"
                    : "C4TargetRemoved",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                state.damage,
                objectQuery(objectId).cell,
                reason
            });
        }
        byTarget_.erase(bomb);
    }

}

void RuntimeTimedBombState::onOwnerChanged(int objectId, const std::string& newOwner) {
    for (auto& [unusedTargetId, bomb] : byTarget_) {
        (void)unusedTargetId;
        if (bomb.sourceObjectId == objectId) {
            bomb.owner = newOwner;
        }
    }
}

void RuntimeTimedBombState::reconcile(
    const TimedBombObjectQuery& objectQuery, int tick,
    const SpecialEventSink& emit, std::vector<SpecialMechanismAction>* actions
) {
    for (auto bomb = byTarget_.begin();
         bomb != byTarget_.end();) {
        const TimedBombObjectView target = objectQuery(bomb->first);
        if (target.present && target.alive && target.health > 0) {
            ++bomb;
            continue;
        }
        const TimedBombState state = bomb->second;
        if (state.kind == TimedBombKind::Ivan
            && target.present && actions != nullptr) {
            SpecialMechanismAction action;
            action.kind = SpecialMechanismActionKind::TimedBombDetonate;
            action.tick = tick;
            action.objectId = state.sourceObjectId;
            action.targetObjectId = state.targetObjectId;
            action.owner = state.owner;
            action.damage = state.damage;
            action.warheadId = state.warheadId;
            action.cell = target.cell;
            action.reason = "Ivan bomb target died during reconciliation";
            actions->push_back(std::move(action));
        }
        if (emit != nullptr) {
            emit({
                "TimedBombReconciled",
                tick,
                state.sourceObjectId,
                state.targetObjectId,
                state.damage,
                target.cell,
                !target.present ? "target missing" : "target dead"
            });
        }
        bomb = byTarget_.erase(bomb);
    }
}
