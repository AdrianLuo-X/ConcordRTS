#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct ArtType;

struct RuntimeAuxiliaryShpBytesCacheEntry {
    bool loadAttempted = false;
    bool loaded = false;
    std::vector<unsigned char> bytes;
    std::uint64_t sourceFingerprint = 0;
    std::string resolvedImageId;
    std::string sourcePath;
    int frameCount = 0;
    std::string errorMessage;
    // Derived Runtime frame-zero validity, scoped to these immutable selected bytes.
    bool timingFrame0Attempted = false;
    bool timingFrame0Valid = false;
    std::string timingFrame0Error;
};

struct RuntimeAuxiliaryShpByteCache {
    std::unordered_map<std::string, RuntimeAuxiliaryShpBytesCacheEntry> auxiliaryShpBytesByImage;
    int auxiliaryShpBytesCacheHits = 0;
    int auxiliaryShpBytesCacheMisses = 0;
    double auxiliaryShpBytesCacheMissMs = 0.0;
};

// Read-only synchronous source view; no drawing context or world state.
struct RuntimeAuxiliaryShpByteContext {
    const std::string& assetRootPath;
    const std::string& structureRootPath;
};

class RuntimeAuxiliaryShpByteSource {
public:
    virtual ~RuntimeAuxiliaryShpByteSource() = default;
    virtual std::vector<std::string> imageCandidates(const std::string&, const ArtType*) = 0;
    virtual const ArtType* findArtType(const std::string&) = 0;
    virtual const std::string& cachedMixExportsRoot() = 0;
    virtual bool sameNormalizedRoot(const std::string&, const std::string&) = 0;
    virtual bool readMix(const std::string&, const std::string&, std::vector<unsigned char>&,
        std::string&, std::string&) = 0;
    virtual bool readByName(const std::vector<std::string>&, const std::string&, const std::string&,
        std::vector<unsigned char>&, std::string&, std::string&) = 0;
    virtual bool detailTimingEnabled() = 0;
};

struct RuntimeAuxiliaryShpByteLookup {
    RuntimeAuxiliaryShpBytesCacheEntry* entry = nullptr;
    std::string candidateKey;
};

RuntimeAuxiliaryShpByteLookup loadRuntimeAuxiliaryShpBytes(
    const std::string& imageId, const RuntimeAuxiliaryShpByteContext&,
    RuntimeAuxiliaryShpByteCache&, RuntimeAuxiliaryShpByteSource&,
    const ArtType* fallbackArtType = nullptr);

namespace concord::runtime::resource_detail {
int runtimeShpFrameCount(const std::vector<unsigned char>& bytes);
int runtimeVisibleMainFrameCount(int frameCount, bool splitShadow);
}
