#include "network/internet/RuntimeInternetLobbyFlow.h"
#include "network/internet/RuntimeInternetServiceHost.h"
#include "network/transport/RuntimeInternetServiceTransport.h"
#include "network/maps/RuntimeMultiplayerCompatibilityFingerprint.h"
#include "../multiplayer/RuntimeMultiplayerResumeFixture.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <map>
#include <sstream>
#include <iomanip>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr std::string_view serviceId = "full-stack.internet.service";
constexpr std::string_view contentFingerprint = "5555666677778888";

struct SmokeContext {
    int failures = 0;
    std::string stage = "setup";

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL [" << stage << "]: " << message << '\n';
    }
};

std::uint64_t nowMilliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

bool secureRandom(std::uint8_t* destination, std::size_t size) {
    return size <= (std::numeric_limits<ULONG>::max)()
        && BCryptGenRandom(
            nullptr,
            destination,
            static_cast<ULONG>(size),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG
        ) == 0;
}

bool shiftSystemTime(
    const SYSTEMTIME& source,
    std::int64_t deltaSeconds,
    SYSTEMTIME& destination
) {
    FILETIME fileTime{};
    if (!SystemTimeToFileTime(&source, &fileTime)) return false;
    ULARGE_INTEGER ticks{};
    ticks.LowPart = fileTime.dwLowDateTime;
    ticks.HighPart = fileTime.dwHighDateTime;
    const std::int64_t shifted = static_cast<std::int64_t>(ticks.QuadPart)
        + deltaSeconds * 10000000LL;
    if (shifted <= 0) return false;
    ticks.QuadPart = static_cast<ULONGLONG>(shifted);
    fileTime.dwLowDateTime = ticks.LowPart;
    fileTime.dwHighDateTime = ticks.HighPart;
    return FileTimeToSystemTime(&fileTime, &destination) != FALSE;
}

class TestCertificate {
public:
    ~TestCertificate() { reset(); }

    bool create(std::string& errorMessage) {
        reset();
        std::array<std::uint8_t, 8U> random{};
        if (!secureRandom(random.data(), random.size())) {
            errorMessage = "could not generate test certificate key name";
            return false;
        }
        container_ = L"ConcordRTS-ServiceLobbyE2E-";
        constexpr wchar_t hex[] = L"0123456789abcdef";
        for (const std::uint8_t byte : random) {
            container_.push_back(hex[byte >> 4U]);
            container_.push_back(hex[byte & 0x0fU]);
        }
        if (!CryptAcquireContextW(
                &provider_,
                container_.c_str(),
                MS_ENH_RSA_AES_PROV_W,
                PROV_RSA_AES,
                CRYPT_NEWKEYSET
            )
            || !CryptGenKey(
                provider_,
                AT_KEYEXCHANGE,
                (2048U << 16U) | CRYPT_EXPORTABLE,
                &key_
            )) {
            errorMessage = "could not create test certificate private key";
            return false;
        }

        DWORD subjectBytes = 0U;
        if (!CertStrToNameW(
                X509_ASN_ENCODING,
                L"CN=localhost",
                CERT_X500_NAME_STR,
                nullptr,
                nullptr,
                &subjectBytes,
                nullptr
            )) {
            errorMessage = "could not size test certificate subject";
            return false;
        }
        std::vector<std::uint8_t> subject(subjectBytes);
        if (!CertStrToNameW(
                X509_ASN_ENCODING,
                L"CN=localhost",
                CERT_X500_NAME_STR,
                nullptr,
                subject.data(),
                &subjectBytes,
                nullptr
            )) {
            errorMessage = "could not encode test certificate subject";
            return false;
        }
        CERT_NAME_BLOB subjectBlob{subjectBytes, subject.data()};
        CERT_ALT_NAME_ENTRY alternateName{};
        alternateName.dwAltNameChoice = CERT_ALT_NAME_DNS_NAME;
        alternateName.pwszDNSName = const_cast<wchar_t*>(L"localhost");
        CERT_ALT_NAME_INFO alternateNames{1U, &alternateName};
        BYTE* alternateNameBytes = nullptr;
        DWORD alternateNameSize = 0U;
        if (!CryptEncodeObjectEx(
                X509_ASN_ENCODING,
                X509_ALTERNATE_NAME,
                &alternateNames,
                CRYPT_ENCODE_ALLOC_FLAG,
                nullptr,
                &alternateNameBytes,
                &alternateNameSize
            )) {
            errorMessage = "could not encode test certificate hostname";
            return false;
        }
        LPSTR usageIdentifiers[] = {
            const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH)
        };
        CERT_ENHKEY_USAGE usage{1U, usageIdentifiers};
        BYTE* usageBytes = nullptr;
        DWORD usageSize = 0U;
        if (!CryptEncodeObjectEx(
                X509_ASN_ENCODING,
                X509_ENHANCED_KEY_USAGE,
                &usage,
                CRYPT_ENCODE_ALLOC_FLAG,
                nullptr,
                &usageBytes,
                &usageSize
            )) {
            LocalFree(alternateNameBytes);
            errorMessage = "could not encode test certificate usage";
            return false;
        }
        CERT_EXTENSION extensions[2]{};
        extensions[0].pszObjId = const_cast<LPSTR>(szOID_SUBJECT_ALT_NAME2);
        extensions[0].Value = {alternateNameSize, alternateNameBytes};
        extensions[1].pszObjId = const_cast<LPSTR>(szOID_ENHANCED_KEY_USAGE);
        extensions[1].Value = {usageSize, usageBytes};
        CERT_EXTENSIONS extensionList{2U, extensions};
        CRYPT_KEY_PROV_INFO keyProvider{};
        keyProvider.pwszContainerName = container_.data();
        keyProvider.pwszProvName = const_cast<wchar_t*>(MS_ENH_RSA_AES_PROV_W);
        keyProvider.dwProvType = PROV_RSA_AES;
        keyProvider.dwKeySpec = AT_KEYEXCHANGE;
        CRYPT_ALGORITHM_IDENTIFIER signature{};
        signature.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
        SYSTEMTIME current{};
        SYSTEMTIME validFrom{};
        SYSTEMTIME validUntil{};
        GetSystemTime(&current);
        if (shiftSystemTime(current, -86400, validFrom)
            && shiftSystemTime(current, 7 * 86400, validUntil)) {
            certificate_ = CertCreateSelfSignCertificate(
                static_cast<HCRYPTPROV_OR_NCRYPT_KEY_HANDLE>(provider_),
                &subjectBlob,
                0U,
                &keyProvider,
                &signature,
                &validFrom,
                &validUntil,
                &extensionList
            );
        }
        LocalFree(usageBytes);
        LocalFree(alternateNameBytes);
        if (certificate_ == nullptr
            || !CertSetCertificateContextProperty(
                certificate_,
                CERT_KEY_PROV_INFO_PROP_ID,
                0U,
                &keyProvider
            )) {
            errorMessage = "could not create local test certificate";
            return false;
        }
        rootStore_ = CertOpenStore(
            CERT_STORE_PROV_MEMORY,
            0U,
            0,
            0U,
            nullptr
        );
        CERT_CHAIN_ENGINE_CONFIG chainConfig{};
        chainConfig.cbSize = sizeof(chainConfig);
        chainConfig.hExclusiveRoot = rootStore_;
        if (rootStore_ == nullptr
            || !CertAddCertificateContextToStore(
                rootStore_,
                certificate_,
                CERT_STORE_ADD_NEW,
                nullptr
            )
            || !CertCreateCertificateChainEngine(
                &chainConfig,
                &chainEngine_
            )) {
            errorMessage = "could not create in-memory test trust chain";
            return false;
        }
        errorMessage.clear();
        return true;
    }

    PCCERT_CONTEXT certificate() const noexcept { return certificate_; }
    HCERTCHAINENGINE chainEngine() const noexcept { return chainEngine_; }

private:
    void reset() noexcept {
        if (chainEngine_ != nullptr) {
            CertFreeCertificateChainEngine(chainEngine_);
            chainEngine_ = nullptr;
        }
        if (rootStore_ != nullptr) {
            CertCloseStore(rootStore_, 0U);
            rootStore_ = nullptr;
        }
        if (certificate_ != nullptr) {
            CertFreeCertificateContext(certificate_);
            certificate_ = nullptr;
        }
        if (key_ != 0) {
            CryptDestroyKey(key_);
            key_ = 0;
        }
        if (provider_ != 0) {
            CryptReleaseContext(provider_, 0U);
            provider_ = 0;
        }
        if (!container_.empty()) {
            HCRYPTPROV deletion = 0;
            (void)CryptAcquireContextW(
                &deletion,
                container_.c_str(),
                MS_ENH_RSA_AES_PROV_W,
                PROV_RSA_AES,
                CRYPT_DELETEKEYSET
            );
            container_.clear();
        }
    }

    std::wstring container_;
    HCRYPTPROV provider_ = 0;
    HCRYPTKEY key_ = 0;
    PCCERT_CONTEXT certificate_ = nullptr;
    HCERTSTORE rootStore_ = nullptr;
    HCERTCHAINENGINE chainEngine_ = nullptr;
};

std::vector<std::uint8_t> credentialFor(std::string_view peerId) {
    std::string text = "full-stack-credential-for-";
    text.append(peerId);
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

RuntimeInternetTlsClientConfig tlsClientConfig(
    std::uint16_t port,
    HCERTCHAINENGINE chainEngine
) {
    RuntimeInternetTlsClientConfig config;
    config.endpoint = {"127.0.0.1", port};
    config.serverName = "localhost";
    config.checkCertificateRevocation = false;
    config.nativeCertificateChainEngine = chainEngine;
    config.limits.connectTimeoutMilliseconds = 5000U;
    config.limits.handshakeTimeoutMilliseconds = 10000U;
    return config;
}

#include "RuntimeInternetReconnectFaults.inl"

std::unique_ptr<RuntimeInternetServiceAdapter> serviceAdapter(
    const std::string& peerId,
    std::uint16_t port,
    HCERTCHAINENGINE chainEngine,
    std::string& errorMessage
) {
    auto transport = std::make_unique<RuntimeInternetServiceTransport>();
    RuntimeInternetServiceTransportConfig config;
    config.serviceId = std::string(serviceId);
    config.peerId = peerId;
    config.credential = credentialFor(peerId);
    config.coordinatorTls = tlsClientConfig(port, chainEngine);
    config.relayTls = tlsClientConfig(port, chainEngine);
    config.applicationAuthenticationTimeoutMilliseconds = 5000U;
    config.maximumPendingRelays = 8U;
    if (!transport->open(std::move(config), errorMessage)) return {};
    auto fault = std::make_shared<ReconnectFaultControl>();
    reconnectFaults[peerId] = fault;
    return std::make_unique<ReconnectFaultAdapter>(std::move(transport), std::move(fault));
}

RuntimeMultiplayerCompatibility compatibility() {
    RuntimeMultiplayerCompatibility result;
    result.protocolFingerprint = 0x1111222233334444ULL;
    result.contentFingerprint = 0x5555666677778888ULL;
    return result;
}

RuntimeInternetCandidate candidate(std::uint16_t port) {
    RuntimeInternetCandidate result;
    result.kind = RuntimeInternetCandidateKind::ServerReflexive;
    result.endpoint = {
        "192.0.2." + std::to_string(port % 200U + 1U),
        port
    };
    result.priority = 100U;
    return result;
}

RuntimeInternetConnectionConfig connectionConfig(
    const std::string& peerId,
    RuntimeInternetRole role,
    std::uint64_t transactionId,
    const std::string& roomId,
    std::uint16_t candidatePort
) {
    RuntimeInternetConnectionConfig config;
    config.serviceId = std::string(serviceId);
    config.localPeerId = peerId;
    config.roomId = roomId;
    config.role = role;
    config.contentFingerprint = std::string(contentFingerprint);
    config.localCandidates = {candidate(candidatePort)};
    config.transactionId = transactionId;
    config.coordinatorHeartbeatMilliseconds = 1000U;
    config.retryMilliseconds = 25U;
    config.coordinatorTimeoutMilliseconds = 5000U;
    config.punchTimeoutMilliseconds = 25U;
    config.relayTimeoutMilliseconds = 5000U;
    config.maximumRetries = 64U;
    return config;
}

RuntimeInternetConnectionConfig queryConfig(
    const std::string& peerId,
    std::uint64_t transactionId
) {
    RuntimeInternetConnectionConfig config = connectionConfig(
        peerId,
        RuntimeInternetRole::Client,
        transactionId,
        {},
        40000U
    );
    config.localCandidates.clear();
    return config;
}

RuntimeInternetRoomRecord roomRecord() {
    RuntimeInternetRoomRecord room;
    room.roomId = "full-stack-room";
    room.roomName = "Full Stack Room";
    room.hostPeerId = "host-peer";
    room.contentFingerprint = std::string(contentFingerprint);
    room.playerCount = 1U;
    room.maximumPlayers = 2U;
    return room;
}

RuntimeLanRoomAdvertisement canonicalAdvertisement(
    const RuntimeInternetRoomRecord& room
) {
    RuntimeLanRoomAdvertisement result;
    result.roomId = room.roomId;
    result.roomName = room.roomName;
    result.hostName = "Host";
    result.hostAddress = "authenticated-relay.invalid";
    result.contentFingerprint = room.contentFingerprint;
    result.protocolVersion = runtimeLanProtocolVersion;
    result.sessionPort = 1U;
    result.playerCount = room.playerCount;
    result.maximumPlayers = room.maximumPlayers;
    result.passwordProtected = room.passwordProtected;
    return result;
}

std::uint64_t mapFingerprint(
    const std::filesystem::path& path,
    SmokeContext& context
) {
    RuntimeMultiplayerContentComponent component;
    std::string error;
    context.expect(
        loadRuntimeMultiplayerContentComponentFile(
            path,
            "multimd/service-lobby-e2e.map",
            component,
            error
        ),
        "fixture map should load: " + error
    );
    std::uint64_t fingerprint = 0U;
    context.expect(
        runtimeMultiplayerContentFingerprint(
            {component},
            fingerprint,
            error
        ),
        "fixture map should fingerprint: " + error
    );
    return fingerprint;
}

RuntimeMultiplayerLobbyHostCreateOptions lobbyOptions(
    std::uint64_t mapContentFingerprint
) {
    RuntimeMultiplayerLobbyHostCreateOptions options;
    options.map.canonicalId = "multimd/service-lobby-e2e.map";
    options.map.contentFingerprint = mapContentFingerprint;
    options.rules.gameMode = "battle";
    options.rules.rulesOverride = "mpbattlemd.ini";
    options.rules.randomSeed = 0x1029384756ULL;
    options.rules.fixedTickHz = 30;
    options.rules.startingCredits = 10000;
    options.rules.startingUnits = 10;
    options.rules.techLevel = 10;
    options.rules.gameSpeed = 2;
    options.hostMember.peerId = "host-peer";
    options.hostMember.displayName = "Host";
    options.hostMember.role = RuntimeMultiplayerRoomMemberRole::Host;
    options.hostMember.participation =
        RuntimeMultiplayerParticipationRole::Combatant;
    options.slotCount = 3U;
    options.computerSlotIndices = {2U};
    return options;
}

RuntimeInternetLobbyHostOptions hostOptions(
    const RuntimeInternetRoomRecord& room,
    std::uint64_t mapContentFingerprint
) {
    RuntimeInternetLobbyHostOptions options;
    options.connection = connectionConfig(
        "host-peer",
        RuntimeInternetRole::Host,
        1001U,
        room.roomId,
        41001U
    );
    options.room = room;
    options.network.session.roomId = room.roomId;
    options.network.session.hostPeerId = "host-peer";
    options.network.session.hostDisplayName = "Host";
    options.network.session.hostParticipation =
        RuntimeMultiplayerParticipationRole::Combatant;
    options.network.session.compatibility = compatibility();
    options.network.session.maximumMembers = runtimeMultiplayerMaximumMembers;
    options.network.session.stateHashPeriodTicks = 1;
    options.network.roomName = room.roomName;
    options.network.passwordProtected = false;
    options.network.roomGeneration = 0x778899ULL;
    options.lobby = lobbyOptions(mapContentFingerprint);
    options.proxy.connectTimeoutMilliseconds = 5000U;
    options.proxy.idleTimeoutMilliseconds = 0U;
    return options;
}

RuntimeInternetLobbyJoinOptions joinOptions(
    const std::string& peerId,
    const std::string& displayName,
    RuntimeInternetRole role,
    const RuntimeInternetRoomRecord& room,
    std::uint64_t transactionId,
    std::uint16_t candidatePort
) {
    RuntimeInternetLobbyJoinOptions options;
    options.connection = connectionConfig(
        peerId,
        role,
        transactionId,
        room.roomId,
        candidatePort
    );
    options.room = room;
    options.canonicalRoom = canonicalAdvertisement(room);
    options.network.peerId = peerId;
    options.network.displayName = displayName;
    options.network.participation = role == RuntimeInternetRole::Spectator
        ? RuntimeMultiplayerParticipationRole::Spectator
        : RuntimeMultiplayerParticipationRole::Combatant;
    options.network.compatibility = compatibility();
    options.network.connectTimeoutMilliseconds = 5000U;
    options.proxy.connectTimeoutMilliseconds = 5000U;
    options.proxy.idleTimeoutMilliseconds = 0U;
    return options;
}

bool hasEvent(
    const RuntimeInternetLobbyFlowPollResult& result,
    RuntimeInternetLobbyFlowEventKind kind
) {
    return std::any_of(
        result.events.begin(),
        result.events.end(),
        [kind](const RuntimeInternetLobbyFlowEvent& event) {
            return event.kind == kind;
        }
    );
}

bool pollHost(
    RuntimeInternetServiceHost& host,
    std::string& errorMessage
) {
    const RuntimeInternetServiceHostPollResult result = host.poll();
    if (!result.succeeded) {
        errorMessage = result.error;
        return false;
    }
    return true;
}

template <typename Predicate>
bool pumpFlows(
    RuntimeInternetServiceHost& host,
    std::span<RuntimeInternetLobbyFlow*> flows,
    Predicate complete,
    SmokeContext& context,
    const std::string& description,
    std::uint32_t timeoutMilliseconds = 12000U
) {
    const std::uint64_t deadline = nowMilliseconds() + timeoutMilliseconds;
    while (nowMilliseconds() < deadline) {
        std::string error;
        if (!pollHost(host, error)) {
            context.expect(false, description + ": service: " + error);
            return false;
        }
        const std::uint64_t now = nowMilliseconds();
        for (std::size_t index = 0U; index < flows.size(); ++index) {
            RuntimeInternetLobbyFlowPollResult result = flows[index]->poll(
                now,
                0
            );
            if (!result.succeeded) {
                context.expect(
                    false,
                    description + ": flow " + std::to_string(index)
                        + " failed: " + result.error
                );
                return false;
            }
            if (!pollHost(host, error)) {
                context.expect(false, description + ": service: " + error);
                return false;
            }
        }
        if (complete()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(false, description + ": timed out");
    return false;
}

struct RuntimeProbeWorld {
    int tick = 0;
    std::vector<RuntimeMultiplayerLockstepApplyCommand> applied;
    std::vector<std::uint8_t> stagedSnapshot;
};

std::uint64_t worldHash(const RuntimeProbeWorld& world) {
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
    return hash == 0U ? 1ULL : hash;
}

#include "RuntimeInternetReconnectWorld.inl"

RuntimeMultiplayerRuntimeCallbacks runtimeCallbacksFor(
    RuntimeProbeWorld& world
) {
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
    callbacks.authoritativeStateHash = [&world]() {
        return worldHash(world);
    };
    callbacks.matchTerminal = []() { return false; };
    callbacks.captureReconnectSnapshot = [&world](const RuntimeMultiplayerSnapshotIdentity& identity,
        RuntimeMultiplayerSnapshotDescriptor& descriptor, std::vector<std::uint8_t>& bytes, std::string& error) {
        bytes = encodeProbeWorld(world);
        return identity.snapshotTick == world.tick && identity.authoritativeStateHash == worldHash(world)
            && makeRuntimeMultiplayerSnapshotDescriptor(identity, bytes, descriptor, error);
    };
    callbacks.stageVerifiedSnapshot = [&world](
        const RuntimeMultiplayerSnapshotDescriptor& descriptor,
        std::span<const std::uint8_t> bytes,
        std::uint64_t& observedHash,
        std::string& errorMessage
    ) {
        world.stagedSnapshot.assign(bytes.begin(), bytes.end());
        RuntimeProbeWorld restored;
        if (!decodeProbeWorld(bytes, restored, errorMessage)) return false;
        observedHash = worldHash(restored);
        if (restored.tick != descriptor.identity.snapshotTick) return false;
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
        RuntimeProbeWorld restored;
        if (!decodeProbeWorld(world.stagedSnapshot, restored, errorMessage)) return false;
        world.tick = restored.tick;
        world.applied = std::move(restored.applied);
        observedHash = worldHash(world);
        if (world.tick != descriptor.identity.snapshotTick
            || observedHash != descriptor.identity.authoritativeStateHash) return false;
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

#include "RuntimeInternetResumeRoomSmoke.inl"
#include "RuntimeInternetRankedTlsSmoke.inl"

bool runFullStack(
    SmokeContext& context,
    const std::filesystem::path& mapPath,
    std::uint64_t mapContentFingerprint
) {
    std::string error;
    TestCertificate certificate;
    context.stage = "certificate";
    context.expect(certificate.create(error), error);
    if (context.failures != 0) return false;

    const std::vector<std::string> knownPeers{
        "host-peer", "client-peer", "spectator-peer", "seal-browser-peer"
    };
    RuntimeInternetServiceHostConfig serviceConfig;
    serviceConfig.serviceId = std::string(serviceId);
    serviceConfig.tls.port = 0U;
    serviceConfig.tls.bindScope = RuntimeInternetTlsBindScope::LoopbackIpv4;
    serviceConfig.tls.maximumConnections = 32U;
    serviceConfig.tls.nativeCertificateContext = certificate.certificate();
    serviceConfig.tls.limits.handshakeTimeoutMilliseconds = 10000U;
    serviceConfig.coordinator.serviceId = std::string(serviceId);
    serviceConfig.coordinator.udpRelayEndpoint = {"127.0.0.1", 9U};
    serviceConfig.coordinator.tcpRelayEndpoint = {"127.0.0.1", 0U};
    // Keep production heartbeat behavior but shorten leases so the loading
    // handoff below must renew them more than once instead of merely finishing
    // before the original lease can expire.
    serviceConfig.coordinator.roomLeaseMilliseconds = 2000U;
    serviceConfig.coordinator.connectionLeaseMilliseconds = 2000U;
    serviceConfig.coordinator.relayLeaseMilliseconds = 2000U;
    serviceConfig.coordinator.replayRetentionMilliseconds = 30000U;
    serviceConfig.coordinator.maximumRooms = 8U;
    serviceConfig.coordinator.maximumConnections = 32U;
    serviceConfig.coordinator.maximumConnectionsPerRoom = 8U;
    serviceConfig.coordinator.maximumSpectatorsPerRoom = 4U;
    serviceConfig.coordinator.maximumRelayRoutes = 32U;
    serviceConfig.coordinator.maximumReplayTombstones = 64U;
    serviceConfig.coordinator.nowMilliseconds = nowMilliseconds;
    serviceConfig.coordinator.secureRandomBytes = secureRandom;
    serviceConfig.matchmaking.serviceId = std::string(serviceId);
    serviceConfig.matchmaking.queueLeaseMilliseconds = 30000U;
    serviceConfig.matchmaking.matchLeaseMilliseconds = 30000U;
    serviceConfig.matchmaking.maximumQueuedPeers = 8U;
    serviceConfig.matchmaking.maximumActiveMatches = 4U;
    serviceConfig.matchmaking.nowMilliseconds = nowMilliseconds;
    serviceConfig.matchmaking.secureRandomBytes = secureRandom;
    serviceConfig.community.rankedPolicy = {30000, 1000, 300000};
    serviceConfig.community.statePath = mapPath.parent_path() / (mapPath.stem().string() + ".ranked.state");
    serviceConfig.relay.maximumQueuedBytes = 2U * 1024U * 1024U;
    serviceConfig.relay.idleTimeoutMilliseconds = 0U;
    serviceConfig.limits.coordinatorIdleTimeoutMilliseconds = 30000U;
    serviceConfig.limits.relayPairIdleTimeoutMilliseconds = 0U;
    serviceConfig.limits.relayHeartbeatMilliseconds = 100U;
    serviceConfig.limits.pendingRelayTimeoutMilliseconds = 10000U;
    serviceConfig.limits.maximumCoordinatorConnections = 8U;
    serviceConfig.limits.maximumCoordinatorConnectionsPerPeer = 1U;
    serviceConfig.limits.maximumPendingRelayLegs = 16U;
    serviceConfig.limits.maximumActiveRelayPairs = 8U;
    serviceConfig.limits.maximumReplayNonces = 128U;
    serviceConfig.limits.maximumReplayNoncesPerPeer = 16U;
    serviceConfig.limits.replayNonceRetentionMilliseconds = 30000U;
    serviceConfig.nowMilliseconds = nowMilliseconds;
    serviceConfig.verifyCredential = [knownPeers](
        const RuntimeInternetServiceCredentialRequest& request,
        RuntimeInternetServiceVerifiedIdentity& identity,
        std::string& errorMessage
    ) {
        for (const std::string& peerId : knownPeers) {
            const std::vector<std::uint8_t> expected = credentialFor(peerId);
            if (request.credential.size() != expected.size()) continue;
            std::uint8_t difference = 0U;
            for (std::size_t index = 0U; index < expected.size(); ++index) {
                difference |= static_cast<std::uint8_t>(
                    request.credential[index] ^ expected[index]
                );
            }
            if (difference != 0U) continue;
            identity.serviceId = std::string(serviceId);
            identity.peerId = peerId;
            errorMessage.clear();
            return true;
        }
        errorMessage = "credential rejected";
        return false;
    };

    RuntimeInternetServiceHost serviceHost;
    context.stage = "service open";
    context.expect(serviceHost.open(std::move(serviceConfig), error), error);
    context.expect(serviceHost.port() != 0U, "service did not bind port 0");
    if (!serviceHost.isOpen() || context.failures != 0) return false;

    RuntimeMultiplayerLockstepConfig lockstepConfig;
    lockstepConfig.inputDelayTicks = 0;
    auto makeFlow = [&](const std::string& peerId) {
        error.clear();
        std::unique_ptr<RuntimeInternetServiceAdapter> adapter =
            serviceAdapter(
                peerId,
                serviceHost.port(),
                certificate.chainEngine(),
                error
            );
        context.expect(adapter != nullptr, peerId + " transport: " + error);
        return adapter;
    };
    if (GetEnvironmentVariableW(L"CONCORD_RANKED_TLS_ONLY", nullptr, 0) != 0)
        return runInternetRankedTlsSmoke(context, serviceHost, makeFlow, mapPath, mapContentFingerprint);
    RuntimeInternetLobbyFlow host(makeFlow("host-peer"), lockstepConfig);
    RuntimeInternetLobbyFlow client(makeFlow("client-peer"), lockstepConfig);
    RuntimeInternetLobbyFlow spectator(
        makeFlow("spectator-peer"),
        lockstepConfig
    );
    if (context.failures != 0) return false;

    const RuntimeInternetRoomRecord room = roomRecord();
    context.stage = "host and browse";
    error.clear();
    const bool hostStarted = host.startHost(
        hostOptions(room, mapContentFingerprint),
        nowMilliseconds(),
        error
    );
    context.expect(
        hostStarted,
        "host start: " + error
    );
    if (!hostStarted) return false;
    std::array<RuntimeInternetLobbyFlow*, 1U> hostOnly{&host};
    if (!pumpFlows(
            serviceHost,
            hostOnly,
            [&host]() {
                return host.mode() == RuntimeInternetLobbyFlowMode::Lobby;
            },
            context,
            "host registration"
        )) {
        return false;
    }

    RuntimeInternetLobbyQueryOptions clientQuery;
    clientQuery.connection = queryConfig("client-peer", 2001U);
    RuntimeInternetLobbyQueryOptions spectatorQuery;
    spectatorQuery.connection = queryConfig("spectator-peer", 3001U);
    error.clear();
    context.expect(
        client.beginRoomQuery(clientQuery, nowMilliseconds(), error),
        "client query: " + error
    );
    error.clear();
    context.expect(
        spectator.beginRoomQuery(spectatorQuery, nowMilliseconds(), error),
        "spectator query: " + error
    );
    std::array<RuntimeInternetLobbyFlow*, 3U> lobbyFlows{
        &host, &client, &spectator
    };
    if (!pumpFlows(
            serviceHost,
            lobbyFlows,
            [&client, &spectator]() {
                return client.mode()
                        == RuntimeInternetLobbyFlowMode::RoomListReady
                    && spectator.mode()
                        == RuntimeInternetLobbyFlowMode::RoomListReady;
            },
            context,
            "production TLS room queries"
        )) {
        return false;
    }
    context.expect(
        client.rooms().size() == 1U && client.rooms().front() == room
            && spectator.rooms().size() == 1U
            && spectator.rooms().front() == room,
        "authenticated room query did not return the hosted room"
    );

    context.stage = "client and spectator join";
    error.clear();
    context.expect(
        client.join(
            joinOptions(
                "client-peer",
                "Client",
                RuntimeInternetRole::Client,
                room,
                2002U,
                42001U
            ),
            nowMilliseconds(),
            error
        ),
        "client join: " + error
    );
    error.clear();
    context.expect(
        spectator.join(
            joinOptions(
                "spectator-peer",
                "Spectator",
                RuntimeInternetRole::Spectator,
                room,
                3002U,
                43001U
            ),
            nowMilliseconds(),
            error
        ),
        "spectator join: " + error
    );
    if (!pumpFlows(
            serviceHost,
            lobbyFlows,
            [&host, &client, &spectator, &serviceHost]() {
                return host.gameplaySessionCount() == 2U
                    && client.gameplaySessionCount() == 1U
                    && spectator.gameplaySessionCount() == 1U
                    && host.lobbyFlow()->lobby().initialized()
                    && host.lobbyFlow()->lobby().setup().members.size() == 3U
                    && client.lobbyFlow()->lobby().setup().members.size() == 3U
                    && spectator.lobbyFlow()->lobby().setup().members.size()
                        == 3U
                    && serviceHost.stats().activeRelayPairs == 2U
                    && serviceHost.stats().coordinatorRelayRoutes == 2U;
            },
            context,
            "production TLS relay admission"
        )) {
        return false;
    }

    context.stage = "ready and seal gate";
    error.clear();
    context.expect(
        host.lobbyFlow()->verifyLocalMap(mapPath, error),
        "host map verify: " + error
    );
    error.clear();
    const auto downloadRoot = mapPath.parent_path() / (mapPath.stem().string() + "_downloads");
    std::filesystem::create_directories(downloadRoot / "client");
    std::filesystem::create_directories(downloadRoot / "spectator");
    std::filesystem::path clientMap, spectatorMap;
    context.expect(!client.lobbyFlow()->downloadMissingMap(downloadRoot / "client", clientMap, error)
        && !spectator.lobbyFlow()->downloadMissingMap(downloadRoot / "spectator", spectatorMap, error)
        && !client.lobbyFlow()->setReady(true).succeeded, "TLS missing maps begin asynchronously and block Ready");
    bool sawPartialMap = false;
    if (!pumpFlows(serviceHost, lobbyFlows, [&]() {
        const auto& lan = client.lobbyFlow()->bridge()->lan();
        sawPartialMap |= lan.lobbyMapOffer() && lan.lobbyMapBytes().empty();
        const bool clientDone = client.lobbyFlow()->downloadMissingMap(downloadRoot / "client", clientMap, error);
        const bool spectatorDone = spectator.lobbyFlow()->downloadMissingMap(downloadRoot / "spectator", spectatorMap, error);
        return clientDone && spectatorDone && host.lobbyFlow()->bridge()->lan().lobbyMapTransfersComplete();
    }, context, "missing maps through actual TLS relay")) return false;
    context.expect(sawPartialMap && clientMap != mapPath && spectatorMap != mapPath && clientMap != spectatorMap,
        "TLS combatant and spectator receive private verified multi-chunk map files");
    std::cout << "TLS missing maps: combatant/spectator downloads, full SHA-256, private caches and ready gates passed.\n";
    const RuntimeMultiplayerLanOperationResult hostReady =
        host.lobbyFlow()->setReady(true);
    const RuntimeMultiplayerLanOperationResult clientReady =
        client.lobbyFlow()->setReady(true);
    const RuntimeMultiplayerLanOperationResult spectatorReady =
        spectator.lobbyFlow()->setReady(true);
    context.expect(
        hostReady.succeeded && clientReady.succeeded
            && spectatorReady.succeeded,
        "ready intent failed"
    );
    if (!pumpFlows(
            serviceHost,
            lobbyFlows,
            [&host]() {
                std::string reason;
                return host.lobbyFlow()->canStart(reason);
            },
            context,
            "ready consensus"
        )) {
        return false;
    }
    const RuntimeMultiplayerLanOperationResult started =
        host.lobbyFlow()->startMatch(1);
    context.expect(started.succeeded, "start match: " + started.reason);
    if (!started.succeeded) return false;

    error.clear();
    context.expect(pollHost(serviceHost, error), error);
    RuntimeInternetLobbyFlowPollResult sealing = host.poll(
        nowMilliseconds(),
        0
    );
    context.expect(
        sealing.succeeded && !sealing.matchStarted
            && !hasEvent(
                sealing,
                RuntimeInternetLobbyFlowEventKind::MatchStarted
            )
            && host.mode() == RuntimeInternetLobbyFlowMode::Lobby,
        "host exposed Running before RoomSealed: " + sealing.error
    );
    error.clear();
    context.expect(
        host.takeRunningBridge(error) == nullptr,
        "running bridge was available before RoomSealed"
    );

    // Flush SealRoom from the production client, then let only the service
    // consume it. RoomSealed deliberately remains unread by the host flow.
    sealing = host.poll(nowMilliseconds(), 0);
    context.expect(
        sealing.succeeded && host.mode() == RuntimeInternetLobbyFlowMode::Lobby,
        "host left seal gate while only flushing its TLS channel"
    );
    for (int iteration = 0; iteration < 32; ++iteration) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "seal service pump: " + error);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    RuntimeInternetLobbyFlow sealBrowser(
        makeFlow("seal-browser-peer"),
        lockstepConfig
    );
    if (context.failures != 0) return false;
    RuntimeInternetLobbyQueryOptions sealQuery;
    sealQuery.connection = queryConfig("seal-browser-peer", 4001U);
    error.clear();
    context.expect(
        sealBrowser.beginRoomQuery(sealQuery, nowMilliseconds(), error),
        "sealed room query: " + error
    );
    std::array<RuntimeInternetLobbyFlow*, 1U> browserOnly{&sealBrowser};
    if (!pumpFlows(
            serviceHost,
            browserOnly,
            [&sealBrowser]() {
                return sealBrowser.mode()
                    == RuntimeInternetLobbyFlowMode::RoomListReady;
            },
            context,
            "query while host ack is pending"
        )) {
        return false;
    }
    context.expect(
        sealBrowser.rooms().empty()
            && host.mode() == RuntimeInternetLobbyFlowMode::Lobby,
        "room remained listed or host bypassed the RoomSealed gate"
    );
    sealBrowser.close();

    bool publishedStart = false;
    const std::uint64_t sealDeadline = nowMilliseconds() + 5000U;
    while (nowMilliseconds() < sealDeadline && !publishedStart) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "RoomSealed service pump: " + error);
            return false;
        }
        const RuntimeInternetLobbyFlowPollResult result = host.poll(
            nowMilliseconds(),
            0
        );
        if (!result.succeeded) {
            context.expect(false, "RoomSealed host poll: " + result.error);
            return false;
        }
        publishedStart = result.matchStarted
            && hasEvent(
                result,
                RuntimeInternetLobbyFlowEventKind::MatchStarted
            );
        if (!publishedStart) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        publishedStart
            && host.mode() == RuntimeInternetLobbyFlowMode::Running,
        "RoomSealed did not publish MatchStarted exactly at the gate"
    );
    if (!pumpFlows(
            serviceHost,
            lobbyFlows,
            [&host, &client, &spectator]() {
                return host.mode() == RuntimeInternetLobbyFlowMode::Running
                    && client.mode()
                        == RuntimeInternetLobbyFlowMode::Running
                    && spectator.mode()
                        == RuntimeInternetLobbyFlowMode::Running;
            },
            context,
            "started broadcast over authenticated relays"
        )) {
        return false;
    }
    error.clear();
    context.expect(pollHost(serviceHost, error), error);
    const RuntimeInternetLobbyFlowPollResult noDuplicate = host.poll(
        nowMilliseconds(),
        0
    );
    context.expect(
        noDuplicate.succeeded && !noDuplicate.matchStarted
            && !hasEvent(
                noDuplicate,
                RuntimeInternetLobbyFlowEventKind::MatchStarted
            ),
        "MatchStarted repeated after RoomSealed"
    );

    context.stage = "runtime handoff";
    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> hostBridge =
        host.takeRunningBridge(error);
    context.expect(hostBridge != nullptr, "host handoff: " + error);
    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> clientBridge =
        client.takeRunningBridge(error);
    context.expect(clientBridge != nullptr, "client handoff: " + error);
    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> spectatorBridge =
        spectator.takeRunningBridge(error);
    context.expect(spectatorBridge != nullptr, "spectator handoff: " + error);
    if (!hostBridge || !clientBridge || !spectatorBridge) return false;
    context.expect(
        hostBridge->hasExternalOrderedTransport()
            && clientBridge->hasExternalOrderedTransport()
            && spectatorBridge->hasExternalOrderedTransport(),
        "a runtime bridge did not retain its production TLS carrier"
    );
    host.close();
    client.close();
    spectator.close();

    error.clear();
    context.expect(
        hostBridge->beginPreAttachMaintenance(error, 10000U),
        "host pre-attach maintenance: " + error
    );
    error.clear();
    context.expect(
        clientBridge->beginPreAttachMaintenance(error, 10000U),
        "client pre-attach maintenance: " + error
    );
    error.clear();
    context.expect(
        spectatorBridge->beginPreAttachMaintenance(error, 10000U),
        "spectator pre-attach maintenance: " + error
    );
    const std::uint64_t maintenanceDeadline = nowMilliseconds() + 6000U;
    const std::uint64_t maintenanceLeaseProbe = nowMilliseconds() + 4500U;
    while (nowMilliseconds() < maintenanceDeadline
        && (nowMilliseconds() < maintenanceLeaseProbe
            || hostBridge->preAttachMaintenancePumpCount() < 5U
            || clientBridge->preAttachMaintenancePumpCount() < 5U
            || spectatorBridge->preAttachMaintenancePumpCount() < 5U)) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "pre-attach service pump: " + error);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const RuntimeInternetServiceHostStats maintenanceStats =
        serviceHost.stats();
    context.expect(
        hostBridge->preAttachMaintenanceRunning()
            && clientBridge->preAttachMaintenanceRunning()
            && spectatorBridge->preAttachMaintenanceRunning()
            && hostBridge->preAttachMaintenancePumpCount() >= 5U
            && clientBridge->preAttachMaintenancePumpCount() >= 5U
            && spectatorBridge->preAttachMaintenancePumpCount() >= 5U
            && maintenanceStats.rooms == 1U
            && maintenanceStats.activeRelayPairs == 2U
            && maintenanceStats.coordinatorRelayRoutes == 2U,
        "pre-attach workers did not preserve the sealed room lease and two TLS relays"
    );

    RuntimeProbeWorld hostWorld;
    RuntimeProbeWorld clientWorld;
    RuntimeProbeWorld spectatorWorld;
    error.clear();
    context.expect(
        hostBridge->attachRuntime(runtimeCallbacksFor(hostWorld), error),
        "host runtime attach: " + error
    );
    error.clear();
    context.expect(
        clientBridge->attachRuntime(runtimeCallbacksFor(clientWorld), error),
        "client runtime attach: " + error
    );
    error.clear();
    context.expect(
        spectatorBridge->attachRuntime(
            runtimeCallbacksFor(spectatorWorld),
            error
        ),
        "spectator runtime attach: " + error
    );
    const std::uint64_t hostMaintenancePumps =
        hostBridge->preAttachMaintenancePumpCount();
    const std::uint64_t clientMaintenancePumps =
        clientBridge->preAttachMaintenancePumpCount();
    const std::uint64_t spectatorMaintenancePumps =
        spectatorBridge->preAttachMaintenancePumpCount();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    context.expect(
        !hostBridge->preAttachMaintenanceRunning()
            && !clientBridge->preAttachMaintenanceRunning()
            && !spectatorBridge->preAttachMaintenanceRunning()
            && hostBridge->preAttachMaintenancePumpCount()
                == hostMaintenancePumps
            && clientBridge->preAttachMaintenancePumpCount()
                == clientMaintenancePumps
            && spectatorBridge->preAttachMaintenancePumpCount()
                == spectatorMaintenancePumps,
        "runtime attach did not join all pre-attach workers before pumping"
    );
    if (!hostBridge->runtimeAttached() || !clientBridge->runtimeAttached()
        || !spectatorBridge->runtimeAttached()) {
        return false;
    }

    error.clear();
    context.expect(
        hostBridge->captureGameplayCommand(stopCommand(), error),
        "host command capture: " + error
    );
    error.clear();
    context.expect(
        clientBridge->captureGameplayCommand(guardCommand(), error),
        "client command capture: " + error
    );
    error.clear();
    context.expect(
        !spectatorBridge->captureGameplayCommand(stopCommand(), error)
            && error.find("spectator") != std::string::npos,
        "spectator authored a gameplay command"
    );

    const std::uint64_t tickDeadline = nowMilliseconds() + 12000U;
    while (nowMilliseconds() < tickDeadline
        && (hostWorld.tick < 3 || clientWorld.tick < 3
            || spectatorWorld.tick < 3
            || !hostBridge->initialStateHashVerified()
            || !clientBridge->initialStateHashVerified()
            || !spectatorBridge->initialStateHashVerified())) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "runtime service pump: " + error);
            return false;
        }
        const RuntimeMultiplayerRuntimePumpResult hostPump =
            hostBridge->pump(true);
        (void)pollHost(serviceHost, error);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            clientBridge->pump(false);
        (void)pollHost(serviceHost, error);
        const RuntimeMultiplayerRuntimePumpResult spectatorPump =
            spectatorBridge->pump(false);
        (void)pollHost(serviceHost, error);
        if (!hostPump.succeeded || !clientPump.succeeded
            || !spectatorPump.succeeded) {
            context.expect(
                false,
                "runtime pump: "
                    + (!hostPump.succeeded
                        ? hostPump.error
                        : (!clientPump.succeeded
                            ? clientPump.error
                            : spectatorPump.error))
            );
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        hostWorld.tick >= 3 && clientWorld.tick >= 3
            && spectatorWorld.tick >= 3,
        "production TLS bridges did not commit three ticks"
    );
    context.expect(
        hostBridge->initialStateHashVerified()
            && clientBridge->initialStateHashVerified()
            && spectatorBridge->initialStateHashVerified()
            && worldHash(hostWorld) == worldHash(clientWorld)
            && worldHash(hostWorld) == worldHash(spectatorWorld)
            && hostWorld.applied == clientWorld.applied
            && hostWorld.applied == spectatorWorld.applied,
        "runtime commands or authoritative hashes diverged"
    );

    context.stage = "spectator departure";
    const int hostTickBeforeSpectatorDeparture = hostWorld.tick;
    const int clientTickBeforeSpectatorDeparture = clientWorld.tick;
    const int continuationTicks = lockstepConfig.inputDelayTicks + 3;
    spectatorBridge.reset();
    const std::uint64_t spectatorDeadline = nowMilliseconds() + 10000U;
    bool spectatorDeparted = false;
    std::string departureError;
    while (nowMilliseconds() < spectatorDeadline && !spectatorDeparted) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "spectator departure service: " + error);
            return false;
        }
        const RuntimeMultiplayerRuntimePumpResult hostPump =
            hostBridge->pump(true);
        (void)pollHost(serviceHost, error);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            clientBridge->pump(false);
        (void)pollHost(serviceHost, error);
        if (!hostPump.succeeded || !clientPump.succeeded
            || hostBridge->fatal() || clientBridge->fatal()) {
            departureError = !hostPump.succeeded
                ? hostPump.error
                : !clientPump.succeeded
                    ? clientPump.error
                    : hostBridge->fatal()
                        ? hostBridge->fatalReason()
                        : clientBridge->fatalReason();
            break;
        }
        const auto spectatorMissing = [](
            const RuntimeMultiplayerRuntimeBridge& bridge
        ) {
            return std::none_of(
                bridge.lan().session().members().begin(),
                bridge.lan().session().members().end(),
                [](const RuntimeMultiplayerRoomMember& member) {
                    return member.peerId == "spectator-peer";
                }
            );
        };
        spectatorDeparted = hostBridge->lan().session().members().size()
                == 2U
            && clientBridge->lan().session().members().size() == 2U
            && spectatorMissing(*hostBridge)
            && spectatorMissing(*clientBridge)
            && hostWorld.tick >= hostTickBeforeSpectatorDeparture
                + continuationTicks
            && clientWorld.tick >= clientTickBeforeSpectatorDeparture
                + continuationTicks;
        if (!spectatorDeparted) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        spectatorDeparted && !hostBridge->fatal() && !clientBridge->fatal()
            && serviceHost.stats().activeRelayPairs == 1U
            && serviceHost.stats().coordinatorRelayRoutes == 1U,
        "spectator departure did not preserve the combatant match: "
            + departureError
    );

    const auto reconnectScenario = [&](const std::string& label, const std::string& peer, bool coordinatorLoss, bool silentLoss = false) {
        context.stage = label;
        std::cout << "RUN " << label << '\n';
        auto& fault = *reconnectFaults.at(peer);
        const auto oldRoutes = fault.openedRoutes.size();
        // Publish an actual command to the host's authoritative history, then
        // drop the carrier before the client consumes that canonical frame.
        for (int drain = 0; drain < 12; ++drain) {
            (void)pollHost(serviceHost, error);
            if (!hostBridge->pump(false).succeeded || !clientBridge->pump(false).succeeded) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto beforeCommand = hostWorld.applied.size();
        if (!hostBridge->captureGameplayCommand(stopCommand(), error)) return false;
        const auto commandDeadline = nowMilliseconds() + 3000U;
        while (hostWorld.applied.size() == beforeCommand && nowMilliseconds() < commandDeadline) {
            if (!clientBridge->pump(false).succeeded) return false;
            (void)pollHost(serviceHost, error);
            if (!hostBridge->pump(true).succeeded) return false;
            (void)pollHost(serviceHost, error);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        context.expect(hostWorld.applied.size() == beforeCommand + 1U
            && clientWorld.applied.size() == beforeCommand && hostWorld.tick == clientWorld.tick + 1,
            "fixture did not isolate a canonical command still in flight");
        if (context.failures != 0) return false;
        const auto oldCommands = hostWorld.applied;
        const int oldTick = hostWorld.tick;
        const auto oldSetup = hostBridge->lan().session().matchSetup();
        fault.silentRelayLoss = silentLoss;
        ++fault.streamGeneration;
        if (coordinatorLoss) {
            fault.dropCoordinator = true;
            fault.offlineUntil = nowMilliseconds() + 700U;
        }
        bool paused = false;
        bool recovered = false;
        const auto deadline = nowMilliseconds() + 15000U;
        while (nowMilliseconds() < deadline && !recovered) {
            std::string serviceError;
            if (!pollHost(serviceHost, serviceError)) { context.expect(false, serviceError);return false; }
            const auto hostPump = hostBridge->pump(true);
            const auto clientPump = clientBridge->pump(false);
            if (!hostPump.succeeded || !clientPump.succeeded) {
                context.expect(false, "reconnect pump: host=" + hostBridge->fatalReason() + " client=" + clientBridge->fatalReason());
                return false;
            }
            paused = paused || hostBridge->lan().reconnectPending() || clientBridge->lan().reconnectPending();
            recovered = paused && !hostBridge->lan().reconnectPending() && !clientBridge->lan().reconnectPending()
                && hostWorld.tick >= oldTick + 3 && clientWorld.tick == hostWorld.tick
                && worldHash(hostWorld) == worldHash(clientWorld)
                && fault.openedRoutes.size() > oldRoutes;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        context.expect(recovered, "bounded reconnect did not resume equal worlds");
        context.expect(hostWorld.applied == oldCommands && clientWorld.applied == oldCommands,
            "reconnect lost or reapplied commands");
        context.expect(hostBridge->lan().session().matchSetup() == oldSetup
            && clientBridge->lan().session().matchSetup() == oldSetup
            && hostBridge->lan().session().members().size() == 2U
            && clientBridge->lan().session().members().size() == 2U,
            "reconnect changed canonical setup or player slots");
        if (fault.openedRoutes.size() > oldRoutes)
            context.expect(fault.openedRoutes.back() != fault.openedRoutes[oldRoutes - 1], "reconnect reused old TLS route");
        if (coordinatorLoss) context.expect(fault.reauthentications != 0, "coordinator outage bypassed reauthentication");
        return recovered && context.failures == 0;
    };
    if (!reconnectScenario("client relay reconnect", "client-peer", false)
        || !reconnectScenario("silent relay outage", "client-peer", false, true)
        || !reconnectScenario("client coordinator reconnect", "client-peer", true)
        || !reconnectScenario("host coordinator reconnect", "host-peer", true)) return false;

    context.stage = "combatant departure";
    clientBridge.reset();
    const auto departureStarted = nowMilliseconds();
    bool combatantFatal = false;
    const std::uint64_t fatalDeadline = nowMilliseconds() + 35000U;
    while (nowMilliseconds() < fatalDeadline && !combatantFatal) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "combatant departure service: " + error);
            return false;
        }
        const RuntimeMultiplayerRuntimePumpResult hostPump =
            hostBridge->pump(false);
        combatantFatal = !hostPump.succeeded || hostBridge->fatal();
        if (!combatantFatal) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        combatantFatal && hostBridge->fatal(),
        "combatant reconnect timeout did not terminate the remaining host"
    );
    std::cout << "combatant timeout after " << (nowMilliseconds() - departureStarted)
        << " ms: " << hostBridge->fatalReason() << '\n';
    context.expect(nowMilliseconds() - departureStarted >= 29000U, "short outage terminated before its grace period");

    const std::uint64_t routeCleanupDeadline = nowMilliseconds() + 5000U;
    while (nowMilliseconds() < routeCleanupDeadline
        && (serviceHost.stats().activeRelayPairs != 0U
            || serviceHost.stats().pendingRelayLegs != 0U
            || serviceHost.stats().coordinatorRelayRoutes != 0U)) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "route cleanup service: " + error);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        serviceHost.stats().activeRelayPairs == 0U
            && serviceHost.stats().pendingRelayLegs == 0U
            && serviceHost.stats().coordinatorRelayRoutes == 0U,
        "combatant departure left a relay pair or coordinator route"
    );

    context.stage = "clean shutdown";
    hostBridge.reset();
    const std::uint64_t cleanupDeadline = nowMilliseconds() + 35000U;
    while (nowMilliseconds() < cleanupDeadline) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "final cleanup service: " + error);
            return false;
        }
        const RuntimeInternetServiceHostStats stats = serviceHost.stats();
        if (stats.coordinatorConnections == 0U
            && stats.pendingRelayLegs == 0U
            && stats.activeRelayPairs == 0U && stats.rooms == 0U
            && stats.coordinatorConnectionsInRooms == 0U
            && stats.coordinatorRelayRoutes == 0U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const RuntimeInternetServiceHostStats finalStats = serviceHost.stats();
    std::cout << "cleanup: coordinators=" << finalStats.coordinatorConnections << " rooms=" << finalStats.rooms
        << " connections=" << finalStats.coordinatorConnectionsInRooms << " relays=" << finalStats.coordinatorRelayRoutes << '\n';
    context.expect(
        finalStats.coordinatorConnections == 0U
            && finalStats.pendingRelayLegs == 0U
            && finalStats.activeRelayPairs == 0U
            && finalStats.rooms == 0U
            && finalStats.coordinatorConnectionsInRooms == 0U
            && finalStats.coordinatorRelayRoutes == 0U,
        "full-stack shutdown left coordinator or relay authority state"
    );

    context.stage = "stalled loader deadline";
    RuntimeInternetLobbyFlow stalledHost(
        makeFlow("host-peer"),
        lockstepConfig
    );
    RuntimeInternetLobbyFlow stalledClient(
        makeFlow("client-peer"),
        lockstepConfig
    );
    if (context.failures != 0) return false;
    RuntimeInternetRoomRecord stalledRoom = roomRecord();
    stalledRoom.roomId = "stalled-loader-room";
    stalledRoom.roomName = "Stalled Loader Room";
    RuntimeInternetLobbyHostOptions stalledHostOptions = hostOptions(
        stalledRoom,
        mapContentFingerprint
    );
    stalledHostOptions.connection.transactionId = 5001U;
    stalledHostOptions.network.roomGeneration = 0x8899aaULL;
    error.clear();
    context.expect(
        stalledHost.startHost(
            std::move(stalledHostOptions),
            nowMilliseconds(),
            error
        ),
        "stalled host start: " + error
    );
    std::array<RuntimeInternetLobbyFlow*, 1U> stalledHostOnly{
        &stalledHost
    };
    if (!pumpFlows(
            serviceHost,
            stalledHostOnly,
            [&stalledHost]() {
                return stalledHost.mode()
                    == RuntimeInternetLobbyFlowMode::Lobby;
            },
            context,
            "stalled host registration"
        )) {
        return false;
    }
    error.clear();
    context.expect(
        stalledClient.join(
            joinOptions(
                "client-peer",
                "Client",
                RuntimeInternetRole::Client,
                stalledRoom,
                5002U,
                44001U
            ),
            nowMilliseconds(),
            error
        ),
        "stalled client join: " + error
    );
    std::array<RuntimeInternetLobbyFlow*, 2U> stalledFlows{
        &stalledHost,
        &stalledClient
    };
    if (!pumpFlows(
            serviceHost,
            stalledFlows,
            [&stalledHost, &stalledClient, &serviceHost]() {
                return stalledHost.lobbyFlow() != nullptr
                    && stalledClient.lobbyFlow() != nullptr
                    && stalledHost.lobbyFlow()->lobby().setup().members.size()
                        == 2U
                    && stalledClient.lobbyFlow()->lobby().setup().members.size()
                        == 2U
                    && serviceHost.stats().activeRelayPairs == 1U
                    && serviceHost.stats().coordinatorRelayRoutes == 1U;
            },
            context,
            "stalled client TLS relay admission"
        )) {
        return false;
    }
    error.clear();
    context.expect(
        stalledHost.lobbyFlow()->verifyLocalMap(mapPath, error),
        "stalled host map verify: " + error
    );
    error.clear();
    context.expect(
        stalledClient.lobbyFlow()->verifyLocalMap(mapPath, error),
        "stalled client map verify: " + error
    );
    const RuntimeMultiplayerLanOperationResult stalledHostReady =
        stalledHost.lobbyFlow()->setReady(true);
    const RuntimeMultiplayerLanOperationResult stalledClientReady =
        stalledClient.lobbyFlow()->setReady(true);
    context.expect(
        stalledHostReady.succeeded && stalledClientReady.succeeded,
        "stalled loader ready intent failed"
    );
    if (!pumpFlows(
            serviceHost,
            stalledFlows,
            [&stalledHost]() {
                std::string reason;
                return stalledHost.lobbyFlow()->canStart(reason);
            },
            context,
            "stalled loader ready consensus"
        )) {
        return false;
    }
    const RuntimeMultiplayerLanOperationResult stalledStarted =
        stalledHost.lobbyFlow()->startMatch(1);
    context.expect(
        stalledStarted.succeeded,
        "stalled loader start: " + stalledStarted.reason
    );
    if (!stalledStarted.succeeded
        || !pumpFlows(
            serviceHost,
            stalledFlows,
            [&stalledHost, &stalledClient]() {
                return stalledHost.mode()
                        == RuntimeInternetLobbyFlowMode::Running
                    && stalledClient.mode()
                        == RuntimeInternetLobbyFlowMode::Running;
            },
            context,
            "stalled loader room seal and start"
        )) {
        return false;
    }

    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> stalledHostBridge =
        stalledHost.takeRunningBridge(error);
    context.expect(
        stalledHostBridge != nullptr,
        "stalled host handoff: " + error
    );
    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge> stalledClientBridge =
        stalledClient.takeRunningBridge(error);
    context.expect(
        stalledClientBridge != nullptr,
        "stalled client handoff: " + error
    );
    if (!stalledHostBridge || !stalledClientBridge) return false;
    stalledHost.close();
    stalledClient.close();
    error.clear();
    context.expect(
        stalledHostBridge->beginPreAttachMaintenance(error, 500U),
        "stalled host maintenance: " + error
    );
    error.clear();
    context.expect(
        stalledClientBridge->beginPreAttachMaintenance(error, 5000U),
        "stalled client maintenance: " + error
    );
    const std::uint64_t stalledMaintenanceDeadline =
        nowMilliseconds() + 1000U;
    while (nowMilliseconds() < stalledMaintenanceDeadline
        && (stalledHostBridge->preAttachMaintenancePumpCount() < 3U
            || stalledClientBridge->preAttachMaintenancePumpCount() < 3U)) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "stalled maintenance service: " + error);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    context.expect(
        stalledHostBridge->preAttachMaintenancePumpCount() >= 3U
            && stalledClientBridge->preAttachMaintenancePumpCount() >= 3U
            && serviceHost.stats().rooms == 1U
            && serviceHost.stats().activeRelayPairs == 1U
            && serviceHost.stats().coordinatorRelayRoutes == 1U,
        "stalled loader did not remain live over the real TLS relay"
    );
    RuntimeProbeWorld stalledHostWorld;
    error.clear();
    context.expect(
        stalledHostBridge->attachRuntime(
            runtimeCallbacksFor(stalledHostWorld),
            error
        ),
        "stalled deadline host attach: " + error
    );

    bool stalledHostFatal = false;
    const std::uint64_t stalledAbortDeadline = nowMilliseconds() + 7000U;
    while (nowMilliseconds() < stalledAbortDeadline
        && (!stalledHostFatal
            || stalledClientBridge->preAttachMaintenanceRunning())) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "stalled abort service: " + error);
            return false;
        }
        if (!stalledHostFatal) {
            const RuntimeMultiplayerRuntimePumpResult pumped =
                stalledHostBridge->pump(false);
            stalledHostFatal = !pumped.succeeded
                && stalledHostBridge->fatal();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const RuntimeMultiplayerLanPhase stalledClientPhase =
        stalledClientBridge->lan().phase();
    context.expect(
        stalledHostFatal
            && stalledHostBridge->fatalReason()
                == "multiplayer initial loading timed out waiting for peer 'client-peer'"
            && stalledClientPhase == RuntimeMultiplayerLanPhase::Aborted
            && stalledClientBridge->lan().session().abortState().affectedPeerId
                == "client-peer"
            && stalledClientBridge->lan().session().abortState().reason
                == RuntimeMultiplayerSessionAbortReason::TransportFailure,
        "real TLS relay did not deliver the canonical stalled-loader SessionAbort"
            " hostFatal=" + std::to_string(stalledHostFatal)
            + " hostReason=" + stalledHostBridge->fatalReason()
            + " clientPhase=" + std::to_string(
                static_cast<int>(stalledClientPhase)
            )
            + " affected="
            + stalledClientBridge->lan().session().abortState().affectedPeerId
            + " abortReason=" + std::to_string(static_cast<int>(
                stalledClientBridge->lan().session().abortState().reason
            ))
    );
    stalledClientBridge.reset();
    stalledHostBridge.reset();
    const std::uint64_t stalledCleanupDeadline = nowMilliseconds() + 35000U;
    while (nowMilliseconds() < stalledCleanupDeadline) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "stalled cleanup service: " + error);
            return false;
        }
        const RuntimeInternetServiceHostStats stats = serviceHost.stats();
        if (stats.coordinatorConnections == 0U
            && stats.pendingRelayLegs == 0U
            && stats.activeRelayPairs == 0U && stats.rooms == 0U
            && stats.coordinatorConnectionsInRooms == 0U
            && stats.coordinatorRelayRoutes == 0U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const RuntimeInternetServiceHostStats stalledFinalStats =
        serviceHost.stats();
    context.expect(
        stalledFinalStats.coordinatorConnections == 0U
            && stalledFinalStats.pendingRelayLegs == 0U
            && stalledFinalStats.activeRelayPairs == 0U
            && stalledFinalStats.rooms == 0U
            && stalledFinalStats.coordinatorConnectionsInRooms == 0U
            && stalledFinalStats.coordinatorRelayRoutes == 0U,
        "stalled-loader abort left room, lease, or relay authority state"
    );

    context.stage = "spectator pre-attach deadline";
    RuntimeInternetLobbyFlow spectatorDeadlineHost(
        makeFlow("host-peer"),
        lockstepConfig
    );
    RuntimeInternetLobbyFlow spectatorDeadlineClient(
        makeFlow("client-peer"),
        lockstepConfig
    );
    RuntimeInternetLobbyFlow spectatorDeadlineSpectator(
        makeFlow("spectator-peer"),
        lockstepConfig
    );
    if (context.failures != 0) return false;
    RuntimeInternetRoomRecord spectatorDeadlineRoom = roomRecord();
    spectatorDeadlineRoom.roomId = "spectator-deadline-room";
    spectatorDeadlineRoom.roomName = "Spectator Deadline Room";
    RuntimeInternetLobbyHostOptions spectatorDeadlineHostOptions =
        hostOptions(spectatorDeadlineRoom, mapContentFingerprint);
    spectatorDeadlineHostOptions.connection.transactionId = 6001U;
    spectatorDeadlineHostOptions.network.roomGeneration = 0x99aabbULL;
    error.clear();
    context.expect(
        spectatorDeadlineHost.startHost(
            std::move(spectatorDeadlineHostOptions),
            nowMilliseconds(),
            error
        ),
        "spectator deadline host start: " + error
    );
    std::array<RuntimeInternetLobbyFlow*, 1U> spectatorDeadlineHostOnly{
        &spectatorDeadlineHost
    };
    if (!pumpFlows(
            serviceHost,
            spectatorDeadlineHostOnly,
            [&spectatorDeadlineHost]() {
                return spectatorDeadlineHost.mode()
                    == RuntimeInternetLobbyFlowMode::Lobby;
            },
            context,
            "spectator deadline host registration"
        )) {
        return false;
    }
    error.clear();
    context.expect(
        spectatorDeadlineClient.join(
            joinOptions(
                "client-peer",
                "Client",
                RuntimeInternetRole::Client,
                spectatorDeadlineRoom,
                6002U,
                45001U
            ),
            nowMilliseconds(),
            error
        ),
        "spectator deadline client join: " + error
    );
    error.clear();
    context.expect(
        spectatorDeadlineSpectator.join(
            joinOptions(
                "spectator-peer",
                "Spectator",
                RuntimeInternetRole::Spectator,
                spectatorDeadlineRoom,
                6003U,
                46001U
            ),
            nowMilliseconds(),
            error
        ),
        "spectator deadline spectator join: " + error
    );
    std::array<RuntimeInternetLobbyFlow*, 3U> spectatorDeadlineFlows{
        &spectatorDeadlineHost,
        &spectatorDeadlineClient,
        &spectatorDeadlineSpectator
    };
    if (!pumpFlows(
            serviceHost,
            spectatorDeadlineFlows,
            [&spectatorDeadlineHost,
             &spectatorDeadlineClient,
             &spectatorDeadlineSpectator,
             &serviceHost]() {
                return spectatorDeadlineHost.lobbyFlow() != nullptr
                    && spectatorDeadlineClient.lobbyFlow() != nullptr
                    && spectatorDeadlineSpectator.lobbyFlow() != nullptr
                    && spectatorDeadlineHost.lobbyFlow()->lobby()
                        .setup().members.size() == 3U
                    && spectatorDeadlineClient.lobbyFlow()->lobby()
                        .setup().members.size() == 3U
                    && spectatorDeadlineSpectator.lobbyFlow()->lobby()
                        .setup().members.size() == 3U
                    && serviceHost.stats().activeRelayPairs == 2U
                    && serviceHost.stats().coordinatorRelayRoutes == 2U;
            },
            context,
            "spectator deadline TLS relay admission"
        )) {
        return false;
    }
    error.clear();
    context.expect(
        spectatorDeadlineHost.lobbyFlow()->verifyLocalMap(mapPath, error),
        "spectator deadline host map: " + error
    );
    error.clear();
    context.expect(
        spectatorDeadlineClient.lobbyFlow()->verifyLocalMap(mapPath, error),
        "spectator deadline client map: " + error
    );
    error.clear();
    context.expect(
        spectatorDeadlineSpectator.lobbyFlow()->verifyLocalMap(
            mapPath,
            error
        ),
        "spectator deadline spectator map: " + error
    );
    const RuntimeMultiplayerLanOperationResult spectatorDeadlineHostReady =
        spectatorDeadlineHost.lobbyFlow()->setReady(true);
    const RuntimeMultiplayerLanOperationResult spectatorDeadlineClientReady =
        spectatorDeadlineClient.lobbyFlow()->setReady(true);
    const RuntimeMultiplayerLanOperationResult
        spectatorDeadlineSpectatorReady =
            spectatorDeadlineSpectator.lobbyFlow()->setReady(true);
    context.expect(
        spectatorDeadlineHostReady.succeeded
            && spectatorDeadlineClientReady.succeeded
            && spectatorDeadlineSpectatorReady.succeeded,
        "spectator deadline ready intent failed"
    );
    if (!pumpFlows(
            serviceHost,
            spectatorDeadlineFlows,
            [&spectatorDeadlineHost]() {
                std::string reason;
                return spectatorDeadlineHost.lobbyFlow()->canStart(reason);
            },
            context,
            "spectator deadline ready consensus"
        )) {
        return false;
    }
    const RuntimeMultiplayerLanOperationResult spectatorDeadlineStarted =
        spectatorDeadlineHost.lobbyFlow()->startMatch(1);
    context.expect(
        spectatorDeadlineStarted.succeeded,
        "spectator deadline start: " + spectatorDeadlineStarted.reason
    );
    if (!spectatorDeadlineStarted.succeeded
        || !pumpFlows(
            serviceHost,
            spectatorDeadlineFlows,
            [&spectatorDeadlineHost,
             &spectatorDeadlineClient,
             &spectatorDeadlineSpectator]() {
                return spectatorDeadlineHost.mode()
                        == RuntimeInternetLobbyFlowMode::Running
                    && spectatorDeadlineClient.mode()
                        == RuntimeInternetLobbyFlowMode::Running
                    && spectatorDeadlineSpectator.mode()
                        == RuntimeInternetLobbyFlowMode::Running;
            },
            context,
            "spectator deadline room seal and start"
        )) {
        return false;
    }

    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge>
        spectatorDeadlineHostBridge =
            spectatorDeadlineHost.takeRunningBridge(error);
    context.expect(
        spectatorDeadlineHostBridge != nullptr,
        "spectator deadline host handoff: " + error
    );
    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge>
        spectatorDeadlineClientBridge =
            spectatorDeadlineClient.takeRunningBridge(error);
    context.expect(
        spectatorDeadlineClientBridge != nullptr,
        "spectator deadline client handoff: " + error
    );
    error.clear();
    std::unique_ptr<RuntimeMultiplayerRuntimeBridge>
        spectatorDeadlineSpectatorBridge =
            spectatorDeadlineSpectator.takeRunningBridge(error);
    context.expect(
        spectatorDeadlineSpectatorBridge != nullptr,
        "spectator deadline spectator handoff: " + error
    );
    if (!spectatorDeadlineHostBridge || !spectatorDeadlineClientBridge
        || !spectatorDeadlineSpectatorBridge) {
        return false;
    }
    spectatorDeadlineHost.close();
    spectatorDeadlineClient.close();
    spectatorDeadlineSpectator.close();
    error.clear();
    context.expect(
        spectatorDeadlineHostBridge->beginPreAttachMaintenance(
            error,
            5000U
        ),
        "spectator deadline host maintenance: " + error
    );
    error.clear();
    context.expect(
        spectatorDeadlineClientBridge->beginPreAttachMaintenance(
            error,
            5000U
        ),
        "spectator deadline client maintenance: " + error
    );
    error.clear();
    context.expect(
        spectatorDeadlineSpectatorBridge->beginPreAttachMaintenance(
            error,
            5000U
        ),
        "spectator deadline spectator maintenance: " + error
    );
    const std::uint64_t spectatorMaintenanceDeadline =
        nowMilliseconds() + 500U;
    while (nowMilliseconds() < spectatorMaintenanceDeadline
        && (spectatorDeadlineHostBridge->preAttachMaintenancePumpCount()
                < 3U
            || spectatorDeadlineClientBridge->preAttachMaintenancePumpCount()
                < 3U
            || spectatorDeadlineSpectatorBridge
                ->preAttachMaintenancePumpCount() < 3U)) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(
                false,
                "spectator deadline maintenance service: " + error
            );
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const std::uint64_t spectatorDeadlineHostPumpsBeforeClose =
        spectatorDeadlineHostBridge->preAttachMaintenancePumpCount();
    const std::uint64_t spectatorDeadlineClientPumpsBeforeClose =
        spectatorDeadlineClientBridge->preAttachMaintenancePumpCount();
    spectatorDeadlineSpectatorBridge.reset();

    // Only the service host is driven here. Host/client are still owned by
    // their pre-attach workers, so the spectator route closure exercises the
    // external carrier callback stack that must use side-effect-free views.
    bool spectatorRemovedDuringPreAttach = false;
    const std::uint64_t spectatorRouteDeadline = nowMilliseconds() + 5000U;
    while (nowMilliseconds() < spectatorRouteDeadline
        && !spectatorRemovedDuringPreAttach) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(
                false,
                "spectator pre-attach route service: " + error
            );
            return false;
        }
        spectatorRemovedDuringPreAttach =
            spectatorDeadlineHostBridge->preAttachMaintenanceRunning()
            && spectatorDeadlineClientBridge->preAttachMaintenanceRunning()
            && spectatorDeadlineHostBridge->preAttachMaintenancePumpCount()
                >= spectatorDeadlineHostPumpsBeforeClose + 5U
            && spectatorDeadlineClientBridge->preAttachMaintenancePumpCount()
                >= spectatorDeadlineClientPumpsBeforeClose + 5U
            && serviceHost.stats().activeRelayPairs == 1U
            && serviceHost.stats().coordinatorRelayRoutes == 1U;
        if (!spectatorRemovedDuringPreAttach) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        spectatorRemovedDuringPreAttach,
        "spectator route closure stopped or deadlocked a pre-attach worker"
    );

    RuntimeProbeWorld spectatorDeadlineHostWorld;
    RuntimeProbeWorld spectatorDeadlineClientWorld;
    error.clear();
    const bool spectatorDeadlineHostAttached =
        spectatorDeadlineHostBridge->attachRuntime(
            runtimeCallbacksFor(spectatorDeadlineHostWorld),
            error
        );
    context.expect(
        spectatorDeadlineHostAttached,
        "spectator deadline host attach: " + error
    );
    error.clear();
    const bool spectatorDeadlineClientAttached =
        spectatorDeadlineClientBridge->attachRuntime(
            runtimeCallbacksFor(spectatorDeadlineClientWorld),
            error
        );
    context.expect(
        spectatorDeadlineClientAttached,
        "spectator deadline client attach: " + error
    );
    if (!spectatorDeadlineHostAttached || !spectatorDeadlineClientAttached) {
        return false;
    }
    const std::uint64_t spectatorDeadlineHostPumps =
        spectatorDeadlineHostBridge->preAttachMaintenancePumpCount();
    const std::uint64_t spectatorDeadlineClientPumps =
        spectatorDeadlineClientBridge->preAttachMaintenancePumpCount();
    const auto spectatorAbsent = [](
        const RuntimeMultiplayerRuntimeBridge& bridge
    ) {
        const RuntimeMultiplayerSessionState& session =
            bridge.lan().session();
        return std::none_of(
            session.members().begin(),
            session.members().end(),
            [](const RuntimeMultiplayerRoomMember& member) {
                return member.peerId == "spectator-peer";
            }
        );
    };
    context.expect(
        spectatorAbsent(*spectatorDeadlineHostBridge)
            && spectatorAbsent(*spectatorDeadlineClientBridge),
        "canonical spectator Leave was not present at runtime attach"
    );

    bool combatantsContinued = false;
    const std::uint64_t spectatorDropDeadline = nowMilliseconds() + 8000U;
    while (nowMilliseconds() < spectatorDropDeadline
        && !combatantsContinued) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "spectator drop service: " + error);
            return false;
        }
        const RuntimeMultiplayerRuntimePumpResult hostPump =
            spectatorDeadlineHostBridge->pump(true);
        (void)pollHost(serviceHost, error);
        const RuntimeMultiplayerRuntimePumpResult clientPump =
            spectatorDeadlineClientBridge->pump(false);
        (void)pollHost(serviceHost, error);
        if (!hostPump.succeeded || !clientPump.succeeded) break;
        combatantsContinued =
            spectatorAbsent(*spectatorDeadlineHostBridge)
            && spectatorAbsent(*spectatorDeadlineClientBridge)
            && spectatorDeadlineHostBridge->initialStateHashVerified()
            && spectatorDeadlineClientBridge->initialStateHashVerified()
            && spectatorDeadlineHostWorld.tick >= 3
            && spectatorDeadlineClientWorld.tick >= 3
            && serviceHost.stats().activeRelayPairs == 1U
            && serviceHost.stats().coordinatorRelayRoutes == 1U;
        if (!combatantsContinued) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    context.expect(
        combatantsContinued
            && !spectatorDeadlineHostBridge->fatal()
            && !spectatorDeadlineClientBridge->fatal()
            && spectatorDeadlineHostBridge->preAttachMaintenancePumpCount()
                == spectatorDeadlineHostPumps
            && spectatorDeadlineClientBridge->preAttachMaintenancePumpCount()
                == spectatorDeadlineClientPumps,
        "stalled TLS spectator did not leave while combatants continued"
    );
    spectatorDeadlineClientBridge.reset();
    spectatorDeadlineHostBridge.reset();
    const std::uint64_t spectatorCleanupDeadline = nowMilliseconds() + 35000U;
    while (nowMilliseconds() < spectatorCleanupDeadline) {
        error.clear();
        if (!pollHost(serviceHost, error)) {
            context.expect(false, "spectator deadline cleanup: " + error);
            return false;
        }
        const RuntimeInternetServiceHostStats stats = serviceHost.stats();
        if (stats.coordinatorConnections == 0U
            && stats.pendingRelayLegs == 0U
            && stats.activeRelayPairs == 0U && stats.rooms == 0U
            && stats.coordinatorConnectionsInRooms == 0U
            && stats.coordinatorRelayRoutes == 0U) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const RuntimeInternetServiceHostStats spectatorFinalStats =
        serviceHost.stats();
    context.expect(
        spectatorFinalStats.coordinatorConnections == 0U
            && spectatorFinalStats.pendingRelayLegs == 0U
            && spectatorFinalStats.activeRelayPairs == 0U
            && spectatorFinalStats.rooms == 0U
            && spectatorFinalStats.coordinatorConnectionsInRooms == 0U
            && spectatorFinalStats.coordinatorRelayRoutes == 0U,
        "spectator deadline left room, lease, or relay authority state"
    );
    if (!runInternetResumeRoomSmoke(context, serviceHost, makeFlow, mapPath, mapContentFingerprint)) return false;
    if (!runInternetRankedTlsSmoke(context, serviceHost, makeFlow, mapPath, mapContentFingerprint)) return false;
    serviceHost.close();
    context.expect(!serviceHost.isOpen(), "service host did not close");
    return context.failures == 0;
}

} // namespace

int main() {
    SmokeContext context;
    const std::filesystem::path mapPath =
        std::filesystem::temp_directory_path()
        / ("concord_service_lobby_e2e_"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            )
            + ".map");
    {
        std::ofstream output(mapPath, std::ios::binary | std::ios::trunc);
        output << "[Map]\nTheater=TEMPERATE\n[IsoMapPack5]\n1=fixture\n;"
            << std::string(runtimeMultiplayerSnapshotChunkBytes * 2 + 23, 'x');
    }
    const std::uint64_t fingerprint = mapFingerprint(mapPath, context);
    (void)runFullStack(context, mapPath, fingerprint);
    std::error_code ignored;
    std::filesystem::remove(mapPath, ignored);
    if (context.failures != 0) {
        std::cerr << context.failures
                  << " full-stack Internet service lobby checks failed\n";
        return 1;
    }
    std::cout << "Full-stack Schannel Internet service lobby E2E passed\n";
    return 0;
}

#else

int main() {
    std::cout << "Full-stack Internet service lobby E2E skipped: Windows only\n";
    return 0;
}

#endif
