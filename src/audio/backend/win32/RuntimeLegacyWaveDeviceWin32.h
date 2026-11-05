#pragma once
// Private composition only; the public playback port has no native types.
#include "audio/backend/RuntimeLegacyWaveDevice.h"
#include <cstdint>
namespace concord::audio {
using LegacyWavePathEncoder = std::wstring(*)(const std::string&);
std::unique_ptr<RuntimeLegacyWaveDevice> createLegacyWaveDeviceWin32(
    RuntimeLegacyWaveChannel, LegacyWavePathEncoder);
// Private synchronous verification seam. Successful operations must be
// forwarded to real MCI; a test may inject a specified failing operation.
using LegacyWaveNativeCommand = std::uint32_t(*)(const wchar_t*, wchar_t*, std::uint32_t);
std::unique_ptr<RuntimeLegacyWaveDevice> createLegacyWaveDeviceWin32ForDiagnostics(
    RuntimeLegacyWaveChannel, LegacyWavePathEncoder, LegacyWaveNativeCommand);
} // namespace concord::audio
