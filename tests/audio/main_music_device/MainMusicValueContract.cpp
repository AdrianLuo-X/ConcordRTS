#include "audio/menu/RuntimeMainMenuMusicDevice.h"
#include <array>
#include <iostream>
#include <limits>
#include <type_traits>
using namespace concord::audio;
static_assert(std::is_base_of_v<MenuMusicDevice,MainMenuMusicDevice>);
int main(){unsigned checks=0,failures=0;const auto expect=[&](bool ok){++checks;if(!ok)++failures;};auto d=makeUnsupportedMainMenuMusicDevice();expect(bool(d));expect(!d->supported());for(int volume:std::array<int,7>{std::numeric_limits<int>::min(),-1,0,1,100,101,std::numeric_limits<int>::max()}){expect(!d->open(std::filesystem::path("no-device-no-file.wav")));d->setVolumePercent(volume);expect(!d->playFromStart());expect(!d->isPlaying().has_value());d->stop();d->close();expect(!d->supported());}std::cout<<"Main music value: checks="<<checks<<" failures="<<failures<<" skipped=0 existingBase=yes unsupportedPlayback=false fileLookup=none\n";return failures?1:0;}
