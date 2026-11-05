#pragma once

#include <filesystem>
#include <memory>

struct RuntimeConfig;

// Owned native OS paths; legacy RuntimeConfig strings remain in the native
// narrow code page. These are neither resource logical names nor UTF-8 labels.
struct RuntimeMenuResourcePaths {
    std::filesystem::path resourceRoot;
    std::filesystem::path localDataRoot;
};

// Synchronous freshness observation, owned by the existing menu state. No
// resource loading, rearming, prewarm, entry or task lifecycle takes place here.
// Preferences are excluded and reapplied by the menu on every entry. Missing
// notifications (including an unimplemented platform) always require a reload.
class RuntimeMenuResourceWatch {
public:
    explicit RuntimeMenuResourceWatch(const RuntimeConfig& config);
    explicit RuntimeMenuResourceWatch(const RuntimeMenuResourcePaths& paths);
    ~RuntimeMenuResourceWatch();

    RuntimeMenuResourceWatch(const RuntimeMenuResourceWatch&) = delete;
    RuntimeMenuResourceWatch& operator=(const RuntimeMenuResourceWatch&) = delete;

    bool sameRoots(const RuntimeConfig& config) const;
    bool sameRoots(const RuntimeMenuResourcePaths& paths) const;
    bool unchanged(const RuntimeConfig& config) const;
    bool unchanged(const RuntimeMenuResourcePaths& paths) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
