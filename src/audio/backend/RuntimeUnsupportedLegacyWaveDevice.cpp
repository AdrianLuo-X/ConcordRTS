#include "audio/backend/RuntimeLegacyWaveDevice.h"
namespace concord::audio {
namespace {
class UnsupportedLegacyWaveDevice final : public RuntimeLegacyWaveDevice {
public:
    bool supported() const noexcept override { return false; }
    bool playFromStart(const std::string&, int) override { return false; }
    std::optional<bool> isPlaying() override { return std::nullopt; }
    void stopAndClose() override {}
    void close() override {}
};
}
std::unique_ptr<RuntimeLegacyWaveDevice> makeUnsupportedLegacyWaveDevice() {
    return std::make_unique<UnsupportedLegacyWaveDevice>();
}
} // namespace concord::audio
