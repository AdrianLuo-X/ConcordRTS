#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct RuntimeCampaignMovieResolverOptions {
    std::string resourceRootPath;
    std::string localDataRootPath;
    std::string modRootPath;
};

// Native OS roots. Legacy options above retain the native narrow code page.
struct RuntimeCampaignMoviePaths {
    std::filesystem::path resourceRoot;
    std::filesystem::path localDataRoot;
    std::filesystem::path modRoot;
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
    // Owned native identities; referencePath/archivePath remain UTF-8 references
    // and diagnostic labels, with the existing archive.mix!entry.bik convention.
    std::filesystem::path filePath;
    std::filesystem::path archiveFilePath;
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

RuntimeCampaignMovieResolution resolveRuntimeCampaignMovieFromPaths(
    const RuntimeCampaignMoviePaths& paths,
    const std::string& sourceId,
    const std::string& movieId);

// The resolver's serialized reference is explicitly UTF-8; the movie logical
// name and original INI bytes are never recoded. Output is an owned OS path.
bool materializeRuntimeCampaignMovieFromPaths(
    const RuntimeCampaignMoviePaths& paths,
    const std::string& utf8Reference,
    std::filesystem::path& moviePath,
    std::string& errorMessage);
