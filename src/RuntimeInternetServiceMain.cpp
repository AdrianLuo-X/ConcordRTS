#include "RuntimeInternetServiceHost.h"
#include "RuntimeInternetServiceHostConfig.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {

std::atomic_bool stopRequested = false;

std::uint64_t monotonicMilliseconds() noexcept {
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
    );
}

bool secureRandomBytes(std::uint8_t* destination, std::size_t size) noexcept {
#ifdef _WIN32
    if ((destination == nullptr && size != 0U)
        || size > static_cast<std::size_t>(
            (std::numeric_limits<ULONG>::max)()
        )) {
        return false;
    }
    return BCryptGenRandom(
        nullptr,
        destination,
        static_cast<ULONG>(size),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    ) == 0;
#else
    (void)destination;
    (void)size;
    return false;
#endif
}

#ifdef _WIN32
BOOL WINAPI consoleControlHandler(DWORD event) noexcept {
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        stopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

RuntimeInternetServiceHostConfig makeHostConfig(
    const RuntimeInternetServiceHostDeploymentConfig& deployment,
    const RuntimeInternetServiceCertificateContext& certificate
) {
    RuntimeInternetServiceHostConfig config;
    config.serviceId = deployment.serviceId;
    config.tls.port = deployment.listenPort;
    config.tls.bindScope = deployment.listenScope;
    config.tls.maximumConnections = runtimeInternetTlsMaximumConnections;
    config.tls.nativeCertificateContext =
        certificate.nativeCertificateContext();

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
    if (!host.open(std::move(hostConfig), error)) {
        std::cerr << "Service startup error: " << error << '\n';
        return 4;
    }

#ifdef _WIN32
    if (!SetConsoleCtrlHandler(consoleControlHandler, TRUE)) {
        std::cerr << "Service startup error: console control handler failed\n";
        host.close();
        return 5;
    }
#endif
    stopRequested.store(false, std::memory_order_relaxed);
    std::cout << "Internet service '" << deployment.serviceId
              << "' listening on TCP port " << host.port()
              << "; public relay " << deployment.publicRelayAddress
              << ':' << host.port() << "; configured peers "
              << deployment.credentials.size()
              << "; community state "
              << deployment.communityStatePath.string() << '\n';

    int exitCode = 0;
    while (!stopRequested.load(std::memory_order_relaxed)) {
        RuntimeInternetServiceHostPollResult result = host.poll();
        if (!result.succeeded) {
            std::cerr << "Service runtime error: " << result.error << '\n';
            exitCode = 6;
            break;
        }
        if (!result.progressed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    host.close();
#ifdef _WIN32
    (void)SetConsoleCtrlHandler(consoleControlHandler, FALSE);
#endif
    return exitCode;
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
