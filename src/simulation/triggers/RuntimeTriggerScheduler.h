#pragma once
#include "simulation/triggers/RuntimeTriggerState.h"
#include "runtime/config/RuntimeConfig.h"
#include "world/model/GameWorld.h"

// Borrowed live authority fields for one synchronous phase. Operations may
// mutate the same session; no copied tick, mode or completion state is cached.
struct RuntimeTriggerContext {
    RuntimeTriggerState& triggers;
    const GameWorld& world;
    const RuntimeConfig& config;
    int& tick;
    const bool& campaignMode;
    bool& skirmishMatchFinished;
    bool& repaintRequested;
};

// Typed synchronous access to existing event/action/team owners. Returned
// objects/teams are borrowed for the current call; no operation is retained.
class RuntimeTriggerOperations {
public:
    virtual ~RuntimeTriggerOperations() = default;
    virtual void sampleLightingThresholdEvents() = 0;
    virtual int campaignIonStrikeDelayTicks() = 0;
    virtual bool fireCampaignIonStrike(const RuntimeCellPoint& cell, const std::string& owner) = 0;
    virtual bool campaignEventSatisfied(const RuntimeTriggerDefinition& trigger, const RuntimeTriggerEvent& event) = 0;
    virtual const WorldObjectInstance* objectIncludingInactive(int objectId) = 0;
    virtual bool campaignHouseMatches(const std::string& authoredHouse, const std::string& objectHouse) = 0;
    virtual bool applyCampaignAction(RuntimeTriggerDefinition& trigger, const RuntimeTriggerAction& action) = 0;
    virtual void snapshotCampaignObservations() = 0;
    virtual bool advanceReinforcements() = 0;
    virtual bool advanceTeamMissions() = 0;
    virtual bool advanceCampaignAiProduction() = 0;
    virtual bool fireSuperWeapon(const RuntimeTriggerDefinition& trigger, const RuntimeTriggerAction& action) = 0;
    virtual bool fireCreateTeam(RuntimeTriggerDefinition& trigger, const RuntimeTriggerAction& action) = 0;
    virtual const RuntimeTeamType* chooseTeam() = 0;
    virtual bool cooperativeActive() = 0;
};

bool runtimeIsCreateTeamAction(int actionType);
bool runtimeIsSuperWeaponTriggerAction(int actionType);
bool advanceRuntimeCampaignTriggers(RuntimeTriggerContext& state, RuntimeTriggerOperations& operations);
bool advanceRuntimeTriggers(RuntimeTriggerContext& state, RuntimeTriggerOperations& operations);
