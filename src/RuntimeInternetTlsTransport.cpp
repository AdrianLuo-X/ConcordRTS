#include "RuntimeInternetTlsTransport.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define SECURITY_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <schannel.h>
#include <security.h>
#include <wincrypt.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {

constexpr std::size_t tlsNetworkReadBytes = 64U * 1024U;
constexpr std::size_t tlsMaximumHandshakeBytes = 1024U * 1024U;
constexpr ULONG clientContextRequirements =
    ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT
    | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR
    | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM
    | ISC_REQ_MANUAL_CRED_VALIDATION;
constexpr ULONG serverContextRequirements =
    ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT
    | ASC_REQ_CONFIDENTIALITY | ASC_REQ_EXTENDED_ERROR
    | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM;

class SocketSystem {
public:
    SocketSystem() {
        WSADATA data{};
        initialized_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~SocketSystem() {
        if (initialized_) WSACleanup();
    }
    bool initialized() const noexcept { return initialized_; }

private:
    bool initialized_ = false;
};

struct SecurityCredential {
    CredHandle handle{};
    bool valid = false;
    PCCERT_CONTEXT certificate = nullptr;

    ~SecurityCredential() {
        if (valid) FreeCredentialsHandle(&handle);
        if (certificate != nullptr) {
            CertFreeCertificateContext(certificate);
        }
    }
};

void closeSocket(SOCKET& socket) noexcept {
    if (socket == INVALID_SOCKET) return;
    closesocket(socket);
    socket = INVALID_SOCKET;
}

bool socketWouldBlock(const int error) noexcept {
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS
        || error == WSAEALREADY;
}

std::string securityError(const char* operation, SECURITY_STATUS status) {
    std::ostringstream stream;
    stream << operation << " failed (0x" << std::hex
           << static_cast<unsigned long>(status) << ')';
    return stream.str();
}

std::string socketError(const char* operation, int code = WSAGetLastError()) {
    return std::string(operation) + " failed (Winsock "
        + std::to_string(code) + ')';
}

bool toWide(
    const std::string& value,
    std::wstring& converted,
    std::string& errorMessage
) {
    converted.clear();
    if (value.empty()) {
        errorMessage = "TLS server name is empty";
        return false;
    }
    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );
    if (length <= 0) {
        errorMessage = "TLS server name is not valid UTF-8";
        return false;
    }
    converted.resize(static_cast<std::size_t>(length));
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            converted.data(),
            length
        ) != length) {
        converted.clear();
        errorMessage = "could not convert TLS server name";
        return false;
    }
    return true;
}

bool validLimits(
    const RuntimeInternetTlsLimits& limits,
    std::string& errorMessage
) {
    if (limits.connectTimeoutMilliseconds == 0
        || limits.handshakeTimeoutMilliseconds == 0) {
        errorMessage = "TLS timeouts must be non-zero";
        return false;
    }
    return true;
}

bool validIdentity(
    const RuntimeInternetAuthenticatedTcpStreamIdentity& identity,
    std::string& errorMessage
) {
    if (identity.authenticatedServiceId.empty()
        || identity.authenticatedPeerId.empty()) {
        errorMessage = "authenticated TLS stream identity is incomplete";
        return false;
    }
    return true;
}

bool setNonBlocking(SOCKET socket, std::string& errorMessage) {
    u_long enabled = 1;
    if (ioctlsocket(socket, FIONBIO, &enabled) != 0) {
        errorMessage = socketError("making TLS socket nonblocking");
        return false;
    }
    return true;
}

void setTcpNoDelay(SOCKET socket) noexcept {
    const int enabled = 1;
    (void)setsockopt(
        socket,
        IPPROTO_TCP,
        TCP_NODELAY,
        reinterpret_cast<const char*>(&enabled),
        static_cast<int>(sizeof(enabled))
    );
}

DWORD clientProtocols() noexcept {
    DWORD protocols = SP_PROT_TLS1_2_CLIENT;
#ifdef SP_PROT_TLS1_3_CLIENT
    protocols |= SP_PROT_TLS1_3_CLIENT;
#endif
    return protocols;
}

DWORD serverProtocols() noexcept {
    DWORD protocols = SP_PROT_TLS1_2_SERVER;
#ifdef SP_PROT_TLS1_3_SERVER
    protocols |= SP_PROT_TLS1_3_SERVER;
#endif
    return protocols;
}

std::shared_ptr<SecurityCredential> acquireClientCredential(
    std::string& errorMessage
) {
    auto credential = std::make_shared<SecurityCredential>();
#pragma warning(push)
#pragma warning(disable: 4996)
    SCHANNEL_CRED schannel{};
    schannel.dwVersion = SCHANNEL_CRED_VERSION;
    schannel.grbitEnabledProtocols = clientProtocols();
    schannel.dwFlags = SCH_CRED_MANUAL_CRED_VALIDATION
        | SCH_CRED_NO_DEFAULT_CREDS | SCH_USE_STRONG_CRYPTO;
    TimeStamp expiry{};
    const SECURITY_STATUS status = AcquireCredentialsHandleW(
        nullptr,
        const_cast<wchar_t*>(UNISP_NAME_W),
        SECPKG_CRED_OUTBOUND,
        nullptr,
        &schannel,
        nullptr,
        nullptr,
        &credential->handle,
        &expiry
    );
#pragma warning(pop)
    if (status != SEC_E_OK) {
        errorMessage = securityError(
            "acquiring outbound Schannel credentials",
            status
        );
        return {};
    }
    credential->valid = true;
    return credential;
}

std::shared_ptr<SecurityCredential> acquireServerCredential(
    const void* nativeCertificateContext,
    std::string& errorMessage
) {
    if (nativeCertificateContext == nullptr) {
        errorMessage = "TLS server certificate context is missing";
        return {};
    }
    auto credential = std::make_shared<SecurityCredential>();
    credential->certificate = CertDuplicateCertificateContext(
        static_cast<PCCERT_CONTEXT>(nativeCertificateContext)
    );
    if (credential->certificate == nullptr) {
        errorMessage = "could not duplicate TLS server certificate";
        return {};
    }
    PCCERT_CONTEXT certificates[] = {credential->certificate};
#pragma warning(push)
#pragma warning(disable: 4996)
    SCHANNEL_CRED schannel{};
    schannel.dwVersion = SCHANNEL_CRED_VERSION;
    schannel.cCreds = 1;
    schannel.paCred = certificates;
    schannel.grbitEnabledProtocols = serverProtocols();
    schannel.dwFlags = SCH_CRED_NO_SYSTEM_MAPPER | SCH_USE_STRONG_CRYPTO;
    TimeStamp expiry{};
    const SECURITY_STATUS status = AcquireCredentialsHandleW(
        nullptr,
        const_cast<wchar_t*>(UNISP_NAME_W),
        SECPKG_CRED_INBOUND,
        nullptr,
        &schannel,
        nullptr,
        nullptr,
        &credential->handle,
        &expiry
    );
#pragma warning(pop)
    if (status != SEC_E_OK) {
        errorMessage = securityError(
            "acquiring inbound Schannel credentials",
            status
        );
        return {};
    }
    credential->valid = true;
    return credential;
}

enum class SocketIoKind : std::uint8_t {
    Progress,
    WouldBlock,
    Closed,
    Failed
};

struct SocketIoResult {
    SocketIoKind kind = SocketIoKind::WouldBlock;
    std::size_t bytes = 0;
    std::string error;
};

SocketIoResult flushSocketBytes(
    SOCKET socket,
    std::vector<std::uint8_t>& bytes,
    std::size_t& offset
) {
    if (offset >= bytes.size()) {
        bytes.clear();
        offset = 0;
        return {SocketIoKind::Progress, 0, {}};
    }
    const std::size_t remaining = bytes.size() - offset;
    const int requested = static_cast<int>(std::min<std::size_t>(
        remaining,
        static_cast<std::size_t>((std::numeric_limits<int>::max)())
    ));
    const int sent = ::send(
        socket,
        reinterpret_cast<const char*>(bytes.data() + offset),
        requested,
        0
    );
    if (sent > 0) {
        offset += static_cast<std::size_t>(sent);
        if (offset == bytes.size()) {
            bytes.clear();
            offset = 0;
        }
        return {
            SocketIoKind::Progress,
            static_cast<std::size_t>(sent),
            {}
        };
    }
    if (sent == 0) return {SocketIoKind::Closed, 0, {}};
    const int error = WSAGetLastError();
    if (socketWouldBlock(error)) {
        return {SocketIoKind::WouldBlock, 0, {}};
    }
    return {SocketIoKind::Failed, 0, socketError("TLS socket send", error)};
}

SocketIoResult receiveSocketBytes(
    SOCKET socket,
    std::vector<std::uint8_t>& bytes,
    std::size_t maximumBytes
) {
    if (bytes.size() >= maximumBytes) {
        return {
            SocketIoKind::Failed,
            0,
            "TLS input exceeded its bounded buffer"
        };
    }
    std::array<std::uint8_t, tlsNetworkReadBytes> buffer{};
    const int requested = static_cast<int>(std::min<std::size_t>(
        buffer.size(),
        maximumBytes - bytes.size()
    ));
    const int received = ::recv(
        socket,
        reinterpret_cast<char*>(buffer.data()),
        requested,
        0
    );
    if (received > 0) {
        bytes.insert(
            bytes.end(),
            buffer.begin(),
            buffer.begin() + received
        );
        return {
            SocketIoKind::Progress,
            static_cast<std::size_t>(received),
            {}
        };
    }
    if (received == 0) return {SocketIoKind::Closed, 0, {}};
    const int error = WSAGetLastError();
    if (socketWouldBlock(error)) {
        return {SocketIoKind::WouldBlock, 0, {}};
    }
    return {SocketIoKind::Failed, 0, socketError("TLS socket receive", error)};
}

void appendSecurityToken(
    const SecBuffer& token,
    std::vector<std::uint8_t>& output
) {
    if (token.pvBuffer == nullptr || token.cbBuffer == 0) return;
    const auto* begin = static_cast<const std::uint8_t*>(token.pvBuffer);
    output.insert(output.end(), begin, begin + token.cbBuffer);
}

void retainExtraInput(
    std::vector<std::uint8_t>& input,
    const SecBuffer& extra
) {
    if (extra.BufferType == SECBUFFER_EXTRA
        && extra.cbBuffer > 0
        && extra.cbBuffer <= input.size()) {
        std::vector<std::uint8_t> remainder(
            input.end() - static_cast<std::ptrdiff_t>(extra.cbBuffer),
            input.end()
        );
        input = std::move(remainder);
    } else {
        input.clear();
    }
}

bool negotiatedTls12OrNewer(
    CtxtHandle& context,
    bool client,
    std::string& errorMessage
) {
    SecPkgContext_ConnectionInfo info{};
    const SECURITY_STATUS status = QueryContextAttributesW(
        &context,
        SECPKG_ATTR_CONNECTION_INFO,
        &info
    );
    if (status != SEC_E_OK) {
        errorMessage = securityError("querying TLS connection", status);
        return false;
    }
    DWORD allowed = client ? SP_PROT_TLS1_2_CLIENT : SP_PROT_TLS1_2_SERVER;
#ifdef SP_PROT_TLS1_3_CLIENT
    allowed |= client ? SP_PROT_TLS1_3_CLIENT : 0;
#endif
#ifdef SP_PROT_TLS1_3_SERVER
    allowed |= client ? 0 : SP_PROT_TLS1_3_SERVER;
#endif
    if ((info.dwProtocol & allowed) == 0) {
        errorMessage = "TLS peer negotiated a protocol older than TLS 1.2";
        return false;
    }
    return true;
}

bool constantTimeEqual(
    const RuntimeInternetTlsSha256& left,
    const RuntimeInternetTlsSha256& right
) noexcept {
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        difference = static_cast<std::uint8_t>(
            difference | static_cast<std::uint8_t>(left[index] ^ right[index])
        );
    }
    return difference == 0;
}

bool validateServerCertificate(
    CtxtHandle& context,
    const std::wstring& serverName,
    bool checkRevocation,
    const std::optional<RuntimeInternetTlsSha256>& expectedPin,
    HCERTCHAINENGINE chainEngine,
    std::string& errorMessage
) {
    PCCERT_CONTEXT certificate = nullptr;
    SECURITY_STATUS status = QueryContextAttributesW(
        &context,
        SECPKG_ATTR_REMOTE_CERT_CONTEXT,
        &certificate
    );
    if (status != SEC_E_OK || certificate == nullptr) {
        errorMessage = securityError(
            "reading the TLS server certificate",
            status
        );
        return false;
    }

    LPSTR usages[] = {const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH)};
    CERT_CHAIN_PARA chainParameters{};
    chainParameters.cbSize = sizeof(chainParameters);
    chainParameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    chainParameters.RequestedUsage.Usage.cUsageIdentifier = 1;
    chainParameters.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    DWORD chainFlags = 0;
    if (checkRevocation) {
        chainFlags = CERT_CHAIN_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    }
    const BOOL chainBuilt = CertGetCertificateChain(
        chainEngine,
        certificate,
        nullptr,
        certificate->hCertStore,
        &chainParameters,
        chainFlags,
        nullptr,
        &chain
    );
    bool valid = chainBuilt != FALSE && chain != nullptr;
    if (!valid) {
        errorMessage = "Windows could not build the TLS server certificate chain";
    }

    if (valid && chain->TrustStatus.dwErrorStatus != CERT_TRUST_NO_ERROR) {
        valid = false;
        errorMessage = "TLS server certificate chain is not trusted";
    }

    if (valid) {
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
        ssl.cbSize = sizeof(ssl);
        ssl.dwAuthType = AUTHTYPE_SERVER;
        ssl.pwszServerName = const_cast<wchar_t*>(serverName.c_str());
        CERT_CHAIN_POLICY_PARA policy{};
        policy.cbSize = sizeof(policy);
        policy.pvExtraPolicyPara = &ssl;
        CERT_CHAIN_POLICY_STATUS policyStatus{};
        policyStatus.cbSize = sizeof(policyStatus);
        valid = CertVerifyCertificateChainPolicy(
            CERT_CHAIN_POLICY_SSL,
            chain,
            &policy,
            &policyStatus
        ) != FALSE && policyStatus.dwError == 0;
        if (!valid) {
            errorMessage = "TLS server certificate chain or hostname is invalid";
        }
    }

    if (valid && expectedPin.has_value()) {
        RuntimeInternetTlsSha256 observed{};
        DWORD observedBytes = static_cast<DWORD>(observed.size());
        valid = CryptHashCertificate2(
            BCRYPT_SHA256_ALGORITHM,
            0,
            nullptr,
            certificate->pbCertEncoded,
            certificate->cbCertEncoded,
            observed.data(),
            &observedBytes
        ) != FALSE && observedBytes == observed.size();
        if (!valid) {
            errorMessage = "could not hash the TLS server certificate";
        } else if (!constantTimeEqual(observed, *expectedPin)) {
            valid = false;
            errorMessage = "TLS server certificate pin does not match";
        }
    }

    if (chain != nullptr) CertFreeCertificateChain(chain);
    CertFreeCertificateContext(certificate);
    return valid;
}

struct PendingTlsConnection {
    SOCKET socket = INVALID_SOCKET;
    std::shared_ptr<SocketSystem> socketSystem;
    std::shared_ptr<SecurityCredential> credential;
    CtxtHandle context{};
    bool contextValid = false;
    bool serverSide = false;
    bool firstHandshakeCall = true;
    bool securityHandshakeComplete = false;
    RuntimeInternetTlsState state = RuntimeInternetTlsState::Handshaking;
    std::uint64_t handshakeDeadline = 0;
    std::vector<std::uint8_t> encryptedInput;
    std::vector<std::uint8_t> networkOutput;
    std::size_t networkOutputOffset = 0;
    SecPkgContext_StreamSizes streamSizes{};
    std::wstring serverName;
    bool checkRevocation = true;
    std::optional<RuntimeInternetTlsSha256> expectedPin;
    HCERTCHAINENGINE chainEngine = nullptr;
    std::string peerAddress;
    std::string error;

    PendingTlsConnection() = default;
    PendingTlsConnection(PendingTlsConnection&& other) noexcept {
        *this = std::move(other);
    }
    PendingTlsConnection& operator=(PendingTlsConnection&& other) noexcept {
        if (this == &other) return *this;
        reset();
        socket = std::exchange(other.socket, INVALID_SOCKET);
        socketSystem = std::move(other.socketSystem);
        credential = std::move(other.credential);
        context = other.context;
        contextValid = std::exchange(other.contextValid, false);
        serverSide = other.serverSide;
        firstHandshakeCall = other.firstHandshakeCall;
        securityHandshakeComplete = other.securityHandshakeComplete;
        state = other.state;
        handshakeDeadline = other.handshakeDeadline;
        encryptedInput = std::move(other.encryptedInput);
        networkOutput = std::move(other.networkOutput);
        networkOutputOffset = other.networkOutputOffset;
        other.networkOutputOffset = 0;
        streamSizes = other.streamSizes;
        serverName = std::move(other.serverName);
        checkRevocation = other.checkRevocation;
        expectedPin = other.expectedPin;
        chainEngine = other.chainEngine;
        peerAddress = std::move(other.peerAddress);
        error = std::move(other.error);
        return *this;
    }
    PendingTlsConnection(const PendingTlsConnection&) = delete;
    PendingTlsConnection& operator=(const PendingTlsConnection&) = delete;
    ~PendingTlsConnection() { reset(); }

    void reset() noexcept {
        if (contextValid) {
            DeleteSecurityContext(&context);
            contextValid = false;
        }
        closeSocket(socket);
    }
};

bool completeHandshake(
    PendingTlsConnection& connection,
    std::string& errorMessage
) {
    if (!negotiatedTls12OrNewer(
            connection.context,
            !connection.serverSide,
            errorMessage
        )) {
        return false;
    }
    if (!connection.serverSide
        && !validateServerCertificate(
            connection.context,
            connection.serverName,
            connection.checkRevocation,
            connection.expectedPin,
            connection.chainEngine,
            errorMessage
        )) {
        return false;
    }
    const SECURITY_STATUS status = QueryContextAttributesW(
        &connection.context,
        SECPKG_ATTR_STREAM_SIZES,
        &connection.streamSizes
    );
    if (status != SEC_E_OK
        || connection.streamSizes.cbMaximumMessage == 0) {
        errorMessage = securityError("querying TLS stream sizes", status);
        return false;
    }
    connection.securityHandshakeComplete = true;
    return true;
}

bool produceHandshakeToken(
    PendingTlsConnection& connection,
    SECURITY_STATUS& status,
    std::string& errorMessage
) {
    SecBuffer outputBuffer{};
    outputBuffer.BufferType = SECBUFFER_TOKEN;
    SecBufferDesc outputDescriptor{};
    outputDescriptor.ulVersion = SECBUFFER_VERSION;
    outputDescriptor.cBuffers = 1;
    outputDescriptor.pBuffers = &outputBuffer;
    SecBuffer inputBuffers[2]{};
    SecBufferDesc inputDescriptor{};
    SecBufferDesc* input = nullptr;
    if (!connection.firstHandshakeCall || connection.serverSide) {
        if (connection.encryptedInput.empty()) {
            status = SEC_E_INCOMPLETE_MESSAGE;
            return true;
        }
        inputBuffers[0].BufferType = SECBUFFER_TOKEN;
        inputBuffers[0].cbBuffer = static_cast<unsigned long>(
            connection.encryptedInput.size()
        );
        inputBuffers[0].pvBuffer = connection.encryptedInput.data();
        inputBuffers[1].BufferType = SECBUFFER_EMPTY;
        inputDescriptor.ulVersion = SECBUFFER_VERSION;
        inputDescriptor.cBuffers = 2;
        inputDescriptor.pBuffers = inputBuffers;
        input = &inputDescriptor;
    }
    ULONG attributes = 0;
    TimeStamp expiry{};
    if (connection.serverSide) {
        status = AcceptSecurityContext(
            &connection.credential->handle,
            connection.contextValid ? &connection.context : nullptr,
            input,
            serverContextRequirements,
            SECURITY_NATIVE_DREP,
            &connection.context,
            &outputDescriptor,
            &attributes,
            &expiry
        );
    } else {
        status = InitializeSecurityContextW(
            &connection.credential->handle,
            connection.contextValid ? &connection.context : nullptr,
            const_cast<wchar_t*>(connection.serverName.c_str()),
            clientContextRequirements,
            0,
            SECURITY_NATIVE_DREP,
            input,
            0,
            &connection.context,
            &outputDescriptor,
            &attributes,
            &expiry
        );
    }
    if (status == SEC_I_COMPLETE_NEEDED
        || status == SEC_I_COMPLETE_AND_CONTINUE) {
        const SECURITY_STATUS completed = CompleteAuthToken(
            &connection.context,
            &outputDescriptor
        );
        if (completed != SEC_E_OK) {
            if (outputBuffer.pvBuffer != nullptr) {
                FreeContextBuffer(outputBuffer.pvBuffer);
            }
            errorMessage = securityError("completing TLS authentication", completed);
            return false;
        }
    }
    if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED
        || status == SEC_I_COMPLETE_NEEDED
        || status == SEC_I_COMPLETE_AND_CONTINUE) {
        connection.contextValid = true;
        connection.firstHandshakeCall = false;
        appendSecurityToken(outputBuffer, connection.networkOutput);
        if (input != nullptr) {
            retainExtraInput(connection.encryptedInput, inputBuffers[1]);
        }
    }
    if (outputBuffer.pvBuffer != nullptr) {
        FreeContextBuffer(outputBuffer.pvBuffer);
    }
    return true;
}

bool advanceHandshake(
    PendingTlsConnection& connection,
    std::uint64_t nowMilliseconds,
    bool& progressed
) {
    progressed = false;
    if (nowMilliseconds >= connection.handshakeDeadline) {
        connection.error = "TLS handshake timed out";
        connection.state = RuntimeInternetTlsState::Failed;
        return false;
    }
    for (int iteration = 0; iteration < 16; ++iteration) {
        if (!connection.networkOutput.empty()) {
            const SocketIoResult flushed = flushSocketBytes(
                connection.socket,
                connection.networkOutput,
                connection.networkOutputOffset
            );
            if (flushed.kind == SocketIoKind::Progress) {
                progressed = progressed || flushed.bytes > 0;
                if (!connection.networkOutput.empty()) return true;
            } else if (flushed.kind == SocketIoKind::WouldBlock) {
                return true;
            } else {
                connection.error = flushed.kind == SocketIoKind::Closed
                    ? "TLS peer closed during handshake"
                    : flushed.error;
                connection.state = RuntimeInternetTlsState::Failed;
                return false;
            }
        }
        if (connection.securityHandshakeComplete) {
            connection.state = RuntimeInternetTlsState::Open;
            progressed = true;
            return true;
        }

        const bool needsInput = connection.serverSide
            || !connection.firstHandshakeCall;
        if (needsInput && connection.encryptedInput.empty()) {
            const SocketIoResult received = receiveSocketBytes(
                connection.socket,
                connection.encryptedInput,
                tlsMaximumHandshakeBytes
            );
            if (received.kind == SocketIoKind::Progress) {
                progressed = true;
            } else if (received.kind == SocketIoKind::WouldBlock) {
                return true;
            } else {
                connection.error = received.kind == SocketIoKind::Closed
                    ? "TLS peer closed during handshake"
                    : received.error;
                connection.state = RuntimeInternetTlsState::Failed;
                return false;
            }
        }

        SECURITY_STATUS status = SEC_E_INTERNAL_ERROR;
        std::string operationError;
        if (!produceHandshakeToken(connection, status, operationError)) {
            connection.error = std::move(operationError);
            connection.state = RuntimeInternetTlsState::Failed;
            return false;
        }
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            const SocketIoResult received = receiveSocketBytes(
                connection.socket,
                connection.encryptedInput,
                tlsMaximumHandshakeBytes
            );
            if (received.kind == SocketIoKind::Progress) {
                progressed = true;
                continue;
            }
            if (received.kind == SocketIoKind::WouldBlock) return true;
            connection.error = received.kind == SocketIoKind::Closed
                ? "TLS peer closed with an incomplete handshake"
                : received.error;
            connection.state = RuntimeInternetTlsState::Failed;
            return false;
        }
        if (status == SEC_E_OK || status == SEC_I_COMPLETE_NEEDED) {
            if (!completeHandshake(connection, connection.error)) {
                connection.state = RuntimeInternetTlsState::Failed;
                return false;
            }
            progressed = true;
            continue;
        }
        if (status == SEC_I_CONTINUE_NEEDED
            || status == SEC_I_COMPLETE_AND_CONTINUE) {
            progressed = true;
            continue;
        }
        connection.error = securityError("TLS handshake", status);
        connection.state = RuntimeInternetTlsState::Failed;
        return false;
    }
    return true;
}

class SchannelAuthenticatedStream final
    : public RuntimeInternetAuthenticatedTcpStream {
public:
    SchannelAuthenticatedStream(
        PendingTlsConnection connection,
        RuntimeInternetAuthenticatedTcpStreamIdentity identity
    )
        : connection_(std::move(connection)),
          identity_(std::move(identity)) {}

    ~SchannelAuthenticatedStream() override { close(); }

    const RuntimeInternetAuthenticatedTcpStreamIdentity& identity()
        const override {
        return identity_;
    }

    RuntimeInternetTcpStreamIoResult read(
        std::span<std::uint8_t> destination
    ) override {
        if (connection_.state == RuntimeInternetTlsState::Failed) {
            return {
                RuntimeInternetTcpStreamIoStatus::Failed,
                0,
                connection_.error
            };
        }
        if (connection_.state == RuntimeInternetTlsState::Closed) {
            return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
        }
        if (connection_.state != RuntimeInternetTlsState::Open) {
            return {
                RuntimeInternetTcpStreamIoStatus::Failed,
                0,
                "TLS stream is not open"
            };
        }
        if (destination.empty()) {
            return {RuntimeInternetTcpStreamIoStatus::Progress, 0, {}};
        }
        if (plainOffset_ < plaintext_.size()) {
            return copyPlaintext(destination);
        }
        if (!connection_.networkOutput.empty()) {
            const SocketIoResult flushed = flushSocketBytes(
                connection_.socket,
                connection_.networkOutput,
                connection_.networkOutputOffset
            );
            if (flushed.kind == SocketIoKind::Failed
                || flushed.kind == SocketIoKind::Closed) {
                return fail(
                    flushed.kind == SocketIoKind::Closed
                        ? "TLS peer closed while output was pending"
                        : flushed.error
                );
            }
        }

        for (int iteration = 0; iteration < 16; ++iteration) {
            if (connection_.encryptedInput.empty()) {
                const SocketIoResult received = receiveSocketBytes(
                    connection_.socket,
                    connection_.encryptedInput,
                    tlsMaximumHandshakeBytes
                );
                if (received.kind == SocketIoKind::WouldBlock) {
                    return {
                        RuntimeInternetTcpStreamIoStatus::WouldBlock,
                        0,
                        {}
                    };
                }
                if (received.kind == SocketIoKind::Closed) {
                    return fail(
                        "TLS socket closed without an authenticated close_notify"
                    );
                }
                if (received.kind == SocketIoKind::Failed) {
                    return fail(received.error);
                }
            }

            SecBuffer buffers[4]{};
            buffers[0].BufferType = SECBUFFER_DATA;
            buffers[0].cbBuffer = static_cast<unsigned long>(
                connection_.encryptedInput.size()
            );
            buffers[0].pvBuffer = connection_.encryptedInput.data();
            for (std::size_t index = 1; index < std::size(buffers); ++index) {
                buffers[index].BufferType = SECBUFFER_EMPTY;
            }
            SecBufferDesc descriptor{};
            descriptor.ulVersion = SECBUFFER_VERSION;
            descriptor.cBuffers = static_cast<unsigned long>(
                std::size(buffers)
            );
            descriptor.pBuffers = buffers;
            const SECURITY_STATUS status = DecryptMessage(
                &connection_.context,
                &descriptor,
                0,
                nullptr
            );
            if (status == SEC_E_INCOMPLETE_MESSAGE) {
                const SocketIoResult received = receiveSocketBytes(
                    connection_.socket,
                    connection_.encryptedInput,
                    tlsMaximumHandshakeBytes
                );
                if (received.kind == SocketIoKind::Progress) continue;
                if (received.kind == SocketIoKind::WouldBlock) {
                    return {
                        RuntimeInternetTcpStreamIoStatus::WouldBlock,
                        0,
                        {}
                    };
                }
                if (received.kind == SocketIoKind::Closed) {
                    return fail("TLS record was truncated by the peer");
                }
                return fail(received.error);
            }
            if (status == SEC_I_CONTEXT_EXPIRED) {
                connection_.encryptedInput.clear();
                connection_.state = RuntimeInternetTlsState::Closed;
                closeSocket(connection_.socket);
                return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
            }
            if (status == SEC_I_RENEGOTIATE) {
                return fail("TLS renegotiation is not permitted");
            }
            if (status != SEC_E_OK) {
                return fail(securityError("decrypting TLS record", status));
            }

            std::vector<std::uint8_t> remainder;
            for (const SecBuffer& buffer : buffers) {
                if (buffer.BufferType == SECBUFFER_DATA
                    && buffer.pvBuffer != nullptr && buffer.cbBuffer > 0) {
                    const auto* data =
                        static_cast<const std::uint8_t*>(buffer.pvBuffer);
                    plaintext_.insert(
                        plaintext_.end(),
                        data,
                        data + buffer.cbBuffer
                    );
                } else if (buffer.BufferType == SECBUFFER_EXTRA
                    && buffer.pvBuffer != nullptr && buffer.cbBuffer > 0) {
                    const auto* data =
                        static_cast<const std::uint8_t*>(buffer.pvBuffer);
                    remainder.assign(data, data + buffer.cbBuffer);
                }
            }
            connection_.encryptedInput = std::move(remainder);
            if (!plaintext_.empty()) return copyPlaintext(destination);
        }
        return {RuntimeInternetTcpStreamIoStatus::WouldBlock, 0, {}};
    }

    RuntimeInternetTcpStreamIoResult write(
        std::span<const std::uint8_t> source
    ) override {
        if (connection_.state == RuntimeInternetTlsState::Failed) {
            return {
                RuntimeInternetTcpStreamIoStatus::Failed,
                0,
                connection_.error
            };
        }
        if (connection_.state == RuntimeInternetTlsState::Closed) {
            return {RuntimeInternetTcpStreamIoStatus::Closed, 0, {}};
        }
        if (connection_.state != RuntimeInternetTlsState::Open) {
            return {
                RuntimeInternetTcpStreamIoStatus::Failed,
                0,
                "TLS stream is not open"
            };
        }
        if (source.empty()) {
            return {RuntimeInternetTcpStreamIoStatus::Progress, 0, {}};
        }
        if (!connection_.networkOutput.empty()) {
            const SocketIoResult flushed = flushSocketBytes(
                connection_.socket,
                connection_.networkOutput,
                connection_.networkOutputOffset
            );
            if (flushed.kind == SocketIoKind::WouldBlock
                || (flushed.kind == SocketIoKind::Progress
                    && !connection_.networkOutput.empty())) {
                return {
                    RuntimeInternetTcpStreamIoStatus::WouldBlock,
                    0,
                    {}
                };
            }
            if (flushed.kind == SocketIoKind::Closed) {
                return fail("TLS peer closed while writing");
            }
            if (flushed.kind == SocketIoKind::Failed) {
                return fail(flushed.error);
            }
        }

        const std::size_t accepted = std::min<std::size_t>(
            source.size(),
            connection_.streamSizes.cbMaximumMessage
        );
        const std::size_t header = connection_.streamSizes.cbHeader;
        const std::size_t trailer = connection_.streamSizes.cbTrailer;
        connection_.networkOutput.assign(header + accepted + trailer, 0);
        std::copy_n(
            source.begin(),
            accepted,
            connection_.networkOutput.begin()
                + static_cast<std::ptrdiff_t>(header)
        );
        SecBuffer buffers[4]{};
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].cbBuffer = static_cast<unsigned long>(header);
        buffers[0].pvBuffer = connection_.networkOutput.data();
        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].cbBuffer = static_cast<unsigned long>(accepted);
        buffers[1].pvBuffer = connection_.networkOutput.data() + header;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].cbBuffer = static_cast<unsigned long>(trailer);
        buffers[2].pvBuffer =
            connection_.networkOutput.data() + header + accepted;
        buffers[3].BufferType = SECBUFFER_EMPTY;
        SecBufferDesc descriptor{};
        descriptor.ulVersion = SECBUFFER_VERSION;
        descriptor.cBuffers = static_cast<unsigned long>(std::size(buffers));
        descriptor.pBuffers = buffers;
        const SECURITY_STATUS status = EncryptMessage(
            &connection_.context,
            0,
            &descriptor,
            0
        );
        if (status != SEC_E_OK) {
            connection_.networkOutput.clear();
            return fail(securityError("encrypting TLS record", status));
        }
        std::vector<std::uint8_t> encrypted;
        encrypted.reserve(
            static_cast<std::size_t>(buffers[0].cbBuffer)
                + static_cast<std::size_t>(buffers[1].cbBuffer)
                + static_cast<std::size_t>(buffers[2].cbBuffer)
        );
        for (std::size_t index = 0; index < 3; ++index) {
            if (buffers[index].pvBuffer == nullptr
                || buffers[index].cbBuffer == 0) {
                continue;
            }
            const auto* begin = static_cast<const std::uint8_t*>(
                buffers[index].pvBuffer
            );
            encrypted.insert(
                encrypted.end(),
                begin,
                begin + buffers[index].cbBuffer
            );
        }
        connection_.networkOutput = std::move(encrypted);
        connection_.networkOutputOffset = 0;
        const SocketIoResult flushed = flushSocketBytes(
            connection_.socket,
            connection_.networkOutput,
            connection_.networkOutputOffset
        );
        if (flushed.kind == SocketIoKind::Closed) {
            const auto failed = fail("TLS peer closed while writing");
            return {failed.status, accepted, failed.error};
        }
        if (flushed.kind == SocketIoKind::Failed) {
            const auto failed = fail(flushed.error);
            return {failed.status, accepted, failed.error};
        }
        return {
            RuntimeInternetTcpStreamIoStatus::Progress,
            accepted,
            {}
        };
    }

    void close() noexcept override {
        if (connection_.socket == INVALID_SOCKET) {
            connection_.state = RuntimeInternetTlsState::Closed;
            return;
        }
        if (connection_.contextValid
            && connection_.state == RuntimeInternetTlsState::Open) {
            sendCloseNotify();
        }
        (void)::shutdown(connection_.socket, SD_BOTH);
        closeSocket(connection_.socket);
        connection_.state = RuntimeInternetTlsState::Closed;
    }

private:
    RuntimeInternetTcpStreamIoResult copyPlaintext(
        std::span<std::uint8_t> destination
    ) {
        const std::size_t copied = std::min(
            destination.size(),
            plaintext_.size() - plainOffset_
        );
        std::copy_n(
            plaintext_.begin() + static_cast<std::ptrdiff_t>(plainOffset_),
            copied,
            destination.begin()
        );
        plainOffset_ += copied;
        if (plainOffset_ == plaintext_.size()) {
            plaintext_.clear();
            plainOffset_ = 0;
        }
        return {RuntimeInternetTcpStreamIoStatus::Progress, copied, {}};
    }

    RuntimeInternetTcpStreamIoResult fail(std::string errorMessage) {
        connection_.error = std::move(errorMessage);
        connection_.state = RuntimeInternetTlsState::Failed;
        closeSocket(connection_.socket);
        return {
            RuntimeInternetTcpStreamIoStatus::Failed,
            0,
            connection_.error
        };
    }

    void sendCloseNotify() noexcept {
        DWORD shutdownToken = SCHANNEL_SHUTDOWN;
        SecBuffer control{};
        control.BufferType = SECBUFFER_TOKEN;
        control.cbBuffer = sizeof(shutdownToken);
        control.pvBuffer = &shutdownToken;
        SecBufferDesc controlDescriptor{};
        controlDescriptor.ulVersion = SECBUFFER_VERSION;
        controlDescriptor.cBuffers = 1;
        controlDescriptor.pBuffers = &control;
        if (ApplyControlToken(
                &connection_.context,
                &controlDescriptor
            ) != SEC_E_OK) {
            return;
        }
        SecBuffer output{};
        output.BufferType = SECBUFFER_TOKEN;
        SecBufferDesc outputDescriptor{};
        outputDescriptor.ulVersion = SECBUFFER_VERSION;
        outputDescriptor.cBuffers = 1;
        outputDescriptor.pBuffers = &output;
        ULONG attributes = 0;
        TimeStamp expiry{};
        SECURITY_STATUS status = SEC_E_INTERNAL_ERROR;
        if (connection_.serverSide) {
            status = AcceptSecurityContext(
                &connection_.credential->handle,
                &connection_.context,
                nullptr,
                serverContextRequirements,
                SECURITY_NATIVE_DREP,
                &connection_.context,
                &outputDescriptor,
                &attributes,
                &expiry
            );
        } else {
            status = InitializeSecurityContextW(
                &connection_.credential->handle,
                &connection_.context,
                const_cast<wchar_t*>(connection_.serverName.c_str()),
                clientContextRequirements,
                0,
                SECURITY_NATIVE_DREP,
                nullptr,
                0,
                &connection_.context,
                &outputDescriptor,
                &attributes,
                &expiry
            );
        }
        if ((status == SEC_E_OK || status == SEC_I_CONTEXT_EXPIRED)
            && output.pvBuffer != nullptr && output.cbBuffer > 0) {
            const auto* begin =
                static_cast<const std::uint8_t*>(output.pvBuffer);
            std::vector<std::uint8_t> bytes(begin, begin + output.cbBuffer);
            std::size_t offset = 0;
            for (int attempt = 0; attempt < 4 && offset < bytes.size(); ++attempt) {
                const SocketIoResult sent = flushSocketBytes(
                    connection_.socket,
                    bytes,
                    offset
                );
                if (sent.kind != SocketIoKind::Progress) break;
            }
        }
        if (output.pvBuffer != nullptr) FreeContextBuffer(output.pvBuffer);
    }

    PendingTlsConnection connection_;
    RuntimeInternetAuthenticatedTcpStreamIdentity identity_;
    std::vector<std::uint8_t> plaintext_;
    std::size_t plainOffset_ = 0;
};

SOCKET openClientSocket(
    const RuntimeInternetEndpoint& endpoint,
    bool& connected,
    std::string& errorMessage
) {
    connected = false;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    const std::string port = std::to_string(endpoint.port);
    const int resolved = getaddrinfo(
        endpoint.address.c_str(),
        port.c_str(),
        &hints,
        &addresses
    );
    if (resolved != 0 || addresses == nullptr) {
        errorMessage = "could not resolve TLS endpoint";
        return INVALID_SOCKET;
    }
    SOCKET selected = INVALID_SOCKET;
    int lastError = WSAHOST_NOT_FOUND;
    for (addrinfo* address = addresses;
         address != nullptr;
         address = address->ai_next) {
        SOCKET candidate = ::socket(
            address->ai_family,
            address->ai_socktype,
            address->ai_protocol
        );
        if (candidate == INVALID_SOCKET) {
            lastError = WSAGetLastError();
            continue;
        }
        std::string nonblockingError;
        if (!setNonBlocking(candidate, nonblockingError)) {
            lastError = WSAGetLastError();
            closeSocket(candidate);
            continue;
        }
        setTcpNoDelay(candidate);
        const int result = ::connect(
            candidate,
            address->ai_addr,
            static_cast<int>(address->ai_addrlen)
        );
        if (result == 0) {
            connected = true;
            selected = candidate;
            break;
        }
        lastError = WSAGetLastError();
        if (socketWouldBlock(lastError)) {
            selected = candidate;
            break;
        }
        closeSocket(candidate);
    }
    freeaddrinfo(addresses);
    if (selected == INVALID_SOCKET) {
        errorMessage = socketError("connecting TLS socket", lastError);
    }
    return selected;
}

SOCKET openServerListener(
    const RuntimeInternetTlsServerConfig& config,
    std::uint16_t& boundPort,
    std::string& errorMessage
) {
    boundPort = 0;
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        errorMessage = socketError("creating TLS listener");
        return INVALID_SOCKET;
    }
    const int enabled = 1;
    (void)setsockopt(
        listener,
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&enabled),
        static_cast<int>(sizeof(enabled))
    );
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(
        config.bindScope == RuntimeInternetTlsBindScope::LoopbackIpv4
            ? INADDR_LOOPBACK
            : INADDR_ANY
    );
    address.sin_port = htons(config.port);
    if (::bind(
            listener,
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<int>(sizeof(address))
        ) != 0
        || ::listen(
            listener,
            static_cast<int>(std::min<std::size_t>(
                config.maximumConnections,
                static_cast<std::size_t>(SOMAXCONN)
            ))
        ) != 0
        || !setNonBlocking(listener, errorMessage)) {
        if (errorMessage.empty()) {
            errorMessage = socketError("binding TLS listener");
        }
        closeSocket(listener);
        return INVALID_SOCKET;
    }
    sockaddr_in observed{};
    int observedBytes = static_cast<int>(sizeof(observed));
    if (getsockname(
            listener,
            reinterpret_cast<sockaddr*>(&observed),
            &observedBytes
        ) != 0) {
        errorMessage = socketError("reading TLS listener address");
        closeSocket(listener);
        return INVALID_SOCKET;
    }
    boundPort = ntohs(observed.sin_port);
    return listener;
}

std::string addressText(const sockaddr_in& address) {
    std::array<char, INET_ADDRSTRLEN> text{};
    if (InetNtopA(AF_INET, &address.sin_addr, text.data(), text.size())
        == nullptr) {
        return "unknown";
    }
    return text.data();
}

} // namespace

struct RuntimeInternetTlsClient::Impl {
    RuntimeInternetTlsClientConfig config;
    RuntimeInternetAuthenticatedTcpStreamIdentity identity;
    PendingTlsConnection connection;
    std::uint64_t connectDeadline = 0;
    bool streamTaken = false;
};

RuntimeInternetTlsClient::RuntimeInternetTlsClient()
    : impl_(std::make_unique<Impl>()) {}

RuntimeInternetTlsClient::~RuntimeInternetTlsClient() = default;

RuntimeInternetTlsClient::RuntimeInternetTlsClient(
    RuntimeInternetTlsClient&&
) noexcept = default;

RuntimeInternetTlsClient& RuntimeInternetTlsClient::operator=(
    RuntimeInternetTlsClient&&
) noexcept = default;

bool RuntimeInternetTlsClient::begin(
    RuntimeInternetTlsClientConfig config,
    RuntimeInternetAuthenticatedTcpStreamIdentity identity,
    std::uint64_t nowMilliseconds,
    std::string& errorMessage
) {
    errorMessage.clear();
    impl_ = std::make_unique<Impl>();
    if (config.endpoint.address.empty() || config.endpoint.port == 0
        || config.serverName.empty() || config.serverName.size() > 253
        || !validLimits(config.limits, errorMessage)
        || !validIdentity(identity, errorMessage)) {
        if (errorMessage.empty()) {
            errorMessage = "TLS client configuration is incomplete";
        }
        impl_->connection.state = RuntimeInternetTlsState::Failed;
        impl_->connection.error = errorMessage;
        return false;
    }
    std::wstring serverName;
    if (!toWide(config.serverName, serverName, errorMessage)) {
        impl_->connection.state = RuntimeInternetTlsState::Failed;
        impl_->connection.error = errorMessage;
        return false;
    }
    auto socketSystem = std::make_shared<SocketSystem>();
    if (!socketSystem->initialized()) {
        errorMessage = "could not initialize Winsock for TLS";
        impl_->connection.state = RuntimeInternetTlsState::Failed;
        impl_->connection.error = errorMessage;
        return false;
    }
    auto credential = acquireClientCredential(errorMessage);
    if (!credential) {
        impl_->connection.state = RuntimeInternetTlsState::Failed;
        impl_->connection.error = errorMessage;
        return false;
    }
    bool connected = false;
    SOCKET socket = openClientSocket(
        config.endpoint,
        connected,
        errorMessage
    );
    if (socket == INVALID_SOCKET) {
        impl_->connection.state = RuntimeInternetTlsState::Failed;
        impl_->connection.error = errorMessage;
        return false;
    }
    impl_->config = std::move(config);
    impl_->identity = std::move(identity);
    impl_->connection.socket = socket;
    impl_->connection.socketSystem = std::move(socketSystem);
    impl_->connection.credential = std::move(credential);
    impl_->connection.serverSide = false;
    impl_->connection.serverName = std::move(serverName);
    impl_->connection.checkRevocation = impl_->config.checkCertificateRevocation;
    impl_->connection.expectedPin = impl_->config.leafCertificateSha256;
    impl_->connection.chainEngine = reinterpret_cast<HCERTCHAINENGINE>(
        const_cast<void*>(impl_->config.nativeCertificateChainEngine)
    );
    impl_->connectDeadline = nowMilliseconds
        + impl_->config.limits.connectTimeoutMilliseconds;
    if (connected) {
        impl_->connection.state = RuntimeInternetTlsState::Handshaking;
        impl_->connection.handshakeDeadline = nowMilliseconds
            + impl_->config.limits.handshakeTimeoutMilliseconds;
    } else {
        impl_->connection.state = RuntimeInternetTlsState::TcpConnecting;
    }
    return true;
}

RuntimeInternetTlsPollResult RuntimeInternetTlsClient::poll(
    std::uint64_t nowMilliseconds
) {
    RuntimeInternetTlsPollResult result;
    result.state = state();
    if (!impl_ || impl_->streamTaken) return result;
    PendingTlsConnection& connection = impl_->connection;
    if (connection.state == RuntimeInternetTlsState::TcpConnecting) {
        if (nowMilliseconds >= impl_->connectDeadline) {
            connection.state = RuntimeInternetTlsState::Failed;
            connection.error = "TLS TCP connection timed out";
            closeSocket(connection.socket);
        } else {
            fd_set writes;
            fd_set exceptions;
            FD_ZERO(&writes);
            FD_ZERO(&exceptions);
            FD_SET(connection.socket, &writes);
            FD_SET(connection.socket, &exceptions);
            timeval timeout{};
            const int selected = select(
                0,
                nullptr,
                &writes,
                &exceptions,
                &timeout
            );
            if (selected == SOCKET_ERROR) {
                connection.state = RuntimeInternetTlsState::Failed;
                connection.error = socketError("polling TLS connection");
                closeSocket(connection.socket);
            } else if (selected > 0) {
                int completionError = 0;
                int completionBytes = static_cast<int>(
                    sizeof(completionError)
                );
                if (getsockopt(
                        connection.socket,
                        SOL_SOCKET,
                        SO_ERROR,
                        reinterpret_cast<char*>(&completionError),
                        &completionBytes
                    ) != 0 || completionError != 0) {
                    connection.state = RuntimeInternetTlsState::Failed;
                    connection.error = socketError(
                        "completing TLS connection",
                        completionError != 0
                            ? completionError
                            : WSAGetLastError()
                    );
                    closeSocket(connection.socket);
                } else {
                    connection.state = RuntimeInternetTlsState::Handshaking;
                    connection.handshakeDeadline = nowMilliseconds
                        + impl_->config.limits.handshakeTimeoutMilliseconds;
                    result.progressed = true;
                }
            }
        }
    }
    if (connection.state == RuntimeInternetTlsState::Handshaking) {
        bool progressed = false;
        (void)advanceHandshake(connection, nowMilliseconds, progressed);
        result.progressed = result.progressed || progressed;
        if (connection.state == RuntimeInternetTlsState::Failed) {
            closeSocket(connection.socket);
        }
    }
    result.state = connection.state;
    result.error = connection.error;
    return result;
}

std::unique_ptr<RuntimeInternetAuthenticatedTcpStream>
RuntimeInternetTlsClient::takeStream(std::string& errorMessage) {
    errorMessage.clear();
    if (!impl_ || impl_->streamTaken
        || impl_->connection.state != RuntimeInternetTlsState::Open) {
        errorMessage = "TLS client stream is not ready";
        return {};
    }
    auto stream = std::make_unique<SchannelAuthenticatedStream>(
        std::move(impl_->connection),
        std::move(impl_->identity)
    );
    impl_->streamTaken = true;
    return stream;
}

RuntimeInternetTlsState RuntimeInternetTlsClient::state() const {
    if (!impl_) return RuntimeInternetTlsState::Closed;
    if (impl_->streamTaken) return RuntimeInternetTlsState::Closed;
    return impl_->connection.state;
}

void RuntimeInternetTlsClient::close() noexcept {
    if (!impl_) return;
    impl_->connection.reset();
    impl_->connection.state = RuntimeInternetTlsState::Closed;
    impl_->streamTaken = false;
}

struct RuntimeInternetTlsServer::Impl {
    struct Connection {
        PendingTlsConnection tls;
        bool handshakeAnnounced = false;
    };

    SOCKET listener = INVALID_SOCKET;
    std::shared_ptr<SocketSystem> socketSystem;
    std::shared_ptr<SecurityCredential> credential;
    RuntimeInternetTlsServerConfig config;
    std::uint16_t boundPort = 0;
    std::uint64_t nextConnectionId = 1;
    std::map<std::uint64_t, Connection> connections;
};

RuntimeInternetTlsServer::RuntimeInternetTlsServer()
    : impl_(std::make_unique<Impl>()) {}

RuntimeInternetTlsServer::~RuntimeInternetTlsServer() = default;

RuntimeInternetTlsServer::RuntimeInternetTlsServer(
    RuntimeInternetTlsServer&&
) noexcept = default;

RuntimeInternetTlsServer& RuntimeInternetTlsServer::operator=(
    RuntimeInternetTlsServer&&
) noexcept = default;

bool RuntimeInternetTlsServer::open(
    RuntimeInternetTlsServerConfig config,
    std::string& errorMessage
) {
    errorMessage.clear();
    impl_ = std::make_unique<Impl>();
    if (config.maximumConnections == 0
        || config.maximumConnections > runtimeInternetTlsMaximumConnections
        || !validLimits(config.limits, errorMessage)) {
        if (errorMessage.empty()) {
            errorMessage = "TLS server connection limit is invalid";
        }
        return false;
    }
    auto socketSystem = std::make_shared<SocketSystem>();
    if (!socketSystem->initialized()) {
        errorMessage = "could not initialize Winsock for TLS server";
        return false;
    }
    auto credential = acquireServerCredential(
        config.nativeCertificateContext,
        errorMessage
    );
    if (!credential) return false;
    std::uint16_t boundPort = 0;
    SOCKET listener = openServerListener(config, boundPort, errorMessage);
    if (listener == INVALID_SOCKET) return false;
    impl_->listener = listener;
    impl_->socketSystem = std::move(socketSystem);
    impl_->credential = std::move(credential);
    impl_->config = config;
    impl_->boundPort = boundPort;
    return true;
}

std::vector<RuntimeInternetTlsServerEvent> RuntimeInternetTlsServer::poll(
    std::uint64_t nowMilliseconds
) {
    std::vector<RuntimeInternetTlsServerEvent> events;
    if (!isOpen()) return events;

    while (impl_->connections.size() < impl_->config.maximumConnections) {
        sockaddr_in peer{};
        int peerBytes = static_cast<int>(sizeof(peer));
        SOCKET socket = accept(
            impl_->listener,
            reinterpret_cast<sockaddr*>(&peer),
            &peerBytes
        );
        if (socket == INVALID_SOCKET) {
            const int error = WSAGetLastError();
            if (!socketWouldBlock(error)) {
                events.push_back({
                    RuntimeInternetTlsServerEventKind::Rejected,
                    0,
                    {},
                    socketError("accepting TLS connection", error)
                });
            }
            break;
        }
        std::string setupError;
        if (!setNonBlocking(socket, setupError)) {
            closeSocket(socket);
            events.push_back({
                RuntimeInternetTlsServerEventKind::Rejected,
                0,
                addressText(peer),
                std::move(setupError)
            });
            continue;
        }
        setTcpNoDelay(socket);
        const std::uint64_t connectionId = impl_->nextConnectionId++;
        Impl::Connection accepted;
        accepted.tls.socket = socket;
        accepted.tls.socketSystem = impl_->socketSystem;
        accepted.tls.credential = impl_->credential;
        accepted.tls.serverSide = true;
        accepted.tls.state = RuntimeInternetTlsState::Handshaking;
        accepted.tls.handshakeDeadline = nowMilliseconds
            + impl_->config.limits.handshakeTimeoutMilliseconds;
        accepted.tls.peerAddress = addressText(peer);
        impl_->connections.emplace(connectionId, std::move(accepted));
    }

    for (auto iterator = impl_->connections.begin();
         iterator != impl_->connections.end();) {
        const std::uint64_t connectionId = iterator->first;
        Impl::Connection& connection = iterator->second;
        if (connection.tls.state == RuntimeInternetTlsState::Handshaking) {
            bool progressed = false;
            (void)advanceHandshake(connection.tls, nowMilliseconds, progressed);
        }
        if (connection.tls.state == RuntimeInternetTlsState::Failed) {
            events.push_back({
                RuntimeInternetTlsServerEventKind::Rejected,
                connectionId,
                connection.tls.peerAddress,
                connection.tls.error
            });
            iterator = impl_->connections.erase(iterator);
            continue;
        }
        if (connection.tls.state == RuntimeInternetTlsState::Open
            && !connection.handshakeAnnounced) {
            connection.handshakeAnnounced = true;
            events.push_back({
                RuntimeInternetTlsServerEventKind::HandshakeComplete,
                connectionId,
                connection.tls.peerAddress,
                {}
            });
        } else if (connection.tls.state == RuntimeInternetTlsState::Open) {
            char byte = 0;
            const int peeked = recv(connection.tls.socket, &byte, 1, MSG_PEEK);
            if (peeked == 0) {
                events.push_back({
                    RuntimeInternetTlsServerEventKind::Disconnected,
                    connectionId,
                    connection.tls.peerAddress,
                    "TLS peer disconnected before application authentication"
                });
                iterator = impl_->connections.erase(iterator);
                continue;
            }
            if (peeked == SOCKET_ERROR) {
                const int error = WSAGetLastError();
                if (!socketWouldBlock(error)) {
                    events.push_back({
                        RuntimeInternetTlsServerEventKind::Disconnected,
                        connectionId,
                        connection.tls.peerAddress,
                        socketError("checking TLS connection", error)
                    });
                    iterator = impl_->connections.erase(iterator);
                    continue;
                }
            }
        }
        ++iterator;
    }
    return events;
}

std::unique_ptr<RuntimeInternetAuthenticatedTcpStream>
RuntimeInternetTlsServer::takeApplicationAuthenticatedStream(
    std::uint64_t connectionId,
    RuntimeInternetAuthenticatedTcpStreamIdentity identity,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (!validIdentity(identity, errorMessage)) return {};
    if (!impl_) {
        errorMessage = "TLS server is closed";
        return {};
    }
    const auto found = impl_->connections.find(connectionId);
    if (found == impl_->connections.end()
        || found->second.tls.state != RuntimeInternetTlsState::Open) {
        errorMessage = "TLS server connection is not ready";
        return {};
    }
    auto stream = std::make_unique<SchannelAuthenticatedStream>(
        std::move(found->second.tls),
        std::move(identity)
    );
    impl_->connections.erase(found);
    return stream;
}

bool RuntimeInternetTlsServer::disconnect(
    std::uint64_t connectionId
) noexcept {
    if (!impl_) return false;
    const auto found = impl_->connections.find(connectionId);
    if (found == impl_->connections.end()) return false;
    impl_->connections.erase(found);
    return true;
}

bool RuntimeInternetTlsServer::isOpen() const {
    return impl_ && impl_->listener != INVALID_SOCKET;
}

std::uint16_t RuntimeInternetTlsServer::port() const {
    return impl_ ? impl_->boundPort : 0;
}

std::size_t RuntimeInternetTlsServer::connectionCount() const {
    return impl_ ? impl_->connections.size() : 0;
}

void RuntimeInternetTlsServer::close() noexcept {
    if (!impl_) return;
    closeSocket(impl_->listener);
    impl_->connections.clear();
    impl_->credential.reset();
    impl_->socketSystem.reset();
    impl_->boundPort = 0;
}

#else

struct RuntimeInternetTlsClient::Impl {
    RuntimeInternetTlsState state = RuntimeInternetTlsState::Idle;
    std::string error;
};

RuntimeInternetTlsClient::RuntimeInternetTlsClient()
    : impl_(std::make_unique<Impl>()) {}
RuntimeInternetTlsClient::~RuntimeInternetTlsClient() = default;
RuntimeInternetTlsClient::RuntimeInternetTlsClient(
    RuntimeInternetTlsClient&&
) noexcept = default;
RuntimeInternetTlsClient& RuntimeInternetTlsClient::operator=(
    RuntimeInternetTlsClient&&
) noexcept = default;
bool RuntimeInternetTlsClient::begin(
    RuntimeInternetTlsClientConfig,
    RuntimeInternetAuthenticatedTcpStreamIdentity,
    std::uint64_t,
    std::string& errorMessage
) {
    errorMessage = "Runtime Internet TLS transport requires Windows Schannel";
    impl_->state = RuntimeInternetTlsState::Failed;
    impl_->error = errorMessage;
    return false;
}
RuntimeInternetTlsPollResult RuntimeInternetTlsClient::poll(std::uint64_t) {
    return {impl_->state, false, impl_->error};
}
std::unique_ptr<RuntimeInternetAuthenticatedTcpStream>
RuntimeInternetTlsClient::takeStream(std::string& errorMessage) {
    errorMessage = "Runtime Internet TLS transport requires Windows Schannel";
    return {};
}
RuntimeInternetTlsState RuntimeInternetTlsClient::state() const {
    return impl_ ? impl_->state : RuntimeInternetTlsState::Closed;
}
void RuntimeInternetTlsClient::close() noexcept {
    if (impl_) impl_->state = RuntimeInternetTlsState::Closed;
}

struct RuntimeInternetTlsServer::Impl {};
RuntimeInternetTlsServer::RuntimeInternetTlsServer()
    : impl_(std::make_unique<Impl>()) {}
RuntimeInternetTlsServer::~RuntimeInternetTlsServer() = default;
RuntimeInternetTlsServer::RuntimeInternetTlsServer(
    RuntimeInternetTlsServer&&
) noexcept = default;
RuntimeInternetTlsServer& RuntimeInternetTlsServer::operator=(
    RuntimeInternetTlsServer&&
) noexcept = default;
bool RuntimeInternetTlsServer::open(
    RuntimeInternetTlsServerConfig,
    std::string& errorMessage
) {
    errorMessage = "Runtime Internet TLS transport requires Windows Schannel";
    return false;
}
std::vector<RuntimeInternetTlsServerEvent> RuntimeInternetTlsServer::poll(
    std::uint64_t
) {
    return {};
}
std::unique_ptr<RuntimeInternetAuthenticatedTcpStream>
RuntimeInternetTlsServer::takeApplicationAuthenticatedStream(
    std::uint64_t,
    RuntimeInternetAuthenticatedTcpStreamIdentity,
    std::string& errorMessage
) {
    errorMessage = "Runtime Internet TLS transport requires Windows Schannel";
    return {};
}
bool RuntimeInternetTlsServer::disconnect(std::uint64_t) noexcept {
    return false;
}
bool RuntimeInternetTlsServer::isOpen() const { return false; }
std::uint16_t RuntimeInternetTlsServer::port() const { return 0; }
std::size_t RuntimeInternetTlsServer::connectionCount() const { return 0; }
void RuntimeInternetTlsServer::close() noexcept {}

#endif
