#pragma once
// Private composition factory, not a native type in the policy/device port.
#include "audio/menu/RuntimeMenuMusicDevice.h"
#include <string>
namespace concord::audio {
// Reuse the native UI owner's exact UTF8/ACP fallback conversion, without a
// duplicate path/encoding implementation or capturing any application state.
using MenuMusicPathEncoder = std::wstring(*)(const std::filesystem::path&);
std::unique_ptr<MenuMusicDevice> createSkirmishMusicDeviceWin32(MenuMusicPathEncoder);
} // namespace concord::audio
