#include "audio/backend/win32/RuntimeAsyncSoundDeviceWin32.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
namespace concord::audio {
namespace {
bool nativeAsyncSound(const wchar_t* file,std::uint32_t flags){return PlaySoundW(file,nullptr,flags)!=FALSE;}
class AsyncSoundWin32 final : public RuntimeAsyncSoundDevice {
public:
    explicit AsyncSoundWin32(AsyncSoundNativeCall call):call_(call){}
    bool supported() const noexcept override {return true;}
    bool playFile(const std::wstring& nativeFilename) override {
        return call_(nativeFilename.c_str(),SND_FILENAME|SND_ASYNC|SND_NODEFAULT);
    }
    bool stopGlobal() override {return call_(nullptr,0);}
private:
    AsyncSoundNativeCall call_;
};
}
std::unique_ptr<RuntimeAsyncSoundDevice> createAsyncSoundDeviceWin32ForDiagnostics(AsyncSoundNativeCall call){
    if(!call)return makeUnsupportedAsyncSoundDevice();
    return std::make_unique<AsyncSoundWin32>(call);
}
std::unique_ptr<RuntimeAsyncSoundDevice> createAsyncSoundDeviceWin32(){return createAsyncSoundDeviceWin32ForDiagnostics(nativeAsyncSound);}
}
#else
namespace concord::audio {
std::unique_ptr<RuntimeAsyncSoundDevice> createAsyncSoundDeviceWin32(){return makeUnsupportedAsyncSoundDevice();}
std::unique_ptr<RuntimeAsyncSoundDevice> createAsyncSoundDeviceWin32ForDiagnostics(AsyncSoundNativeCall){return makeUnsupportedAsyncSoundDevice();}
}
#endif
