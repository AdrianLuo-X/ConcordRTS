#include "ui/skirmish/RuntimeSkirmishSetup.h"
#include <algorithm>
#include <utility>

void applyRuntimeSkirmishSelection(RuntimeConfig& activeConfig,
    const RuntimeSkirmishSelection& selection) {
    activeConfig.skirmishLoadSavePath.clear();
    activeConfig.campaignLoadSavePath.clear();
    activeConfig.campaignSourceId.clear();
    activeConfig.defaultMapPath = selection.mapPath;
    activeConfig.mapStartingPointLimit = selection.mapStartingPointLimit;
    activeConfig.gameMode = selection.gameMode;
    activeConfig.gameModeRulesOverride = selection.gameModeRulesOverride;
    activeConfig.playerCountry = selection.playerCountry;
    activeConfig.playerColor = selection.playerColor;
    activeConfig.loadingCountryDisplayName =
        selection.playerCountryDisplayName;
    activeConfig.loadingCountryBrief = selection.playerCountryBrief;
    activeConfig.loadingScreenAssetId =
        selection.loadingScreenAssetId;
    activeConfig.startingCredits = selection.startingCredits;
    activeConfig.startingUnits = selection.unitCount;
    activeConfig.gameSpeed = std::clamp(selection.gameSpeed, 0, 6);
    activeConfig.shortGame = selection.shortGame;
    activeConfig.mcvRepacks = selection.mcvRepacks;
    activeConfig.crates = selection.crates;
    activeConfig.superweapons = selection.superweapons;
    activeConfig.buildOffAlly = selection.buildOffAlly;
    activeConfig.computerAiEnabled = selection.computerAiEnabled;
    activeConfig.noCooldown = selection.noCooldown && !activeConfig.multiplayerActive;
    activeConfig.playerSlots.clear();
    activeConfig.playerSlots.reserve(selection.slots.size());
    for (const RuntimeSkirmishPlayerSlot& slot : selection.slots) {
        RuntimeConfig::PlayerSlot configured;
        configured.enabled = slot.enabled;
        configured.human = slot.human;
        configured.name = slot.name;
        configured.country = slot.country;
        configured.color = slot.color;
        configured.team = slot.team;
        configured.difficulty = slot.difficulty;
        configured.startPosition = std::max(0, slot.startPosition);
        activeConfig.playerSlots.push_back(std::move(configured));
    }

    // Game speed controls only wall-clock pacing. The fixed simulation
    // rate is match authority and must remain stable for save/replay
    // clocks and authored millisecond-to-tick conversions.
}
