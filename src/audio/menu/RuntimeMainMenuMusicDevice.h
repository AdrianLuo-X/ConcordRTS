#pragma once
#include "audio/menu/RuntimeMenuMusicDevice.h"
namespace concord::audio {
// Main's original volume command is independent of opening/playing. A native
// failure stays deliberately ignored; no physical gain guarantee is added.
class MainMenuMusicDevice : public MenuMusicDevice {
public:
    virtual void setVolumePercent(int percent) = 0;
};
std::unique_ptr<MainMenuMusicDevice> makeUnsupportedMainMenuMusicDevice();
} // namespace concord::audio
