#pragma once

#include <filesystem>
#include <string>

class IniFile;

// Equivalence diagnostics can exercise both real loose-file reader branches
// in one process. The default production choice remains the environment flag.
class RuntimeIniTextReadReferenceScope {
public:
    explicit RuntimeIniTextReadReferenceScope(bool reference) noexcept;
    ~RuntimeIniTextReadReferenceScope() noexcept;
    RuntimeIniTextReadReferenceScope(const RuntimeIniTextReadReferenceScope&) = delete;
    RuntimeIniTextReadReferenceScope& operator=(const RuntimeIniTextReadReferenceScope&) = delete;
private:
    int previous_ = -1;
};

enum class RuntimeCampaignIniKind {
    Rules,
    Art,
    Ai,
    Sound,
    Eva
};

struct RuntimeCampaignDataSourceOptions {
    // Compatibility roots in the native narrow OS encoding, not UTF-8. INI
    // content and MIX logical names have independent byte contracts.
    std::string resourceRootPath;
    std::string localDataRootPath;
    std::string modRootPath;
    // "ra2" and "yr" are strict edition selectors. An empty value retains
    // the historical non-campaign behavior (YR first, then base RA2).
    std::string sourceId;
};

// Owned native roots. UTF-8 boundaries explicitly construct paths from
// char8_t/u8string; no global reinterpretation of legacy configuration fields.
struct RuntimeCampaignDataSourcePaths {
    std::filesystem::path resourceRootPath;
    std::filesystem::path localDataRootPath;
    std::filesystem::path modRootPath;
    std::string sourceId;
};

struct RuntimeIniSource {
    std::string sourceId;
    std::string displayPath;
    std::string backingPath;
    std::string entryName;
    bool mixEntry = false;
    // Resolver results retain the exact selected OS path through read/load.
    // displayPath/backingPath remain UTF-8 labels. Empty here preserves the
    // historical native-narrow backingPath contract of manually built sources.
    std::filesystem::path filePath;

    bool available() const;
    bool load(IniFile& ini, std::string* errorMessage = nullptr) const;
    bool readText(
        std::string& text,
        std::string* errorMessage = nullptr
    ) const;
};

std::string normalizeRuntimeCampaignSourceId(const std::string& sourceId);
std::string runtimeCampaignIniFileName(
    RuntimeCampaignIniKind kind,
    const std::string& sourceId
);
RuntimeIniSource resolveRuntimeCampaignIniSource(
    const RuntimeCampaignDataSourceOptions& options,
    RuntimeCampaignIniKind kind
);
RuntimeIniSource resolveRuntimeCampaignTheaterSource(
    const RuntimeCampaignDataSourceOptions& options,
    const std::string& mapTheater
);

// Same candidate sequence as the compatibility entry: edition first; all loose
// roots (mod/local/resource) before any MIX root. No directory enumeration,
// sorting, case folding of OS paths, canonicalization or missing-file fallback
// changes. Synchronous reads own all paths; no borrowed roots or new cache.
RuntimeIniSource resolveRuntimeCampaignIniSourceFromPaths(
    const RuntimeCampaignDataSourcePaths& paths,
    RuntimeCampaignIniKind kind
);
RuntimeIniSource resolveRuntimeCampaignTheaterSourceFromPaths(
    const RuntimeCampaignDataSourcePaths& paths,
    const std::string& mapTheater
);
