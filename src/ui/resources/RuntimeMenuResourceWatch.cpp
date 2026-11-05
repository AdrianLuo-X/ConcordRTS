#include "ui/resources/RuntimeMenuResourceWatch.h"
#include "runtime/config/RuntimeConfig.h"

#include <array>
#include <cstdint>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;

fs::path absolutePath(const fs::path& path) {
    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    return error ? path : absolute.lexically_normal();
}

RuntimeMenuResourcePaths configPaths(const RuntimeConfig& config) {
    // Reuse RuntimeConfig's resolved roots; do not rediscover or reorder them.
    // Like RuntimeConfigPaths, this compatibility input is ACP on Windows.
    return {fs::path(config.resourceRootPath), fs::path(config.localDataRootPath)};
}

struct FileStamp {
    fs::file_time_type modified{};
    std::uintmax_t size = 0;
    bool operator==(const FileStamp&) const = default;
};

FileStamp stamp(const fs::path& path) {
    std::error_code error;
    FileStamp result;
    result.modified = fs::last_write_time(path, error);
    if (!error) result.size = fs::file_size(path, error);
    return result;
}

constexpr std::array<const char*, 4> localInputs{
    "rulesmd.ini", "mpmodesmd.ini", "coopcampmd.ini", "soundmd.ini"
};
} // namespace

struct RuntimeMenuResourceWatch::Impl {
    fs::path resourceRoot;
    fs::path localRoot;
    std::array<FileStamp, localInputs.size()> localStamps{};
#ifdef _WIN32
    std::array<HANDLE, 2> handles{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
#endif

    explicit Impl(const RuntimeMenuResourcePaths& paths)
        : resourceRoot(absolutePath(paths.resourceRoot)),
          localRoot(absolutePath(paths.localDataRoot)) {
#ifdef _WIN32
        const std::array<fs::path, 2> roots{
            resourceRoot, localRoot / "resources" / "mix_exports"
        };
        for (std::size_t i = 0; i < roots.size(); ++i) {
            handles[i] = FindFirstChangeNotificationW(
                roots[i].c_str(), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
                    | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        }
#endif
        for (std::size_t i = 0; i < localInputs.size(); ++i) {
            localStamps[i] = stamp(localRoot / localInputs[i]);
        }
    }

    ~Impl() {
#ifdef _WIN32
        for (HANDLE handle : handles) {
            if (handle != INVALID_HANDLE_VALUE) FindCloseChangeNotification(handle);
        }
#endif
    }

    bool sameRoots(const RuntimeMenuResourcePaths& paths) const {
        return resourceRoot == absolutePath(paths.resourceRoot)
            && localRoot == absolutePath(paths.localDataRoot);
    }

    bool unchanged(const RuntimeMenuResourcePaths& paths) const {
        if (!sameRoots(paths)) return false;
#ifdef _WIN32
        for (HANDLE handle : handles) {
            if (handle == INVALID_HANDLE_VALUE
                || WaitForSingleObject(handle, 0) != WAIT_TIMEOUT) return false;
        }
#else
        // No notification backend exists here yet. Do not certify a stale tree.
        return false;
#endif
        for (std::size_t i = 0; i < localInputs.size(); ++i) {
            if (stamp(localRoot / localInputs[i]) != localStamps[i]) return false;
        }
        return true;
    }
};

RuntimeMenuResourceWatch::RuntimeMenuResourceWatch(const RuntimeConfig& config)
    : RuntimeMenuResourceWatch(configPaths(config)) {}
RuntimeMenuResourceWatch::RuntimeMenuResourceWatch(const RuntimeMenuResourcePaths& paths)
    : impl_(std::make_unique<Impl>(paths)) {}
RuntimeMenuResourceWatch::~RuntimeMenuResourceWatch() = default;
bool RuntimeMenuResourceWatch::sameRoots(const RuntimeConfig& config) const {
    return sameRoots(configPaths(config));
}
bool RuntimeMenuResourceWatch::sameRoots(const RuntimeMenuResourcePaths& paths) const {
    return impl_->sameRoots(paths);
}
bool RuntimeMenuResourceWatch::unchanged(const RuntimeConfig& config) const {
    return unchanged(configPaths(config));
}
bool RuntimeMenuResourceWatch::unchanged(const RuntimeMenuResourcePaths& paths) const {
    return impl_->unchanged(paths);
}
