#pragma once

#include "IniFile.h"

#include <iosfwd>
#include <string>
#include <vector>

struct AudioIntentSampleCandidate {
    std::string sampleName;
    std::string resourcePath;
    std::string resourceFormat;
    std::string blocker;
    bool resolved = false;
    bool decodable = false;
    bool playable = false;
};

struct AudioIntentResolution {
    std::string intent;
    std::string source;
    std::string iniSourceFile;
    std::string sampleSourceFile;
    std::string sectionName;
    std::string keyName;
    std::string eventName;
    std::string resolvedEventId;
    std::string status;
    std::string note;
    int sampleReferences = 0;
    int resolvedSamples = 0;
    int missingSamples = 0;
    std::vector<AudioIntentSampleCandidate> samples;
};

struct AudioUnitVoiceCoverage {
    std::string side;
    int unitTypes = 0;
    int unitTypesWithVoices = 0;
    int eventReferences = 0;
    int eventSectionsFound = 0;
    int missingEventSections = 0;
    int intentionalSilentEvents = 0;
    int sampleReferences = 0;
    int resolvedSamples = 0;
    int missingSamples = 0;
};

struct AudioCatalogReport {
    bool success = false;
    std::string errorMessage;
    std::string soundPath;
    std::string evaPath;
    std::string rulesPath;
    std::string themePath;
    std::string assetRootPath;
    int soundListEntries = 0;
    int soundSectionsFound = 0;
    int eventsWithSamples = 0;
    int sampleReferences = 0;
    int uniqueSamples = 0;
    int resolvedSamples = 0;
    int missingSamples = 0;
    int extractedWavFiles = 0;
    int extractedAudFiles = 0;
    int audioMixArchives = 0;
    int indexedAudioSamples = 0;
    int indexedVoxLabels = 0;
    int focusedIntentCount = 0;
    int focusedIntentResolvedSamples = 0;
    int focusedIntentMissingSamples = 0;
    int evaSections = 0;
    int evaSampleReferences = 0;
    int evaResolvedSamples = 0;
    int evaMissingSamples = 0;
    int unitVoiceTypes = 0;
    int unitVoiceTypesWithVoices = 0;
    int unitVoiceEventReferences = 0;
    int unitVoiceMissingEvents = 0;
    int unitVoiceIntentionalSilentEvents = 0;
    int unitVoiceSampleReferences = 0;
    int unitVoiceResolvedSamples = 0;
    int unitVoiceMissingSamples = 0;
    int themeEntries = 0;
    int normalMusicTracks = 0;
    int resolvedMusicTracks = 0;
    int missingMusicTracks = 0;
    std::vector<std::string> missingSampleExamples;
    std::vector<std::string> missingUnitVoiceExamples;
    std::vector<AudioUnitVoiceCoverage> unitVoiceCoverageBySide;
    std::vector<std::string> unitVoiceRuntimeTriggerableFields;
    std::vector<std::string> unitVoiceRuntimeNotTriggerableFields;
    std::vector<std::string> resolvedMusicTrackPaths;
    std::vector<std::string> missingMusicTrackExamples;
    std::vector<AudioIntentResolution> focusedIntents;
    std::vector<std::string> warnings;
};

AudioCatalogReport buildAudioCatalogReport(
    const IniFile* soundIni,
    const std::string& soundPath,
    const IniFile* evaIni,
    const std::string& evaPath,
    const std::string& assetRootPath
);

void printAudioCatalogReport(std::ostream& output, const AudioCatalogReport& report);
