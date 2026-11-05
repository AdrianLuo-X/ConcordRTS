#include "simulation/triggers/RuntimeTriggerScheduler.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "runtime/performance/RuntimePerfTrace.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using runtime_authority_hash::runtimeStableHashString;
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_save_text::trimAsciiLocal;

// Original logical cell comparison is inline in world/values/RuntimeCellPoint.h.

// Original logical cell comparison is inline in world/values/RuntimeCellPoint.h.

bool runtimeIsCreateTeamAction(int actionType) {
    return actionType == 4
        || actionType == 7
        || actionType == 80
        || actionType == 107;
}

bool runtimeIsSuperWeaponTriggerAction(int actionType) {
    return actionType == 33
        || actionType == 34
        || actionType == 95
        || actionType == 102
        || actionType == 109;
}

bool advanceRuntimeCampaignTriggers(RuntimeTriggerContext& state, RuntimeTriggerOperations& operations) {
    RuntimePerfTraceScope originalMapTriggerPhaseTrace("advanceRuntimeCampaignTriggers", state.tick);
    if (!state.triggers.loaded || state.skirmishMatchFinished) {
        return false;
    }

    operations.sampleLightingThresholdEvents();
    bool changed = false;
    if (state.triggers.campaignIonStormUntilTick > state.tick) {
        if (state.triggers.campaignNextIonStormStrikeTick <= 0) {
            state.triggers.campaignNextIonStormStrikeTick = state.tick
                + operations.campaignIonStrikeDelayTicks();
            changed = true;
        }
        if (state.tick
                >= state.triggers.campaignNextIonStormStrikeTick) {
            const std::vector<TerrainCell>& terrain =
                state.world.getTerrainCells();
            if (!terrain.empty()) {
                std::uint64_t strikeSeed = runtimeStableHashString(
                    state.config.defaultMapPath + "|ion-storm|"
                    + std::to_string(state.tick)
                );
                const TerrainCell& strikeCell = terrain[
                    static_cast<std::size_t>(
                        strikeSeed % terrain.size()
                    )
                ];
                (void)operations.fireCampaignIonStrike(RuntimeCellPoint{strikeCell.x, strikeCell.y},
                    "Neutral"
                );
            }
            state.triggers.campaignNextIonStormStrikeTick = state.tick
                + operations.campaignIonStrikeDelayTicks();
            changed = true;
        }
    } else if (state.triggers.campaignIonStormUntilTick > 0
        || state.triggers.campaignNextIonStormStrikeTick > 0) {
        state.triggers.campaignIonStormUntilTick = 0;
        state.triggers.campaignNextIonStormStrikeTick = 0;
        state.repaintRequested = true;
        changed = true;
    }
    if (state.triggers.campaignMissionTimerRunning
        && state.triggers.campaignMissionTimerRemainingTicks > 0) {
        --state.triggers.campaignMissionTimerRemainingTicks;
        changed = true;
    }
    for (RuntimeTriggerDefinition& trigger : state.triggers.triggers) {
        if (!trigger.enabled
            || (!trigger.repeating && trigger.fired)
            || state.tick < trigger.nextEligibleTick) {
            continue;
        }

        // AttachedTrigger is a shared-tag chain, not a prerequisite edge.
        if (trigger.events.empty()) {
            continue;
        }

        bool eventsSatisfied = true;
        for (const RuntimeTriggerEvent& event : trigger.events) {
            if (!operations.campaignEventSatisfied(trigger, event)) {
                eventsSatisfied = false;
                break;
            }
        }
        if (!eventsSatisfied) {
            continue;
        }

        if (trigger.tagRepeatType == 1
            && !trigger.remainingOnceAllObjectIds.empty()) {
            std::vector<int> completedTargets;
            for (int objectId : trigger.remainingOnceAllObjectIds) {
                const WorldObjectInstance* object =
                    operations.objectIncludingInactive(objectId);
                if (object == nullptr) {
                    completedTargets.push_back(objectId);
                    continue;
                }
                const auto previousAlive =
                    state.triggers.campaignPreviousAliveByObject.find(
                        objectId
                    );
                const auto previousOwner =
                    state.triggers.campaignPreviousOwnerByObject.find(
                        objectId
                    );
                const auto entered =
                    state.triggers.campaignEnteredTickByTargetObject.find(
                        objectId
                    );
                const bool destroyed = !object->alive
                    && previousAlive
                        != state.triggers.campaignPreviousAliveByObject.end()
                    && previousAlive->second;
                const bool captured = previousOwner
                        != state.triggers.campaignPreviousOwnerByObject.end()
                    && !operations.campaignHouseMatches(previousOwner->second,
                        object->owner
                    );
                const bool infiltrated = entered
                        != state.triggers.campaignEnteredTickByTargetObject.end()
                    && entered->second >= trigger.activationTick
                    && entered->second > trigger.lastFiredTick;
                if (destroyed || captured || infiltrated) {
                    completedTargets.push_back(objectId);
                }
            }
            for (int objectId : completedTargets) {
                trigger.remainingOnceAllObjectIds.erase(objectId);
            }
            if (!completedTargets.empty()) {
                changed = true;
            }
            if (!trigger.remainingOnceAllObjectIds.empty()) {
                continue;
            }
        }

        const std::size_t firedBefore = state.triggers.firedTriggers;
        bool actionApplied = false;
        for (const RuntimeTriggerAction& action : trigger.actions) {
            actionApplied = operations.applyCampaignAction(trigger,
                action
            ) || actionApplied;
            if (state.skirmishMatchFinished) {
                break;
            }
        }
        trigger.lastFiredTick = state.tick;
        ++state.triggers.firedTriggers;
        trigger.nextEligibleTick = state.tick
            + (trigger.repeating ? std::max(1, state.config.fixedTickHz) : 0);
        if (!trigger.repeating) {
            trigger.fired = true;
        } else {
            trigger.activationTick = state.tick;
        }
        changed = true;
        if (firedBefore == static_cast<std::size_t>(state.triggers.firedTriggers) && !actionApplied) {
            state.triggers.status =
                "campaign trigger fired without executable action "
                + trigger.id;
        }
        if (state.skirmishMatchFinished) {
            break;
        }
    }
    operations.snapshotCampaignObservations();
    return changed;
}

bool advanceRuntimeTriggers(RuntimeTriggerContext& state, RuntimeTriggerOperations& operations) {
    const bool reinforcementChanged = operations.advanceReinforcements();
    if (operations.cooperativeActive()) {
        const bool missionsChanged = operations.advanceTeamMissions() || reinforcementChanged;
        const bool eventsChanged = advanceRuntimeCampaignTriggers(state, operations);
        state.triggers.bootstrapComplete = true;
        return missionsChanged || eventsChanged;
    }
    if (state.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode))
            == "CAMPAIGNMISSION") {
        const bool missionsChanged = operations.advanceTeamMissions() || reinforcementChanged;
        const bool triggersChanged = advanceRuntimeCampaignTriggers(state, operations);
        const bool aiChanged = operations.advanceCampaignAiProduction();
        return missionsChanged || triggersChanged || aiChanged;
    }
    if (!state.triggers.loaded) {
        return reinforcementChanged;
    }

    const bool missionsChanged = operations.advanceTeamMissions() || reinforcementChanged;
    if (!state.triggers.bootstrapComplete
        && !state.triggers.allowLegacyBootstrapForSmokeTest) {
        // A real skirmish receives its opponents from the configured player
        // slots.  The legacy runtime bootstrap below exists only to exercise
        // trigger-team execution in the dedicated smoke test: it ignores
        // trigger events and may select an arbitrary AIMD TeamType.  Such a
        // team frequently has no map waypoint, so its fallback is waypoint 0
        // -- normally the local start -- and it injects hostile units beside
        // the player's construction yard.  Keep normal play isolated even if
        // no lobby slots were propagated into the runtime configuration.
        state.triggers.bootstrapComplete = true;
        state.triggers.status =
            "skirmish: legacy trigger-team bootstrap disabled";
        return missionsChanged;
    }
    if (state.triggers.bootstrapComplete) {
        return missionsChanged;
    }

    if (state.tick < 30) {
        return missionsChanged;
    }

    for (RuntimeTriggerDefinition& trigger : state.triggers.triggers) {
        if (trigger.fired) {
            continue;
        }

        for (const RuntimeTriggerAction& action : trigger.actions) {
            if (runtimeIsSuperWeaponTriggerAction(action.type)) {
                const bool fired = operations.fireSuperWeapon(trigger,
                    action
                );
                if (fired) {
                    trigger.fired = true;
                    ++state.triggers.firedTriggers;
                    state.triggers.bootstrapComplete = true;
                    state.triggers.status =
                        "fired " + trigger.id
                        + " -> superweapon action="
                        + std::to_string(action.type);
                    return true;
                }
                continue;
            }
            if (runtimeIsCreateTeamAction(action.type)) {
                const bool fired =
                    operations.fireCreateTeam(trigger, action);
                state.triggers.bootstrapComplete = true;
                return fired || missionsChanged;
            }
            ++state.triggers.unsupportedActions;
        }
    }

    if (const RuntimeTeamType* fallbackTeam = operations.chooseTeam()) {
        RuntimeTriggerDefinition syntheticTrigger;
        syntheticTrigger.id = "runtime_bootstrap";
        RuntimeTriggerAction syntheticAction;
        syntheticAction.type = 80;
        syntheticAction.targetId = fallbackTeam->id;
        const bool fired =
            operations.fireCreateTeam(syntheticTrigger, syntheticAction);
        state.triggers.bootstrapComplete = true;
        return fired || missionsChanged;
    }

    state.triggers.bootstrapComplete = true;
    state.triggers.status = "no create-team trigger available";
    return missionsChanged;
}
