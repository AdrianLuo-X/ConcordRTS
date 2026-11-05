#pragma once
#include <memory>
#include <string>
namespace concord::audio {
// Same process-global async sound as the original PlaySound calls. Instances
// do not own a private channel; destruction never stops another caller.
class RuntimeAsyncSoundDevice {
public:
    virtual ~RuntimeAsyncSoundDevice() = default;
    virtual bool supported() const noexcept = 0;
    virtual bool playFile(const std::wstring& nativeFilename) = 0;
    virtual bool stopGlobal() = 0;
};
std::unique_ptr<RuntimeAsyncSoundDevice> makeUnsupportedAsyncSoundDevice();
}
