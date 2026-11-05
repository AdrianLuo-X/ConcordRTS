#include "audio/events/RuntimeMenuSoundDevice.h"
#include "audio/events/RuntimeMenuSoundPool.h"
#include <iostream>
int main(){auto d=concord::audio::makeUnsupportedMenuSoundDevice();int checks=0,fail=0;auto check=[&](bool x){++checks;if(!x)++fail;};check(!d->supported());for(auto n:{std::size_t{0},std::size_t{31},std::size_t{32}}){check(!d->isPlaying(n).has_value());check(!d->open(n,L"missing.wav"));check(!d->playFromStart(n));d->close(n);}std::cout<<"MenuSound value: checks="<<checks<<" failures="<<fail<<" skipped=0 unsupported=honest"<<std::endl;return fail?1:0;}
