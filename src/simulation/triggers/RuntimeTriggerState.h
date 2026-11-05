#pragma once
#include "world/values/RuntimeCellPoint.h"
#include "simulation/ai/RuntimeComputerTactics.h"
#include "simulation/triggers/RuntimeCampaignAiTriggerDefinition.h"
#include "simulation/triggers/RuntimeTriggerReinforcements.h"
#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Original trigger domain values. Field order, defaults and container types
// are shared by production simulation, authority hashing and persistence.
struct RuntimeScriptStep {
    int action = -1;
    int argument = 0;
};

struct RuntimeScriptType {
    std::string id;
    std::string name;
    std::vector<RuntimeScriptStep> steps;
};

struct RuntimeTaskForceMember {
    int count = 0;
    std::string unitId;
};

struct RuntimeTaskForce {
    std::string id;
    std::string name;
    int group = -1;
    std::vector<RuntimeTaskForceMember> members;
};

struct RuntimeTriggerEvent {
    int type = 0;
    int authoredOrdinal = -1;
    int lightingEventId = -1;
    std::vector<std::string> arguments;
};

struct RuntimeTeamType {
    std::string id;
    std::string name;
    std::string house;
    std::string waypointId;
    std::string scriptId;
    std::string taskForceId;
    int priority = 7;
    int max = -1;
    int group = -1;
    bool autocreate = false;
    bool prebuild = false;
    bool reinforce = false;
    bool full = false;
    bool aggressive = false;
    bool isBaseDefense = false;
    bool recruitable = true;
    bool recruiter = false;
    bool looseRecruit = false;
    bool avoidThreats = false;
    bool suicide = false;
    bool guardSlower = false;
    bool onlyTargetHouseEnemy = false;
    bool transportsReturnOnUnload = false;
    int veteranLevel = 1;
    int mindControlDecision = 0;
};

struct RuntimeCampaignHouseProductionState {
    std::string owner;
    std::string country;
    int techLevel = 10;
    int iq = 0;
    int side = -1;
    int credits = 0;
    int nextAiTriggerCheckTick = 0;
    int selectionSerial = 0;
    int activeBuildId = -1;
    // TAction 35 uses the classic TS quarry domain (0..9) to steer this
    // house's automatic special-weapon target selection.
    int preferredTargetQuarry = 0;
    // TActions 76..79 author the percentage of AI output reserved for
    // trigger teams globally and per production category.
    int aiTriggerTeamRatio = 100;
    int teamAircraftRatio = 100;
    int teamInfantryRatio = 100;
    int teamUnitRatio = 100;
    bool playerControlled = false;
    bool productionStarted = false;
    bool aiTriggersEnabled = false;
};

struct RuntimeCampaignTeamBuild {
    int instanceId = 0;
    std::string aiTriggerId;
    std::string teamTypeId;
    std::string owner;
    std::size_t memberIndex = 0;
    int memberProduced = 0;
    int remainingTicks = 0;
    int paidCost = 0;
    std::string activeTypeId;
    std::vector<int> objectIds;
    int deadlineTick = 0;
    bool completed = false;
    bool failed = false;
    std::string status;
};

struct RuntimeTeamMissionState {
    int instanceId = 0;
    std::string teamTypeId;
    std::string scriptId;
    std::vector<int> objectIds;
    std::size_t stepIndex = 0;
    int stepStartedTick = -1;
    bool stepIssued = false;
    int targetObjectId = -1;
    RuntimeCellPoint targetCell{-1, -1};
    bool completed = false;
    std::string sourceAiTriggerId;
    int sourceAiBuildId = -1;
    bool aiOutcomeReported = false;
    bool achievedGreatSuccess = false;
    bool targetInteractionCompleted = false;
    std::map<int, RuntimeCellPoint> transportReturnCells;
    bool regrouping = false;
    RuntimeCellPoint regroupCell{-1, -1};
    int regroupStartedTick = -1;
    std::map<int, RuntimeCellPoint> regroupDestinations;
    std::string status;
};

struct RuntimeTriggerAction {
    int type = 0;
    int authoredOrdinal = -1;
    std::string targetId;
    std::vector<std::string> arguments;
};

struct RuntimeTriggerDefinition {
    std::string id;
    std::string house;
    std::string linkedTriggerId;
    std::string name;
    bool fired = false;
    bool enabled = true;
    bool repeating = false;
    std::array<bool, 3> difficultyEnabled{{true, true, true}};
    int activationTick = 0;
    int lastFiredTick = -1;
    int nextEligibleTick = 0;
    // A tag is attached to the root trigger in the map, and every trigger in
    // its AttachedTrigger chain shares that tag.  The repeat policy belongs
    // to the tag (0=once for any target, 1=once after all targets, 2=repeat),
    // not to the final field of [Triggers].
    int tagRepeatType = 0;
    std::unordered_set<std::string> tagIds;
    std::unordered_set<int> remainingOnceAllObjectIds;
    std::vector<RuntimeTriggerEvent> events;
    std::vector<RuntimeTriggerAction> actions;
};

struct RuntimeCampaignRadarEvent {
    RuntimeCellPoint cell;
    int eventType = 0;
    int untilTick = 0;
};

struct RuntimePublicBuildingLocation {
    std::string owner;
    RuntimeCellPoint cell;
    int noticeTick = -1;
};

struct RuntimeTriggerState {
    std::map<int, RuntimePublicBuildingLocation> publicBuildingLocations;
    bool loaded = false;
    bool truckCrate = false;
    bool trainCrate = false;
    bool bootstrapComplete = false;
    int triggerCount = 0;
    int eventCount = 0;
    int actionCount = 0;
    int tagCount = 0;
    int teamTypeCount = 0;
    int taskForceCount = 0;
    int scriptTypeCount = 0;
    int createTeamActionCount = 0;
    int firedTriggers = 0;
    int teamsCreated = 0;
    int unitsCreated = 0;
    int ordersIssued = 0;
    int scriptStepsIssued = 0;
    int scriptStepsCompleted = 0;
    int unsupportedScriptSteps = 0;
    int unsupportedActions = 0;
    int unsupportedEvents = 0;
    bool allowLegacyBootstrapForSmokeTest = false;
    bool collectSmokeTimings = false;
    double smokeCreateTeamMs = 0.0;
    double smokeIssueOrderMs = 0.0;
    std::string status = "triggers not loaded";
    std::vector<RuntimeTriggerDefinition> triggers;
    std::map<std::string, RuntimeTeamType> teams;
    std::map<std::string, RuntimeTaskForce> taskForces;
    std::map<std::string, RuntimeScriptType> scripts;
    std::vector<std::string> teamIds;
    std::vector<std::string> scriptIds;
    std::map<int, RuntimeTeamMissionState> teamMissions;
    RuntimeTriggerReinforcements reinforcements;
    int nextTeamMissionInstanceId = 1;
    std::unordered_map<int, std::string> campaignHouseByIndex;
    std::unordered_map<int, std::string> campaignCountryByIndex;
    std::unordered_map<std::string, std::string>
        campaignCanonicalHouseByAlias;
    std::unordered_map<std::string, std::string> campaignCountryByHouse;
    std::map<std::string, RuntimeCampaignHouseProductionState>
        campaignHouseProduction;
    std::vector<RuntimeCampaignAiTriggerDefinition> campaignAiTriggers;
    std::vector<RuntimeCampaignAiTriggerDefinition> computerAiTriggers;
    std::map<std::string, RuntimeComputerTacticsState> computerTacticsByOwner;
    std::map<int, RuntimeCampaignTeamBuild> campaignTeamBuilds;
    int nextCampaignTeamBuildId = 1;
    int campaignAiTriggerSelections = 0;
    int campaignAiTriggerTeamsCompleted = 0;
    int campaignAiTriggerTeamsFailed = 0;
    std::set<std::string> campaignAlliedPairs;
    std::unordered_map<int, int> campaignLocalVariables;
    std::unordered_map<int, int> campaignGlobalVariables;
    std::unordered_map<int, std::string> campaignCellTagByCell;
    std::unordered_map<int, int> campaignPreviousCellByObject;
    std::unordered_map<int, int> campaignPreviousHealthByObject;
    std::unordered_map<int, int> campaignInitialHealthByObject;
    std::unordered_map<int, bool> campaignPreviousAliveByObject;
    std::unordered_map<int, std::string> campaignInitialOwnerByObject;
    std::unordered_map<int, std::string> campaignPreviousOwnerByObject;
    std::unordered_set<int> campaignPreviouslyKnownObjects;
    std::unordered_set<int> campaignPreviouslyDiscoveredObjects;
    std::unordered_set<int> campaignPreviousSelectedObjects;
    std::unordered_map<int, int> campaignAttackedTickByObject;
    std::unordered_map<int, std::string> campaignAttackerHouseByObject;
    std::unordered_map<int, int> campaignDestroyedTickByObject;
    std::unordered_map<int, std::string> campaignDestroyerHouseByObject;
    std::unordered_map<int, int> campaignEnteredTickByTargetObject;
    std::unordered_map<int, std::string> campaignEntrantHouseByTargetObject;
    std::unordered_map<int, int> campaignSpiedTickByTargetObject;
    std::unordered_map<int, std::string> campaignSpyHouseByTargetObject;
    std::unordered_map<int, int> campaignBridgeDestroyedTickByCell;
    std::unordered_map<int, std::string> campaignMoviePathByIndex;
    std::unordered_map<std::string, std::unordered_set<int>>
        campaignTeamObjects;
    bool campaignMissionTimerStarted = false;
    bool campaignMissionTimerRunning = false;
    int campaignMissionTimerRemainingTicks = 0;
    std::string campaignMissionTimerLabel;
    bool campaignInputLocked = false;
    int campaignForceDepth = 0;
    int campaignRadarBlackoutUntilTick = 0;
    int campaignChronoScreenEffectUntilTick = 0;
    int campaignIonStormUntilTick = 0;
    int campaignNextIonStormStrikeTick = 0;
    std::vector<RuntimeCampaignRadarEvent> campaignRadarEvents;
    std::unordered_map<std::string, int> campaignTechLevelByType;
    std::unordered_map<std::string, RuntimeCellPoint>
        campaignBaseCenterByOwner;
    std::unordered_map<std::string, RuntimeCellPoint>
        campaignPreferredSuperWeaponTargetByOwner;
    mutable std::map<std::string, bool> buildableTaskForceCache;
};
