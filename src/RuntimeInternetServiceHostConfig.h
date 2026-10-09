#pragma once

#include "RuntimeInternetServiceHost.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

enum class RuntimeInternetCertificateStoreLocation : std::uint8_t {
    CurrentUser = 1,
    LocalMachine = 2
};

struct RuntimeInternetServiceCertificateSelection {
    RuntimeInternetCertificateStoreLocation location =
        RuntimeInternetCertificateStoreLocation::LocalMachine;
    std::string storeName;
    std::array<std::uint8_t, 20U> sha1Thumbprint{};
};

struct RuntimeInternetServiceHostDeploymentConfig;

// Only SHA-256 credential digests are retained in the shared, read-only
// store. Raw client credentials never enter the deployment configuration.
class RuntimeInternetServiceCredentialStore {
public:
    RuntimeInternetServiceCredentialStore();
    ~RuntimeInternetServiceCredentialStore();
    RuntimeInternetServiceCredentialStore(
        const RuntimeInternetServiceCredentialStore&
    ) noexcept;
    RuntimeInternetServiceCredentialStore& operator=(
        const RuntimeInternetServiceCredentialStore&
    ) noexcept;
    RuntimeInternetServiceCredentialStore(
        RuntimeInternetServiceCredentialStore&&
    ) noexcept;
    RuntimeInternetServiceCredentialStore& operator=(
        RuntimeInternetServiceCredentialStore&&
    ) noexcept;

    std::size_t size() const noexcept;
    RuntimeInternetServiceCredentialVerifier makeVerifier(
        std::string serviceId
    ) const;

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;

    bool insert(
        std::string peerId,
        std::array<std::uint8_t, 32U> credentialSha256,
        std::string& errorMessage
    );

    friend bool parseRuntimeInternetServiceHostConfigText(
        std::string_view,
        RuntimeInternetServiceHostDeploymentConfig&,
        std::string&
    );
};

struct RuntimeInternetServiceHostDeploymentConfig {
    std::string serviceId;
    std::uint16_t listenPort = 0U;
    RuntimeInternetTlsBindScope listenScope =
        RuntimeInternetTlsBindScope::AnyIpv4;
    std::string publicRelayAddress;
    RuntimeInternetServiceCertificateSelection certificate;
    RuntimeInternetServiceCredentialStore credentials;
    // Derived by the file loader and kept beside the deployment INI. The
    // strict text schema never accepts an arbitrary state path from clients.
    std::filesystem::path communityStatePath;
};

// Strict INI parser. Only [Service] and [Credentials] are accepted, both
// exactly once. Every [Service] key is required exactly once; credential keys
// are case-sensitive peer ids and may also occur only once.
bool parseRuntimeInternetServiceHostConfigText(
    std::string_view iniText,
    RuntimeInternetServiceHostDeploymentConfig& config,
    std::string& errorMessage
);

bool loadRuntimeInternetServiceHostConfig(
    const std::filesystem::path& path,
    RuntimeInternetServiceHostDeploymentConfig& config,
    std::string& errorMessage
);

// RAII wrapper for the unique Windows certificate selected by SHA-1
// thumbprint. The native context remains owned by this object.
class RuntimeInternetServiceCertificateContext {
public:
    RuntimeInternetServiceCertificateContext();
    ~RuntimeInternetServiceCertificateContext();
    RuntimeInternetServiceCertificateContext(
        RuntimeInternetServiceCertificateContext&&
    ) noexcept;
    RuntimeInternetServiceCertificateContext& operator=(
        RuntimeInternetServiceCertificateContext&&
    ) noexcept;
    RuntimeInternetServiceCertificateContext(
        const RuntimeInternetServiceCertificateContext&
    ) = delete;
    RuntimeInternetServiceCertificateContext& operator=(
        const RuntimeInternetServiceCertificateContext&
    ) = delete;

    const void* nativeCertificateContext() const noexcept;
    bool valid() const noexcept;
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    friend bool loadRuntimeInternetServiceCertificate(
        const RuntimeInternetServiceCertificateSelection&,
        RuntimeInternetServiceCertificateContext&,
        std::string&
    );
};

bool loadRuntimeInternetServiceCertificate(
    const RuntimeInternetServiceCertificateSelection& selection,
    RuntimeInternetServiceCertificateContext& certificate,
    std::string& errorMessage
);
