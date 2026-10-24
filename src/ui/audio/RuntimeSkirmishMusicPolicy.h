#pragma once
#include "audio/menu/RuntimeMenuMusicDevice.h"
#include "ui/time/RuntimeUiClock.h"
namespace concord::ui {
// Pure presentation policy, not an authoritative clock or physical device.
// The immutable clock outlives this borrow; probe is an ordinary static function.
// The production caller supplies its exact existing regularFile implementation.
class SkirmishMusicPolicy {
public:
    using FileProbe = bool(*)(const std::filesystem::path&);
    SkirmishMusicPolicy(const RuntimeUiClock& clock,FileProbe probe) noexcept
        : clock_(clock),probe_(probe) {}
    void start(const std::filesystem::path&,bool enabled,concord::audio::MenuMusicDevice&);
    void poll(bool enabled,concord::audio::MenuMusicDevice&);
    void stop(concord::audio::MenuMusicDevice&);
    bool isOpen() const noexcept { return open_; }
    const std::filesystem::path& path() const noexcept { return path_; }
    RuntimeUiTimeMs nextProbe() const noexcept { return next_; }
private:
    const RuntimeUiClock& clock_;
    FileProbe probe_;
    bool open_ = false;
    std::filesystem::path path_;
    RuntimeUiTimeMs next_ = 0;
};
} // namespace concord::ui
