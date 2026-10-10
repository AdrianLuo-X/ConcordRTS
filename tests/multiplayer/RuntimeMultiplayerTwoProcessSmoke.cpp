#include "network/lockstep/RuntimeMultiplayerRuntimeBridge.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr DWORD childStartupTimeoutMilliseconds = 10000;
constexpr DWORD testTimeoutMilliseconds = 60000;
constexpr DWORD shutdownTimeoutMilliseconds = 10000;
constexpr LONG sharedMagic = 0x43545053L; // CTPS

enum class ProcessRole {
    Host,
    Client,
    Spectator
};

struct ProcessResult {
    volatile LONG status = 0;
    // The last cross-peer-complete hash frame is the synchronized outcome.
    // localTick may advance slightly while a participant receives the other
    // peers' reports and observes the coordinated-save commit.
    LONG completedTick = -1;
    LONG localTick = -1;
    LONG appliedCommands = 0;
    LONG completeHashFrames = 0;
    LONG snapshotStages = 0;
    LONG snapshotCommits = 0;
    LONG spectatorCaptureDenied = 0;
    std::uint64_t worldHash = 0;
    std::uint64_t sessionHash = 0;
    std::uint64_t timelineEpoch = 0;
    char message[256]{};
};

struct SharedState {
    LONG magic = sharedMagic;
    volatile LONG sessionPort = 0;
    ProcessResult host;
    ProcessResult client;
    ProcessResult spectator;
};

struct NamedHandles {
    HANDLE mapping = nullptr;
    HANDLE listening = nullptr;
    HANDLE hostDone = nullptr;
    HANDLE clientDone = nullptr;
    HANDLE spectatorDone = nullptr;
    HANDLE shutdown = nullptr;
    SharedState* shared = nullptr;
};

struct FakeWorld {
    int tick = 0;
    std::vector<RuntimeMultiplayerLockstepApplyCommand> applied;
    std::vector<std::uint8_t> stagedSnapshot;
    int snapshotStages = 0;
    int snapshotCommits = 0;

    std::uint64_t hash() const {
        std::uint64_t value = 1469598103934665603ULL;
        const auto mix = [&value](std::uint64_t input) {
            for (int shift = 56; shift >= 0; shift -= 8) {
                value ^= static_cast<std::uint8_t>(input >> shift);
                value *= 1099511628211ULL;
            }
        };
        mix(static_cast<std::uint64_t>(tick));
        mix(static_cast<std::uint64_t>(applied.size()));
        for (const auto& item : applied) {
            mix(static_cast<std::uint64_t>(item.tick));
            mix(item.frameSequence);
            mix(static_cast<std::uint64_t>(item.commandIndex));
            for (const unsigned char byte : item.peerId) mix(byte);
            mix(static_cast<std::uint64_t>(
                runtimeMultiplayerGameplayCommandType(item.command)
            ));
        }
        return value == 0 ? 1ULL : value;
    }
};

std::wstring widenAscii(std::string_view value) {
    return std::wstring(value.begin(), value.end());
}

std::wstring objectName(std::string_view token, std::wstring_view suffix) {
    return L"Local\\ConcordRTS_TwoProcess_" + widenAscii(token)
        + std::wstring(suffix);
}

void closeHandle(HANDLE& handle) {
    if (handle != nullptr) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

void closeNamedHandles(NamedHandles& handles) {
    if (handles.shared != nullptr) {
        UnmapViewOfFile(handles.shared);
        handles.shared = nullptr;
    }
    closeHandle(handles.shutdown);
    closeHandle(handles.spectatorDone);
    closeHandle(handles.clientDone);
    closeHandle(handles.hostDone);
    closeHandle(handles.listening);
    closeHandle(handles.mapping);
}

bool openChildHandles(std::string_view token, NamedHandles& handles) {
    handles.mapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        objectName(token, L"_Mapping").c_str()
    );
    if (handles.mapping == nullptr) return false;
    handles.shared = static_cast<SharedState*>(MapViewOfFile(
        handles.mapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(SharedState)
    ));
    handles.listening = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE,
        FALSE,
        objectName(token, L"_Listening").c_str()
    );
    handles.hostDone = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE,
        FALSE,
        objectName(token, L"_HostDone").c_str()
    );
    handles.clientDone = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE,
        FALSE,
        objectName(token, L"_ClientDone").c_str()
    );
    handles.spectatorDone = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE,
        FALSE,
        objectName(token, L"_SpectatorDone").c_str()
    );
    handles.shutdown = OpenEventW(
        EVENT_MODIFY_STATE | SYNCHRONIZE,
        FALSE,
        objectName(token, L"_Shutdown").c_str()
    );
    return handles.shared != nullptr
        && handles.shared->magic == sharedMagic
        && handles.listening != nullptr
        && handles.hostDone != nullptr
        && handles.clientDone != nullptr
        && handles.spectatorDone != nullptr
        && handles.shutdown != nullptr;
}

ProcessResult& resultFor(SharedState& shared, ProcessRole role) {
    switch (role) {
    case ProcessRole::Host: return shared.host;
    case ProcessRole::Client: return shared.client;
    case ProcessRole::Spectator: return shared.spectator;
    }
    return shared.host;
}

HANDLE doneEventFor(const NamedHandles& handles, ProcessRole role) {
    switch (role) {
    case ProcessRole::Host: return handles.hostDone;
    case ProcessRole::Client: return handles.clientDone;
    case ProcessRole::Spectator: return handles.spectatorDone;
    }
    return handles.hostDone;
}

void publishFailure(
    NamedHandles& handles,
    ProcessRole role,
    const std::string& message
) {
    if (handles.shared == nullptr) return;
    ProcessResult& result = resultFor(*handles.shared, role);
    strncpy_s(result.message, message.c_str(), _TRUNCATE);
    InterlockedExchange(&result.status, -1);
    SetEvent(doneEventFor(handles, role));
}

RuntimeMultiplayerSessionConfig sessionConfig() {
    RuntimeMultiplayerSessionConfig config;
    config.roomId = "two-process-room";
    config.hostPeerId = "host-peer";
    config.hostDisplayName = "Host";
    config.compatibility.protocolFingerprint = 0x1020304050607080ULL;
    config.compatibility.contentFingerprint = 0x8877665544332211ULL;
    config.maximumMembers = 3;
    config.stateHashPeriodTicks = 10;
    return config;
}

RuntimeMultiplayerMatchSetup matchSetup() {
    RuntimeMultiplayerMatchSetup setup;
    setup.revision = 1;
    setup.hostPeerId = "host-peer";
    setup.map.canonicalId = "maps/two-process.map";
    setup.map.contentFingerprint = 0x8877665544332211ULL;
    setup.rules.gameMode = "battle";
    setup.rules.rulesOverride = "mpbattlemd.ini";
    setup.rules.randomSeed = 0x3141592653589793ULL;
    setup.rules.fixedTickHz = 30;
    setup.rules.startingCredits = 10000;
    setup.rules.startingUnits = 10;
    setup.rules.techLevel = 10;
    setup.rules.gameSpeed = 2;

    RuntimeMultiplayerMatchMember host;
    host.peerId = "host-peer";
    host.displayName = "Host";
    host.identity = RuntimeMultiplayerMatchMemberIdentity::Combatant;
    RuntimeMultiplayerMatchMember client;
    client.peerId = "client-peer";
    client.displayName = "Client";
    client.identity = RuntimeMultiplayerMatchMemberIdentity::Combatant;
    RuntimeMultiplayerMatchMember spectator;
    spectator.peerId = "spectator-peer";
    spectator.displayName = "Spectator";
    spectator.identity = RuntimeMultiplayerMatchMemberIdentity::Spectator;
    setup.members = {client, host, spectator};

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
        world.tick = completedTick;
    };
    callbacks.authoritativeStateHash = [&world]() { return world.hash(); };
    callbacks.matchTerminal = []() { return false; };
    callbacks.stageVerifiedSnapshot = [&world](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::span<const std::uint8_t> bytes,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        if (world.hash() != descriptor.identity.authoritativeStateHash) {
            errorMessage = "snapshot barrier world hash mismatch";
            return false;
        }
        world.stagedSnapshot.assign(bytes.begin(), bytes.end());
        ++world.snapshotStages;
        observedHash = world.hash();
        errorMessage.clear();
        return true;
    };
    callbacks.commitStagedSnapshot = [&world](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        if (world.stagedSnapshot.empty()
            || world.hash() != descriptor.identity.authoritativeStateHash) {
            errorMessage = "snapshot commit has no matching staged world";
            return false;
        }
        ++world.snapshotCommits;
        observedHash = world.hash();
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

bool pumpUntil(
    RuntimeMultiplayerRuntimeBridge& bridge,
    bool requestHostAdvance,
    const std::function<bool()>& complete,
    std::chrono::steady_clock::time_point deadline,
    std::string& errorMessage
) {
    while (std::chrono::steady_clock::now() < deadline) {
        const RuntimeMultiplayerRuntimePumpResult result =
            bridge.pump(requestHostAdvance, 1);
        if (!result.succeeded) {
            errorMessage = result.error;
            return false;
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    errorMessage = "phase timed out";
    return false;
}

RuntimeMultiplayerSaveLoadProposal makeSaveProposal(
    const RuntimeMultiplayerSessionState& session,
    const FakeWorld& world,
    std::span<const std::uint8_t> bytes,
    std::string& errorMessage
) {
    std::uint64_t setupFingerprint = 0;
    if (!session.matchSetup().has_value()
        || !runtimeMultiplayerGameplaySetupFingerprint(
            *session.matchSetup(),
            setupFingerprint,
            errorMessage
        )) {
        return {};
    }
    RuntimeMultiplayerSnapshotIdentity identity;
    identity.proposalId = 1;
    identity.kind = RuntimeMultiplayerSnapshotKind::Save;
    identity.sourceTimelineEpoch = session.timelineEpoch();
    identity.barrierTick = session.authoritativeTick() - 1;
    identity.snapshotTick = world.tick;
    identity.snapshotToken = "two-process-save-1";
    identity.authoritativeStateHash = world.hash();
    identity.gameplaySetupFingerprint = setupFingerprint;
    RuntimeMultiplayerSnapshotDescriptor descriptor;
    if (!makeRuntimeMultiplayerSnapshotDescriptor(
            identity,
            bytes,
            descriptor,
            errorMessage
        )) {
        return {};
    }
    RuntimeMultiplayerSaveLoadProposal proposal;
    proposal.timelineEpoch = session.timelineEpoch();
    proposal.proposalId = identity.proposalId;
    proposal.kind = RuntimeMultiplayerSaveLoadKind::Save;
    proposal.proposerPeerId = session.config().hostPeerId;
    proposal.descriptor = std::move(descriptor);
    proposal.tick = identity.snapshotTick;
    proposal.snapshotId = identity.snapshotToken;
    proposal.snapshotFingerprint = proposal.descriptor.snapshotFingerprint;
    return proposal;
}

int completeHashFrameCount(const RuntimeMultiplayerSessionState& session) {
    return static_cast<int>(std::count_if(
        session.stateHashFrames().begin(),
        session.stateHashFrames().end(),
        [](const RuntimeMultiplayerStateHashFrame& frame) {
            return frame.complete && !frame.desync;
        }
    ));
}

bool hasCompleteHashAtOrAfter(
    const RuntimeMultiplayerSessionState& session,
    int tick
) {
    return std::any_of(
        session.stateHashFrames().begin(),
        session.stateHashFrames().end(),
        [tick](const RuntimeMultiplayerStateHashFrame& frame) {
            return frame.tick >= tick && frame.complete && !frame.desync;
        }
    );
}

const RuntimeMultiplayerStateHashFrame* latestCompleteHashAtOrAfter(
    const RuntimeMultiplayerSessionState& session,
    int tick
) {
    const RuntimeMultiplayerStateHashFrame* latest = nullptr;
    for (const RuntimeMultiplayerStateHashFrame& frame :
            session.stateHashFrames()) {
        if (frame.tick >= tick && frame.complete && !frame.desync
            && (latest == nullptr || frame.tick > latest->tick)) {
            latest = &frame;
        }
    }
    return latest;
}

bool validateAppliedCommands(const FakeWorld& world) {
    return world.applied.size() == 2U
        && world.applied[0].peerId == "client-peer"
        && runtimeMultiplayerGameplayCommandType(world.applied[0].command)
            == RuntimeMultiplayerGameplayCommandType::Guard
        && world.applied[1].peerId == "host-peer"
        && runtimeMultiplayerGameplayCommandType(world.applied[1].command)
            == RuntimeMultiplayerGameplayCommandType::Stop;
}

bool runParticipant(
    ProcessRole role,
    NamedHandles& handles,
    std::string& errorMessage
) {
    RuntimeMultiplayerLockstepConfig lockstep;
    lockstep.inputDelayTicks = 0;
    RuntimeMultiplayerRuntimeBridge bridge(lockstep);
    if (!bridge.ready(errorMessage)) return false;

    if (role == ProcessRole::Host) {
        RuntimeMultiplayerLanHostOptions options;
        options.session = sessionConfig();
        options.roomName = "Two Process Runtime";
        options.discoveryPort = 0;
        options.sessionPort = 0;
        options.roomGeneration = 0x7766554433221100ULL;
        if (!bridge.lan().startHost(options, errorMessage)) return false;
        InterlockedExchange(
            &handles.shared->sessionPort,
            static_cast<LONG>(bridge.lan().sessionPort())
        );
        SetEvent(handles.listening);
    } else {
        const LONG port = InterlockedCompareExchange(
            &handles.shared->sessionPort,
            0,
            0
        );
        if (port <= 0 || port > 65535) {
            errorMessage = "host did not publish a valid session port";
            return false;
        }
        RuntimeMultiplayerLanJoinOptions join;
        join.room.roomId = "two-process-room";
        join.room.roomName = "Two Process Runtime";
        join.room.hostName = "Host";
        join.room.hostAddress = "127.0.0.1";
        join.room.contentFingerprint = "8877665544332211";
        join.room.protocolVersion = runtimeLanProtocolVersion;
        join.room.sessionPort = static_cast<std::uint16_t>(port);
        join.room.playerCount = 1;
        join.room.maximumPlayers = 3;
        join.peerId = role == ProcessRole::Client
            ? "client-peer"
            : "spectator-peer";
        join.displayName = role == ProcessRole::Client ? "Client" : "Spectator";
        join.participation = role == ProcessRole::Client
            ? RuntimeMultiplayerParticipationRole::Combatant
            : RuntimeMultiplayerParticipationRole::Spectator;
        join.compatibility = sessionConfig().compatibility;
        join.connectTimeoutMilliseconds = 3000;
        if (!bridge.lan().join(join, errorMessage)) return false;
    }

    const auto totalDeadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(testTimeoutMilliseconds - 5000U);
    if (!pumpUntil(
            bridge,
            false,
            [&]() {
                return bridge.lan().phase() == RuntimeMultiplayerLanPhase::Lobby
                    && bridge.lan().session().members().size() == 3U;
            },
            totalDeadline,
            errorMessage
        )) return false;

    if (role == ProcessRole::Host) {
        const RuntimeMultiplayerLanOperationResult setup =
            bridge.lan().setMatchSetup(matchSetup());
        if (!setup.succeeded || !setup.applied) {
            errorMessage = "host could not publish canonical setup: " + setup.reason;
            return false;
        }
    }
    if (!pumpUntil(
            bridge,
            false,
            [&]() { return bridge.lan().session().matchSetup().has_value(); },
            totalDeadline,
            errorMessage
        )) return false;
    if (*bridge.lan().session().matchSetup() != matchSetup()) {
        errorMessage = "canonical match setup differs between processes";
        return false;
    }

    const RuntimeMultiplayerLanOperationResult ready = bridge.lan().setReady(true);
    if (!ready.succeeded) {
        errorMessage = "ready request failed: " + ready.reason;
        return false;
    }
    if (!pumpUntil(
            bridge,
            false,
            [&]() { return bridge.lan().session().allMembersReady(); },
            totalDeadline,
            errorMessage
        )) return false;

    if (role == ProcessRole::Host) {
        const RuntimeMultiplayerLanOperationResult started =
            bridge.lan().startMatch(1);
        if (!started.succeeded || !started.applied) {
            errorMessage = "host start failed: " + started.reason;
            return false;
        }
    }
    if (!pumpUntil(
            bridge,
            false,
            [&]() {
                return bridge.lan().phase() == RuntimeMultiplayerLanPhase::Running
                    && bridge.lockstepBound();
            },
            totalDeadline,
            errorMessage
        )) return false;

    FakeWorld world;
    if (!bridge.attachRuntime(callbacksFor(world), errorMessage)) return false;
    bool spectatorCaptureDenied = false;
    if (role == ProcessRole::Host) {
        if (!bridge.captureGameplayCommand(stopCommand(), errorMessage)) return false;
    } else if (role == ProcessRole::Client) {
        if (!bridge.captureGameplayCommand(guardCommand(), errorMessage)) return false;
    } else {
        std::string captureError;
        spectatorCaptureDenied = !bridge.captureGameplayCommand(
            stopCommand(),
            captureError
        ) && !captureError.empty() && !bridge.mayCaptureGameplay();
        if (!spectatorCaptureDenied) {
            errorMessage = "spectator gameplay capture was not rejected";
            return false;
        }
    }

    std::vector<std::uint8_t> saveBytes(
        runtimeMultiplayerSnapshotChunkBytes + 4099U
    );
    for (std::size_t index = 0; index < saveBytes.size(); ++index) {
        saveBytes[index] = static_cast<std::uint8_t>(index * 29U + 7U);
    }
    bool saveProposed = false;
    while (std::chrono::steady_clock::now() < totalDeadline) {
        const RuntimeMultiplayerRuntimePumpResult pump = bridge.pump(
            role == ProcessRole::Host,
            1
        );
        if (!pump.succeeded) {
            errorMessage = pump.error;
            return false;
        }
        // The snapshot barrier must be chosen only after every command issued
        // for this scenario has been admitted and applied. Starting a save as
        // soon as tick 120 is reached races a still-in-flight client command;
        // the host then correctly rejects that command while the save/load
        // handshake holds command admission.
        if (role == ProcessRole::Host
            && world.tick >= 120
            && validateAppliedCommands(world)
            && !saveProposed) {
            RuntimeMultiplayerSaveLoadProposal proposal = makeSaveProposal(
                bridge.lan().session(),
                world,
                saveBytes,
                errorMessage
            );
            if (!errorMessage.empty()) return false;
            const RuntimeMultiplayerLanOperationResult proposed =
                bridge.proposeSaveLoad(std::move(proposal), saveBytes);
            if (!proposed.succeeded) {
                errorMessage = "coordinated save proposal failed: " + proposed.reason;
                return false;
            }
            saveProposed = true;
        }
        const auto& handshakes = bridge.lan().session().saveLoadHandshakes();
        const bool saveCommitted = !handshakes.empty()
            && handshakes.back().status
                == RuntimeMultiplayerSaveLoadStatus::Committed;
        if (world.tick >= 120 && saveCommitted
            && hasCompleteHashAtOrAfter(bridge.lan().session(), 120)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto& handshakes = bridge.lan().session().saveLoadHandshakes();
    if (world.tick < 120 || !bridge.initialStateHashVerified()
        || !validateAppliedCommands(world)
        || bridge.lan().session().desyncDetected()
        || !hasCompleteHashAtOrAfter(bridge.lan().session(), 120)
        || handshakes.empty()
        || handshakes.back().status != RuntimeMultiplayerSaveLoadStatus::Committed
        || world.snapshotStages != 1 || world.snapshotCommits != 1
        || world.stagedSnapshot != saveBytes
        || bridge.lan().session().timelineEpoch() != 1U) {
        errorMessage = "runtime/hash/save invariants were not all satisfied";
        return false;
    }

    const RuntimeMultiplayerStateHashFrame* verifiedFrame =
        latestCompleteHashAtOrAfter(bridge.lan().session(), 120);
    if (verifiedFrame == nullptr || verifiedFrame->hostStateHash == 0U) {
        errorMessage = "no synchronized hash outcome was retained";
        return false;
    }

    ProcessResult& result = resultFor(*handles.shared, role);
    result.completedTick = verifiedFrame->tick;
    result.localTick = world.tick;
    result.appliedCommands = static_cast<LONG>(world.applied.size());
    result.completeHashFrames = completeHashFrameCount(bridge.lan().session());
    result.snapshotStages = world.snapshotStages;
    result.snapshotCommits = world.snapshotCommits;
    result.spectatorCaptureDenied = spectatorCaptureDenied ? 1L : 0L;
    result.worldHash = verifiedFrame->hostStateHash;
    result.sessionHash = bridge.lan().session().stateHash();
    result.timelineEpoch = bridge.lan().session().timelineEpoch();
    strncpy_s(result.message, "ok", _TRUNCATE);
    InterlockedExchange(&result.status, 1);
    SetEvent(doneEventFor(handles, role));

    DWORD shutdownWait = WAIT_TIMEOUT;
    const auto shutdownDeadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(shutdownTimeoutMilliseconds);
    while (std::chrono::steady_clock::now() < shutdownDeadline) {
        shutdownWait = WaitForSingleObject(handles.shutdown, 0);
        if (shutdownWait == WAIT_OBJECT_0) break;
        const RuntimeMultiplayerRuntimePumpResult keepAlive = bridge.pump(false, 1);
        if (!keepAlive.succeeded) {
            if (WaitForSingleObject(handles.shutdown, 0) == WAIT_OBJECT_0) {
                shutdownWait = WAIT_OBJECT_0;
                break;
            }
            errorMessage = "shutdown keep-alive failed: " + keepAlive.error;
            bridge.detachRuntime();
            bridge.lan().close();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    bridge.detachRuntime();
    bridge.lan().close();
    if (shutdownWait != WAIT_OBJECT_0) {
        errorMessage = "parent did not issue bounded shutdown";
        return false;
    }
    return true;
}

int childMain(ProcessRole role, std::string_view token) {
    NamedHandles handles;
    if (!openChildHandles(token, handles)) {
        closeNamedHandles(handles);
        return 2;
    }
    std::string error;
    const bool succeeded = runParticipant(role, handles, error);
    if (!succeeded) {
        publishFailure(handles, role, error.empty() ? "unknown child failure" : error);
        WaitForSingleObject(handles.shutdown, 2000);
    }
    closeNamedHandles(handles);
    return succeeded ? 0 : 1;
}

bool createParentHandles(std::string_view token, NamedHandles& handles) {
    handles.mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(SharedState)),
        objectName(token, L"_Mapping").c_str()
    );
    if (handles.mapping == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
        return false;
    }
    handles.shared = static_cast<SharedState*>(MapViewOfFile(
        handles.mapping,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(SharedState)
    ));
    if (handles.shared == nullptr) return false;
    std::memset(handles.shared, 0, sizeof(SharedState));
    handles.shared->magic = sharedMagic;
    const auto makeEvent = [&](std::wstring_view suffix) {
        return CreateEventW(
            nullptr,
            TRUE,
            FALSE,
            objectName(token, suffix).c_str()
        );
    };
    handles.listening = makeEvent(L"_Listening");
    handles.hostDone = makeEvent(L"_HostDone");
    handles.clientDone = makeEvent(L"_ClientDone");
    handles.spectatorDone = makeEvent(L"_SpectatorDone");
    handles.shutdown = makeEvent(L"_Shutdown");
    return handles.listening != nullptr && handles.hostDone != nullptr
        && handles.clientDone != nullptr && handles.spectatorDone != nullptr
        && handles.shutdown != nullptr;
}

struct ChildProcess {
    PROCESS_INFORMATION info{};
};

bool launchChild(
    const std::wstring& executable,
    std::string_view token,
    std::wstring_view role,
    HANDLE job,
    ChildProcess& child,
    std::string& errorMessage
) {
    std::wstring command = L"\"" + executable + L"\" --child "
        + std::wstring(role) + L" " + widenAscii(token);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(
            executable.c_str(),
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &child.info
        )) {
        errorMessage = "CreateProcessW failed: " + std::to_string(GetLastError());
        return false;
    }
    if (!AssignProcessToJobObject(job, child.info.hProcess)) {
        errorMessage = "AssignProcessToJobObject failed: "
            + std::to_string(GetLastError());
        TerminateProcess(child.info.hProcess, 3);
        CloseHandle(child.info.hThread);
        CloseHandle(child.info.hProcess);
        child.info = {};
        return false;
    }
    return true;
}

void closeChild(ChildProcess& child) {
    closeHandle(child.info.hThread);
    closeHandle(child.info.hProcess);
}

std::wstring executablePath() {
    std::vector<wchar_t> buffer(32768);
    const DWORD size = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );
    if (size == 0 || size >= buffer.size()) return {};
    return std::wstring(buffer.data(), size);
}

bool successfulResult(const ProcessResult& result, bool spectator) {
    return result.status == 1 && result.completedTick >= 120
        && result.localTick >= result.completedTick
        && result.localTick - result.completedTick <= 2
        && result.appliedCommands == 2 && result.completeHashFrames >= 12
        && result.snapshotStages == 1 && result.snapshotCommits == 1
        && result.timelineEpoch == 1U
        && (!spectator || result.spectatorCaptureDenied == 1);
}

std::string describeResult(const ProcessResult& result) {
    return std::string(result.message)
        + "[status=" + std::to_string(result.status)
        + ",verifiedTick=" + std::to_string(result.completedTick)
        + ",localTick=" + std::to_string(result.localTick)
        + ",commands=" + std::to_string(result.appliedCommands)
        + ",hashFrames=" + std::to_string(result.completeHashFrames)
        + ",stages=" + std::to_string(result.snapshotStages)
        + ",commits=" + std::to_string(result.snapshotCommits)
        + ",spectatorDenied=" + std::to_string(result.spectatorCaptureDenied)
        + ",epoch=" + std::to_string(result.timelineEpoch) + "]";
}

DWORD remainingTimeout(ULONGLONG deadline, DWORD maximum) {
    const ULONGLONG now = GetTickCount64();
    if (now >= deadline) return 0;
    return static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, maximum));
}

int parentMain(int iteration) {
    const ULONGLONG overallDeadline =
        GetTickCount64() + testTimeoutMilliseconds;
    const std::string token = std::to_string(GetCurrentProcessId()) + "_"
        + std::to_string(GetTickCount64()) + "_"
        + std::to_string(iteration);
    NamedHandles handles;
    ChildProcess host;
    ChildProcess client;
    ChildProcess spectator;
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    bool passed = false;
    std::string error;

    if (job == nullptr || !createParentHandles(token, handles)) {
        error = "could not create unique named process coordination objects";
    } else {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                job,
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits)
            )) {
            error = "could not configure child process job";
        }
    }
    const std::wstring executable = executablePath();
    if (error.empty() && executable.empty()) error = "could not resolve test executable";
    if (error.empty()) {
        (void)launchChild(executable, token, L"host", job, host, error);
    }
    if (error.empty()) {
        const std::array<HANDLE, 3> hostStartup{
            handles.listening,
            handles.hostDone,
            host.info.hProcess
        };
        const DWORD wait = WaitForMultipleObjects(
            static_cast<DWORD>(hostStartup.size()),
            hostStartup.data(),
            FALSE,
            remainingTimeout(
                overallDeadline,
                childStartupTimeoutMilliseconds
            )
        );
        if (wait != WAIT_OBJECT_0) {
            error = handles.shared->host.message[0] != '\0'
                ? handles.shared->host.message
                : "host did not publish a listening port before timeout";
        }
    }
    if (error.empty()) {
        (void)launchChild(executable, token, L"client", job, client, error);
    }
    if (error.empty()) {
        (void)launchChild(executable, token, L"spectator", job, spectator, error);
    }

    if (error.empty()) {
        while (GetTickCount64() < overallDeadline) {
            const LONG hostStatus = InterlockedCompareExchange(
                &handles.shared->host.status, 0, 0);
            const LONG clientStatus = InterlockedCompareExchange(
                &handles.shared->client.status, 0, 0);
            const LONG spectatorStatus = InterlockedCompareExchange(
                &handles.shared->spectator.status, 0, 0);
            if (hostStatus != 0 && clientStatus != 0 && spectatorStatus != 0) break;
            if (hostStatus < 0 || clientStatus < 0 || spectatorStatus < 0) break;
            const std::array<HANDLE, 3> processes{
                host.info.hProcess,
                client.info.hProcess,
                spectator.info.hProcess
            };
            const DWORD exited = WaitForMultipleObjects(
                static_cast<DWORD>(processes.size()),
                processes.data(),
                FALSE,
                20
            );
            if (exited >= WAIT_OBJECT_0
                && exited < WAIT_OBJECT_0
                    + static_cast<DWORD>(processes.size())) {
                break;
            }
        }
        // A failing participant tears down its transport before publishing its
        // shared result. Give the other processes a bounded opportunity to
        // observe that disconnect and publish their own diagnostic instead of
        // reporting two misleading zero-initialized results.
        const ULONGLONG settleDeadline = (std::min)(
            overallDeadline,
            GetTickCount64() + 1000ULL
        );
        while (GetTickCount64() < settleDeadline) {
            const LONG hostStatus = InterlockedCompareExchange(
                &handles.shared->host.status, 0, 0);
            const LONG clientStatus = InterlockedCompareExchange(
                &handles.shared->client.status, 0, 0);
            const LONG spectatorStatus = InterlockedCompareExchange(
                &handles.shared->spectator.status, 0, 0);
            if (hostStatus != 0 && clientStatus != 0 && spectatorStatus != 0) {
                break;
            }
            Sleep(1);
        }
        const ProcessResult& hostResult = handles.shared->host;
        const ProcessResult& clientResult = handles.shared->client;
        const ProcessResult& spectatorResult = handles.shared->spectator;
        passed = successfulResult(hostResult, false)
            && successfulResult(clientResult, false)
            && successfulResult(spectatorResult, true)
            && hostResult.worldHash == clientResult.worldHash
            && hostResult.worldHash == spectatorResult.worldHash;
        if (!passed) {
            error = "child result mismatch: host="
                + describeResult(hostResult) + " client="
                + describeResult(clientResult) + " spectator="
                + describeResult(spectatorResult);
        }
    }

    if (handles.shutdown != nullptr) SetEvent(handles.shutdown);
    std::array<HANDLE, 3> processes{
        host.info.hProcess,
        client.info.hProcess,
        spectator.info.hProcess
    };
    const bool allLaunched = std::all_of(
        processes.begin(),
        processes.end(),
        [](HANDLE process) { return process != nullptr; }
    );
    if (allLaunched) {
        const DWORD stopped = WaitForMultipleObjects(
            static_cast<DWORD>(processes.size()),
            processes.data(),
            TRUE,
            remainingTimeout(overallDeadline, shutdownTimeoutMilliseconds)
        );
        if (stopped != WAIT_OBJECT_0) {
            TerminateJobObject(job, 4);
            passed = false;
            if (error.empty()) error = "children did not shut down cleanly";
        } else {
            for (HANDLE process : processes) {
                DWORD exitCode = 1;
                if (!GetExitCodeProcess(process, &exitCode) || exitCode != 0) {
                    passed = false;
                    if (error.empty()) error = "one or more child exits were nonzero";
                }
            }
        }
    } else if (job != nullptr) {
        TerminateJobObject(job, 4);
    }

    closeChild(spectator);
    closeChild(client);
    closeChild(host);
    closeNamedHandles(handles);
    closeHandle(job);
    if (!passed) {
        std::cerr << "Runtime multiplayer two-process smoke failed: "
                  << error << "\n";
        return 1;
    }
    std::cout
        << "Runtime multiplayer two-process smoke passed: "
        << "iteration=" << iteration << ' '
        << "processes=host+client+spectator ticks=120+ "
        << "commands=2 hashes=consistent save=committed shutdown=clean\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string_view(argv[1]) == "--child") {
        const std::string_view role(argv[2]);
        if (role == "host") return childMain(ProcessRole::Host, argv[3]);
        if (role == "client") return childMain(ProcessRole::Client, argv[3]);
        if (role == "spectator") return childMain(ProcessRole::Spectator, argv[3]);
        return 2;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--repeat") {
        int repeatCount = 0;
        for (const char character : std::string_view(argv[2])) {
            if (character < '0' || character > '9'
                || repeatCount > 100) {
                std::cerr << "invalid two-process repeat count\n";
                return 2;
            }
            repeatCount = repeatCount * 10 + (character - '0');
        }
        if (repeatCount < 1 || repeatCount > 100) {
            std::cerr << "invalid two-process repeat count\n";
            return 2;
        }
        for (int iteration = 1; iteration <= repeatCount; ++iteration) {
            if (parentMain(iteration) != 0) return 1;
        }
        std::cout << "Runtime multiplayer two-process stability passed: "
                  << "consecutiveRuns=" << repeatCount << '\n';
        return 0;
    }
    if (argc != 1) {
        std::cerr << "unexpected two-process smoke arguments\n";
        return 2;
    }
    return parentMain(1);
}
