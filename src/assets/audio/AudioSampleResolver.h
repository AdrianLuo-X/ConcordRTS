#pragma once

#include "core/io/IniFile.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

struct AudioIndexedSample {
    std::string name;
    std::string sourceMixPath;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    int sampleRate = 0;
    std::uint32_t flags = 0;
    std::uint32_t chunkSize = 0;
};

struct AudioSampleResolveResult {
    bool success = false;
    std::string errorMessage;
    std::string sampleName;
    std::string normalizedName;
    std::string sourcePath;
    std::string sourceFormat;
    std::string wavPath;
    bool fromCache = false;
    bool decoded = false;
    int sampleRate = 0;
    int bitsPerSample = 0;
    int channels = 0;
    std::size_t pcmBytes = 0;
    // Owned OS path. Never reconstruct it from a diagnostic label.
    std::filesystem::path wavFilePath;
};

struct AudioBackgroundMusicResolveResult {
    bool success = false;
    std::string errorMessage;
    std::string themeIniPath;
    std::string themeId;
    std::string soundName;
    std::string sourcePath;
    std::string sourceFormat;
    std::string wavPath;
    bool normalTrack = false;
    bool fromCache = false;
    bool decoded = false;
    std::filesystem::path wavFilePath;
};

// Synchronous, owned roots from the existing configuration/resource selection.
// These APIs do not discover roots, add MOD layers, enumerate directories or
// change codec/playback policy. Construct UTF-8 inputs explicitly as u8string;
// INI values and MIX/audio logical names retain their original bytes.
struct AudioSampleResolverPaths {
    std::filesystem::path assetRoot;
    std::filesystem::path cacheRoot;
};

// Gameplay playlists are intentionally separate from the front-end INTRO
// theme.  Retail RA2/YR documents a Side field in theme.ini, but the shipped
// normal tracks leave it blank, so the resolver also applies the game's
// Allied/Soviet/Yuri soundtrack grouping for those entries.
enum class AudioBackgroundMusicFaction {
    Any,
    Allied,
    Soviet,
    Yuri
};

struct AudioDecodeSmokeReport {
    bool success = false;
    std::string errorMessage;
    std::string assetRootPath;
    std::string cacheRootPath;
    int indexedLibraries = 0;
    int indexedSamples = 0;
    int attemptedSamples = 0;
    int resolvedSamples = 0;
    int decodedSamples = 0;
    int validatedWaveSamples = 0;
    int attemptedMusicTracks = 0;
    int resolvedMusicTracks = 0;
    std::vector<AudioSampleResolveResult> samples;
    std::vector<AudioBackgroundMusicResolveResult> musicTracks;
    std::vector<std::string> warnings;
};

std::string normalizeAudioSampleName(const std::string& sampleName);

// Native path entry points: diagnostic path labels are UTF-8; wavFilePath owns
// the actual playable path. Existing narrow-string APIs below remain native
// narrow-code-page compatibility boundaries (ACP on Windows), not UTF-8.
// Legacy candidate/cache keys and failure caching are unchanged. Native calls
// have a separate in-memory lookup domain; persistent cache filenames reuse
// the legacy source identity where representable, otherwise tagged UTF-8.
std::vector<AudioIndexedSample> collectIndexedAudioSamplesFromPaths(
    const AudioSampleResolverPaths& paths,
    const std::string& campaignSourceId = {});
AudioSampleResolveResult resolveAudioSampleToWaveFromPaths(
    const AudioSampleResolverPaths& paths,
    const std::string& sampleName,
    const std::string& campaignSourceId = {});
AudioBackgroundMusicResolveResult resolveBackgroundMusicToWaveFromPaths(
    const AudioSampleResolverPaths& paths,
    const std::vector<std::string>& preferredThemeIds = {});
std::vector<std::string> collectBackgroundMusicThemeIdsFromPaths(
    const AudioSampleResolverPaths& paths,
    AudioBackgroundMusicFaction faction);
AudioBackgroundMusicResolveResult resolveBackgroundMusicThemeToWaveFromPaths(
    const AudioSampleResolverPaths& paths,
    const std::string& themeId);

std::vector<AudioIndexedSample> collectIndexedAudioSamples(
    const std::string& assetRootPath,
    const std::string& campaignSourceId = {}
);

AudioSampleResolveResult resolveAudioSampleToWave(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const std::string& sampleName,
    const std::string& campaignSourceId = {}
);

AudioBackgroundMusicResolveResult resolveBackgroundMusicToWave(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const std::vector<std::string>& preferredThemeIds = {}
);

std::vector<std::string> collectBackgroundMusicThemeIds(
    const std::string& assetRootPath,
    AudioBackgroundMusicFaction faction
);

AudioBackgroundMusicResolveResult resolveBackgroundMusicThemeToWave(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const std::string& themeId
);

AudioDecodeSmokeReport runAudioDecodeSmoke(
    const std::string& assetRootPath,
    const std::string& cacheRootPath,
    const IniFile* soundIni
);

void printAudioDecodeSmokeReport(
    std::ostream& output,
    const AudioDecodeSmokeReport& report
);
