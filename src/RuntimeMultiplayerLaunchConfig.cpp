#include "RuntimeMultiplayerLaunchConfig.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace {

bool fail(std::string& errorMessage, std::string message) {
    errorMessage = std::move(message);
    return false;
}

bool validLocalMapPath(const std::string& path) {
    if (path.empty()) return false;
    return std::all_of(
        path.begin(),
        path.end(),
        [](const char character) {
            const unsigned char byte = static_cast<unsigned char>(character);
            return byte >= 0x20U && byte != 0x7fU;
        }
    );
}

void copyProcessLocalPreferences(
    const RuntimeConfig& base,
    RuntimeConfig& target
) {
    target.configPath = base.configPath;
    target.resourceRootPath = base.resourceRootPath;
    target.localDataRootPath = base.localDataRootPath;
    target.modRootPath = base.modRootPath;
    target.windowWidth = base.windowWidth;
    target.windowHeight = base.windowHeight;
    target.targetFps = base.targetFps;
    target.showMainMenu = false;
    target.showSkirmishMenu = false;
    target.runtimeReplayEventOutputPath = base.runtimeReplayEventOutputPath;
}

void clearCampaignAndLoadState(RuntimeConfig& config) {
    config.campaignId.clear();
    config.campaignMissionId.clear();
    config.campaignDifficulty.clear();
    config.campaignPlayerHouse.clear();
    config.campaignNextMissionId.clear();
    config.campaignIntroVideoPath.clear();
    config.campaignPostScoreVideoPath.clear();
    config.campaignBriefingKey.clear();
    config.campaignLoadSavePath.clear();
    config.campaignSavePath.clear();
    config.skirmishLoadSavePath.clear();
    config.loadingCountryDisplayName.clear();
    config.loadingCountryBrief.clear();
    config.loadingScreenAssetId.clear();
}

} // namespace

bool makeRuntimeMultiplayerLaunchConfig(
    const RuntimeMultiplayerMatchSetup& setup,
    std::string_view localPeerId,
    const std::string& resolvedLocalMapPath,
    std::uint64_t actualMapContentFingerprint,
    const RuntimeConfig& baseConfig,
    RuntimeMultiplayerLaunchConfigResult& result,
    std::string& errorMessage,
    std::uint64_t multiplayerTimelineEpoch
) {
    errorMessage.clear();
    if (multiplayerTimelineEpoch == 0U) {
        return fail(errorMessage, "multiplayer timeline epoch must be nonzero");
    }
    if (!validLocalMapPath(resolvedLocalMapPath)) {
        return fail(errorMessage, "resolved local multiplayer map path is invalid");
    }
    if (actualMapContentFingerprint == 0U) {
        return fail(errorMessage, "actual multiplayer map fingerprint is zero");
    }

    try {
        RuntimeMultiplayerMatchSetup canonical = setup;
        if (!canonicalizeRuntimeMultiplayerMatchSetup(
                canonical,
                errorMessage
            )) {
            return false;
        }
        if (!validateRuntimeMultiplayerMatchSetup(
                canonical,
                errorMessage
            )) {
            return false;
        }
        if (canonical.map.contentFingerprint
            != actualMapContentFingerprint) {
            return fail(
                errorMessage,
                "actual multiplayer map fingerprint does not match setup"
            );
        }

        RuntimeMultiplayerMatchLaunchDescription launch;
        if (!makeRuntimeMultiplayerMatchLaunchDescription(
                canonical,
                localPeerId,
                launch,
                errorMessage
            )) {
            return false;
        }
        std::uint64_t gameplayFingerprint = 0U;
        if (!runtimeMultiplayerGameplaySetupFingerprint(
                canonical,
                gameplayFingerprint,
                errorMessage
            )) {
            return false;
        }
        if (gameplayFingerprint == 0U) {
            return fail(errorMessage, "multiplayer gameplay fingerprint is zero");
        }

        RuntimeMultiplayerLaunchConfigResult candidate;
        copyProcessLocalPreferences(baseConfig, candidate.config);
        clearCampaignAndLoadState(candidate.config);
        candidate.config.defaultMapPath = resolvedLocalMapPath;
        candidate.config.fixedTickHz = launch.rules.fixedTickHz;
        candidate.config.multiplayerActive = true;
        candidate.config.noCooldown = false;
        candidate.config.multiplayerSetupHash = gameplayFingerprint;
        candidate.config.multiplayerCanonicalMapId = launch.map.canonicalId;
        candidate.config.multiplayerMapContentFingerprint =
            launch.map.contentFingerprint;
        candidate.config.multiplayerCooperativeScenarioFingerprint =
            launch.rules.cooperativeMission.scenarioFingerprint;
        candidate.config.multiplayerTimelineEpoch = multiplayerTimelineEpoch;
        candidate.config.simulationRandomSeed = launch.rules.randomSeed;
        candidate.config.gameMode = launch.rules.gameMode;
        candidate.config.gameModeRulesOverride =
            launch.rules.rulesOverride;
        candidate.config.startingCredits = launch.rules.startingCredits;
        candidate.config.startingUnits = launch.rules.startingUnits;
        candidate.config.techLevel = launch.rules.techLevel;
        candidate.config.gameSpeed = launch.rules.gameSpeed;
        candidate.config.shortGame = launch.rules.shortGame;
        candidate.config.mcvRepacks = launch.rules.mcvRepacks;
        candidate.config.crates = launch.rules.crates;
        candidate.config.superweapons = launch.rules.superweapons;
        candidate.config.buildOffAlly = launch.rules.buildOffAlly;
        candidate.config.playerSlots.reserve(launch.slots.size());

        candidate.authority.gameplaySetupFingerprint = gameplayFingerprint;
        candidate.authority.map = launch.map;
        candidate.authority.rules = launch.rules;
        candidate.authority.slots.reserve(launch.slots.size());

        const RuntimeMultiplayerMatchLaunchSlot* displaySelection = nullptr;
        std::size_t localSlotCount = 0U;
        for (const RuntimeMultiplayerMatchLaunchSlot& source : launch.slots) {
            if (source.slotIndex != candidate.config.playerSlots.size()) {
                return fail(
                    errorMessage,
                    "multiplayer launch slot index is not contiguous"
                );
            }

            RuntimeConfig::PlayerSlot target;
            target.enabled = source.enabled;
            target.human = source.human;
            target.peerId = source.human ? source.peerId : std::string{};
            target.local = source.local;
            target.country = source.countryId;
            target.color = source.colorId;
            target.team = source.teamId;
            target.difficulty = source.difficultyId;
            target.startPosition = static_cast<int>(source.startPosition);
            if (source.human) {
                target.name = source.displayName;
                if (displaySelection == nullptr) displaySelection = &source;
            } else if (source.enabled) {
                target.name = "Computer "
                    + std::to_string(
                        static_cast<unsigned int>(source.slotIndex) + 1U
                    );
            } else {
                target.name.clear();
            }
            if (source.local) {
                ++localSlotCount;
                displaySelection = &source;
            }
            candidate.config.playerSlots.push_back(std::move(target));

            RuntimeMultiplayerLaunchAuthoritySlot authoritySlot;
            authoritySlot.slotIndex = source.slotIndex;
            authoritySlot.controller = source.controller;
            authoritySlot.enabled = source.enabled;
            authoritySlot.human = source.human;
            authoritySlot.countryId = source.countryId;
            authoritySlot.colorId = source.colorId;
            authoritySlot.teamId = source.teamId;
            authoritySlot.difficultyId = source.difficultyId;
            authoritySlot.startPosition = source.startPosition;
            candidate.authority.slots.push_back(std::move(authoritySlot));
        }

        if (launch.localPeerIsSpectator) {
            if (localSlotCount != 0U) {
                return fail(
                    errorMessage,
                    "spectator launch unexpectedly owns a local slot"
                );
            }
        } else if (localSlotCount != 1U) {
            return fail(
                errorMessage,
                "combatant launch must own exactly one local slot"
            );
        }
        if (displaySelection == nullptr) {
            return fail(
                errorMessage,
                "multiplayer launch has no human display combatant"
            );
        }
        candidate.config.playerCountry = displaySelection->countryId;
        candidate.config.playerColor = displaySelection->colorId;
        candidate.fullRoomSetupHash = launch.setupHash;
        candidate.localPeerIsHost = launch.localPeerIsHost;
        candidate.localPeerIsSpectator = launch.localPeerIsSpectator;

        result = std::move(candidate);
        errorMessage.clear();
        return true;
    } catch (...) {
        return fail(
            errorMessage,
            "multiplayer launch config could not be constructed"
        );
    }
}
