#include "RuntimeMultiplayerRuntimeBridge.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct SmokeContext {
    int failures = 0;

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
};

struct FakeWorld {
    int tick = 0;
    bool ignoreAdvance = false;
    std::vector<RuntimeMultiplayerLockstepApplyCommand> applied;
    std::vector<std::uint8_t> stagedSnapshot;
};

std::uint64_t fakeWorldHash(const FakeWorld& world) {
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int shift = 56; shift >= 0; shift -= 8) {
            hash ^= static_cast<std::uint8_t>(value >> shift);
            hash *= 1099511628211ULL;
        }
    };
    mix(static_cast<std::uint64_t>(world.tick));
    mix(static_cast<std::uint64_t>(world.applied.size()));
    for (const RuntimeMultiplayerLockstepApplyCommand& item : world.applied) {
        mix(static_cast<std::uint64_t>(item.tick));
        mix(item.frameSequence);
        mix(static_cast<std::uint64_t>(item.commandIndex));
        for (const unsigned char byte : item.peerId) mix(byte);
        mix(static_cast<std::uint64_t>(
            runtimeMultiplayerGameplayCommandType(item.command)
        ));
    }
    return hash == 0 ? 1ULL : hash;
}

RuntimeMultiplayerRuntimeCallbacks callbacksFor(FakeWorld& world) {
    RuntimeMultiplayerRuntimeCallbacks callbacks;
    callbacks.prevalidateCommands = [](
        std::span<const RuntimeMultiplayerLockstepApplyCommand>,
        std::string& errorMessage
    ) {
        errorMessage.clear();
        return true;
    };
    callbacks.applyCommands = [&world](
        std::span<const RuntimeMultiplayerLockstepApplyCommand> batch,
        std::string& errorMessage
    ) {
        world.applied.insert(world.applied.end(), batch.begin(), batch.end());
        errorMessage.clear();
        return true;
    };
    callbacks.currentCompletedTick = [&world]() { return world.tick; };
    callbacks.advanceSimulation = [&world](int completedTick) {
        if (!world.ignoreAdvance) world.tick = completedTick;
    };
    callbacks.authoritativeStateHash = [&world]() {
        return fakeWorldHash(world);
    };
    callbacks.matchTerminal = []() { return false; };
    callbacks.captureReconnectSnapshot = [&world](
        const RuntimeMultiplayerSnapshotIdentity& identity,
        RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::vector<std::uint8_t>& bytes,
        std::string& errorMessage
    ) {
        if (identity.snapshotTick != world.tick
            || identity.authoritativeStateHash != fakeWorldHash(world)) {
            errorMessage = "fixture reconnect identity does not match world";
            return false;
        }
        const std::string payload = "reconnect-world-tick="
            + std::to_string(world.tick)
            + ";hash=" + std::to_string(fakeWorldHash(world));
        std::vector<std::uint8_t> candidate(
            payload.begin(),
            payload.end()
        );
        RuntimeMultiplayerSnapshotDescriptor candidateDescriptor;
        if (!makeRuntimeMultiplayerSnapshotDescriptor(
                identity,
                candidate,
                candidateDescriptor,
                errorMessage
            )) {
            return false;
        }
        descriptor = std::move(candidateDescriptor);
        bytes = std::move(candidate);
        errorMessage.clear();
        return true;
    };
    callbacks.stageVerifiedSnapshot = [&world](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::span<const std::uint8_t> bytes,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        world.stagedSnapshot.assign(bytes.begin(), bytes.end());
        observedHash = descriptor.identity.authoritativeStateHash;
        errorMessage.clear();
        return true;
    };
    callbacks.commitStagedSnapshot = [&world](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        if (world.stagedSnapshot.empty()) {
            errorMessage = "fixture has no staged snapshot";
            return false;
        }
        observedHash = descriptor.identity.authoritativeStateHash;
        errorMessage.clear();
        return true;
    };
    callbacks.discardStagedSnapshot = [&world](
        const RuntimeMultiplayerSnapshotDescriptor&
    ) {
        world.stagedSnapshot.clear();
    };
    callbacks.onLoadTimelineRebound = [](
        const RuntimeMultiplayerSnapshotDescriptor&,
        const RuntimeMultiplayerSessionState&,
        std::string& errorMessage
    ) {
        errorMessage.clear();
        return true;
    };
    return callbacks;
}

RuntimeMultiplayerSessionConfig sessionConfig() {
    RuntimeMultiplayerSessionConfig config;
    config.roomId = "runtime-bridge-room";
    config.hostPeerId = "host-peer";
    config.hostDisplayName = "Host";
    config.compatibility.protocolFingerprint = 0x1020304050607080ULL;
    config.compatibility.contentFingerprint = 0x8877665544332211ULL;
    config.stateHashPeriodTicks = 1;
    return config;
}

RuntimeLanRoomAdvertisement advertisementFor(
    const RuntimeMultiplayerRuntimeBridge& host
) {
    RuntimeLanRoomAdvertisement room;
    room.roomId = host.lan().session().config().roomId;
    room.roomName = "Runtime Bridge";
    room.hostName = "Host";
    room.hostAddress = "127.0.0.1";
    room.contentFingerprint = "8877665544332211";
    room.protocolVersion = runtimeLanProtocolVersion;
    room.sessionPort = host.lan().sessionPort();
    room.playerCount = 1;
    room.maximumPlayers = static_cast<std::uint8_t>(
        host.lan().session().config().maximumMembers
    );
    return room;
}

RuntimeMultiplayerMatchSetup matchSetup() {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 1;
    setup.hostPeerId = "host-peer";
    setup.map.canonicalId = "maps/runtime-bridge.map";
    setup.map.contentFingerprint = 0x8877665544332211ULL;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x3141592653589793ULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;

    RuntimeMultiplayerMatchMember client;
    client.peerId = "client-peer";
    client.displayName = "Client";
    client.identity = RuntimeMultiplayerMatchMemberIdentity::Combatant;
    RuntimeMultiplayerMatchMember host;
    host.peerId = "host-peer";
    host.displayName = "Host";
    host.identity = RuntimeMultiplayerMatchMemberIdentity::Combatant;
    setup.members = {client, host};

    RuntimeMultiplayerMatchSlot hostSlot;
    hostSlot.slotIndex = 0;
    hostSlot.controller = RuntimeMultiplayerMatchSlotController::Human;
    hostSlot.peerId = "host-peer";
    hostSlot.countryId = "random";
    hostSlot.colorId = "red";
    hostSlot.teamId = "-";
    hostSlot.difficultyId = "medium";
    hostSlot.startPosition = 1;
    RuntimeMultiplayerMatchSlot clientSlot = hostSlot;
    clientSlot.slotIndex = 1;
    clientSlot.peerId = "client-peer";
    clientSlot.colorId = "blue";
    clientSlot.startPosition = 2;
    setup.slots = {hostSlot, clientSlot};
    return setup;
}

class FailingPreAttachTransport final
    : public RuntimeMultiplayerExternalOrderedTransport {
public:
    bool pump(
        RuntimeMultiplayerOrderedTransportPumpStage,
        int,
        std::string& errorMessage
    ) override {
        errorMessage = "synthetic pre-attach carrier failure";
        return false;
    }

    void close() noexcept override {}
};

struct RecordingRankedTransportState {
    int reportCalls = 0;
    bool localVictory = false;
    bool closed = false;
};

class RecordingRankedTransport final
    : public RuntimeMultiplayerExternalOrderedTransport {
public:
    explicit RecordingRankedTransport(
        std::shared_ptr<RecordingRankedTransportState> state
    ) : state_(std::move(state)) {}

    bool pump(
        RuntimeMultiplayerOrderedTransportPumpStage,
        int,
        std::string& errorMessage
    ) override {
        errorMessage.clear();
        return true;
    }

    bool reportRankedMatchResult(
        bool localVictory,
        bool& submitted,
        std::string& errorMessage
    ) override {
        ++state_->reportCalls;
        state_->localVictory = localVictory;
        submitted = true;
        errorMessage.clear();
        return true;
    }

    void close() noexcept override { state_->closed = true; }

private:
    std::shared_ptr<RecordingRankedTransportState> state_;
};

void runRankedResultReportIsOneShot(SmokeContext& context) {
    auto state = std::make_shared<RecordingRankedTransportState>();
    {
        RuntimeMultiplayerRuntimeBridge bridge;
        std::string error;
        context.expect(
            bridge.adoptExternalOrderedTransport(
                std::make_unique<RecordingRankedTransport>(state),
                error
            ),
            "ranked result carrier adoption: " + error
        );
        bool submitted = false;
        context.expect(
            bridge.reportRankedMatchResult(true, submitted, error)
                && submitted
                && state->reportCalls == 1
                && state->localVictory,
            "ranked result should reach the carrier once: " + error
        );
        submitted = true;
        error.clear();
        context.expect(
            bridge.reportRankedMatchResult(false, submitted, error)
                && !submitted
                && state->reportCalls == 1
                && state->localVictory,
            "ranked result retry should be a no-op"
        );
    }
    context.expect(
        state->closed,
        "ranked result carrier should close with bridge ownership"
    );

    RuntimeMultiplayerRuntimeBridge plainLan;
    bool submitted = true;
    std::string error;
    context.expect(
        plainLan.reportRankedMatchResult(true, submitted, error)
            && !submitted
            && error.empty(),
        "plain LAN result reporting should be a successful no-op"
    );
}

bool pumpUntil(
    RuntimeMultiplayerRuntimeBridge& host,
    RuntimeMultiplayerRuntimeBridge& client,
    const std::function<bool()>& complete,
    SmokeContext& context,
    const std::string& description,
    int timeoutMilliseconds = 5000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerRuntimePumpResult hostPoll = host.pump(false);
        const RuntimeMultiplayerRuntimePumpResult clientPoll = client.pump(false);
        if (!hostPoll.succeeded || !clientPoll.succeeded) {
            context.expect(
                false,
                description + ": pump failed: "
                    + (!hostPoll.succeeded ? hostPoll.error : clientPoll.error)
            );
            return false;
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": timed out");
    return false;
}

bool startRunningPair(
    RuntimeMultiplayerRuntimeBridge& host,
    RuntimeMultiplayerRuntimeBridge& client,
    SmokeContext& context,
    std::uint64_t roomGeneration,
    const std::string& description
) {
    std::string error;
    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.roomName = "Runtime Bridge Deadline";
    hostOptions.discoveryPort = 0;
    hostOptions.sessionPort = 0;
    hostOptions.roomGeneration = roomGeneration;
    if (!host.lan().startHost(hostOptions, error)) {
        context.expect(false, description + ": host start: " + error);
        return false;
    }

    RuntimeMultiplayerLanJoinOptions join;
    join.room = advertisementFor(host);
    join.peerId = "client-peer";
    join.displayName = "Client";
    join.compatibility = hostOptions.session.compatibility;
    join.connectTimeoutMilliseconds = 2000;
    if (!client.lan().join(join, error)) {
        context.expect(false, description + ": client join: " + error);
        return false;
    }
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.lan().phase() == RuntimeMultiplayerLanPhase::Lobby
                    && client.lan().phase()
                        == RuntimeMultiplayerLanPhase::Lobby
                    && host.lan().session().members().size() == 2U
                    && client.lan().session().members().size() == 2U;
            },
            context,
            description + " admission"
        )) {
        return false;
    }

    const RuntimeMultiplayerLanOperationResult setup =
        host.lan().setMatchSetup(matchSetup());
    if (!setup.succeeded) {
        context.expect(false, description + ": setup: " + setup.reason);
        return false;
    }
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.lan().session().matchSetup().has_value()
                    && client.lan().session().matchSetup().has_value();
            },
            context,
            description + " setup"
        )) {
        return false;
    }

    const RuntimeMultiplayerLanOperationResult hostReady =
        host.lan().setReady(true);
    const RuntimeMultiplayerLanOperationResult clientReady =
        client.lan().setReady(true);
    if (!hostReady.succeeded || !clientReady.succeeded
        || !pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.lan().session().allMembersReady()
                    && client.lan().session().allMembersReady();
            },
            context,
            description + " ready"
        )) {
        context.expect(
            hostReady.succeeded && clientReady.succeeded,
            description + ": ready intent failed"
        );
        return false;
    }

    const RuntimeMultiplayerLanOperationResult started =
        host.lan().startMatch(1);
    if (!started.succeeded) {
        context.expect(false, description + ": start: " + started.reason);
        return false;
    }
    return pumpUntil(
        host,
        client,
        [&host, &client]() {
            return host.lan().phase() == RuntimeMultiplayerLanPhase::Running
                && client.lan().phase()
                    == RuntimeMultiplayerLanPhase::Running
                && host.lockstepBound() && client.lockstepBound();
        },
        context,
        description + " start"
    );
}

bool waitForMaintenancePumps(
    RuntimeMultiplayerRuntimeBridge& first,
    RuntimeMultiplayerRuntimeBridge& second,
    std::uint64_t minimumPumps,
    int timeoutMilliseconds = 2000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (first.preAttachMaintenancePumpCount() >= minimumPumps
            && second.preAttachMaintenancePumpCount() >= minimumPumps) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool pumpThreeUntil(
    RuntimeMultiplayerRuntimeBridge& host,
    RuntimeMultiplayerRuntimeBridge& client,
    RuntimeMultiplayerRuntimeBridge& spectator,
    const std::function<bool()>& complete,
    SmokeContext& context,
    const std::string& description,
    int timeoutMilliseconds = 5000
) {
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerRuntimePumpResult hostPoll = host.pump(false);
        const RuntimeMultiplayerRuntimePumpResult clientPoll =
            client.pump(false);
        const RuntimeMultiplayerRuntimePumpResult spectatorPoll =
            spectator.pump(false);
        if (!hostPoll.succeeded || !clientPoll.succeeded
            || !spectatorPoll.succeeded) {
            context.expect(
                false,
                description + ": pump failed: "
                    + (!hostPoll.succeeded
                        ? hostPoll.error
                        : (!clientPoll.succeeded
                            ? clientPoll.error
                            : spectatorPoll.error))
            );
            return false;
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": timed out");
    return false;
}

bool startRunningTrio(
    RuntimeMultiplayerRuntimeBridge& host,
    RuntimeMultiplayerRuntimeBridge& client,
    RuntimeMultiplayerRuntimeBridge& spectator,
    SmokeContext& context
) {
    std::string error;
    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.roomName = "Runtime Bridge Spectator Deadline";
    hostOptions.discoveryPort = 0;
    hostOptions.sessionPort = 0;
    hostOptions.roomGeneration = 0x60606060ULL;
    if (!host.lan().startHost(hostOptions, error)) {
        context.expect(false, "spectator deadline host start: " + error);
        return false;
    }

    RuntimeMultiplayerLanJoinOptions join;
    join.room = advertisementFor(host);
    join.peerId = "client-peer";
    join.displayName = "Client";
    join.compatibility = hostOptions.session.compatibility;
    join.connectTimeoutMilliseconds = 2000;
    if (!client.lan().join(join, error)
        || !pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.lan().session().members().size() == 2U
                    && client.lan().phase()
                        == RuntimeMultiplayerLanPhase::Lobby;
            },
            context,
            "spectator deadline combatant admission"
        )) {
        context.expect(false, "spectator deadline client join: " + error);
        return false;
    }

    RuntimeMultiplayerLanJoinOptions spectatorJoin = join;
    spectatorJoin.room = advertisementFor(host);
    spectatorJoin.peerId = "spectator-peer";
    spectatorJoin.displayName = "Spectator";
    spectatorJoin.participation =
        RuntimeMultiplayerParticipationRole::Spectator;
    if (!spectator.lan().join(spectatorJoin, error)
        || !pumpThreeUntil(
            host,
            client,
            spectator,
            [&host, &client, &spectator]() {
                return host.lan().session().members().size() == 3U
                    && client.lan().session().members().size() == 3U
                    && spectator.lan().session().members().size() == 3U;
            },
            context,
            "spectator deadline spectator admission"
        )) {
        context.expect(false, "spectator deadline spectator join: " + error);
        return false;
    }

    RuntimeMultiplayerMatchSetup setup = matchSetup();
    RuntimeMultiplayerMatchMember spectatorMember;
    spectatorMember.peerId = "spectator-peer";
    spectatorMember.displayName = "Spectator";
    spectatorMember.identity =
        RuntimeMultiplayerMatchMemberIdentity::Spectator;
    setup.members.push_back(std::move(spectatorMember));
    const RuntimeMultiplayerLanOperationResult configured =
        host.lan().setMatchSetup(std::move(setup));
    if (!configured.succeeded
        || !pumpThreeUntil(
            host,
            client,
            spectator,
            [&host, &client, &spectator]() {
                return host.lan().session().matchSetup().has_value()
                    && client.lan().session().matchSetup().has_value()
                    && spectator.lan().session().matchSetup().has_value();
            },
            context,
            "spectator deadline setup"
        )) {
        context.expect(false, "spectator deadline setup failed");
        return false;
    }

    const RuntimeMultiplayerLanOperationResult hostReady =
        host.lan().setReady(true);
    const RuntimeMultiplayerLanOperationResult clientReady =
        client.lan().setReady(true);
    const RuntimeMultiplayerLanOperationResult spectatorReady =
        spectator.lan().setReady(true);
    if (!hostReady.succeeded || !clientReady.succeeded
        || !spectatorReady.succeeded
        || !pumpThreeUntil(
            host,
            client,
            spectator,
            [&host, &client, &spectator]() {
                return host.lan().session().allMembersReady()
                    && client.lan().session().allMembersReady()
                    && spectator.lan().session().allMembersReady();
            },
            context,
            "spectator deadline ready"
        )) {
        context.expect(false, "spectator deadline ready failed");
        return false;
    }
    const RuntimeMultiplayerLanOperationResult started =
        host.lan().startMatch(1);
    return started.succeeded && pumpThreeUntil(
        host,
        client,
        spectator,
        [&host, &client, &spectator]() {
            return host.lan().phase() == RuntimeMultiplayerLanPhase::Running
                && client.lan().phase()
                    == RuntimeMultiplayerLanPhase::Running
                && spectator.lan().phase()
                    == RuntimeMultiplayerLanPhase::Running;
        },
        context,
        "spectator deadline start"
    );
}

void runPreAttachCarrierFailure(SmokeContext& context) {
    (void)consumeRuntimeMultiplayerVisibleFailure();
    RuntimeMultiplayerRuntimeBridge bridge;
    RuntimeMultiplayerRuntimeBridge peer;
    std::string error;
    if (!startRunningPair(
            bridge,
            peer,
            context,
            0x20202020ULL,
            "pre-attach carrier fixture"
        )) {
        return;
    }
    error.clear();
    context.expect(
        bridge.adoptExternalOrderedTransport(
            std::make_unique<FailingPreAttachTransport>(),
            error
        ),
        "pre-attach carrier fixture should install: " + error
    );
    error.clear();
    context.expect(
        bridge.beginPreAttachMaintenance(error, 5000U),
        "pre-attach carrier maintenance should start: " + error
    );
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (bridge.preAttachMaintenanceRunning()
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    (void)bridge.lan();
    context.expect(
        bridge.fatal()
            && bridge.fatalReason() == "synthetic pre-attach carrier failure",
        "pre-attach carrier failure should latch after worker join"
    );
    const std::optional<std::string> visible =
        consumeRuntimeMultiplayerVisibleFailure();
    context.expect(
        visible.has_value()
            && *visible == "synthetic pre-attach carrier failure",
        "pre-attach carrier failure should reach the visible mailbox"
    );
    bridge.lan().close();
    peer.lan().close();
}

void runAttachedHostStalledClientDeadline(SmokeContext& context) {
    (void)consumeRuntimeMultiplayerVisibleFailure();
    RuntimeMultiplayerLockstepConfig config;
    config.inputDelayTicks = 0;
    RuntimeMultiplayerRuntimeBridge host(config);
    RuntimeMultiplayerRuntimeBridge client(config);
    if (!startRunningPair(
            host,
            client,
            context,
            0x30303030ULL,
            "attached-host stalled-client deadline"
        )) {
        return;
    }

    std::string error;
    context.expect(
        host.beginPreAttachMaintenance(error, 250U),
        "attached-host deadline maintenance should start: " + error
    );
    error.clear();
    context.expect(
        client.beginPreAttachMaintenance(error, 5000U),
        "stalled client maintenance should start: " + error
    );
    context.expect(
        waitForMaintenancePumps(host, client, 3U),
        "stalled-loader peers should remain healthy before attach"
    );

    FakeWorld hostWorld;
    error.clear();
    context.expect(
        host.attachRuntime(callbacksFor(hostWorld), error),
        "deadline host should attach: " + error
    );
    const std::uint64_t hostMaintenancePumps =
        host.preAttachMaintenancePumpCount();
    bool timedOut = false;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline && !timedOut) {
        const RuntimeMultiplayerRuntimePumpResult pumped = host.pump(false);
        timedOut = !pumped.succeeded && host.fatal();
        if (!timedOut) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        timedOut
            && host.fatalReason()
                == "multiplayer initial loading timed out waiting for peer 'client-peer'"
            && host.preAttachMaintenancePumpCount() == hostMaintenancePumps,
        "attached host should abort one healthy client that never attaches"
    );

    const auto clientDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (client.preAttachMaintenanceRunning()
        && std::chrono::steady_clock::now() < clientDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const RuntimeMultiplayerLanPhase clientPhase = client.lan().phase();
    context.expect(
        host.lan().phase() == RuntimeMultiplayerLanPhase::Aborted
            && clientPhase == RuntimeMultiplayerLanPhase::Aborted
            && host.lan().session().abortState().affectedPeerId
                == "client-peer"
            && host.lan().session().abortState().reason
                == RuntimeMultiplayerSessionAbortReason::TransportFailure,
        "loading timeout should replicate one canonical SessionAbort"
    );
    host.detachRuntime();
    host.lan().close();
    client.lan().close();
    (void)consumeRuntimeMultiplayerVisibleFailure();
}

void runUnattachedHostDeadline(SmokeContext& context) {
    (void)consumeRuntimeMultiplayerVisibleFailure();
    RuntimeMultiplayerRuntimeBridge host;
    RuntimeMultiplayerRuntimeBridge client;
    if (!startRunningPair(
            host,
            client,
            context,
            0x40404040ULL,
            "unattached-host deadline"
        )) {
        return;
    }
    std::string error;
    context.expect(
        host.beginPreAttachMaintenance(error, 200U),
        "unattached host maintenance should start: " + error
    );
    error.clear();
    context.expect(
        client.beginPreAttachMaintenance(error, 5000U),
        "unattached client maintenance should start: " + error
    );
    context.expect(
        waitForMaintenancePumps(host, client, 3U),
        "unattached peers should pump before the deadline"
    );
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (host.preAttachMaintenanceRunning()
        && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const RuntimeMultiplayerLanPhase hostPhase = host.lan().phase();
    context.expect(
        host.fatal()
            && host.fatalReason().find(
                "multiplayer initial loading timed out"
            ) == 0U
            && hostPhase == RuntimeMultiplayerLanPhase::Aborted,
        "host that never attaches should still end the sealed session"
    );
    const auto clientDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (client.preAttachMaintenanceRunning()
        && std::chrono::steady_clock::now() < clientDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    (void)client.lan();
    host.lan().close();
    client.lan().close();
    (void)consumeRuntimeMultiplayerVisibleFailure();
}

void runCompletedHashCancelsDeadline(SmokeContext& context) {
    (void)consumeRuntimeMultiplayerVisibleFailure();
    RuntimeMultiplayerLockstepConfig config;
    config.inputDelayTicks = 0;
    RuntimeMultiplayerRuntimeBridge host(config);
    RuntimeMultiplayerRuntimeBridge client(config);
    if (!startRunningPair(
            host,
            client,
            context,
            0x50505050ULL,
            "completed-hash deadline"
        )) {
        return;
    }
    std::string error;
    context.expect(
        host.beginPreAttachMaintenance(error, 500U),
        "completed-hash host maintenance should start: " + error
    );
    error.clear();
    context.expect(
        client.beginPreAttachMaintenance(error, 500U),
        "completed-hash client maintenance should start: " + error
    );
    context.expect(
        waitForMaintenancePumps(host, client, 3U),
        "completed-hash peers should pump before attach"
    );
    FakeWorld hostWorld;
    FakeWorld clientWorld;
    error.clear();
    const bool hostAttached = host.attachRuntime(
        callbacksFor(hostWorld),
        error
    );
    context.expect(hostAttached, "completed-hash host attach: " + error);
    error.clear();
    const bool clientAttached = client.attachRuntime(
        callbacksFor(clientWorld),
        error
    );
    context.expect(clientAttached, "completed-hash client attach: " + error);
    if (!hostAttached || !clientAttached) return;
    const std::uint64_t hostPumps = host.preAttachMaintenancePumpCount();
    const std::uint64_t clientPumps = client.preAttachMaintenancePumpCount();
    const bool verified = pumpUntil(
        host,
        client,
        [&host, &client]() {
            return host.initialStateHashVerified()
                && client.initialStateHashVerified();
        },
        context,
        "completed initial hash"
    );
    if (!verified) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    const RuntimeMultiplayerRuntimePumpResult hostPump = host.pump(false);
    const RuntimeMultiplayerRuntimePumpResult clientPump = client.pump(false);
    context.expect(
        hostPump.succeeded && clientPump.succeeded
            && !host.fatal() && !client.fatal()
            && host.preAttachMaintenancePumpCount() == hostPumps
            && client.preAttachMaintenancePumpCount() == clientPumps,
        "completed initial hashes should cancel the loading deadline without double pumping"
    );
    host.detachRuntime();
    client.detachRuntime();
    host.lan().close();
    client.lan().close();
    (void)consumeRuntimeMultiplayerVisibleFailure();
}

void runStalledSpectatorIsDropped(SmokeContext& context) {
    (void)consumeRuntimeMultiplayerVisibleFailure();
    RuntimeMultiplayerLockstepConfig config;
    config.inputDelayTicks = 0;
    RuntimeMultiplayerRuntimeBridge host(config);
    RuntimeMultiplayerRuntimeBridge client(config);
    RuntimeMultiplayerRuntimeBridge spectator(config);
    if (!startRunningTrio(host, client, spectator, context)) return;

    std::string error;
    context.expect(
        host.beginPreAttachMaintenance(error, 250U),
        "spectator deadline host maintenance: " + error
    );
    error.clear();
    context.expect(
        client.beginPreAttachMaintenance(error, 5000U),
        "spectator deadline client maintenance: " + error
    );
    error.clear();
    context.expect(
        spectator.beginPreAttachMaintenance(error, 5000U),
        "spectator deadline stalled maintenance: " + error
    );
    const auto maintenanceDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < maintenanceDeadline
        && (host.preAttachMaintenancePumpCount() < 3U
            || client.preAttachMaintenancePumpCount() < 3U
            || spectator.preAttachMaintenancePumpCount() < 3U)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    FakeWorld hostWorld;
    FakeWorld clientWorld;
    error.clear();
    const bool hostAttached = host.attachRuntime(
        callbacksFor(hostWorld),
        error
    );
    context.expect(hostAttached, "spectator deadline host attach: " + error);
    error.clear();
    const bool clientAttached = client.attachRuntime(
        callbacksFor(clientWorld),
        error
    );
    context.expect(
        clientAttached,
        "spectator deadline client attach: " + error
    );
    if (!hostAttached || !clientAttached) return;

    bool continued = false;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(4);
    while (std::chrono::steady_clock::now() < deadline && !continued) {
        const RuntimeMultiplayerRuntimePumpResult hostPump = host.pump(true);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            client.pump(false);
        if (!hostPump.succeeded || !clientPump.succeeded) break;
        const auto spectatorAbsent = [](const auto& session) {
            return std::none_of(
                session.members().begin(),
                session.members().end(),
                [](const RuntimeMultiplayerRoomMember& member) {
                    return member.peerId == "spectator-peer";
                }
            );
        };
        continued = spectatorAbsent(host.lan().session())
            && spectatorAbsent(client.lan().session())
            && host.initialStateHashVerified()
            && client.initialStateHashVerified()
            && hostWorld.tick >= 1 && clientWorld.tick >= 1;
        if (!continued) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        continued && !host.fatal() && !client.fatal()
            && host.lan().phase() == RuntimeMultiplayerLanPhase::Running
            && client.lan().phase() == RuntimeMultiplayerLanPhase::Running,
        "stalled spectator should leave canonically while combatants continue"
    );

    const auto spectatorDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (spectator.preAttachMaintenanceRunning()
        && std::chrono::steady_clock::now() < spectatorDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    (void)spectator.lan();
    host.detachRuntime();
    client.detachRuntime();
    host.lan().close();
    client.lan().close();
    spectator.lan().close();
    (void)consumeRuntimeMultiplayerVisibleFailure();
}

RuntimeMultiplayerGameplayCommand stopCommand() {
    RuntimeMultiplayerGameplayStopCommand stop;
    stop.objectIds = {9, 4, 9};
    RuntimeMultiplayerGameplayCommand command;
    command.body = std::move(stop);
    return command;
}

RuntimeMultiplayerGameplayCommand guardCommand() {
    RuntimeMultiplayerGameplayGuardCommand guard;
    guard.objectIds = {7, 3};
    RuntimeMultiplayerGameplayCommand command;
    command.body = std::move(guard);
    return command;
}

void runReconnectSnapshotRecovery(SmokeContext& context, bool inFlightAdvance = false) {
    (void)consumeRuntimeMultiplayerVisibleFailure();
    RuntimeMultiplayerLockstepConfig config;
    config.inputDelayTicks = 0;
    RuntimeMultiplayerRuntimeBridge host(config);
    RuntimeMultiplayerRuntimeBridge client(config);
    if (!startRunningPair(
            host,
            client,
            context,
            0x70707070ULL,
            "reconnect snapshot recovery"
        )) {
        return;
    }

    FakeWorld hostWorld;
    FakeWorld clientWorld;
    std::string error;
    const bool hostAttached = host.attachRuntime(
        callbacksFor(hostWorld),
        error
    );
    context.expect(hostAttached, "reconnect host attach: " + error);
    error.clear();
    const bool clientAttached = client.attachRuntime(
        callbacksFor(clientWorld),
        error
    );
    context.expect(clientAttached, "reconnect client attach: " + error);
    if (!hostAttached || !clientAttached) return;

    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.initialStateHashVerified()
                    && client.initialStateHashVerified();
            },
            context,
            "reconnect initial hash"
        )) {
        return;
    }

    error.clear();
    context.expect(
        host.captureGameplayCommand(stopCommand(), error),
        "reconnect host command capture: " + error
    );
    error.clear();
    context.expect(
        client.captureGameplayCommand(guardCommand(), error),
        "reconnect client command capture: " + error
    );

    bool stableBeforeDisconnect = false;
    const auto stableDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < stableDeadline
        && !stableBeforeDisconnect) {
        const RuntimeMultiplayerRuntimePumpResult hostPump = host.pump(true);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            client.pump(false);
        if (!hostPump.succeeded || !clientPump.succeeded) {
            context.expect(
                false,
                "reconnect pre-disconnect pump failed: "
                    + (!hostPump.succeeded
                        ? hostPump.error
                        : clientPump.error)
            );
            break;
        }
        stableBeforeDisconnect = hostWorld.tick >= 4
            && hostWorld.tick == clientWorld.tick
            && hostWorld.applied.size() >= 2U
            && hostWorld.applied.size() == clientWorld.applied.size()
            && fakeWorldHash(hostWorld) == fakeWorldHash(clientWorld)
            && host.lan().session().stateHash()
                == client.lan().session().stateHash();
        if (!stableBeforeDisconnect) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        stableBeforeDisconnect,
        "reconnect fixture should reach an equal command/hash barrier"
    );
    if (!stableBeforeDisconnect) return;

    // Establish a known checkpoint; the second case then cuts the socket
    // while a newer authoritative frame has not reached the client.
    for (int drain = 0; drain < 8; ++drain) {
        const RuntimeMultiplayerRuntimePumpResult hostPump = host.pump(false);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            client.pump(false);
        context.expect(
            hostPump.succeeded && clientPump.succeeded,
            "reconnect checkpoint drain should remain healthy"
        );
        if (!hostPump.succeeded || !clientPump.succeeded) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        host.lan().session().stateHash()
                == client.lan().session().stateHash()
            && fakeWorldHash(hostWorld) == fakeWorldHash(clientWorld),
        "reconnect checkpoint must be fully acknowledged before transport loss"
    );
    if (host.lan().session().stateHash()
            != client.lan().session().stateHash()) {
        return;
    }

    if (inFlightAdvance) {
        const auto published = host.pump(true);
        context.expect(published.succeeded, "publish in-flight frame: " + published.error);
        context.expect(hostWorld.tick == clientWorld.tick + 1,
            "host should have completed one frame still in flight to the client");
        context.expect(host.lan().session().stateHash() != client.lan().session().stateHash(),
            "client should still hold the earlier exact checkpoint");
        if (!published.succeeded
            || host.lan().session().stateHash() == client.lan().session().stateHash()) return;
    }
    const int stableTick = hostWorld.tick;
    const std::uint64_t stableWorldHash = fakeWorldHash(hostWorld);
    const std::uint64_t epochBeforeReconnect =
        host.lan().session().timelineEpoch();
    error.clear();
    context.expect(
        client.lan().interruptClientTransportForReconnect(error),
        "running client transport should enter reconnect: " + error
    );

    bool sawPending = false;
    bool sawReconnected = false;
    bool recovered = false;
    const auto recoveryDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < recoveryDeadline
        && !recovered) {
        const RuntimeMultiplayerRuntimePumpResult hostPump = host.pump(false);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            client.pump(false);
        const auto observe = [&sawPending, &sawReconnected](
            const RuntimeMultiplayerRuntimePumpResult& pump
        ) {
            for (const RuntimeMultiplayerLanEvent& event : pump.events) {
                sawPending = sawPending || event.kind
                    == RuntimeMultiplayerLanEventKind::ReconnectPending;
                sawReconnected = sawReconnected || event.kind
                    == RuntimeMultiplayerLanEventKind::Reconnected;
            }
        };
        observe(hostPump);
        observe(clientPump);
        if (!hostPump.succeeded || !clientPump.succeeded) {
            std::cerr << "RECONNECT DIAGNOSTIC hostPhase="
                      << static_cast<int>(host.lan().phase())
                      << " clientPhase="
                      << static_cast<int>(client.lan().phase())
                      << " hostFatal='" << host.fatalReason()
                      << "' clientFatal='" << client.fatalReason()
                      << "' hostAbortPeer='"
                      << host.lan().session().abortState().affectedPeerId
                      << "' clientAbortPeer='"
                      << client.lan().session().abortState().affectedPeerId
                      << "' hostEpoch="
                      << host.lan().session().timelineEpoch()
                      << " clientEpoch="
                      << client.lan().session().timelineEpoch()
                      << " hostTick="
                      << host.lan().session().authoritativeTick()
                      << " clientTick="
                      << client.lan().session().authoritativeTick()
                      << " hostHash=" << host.lan().session().stateHash()
                      << " clientHash=" << client.lan().session().stateHash()
                      << '\n';
            for (const RuntimeMultiplayerLanEvent& event : hostPump.events) {
                std::cerr << "RECONNECT HOST EVENT kind="
                          << static_cast<int>(event.kind)
                          << " peer='" << event.peerId << "' reason='"
                          << event.reason << "'\n";
            }
            for (const RuntimeMultiplayerLanEvent& event : clientPump.events) {
                std::cerr << "RECONNECT CLIENT EVENT kind="
                          << static_cast<int>(event.kind)
                          << " peer='" << event.peerId << "' reason='"
                          << event.reason << "'\n";
            }
            context.expect(
                false,
                "reconnect recovery pump failed: "
                    + (!hostPump.succeeded
                        ? hostPump.error
                        : clientPump.error)
            );
            break;
        }
        recovered = host.lan().phase()
                == RuntimeMultiplayerLanPhase::Running
            && client.lan().phase() == RuntimeMultiplayerLanPhase::Running
            && !host.lan().reconnectPending()
            && !client.lan().reconnectPending()
            && host.lan().session().timelineEpoch()
                == epochBeforeReconnect + 1U
            && client.lan().session().timelineEpoch()
                == epochBeforeReconnect + 1U
            && host.initialStateHashVerified()
            && client.initialStateHashVerified()
            && host.lan().session().stateHash()
                == client.lan().session().stateHash();
        if (!recovered) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        recovered && sawPending && sawReconnected
            && !host.fatal() && !client.fatal(),
        "reconnect should re-authenticate and commit one recovery Load"
    );
    context.expect(
        recovered && hostWorld.tick == stableTick
            && clientWorld.tick == stableTick
            && fakeWorldHash(hostWorld) == stableWorldHash
            && fakeWorldHash(clientWorld) == stableWorldHash
            && !hostWorld.stagedSnapshot.empty()
            && hostWorld.stagedSnapshot == clientWorld.stagedSnapshot,
        "reconnect snapshot should preserve the exact authoritative barrier"
    );

    bool resumed = false;
    const auto resumeDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (recovered && std::chrono::steady_clock::now() < resumeDeadline
        && !resumed) {
        const RuntimeMultiplayerRuntimePumpResult hostPump = host.pump(true);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            client.pump(false);
        if (!hostPump.succeeded || !clientPump.succeeded) break;
        resumed = hostWorld.tick >= stableTick + 2
            && hostWorld.tick == clientWorld.tick
            && fakeWorldHash(hostWorld) == fakeWorldHash(clientWorld)
            && host.lan().session().stateHash()
                == client.lan().session().stateHash();
        if (!resumed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        resumed,
        "reconnected peers should resume equal lockstep ticks and hashes"
    );

    host.detachRuntime();
    client.detachRuntime();
    host.lan().close();
    client.lan().close();
    (void)consumeRuntimeMultiplayerVisibleFailure();
}

void runBridgeFlow(SmokeContext& context) {
    (void)consumeRuntimeMultiplayerVisibleFailure();
    RuntimeMultiplayerLockstepConfig lockstepConfig;
    lockstepConfig.inputDelayTicks = 0;
    RuntimeMultiplayerRuntimeBridge host(lockstepConfig);
    RuntimeMultiplayerRuntimeBridge client(lockstepConfig);
    std::string error;
    context.expect(host.ready(error), "host bridge should initialize: " + error);
    error.clear();
    context.expect(
        client.ready(error),
        "client bridge should initialize: " + error
    );

    RuntimeMultiplayerLanHostOptions hostOptions;
    hostOptions.session = sessionConfig();
    hostOptions.roomName = "Runtime Bridge";
    hostOptions.discoveryPort = 0;
    hostOptions.sessionPort = 0;
    hostOptions.roomGeneration = 0x10101010ULL;
    error.clear();
    context.expect(
        host.lan().startHost(hostOptions, error),
        "bridge host should start: " + error
    );

    RuntimeMultiplayerLanJoinOptions join;
    join.room = advertisementFor(host);
    join.peerId = "client-peer";
    join.displayName = "Client";
    join.compatibility = hostOptions.session.compatibility;
    join.connectTimeoutMilliseconds = 2000;
    error.clear();
    context.expect(
        client.lan().join(join, error),
        "bridge client should connect: " + error
    );
    if (!pumpUntil(
            host,
            client,
            [&host, &client]() {
                return host.lan().phase() == RuntimeMultiplayerLanPhase::Lobby
                    && client.lan().phase()
                        == RuntimeMultiplayerLanPhase::Lobby
                    && host.lan().session().members().size() == 2U
                    && client.lan().session().members().size() == 2U;
            },
            context,
            "lobby admission"
        )) {
        return;
    }

    const RuntimeMultiplayerLanOperationResult setup =
        host.lan().setMatchSetup(matchSetup());
    context.expect(
        setup.succeeded && setup.applied,
        "host setup should apply: " + setup.reason
    );
    pumpUntil(
        host,
        client,
        [&host, &client]() {
            return host.lan().session().matchSetup().has_value()
                && client.lan().session().matchSetup().has_value();
        },
        context,
        "setup replication"
    );

    const RuntimeMultiplayerLanOperationResult hostReady =
        host.lan().setReady(true);
    const RuntimeMultiplayerLanOperationResult clientReady =
        client.lan().setReady(true);
    context.expect(
        hostReady.succeeded && hostReady.applied,
        "host should become ready: " + hostReady.reason
    );
    context.expect(
        clientReady.succeeded && clientReady.queued,
        "client ready should queue: " + clientReady.reason
    );
    pumpUntil(
        host,
        client,
        [&host, &client]() {
            return host.lan().session().allMembersReady()
                && client.lan().session().allMembersReady();
        },
        context,
        "ready replication"
    );

    const RuntimeMultiplayerLanOperationResult started =
        host.lan().startMatch(1);
    context.expect(
        started.succeeded && started.applied,
        "host should start match: " + started.reason
    );
    pumpUntil(
        host,
        client,
        [&host, &client]() {
            return host.lan().phase() == RuntimeMultiplayerLanPhase::Running
                && client.lan().phase()
                    == RuntimeMultiplayerLanPhase::Running
                && host.lockstepBound() && client.lockstepBound();
        },
        context,
        "start replication"
    );

    error.clear();
    context.expect(
        host.beginPreAttachMaintenance(error),
        "host pre-attach maintenance should start: " + error
    );
    error.clear();
    context.expect(
        client.beginPreAttachMaintenance(error),
        "client pre-attach maintenance should start: " + error
    );
    const auto maintenanceDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < maintenanceDeadline
        && (host.preAttachMaintenancePumpCount() < 3U
            || client.preAttachMaintenancePumpCount() < 3U)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        host.preAttachMaintenanceRunning()
            && client.preAttachMaintenanceRunning()
            && host.preAttachMaintenancePumpCount() >= 3U
            && client.preAttachMaintenancePumpCount() >= 3U,
        "both handoff bridges should keep pumping throughout loading"
    );

    FakeWorld hostWorld;
    FakeWorld clientWorld;
    error.clear();
    context.expect(
        host.attachRuntime(callbacksFor(hostWorld), error),
        "host world should attach: " + error
    );
    error.clear();
    context.expect(
        client.attachRuntime(callbacksFor(clientWorld), error),
        "client world should attach: " + error
    );
    const std::uint64_t hostMaintenancePumps =
        host.preAttachMaintenancePumpCount();
    const std::uint64_t clientMaintenancePumps =
        client.preAttachMaintenancePumpCount();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    context.expect(
        !host.preAttachMaintenanceRunning()
            && !client.preAttachMaintenanceRunning()
            && host.preAttachMaintenancePumpCount()
                == hostMaintenancePumps
            && client.preAttachMaintenancePumpCount()
                == clientMaintenancePumps,
        "attach should join maintenance before runtime pumping begins"
    );
    context.expect(
        host.mayCaptureGameplay() && client.mayCaptureGameplay(),
        "both combatants should own input capture"
    );

    RuntimeMultiplayerSaveLoadProposal earlyProposal;
    earlyProposal.proposalId = 1U;
    const RuntimeMultiplayerLanOperationResult earlySaveLoad =
        host.proposeSaveLoad(std::move(earlyProposal), {});
    context.expect(
        !earlySaveLoad.succeeded
            && earlySaveLoad.reason
                == "waiting for all players to finish loading",
        "save/load must wait for the initial state-hash barrier"
    );

    const RuntimeMultiplayerRuntimePumpResult initialHostPump =
        host.pump(true);
    context.expect(
        initialHostPump.succeeded && hostWorld.tick == 0
            && !host.initialStateHashVerified(),
        "host must not advance before every initial world hash arrives"
    );
    const RuntimeMultiplayerRuntimePumpResult initialClientPump =
        client.pump(false);
    context.expect(
        initialClientPump.succeeded && clientWorld.tick == 0,
        "client should publish its initial world hash without advancing"
    );

    error.clear();
    context.expect(
        host.captureGameplayCommand(stopCommand(), error),
        "host stop should capture: " + error
    );
    error.clear();
    context.expect(
        client.captureGameplayCommand(guardCommand(), error),
        "client guard should capture: " + error
    );

    const auto tickDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < tickDeadline
        && (hostWorld.tick < 2 || clientWorld.tick < 2)) {
        const RuntimeMultiplayerRuntimePumpResult hostPump = host.pump(true);
        const RuntimeMultiplayerRuntimePumpResult clientPump = client.pump(false);
        context.expect(
            hostPump.succeeded,
            "host runtime pump should succeed: " + hostPump.error
        );
        context.expect(
            clientPump.succeeded,
            "client runtime pump should succeed: " + clientPump.error
        );
        if (!hostPump.succeeded || !clientPump.succeeded) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    context.expect(
        hostWorld.tick == 2 && clientWorld.tick == 2,
        "host and client should commit exactly two canonical ticks"
    );
    context.expect(
        hostWorld.applied == clientWorld.applied
            && hostWorld.applied.size() == 2U
            && hostWorld.applied[0U].peerId == "client-peer"
            && runtimeMultiplayerGameplayCommandType(
                hostWorld.applied[0U].command
            ) == RuntimeMultiplayerGameplayCommandType::Guard
            && hostWorld.applied[1U].peerId == "host-peer"
            && runtimeMultiplayerGameplayCommandType(
                hostWorld.applied[1U].command
            ) == RuntimeMultiplayerGameplayCommandType::Stop,
        "both worlds should apply the same peer-sorted authored batch"
    );
    context.expect(
        host.pendingGameplayCommandCount() == 0U
            && client.pendingGameplayCommandCount() == 0U,
        "submitted local command queues should be empty"
    );

    pumpUntil(
        host,
        client,
        [&host, &client]() {
            const auto completeFrame = [](const auto& session) {
                return std::any_of(
                    session.stateHashFrames().begin(),
                    session.stateHashFrames().end(),
                    [](const RuntimeMultiplayerStateHashFrame& frame) {
                        return frame.tick == 2 && frame.complete
                            && !frame.desync;
                    }
                );
            };
            return completeFrame(host.lan().session())
                && completeFrame(client.lan().session());
        },
        context,
        "equal state hash replication"
    );

    hostWorld.ignoreAdvance = true;
    bool committedTickMismatchObserved = false;
    const auto mismatchDeadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < mismatchDeadline
        && !committedTickMismatchObserved) {
        const RuntimeMultiplayerRuntimePumpResult hostPump = host.pump(true);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            client.pump(false);
        committedTickMismatchObserved = !hostPump.succeeded
            && hostPump.error
                == "bridge committed tick did not match runtime state";
        if (!clientPump.succeeded && !committedTickMismatchObserved) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        committedTickMismatchObserved,
        "runtime tick mismatch should become a bridge fatal"
    );
    const std::optional<std::string> visibleFatal =
        consumeRuntimeMultiplayerVisibleFailure();
    context.expect(
        visibleFatal.has_value()
            && *visibleFatal
                == "bridge committed tick did not match runtime state"
            && formatRuntimeMultiplayerVisibleFailureMessage(*visibleFatal)
                .find(*visibleFatal) != std::string::npos,
        "a concrete runtime fatal should enter the visible modal text"
    );

    host.detachRuntime();
    client.detachRuntime();
    host.lan().close();
    client.lan().close();
}

} // namespace

int main() {
    SmokeContext context;
    (void)consumeRuntimeMultiplayerVisibleFailure();
    {
        RuntimeMultiplayerRuntimeBridge neverHandedOff;
        std::string beginError;
        context.expect(
            !neverHandedOff.beginPreAttachMaintenance(beginError)
                && !beginError.empty(),
            "pre-attach maintenance should reject a non-running room"
        );
    }
    context.expect(
        !consumeRuntimeMultiplayerVisibleFailure().has_value(),
        "a begin failure before handoff must not leave a visible mailbox"
    );
    runPreAttachCarrierFailure(context);
    runAttachedHostStalledClientDeadline(context);
    runUnattachedHostDeadline(context);
    runCompletedHashCancelsDeadline(context);
    runStalledSpectatorIsDropped(context);
    runRankedResultReportIsOneShot(context);
    runReconnectSnapshotRecovery(context);
    runReconnectSnapshotRecovery(context, true);
    runBridgeFlow(context);
    const std::optional<std::string> teardownFailure =
        consumeRuntimeMultiplayerVisibleFailure();
    context.expect(
        teardownFailure.has_value()
            && *teardownFailure
                == "bridge committed tick did not match runtime state",
        "an attached fatal teardown should preserve the concrete cause"
    );
    context.expect(
        !consumeRuntimeMultiplayerVisibleFailure().has_value(),
        "the visible multiplayer failure mailbox should consume once"
    );
    if (context.failures != 0) {
        std::cerr << "Runtime multiplayer bridge smoke failed with "
                  << context.failures << " failure(s).\n";
        return 1;
    }
    std::cout << "Runtime multiplayer bridge smoke passed.\n";
    return 0;
}
