#pragma once

#include "network/session/RuntimeLanTransport.h"
#include "network/internet/RuntimeInternetRankedMatch.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::uint16_t runtimeInternetCommunityWireVersion = 3U;
inline constexpr std::size_t runtimeInternetCommunityMaximumMatchHistory = 32U;
inline constexpr std::uint16_t runtimeInternetCommunityPacketType = 0x0103U;
inline constexpr std::size_t runtimeInternetCommunityMaximumPacketBytes =
    64U * 1024U;
inline constexpr std::size_t runtimeInternetCommunityMaximumDisplayNameBytes =
    24U;
inline constexpr std::size_t runtimeInternetCommunityMaximumChannelIdBytes =
    32U;
inline constexpr std::size_t runtimeInternetCommunityMaximumChannelTitleBytes =
    48U;
inline constexpr std::size_t runtimeInternetCommunityMaximumChannelTopicBytes =
    160U;
inline constexpr std::size_t runtimeInternetCommunityMaximumChatBytes = 256U;
inline constexpr std::size_t runtimeInternetCommunityMaximumStatusBytes = 256U;
inline constexpr std::size_t runtimeInternetCommunityMaximumFriends = 128U;
inline constexpr std::size_t runtimeInternetCommunityMaximumChannels = 16U;
inline constexpr std::size_t runtimeInternetCommunityMaximumParticipants = 256U;
inline constexpr std::size_t runtimeInternetCommunityMaximumChatHistory = 64U;
inline constexpr std::size_t runtimeInternetCommunityMaximumLadderEntries = 100U;
inline constexpr std::size_t runtimeInternetCommunityMaximumNewsItems = 32U;
inline constexpr std::size_t runtimeInternetCommunityMaximumClanIdBytes = 8U;
inline constexpr std::size_t runtimeInternetCommunityMaximumClanNameBytes = 32U;
inline constexpr std::size_t runtimeInternetCommunityMaximumClanTagBytes = 8U;
inline constexpr std::size_t runtimeInternetCommunityMaximumClanMembers = 64U;
inline constexpr std::size_t runtimeInternetCommunityMaximumClanInvitations = 32U;
inline constexpr std::size_t runtimeInternetCommunityMaximumClanLadderEntries =
    100U;

enum class RuntimeInternetCommunityOperation : std::uint8_t {
    Login = 1,
    RegisterProfile = 2,
    Refresh = 3,
    JoinChannel = 4,
    SendChannelMessage = 5,
    AddFriend = 6,
    RemoveFriend = 7,
    ReportQuickMatch = 8,
    CreateClan = 9,
    InviteClanMember = 10,
    AcceptClanInvitation = 11,
    RemoveClanMember = 12,
    LeaveClan = 13,
    TransferClanOwnership = 14,
    QuickMatchHistory = 15,
    AbandonQuickMatch = 16
};

enum class RuntimeInternetCommunityResponseKind : std::uint8_t {
    Snapshot = 1,
    ChannelMessage = 2,
    Error = 3,
    MatchHistory = 4
};

enum class RuntimeInternetCommunityQuickMatchResult : std::uint8_t {
    None = 0,
    Win = 1,
    Loss = 2
};

struct RuntimeInternetCommunityProfile {
    std::string peerId;
    std::string displayName;
    std::uint64_t registeredAtMilliseconds = 0U;
    std::uint64_t lastSeenMilliseconds = 0U;
    std::uint32_t wins = 0U;
    std::uint32_t losses = 0U;
    std::uint32_t disconnects = 0U;
    std::int32_t points = 1000;
    std::uint32_t rank = 0U;
    bool online = false;

    bool operator==(const RuntimeInternetCommunityProfile&) const = default;
};

struct RuntimeInternetCommunityFriend {
    std::string peerId;
    std::string displayName;
    std::int32_t points = 1000;
    std::uint32_t rank = 0U;
    bool online = false;

    bool operator==(const RuntimeInternetCommunityFriend&) const = default;
};

struct RuntimeInternetCommunityChannel {
    std::string channelId;
    std::string title;
    std::string topic;
    std::uint32_t participantCount = 0U;

    bool operator==(const RuntimeInternetCommunityChannel&) const = default;
};

struct RuntimeInternetCommunityChatMessage {
    std::uint64_t messageId = 0U;
    std::uint64_t sentAtMilliseconds = 0U;
    std::string channelId;
    std::string senderPeerId;
    std::string senderDisplayName;
    std::string text;

    bool operator==(
        const RuntimeInternetCommunityChatMessage&
    ) const = default;
};

struct RuntimeInternetCommunityLadderEntry {
    std::uint32_t rank = 0U;
    std::string peerId;
    std::string displayName;
    std::int32_t points = 1000;
    std::uint32_t wins = 0U;
    std::uint32_t losses = 0U;
    std::uint32_t disconnects = 0U;
    bool online = false;

    bool operator==(
        const RuntimeInternetCommunityLadderEntry&
    ) const = default;
};

struct RuntimeInternetCommunityNewsItem {
    std::string newsId;
    std::uint64_t publishedAtMilliseconds = 0U;
    std::string title;
    std::string body;

    bool operator==(const RuntimeInternetCommunityNewsItem&) const = default;
};

struct RuntimeInternetCommunityClan {
    std::string clanId;
    std::string name;
    std::string tag;
    std::string ownerPeerId;
    std::uint64_t createdAtMilliseconds = 0U;
    std::uint32_t wins = 0U;
    std::uint32_t losses = 0U;
    std::int32_t points = 1000;
    std::uint32_t rank = 0U;
    std::uint32_t memberCount = 0U;

    bool operator==(const RuntimeInternetCommunityClan&) const = default;
};

struct RuntimeInternetCommunityClanMember {
    std::string peerId;
    std::string displayName;
    std::uint64_t joinedAtMilliseconds = 0U;
    std::uint32_t wins = 0U;
    std::uint32_t losses = 0U;
    std::uint32_t disconnects = 0U;
    std::int32_t points = 1000;
    bool owner = false;
    bool online = false;

    bool operator==(
        const RuntimeInternetCommunityClanMember&
    ) const = default;
};

struct RuntimeInternetCommunitySnapshot {
    bool registeredProfile = false;
    RuntimeInternetCommunityProfile profile;
    std::string selectedChannelId;
    std::vector<RuntimeInternetCommunityChannel> channels;
    std::vector<RuntimeInternetCommunityProfile> participants;
    std::vector<RuntimeInternetCommunityChatMessage> chatHistory;
    std::vector<RuntimeInternetCommunityFriend> friends;
    std::vector<RuntimeInternetCommunityLadderEntry> ladder;
    std::vector<RuntimeInternetCommunityNewsItem> news;
    bool clanMember = false;
    RuntimeInternetCommunityClan clan;
    std::vector<RuntimeInternetCommunityClanMember> clanMembers;
    std::vector<RuntimeInternetCommunityClan> clanInvitations;
    std::vector<RuntimeInternetCommunityClan> clanLadder;

    bool operator==(const RuntimeInternetCommunitySnapshot&) const = default;
};

struct RuntimeInternetCommunityRequest {
    RuntimeInternetCommunityOperation operation =
        RuntimeInternetCommunityOperation::Login;
    std::uint64_t transactionId = 0U;
    std::string serviceId;
    std::string senderPeerId;
    std::string displayName;
    std::string channelId;
    std::string text;
    std::string target;
    std::string matchId;
    std::string clanId;
    std::string clanName;
    std::string clanTag;
    RuntimeInternetCommunityQuickMatchResult quickMatchResult =
        RuntimeInternetCommunityQuickMatchResult::None;

    bool operator==(const RuntimeInternetCommunityRequest&) const = default;
};

struct RuntimeInternetCommunityResponse {
    RuntimeInternetCommunityResponseKind kind =
        RuntimeInternetCommunityResponseKind::Error;
    std::uint64_t transactionId = 0U;
    std::string serviceId;
    std::string senderPeerId;
    std::string targetPeerId;
    std::string status;
    RuntimeInternetCommunitySnapshot snapshot;
    RuntimeInternetCommunityChatMessage channelMessage;
    std::vector<RuntimeInternetRankedRecord> matchHistory;

    bool operator==(const RuntimeInternetCommunityResponse&) const = default;
};

bool encodeRuntimeInternetCommunityRequest(
    const RuntimeInternetCommunityRequest& request,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
);

bool decodeRuntimeInternetCommunityRequest(
    const RuntimeNetworkPacket& packet,
    RuntimeInternetCommunityRequest& request,
    std::string& errorMessage
);

bool encodeRuntimeInternetCommunityResponse(
    const RuntimeInternetCommunityResponse& response,
    RuntimeNetworkPacket& packet,
    std::string& errorMessage
);

bool decodeRuntimeInternetCommunityResponse(
    const RuntimeNetworkPacket& packet,
    RuntimeInternetCommunityResponse& response,
    std::string& errorMessage
);

struct RuntimeInternetCommunityServiceEnvelope {
    std::string authenticatedServiceId;
    std::string authenticatedPeerId;
    RuntimeNetworkPacket packet;
};

struct RuntimeInternetCommunityServiceAction {
    std::string targetPeerId;
    RuntimeNetworkPacket packet;
};

struct RuntimeInternetCommunityServiceConfig {
    std::string serviceId;
    std::filesystem::path statePath;
    std::function<std::uint64_t()> nowMilliseconds;
    // UTC is used only for durable audit timestamps. Live deadlines remain
    // monotonic, and unresolved authority is voided on service restart.
    std::function<std::uint64_t()> utcMilliseconds;
    RuntimeInternetRankedPolicy rankedPolicy;
    std::vector<RuntimeInternetCommunityNewsItem> news;
    std::size_t maximumAccounts = 4096U;
    std::size_t maximumClans = 1024U;
    std::size_t maximumAuthorizedQuickMatches = 4096U;
    std::size_t maximumReplayTransactionsPerPeer = 128U;
};

struct RuntimeInternetCommunityServiceStats {
    std::size_t registeredAccounts = 0U;
    std::size_t onlineAccounts = 0U;
    std::size_t authorizedQuickMatches = 0U;
    std::size_t pendingQuickMatchReports = 0U;
    std::size_t unresolvedQuickMatches = 0U;
    std::size_t finalizedQuickMatches = 0U;
    std::size_t registeredClans = 0U;
};

// Authoritative, transport-independent WOL-style community service. The
// deployment's authenticated peer id is the account authority; a registered
// profile supplies the public handle shown in chat, Buddy List, and ladder.
class RuntimeInternetCommunityService {
public:
    RuntimeInternetCommunityService();
    ~RuntimeInternetCommunityService();
    RuntimeInternetCommunityService(
        RuntimeInternetCommunityService&&
    ) noexcept;
    RuntimeInternetCommunityService& operator=(
        RuntimeInternetCommunityService&&
    ) noexcept;
    RuntimeInternetCommunityService(
        const RuntimeInternetCommunityService&
    ) = delete;
    RuntimeInternetCommunityService& operator=(
        const RuntimeInternetCommunityService&
    ) = delete;

    bool open(
        RuntimeInternetCommunityServiceConfig config,
        std::string& errorMessage
    );
    bool receive(
        const RuntimeInternetCommunityServiceEnvelope& envelope,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    );
    bool authenticatedPeerConnected(
        std::string_view authenticatedServiceId,
        std::string_view authenticatedPeerId,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    );
    bool authenticatedPeerDisconnected(
        std::string_view authenticatedServiceId,
        std::string_view authenticatedPeerId,
        bool unexpected,
        std::vector<RuntimeInternetCommunityServiceAction>& actions,
        std::string& errorMessage
    );
    bool authorizeQuickMatch(
        std::string matchId,
        std::span<const std::string> peerIds,
        std::string& errorMessage,
        std::string roomId = {}
    );
    bool rankedRoomStarted(std::string_view roomId,
        std::vector<RuntimeInternetCommunityServiceAction>& actions, std::string& error);
    bool poll(std::vector<RuntimeInternetCommunityServiceAction>& actions, std::string& error);
    RuntimeInternetCommunityServiceStats stats() const;
    bool isOpen() const;
    void close() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

enum class RuntimeInternetCommunityClientEventKind : std::uint8_t {
    RegistrationRequired,
    SnapshotUpdated,
    ChannelMessageReceived,
    OperationRejected
};

struct RuntimeInternetCommunityClientEvent {
    RuntimeInternetCommunityClientEventKind kind =
        RuntimeInternetCommunityClientEventKind::OperationRejected;
    std::uint64_t transactionId = 0U;
    std::string status;
    RuntimeInternetCommunityChatMessage channelMessage;
};

struct RuntimeInternetCommunityClientConfig {
    std::string serviceId;
    std::string localPeerId;
};

// Client-side canonical packet builder and projection cache. Application
// errors are surfaced as events and do not tear down the authenticated lobby.
class RuntimeInternetCommunityClient {
public:
    bool makeQuickMatchHistoryRequest(std::uint64_t transactionId,
        RuntimeNetworkPacket& packet, std::string& error) const;
    bool makeAbandonQuickMatchRequest(std::uint64_t transactionId, std::string matchId,
        std::string opponentPeerId, RuntimeNetworkPacket& packet, std::string& error) const;
    const std::vector<RuntimeInternetRankedRecord>& matchHistory() const noexcept;
    bool open(
        RuntimeInternetCommunityClientConfig config,
        std::string& errorMessage
    );
    bool makeLoginRequest(
        std::uint64_t transactionId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeRegisterProfileRequest(
        std::uint64_t transactionId,
        std::string displayName,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeRefreshRequest(
        std::uint64_t transactionId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeJoinChannelRequest(
        std::uint64_t transactionId,
        std::string channelId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeSendChannelMessageRequest(
        std::uint64_t transactionId,
        std::string text,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeAddFriendRequest(
        std::uint64_t transactionId,
        std::string handleOrPeerId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeRemoveFriendRequest(
        std::uint64_t transactionId,
        std::string peerId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeReportQuickMatchRequest(
        std::uint64_t transactionId,
        std::string matchId,
        std::string opponentPeerId,
        RuntimeInternetCommunityQuickMatchResult result,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeCreateClanRequest(
        std::uint64_t transactionId,
        std::string clanName,
        std::string clanTag,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeInviteClanMemberRequest(
        std::uint64_t transactionId,
        std::string handleOrPeerId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeAcceptClanInvitationRequest(
        std::uint64_t transactionId,
        std::string clanId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeRemoveClanMemberRequest(
        std::uint64_t transactionId,
        std::string peerId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeLeaveClanRequest(
        std::uint64_t transactionId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool makeTransferClanOwnershipRequest(
        std::uint64_t transactionId,
        std::string peerId,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;
    bool receive(
        const RuntimeNetworkPacket& packet,
        std::string_view authenticatedServiceId,
        std::string& errorMessage
    );

    bool isOpen() const noexcept;
    bool registeredProfile() const noexcept;
    const RuntimeInternetCommunitySnapshot& snapshot() const noexcept;
    std::vector<RuntimeInternetCommunityClientEvent> takeEvents();
    void close() noexcept;

private:
    bool makeRequest(
        RuntimeInternetCommunityRequest request,
        RuntimeNetworkPacket& packet,
        std::string& errorMessage
    ) const;

    RuntimeInternetCommunityClientConfig config_;
    RuntimeInternetCommunitySnapshot snapshot_;
    std::vector<RuntimeInternetRankedRecord> matchHistory_;
    std::vector<RuntimeInternetCommunityClientEvent> events_;
    bool opened_ = false;
};
