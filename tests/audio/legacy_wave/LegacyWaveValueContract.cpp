#include "audio/backend/RuntimeLegacyWaveDevice.h"
#include <iostream>
int main() {
    unsigned checks = 0, failures = 0;
    const auto expect = [&](bool value) { ++checks; if (!value) ++failures; };
    auto device = concord::audio::makeUnsupportedLegacyWaveDevice();
    expect(bool(device)); expect(!device->supported());
    for (const auto* path : {"", "missing.wav", "literal-wave-token"}) for (int volume : {-100, 0, 70, 500}) {
        expect(!device->playFromStart(path, volume)); expect(!device->isPlaying().has_value());
        device->close(); device->stopAndClose(); expect(!device->supported());
    }
    std::cout << "Legacy wave unsupported value: checks=" << checks << " failures=" << failures << " skipped=0 nativePlaybackCredit=0\n";
    return failures ? 1 : 0;
}
