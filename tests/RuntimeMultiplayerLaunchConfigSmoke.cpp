#include "RuntimeMultiplayerLaunchConfig.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr
            << "Runtime multiplayer launch config smoke failure: "
            << message << '\n';
    }
    return condition;
}

RuntimeMultiplayerMatchMember member(
    std::string peerId,
    std::string displayName,
    RuntimeMultiplayerMatchMemberIdentity identity =
        RuntimeMultiplayerMatchMemberIdentity::Combatant
) {
    RuntimeMultiplayerMatchMember value;
    value.peerId = std::move(peerId);
    value.displayName = std::move(displayName);
    value.identity = identity;
    return value;
}

RuntimeMultiplayerMatchSlot activeSlot(
    std::uint8_t index,
    RuntimeMultiplayerMatchSlotController controller,
    std::string peerId,
    std::string country,
    std::string color,
    std::string team,
    std::string difficulty,
    std::uint8_t startPosition
) {
    RuntimeMultiplayerMatchSlot slot;
    slot.slotIndex = index;
    slot.controller = controller;
    slot.peerId = std::move(peerId);
    slot.countryId = std::move(country);
    slot.colorId = std::move(color);
    slot.teamId = std::move(team);
    slot.difficultyId = std::move(difficulty);
    slot.startPosition = startPosition;
    return slot;
}

RuntimeMultiplayerMatchSlot closedSlot(std::uint8_t index) {
    RuntimeMultiplayerMatchSlot slot;
    slot.slotIndex = index;
    return slot;
}

RuntimeMultiplayerMatchSetup representativeSetup() {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 42U;
    setup.hostPeerId = "host-peer";
    setup.map.canonicalId = "maps/official/two.map";
    setup.map.contentFingerprint = 0x1020304050607080ULL;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x8877665544332211ULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 12345;
    setup.rules.startingUnits = 7;
    setup.rules.techLevel = 9;
    setup.rules.gameSpeed = 2;
    setup.rules.shortGame = true;
    setup.rules.mcvRepacks = false;
    setup.rules.crates = true;
    setup.rules.superweapons = false;
    setup.rules.buildOffAlly = true;
    setup.members = {
        member("guest-peer", "Guest Commander"),
        member("host-peer", "Host Commander"),
        member(
            "watch-peer",
            "Observer",
            RuntimeMultiplayerMatchMemberIdentity::Spectator
        )
    };
    setup.slots = {
        activeSlot(
            0U,
            RuntimeMultiplayerMatchSlotController::Human,
            "host-peer",
            "americans",
            "gold",
            "one",
            "medium",
            1U
        ),
        activeSlot(
            1U,
            RuntimeMultiplayerMatchSlotController::Computer,
            "",
            "russians",
            "red",
            "two",
            "hard",
            2U
        ),
        activeSlot(
            2U,
            RuntimeMultiplayerMatchSlotController::Human,
            "guest-peer",
            "korea",
            "blue",
            "one",
            "easy",
            0U
        ),
        closedSlot(3U)
    };
    return setup;
}

RuntimeConfig poisonedBaseConfig() {
    RuntimeConfig config;
    config.configPath = "machine/config.ini";
    config.resourceRootPath = "machine/resources";
    config.localDataRootPath = "machine/local-data";
    config.modRootPath = "machine/mod";
    config.defaultMapPath = "poison/map.map";
    config.windowWidth = 1600;
    config.windowHeight = 900;
    config.fixedTickHz = 11;
    config.multiplayerActive = false;
    config.multiplayerSetupHash = 0x1111U;
    config.multiplayerCanonicalMapId = "poison-map";
    config.multiplayerMapContentFingerprint = 0x3333U;
    config.multiplayerTimelineEpoch = 99U;
    config.simulationRandomSeed = 0x2222U;
    config.targetFps = 144;
    config.showMainMenu = true;
    config.showSkirmishMenu = true;
    config.gameMode = "poison-mode";
    config.gameModeRulesOverride = "poison-rules.ini";
    config.campaignId = "poison-campaign";
    config.campaignMissionId = "poison-mission";
    config.campaignDifficulty = "Poison";
    config.campaignPlayerHouse = "PoisonHouse";
    config.campaignNextMissionId = "poison-next";
    config.campaignIntroVideoPath = "poison-intro.bik";
    config.campaignPostScoreVideoPath = "poison-score.bik";
    config.campaignBriefingKey = "POISON_BRIEF";
    config.campaignLoadSavePath = "poison-load.sav";
    config.campaignSavePath = "poison-save.sav";
    config.skirmishLoadSavePath = "poison-skirmish.sav";
    config.playerCountry = "poison-country";
    config.playerColor = "poison-color";
    config.loadingCountryDisplayName = L"Poison Display";
    config.loadingCountryBrief = L"Poison Brief";
    config.loadingScreenAssetId = "poison-loading";
    config.startingCredits = 1;
    config.startingUnits = 2;
    config.techLevel = 3;
    config.gameSpeed = 4;
    config.shortGame = false;
    config.mcvRepacks = true;
    config.crates = false;
    config.superweapons = true;
    config.buildOffAlly = false;
    RuntimeConfig::PlayerSlot poisonSlot;
    poisonSlot.enabled = true;
    poisonSlot.human = true;
    poisonSlot.peerId = "poison-peer";
    poisonSlot.local = true;
    poisonSlot.name = "Poison Player";
    poisonSlot.country = "poison";
    poisonSlot.color = "poison";
    poisonSlot.team = "poison";
    poisonSlot.difficulty = "poison";
    poisonSlot.startPosition = 8;
    config.playerSlots = {poisonSlot};
    config.runtimeReplayEventOutputPath = "machine/replay.txt";
    return config;
}

bool samePlayerSlot(
    const RuntimeConfig::PlayerSlot& left,
    const RuntimeConfig::PlayerSlot& right
) {
    return left.enabled == right.enabled
        && left.human == right.human
        && left.peerId == right.peerId
        && left.local == right.local
        && left.name == right.name
        && left.country == right.country
        && left.color == right.color
        && left.team == right.team
        && left.difficulty == right.difficulty
        && left.startPosition == right.startPosition;
}

bool sameRuntimeConfig(const RuntimeConfig& left, const RuntimeConfig& right) {
    if (left.configPath != right.configPath
        || left.resourceRootPath != right.resourceRootPath
        || left.localDataRootPath != right.localDataRootPath
        || left.modRootPath != right.modRootPath
        || left.defaultMapPath != right.defaultMapPath
        || left.windowWidth != right.windowWidth
        || left.windowHeight != right.windowHeight
        || left.fixedTickHz != right.fixedTickHz
        || left.multiplayerActive != right.multiplayerActive
        || left.multiplayerSetupHash != right.multiplayerSetupHash
        || left.multiplayerCanonicalMapId != right.multiplayerCanonicalMapId
        || left.multiplayerMapContentFingerprint
            != right.multiplayerMapContentFingerprint
        || left.multiplayerTimelineEpoch != right.multiplayerTimelineEpoch
        || left.simulationRandomSeed != right.simulationRandomSeed
        || left.targetFps != right.targetFps
        || left.showMainMenu != right.showMainMenu
        || left.showSkirmishMenu != right.showSkirmishMenu
        || left.gameMode != right.gameMode
        || left.gameModeRulesOverride != right.gameModeRulesOverride
        || left.campaignId != right.campaignId
        || left.campaignMissionId != right.campaignMissionId
        || left.campaignDifficulty != right.campaignDifficulty
        || left.campaignPlayerHouse != right.campaignPlayerHouse
        || left.campaignNextMissionId != right.campaignNextMissionId
        || left.campaignIntroVideoPath != right.campaignIntroVideoPath
        || left.campaignPostScoreVideoPath
            != right.campaignPostScoreVideoPath
        || left.campaignBriefingKey != right.campaignBriefingKey
        || left.campaignLoadSavePath != right.campaignLoadSavePath
        || left.campaignSavePath != right.campaignSavePath
        || left.skirmishLoadSavePath != right.skirmishLoadSavePath
        || left.playerCountry != right.playerCountry
        || left.playerColor != right.playerColor
        || left.loadingCountryDisplayName
            != right.loadingCountryDisplayName
        || left.loadingCountryBrief != right.loadingCountryBrief
        || left.loadingScreenAssetId != right.loadingScreenAssetId
        || left.startingCredits != right.startingCredits
        || left.startingUnits != right.startingUnits
        || left.techLevel != right.techLevel
        || left.gameSpeed != right.gameSpeed
        || left.shortGame != right.shortGame
        || left.mcvRepacks != right.mcvRepacks
        || left.crates != right.crates
        || left.superweapons != right.superweapons
        || left.buildOffAlly != right.buildOffAlly
        || left.runtimeReplayEventOutputPath
            != right.runtimeReplayEventOutputPath
        || left.playerSlots.size() != right.playerSlots.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.playerSlots.size(); ++index) {
        if (!samePlayerSlot(left.playerSlots[index], right.playerSlots[index])) {
            return false;
        }
    }
    return true;
}

bool sameResult(
    const RuntimeMultiplayerLaunchConfigResult& left,
    const RuntimeMultiplayerLaunchConfigResult& right
) {
    return sameRuntimeConfig(left.config, right.config)
        && left.authority == right.authority
        && left.fullRoomSetupHash == right.fullRoomSetupHash
        && left.localPeerIsHost == right.localPeerIsHost
        && left.localPeerIsSpectator == right.localPeerIsSpectator;
}

bool rejectedAtomically(
    const RuntimeMultiplayerMatchSetup& setup,
    const std::string& localPeerId,
    const std::string& localMapPath,
    std::uint64_t actualFingerprint,
    const RuntimeConfig& base,
    std::uint64_t epoch,
    RuntimeMultiplayerLaunchConfigResult& output,
    const std::string& label
) {
    const RuntimeConfig baseBefore = base;
    const RuntimeMultiplayerLaunchConfigResult outputBefore = output;
    std::string error;
    return expect(
        !makeRuntimeMultiplayerLaunchConfig(
            setup,
            localPeerId,
            localMapPath,
            actualFingerprint,
            base,
            output,
            error,
            epoch
        )
            && sameRuntimeConfig(base, baseBefore)
            && sameResult(output, outputBefore)
            && !error.empty(),
        label + " was not rejected atomically: " + error
    );
}

bool testHostClientAndSpectatorViews() {
    bool success = true;
    const RuntimeMultiplayerMatchSetup setup = representativeSetup();
    const RuntimeConfig base = poisonedBaseConfig();
    const RuntimeConfig baseBefore = base;
    std::string error;
    RuntimeMultiplayerLaunchConfigResult host;
    success &= expect(
        makeRuntimeMultiplayerLaunchConfig(
            setup,
            "host-peer",
            "D:\\machine-a\\maps\\two.map",
            setup.map.contentFingerprint,
            base,
            host,
            error,
            7U
        ),
        "host launch config failed: " + error
    );
    success &= expect(
        sameRuntimeConfig(base, baseBefore),
        "successful adapter changed base RuntimeConfig"
    );

    std::uint64_t expectedGameplayFingerprint = 0U;
    std::uint64_t expectedRoomHash = 0U;
    success &= expect(
        runtimeMultiplayerGameplaySetupFingerprint(
            setup,
            expectedGameplayFingerprint,
            error
        )
            && runtimeMultiplayerMatchSetupHash(
                setup,
                expectedRoomHash,
                error
            ),
        "expected setup fingerprints failed: " + error
    );
    const RuntimeConfig& config = host.config;
    success &= expect(
        config.configPath == base.configPath
            && config.resourceRootPath == base.resourceRootPath
            && config.localDataRootPath == base.localDataRootPath
            && config.modRootPath == base.modRootPath
            && config.windowWidth == base.windowWidth
            && config.windowHeight == base.windowHeight
            && config.targetFps == base.targetFps
            && !config.showMainMenu
            && !config.showSkirmishMenu
            && config.runtimeReplayEventOutputPath
                == base.runtimeReplayEventOutputPath,
        "process-local resource/window preferences were not copied"
    );
    success &= expect(
        config.defaultMapPath == "D:\\machine-a\\maps\\two.map"
            && config.multiplayerActive
            && config.multiplayerSetupHash == expectedGameplayFingerprint
            && config.multiplayerCanonicalMapId == setup.map.canonicalId
            && config.multiplayerMapContentFingerprint
                == setup.map.contentFingerprint
            && config.multiplayerTimelineEpoch == 7U
            && config.simulationRandomSeed == setup.rules.randomSeed
            && host.authority.gameplaySetupFingerprint
                == expectedGameplayFingerprint
            && host.fullRoomSetupHash == expectedRoomHash
            && host.fullRoomSetupHash != config.multiplayerSetupHash,
        "map/epoch/fingerprint multiplayer authority was not projected"
    );
    success &= expect(
        config.gameMode == setup.rules.gameMode
            && config.gameModeRulesOverride == setup.rules.rulesOverride
            && config.fixedTickHz == setup.rules.fixedTickHz
            && config.startingCredits == setup.rules.startingCredits
            && config.startingUnits == setup.rules.startingUnits
            && config.techLevel == setup.rules.techLevel
            && config.gameSpeed == setup.rules.gameSpeed
            && config.shortGame == setup.rules.shortGame
            && config.mcvRepacks == setup.rules.mcvRepacks
            && config.crates == setup.rules.crates
            && config.superweapons == setup.rules.superweapons
            && config.buildOffAlly == setup.rules.buildOffAlly,
        "one or more gameplay rule fields were not projected"
    );
    success &= expect(
        config.campaignId.empty()
            && config.campaignMissionId.empty()
            && config.campaignDifficulty.empty()
            && config.campaignPlayerHouse.empty()
            && config.campaignNextMissionId.empty()
            && config.campaignIntroVideoPath.empty()
            && config.campaignPostScoreVideoPath.empty()
            && config.campaignBriefingKey.empty()
            && config.campaignLoadSavePath.empty()
            && config.campaignSavePath.empty()
            && config.skirmishLoadSavePath.empty()
            && config.loadingCountryDisplayName.empty()
            && config.loadingCountryBrief.empty()
            && config.loadingScreenAssetId.empty(),
        "campaign/skirmish load or presentation state was retained"
    );
    success &= expect(
        host.localPeerIsHost && !host.localPeerIsSpectator
            && config.playerCountry == "americans"
            && config.playerColor == "gold"
            && config.playerSlots.size() == 4U
            && config.playerSlots[0U].enabled
            && config.playerSlots[0U].human
            && config.playerSlots[0U].peerId == "host-peer"
            && config.playerSlots[0U].local
            && config.playerSlots[0U].name == "Host Commander"
            && config.playerSlots[0U].country == "americans"
            && config.playerSlots[0U].color == "gold"
            && config.playerSlots[0U].team == "one"
            && config.playerSlots[0U].difficulty == "medium"
            && config.playerSlots[0U].startPosition == 1,
        "host human slot/local presentation mapping is incomplete"
    );
    success &= expect(
        config.playerSlots[1U].enabled
            && !config.playerSlots[1U].human
            && config.playerSlots[1U].peerId.empty()
            && !config.playerSlots[1U].local
            && config.playerSlots[1U].name == "Computer 2"
            && config.playerSlots[1U].country == "russians"
            && config.playerSlots[1U].color == "red"
            && config.playerSlots[1U].team == "two"
            && config.playerSlots[1U].difficulty == "hard"
            && config.playerSlots[1U].startPosition == 2
            && config.playerSlots[2U].enabled
            && config.playerSlots[2U].human
            && config.playerSlots[2U].peerId == "guest-peer"
            && !config.playerSlots[2U].local
            && config.playerSlots[2U].name == "Guest Commander"
            && config.playerSlots[2U].country == "korea"
            && config.playerSlots[2U].color == "blue"
            && config.playerSlots[2U].team == "one"
            && config.playerSlots[2U].difficulty == "easy"
            && config.playerSlots[2U].startPosition == 0,
        "AI or remote-human slot mapping is incomplete"
    );
    success &= expect(
        !config.playerSlots[3U].enabled
            && !config.playerSlots[3U].human
            && config.playerSlots[3U].peerId.empty()
            && !config.playerSlots[3U].local
            && config.playerSlots[3U].name.empty()
            && config.playerSlots[3U].country.empty()
            && config.playerSlots[3U].color.empty()
            && config.playerSlots[3U].team.empty()
            && config.playerSlots[3U].difficulty.empty()
            && config.playerSlots[3U].startPosition == 0,
        "closed slot index/data was not preserved and cleared"
    );
    success &= expect(
        host.authority.map == setup.map
            && host.authority.rules == setup.rules
            && host.authority.slots.size() == setup.slots.size(),
        "authority projection lost map, rules, or slot shape"
    );
    for (std::size_t index = 0U;
        index < host.authority.slots.size();
        ++index) {
        const RuntimeMultiplayerLaunchAuthoritySlot& projected =
            host.authority.slots[index];
        const RuntimeMultiplayerMatchSlot& source = setup.slots[index];
        success &= expect(
            projected.slotIndex == source.slotIndex
                && projected.controller == source.controller
                && projected.enabled
                    == (source.controller
                        != RuntimeMultiplayerMatchSlotController::Closed)
                && projected.human
                    == (source.controller
                        == RuntimeMultiplayerMatchSlotController::Human)
                && projected.countryId == source.countryId
                && projected.colorId == source.colorId
                && projected.teamId == source.teamId
                && projected.difficultyId == source.difficultyId
                && projected.startPosition == source.startPosition,
            "authority slot projection differs at index "
                + std::to_string(index)
        );
    }

    RuntimeMultiplayerLaunchConfigResult guest;
    success &= expect(
        makeRuntimeMultiplayerLaunchConfig(
            setup,
            "guest-peer",
            "E:\\machine-b\\cache\\renamed-local.map",
            setup.map.contentFingerprint,
            base,
            guest,
            error,
            7U
        ),
        "client launch config failed: " + error
    );
    success &= expect(
        !guest.localPeerIsHost && !guest.localPeerIsSpectator
            && !guest.config.playerSlots[0U].local
            && guest.config.playerSlots[2U].local
            && guest.config.playerCountry == "korea"
            && guest.config.playerColor == "blue"
            && guest.config.defaultMapPath != host.config.defaultMapPath
            && guest.authority == host.authority
            && guest.config.multiplayerSetupHash
                == host.config.multiplayerSetupHash
            && guest.fullRoomSetupHash == host.fullRoomSetupHash,
        "local peer/path changed gameplay authority projection"
    );

    RuntimeMultiplayerLaunchConfigResult spectator;
    success &= expect(
        makeRuntimeMultiplayerLaunchConfig(
            setup,
            "watch-peer",
            "F:\\spectator\\two.map",
            setup.map.contentFingerprint,
            base,
            spectator,
            error
        ),
        "spectator launch config failed: " + error
    );
    const bool anySpectatorLocal = std::any_of(
        spectator.config.playerSlots.begin(),
        spectator.config.playerSlots.end(),
        [](const RuntimeConfig::PlayerSlot& slot) { return slot.local; }
    );
    success &= expect(
        spectator.localPeerIsSpectator
            && !spectator.localPeerIsHost
            && !anySpectatorLocal
            && spectator.config.multiplayerTimelineEpoch == 1U
            && spectator.config.playerCountry == "americans"
            && spectator.config.playerColor == "gold"
            && spectator.authority == host.authority
            && spectator.config.multiplayerSetupHash
                == host.config.multiplayerSetupHash,
        "spectator view was not local-free with canonical display fallback"
    );
    return success;
}

bool testCanonicalizationRebindingAndGameplayChanges() {
    bool success = true;
    const RuntimeMultiplayerMatchSetup setup = representativeSetup();
    const RuntimeConfig base = poisonedBaseConfig();
    std::string error;
    RuntimeMultiplayerLaunchConfigResult baseline;
    success &= expect(
        makeRuntimeMultiplayerLaunchConfig(
            setup,
            "host-peer",
            "local/a.map",
            setup.map.contentFingerprint,
            base,
            baseline,
            error
        ),
        "baseline launch failed: " + error
    );

    RuntimeMultiplayerMatchSetup noncanonical = setup;
    std::reverse(noncanonical.members.begin(), noncanonical.members.end());
    std::reverse(noncanonical.slots.begin(), noncanonical.slots.end());
    noncanonical.map.canonicalId = " MAPS\\OFFICIAL\\TWO.MAP ";
    noncanonical.rules.gameMode = " Battle ";
    noncanonical.rules.rulesOverride = " MPBATTLEMD.INI ";
    for (RuntimeMultiplayerMatchSlot& slot : noncanonical.slots) {
        if (!slot.countryId.empty()) slot.countryId[0U] =
            static_cast<char>(slot.countryId[0U] - 'a' + 'A');
        if (!slot.colorId.empty()) slot.colorId[0U] =
            static_cast<char>(slot.colorId[0U] - 'a' + 'A');
    }
    RuntimeMultiplayerLaunchConfigResult normalized;
    success &= expect(
        makeRuntimeMultiplayerLaunchConfig(
            noncanonical,
            "host-peer",
            "local/a.map",
            setup.map.contentFingerprint,
            base,
            normalized,
            error
        )
            && normalized.authority == baseline.authority
            && normalized.fullRoomSetupHash == baseline.fullRoomSetupHash,
        "adapter did not canonicalize setup before launch: " + error
    );

    RuntimeMultiplayerMatchSetup rebound = setup;
    rebound.revision = 88U;
    rebound.hostPeerId = "new-host";
    rebound.members = {
        member("new-guest", "Different Guest"),
        member("new-host", "Different Host"),
        member(
            "new-watch",
            "Different Observer",
            RuntimeMultiplayerMatchMemberIdentity::Spectator
        )
    };
    rebound.slots[0U].peerId = "new-host";
    rebound.slots[2U].peerId = "new-guest";
    RuntimeMultiplayerLaunchConfigResult reboundResult;
    success &= expect(
        makeRuntimeMultiplayerLaunchConfig(
            rebound,
            "new-host",
            "other-machine/two.map",
            rebound.map.contentFingerprint,
            base,
            reboundResult,
            error
        ),
        "peer-rebound launch failed: " + error
    );
    success &= expect(
        reboundResult.authority == baseline.authority
            && reboundResult.config.multiplayerSetupHash
                == baseline.config.multiplayerSetupHash
            && reboundResult.fullRoomSetupHash
                != baseline.fullRoomSetupHash
            && reboundResult.config.playerSlots[0U].peerId == "new-host"
            && reboundResult.config.playerSlots[0U].name
                == "Different Host",
        "room identity rebinding changed gameplay fingerprint/projection"
    );

    RuntimeMultiplayerMatchSetup gameplayChanged = setup;
    ++gameplayChanged.rules.fixedTickHz;
    RuntimeMultiplayerLaunchConfigResult changedResult;
    success &= expect(
        makeRuntimeMultiplayerLaunchConfig(
            gameplayChanged,
            "host-peer",
            "local/a.map",
            gameplayChanged.map.contentFingerprint,
            base,
            changedResult,
            error
        ),
        "gameplay-mutated launch failed: " + error
    );
    success &= expect(
        changedResult.config.fixedTickHz
                == gameplayChanged.rules.fixedTickHz
            && changedResult.config.multiplayerSetupHash
                != baseline.config.multiplayerSetupHash
            && changedResult.authority != baseline.authority,
        "gameplay field mutation did not change authority"
    );
    return success;
}

bool testFailuresAreAtomic() {
    bool success = true;
    const RuntimeMultiplayerMatchSetup valid = representativeSetup();
    const RuntimeConfig base = poisonedBaseConfig();
    std::string error;
    RuntimeMultiplayerLaunchConfigResult output;
    success &= expect(
        makeRuntimeMultiplayerLaunchConfig(
            valid,
            "host-peer",
            "local/two.map",
            valid.map.contentFingerprint,
            base,
            output,
            error
        ),
        "atomic seed launch failed: " + error
    );
    success &= rejectedAtomically(
        valid,
        "host-peer",
        "local/two.map",
        valid.map.contentFingerprint ^ 1U,
        base,
        1U,
        output,
        "wrong actual map fingerprint"
    );
    success &= rejectedAtomically(
        valid,
        "host-peer",
        "local/two.map",
        0U,
        base,
        1U,
        output,
        "zero actual map fingerprint"
    );
    success &= rejectedAtomically(
        valid,
        "missing-peer",
        "local/two.map",
        valid.map.contentFingerprint,
        base,
        1U,
        output,
        "missing local member"
    );
    success &= rejectedAtomically(
        valid,
        "host-peer",
        "local/two.map",
        valid.map.contentFingerprint,
        base,
        0U,
        output,
        "zero timeline epoch"
    );
    success &= rejectedAtomically(
        valid,
        "host-peer",
        "",
        valid.map.contentFingerprint,
        base,
        1U,
        output,
        "empty local map path"
    );

    RuntimeMultiplayerMatchSetup invalid = valid;
    invalid.rules.fixedTickHz = 0;
    success &= rejectedAtomically(
        invalid,
        "host-peer",
        "local/two.map",
        valid.map.contentFingerprint,
        base,
        1U,
        output,
        "invalid fixed tick"
    );
    invalid = valid;
    invalid.rules.randomSeed = 0U;
    success &= rejectedAtomically(
        invalid,
        "host-peer",
        "local/two.map",
        valid.map.contentFingerprint,
        base,
        1U,
        output,
        "zero simulation seed"
    );
    invalid = valid;
    invalid.map.contentFingerprint = 0U;
    success &= rejectedAtomically(
        invalid,
        "host-peer",
        "local/two.map",
        valid.map.contentFingerprint,
        base,
        1U,
        output,
        "zero setup map fingerprint"
    );
    invalid = valid;
    invalid.members.erase(invalid.members.begin());
    invalid.slots[1U] = closedSlot(1U);
    invalid.slots[2U] = closedSlot(2U);
    success &= rejectedAtomically(
        invalid,
        "host-peer",
        "local/two.map",
        valid.map.contentFingerprint,
        base,
        1U,
        output,
        "fewer than two combatants"
    );
    return success;
}

} // namespace

int main() {
    bool success = true;
    success &= testHostClientAndSpectatorViews();
    success &= testCanonicalizationRebindingAndGameplayChanges();
    success &= testFailuresAreAtomic();
    if (!success) return 1;

    std::cout
        << "Runtime multiplayer launch config smoke:"
        << " slots=human+ai+closed spectator=yes"
        << " authority=peer/path-independent atomic=yes\n";
    return 0;
}
