#include "assets/audio/AudioCatalog.h"

#include "assets/audio/AudioDecoder.h"
#include "assets/audio/AudioSampleResolver.h"
#include "assets/archive/MixArchive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <initializer_list>
#include <limits>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {

std::string lowerAscii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}

std::string trimAscii(const std::string& value) {
    std::size_t first = 0;
    while (first < value.size()
        && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (last > first
        && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string stripAudioComment(const std::string& value) {
    const std::size_t semicolon = value.find(';');
    const std::size_t slash = value.find("//");
    std::size_t comment = std::string::npos;
    if (semicolon != std::string::npos) {
        comment = semicolon;
    }
    if (slash != std::string::npos
        && (comment == std::string::npos || slash < comment)) {
        comment = slash;
    }
    return trimAscii(comment == std::string::npos ? value : value.substr(0, comment));
}

std::vector<std::string> splitWhitespace(const std::string& value) {
    std::vector<std::string> tokens;
    std::string normalized = stripAudioComment(value);
    for (char& character : normalized) {
        if (character == ',' || character == '|') {
            character = ' ';
        }
    }

    std::istringstream stream(normalized);
    std::string token;
    while (stream >> token) {
        token = trimAscii(token);
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

std::string stripAudioDecorators(std::string value) {
    value = trimAscii(stripAudioComment(value));
    while (value.size() >= 2U
        && ((value.front() == '"' && value.back() == '"')
            || (value.front() == '\'' && value.back() == '\''))) {
        value = trimAscii(value.substr(1U, value.size() - 2U));
    }
    while (!value.empty() && (value.front() == '$' || value.front() == '@')) {
        value.erase(value.begin());
        value = trimAscii(value);
    }
    return value;
}

bool isNullSampleName(const std::string& value) {
    const std::string normalized = lowerAscii(trimAscii(value));
    return normalized.empty()
        || normalized == "none"
        || normalized == "<none>"
        || normalized == "null"
        || normalized == "dummy";
}

std::string normalizeSampleName(std::string value) {
    value = stripAudioDecorators(std::move(value));
    if (isNullSampleName(value)) {
        return "";
    }

    const std::filesystem::path path(value);
    if (path.has_extension()) {
        value = path.stem().string();
    }
    value = lowerAscii(trimAscii(value));
    return isNullSampleName(value) ? "" : value;
}

std::vector<std::string> sampleTokensForSection(
    const IniFile& ini,
    const std::string& sectionName
) {
    std::vector<std::string> samples;
    const IniFile::Section* section = ini.getSection(sectionName);
    if (section == nullptr) {
        return samples;
    }

    for (const auto& [key, value] : *section) {
        const std::string lowerKey = lowerAscii(trimAscii(key));
        if (lowerKey != "sounds"
            && lowerKey != "sound"
            && lowerKey != "samples"
            && lowerKey != "sample"
            && lowerKey != "file"
            && lowerKey != "files"
            && lowerKey != "russian"
            && lowerKey != "allied"
            && lowerKey != "yuri") {
            continue;
        }

        const std::vector<std::string> tokens = splitWhitespace(value);
        samples.insert(samples.end(), tokens.begin(), tokens.end());
    }
    return samples;
}

bool isIntentionalSilentSoundSection(
    const IniFile& ini,
    const std::string& sectionName
) {
    if (ini.getSection(sectionName) == nullptr) {
        return false;
    }
    const std::string volumeText =
        trimAscii(stripAudioComment(
            ini.getValue(sectionName, "Volume", "")
        ));
    if (volumeText.empty()) {
        return false;
    }
    std::istringstream stream(volumeText);
    double volume = 1.0;
    if (!(stream >> volume)) {
        return false;
    }
    std::string trailingToken;
    return !(stream >> trailingToken) && volume == 0.0;
}

bool isAudioMixName(const std::string& filename) {
    const std::string lower = lowerAscii(filename);
    return lower == "audio.mix"
        || lower == "audiomd.mix"
        || lower.find("audio") != std::string::npos;
}

struct AssetAudioIndex {
    struct SampleResource {
        std::string path;
        std::string format;
        bool decodable = false;
        bool playable = false;
    };

    int wavFiles = 0;
    int audFiles = 0;
    int audioMixArchives = 0;
    int indexedAudioSamples = 0;
    int indexedVoxLabels = 0;
    std::unordered_set<std::string> sampleStems;
    std::unordered_map<std::string, SampleResource> sampleResources;
    std::vector<MixArchive> readableAudioMixes;
};

struct SemanticAudioIntent {
    const char* intent;
    const char* source;
    const char* sectionName;
    const char* keyName;
    const char* eventName;
};

bool regularFileExists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error)
        && std::filesystem::is_regular_file(path, error);
}

std::string pathString(const std::filesystem::path& path) {
    return path.string();
}

std::vector<std::uint8_t> readFileBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    return std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    );
}

std::vector<std::filesystem::path> voiceCsfPathCandidates(
    const std::string& assetRootPath
) {
    std::vector<std::filesystem::path> candidates;
    if (!assetRootPath.empty()) {
        const std::filesystem::path root(assetRootPath);
        candidates.push_back(root / "langmd" / "ra2md.csf");
        candidates.push_back(root / "language" / "ra2.csf");
        candidates.push_back(root / "GameMD" / "ra2md.csf");
        candidates.push_back(root / "Game" / "ra2.csf");

        std::filesystem::path parent = root;
        for (int depth = 0; depth < 5 && !parent.empty(); ++depth) {
            candidates.push_back(parent / "RedAlert2" / "GameMD" / "ra2md.csf");
            candidates.push_back(parent / "RedAlert2" / "Game" / "ra2.csf");
            parent = parent.parent_path();
        }
    }

    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    if (!error) {
        candidates.push_back(cwd / "RedAlert2" / "GameMD" / "ra2md.csf");
        candidates.push_back(cwd / "RedAlert2" / "Game" / "ra2.csf");
    }

    std::vector<std::filesystem::path> existing;
    std::set<std::string> seen;
    for (const std::filesystem::path& candidate : candidates) {
        if (!regularFileExists(candidate)) {
            continue;
        }
        const std::string key = lowerAscii(pathString(candidate));
        if (seen.insert(key).second) {
            existing.push_back(candidate);
        }
    }
    return existing;
}

std::vector<std::filesystem::path> voiceMixPathCandidates(
    const std::string& assetRootPath
) {
    std::vector<std::filesystem::path> candidates;
    if (!assetRootPath.empty()) {
        const std::filesystem::path root(assetRootPath);
        candidates.push_back(root / "langmd" / "langmd.mix");
        candidates.push_back(root / "language" / "language.mix");
        candidates.push_back(root / "ra2md" / "langmd.mix");
        candidates.push_back(root / "ra2" / "language.mix");
        candidates.push_back(root / "GameMD" / "langmd.mix");
        candidates.push_back(root / "GameMD" / "language.mix");
        candidates.push_back(root / "Game" / "language.mix");

        std::filesystem::path parent = root;
        for (int depth = 0; depth < 5 && !parent.empty(); ++depth) {
            candidates.push_back(parent / "RedAlert2" / "GameMD" / "langmd.mix");
            candidates.push_back(parent / "RedAlert2" / "GameMD" / "language.mix");
            candidates.push_back(parent / "RedAlert2" / "Game" / "language.mix");
            parent = parent.parent_path();
        }
    }

    std::error_code error;
    const std::filesystem::path cwd = std::filesystem::current_path(error);
    if (!error) {
        candidates.push_back(cwd / "RedAlert2" / "GameMD" / "langmd.mix");
        candidates.push_back(cwd / "RedAlert2" / "GameMD" / "language.mix");
        candidates.push_back(cwd / "RedAlert2" / "Game" / "language.mix");
    }

    std::vector<std::filesystem::path> existing;
    std::set<std::string> seen;
    for (const std::filesystem::path& candidate : candidates) {
        if (!regularFileExists(candidate)) {
            continue;
        }
        const std::string key = lowerAscii(pathString(candidate));
        if (seen.insert(key).second) {
            existing.push_back(candidate);
        }
    }
    return existing;
}

std::vector<AudioIndexedSample> collectVoxLabelSamplesForCatalog(
    const std::string& assetRootPath
) {
    const auto scanCandidates =
        [](const std::vector<std::filesystem::path>& candidates) {
            std::vector<AudioIndexedSample> labels;
            std::unordered_set<std::string> seen;
            for (const std::filesystem::path& sourcePath : candidates) {
                const std::vector<std::uint8_t> bytes =
                    readFileBytes(sourcePath);
                if (bytes.empty()) {
                    continue;
                }

                for (std::size_t index = 0; index + 4U < bytes.size(); ++index) {
                    if (bytes[index] != 'V'
                        || bytes[index + 1U] != 'O'
                        || bytes[index + 2U] != 'X'
                        || bytes[index + 3U] != ':') {
                        continue;
                    }

                    std::string label;
                    std::size_t cursor = index + 4U;
                    while (cursor < bytes.size()) {
                        const unsigned char value = bytes[cursor];
                        if ((value >= 'a' && value <= 'z')
                            || (value >= '0' && value <= '9')
                            || value == '_') {
                            label.push_back(static_cast<char>(value));
                            ++cursor;
                            continue;
                        }
                        break;
                    }

                    label = normalizeSampleName(label);
                    if (label.empty() || !seen.insert(label).second) {
                        continue;
                    }

                    AudioIndexedSample sample;
                    sample.name = label;
                    sample.sourceMixPath = pathString(sourcePath);
                    sample.offset = static_cast<std::uint32_t>(
                        std::min<std::size_t>(
                            index,
                            std::numeric_limits<std::uint32_t>::max()
                        )
                    );
                    labels.push_back(std::move(sample));
                }
            }
            return labels;
        };

    std::vector<AudioIndexedSample> labels =
        scanCandidates(voiceCsfPathCandidates(assetRootPath));
    if (!labels.empty()) {
        return labels;
    }
    return scanCandidates(voiceMixPathCandidates(assetRootPath));
}

std::string firstExistingRulesIniPath(
    const std::string& assetRootPath,
    const std::string& soundPath
) {
    std::vector<std::filesystem::path> candidates;
    if (!assetRootPath.empty()) {
        const std::filesystem::path root(assetRootPath);
        candidates.push_back(root / "rulesmd.ini");
        candidates.push_back(root / "rules.ini");
        candidates.push_back(root / "localmd" / "rulesmd.ini");
        candidates.push_back(root / "local" / "rules.ini");
        candidates.push_back(root / "ra2md" / "localmd" / "rulesmd.ini");
        candidates.push_back(root / "ra2" / "local" / "rules.ini");
    }
    if (!soundPath.empty()) {
        const std::filesystem::path soundIniPath(soundPath);
        const std::filesystem::path parent = soundIniPath.parent_path();
        candidates.push_back(parent / "rulesmd.ini");
        candidates.push_back(parent / "rules.ini");
    }

    for (const std::filesystem::path& candidate : candidates) {
        if (regularFileExists(candidate)) {
            return candidate.string();
        }
    }
    return "";
}

std::string firstExistingThemeIniPath(
    const std::string& assetRootPath,
    const std::string& soundPath
) {
    std::vector<std::filesystem::path> candidates;
    if (!assetRootPath.empty()) {
        const std::filesystem::path root(assetRootPath);
        candidates.push_back(root / "ra2md" / "localmd" / "thememd.ini");
        candidates.push_back(root / "localmd" / "thememd.ini");
        candidates.push_back(root / "thememd.ini");
        candidates.push_back(root / "theme.ini");
    }
    if (!soundPath.empty()) {
        const std::filesystem::path parent =
            std::filesystem::path(soundPath).parent_path();
        candidates.push_back(parent / "thememd.ini");
        candidates.push_back(parent / "theme.ini");
    }
    for (const std::filesystem::path& candidate : candidates) {
        if (regularFileExists(candidate)) {
            return pathString(candidate);
        }
    }
    return "";
}

void auditThemeMusic(
    AudioCatalogReport& report,
    const std::string& assetRootPath,
    const std::string& soundPath
) {
    report.themePath = firstExistingThemeIniPath(assetRootPath, soundPath);
    IniFile themeIni;
    if (report.themePath.empty() || !themeIni.loadFromFile(report.themePath)) {
        report.warnings.push_back(
            "thememd.ini/theme.ini was not found; background music catalog is unavailable."
        );
        return;
    }

    const IniFile::Section* themes = themeIni.getSection("Themes");
    if (themes == nullptr) {
        report.warnings.push_back("[Themes] was not found in the theme INI.");
        return;
    }
    const std::filesystem::path root(assetRootPath);
    for (const auto& [index, rawTheme] : *themes) {
        (void)index;
        const std::string theme = stripAudioComment(rawTheme);
        if (theme.empty() || themeIni.getSection(theme) == nullptr) {
            continue;
        }
        const std::string sound = stripAudioComment(
            themeIni.getValue(theme, "Sound", "")
        );
        if (sound.empty()) {
            continue;
        }
        ++report.themeEntries;
        const std::string normalValue = lowerAscii(stripAudioComment(
            themeIni.getValue(theme, "Normal", "yes")
        ));
        const bool normal = normalValue.empty()
            || normalValue == "yes"
            || normalValue == "true"
            || normalValue == "1";
        if (!normal) {
            continue;
        }
        ++report.normalMusicTracks;

        const std::filesystem::path candidates[] = {
            root / "thememd" / (sound + ".wav"),
            root / "theme" / (sound + ".wav"),
            root / "thememd" / (sound + ".aud"),
            root / "theme" / (sound + ".aud")
        };
        std::string resolvedPath;
        for (const std::filesystem::path& candidate : candidates) {
            if (!regularFileExists(candidate)) {
                continue;
            }
            const std::string extension =
                lowerAscii(candidate.extension().string());
            if (extension == ".wav"
                && !isWaveFileBytes(readFileBytes(candidate))) {
                continue;
            }
            resolvedPath = pathString(candidate);
            break;
        }
        if (!resolvedPath.empty()) {
            ++report.resolvedMusicTracks;
            if (report.resolvedMusicTrackPaths.size() < 16U) {
                report.resolvedMusicTrackPaths.push_back(resolvedPath);
            }
        } else {
            ++report.missingMusicTracks;
            if (report.missingMusicTrackExamples.size() < 16U) {
                report.missingMusicTrackExamples.push_back(
                    theme + "->" + sound
                );
            }
        }
    }
}

const AssetAudioIndex::SampleResource* resolveAssetAudioResource(
    AssetAudioIndex& assetIndex,
    const std::string& sample
) {
    const auto existing = assetIndex.sampleResources.find(sample);
    if (existing != assetIndex.sampleResources.end()
        && existing->second.playable) {
        return &existing->second;
    }

    const std::array<std::pair<const char*, const char*>, 2> candidates = {{
        {".wav", "mix wav"},
        {".aud", "mix aud"}
    }};
    for (const MixArchive& archive : assetIndex.readableAudioMixes) {
        for (const auto& [extension, format] : candidates) {
            const std::string fileName = sample + extension;
            if (archive.findEntryByName(fileName) == nullptr) {
                continue;
            }
            assetIndex.sampleStems.insert(sample);
            auto [inserted, _] = assetIndex.sampleResources.insert_or_assign(
                sample,
                AssetAudioIndex::SampleResource{
                    archive.getPath() + "|" + fileName,
                    format,
                    true,
                    true
                }
            );
            return &inserted->second;
        }
    }

    return existing == assetIndex.sampleResources.end()
        ? nullptr
        : &existing->second;
}

void recordIntentSamples(
    AudioIntentResolution& resolution,
    const std::vector<std::string>& tokens,
    AssetAudioIndex& assetIndex
) {
    std::set<std::string> seenSamples;
    for (const std::string& token : tokens) {
        const std::string sample = normalizeSampleName(token);
        if (sample.empty() || !seenSamples.insert(sample).second) {
            continue;
        }

        AudioIntentSampleCandidate candidate;
        candidate.sampleName = sample;
        const AssetAudioIndex::SampleResource* resource =
            resolveAssetAudioResource(assetIndex, sample);
        candidate.resolved = resource != nullptr;
        if (candidate.resolved) {
            candidate.resourcePath = resource->path;
            candidate.resourceFormat = resource->format;
            candidate.decodable = resource->decodable;
            candidate.playable = resource->playable;
            if (!candidate.playable) {
                if (candidate.resourceFormat == "csf vox label") {
                    candidate.blocker =
                        "csf-vox-label-resolved-playable-voice-payload-not-exposed";
                } else if (!candidate.decodable) {
                    candidate.blocker =
                        "resource-exists-decoder-path-not-available";
                } else {
                    candidate.blocker =
                        "resource-exists-runtime-playback-path-not-available";
                }
            }
        } else {
            candidate.blocker = "resource-missing-from-indexed-audio-sources";
        }
        resolution.samples.push_back(candidate);
        ++resolution.sampleReferences;
        if (candidate.resolved) {
            ++resolution.resolvedSamples;
        } else {
            ++resolution.missingSamples;
        }
    }
}

AudioIntentResolution resolveSemanticAudioIntent(
    const SemanticAudioIntent& intent,
    const IniFile* rulesIni,
    const std::string& rulesPath,
    const IniFile* soundIni,
    const std::string& soundPath,
    const IniFile* evaIni,
    const std::string& evaPath,
    AssetAudioIndex& assetIndex
) {
    AudioIntentResolution resolution;
    resolution.intent = intent.intent;
    resolution.source = intent.source;
    resolution.sectionName = intent.sectionName;
    resolution.keyName = intent.keyName;
    resolution.eventName = intent.eventName;
    resolution.resolvedEventId = intent.eventName;
    resolution.status = "unknown";

    const std::string source = intent.source;
    if (source == "rules-sound") {
        resolution.iniSourceFile = rulesPath;
        resolution.sampleSourceFile = soundPath;
        if (rulesIni == nullptr) {
            resolution.note = "rules INI missing";
            return resolution;
        }

        const std::string eventRaw =
            rulesIni->getValue(intent.sectionName, intent.keyName, "");
        std::vector<std::string> eventTokens = splitWhitespace(eventRaw);
        if (eventTokens.empty()) {
            const std::string eventId = stripAudioDecorators(eventRaw);
            if (!eventId.empty()) {
                eventTokens.push_back(eventId);
            }
        }
        if (eventTokens.empty()) {
            resolution.resolvedEventId.clear();
            resolution.note = "section/key missing or empty";
            return resolution;
        }

        resolution.resolvedEventId =
            stripAudioDecorators(eventTokens.front());
        resolution.eventName = resolution.resolvedEventId;
        if (soundIni == nullptr) {
            resolution.note = "sound INI missing";
            return resolution;
        }

        recordIntentSamples(
            resolution,
            sampleTokensForSection(*soundIni, resolution.resolvedEventId),
            assetIndex
        );
    } else if (source == "sound-section") {
        resolution.iniSourceFile = soundPath;
        resolution.sampleSourceFile = soundPath;
        resolution.resolvedEventId = intent.eventName;
        resolution.eventName = intent.eventName;
        if (soundIni == nullptr) {
            resolution.note = "sound INI missing";
            return resolution;
        }

        recordIntentSamples(
            resolution,
            sampleTokensForSection(*soundIni, intent.eventName),
            assetIndex
        );
    } else if (source == "eva") {
        resolution.iniSourceFile = evaPath;
        resolution.sampleSourceFile = evaPath;
        resolution.resolvedEventId = intent.eventName;
        resolution.eventName = intent.eventName;
        if (evaIni == nullptr) {
            resolution.note = "EVA INI missing";
            return resolution;
        }

        recordIntentSamples(
            resolution,
            sampleTokensForSection(*evaIni, intent.eventName),
            assetIndex
        );
    }

    if (resolution.sampleReferences == 0) {
        resolution.note = "no sample candidates in referenced INI section";
    } else if (resolution.missingSamples == 0) {
        resolution.status = "resolved";
    } else if (resolution.resolvedSamples > 0) {
        resolution.status = "partial";
    } else {
        resolution.status = "missing-samples";
    }

    return resolution;
}

AssetAudioIndex buildAssetAudioIndex(const std::string& assetRootPath) {
    AssetAudioIndex index;
    if (assetRootPath.empty() || !std::filesystem::exists(assetRootPath)) {
        return index;
    }

    for (const std::filesystem::directory_entry& entry :
        std::filesystem::recursive_directory_iterator(
            assetRootPath,
            std::filesystem::directory_options::skip_permission_denied
        )) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::filesystem::path path = entry.path();
        const std::string extension = lowerAscii(path.extension().string());
        if (extension == ".wav") {
            ++index.wavFiles;
            const std::string sample = lowerAscii(path.stem().string());
            index.sampleStems.insert(sample);
            index.sampleResources.try_emplace(
                sample,
                AssetAudioIndex::SampleResource{
                    pathString(path),
                    "wav",
                    true,
                    true
                }
            );
        } else if (extension == ".aud") {
            ++index.audFiles;
            const std::string sample = lowerAscii(path.stem().string());
            index.sampleStems.insert(sample);
            index.sampleResources.try_emplace(
                sample,
                AssetAudioIndex::SampleResource{
                    pathString(path),
                    "aud",
                    true,
                    true
                }
            );
        } else if (extension == ".mix" && isAudioMixName(path.filename().string())) {
            ++index.audioMixArchives;
            MixArchive archive;
            if (archive.loadFromFile(pathString(path))
                && archive.isIndexReadable()) {
                index.readableAudioMixes.push_back(std::move(archive));
            }
        }
    }

    const std::vector<AudioIndexedSample> indexedSamples =
        collectIndexedAudioSamples(assetRootPath);
    index.indexedAudioSamples = static_cast<int>(indexedSamples.size());
    for (const AudioIndexedSample& sample : indexedSamples) {
        if (!sample.name.empty()) {
            index.sampleStems.insert(sample.name);
            index.sampleResources.try_emplace(
                sample.name,
                AssetAudioIndex::SampleResource{
                    sample.sourceMixPath + "|audio.idx/audio.bag",
                    "idx/bag",
                    true,
                    true
                }
            );
        }
    }

    const std::vector<AudioIndexedSample> voxLabels =
        collectVoxLabelSamplesForCatalog(assetRootPath);
    index.indexedVoxLabels = static_cast<int>(voxLabels.size());
    for (const AudioIndexedSample& sample : voxLabels) {
        if (sample.name.empty()) {
            continue;
        }
        index.sampleStems.insert(sample.name);
        index.sampleResources.try_emplace(
            sample.name,
            AssetAudioIndex::SampleResource{
                sample.sourceMixPath + "|VOX:" + sample.name,
                "csf vox label",
                false,
                false
            }
        );
    }
    return index;
}

bool listContainsToken(
    const std::string& value,
    std::initializer_list<const char*> candidates
) {
    for (const std::string& token : splitWhitespace(value)) {
        const std::string normalized = lowerAscii(token);
        for (const char* candidate : candidates) {
            if (normalized == lowerAscii(candidate)) {
                return true;
            }
        }
    }
    return false;
}

std::string unitVoiceSide(const IniFile& rulesIni, const std::string& unitId) {
    const std::string prerequisite =
        rulesIni.getValue(unitId, "Prerequisite", "");
    if (listContainsToken(
            prerequisite,
            {"GAPILE", "GAWEAP", "GAAIRC", "GACNST"}
        )) {
        return "Allied";
    }
    if (listContainsToken(
            prerequisite,
            {"NAHAND", "NAWEAP", "NACNST", "NARADR", "NATECH"}
        )) {
        return "Soviet";
    }
    if (listContainsToken(
            prerequisite,
            {"YABRCK", "YAWEAP", "YACNST", "YATECH"}
        )) {
        return "Yuri";
    }

    const std::string houses =
        rulesIni.getValue(unitId, "RequiredHouses", "")
        + ","
        + rulesIni.getValue(unitId, "Owner", "");
    const bool allied = listContainsToken(
        houses,
        {"British", "French", "Germans", "Americans", "Alliance"}
    );
    const bool soviet = listContainsToken(
        houses,
        {"Russians", "Confederation", "Africans", "Arabs"}
    );
    const bool yuri = listContainsToken(houses, {"YuriCountry", "Yuri"});
    if (allied && !soviet && !yuri) {
        return "Allied";
    }
    if (soviet && !allied && !yuri) {
        return "Soviet";
    }
    if (yuri && !allied && !soviet) {
        return "Yuri";
    }
    return "Shared/Other";
}

AudioUnitVoiceCoverage& unitVoiceCoverageForSide(
    AudioCatalogReport& report,
    const std::string& side
) {
    const auto existing = std::find_if(
        report.unitVoiceCoverageBySide.begin(),
        report.unitVoiceCoverageBySide.end(),
        [&side](const AudioUnitVoiceCoverage& coverage) {
            return coverage.side == side;
        }
    );
    if (existing != report.unitVoiceCoverageBySide.end()) {
        return *existing;
    }
    report.unitVoiceCoverageBySide.push_back(AudioUnitVoiceCoverage{});
    report.unitVoiceCoverageBySide.back().side = side;
    return report.unitVoiceCoverageBySide.back();
}

void recordMissingUnitVoiceExample(
    AudioCatalogReport& report,
    const std::string& example
) {
    if (report.missingUnitVoiceExamples.size() < 24U) {
        report.missingUnitVoiceExamples.push_back(example);
    }
}

void auditUnitVoiceCoverage(
    AudioCatalogReport& report,
    const IniFile& rulesIni,
    const IniFile& soundIni,
    AssetAudioIndex& assetIndex
) {
    for (const char* side : {"Allied", "Soviet", "Yuri", "Shared/Other"}) {
        static_cast<void>(unitVoiceCoverageForSide(report, side));
    }
    const char* typeLists[] = {
        "InfantryTypes",
        "VehicleTypes",
        "AircraftTypes"
    };
    const char* voiceKeys[] = {
        "VoiceSelect",
        "VoiceMove",
        "VoiceAttack",
        "VoiceFeedback",
        "VoiceSpecialAttack",
        "VoiceSecondaryWeaponAttack",
        "VoiceEnter",
        "VoiceCapture",
        "VoiceHarvest",
        "VoiceDeploy",
        "VoiceCrashing",
        "VoiceSelectDeactivated",
        "VoiceSelectEnslaved"
    };

    std::unordered_set<std::string> seenUnits;
    for (const char* typeList : typeLists) {
        const IniFile::Section* list = rulesIni.getSection(typeList);
        if (list == nullptr) {
            continue;
        }
        for (const auto& [key, rawUnitId] : *list) {
            (void)key;
            const std::vector<std::string> unitTokens =
                splitWhitespace(rawUnitId);
            if (unitTokens.empty()) {
                continue;
            }
            const std::string unitId = unitTokens.front();
            const std::string normalizedUnit = lowerAscii(unitId);
            if (!seenUnits.insert(normalizedUnit).second
                || rulesIni.getSection(unitId) == nullptr) {
                continue;
            }

            AudioUnitVoiceCoverage& sideCoverage = unitVoiceCoverageForSide(
                report,
                unitVoiceSide(rulesIni, unitId)
            );
            ++report.unitVoiceTypes;
            ++sideCoverage.unitTypes;
            bool unitHasVoices = false;

            for (const char* voiceKey : voiceKeys) {
                const std::vector<std::string> events = splitWhitespace(
                    rulesIni.getValue(unitId, voiceKey, "")
                );
                for (const std::string& eventName : events) {
                    if (isNullSampleName(eventName)) {
                        continue;
                    }
                    unitHasVoices = true;
                    ++report.unitVoiceEventReferences;
                    ++sideCoverage.eventReferences;
                    if (soundIni.getSection(eventName) == nullptr) {
                        ++report.unitVoiceMissingEvents;
                        ++sideCoverage.missingEventSections;
                        recordMissingUnitVoiceExample(
                            report,
                            unitId + ":" + voiceKey + "->" + eventName
                                + " (event missing)"
                        );
                        continue;
                    }
                    ++sideCoverage.eventSectionsFound;

                    const std::vector<std::string> samples =
                        sampleTokensForSection(soundIni, eventName);
                    if (samples.empty()) {
                        if (isIntentionalSilentSoundSection(
                                soundIni,
                                eventName
                            )) {
                            ++report.unitVoiceIntentionalSilentEvents;
                            ++sideCoverage.intentionalSilentEvents;
                            continue;
                        }
                        ++report.unitVoiceMissingSamples;
                        ++sideCoverage.missingSamples;
                        recordMissingUnitVoiceExample(
                            report,
                            unitId + ":" + eventName + " (no samples)"
                        );
                        continue;
                    }
                    for (const std::string& rawSample : samples) {
                        const std::string sample = normalizeSampleName(rawSample);
                        if (sample.empty()) {
                            continue;
                        }
                        ++report.unitVoiceSampleReferences;
                        ++sideCoverage.sampleReferences;
                        const AssetAudioIndex::SampleResource* resource =
                            resolveAssetAudioResource(assetIndex, sample);
                        if (resource != nullptr && resource->playable) {
                            ++report.unitVoiceResolvedSamples;
                            ++sideCoverage.resolvedSamples;
                        } else {
                            ++report.unitVoiceMissingSamples;
                            ++sideCoverage.missingSamples;
                            recordMissingUnitVoiceExample(
                                report,
                                unitId + ":" + eventName + "->" + sample
                                    + " (sample missing)"
                            );
                        }
                    }
                }
            }

            if (unitHasVoices) {
                ++report.unitVoiceTypesWithVoices;
                ++sideCoverage.unitTypesWithVoices;
            }
        }
    }

    // VoiceIFVRepair is an [AudioVisual] event, not a UnitType field.  Keep it in
    // the resource audit without pretending that every unit declares it.
    AudioUnitVoiceCoverage& sharedCoverage =
        unitVoiceCoverageForSide(report, "Shared/Other");
    for (const std::string& eventName : splitWhitespace(
            rulesIni.getValue("AudioVisual", "VoiceIFVRepair", "")
        )) {
        if (isNullSampleName(eventName)) {
            continue;
        }
        ++report.unitVoiceEventReferences;
        ++sharedCoverage.eventReferences;
        if (soundIni.getSection(eventName) == nullptr) {
            ++report.unitVoiceMissingEvents;
            ++sharedCoverage.missingEventSections;
            recordMissingUnitVoiceExample(
                report,
                "AudioVisual:VoiceIFVRepair->" + eventName + " (event missing)"
            );
            continue;
        }
        ++sharedCoverage.eventSectionsFound;
        const std::vector<std::string> samples =
            sampleTokensForSection(soundIni, eventName);
        if (samples.empty()) {
            if (isIntentionalSilentSoundSection(soundIni, eventName)) {
                ++report.unitVoiceIntentionalSilentEvents;
                ++sharedCoverage.intentionalSilentEvents;
                continue;
            }
            ++report.unitVoiceMissingSamples;
            ++sharedCoverage.missingSamples;
            recordMissingUnitVoiceExample(
                report,
                "AudioVisual:VoiceIFVRepair->" + eventName + " (no samples)"
            );
            continue;
        }
        for (const std::string& rawSample : samples) {
            const std::string sample = normalizeSampleName(rawSample);
            if (sample.empty()) {
                continue;
            }
            ++report.unitVoiceSampleReferences;
            ++sharedCoverage.sampleReferences;
            const AssetAudioIndex::SampleResource* resource =
                resolveAssetAudioResource(assetIndex, sample);
            if (resource != nullptr && resource->playable) {
                ++report.unitVoiceResolvedSamples;
                ++sharedCoverage.resolvedSamples;
            } else {
                ++report.unitVoiceMissingSamples;
                ++sharedCoverage.missingSamples;
                recordMissingUnitVoiceExample(
                    report,
                    "AudioVisual:VoiceIFVRepair->" + eventName + "->" + sample
                        + " (sample missing)"
                );
            }
        }
    }

    report.unitVoiceRuntimeTriggerableFields = {
        "VoiceSelect",
        "VoiceMove",
        "VoiceAttack",
        "VoiceFeedback",
        "VoiceSecondaryWeaponAttack",
        "VoiceEnter",
        "VoiceCapture",
        "VoiceHarvest",
        "VoiceDeploy",
        "VoiceCrashing",
        "VoiceSelectEnslaved",
        "VoiceSpecialAttack",
        "VoiceIFVRepair",
        "VoiceSelectDeactivated"
    };
    report.unitVoiceRuntimeNotTriggerableFields.clear();
}

} // namespace

AudioCatalogReport buildAudioCatalogReport(
    const IniFile* soundIni,
    const std::string& soundPath,
    const IniFile* evaIni,
    const std::string& evaPath,
    const std::string& assetRootPath
) {
    AudioCatalogReport report;
    report.soundPath = soundPath;
    report.evaPath = evaPath;
    report.assetRootPath = assetRootPath;
    report.rulesPath = firstExistingRulesIniPath(assetRootPath, soundPath);
    auditThemeMusic(report, assetRootPath, soundPath);

    IniFile rulesIni;
    const IniFile* rulesIniForIntents = nullptr;
    if (!report.rulesPath.empty() && rulesIni.loadFromFile(report.rulesPath)) {
        rulesIniForIntents = &rulesIni;
    } else {
        report.warnings.push_back(
            "rulesmd.ini or rules.ini was not found; rules-driven UI sound intents "
            "remain unknown."
        );
    }

    AssetAudioIndex assetIndex = buildAssetAudioIndex(assetRootPath);
    report.extractedWavFiles = assetIndex.wavFiles;
    report.extractedAudFiles = assetIndex.audFiles;
    report.audioMixArchives = assetIndex.audioMixArchives;
    report.indexedAudioSamples = assetIndex.indexedAudioSamples;
    report.indexedVoxLabels = assetIndex.indexedVoxLabels;

    if (soundIni == nullptr) {
        report.errorMessage = "soundmd.ini or sound.ini was not found.";
        report.warnings.push_back(
            "Audio events cannot be resolved until soundmd.ini is available."
        );
        return report;
    }

    const IniFile::Section* soundList = soundIni->getSection("SoundList");
    if (soundList == nullptr) {
        report.errorMessage = "[SoundList] section was not found.";
        return report;
    }

    std::set<std::string> uniqueSamples;
    std::set<std::string> missingSamples;
    std::set<std::string> evaUniqueSamples;
    std::set<std::string> evaMissingSamples;
    const auto recordSamples = [&](
        const std::vector<std::string>& samples,
        bool eva
    ) {
        for (const std::string& sampleRaw : samples) {
            const std::string sample = normalizeSampleName(sampleRaw);
            if (sample.empty()) {
                continue;
            }

            if (eva) {
                ++report.evaSampleReferences;
                evaUniqueSamples.insert(sample);
                if (resolveAssetAudioResource(assetIndex, sample) == nullptr) {
                    evaMissingSamples.insert(sample);
                }
                continue;
            }

            ++report.sampleReferences;
            uniqueSamples.insert(sample);
            if (resolveAssetAudioResource(assetIndex, sample) == nullptr) {
                missingSamples.insert(sample);
            }
        }
    };

    for (const auto& [key, eventNameRaw] : *soundList) {
        (void)key;
        const std::string eventName = trimAscii(stripAudioComment(eventNameRaw));
        if (eventName.empty()) {
            continue;
        }

        ++report.soundListEntries;
        const IniFile::Section* eventSection = soundIni->getSection(eventName);
        if (eventSection == nullptr) {
            continue;
        }

        ++report.soundSectionsFound;
        const std::vector<std::string> samples =
            sampleTokensForSection(*soundIni, eventName);
        if (!samples.empty()) {
            ++report.eventsWithSamples;
        }

        recordSamples(samples, false);
    }

    report.uniqueSamples = static_cast<int>(uniqueSamples.size());
    report.missingSamples = static_cast<int>(missingSamples.size());
    report.resolvedSamples = report.uniqueSamples - report.missingSamples;

    std::set<std::string> missingSampleExamples = missingSamples;
    if (evaIni != nullptr) {
        report.evaSections = static_cast<int>(evaIni->getSectionNames().size());
        for (const std::string& sectionName : evaIni->getSectionNames()) {
            recordSamples(sampleTokensForSection(*evaIni, sectionName), true);
        }
        report.evaMissingSamples = static_cast<int>(evaMissingSamples.size());
        report.evaResolvedSamples =
            static_cast<int>(evaUniqueSamples.size()) - report.evaMissingSamples;
        missingSampleExamples.insert(
            evaMissingSamples.begin(),
            evaMissingSamples.end()
        );
    } else {
        report.warnings.push_back(
            "eva.ini was not found; EVA voice events are not cataloged yet."
        );
    }

    if (rulesIniForIntents != nullptr) {
        auditUnitVoiceCoverage(
            report,
            *rulesIniForIntents,
            *soundIni,
            assetIndex
        );
        if (report.unitVoiceMissingEvents > 0
            || report.unitVoiceMissingSamples > 0) {
            report.warnings.push_back(
                "One or more declared unit voice events do not resolve to a "
                "playable original sample."
            );
        }
    }

    const SemanticAudioIntent focusedIntents[] = {
        {
            "SidebarButtonClick",
            "rules-sound",
            "AudioVisual",
            "GUIMainButtonSound",
            ""
        },
        {
            "SidebarTabClick",
            "rules-sound",
            "AudioVisual",
            "GUITabSound",
            ""
        },
        {
            "BuildQueued",
            "rules-sound",
            "AudioVisual",
            "GUIBuildSound",
            ""
        },
        {
            "BuildStartedEVA",
            "eva",
            "EVA_Building",
            "Russian/Allied/Yuri",
            "EVA_Building"
        },
        {
            "TrainingStartedEVA",
            "eva",
            "EVA_Training",
            "Russian/Allied/Yuri",
            "EVA_Training"
        },
        {
            "BuildingReady/ConstructionComplete",
            "eva",
            "EVA_ConstructionComplete",
            "Russian/Allied/Yuri",
            "EVA_ConstructionComplete"
        },
        {
            "BuildCanceled",
            "eva",
            "EVA_Canceled",
            "Russian/Allied/Yuri",
            "EVA_Canceled"
        },
        {
            "PlacementConfirmed",
            "rules-sound",
            "AudioVisual",
            "BuildingSlam",
            ""
        },
        {
            "CannotDeployHere",
            "eva",
            "EVA_CannotDeployHere",
            "Russian/Allied/Yuri",
            "EVA_CannotDeployHere"
        },
        {
            "SidebarScold",
            "rules-sound",
            "AudioVisual",
            "ScoldSound",
            ""
        },
        {
            "Repair",
            "rules-sound",
            "AudioVisual",
            "GUITabSound",
            ""
        },
        {
            "Sell",
            "rules-sound",
            "AudioVisual",
            "GUITabSound",
            ""
        },
        {
            "BuildingRepairedSfx",
            "rules-sound",
            "AudioVisual",
            "BuildingRepairedSound",
            ""
        },
        {
            "SellStructureSfx",
            "rules-sound",
            "AudioVisual",
            "SellSound",
            ""
        },
        {
            "Repairing",
            "eva",
            "EVA_Repairing",
            "Russian/Allied/Yuri",
            "EVA_Repairing"
        },
        {
            "StructureSold",
            "eva",
            "EVA_StructureSold",
            "Russian/Allied/Yuri",
            "EVA_StructureSold"
        },
        {
            "NewConstructionOptions",
            "eva",
            "EVA_NewConstructionOptions",
            "Russian/Allied/Yuri",
            "EVA_NewConstructionOptions"
        },
        {
            "BaseUnderAttack",
            "eva",
            "EVA_OurBaseIsUnderAttack",
            "Russian/Allied/Yuri",
            "EVA_OurBaseIsUnderAttack"
        },
        {
            "BaseUnderAttackSiren",
            "rules-sound",
            "AudioVisual",
            "BaseUnderAttackSound",
            ""
        },
        {
            "BuildingCaptured",
            "eva",
            "EVA_BuildingCaptured",
            "Russian/Allied/Yuri",
            "EVA_BuildingCaptured"
        },
        {
            "StructureGarrisoned",
            "eva",
            "EVA_StructureGarrisoned",
            "Russian/Allied/Yuri",
            "EVA_StructureGarrisoned"
        },
        {
            "StructureRepaired",
            "eva",
            "EVA_StructureRepaired",
            "Russian/Allied/Yuri",
            "EVA_StructureRepaired"
        },
        {
            "LowPower",
            "eva",
            "EVA_LowPower",
            "Russian/Allied/Yuri",
            "EVA_LowPower"
        },
        {
            "PowerOn",
            "sound-section",
            "soundmd.ini",
            "section",
            "PowerOn"
        },
        {
            "PowerOff",
            "sound-section",
            "soundmd.ini",
            "section",
            "PowerOff"
        },
        {
            "InsufficientFunds",
            "eva",
            "EVA_InsufficientFunds",
            "Russian/Allied/Yuri",
            "EVA_InsufficientFunds"
        },
        {
            "UnitReady",
            "eva",
            "EVA_UnitReady",
            "Russian/Allied/Yuri",
            "EVA_UnitReady"
        }
    };
    for (const SemanticAudioIntent& intent : focusedIntents) {
        AudioIntentResolution resolution =
            resolveSemanticAudioIntent(
                intent,
                rulesIniForIntents,
                report.rulesPath,
                soundIni,
                soundPath,
                evaIni,
                evaPath,
                assetIndex
            );
        report.focusedIntentResolvedSamples += resolution.resolvedSamples;
        report.focusedIntentMissingSamples += resolution.missingSamples;
        report.focusedIntents.push_back(std::move(resolution));
    }
    report.focusedIntentCount =
        static_cast<int>(report.focusedIntents.size());

    for (const std::string& sample : missingSampleExamples) {
        if (report.missingSampleExamples.size() >= 20) {
            break;
        }
        report.missingSampleExamples.push_back(sample);
    }

    if (report.audioMixArchives > 0 && report.resolvedSamples == 0) {
        report.warnings.push_back(
            "Audio MIX archives are present, but no referenced samples resolved through "
            "loose WAV/AUD files or audio.idx/audio.bag."
        );
    }

    if (report.extractedWavFiles == 0 && report.extractedAudFiles == 0) {
        report.warnings.push_back(
            "No extracted WAV/AUD files were found under the asset root."
        );
    }

    const int unresolvedFocusedIntents = static_cast<int>(std::count_if(
        report.focusedIntents.begin(),
        report.focusedIntents.end(),
        [](const AudioIntentResolution& resolution) {
            return resolution.status != "resolved";
        }
    ));
    const bool inputsComplete = rulesIniForIntents != nullptr
        && evaIni != nullptr
        && report.themeEntries > 0;
    report.success = inputsComplete
        && report.soundSectionsFound == report.soundListEntries
        && report.missingSamples == 0
        && report.evaMissingSamples == 0
        && report.unitVoiceMissingEvents == 0
        && report.unitVoiceMissingSamples == 0
        && report.focusedIntentMissingSamples == 0
        && unresolvedFocusedIntents == 0
        && report.missingMusicTracks == 0;
    if (!report.success) {
        std::ostringstream error;
        error << "audio audit incomplete: soundSections="
              << report.soundSectionsFound << "/" << report.soundListEntries
              << " missingSamples=" << report.missingSamples
              << " evaMissing=" << report.evaMissingSamples
              << " voiceMissingEvents=" << report.unitVoiceMissingEvents
              << " voiceMissingSamples=" << report.unitVoiceMissingSamples
              << " unresolvedIntents=" << unresolvedFocusedIntents
              << " musicMissing=" << report.missingMusicTracks;
        report.errorMessage = error.str();
    }
    return report;
}

void printAudioCatalogReport(std::ostream& output, const AudioCatalogReport& report) {
    output << "\n========== Audio Catalog ==========\n";
    output << "Success:           " << (report.success ? "yes" : "no") << "\n";
    if (!report.success && !report.errorMessage.empty()) {
        output << "Reason:            " << report.errorMessage << "\n";
    }
    output << "Asset root:        "
           << (report.assetRootPath.empty() ? "-" : report.assetRootPath)
           << "\n";
    output << "Sound INI:         "
           << (report.soundPath.empty() ? "-" : report.soundPath)
           << "\n";
    output << "EVA INI:           "
           << (report.evaPath.empty() ? "-" : report.evaPath)
           << "\n";
    output << "Rules INI:         "
           << (report.rulesPath.empty() ? "-" : report.rulesPath)
           << "\n";
    output << "Theme INI:         "
           << (report.themePath.empty() ? "-" : report.themePath)
           << "\n";
    output << "Music tracks:      " << report.normalMusicTracks
           << " resolved=" << report.resolvedMusicTracks
           << " missing=" << report.missingMusicTracks
           << " themeEntries=" << report.themeEntries << "\n";
    output << "Sound events:      " << report.soundListEntries << "\n";
    output << "Event sections:    " << report.soundSectionsFound << "\n";
    output << "Events w/ samples: " << report.eventsWithSamples << "\n";
    output << "Sample refs:       " << report.sampleReferences << "\n";
    output << "Unique samples:    " << report.uniqueSamples << "\n";
    output << "Resolved samples:  " << report.resolvedSamples << "\n";
    output << "Missing samples:   " << report.missingSamples << "\n";
    output << "Extracted WAV:     " << report.extractedWavFiles << "\n";
    output << "Extracted AUD:     " << report.extractedAudFiles << "\n";
    output << "Audio MIX files:   " << report.audioMixArchives << "\n";
    output << "Indexed audio:     " << report.indexedAudioSamples << "\n";
    output << "Indexed VOX labels: " << report.indexedVoxLabels << "\n";
    output << "Semantic intents:  " << report.focusedIntentCount
           << " resolvedSamples="
           << report.focusedIntentResolvedSamples
           << " missingSamples="
           << report.focusedIntentMissingSamples
           << "\n";
    output << "EVA sections:      " << report.evaSections << "\n";
    output << "EVA sample refs:   " << report.evaSampleReferences << "\n";
    output << "EVA resolved:      " << report.evaResolvedSamples << "\n";
    output << "EVA missing:       " << report.evaMissingSamples << "\n";
    output << "Unit voice types:  " << report.unitVoiceTypes
           << " withVoices=" << report.unitVoiceTypesWithVoices << "\n";
    output << "Unit voice events: " << report.unitVoiceEventReferences
           << " missingEvents=" << report.unitVoiceMissingEvents
           << " intentionalSilent="
           << report.unitVoiceIntentionalSilentEvents << "\n";
    output << "Unit voice samples: " << report.unitVoiceSampleReferences
           << " resolved=" << report.unitVoiceResolvedSamples
           << " missing=" << report.unitVoiceMissingSamples << "\n";
    output << "Unit voice runtime triggerable: ";
    if (report.unitVoiceRuntimeTriggerableFields.empty()) {
        output << "-";
    } else {
        for (std::size_t index = 0;
                index < report.unitVoiceRuntimeTriggerableFields.size();
                ++index) {
            if (index > 0U) {
                output << ",";
            }
            output << report.unitVoiceRuntimeTriggerableFields[index];
        }
    }
    output << "\nUnit voice runtime not-triggerable: ";
    if (report.unitVoiceRuntimeNotTriggerableFields.empty()) {
        output << "-";
    } else {
        for (std::size_t index = 0;
                index < report.unitVoiceRuntimeNotTriggerableFields.size();
                ++index) {
            if (index > 0U) {
                output << ",";
            }
            output << report.unitVoiceRuntimeNotTriggerableFields[index];
        }
    }
    output << "\n";

    if (!report.unitVoiceCoverageBySide.empty()) {
        output << "Unit voice coverage by side:\n";
        for (const AudioUnitVoiceCoverage& coverage :
                report.unitVoiceCoverageBySide) {
            output << "- " << coverage.side
                   << " units=" << coverage.unitTypes
                   << " voiced=" << coverage.unitTypesWithVoices
                   << " events=" << coverage.eventReferences
                   << " eventSections=" << coverage.eventSectionsFound
                   << " missingEvents=" << coverage.missingEventSections
                   << " intentionalSilent="
                   << coverage.intentionalSilentEvents
                   << " samples=" << coverage.sampleReferences
                   << " resolved=" << coverage.resolvedSamples
                   << " missing=" << coverage.missingSamples
                   << "\n";
        }
    }

    if (!report.focusedIntents.empty()) {
        output << "\nSemantic audio intents:\n";
        for (const AudioIntentResolution& intent : report.focusedIntents) {
            output << "- "
                   << intent.intent
                   << " source="
                   << intent.source
                   << " ini="
                   << (intent.iniSourceFile.empty()
                           ? "-"
                           : intent.iniSourceFile)
                   << " section="
                   << (intent.sectionName.empty() ? "-" : intent.sectionName)
                   << " key="
                   << (intent.keyName.empty() ? "-" : intent.keyName)
                   << " id="
                   << (intent.resolvedEventId.empty()
                           ? "-"
                           : intent.resolvedEventId)
                   << " sampleIni="
                   << (intent.sampleSourceFile.empty()
                           ? "-"
                           : intent.sampleSourceFile)
                   << " status="
                   << (intent.status.empty() ? "-" : intent.status)
                   << " refs="
                   << intent.sampleReferences
                   << " resolved="
                   << intent.resolvedSamples
                   << " missing="
                   << intent.missingSamples
                   << " samples=";
            if (intent.samples.empty()) {
                output << "-";
            } else {
                for (std::size_t index = 0; index < intent.samples.size(); ++index) {
                    if (index > 0U) {
                        output << ",";
                    }
                    const AudioIntentSampleCandidate& sample =
                        intent.samples[index];
                    output << sample.sampleName
                           << (sample.resolved ? ":exists=yes" : ":exists=no")
                           << ":decodable="
                           << (sample.decodable ? "yes" : "no")
                           << ":playable="
                           << (sample.playable ? "yes" : "no");
                    if (!sample.resourceFormat.empty()) {
                        output << ":format=" << sample.resourceFormat;
                    }
                    if (!sample.resourcePath.empty()) {
                        output << ":path=" << sample.resourcePath;
                    }
                    if (!sample.blocker.empty()) {
                        output << ":blocker=" << sample.blocker;
                    }
                }
            }
            if (!intent.note.empty()) {
                output << " note=" << intent.note;
            }
            output << "\n";
        }
    }

    if (!report.missingSampleExamples.empty()) {
        output << "\nMissing sample examples:\n";
        for (const std::string& sample : report.missingSampleExamples) {
            output << "- " << sample << "\n";
        }
    }

    if (!report.missingUnitVoiceExamples.empty()) {
        output << "\nMissing unit voice examples:\n";
        for (const std::string& example : report.missingUnitVoiceExamples) {
            output << "- " << example << "\n";
        }
    }

    if (!report.resolvedMusicTrackPaths.empty()) {
        output << "\nResolved background music:\n";
        for (const std::string& path : report.resolvedMusicTrackPaths) {
            output << "- " << path << "\n";
        }
    }
    if (!report.missingMusicTrackExamples.empty()) {
        output << "\nMissing background music:\n";
        for (const std::string& track : report.missingMusicTrackExamples) {
            output << "- " << track << "\n";
        }
    }

    if (!report.warnings.empty()) {
        output << "\nWarnings:\n";
        for (const std::string& warning : report.warnings) {
            output << "- " << warning << "\n";
        }
    }
}
