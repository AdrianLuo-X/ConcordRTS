#include "audio/events/win32/RuntimeMenuSoundWin32.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <array>
namespace concord::audio {
namespace {
struct MenuSoundLease {};
struct MenuSoundSlots { std::array<std::weak_ptr<MenuSoundLease>,32> owners; };
std::shared_ptr<MenuSoundSlots> menuSoundSlots(){static const auto slots=std::make_shared<MenuSoundSlots>(); return slots;}
std::uint32_t nativeMenuSoundCommand(const wchar_t* text,wchar_t* result,std::uint32_t capacity){return mciSendStringW(text,result,static_cast<UINT>(capacity),nullptr);}
class MenuSoundWin32 final : public MenuSoundDevice {
public:
    explicit MenuSoundWin32(MenuSoundNativeCommand send):send_(send),slots_(menuSoundSlots()) {}
    ~MenuSoundWin32() override { for(std::size_t channel=0;channel<32;++channel) if(leases_[channel] && slots_->owners[channel].lock()==leases_[channel]) close(channel); }
    bool supported() const noexcept override { return true; }
    static std::wstring alias(std::size_t channel) {
        return L"concord_menu_effect_" + std::to_wstring(channel);
    }
    std::optional<bool> isPlaying(std::size_t channel) const override {
        if(channel>=32 || !canAddress(channel)) return std::nullopt;
        wchar_t status[32]{};
        const auto command = L"status " + alias(channel) + L" mode";
        if (send_(command.c_str(), status, static_cast<std::uint32_t>(std::size(status))) != 0)
            return std::nullopt;
        return std::wstring(status) == L"playing";
    }
    bool open(std::size_t channel, const std::wstring& path) override {
        if(channel>=32) return false;
        const auto lease=std::make_shared<MenuSoundLease>();
        const auto command = L"open \"" + path + L"\" type waveaudio alias " + alias(channel);
        const auto shareable = command + L" shareable";
        const bool opened = send_(shareable.c_str(), nullptr, 0) == 0
            || send_(command.c_str(), nullptr, 0) == 0;
        slots_->owners[channel].reset(); leases_[channel].reset();
        if(mciGetDeviceIDW(alias(channel).c_str())!=0){ leases_[channel]=lease; slots_->owners[channel]=lease; }
        return opened;
    }
    bool playFromStart(std::size_t channel) override {
        if(channel>=32 || !canAddress(channel)) return false;
        const auto command = L"play " + alias(channel) + L" from 0";
        return send_(command.c_str(), nullptr, 0) == 0;
    }
    void close(std::size_t channel) override {
        if(channel>=32 || !canAddress(channel)) return;
        const auto command = L"close " + alias(channel);
        const auto error=send_(command.c_str(), nullptr, 0);
        if(error==0 || mciGetDeviceIDW(alias(channel).c_str())==0){
            if(slots_->owners[channel].lock()==leases_[channel]) slots_->owners[channel].reset();
            leases_[channel].reset();
        }
    }
private:
    bool canAddress(std::size_t channel) const { const auto owner=slots_->owners[channel].lock(); return !owner || owner==leases_[channel]; }
    MenuSoundNativeCommand send_;
    std::shared_ptr<MenuSoundSlots> slots_;
    std::array<std::shared_ptr<MenuSoundLease>,32> leases_{};
};

}
std::unique_ptr<MenuSoundDevice> createMenuSoundDeviceWin32ForDiagnostics(MenuSoundNativeCommand send){
    if(!send) return makeUnsupportedMenuSoundDevice();
    return std::make_unique<MenuSoundWin32>(send);
}
std::unique_ptr<MenuSoundDevice> createMenuSoundDeviceWin32(){return createMenuSoundDeviceWin32ForDiagnostics(nativeMenuSoundCommand);}
}
#else
namespace concord::audio {
std::unique_ptr<MenuSoundDevice> createMenuSoundDeviceWin32(){return makeUnsupportedMenuSoundDevice();}
std::unique_ptr<MenuSoundDevice> createMenuSoundDeviceWin32ForDiagnostics(MenuSoundNativeCommand){return makeUnsupportedMenuSoundDevice();}
}
#endif
