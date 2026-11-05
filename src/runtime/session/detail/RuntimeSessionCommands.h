#pragma once

#include "RuntimeProductionOperations.h"

namespace concord::runtime::session_detail {

struct RuntimeBuildAvailability {
    bool canQueue = false;
    bool canStart = false;
    bool enoughCredits = false;
    bool lowPower = false;
    bool lowPowerAfterBuild = false;
    int powerNetAfterBuild = 0;
    RuntimeTechTreeDecision decision;
    std::string reason;
};

RuntimeBuildAvailability runtimeBuildAvailability(
    const RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    bool respectStructureQueueLimit = true,
    bool respectBuildLimit = true
);

bool runtimeCellCanPlaceSidebarBuilding(
    const RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const RuntimeCellPoint& cell,
    const std::unordered_map<int, int>* occupiedByCell = nullptr,
    const std::string& placementOwner = {},
    const std::unordered_set<int>* rubbleCellsOverride = nullptr
);

WorldObjectInstance* addRuntimeObjectInstance(
    RuntimeProductionHostState& state,
    RuntimeBuildKind kind,
    const std::string& typeId,
    const RuntimeCellPoint& cell,
    bool clearPlacedSmudges = true
);

std::vector<RuntimeCellPoint> runtimeWallConnectorCells(
    const RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const RuntimeCellPoint& placementCell
);

bool queueRuntimeBuildOrder(
    RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    RuntimeProductionInsertMode insertMode =
        RuntimeProductionInsertMode::Back
);

bool queueRuntimeBuildOrderForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& commandOwner,
    const RuntimeBuildCatalogItem& item,
    RuntimeProductionInsertMode insertMode =
        RuntimeProductionInsertMode::Back
);

bool cancelRuntimeBuildOrderForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& commandOwner,
    RuntimeProductionLane lane,
    std::uint64_t serial
);

bool placeRuntimeReadyBuilding(
    RuntimeProductionHostState& state,
    const RuntimeCellPoint& cell
);

bool runtimeStructureCanBeSold(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& object
);

bool runtimeStructureCanStartRepair(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& object
);

bool sellRuntimeStructure(RuntimeProductionHostState& state, int objectId);

bool repairRuntimeStructure(RuntimeProductionHostState& state, int objectId);

bool sellRuntimeStructureForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& commandOwner,
    int objectId
);

bool repairRuntimeStructureForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& commandOwner,
    int objectId
);

void issueRuntimeGuardCommand(RuntimeProductionHostState& state);

void issueRuntimeScatterCommand(RuntimeProductionHostState& state);

std::optional<bool> runtimeSecondaryDeploymentState(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& object
);

bool executeRuntimeSecondaryActions(
    RuntimeProductionHostState& state,
    const std::vector<int>& objectIds,
    RuntimeMultiplayerGameplaySecondaryActionKind action
);

const RuntimeBuildCatalogItem* findRuntimeGameplayCatalogItem(
    const RuntimeProductionHostState& state,
    const std::string& owner,
    const std::string& typeId
);

bool prevalidateRuntimeMultiplayerGameplayCommand(
    const RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayCommand& command,
    std::string& errorMessage
);

bool prevalidateRuntimeMultiplayerGameplayBatch(
    const RuntimeProductionHostState& state,
    std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
    std::string& errorMessage
);

bool applyRuntimeGameplaySuperWeaponCommand(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayFireSuperWeaponCommand& command
);

bool placeRuntimeGameplayBuildingForHumanOwner(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayPlaceBuildingCommand& command
);

bool applyRuntimeMultiplayerGameplayCommand(
    RuntimeProductionHostState& state,
    const std::string& owner,
    const RuntimeMultiplayerGameplayCommand& command
);

bool applyRuntimeMultiplayerGameplayBatch(
    RuntimeProductionHostState& state,
    std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
    std::string& errorMessage
);

bool validateRuntimePlayerFactoryCommand(const RuntimeProductionHostState& state,
    const std::string& owner, int objectId, std::string& error
);

bool applyRuntimePlayerPrimaryFactory(RuntimeProductionHostState& state,
    const std::string& owner, int objectId
);

bool applyRuntimePlayerRallyPoint(RuntimeProductionHostState& state,
    const std::string& owner, int objectId, RuntimeMultiplayerGameplayCell cell
);

bool applyRuntimePlayerTargetOrder(RuntimeProductionHostState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayTargetOrderCommand& body,
    bool useCurrentUiSelection = false
);

std::string runtimePlayerCommunicationViewerOwner(const RuntimeProductionHostState& state);

bool runtimePlayerBeaconVisibleToLocal(const RuntimeProductionHostState& state,
    const RuntimePlayerBeacon& beacon);

std::optional<std::size_t> runtimePlayerChatSlot(const RuntimeProductionHostState& state,
    const std::string& owner);

std::uint8_t runtimePlayerChatSlotsMask(const RuntimeProductionHostState& state);

bool runtimePlayerChatRecipientsAvailable(const RuntimeProductionHostState& state);

std::uint8_t runtimePlayerOutgoingChatMask(const RuntimeProductionHostState& state);

std::vector<std::string> runtimePlayerDiplomacyOwners(const RuntimeProductionHostState& state);

bool validateRuntimePlayerCommunication(const RuntimeProductionHostState& state,
    const std::string& sender, const RuntimeMultiplayerGameplayPlayerCommunicationCommand& command,
    std::string& error);

void runtimePlayerAddChatLine(RuntimeProductionHostState& state, const std::string& owner,
    const std::string& text, bool allies = false);

bool applyRuntimePlayerCommunication(RuntimeProductionHostState& state, const std::string& sender,
    const RuntimeMultiplayerGameplayPlayerCommunicationCommand& command);

bool validateRuntimePlayerWaypointCommand(const RuntimeProductionHostState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayWaypointCommand& body,
    std::string& error
);

bool applyRuntimePlayerWaypointCommand(RuntimeProductionHostState& state,
    const std::string& owner, const RuntimeMultiplayerGameplayWaypointCommand& body
);

bool appendRuntimePlayerWaypointOrder(RuntimeProductionHostState& state, const std::string& owner,
    const RuntimeMultiplayerGameplayTargetOrderCommand& body
);

bool runtimeBuildingPlacementAnchorEligible(
    const RuntimeProductionHostState& state,
    const WorldObjectInstance& anchor,
    const std::string& placementOwner,
    bool placingWall = false
);

bool runtimeBuildingPlacementInRange(
    const RuntimeProductionHostState& state,
    const RuntimeBuildCatalogItem& item,
    const RuntimeCellPoint& cell,
    const std::string& placementOwner = {}
);

bool playRuntimeSfxOnDedicatedChannel(RuntimeProductionHostState& state, const std::string& path, int volume);

bool advanceRuntimeCommittedTick(RuntimeProductionHostState& state, int completedTick);

}
