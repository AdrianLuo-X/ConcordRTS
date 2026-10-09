#pragma once

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
    std::string resourceRootPath;
    std::string localDataRootPath;
    std::string modRootPath;
    // "ra2" and "yr" are strict edition selectors. An empty value retains
    // the historical non-campaign behavior (YR first, then base RA2).
    std::string sourceId;
};

struct RuntimeIniSource {
    std::string sourceId;
    std::string displayPath;
    std::string backingPath;
    std::string entryName;
    bool mixEntry = false;

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
