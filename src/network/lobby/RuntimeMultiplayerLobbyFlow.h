#pragma once

#include "network/protocol/RuntimeMultiplayerLaunchConfig.h"
#include "network/lobby/RuntimeMultiplayerLobbyModel.h"
#include "network/lockstep/RuntimeMultiplayerRuntimeBridge.h"
#include "network/snapshots/RuntimeMultiplayerResumeSave.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct RuntimeMultiplayerLobbyFlowPollResult {
    bool succeeded = true;
    bool lobbyChanged = false;
    bool matchStarted = false;
    std::vector<RuntimeMultiplayerLanEvent> events;
    std::string error;
};

// Player-flow orchestration shared by the LAN browser and direct Internet
// connection screens.  It keeps draft editing, canonical room publication,
// ready gating, local map verification, and launch projection in one place.
class RuntimeMultiplayerLobbyFlow {
public:
    explicit RuntimeMultiplayerLobbyFlow(
        RuntimeMultiplayerLockstepConfig lockstepConfig = {}
    );
    ~RuntimeMultiplayerLobbyFlow() = default;

    RuntimeMultiplayerLobbyFlow(RuntimeMultiplayerLobbyFlow&&) noexcept =
        default;
    RuntimeMultiplayerLobbyFlow& operator=(
        RuntimeMultiplayerLobbyFlow&&
    ) noexcept = default;
    RuntimeMultiplayerLobbyFlow(const RuntimeMultiplayerLobbyFlow&) = delete;
    RuntimeMultiplayerLobbyFlow& operator=(
        const RuntimeMultiplayerLobbyFlow&
    ) = delete;

    bool startHost(
        RuntimeMultiplayerLanHostOptions networkOptions,
        const RuntimeMultiplayerLobbyHostCreateOptions& lobbyOptions,
        std::string& errorMessage,
        const RuntimeMultiplayerResumeSave* resumeSave = nullptr
    );
    bool startBrowsing(
        std::uint16_t discoveryPort,
        std::string& errorMessage
    );
    bool join(
        RuntimeMultiplayerLanJoinOptions options,
        std::string& errorMessage
    );
    bool sendDiscoveryProbe(
        const std::string& targetAddress,
        std::string& errorMessage
    );
    std::vector<RuntimeLanRoomAdvertisement> discoveredRooms(
        std::uint64_t maximumAgeMilliseconds = 5000
    ) const;

    RuntimeMultiplayerLobbyFlowPollResult poll(
        int transportTimeoutMilliseconds = 0
    );

    RuntimeMultiplayerLobbyEditResult setMapAndRules(
        const RuntimeMultiplayerMatchMap& map,
        const RuntimeMultiplayerMatchRules& rules,
        std::string& publishError
    );
    RuntimeMultiplayerLobbyEditResult setSlot(
        std::size_t slotIndex,
        const RuntimeMultiplayerMatchSlot& slot,
        std::string& publishError
    );
    RuntimeMultiplayerLobbyEditResult setSlotController(
        std::size_t slotIndex,
        RuntimeMultiplayerMatchSlotController controller,
        std::string_view humanPeerId,
        std::string& publishError
    );
    RuntimeMultiplayerLobbyEditResult setSlotOptions(
        std::size_t slotIndex,
        std::string countryId,
        std::string colorId,
        std::string teamId,
        std::string difficultyId,
        std::uint8_t startPosition,
        std::string& publishError
    );
    RuntimeMultiplayerLanOperationResult requestLocalSlotOptions(
        std::string countryId,
        std::string colorId,
        std::string teamId,
        std::string difficultyId,
        std::uint8_t startPosition
    );
    RuntimeMultiplayerLanOperationResult kickPeer(
        const std::string& peerId,
        std::string reason = "removed by the host"
    );

    // Reads the selected local file and proves it is exactly the canonical map
    // published by the host.  Any subsequent host map edit invalidates this
    // proof and clears local ready state before another Ready=true is allowed.
    bool verifyLocalMap(
        const std::filesystem::path& localMapPath,
        std::string& errorMessage
    );
    bool localMapVerified() const;
    // Called after existing local map lookup (including random regeneration)
    // fails. Repeated calls observe one request; no partial file is published.
    bool downloadMissingMap(const std::filesystem::path& localDataRoot,
        std::filesystem::path& output, std::string& error);
    const std::filesystem::path& verifiedLocalMapPath() const;

    RuntimeMultiplayerLanOperationResult setReady(bool ready);
    RuntimeMultiplayerLanOperationResult startMatch(int startTick = 1);

    bool makeLaunchConfig(
        const RuntimeConfig& baseConfig,
        RuntimeMultiplayerLaunchConfigResult& result,
        std::string& errorMessage
    ) const;

    bool initialized() const;
    bool hostAuthoritative() const;
    bool canonicalSetupPublished() const;
    bool canStart(std::string& errorMessage) const;
    bool resuming() const;
    bool resumeReady(std::string& errorMessage) const;
    const RuntimeMultiplayerLobbyModel& lobby() const;
    RuntimeMultiplayerRuntimeBridge* bridge();
    const RuntimeMultiplayerRuntimeBridge* bridge() const;
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> takeRunningBridge(
        std::string& errorMessage
    );
    void close();

private:
    bool createFreshBridge(std::string& errorMessage);
    bool synchronizeLobbyFromSession(
        bool& changed,
        std::string& errorMessage
    );
    bool publishHostDraft(bool& published, std::string& errorMessage);
    bool afterHostEdit(
        const RuntimeMultiplayerLobbyEditResult& edit,
        std::string& publishError
    );
    bool handleLobbySlotOptionRequests(
        const std::vector<RuntimeMultiplayerLanEvent>& events,
        bool& changed,
        std::string& errorMessage
    );
    void invalidateMapVerificationIfChanged();
    std::vector<std::string> readyPeerIds() const;

    RuntimeMultiplayerLockstepConfig lockstepConfig_;
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> bridge_;
    RuntimeMultiplayerLobbyModel lobby_;
    std::filesystem::path verifiedMapPath_;
    std::string verifiedCanonicalMapId_;
    std::uint64_t verifiedMapFingerprint_ = 0;
    std::uint64_t verifiedSetupRevision_ = 0;
    std::optional<RuntimeMultiplayerMatchSetup> resumeSetup_;
    bool resumeManifestVerified_ = false;
};
