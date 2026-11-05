#pragma once
#include "audio/backend/RuntimeAsyncSoundDevice.h"
#include <cstdint>
namespace concord::audio {
std::unique_ptr<RuntimeAsyncSoundDevice> createAsyncSoundDeviceWin32();
using AsyncSoundNativeCall = bool(*)(const wchar_t*,std::uint32_t);
// Private diagnostics forwards real API calls except specifically requested errors.
std::unique_ptr<RuntimeAsyncSoundDevice> createAsyncSoundDeviceWin32ForDiagnostics(AsyncSoundNativeCall);
}
