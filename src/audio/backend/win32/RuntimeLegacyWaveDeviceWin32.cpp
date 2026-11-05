#include "audio/backend/win32/RuntimeLegacyWaveDeviceWin32.h"
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
struct WaveLease {};
struct WaveSlot { std::weak_ptr<WaveLease> owner; };
std::shared_ptr<WaveSlot> waveSlot(RuntimeLegacyWaveChannel channel) {
    // The old aliases are process-local. Strong slot retention makes the
    // channel's destruction independent of static factory destruction order.
    static const auto eva = std::make_shared<WaveSlot>();
    static const auto music = std::make_shared<WaveSlot>();
    return channel == RuntimeLegacyWaveChannel::Eva ? eva : music;
}
std::uint32_t nativeCommand(const wchar_t* command, wchar_t* output, std::uint32_t capacity) {
    return mciSendStringW(command, output, static_cast<UINT>(capacity), nullptr);
}
class LegacyWaveDeviceWin32 final : public RuntimeLegacyWaveDevice {
public:
    LegacyWaveDeviceWin32(RuntimeLegacyWaveChannel channel, LegacyWavePathEncoder encode,
                         LegacyWaveNativeCommand send)
        : channel_(channel), encode_(encode), send_(send), slot_(waveSlot(channel)),
          alias_(channel == RuntimeLegacyWaveChannel::Eva ? L"concord_runtime_eva" : L"concord_runtime_music") {}
    ~LegacyWaveDeviceWin32() override {
        if (owns_ && slot_->owner.lock() == lease_) stopAndClose();
    }
    bool supported() const noexcept override { return true; }
    bool playFromStart(const std::string& path, int volumePercent) override {
        const auto attempt = std::make_shared<WaveLease>();
        // Preserve the two original orders: EVA stop/close then encode;
        // Shell music encode then close, without an extra pre-open stop.
        if (channel_ == RuntimeLegacyWaveChannel::Eva) forceClose(true);
        const auto widePath = encode_(path);
        if (channel_ == RuntimeLegacyWaveChannel::ShellMusic) forceClose(false);
        const auto open = L"open \"" + widePath + L"\" type waveaudio alias " + alias_ + L" shareable";
        auto openError = send_(open.c_str(), nullptr, 0);
        if (openError != 0) {
            const auto fallback = L"open \"" + widePath + L"\" type waveaudio alias " + alias_;
            openError = send_(fallback.c_str(), nullptr, 0);
        }
        if (openError == 0) {
            const auto volume = L"setaudio " + alias_ + L" volume to "
                + std::to_wstring(std::clamp(volumePercent, 0, 100) * 10);
            (void)send_(volume.c_str(), nullptr, 0); // Original ignored result.
        }
        const auto play = L"play " + alias_ + L" from 0";
        const auto playError = openError == 0 ? send_(play.c_str(), nullptr, 0) : openError;
        const bool played = openError == 0 && playError == 0;
        if (!played) forceClose(channel_ == RuntimeLegacyWaveChannel::Eva);
        slot_->owner.reset(); owns_ = false; lease_.reset();
        if (mciGetDeviceIDW(alias_.c_str()) != 0) {
            lease_ = attempt; slot_->owner = lease_; owns_ = true;
        }
        return played;
    }
    std::optional<bool> isPlaying() override {
        if (!canAddress()) return std::nullopt;
        std::array<wchar_t, 64> status{};
        const auto capacity = channel_ == RuntimeLegacyWaveChannel::Eva ? 32U : 64U;
        const auto command = L"status " + alias_ + L" mode";
        if (send_(command.c_str(), status.data(), capacity) != 0) return std::nullopt;
        return std::wstring(status.data()) == L"playing";
    }
    void stopAndClose() override {
        if (canAddress()) {
            const auto command = L"stop " + alias_; (void)send_(command.c_str(), nullptr, 0);
        }
        close();
    }
    void close() override {
        if (!canAddress()) { owns_ = false; lease_.reset(); return; }
        const auto command = L"close " + alias_;
        const auto error = send_(command.c_str(), nullptr, 0);
        if (error == 0 || mciGetDeviceIDW(alias_.c_str()) == 0) {
            if (slot_->owner.lock() == lease_) slot_->owner.reset();
            owns_ = false; lease_.reset();
        }
        // A failed real close retains its lease for explicit close or the
        // destructor. A superseded owner never closes its replacement.
    }
private:
    bool canAddress() const {
        const auto owner = slot_->owner.lock(); return !owner || owner == lease_;
    }
    void forceClose(bool stop) {
        if (stop) { const auto command = L"stop " + alias_; (void)send_(command.c_str(), nullptr, 0); }
        const auto command = L"close " + alias_; (void)send_(command.c_str(), nullptr, 0);
    }
    RuntimeLegacyWaveChannel channel_;
    LegacyWavePathEncoder encode_;
    LegacyWaveNativeCommand send_;
    std::shared_ptr<WaveSlot> slot_;
    std::shared_ptr<WaveLease> lease_;
    std::wstring alias_;
    bool owns_ = false;
};
}
std::unique_ptr<RuntimeLegacyWaveDevice> createLegacyWaveDeviceWin32ForDiagnostics(
    RuntimeLegacyWaveChannel channel, LegacyWavePathEncoder encode, LegacyWaveNativeCommand send) {
    if (!encode || !send || (channel != RuntimeLegacyWaveChannel::Eva && channel != RuntimeLegacyWaveChannel::ShellMusic))
        return makeUnsupportedLegacyWaveDevice();
    return std::make_unique<LegacyWaveDeviceWin32>(channel, encode, send);
}
std::unique_ptr<RuntimeLegacyWaveDevice> createLegacyWaveDeviceWin32(
    RuntimeLegacyWaveChannel channel, LegacyWavePathEncoder encode) {
    return createLegacyWaveDeviceWin32ForDiagnostics(channel, encode, nativeCommand);
}
} // namespace concord::audio
#else
namespace concord::audio {
std::unique_ptr<RuntimeLegacyWaveDevice> createLegacyWaveDeviceWin32(RuntimeLegacyWaveChannel, LegacyWavePathEncoder) {
    return makeUnsupportedLegacyWaveDevice();
}
std::unique_ptr<RuntimeLegacyWaveDevice> createLegacyWaveDeviceWin32ForDiagnostics(
    RuntimeLegacyWaveChannel, LegacyWavePathEncoder, LegacyWaveNativeCommand) {
    return makeUnsupportedLegacyWaveDevice();
}
}
#endif
