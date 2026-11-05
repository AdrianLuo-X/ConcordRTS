#pragma once
#include "audio/events/RuntimeMenuSoundDevice.h"
#include <cstdint>
namespace concord::audio {
std::unique_ptr<MenuSoundDevice> createMenuSoundDeviceWin32();
using MenuSoundNativeCommand = std::uint32_t(*)(const wchar_t*, wchar_t*, std::uint32_t);
// Private verification only: all calls except explicit injected failures are real.
std::unique_ptr<MenuSoundDevice> createMenuSoundDeviceWin32ForDiagnostics(MenuSoundNativeCommand);
}
