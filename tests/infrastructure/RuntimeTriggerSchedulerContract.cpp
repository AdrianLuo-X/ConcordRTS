#include "simulation/triggers/RuntimeTriggerScheduler.h"
#include "core/determinism/RuntimeAuthorityHashPrimitives.h"
#include "runtime/performance/RuntimePerfTrace.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS) || defined(_WINSOCKAPI_)
#error H035 standard contract included a native Windows surface
#endif

using runtime_authority_hash::runtimeStableHashString;
using runtime_authority_hash::toUpperAsciiLocal;
using runtime_save_text::trimAsciiLocal;

// Frozen original declarations prove layout/default continuity separately
// from the phase differential, which shares the current production values.
namespace original_values {
struct RuntimeCellPoint {
    int x = -1;
    int y = -1;
};

struct RuntimeCellPointLess {
    bool operator()(const RuntimeCellPoint& left, const RuntimeCellPoint& right) const {
        if (left.y != right.y) {
            return left.y < right.y;
        }
        return left.x < right.x;
    }
};

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
}

static int checks = 0, failures = 0;
void expect(bool yes, const char* label) {
    ++checks;
    if (!yes) { ++failures; std::cerr << "H035 contract failed: " << label << '\n'; }
}
template<class T, class M> std::ptrdiff_t memberOffset(T& value, M T::* member) {
    return reinterpret_cast<const char*>(&(value.*member)) - reinterpret_cast<const char*>(&value);
}
void checkOriginalLayouts() {
    { RuntimeCellPoint current; original_values::RuntimeCellPoint original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeCellPoint size/alignment");
      expect(memberOffset(current, &RuntimeCellPoint::x)==memberOffset(original, &original_values::RuntimeCellPoint::x), "RuntimeCellPoint.x offset");
      expect(memberOffset(current, &RuntimeCellPoint::y)==memberOffset(original, &original_values::RuntimeCellPoint::y), "RuntimeCellPoint.y offset");
    }
    { RuntimeCellPointLess current; original_values::RuntimeCellPointLess original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeCellPointLess size/alignment");
      expect(current(RuntimeCellPoint{9,1},RuntimeCellPoint{1,2}) == original(original_values::RuntimeCellPoint{9,1},original_values::RuntimeCellPoint{1,2}), "cell order remains y then x");
    }
    { RuntimeScriptStep current; original_values::RuntimeScriptStep original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeScriptStep size/alignment");
      expect(memberOffset(current, &RuntimeScriptStep::action)==memberOffset(original, &original_values::RuntimeScriptStep::action), "RuntimeScriptStep.action offset");
      expect(memberOffset(current, &RuntimeScriptStep::argument)==memberOffset(original, &original_values::RuntimeScriptStep::argument), "RuntimeScriptStep.argument offset");
    }
    { RuntimeScriptType current; original_values::RuntimeScriptType original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeScriptType size/alignment");
      expect(memberOffset(current, &RuntimeScriptType::id)==memberOffset(original, &original_values::RuntimeScriptType::id), "RuntimeScriptType.id offset");
      expect(memberOffset(current, &RuntimeScriptType::name)==memberOffset(original, &original_values::RuntimeScriptType::name), "RuntimeScriptType.name offset");
      expect(memberOffset(current, &RuntimeScriptType::steps)==memberOffset(original, &original_values::RuntimeScriptType::steps), "RuntimeScriptType.steps offset");
    }
    { RuntimeTaskForceMember current; original_values::RuntimeTaskForceMember original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeTaskForceMember size/alignment");
      expect(memberOffset(current, &RuntimeTaskForceMember::count)==memberOffset(original, &original_values::RuntimeTaskForceMember::count), "RuntimeTaskForceMember.count offset");
      expect(memberOffset(current, &RuntimeTaskForceMember::unitId)==memberOffset(original, &original_values::RuntimeTaskForceMember::unitId), "RuntimeTaskForceMember.unitId offset");
    }
    { RuntimeTaskForce current; original_values::RuntimeTaskForce original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeTaskForce size/alignment");
      expect(memberOffset(current, &RuntimeTaskForce::id)==memberOffset(original, &original_values::RuntimeTaskForce::id), "RuntimeTaskForce.id offset");
      expect(memberOffset(current, &RuntimeTaskForce::name)==memberOffset(original, &original_values::RuntimeTaskForce::name), "RuntimeTaskForce.name offset");
      expect(memberOffset(current, &RuntimeTaskForce::group)==memberOffset(original, &original_values::RuntimeTaskForce::group), "RuntimeTaskForce.group offset");
      expect(memberOffset(current, &RuntimeTaskForce::members)==memberOffset(original, &original_values::RuntimeTaskForce::members), "RuntimeTaskForce.members offset");
    }
    { RuntimeTriggerEvent current; original_values::RuntimeTriggerEvent original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeTriggerEvent size/alignment");
      expect(memberOffset(current, &RuntimeTriggerEvent::type)==memberOffset(original, &original_values::RuntimeTriggerEvent::type), "RuntimeTriggerEvent.type offset");
      expect(memberOffset(current, &RuntimeTriggerEvent::authoredOrdinal)==memberOffset(original, &original_values::RuntimeTriggerEvent::authoredOrdinal), "RuntimeTriggerEvent.authoredOrdinal offset");
      expect(memberOffset(current, &RuntimeTriggerEvent::lightingEventId)==memberOffset(original, &original_values::RuntimeTriggerEvent::lightingEventId), "RuntimeTriggerEvent.lightingEventId offset");
      expect(memberOffset(current, &RuntimeTriggerEvent::arguments)==memberOffset(original, &original_values::RuntimeTriggerEvent::arguments), "RuntimeTriggerEvent.arguments offset");
    }
    { RuntimeTeamType current; original_values::RuntimeTeamType original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeTeamType size/alignment");
      expect(memberOffset(current, &RuntimeTeamType::id)==memberOffset(original, &original_values::RuntimeTeamType::id), "RuntimeTeamType.id offset");
      expect(memberOffset(current, &RuntimeTeamType::name)==memberOffset(original, &original_values::RuntimeTeamType::name), "RuntimeTeamType.name offset");
      expect(memberOffset(current, &RuntimeTeamType::house)==memberOffset(original, &original_values::RuntimeTeamType::house), "RuntimeTeamType.house offset");
      expect(memberOffset(current, &RuntimeTeamType::waypointId)==memberOffset(original, &original_values::RuntimeTeamType::waypointId), "RuntimeTeamType.waypointId offset");
      expect(memberOffset(current, &RuntimeTeamType::scriptId)==memberOffset(original, &original_values::RuntimeTeamType::scriptId), "RuntimeTeamType.scriptId offset");
      expect(memberOffset(current, &RuntimeTeamType::taskForceId)==memberOffset(original, &original_values::RuntimeTeamType::taskForceId), "RuntimeTeamType.taskForceId offset");
      expect(memberOffset(current, &RuntimeTeamType::priority)==memberOffset(original, &original_values::RuntimeTeamType::priority), "RuntimeTeamType.priority offset");
      expect(memberOffset(current, &RuntimeTeamType::max)==memberOffset(original, &original_values::RuntimeTeamType::max), "RuntimeTeamType.max offset");
      expect(memberOffset(current, &RuntimeTeamType::group)==memberOffset(original, &original_values::RuntimeTeamType::group), "RuntimeTeamType.group offset");
      expect(memberOffset(current, &RuntimeTeamType::autocreate)==memberOffset(original, &original_values::RuntimeTeamType::autocreate), "RuntimeTeamType.autocreate offset");
      expect(memberOffset(current, &RuntimeTeamType::prebuild)==memberOffset(original, &original_values::RuntimeTeamType::prebuild), "RuntimeTeamType.prebuild offset");
      expect(memberOffset(current, &RuntimeTeamType::reinforce)==memberOffset(original, &original_values::RuntimeTeamType::reinforce), "RuntimeTeamType.reinforce offset");
      expect(memberOffset(current, &RuntimeTeamType::full)==memberOffset(original, &original_values::RuntimeTeamType::full), "RuntimeTeamType.full offset");
      expect(memberOffset(current, &RuntimeTeamType::aggressive)==memberOffset(original, &original_values::RuntimeTeamType::aggressive), "RuntimeTeamType.aggressive offset");
      expect(memberOffset(current, &RuntimeTeamType::isBaseDefense)==memberOffset(original, &original_values::RuntimeTeamType::isBaseDefense), "RuntimeTeamType.isBaseDefense offset");
      expect(memberOffset(current, &RuntimeTeamType::recruitable)==memberOffset(original, &original_values::RuntimeTeamType::recruitable), "RuntimeTeamType.recruitable offset");
      expect(memberOffset(current, &RuntimeTeamType::recruiter)==memberOffset(original, &original_values::RuntimeTeamType::recruiter), "RuntimeTeamType.recruiter offset");
      expect(memberOffset(current, &RuntimeTeamType::looseRecruit)==memberOffset(original, &original_values::RuntimeTeamType::looseRecruit), "RuntimeTeamType.looseRecruit offset");
      expect(memberOffset(current, &RuntimeTeamType::avoidThreats)==memberOffset(original, &original_values::RuntimeTeamType::avoidThreats), "RuntimeTeamType.avoidThreats offset");
      expect(memberOffset(current, &RuntimeTeamType::suicide)==memberOffset(original, &original_values::RuntimeTeamType::suicide), "RuntimeTeamType.suicide offset");
      expect(memberOffset(current, &RuntimeTeamType::guardSlower)==memberOffset(original, &original_values::RuntimeTeamType::guardSlower), "RuntimeTeamType.guardSlower offset");
      expect(memberOffset(current, &RuntimeTeamType::onlyTargetHouseEnemy)==memberOffset(original, &original_values::RuntimeTeamType::onlyTargetHouseEnemy), "RuntimeTeamType.onlyTargetHouseEnemy offset");
      expect(memberOffset(current, &RuntimeTeamType::transportsReturnOnUnload)==memberOffset(original, &original_values::RuntimeTeamType::transportsReturnOnUnload), "RuntimeTeamType.transportsReturnOnUnload offset");
      expect(memberOffset(current, &RuntimeTeamType::veteranLevel)==memberOffset(original, &original_values::RuntimeTeamType::veteranLevel), "RuntimeTeamType.veteranLevel offset");
      expect(memberOffset(current, &RuntimeTeamType::mindControlDecision)==memberOffset(original, &original_values::RuntimeTeamType::mindControlDecision), "RuntimeTeamType.mindControlDecision offset");
    }
    { RuntimeCampaignHouseProductionState current; original_values::RuntimeCampaignHouseProductionState original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeCampaignHouseProductionState size/alignment");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::owner)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::owner), "RuntimeCampaignHouseProductionState.owner offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::country)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::country), "RuntimeCampaignHouseProductionState.country offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::techLevel)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::techLevel), "RuntimeCampaignHouseProductionState.techLevel offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::iq)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::iq), "RuntimeCampaignHouseProductionState.iq offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::side)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::side), "RuntimeCampaignHouseProductionState.side offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::credits)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::credits), "RuntimeCampaignHouseProductionState.credits offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::nextAiTriggerCheckTick)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::nextAiTriggerCheckTick), "RuntimeCampaignHouseProductionState.nextAiTriggerCheckTick offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::selectionSerial)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::selectionSerial), "RuntimeCampaignHouseProductionState.selectionSerial offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::activeBuildId)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::activeBuildId), "RuntimeCampaignHouseProductionState.activeBuildId offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::preferredTargetQuarry)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::preferredTargetQuarry), "RuntimeCampaignHouseProductionState.preferredTargetQuarry offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::aiTriggerTeamRatio)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::aiTriggerTeamRatio), "RuntimeCampaignHouseProductionState.aiTriggerTeamRatio offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::teamAircraftRatio)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::teamAircraftRatio), "RuntimeCampaignHouseProductionState.teamAircraftRatio offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::teamInfantryRatio)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::teamInfantryRatio), "RuntimeCampaignHouseProductionState.teamInfantryRatio offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::teamUnitRatio)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::teamUnitRatio), "RuntimeCampaignHouseProductionState.teamUnitRatio offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::playerControlled)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::playerControlled), "RuntimeCampaignHouseProductionState.playerControlled offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::productionStarted)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::productionStarted), "RuntimeCampaignHouseProductionState.productionStarted offset");
      expect(memberOffset(current, &RuntimeCampaignHouseProductionState::aiTriggersEnabled)==memberOffset(original, &original_values::RuntimeCampaignHouseProductionState::aiTriggersEnabled), "RuntimeCampaignHouseProductionState.aiTriggersEnabled offset");
    }
    { RuntimeCampaignTeamBuild current; original_values::RuntimeCampaignTeamBuild original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeCampaignTeamBuild size/alignment");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::instanceId)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::instanceId), "RuntimeCampaignTeamBuild.instanceId offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::aiTriggerId)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::aiTriggerId), "RuntimeCampaignTeamBuild.aiTriggerId offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::teamTypeId)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::teamTypeId), "RuntimeCampaignTeamBuild.teamTypeId offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::owner)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::owner), "RuntimeCampaignTeamBuild.owner offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::memberIndex)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::memberIndex), "RuntimeCampaignTeamBuild.memberIndex offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::memberProduced)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::memberProduced), "RuntimeCampaignTeamBuild.memberProduced offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::remainingTicks)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::remainingTicks), "RuntimeCampaignTeamBuild.remainingTicks offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::paidCost)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::paidCost), "RuntimeCampaignTeamBuild.paidCost offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::activeTypeId)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::activeTypeId), "RuntimeCampaignTeamBuild.activeTypeId offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::objectIds)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::objectIds), "RuntimeCampaignTeamBuild.objectIds offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::deadlineTick)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::deadlineTick), "RuntimeCampaignTeamBuild.deadlineTick offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::completed)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::completed), "RuntimeCampaignTeamBuild.completed offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::failed)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::failed), "RuntimeCampaignTeamBuild.failed offset");
      expect(memberOffset(current, &RuntimeCampaignTeamBuild::status)==memberOffset(original, &original_values::RuntimeCampaignTeamBuild::status), "RuntimeCampaignTeamBuild.status offset");
    }
    { RuntimeTeamMissionState current; original_values::RuntimeTeamMissionState original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeTeamMissionState size/alignment");
      expect(memberOffset(current, &RuntimeTeamMissionState::instanceId)==memberOffset(original, &original_values::RuntimeTeamMissionState::instanceId), "RuntimeTeamMissionState.instanceId offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::teamTypeId)==memberOffset(original, &original_values::RuntimeTeamMissionState::teamTypeId), "RuntimeTeamMissionState.teamTypeId offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::scriptId)==memberOffset(original, &original_values::RuntimeTeamMissionState::scriptId), "RuntimeTeamMissionState.scriptId offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::objectIds)==memberOffset(original, &original_values::RuntimeTeamMissionState::objectIds), "RuntimeTeamMissionState.objectIds offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::stepIndex)==memberOffset(original, &original_values::RuntimeTeamMissionState::stepIndex), "RuntimeTeamMissionState.stepIndex offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::stepStartedTick)==memberOffset(original, &original_values::RuntimeTeamMissionState::stepStartedTick), "RuntimeTeamMissionState.stepStartedTick offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::stepIssued)==memberOffset(original, &original_values::RuntimeTeamMissionState::stepIssued), "RuntimeTeamMissionState.stepIssued offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::targetObjectId)==memberOffset(original, &original_values::RuntimeTeamMissionState::targetObjectId), "RuntimeTeamMissionState.targetObjectId offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::targetCell)==memberOffset(original, &original_values::RuntimeTeamMissionState::targetCell), "RuntimeTeamMissionState.targetCell offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::completed)==memberOffset(original, &original_values::RuntimeTeamMissionState::completed), "RuntimeTeamMissionState.completed offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::sourceAiTriggerId)==memberOffset(original, &original_values::RuntimeTeamMissionState::sourceAiTriggerId), "RuntimeTeamMissionState.sourceAiTriggerId offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::sourceAiBuildId)==memberOffset(original, &original_values::RuntimeTeamMissionState::sourceAiBuildId), "RuntimeTeamMissionState.sourceAiBuildId offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::aiOutcomeReported)==memberOffset(original, &original_values::RuntimeTeamMissionState::aiOutcomeReported), "RuntimeTeamMissionState.aiOutcomeReported offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::achievedGreatSuccess)==memberOffset(original, &original_values::RuntimeTeamMissionState::achievedGreatSuccess), "RuntimeTeamMissionState.achievedGreatSuccess offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::targetInteractionCompleted)==memberOffset(original, &original_values::RuntimeTeamMissionState::targetInteractionCompleted), "RuntimeTeamMissionState.targetInteractionCompleted offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::transportReturnCells)==memberOffset(original, &original_values::RuntimeTeamMissionState::transportReturnCells), "RuntimeTeamMissionState.transportReturnCells offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::regrouping)==memberOffset(original, &original_values::RuntimeTeamMissionState::regrouping), "RuntimeTeamMissionState.regrouping offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::regroupCell)==memberOffset(original, &original_values::RuntimeTeamMissionState::regroupCell), "RuntimeTeamMissionState.regroupCell offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::regroupStartedTick)==memberOffset(original, &original_values::RuntimeTeamMissionState::regroupStartedTick), "RuntimeTeamMissionState.regroupStartedTick offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::regroupDestinations)==memberOffset(original, &original_values::RuntimeTeamMissionState::regroupDestinations), "RuntimeTeamMissionState.regroupDestinations offset");
      expect(memberOffset(current, &RuntimeTeamMissionState::status)==memberOffset(original, &original_values::RuntimeTeamMissionState::status), "RuntimeTeamMissionState.status offset");
    }
    { RuntimeTriggerAction current; original_values::RuntimeTriggerAction original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeTriggerAction size/alignment");
      expect(memberOffset(current, &RuntimeTriggerAction::type)==memberOffset(original, &original_values::RuntimeTriggerAction::type), "RuntimeTriggerAction.type offset");
      expect(memberOffset(current, &RuntimeTriggerAction::authoredOrdinal)==memberOffset(original, &original_values::RuntimeTriggerAction::authoredOrdinal), "RuntimeTriggerAction.authoredOrdinal offset");
      expect(memberOffset(current, &RuntimeTriggerAction::targetId)==memberOffset(original, &original_values::RuntimeTriggerAction::targetId), "RuntimeTriggerAction.targetId offset");
      expect(memberOffset(current, &RuntimeTriggerAction::arguments)==memberOffset(original, &original_values::RuntimeTriggerAction::arguments), "RuntimeTriggerAction.arguments offset");
    }
    { RuntimeTriggerDefinition current; original_values::RuntimeTriggerDefinition original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeTriggerDefinition size/alignment");
      expect(memberOffset(current, &RuntimeTriggerDefinition::id)==memberOffset(original, &original_values::RuntimeTriggerDefinition::id), "RuntimeTriggerDefinition.id offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::house)==memberOffset(original, &original_values::RuntimeTriggerDefinition::house), "RuntimeTriggerDefinition.house offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::linkedTriggerId)==memberOffset(original, &original_values::RuntimeTriggerDefinition::linkedTriggerId), "RuntimeTriggerDefinition.linkedTriggerId offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::name)==memberOffset(original, &original_values::RuntimeTriggerDefinition::name), "RuntimeTriggerDefinition.name offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::fired)==memberOffset(original, &original_values::RuntimeTriggerDefinition::fired), "RuntimeTriggerDefinition.fired offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::enabled)==memberOffset(original, &original_values::RuntimeTriggerDefinition::enabled), "RuntimeTriggerDefinition.enabled offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::repeating)==memberOffset(original, &original_values::RuntimeTriggerDefinition::repeating), "RuntimeTriggerDefinition.repeating offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::difficultyEnabled)==memberOffset(original, &original_values::RuntimeTriggerDefinition::difficultyEnabled), "RuntimeTriggerDefinition.difficultyEnabled offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::activationTick)==memberOffset(original, &original_values::RuntimeTriggerDefinition::activationTick), "RuntimeTriggerDefinition.activationTick offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::lastFiredTick)==memberOffset(original, &original_values::RuntimeTriggerDefinition::lastFiredTick), "RuntimeTriggerDefinition.lastFiredTick offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::nextEligibleTick)==memberOffset(original, &original_values::RuntimeTriggerDefinition::nextEligibleTick), "RuntimeTriggerDefinition.nextEligibleTick offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::tagRepeatType)==memberOffset(original, &original_values::RuntimeTriggerDefinition::tagRepeatType), "RuntimeTriggerDefinition.tagRepeatType offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::tagIds)==memberOffset(original, &original_values::RuntimeTriggerDefinition::tagIds), "RuntimeTriggerDefinition.tagIds offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::remainingOnceAllObjectIds)==memberOffset(original, &original_values::RuntimeTriggerDefinition::remainingOnceAllObjectIds), "RuntimeTriggerDefinition.remainingOnceAllObjectIds offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::events)==memberOffset(original, &original_values::RuntimeTriggerDefinition::events), "RuntimeTriggerDefinition.events offset");
      expect(memberOffset(current, &RuntimeTriggerDefinition::actions)==memberOffset(original, &original_values::RuntimeTriggerDefinition::actions), "RuntimeTriggerDefinition.actions offset");
    }
    { RuntimeCampaignRadarEvent current; original_values::RuntimeCampaignRadarEvent original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeCampaignRadarEvent size/alignment");
      expect(memberOffset(current, &RuntimeCampaignRadarEvent::cell)==memberOffset(original, &original_values::RuntimeCampaignRadarEvent::cell), "RuntimeCampaignRadarEvent.cell offset");
      expect(memberOffset(current, &RuntimeCampaignRadarEvent::eventType)==memberOffset(original, &original_values::RuntimeCampaignRadarEvent::eventType), "RuntimeCampaignRadarEvent.eventType offset");
      expect(memberOffset(current, &RuntimeCampaignRadarEvent::untilTick)==memberOffset(original, &original_values::RuntimeCampaignRadarEvent::untilTick), "RuntimeCampaignRadarEvent.untilTick offset");
    }
    { RuntimePublicBuildingLocation current; original_values::RuntimePublicBuildingLocation original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimePublicBuildingLocation size/alignment");
      expect(memberOffset(current, &RuntimePublicBuildingLocation::owner)==memberOffset(original, &original_values::RuntimePublicBuildingLocation::owner), "RuntimePublicBuildingLocation.owner offset");
      expect(memberOffset(current, &RuntimePublicBuildingLocation::cell)==memberOffset(original, &original_values::RuntimePublicBuildingLocation::cell), "RuntimePublicBuildingLocation.cell offset");
      expect(memberOffset(current, &RuntimePublicBuildingLocation::noticeTick)==memberOffset(original, &original_values::RuntimePublicBuildingLocation::noticeTick), "RuntimePublicBuildingLocation.noticeTick offset");
    }
    { RuntimeTriggerState current; original_values::RuntimeTriggerState original;
      expect(sizeof(current)==sizeof(original) && alignof(decltype(current))==alignof(decltype(original)), "RuntimeTriggerState size/alignment");
      expect(memberOffset(current, &RuntimeTriggerState::publicBuildingLocations)==memberOffset(original, &original_values::RuntimeTriggerState::publicBuildingLocations), "RuntimeTriggerState.publicBuildingLocations offset");
      expect(memberOffset(current, &RuntimeTriggerState::loaded)==memberOffset(original, &original_values::RuntimeTriggerState::loaded), "RuntimeTriggerState.loaded offset");
      expect(memberOffset(current, &RuntimeTriggerState::truckCrate)==memberOffset(original, &original_values::RuntimeTriggerState::truckCrate), "RuntimeTriggerState.truckCrate offset");
      expect(memberOffset(current, &RuntimeTriggerState::trainCrate)==memberOffset(original, &original_values::RuntimeTriggerState::trainCrate), "RuntimeTriggerState.trainCrate offset");
      expect(memberOffset(current, &RuntimeTriggerState::bootstrapComplete)==memberOffset(original, &original_values::RuntimeTriggerState::bootstrapComplete), "RuntimeTriggerState.bootstrapComplete offset");
      expect(memberOffset(current, &RuntimeTriggerState::triggerCount)==memberOffset(original, &original_values::RuntimeTriggerState::triggerCount), "RuntimeTriggerState.triggerCount offset");
      expect(memberOffset(current, &RuntimeTriggerState::eventCount)==memberOffset(original, &original_values::RuntimeTriggerState::eventCount), "RuntimeTriggerState.eventCount offset");
      expect(memberOffset(current, &RuntimeTriggerState::actionCount)==memberOffset(original, &original_values::RuntimeTriggerState::actionCount), "RuntimeTriggerState.actionCount offset");
      expect(memberOffset(current, &RuntimeTriggerState::tagCount)==memberOffset(original, &original_values::RuntimeTriggerState::tagCount), "RuntimeTriggerState.tagCount offset");
      expect(memberOffset(current, &RuntimeTriggerState::teamTypeCount)==memberOffset(original, &original_values::RuntimeTriggerState::teamTypeCount), "RuntimeTriggerState.teamTypeCount offset");
      expect(memberOffset(current, &RuntimeTriggerState::taskForceCount)==memberOffset(original, &original_values::RuntimeTriggerState::taskForceCount), "RuntimeTriggerState.taskForceCount offset");
      expect(memberOffset(current, &RuntimeTriggerState::scriptTypeCount)==memberOffset(original, &original_values::RuntimeTriggerState::scriptTypeCount), "RuntimeTriggerState.scriptTypeCount offset");
      expect(memberOffset(current, &RuntimeTriggerState::createTeamActionCount)==memberOffset(original, &original_values::RuntimeTriggerState::createTeamActionCount), "RuntimeTriggerState.createTeamActionCount offset");
      expect(memberOffset(current, &RuntimeTriggerState::firedTriggers)==memberOffset(original, &original_values::RuntimeTriggerState::firedTriggers), "RuntimeTriggerState.firedTriggers offset");
      expect(memberOffset(current, &RuntimeTriggerState::teamsCreated)==memberOffset(original, &original_values::RuntimeTriggerState::teamsCreated), "RuntimeTriggerState.teamsCreated offset");
      expect(memberOffset(current, &RuntimeTriggerState::unitsCreated)==memberOffset(original, &original_values::RuntimeTriggerState::unitsCreated), "RuntimeTriggerState.unitsCreated offset");
      expect(memberOffset(current, &RuntimeTriggerState::ordersIssued)==memberOffset(original, &original_values::RuntimeTriggerState::ordersIssued), "RuntimeTriggerState.ordersIssued offset");
      expect(memberOffset(current, &RuntimeTriggerState::scriptStepsIssued)==memberOffset(original, &original_values::RuntimeTriggerState::scriptStepsIssued), "RuntimeTriggerState.scriptStepsIssued offset");
      expect(memberOffset(current, &RuntimeTriggerState::scriptStepsCompleted)==memberOffset(original, &original_values::RuntimeTriggerState::scriptStepsCompleted), "RuntimeTriggerState.scriptStepsCompleted offset");
      expect(memberOffset(current, &RuntimeTriggerState::unsupportedScriptSteps)==memberOffset(original, &original_values::RuntimeTriggerState::unsupportedScriptSteps), "RuntimeTriggerState.unsupportedScriptSteps offset");
      expect(memberOffset(current, &RuntimeTriggerState::unsupportedActions)==memberOffset(original, &original_values::RuntimeTriggerState::unsupportedActions), "RuntimeTriggerState.unsupportedActions offset");
      expect(memberOffset(current, &RuntimeTriggerState::unsupportedEvents)==memberOffset(original, &original_values::RuntimeTriggerState::unsupportedEvents), "RuntimeTriggerState.unsupportedEvents offset");
      expect(memberOffset(current, &RuntimeTriggerState::allowLegacyBootstrapForSmokeTest)==memberOffset(original, &original_values::RuntimeTriggerState::allowLegacyBootstrapForSmokeTest), "RuntimeTriggerState.allowLegacyBootstrapForSmokeTest offset");
      expect(memberOffset(current, &RuntimeTriggerState::collectSmokeTimings)==memberOffset(original, &original_values::RuntimeTriggerState::collectSmokeTimings), "RuntimeTriggerState.collectSmokeTimings offset");
      expect(memberOffset(current, &RuntimeTriggerState::smokeCreateTeamMs)==memberOffset(original, &original_values::RuntimeTriggerState::smokeCreateTeamMs), "RuntimeTriggerState.smokeCreateTeamMs offset");
      expect(memberOffset(current, &RuntimeTriggerState::smokeIssueOrderMs)==memberOffset(original, &original_values::RuntimeTriggerState::smokeIssueOrderMs), "RuntimeTriggerState.smokeIssueOrderMs offset");
      expect(memberOffset(current, &RuntimeTriggerState::status)==memberOffset(original, &original_values::RuntimeTriggerState::status), "RuntimeTriggerState.status offset");
      expect(memberOffset(current, &RuntimeTriggerState::triggers)==memberOffset(original, &original_values::RuntimeTriggerState::triggers), "RuntimeTriggerState.triggers offset");
      expect(memberOffset(current, &RuntimeTriggerState::teams)==memberOffset(original, &original_values::RuntimeTriggerState::teams), "RuntimeTriggerState.teams offset");
      expect(memberOffset(current, &RuntimeTriggerState::taskForces)==memberOffset(original, &original_values::RuntimeTriggerState::taskForces), "RuntimeTriggerState.taskForces offset");
      expect(memberOffset(current, &RuntimeTriggerState::scripts)==memberOffset(original, &original_values::RuntimeTriggerState::scripts), "RuntimeTriggerState.scripts offset");
      expect(memberOffset(current, &RuntimeTriggerState::teamIds)==memberOffset(original, &original_values::RuntimeTriggerState::teamIds), "RuntimeTriggerState.teamIds offset");
      expect(memberOffset(current, &RuntimeTriggerState::scriptIds)==memberOffset(original, &original_values::RuntimeTriggerState::scriptIds), "RuntimeTriggerState.scriptIds offset");
      expect(memberOffset(current, &RuntimeTriggerState::teamMissions)==memberOffset(original, &original_values::RuntimeTriggerState::teamMissions), "RuntimeTriggerState.teamMissions offset");
      expect(memberOffset(current, &RuntimeTriggerState::reinforcements)==memberOffset(original, &original_values::RuntimeTriggerState::reinforcements), "RuntimeTriggerState.reinforcements offset");
      expect(memberOffset(current, &RuntimeTriggerState::nextTeamMissionInstanceId)==memberOffset(original, &original_values::RuntimeTriggerState::nextTeamMissionInstanceId), "RuntimeTriggerState.nextTeamMissionInstanceId offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignHouseByIndex)==memberOffset(original, &original_values::RuntimeTriggerState::campaignHouseByIndex), "RuntimeTriggerState.campaignHouseByIndex offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignCountryByIndex)==memberOffset(original, &original_values::RuntimeTriggerState::campaignCountryByIndex), "RuntimeTriggerState.campaignCountryByIndex offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignCanonicalHouseByAlias)==memberOffset(original, &original_values::RuntimeTriggerState::campaignCanonicalHouseByAlias), "RuntimeTriggerState.campaignCanonicalHouseByAlias offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignCountryByHouse)==memberOffset(original, &original_values::RuntimeTriggerState::campaignCountryByHouse), "RuntimeTriggerState.campaignCountryByHouse offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignHouseProduction)==memberOffset(original, &original_values::RuntimeTriggerState::campaignHouseProduction), "RuntimeTriggerState.campaignHouseProduction offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignAiTriggers)==memberOffset(original, &original_values::RuntimeTriggerState::campaignAiTriggers), "RuntimeTriggerState.campaignAiTriggers offset");
      expect(memberOffset(current, &RuntimeTriggerState::computerAiTriggers)==memberOffset(original, &original_values::RuntimeTriggerState::computerAiTriggers), "RuntimeTriggerState.computerAiTriggers offset");
      expect(memberOffset(current, &RuntimeTriggerState::computerTacticsByOwner)==memberOffset(original, &original_values::RuntimeTriggerState::computerTacticsByOwner), "RuntimeTriggerState.computerTacticsByOwner offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignTeamBuilds)==memberOffset(original, &original_values::RuntimeTriggerState::campaignTeamBuilds), "RuntimeTriggerState.campaignTeamBuilds offset");
      expect(memberOffset(current, &RuntimeTriggerState::nextCampaignTeamBuildId)==memberOffset(original, &original_values::RuntimeTriggerState::nextCampaignTeamBuildId), "RuntimeTriggerState.nextCampaignTeamBuildId offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignAiTriggerSelections)==memberOffset(original, &original_values::RuntimeTriggerState::campaignAiTriggerSelections), "RuntimeTriggerState.campaignAiTriggerSelections offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignAiTriggerTeamsCompleted)==memberOffset(original, &original_values::RuntimeTriggerState::campaignAiTriggerTeamsCompleted), "RuntimeTriggerState.campaignAiTriggerTeamsCompleted offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignAiTriggerTeamsFailed)==memberOffset(original, &original_values::RuntimeTriggerState::campaignAiTriggerTeamsFailed), "RuntimeTriggerState.campaignAiTriggerTeamsFailed offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignAlliedPairs)==memberOffset(original, &original_values::RuntimeTriggerState::campaignAlliedPairs), "RuntimeTriggerState.campaignAlliedPairs offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignLocalVariables)==memberOffset(original, &original_values::RuntimeTriggerState::campaignLocalVariables), "RuntimeTriggerState.campaignLocalVariables offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignGlobalVariables)==memberOffset(original, &original_values::RuntimeTriggerState::campaignGlobalVariables), "RuntimeTriggerState.campaignGlobalVariables offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignCellTagByCell)==memberOffset(original, &original_values::RuntimeTriggerState::campaignCellTagByCell), "RuntimeTriggerState.campaignCellTagByCell offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignPreviousCellByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignPreviousCellByObject), "RuntimeTriggerState.campaignPreviousCellByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignPreviousHealthByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignPreviousHealthByObject), "RuntimeTriggerState.campaignPreviousHealthByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignInitialHealthByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignInitialHealthByObject), "RuntimeTriggerState.campaignInitialHealthByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignPreviousAliveByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignPreviousAliveByObject), "RuntimeTriggerState.campaignPreviousAliveByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignInitialOwnerByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignInitialOwnerByObject), "RuntimeTriggerState.campaignInitialOwnerByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignPreviousOwnerByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignPreviousOwnerByObject), "RuntimeTriggerState.campaignPreviousOwnerByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignPreviouslyKnownObjects)==memberOffset(original, &original_values::RuntimeTriggerState::campaignPreviouslyKnownObjects), "RuntimeTriggerState.campaignPreviouslyKnownObjects offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignPreviouslyDiscoveredObjects)==memberOffset(original, &original_values::RuntimeTriggerState::campaignPreviouslyDiscoveredObjects), "RuntimeTriggerState.campaignPreviouslyDiscoveredObjects offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignPreviousSelectedObjects)==memberOffset(original, &original_values::RuntimeTriggerState::campaignPreviousSelectedObjects), "RuntimeTriggerState.campaignPreviousSelectedObjects offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignAttackedTickByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignAttackedTickByObject), "RuntimeTriggerState.campaignAttackedTickByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignAttackerHouseByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignAttackerHouseByObject), "RuntimeTriggerState.campaignAttackerHouseByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignDestroyedTickByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignDestroyedTickByObject), "RuntimeTriggerState.campaignDestroyedTickByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignDestroyerHouseByObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignDestroyerHouseByObject), "RuntimeTriggerState.campaignDestroyerHouseByObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignEnteredTickByTargetObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignEnteredTickByTargetObject), "RuntimeTriggerState.campaignEnteredTickByTargetObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignEntrantHouseByTargetObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignEntrantHouseByTargetObject), "RuntimeTriggerState.campaignEntrantHouseByTargetObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignSpiedTickByTargetObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignSpiedTickByTargetObject), "RuntimeTriggerState.campaignSpiedTickByTargetObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignSpyHouseByTargetObject)==memberOffset(original, &original_values::RuntimeTriggerState::campaignSpyHouseByTargetObject), "RuntimeTriggerState.campaignSpyHouseByTargetObject offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignBridgeDestroyedTickByCell)==memberOffset(original, &original_values::RuntimeTriggerState::campaignBridgeDestroyedTickByCell), "RuntimeTriggerState.campaignBridgeDestroyedTickByCell offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignMoviePathByIndex)==memberOffset(original, &original_values::RuntimeTriggerState::campaignMoviePathByIndex), "RuntimeTriggerState.campaignMoviePathByIndex offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignTeamObjects)==memberOffset(original, &original_values::RuntimeTriggerState::campaignTeamObjects), "RuntimeTriggerState.campaignTeamObjects offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignMissionTimerStarted)==memberOffset(original, &original_values::RuntimeTriggerState::campaignMissionTimerStarted), "RuntimeTriggerState.campaignMissionTimerStarted offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignMissionTimerRunning)==memberOffset(original, &original_values::RuntimeTriggerState::campaignMissionTimerRunning), "RuntimeTriggerState.campaignMissionTimerRunning offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignMissionTimerRemainingTicks)==memberOffset(original, &original_values::RuntimeTriggerState::campaignMissionTimerRemainingTicks), "RuntimeTriggerState.campaignMissionTimerRemainingTicks offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignMissionTimerLabel)==memberOffset(original, &original_values::RuntimeTriggerState::campaignMissionTimerLabel), "RuntimeTriggerState.campaignMissionTimerLabel offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignInputLocked)==memberOffset(original, &original_values::RuntimeTriggerState::campaignInputLocked), "RuntimeTriggerState.campaignInputLocked offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignForceDepth)==memberOffset(original, &original_values::RuntimeTriggerState::campaignForceDepth), "RuntimeTriggerState.campaignForceDepth offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignRadarBlackoutUntilTick)==memberOffset(original, &original_values::RuntimeTriggerState::campaignRadarBlackoutUntilTick), "RuntimeTriggerState.campaignRadarBlackoutUntilTick offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignChronoScreenEffectUntilTick)==memberOffset(original, &original_values::RuntimeTriggerState::campaignChronoScreenEffectUntilTick), "RuntimeTriggerState.campaignChronoScreenEffectUntilTick offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignIonStormUntilTick)==memberOffset(original, &original_values::RuntimeTriggerState::campaignIonStormUntilTick), "RuntimeTriggerState.campaignIonStormUntilTick offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignNextIonStormStrikeTick)==memberOffset(original, &original_values::RuntimeTriggerState::campaignNextIonStormStrikeTick), "RuntimeTriggerState.campaignNextIonStormStrikeTick offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignRadarEvents)==memberOffset(original, &original_values::RuntimeTriggerState::campaignRadarEvents), "RuntimeTriggerState.campaignRadarEvents offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignTechLevelByType)==memberOffset(original, &original_values::RuntimeTriggerState::campaignTechLevelByType), "RuntimeTriggerState.campaignTechLevelByType offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignBaseCenterByOwner)==memberOffset(original, &original_values::RuntimeTriggerState::campaignBaseCenterByOwner), "RuntimeTriggerState.campaignBaseCenterByOwner offset");
      expect(memberOffset(current, &RuntimeTriggerState::campaignPreferredSuperWeaponTargetByOwner)==memberOffset(original, &original_values::RuntimeTriggerState::campaignPreferredSuperWeaponTargetByOwner), "RuntimeTriggerState.campaignPreferredSuperWeaponTargetByOwner offset");
      expect(memberOffset(current, &RuntimeTriggerState::buildableTaskForceCache)==memberOffset(original, &original_values::RuntimeTriggerState::buildableTaskForceCache), "RuntimeTriggerState.buildableTaskForceCache offset");
    }
    expect(RuntimeCellPoint{}.x==-1 && RuntimeCellPoint{}.y==-1, "logical cell invalid defaults");
    expect(RuntimeCellPoint{1,2}==RuntimeCellPoint{1,2} && RuntimeCellPoint{1,2}!=RuntimeCellPoint{2,1}, "original equality/inequality");
}
auto authorityFields(const RuntimeCellPoint& v) { return std::tie(v.x, v.y); }
auto authorityFields(const RuntimeScriptStep& v) { return std::tie(v.action, v.argument); }
auto authorityFields(const RuntimeScriptType& v) { return std::tie(v.id, v.name, v.steps); }
auto authorityFields(const RuntimeTaskForceMember& v) { return std::tie(v.count, v.unitId); }
auto authorityFields(const RuntimeTaskForce& v) { return std::tie(v.id, v.name, v.group, v.members); }
auto authorityFields(const RuntimeTriggerEvent& v) { return std::tie(v.type, v.authoredOrdinal, v.lightingEventId, v.arguments); }
auto authorityFields(const RuntimeTeamType& v) { return std::tie(v.id, v.name, v.house, v.waypointId, v.scriptId, v.taskForceId, v.priority, v.max, v.group, v.autocreate, v.prebuild, v.reinforce, v.full, v.aggressive, v.isBaseDefense, v.recruitable, v.recruiter, v.looseRecruit, v.avoidThreats, v.suicide, v.guardSlower, v.onlyTargetHouseEnemy, v.transportsReturnOnUnload, v.veteranLevel, v.mindControlDecision); }
auto authorityFields(const RuntimeCampaignHouseProductionState& v) { return std::tie(v.owner, v.country, v.techLevel, v.iq, v.side, v.credits, v.nextAiTriggerCheckTick, v.selectionSerial, v.activeBuildId, v.preferredTargetQuarry, v.aiTriggerTeamRatio, v.teamAircraftRatio, v.teamInfantryRatio, v.teamUnitRatio, v.playerControlled, v.productionStarted, v.aiTriggersEnabled); }
auto authorityFields(const RuntimeCampaignTeamBuild& v) { return std::tie(v.instanceId, v.aiTriggerId, v.teamTypeId, v.owner, v.memberIndex, v.memberProduced, v.remainingTicks, v.paidCost, v.activeTypeId, v.objectIds, v.deadlineTick, v.completed, v.failed, v.status); }
auto authorityFields(const RuntimeTeamMissionState& v) { return std::tie(v.instanceId, v.teamTypeId, v.scriptId, v.objectIds, v.stepIndex, v.stepStartedTick, v.stepIssued, v.targetObjectId, v.targetCell, v.completed, v.sourceAiTriggerId, v.sourceAiBuildId, v.aiOutcomeReported, v.achievedGreatSuccess, v.targetInteractionCompleted, v.transportReturnCells, v.regrouping, v.regroupCell, v.regroupStartedTick, v.regroupDestinations, v.status); }
auto authorityFields(const RuntimeTriggerAction& v) { return std::tie(v.type, v.authoredOrdinal, v.targetId, v.arguments); }
auto authorityFields(const RuntimeTriggerDefinition& v) { return std::tie(v.id, v.house, v.linkedTriggerId, v.name, v.fired, v.enabled, v.repeating, v.difficultyEnabled, v.activationTick, v.lastFiredTick, v.nextEligibleTick, v.tagRepeatType, v.tagIds, v.remainingOnceAllObjectIds, v.events, v.actions); }
auto authorityFields(const RuntimeCampaignRadarEvent& v) { return std::tie(v.cell, v.eventType, v.untilTick); }
auto authorityFields(const RuntimePublicBuildingLocation& v) { return std::tie(v.owner, v.cell, v.noticeTick); }
auto authorityFields(const RuntimeCampaignAiTriggerDefinition& v) { return std::tie(v.id, v.name, v.primaryTeamId, v.owner, v.techLevel, v.conditionType, v.conditionObject, v.conditionNumber, v.conditionComparator, v.currentWeight, v.minimumWeight, v.maximumWeight, v.availableInSkirmish, v.baseDefense, v.side, v.scope, v.secondaryTeamId, v.difficultyEnabled, v.enabled, v.valid, v.timesExecuted, v.timesCompleted, v.timesFailed, v.nextEligibleTick); }
auto authorityFields(const original_values::RuntimeCellPoint& v) { return std::tie(v.x, v.y); }
auto authorityFields(const original_values::RuntimeScriptStep& v) { return std::tie(v.action, v.argument); }
auto authorityFields(const original_values::RuntimeScriptType& v) { return std::tie(v.id, v.name, v.steps); }
auto authorityFields(const original_values::RuntimeTaskForceMember& v) { return std::tie(v.count, v.unitId); }
auto authorityFields(const original_values::RuntimeTaskForce& v) { return std::tie(v.id, v.name, v.group, v.members); }
auto authorityFields(const original_values::RuntimeTriggerEvent& v) { return std::tie(v.type, v.authoredOrdinal, v.lightingEventId, v.arguments); }
auto authorityFields(const original_values::RuntimeTeamType& v) { return std::tie(v.id, v.name, v.house, v.waypointId, v.scriptId, v.taskForceId, v.priority, v.max, v.group, v.autocreate, v.prebuild, v.reinforce, v.full, v.aggressive, v.isBaseDefense, v.recruitable, v.recruiter, v.looseRecruit, v.avoidThreats, v.suicide, v.guardSlower, v.onlyTargetHouseEnemy, v.transportsReturnOnUnload, v.veteranLevel, v.mindControlDecision); }
auto authorityFields(const original_values::RuntimeCampaignHouseProductionState& v) { return std::tie(v.owner, v.country, v.techLevel, v.iq, v.side, v.credits, v.nextAiTriggerCheckTick, v.selectionSerial, v.activeBuildId, v.preferredTargetQuarry, v.aiTriggerTeamRatio, v.teamAircraftRatio, v.teamInfantryRatio, v.teamUnitRatio, v.playerControlled, v.productionStarted, v.aiTriggersEnabled); }
auto authorityFields(const original_values::RuntimeCampaignTeamBuild& v) { return std::tie(v.instanceId, v.aiTriggerId, v.teamTypeId, v.owner, v.memberIndex, v.memberProduced, v.remainingTicks, v.paidCost, v.activeTypeId, v.objectIds, v.deadlineTick, v.completed, v.failed, v.status); }
auto authorityFields(const original_values::RuntimeTeamMissionState& v) { return std::tie(v.instanceId, v.teamTypeId, v.scriptId, v.objectIds, v.stepIndex, v.stepStartedTick, v.stepIssued, v.targetObjectId, v.targetCell, v.completed, v.sourceAiTriggerId, v.sourceAiBuildId, v.aiOutcomeReported, v.achievedGreatSuccess, v.targetInteractionCompleted, v.transportReturnCells, v.regrouping, v.regroupCell, v.regroupStartedTick, v.regroupDestinations, v.status); }
auto authorityFields(const original_values::RuntimeTriggerAction& v) { return std::tie(v.type, v.authoredOrdinal, v.targetId, v.arguments); }
auto authorityFields(const original_values::RuntimeTriggerDefinition& v) { return std::tie(v.id, v.house, v.linkedTriggerId, v.name, v.fired, v.enabled, v.repeating, v.difficultyEnabled, v.activationTick, v.lastFiredTick, v.nextEligibleTick, v.tagRepeatType, v.tagIds, v.remainingOnceAllObjectIds, v.events, v.actions); }
auto authorityFields(const original_values::RuntimeCampaignRadarEvent& v) { return std::tie(v.cell, v.eventType, v.untilTick); }
auto authorityFields(const original_values::RuntimePublicBuildingLocation& v) { return std::tie(v.owner, v.cell, v.noticeTick); }

auto authorityFields(const RuntimeComputerTacticHistory& v) { return std::tie(v.weight,v.executions,v.successes,v.failures); }
auto authorityFields(const RuntimeComputerTacticTeam& v) { return std::tie(v.instanceId,v.triggerId,v.teamTypeId,v.targetOwner,v.objectIds,v.joiningObjectIds,v.missingMembers,v.createdTick,v.lastProgressTick,v.missionInstanceId,v.secondary,v.baseDefense,v.launched,v.outcomeReported,v.missionFinished,v.achievedGreatSuccess); }
auto authorityFields(const RuntimeComputerTacticProduction& v) { return std::tie(v.teamInstanceId,v.typeId,v.remainingTicks,v.paidCost,v.totalTicks); }
auto authorityFields(const RuntimeComputerTacticsState::CaptureDecision& v) { return std::tie(v.controllerId,v.startedTick,v.decision); }
auto authorityFields(const RuntimeComputerTacticsState& v) { return std::tie(v.nextTriggerTick,v.selectionSerial,v.nextTeamInstanceId,v.production,v.teams,v.history,v.captureDecisions); }

// Canonical complete value encoding for comparisons: unordered containers
// are sorted by encoded entries, while authored vectors retain their order.
struct Encoder {
    std::ostringstream out;
    template<class T> void value(const T& v) {
        if constexpr (std::is_same_v<T,std::string>) out << v.size() << ':' << v << ';';
        else if constexpr (std::is_same_v<T,double>) out << std::bit_cast<std::uint64_t>(v) << ';';
        else if constexpr (std::is_arithmetic_v<T>) out << v << ';';
        else if constexpr (requires { v.first; v.second; }) { value(v.first); value(v.second); }
        else if constexpr (requires { v.begin(); v.end(); }) {
            out << v.size() << '[';
            if constexpr (requires { typename T::hasher; }) {
                std::vector<std::string> entries;
                for (const auto& x:v) { Encoder e; e.value(x); entries.push_back(e.out.str()); }
                std::sort(entries.begin(), entries.end());
                for (const auto& x:entries) value(x);
            } else for (const auto& x:v) value(x);
            out << ']';
        } else std::apply([&](const auto&... field) { (value(field),...); },authorityFields(v));
    }
};
template<class Trigger> std::string completeTriggerValue(const Trigger& value) {
    Encoder e;
    e.value(value.publicBuildingLocations);
    e.value(value.loaded);
    e.value(value.truckCrate);
    e.value(value.trainCrate);
    e.value(value.bootstrapComplete);
    e.value(value.triggerCount);
    e.value(value.eventCount);
    e.value(value.actionCount);
    e.value(value.tagCount);
    e.value(value.teamTypeCount);
    e.value(value.taskForceCount);
    e.value(value.scriptTypeCount);
    e.value(value.createTeamActionCount);
    e.value(value.firedTriggers);
    e.value(value.teamsCreated);
    e.value(value.unitsCreated);
    e.value(value.ordersIssued);
    e.value(value.scriptStepsIssued);
    e.value(value.scriptStepsCompleted);
    e.value(value.unsupportedScriptSteps);
    e.value(value.unsupportedActions);
    e.value(value.unsupportedEvents);
    e.value(value.allowLegacyBootstrapForSmokeTest);
    e.value(value.collectSmokeTimings);
    e.value(value.smokeCreateTeamMs);
    e.value(value.smokeIssueOrderMs);
    e.value(value.status);
    e.value(value.triggers);
    e.value(value.teams);
    e.value(value.taskForces);
    e.value(value.scripts);
    e.value(value.teamIds);
    e.value(value.scriptIds);
    e.value(value.teamMissions);
    e.value(serializeRuntimeTriggerReinforcements(value.reinforcements));
    e.value(value.nextTeamMissionInstanceId);
    e.value(value.campaignHouseByIndex);
    e.value(value.campaignCountryByIndex);
    e.value(value.campaignCanonicalHouseByAlias);
    e.value(value.campaignCountryByHouse);
    e.value(value.campaignHouseProduction);
    e.value(value.campaignAiTriggers);
    e.value(value.computerAiTriggers);
    e.value(value.computerTacticsByOwner);
    e.value(value.campaignTeamBuilds);
    e.value(value.nextCampaignTeamBuildId);
    e.value(value.campaignAiTriggerSelections);
    e.value(value.campaignAiTriggerTeamsCompleted);
    e.value(value.campaignAiTriggerTeamsFailed);
    e.value(value.campaignAlliedPairs);
    e.value(value.campaignLocalVariables);
    e.value(value.campaignGlobalVariables);
    e.value(value.campaignCellTagByCell);
    e.value(value.campaignPreviousCellByObject);
    e.value(value.campaignPreviousHealthByObject);
    e.value(value.campaignInitialHealthByObject);
    e.value(value.campaignPreviousAliveByObject);
    e.value(value.campaignInitialOwnerByObject);
    e.value(value.campaignPreviousOwnerByObject);
    e.value(value.campaignPreviouslyKnownObjects);
    e.value(value.campaignPreviouslyDiscoveredObjects);
    e.value(value.campaignPreviousSelectedObjects);
    e.value(value.campaignAttackedTickByObject);
    e.value(value.campaignAttackerHouseByObject);
    e.value(value.campaignDestroyedTickByObject);
    e.value(value.campaignDestroyerHouseByObject);
    e.value(value.campaignEnteredTickByTargetObject);
    e.value(value.campaignEntrantHouseByTargetObject);
    e.value(value.campaignSpiedTickByTargetObject);
    e.value(value.campaignSpyHouseByTargetObject);
    e.value(value.campaignBridgeDestroyedTickByCell);
    e.value(value.campaignMoviePathByIndex);
    e.value(value.campaignTeamObjects);
    e.value(value.campaignMissionTimerStarted);
    e.value(value.campaignMissionTimerRunning);
    e.value(value.campaignMissionTimerRemainingTicks);
    e.value(value.campaignMissionTimerLabel);
    e.value(value.campaignInputLocked);
    e.value(value.campaignForceDepth);
    e.value(value.campaignRadarBlackoutUntilTick);
    e.value(value.campaignChronoScreenEffectUntilTick);
    e.value(value.campaignIonStormUntilTick);
    e.value(value.campaignNextIonStormStrikeTick);
    e.value(value.campaignRadarEvents);
    e.value(value.campaignTechLevelByType);
    e.value(value.campaignBaseCenterByOwner);
    e.value(value.campaignPreferredSuperWeaponTargetByOwner);
    e.value(value.buildableTaskForceCache);
    return e.out.str();
}
// Recording operations exercise phase policy only. Complete real Campaign,
// legacy-trigger and reinforcement routes are separate native acceptance.
struct FixtureSession {
    RuntimeTriggerState triggers;
    GameWorld world;
    RuntimeConfig config;
    int tick=0;
    bool campaignMode=false, skirmishMatchFinished=false, repaintRequested=false;
    struct Cooperative { bool enabled=false; bool active() const { return enabled; } } cooperativeSession;
};
struct RecordingOperations final : RuntimeTriggerOperations {
    FixtureSession& s;
    unsigned scenario;
    int throwAt, calls=0;
    std::vector<std::string> trace;
    RecordingOperations(FixtureSession& session,unsigned test,int failure) : s(session),scenario(test),throwAt(failure) {}
    void mark(std::string message) {
        trace.push_back(std::move(message));
        if (++calls==throwAt) throw std::runtime_error("planned original phase boundary");
    }
    void sampleLightingThresholdEvents() override { mark("lighting"); ++s.triggers.campaignLocalVariables[101]; }
    int campaignIonStrikeDelayTicks() override { mark("ion-delay"); return 2+(s.tick%3); }
    bool fireCampaignIonStrike(const RuntimeCellPoint& cell,const std::string& owner) override {
        mark("ion:"+std::to_string(cell.x)+","+std::to_string(cell.y)+owner);
        ++s.triggers.campaignGlobalVariables[102]; return (scenario&1)!=0;
    }
    bool campaignEventSatisfied(const RuntimeTriggerDefinition& trigger,const RuntimeTriggerEvent& event) override {
        mark("event:"+trigger.id+":"+std::to_string(event.type));
        ++s.triggers.campaignLocalVariables[event.type]; return event.type!=9 || (scenario&2)!=0;
    }
    const WorldObjectInstance* objectIncludingInactive(int id) override {
        mark("object:"+std::to_string(id));
        for (const auto& object:s.world.getObjects()) if (object.instanceId==id) return &object;
        return nullptr;
    }
    bool campaignHouseMatches(const std::string& a,const std::string& b) override { mark("house:"+a+":"+b); return a==b; }
    bool applyCampaignAction(RuntimeTriggerDefinition& trigger,const RuntimeTriggerAction& action) override {
        mark("action:"+trigger.id+":"+std::to_string(action.type));
        ++s.triggers.campaignGlobalVariables[action.type];
        if (action.type==991) { s.tick+=2; s.config.fixedTickHz=3; }
        if (action.type==992) s.skirmishMatchFinished=true;
        return (action.type&1)!=0;
    }
    void snapshotCampaignObservations() override { mark("snapshot"); s.triggers.campaignPreviousSelectedObjects.insert(s.tick); }
    bool advanceReinforcements() override {
        mark("reinforcements"); ++s.triggers.nextTeamMissionInstanceId;
        if ((scenario%17)==0) s.cooperativeSession.enabled=true;
        return (scenario&4)!=0;
    }
    bool advanceTeamMissions() override { mark("missions"); ++s.triggers.scriptStepsIssued; return (scenario&8)!=0; }
    bool advanceCampaignAiProduction() override { mark("campaign-ai"); ++s.triggers.campaignAiTriggerSelections; return (scenario&16)!=0; }
    bool fireSuperWeapon(const RuntimeTriggerDefinition& trigger,const RuntimeTriggerAction& action) override {
        mark("super:"+trigger.id+":"+std::to_string(action.type)); return (scenario&32)!=0;
    }
    bool fireCreateTeam(RuntimeTriggerDefinition& trigger,const RuntimeTriggerAction& action) override {
        mark("team:"+trigger.id+":"+action.targetId); ++s.triggers.teamsCreated; return (scenario&64)!=0;
    }
    const RuntimeTeamType* chooseTeam() override { mark("choose-team"); return s.triggers.teams.empty()?nullptr:&s.triggers.teams.begin()->second; }
    bool cooperativeActive() override { return s.cooperativeSession.active(); }
};
namespace original_phase {
static RecordingOperations* bound=nullptr;
void sampleRuntimeLightingThresholdEvents(FixtureSession& state) { (void)state; bound->sampleLightingThresholdEvents(); }
int runtimeCampaignIonStrikeDelayTicks(FixtureSession& state) { (void)state; return bound->campaignIonStrikeDelayTicks(); }
bool fireRuntimeCampaignIonStrike(FixtureSession& state, const RuntimeCellPoint& cell, const std::string& owner) { (void)state; return bound->fireCampaignIonStrike(cell, owner); }
bool runtimeCampaignEventSatisfied(FixtureSession& state, const RuntimeTriggerDefinition& trigger, const RuntimeTriggerEvent& event) { (void)state; return bound->campaignEventSatisfied(trigger, event); }
const WorldObjectInstance* findRuntimeObjectByIdIncludingInactive(FixtureSession& state, int objectId) { (void)state; return bound->objectIncludingInactive(objectId); }
bool runtimeCampaignHouseMatches(FixtureSession& state, const std::string& authoredHouse, const std::string& objectHouse) { (void)state; return bound->campaignHouseMatches(authoredHouse, objectHouse); }
bool applyRuntimeCampaignAction(FixtureSession& state, RuntimeTriggerDefinition& trigger, const RuntimeTriggerAction& action) { (void)state; return bound->applyCampaignAction(trigger, action); }
void snapshotRuntimeCampaignTriggerObservations(FixtureSession& state) { (void)state; bound->snapshotCampaignObservations(); }
bool advanceRuntimeTriggerReinforcements(FixtureSession& state) { (void)state; return bound->advanceReinforcements(); }
bool advanceRuntimeTeamMissions(FixtureSession& state) { (void)state; return bound->advanceTeamMissions(); }
bool advanceRuntimeCampaignAiProduction(FixtureSession& state) { (void)state; return bound->advanceCampaignAiProduction(); }
bool fireRuntimeTriggerSuperWeapon(FixtureSession& state, const RuntimeTriggerDefinition& trigger, const RuntimeTriggerAction& action) { (void)state; return bound->fireSuperWeapon(trigger, action); }
bool fireRuntimeCreateTeamTrigger(FixtureSession& state, RuntimeTriggerDefinition& trigger, const RuntimeTriggerAction& action) { (void)state; return bound->fireCreateTeam(trigger, action); }
const RuntimeTeamType* chooseRuntimeTriggerTeam(FixtureSession& state) { (void)state; return bound->chooseTeam(); }

bool advanceRuntimeCampaignTriggers(FixtureSession& state) {
    RuntimePerfTraceScope originalMapTriggerPhaseTrace("advanceRuntimeCampaignTriggers", state.tick);
    if (!state.triggers.loaded || state.skirmishMatchFinished) {
        return false;
    }

    sampleRuntimeLightingThresholdEvents(state);
    bool changed = false;
    if (state.triggers.campaignIonStormUntilTick > state.tick) {
        if (state.triggers.campaignNextIonStormStrikeTick <= 0) {
            state.triggers.campaignNextIonStormStrikeTick = state.tick
                + runtimeCampaignIonStrikeDelayTicks(state);
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
                (void)fireRuntimeCampaignIonStrike(
                    state,
                    RuntimeCellPoint{strikeCell.x, strikeCell.y},
                    "Neutral"
                );
            }
            state.triggers.campaignNextIonStormStrikeTick = state.tick
                + runtimeCampaignIonStrikeDelayTicks(state);
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
            if (!runtimeCampaignEventSatisfied(state, trigger, event)) {
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
                    findRuntimeObjectByIdIncludingInactive(state, objectId);
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
                    && !runtimeCampaignHouseMatches(
                        state,
                        previousOwner->second,
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
            actionApplied = applyRuntimeCampaignAction(
                state,
                trigger,
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
    snapshotRuntimeCampaignTriggerObservations(state);
    return changed;
}

bool advanceRuntimeTriggers(FixtureSession& state) {
    const bool reinforcementChanged = advanceRuntimeTriggerReinforcements(state);
    if (state.cooperativeSession.active()) {
        const bool missionsChanged = advanceRuntimeTeamMissions(state) || reinforcementChanged;
        const bool eventsChanged = advanceRuntimeCampaignTriggers(state);
        state.triggers.bootstrapComplete = true;
        return missionsChanged || eventsChanged;
    }
    if (state.campaignMode
        || toUpperAsciiLocal(trimAsciiLocal(state.config.gameMode))
            == "CAMPAIGNMISSION") {
        const bool missionsChanged = advanceRuntimeTeamMissions(state) || reinforcementChanged;
        const bool triggersChanged = advanceRuntimeCampaignTriggers(state);
        const bool aiChanged = advanceRuntimeCampaignAiProduction(state);
        return missionsChanged || triggersChanged || aiChanged;
    }
    if (!state.triggers.loaded) {
        return reinforcementChanged;
    }

    const bool missionsChanged = advanceRuntimeTeamMissions(state) || reinforcementChanged;
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
                const bool fired = fireRuntimeTriggerSuperWeapon(
                    state,
                    trigger,
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
                    fireRuntimeCreateTeamTrigger(state, trigger, action);
                state.triggers.bootstrapComplete = true;
                return fired || missionsChanged;
            }
            ++state.triggers.unsupportedActions;
        }
    }

    if (const RuntimeTeamType* fallbackTeam = chooseRuntimeTriggerTeam(state)) {
        RuntimeTriggerDefinition syntheticTrigger;
        syntheticTrigger.id = "runtime_bootstrap";
        RuntimeTriggerAction syntheticAction;
        syntheticAction.type = 80;
        syntheticAction.targetId = fallbackTeam->id;
        const bool fired =
            fireRuntimeCreateTeamTrigger(state, syntheticTrigger, syntheticAction);
        state.triggers.bootstrapComplete = true;
        return fired || missionsChanged;
    }

    state.triggers.bootstrapComplete = true;
    state.triggers.status = "no create-team trigger available";
    return missionsChanged;
}

}
FixtureSession scenarioState(unsigned scenario) {
    FixtureSession s;
    s.tick=static_cast<int>(scenario%5)*10;
    s.config.fixedTickHz=(scenario%3)==0?0:8;
    s.config.gameMode=(scenario%4)==0?" CampaignMission ":"Skirmish";
    s.config.defaultMapPath="original/ion/seed.map";
    s.campaignMode=(scenario&1)!=0;
    s.cooperativeSession.enabled=(scenario%7)==0;
    s.skirmishMatchFinished=(scenario%19)==0;
    s.triggers.loaded=(scenario%11)!=0;
    s.triggers.allowLegacyBootstrapForSmokeTest=(scenario&2)!=0;
    s.triggers.bootstrapComplete=(scenario%13)==0;
    s.triggers.campaignMissionTimerRunning=(scenario&4)!=0;
    s.triggers.campaignMissionTimerRemainingTicks=17;
    s.triggers.campaignIonStormUntilTick=(scenario%3)==0?s.tick+14:((scenario%3)==1?1:0);
    s.triggers.campaignNextIonStormStrikeTick=(scenario%4)==0?0:s.tick-1;
    if ((scenario%3)!=0) for (int k=0;k<4;++k) {
        TerrainCell cell;cell.x=7+k;cell.y=11-k;s.world.accessTerrainCells().push_back(cell);
    }
    for (int id=1;id<=3;++id) {
        WorldObjectInstance object;object.instanceId=id;object.alive=id!=1;object.owner=id==2?"Captured":"Neutral";
        s.world.accessObjects().push_back(object);
        s.triggers.campaignPreviousAliveByObject[id]=true;
        s.triggers.campaignPreviousOwnerByObject[id]="Neutral";
        s.triggers.campaignEnteredTickByTargetObject[id]=id==3?s.tick:-1;
    }
    for (int k=0;k<6;++k) {
        RuntimeTriggerDefinition trigger;trigger.id="original-"+std::to_string(k);
        trigger.enabled=k!=0;trigger.fired=k==1;trigger.repeating=k==2;
        trigger.nextEligibleTick=k==3?s.tick+4:0;
        trigger.activationTick=0;trigger.lastFiredTick=-1;
        if (k!=4) { RuntimeTriggerEvent event;event.type=k==3?9:1;trigger.events.push_back(event); }
        if (k==5 && (scenario&8)!=0) { trigger.tagRepeatType=1;trigger.remainingOnceAllObjectIds={1,2,3,4}; }
        for (int actionType:{999,33,80,991,((scenario&128)!=0?992:990)}) {
            RuntimeTriggerAction action;action.type=actionType;action.targetId="team-a";trigger.actions.push_back(action);
        }
        if ((scenario%9)==0) trigger.actions.clear();
        s.triggers.triggers.push_back(trigger);
    }
    if ((scenario%5)!=0) { RuntimeTeamType team;team.id="team-a";s.triggers.teams.emplace(team.id,team); }
    return s;
}
int main() {
    checkOriginalLayouts();
    expect(completeTriggerValue(RuntimeTriggerState{})==completeTriggerValue(original_values::RuntimeTriggerState{}), "all original trigger defaults and nested values");
    for (int type=-2;type<=112;++type) {
        expect(runtimeIsCreateTeamAction(type)==(type==4||type==7||type==80||type==107),"original create-team classification");
        expect(runtimeIsSuperWeaponTriggerAction(type)==(type==33||type==34||type==95||type==102||type==109),"original superweapon classification");
    }
    int observations=0;
    for (unsigned scenario=0;scenario<256;++scenario) for (bool campaignOnly:{false,true}) {
        FixtureSession old=scenarioState(scenario),current=old;
        const int failure=(scenario%4)==0?static_cast<int>(scenario%23)+1:-1;
        RecordingOperations oldOps(old,scenario,failure),currentOps(current,scenario,failure);
        original_phase::bound=&oldOps;
        for (int step=0;step<36;++step) {
            bool originalResult=false,currentResult=false,oldThrow=false,currentThrow=false;
            try { originalResult=campaignOnly?original_phase::advanceRuntimeCampaignTriggers(old):original_phase::advanceRuntimeTriggers(old); }
            catch (const std::runtime_error&) { oldThrow=true; }
            RuntimeTriggerContext context{current.triggers,current.world,current.config,current.tick,current.campaignMode,current.skirmishMatchFinished,current.repaintRequested};
            try { currentResult=campaignOnly?advanceRuntimeCampaignTriggers(context,currentOps):advanceRuntimeTriggers(context,currentOps); }
            catch (const std::runtime_error&) { currentThrow=true; }
            expect(originalResult==currentResult && oldThrow==currentThrow,"return/exception timing");
            expect(oldOps.trace==currentOps.trace,"ordered operation calls and short-circuit paths");
            expect(completeTriggerValue(old.triggers)==completeTriggerValue(current.triggers),"all trigger domain fields including original codecs");
            expect(old.world.deterministicSimulationStateHash()==current.world.deterministicSimulationStateHash(),"complete production GameWorld authority hash");
            expect(old.tick==current.tick && old.campaignMode==current.campaignMode && old.skirmishMatchFinished==current.skirmishMatchFinished && old.repaintRequested==current.repaintRequested && old.config.fixedTickHz==current.config.fixedTickHz && old.cooperativeSession.active()==current.cooperativeSession.active(),"live external context fields");
            ++observations;
            if (oldThrow || currentThrow) break;
            ++old.tick;++current.tick;
        }
    }
    std::cout << "H035 scheduler policy contract: checks=" << checks << " observations=" << observations << " scenarios=512 failures=" << failures << "\n";
    return failures?1:0;
}
