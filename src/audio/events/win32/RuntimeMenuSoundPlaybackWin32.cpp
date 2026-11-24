#include "audio/events/RuntimeMenuSoundPlayback.h"

#include "audio/events/RuntimeMenuSoundPool.h"
#include "audio/events/win32/RuntimeMenuSoundWin32.h"
#include "runtime/host/RuntimeUiClockWin32.h"

#include <memory>

namespace {

std::unique_ptr<concord::audio::MenuSoundDevice> makeRuntimeMenuSoundDevice() {
    return concord::audio::createMenuSoundDeviceWin32();
}

#include "audio/events/RuntimeMenuSoundMci.inl"

} // namespace

namespace concord::audio {

void playSharedMenuSound(const std::wstring& path) {
    runtimeMenuSoundPlayback().play(path);
}

void collectSharedMenuSounds() {
    runtimeMenuSoundPlayback().collect();
}

} // namespace concord::audio
