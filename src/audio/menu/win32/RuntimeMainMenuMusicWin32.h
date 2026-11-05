#pragma once
#include "audio/menu/RuntimeMainMenuMusicDevice.h"
#include <cstdint>
namespace concord::audio {
std::unique_ptr<MainMenuMusicDevice> createMainMenuMusicDeviceWin32();
// Private verification only. Successful calls use the real original WinMM API;
// the narrow seam injects individual failures without replacing playback.
using MainMusicNativeCommand = std::uint32_t(*)(const wchar_t*, wchar_t*, std::uint32_t);
std::unique_ptr<MainMenuMusicDevice> createMainMenuMusicDeviceWin32ForDiagnostics(MainMusicNativeCommand);
} // namespace concord::audio
