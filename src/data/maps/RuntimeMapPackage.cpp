#include "data/maps/RuntimeMapPackage.h"

#include "core/io/IniFile.h"
#include "assets/archive/MixArchive.h"
#include "core/io/RuntimeAtomicFile.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <ostream>
#include <set>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

namespace fs = std::filesystem;

constexpr std::uint64_t maximumPacketBytes = 8U * 1024U * 1024U;
constexpr std::uint64_t maximumMapBytes = 64U * 1024U * 1024U;
constexpr std::uint64_t maximumPackageMapBytes = 256U * 1024U * 1024U;
constexpr std::size_t maximumMapsPerPackage = 256U;
constexpr std::size_t maximumWarnings = 256U;

struct PendingPackageMap {
    RuntimeMapPackageMap metadata;
    std::uint32_t entryId = 0;
    std::vector<std::uint8_t> bytes;
};

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

std::string pathToUtf8(const fs::path& path) {
    const std::u8string encoded = path.generic_u8string();
    return std::string(
        reinterpret_cast<const char*>(encoded.data()),
        encoded.size()
    );
}

std::string trimAscii(std::string value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

void addWarning(
    RuntimeMapPackageScanResult& result,
    const fs::path& packagePath,
    const std::string& message
) {
    if (result.warnings.size() >= maximumWarnings) {
        return;
    }
    result.warnings.push_back(
        packagePath.empty()
            ? message
            : pathToUtf8(packagePath) + ": " + message
    );
}

int parseIntInRange(
    const std::string& text,
    int fallback,
    int minimum,
    int maximum
) {
    const std::string trimmed = trimAscii(text);
    int parsed = 0;
    const auto conversion = std::from_chars(
        trimmed.data(), trimmed.data() + trimmed.size(), parsed
    );
    if (trimmed.empty()
        || conversion.ec != std::errc{}
        || conversion.ptr != trimmed.data() + trimmed.size()) {
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

std::vector<std::string> splitCsv(const std::string& text) {
    std::vector<std::string> values;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string::npos
            ? text.size()
            : comma;
        std::string value = trimAscii(text.substr(start, end - start));
        if (!value.empty()) {
            values.push_back(std::move(value));
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1U;
    }
    return values;
}

std::string bytesAsString(const std::vector<std::uint8_t>& bytes) {
    return std::string(
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size()
    );
}

bool readBoundedEntry(
    const MixArchive& archive,
    const MixEntry& entry,
    std::uint64_t maximumBytes,
    std::vector<std::uint8_t>& bytes,
    std::string& error
) {
    if (entry.size == 0U || entry.size > maximumBytes) {
        error = entry.size == 0U
            ? "MIX entry is empty"
            : "MIX entry exceeds the supported size limit";
        bytes.clear();
        return false;
    }
    return archive.readEntryData(entry, bytes, error);
}

bool parsePacketBytes(
    const std::vector<std::uint8_t>& bytes,
    IniFile& packet
) {
    return packet.loadFromString(bytesAsString(bytes))
        && packet.getSection("MultiMaps") != nullptr;
}

bool loadPackagePacket(
    const MixArchive& archive,
    const fs::path& packagePath,
    IniFile& packet,
    std::string& error
) {
    std::vector<std::uint8_t> bytes;
    const MixEntry* namedEntry = archive.findEntryByName(
        pathToUtf8(packagePath.stem()) + ".pkt"
    );
    if (namedEntry != nullptr
        && readBoundedEntry(
            archive, *namedEntry, maximumPacketBytes, bytes, error
        )
        && parsePacketBytes(bytes, packet)) {
        return true;
    }

    for (const MixEntry& entry : archive.getEntries()) {
        if (&entry == namedEntry || entry.size > maximumPacketBytes) {
            continue;
        }
        if (!readBoundedEntry(
                archive, entry, maximumPacketBytes, bytes, error
            )) {
            continue;
        }
        if (parsePacketBytes(bytes, packet)) {
            return true;
        }
    }

    error = "package does not contain a readable MultiMaps packet";
    return false;
}

const MixEntry* findMapEntry(
    const MixArchive& archive,
    const std::string& mapId
) {
    for (const char* extension : {".map", ".mpr", ".yrm"}) {
        const MixEntry* entry = archive.findEntryByName(mapId + extension);
        if (entry != nullptr) {
            return entry;
        }
    }
    return nullptr;
}

bool mapBytesHaveRequiredSections(
    const std::vector<std::uint8_t>& bytes
) {
    IniFile map;
    if (!map.loadFromString(bytesAsString(bytes))
        || map.getSection("Map") == nullptr
        || map.getSection("Preview") == nullptr) {
        return false;
    }
    const std::vector<std::string> previewSize = splitCsv(
        map.getValue("Preview", "Size")
    );
    return previewSize.size() >= 4U
        && parseIntInRange(
            previewSize[2],
            0,
            0,
            std::numeric_limits<int>::max()
        ) > 0
        && parseIntInRange(
            previewSize[3],
            0,
            0,
            std::numeric_limits<int>::max()
        ) > 0;
}

std::vector<PendingPackageMap> loadPackageMaps(
    const fs::path& packagePath,
    RuntimeMapPackageScanResult& result
) {
    std::vector<PendingPackageMap> maps;
    MixArchive archive;
    if (!archive.loadFromPath(packagePath)
        || !archive.isIndexReadable()) {
        addWarning(
            result,
            packagePath,
            archive.getErrorMessage().empty()
                ? "MIX index is not readable"
                : archive.getErrorMessage()
        );
        return maps;
    }
    ++result.readablePackageCount;

    IniFile packet;
    std::string error;
    if (!loadPackagePacket(archive, packagePath, packet, error)) {
        addWarning(result, packagePath, error);
        return maps;
    }

    const IniFile::Section* advertisedMaps = packet.getSection("MultiMaps");
    if (advertisedMaps == nullptr || advertisedMaps->empty()) {
        addWarning(result, packagePath, "MultiMaps packet is empty");
        return maps;
    }

    std::set<std::string> packageIds;
    std::uint64_t totalMapBytes = 0;
    for (const auto& [ordinal, advertisedId] : *advertisedMaps) {
        (void)ordinal;
        if (packageIds.size() >= maximumMapsPerPackage) {
            addWarning(
                result,
                packagePath,
                "package advertises more than the supported map limit"
            );
            break;
        }
        const std::string mapId = trimAscii(advertisedId);
        const std::string normalizedId = lowerAscii(mapId);
        if (mapId.empty() || !packageIds.insert(normalizedId).second) {
            continue;
        }

        const MixEntry* mapEntry = findMapEntry(archive, mapId);
        if (mapEntry == nullptr) {
            addWarning(
                result,
                packagePath,
                "advertised map entry was not found: " + mapId
            );
            continue;
        }
        if (mapEntry->size > maximumPackageMapBytes - totalMapBytes) {
            addWarning(
                result,
                packagePath,
                "package map data exceeds the supported aggregate size limit"
            );
            break;
        }

        std::vector<std::uint8_t> mapBytes;
        if (!readBoundedEntry(
                archive,
                *mapEntry,
                maximumMapBytes,
                mapBytes,
                error
            )) {
            addWarning(
                result,
                packagePath,
                "failed to read map " + mapId + ": " + error
            );
            continue;
        }
        totalMapBytes += mapBytes.size();
        if (!mapBytesHaveRequiredSections(mapBytes)) {
            addWarning(
                result,
                packagePath,
                "map entry lacks Map or Preview data: " + mapId
            );
            continue;
        }

        PendingPackageMap pending;
        pending.metadata.sourcePackagePath = packagePath;
        pending.metadata.id = mapId;
        pending.metadata.descriptionKey = packet.getValue(
            mapId, "Description"
        );
        pending.metadata.minPlayers = parseIntInRange(
            packet.getValue(mapId, "MinPlayers"), 2, 1, 8
        );
        pending.metadata.maxPlayers = parseIntInRange(
            packet.getValue(mapId, "MaxPlayers"),
            8,
            pending.metadata.minPlayers,
            8
        );
        pending.metadata.filters = splitCsv(
            packet.getValue(mapId, "GameMode")
        );
        if (pending.metadata.filters.empty()) {
            pending.metadata.filters.push_back("standard");
        }
        pending.entryId = mapEntry->id;
        pending.bytes = std::move(mapBytes);
        maps.push_back(std::move(pending));
    }
    return maps;
}

std::string safeLeafComponent(std::string value, std::size_t maximumLength) {
    for (char& character : value) {
        const bool valid = (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '-'
            || character == '_';
        if (!valid) {
            character = '_';
        }
    }
    if (value.size() > maximumLength) {
        value.resize(maximumLength);
    }
    return value.empty() ? std::string("map") : value;
}

std::string entryIdSuffix(std::uint32_t entryId) {
    std::string suffix = formatMixId(entryId);
    if (suffix.rfind("0x", 0U) == 0U) {
        suffix.erase(0U, 2U);
    }
    return lowerAscii(suffix);
}

fs::path cachePathFor(
    const fs::path& cacheDirectory,
    const PendingPackageMap& map
) {
    return cacheDirectory / (
        safeLeafComponent(map.metadata.id, 64U)
        + "_"
        + entryIdSuffix(map.entryId)
        + ".map"
    );
}

bool fileMatchesBytes(
    const fs::path& path,
    const std::vector<std::uint8_t>& bytes
) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)
        || error
        || fs::file_size(path, error) != bytes.size()
        || error) {
        return false;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::array<char, 64U * 1024U> buffer{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::size_t count = std::min(
            buffer.size(), bytes.size() - offset
        );
        input.read(buffer.data(), static_cast<std::streamsize>(count));
        if (input.gcount() != static_cast<std::streamsize>(count)
            || !std::equal(
                buffer.data(),
                buffer.data() + count,
                reinterpret_cast<const char*>(bytes.data() + offset)
            )) {
            return false;
        }
        offset += count;
    }
    return true;
}

bool materializeMap(
    const fs::path& path,
    const std::vector<std::uint8_t>& bytes,
    std::string& error
) {
    if (fileMatchesBytes(path, bytes)) {
        error.clear();
        return true;
    }
    return runtimeWriteFileAtomically(
        path,
        [&bytes](std::ostream& output, std::string& writerError) {
            output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size())
            );
            if (!output) {
                writerError = "failed to write complete map entry";
                return false;
            }
            return true;
        },
        error
    );
}

std::string normalizedPathKey(const fs::path& path) {
    return lowerAscii(pathToUtf8(path.lexically_normal()));
}

} // namespace


int resolveRuntimeMapPackageStartingPointLimit(
    const std::vector<std::filesystem::path>& directories,
    const std::filesystem::path& actualMapPath,
    const std::string& canonicalMapId
) {
    const std::string actualStem = lowerAscii(pathToUtf8(actualMapPath.stem()));
    const std::string canonicalStem = lowerAscii(pathToUtf8(
        fs::path(canonicalMapId).filename().stem()
    ));
    std::set<std::string> visited;
    for (const fs::path& directory : directories) {
        std::error_code error;
        fs::directory_iterator iterator(
            directory, fs::directory_options::skip_permission_denied, error
        );
        const fs::directory_iterator end;
        std::vector<fs::path> packages;
        for (; !error && iterator != end; iterator.increment(error)) {
            std::error_code entryError;
            if (iterator->is_regular_file(entryError) && !entryError
                && isRuntimeMapPackageExtension(iterator->path())) {
                packages.push_back(iterator->path());
            }
        }
        std::sort(packages.begin(), packages.end(), [](const fs::path& a, const fs::path& b) {
            return lowerAscii(pathToUtf8(a.filename())) < lowerAscii(pathToUtf8(b.filename()));
        });
        for (const fs::path& package : packages) {
            if (!visited.insert(normalizedPathKey(package)).second) continue;
            MixArchive archive;
            if (!archive.loadFromPath(package) || !archive.isIndexReadable()) continue;
            IniFile packet;
            std::string errorMessage;
            if (!loadPackagePacket(archive, package, packet, errorMessage)) continue;
            const IniFile::Section* advertised = packet.getSection("MultiMaps");
            if (advertised == nullptr) continue;
            std::set<std::string> seenIds;
            for (const auto& [ordinal, value] : *advertised) {
                (void)ordinal;
                if (seenIds.size() >= maximumMapsPerPackage) break;
                const std::string mapId = trimAscii(value);
                const std::string id = lowerAscii(mapId);
                if (id.empty() || !seenIds.insert(id).second) continue;
                const MixEntry* entry = findMapEntry(archive, mapId);
                if (entry == nullptr) continue;
                const std::string cacheStem = lowerAscii(
                    safeLeafComponent(mapId, 64U) + "_" + entryIdSuffix(entry->id)
                );
                if (actualStem != id && actualStem != cacheStem
                    && canonicalStem != id && canonicalStem != cacheStem) continue;
                std::vector<std::uint8_t> bytes;
                if (!readBoundedEntry(archive, *entry, maximumMapBytes, bytes, errorMessage)
                    || !fileMatchesBytes(actualMapPath, bytes)) continue;
                const int minimum = parseIntInRange(
                    packet.getValue(mapId, "MinPlayers"), 2, 1,
                    8
                );
                return parseIntInRange(
                    packet.getValue(mapId, "MaxPlayers"), 8, minimum,
                    8
                );
            }
        }
    }
    return 0;
}

bool isRuntimeMapPackageExtension(const std::filesystem::path& path) {
    const std::string extension = lowerAscii(pathToUtf8(path.extension()));
    return extension == ".mmx" || extension == ".yro";
}

RuntimeMapPackageScanResult scanRuntimeMapPackages(
    const std::vector<std::filesystem::path>& directories,
    const std::filesystem::path& cacheDirectory
) {
    RuntimeMapPackageScanResult result;
    std::error_code error;
    fs::create_directories(cacheDirectory, error);
    if (error || !fs::is_directory(cacheDirectory, error)) {
        addWarning(
            result,
            {},
            "failed to create map package cache: "
                + pathToUtf8(cacheDirectory)
                + (error ? " (" + error.message() + ")" : "")
        );
        return result;
    }

    std::set<std::string> seenPackagePaths;
    std::set<std::string> seenMapIds;
    std::map<std::string, std::string> cachePathOwners;
    for (const fs::path& directory : directories) {
        error.clear();
        if (!fs::is_directory(directory, error)) {
            continue;
        }

        std::vector<fs::path> packagePaths;
        fs::directory_iterator iterator(
            directory,
            fs::directory_options::skip_permission_denied,
            error
        );
        const fs::directory_iterator end;
        for (; !error && iterator != end; iterator.increment(error)) {
            const fs::directory_entry& entry = *iterator;
            std::error_code entryError;
            if (entry.is_regular_file(entryError)
                && !entryError
                && isRuntimeMapPackageExtension(entry.path())) {
                packagePaths.push_back(entry.path());
            }
        }
        if (error) {
            addWarning(
                result,
                directory,
                "stopped package scan early: " + error.message()
            );
        }
        std::sort(
            packagePaths.begin(),
            packagePaths.end(),
            [](const fs::path& left, const fs::path& right) {
                return lowerAscii(pathToUtf8(left.filename()))
                    < lowerAscii(pathToUtf8(right.filename()));
            }
        );

        for (const fs::path& packagePath : packagePaths) {
            if (!seenPackagePaths.insert(
                    normalizedPathKey(packagePath)
                ).second) {
                continue;
            }
            ++result.packageFileCount;
            std::vector<PendingPackageMap> packageMaps = loadPackageMaps(
                packagePath, result
            );
            for (PendingPackageMap& packageMap : packageMaps) {
                const std::string mapKey = lowerAscii(packageMap.metadata.id);
                if (seenMapIds.find(mapKey) != seenMapIds.end()) {
                    ++result.duplicateMapCount;
                    continue;
                }

                const fs::path outputPath = cachePathFor(
                    cacheDirectory, packageMap
                );
                const std::string outputKey = normalizedPathKey(outputPath);
                const auto cacheOwner = cachePathOwners.find(outputKey);
                if (cacheOwner != cachePathOwners.end()
                    && cacheOwner->second != mapKey) {
                    addWarning(
                        result,
                        packagePath,
                        "map cache name collides with another map: "
                            + packageMap.metadata.id
                    );
                    continue;
                }
                std::string materializeError;
                if (!materializeMap(
                        outputPath,
                        packageMap.bytes,
                        materializeError
                    )) {
                    addWarning(
                        result,
                        packagePath,
                        "failed to cache map "
                            + packageMap.metadata.id
                            + ": "
                            + materializeError
                    );
                    continue;
                }
                seenMapIds.insert(mapKey);
                cachePathOwners.emplace(outputKey, mapKey);
                packageMap.metadata.mapPath = outputPath;
                result.maps.push_back(std::move(packageMap.metadata));
            }
        }
    }

    std::sort(
        result.maps.begin(),
        result.maps.end(),
        [](const RuntimeMapPackageMap& left,
           const RuntimeMapPackageMap& right) {
            return lowerAscii(left.id) < lowerAscii(right.id);
        }
    );
    return result;
}
