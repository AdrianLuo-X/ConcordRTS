#include "CampaignDatabase.h"

#include "AudioSampleResolver.h"
#include "IniFile.h"
#include "MixArchive.h"
#include "RuntimeCampaignCoverage.h"
#include "RuntimeCampaignDataSource.h"
#include "RuntimeCampaignMovieResolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace {

namespace fs = std::filesystem;

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
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool isPlaceholder(const std::string& value) {
    const std::string normalized = lowerAscii(trimAscii(value));
    return normalized.empty()
        || normalized == "<none>"
        || normalized == "none"
        || normalized == "-";
}

bool regularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

std::string pathString(const fs::path& path) {
    std::error_code error;
    const fs::path canonical = fs::weakly_canonical(path, error);
    return (error ? path.lexically_normal() : canonical).generic_string();
}

bool parseInt(const std::string& value, int& output) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(trimAscii(value), &consumed);
        if (consumed != trimAscii(value).size()) {
            return false;
        }
        output = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<fs::path> existingRoots(const CampaignDatabaseOptions& options) {
    // Mods are mounted after local data and local data after the original
    // game.  Search in that same precedence order so a replacement selector
    // is the one that drives the campaign database.
    std::vector<fs::path> roots;
    for (const std::string& root : {
            options.modRootPath,
            options.localDataRootPath,
            options.resourceRootPath
        }) {
        if (root.empty()) {
            continue;
        }
        const fs::path path(root);
        std::error_code error;
        if (fs::is_directory(path, error)) {
            roots.push_back(path);
        }
    }
    return roots;
}

fs::path findNamedFile(
    const std::vector<fs::path>& roots,
    const std::string& fileName
) {
    const std::string wanted = lowerAscii(fileName);
    for (const fs::path& root : roots) {
        const std::vector<fs::path> directCandidates{
            root / fileName,
            root / "GameMD" / fileName,
            root / "Game" / fileName,
            root / "resources" / "mix_exports" / fileName,
            root / "resources" / "mix_exports" / "ra2md" / "localmd" / fileName,
            root / "resources" / "mix_exports" / "langmd" / fileName,
            root / "resources" / "mix_exports" / "language" / fileName
        };
        for (const fs::path& candidate : directCandidates) {
            if (regularFile(candidate)) {
                return candidate;
            }
        }

        std::error_code error;
        fs::recursive_directory_iterator iterator(
            root,
            fs::directory_options::skip_permission_denied,
            error
        );
        const fs::recursive_directory_iterator end;
        std::vector<fs::path> matches;
        while (!error && iterator != end) {
            const fs::directory_entry entry = *iterator;
            if (entry.is_regular_file(error)
                && lowerAscii(entry.path().filename().string()) == wanted) {
                matches.push_back(entry.path());
            }
            iterator.increment(error);
        }
        std::sort(
            matches.begin(),
            matches.end(),
            [](const fs::path& left, const fs::path& right) {
                return lowerAscii(left.generic_string())
                    < lowerAscii(right.generic_string());
            }
        );
        if (!matches.empty()) {
            return matches.front();
        }
    }
    return {};
}

fs::path findRelativeFile(
    const std::vector<fs::path>& roots,
    const std::string& relativePath,
    const std::string& fileName
) {
    for (const fs::path& root : roots) {
        const fs::path direct = root / fs::path(relativePath);
        if (regularFile(direct)) {
            return direct;
        }
    }
    return findNamedFile(roots, fileName);
}

struct LoadedCampaignIni {
    IniFile ini;
    bool loaded = false;
    std::string source;
};

struct CampaignSourceSpec {
    std::string sourceId;
    std::string edition;
    std::string campaignIdPrefix;
    std::string battleFileName;
    std::string mapSelectionFileName;
    std::string missionFileName;
    std::string textFileName;
    std::vector<std::string> iniDirectories;
    std::vector<std::string> mixRelativePaths;
    std::string textRelativePath;
    bool baseGame = false;
};

bool loadIniFile(const fs::path& path, LoadedCampaignIni& output) {
    IniFile ini;
    if (!regularFile(path) || !ini.loadFromFile(path.string())) {
        return false;
    }
    output.ini = std::move(ini);
    output.loaded = true;
    output.source = pathString(path);
    return true;
}

bool loadIniFromMix(
    const fs::path& mixPath,
    const std::string& fileName,
    LoadedCampaignIni& output
) {
    constexpr std::size_t kMaximumCampaignIniBytes = 16U * 1024U * 1024U;
    if (!regularFile(mixPath)) {
        return false;
    }
    MixArchive archive;
    if (!archive.loadFromPath(mixPath) || !archive.isIndexReadable()) {
        return false;
    }
    const MixEntry* entry = archive.findEntryByName(fileName);
    if (entry == nullptr || entry->size == 0
        || entry->size > kMaximumCampaignIniBytes) {
        return false;
    }
    std::vector<std::uint8_t> bytes;
    std::string errorMessage;
    if (!archive.readEntryData(*entry, bytes, errorMessage)
        || bytes.empty() || bytes.size() > kMaximumCampaignIniBytes) {
        return false;
    }
    while (!bytes.empty() && bytes.back() == 0) {
        bytes.pop_back();
    }
    IniFile ini;
    if (bytes.empty() || !ini.loadFromString(std::string(
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size()
        ))) {
        return false;
    }
    output.ini = std::move(ini);
    output.loaded = true;
    output.source = pathString(mixPath) + "!" + fileName;
    return true;
}

LoadedCampaignIni loadCampaignIni(
    const std::vector<fs::path>& roots,
    const CampaignSourceSpec& spec,
    const std::string& fileName
) {
    LoadedCampaignIni output;
    for (const fs::path& root : roots) {
        std::vector<fs::path> looseCandidates;
        looseCandidates.push_back(root / fileName);
        looseCandidates.push_back(root / "Game" / fileName);
        looseCandidates.push_back(root / "GameMD" / fileName);
        for (const std::string& directory : spec.iniDirectories) {
            looseCandidates.push_back(root / fs::path(directory) / fileName);
        }
        for (const fs::path& candidate : looseCandidates) {
            if (loadIniFile(candidate, output)) {
                return output;
            }
        }

        // Preserve the mounted-layer precedence for non-standard mod layouts:
        // a loose file anywhere in this root wins over a lower-priority root.
        const fs::path recursive = findNamedFile({root}, fileName);
        if (!recursive.empty() && loadIniFile(recursive, output)) {
            return output;
        }
        for (const std::string& relativeMix : spec.mixRelativePaths) {
            if (loadIniFromMix(root / fs::path(relativeMix), fileName, output)) {
                return output;
            }
        }
    }
    return output;
}

fs::path findCampaignTextCatalog(
    const std::vector<fs::path>& roots,
    const CampaignSourceSpec& spec
) {
    for (const fs::path& root : roots) {
        for (const fs::path& candidate : {
                root / fs::path(spec.textRelativePath),
                root / spec.textFileName,
                root / "Game" / spec.textFileName,
                root / "GameMD" / spec.textFileName
            }) {
            if (regularFile(candidate)) {
                return candidate;
            }
        }
        const fs::path recursive = findNamedFile({root}, spec.textFileName);
        if (!recursive.empty()) {
            return recursive;
        }
    }
    return {};
}

std::string sectionValue(
    const IniFile& ini,
    const std::string& section,
    const std::string& key
) {
    return trimAscii(ini.getValue(section, key, ""));
}

std::vector<std::string> splitCsv(const std::string& value) {
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trimAscii(token);
        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}

std::vector<std::string> splitCsvPreservingEmpty(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (true) {
        const std::size_t separator = value.find(',', start);
        result.push_back(trimAscii(value.substr(
            start,
            separator == std::string::npos
                ? std::string::npos
                : separator - start
        )));
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1U;
    }
    return result;
}

bool sectionHasKey(
    const IniFile& ini,
    const std::string& section,
    const std::string& key
) {
    const IniFile::Section* values = ini.getSection(section);
    if (values == nullptr) {
        return false;
    }
    const std::string wanted = lowerAscii(key);
    return std::any_of(
        values->begin(),
        values->end(),
        [&wanted](const IniFile::KeyValue& entry) {
            return lowerAscii(entry.first) == wanted;
        }
    );
}

std::vector<std::string> sortedSectionNames(const IniFile& ini) {
    std::vector<std::string> names = ini.getSectionNames();
    std::sort(
        names.begin(),
        names.end(),
        [](const std::string& left, const std::string& right) {
            const std::string leftKey = lowerAscii(left);
            const std::string rightKey = lowerAscii(right);
            return leftKey == rightKey ? left < right : leftKey < rightKey;
        }
    );
    return names;
}

struct OrderedEntry {
    int number = 0;
    std::string key;
    std::string value;
};

std::vector<OrderedEntry> numericEntries(const IniFile& ini, const std::string& section) {
    std::vector<OrderedEntry> entries;
    const IniFile::Section* values = ini.getSection(section);
    if (values == nullptr) {
        return entries;
    }
    for (const auto& [key, value] : *values) {
        int number = 0;
        if (parseInt(key, number)) {
            entries.push_back({number, trimAscii(key), trimAscii(value)});
        }
    }
    std::sort(
        entries.begin(),
        entries.end(),
        [](const OrderedEntry& left, const OrderedEntry& right) {
            if (left.number != right.number) {
                return left.number < right.number;
            }
            return left.key < right.key;
        }
    );
    return entries;
}

std::vector<int> uniqueSorted(std::set<int> values) {
    return std::vector<int>(values.begin(), values.end());
}

std::set<int> eventTypesFromMap(const IniFile& mapIni) {
    std::set<int> types;
    const IniFile::Section* section = mapIni.getSection("Events");
    if (section == nullptr) {
        return types;
    }
    for (const auto& [id, value] : *section) {
        (void)id;
        const std::vector<std::string> tokens = splitCsv(value);
        if (tokens.size() < 2) {
            continue;
        }
        int count = 0;
        if (!parseInt(tokens.front(), count)) {
            continue;
        }
        std::size_t offset = 1;
        for (int index = 0;
            index < count && offset < tokens.size();
            ++index) {
            int type = 0;
            if (parseInt(tokens[offset], type)) {
                types.insert(type);
            }
            int parameterCount = 0;
            if (offset + 1 < tokens.size()) {
                parseInt(tokens[offset + 1], parameterCount);
            }
            offset += parameterCount == 2 ? 4U : 3U;
        }
    }
    return types;
}

std::set<int> actionTypesFromMap(const IniFile& mapIni) {
    std::set<int> types;
    const IniFile::Section* section = mapIni.getSection("Actions");
    if (section == nullptr) {
        return types;
    }
    for (const auto& [id, value] : *section) {
        (void)id;
        const std::vector<std::string> tokens = splitCsv(value);
        if (tokens.empty()) {
            continue;
        }
        int count = 0;
        if (!parseInt(tokens.front(), count)) {
            continue;
        }
        for (int index = 0; index < count; ++index) {
            const std::size_t offset = 1U + static_cast<std::size_t>(index) * 8U;
            if (offset >= tokens.size()) {
                break;
            }
            int type = 0;
            if (parseInt(tokens[offset], type)) {
                types.insert(type);
            }
        }
    }
    return types;
}

int sectionCount(const IniFile& ini, const std::string& section) {
    const IniFile::Section* values = ini.getSection(section);
    return values == nullptr ? 0 : static_cast<int>(values->size());
}

bool supportedEventType(int type) {
    switch (type) {
    case 0:
    case 1:
    case 4:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 36:
    case 37:
    case 41:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 51:
    case 52:
    case 55:
    case 56:
    case 57:
    case 58:
    case 59:
    case 60:
    case 61:
        return true;
    default:
        return false;
    }
}

bool supportedActionType(int type) {
    switch (type) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 51:
    case 53:
    case 54:
    case 55:
    case 56:
    case 57:
    case 60:
    case 61:
    case 62:
    case 63:
    case 64:
    case 65:
    case 66:
    case 67:
    case 68:
    case 69:
    case 70:
    case 71:
    case 72:
    case 73:
    case 74:
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
    case 80:
    case 81:
    case 82:
    case 83:
    case 88:
    case 89:
    case 90:
    case 99:
    case 100:
    case 101:
    case 102:
    case 103:
    case 104:
    case 105:
    case 106:
    case 107:
    case 108:
    case 109:
    case 110:
    case 111:
    case 112:
    case 113:
    case 114:
    case 115:
    case 116:
    case 117:
    case 118:
    case 119:
    case 120:
    case 121:
    case 122:
    case 125:
    case 127:
    case 128:
    case 129:
    case 130:
    case 131:
    case 132:
    case 133:
    case 134:
    case 135:
    case 136:
    case 137:
    case 138:
    case 139:
        return true;
    default:
        return false;
    }
}

struct ResolvedCampaignMap {
    fs::path path;
    std::string source;
};

bool safeCampaignMapFileName(const std::string& fileName) {
    const fs::path path(fileName);
    if (fileName.empty() || path.filename() != path
        || lowerAscii(path.extension().string()) != ".map") {
        return false;
    }
    return std::all_of(
        fileName.begin(),
        fileName.end(),
        [](unsigned char character) {
            return std::isalnum(character) != 0
                || character == '_' || character == '-'
                || character == '.';
        }
    );
}

std::uint64_t campaignBytesHash(const std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool materializeCampaignMap(
    const CampaignDatabaseOptions& options,
    const fs::path& mixPath,
    const std::string& fileName,
    const std::vector<std::uint8_t>& bytes,
    fs::path& outputPath
) {
    constexpr std::size_t kMaximumCampaignMapBytes = 128U * 1024U * 1024U;
    outputPath.clear();
    if (options.localDataRootPath.empty()
        || !safeCampaignMapFileName(fileName)
        || bytes.empty() || bytes.size() > kMaximumCampaignMapBytes) {
        return false;
    }
    IniFile mapProbe;
    if (!mapProbe.loadFromString(std::string(
            reinterpret_cast<const char*>(bytes.data()),
            bytes.size()
        )) || mapProbe.getSection("Basic") == nullptr) {
        return false;
    }

    std::ostringstream cacheName;
    cacheName << lowerAscii(fs::path(fileName).stem().string())
              << '_' << std::hex << std::setfill('0') << std::setw(16)
              << campaignBytesHash(bytes) << ".map";
    const fs::path cacheRoot = fs::path(options.localDataRootPath)
        / "runtime_campaign_cache" / "maps";
    std::error_code error;
    fs::create_directories(cacheRoot, error);
    if (error) {
        return false;
    }
    const fs::path candidate = cacheRoot / cacheName.str();
    const auto fileMatches = [&bytes](const fs::path& path) {
        std::error_code sizeError;
        if (!fs::is_regular_file(path, sizeError) || sizeError
            || fs::file_size(path, sizeError) != bytes.size() || sizeError) {
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            return false;
        }
        std::vector<std::uint8_t> existing(bytes.size());
        input.read(
            reinterpret_cast<char*>(existing.data()),
            static_cast<std::streamsize>(existing.size())
        );
        return input.good() && existing == bytes;
    };
    error.clear();
    if (fileMatches(candidate)) {
        outputPath = candidate;
        return true;
    }
    error.clear();
    if (fs::is_regular_file(candidate, error) && !error) {
        error.clear();
        if (!fs::remove(candidate, error) || error) {
            return false;
        }
    }

    fs::path temporary = candidate;
    temporary += ".tmp";
    error.clear();
    fs::remove(temporary, error);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return false;
        }
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
        if (!output.good()) {
            output.close();
            error.clear();
            fs::remove(temporary, error);
            return false;
        }
    }
    error.clear();
    fs::rename(temporary, candidate, error);
    if (error) {
        const bool targetReady = fileMatches(candidate);
        std::error_code cleanupError;
        fs::remove(temporary, cleanupError);
        if (!targetReady) {
            return false;
        }
    }
    outputPath = candidate;
    (void)mixPath;
    return true;
}

struct CampaignMapResolver {
    const CampaignDatabaseOptions& options;
    const std::vector<fs::path>& roots;
    bool allowBaseMapMixes = false;
    std::unordered_map<std::string, std::unique_ptr<MixArchive>> mixArchives;

    MixArchive* loadMix(const fs::path& path) {
        const std::string key = lowerAscii(pathString(path));
        const auto cached = mixArchives.find(key);
        if (cached != mixArchives.end()) {
            return cached->second.get();
        }
        std::unique_ptr<MixArchive> archive;
        if (regularFile(path)) {
            auto candidate = std::make_unique<MixArchive>();
            if (candidate->loadFromPath(path) && candidate->isIndexReadable()) {
                archive = std::move(candidate);
            }
        }
        MixArchive* result = archive.get();
        mixArchives.emplace(key, std::move(archive));
        return result;
    }

    ResolvedCampaignMap resolve(const std::string& scenario) {
        ResolvedCampaignMap resolved;
        if (isPlaceholder(scenario)) {
            return resolved;
        }
        const std::string fileName = fs::path(scenario).filename().string();
        const std::vector<std::string> relativeDirectories{
            "resources/mix_exports/mapsmd03",
            "resources/mix_exports/multimd",
            "resources/mix_exports/ra2md",
            "resources/mix_exports/maps01",
            "resources/mix_exports/maps02",
            "resources/mix_exports/ra2",
            "Maps",
            "Game",
            "GameMD"
        };
        for (const fs::path& root : roots) {
            for (const std::string& directory : relativeDirectories) {
                const fs::path candidate = root / directory / fileName;
                if (regularFile(candidate)) {
                    resolved.path = candidate;
                    resolved.source = pathString(candidate);
                    return resolved;
                }
            }
            const fs::path recursive = findNamedFile({root}, fileName);
            if (!recursive.empty()) {
                resolved.path = recursive;
                resolved.source = pathString(recursive);
                return resolved;
            }
            if (!allowBaseMapMixes || !safeCampaignMapFileName(fileName)) {
                continue;
            }
            const std::vector<fs::path> mixCandidates{
                root / "Game" / "Maps01.mix",
                root / "Game" / "Maps02.mix",
                root / "GameMD" / "Maps01.mix",
                root / "GameMD" / "Maps02.mix",
                root / "Maps01.mix",
                root / "Maps02.mix",
                root / "resources" / "mix_exports" / "Maps01.mix",
                root / "resources" / "mix_exports" / "Maps02.mix",
                root / "resources" / "mix_exports" / "ra2" / "Maps01.mix",
                root / "resources" / "mix_exports" / "ra2" / "Maps02.mix"
            };
            for (const fs::path& mixPath : mixCandidates) {
                MixArchive* archive = loadMix(mixPath);
                if (archive == nullptr) {
                    continue;
                }
                const MixEntry* entry = archive->findEntryByName(fileName);
                if (entry == nullptr || entry->size == 0
                    || entry->size > 128U * 1024U * 1024U) {
                    continue;
                }
                std::vector<std::uint8_t> bytes;
                std::string errorMessage;
                if (!archive->readEntryData(*entry, bytes, errorMessage)) {
                    continue;
                }
                fs::path materialized;
                if (materializeCampaignMap(
                        options,
                        mixPath,
                        fileName,
                        bytes,
                        materialized
                    )) {
                    resolved.path = materialized;
                    resolved.source = pathString(mixPath) + "!" + fileName;
                    return resolved;
                }
            }
        }
        return resolved;
    }
};

fs::path findMapFile(
    CampaignMapResolver& resolver,
    const std::string& scenario,
    std::string& source
) {
    const ResolvedCampaignMap resolved = resolver.resolve(scenario);
    source = resolved.source;
    return resolved.path;
}

std::vector<fs::path> findMissionVideos(
    const std::vector<fs::path>& roots,
    const std::string& sourceId,
    const std::string& videoId
) {
    std::vector<fs::path> matches;
    if (isPlaceholder(videoId)) {
        return matches;
    }
    const std::string prefix = lowerAscii(fs::path(videoId).stem().string());
    const bool baseCampaign = lowerAscii(trimAscii(sourceId)) == "ra2";
    for (const fs::path& root : roots) {
        std::vector<fs::path> rootMatches;
        const std::vector<fs::path> directRoots = baseCampaign
            ? std::vector<fs::path>{
                root / "resources" / "mix_exports" / "movies01",
                root / "resources" / "mix_exports" / "movies02",
                root / "movies01",
                root / "movies02",
                root / "Game" / "Movies",
                root / "Game"
            }
            : std::vector<fs::path>{
                root / "resources" / "mix_exports" / "movmd03",
                root / "resources" / "mix_exports" / "ra2md" / "movmd03",
                root / "movmd03",
                root / "GameMD" / "Movies",
                root / "GameMD"
            };
        for (const fs::path& videoRoot : directRoots) {
            std::error_code directoryError;
            if (!fs::is_directory(videoRoot, directoryError)) {
                continue;
            }
            std::error_code error;
            fs::recursive_directory_iterator iterator(
                videoRoot,
                fs::directory_options::skip_permission_denied,
                error
            );
            const fs::recursive_directory_iterator end;
            while (!error && iterator != end) {
                const fs::directory_entry entry = *iterator;
                if (entry.is_regular_file(error)
                    && lowerAscii(entry.path().extension().string()) == ".bik") {
                    const std::string stem = lowerAscii(entry.path().stem().string());
                    if (stem.rfind(prefix, 0) == 0) {
                        rootMatches.push_back(entry.path());
                    }
                }
                iterator.increment(error);
            }
        }
        std::sort(
            rootMatches.begin(),
            rootMatches.end(),
            [](const fs::path& left, const fs::path& right) {
                return lowerAscii(left.generic_string())
                    < lowerAscii(right.generic_string());
            }
        );
        for (const fs::path& candidate : rootMatches) {
            const bool duplicate = std::any_of(
                matches.begin(),
                matches.end(),
                [&candidate](const fs::path& existing) {
                    return lowerAscii(existing.generic_string())
                        == lowerAscii(candidate.generic_string());
                }
            );
            if (!duplicate) {
                matches.push_back(candidate);
            }
        }
    }
    return matches;
}

std::string resolveMissionVideoCandidates(
    const std::vector<fs::path>& videos,
    const std::string& videoId,
    CampaignMissionDefinition& mission,
    bool allowPrefixFallback
) {
    const std::string wantedStem = lowerAscii(
        fs::path(videoId).stem().string()
    );
    std::string resolvedPath;
    for (const fs::path& video : videos) {
        const std::string videoPath = pathString(video);
        const bool exactMatch =
            lowerAscii(video.stem().string()) == wantedStem;
        if (allowPrefixFallback || exactMatch) {
            const bool alreadyAdded = std::any_of(
                mission.videoPaths.begin(),
                mission.videoPaths.end(),
                [&videoPath](const std::string& existing) {
                    return lowerAscii(existing) == lowerAscii(videoPath);
                }
            );
            if (!alreadyAdded) {
                mission.videoIds.push_back(video.stem().string());
                mission.videoPaths.push_back(videoPath);
            }
        }
        if (resolvedPath.empty() && exactMatch) {
            resolvedPath = videoPath;
        }
    }
    if (resolvedPath.empty() && allowPrefixFallback && !videos.empty()) {
        resolvedPath = pathString(videos.front());
    }
    return resolvedPath;
}

std::string resolveMissionVideo(
    const CampaignDatabaseOptions& options,
    const std::vector<fs::path>& roots,
    const std::string& videoId,
    CampaignMissionDefinition& mission,
    bool allowPrefixFallback
) {
    const std::vector<fs::path> looseVideos = findMissionVideos(
        roots,
        mission.sourceId,
        videoId
    );
    std::string resolvedPath = resolveMissionVideoCandidates(
        looseVideos,
        videoId,
        mission,
        false
    );
    if (!resolvedPath.empty()) {
        return resolvedPath;
    }

    RuntimeCampaignMovieResolverOptions movieOptions;
    movieOptions.resourceRootPath = options.resourceRootPath;
    movieOptions.localDataRootPath = options.localDataRootPath;
    movieOptions.modRootPath = options.modRootPath;
    const RuntimeCampaignMovieResolution resolution =
        resolveRuntimeCampaignMovie(
            movieOptions,
            mission.sourceId,
            videoId
        );
    if (resolution.found) {
        const bool alreadyAdded = std::any_of(
            mission.videoPaths.begin(),
            mission.videoPaths.end(),
            [&resolution](const std::string& existing) {
                return lowerAscii(existing)
                    == lowerAscii(resolution.referencePath);
            }
        );
        if (!alreadyAdded) {
            mission.videoIds.push_back(resolution.movieId);
            mission.videoPaths.push_back(resolution.referencePath);
        }
        return resolution.referencePath;
    }
    if (!allowPrefixFallback) {
        return {};
    }
    return resolveMissionVideoCandidates(
        looseVideos,
        videoId,
        mission,
        true
    );
}

std::string campaignIdForSection(const std::string& section) {
    return lowerAscii(trimAscii(section));
}

std::string defaultCountryForCampaign(
    const std::string& campaignId,
    const std::string& playerHouse
) {
    if (!isPlaceholder(playerHouse)
        && lowerAscii(playerHouse) != "player house") {
        return playerHouse;
    }
    const std::string key = lowerAscii(campaignId);
    if (key.find("nod") != std::string::npos
        || key.find("sov") != std::string::npos) {
        return "Russians";
    }
    return "Americans";
}

void populateMapAudit(
    CampaignMissionDefinition& mission,
    const fs::path& mapPath,
    const std::string& source
) {
    mission.sourceMap = source.empty() ? pathString(mapPath) : source;
    IniFile mapIni;
    if (!mapIni.loadFromFile(mapPath.string())) {
        mission.status = "map INI could not be parsed";
        return;
    }

    mission.playerHouse = sectionValue(mapIni, "Basic", "Player");
    mission.playerCountry = mission.playerHouse;
    mission.introVideoId = sectionValue(mapIni, "Basic", "Intro");
    mission.postScoreVideoId = sectionValue(mapIni, "Basic", "PostScore");
    mission.startingCredits = 0;
    int credits = 0;
    if (parseInt(sectionValue(mapIni, "Basic", "Credits"), credits)) {
        mission.startingCredits = std::max(0, credits);
    }
    mission.nextMissionId = sectionValue(mapIni, "Basic", "NextScenario");
    mission.official = lowerAscii(sectionValue(mapIni, "Basic", "Official")) == "yes";
    mission.oneTimeOnly = lowerAscii(sectionValue(mapIni, "Basic", "OneTimeOnly")) == "yes";
    mission.endOfGame = lowerAscii(sectionValue(mapIni, "Basic", "EndOfGame")) == "yes";

    mission.triggerCount = sectionCount(mapIni, "Triggers");
    mission.eventCount = sectionCount(mapIni, "Events");
    mission.actionCount = sectionCount(mapIni, "Actions");
    mission.tagCount = sectionCount(mapIni, "Tags");
    mission.teamTypeCount = sectionCount(mapIni, "TeamTypes");
    mission.taskForceCount = sectionCount(mapIni, "TaskForces");
    mission.scriptTypeCount = sectionCount(mapIni, "ScriptTypes");

    const RuntimeCampaignCoverageInventory coverage =
        inspectRuntimeCampaignCoverage(mapIni);
    mission.eventTypes = uniqueSorted(coverage.eventTypes);
    mission.actionTypes = uniqueSorted(coverage.actionTypes);
    mission.teamScriptActionTypes = uniqueSorted(
        coverage.teamScriptActionTypes
    );
    mission.triggerMovieIndexes = uniqueSorted(coverage.movieIndexes);
    mission.unsupportedEventTypes =
        unsupportedRuntimeCampaignCoverageEventTypes(coverage);
    mission.unsupportedActionTypes =
        unsupportedRuntimeCampaignCoverageActionTypes(coverage);
    mission.unsupportedTeamScriptActionTypes =
        unsupportedRuntimeCampaignCoverageTeamScriptActions(coverage);
    mission.coverageErrors = coverage.parseErrors;
    mission.victoryActionCount = coverage.victoryActionCount;
    mission.defeatActionCount = coverage.defeatActionCount;
    mission.reachableVictoryActionCount =
        coverage.reachableVictoryActionCount;
    mission.reachableDefeatActionCount =
        coverage.reachableDefeatActionCount;
    mission.available = true;
    mission.status = "map loaded";
}

CampaignMissionDefinition makeMission(
    const CampaignSourceSpec& sourceSpec,
    CampaignMapResolver& mapResolver,
    const IniFile* mapSelectionIni,
    const IniFile* missionIni,
    const std::string& mapSelectionSource,
    const std::string& missionMetadataSource,
    const std::string& campaignId,
    const std::string& campaignSection,
    const std::string& mapSelectionSection,
    int ordinal,
    const std::string& mapFileName,
    bool debugOnly
) {
    CampaignMissionDefinition mission;
    mission.campaignId = campaignId;
    mission.sourceId = sourceSpec.sourceId;
    mission.edition = sourceSpec.edition;
    mission.campaignSection = campaignSection;
    mission.mapSelectionSection = mapSelectionSection;
    mission.id = lowerAscii(mapSelectionSection);
    mission.ordinal = ordinal;
    mission.mapFileName = trimAscii(mapFileName);
    mission.debugOnly = debugOnly;
    mission.sourceMapSelection = mapSelectionIni == nullptr
        ? ""
        : mapSelectionSource + ":" + campaignSection + ":"
            + mapSelectionSection;

    if (mapSelectionIni != nullptr) {
        mission.displayNameKey = sectionValue(
            *mapSelectionIni,
            mapSelectionSection,
            "UIName"
        );
        if (mission.displayNameKey.empty()) {
            mission.displayNameKey = sectionValue(
                *mapSelectionIni,
                mapSelectionSection,
                "Description"
            );
        }
        mission.descriptionKey = sectionValue(
            *mapSelectionIni,
            mapSelectionSection,
            "Description"
        );
        mission.voiceOver = sectionValue(
            *mapSelectionIni,
            mapSelectionSection,
            "VoiceOver"
        );
        mission.mapVq = sectionValue(
            *mapSelectionIni,
            mapSelectionSection,
            "MapVQ"
        );
        mission.clickMap = sectionValue(
            *mapSelectionIni,
            mapSelectionSection,
            "ClickMap"
        );
        for (const std::string& token : splitCsv(sectionValue(
                 *mapSelectionIni,
                 mapSelectionSection,
                 "Targets"
             ))) {
            int target = 0;
            if (parseInt(token, target)) {
                mission.targetCells.push_back(target);
            }
        }
    }

    if (!isPlaceholder(mission.mapFileName)) {
        std::string mapSource;
        const fs::path mapPath = findMapFile(
            mapResolver,
            mission.mapFileName,
            mapSource
        );
        if (!mapPath.empty()) {
            mission.mapPath = pathString(mapPath);
            populateMapAudit(mission, mapPath, mapSource);
        } else {
            mission.status = "map file not found in mounted resources";
        }
    } else {
        mission.status = "map selector has no Scenario";
    }

    if (missionIni != nullptr && !isPlaceholder(mission.mapFileName)) {
        mission.sourceMissionMetadata = missionMetadataSource + ":"
            + mission.mapFileName;
        const std::string missionUiName = sectionValue(
            *missionIni,
            mission.mapFileName,
            "UIName"
        );
        if (!missionUiName.empty()) {
            mission.displayNameKey = missionUiName;
        }
        mission.briefingKey = sectionValue(
            *missionIni,
            mission.mapFileName,
            "Briefing"
        );
        mission.loadMessageKey = sectionValue(
            *missionIni,
            mission.mapFileName,
            "LSLoadMessage"
        );
        mission.loadBriefingKey = sectionValue(
            *missionIni,
            mission.mapFileName,
            "LSLoadBriefing"
        );
        mission.loadingBackground640 = sectionValue(
            *missionIni,
            mission.mapFileName,
            "LS640BkgdName"
        );
        mission.loadingBackground800 = sectionValue(
            *missionIni,
            mission.mapFileName,
            "LS800BkgdName"
        );
        mission.loadingPalette800 = sectionValue(
            *missionIni,
            mission.mapFileName,
            "LS800BkgdPal"
        );
    }

    mission.playerCountry = defaultCountryForCampaign(
        campaignId,
        mission.playerHouse
    );
    mission.introVideoPath = resolveMissionVideo(
        mapResolver.options,
        mapResolver.roots,
        mission.introVideoId,
        mission,
        true
    );
    mission.postScoreVideoPath = resolveMissionVideo(
        mapResolver.options,
        mapResolver.roots,
        mission.postScoreVideoId,
        mission,
        false
    );
    return mission;
}

void setSequentialNextMission(CampaignDefinition& campaign) {
    for (std::size_t index = 0; index + 1 < campaign.missions.size(); ++index) {
        if (campaign.missions[index].nextMissionId.empty()
            || isPlaceholder(campaign.missions[index].nextMissionId)
            || campaign.missions[index].nextMissionId.find('.') != std::string::npos) {
            campaign.missions[index].nextMissionId =
                campaign.missions[index + 1].id;
        } else {
            campaign.missions[index].nextMissionId = lowerAscii(
                fs::path(campaign.missions[index].nextMissionId).stem().string()
            );
        }
    }
    if (!campaign.missions.empty()) {
        campaign.missions.back().nextMissionId.clear();
    }
}

void addMapSelectionWarning(
    std::vector<std::string>& warnings,
    const CampaignDefinition& campaign,
    const std::string& section,
    const std::string& detail
) {
    warnings.push_back(
        campaign.id + "/" + lowerAscii(section)
        + ": map selection " + detail
    );
}

std::unordered_map<std::string, std::string> sectionNamesById(
    const IniFile& ini
) {
    std::unordered_map<std::string, std::string> result;
    for (const std::string& section : sortedSectionNames(ini)) {
        result.emplace(lowerAscii(section), section);
    }
    return result;
}

CampaignMapSelectionSound parseMapSelectionSound(
    const IniFile& ini,
    const CampaignDefinition& campaign,
    const std::string& section,
    const std::string& key,
    std::vector<std::string>& warnings
) {
    CampaignMapSelectionSound sound;
    if (!sectionHasKey(ini, section, key)) {
        return sound;
    }
    const std::vector<std::string> tokens = splitCsvPreservingEmpty(
        sectionValue(ini, section, key)
    );
    if (tokens.empty() || tokens.front().empty()) {
        return sound;
    }
    sound.sample = tokens.front();
    if (tokens.size() >= 2U && !tokens[1].empty()) {
        int volume = 0;
        if (parseInt(tokens[1], volume)) {
            sound.volume = volume;
            sound.volumeSpecified = true;
        } else {
            addMapSelectionWarning(
                warnings,
                campaign,
                section,
                key + " has an invalid volume"
            );
        }
    }
    if (tokens.size() > 2U) {
        addMapSelectionWarning(
            warnings,
            campaign,
            section,
            key + " has extra sound fields"
        );
    }
    return sound;
}

void parseMapSelectionConfigs(
    const IniFile& ini,
    const std::unordered_map<std::string, std::string>& sectionNames,
    CampaignDefinition& campaign,
    std::vector<std::string>& warnings
) {
    campaign.mapSelectionAnimsSection = sectionValue(
        ini,
        campaign.sourceSection,
        "Anims"
    );
    campaign.mapSelectionSoundsSection = sectionValue(
        ini,
        campaign.sourceSection,
        "Sounds"
    );

    if (!isPlaceholder(campaign.mapSelectionAnimsSection)) {
        const auto found = sectionNames.find(lowerAscii(
            campaign.mapSelectionAnimsSection
        ));
        if (found == sectionNames.end()) {
            addMapSelectionWarning(
                warnings,
                campaign,
                campaign.sourceSection,
                "Anims references missing section "
                    + campaign.mapSelectionAnimsSection
            );
        } else {
            const std::string& section = found->second;
            campaign.mapSelectionAnims.sourceSection = section;
            campaign.mapSelectionAnims.palette = sectionValue(
                ini,
                section,
                "Palette"
            );
            if (sectionHasKey(ini, section, "TextRect")) {
                const std::vector<std::string> values =
                    splitCsvPreservingEmpty(sectionValue(
                        ini,
                        section,
                        "TextRect"
                    ));
                CampaignMapSelectionRect& rect =
                    campaign.mapSelectionAnims.textRect;
                if (values.size() == 4U
                    && parseInt(values[0], rect.x)
                    && parseInt(values[1], rect.y)
                    && parseInt(values[2], rect.width)
                    && parseInt(values[3], rect.height)) {
                    rect.valid = true;
                } else {
                    addMapSelectionWarning(
                        warnings,
                        campaign,
                        section,
                        "TextRect must contain four integers"
                    );
                }
            }

            std::set<int> animationIndexes;
            for (const OrderedEntry& entry : numericEntries(ini, section)) {
                const std::vector<std::string> values =
                    splitCsvPreservingEmpty(entry.value);
                CampaignMapSelectionAnimation animation;
                animation.index = entry.number;
                if (values.size() != 4U
                    || values[0].empty()
                    || !parseInt(values[1], animation.x)
                    || !parseInt(values[2], animation.y)
                    || !parseInt(values[3], animation.rate)) {
                    addMapSelectionWarning(
                        warnings,
                        campaign,
                        section,
                        "animation " + entry.key
                            + " must be SHP,x,y,rate"
                    );
                    continue;
                }
                animation.shpFile = values[0];
                if (!animationIndexes.insert(animation.index).second) {
                    addMapSelectionWarning(
                        warnings,
                        campaign,
                        section,
                        "duplicate animation index " + entry.key
                    );
                }
                campaign.mapSelectionAnims.animations.push_back(
                    std::move(animation)
                );
            }
        }
    }

    if (!isPlaceholder(campaign.mapSelectionSoundsSection)) {
        const auto found = sectionNames.find(lowerAscii(
            campaign.mapSelectionSoundsSection
        ));
        if (found == sectionNames.end()) {
            addMapSelectionWarning(
                warnings,
                campaign,
                campaign.sourceSection,
                "Sounds references missing section "
                    + campaign.mapSelectionSoundsSection
            );
        } else {
            const std::string& section = found->second;
            CampaignMapSelectionSoundConfig& sounds =
                campaign.mapSelectionSounds;
            sounds.sourceSection = section;
            sounds.overlay = parseMapSelectionSound(
                ini, campaign, section, "Overlay", warnings
            );
            sounds.targetFlyIn = parseMapSelectionSound(
                ini, campaign, section, "TargetFlyIn", warnings
            );
            sounds.enterRegion = parseMapSelectionSound(
                ini, campaign, section, "EnterRegion", warnings
            );
            sounds.exitRegion = parseMapSelectionSound(
                ini, campaign, section, "ExitRegion", warnings
            );
            sounds.clickRegion = parseMapSelectionSound(
                ini, campaign, section, "ClickRegion", warnings
            );
        }
    }
}

std::unordered_map<std::string, std::size_t> missionIndexesByAlias(
    const CampaignDefinition& campaign
) {
    std::unordered_map<std::string, std::size_t> result;
    for (std::size_t index = 0; index < campaign.missions.size(); ++index) {
        const CampaignMissionDefinition& mission = campaign.missions[index];
        for (const std::string& alias : {
                mission.id,
                mission.mapSelectionSection,
                fs::path(mission.mapFileName).stem().string()
            }) {
            if (!alias.empty()) {
                result.emplace(lowerAscii(alias), index);
            }
        }
    }
    return result;
}

CampaignMapSelectionStage parseMapSelectionStage(
    const IniFile& ini,
    const CampaignDefinition& campaign,
    const std::string& section,
    const std::set<std::string>& retainedStageIds,
    const std::unordered_map<std::string, std::size_t>& missionIndexes,
    std::vector<std::string>& warnings
) {
    CampaignMapSelectionStage stage;
    stage.id = lowerAscii(section);
    stage.sourceSection = section;
    stage.scenario = sectionValue(ini, section, "Scenario");
    stage.description = sectionValue(ini, section, "Description");
    stage.voiceOver = sectionValue(ini, section, "VoiceOver");
    stage.mapVq = sectionValue(ini, section, "MapVQ");
    stage.overlays = splitCsv(sectionValue(ini, section, "Overlays"));
    stage.clickMap = sectionValue(ini, section, "ClickMap");

    if (sectionHasKey(ini, section, "Targets")) {
        const std::vector<std::string> values = splitCsvPreservingEmpty(
            sectionValue(ini, section, "Targets")
        );
        int declaredCount = -1;
        if (!values.empty()
            && parseInt(values.front(), declaredCount)) {
            stage.declaredTargetCount = declaredCount;
            if (declaredCount < 0) {
                addMapSelectionWarning(
                    warnings,
                    campaign,
                    section,
                    "Targets has a negative declared count"
                );
            }
        } else {
            addMapSelectionWarning(
                warnings,
                campaign,
                section,
                "Targets has an invalid declared count"
            );
        }

        const std::size_t coordinateValueCount = values.empty()
            ? 0U
            : values.size() - 1U;
        if ((coordinateValueCount % 2U) != 0U) {
            addMapSelectionWarning(
                warnings,
                campaign,
                section,
                "Targets has an odd coordinate value count"
            );
        }
        std::set<std::pair<int, int>> seenPoints;
        for (std::size_t index = 1U; index + 1U < values.size(); index += 2U) {
            CampaignMapSelectionPoint point;
            if (!parseInt(values[index], point.x)
                || !parseInt(values[index + 1U], point.y)) {
                addMapSelectionWarning(
                    warnings,
                    campaign,
                    section,
                    "Targets contains an invalid coordinate pair"
                );
                continue;
            }
            if (!seenPoints.emplace(point.x, point.y).second) {
                addMapSelectionWarning(
                    warnings,
                    campaign,
                    section,
                    "Targets contains a duplicate coordinate pair"
                );
            }
            stage.targets.push_back(point);
        }
        if (stage.declaredTargetCount >= 0
            && static_cast<std::size_t>(stage.declaredTargetCount)
                != stage.targets.size()) {
            addMapSelectionWarning(
                warnings,
                campaign,
                section,
                "Targets count mismatch: declared="
                    + std::to_string(stage.declaredTargetCount)
                    + " parsed=" + std::to_string(stage.targets.size())
            );
        }
    }

    std::set<int> colors;
    for (const OrderedEntry& entry : numericEntries(ini, section)) {
        CampaignMapSelectionChoice choice;
        choice.colorIndex = entry.number;
        choice.targetSection = trimAscii(entry.value);
        if (choice.colorIndex <= 0 || choice.colorIndex > 255) {
            addMapSelectionWarning(
                warnings,
                campaign,
                section,
                "color index out of range: " + entry.key
            );
        }
        if (!colors.insert(choice.colorIndex).second) {
            addMapSelectionWarning(
                warnings,
                campaign,
                section,
                "duplicate color edge: " + entry.key
            );
        }

        const std::string targetId = lowerAscii(choice.targetSection);
        const std::string targetStem = lowerAscii(
            fs::path(choice.targetSection).stem().string()
        );
        if (retainedStageIds.find(targetId) != retainedStageIds.end()) {
            choice.targetStageId = targetId;
        }
        auto missionFound = missionIndexes.find(targetId);
        if (missionFound == missionIndexes.end()) {
            missionFound = missionIndexes.find(targetStem);
        }
        if (missionFound != missionIndexes.end()) {
            choice.targetMissionId =
                campaign.missions[missionFound->second].id;
        }
        if (choice.targetStageId.empty()
            && choice.targetMissionId.empty()
            && ini.getSection(choice.targetSection) == nullptr) {
            addMapSelectionWarning(
                warnings,
                campaign,
                section,
                "dangling edge " + entry.key + "=" + choice.targetSection
            );
        }
        stage.choices.push_back(std::move(choice));
    }
    return stage;
}

void populateMapSelectionGraph(
    const IniFile& ini,
    const std::set<std::string>& campaignSelectorSections,
    CampaignDefinition& campaign,
    std::vector<std::string>& warnings
) {
    const std::unordered_map<std::string, std::string> sectionNames =
        sectionNamesById(ini);
    parseMapSelectionConfigs(
        ini,
        sectionNames,
        campaign,
        warnings
    );

    const std::unordered_map<std::string, std::size_t> missionIndexes =
        missionIndexesByAlias(campaign);
    std::set<std::string> retainedStageIds;
    for (const CampaignMissionDefinition& mission : campaign.missions) {
        retainedStageIds.insert(lowerAscii(mission.mapSelectionSection));
    }

    const std::string animsId = lowerAscii(
        campaign.mapSelectionAnimsSection
    );
    const std::string soundsId = lowerAscii(
        campaign.mapSelectionSoundsSection
    );
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [sectionId, section] : sectionNames) {
            if (campaignSelectorSections.find(sectionId)
                    != campaignSelectorSections.end()
                || sectionId == animsId
                || sectionId == soundsId) {
                continue;
            }
            const bool sourceRetained =
                retainedStageIds.find(sectionId) != retainedStageIds.end();
            for (const OrderedEntry& edge : numericEntries(ini, section)) {
                const std::string targetId = lowerAscii(edge.value);
                const std::string targetStem = lowerAscii(
                    fs::path(edge.value).stem().string()
                );
                const bool pointsIntoGraph =
                    retainedStageIds.find(targetId)
                        != retainedStageIds.end()
                    || missionIndexes.find(targetId) != missionIndexes.end()
                    || missionIndexes.find(targetStem) != missionIndexes.end();
                // A different route can share this selector section (base
                // RA2 Boot Camp and the Allied campaign both use [GDI]).
                // Retain backwards-only presentation nodes such as ALL00,
                // but never absorb a scenario stage owned by another route.
                if (!sourceRetained && pointsIntoGraph
                    && isPlaceholder(sectionValue(
                        ini,
                        section,
                        "Scenario"
                    ))) {
                    retainedStageIds.insert(sectionId);
                    changed = true;
                    break;
                }
                const auto targetSection = sectionNames.find(targetId);
                const bool targetIsMission =
                    missionIndexes.find(targetId) != missionIndexes.end()
                    || missionIndexes.find(targetStem) != missionIndexes.end();
                if (sourceRetained
                    && targetSection != sectionNames.end()
                    && campaignSelectorSections.find(targetId)
                        == campaignSelectorSections.end()
                    && targetId != animsId
                    && targetId != soundsId
                    && (targetIsMission || isPlaceholder(sectionValue(
                        ini,
                        targetSection->second,
                        "Scenario"
                    )))
                    && retainedStageIds.insert(targetId).second) {
                    changed = true;
                }
            }
        }
    }

    for (const std::string& stageId : retainedStageIds) {
        const auto sectionFound = sectionNames.find(stageId);
        if (sectionFound == sectionNames.end()) {
            continue;
        }
        campaign.mapSelectionStages.push_back(parseMapSelectionStage(
            ini,
            campaign,
            sectionFound->second,
            retainedStageIds,
            missionIndexes,
            warnings
        ));
    }

    std::unordered_map<std::string, std::size_t> missionById;
    for (std::size_t index = 0; index < campaign.missions.size(); ++index) {
        missionById.emplace(lowerAscii(campaign.missions[index].id), index);
    }
    for (const CampaignMapSelectionStage& stage :
         campaign.mapSelectionStages) {
        for (const CampaignMapSelectionChoice& choice : stage.choices) {
            const auto missionFound = missionById.find(
                lowerAscii(choice.targetMissionId)
            );
            if (missionFound == missionById.end()) {
                continue;
            }
            std::vector<std::string>& incoming = campaign.missions[
                missionFound->second
            ].incomingMapSelectionStages;
            if (std::find(incoming.begin(), incoming.end(), stage.id)
                == incoming.end()) {
                incoming.push_back(stage.id);
            }
        }
    }
}

} // namespace

const CampaignMapSelectionStage* CampaignDefinition::findMapSelectionStage(
    const std::string& stageId
) const {
    const std::string wanted = lowerAscii(trimAscii(stageId));
    for (const CampaignMapSelectionStage& stage : mapSelectionStages) {
        if (lowerAscii(stage.id) == wanted
            || lowerAscii(stage.sourceSection) == wanted) {
            return &stage;
        }
    }
    return nullptr;
}

const CampaignDefinition* CampaignDatabase::findCampaign(
    const std::string& id
) const {
    const std::string wanted = lowerAscii(trimAscii(id));
    for (const CampaignDefinition& campaign : campaigns) {
        if (lowerAscii(campaign.id) == wanted) {
            return &campaign;
        }
    }
    const CampaignDefinition* aliasMatch = nullptr;
    for (const CampaignDefinition& campaign : campaigns) {
        if (lowerAscii(campaign.sourceSection) != wanted) {
            continue;
        }
        if (aliasMatch != nullptr) {
            return nullptr;
        }
        aliasMatch = &campaign;
    }
    return aliasMatch;
}

const CampaignMissionDefinition* CampaignDatabase::findMission(
    const std::string& campaignId,
    const std::string& missionId
) const {
    const std::string wantedCampaign = lowerAscii(trimAscii(campaignId));
    const std::string wantedMission = lowerAscii(
        fs::path(trimAscii(missionId)).stem().string()
    );
    const CampaignDefinition* campaign = findCampaign(wantedCampaign);
    if (campaign == nullptr) {
        return nullptr;
    }
    for (const CampaignMissionDefinition& mission : campaign->missions) {
        if (lowerAscii(mission.id) == wantedMission
            || lowerAscii(fs::path(mission.mapFileName).stem().string())
                == wantedMission) {
            return &mission;
        }
    }
    return nullptr;
}

std::size_t CampaignDatabase::missionCount() const {
    std::size_t count = 0;
    for (const CampaignDefinition& campaign : campaigns) {
        count += campaign.missions.size();
    }
    return count;
}

std::size_t CampaignDatabase::availableMissionCount() const {
    std::size_t count = 0;
    for (const CampaignDefinition& campaign : campaigns) {
        for (const CampaignMissionDefinition& mission : campaign.missions) {
            if (mission.available) {
                ++count;
            }
        }
    }
    return count;
}

CampaignDatabase loadCampaignDatabase(const CampaignDatabaseOptions& options) {
    CampaignDatabase database;
    const std::vector<fs::path> roots = existingRoots(options);
    if (roots.empty()) {
        database.status = "no campaign resource roots exist";
        return database;
    }

    const CampaignSourceSpec yrSpec{
        "yr",
        "Yuri's Revenge",
        "",
        "battlemd.ini",
        "mapselmd.ini",
        "missionmd.ini",
        "ra2md.csf",
        {
            "resources/mix_exports/ra2md/localmd",
            "ra2md/localmd"
        },
        {
            "resources/mix_exports/ra2md/localmd.mix",
            "ra2md/localmd.mix",
            "localmd.mix"
        },
        "resources/mix_exports/langmd/ra2md.csf",
        false
    };
    const CampaignSourceSpec ra2Spec{
        "ra2",
        "Red Alert 2",
        "ra2-",
        "battle.ini",
        "mapsel.ini",
        "mission.ini",
        "ra2.csf",
        {
            "resources/mix_exports/ra2/local",
            "resources/mix_exports/ra2",
            "ra2/local",
            "ra2"
        },
        {
            "resources/mix_exports/ra2/local.mix",
            "ra2/local.mix",
            "Game/local.mix",
            "GameMD/local.mix",
            "local.mix"
        },
        "resources/mix_exports/language/ra2.csf",
        true
    };

    for (const CampaignSourceSpec& spec : {yrSpec, ra2Spec}) {
        LoadedCampaignIni battle = loadCampaignIni(
            roots,
            spec,
            spec.battleFileName
        );
        LoadedCampaignIni mapSelection = loadCampaignIni(
            roots,
            spec,
            spec.mapSelectionFileName
        );
        LoadedCampaignIni missionMetadata = loadCampaignIni(
            roots,
            spec,
            spec.missionFileName
        );
        const fs::path textPath = findCampaignTextCatalog(roots, spec);

        CampaignSourceDefinition sourceDefinition;
        sourceDefinition.sourceId = spec.sourceId;
        sourceDefinition.edition = spec.edition;
        sourceDefinition.battleIniPath = battle.source;
        sourceDefinition.mapSelectionIniPath = mapSelection.source;
        sourceDefinition.missionIniPath = missionMetadata.source;
        sourceDefinition.textCatalogPath = textPath.empty()
            ? ""
            : pathString(textPath);
        if (spec.sourceId == "yr") {
            // Preserve the original public fields as YR aliases so existing
            // diagnostics and callers do not silently change meaning.
            database.battleIniPath = sourceDefinition.battleIniPath;
            database.mapSelectionIniPath =
                sourceDefinition.mapSelectionIniPath;
            database.missionIniPath = sourceDefinition.missionIniPath;
            database.textCatalogPath = sourceDefinition.textCatalogPath;
        }
        if (!sourceDefinition.textCatalogPath.empty()) {
            const bool duplicate = std::any_of(
                database.textCatalogPaths.begin(),
                database.textCatalogPaths.end(),
                [&sourceDefinition](const std::string& existing) {
                    return lowerAscii(existing)
                        == lowerAscii(sourceDefinition.textCatalogPath);
                }
            );
            if (!duplicate) {
                database.textCatalogPaths.push_back(
                    sourceDefinition.textCatalogPath
                );
            }
        }
        database.sources.push_back(sourceDefinition);

        CampaignMapResolver mapResolver{
            options,
            roots,
            spec.baseGame,
            {}
        };
        const std::size_t sourceCampaignStart = database.campaigns.size();
        if (mapSelection.loaded) {
            std::vector<std::string> selectorSections;
            std::set<std::string> selectorSectionIds;
            for (const std::string& sectionName :
                 sortedSectionNames(mapSelection.ini)) {
                const std::vector<OrderedEntry> entries = numericEntries(
                    mapSelection.ini,
                    sectionName
                );
                if (entries.empty()) {
                    continue;
                }
                const bool looksLikeCampaignSelector =
                    !sectionValue(
                        mapSelection.ini,
                        sectionName,
                        "Anims"
                    ).empty()
                    || !sectionValue(
                        mapSelection.ini,
                        sectionName,
                        "Sounds"
                    ).empty();
                if (!looksLikeCampaignSelector) {
                    continue;
                }
                selectorSections.push_back(sectionName);
                selectorSectionIds.insert(lowerAscii(sectionName));
            }

            for (const std::string& sectionName : selectorSections) {
                const std::vector<OrderedEntry> entries = numericEntries(
                    mapSelection.ini,
                    sectionName
                );
                const auto appendSelectorCampaign = [
                    &database,
                    &spec,
                    &sourceDefinition,
                    &battle,
                    &mapSelection,
                    &missionMetadata,
                    &mapResolver,
                    &selectorSectionIds,
                    &sectionName,
                    &entries
                ](
                    const std::string& campaignId,
                    const std::string& battleSection,
                    const auto& includeMission
                ) {
                    CampaignDefinition campaign;
                    campaign.id = campaignId;
                    campaign.sourceId = spec.sourceId;
                    campaign.edition = spec.edition;
                    campaign.battleSection = battleSection;
                    campaign.sourceSection = sectionName;
                    campaign.textCatalogPath =
                        sourceDefinition.textCatalogPath;
                    campaign.side = sectionName;
                    campaign.displayNameKey = sectionValue(
                        mapSelection.ini,
                        sectionName,
                        "Name"
                    );
                    if (battle.loaded && !battleSection.empty()) {
                        const std::string battleDescription = sectionValue(
                            battle.ini,
                            battleSection,
                            "Description"
                        );
                        if (!battleDescription.empty()) {
                            campaign.displayNameKey = battleDescription;
                        }
                    }
                    campaign.displayName = sectionName;
                    std::set<std::string> missionSections;
                    for (const OrderedEntry& entry : entries) {
                        if (isPlaceholder(entry.value)) {
                            continue;
                        }
                        const std::string missionSection =
                            trimAscii(entry.value);
                        const std::string missionSectionId =
                            lowerAscii(missionSection);
                        if (!includeMission(missionSectionId)
                            || missionSections.find(missionSectionId)
                                != missionSections.end()) {
                            continue;
                        }
                        const std::string scenario = sectionValue(
                            mapSelection.ini,
                            missionSection,
                            "Scenario"
                        );
                        if (isPlaceholder(scenario)) {
                            continue;
                        }
                        missionSections.insert(missionSectionId);
                        campaign.missions.push_back(makeMission(
                            spec,
                            mapResolver,
                            &mapSelection.ini,
                            missionMetadata.loaded
                                ? &missionMetadata.ini
                                : nullptr,
                            mapSelection.source,
                            missionMetadata.source,
                            campaign.id,
                            sectionName,
                            missionSection,
                            entry.number,
                            scenario,
                            false
                        ));
                    }
                    if (campaign.missions.empty()) {
                        return;
                    }
                    std::sort(
                        campaign.missions.begin(),
                        campaign.missions.end(),
                        [](const CampaignMissionDefinition& left,
                           const CampaignMissionDefinition& right) {
                            return left.ordinal < right.ordinal;
                        }
                    );
                    for (std::size_t index = 0;
                        index < campaign.missions.size();
                        ++index) {
                        campaign.missions[index].ordinal =
                            static_cast<int>(index + 1U);
                    }
                    setSequentialNextMission(campaign);
                    populateMapSelectionGraph(
                        mapSelection.ini,
                        selectorSectionIds,
                        campaign,
                        database.warnings
                    );
                    database.campaigns.push_back(std::move(campaign));
                };

                const std::string selectorId = lowerAscii(sectionName);
                if (spec.baseGame && selectorId == "gdi") {
                    appendSelectorCampaign(
                        "ra2-tutorial",
                        "TUT1",
                        [](const std::string& missionId) {
                            return missionId.rfind("trn", 0) == 0;
                        }
                    );
                    appendSelectorCampaign(
                        "ra2-gdi",
                        "ALL1",
                        [](const std::string& missionId) {
                            return missionId.rfind("all", 0) == 0;
                        }
                    );
                } else if (spec.baseGame && selectorId == "nod") {
                    appendSelectorCampaign(
                        "ra2-nod",
                        "SOV1",
                        [](const std::string& missionId) {
                            return missionId.rfind("sov", 0) == 0;
                        }
                    );
                } else {
                    std::string battleSection;
                    if (selectorId == "gdi") {
                        battleSection = "ALL1";
                    } else if (selectorId == "nod") {
                        battleSection = "SOV1";
                    }
                    appendSelectorCampaign(
                        spec.campaignIdPrefix
                            + campaignIdForSection(sectionName),
                        battleSection,
                        [](const std::string&) { return true; }
                    );
                }
            }
        }

        // Keep the original battle-only fallback per source.  A valid YR
        // selector must not prevent an independently mounted RA2 battle file
        // from being considered (or vice versa).
        if (database.campaigns.size() == sourceCampaignStart && battle.loaded) {
            std::map<std::string, CampaignDefinition> grouped;
            for (const OrderedEntry& entry : numericEntries(
                     battle.ini,
                     "Battles"
                 )) {
                const std::string battleSection = trimAscii(entry.value);
                const std::string scenario = sectionValue(
                    battle.ini,
                    battleSection,
                    "Scenario"
                );
                if (isPlaceholder(scenario)) {
                    continue;
                }
                const bool debugOnly = lowerAscii(sectionValue(
                    battle.ini,
                    battleSection,
                    "DebugOnly"
                )) == "yes";
                if (debugOnly && !options.includeDebugOnlyMissions) {
                    continue;
                }
                std::string campaignKey = lowerAscii(battleSection);
                const std::size_t separator =
                    campaignKey.find_first_of("0123456789");
                if (separator != std::string::npos) {
                    campaignKey.resize(separator);
                }
                std::string campaignId = spec.campaignIdPrefix + campaignKey;
                std::string sourceSection = campaignKey;
                if (spec.baseGame && campaignKey == "all") {
                    campaignId = "ra2-gdi";
                    sourceSection = "GDI";
                } else if (spec.baseGame && campaignKey == "sov") {
                    campaignId = "ra2-nod";
                    sourceSection = "Nod";
                } else if (spec.baseGame
                    && (campaignKey == "trn" || campaignKey == "tut")) {
                    campaignId = "ra2-tutorial";
                    sourceSection = "GDI";
                }
                CampaignDefinition& campaign = grouped[campaignId];
                campaign.id = campaignId;
                campaign.sourceId = spec.sourceId;
                campaign.edition = spec.edition;
                if (campaign.battleSection.empty()) {
                    campaign.battleSection = battleSection;
                }
                campaign.sourceSection = sourceSection;
                campaign.textCatalogPath =
                    sourceDefinition.textCatalogPath;
                campaign.displayName = sourceSection;
                if (campaign.displayNameKey.empty()) {
                    campaign.displayNameKey = sectionValue(
                        battle.ini,
                        battleSection,
                        "Description"
                    );
                }
                campaign.side = sourceSection;
                campaign.missions.push_back(makeMission(
                    spec,
                    mapResolver,
                    nullptr,
                    missionMetadata.loaded
                        ? &missionMetadata.ini
                        : nullptr,
                    "",
                    missionMetadata.source,
                    campaign.id,
                    campaign.sourceSection,
                    battleSection,
                    entry.number,
                    scenario,
                    debugOnly
                ));
            }
            for (auto& [id, campaign] : grouped) {
                (void)id;
                std::sort(
                    campaign.missions.begin(),
                    campaign.missions.end(),
                    [](const CampaignMissionDefinition& left,
                       const CampaignMissionDefinition& right) {
                        return left.ordinal < right.ordinal;
                    }
                );
                for (std::size_t index = 0;
                    index < campaign.missions.size();
                    ++index) {
                    campaign.missions[index].ordinal =
                        static_cast<int>(index + 1U);
                }
                setSequentialNextMission(campaign);
                if (!campaign.missions.empty()) {
                    database.campaigns.push_back(std::move(campaign));
                }
            }
        }

        const bool sourcePresent = battle.loaded || mapSelection.loaded
            || missionMetadata.loaded || !textPath.empty();
        if (sourcePresent && !mapSelection.loaded && !battle.loaded) {
            database.warnings.push_back(
                spec.sourceId + ": " + spec.mapSelectionFileName
                + " and " + spec.battleFileName
                + " are missing or unreadable"
            );
        }
        if (sourcePresent && !missionMetadata.loaded) {
            database.warnings.push_back(
                spec.sourceId + ": " + spec.missionFileName
                + " is missing or unreadable"
            );
        }
    }

    if (database.campaigns.empty()) {
        database.status = "RA2/YR campaign selectors contain no usable missions";
        return database;
    }

    std::sort(
        database.campaigns.begin(),
        database.campaigns.end(),
        [](const CampaignDefinition& left, const CampaignDefinition& right) {
            return lowerAscii(left.id) < lowerAscii(right.id);
        }
    );
    for (const CampaignDefinition& campaign : database.campaigns) {
        for (const CampaignMissionDefinition& mission : campaign.missions) {
            if (!mission.available) {
                database.warnings.push_back(
                    campaign.id + "/" + mission.id + ": " + mission.status
                );
            }
            if (mission.introVideoId.empty()
                || isPlaceholder(mission.introVideoId)) {
                database.warnings.push_back(
                    campaign.id + "/" + mission.id
                    + ": map has no authored Basic.Intro video"
                );
            } else if (mission.introVideoPath.empty()) {
                database.warnings.push_back(
                    campaign.id + "/" + mission.id
                    + ": authored Intro video is not in mounted resources"
                );
            }
            if (!isPlaceholder(mission.postScoreVideoId)
                && mission.postScoreVideoPath.empty()) {
                database.warnings.push_back(
                    campaign.id + "/" + mission.id
                    + ": authored PostScore video is not in mounted resources"
                );
            }
            if (!mission.unsupportedEventTypes.empty()
                || !mission.unsupportedActionTypes.empty()
                || !mission.unsupportedTeamScriptActionTypes.empty()) {
                std::ostringstream diagnostic;
                diagnostic << campaign.id << "/" << mission.id
                           << ": unsupported map trigger types events=";
                for (std::size_t index = 0;
                    index < mission.unsupportedEventTypes.size();
                    ++index) {
                    if (index != 0) diagnostic << ',';
                    diagnostic << mission.unsupportedEventTypes[index];
                }
                diagnostic << " actions=";
                for (std::size_t index = 0;
                    index < mission.unsupportedActionTypes.size();
                    ++index) {
                    if (index != 0) diagnostic << ',';
                    diagnostic << mission.unsupportedActionTypes[index];
                }
                diagnostic << " scripts=";
                for (std::size_t index = 0;
                    index < mission.unsupportedTeamScriptActionTypes.size();
                    ++index) {
                    if (index != 0) diagnostic << ',';
                    diagnostic
                        << mission.unsupportedTeamScriptActionTypes[index];
                }
                database.warnings.push_back(diagnostic.str());
            }
            for (const std::string& coverageError : mission.coverageErrors) {
                database.warnings.push_back(
                    campaign.id + "/" + mission.id
                    + ": campaign coverage parse error: " + coverageError
                );
            }
        }
    }
    database.loaded = true;
    database.status = "campaign database loaded campaigns="
        + std::to_string(database.campaigns.size())
        + " missions=" + std::to_string(database.missionCount())
        + " available=" + std::to_string(database.availableMissionCount());
    return database;
}

int runCampaignDatabaseSmokeTest(const CampaignDatabaseOptions& options) {
    CampaignMissionDefinition strictPostScoreProbe;
    const std::string strictPostScorePath = resolveMissionVideoCandidates(
        {fs::path("ending_alt.bik")},
        "ending",
        strictPostScoreProbe,
        false
    );
    if (!strictPostScorePath.empty()
        || !strictPostScoreProbe.videoPaths.empty()) {
        std::cerr << "Campaign database: PostScore prefix fallback is unsafe\n";
        return 1;
    }

    const auto joinIntegers = [](const std::vector<int>& values) {
        if (values.empty()) return std::string("-");
        std::ostringstream output;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            if (index != 0U) output << ',';
            output << values[index];
        }
        return output.str();
    };
    const auto sourceLeaf = [](const RuntimeIniSource& source) {
        return source.entryName.empty()
            ? fs::path(source.backingPath).filename().string()
            : fs::path(source.entryName).filename().string();
    };
    const auto movieSourceMatches = [](const std::string& sourceId,
                                       const RuntimeCampaignMovieResolution& movie) {
        const std::string reference = lowerAscii(movie.referencePath);
        if (sourceId == "ra2") {
            return movie.found && movie.sourceId == "ra2"
                && reference.find("movmd03") == std::string::npos
                && (reference.find("movies01.mix!") != std::string::npos
                    || reference.find("movies02.mix!") != std::string::npos);
        }
        return movie.found && movie.sourceId == "yr"
            && reference.find("movmd03") != std::string::npos
            && reference.find("movies01") == std::string::npos
            && reference.find("movies02") == std::string::npos;
    };

    struct EditionAudit {
        bool valid = true;
        std::map<RuntimeCampaignIniKind, RuntimeIniSource> iniSources;
        std::map<int, std::pair<std::string, RuntimeCampaignMovieResolution>>
            movieCatalog;
        AudioIndexedSample audioSample;
        std::vector<std::string> errors;
    };
    const std::array<std::pair<RuntimeCampaignIniKind, const char*>, 5>
        sourceKinds{{
            {RuntimeCampaignIniKind::Rules, "Rules"},
            {RuntimeCampaignIniKind::Art, "Art"},
            {RuntimeCampaignIniKind::Ai, "AI"},
            {RuntimeCampaignIniKind::Sound, "Sound"},
            {RuntimeCampaignIniKind::Eva, "EVA"}
        }};
    std::map<std::string, EditionAudit> editions;
    RuntimeCampaignMovieResolverOptions movieOptions;
    movieOptions.resourceRootPath = options.resourceRootPath;
    movieOptions.localDataRootPath = options.localDataRootPath;
    movieOptions.modRootPath = options.modRootPath;
    const fs::path audioAssetRoot = fs::path(options.localDataRootPath)
        / "resources" / "mix_exports";
    for (const std::string sourceId : {std::string("ra2"), std::string("yr")}) {
        EditionAudit audit;
        RuntimeCampaignDataSourceOptions sourceOptions;
        sourceOptions.resourceRootPath = options.resourceRootPath;
        sourceOptions.localDataRootPath = options.localDataRootPath;
        sourceOptions.modRootPath = options.modRootPath;
        sourceOptions.sourceId = sourceId;
        for (const auto& [kind, label] : sourceKinds) {
            RuntimeIniSource source = resolveRuntimeCampaignIniSource(
                sourceOptions,
                kind
            );
            const std::string expected = runtimeCampaignIniFileName(
                kind,
                sourceId
            );
            if (!source.available()
                || lowerAscii(source.sourceId) != sourceId
                || lowerAscii(sourceLeaf(source)) != lowerAscii(expected)) {
                audit.valid = false;
                audit.errors.push_back(
                    std::string(label) + " source mismatch: "
                    + source.displayPath
                );
            }
            audit.iniSources.emplace(kind, std::move(source));
        }

        IniFile artIni;
        const RuntimeIniSource& artSource = audit.iniSources.at(
            RuntimeCampaignIniKind::Art
        );
        if (!artSource.load(artIni)) {
            audit.valid = false;
            audit.errors.push_back("Art source could not be parsed");
        } else if (const IniFile::Section* movies =
                artIni.getSection("Movies")) {
            for (const auto& [indexText, movieIdText] : *movies) {
                int index = -1;
                const std::string movieId = trimAscii(movieIdText);
                if (!parseInt(indexText, index)
                    || index < 0 || movieId.empty()) {
                    continue;
                }
                audit.movieCatalog[index] = {
                    movieId,
                    resolveRuntimeCampaignMovie(
                        movieOptions,
                        sourceId,
                        movieId
                    )
                };
            }
        } else {
            audit.valid = false;
            audit.errors.push_back("Art source has no [Movies]");
        }

        const std::vector<AudioIndexedSample> samples =
            collectIndexedAudioSamples(audioAssetRoot.string(), sourceId);
        const auto usableSample = std::find_if(
            samples.begin(),
            samples.end(),
            [](const AudioIndexedSample& sample) {
                return !sample.name.empty() && sample.size > 0U;
            }
        );
        const std::string expectedAudio = sourceId == "ra2"
            ? "audio.mix" : "audiomd.mix";
        if (usableSample == samples.end()
            || lowerAscii(fs::path(usableSample->sourceMixPath)
                .filename().string()) != expectedAudio) {
            audit.valid = false;
            audit.errors.push_back(
                "audio sample payload source mismatch"
            );
        } else {
            audit.audioSample = *usableSample;
        }
        editions.emplace(sourceId, std::move(audit));
    }

    const CampaignDatabase database = loadCampaignDatabase(options);
    std::cout << "Campaign database: " << database.status << "\n";
    std::cout << "  battle=" << database.battleIniPath << "\n";
    std::cout << "  mapSelector=" << database.mapSelectionIniPath << "\n";
    std::cout << "  missionMetadata=" << database.missionIniPath << "\n";
    std::cout << "  campaigns=" << database.campaigns.size()
              << " missions=" << database.missionCount()
              << " available=" << database.availableMissionCount() << "\n";

    bool auditPassed = database.loaded && !database.campaigns.empty();
    if (options.modRootPath.empty()) {
        auditPassed = auditPassed
            && database.campaigns.size() == 5U
            && database.missionCount() == 40U
            && database.availableMissionCount() == 40U;
    }
    int auditedMissions = 0;
    int objectiveMissions = 0;
    for (const CampaignDefinition& campaign : database.campaigns) {
        std::cout << "  [" << campaign.id << "] missions="
                  << campaign.missions.size() << "\n";
        if (campaign.missions.empty()) auditPassed = false;
        for (const CampaignMissionDefinition& mission : campaign.missions) {
            ++auditedMissions;
            const auto edition = editions.find(mission.sourceId);
            bool missionPassed = mission.available
                && !mission.mapPath.empty()
                && mission.unsupportedEventTypes.empty()
                && mission.unsupportedActionTypes.empty()
                && mission.unsupportedTeamScriptActionTypes.empty()
                && mission.coverageErrors.empty()
                && mission.victoryActionCount > 0
                && mission.defeatActionCount > 0
                && mission.reachableVictoryActionCount > 0
                && mission.reachableDefeatActionCount > 0
                && edition != editions.end()
                && edition->second.valid;
            if (mission.reachableVictoryActionCount > 0
                && mission.reachableDefeatActionCount > 0) {
                ++objectiveMissions;
            }

            std::ostringstream movies;
            if (mission.triggerMovieIndexes.empty()) {
                movies << '-';
            } else if (edition == editions.end()) {
                movies << "edition-missing";
                missionPassed = false;
            } else {
                for (std::size_t index = 0U;
                    index < mission.triggerMovieIndexes.size(); ++index) {
                    if (index != 0U) movies << ',';
                    const int movieIndex = mission.triggerMovieIndexes[index];
                    const auto movie = edition->second.movieCatalog.find(
                        movieIndex
                    );
                    if (movie == edition->second.movieCatalog.end()
                        || !movieSourceMatches(
                            mission.sourceId,
                            movie->second.second
                        )) {
                        movies << movieIndex << ":UNRESOLVED";
                        missionPassed = false;
                    } else {
                        movies << movieIndex << ':' << movie->second.first
                               << '@' << movie->second.second.referencePath;
                    }
                }
            }

            std::ostringstream sources;
            std::string audioPayload = "missing";
            if (edition != editions.end()) {
                for (std::size_t index = 0U;
                    index < sourceKinds.size(); ++index) {
                    if (index != 0U) sources << ';';
                    const auto& [kind, label] = sourceKinds[index];
                    const auto source = edition->second.iniSources.find(kind);
                    sources << label << ':'
                        << (source == edition->second.iniSources.end()
                            ? std::string("missing")
                            : source->second.displayPath);
                }
                const AudioIndexedSample& sample = edition->second.audioSample;
                if (!sample.name.empty()) {
                    audioPayload = sample.sourceMixPath + "!" + sample.name
                        + "@" + std::to_string(sample.offset)
                        + "+" + std::to_string(sample.size);
                }
            }

            std::cout << "    Coverage " << campaign.id << '/'
                      << mission.id
                      << " Map=" << mission.mapPath
                      << " Events=[" << joinIntegers(mission.eventTypes) << ']'
                      << " Actions=[" << joinIntegers(mission.actionTypes) << ']'
                      << " Scripts=["
                      << joinIntegers(mission.teamScriptActionTypes) << ']'
                      << " Movies=[" << movies.str() << ']'
                      << " Sources={" << sources.str() << '}'
                      << " AudioPayload=" << audioPayload
                      << " ObjectiveChain="
                      << mission.reachableVictoryActionCount << '/'
                      << mission.victoryActionCount << " win,"
                      << mission.reachableDefeatActionCount << '/'
                      << mission.defeatActionCount << " lose"
                      << " Status=" << (missionPassed ? "PASS" : "FAIL")
                      << "\n";
            if (!missionPassed) {
                auditPassed = false;
                std::cerr << "Campaign coverage failed: "
                          << campaign.id << '/' << mission.id
                          << " unsupportedEvents=["
                          << joinIntegers(mission.unsupportedEventTypes) << ']'
                          << " unsupportedActions=["
                          << joinIntegers(mission.unsupportedActionTypes) << ']'
                          << " unsupportedScripts=["
                          << joinIntegers(
                                mission.unsupportedTeamScriptActionTypes
                             ) << ']';
                for (const std::string& error : mission.coverageErrors) {
                    std::cerr << " error={" << error << '}';
                }
                if (edition != editions.end()) {
                    for (const std::string& error : edition->second.errors) {
                        std::cerr << " sourceError={" << error << '}';
                    }
                }
                std::cerr << "\n";
            }
        }
    }
    for (const std::string& warning : database.warnings) {
        std::cout << "  warning: " << warning << "\n";
    }
    std::cout << "Campaign retail coverage gate: maps=" << auditedMissions
              << " objectives=" << objectiveMissions
              << " events/actions/scripts/movies/sources/audio="
              << (auditPassed ? "PASS" : "FAIL") << "\n";
    return auditPassed ? 0 : 1;
}

int runCampaignCoverageMapGateTest(const std::string& mapPath) {
    IniFile ini;
    if (!ini.loadFromFile(mapPath)) {
        std::cerr << "Campaign coverage map gate failed: could not load "
                  << mapPath << "\n";
        return 1;
    }
    const RuntimeCampaignCoverageInventory coverage =
        inspectRuntimeCampaignCoverage(ini);
    const std::vector<int> unsupportedEvents =
        unsupportedRuntimeCampaignCoverageEventTypes(coverage);
    const std::vector<int> unsupportedActions =
        unsupportedRuntimeCampaignCoverageActionTypes(coverage);
    const std::vector<int> unsupportedScripts =
        unsupportedRuntimeCampaignCoverageTeamScriptActions(coverage);
    const auto print = [](const std::vector<int>& values) {
        std::ostringstream output;
        for (std::size_t index = 0U; index < values.size(); ++index) {
            if (index != 0U) output << ',';
            output << values[index];
        }
        return output.str();
    };
    const bool accepted = coverage.parseErrors.empty()
        && unsupportedEvents.empty()
        && unsupportedActions.empty()
        && unsupportedScripts.empty()
        && coverage.reachableVictoryActionCount > 0
        && coverage.reachableDefeatActionCount > 0;
    std::cout << "Campaign coverage map gate: map=" << mapPath
              << " unsupportedEvents=[" << print(unsupportedEvents) << ']'
              << " unsupportedActions=[" << print(unsupportedActions) << ']'
              << " unsupportedScripts=[" << print(unsupportedScripts) << ']'
              << " parseErrors=" << coverage.parseErrors.size()
              << " objectives=" << coverage.reachableVictoryActionCount
              << '/' << coverage.reachableDefeatActionCount
              << " result=" << (accepted ? "PASS" : "REJECT") << "\n";
    return accepted ? 0 : 1;
}
