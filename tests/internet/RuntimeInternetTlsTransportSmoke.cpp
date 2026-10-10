#include "network/transport/RuntimeInternetTlsTransport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>
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
        container_ = L"ConcordRTS-TlsSmoke-";
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
            )) {
            errorMessage = "could not open the test certificate key provider";
            return false;
        }
        if (!CryptGenKey(
                provider_,
                AT_KEYEXCHANGE,
                (2048U << 16U) | CRYPT_EXPORTABLE,
                &key_
            )) {
            errorMessage = "could not generate test certificate key";
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
        CERT_NAME_BLOB subjectBlob{};
        subjectBlob.cbData = subjectBytes;
        subjectBlob.pbData = subject.data();

        CERT_ALT_NAME_ENTRY alternateName{};
        alternateName.dwAltNameChoice = CERT_ALT_NAME_DNS_NAME;
        alternateName.pwszDNSName = const_cast<wchar_t*>(L"localhost");
        CERT_ALT_NAME_INFO alternateNames{};
        alternateNames.cAltEntry = 1;
        alternateNames.rgAltEntry = &alternateName;
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
            errorMessage = "could not encode certificate alternate name";
            return false;
        }

        LPSTR serverUsage[] = {
            const_cast<LPSTR>(szOID_PKIX_KP_SERVER_AUTH)
        };
        CERT_ENHKEY_USAGE enhancedUsage{};
        enhancedUsage.cUsageIdentifier = 1;
        enhancedUsage.rgpszUsageIdentifier = serverUsage;
        BYTE* enhancedUsageBytes = nullptr;
        DWORD enhancedUsageSize = 0;
        if (!CryptEncodeObjectEx(
                X509_ASN_ENCODING,
                X509_ENHANCED_KEY_USAGE,
                &enhancedUsage,
                CRYPT_ENCODE_ALLOC_FLAG,
                nullptr,
                &enhancedUsageBytes,
                &enhancedUsageSize
            )) {
            LocalFree(alternateNameBytes);
            errorMessage = "could not encode certificate server usage";
            return false;
        }

        CERT_EXTENSION extensions[2]{};
        extensions[0].pszObjId = const_cast<LPSTR>(szOID_SUBJECT_ALT_NAME2);
        extensions[0].Value.cbData = alternateNameSize;
        extensions[0].Value.pbData = alternateNameBytes;
        extensions[1].pszObjId = const_cast<LPSTR>(szOID_ENHANCED_KEY_USAGE);
        extensions[1].Value.cbData = enhancedUsageSize;
        extensions[1].Value.pbData = enhancedUsageBytes;
        CERT_EXTENSIONS extensionList{};
        extensionList.cExtension = 2;
        extensionList.rgExtension = extensions;

        CRYPT_ALGORITHM_IDENTIFIER signature{};
        signature.pszObjId = const_cast<LPSTR>(szOID_RSA_SHA256RSA);
        CRYPT_KEY_PROV_INFO keyProviderInfo{};
        keyProviderInfo.pwszContainerName = container_.data();
        keyProviderInfo.pwszProvName =
            const_cast<wchar_t*>(MS_ENH_RSA_AES_PROV_W);
        keyProviderInfo.dwProvType = PROV_RSA_AES;
        keyProviderInfo.dwKeySpec = AT_KEYEXCHANGE;
        SYSTEMTIME now{};
        SYSTEMTIME validFrom{};
        SYSTEMTIME validUntil{};
        GetSystemTime(&now);
        const bool datesReady = shiftSystemTime(now, -86400, validFrom)
            && shiftSystemTime(now, 7 * 86400, validUntil);
        if (datesReady) {
            certificate_ = CertCreateSelfSignCertificate(
                static_cast<HCRYPTPROV_OR_NCRYPT_KEY_HANDLE>(provider_),
                &subjectBlob,
                0,
                &keyProviderInfo,
                &signature,
                &validFrom,
                &validUntil,
                &extensionList
            );
        }
        LocalFree(enhancedUsageBytes);
        LocalFree(alternateNameBytes);
        if (certificate_ == nullptr) {
            errorMessage = "could not create local self-signed certificate";
            return false;
        }
        if (!CertSetCertificateContextProperty(
                certificate_,
                CERT_KEY_PROV_INFO_PROP_ID,
                0,
                &keyProviderInfo
            )) {
            errorMessage = "could not attach the test certificate private key";
            return false;
        }
        return true;
    }

    bool trust(std::string& errorMessage) {
        if (certificate_ == nullptr || chainEngine_ != nullptr) {
            errorMessage = "test certificate is not ready for trust";
            return false;
        }
        rootStore_ = CertOpenStore(
            CERT_STORE_PROV_MEMORY,
            0,
            0,
            0,
            nullptr
        );
        if (rootStore_ == nullptr
            || !CertAddCertificateContextToStore(
                rootStore_,
                certificate_,
                CERT_STORE_ADD_NEW,
                nullptr
            )) {
            errorMessage = "could not create the in-memory test root store";
            return false;
        }
        CERT_CHAIN_ENGINE_CONFIG chainConfig{};
        chainConfig.cbSize = sizeof(chainConfig);
        chainConfig.hExclusiveRoot = rootStore_;
        if (!CertCreateCertificateChainEngine(
                &chainConfig,
                &chainEngine_
            )) {
            errorMessage = "could not create the private test chain engine";
            return false;
        }
        return true;
    }

    RuntimeInternetTlsSha256 sha256(std::string& errorMessage) const {
        RuntimeInternetTlsSha256 digest{};
        DWORD digestBytes = static_cast<DWORD>(digest.size());
        if (certificate_ == nullptr
            || !CryptHashCertificate2(
                BCRYPT_SHA256_ALGORITHM,
                0,
                nullptr,
                certificate_->pbCertEncoded,
                certificate_->cbCertEncoded,
                digest.data(),
                &digestBytes
            )
            || digestBytes != static_cast<DWORD>(digest.size())) {
            errorMessage = "could not hash the test certificate";
            digest.fill(0);
        }
        return digest;
    }

    PCCERT_CONTEXT get() const noexcept { return certificate_; }
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

    HCRYPTPROV provider_ = 0;
    HCRYPTKEY key_ = 0;
    std::wstring container_;
    PCCERT_CONTEXT certificate_ = nullptr;
    HCERTSTORE rootStore_ = nullptr;
    HCERTCHAINENGINE chainEngine_ = nullptr;
};

RuntimeInternetAuthenticatedTcpStreamIdentity identityFor(
    const std::string& peerId
) {
    RuntimeInternetAuthenticatedTcpStreamIdentity identity;
    identity.authenticatedServiceId = "tls-smoke-service";
    identity.authenticatedPeerId = peerId;
    return identity;
}

struct ConnectedPair {
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> client;
    std::unique_ptr<RuntimeInternetAuthenticatedTcpStream> server;
};

bool waitForConnection(
    RuntimeInternetTlsServer& server,
    RuntimeInternetTlsClientConfig config,
    bool expectSuccess,
    ConnectedPair& pair,
    std::string& observedError
) {
    RuntimeInternetTlsClient client;
    std::string error;
    if (!client.begin(
            std::move(config),
            identityFor("client-account"),
            nowMilliseconds(),
            error
        )) {
        observedError = std::move(error);
        return !expectSuccess;
    }
    std::uint64_t readyConnectionId = 0;
    const std::uint64_t deadline = nowMilliseconds() + 10000;
    while (nowMilliseconds() < deadline) {
        const RuntimeInternetTlsPollResult clientPoll = client.poll(
            nowMilliseconds()
        );
        for (const RuntimeInternetTlsServerEvent& event
             : server.poll(nowMilliseconds())) {
            if (event.kind
                == RuntimeInternetTlsServerEventKind::HandshakeComplete) {
                readyConnectionId = event.connectionId;
            } else if (event.kind
                == RuntimeInternetTlsServerEventKind::Rejected) {
                observedError = event.error;
            }
        }
        if (clientPoll.state == RuntimeInternetTlsState::Failed) {
            observedError = clientPoll.error;
            if (readyConnectionId != 0) {
                (void)server.disconnect(readyConnectionId);
            }
            return !expectSuccess;
        }
        if (clientPoll.state == RuntimeInternetTlsState::Open
            && readyConnectionId != 0) {
            if (!expectSuccess) {
                (void)server.disconnect(readyConnectionId);
                client.close();
                observedError = "unexpected TLS connection success";
                return false;
            }
            pair.client = client.takeStream(error);
            pair.server = server.takeApplicationAuthenticatedStream(
                readyConnectionId,
                identityFor("service-side-client"),
                error
            );
            if (!pair.client || !pair.server) {
                observedError = std::move(error);
                return false;
            }
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (readyConnectionId != 0) {
        (void)server.disconnect(readyConnectionId);
    }
    client.close();
    observedError = "TLS connection smoke timed out";
    return false;
}

bool transferBytes(
    RuntimeInternetAuthenticatedTcpStream& sender,
    RuntimeInternetAuthenticatedTcpStream& receiver,
    const std::vector<std::uint8_t>& expected,
    std::string& errorMessage
) {
    std::size_t sent = 0;
    std::vector<std::uint8_t> observed;
    observed.reserve(expected.size());
    std::array<std::uint8_t, 64U * 1024U> buffer{};
    const std::uint64_t deadline = nowMilliseconds() + 15000;
    while (observed.size() < expected.size()
        && nowMilliseconds() < deadline) {
        bool progressed = false;
        if (sent < expected.size()) {
            const RuntimeInternetTcpStreamIoResult write = sender.write(
                std::span<const std::uint8_t>(expected).subspan(sent)
            );
            if (write.status == RuntimeInternetTcpStreamIoStatus::Failed
                || write.status == RuntimeInternetTcpStreamIoStatus::Closed) {
                errorMessage = write.error.empty()
                    ? "TLS sender closed during transfer"
                    : write.error;
                return false;
            }
            sent += write.bytesTransferred;
            progressed = progressed || write.bytesTransferred > 0;
        }
        const RuntimeInternetTcpStreamIoResult read = receiver.read(buffer);
        if (read.status == RuntimeInternetTcpStreamIoStatus::Failed
            || read.status == RuntimeInternetTcpStreamIoStatus::Closed) {
            errorMessage = read.error.empty()
                ? "TLS receiver closed during transfer"
                : read.error;
            return false;
        }
        if (read.bytesTransferred > 0) {
            observed.insert(
                observed.end(),
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
    if (sent != expected.size() || observed != expected) {
        errorMessage = "TLS large payload was truncated or changed";
        return false;
    }
    return true;
}

bool waitForPlaintextRejection(
    RuntimeInternetTlsServer& server,
    std::string& errorMessage
) {
    SOCKET socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) {
        errorMessage = "could not create plaintext probe socket";
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(server.port());
    if (connect(
            socket,
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<int>(sizeof(address))
        ) != 0) {
        closesocket(socket);
        errorMessage = "could not connect plaintext probe";
        return false;
    }
    constexpr char plaintext[] = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
    const int sent = send(
        socket,
        plaintext,
        static_cast<int>(sizeof(plaintext) - 1),
        0
    );
    (void)::shutdown(socket, SD_BOTH);
    closesocket(socket);
    if (sent != static_cast<int>(sizeof(plaintext) - 1)) {
        errorMessage = "could not send plaintext probe";
        return false;
    }
    const std::uint64_t deadline = nowMilliseconds() + 5000;
    while (nowMilliseconds() < deadline) {
        for (const RuntimeInternetTlsServerEvent& event
             : server.poll(nowMilliseconds())) {
            if (event.kind == RuntimeInternetTlsServerEventKind::Rejected) {
                return true;
            }
            if (event.kind
                == RuntimeInternetTlsServerEventKind::HandshakeComplete) {
                (void)server.disconnect(event.connectionId);
                errorMessage = "server accepted plaintext as TLS";
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    errorMessage = "server did not reject plaintext probe";
    return false;
}

} // namespace
#endif

int main() {
#ifndef _WIN32
    std::cout << "Runtime Internet TLS smoke skipped: Windows only\n";
    return 0;
#else
    SmokeContext smoke;
    TestCertificate certificate;
    std::string error;
    smoke.expect(certificate.create(error), error);
    if (certificate.get() == nullptr) return 1;

    RuntimeInternetTlsServer server;
    RuntimeInternetTlsServerConfig serverConfig;
    serverConfig.bindScope = RuntimeInternetTlsBindScope::LoopbackIpv4;
    serverConfig.maximumConnections = 16;
    serverConfig.nativeCertificateContext = certificate.get();
    smoke.expect(server.open(serverConfig, error), error);
    if (!server.isOpen()) return 1;

    RuntimeInternetTlsClientConfig clientConfig;
    clientConfig.endpoint = {"127.0.0.1", server.port()};
    clientConfig.serverName = "localhost";

    ConnectedPair unused;
    std::string failure;
    smoke.expect(
        waitForConnection(server, clientConfig, false, unused, failure),
        "an untrusted self-signed certificate was accepted: " + failure
    );

    smoke.expect(certificate.trust(error), error);
    RuntimeInternetTlsSha256 pin = certificate.sha256(error);
    smoke.expect(error.empty(), error);
    clientConfig.leafCertificateSha256 = pin;
    clientConfig.nativeCertificateChainEngine = certificate.chainEngine();

    ConnectedPair connected;
    smoke.expect(
        waitForConnection(server, clientConfig, true, connected, error),
        error
    );
    if (connected.client && connected.server) {
        std::vector<std::uint8_t> payload(2U * 1024U * 1024U + 37U);
        for (std::size_t index = 0; index < payload.size(); ++index) {
            payload[index] = static_cast<std::uint8_t>(
                (index * 131U + 17U) & 0xffU
            );
        }
        smoke.expect(
            transferBytes(
                *connected.client,
                *connected.server,
                payload,
                error
            ),
            error
        );
        std::vector<std::uint8_t> response(257U * 1024U + 11U, 0x5aU);
        smoke.expect(
            transferBytes(
                *connected.server,
                *connected.client,
                response,
                error
            ),
            error
        );
        connected.client->close();
        std::array<std::uint8_t, 16> closeBuffer{};
        bool observedClose = false;
        const std::uint64_t closeDeadline = nowMilliseconds() + 3000;
        while (nowMilliseconds() < closeDeadline) {
            const RuntimeInternetTcpStreamIoResult read =
                connected.server->read(closeBuffer);
            if (read.status == RuntimeInternetTcpStreamIoStatus::Closed) {
                observedClose = true;
                break;
            }
            if (read.status == RuntimeInternetTcpStreamIoStatus::Failed) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        smoke.expect(observedClose, "TLS close_notify was not observed");
    }

    RuntimeInternetTlsClientConfig wrongHost = clientConfig;
    wrongHost.serverName = "not-localhost.invalid";
    ConnectedPair wrongHostPair;
    failure.clear();
    smoke.expect(
        waitForConnection(server, wrongHost, false, wrongHostPair, failure),
        "TLS hostname mismatch was accepted: " + failure
    );

    RuntimeInternetTlsClientConfig wrongPin = clientConfig;
    wrongPin.leafCertificateSha256->at(0) ^= 0xffU;
    ConnectedPair wrongPinPair;
    failure.clear();
    smoke.expect(
        waitForConnection(server, wrongPin, false, wrongPinPair, failure),
        "TLS certificate pin mismatch was accepted: " + failure
    );

    smoke.expect(waitForPlaintextRejection(server, error), error);
    server.close();

    if (smoke.failures == 0) {
        std::cout
            << "Runtime Internet TLS transport smoke passed: Schannel "
               "trust, hostname, pin, plaintext rejection, close, and large "
               "payload behavior are valid\n";
        return 0;
    }
    std::cerr << smoke.failures << " TLS smoke assertion(s) failed\n";
    return 1;
#endif
}
