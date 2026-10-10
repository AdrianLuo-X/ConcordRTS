#pragma once

#include "network/internet/RuntimeInternetLobbyFlow.h"
#include "network/transport/RuntimeInternetServiceTransport.h"
#include "ui/network/RuntimeMultiplayerMenu.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// User-owned Internet service configuration. Certificate pinning is an
// optional additional restriction; the production transport always retains
// Windows system-chain, hostname, and revocation validation.
struct RuntimeInternetMultiplayerServiceConfig {
    std::string serviceAddress;
    std::uint16_t servicePort = 0U;
    std::string serverName;
    std::string serviceId;
    std::string peerId;
    std::vector<std::uint8_t> credential;
    std::optional<RuntimeInternetTlsSha256> leafCertificateSha256;

    bool operator==(
        const RuntimeInternetMultiplayerServiceConfig&
    ) const = default;
};

// Every correlation value and the room identity are generated in one
// BCryptGenRandom-backed transaction before a UI action mutates networking.
struct RuntimeInternetMultiplayerMenuEntropy {
    std::uint64_t queryTransactionId = 0U;
    std::uint64_t hostTransactionId = 0U;
    std::uint64_t joinTransactionId = 0U;
    std::uint64_t matchRequestId = 0U;
    std::uint64_t matchActivationTransactionId = 0U;
    std::uint64_t matchHostTransactionId = 0U;
    std::uint64_t matchJoinTransactionId = 0U;
    std::uint64_t roomGeneration = 0U;
    std::uint64_t gameplayRandomSeed = 0U;
    std::array<std::uint8_t, 16U> roomIdBytes{};

    bool operator==(
        const RuntimeInternetMultiplayerMenuEntropy&
    ) const = default;
};

struct RuntimeInternetMultiplayerRoomRow {
    std::string roomId;
    std::string text;
    bool joinable = false;

    bool operator==(
        const RuntimeInternetMultiplayerRoomRow&
    ) const = default;
};

struct RuntimeInternetMultiplayerRosterRow {
    std::string peerId;
    std::string text;
    bool local = false;
    bool kickable = false;

    bool operator==(
        const RuntimeInternetMultiplayerRosterRow&
    ) const = default;
};

// Strict parser for the [Internet] section. All six required keys must occur
// exactly once. Unknown or duplicate keys in the section fail closed.
bool parseRuntimeInternetMultiplayerServiceConfigText(
    std::string_view iniText,
    RuntimeInternetMultiplayerServiceConfig& config,
    std::string& errorMessage
);

bool loadRuntimeInternetMultiplayerServiceConfig(
    const std::filesystem::path& path,
    RuntimeInternetMultiplayerServiceConfig& config,
    std::string& errorMessage
);

bool makeRuntimeInternetMultiplayerTransportConfig(
    const RuntimeInternetMultiplayerServiceConfig& service,
    RuntimeInternetServiceTransportConfig& transport,
    std::string& errorMessage
);

bool generateRuntimeInternetMultiplayerMenuEntropy(
    RuntimeInternetMultiplayerMenuEntropy& entropy,
    std::string& errorMessage
);

std::string runtimeInternetMultiplayerContentFingerprint(
    const RuntimeMultiplayerCompatibility& compatibility
);

bool makeRuntimeInternetMultiplayerQueryOptions(
    const RuntimeInternetMultiplayerServiceConfig& service,
    const RuntimeMultiplayerCompatibility& compatibility,
    std::uint64_t transactionId,
    RuntimeInternetLobbyQueryOptions& options,
    std::string& errorMessage
);

bool makeRuntimeInternetMultiplayerHostOptions(
    const RuntimeInternetMultiplayerServiceConfig& service,
    const RuntimeMultiplayerCompatibility& compatibility,
    const RuntimeMultiplayerHostSelectionProjection& selection,
    std::string roomName,
    std::string password,
    const RuntimeInternetMultiplayerMenuEntropy& entropy,
    RuntimeInternetLobbyHostOptions& options,
    std::string& errorMessage,
    std::shared_ptr<const RuntimeMultiplayerResumeSave> resumeSave = {}
);

bool makeRuntimeInternetMultiplayerJoinOptions(
    const RuntimeInternetMultiplayerServiceConfig& service,
    const RuntimeMultiplayerCompatibility& compatibility,
    const RuntimeInternetRoomRecord& room,
    std::string displayName,
    std::string password,
    bool spectator,
    std::uint64_t transactionId,
    RuntimeInternetLobbyJoinOptions& options,
    std::string& errorMessage
);

bool makeRuntimeInternetMultiplayerMatchmakingOptions(
    const RuntimeInternetMultiplayerServiceConfig& service,
    const RuntimeMultiplayerCompatibility& compatibility,
    const RuntimeMultiplayerHostSelectionProjection& selection,
    std::string displayName,
    std::int32_t skillRating,
    const RuntimeInternetMultiplayerMenuEntropy& entropy,
    RuntimeInternetLobbyMatchmakingOptions& options,
    std::string& errorMessage
);

std::vector<RuntimeInternetMultiplayerRoomRow>
projectRuntimeInternetMultiplayerRoomRows(
    std::span<const RuntimeInternetRoomRecord> rooms,
    bool joiningAsSpectator = false
);

std::vector<RuntimeInternetMultiplayerRosterRow>
projectRuntimeInternetMultiplayerRosterRows(
    std::span<const RuntimeMultiplayerRoomMember> members,
    const RuntimeMultiplayerMatchSetup* setup,
    std::string_view localPeerId,
    bool localPeerIsHost
);

std::string projectRuntimeInternetMultiplayerConnectionStatus(
    bool coordinatorAuthenticated,
    RuntimeInternetLobbyFlowMode mode,
    RuntimeInternetConnectionState connectionState
);

std::string projectRuntimeInternetCommunityProfileSummary(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetCommunityChannelRows(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetCommunityChatRows(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetCommunityParticipantRows(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetCommunityFriendRows(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetCommunityLadderRows(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::string projectRuntimeInternetCommunityClanSummary(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetCommunityClanMemberRows(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetCommunityClanInvitationRows(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetCommunityClanLadderRows(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetCommunityNewsRows(
    const RuntimeInternetCommunitySnapshot& snapshot
);

std::vector<std::string> projectRuntimeInternetRankedHistoryRows(
    std::span<const RuntimeInternetRankedRecord> records, std::string_view localPeerId);
std::string projectRuntimeInternetRankedHistoryDetail(
    const RuntimeInternetRankedRecord& record, std::string_view localPeerId);

// Modal Win32 Internet front-end. It has no direct-connect path: all control
// and gameplay traffic uses the configured outbound TLS service and its
// authenticated TCP relay.
RuntimeMultiplayerMenuResult runRuntimeInternetMultiplayerMenu(
    const RuntimeConfig& baseConfig,
    void* hostWindow = nullptr
);

int runRuntimeInternetMultiplayerMenuUiSmoke(
    const RuntimeConfig& baseConfig,
    void* hostWindow = nullptr
);

int runRuntimeInternetMultiplayerMenuSkinWindowSmoke(
    const RuntimeConfig& baseConfig,
    const std::string& outputPath
);
