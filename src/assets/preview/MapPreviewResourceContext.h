#pragma once

#include <any>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace concord::assets {

// The only S10 policy is the existing candidate-first, loose-then-MIX rule,
// with configured tile/asset roots and their existing sibling/root expansion.
// Edition/mod precedence remains the caller's configured roots in that order.
enum class MapPreviewEditionModPriority { LegacyConfiguredRootOrder };

struct MapPreviewResourceContext {
    std::string packageRoot;
    std::string tileRootPath;
    std::string assetRootPath;
    std::string palettePath;
    std::string structurePalettePath;
    MapPreviewEditionModPriority editionModPriority = MapPreviewEditionModPriority::LegacyConfiguredRootOrder;
    std::uint64_t mountGeneration = 0;
};

enum class MapPreviewCacheDomain : std::size_t {
    OverlayTmp, OverlayTmpError, OverlayShpFrame, OverlayShpError,
    OverlayLooseBytes, OverlayMixBytes, DiagnosticResult,
    DiagnosticLooseBytes, DiagnosticMixBytes, Count
};

// One explicit consumer owns a session. No process-global or thread-local cache.
// Calls/refresh are serialized by that consumer; references expire at refresh.
class MapPreviewResourceSession {
public:
    explicit MapPreviewResourceSession(MapPreviewResourceContext context);
    ~MapPreviewResourceSession();
    MapPreviewResourceSession(MapPreviewResourceSession&&) noexcept;
    MapPreviewResourceSession& operator=(MapPreviewResourceSession&&) noexcept;
    MapPreviewResourceSession(const MapPreviewResourceSession&) = delete;
    MapPreviewResourceSession& operator=(const MapPreviewResourceSession&) = delete;

    const MapPreviewResourceContext& context() const;
    void refresh();
    std::size_t cachedDomainCount() const;

    // Exactly nine legacy memoization domains. TMP/SHP values stay private to
    // MapPreviewExporter; this interface adds no filesystem search/VFS policy.
    template <typename Value>
    std::unordered_map<std::string, Value>& cache(MapPreviewCacheDomain domain) {
        using Cache = std::unordered_map<std::string, Value>;
        auto& storage = domainStorage(domain);
        if (!storage.has_value()) storage.emplace<Cache>();
        return std::any_cast<Cache&>(storage);
    }

    // Successful bytes are cached; open/size/read failures are never cached.
    // Relative paths use this session's package root. Error/source spelling
    // remains the supplied path; no lower-case or canonical cache-key rewrite.
    bool readLooseBytes(MapPreviewCacheDomain domain, const std::string& path,
        std::vector<unsigned char>& bytes, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::any& domainStorage(MapPreviewCacheDomain domain);
};

} // namespace concord::assets