#pragma once

#include "runtime/menu/RuntimeSkirmishController.h"
#include "assets/text/CsfCodec.h"
#include <fstream>
#include <iterator>

namespace concord::skirmish::map_detail {

using MapDisplayProjection = std::wstring(*)(const std::string&);

struct CsfCatalog {
    std::unordered_map<std::string, std::wstring> values;

    bool load(const fs::path& path) {
        values.clear();
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        const std::vector<unsigned char> bytes(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        for (auto& entry : concord::csf::decodeEntries(bytes)) {
            const auto value = concord::csf::projectCarriageReturns(
                std::move(entry.firstValue), concord::csf::CarriageReturnPolicy::Space);
            values[lowerAscii(entry.label)] = std::wstring(value.begin(), value.end());
        }
        return !values.empty();
    }

    std::wstring get(const std::string& key, const std::wstring& fallback) const {
        const auto found = values.find(lowerAscii(trimAscii(key)));
        if (found == values.end() || found->second.empty()) {
            return fallback;
        }
        return found->second;
    }

    std::wstring getAny(
        std::initializer_list<std::string> keys,
        const std::wstring& fallback
    ) const {
        for (const std::string& key : keys) {
            const auto found = values.find(lowerAscii(key));
            if (found != values.end() && !found->second.empty()) {
                return found->second;
            }
        }
        return fallback;
    }
};

struct MapPacketEntry {
    std::string id;
    std::string descriptionKey;
    int minPlayers = 2;
    int maxPlayers = 8;
    std::vector<std::string> filters;
};

std::vector<std::string> splitCsv(const std::string& value) ;

int parseIntOr(const std::string& value, int fallback) ;

bool regularFile(const fs::path& path) ;

fs::path firstExisting(std::initializer_list<fs::path> paths) ;

std::string iniValueNoCase(
    const IniFile& ini,
    const std::string& wantedSection,
    const std::string& wantedKey,
    const std::string& fallback = ""
) ;

bool parsePair(const std::string& value, int& left, int& right) ;

bool readMapMetadata(const fs::path& path, MapInfo& map, MapDisplayProjection textProjection) ;

std::unordered_map<std::string, MapPacketEntry> loadMapPacket(
    const fs::path& path
) ;

std::vector<MapInfo> scanMaps(
    const fs::path& directory,
    const std::unordered_map<std::string, MapPacketEntry>& packet,
    const CsfCatalog& csf
, MapDisplayProjection textProjection) ;

int resolveMapStartingPointLimit(const RuntimeConfig&, MapDisplayProjection);

}
