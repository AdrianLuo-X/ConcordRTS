#pragma once

#include "runtime/campaign/RuntimeCampaignDataSource.h"
#include "runtime/config/RuntimeConfig.h"
#include "runtime/contracts/RuntimeCampaignSelection.h"

#include <algorithm>

namespace concord::runtime::campaign {

inline void applyRuntimeCampaignSelection(
    RuntimeConfig& activeConfig,
    const RuntimeCampaignSelection& selection) {
    activeConfig.skirmishLoadSavePath.clear();
    activeConfig.defaultMapPath = selection.mapPath;
    activeConfig.mapStartingPointLimit = 0;
    activeConfig.gameMode = "CampaignMission";
    activeConfig.noCooldown = false;
    activeConfig.gameModeRulesOverride.clear();
    activeConfig.campaignId = selection.campaignId;
    activeConfig.campaignMissionId = selection.missionId;
    activeConfig.campaignSourceId =
        normalizeRuntimeCampaignSourceId(selection.sourceId);
    activeConfig.campaignDifficulty = selection.difficulty;
    activeConfig.campaignPlayerHouse = selection.playerHouse;
    activeConfig.campaignNextMissionId = selection.nextMissionId;
    activeConfig.campaignIntroVideoPath = selection.introVideoPath;
    activeConfig.campaignPostScoreVideoPath =
        selection.postScoreVideoPath;
    activeConfig.campaignBriefingKey = selection.briefingKey;
    activeConfig.campaignLoadSavePath = selection.loadedFromSave
        ? selection.savePath
        : "";
    activeConfig.campaignSavePath = selection.savePath;
    activeConfig.playerCountry = selection.playerCountry;
    activeConfig.playerColor = "Gold";
    activeConfig.loadingCountryDisplayName =
        selection.playerCountryDisplayName;
    activeConfig.loadingCountryBrief = selection.playerCountryBrief;
    activeConfig.loadingScreenAssetId = selection.loadingScreenAssetId;
    // Campaign maps own their initial buildings, units, ownership and
    // trigger-created reinforcements. No skirmish MCV, random opponent,
    // crate or short-game bootstrap is allowed to enter this mode.
    activeConfig.startingCredits = std::max(0, selection.mission.startingCredits);
    activeConfig.startingUnits = 0;
    activeConfig.gameSpeed = 3;
    activeConfig.shortGame = false;
    activeConfig.mcvRepacks = false;
    activeConfig.crates = false;
    activeConfig.superweapons = true;
    activeConfig.buildOffAlly = false;
    activeConfig.computerAiEnabled = true;
    activeConfig.playerSlots.clear();
}

} // namespace concord::runtime::campaign
