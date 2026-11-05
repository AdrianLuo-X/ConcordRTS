#include "runtime/config/RuntimeConfigPaths.h"

#include <system_error>
#include <vector>

namespace {
namespace fs = std::filesystem;

bool pathExists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error);
}

bool isProjectRoot(const fs::path& path) {
    return pathExists(path / "local_data")
        && pathExists(path / "CMakeLists.txt")
        && (pathExists(path / "RedAlert2") || pathExists(path / ".git"));
}

fs::path normalized(const fs::path& path) {
    std::error_code error;
    const fs::path canonical = fs::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

void addAncestors(std::vector<fs::path>& candidates, fs::path path) {
    if (path.empty()) return;
    path = normalized(path);
    while (!path.empty()) {
        candidates.push_back(path);
        const fs::path parent = path.parent_path();
        if (parent.empty() || parent == path) break;
        path = parent;
    }
}
} // namespace

std::filesystem::path runtimeConfigProjectRoot(
    const std::filesystem::path& currentDirectory,
    const std::filesystem::path& executableDirectory
) {
    std::vector<fs::path> candidates;
    addAncestors(candidates, currentDirectory);
    addAncestors(candidates, executableDirectory);
    for (const fs::path& candidate : candidates) {
        if (isProjectRoot(candidate)) return candidate;
    }
    if (!candidates.empty()) return candidates.front();
    return fs::current_path();
}

std::filesystem::path runtimeResolveConfigPath(
    const std::filesystem::path& value,
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& currentDirectory
) {
    if (value.empty()) return {};
    if (value.is_absolute()) return normalized(value);
    const fs::path currentCandidate = currentDirectory / value;
    if (pathExists(currentCandidate)) return normalized(currentCandidate);
    return normalized(projectRoot / value);
}

std::string runtimeConfigLegacyPathString(const std::filesystem::path& path) {
    return path.generic_string();
}
