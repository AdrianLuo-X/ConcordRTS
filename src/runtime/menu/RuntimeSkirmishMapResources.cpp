#include "runtime/menu/detail/RuntimeSkirmishMapResources.h"

#include "runtime/menu/RuntimeSkirmishController.h"
#include "data/maps/RuntimeMapPackage.h"
#include "assets/text/CsfCodec.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace concord::skirmish::map_detail {
namespace fs=std::filesystem;
using namespace concord::skirmish;



void assignProjectedMapDisplayName(MapInfo& map,const std::string& text,MapDisplayProjection projection) {
    if (projection) map.displayName = projection(text);
}
std::vector<std::string> splitCsv(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        token = trimAscii(token);
        if (!token.empty()) {
            result.push_back(token);
        }
    }
    return result;
}

int parseIntOr(const std::string& value, int fallback) {
    const std::string trimmed = trimAscii(value);
    if (trimmed.empty()) {
        return fallback;
    }
    try {
        std::size_t used = 0;
        const int result = std::stoi(trimmed, &used);
        return used == trimmed.size() ? result : fallback;
    } catch (...) {
        return fallback;
    }
}

bool regularFile(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

fs::path firstExisting(std::initializer_list<fs::path> paths) {
    for (const fs::path& path : paths) {
        if (!path.empty() && regularFile(path)) {
            return path;
        }
    }
    return {};
}

std::string iniValueNoCase(
    const IniFile& ini,
    const std::string& wantedSection,
    const std::string& wantedKey,
    const std::string& fallback
) {
    for (const std::string& sectionName : ini.getSectionNames()) {
        if (!equalsAsciiNoCase(sectionName, wantedSection)) {
            continue;
        }
        const IniFile::Section* section = ini.getSection(sectionName);
        if (section == nullptr) {
            return fallback;
        }
        for (auto it = section->rbegin(); it != section->rend(); ++it) {
            if (equalsAsciiNoCase(it->first, wantedKey)) {
                return it->second;
            }
        }
        return fallback;
    }
    return fallback;
}

bool parsePair(const std::string& value, int& left, int& right) {
    const std::vector<std::string> parts = splitCsv(value);
    if (parts.size() < 2) {
        return false;
    }
    left = parseIntOr(parts[0], std::numeric_limits<int>::min());
    right = parseIntOr(parts[1], std::numeric_limits<int>::min());
    return left != std::numeric_limits<int>::min()
        && right != std::numeric_limits<int>::min();
}

bool readMapMetadata(const fs::path& path, MapInfo& map, MapDisplayProjection textProjection) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    bool hasMap = false;
    bool hasPreview = false;
    std::string section;
    std::string basicName;
    std::vector<std::optional<RuntimeInputPixelPoint>> headerPoints(kPlayerSlots);
    std::vector<std::optional<RuntimeInputPixelPoint>> waypointPoints(kPlayerSlots);
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t semicolon = line.find(';');
        if (semicolon != std::string::npos) {
            line.erase(semicolon);
        }
        line = trimAscii(line);
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = lowerAscii(trimAscii(line.substr(1, line.size() - 2)));
            hasMap = hasMap || section == "map";
            hasPreview = hasPreview || section == "preview";
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string key = lowerAscii(trimAscii(line.substr(0, equals)));
        const std::string value = trimAscii(line.substr(equals + 1));
        if (section == "header") {
            if (key == "startx") map.headerX = parseIntOr(value, map.headerX);
            else if (key == "starty") map.headerY = parseIntOr(value, map.headerY);
            else if (key == "width") map.headerWidth = parseIntOr(value, map.headerWidth);
            else if (key == "height") map.headerHeight = parseIntOr(value, map.headerHeight);
            else if (key == "numberstartingpoints") {
                map.maxPlayers = std::clamp(parseIntOr(value, map.maxPlayers), 1, 8);
            } else if (key.rfind("waypoint", 0) == 0) {
                const int number = parseIntOr(key.substr(8), 0);
                RuntimeInputPixelPoint point{};
                int pointX = 0;
                int pointY = 0;
                if (number >= 1 && number <= kPlayerSlots
                    && parsePair(value, pointX, pointY)
                    && (pointX != 0 || pointY != 0)) {
                    point.x = pointX;
                    point.y = pointY;
                    headerPoints[static_cast<std::size_t>(number - 1)] = point;
                }
            }
        } else if (section == "map") {
            if (key == "theater") {
                map.theater = value;
            } else if (key == "size") {
                map.size = value;
            }
        } else if (section == "preview" && key == "size") {
            const std::vector<std::string> values = splitCsv(value);
            if (values.size() >= 4) {
                map.previewWidth = parseIntOr(values[2], 0);
                map.previewHeight = parseIntOr(values[3], 0);
            }
        } else if (section == "basic") {
            if (key == "name") {
                basicName = value;
            } else if (key == "gamemode") {
                map.filters = splitCsv(value);
            }
        } else if (section == "waypoints") {
            const int index = parseIntOr(key, -1);
            const int packed = parseIntOr(value, -1);
            if (index >= 0 && index < kPlayerSlots && packed > 0) {
                RuntimeInputPixelPoint point{packed % 1000, packed / 1000};
                waypointPoints[static_cast<std::size_t>(index)] = point;
            }
        }
    }
    // RA2 MMX maps predate the optional [Header] lobby metadata. Their
    // starting locations are still available through [Waypoints], and map
    // bounds/preview data remain sufficient for selection and rendering.
    if (!hasMap || !hasPreview
        || map.previewWidth <= 0 || map.previewHeight <= 0) {
        return false;
    }
    map.id = path.stem().string();
    if (map.id.empty()) {
        map.id = path.filename().string();
    }
    assignProjectedMapDisplayName(map,basicName.empty() ? map.id : basicName,textProjection);
    for (int i = 0; i < map.maxPlayers && i < kPlayerSlots; ++i) {
        if (headerPoints[static_cast<std::size_t>(i)]) {
            const auto point = *headerPoints[static_cast<std::size_t>(i)];
            map.startPoints.push_back({point.x, point.y});
        } else if (waypointPoints[static_cast<std::size_t>(i)]) {
            const auto point = *waypointPoints[static_cast<std::size_t>(i)];
            map.startPoints.push_back({point.x, point.y});
        }
    }
    if (map.filters.empty()) {
        map.filters.push_back("standard");
    }
    map.metadataLoaded = true;
    return true;
}

std::unordered_map<std::string, MapPacketEntry> loadMapPacket(
    const fs::path& path
) {
    std::unordered_map<std::string, MapPacketEntry> result;
    IniFile packet;
    if (path.empty() || !packet.loadFromPath(path)) {
        return result;
    }
    for (const std::string& sectionName : packet.getSectionNames()) {
        if (equalsAsciiNoCase(sectionName, "MultiMaps")) {
            continue;
        }
        const std::string description =
            iniValueNoCase(packet, sectionName, "Description");
        const std::string modes = iniValueNoCase(packet, sectionName, "GameMode");
        if (description.empty() && modes.empty()) {
            continue;
        }
        MapPacketEntry entry;
        entry.id = sectionName;
        entry.descriptionKey = description;
        entry.minPlayers = std::clamp(
            parseIntOr(iniValueNoCase(packet, sectionName, "MinPlayers"), 2),
            1,
            8
        );
        entry.maxPlayers = std::clamp(
            parseIntOr(iniValueNoCase(packet, sectionName, "MaxPlayers"), 8),
            entry.minPlayers,
            8
        );
        entry.filters = splitCsv(modes);
        result[lowerAscii(sectionName)] = std::move(entry);
    }
    return result;
}

std::vector<MapInfo> scanMaps(
    const fs::path& directory,
    const std::unordered_map<std::string, MapPacketEntry>& packet,
    const CsfCatalog& csf
, MapDisplayProjection textProjection) {
    std::map<std::string, MapInfo> unique;
    std::error_code error;
    fs::directory_iterator iterator(directory, error);
    fs::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
        const fs::directory_entry& entry = *iterator;
        if (!entry.is_regular_file(error)) {
            continue;
        }
        const fs::path path = entry.path();
        const std::string extension = lowerAscii(path.extension().string());
        if (!extension.empty()
            && extension != ".map"
            && extension != ".mpr"
            && extension != ".yrm") {
            continue;
        }
        MapInfo map;
        map.path = path;
        map.id = path.stem().string();
        if (map.id.empty()) map.id = path.filename().string();
        assignProjectedMapDisplayName(map,map.id,textProjection);
        const auto packetEntry = packet.find(lowerAscii(map.id));
        // This directory is an extraction of MULTI*.MIX, which also contains
        // maps Westwood deliberately commented out of missionsmd.pkt.  The
        // original skirmish browser only exposes active packet entries.
        if (!packet.empty() && packetEntry == packet.end()) {
            continue;
        }
        if (packet.empty() && !readMapMetadata(path, map, textProjection)) {
            continue;
        }
        if (packetEntry != packet.end()) {
            map.minPlayers = packetEntry->second.minPlayers;
            map.maxPlayers = packetEntry->second.maxPlayers;
            if (!packetEntry->second.filters.empty()) {
                map.filters = packetEntry->second.filters;
            }
            if (!packetEntry->second.descriptionKey.empty()) {
                map.displayName = csf.get(
                    packetEntry->second.descriptionKey,
                    map.displayName
                );
            }
        }
        if (map.filters.empty()) map.filters.push_back("standard");
        const std::string key = lowerAscii(map.id);
        auto found = unique.find(key);
        if (found == unique.end()
            || (found->second.path.extension().empty() && !path.extension().empty())) {
            unique[key] = std::move(map);
        }
    }
    std::vector<MapInfo> maps;
    maps.reserve(unique.size());
    for (auto& [key, map] : unique) {
        (void)key;
        maps.push_back(std::move(map));
    }
    std::sort(
        maps.begin(),
        maps.end(),
        [](const MapInfo& left, const MapInfo& right) {
            if (left.maxPlayers != right.maxPlayers) {
                return left.maxPlayers < right.maxPlayers;
            }
            return lowerAscii(left.id) < lowerAscii(right.id);
        }
    );
    return maps;
}

int resolveMapStartingPointLimit(const RuntimeConfig& config,
    MapDisplayProjection textProjection) {
    if (config.mapStartingPointLimit > 0) {
        return std::min(config.mapStartingPointLimit, kPlayerSlots);
    }
    const fs::path actualPath(config.defaultMapPath);
    IniFile actualMap;
    if (!actualMap.loadFromPath(actualPath)) return 0;
    const int headerCount = parseIntOr(
        iniValueNoCase(actualMap, "Header", "NumberStartingPoints"), 0
    );
    if (headerCount > 0) return std::min(headerCount, kPlayerSlots);

    const fs::path exportRoot = fs::path(config.localDataRootPath)
        / "resources" / "mix_exports";
    const auto packet = loadMapPacket(firstExisting({
        exportRoot / "langmd" / "missionsmd.pkt",
        exportRoot / "lang" / "missions.pkt",
        exportRoot / "ra2md" / "localmd" / "missionsmd.pkt"
    }));
    const std::string actualStem = lowerAscii(actualPath.stem().string());
    const std::string canonicalStem = lowerAscii(
        fs::path(config.multiplayerCanonicalMapId).filename().stem().string()
    );
    // A filename or canonical id only locates a candidate. Its original bytes
    // must also match; a custom map with the same name inherits no packet limit.
    const auto sameFileBytes = [](const fs::path& a, const fs::path& b) {
        std::error_code error;
        const auto size = fs::file_size(a, error);
        if (error) return false;
        const auto otherSize = fs::file_size(b, error);
        if (error || size != otherSize) return false;
        std::ifstream left(a, std::ios::binary), right(b, std::ios::binary);
        if (!left || !right) return false;
        std::array<char, 64U * 1024U> x{}, y{};
        std::uintmax_t remaining = size;
        while (remaining > 0) {
            const auto count = static_cast<std::streamsize>(
                std::min<std::uintmax_t>(remaining, x.size())
            );
            left.read(x.data(), count);
            right.read(y.data(), count);
            if (left.gcount() != count || right.gcount() != count
                || !std::equal(x.begin(), x.begin() + count, y.begin())) return false;
            remaining -= static_cast<std::uintmax_t>(count);
        }
        return true;
    };
    if (!packet.empty()) {
        const CsfCatalog unusedLabels;
        for (const MapInfo& map : scanMaps(exportRoot / "multimd", packet, unusedLabels, textProjection)) {
            const std::string id = lowerAscii(map.id);
            if ((id == actualStem || id == canonicalStem)
                && sameFileBytes(actualPath, map.path)) return map.maxPlayers;
        }
    }
    const fs::path resources(config.resourceRootPath);
    return resolveRuntimeMapPackageStartingPointLimit(
        {resources / "GameMD", resources / "Game", resources},
        actualPath, config.multiplayerCanonicalMapId
    );
}

}
