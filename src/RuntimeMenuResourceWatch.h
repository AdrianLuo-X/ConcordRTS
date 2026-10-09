#pragma once

#include "RuntimeShell.h"

#include <windows.h>

#include <array>
#include <filesystem>
#include <string>

// A menu may reuse decoded resources while its input trees are unchanged.
// Directory notifications avoid rescanning hundreds of maps on the UI thread.
// Preferences are deliberately excluded: they are reapplied on every entry.
class RuntimeMenuResourceWatch {
public:
    explicit RuntimeMenuResourceWatch(const RuntimeConfig& config)
        : resourceRoot_(absolutePath(config.resourceRootPath)),
          localRoot_(absolutePath(config.localDataRootPath)) {
        const std::array<std::filesystem::path, 2> roots{
            std::filesystem::path(resourceRoot_),
            std::filesystem::path(localRoot_) / "resources" / "mix_exports"
        };
        for (std::size_t i = 0; i < roots.size(); ++i) {
            handles_[i] = FindFirstChangeNotificationW(
                roots[i].c_str(), TRUE,
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME
                    | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE);
        }
        for (std::size_t i = 0; i < localInputs_.size(); ++i) {
            localStamps_[i] = stamp(std::filesystem::path(localRoot_) / localInputs_[i]);
        }
    }

    ~RuntimeMenuResourceWatch() {
        for (HANDLE handle : handles_) {
            if (handle != INVALID_HANDLE_VALUE) FindCloseChangeNotification(handle);
        }
    }

    RuntimeMenuResourceWatch(const RuntimeMenuResourceWatch&) = delete;
    RuntimeMenuResourceWatch& operator=(const RuntimeMenuResourceWatch&) = delete;

    bool sameRoots(const RuntimeConfig& config) const {
        return resourceRoot_ == absolutePath(config.resourceRootPath)
            && localRoot_ == absolutePath(config.localDataRootPath);
    }

    bool unchanged(const RuntimeConfig& config) const {
        if (!sameRoots(config)) return false;
        for (HANDLE handle : handles_) {
            // An unavailable watcher cannot establish freshness. Reload on
            // the next visit instead of silently retaining stale resources.
            if (handle == INVALID_HANDLE_VALUE
                || WaitForSingleObject(handle, 0) != WAIT_TIMEOUT) return false;
        }
        for (std::size_t i = 0; i < localInputs_.size(); ++i) {
            if (stamp(std::filesystem::path(localRoot_) / localInputs_[i])
                != localStamps_[i]) return false;
        }
        return true;
    }

private:
    static std::string absolutePath(const std::string& path) {
        std::error_code error;
        const auto absolute = std::filesystem::absolute(path, error);
        return error ? path : absolute.lexically_normal().string();
    }

    struct FileStamp {
        std::filesystem::file_time_type modified{};
        std::uintmax_t size = 0;
        bool operator==(const FileStamp&) const = default;
    };

    static FileStamp stamp(const std::filesystem::path& path) {
        std::error_code error;
        FileStamp result;
        result.modified = std::filesystem::last_write_time(path, error);
        if (!error) result.size = std::filesystem::file_size(path, error);
        return result;
    }

    std::string resourceRoot_;
    std::string localRoot_;
    std::array<HANDLE, 2> handles_{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
    static constexpr std::array<const char*, 4> localInputs_{
        "rulesmd.ini", "mpmodesmd.ini", "coopcampmd.ini", "soundmd.ini"
    };
    std::array<FileStamp, localInputs_.size()> localStamps_{};
};
