#include "network/internet/RuntimeInternetServiceProcess.h"
#include "network/internet/RuntimeInternetServiceHost.h"
#include "network/internet/RuntimeInternetServiceHostConfig.h"
#include "network/internet/private/RuntimeInternetServiceConsoleControl.h"
#include "network/transport/private/RuntimeInternetTlsCertificateWin32.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace {
#include "../support/RuntimeServiceTestCertificateWin32.h"

int checks = 0;
int failures = 0;
void expect(bool ok, const char* message) {
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

std::uint64_t nowMilliseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

RuntimeInternetServiceHostConfig hostConfig(TestCertificate& certificate) {
    RuntimeInternetServiceHostConfig c;
    c.serviceId = "console-lifecycle";
    c.tls.bindScope = RuntimeInternetTlsBindScope::LoopbackIpv4;
    c.tls.serverCertificate = certificate.serverCertificate();
    c.coordinator.serviceId = c.serviceId;
    c.coordinator.udpRelayEndpoint = {"127.0.0.1", 9U};
    c.coordinator.tcpRelayEndpoint = {"127.0.0.1", 0U};
    c.coordinator.nowMilliseconds = nowMilliseconds;
    c.coordinator.secureRandomBytes = secureRandom;
    c.matchmaking.serviceId = c.serviceId;
    c.matchmaking.nowMilliseconds = nowMilliseconds;
    c.matchmaking.secureRandomBytes = secureRandom;
    c.nowMilliseconds = nowMilliseconds;
    c.verifyCredential = [](const RuntimeInternetServiceCredentialRequest&,
        RuntimeInternetServiceVerifiedIdentity&, std::string& error) {
        error = "no peer admitted by console-only fixture";
        return false;
    };
    return c;
}

class ScriptControl final : public runtime_service_console_detail::Control {
public:
    explicit ScriptControl(RuntimeInternetServiceHost& host) : host_(host) {}
    bool installOk = true;
    bool stop = true;
    int idles = 0;
    bool removedAfterClose = false;
    std::string trace;
    bool install() noexcept override { trace += 'I'; return installOk; }
    void resetStop() noexcept override { trace += 'R'; stop = false; }
    bool stopRequested() const noexcept override { return stop || idles >= 3; }
    void idleTwoMilliseconds() override {
        trace += 'W'; ++idles;
        runtime_service_console_detail::SystemControl{}.idleTwoMilliseconds();
    }
    void remove() noexcept override {
        trace += 'U'; removedAfterClose = !host_.isOpen();
    }
private:
    RuntimeInternetServiceHost& host_;
};

class ObservedSystemControl final : public runtime_service_console_detail::Control {
public:
    explicit ObservedSystemControl(RuntimeInternetServiceHost& host) : host_(host) {}
    runtime_service_console_detail::SystemControl system;
    std::atomic_bool ready = false;
    std::atomic_bool emergency = false;
    int idles = 0;
    bool installed = false;
    bool reset = false;
    bool removed = false;
    bool closedBeforeRemove = false;
    bool install() noexcept override { installed = system.install(); return installed; }
    void resetStop() noexcept override { system.resetStop(); reset = true; }
    bool stopRequested() const noexcept override {
        return system.stopRequested() || emergency.load();
    }
    void idleTwoMilliseconds() override {
        ++idles; system.idleTwoMilliseconds(); ready.store(true);
    }
    void remove() noexcept override {
        closedBeforeRemove = !host_.isOpen(); system.remove(); removed = true;
    }
private:
    RuntimeInternetServiceHost& host_;
};

void policyCases(TestCertificate& certificate,
    const RuntimeInternetServiceHostDeploymentConfig& deployment) {
    RuntimeInternetServiceHost invalid;
    expect(runRuntimeInternetServiceProcess(invalid, {}, deployment) == 4,
        "default production startup failure preserves exit4");
    expect(!invalid.isOpen(), "startup failure leaves host closed");

    std::string error;
    RuntimeInternetServiceHost registration;
    expect(registration.open(hostConfig(certificate), error), "registration case actual host open");
    ScriptControl refused(registration); refused.installOk = false;
    expect(runtime_service_console_detail::runOpenedHost(registration, deployment, refused) == 5,
        "registration failure preserves exit5");
    expect(!registration.isOpen() && refused.trace == "I" && refused.idles == 0,
        "registration failure closes before return without reset/wait/unregister");

    RuntimeInternetServiceHost closed;
    ScriptControl runtimeFailure(closed);
    expect(runtime_service_console_detail::runOpenedHost(closed, deployment, runtimeFailure) == 6,
        "actual closed-host poll preserves runtime exit6");
    expect(runtimeFailure.trace == "IRU" && runtimeFailure.idles == 0
        && runtimeFailure.removedAfterClose, "poll failure closes then unregisters without wait");

    RuntimeInternetServiceHost idle;
    expect(idle.open(hostConfig(certificate), error), "idle case actual host open");
    ScriptControl controlled(idle);
    const auto start = std::chrono::steady_clock::now();
    expect(runtime_service_console_detail::runOpenedHost(idle, deployment, controlled) == 0,
        "same original production loop normal stop exit0");
    const auto elapsed = std::chrono::steady_clock::now() - start;
    expect(controlled.trace == "IRWWWU" && controlled.idles == 3,
        "original reset-before-poll and each no-progress wait sequence");
    expect(elapsed >= std::chrono::milliseconds(6), "three actual waits retain two milliseconds");
    expect(controlled.removedAfterClose && !idle.isOpen(), "normal stop closes before unregister");
}

void nativeConsoleCases(TestCertificate& certificate,
    const RuntimeInternetServiceHostDeploymentConfig& deployment) {
    std::string error;
    runtime_service_console_detail::SystemControl flags;
    expect(flags.install(), "actual console handler install");
    flags.resetStop();
    expect(!flags.stopRequested(), "actual atomic stop initially reset");
    expect(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetCurrentProcessId()) != FALSE,
        "actual own process-group CTRL_BREAK delivery");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!flags.stopRequested() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(flags.stopRequested(), "actual asynchronous console handler sets stop atomic");
    flags.remove();

    for (int round = 0; round < 2; ++round) {
        RuntimeInternetServiceHost host;
        expect(host.open(hostConfig(certificate), error), "actual console loop host open");
        ObservedSystemControl control(host);
        std::atomic_bool sent = false;
        std::atomic_bool done = false;
        std::thread signal([&] {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            while (!control.ready.load() && !done.load()
                && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (control.ready.load()) {
                sent.store(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT,
                    GetCurrentProcessId()) != FALSE);
            }
            while (!done.load() && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (!done.load()) control.emergency.store(true);
        });
        const int result = runtime_service_console_detail::runOpenedHost(host, deployment, control);
        done.store(true); signal.join();
        expect(result == 0, "real console stop exits production loop0");
        expect(control.installed && control.reset && control.removed,
            "real install/reset/remove completed on repeated loop");
        expect(sent.load() && control.system.stopRequested() && !control.emergency.load(),
            "only real console event caused successful termination");
        expect(control.idles > 0 && control.closedBeforeRemove && !host.isOpen(),
            "actual idle wait occurred and host closed before actual unregister");
    }

    RuntimeInternetServiceHost host;
    std::atomic_bool sent = false;
    std::thread signal([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        sent.store(GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetCurrentProcessId()) != FALSE);
    });
    const auto start = std::chrono::steady_clock::now();
    const int result = runRuntimeInternetServiceProcess(host, hostConfig(certificate), deployment);
    signal.join();
    expect(result == 0 && sent.load(), "actual default public production runner stops by console");
    expect(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(250),
        "default production run did not inherit prior stop state");
    expect(!host.isOpen(), "default runner released actual listener before return");
}

int child(const std::filesystem::path& report) {
    std::ofstream log(report);
    if (!log) return 90;
    auto* oldOut = std::cout.rdbuf(log.rdbuf());
    auto* oldErr = std::cerr.rdbuf(log.rdbuf());
    TestCertificate certificate;
    std::string error;
    expect(certificate.create(error), "original shared test certificate creation");
    if (failures == 0) {
        RuntimeInternetServiceHostDeploymentConfig deployment;
        deployment.serviceId = "console-lifecycle";
        deployment.publicRelayAddress = "127.0.0.1";
        policyCases(certificate, deployment);
        nativeConsoleCases(certificate, deployment);
    }
    std::cout << "service console checks=" << checks << " failures=" << failures
              << " realCTRL_BREAK=4 repeatedLoops=2 defaultRunner=1\n";
    std::cout.flush(); std::cerr.flush();
    std::cout.rdbuf(oldOut); std::cerr.rdbuf(oldErr);
    return failures == 0 ? 0 : 1;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc == 3 && std::wstring(argv[1]) == L"--child") return child(argv[2]);
    const auto report = std::filesystem::temp_directory_path() / "service-console-child-r1.txt";
    wchar_t exe[32768]{};
    if (!GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)))) return 91;
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --child \"" + report.wstring() + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE,
            CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process)) {
        std::cerr << "own hidden-console child creation error " << GetLastError() << '\n';
        return 92;
    }
    CloseHandle(process.hThread);
    DWORD code = 93;
    const DWORD waited = WaitForSingleObject(process.hProcess, 15000U);
    if (waited == WAIT_OBJECT_0) (void)GetExitCodeProcess(process.hProcess, &code);
    else {
        // This handle belongs only to the child created immediately above.
        (void)TerminateProcess(process.hProcess, 94U);
        (void)WaitForSingleObject(process.hProcess, 5000U);
    }
    CloseHandle(process.hProcess);
    std::ifstream log(report);
    std::cout << log.rdbuf();
    return static_cast<int>(code);
}
