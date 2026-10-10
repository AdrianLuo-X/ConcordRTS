#include "network/internet/RuntimeInternetServiceHost.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
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

constexpr std::string_view serviceId = "host-smoke-service";

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
        container_ = L"ConcordRTS-HostSmoke-";
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
    std::string text = "credential-for-";
    text.append(peerId);
    while (text.size() < runtimeInternetServiceMinimumCredentialBytes) {
        text.push_back('x');
    }
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

RuntimeInternetServiceNonce nonceFor(std::uint8_t seed) {
    RuntimeInternetServiceNonce nonce{};
    for (std::size_t index = 0U; index < nonce.size(); ++index) {
        nonce[index] = static_cast<std::uint8_t>(seed + index);
    }
    return nonce;
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

RuntimeInternetServiceTransportConfig transportConfig(
    std::string peerId,
    std::uint16_t port,
    HCERTCHAINENGINE chainEngine
) {
    RuntimeInternetServiceTransportConfig config;
    config.serviceId = std::string(serviceId);
    config.peerId = peerId;
    config.credential = credentialFor(peerId);
    config.coordinatorTls = tlsClientConfig(port, chainEngine);
    config.relayTls = tlsClientConfig(port, chainEngine);
    config.applicationAuthenticationTimeoutMilliseconds = 5000U;
    config.maximumPendingRelays = 8U;
    return config;
}

struct ClientHarness {
    std::string peerId;
    RuntimeInternetServiceTransport transport;
    RuntimeInternetMatchmakingClient matchmaking;
    RuntimeInternetCommunityClient community;
    std::deque<RuntimeInternetControlMessage> coordinatorMessages;
    std::deque<RuntimeInternetControlMessage> relayMessages;
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> relayStream;
    bool streamArrivedBeforeBound = false;
    std::string failure;
};

bool pumpClient(ClientHarness& client) {
    for (RuntimeInternetServiceEvent& event : client.transport.poll(0)) {
        if (event.kind == RuntimeInternetServiceEventKind::Failure) {
            client.failure = event.reason;
            return false;
        }
        if (event.kind == RuntimeInternetServiceEventKind::TcpStreamOpened) {
            if (client.relayMessages.empty()) {
                client.streamArrivedBeforeBound = true;
            }
            client.relayStream = std::move(event.tcpStream);
            continue;
        }
        if (event.kind != RuntimeInternetServiceEventKind::CoordinatorPacket
            && event.kind != RuntimeInternetServiceEventKind::RelayPacket) {
            continue;
        }
        if (event.packet.type == runtimeInternetMatchmakingPacketType) {
            std::string error;
            if (!client.matchmaking.receive(
                    event.packet,
                    std::string(serviceId),
                    nowMilliseconds(),
                    error
                )) {
                client.failure = std::move(error);
                return false;
            }
            continue;
        }
        if (event.packet.type == runtimeInternetCommunityPacketType) {
            std::string error;
            if (!client.community.receive(
                    event.packet,
                    event.authenticatedServiceId,
                    error
                )) {
                client.failure = std::move(error);
                return false;
            }
            continue;
        }
        RuntimeInternetControlMessage message;
        std::string error;
        if (!decodeRuntimeInternetControlMessage(
                event.packet,
                message,
                error
            )) {
            client.failure = std::move(error);
            return false;
        }
        if (event.kind == RuntimeInternetServiceEventKind::RelayPacket) {
            client.relayMessages.push_back(std::move(message));
        } else {
            client.coordinatorMessages.push_back(std::move(message));
        }
    }
    return true;
}

template <typename Predicate>
bool waitUntil(
    RuntimeInternetServiceHost& host,
    std::span<ClientHarness*> clients,
    Predicate predicate,
    std::string& errorMessage,
    std::uint32_t timeoutMilliseconds = 10000U
) {
    const std::uint64_t deadline = nowMilliseconds() + timeoutMilliseconds;
    while (nowMilliseconds() < deadline) {
        const RuntimeInternetServiceHostPollResult hostResult = host.poll();
        if (!hostResult.succeeded) {
            errorMessage = hostResult.error;
            return false;
        }
        for (ClientHarness* client : clients) {
            if (!pumpClient(*client)) {
                errorMessage = client->failure;
                return false;
            }
        }
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    errorMessage = "timed out waiting for self-hosted Internet service";
    return false;
}

bool sendControl(
    ClientHarness& client,
    const RuntimeInternetControlMessage& message,
    std::span<const std::uint8_t> proof,
    std::string& errorMessage
) {
    RuntimeNetworkPacket packet;
    if (!encodeRuntimeInternetControlMessage(
            message,
            packet,
            errorMessage
        )) {
        return false;
    }
    return proof.empty()
        ? client.transport.sendCoordinator(packet, errorMessage)
        : client.transport.sendCoordinatorWithAdmissionProof(
            packet,
            proof,
            errorMessage
        );
}

RuntimeInternetControlMessage peerMessage(
    RuntimeInternetControlType type,
    std::uint64_t transactionId,
    std::string roomId,
    std::string peerId,
    std::string targetPeerId = {}
) {
    RuntimeInternetControlMessage message;
    message.type = type;
    message.transactionId = transactionId;
    message.serviceId = std::string(serviceId);
    message.roomId = std::move(roomId);
    message.senderPeerId = std::move(peerId);
    message.targetPeerId = std::move(targetPeerId);
    return message;
}

std::optional<RuntimeInternetControlMessage> takeMessage(
    std::deque<RuntimeInternetControlMessage>& messages,
    RuntimeInternetControlType type
) {
    const auto found = std::find_if(
        messages.begin(),
        messages.end(),
        [type](const RuntimeInternetControlMessage& message) {
            return message.type == type;
        }
    );
    if (found == messages.end()) return std::nullopt;
    RuntimeInternetControlMessage result = std::move(*found);
    messages.erase(found);
    return result;
}

struct RawTlsConnection {
    RuntimeInternetTlsClient connector;
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream;
    RuntimeInternetServiceWireDecoder decoder;
};

bool connectRaw(
    RuntimeInternetServiceHost& host,
    RuntimeInternetTlsClientConfig tlsConfig,
    std::string claimedPeerId,
    RawTlsConnection& raw,
    std::string& errorMessage
) {
    RuntimeInternetAuthenticatedTcpStreamIdentity identity;
    identity.authenticatedServiceId = std::string(serviceId);
    identity.authenticatedPeerId = std::move(claimedPeerId);
    if (!raw.connector.begin(
            std::move(tlsConfig),
            std::move(identity),
            nowMilliseconds(),
            errorMessage
        )) {
        return false;
    }
    const std::uint64_t deadline = nowMilliseconds() + 10000U;
    while (nowMilliseconds() < deadline) {
        const RuntimeInternetServiceHostPollResult hostResult = host.poll();
        if (!hostResult.succeeded) {
            errorMessage = hostResult.error;
            return false;
        }
        const RuntimeInternetTlsPollResult tls =
            raw.connector.poll(nowMilliseconds());
        if (tls.state == RuntimeInternetTlsState::Open) {
            raw.stream = raw.connector.takeStream(errorMessage);
            return raw.stream != nullptr;
        }
        if (tls.state == RuntimeInternetTlsState::Failed
            || tls.state == RuntimeInternetTlsState::Closed) {
            errorMessage = tls.error;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    errorMessage = "raw TLS connection timed out";
    return false;
}

bool writeAll(
    RuntimeInternetServiceHost& host,
    RuntimeInternetAuthenticatedTcpStream& stream,
    std::span<const std::uint8_t> bytes,
    std::string& errorMessage
) {
    std::size_t offset = 0U;
    const std::uint64_t deadline = nowMilliseconds() + 10000U;
    while (offset < bytes.size() && nowMilliseconds() < deadline) {
        const RuntimeInternetServiceHostPollResult hostResult = host.poll();
        if (!hostResult.succeeded) {
            errorMessage = hostResult.error;
            return false;
        }
        const RuntimeInternetTcpStreamIoResult write =
            stream.write(bytes.subspan(offset));
        if (write.status == RuntimeInternetTcpStreamIoStatus::Progress) {
            offset += write.bytesTransferred;
        } else if (write.status == RuntimeInternetTcpStreamIoStatus::Closed
            || write.status == RuntimeInternetTcpStreamIoStatus::Failed) {
            errorMessage = write.error.empty()
                ? "raw TLS stream closed while writing"
                : write.error;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (offset != bytes.size()) errorMessage = "raw TLS write timed out";
    return offset == bytes.size();
}

bool sendRawAuthentication(
    RuntimeInternetServiceHost& host,
    RawTlsConnection& raw,
    RuntimeInternetServiceAuthenticationRequest request,
    RuntimeInternetServiceWireMessage& response,
    std::string& errorMessage
) {
    RuntimeInternetServiceWireMessage message;
    message.body = std::move(request);
    std::vector<std::uint8_t> frame;
    if (!encodeRuntimeInternetServiceWireFrame(
            message,
            frame,
            errorMessage
        )
        || !raw.stream
        || !writeAll(host, *raw.stream, frame, errorMessage)) {
        return false;
    }
    const std::uint64_t deadline = nowMilliseconds() + 10000U;
    while (nowMilliseconds() < deadline) {
        const RuntimeInternetServiceHostPollResult hostResult = host.poll();
        if (!hostResult.succeeded) {
            errorMessage = hostResult.error;
            return false;
        }
        bool available = false;
        if (!raw.decoder.pop(response, available, errorMessage)) return false;
        if (available) return true;
        std::array<std::uint8_t, 4096U> bytes{};
        const RuntimeInternetTcpStreamIoResult read = raw.stream->read(bytes);
        if (read.status == RuntimeInternetTcpStreamIoStatus::Progress) {
            if (!raw.decoder.push(
                    std::span<const std::uint8_t>(
                        bytes.data(),
                        read.bytesTransferred
                    ),
                    errorMessage
                )) {
                return false;
            }
        } else if (read.status == RuntimeInternetTcpStreamIoStatus::Closed
            || read.status == RuntimeInternetTcpStreamIoStatus::Failed) {
            errorMessage = read.error.empty()
                ? "raw TLS stream closed before authentication response"
                : read.error;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    errorMessage = "raw authentication response timed out";
    return false;
}

RuntimeInternetServiceAuthenticationRequest coordinatorAuthentication(
    std::string peerId,
    RuntimeInternetServiceNonce nonce,
    std::vector<std::uint8_t> credential
) {
    RuntimeInternetServiceAuthenticationRequest request;
    request.channel = RuntimeInternetServiceWireChannel::Coordinator;
    request.nonce = nonce;
    request.serviceId = std::string(serviceId);
    request.peerId = std::move(peerId);
    request.credential = std::move(credential);
    return request;
}

bool transfer(
    RuntimeInternetServiceHost& host,
    ClientHarness& hostClient,
    ClientHarness& remoteClient,
    RuntimeInternetAuthenticatedTcpStream& source,
    RuntimeInternetAuthenticatedTcpStream& destination,
    std::span<const std::uint8_t> payload,
    std::string& errorMessage
) {
    std::size_t sent = 0U;
    std::vector<std::uint8_t> received;
    const std::uint64_t deadline = nowMilliseconds() + 10000U;
    while (received.size() < payload.size() && nowMilliseconds() < deadline) {
        const RuntimeInternetServiceHostPollResult hostResult = host.poll();
        if (!hostResult.succeeded) {
            errorMessage = hostResult.error;
            return false;
        }
        (void)pumpClient(hostClient);
        (void)pumpClient(remoteClient);
        if (sent < payload.size()) {
            const RuntimeInternetTcpStreamIoResult write = source.write(
                payload.subspan(sent)
            );
            if (write.status == RuntimeInternetTcpStreamIoStatus::Progress) {
                sent += write.bytesTransferred;
            } else if (write.status == RuntimeInternetTcpStreamIoStatus::Failed
                || write.status == RuntimeInternetTcpStreamIoStatus::Closed) {
                errorMessage = write.error.empty()
                    ? "relay source closed"
                    : write.error;
                return false;
            }
        }
        std::array<std::uint8_t, 1024U> bytes{};
        const RuntimeInternetTcpStreamIoResult read = destination.read(bytes);
        if (read.status == RuntimeInternetTcpStreamIoStatus::Progress) {
            received.insert(
                received.end(),
                bytes.begin(),
                bytes.begin()
                    + static_cast<std::ptrdiff_t>(read.bytesTransferred)
            );
        } else if (read.status == RuntimeInternetTcpStreamIoStatus::Failed
            || read.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            errorMessage = read.error.empty()
                ? "relay destination closed"
                : read.error;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (received != std::vector<std::uint8_t>(payload.begin(), payload.end())) {
        errorMessage = "relayed payload did not arrive intact";
        return false;
    }
    return true;
}

bool runSmoke() {
    SmokeContext smoke;
    std::string error;
    TestCertificate certificate;
    smoke.expect(certificate.create(error), error);
    if (smoke.failures != 0) return false;

    const std::vector<std::string> knownPeers = {
        "match-host", "match-client", "replay-peer", "limit-peer",
        "route-peer", "queued-peer", "quota-survivor"
    };
    RuntimeInternetServiceHostConfig hostConfig;
    hostConfig.serviceId = std::string(serviceId);
    hostConfig.tls.port = 0U;
    hostConfig.tls.bindScope = RuntimeInternetTlsBindScope::LoopbackIpv4;
    hostConfig.tls.maximumConnections = 32U;
    hostConfig.tls.nativeCertificateContext = certificate.certificate();
    hostConfig.tls.limits.handshakeTimeoutMilliseconds = 10000U;
    hostConfig.coordinator.serviceId = std::string(serviceId);
    hostConfig.coordinator.udpRelayEndpoint = {"127.0.0.1", 9U};
    hostConfig.coordinator.tcpRelayEndpoint = {"127.0.0.1", 0U};
    hostConfig.coordinator.roomLeaseMilliseconds = 10000U;
    hostConfig.coordinator.connectionLeaseMilliseconds = 10000U;
    hostConfig.coordinator.relayLeaseMilliseconds = 10000U;
    hostConfig.coordinator.replayRetentionMilliseconds = 10000U;
    hostConfig.coordinator.maximumRooms = 8U;
    hostConfig.coordinator.maximumConnections = 16U;
    hostConfig.coordinator.maximumConnectionsPerRoom = 4U;
    hostConfig.coordinator.maximumSpectatorsPerRoom = 2U;
    hostConfig.coordinator.maximumRelayRoutes = 8U;
    hostConfig.coordinator.maximumReplayTombstones = 32U;
    hostConfig.coordinator.nowMilliseconds = nowMilliseconds;
    hostConfig.coordinator.secureRandomBytes = secureRandom;
    hostConfig.matchmaking.serviceId = std::string(serviceId);
    hostConfig.matchmaking.queueLeaseMilliseconds = 10000U;
    hostConfig.matchmaking.matchLeaseMilliseconds = 20000U;
    hostConfig.matchmaking.maximumQueuedPeers = 8U;
    hostConfig.matchmaking.maximumActiveMatches = 4U;
    hostConfig.matchmaking.nowMilliseconds = nowMilliseconds;
    hostConfig.matchmaking.secureRandomBytes = secureRandom;
    hostConfig.relay.maximumQueuedBytes = 2U * 1024U * 1024U;
    hostConfig.relay.idleTimeoutMilliseconds = 0U;
    hostConfig.limits.applicationAuthenticationTimeoutMilliseconds = 150U;
    hostConfig.limits.pendingRelayTimeoutMilliseconds = 5000U;
    hostConfig.limits.relayPairIdleTimeoutMilliseconds = 0U;
    hostConfig.limits.relayHeartbeatMilliseconds = 500U;
    hostConfig.limits.maximumCoordinatorConnections = 8U;
    hostConfig.limits.maximumCoordinatorConnectionsPerPeer = 1U;
    hostConfig.limits.maximumPendingRelayLegs = 8U;
    hostConfig.limits.maximumActiveRelayPairs = 4U;
    hostConfig.limits.maximumReplayNonces = 64U;
    hostConfig.limits.maximumReplayNoncesPerPeer = 8U;
    hostConfig.limits.replayNonceRetentionMilliseconds = 10000U;
    hostConfig.nowMilliseconds = nowMilliseconds;
    hostConfig.verifyCredential = [knownPeers](
        const RuntimeInternetServiceCredentialRequest& request,
        RuntimeInternetServiceVerifiedIdentity& identity,
        std::string& errorMessage
    ) {
        for (const std::string& peer : knownPeers) {
            const std::vector<std::uint8_t> expected = credentialFor(peer);
            if (std::equal(
                    request.credential.begin(),
                    request.credential.end(),
                    expected.begin(),
                    expected.end()
                )) {
                identity.serviceId = std::string(serviceId);
                identity.peerId = peer;
                errorMessage.clear();
                return true;
            }
        }
        errorMessage = "credential rejected";
        return false;
    };

    smoke.stage = "host open";
    RuntimeInternetServiceHost host;
    smoke.expect(host.open(std::move(hostConfig), error), error);
    smoke.expect(host.port() != 0U, "host did not bind a TLS port");
    if (smoke.failures != 0) return false;

    smoke.stage = "coordinator authentication";
    ClientHarness hostClient;
    hostClient.peerId = "match-host";
    ClientHarness remoteClient;
    remoteClient.peerId = "match-client";
    smoke.expect(
        hostClient.community.open(
            {std::string(serviceId), hostClient.peerId},
            error
        ) && remoteClient.community.open(
            {std::string(serviceId), remoteClient.peerId},
            error
        ),
        "community clients should initialize: " + error
    );
    smoke.expect(
        hostClient.transport.open(
            transportConfig(
                hostClient.peerId,
                host.port(),
                certificate.chainEngine()
            ),
            error
        ),
        error
    );
    smoke.expect(
        remoteClient.transport.open(
            transportConfig(
                remoteClient.peerId,
                host.port(),
                certificate.chainEngine()
            ),
            error
        ),
        error
    );
    std::array<ClientHarness*, 2U> clients{&hostClient, &remoteClient};
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return hostClient.transport.coordinatorAuthenticated()
                && remoteClient.transport.coordinatorAuthenticated();
        },
        error
    ), error);

    smoke.stage = "community profile registration";
    RuntimeNetworkPacket communityPacket;
    smoke.expect(
        hostClient.community.makeRegisterProfileRequest(
            91U,
            "HostCommander",
            communityPacket,
            error
        ) && hostClient.transport.sendCoordinator(
            communityPacket,
            error
        ),
        error
    );
    smoke.expect(
        remoteClient.community.makeRegisterProfileRequest(
            92U,
            "ClientCommander",
            communityPacket,
            error
        ) && remoteClient.transport.sendCoordinator(
            communityPacket,
            error
        ),
        error
    );
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return hostClient.community.registeredProfile()
                && remoteClient.community.registeredProfile()
                && host.stats().registeredCommunityAccounts == 2U
                && host.stats().onlineCommunityAccounts == 2U;
        },
        error
    ), error);

    smoke.stage = "community Clan creation";
    smoke.expect(
        hostClient.community.makeCreateClanRequest(
            191U,
            "Host Alliance",
            "HOST",
            communityPacket,
            error
        ) && hostClient.transport.sendCoordinator(
            communityPacket,
            error
        ),
        error
    );
    smoke.expect(
        remoteClient.community.makeCreateClanRequest(
            192U,
            "Client Alliance",
            "CLNT",
            communityPacket,
            error
        ) && remoteClient.transport.sendCoordinator(
            communityPacket,
            error
        ),
        error
    );
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return hostClient.community.snapshot().clanMember
                && remoteClient.community.snapshot().clanMember
                && host.stats().registeredCommunityClans == 2U;
        },
        error
    ), error);

    smoke.stage = "matchmaking enqueue";
    RuntimeInternetMatchmakingRequest hostRequest;
    hostRequest.requestId = 101U;
    hostRequest.queueId = "ranked";
    hostRequest.regionId = "local";
    hostRequest.contentFingerprint = "content-v1";
    hostRequest.displayName = "Host";
    hostRequest.playerCount = 2U;
    RuntimeNetworkPacket packet;
    smoke.expect(hostClient.matchmaking.begin(
        {std::string(serviceId), hostClient.peerId, 1000U},
        hostRequest,
        nowMilliseconds(),
        packet,
        error
    ), error);
    smoke.expect(hostClient.transport.sendCoordinator(packet, error), error);
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return hostClient.matchmaking.state()
                == RuntimeInternetMatchmakingClientState::Queued;
        },
        error
    ), error);

    RuntimeInternetMatchmakingRequest remoteRequest = hostRequest;
    remoteRequest.requestId = 201U;
    remoteRequest.displayName = "Client";
    smoke.expect(remoteClient.matchmaking.begin(
        {std::string(serviceId), remoteClient.peerId, 1000U},
        remoteRequest,
        nowMilliseconds(),
        packet,
        error
    ), error);
    smoke.expect(remoteClient.transport.sendCoordinator(packet, error), error);
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return hostClient.matchmaking.assignment() != nullptr;
        },
        error
    ), error);
    const RuntimeInternetMatchmakingAssignment hostAssignment =
        *hostClient.matchmaking.assignment();
    smoke.expect(
        remoteClient.matchmaking.assignment() == nullptr,
        "non-host assignment was released before activation"
    );

    smoke.stage = "matched room registration and activation";
    RuntimeInternetControlMessage registration = peerMessage(
        RuntimeInternetControlType::RegisterRoom,
        301U,
        hostAssignment.roomId,
        hostClient.peerId
    );
    RuntimeInternetRoomRecord room;
    room.roomId = hostAssignment.roomId;
    room.roomName = "Matched room";
    room.hostPeerId = hostClient.peerId;
    room.contentFingerprint = "content-v1";
    room.playerCount = 1U;
    room.maximumPlayers = 2U;
    room.passwordProtected = true;
    registration.body = RuntimeInternetRegisterRoomBody{
        room,
        {{RuntimeInternetCandidateKind::Host, {"127.0.0.1", 40001U}, 1U}}
    };
    smoke.expect(sendControl(hostClient, registration, {}, error), error);
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return std::any_of(
                hostClient.coordinatorMessages.begin(),
                hostClient.coordinatorMessages.end(),
                [](const RuntimeInternetControlMessage& message) {
                    return message.type
                        == RuntimeInternetControlType::RegisterAccepted;
                }
            );
        },
        error
    ), error);
    (void)takeMessage(
        hostClient.coordinatorMessages,
        RuntimeInternetControlType::RegisterAccepted
    );
    smoke.expect(hostClient.matchmaking.requestActivateMatch(
        401U,
        packet,
        error
    ), error);
    smoke.expect(hostClient.transport.sendCoordinator(packet, error), error);
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return remoteClient.matchmaking.assignment() != nullptr;
        },
        error
    ), error);
    const RuntimeInternetMatchmakingAssignment remoteAssignment =
        *remoteClient.matchmaking.assignment();
    smoke.expect(
        remoteAssignment.roomId == hostAssignment.roomId
            && remoteAssignment.hostPeerId == hostClient.peerId
            && !remoteAssignment.admissionProof.empty(),
        "activated matchmaking assignment was incomplete"
    );

    smoke.stage = "ranked community settlement";
    smoke.expect(
        hostClient.community.makeReportQuickMatchRequest(
            93U,
            hostAssignment.matchId,
            remoteClient.peerId,
            RuntimeInternetCommunityQuickMatchResult::Win,
            communityPacket,
            error
        ) && hostClient.transport.sendCoordinator(
            communityPacket,
            error
        ),
        error
    );
    smoke.expect(
        remoteClient.community.makeReportQuickMatchRequest(
            94U,
            remoteAssignment.matchId,
            hostClient.peerId,
            RuntimeInternetCommunityQuickMatchResult::Loss,
            communityPacket,
            error
        ) && remoteClient.transport.sendCoordinator(
            communityPacket,
            error
        ),
        error
    );
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            const auto& winner = hostClient.community.snapshot().profile;
            const auto& loser = remoteClient.community.snapshot().profile;
            const auto& winnerClan = hostClient.community.snapshot().clan;
            const auto& loserClan = remoteClient.community.snapshot().clan;
            return winner.wins == 1U && winner.points == 1032
                && loser.losses == 1U && loser.points == 984
                && winnerClan.wins == 1U && winnerClan.points == 1032
                && loserClan.losses == 1U && loserClan.points == 984;
        },
        error
    ), error);

    smoke.stage = "proof-bound join";
    RuntimeInternetControlMessage join = peerMessage(
        RuntimeInternetControlType::JoinRoom,
        501U,
        remoteAssignment.roomId,
        remoteClient.peerId
    );
    join.body = RuntimeInternetJoinRoomBody{
        RuntimeInternetRole::Client,
        "content-v1",
        {{RuntimeInternetCandidateKind::Host, {"127.0.0.1", 40002U}, 1U}}
    };
    smoke.expect(sendControl(
        remoteClient,
        join,
        remoteAssignment.admissionProof,
        error
    ), error);
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            const auto hostOffer = std::find_if(
                hostClient.coordinatorMessages.begin(),
                hostClient.coordinatorMessages.end(),
                [](const RuntimeInternetControlMessage& message) {
                    return message.type
                        == RuntimeInternetControlType::PeerCandidates;
                }
            );
            const auto remoteOffer = std::find_if(
                remoteClient.coordinatorMessages.begin(),
                remoteClient.coordinatorMessages.end(),
                [](const RuntimeInternetControlMessage& message) {
                    return message.type
                        == RuntimeInternetControlType::PeerCandidates;
                }
            );
            return hostOffer != hostClient.coordinatorMessages.end()
                && remoteOffer != remoteClient.coordinatorMessages.end();
        },
        error
    ), error);
    const auto hostOfferMessage = takeMessage(
        hostClient.coordinatorMessages,
        RuntimeInternetControlType::PeerCandidates
    );
    const auto remoteOfferMessage = takeMessage(
        remoteClient.coordinatorMessages,
        RuntimeInternetControlType::PeerCandidates
    );
    const auto& hostOffer = std::get<RuntimeInternetPeerCandidatesBody>(
        hostOfferMessage->body
    );
    const auto& remoteOffer = std::get<RuntimeInternetPeerCandidatesBody>(
        remoteOfferMessage->body
    );
    smoke.expect(
        hostOffer.connectionId == remoteOffer.connectionId
            && hostOffer.connectionToken == remoteOffer.connectionToken,
        "coordinator offers did not describe one authenticated route"
    );

    smoke.stage = "relay lease";
    auto requestRelay = [&](
       ClientHarness& client,
        const RuntimeInternetPeerCandidatesBody& offer
    ) {
        RuntimeInternetControlMessage request = peerMessage(
            RuntimeInternetControlType::RelayRequest,
            offer.connectionId,
            hostAssignment.roomId,
            client.peerId
        );
        request.body = RuntimeInternetRelayRequestBody{
            offer.connectionId,
            RuntimeInternetRelayTransport::Tcp,
            offer.connectionToken
        };
        return sendControl(client, request, {}, error);
    };
    smoke.expect(requestRelay(hostClient, hostOffer), error);
    smoke.expect(requestRelay(remoteClient, remoteOffer), error);
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return std::any_of(
                    hostClient.coordinatorMessages.begin(),
                    hostClient.coordinatorMessages.end(),
                    [](const RuntimeInternetControlMessage& message) {
                        return message.type
                            == RuntimeInternetControlType::RelayLease;
                    }
                )
                && std::any_of(
                    remoteClient.coordinatorMessages.begin(),
                    remoteClient.coordinatorMessages.end(),
                    [](const RuntimeInternetControlMessage& message) {
                        return message.type
                            == RuntimeInternetControlType::RelayLease;
                    }
                );
        },
        error
    ), error);
    const auto hostLeaseMessage = takeMessage(
        hostClient.coordinatorMessages,
        RuntimeInternetControlType::RelayLease
    );
    const auto remoteLeaseMessage = takeMessage(
        remoteClient.coordinatorMessages,
        RuntimeInternetControlType::RelayLease
    );
    const auto& hostLease = std::get<RuntimeInternetRelayLeaseBody>(
        hostLeaseMessage->body
    );
    const auto& remoteLease = std::get<RuntimeInternetRelayLeaseBody>(
        remoteLeaseMessage->body
    );

    auto relayOpenRequest = [&](
        const ClientHarness& local,
        RuntimeInternetRole localRole,
        const ClientHarness& remote,
        RuntimeInternetRole remoteRole,
        const RuntimeInternetRelayLeaseBody& lease
    ) {
        RuntimeInternetServiceTcpOpenRequest request;
        request.endpoint = lease.endpoint;
        request.route = {
            RuntimeInternetTcpRouteKind::Relay,
            lease.connectionId,
            std::string(serviceId),
            hostAssignment.roomId,
            local.peerId,
            localRole,
            remote.peerId,
            remoteRole,
            lease.endpoint
        };
        RuntimeInternetControlMessage bind = peerMessage(
            RuntimeInternetControlType::RelayBind,
            lease.connectionId,
            hostAssignment.roomId,
            local.peerId,
            std::string(serviceId)
        );
        bind.body = RuntimeInternetRelayBindBody{
            lease.connectionId,
            RuntimeInternetRelayTransport::Tcp,
            lease.ticket
        };
        (void)encodeRuntimeInternetControlMessage(
            bind,
            request.relayBindPacket,
            error
        );
        return request;
    };
    smoke.stage = "two-leg relay activation";
    RuntimeInternetServiceTcpOpenRequest hostRelay = relayOpenRequest(
        hostClient,
        RuntimeInternetRole::Host,
        remoteClient,
        RuntimeInternetRole::Client,
        hostLease
    );
    RuntimeInternetServiceTcpOpenRequest remoteRelay = relayOpenRequest(
        remoteClient,
        RuntimeInternetRole::Client,
        hostClient,
        RuntimeInternetRole::Host,
        remoteLease
    );
    smoke.expect(
        hostClient.transport.openAuthenticatedTcpRelay(hostRelay, error),
        error
    );
    smoke.expect(
        remoteClient.transport.openAuthenticatedTcpRelay(remoteRelay, error),
        error
    );
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return hostClient.relayStream != nullptr
                && remoteClient.relayStream != nullptr
                && host.stats().activeRelayPairs == 1U;
        },
        error
    ), error);
    smoke.expect(
        !hostClient.streamArrivedBeforeBound
            && !remoteClient.streamArrivedBeforeBound,
        "relay stream was exposed before RelayBound"
    );
    smoke.expect(
        !hostClient.relayMessages.empty()
            && !remoteClient.relayMessages.empty()
            && hostClient.relayMessages.front().type
                == RuntimeInternetControlType::RelayBound
            && remoteClient.relayMessages.front().type
                == RuntimeInternetControlType::RelayBound,
        "both clients did not receive TCP RelayBound before their stream"
    );

    smoke.stage = "bidirectional relay bytes";
    RuntimeNetworkPacket hostPacket;
    hostPacket.type = 0x7001U;
    hostPacket.payload = {0x01U, 0x02U, 0x03U, 0x7fU, 0x80U, 0xfeU, 0xffU};
    RuntimeNetworkPacket remotePacket;
    remotePacket.type = 0x7002U;
    remotePacket.payload = {0x55U, 0x44U, 0x33U, 0x22U, 0x11U};
    std::vector<std::uint8_t> hostPayload;
    std::vector<std::uint8_t> remotePayload;
    smoke.expect(encodeRuntimeNetworkPacket(
        hostPacket,
        hostPayload,
        error
    ), error);
    smoke.expect(encodeRuntimeNetworkPacket(
        remotePacket,
        remotePayload,
        error
    ), error);
    smoke.expect(transfer(
        host,
        hostClient,
        remoteClient,
        *hostClient.relayStream,
        *remoteClient.relayStream,
        hostPayload,
        error
    ), error);
    smoke.expect(transfer(
        host,
        hostClient,
        remoteClient,
        *remoteClient.relayStream,
        *hostClient.relayStream,
        remotePayload,
        error
    ), error);

    smoke.stage = "wrong credential";
    const RuntimeInternetTlsClientConfig rawTls = tlsClientConfig(
        host.port(),
        certificate.chainEngine()
    );
    RawTlsConnection wrongCredential;
    smoke.expect(connectRaw(
        host,
        rawTls,
        "replay-peer",
        wrongCredential,
        error
    ), error);
    RuntimeInternetServiceWireMessage authResponse;
    smoke.expect(sendRawAuthentication(
        host,
        wrongCredential,
        coordinatorAuthentication(
            "replay-peer",
            nonceFor(1U),
            std::vector<std::uint8_t>(16U, 0xeeU)
        ),
        authResponse,
        error
    ), error);
    smoke.expect(
        std::holds_alternative<
            RuntimeInternetServiceAuthenticationRejection
        >(authResponse.body),
        "wrong credential was not rejected"
    );

    smoke.stage = "nonce replay";
    RawTlsConnection replayFirst;
    smoke.expect(connectRaw(
        host,
        rawTls,
        "replay-peer",
        replayFirst,
        error
    ), error);
    const RuntimeInternetServiceNonce replayNonce = nonceFor(31U);
    smoke.expect(sendRawAuthentication(
        host,
        replayFirst,
        coordinatorAuthentication(
            "replay-peer",
            replayNonce,
            credentialFor("replay-peer")
        ),
        authResponse,
        error
    ), error);
    smoke.expect(
        std::holds_alternative<
            RuntimeInternetServiceAuthenticationConfirmation
        >(authResponse.body),
        "first nonce use was not authenticated"
    );
    replayFirst.stream->close();
    const std::uint64_t replayCloseDeadline = nowMilliseconds() + 1000U;
    while (nowMilliseconds() < replayCloseDeadline
        && host.stats().coordinatorConnections > 2U) {
        (void)host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    smoke.expect(
        host.stats().coordinatorConnections == 2U,
        "first raw coordinator connection did not close before replay"
    );
    RawTlsConnection replaySecond;
    smoke.expect(connectRaw(
        host,
        rawTls,
        "replay-peer",
        replaySecond,
        error
    ), error);
    smoke.expect(sendRawAuthentication(
        host,
        replaySecond,
        coordinatorAuthentication(
            "replay-peer",
            replayNonce,
            credentialFor("replay-peer")
        ),
        authResponse,
        error
    ), error);
    smoke.expect(
        std::holds_alternative<
            RuntimeInternetServiceAuthenticationRejection
        >(authResponse.body),
        "replayed application nonce was not rejected"
    );

    smoke.stage = "claim spoof";
    RawTlsConnection spoofedClaim;
    smoke.expect(connectRaw(
        host,
        rawTls,
        "match-host",
        spoofedClaim,
        error
    ), error);
    smoke.expect(sendRawAuthentication(
        host,
        spoofedClaim,
        coordinatorAuthentication(
            "match-host",
            nonceFor(71U),
            credentialFor("route-peer")
        ),
        authResponse,
        error
    ), error);
    smoke.expect(
        std::holds_alternative<
            RuntimeInternetServiceAuthenticationRejection
        >(authResponse.body),
        "credential identity was replaced by a spoofed wire claim"
    );

    smoke.stage = "wrong relay route";
    RawTlsConnection wrongRoute;
    smoke.expect(connectRaw(
        host,
        rawTls,
        "route-peer",
        wrongRoute,
        error
    ), error);
    RuntimeInternetServiceAuthenticationRequest wrongRouteRequest;
    wrongRouteRequest.channel = RuntimeInternetServiceWireChannel::RelayTcp;
    wrongRouteRequest.nonce = nonceFor(81U);
    wrongRouteRequest.serviceId = std::string(serviceId);
    wrongRouteRequest.peerId = "route-peer";
    wrongRouteRequest.credential = credentialFor("route-peer");
    wrongRouteRequest.route = {
        RuntimeInternetTcpRouteKind::Relay,
        9999U,
        std::string(serviceId),
        hostAssignment.roomId,
        "route-peer",
        RuntimeInternetRole::Client,
        hostClient.peerId,
        RuntimeInternetRole::Host,
        {"127.0.0.1", host.port()}
    };
    RuntimeInternetControlMessage wrongBind = peerMessage(
        RuntimeInternetControlType::RelayBind,
        9999U,
        hostAssignment.roomId,
        "route-peer",
        std::string(serviceId)
    );
    wrongBind.body = RuntimeInternetRelayBindBody{
        9999U,
        RuntimeInternetRelayTransport::Tcp,
        std::vector<std::uint8_t>(32U, 0x5aU)
    };
    smoke.expect(encodeRuntimeInternetControlMessage(
        wrongBind,
        wrongRouteRequest.relayBindPacket,
        error
    ), error);
    smoke.expect(sendRawAuthentication(
        host,
        wrongRoute,
        std::move(wrongRouteRequest),
        authResponse,
        error
    ), error);
    smoke.expect(
        std::holds_alternative<
            RuntimeInternetServiceAuthenticationRejection
        >(authResponse.body),
        "unknown or mismatched relay route was not rejected"
    );

    smoke.stage = "wire frame limit";
    const std::size_t stableTlsConnectionCount =
        host.stats().tlsConnections;
    RawTlsConnection oversizedFrame;
    smoke.expect(connectRaw(
        host,
        rawTls,
        "route-peer",
        oversizedFrame,
        error
    ), error);
    const std::array<std::uint8_t, 4U> oversizedLength{
        0x00U, 0x20U, 0x00U, 0x00U
    };
    smoke.expect(
        oversizedFrame.stream
            && writeAll(
                host,
                *oversizedFrame.stream,
                oversizedLength,
                error
            ),
        error
    );
    const std::uint64_t frameCloseDeadline = nowMilliseconds() + 1000U;
    while (nowMilliseconds() < frameCloseDeadline
        && host.stats().tlsConnections > stableTlsConnectionCount) {
        (void)host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    smoke.expect(
        host.stats().tlsConnections == stableTlsConnectionCount,
        "oversized application frame connection was not reclaimed"
    );

    smoke.stage = "connection limit";
    RawTlsConnection limitFirst;
    smoke.expect(connectRaw(
        host,
        rawTls,
        "limit-peer",
        limitFirst,
        error
    ), error);
    smoke.expect(sendRawAuthentication(
        host,
        limitFirst,
        coordinatorAuthentication(
            "limit-peer",
            nonceFor(91U),
            credentialFor("limit-peer")
        ),
        authResponse,
        error
    ), error);
    RawTlsConnection limitSecond;
    smoke.expect(connectRaw(
        host,
        rawTls,
        "limit-peer",
        limitSecond,
        error
    ), error);
    smoke.expect(sendRawAuthentication(
        host,
        limitSecond,
        coordinatorAuthentication(
            "limit-peer",
            nonceFor(111U),
            credentialFor("limit-peer")
        ),
        authResponse,
        error
    ), error);
    smoke.expect(
        std::holds_alternative<
            RuntimeInternetServiceAuthenticationRejection
        >(authResponse.body),
        "per-peer coordinator connection limit was not enforced"
    );

    smoke.stage = "quota rejection nonce isolation";
    for (std::uint8_t attempt = 0U; attempt < 12U; ++attempt) {
        RawTlsConnection rejectedQuotaAttempt;
        error.clear();
        smoke.expect(connectRaw(
            host,
            rawTls,
            "limit-peer",
            rejectedQuotaAttempt,
            error
        ), error);
        if (!rejectedQuotaAttempt.stream) continue;
        error.clear();
        smoke.expect(sendRawAuthentication(
            host,
            rejectedQuotaAttempt,
            coordinatorAuthentication(
                "limit-peer",
                nonceFor(static_cast<std::uint8_t>(130U + attempt)),
                credentialFor("limit-peer")
            ),
            authResponse,
            error
        ), error);
        smoke.expect(
            std::holds_alternative<
                RuntimeInternetServiceAuthenticationRejection
            >(authResponse.body),
            "quota-rejected coordinator attempt was unexpectedly accepted"
        );
        rejectedQuotaAttempt.stream->close();
    }
    RawTlsConnection quotaSurvivor;
    error.clear();
    smoke.expect(connectRaw(
        host,
        rawTls,
        "quota-survivor",
        quotaSurvivor,
        error
    ), error);
    error.clear();
    smoke.expect(sendRawAuthentication(
        host,
        quotaSurvivor,
        coordinatorAuthentication(
            "quota-survivor",
            nonceFor(160U),
            credentialFor("quota-survivor")
        ),
        authResponse,
        error
    ), error);
    smoke.expect(
        std::holds_alternative<
            RuntimeInternetServiceAuthenticationConfirmation
        >(authResponse.body),
        "one peer's quota rejections consumed another peer's nonce capacity"
    );
    if (quotaSurvivor.stream) quotaSurvivor.stream->close();

    smoke.stage = "authentication timeout";
    RawTlsConnection timeoutConnection;
    smoke.expect(connectRaw(
        host,
        rawTls,
        "route-peer",
        timeoutConnection,
        error
    ), error);
    const std::uint64_t timeoutDeadline = nowMilliseconds() + 1000U;
    while (nowMilliseconds() < timeoutDeadline
        && host.stats().authenticatingConnections > 0U) {
        (void)host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    smoke.expect(
        host.stats().authenticatingConnections == 0U,
        "unauthenticated TLS connection was not reclaimed on timeout"
    );

    smoke.stage = "disconnect reclamation";
    smoke.expect(
        host.stats().activeMatches == 1U,
        "activated matchmaking reservation disappeared before disconnect"
    );
    hostClient.relayStream->close();
    remoteClient.relayStream->close();
    smoke.expect(waitUntil(
        host,
        clients,
        [&] { return host.stats().activeRelayPairs == 0U; },
        error
    ), error);
    smoke.expect(
        host.stats().coordinatorConnectionsInRooms == 0U,
        "relay disconnect did not promptly revoke coordinator route state"
    );
    smoke.expect(
        host.stats().activeMatches == 1U
            && host.stats().rooms == 1U,
        "relay-only disconnect incorrectly revoked coordinator presence"
    );

    smoke.stage = "coordinator disconnect matchmaking reclamation";
    const std::size_t coordinatorsBeforeRemoteClose =
        host.stats().coordinatorConnections;
    remoteClient.transport.close();
    smoke.expect(waitUntil(
        host,
        clients,
        [&] {
            return host.stats().coordinatorConnections + 1U
                    == coordinatorsBeforeRemoteClose
                && host.stats().activeMatches == 0U;
        },
        error
    ), error);

    smoke.stage = "queued peer disconnect reclamation";
    ClientHarness queuedClient;
    queuedClient.peerId = "queued-peer";
    smoke.expect(
        queuedClient.transport.open(
            transportConfig(
                queuedClient.peerId,
                host.port(),
                certificate.chainEngine()
            ),
            error
        ),
        error
    );
    std::array<ClientHarness*, 2U> queueClients{
        &hostClient,
        &queuedClient
    };
    smoke.expect(waitUntil(
        host,
        queueClients,
        [&] { return queuedClient.transport.coordinatorAuthenticated(); },
        error
    ), error);
    RuntimeInternetMatchmakingRequest queuedRequest = hostRequest;
    queuedRequest.requestId = 701U;
    queuedRequest.contentFingerprint = "queue-disconnect-content";
    queuedRequest.playerCount = 3U;
    queuedRequest.displayName = "Queued";
    smoke.expect(queuedClient.matchmaking.begin(
        {std::string(serviceId), queuedClient.peerId, 1000U},
        queuedRequest,
        nowMilliseconds(),
        packet,
        error
    ), error);
    smoke.expect(
        queuedClient.transport.sendCoordinator(packet, error),
        error
    );
    smoke.expect(waitUntil(
        host,
        queueClients,
        [&] {
            return queuedClient.matchmaking.state()
                    == RuntimeInternetMatchmakingClientState::Queued
                && host.stats().queuedMatchmakingPeers == 1U;
        },
        error
    ), error);
    const std::size_t coordinatorsBeforeQueuedClose =
        host.stats().coordinatorConnections;
    queuedClient.transport.close();
    smoke.expect(waitUntil(
        host,
        queueClients,
        [&] {
            return host.stats().queuedMatchmakingPeers == 0U
                && host.stats().coordinatorConnections + 1U
                    == coordinatorsBeforeQueuedClose;
        },
        error
    ), error);

    smoke.stage = "host coordinator room reclamation";
    hostClient.transport.close();
    smoke.expect(waitUntil(
        host,
        queueClients,
        [&] { return host.stats().rooms == 0U; },
        error
    ), error);

    smoke.stage = "clean shutdown";
    limitFirst.stream->close();
    timeoutConnection.stream->close();
    host.close();
    smoke.expect(
        !host.isOpen() && host.stats().tlsConnections == 0U,
        "self-hosted service did not close cleanly"
    );

    if (smoke.failures == 0) {
        std::cout << "Runtime Internet service host smoke passed\n";
    }
    return smoke.failures == 0;
}

} // namespace

int main() {
    return runSmoke() ? 0 : 1;
}

#else

int main() {
    std::cout << "Runtime Internet service host smoke skipped: Windows only\n";
    return 0;
}

#endif
