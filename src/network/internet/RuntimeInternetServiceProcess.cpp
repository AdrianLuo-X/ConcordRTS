#include "network/internet/RuntimeInternetServiceProcess.h"
#include "network/internet/private/RuntimeInternetServiceConsoleControl.h"
#include "network/internet/RuntimeInternetServiceHost.h"
#include "network/internet/RuntimeInternetServiceHostConfig.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
std::atomic_bool stopRequested = false;
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

} // namespace

bool runtime_service_console_detail::SystemControl::install() noexcept {
#ifdef _WIN32
    return SetConsoleCtrlHandler(consoleControlHandler, TRUE) != FALSE;
#else
    return false;
#endif
}

void runtime_service_console_detail::SystemControl::resetStop() noexcept {
    ::stopRequested.store(false, std::memory_order_relaxed);
}

bool runtime_service_console_detail::SystemControl::stopRequested() const noexcept {
    return ::stopRequested.load(std::memory_order_relaxed);
}

void runtime_service_console_detail::SystemControl::idleTwoMilliseconds() {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

void runtime_service_console_detail::SystemControl::remove() noexcept {
#ifdef _WIN32
    (void)SetConsoleCtrlHandler(consoleControlHandler, FALSE);
#endif
}

int runtime_service_console_detail::runOpenedHost(
    RuntimeInternetServiceHost& host,
    const RuntimeInternetServiceHostDeploymentConfig& deployment,
    Control& control
) {
    if (!control.install()) {
        std::cerr << "Service startup error: console control handler failed\n";
        host.close();
        return 5;
    }
    control.resetStop();
    std::cout << "Internet service '" << deployment.serviceId
              << "' listening on TCP port " << host.port()
              << "; public relay " << deployment.publicRelayAddress
              << ':' << host.port() << "; configured peers "
              << deployment.credentials.size()
              << "; community state "
              << deployment.communityStatePath.string() << '\n';

    int exitCode = 0;
    while (!control.stopRequested()) {
        RuntimeInternetServiceHostPollResult result = host.poll();
        if (!result.succeeded) {
            std::cerr << "Service runtime error: " << result.error << '\n';
            exitCode = 6;
            break;
        }
        if (!result.progressed) {
            control.idleTwoMilliseconds();
        }
    }
    host.close();
    control.remove();
    return exitCode;
}

int runRuntimeInternetServiceProcess(
    RuntimeInternetServiceHost& host,
    RuntimeInternetServiceHostConfig config,
    const RuntimeInternetServiceHostDeploymentConfig& deployment
) {
#ifdef _WIN32
    std::string error;
    if (!host.open(std::move(config), error)) {
        std::cerr << "Service startup error: " << error << '\n';
        return 4;
    }

    runtime_service_console_detail::SystemControl control;
    return runtime_service_console_detail::runOpenedHost(host, deployment, control);
#else
    (void)host;
    (void)config;
    (void)deployment;
    std::cerr << "The Internet service host requires Windows Schannel.\n";
    return 1;
#endif
}
