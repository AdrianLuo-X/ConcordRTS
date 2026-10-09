#pragma once

#include "IniFile.h"

#include <cstddef>
#include <cstdint>
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
