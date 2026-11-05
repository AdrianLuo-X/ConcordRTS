#pragma once
#include <filesystem>
#include <string>
namespace concord::audio {
// The existing resolver/pitch cache returns native-narrow path labels (ACP on
// Windows). Voice open/queue consume UTF-8. Convert only at that device boundary;
// do not reinterpret logical sound names or change the legacy configuration API.
inline std::string runtimeVoiceUtf8PathFromLegacyNative(const std::string& path) {
    const auto bytes = std::filesystem::path(path).u8string();
    return {bytes.begin(), bytes.end()};
}
} // namespace concord::audio
