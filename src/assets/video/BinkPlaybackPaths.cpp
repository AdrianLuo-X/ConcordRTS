#include "assets/video/BinkPlaybackPaths.h"

#include <initializer_list>
#include <system_error>

namespace {
namespace fs = std::filesystem;

fs::path firstExisting(std::initializer_list<fs::path> paths) {
    for (const fs::path& path : paths) {
        std::error_code error;
        if (!path.empty() && fs::is_regular_file(path, error)) return path;
    }
    return {};
}
} // namespace

BinkPlaybackPaths resolveBinkPlaybackPaths(const BinkPlaybackPathInputs& inputs) {
    const auto& executableDirectory = inputs.executableDirectory;
    const auto& current = inputs.currentDirectory;
    BinkPlaybackPaths paths;
    paths.movie = inputs.requestedMovie.empty()
        ? firstExisting({
            inputs.exportRoot / "langmd" / "ra2ts_l.bik",
            inputs.exportRoot / "lang" / "ra2ts_l.bik"
        })
        : firstExisting({inputs.requestedMovie});
    paths.decoderDll = firstExisting({
        inputs.resourceRoot / "GameMD" / "Binkw32.dll",
        inputs.resourceRoot / "Game" / "Binkw32.dll"
    });
    paths.helper = firstExisting({
        executableDirectory / "BinkFrameExtractor.exe",
        executableDirectory.parent_path() / "BinkFrameExtractor.exe",
        executableDirectory.parent_path() / "bink_decoder32" / "Debug"
            / "BinkFrameExtractor.exe",
        current / "build" / "BinkFrameExtractor.exe",
        current / "BinkFrameExtractor.exe"
    });
    return paths;
}
