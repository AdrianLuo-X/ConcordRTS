#include "network/internet/RuntimeInternetServiceProcess.h"
#include "network/internet/private/RuntimeInternetServiceRandomBytes.h"
#include "network/internet/RuntimeInternetServiceHost.h"
#include "network/internet/RuntimeInternetServiceHostConfig.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

namespace {

std::uint64_t monotonicMilliseconds() noexcept {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
    );
}

bool secureRandomBytes(std::uint8_t* destination, std::size_t size) noexcept {
    return runtime_service_random_detail::fillDefault(destination, size);
}

RuntimeInternetServiceHostConfig makeHostConfig(
    const RuntimeInternetServiceHostDeploymentConfig& deployment,
    const RuntimeInternetServiceCertificateContext& certificate
) {
    RuntimeInternetServiceHostConfig config;
    config.serviceId = deployment.serviceId;
    config.tls.port = deployment.listenPort;
    config.tls.bindScope = deployment.listenScope;
    config.tls.maximumConnections = runtimeInternetTlsMaximumConnections;
    config.tls.serverCertificate =
        certificate.serverCertificate();

    config.coordinator.serviceId = deployment.serviceId;
    // This process deliberately has no UDP listener. A nonzero sentinel is
    // required by the transport-independent coordinator configuration, while
    // the service host rejects every UDP relay request before advertisement.
    config.coordinator.udpRelayEndpoint = {"127.0.0.1", 9U};
    config.coordinator.tcpRelayEndpoint = {
        deployment.publicRelayAddress,
        deployment.listenPort
    };
    config.coordinator.nowMilliseconds = monotonicMilliseconds;
    config.coordinator.secureRandomBytes = secureRandomBytes;

    config.matchmaking.serviceId = deployment.serviceId;
    config.matchmaking.nowMilliseconds = monotonicMilliseconds;
    config.matchmaking.secureRandomBytes = secureRandomBytes;

    config.community.serviceId = deployment.serviceId;
    config.community.statePath = deployment.communityStatePath;
    config.community.nowMilliseconds = monotonicMilliseconds;

    config.nowMilliseconds = monotonicMilliseconds;
    config.verifyCredential = deployment.credentials.makeVerifier(
        deployment.serviceId
    );
    return config;
}

int runService(const std::filesystem::path& configPath) {
    RuntimeInternetServiceHostDeploymentConfig deployment;
    std::string error;
    if (!loadRuntimeInternetServiceHostConfig(
            configPath,
            deployment,
            error
        )) {
        std::cerr << "Configuration error: " << error << '\n';
        return 2;
    }

    RuntimeInternetServiceCertificateContext certificate;
    if (!loadRuntimeInternetServiceCertificate(
            deployment.certificate,
            certificate,
            error
        )) {
        std::cerr << "Certificate error: " << error << '\n';
        return 3;
    }

    RuntimeInternetServiceHost host;
    RuntimeInternetServiceHostConfig hostConfig = makeHostConfig(
        deployment,
        certificate
    );
    return runRuntimeInternetServiceProcess(
        host, std::move(hostConfig), deployment
    );
}

} // namespace

#ifdef _WIN32
int wmain(int argumentCount, wchar_t* arguments[]) {
    if (argumentCount != 3 || std::wstring(arguments[1]) != L"--config") {
        std::cerr
            << "Usage: concord_internet_service --config <absolute-or-relative-path>\n";
        return 1;
    }
    return runService(std::filesystem::path(arguments[2]));
}
#else
int main(int, char**) {
    std::cerr << "The Internet service host requires Windows Schannel.\n";
    return 1;
}
#endif
