#include "audio/menu/win32/RuntimeSkirmishMusicWin32.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <utility>
namespace concord::audio {
namespace {
constexpr const wchar_t* alias=L"concord_skirmish_intro";
struct MusicLease {};
struct AliasSlot { std::weak_ptr<MusicLease> owner; };
std::shared_ptr<AliasSlot> skirmishAliasSlot() {
    // Existing process-local alias, private to this native capability.
    // Every instance retains the slot, so global destruction order is safe.
    // Only UI-thread operations mutate owner; unused worker instances do not.
    static const auto slot=std::make_shared<AliasSlot>();
    return slot;
}
class SkirmishMusicWin32 final : public MenuMusicDevice {
public:
    explicit SkirmishMusicWin32(MenuMusicPathEncoder encode)
        : encode_(encode),slot_(skirmishAliasSlot()) {}
    ~SkirmishMusicWin32() override {
        if(owns_ && slot_->owner.lock()==lease_) { stop();close(); }
    }
    bool supported() const noexcept override { return true; }
    bool open(const std::filesystem::path& path) override {
        const auto widePath=encode_(path);
        const auto command=L"open \""+widePath+L"\" type waveaudio alias concord_skirmish_intro";
        const auto attempt=std::make_shared<MusicLease>();
        // Preserve original pre-open close and exact command strings. An
        // explicit start replaces this one process-local music slot.
        mciSendStringW(L"close concord_skirmish_intro",nullptr,0,nullptr);
        const auto error=mciSendStringW(command.c_str(),nullptr,0,nullptr);
        slot_->owner.reset();owns_=false;lease_.reset();
        if(error==0 || mciGetDeviceIDW(alias)!=0) {
            lease_=attempt;slot_->owner=lease_;owns_=true;
        }
        return error==0;
    }
    bool playFromStart() override {
        if(!canAddress())return false;
        return mciSendStringW(L"play concord_skirmish_intro from 0",nullptr,0,nullptr)==0;
    }
    std::optional<bool> isPlaying() override {
        if(!canAddress())return std::nullopt;
        wchar_t status[32]{};
        const auto error=mciSendStringW(L"status concord_skirmish_intro mode",status,32,nullptr);
        if(error!=0)return std::nullopt;
        return std::wstring(status)==L"playing";
    }
    void stop() override {
        if(canAddress())mciSendStringW(L"stop concord_skirmish_intro",nullptr,0,nullptr);
    }
    void close() override {
        if(!canAddress()){owns_=false;lease_.reset();return;}
        const auto error=mciSendStringW(L"close concord_skirmish_intro",nullptr,0,nullptr);
        if(error==0 || mciGetDeviceIDW(alias)==0) {
            if(slot_->owner.lock()==lease_)slot_->owner.reset();
            owns_=false;lease_.reset();
        }
        // If an actual close fails while the device still exists, keep its
        // lease for the next close/destructor attempt instead of losing it.
    }
private:
    bool canAddress() const {
        const auto current=slot_->owner.lock();
        return !current || current==lease_;
    }
    MenuMusicPathEncoder encode_;
    std::shared_ptr<AliasSlot> slot_;
    std::shared_ptr<MusicLease> lease_;
    bool owns_=false;
};
} // namespace
std::unique_ptr<MenuMusicDevice> createSkirmishMusicDeviceWin32(MenuMusicPathEncoder encode) {
    if(!encode)return makeUnsupportedMenuMusicDevice();
    return std::make_unique<SkirmishMusicWin32>(encode);
}
} // namespace concord::audio
#else
namespace concord::audio {
std::unique_ptr<MenuMusicDevice> createSkirmishMusicDeviceWin32(MenuMusicPathEncoder) {
    return makeUnsupportedMenuMusicDevice();
}
}
#endif
