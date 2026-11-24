#include "RuntimeTemporalState.h"
#include "world/model/GameWorld.h"
#include "simulation/buildings/RuntimeStructureRepack.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

void RuntimeTemporalState::reset() { byTarget_.clear(); }
void RuntimeTemporalState::swap(RuntimeTemporalState& other) noexcept { byTarget_.swap(other.byTarget_); }
bool RuntimeTemporalState::empty() const { return byTarget_.empty(); }
const std::map<int, TemporalTargetState>& RuntimeTemporalState::records() const { return byTarget_; }
bool RuntimeTemporalState::hasTemporalAttacker(int attackerObjectId, int targetObjectId) const {
    const auto target = byTarget_.find(targetObjectId);
    return target != byTarget_.end()
        && target->second.progressByAttacker.contains(attackerObjectId);
}
bool RuntimeTemporalState::beginTemporalTick(int targetObjectId, int tick) {
    const auto target = byTarget_.find(targetObjectId);
    if (target == byTarget_.end() || tick <= target->second.lastAdvancedTick) return false;
    target->second.lastAdvancedTick = tick;
    return true;
}
bool RuntimeTemporalState::applyTemporalHit(
    const RuntimeTemporalStateHitQueries& queries,
    int attackerObjectId,
    int targetObjectId,
    int progress,
    int requiredProgress,
    int tick,
    const std::function<bool(const std::string&, const std::string&)>& areAllied,
    const SpecialEventSink& emit,
    bool& completed,
    std::string& reason,
    bool allowAlliedTarget
) {
    completed = false;
    WorldObjectInstance* attacker = queries.findObject(attackerObjectId);
    WorldObjectInstance* target = queries.findObject(targetObjectId);
    if (attacker == nullptr || target == nullptr
        || !queries.combatSourceAvailable(attacker) || !target->alive) {
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
    if (queries.targetImmune(*target)) {
        reason = "target immune to temporal";
        return false;
    }

    auto [targetState, inserted] = byTarget_.try_emplace(targetObjectId);
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
        byTarget_.erase(targetObjectId);
    }
    return true;
}

bool RuntimeTemporalState::isTemporalFrozen(int objectId) const {
    const auto found = byTarget_.find(objectId);
    return found != byTarget_.end() && found->second.frozen;
}

bool RuntimeTemporalState::hasTemporalAuthorityForObject(
    int objectId
) const {
    if (byTarget_.find(objectId) != byTarget_.end()) {
        return true;
    }
    return std::any_of(
        byTarget_.begin(),
        byTarget_.end(),
        [objectId](const auto& entry) {
            return entry.second.progressByAttacker.find(objectId)
                != entry.second.progressByAttacker.end();
        }
    );
}

void RuntimeTemporalState::clearTemporalAttacker(
    int attackerObjectId,
    int targetObjectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    const auto targetIt = byTarget_.find(targetObjectId);
    if (targetIt == byTarget_.end()) {
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
        byTarget_.erase(targetIt);
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

void RuntimeTemporalState::clearTemporalTarget(
    int targetObjectId,
    int tick,
    const SpecialEventSink& emit,
    const std::string& reason
) {
    if (byTarget_.erase(targetObjectId) == 0) {
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


void RuntimeTemporalState::onObjectRemoved(int objectId, int tick,
    const SpecialEventSink& emit, const std::string& reason) {
    clearTemporalTarget(objectId, tick, emit, reason);
    std::vector<int> affectedTemporalTargets;
    for (const auto& [targetId, temporal] : byTarget_) {
        if (temporal.progressByAttacker.contains(objectId)) affectedTemporalTargets.push_back(targetId);
    }
    for (int targetId : affectedTemporalTargets) {
        clearTemporalAttacker(objectId, targetId, tick, emit, reason);
    }
}
void RuntimeTemporalState::reconcile(const RuntimeTemporalStateObjectQueries& queries) {
    for (auto it = byTarget_.begin(); it != byTarget_.end();) {
        if (queries.findObject(it->first) == nullptr
            || !queries.findObject(it->first)->alive) {
            it = byTarget_.erase(it);
        } else {
            ++it;
        }
    }
}
