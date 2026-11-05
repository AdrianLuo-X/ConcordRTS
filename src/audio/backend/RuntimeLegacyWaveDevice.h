#pragma once
#include <memory>
#include <optional>
#include <string>
namespace concord::audio {
// Presentation-only synchronous playback, on the caller's UI thread. The path
// is the existing legacy resolver token; its unchanged encoder is composed by
// the private factory. No arbitrary-Unicode promise is added to legacy Config.
enum class RuntimeLegacyWaveChannel { Eva, ShellMusic };
class RuntimeLegacyWaveDevice {
public:
    virtual ~RuntimeLegacyWaveDevice() = default;
    virtual bool supported() const noexcept = 0;
    // Replaces the channel and plays from zero. Volume is clamped to 0..100;
    // the existing backend ignores a device volume-command failure.
    virtual bool playFromStart(const std::string& path, int volumePercent) = 0;
    virtual std::optional<bool> isPlaying() = 0;
    virtual void stopAndClose() = 0;
    virtual void close() = 0;
};
std::unique_ptr<RuntimeLegacyWaveDevice> makeUnsupportedLegacyWaveDevice();
} // namespace concord::audio
