#pragma once
#include <string>
class IniFile;

// Borrowed live identity fields only. Neither this view nor a normalization
// operation is retained beyond the synchronous validation call.
struct RuntimeSaveActiveIdentity {
    const bool& campaignMode;
    const std::string& defaultMapPath;
    const std::string& campaignId;
    const std::string& campaignMissionId;
    const std::string& campaignSourceId;
};

// Existing logical path/source policies remain with their original owners.
// Both methods can propagate the same allocation/filesystem exceptions as
// the original helpers; validation does not catch or replace them.
class RuntimeSaveIdentityNormalization {
public:
    virtual ~RuntimeSaveIdentityNormalization() = default;
    virtual std::string mapIdentity(const std::string& path) const = 0;
    virtual std::string campaignSourceId(const std::string& source) const = 0;
};

bool decodeRuntimeSaveIdentityField(const IniFile& ini,
    const std::string& section, const std::string& encodedKey,
    const std::string& legacyKey, std::string& value, std::string& errorMessage);
bool validateRuntimeSaveOuterIdentity(const RuntimeSaveActiveIdentity& state,
    const IniFile& ini, int version,
    const RuntimeSaveIdentityNormalization& normalization,
    std::string& errorMessage);
