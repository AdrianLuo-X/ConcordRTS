#pragma once

#include <cstdint>
#include <string>

struct RuntimeCampaignMovieResolverOptions {
    std::string resourceRootPath;
    std::string localDataRootPath;
    std::string modRootPath;
};

struct RuntimeCampaignMovieResolution {
    bool found = false;
    bool mixBacked = false;
    std::string sourceId;
    std::string movieId;
    // Loose movies use a normal file path. MIX-backed movies use the same
    // virtual reference convention as campaign maps: archive.mix!entry.bik.
    std::string referencePath;
    std::string archivePath;
    std::string entryName;
    std::uint64_t byteSize = 0;
};

std::string normalizeRuntimeCampaignMovieSourceId(
    const std::string& sourceId
);

bool isRuntimeCampaignMovieMixReference(
    const std::string& referencePath
);

RuntimeCampaignMovieResolution resolveRuntimeCampaignMovie(
    const RuntimeCampaignMovieResolverOptions& options,
    const std::string& sourceId,
    const std::string& movieId
);

// Bink consumes a normal filesystem path. Loose references pass through;
// archive references are streamed into a deterministic per-entry cache only
// when playback is requested.
bool materializeRuntimeCampaignMovie(
    const RuntimeCampaignMovieResolverOptions& options,
    const std::string& referencePath,
    std::string& moviePath,
    std::string& errorMessage
);
