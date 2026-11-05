#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "audio/backend/win32/RuntimeLegacyWaveDeviceWin32.h"
#include "audio/backend/win32/RuntimeAsyncSoundDeviceWin32.h"
#include "runtime/session/detail/RuntimeProductionOperations.h"

namespace concord::runtime::session_detail {


std::wstring encodeRuntimeLegacyWaveDevicePath(const std::string& path) {
    // Preserve the exact P03 native-token -> UTF8 -> original wide conversion.
    // The private device invokes this after EVA preclose/before music preclose.
    return widenUtf8(concord::audio::runtimeVoiceUtf8PathFromLegacyNative(path));
}

concord::audio::RuntimeAsyncSoundDevice& runtimeAsyncSoundDevice() {
    if(runtimeAsyncSoundOverrideForDiagnostics)return *runtimeAsyncSoundOverrideForDiagnostics;
    static auto device=concord::audio::createAsyncSoundDeviceWin32();
    return *device;
}

concord::audio::RuntimeLegacyWaveDevice& runtimeEvaWaveDevice() {
    static auto device = concord::audio::createLegacyWaveDeviceWin32(
        concord::audio::RuntimeLegacyWaveChannel::Eva, encodeRuntimeLegacyWaveDevicePath);
    return *device;
}

concord::audio::RuntimeLegacyWaveDevice& runtimeShellMusicWaveDevice() {
    static auto device = concord::audio::createLegacyWaveDeviceWin32(
        concord::audio::RuntimeLegacyWaveChannel::ShellMusic, encodeRuntimeLegacyWaveDevicePath);
    return *device;
}

std::wstring widenUtf8(const std::string& value) {
    if (value.empty()) {
        return L"";
    }

    const int required = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0
    );

    if (required <= 0) {
        return std::wstring(value.begin(), value.end());
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        required
    );
    return result;
}

std::string runtimeEncodePresentationUtf8(const std::wstring& localized, const std::string& key) {
    const int byteCount = WideCharToMultiByte(
        CP_UTF8,
        0,
        localized.data(),
        static_cast<int>(localized.size()),
        nullptr,
        0,
        nullptr,
        nullptr
    );
    if (byteCount <= 0) {
        return key;
    }
    std::string utf8(static_cast<std::size_t>(byteCount), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        localized.data(),
        static_cast<int>(localized.size()),
        utf8.data(),
        byteCount,
        nullptr,
        nullptr
    );
    return utf8;
}

}
