#pragma once
// Diagnostic file precondition only. No saving, deletion, message pumping or game-clock reads.
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <ostream>
#include <thread>

namespace RuntimeInputPreferenceFixture {
struct Readiness {
    bool ready = false;
    unsigned probes = 0;
    unsigned denials = 0;
    DWORD lastError = 0;
    long long elapsedMs = 0;
};
inline Readiness wait(const std::filesystem::path& file,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
    const DWORD callerError = GetLastError();
    const auto start = std::chrono::steady_clock::now();
    auto stableSince = start;
    bool stable = false;
    Readiness result;
    for (;;) {
        ++result.probes;
        const HANDLE handle = CreateFileW(file.c_str(), DELETE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const auto now = std::chrono::steady_clock::now();
        if (handle == INVALID_HANDLE_VALUE) {
            result.lastError = GetLastError();
            ++result.denials;
            stable = false;
        } else {
            if (!CloseHandle(handle)) {
                result.lastError = GetLastError();
                result.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
                SetLastError(callerError);
                return result;
            }
            if (!stable) { stable = true; stableSince = now; }
            if (now - stableSince >= std::chrono::milliseconds(200)) {
                result.ready = true;
            }
        }
        result.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (result.ready || now - start >= timeout) {
            SetLastError(callerError);
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
inline bool check(const std::filesystem::path& file, std::ostream& trace, const std::string& label) {
    const auto result = wait(file);
    trace << "preferenceFixtureReady label=" << label << " ready=" << result.ready
          << " probes=" << result.probes << " denials=" << result.denials
          << " lastError=" << result.lastError << " elapsedMs=" << result.elapsedMs << '\n';
    return result.ready;
}
}
