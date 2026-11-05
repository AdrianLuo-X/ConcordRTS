#include "audio/menu/RuntimeCampaignMapAudioDevice.h"
#include <array>
#include <iostream>
#include <limits>
using namespace concord::audio;
int main(){unsigned checks=0,failures=0;const auto require=[&](bool ok){++checks;if(!ok)++failures;};auto d=makeUnsupportedCampaignMapAudioDevice();require(bool(d));require(!d->supported());for(int volume:std::array<int,7>{std::numeric_limits<int>::min(),-1,0,1,100,101,std::numeric_limits<int>::max()}){require(!d->play(std::filesystem::path("no-file-no-device.wav"),volume));d->stopAndClose();require(!d->supported());}std::cout<<"Campaign map audio value: checks="<<checks<<" failures="<<failures<<" skipped=0 unsupportedPlayback=false fileLookup=none\n";return failures?1:0;}
