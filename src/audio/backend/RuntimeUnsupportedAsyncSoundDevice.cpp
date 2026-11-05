#include "audio/backend/RuntimeAsyncSoundDevice.h"
namespace concord::audio {
namespace {
class Unsupported final : public RuntimeAsyncSoundDevice {
public:
    bool supported() const noexcept override {return false;}
    bool playFile(const std::wstring&) override {return false;}
    bool stopGlobal() override {return false;}
};
}
std::unique_ptr<RuntimeAsyncSoundDevice> makeUnsupportedAsyncSoundDevice(){return std::make_unique<Unsupported>();}
}
