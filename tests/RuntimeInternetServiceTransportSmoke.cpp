#include "RuntimeInternetServiceTransport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <memory>
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

struct SmokeContext {
    int failures = 0;

    void expect(bool condition, const std::string& message) {
        if (condition) return;
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
};

std::uint64_t nowMilliseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
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
        std::array<std::uint8_t, 8> random{};
        if (BCryptGenRandom(
                nullptr,
                random.data(),
                static_cast<ULONG>(random.size()),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG
            ) != 0) {
            errorMessage = "could not generate test certificate key name";
            return false;
        }
        container_ = L"ConcordRTS-ServiceSmoke-";
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

        DWORD subjectBytes = 0;
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
        CERT_ALT_NAME_INFO alternateNames{1, &alternateName};
        BYTE* alternateNameBytes = nullptr;
        DWORD alternateNameSize = 0;
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
        CERT_ENHKEY_USAGE usage{1, usageIdentifiers};
        BYTE* usageBytes = nullptr;
        DWORD usageSize = 0;
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
        CERT_EXTENSIONS extensionList{2, extensions};

        CRYPT_KEY_PROV_INFO keyProvider{};
        keyProvider.pwszContainerName = container_.data();
        keyProvider.pwszProvName = const_cast<wchar_t*>(MS_ENH_RSA_AES_PROV_W);
        keyProvider.dwProvType = PROV_RSA_AES;
        keyProvider.dwKeySpec = AT_KEYEXCHANGE;
        CRYPT_ALGORITHM_IDENTIFIER signature{};
        signature.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
        SYSTEMTIME now{};
        SYSTEMTIME validFrom{};
        SYSTEMTIME validUntil{};
        GetSystemTime(&now);
        if (shiftSystemTime(now, -86400, validFrom)
            && shiftSystemTime(now, 7 * 86400, validUntil)) {
            certificate_ = CertCreateSelfSignCertificate(
                static_cast<HCRYPTPROV_OR_NCRYPT_KEY_HANDLE>(provider_),
                &subjectBlob,
                0,
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
                0,
                &keyProvider
            )) {
            errorMessage = "could not create local test certificate";
            return false;
        }

        rootStore_ = CertOpenStore(
            CERT_STORE_PROV_MEMORY,
            0,
            0,
            0,
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
            CertCloseStore(rootStore_, 0);
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
            CryptReleaseContext(provider_, 0);
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

RuntimeInternetAuthenticatedTcpStreamIdentity provisionalIdentity(
    std::uint64_t connectionId
) {
    RuntimeInternetAuthenticatedTcpStreamIdentity identity;
    identity.authenticatedServiceId = "smoke-service";
    identity.authenticatedPeerId =
        "pending-" + std::to_string(connectionId);
    return identity;
}

enum class SimulatedStage : std::uint8_t {
    AwaitAuthentication,
    CoordinatorConfirmation,
    Coordinator,
    RelayConfirmationDelay,
    RelayConfirmation,
    RelayRaw
};

class SimulatedService {
public:
    bool open(PCCERT_CONTEXT certificate, std::string& errorMessage) {
        RuntimeInternetTlsServerConfig config;
        config.bindScope = RuntimeInternetTlsBindScope::LoopbackIpv4;
        config.maximumConnections = 32;
        config.nativeCertificateContext = certificate;
        return server_.open(config, errorMessage);
    }

    std::uint16_t port() const { return server_.port(); }
    std::size_t rejectedTlsConnections() const { return rejectedTls_; }
    const std::vector<std::uint8_t>& lastAdmissionProof() const {
        return lastAdmissionProof_;
    }
    bool relayBindObserved() const { return relayBindObserved_; }
    std::size_t maximumRawQueued() const { return maximumRawQueued_; }

    void poll() {
        const std::uint64_t now = nowMilliseconds();
        for (const RuntimeInternetTlsServerEvent& event : server_.poll(now)) {
            if (event.kind == RuntimeInternetTlsServerEventKind::Rejected) {
                ++rejectedTls_;
                continue;
            }
            if (event.kind
                != RuntimeInternetTlsServerEventKind::HandshakeComplete) {
                continue;
            }
            std::string error;
            auto stream = server_.takeApplicationAuthenticatedStream(
                event.connectionId,
                provisionalIdentity(event.connectionId),
                error
            );
            if (!stream) {
                lastError_ = std::move(error);
                continue;
            }
            Connection connection;
            connection.stream = std::move(stream);
            connections_.emplace(event.connectionId, std::move(connection));
        }
        for (auto iterator = connections_.begin();
             iterator != connections_.end();) {
            if (!step(iterator->second, now)) {
                iterator = connections_.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

private:
    struct Connection {
        std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> stream;
        RuntimeInternetServiceWireDecoder decoder;
        std::deque<std::vector<std::uint8_t>> output;
        std::size_t outputOffset = 0;
        std::size_t queuedBytes = 0;
        SimulatedStage stage = SimulatedStage::AwaitAuthentication;
        RuntimeInternetServiceWireChannel channel =
            RuntimeInternetServiceWireChannel::Coordinator;
        RuntimeInternetServiceNonce nonce{};
        std::string serviceId;
        std::string peerId;
        RuntimeInternetTcpRouteDescriptor route;
        int confirmationDelay = 0;
        bool oversizeSent = false;
    };

    bool queueFrame(
        Connection& connection,
        const RuntimeInternetServiceWireMessage& message
    ) {
        std::vector<std::uint8_t> frame;
        std::string error;
        if (!encodeRuntimeInternetServiceWireFrame(message, frame, error)) {
            lastError_ = std::move(error);
            return false;
        }
        connection.queuedBytes += frame.size();
        maximumRawQueued_ = std::max(
            maximumRawQueued_,
            connection.queuedBytes
        );
        connection.output.push_back(std::move(frame));
        return true;
    }

    bool queueRaw(Connection& connection, std::vector<std::uint8_t> bytes) {
        if (connection.queuedBytes + bytes.size() > 64U * 1024U) {
            return false;
        }
        connection.queuedBytes += bytes.size();
        maximumRawQueued_ = std::max(
            maximumRawQueued_,
            connection.queuedBytes
        );
        connection.output.push_back(std::move(bytes));
        return true;
    }

    bool pumpWrite(Connection& connection) {
        for (int iteration = 0;
             iteration < 32 && !connection.output.empty();
             ++iteration) {
            std::vector<std::uint8_t>& bytes = connection.output.front();
            const RuntimeInternetTcpStreamIoResult write =
                connection.stream->write(
                    std::span<const std::uint8_t>(bytes).subspan(
                        connection.outputOffset
                    )
                );
            if (write.status == RuntimeInternetTcpStreamIoStatus::WouldBlock) {
                return true;
            }
            if (write.status == RuntimeInternetTcpStreamIoStatus::Closed
                || write.status == RuntimeInternetTcpStreamIoStatus::Failed
                || write.bytesTransferred
                    > bytes.size() - connection.outputOffset) {
                return false;
            }
            if (write.bytesTransferred == 0) return true;
            connection.outputOffset += write.bytesTransferred;
            connection.queuedBytes -= write.bytesTransferred;
            if (connection.outputOffset == bytes.size()) {
                connection.output.pop_front();
                connection.outputOffset = 0;
            }
        }
        return true;
    }

    bool readFrame(
        Connection& connection,
        RuntimeInternetServiceWireMessage& message,
        bool& available
    ) {
        available = false;
        for (int iteration = 0; iteration < 16; ++iteration) {
            std::string error;
            if (!connection.decoder.pop(message, available, error)) {
                lastError_ = std::move(error);
                return false;
            }
            if (available) return true;
            std::array<std::uint8_t, 64U * 1024U> bytes{};
            const RuntimeInternetTcpStreamIoResult read =
                connection.stream->read(bytes);
            if (read.status == RuntimeInternetTcpStreamIoStatus::WouldBlock) {
                return true;
            }
            if (read.status == RuntimeInternetTcpStreamIoStatus::Closed
                || read.status == RuntimeInternetTcpStreamIoStatus::Failed) {
                return false;
            }
            if (read.bytesTransferred == 0) return true;
            if (!connection.decoder.push(
                    std::span<const std::uint8_t>(
                        bytes.data(),
                        read.bytesTransferred
                    ),
                    error
                )) {
                lastError_ = std::move(error);
                return false;
            }
        }
        return true;
    }

    bool handleAuthentication(
        Connection& connection,
        const RuntimeInternetServiceAuthenticationRequest& request,
        std::uint64_t now
    ) {
        connection.channel = request.channel;
        connection.nonce = request.nonce;
        connection.serviceId = request.serviceId;
        connection.peerId = request.peerId;
        connection.route = request.route;
        std::string replayError;
        const bool nonceAccepted = replay_.accept(
            request.nonce,
            now,
            replayError
        );
        const std::vector<std::uint8_t> expectedCredential(
            32U,
            0x6dU
        );
        if (!nonceAccepted || request.serviceId != "smoke-service"
            || request.credential != expectedCredential) {
            RuntimeInternetServiceWireMessage rejection;
            rejection.body = RuntimeInternetServiceAuthenticationRejection{
                request.channel,
                request.nonce
            };
            return queueFrame(connection, rejection);
        }

        RuntimeInternetServiceAuthenticationConfirmation confirmation;
        confirmation.channel = request.channel;
        confirmation.nonce = request.nonce;
        confirmation.serviceId = request.serviceId;
        confirmation.peerId = request.peerId;
        confirmation.route = request.route;
        if (request.channel == RuntimeInternetServiceWireChannel::RelayTcp) {
            RuntimeInternetControlMessage bind;
            std::string ignored;
            relayBindObserved_ = decodeRuntimeInternetControlMessage(
                request.relayBindPacket,
                bind,
                ignored
            ) && bind.type == RuntimeInternetControlType::RelayBind;
            if (request.peerId == "wrong-relay-confirm") {
                confirmation.route.roomId += "-forged";
            }
            connection.confirmationDelay = 5;
            connection.stage = SimulatedStage::RelayConfirmationDelay;
        } else {
            connection.stage = SimulatedStage::CoordinatorConfirmation;
        }
        RuntimeInternetServiceWireMessage response;
        response.body = std::move(confirmation);
        return queueFrame(connection, response);
    }

    bool handleCoordinatorMessage(
        Connection& connection,
        const RuntimeInternetServiceWireMessage& message
    ) {
        const auto* packet = std::get_if<
            RuntimeInternetServiceCoordinatorPacket
        >(&message.body);
        if (packet == nullptr) return false;
        lastAdmissionProof_ = packet->roomAdmissionProof;
        RuntimeInternetServiceCoordinatorPacket response;
        response.packet = packet->packet;
        RuntimeInternetServiceWireMessage framed;
        framed.body = std::move(response);
        return queueFrame(connection, framed);
    }

    bool stepRaw(Connection& connection) {
        if (connection.queuedBytes >= 48U * 1024U) return true;
        std::array<std::uint8_t, 16U * 1024U> bytes{};
        const RuntimeInternetTcpStreamIoResult read = connection.stream->read(
            bytes
        );
        if (read.status == RuntimeInternetTcpStreamIoStatus::WouldBlock) {
            return true;
        }
        if (read.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            return false;
        }
        if (read.status == RuntimeInternetTcpStreamIoStatus::Failed) {
            return false;
        }
        if (read.bytesTransferred == 0) return true;
        std::vector<std::uint8_t> echo(
            bytes.begin(),
            bytes.begin()
                + static_cast<std::ptrdiff_t>(read.bytesTransferred)
        );
        return queueRaw(connection, std::move(echo));
    }

    bool step(Connection& connection, std::uint64_t now) {
        if (connection.stage == SimulatedStage::RelayConfirmationDelay) {
            if (--connection.confirmationDelay > 0) return true;
            connection.stage = SimulatedStage::RelayConfirmation;
        }
        if (!pumpWrite(connection)) return false;
        if (connection.output.empty()) {
            if (connection.stage == SimulatedStage::CoordinatorConfirmation) {
                connection.stage = SimulatedStage::Coordinator;
            } else if (connection.stage
                == SimulatedStage::RelayConfirmation) {
                connection.stage = SimulatedStage::RelayRaw;
            }
        }
        if (connection.stage == SimulatedStage::RelayRaw) {
            return stepRaw(connection);
        }
        if (connection.stage == SimulatedStage::Coordinator
            && connection.peerId == "oversize-peer"
            && !connection.oversizeSent) {
            const std::uint32_t invalid = static_cast<std::uint32_t>(
                runtimeInternetServiceMaximumWireFrameBytes + 1U
            );
            std::vector<std::uint8_t> length{
                static_cast<std::uint8_t>((invalid >> 24U) & 0xffU),
                static_cast<std::uint8_t>((invalid >> 16U) & 0xffU),
                static_cast<std::uint8_t>((invalid >> 8U) & 0xffU),
                static_cast<std::uint8_t>(invalid & 0xffU)
            };
            connection.oversizeSent = true;
            return queueRaw(connection, std::move(length));
        }

        RuntimeInternetServiceWireMessage message;
        bool available = false;
        if (!readFrame(connection, message, available)) return false;
        if (!available) return true;
        if (connection.stage == SimulatedStage::AwaitAuthentication) {
            const auto* request = std::get_if<
                RuntimeInternetServiceAuthenticationRequest
            >(&message.body);
            return request != nullptr
                && handleAuthentication(connection, *request, now);
        }
        if (connection.stage == SimulatedStage::Coordinator) {
            return handleCoordinatorMessage(connection, message);
        }
        return false;
    }

    RuntimeInternetTlsServer server_;
    std::map<std::uint64_t, Connection> connections_;
    RuntimeInternetServiceNonceReplayWindow replay_{128U, 60000U};
    std::size_t rejectedTls_ = 0;
    std::size_t maximumRawQueued_ = 0;
    bool relayBindObserved_ = false;
    std::vector<std::uint8_t> lastAdmissionProof_;
    std::string lastError_;
};

RuntimeInternetServiceTransportConfig transportConfig(
    const std::string& peerId,
    std::vector<std::uint8_t> credential,
    const SimulatedService& service,
    HCERTCHAINENGINE chainEngine
) {
    RuntimeInternetServiceTransportConfig config;
    config.serviceId = "smoke-service";
    config.peerId = peerId;
    config.credential = std::move(credential);
    config.coordinatorTls.endpoint = {"127.0.0.1", service.port()};
    config.coordinatorTls.serverName = "localhost";
    config.coordinatorTls.checkCertificateRevocation = false;
    config.coordinatorTls.nativeCertificateChainEngine = chainEngine;
    config.relayTls = config.coordinatorTls;
    config.applicationAuthenticationTimeoutMilliseconds = 5000;
    return config;
}

RuntimeNetworkPacket roomListPacket(
    const std::string& peerId,
    std::uint64_t transactionId,
    std::string& errorMessage
) {
    RuntimeInternetControlMessage message;
    message.type = RuntimeInternetControlType::RoomListRequest;
    message.transactionId = transactionId;
    message.serviceId = "smoke-service";
    message.senderPeerId = peerId;
    message.body = RuntimeInternetRoomListRequestBody{"fingerprint"};
    RuntimeNetworkPacket packet;
    (void)encodeRuntimeInternetControlMessage(message, packet, errorMessage);
    return packet;
}

RuntimeNetworkPacket joinPacket(
    const std::string& peerId,
    std::uint64_t transactionId,
    std::string& errorMessage
) {
    RuntimeInternetControlMessage message;
    message.type = RuntimeInternetControlType::JoinRoom;
    message.transactionId = transactionId;
    message.serviceId = "smoke-service";
    message.roomId = "room-one";
    message.senderPeerId = peerId;
    message.body = RuntimeInternetJoinRoomBody{
        RuntimeInternetRole::Client,
        "fingerprint",
        {
            RuntimeInternetCandidate{
                RuntimeInternetCandidateKind::Host,
                RuntimeInternetEndpoint{"127.0.0.1", 19001},
                100U
            }
        }
    };
    RuntimeNetworkPacket packet;
    (void)encodeRuntimeInternetControlMessage(message, packet, errorMessage);
    return packet;
}

RuntimeNetworkPacket registerRoomPacket(
    const std::string& peerId,
    std::uint64_t transactionId,
    bool passwordProtected,
    std::string& errorMessage
) {
    RuntimeInternetControlMessage message;
    message.type = RuntimeInternetControlType::RegisterRoom;
    message.transactionId = transactionId;
    message.serviceId = "smoke-service";
    message.roomId = "room-registered";
    message.senderPeerId = peerId;
    RuntimeInternetRoomRecord room;
    room.roomId = message.roomId;
    room.roomName = "Registered Room";
    room.hostPeerId = peerId;
    room.contentFingerprint = "fingerprint";
    room.playerCount = 1U;
    room.maximumPlayers = 2U;
    room.passwordProtected = passwordProtected;
    message.body = RuntimeInternetRegisterRoomBody{
        room,
        {
            RuntimeInternetCandidate{
                RuntimeInternetCandidateKind::Host,
                RuntimeInternetEndpoint{"127.0.0.1", 19002U},
                100U
            }
        }
    };
    RuntimeNetworkPacket packet;
    (void)encodeRuntimeInternetControlMessage(message, packet, errorMessage);
    return packet;
}

RuntimeInternetServiceTcpOpenRequest relayRequest(
    const std::string& peerId,
    std::uint64_t connectionId,
    const SimulatedService& service,
    std::string& errorMessage
) {
    RuntimeInternetServiceTcpOpenRequest request;
    request.endpoint = {"127.0.0.1", service.port()};
    request.route = {
        RuntimeInternetTcpRouteKind::Relay,
        connectionId,
        "smoke-service",
        "room-one",
        peerId,
        RuntimeInternetRole::Client,
        "host-peer",
        RuntimeInternetRole::Host,
        request.endpoint
    };
    RuntimeInternetControlMessage bind;
    bind.type = RuntimeInternetControlType::RelayBind;
    bind.transactionId = connectionId;
    bind.serviceId = "smoke-service";
    bind.roomId = "room-one";
    bind.senderPeerId = peerId;
    bind.targetPeerId = "smoke-service";
    bind.body = RuntimeInternetRelayBindBody{
        connectionId,
        RuntimeInternetRelayTransport::Tcp,
        std::vector<std::uint8_t>(32U, 0x79U)
    };
    (void)encodeRuntimeInternetControlMessage(
        bind,
        request.relayBindPacket,
        errorMessage
    );
    return request;
}

bool waitForCoordinator(
    SimulatedService& service,
    RuntimeInternetServiceTransport& transport,
    std::vector<RuntimeInternetServiceEvent>& observed,
    std::string& errorMessage
) {
    const std::uint64_t deadline = nowMilliseconds() + 10000U;
    while (nowMilliseconds() < deadline) {
        service.poll();
        std::vector<RuntimeInternetServiceEvent> events = transport.poll(0);
        for (RuntimeInternetServiceEvent& event : events) {
            if (event.kind == RuntimeInternetServiceEventKind::Failure) {
                errorMessage = event.reason;
                return false;
            }
            observed.push_back(std::move(event));
        }
        if (transport.coordinatorAuthenticated()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    errorMessage = "coordinator authentication timed out in smoke";
    return false;
}

bool waitForCoordinatorPacket(
    SimulatedService& service,
    RuntimeInternetServiceTransport& transport,
    const RuntimeNetworkPacket& expected,
    std::string& errorMessage
) {
    const std::uint64_t deadline = nowMilliseconds() + 10000U;
    while (nowMilliseconds() < deadline) {
        service.poll();
        for (RuntimeInternetServiceEvent& event : transport.poll(0)) {
            if (event.kind == RuntimeInternetServiceEventKind::Failure) {
                errorMessage = event.reason;
                return false;
            }
            if (event.kind == RuntimeInternetServiceEventKind::CoordinatorPacket
                && event.packet.type == expected.type
                && event.packet.payload == expected.payload) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    errorMessage = "coordinator packet round trip timed out";
    return false;
}

std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> waitForRelay(
    SimulatedService& service,
    RuntimeInternetServiceTransport& transport,
    const RuntimeInternetServiceTcpOpenRequest& request,
    std::string& errorMessage
) {
    bool relayBoundObserved = false;
    const std::uint64_t deadline = nowMilliseconds() + 10000U;
    while (nowMilliseconds() < deadline) {
        service.poll();
        for (RuntimeInternetServiceEvent& event : transport.poll(0)) {
            if (event.kind == RuntimeInternetServiceEventKind::Failure) {
                errorMessage = event.reason;
                return {};
            }
            if (event.kind == RuntimeInternetServiceEventKind::RelayPacket
                && event.connectionId == request.route.connectionId) {
                RuntimeInternetControlMessage message;
                std::string decodeError;
                if (!decodeRuntimeInternetControlMessage(
                        event.packet,
                        message,
                        decodeError
                    )) {
                    errorMessage = "confirmed relay binding did not decode";
                    return {};
                }
                const auto* body = std::get_if<RuntimeInternetRelayBoundBody>(
                    &message.body
                );
                if (event.authenticatedServiceId != request.route.serviceId
                    || event.authenticatedPeerId
                        != request.route.localPeerId
                    || event.source != request.endpoint
                    || message.type != RuntimeInternetControlType::RelayBound
                    || message.transactionId
                        != request.route.connectionId
                    || message.serviceId != request.route.serviceId
                    || message.roomId != request.route.roomId
                    || message.senderPeerId != request.route.serviceId
                    || message.targetPeerId != request.route.localPeerId
                    || body == nullptr
                    || body->connectionId != request.route.connectionId
                    || body->transport
                        != RuntimeInternetRelayTransport::Tcp) {
                    errorMessage =
                        "confirmed relay binding did not match its authenticated route";
                    return {};
                }
                relayBoundObserved = true;
                continue;
            }
            if (event.kind == RuntimeInternetServiceEventKind::TcpStreamOpened
                && event.connectionId == request.route.connectionId) {
                if (!relayBoundObserved) {
                    if (event.tcpStream) event.tcpStream->close();
                    errorMessage =
                        "authenticated relay stream preceded its RelayBound event";
                    return {};
                }
                return std::move(event.tcpStream);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    errorMessage = "relay application authentication timed out in smoke";
    return {};
}

bool waitForFailure(
    SimulatedService& service,
    RuntimeInternetServiceTransport& transport,
    std::string& reason
) {
    const std::uint64_t deadline = nowMilliseconds() + 10000U;
    while (nowMilliseconds() < deadline) {
        service.poll();
        for (RuntimeInternetServiceEvent& event : transport.poll(0)) {
            if (event.kind == RuntimeInternetServiceEventKind::TcpStreamOpened
                || event.kind == RuntimeInternetServiceEventKind::RelayPacket) {
                reason =
                    "unauthorized relay binding or stream was exposed";
                return false;
            }
            if (event.kind == RuntimeInternetServiceEventKind::Failure) {
                reason = event.reason;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    reason = "expected Internet service failure timed out";
    return false;
}

bool transferEcho(
    SimulatedService& service,
    RuntimeInternetServiceTransport& transport,
    RuntimeInternetAuthenticatedTcpStream& stream,
    const std::vector<std::uint8_t>& payload,
    bool& sawBackpressure,
    std::string& errorMessage
) {
    std::size_t sent = 0;
    std::vector<std::uint8_t> received;
    received.reserve(payload.size());
    std::array<std::uint8_t, 64U * 1024U> buffer{};
    const std::uint64_t deadline = nowMilliseconds() + 20000U;
    while (received.size() < payload.size() && nowMilliseconds() < deadline) {
        service.poll();
        (void)transport.poll(0);
        bool progressed = false;
        if (sent < payload.size()) {
            const RuntimeInternetTcpStreamIoResult write = stream.write(
                std::span<const std::uint8_t>(payload).subspan(sent)
            );
            if (write.status == RuntimeInternetTcpStreamIoStatus::Failed
                || write.status == RuntimeInternetTcpStreamIoStatus::Closed) {
                errorMessage = write.error.empty()
                    ? "relay closed while sending large payload"
                    : write.error;
                return false;
            }
            sawBackpressure = sawBackpressure
                || write.status == RuntimeInternetTcpStreamIoStatus::WouldBlock;
            sent += write.bytesTransferred;
            progressed = progressed || write.bytesTransferred > 0;
        }
        const RuntimeInternetTcpStreamIoResult read = stream.read(buffer);
        if (read.status == RuntimeInternetTcpStreamIoStatus::Failed
            || read.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            errorMessage = read.error.empty()
                ? "relay closed while receiving large payload"
                : read.error;
            return false;
        }
        if (read.bytesTransferred > 0) {
            received.insert(
                received.end(),
                buffer.begin(),
                buffer.begin()
                    + static_cast<std::ptrdiff_t>(read.bytesTransferred)
            );
            progressed = true;
        }
        if (!progressed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (sent != payload.size() || received != payload) {
        errorMessage = "relay large payload was truncated or changed";
        return false;
    }
    return true;
}

bool sendPlaintextAuthentication(
    SimulatedService& service,
    std::string& errorMessage
) {
    RuntimeInternetServiceAuthenticationRequest request;
    request.channel = RuntimeInternetServiceWireChannel::Coordinator;
    request.nonce.fill(0x31U);
    request.serviceId = "smoke-service";
    request.peerId = "plaintext-peer";
    request.credential.assign(32U, 0x6dU);
    RuntimeInternetServiceWireMessage message;
    message.body = std::move(request);
    std::vector<std::uint8_t> frame;
    if (!encodeRuntimeInternetServiceWireFrame(message, frame, errorMessage)) {
        return false;
    }
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        errorMessage = "could not create plaintext service probe";
        return false;
    }
    sockaddr_in endpoint{};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    endpoint.sin_port = htons(service.port());
    if (connect(
            socket,
            reinterpret_cast<const sockaddr*>(&endpoint),
            static_cast<int>(sizeof(endpoint))
        ) != 0) {
        closesocket(socket);
        errorMessage = "could not connect plaintext service probe";
        return false;
    }
    const int sent = send(
        socket,
        reinterpret_cast<const char*>(frame.data()),
        static_cast<int>(frame.size()),
        0
    );
    (void)::shutdown(socket, SD_BOTH);
    closesocket(socket);
    if (sent != static_cast<int>(frame.size())) {
        errorMessage = "could not send plaintext service probe";
        return false;
    }
    const std::size_t rejectedBefore = service.rejectedTlsConnections();
    const std::uint64_t deadline = nowMilliseconds() + 5000U;
    while (nowMilliseconds() < deadline) {
        service.poll();
        if (service.rejectedTlsConnections() > rejectedBefore) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    errorMessage = "plaintext service authentication was not rejected";
    return false;
}

} // namespace
#endif

int main() {
#ifndef _WIN32
    std::cout << "Runtime Internet service transport smoke skipped: Windows only\n";
    return 0;
#else
    SmokeContext smoke;
    std::string error;
    TestCertificate certificate;
    smoke.expect(certificate.create(error), error);
    if (certificate.certificate() == nullptr) return 1;
    SimulatedService service;
    smoke.expect(service.open(certificate.certificate(), error), error);
    if (service.port() == 0) return 1;

    RuntimeInternetServiceNonce replayNonce{};
    replayNonce.fill(0x44U);
    RuntimeInternetServiceNonceReplayWindow replay(2U, 100U);
    smoke.expect(replay.accept(replayNonce, 1000U, error), error);
    smoke.expect(
        !replay.accept(replayNonce, 1001U, error),
        "replayed application-authentication nonce was accepted"
    );
    smoke.expect(
        replay.accept(replayNonce, 1200U, error),
        "expired nonce tombstone was not pruned"
    );

    RuntimeInternetServiceWireDecoder oversizedDecoder;
    const std::uint32_t oversized = static_cast<std::uint32_t>(
        runtimeInternetServiceMaximumWireFrameBytes + 1U
    );
    const std::array<std::uint8_t, 4> oversizedHeader{
        static_cast<std::uint8_t>((oversized >> 24U) & 0xffU),
        static_cast<std::uint8_t>((oversized >> 16U) & 0xffU),
        static_cast<std::uint8_t>((oversized >> 8U) & 0xffU),
        static_cast<std::uint8_t>(oversized & 0xffU)
    };
    smoke.expect(oversizedDecoder.push(oversizedHeader, error), error);
    RuntimeInternetServiceWireMessage ignoredMessage;
    bool available = false;
    smoke.expect(
        !oversizedDecoder.pop(ignoredMessage, available, error),
        "oversized Internet service frame was accepted"
    );

    std::vector<std::uint8_t> credential(32U, 0x6dU);
    RuntimeInternetServiceTransport transport;
    smoke.expect(
        transport.open(
            transportConfig(
                "good-peer",
                credential,
                service,
                certificate.chainEngine()
            ),
            error
        ),
        error
    );
    RuntimeNetworkPacket firstPacket = roomListPacket("good-peer", 1U, error);
    smoke.expect(error.empty(), error);
    smoke.expect(
        transport.sendCoordinator(firstPacket, error),
        "coordinator pre-auth queue rejected a valid packet: " + error
    );
    std::vector<RuntimeInternetServiceEvent> earlyEvents;
    smoke.expect(
        waitForCoordinator(service, transport, earlyEvents, error),
        error
    );
    bool firstEchoed = std::any_of(
        earlyEvents.begin(),
        earlyEvents.end(),
        [&](const RuntimeInternetServiceEvent& event) {
            return event.kind
                    == RuntimeInternetServiceEventKind::CoordinatorPacket
                && event.packet.type == firstPacket.type
                && event.packet.payload == firstPacket.payload;
        }
    );
    if (!firstEchoed) {
        smoke.expect(
            waitForCoordinatorPacket(service, transport, firstPacket, error),
            error
        );
    }

    RuntimeNetworkPacket join = joinPacket("good-peer", 2U, error);
    const std::vector<std::uint8_t> proof{0x41U, 0x42U, 0x43U};
    smoke.expect(
        transport.sendCoordinatorWithAdmissionProof(join, proof, error),
        error
    );
    smoke.expect(
        waitForCoordinatorPacket(service, transport, join, error),
        error
    );
    smoke.expect(
        service.lastAdmissionProof() == proof,
        "JoinRoom admission proof was not preserved out-of-band"
    );
    const std::vector<std::uint8_t> verifier(32U, 0x5aU);
    RuntimeNetworkPacket protectedRegistration = registerRoomPacket(
        "good-peer",
        3U,
        true,
        error
    );
    smoke.expect(
        transport.sendCoordinatorWithAdmissionProof(
            protectedRegistration,
            verifier,
            error
        ),
        "protected RegisterRoom verifier metadata was rejected: " + error
    );
    smoke.expect(
        waitForCoordinatorPacket(
            service,
            transport,
            protectedRegistration,
            error
        ),
        error
    );
    smoke.expect(
        service.lastAdmissionProof() == verifier,
        "RegisterRoom verifier was not preserved out-of-band"
    );
    RuntimeNetworkPacket publicRegistration = registerRoomPacket(
        "good-peer",
        4U,
        false,
        error
    );
    error.clear();
    smoke.expect(
        !transport.sendCoordinatorWithAdmissionProof(
            publicRegistration,
            verifier,
            error
        ) && error.find("protected RegisterRoom") != std::string::npos,
        "public RegisterRoom unexpectedly accepted password verifier metadata"
    );

    RuntimeInternetServiceTcpOpenRequest relay = relayRequest(
        "good-peer",
        77U,
        service,
        error
    );
    smoke.expect(error.empty(), error);
    smoke.expect(transport.openAuthenticatedTcpRelay(relay, error), error);
    bool exposedBeforeConfirmation = false;
    for (int iteration = 0; iteration < 3; ++iteration) {
        service.poll();
        for (RuntimeInternetServiceEvent& event : transport.poll(0)) {
            exposedBeforeConfirmation = exposedBeforeConfirmation
                || event.kind
                    == RuntimeInternetServiceEventKind::TcpStreamOpened;
        }
    }
    smoke.expect(
        !exposedBeforeConfirmation,
        "relay stream was exposed before application confirmation"
    );
    auto relayStream = waitForRelay(service, transport, relay, error);
    smoke.expect(relayStream != nullptr, error);
    smoke.expect(service.relayBindObserved(), "relay bind packet was not observed");
    if (relayStream) {
        std::vector<std::uint8_t> payload(2U * 1024U * 1024U + 29U);
        for (std::size_t index = 0; index < payload.size(); ++index) {
            payload[index] = static_cast<std::uint8_t>(
                (index * 193U + 11U) & 0xffU
            );
        }
        bool sawBackpressure = false;
        smoke.expect(
            transferEcho(
                service,
                transport,
                *relayStream,
                payload,
                sawBackpressure,
                error
            ),
            error
        );
        smoke.expect(
            sawBackpressure || service.maximumRawQueued() <= 64U * 1024U,
            "2 MiB relay transfer did not exercise bounded backpressure"
        );
        relayStream->close();
    }

    RuntimeInternetServiceTransport wrongConfirmation;
    smoke.expect(
        wrongConfirmation.open(
            transportConfig(
                "wrong-relay-confirm",
                credential,
                service,
                certificate.chainEngine()
            ),
            error
        ),
        error
    );
    std::vector<RuntimeInternetServiceEvent> noEvents;
    smoke.expect(
        waitForCoordinator(service, wrongConfirmation, noEvents, error),
        error
    );
    RuntimeInternetServiceTcpOpenRequest wrongRelay = relayRequest(
        "wrong-relay-confirm",
        78U,
        service,
        error
    );
    smoke.expect(
        wrongConfirmation.openAuthenticatedTcpRelay(wrongRelay, error),
        error
    );
    std::string failure;
    smoke.expect(
        waitForFailure(service, wrongConfirmation, failure),
        failure
    );
    smoke.expect(
        failure.find("route") != std::string::npos,
        "wrong relay confirmation did not report a generic route mismatch"
    );

    RuntimeInternetServiceTransport wrongCredential;
    std::vector<std::uint8_t> secretCredential(
        {'T','H','I','S','-','C','R','E','D','E','N','T','I','A','L','-',
         'M','U','S','T','-','N','O','T','-','L','E','A','K','!','!','!' }
    );
    smoke.expect(
        wrongCredential.open(
            transportConfig(
                "bad-credential-peer",
                secretCredential,
                service,
                certificate.chainEngine()
            ),
            error
        ),
        error
    );
    failure.clear();
    smoke.expect(waitForFailure(service, wrongCredential, failure), failure);
    const std::string secretText(
        secretCredential.begin(),
        secretCredential.end()
    );
    smoke.expect(
        failure.find(secretText) == std::string::npos,
        "credential bytes were reflected in an authentication error"
    );

    RuntimeInternetServiceTransport oversizedTransport;
    smoke.expect(
        oversizedTransport.open(
            transportConfig(
                "oversize-peer",
                credential,
                service,
                certificate.chainEngine()
            ),
            error
        ),
        error
    );
    noEvents.clear();
    smoke.expect(
        waitForCoordinator(service, oversizedTransport, noEvents, error),
        error
    );
    failure.clear();
    smoke.expect(
        waitForFailure(service, oversizedTransport, failure),
        failure
    );

    smoke.expect(sendPlaintextAuthentication(service, error), error);

    transport.close();
    wrongConfirmation.close();
    wrongCredential.close();
    oversizedTransport.close();

    if (smoke.failures == 0) {
        std::cout
            << "Runtime Internet service transport smoke passed: real TLS "
               "coordinator/proof framing, confirmed relay authentication, "
               "mismatch rejection, replay/length guards, plaintext refusal, "
               "and 2 MiB backpressure are valid\n";
        return 0;
    }
    std::cerr << smoke.failures
              << " Internet service transport assertion(s) failed\n";
    return 1;
#endif
}
