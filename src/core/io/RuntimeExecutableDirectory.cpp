#include "core/io/RuntimeExecutableDirectory.h"

#include <string>
#include <array>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#include <vector>
#endif

std::filesystem::path runtimeExecutableDirectory() {
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    while (length == buffer.size()) {
        buffer.resize(buffer.size() * 2);
        length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    }
    if (length != 0) {
        buffer.resize(length);
        return std::filesystem::path(buffer).parent_path();
    }
#elif defined(__linux__)
    // Native provider; non-Windows execution is separately required by P12.
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error && !executable.empty()) return executable.parent_path();
#elif defined(__APPLE__)
    std::uint32_t size = 0;
    (void)_NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (size != 0 && _NSGetExecutablePath(buffer.data(), &size) == 0) {
        return std::filesystem::path(buffer.data()).parent_path();
    }
#endif
    return std::filesystem::current_path();
}

std::filesystem::path runtimeVideoExecutableDirectory() {
#ifdef _WIN32
    std::array<wchar_t, 32768> path{};
    const DWORD copied = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size())
    );
    if (copied == 0 || copied >= path.size()) return {};
    return std::filesystem::path(std::wstring(path.data(), copied)).parent_path();
#else
    // S25 playback remains unsupported; no invented process-directory fallback.
    return {};
#endif
}
