#include "audio/menu/win32/RuntimeMainMenuMusicWin32.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
namespace concord::audio {
namespace {
struct MainMusicLease {};
struct MainMusicSlot { std::weak_ptr<MainMusicLease> owner; };
std::shared_ptr<MainMusicSlot> mainMusicSlot() {
    static const auto slot = std::make_shared<MainMusicSlot>();
    return slot;
}
std::uint32_t nativeMainMusicCommand(const wchar_t* command, wchar_t* result, std::uint32_t capacity) {
    return mciSendStringW(command, result, static_cast<UINT>(capacity), nullptr);
}
class MainMenuMusicWin32 final : public MainMenuMusicDevice {
public:
    explicit MainMenuMusicWin32(MainMusicNativeCommand send) : send_(send), slot_(mainMusicSlot()) {}
    ~MainMenuMusicWin32() override {
        if (owns_ && slot_->owner.lock() == lease_) { stop(); close(); }
    }
    bool supported() const noexcept override { return true; }
    bool open(const std::filesystem::path& path) override {
        const auto attempt = std::make_shared<MainMusicLease>();
        // Original Main order: preclose, path.native(), open without shareable.
        (void)send_(L"close concord_runtime_intro", nullptr, 0);
        const std::wstring command = L"open \"" + path.native()
            + L"\" type waveaudio alias concord_runtime_intro";
        const auto error = send_(command.c_str(), nullptr, 0);
        slot_->owner.reset(); lease_.reset(); owns_ = false;
        if (mciGetDeviceIDW(L"concord_runtime_intro") != 0) {
            lease_ = attempt; slot_->owner = lease_; owns_ = true;
        }
        return error == 0;
    }
    void setVolumePercent(int percent) override {
        if (!canAddress()) return;
        const std::wstring command = L"setaudio concord_runtime_intro volume to "
            + std::to_wstring(std::clamp(percent, 0, 100) * 10);
        (void)send_(command.c_str(), nullptr, 0);
    }
    bool playFromStart() override {
        return canAddress() && send_(L"play concord_runtime_intro from 0", nullptr, 0) == 0;
    }
    std::optional<bool> isPlaying() override {
        if (!canAddress()) return std::nullopt;
        std::array<wchar_t, 32> status{};
        if (send_(L"status concord_runtime_intro mode", status.data(), 32) != 0) return std::nullopt;
        return std::wstring(status.data()) == L"playing";
    }
    void stop() override {
        if (canAddress()) (void)send_(L"stop concord_runtime_intro", nullptr, 0);
    }
    void close() override {
        if (!canAddress()) { owns_ = false; lease_.reset(); return; }
        const auto error = send_(L"close concord_runtime_intro", nullptr, 0);
        if (error == 0 || mciGetDeviceIDW(L"concord_runtime_intro") == 0) {
            if (slot_->owner.lock() == lease_) slot_->owner.reset();
            owns_ = false; lease_.reset();
        }
        // Retain an actual failed close for the caller/destructor to retry;
        // superseded and unopened objects never close a newer owner.
    }
private:
    bool canAddress() const { const auto owner = slot_->owner.lock(); return !owner || owner == lease_; }
    MainMusicNativeCommand send_;
    std::shared_ptr<MainMusicSlot> slot_;
    std::shared_ptr<MainMusicLease> lease_;
    bool owns_ = false;
};
}
std::unique_ptr<MainMenuMusicDevice> createMainMenuMusicDeviceWin32ForDiagnostics(MainMusicNativeCommand send) {
    if (!send) return makeUnsupportedMainMenuMusicDevice();
    return std::make_unique<MainMenuMusicWin32>(send);
}
std::unique_ptr<MainMenuMusicDevice> createMainMenuMusicDeviceWin32() {
    return createMainMenuMusicDeviceWin32ForDiagnostics(nativeMainMusicCommand);
}
} // namespace concord::audio
#else
namespace concord::audio {
std::unique_ptr<MainMenuMusicDevice> createMainMenuMusicDeviceWin32() { return makeUnsupportedMainMenuMusicDevice(); }
std::unique_ptr<MainMenuMusicDevice> createMainMenuMusicDeviceWin32ForDiagnostics(MainMusicNativeCommand) { return makeUnsupportedMainMenuMusicDevice(); }
}
#endif
