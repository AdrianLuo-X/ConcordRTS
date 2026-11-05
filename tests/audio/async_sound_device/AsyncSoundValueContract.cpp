#include "audio/backend/RuntimeAsyncSoundDevice.h"
#include <iostream>
int main(){auto d=concord::audio::makeUnsupportedAsyncSoundDevice();int checks=0,failures=0;auto check=[&](bool x){++checks;if(!x)++failures;};check(!d->supported());check(!d->playFile(L"missing.wav"));check(!d->stopGlobal());d.reset();std::cout<<"AsyncSound value: checks="<<checks<<" failures="<<failures<<" skipped=0 unsupported=honest"<<std::endl;return failures?1:0;}
