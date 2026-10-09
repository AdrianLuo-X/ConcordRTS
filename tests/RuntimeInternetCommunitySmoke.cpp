#include "RuntimeInternetCommunity.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct SmokeContext {
    int failures = 0;

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAILED: " << message << '\n';
    }
};

struct ClientFixture {
    std::string peerId;
    RuntimeInternetCommunityClient client;
};

bool deliver(
    std::vector<RuntimeInternetCommunityServiceAction> actions,
    std::span<ClientFixture> clients,
    std::string& errorMessage
) {
    for (RuntimeInternetCommunityServiceAction& action : actions) {
        const auto target = std::find_if(
            clients.begin(),
            clients.end(),
            [&](const ClientFixture& client) {
                return client.peerId == action.targetPeerId;
            }
        );
        if (target == clients.end()) continue;
        if (!target->client.receive(
                action.packet,
                "community-service",
                errorMessage
            )) {
            return false;
        }
    }
    errorMessage.clear();
    return true;
}

bool submit(
    RuntimeInternetCommunityService& service,
    std::string_view peerId,
    RuntimeNetworkPacket packet,
    std::span<ClientFixture> clients,
    std::string& errorMessage
) {
    RuntimeInternetCommunityServiceEnvelope envelope;
    envelope.authenticatedServiceId = "community-service";
    envelope.authenticatedPeerId = std::string(peerId);
    envelope.packet = std::move(packet);
    std::vector<RuntimeInternetCommunityServiceAction> actions;
    if (!service.receive(envelope, actions, errorMessage)) return false;
    return deliver(std::move(actions), clients, errorMessage);
}

const RuntimeInternetCommunityLadderEntry* ladderEntry(
    const RuntimeInternetCommunitySnapshot& snapshot,
    std::string_view peerId
) {
    const auto found = std::find_if(
        snapshot.ladder.begin(),
        snapshot.ladder.end(),
        [&](const RuntimeInternetCommunityLadderEntry& entry) {
            return entry.peerId == peerId;
        }
    );
    return found == snapshot.ladder.end() ? nullptr : &*found;
}

const RuntimeInternetCommunityClan* clanEntry(
    const RuntimeInternetCommunitySnapshot& snapshot,
    std::string_view clanId
) {
    const auto found = std::find_if(
        snapshot.clanLadder.begin(),
        snapshot.clanLadder.end(),
        [&](const RuntimeInternetCommunityClan& clan) {
            return clan.clanId == clanId;
        }
    );
    return found == snapshot.clanLadder.end() ? nullptr : &*found;
}

} // namespace

int main() {
    SmokeContext context;
    std::uint64_t nowMilliseconds = 1000U;
    const std::filesystem::path statePath =
        std::filesystem::current_path()
        / "runtime_internet_community_smoke.state";
    std::error_code cleanupError;
    (void)std::filesystem::remove(statePath, cleanupError);

    RuntimeInternetCommunityService service;
    RuntimeInternetCommunityServiceConfig config;
    config.serviceId = "community-service";
    config.statePath = statePath;
    config.nowMilliseconds = [&] { return nowMilliseconds; };
    std::string error;
    context.expect(service.open(config, error), "service open: " + error);

    std::array<ClientFixture, 3U> clients{{
        {"peer-alice", {}},
        {"peer-bob", {}},
        {"peer-charlie", {}}
    }};
    for (ClientFixture& fixture : clients) {
        RuntimeInternetCommunityClientConfig clientConfig;
        clientConfig.serviceId = "community-service";
        clientConfig.localPeerId = fixture.peerId;
        context.expect(
            fixture.client.open(std::move(clientConfig), error),
            "client open for " + fixture.peerId + ": " + error
        );
        std::vector<RuntimeInternetCommunityServiceAction> actions;
        context.expect(
            service.authenticatedPeerConnected(
                "community-service",
                fixture.peerId,
                actions,
                error
            ),
            "authenticated connect for " + fixture.peerId + ": " + error
        );
        context.expect(
            deliver(std::move(actions), clients, error),
            "connect delivery for " + fixture.peerId + ": " + error
        );
    }

    RuntimeNetworkPacket packet;
    context.expect(
        clients[0].client.makeLoginRequest(1U, packet, error)
            && submit(service, "peer-alice", packet, clients, error),
        "unregistered login: " + error
    );
    const auto loginEvents = clients[0].client.takeEvents();
    context.expect(
        !clients[0].client.registeredProfile()
            && !loginEvents.empty()
            && loginEvents.back().kind
                == RuntimeInternetCommunityClientEventKind::RegistrationRequired,
        "unregistered account should receive a registration-required snapshot"
    );

    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeRegisterProfileRequest(
            2U,
            "CommanderAlice",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "Alice profile registration: " + error
    );
    ++nowMilliseconds;
    context.expect(
        clients[1].client.makeRegisterProfileRequest(
            3U,
            "CommanderBob",
            packet,
            error
        ) && submit(service, "peer-bob", packet, clients, error),
        "Bob profile registration: " + error
    );
    context.expect(
        clients[0].client.registeredProfile()
            && clients[1].client.registeredProfile()
            && clients[0].client.snapshot().channels.size() == 4U
            && clients[0].client.snapshot().news.size() >= 3U,
        "registration should expose profile, canonical channels, and news"
    );

    ++nowMilliseconds;
    context.expect(
        clients[2].client.makeRegisterProfileRequest(
            4U,
            "commanderalice",
            packet,
            error
        ) && submit(service, "peer-charlie", packet, clients, error),
        "duplicate-handle request transport: " + error
    );
    const auto duplicateEvents = clients[2].client.takeEvents();
    context.expect(
        !clients[2].client.registeredProfile()
            && !duplicateEvents.empty()
            && duplicateEvents.back().kind
                == RuntimeInternetCommunityClientEventKind::OperationRejected,
        "profile handles should be unique without regard to ASCII case"
    );

    ++nowMilliseconds;
    context.expect(
        clients[2].client.makeRegisterProfileRequest(
            16U,
            "CommanderCharlie",
            packet,
            error
        ) && submit(service, "peer-charlie", packet, clients, error),
        "Charlie profile registration: " + error
    );

    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeCreateClanRequest(
            20U,
            "Red Alert Command",
            "RAC",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "Alice Clan creation: " + error
    );
    context.expect(
        clients[0].client.snapshot().clanMember
            && clients[0].client.snapshot().clan.clanId == "rac"
            && clients[0].client.snapshot().clan.ownerPeerId == "peer-alice"
            && clients[0].client.snapshot().clanMembers.size() == 1U
            && clients[0].client.snapshot().clanMembers.front().owner
            && service.stats().registeredClans == 1U,
        "Clan creation should establish an owner and ranked Clan projection"
    );

    context.expect(
        clients[1].client.makeAcceptClanInvitationRequest(
            20U,
            "rac",
            packet,
            error
        ) && submit(service, "peer-bob", packet, clients, error),
        "uninvited Clan acceptance response: " + error
    );
    const auto uninvitedEvents = clients[1].client.takeEvents();
    context.expect(
        !uninvitedEvents.empty()
            && uninvitedEvents.back().kind
                == RuntimeInternetCommunityClientEventKind::OperationRejected
            && !clients[1].client.snapshot().clanMember,
        "Clan membership should require an authoritative invitation"
    );

    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeInviteClanMemberRequest(
            21U,
            "CommanderBob",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "Bob Clan invitation: " + error
    );
    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeInviteClanMemberRequest(
            22U,
            "CommanderCharlie",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "Charlie Clan invitation: " + error
    );
    context.expect(
        clients[1].client.snapshot().clanInvitations.size() == 1U
            && clients[1].client.snapshot().clanInvitations.front().clanId
                == "rac"
            && clients[2].client.snapshot().clanInvitations.size() == 1U,
        "online invitees should receive authoritative Clan invitations"
    );

    ++nowMilliseconds;
    context.expect(
        clients[1].client.makeAcceptClanInvitationRequest(
            21U,
            "rac",
            packet,
            error
        ) && submit(service, "peer-bob", packet, clients, error),
        "Bob Clan acceptance: " + error
    );
    ++nowMilliseconds;
    context.expect(
        clients[2].client.makeAcceptClanInvitationRequest(
            22U,
            "rac",
            packet,
            error
        ) && submit(service, "peer-charlie", packet, clients, error),
        "Charlie Clan acceptance: " + error
    );
    context.expect(
        clients[0].client.snapshot().clanMembers.size() == 3U
            && clients[1].client.snapshot().clanMember
            && clients[2].client.snapshot().clanMember,
        "accepted invitations should synchronize the Clan member roster"
    );

    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeRemoveClanMemberRequest(
            23U,
            "peer-charlie",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "Clan member removal: " + error
    );
    context.expect(
        clients[0].client.snapshot().clanMembers.size() == 2U
            && !clients[2].client.snapshot().clanMember,
        "Clan owner should be able to remove a non-owner member"
    );

    context.expect(
        clients[0].client.makeLeaveClanRequest(24U, packet, error)
            && submit(service, "peer-alice", packet, clients, error),
        "owner leave rejection response: " + error
    );
    const auto ownerLeaveEvents = clients[0].client.takeEvents();
    context.expect(
        !ownerLeaveEvents.empty()
            && ownerLeaveEvents.back().kind
                == RuntimeInternetCommunityClientEventKind::OperationRejected
            && clients[0].client.snapshot().clanMember,
        "Clan owner should transfer ownership before leaving other members"
    );

    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeTransferClanOwnershipRequest(
            25U,
            "peer-bob",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "Clan ownership transfer: " + error
    );
    context.expect(
        clients[1].client.snapshot().clan.ownerPeerId == "peer-bob"
            && std::any_of(
                clients[1].client.snapshot().clanMembers.begin(),
                clients[1].client.snapshot().clanMembers.end(),
                [](const RuntimeInternetCommunityClanMember& member) {
                    return member.peerId == "peer-bob" && member.owner;
                }
            ),
        "Clan ownership transfer should update the authoritative owner role"
    );

    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeLeaveClanRequest(26U, packet, error)
            && submit(service, "peer-alice", packet, clients, error),
        "former owner Clan leave: " + error
    );
    context.expect(
        !clients[0].client.snapshot().clanMember
            && clients[1].client.snapshot().clanMembers.size() == 1U,
        "non-owner should be able to leave without dissolving the Clan"
    );

    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeCreateClanRequest(
            27U,
            "Allied Command",
            "AC",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "Alice second Clan creation: " + error
    );
    context.expect(
        clients[0].client.snapshot().clan.clanId == "ac"
            && clients[0].client.snapshot().clanLadder.size() == 2U
            && service.stats().registeredClans == 2U,
        "Clan leave should permit creating a distinct persistent Clan"
    );

    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeAddFriendRequest(
            5U,
            "CommanderBob",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "Buddy add: " + error
    );
    context.expect(
        clients[0].client.snapshot().friends.size() == 1U
            && clients[0].client.snapshot().friends.front().peerId
                == "peer-bob"
            && clients[0].client.snapshot().friends.front().online,
        "Buddy List should resolve handles and project authenticated presence"
    );

    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeJoinChannelRequest(
            6U,
            "ra2",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "Alice channel join: " + error
    );
    ++nowMilliseconds;
    context.expect(
        clients[1].client.makeJoinChannelRequest(
            7U,
            "ra2",
            packet,
            error
        ) && submit(service, "peer-bob", packet, clients, error),
        "Bob channel join: " + error
    );
    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeSendChannelMessageRequest(
            8U,
            "Ready for a Red Alert 2 match?",
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "channel chat: " + error
    );
    context.expect(
        clients[0].client.snapshot().chatHistory.size() == 1U
            && clients[1].client.snapshot().chatHistory.size() == 1U
            && clients[1].client.snapshot().chatHistory.front().senderPeerId
                == "peer-alice",
        "channel chat should broadcast once to each authenticated member"
    );

    RuntimeNetworkPacket forgedPacket;
    context.expect(
        clients[0].client.makeRefreshRequest(9U, forgedPacket, error),
        "forged fixture packet build: " + error
    );
    RuntimeInternetCommunityServiceEnvelope forgedEnvelope;
    forgedEnvelope.authenticatedServiceId = "community-service";
    forgedEnvelope.authenticatedPeerId = "peer-bob";
    forgedEnvelope.packet = forgedPacket;
    std::vector<RuntimeInternetCommunityServiceAction> forgedActions;
    context.expect(
        !service.receive(forgedEnvelope, forgedActions, error)
            && forgedActions.empty(),
        "authenticated outer identity should reject another profile's claim"
    );

    const std::array<std::string, 2U> quickPeers{{
        "peer-alice",
        "peer-bob"
    }};
    context.expect(
        service.authorizeQuickMatch("quick-match-1", quickPeers, error),
        "Quick Match authorization: " + error
    );
    std::vector<RuntimeInternetCommunityServiceAction> startActions;
    context.expect(service.rankedRoomStarted("quick-match-1", startActions, error)
        && deliver(std::move(startActions), clients, error), "trusted first match start: " + error);
    ++nowMilliseconds;
    context.expect(
        clients[0].client.makeReportQuickMatchRequest(
            10U,
            "quick-match-1",
            "peer-bob",
            RuntimeInternetCommunityQuickMatchResult::Win,
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "winner report: " + error
    );
    context.expect(
        service.stats().pendingQuickMatchReports == 1U,
        "one-sided Quick Match result should remain pending"
    );
    ++nowMilliseconds;
    context.expect(
        clients[1].client.makeReportQuickMatchRequest(
            11U,
            "quick-match-1",
            "peer-alice",
            RuntimeInternetCommunityQuickMatchResult::Loss,
            packet,
            error
        ) && submit(service, "peer-bob", packet, clients, error),
        "loser confirmation: " + error
    );
    const RuntimeInternetCommunityLadderEntry* alice = ladderEntry(
        clients[0].client.snapshot(),
        "peer-alice"
    );
    const RuntimeInternetCommunityLadderEntry* bob = ladderEntry(
        clients[0].client.snapshot(),
        "peer-bob"
    );
    context.expect(
        alice != nullptr && bob != nullptr
            && alice->rank == 1U
            && alice->wins == 1U && alice->points == 1032
            && bob->losses == 1U && bob->points == 984,
        "opposite authenticated reports should update profile and ladder"
    );
    const RuntimeInternetCommunityClan* alliedClan = clanEntry(
        clients[0].client.snapshot(),
        "ac"
    );
    const RuntimeInternetCommunityClan* redAlertClan = clanEntry(
        clients[0].client.snapshot(),
        "rac"
    );
    context.expect(
        alliedClan != nullptr && redAlertClan != nullptr
            && alliedClan->rank == 1U
            && alliedClan->wins == 1U
            && alliedClan->points == 1032
            && redAlertClan->losses == 1U
            && redAlertClan->points == 984,
        "confirmed inter-Clan Quick Match should update Clan standings"
    );

    context.expect(
        service.authorizeQuickMatch("quick-match-2", quickPeers, error),
        "conflict fixture Quick Match authorization: " + error
    );
    startActions.clear();
    context.expect(service.rankedRoomStarted("quick-match-2", startActions, error)
        && deliver(std::move(startActions), clients, error), "trusted conflict match start: " + error);
    context.expect(
        clients[0].client.makeReportQuickMatchRequest(
            12U,
            "quick-match-2",
            "peer-bob",
            RuntimeInternetCommunityQuickMatchResult::Win,
            packet,
            error
        ) && submit(service, "peer-alice", packet, clients, error),
        "conflict fixture first report: " + error
    );
    context.expect(
        clients[1].client.makeReportQuickMatchRequest(
            13U,
            "quick-match-2",
            "peer-alice",
            RuntimeInternetCommunityQuickMatchResult::Win,
            packet,
            error
        ) && submit(service, "peer-bob", packet, clients, error),
        "conflicting report response: " + error
    );
    const auto conflictEvents = clients[1].client.takeEvents();
    alice = ladderEntry(clients[0].client.snapshot(), "peer-alice");
    context.expect(
        !conflictEvents.empty()
            && conflictEvents.back().kind
                == RuntimeInternetCommunityClientEventKind::OperationRejected
            && service.stats().pendingQuickMatchReports == 1U
            && alice != nullptr && alice->wins == 1U,
        "conflicting reports must not change standings"
    );
    context.expect(
        clients[1].client.makeReportQuickMatchRequest(
            14U,
            "quick-match-2",
            "peer-alice",
            RuntimeInternetCommunityQuickMatchResult::Loss,
            packet,
            error
        ) && submit(service, "peer-bob", packet, clients, error),
        "corrected opposite report: " + error
    );
    alice = ladderEntry(clients[0].client.snapshot(), "peer-alice");
    bob = ladderEntry(clients[0].client.snapshot(), "peer-bob");
    context.expect(
        service.stats().pendingQuickMatchReports == 0U
            && alice != nullptr && alice->wins == 2U
            && alice->points == 1064
            && bob != nullptr && bob->losses == 2U
            && bob->points == 968,
        "a later opposite confirmation should settle exactly once"
    );
    alliedClan = clanEntry(clients[0].client.snapshot(), "ac");
    redAlertClan = clanEntry(clients[0].client.snapshot(), "rac");
    context.expect(
        alliedClan != nullptr && redAlertClan != nullptr
            && alliedClan->wins == 2U && alliedClan->points == 1064
            && redAlertClan->losses == 2U
            && redAlertClan->points == 968,
        "Clan standings should settle exactly once per confirmed match"
    );

    RuntimeNetworkPacket malformedPacket;
    context.expect(
        clients[0].client.makeRefreshRequest(15U, malformedPacket, error),
        "malformed packet fixture build: " + error
    );
    malformedPacket.payload.push_back(0U);
    RuntimeInternetCommunityServiceEnvelope malformedEnvelope;
    malformedEnvelope.authenticatedServiceId = "community-service";
    malformedEnvelope.authenticatedPeerId = "peer-alice";
    malformedEnvelope.packet = std::move(malformedPacket);
    std::vector<RuntimeInternetCommunityServiceAction> malformedActions;
    context.expect(
        !service.receive(malformedEnvelope, malformedActions, error)
            && malformedActions.empty(),
        "community codec should reject trailing non-canonical bytes"
    );

    RuntimeNetworkPacket duplicateTransaction;
    context.expect(
        clients[0].client.makeRefreshRequest(
            10U,
            duplicateTransaction,
            error
        ) && submit(
            service,
            "peer-alice",
            duplicateTransaction,
            clients,
            error
        ),
        "duplicate transaction response: " + error
    );
    const auto replayEvents = clients[0].client.takeEvents();
    context.expect(
        !replayEvents.empty()
            && replayEvents.back().kind
                == RuntimeInternetCommunityClientEventKind::OperationRejected,
        "per-account transaction replay should be rejected without disconnecting"
    );

    std::vector<RuntimeInternetCommunityServiceAction> presenceActions;
    ++nowMilliseconds;
    context.expect(
        service.authenticatedPeerDisconnected(
            "community-service",
            "peer-bob",
            true,
            presenceActions,
            error
        ) && deliver(std::move(presenceActions), clients, error),
        "unexpected disconnect presence: " + error
    );
    context.expect(
        clients[0].client.snapshot().friends.size() == 1U
            && !clients[0].client.snapshot().friends.front().online,
        "Buddy presence should change to offline after disconnect"
    );

    service.close();
    RuntimeInternetCommunityService reloaded;
    context.expect(
        reloaded.open(config, error),
        "persistent community reload: " + error
    );
    std::vector<RuntimeInternetCommunityServiceAction> reconnectActions;
    context.expect(
        reloaded.authenticatedPeerConnected(
            "community-service",
            "peer-alice",
            reconnectActions,
            error
        ) && deliver(std::move(reconnectActions), clients, error),
        "persistent profile reconnect: " + error
    );
    alice = ladderEntry(clients[0].client.snapshot(), "peer-alice");
    bob = ladderEntry(clients[0].client.snapshot(), "peer-bob");
    context.expect(
        reloaded.stats().registeredAccounts == 3U
            && reloaded.stats().registeredClans == 2U
            && alice != nullptr && alice->wins == 2U
            && bob != nullptr && bob->losses == 2U
            && bob->disconnects == 1U
            && clients[0].client.snapshot().friends.size() == 1U
            && clients[0].client.snapshot().clanMember
            && clients[0].client.snapshot().clan.clanId == "ac"
            && clanEntry(clients[0].client.snapshot(), "ac") != nullptr
            && clanEntry(clients[0].client.snapshot(), "ac")->wins == 2U
            && clanEntry(clients[0].client.snapshot(), "rac") != nullptr
            && clanEntry(clients[0].client.snapshot(), "rac")->losses == 2U,
        "profiles, Buddy relations, player/Clan standings, membership, and disconnect history should persist"
    );
    reloaded.close();

    cleanupError.clear();
    const bool removed = std::filesystem::remove(statePath, cleanupError);
    context.expect(
        removed && !cleanupError,
        "community smoke state file cleanup"
    );

    if (context.failures != 0) {
        std::cerr << "Runtime Internet community smoke failed: "
                  << context.failures << " check(s)\n";
        return 1;
    }
    std::cout
        << "Runtime Internet community smoke passed: "
        << "account+channels+chat+buddies+clans+quick-match-ladders+news+persistence\n";
    return 0;
}
