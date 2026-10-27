#include "runtime/window/win32/RuntimeWindowParentWin32.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ui/skirmish/RuntimeSkirmishMenu.h"
#include "runtime/menu/RuntimeSkirmishController.h"
#include "runtime/window/RuntimeSoftwareWindow.h"
#include <cstdlib>
#include "runtime/host/RuntimeUiClockWin32.h"
#include "ui/audio/RuntimeSkirmishMusicPolicy.h"
#include "audio/menu/win32/RuntimeSkirmishMusicWin32.h"
#include "assets/text/CsfCodec.h"
#include "assets/text/WestwoodFontCodec.h"
#include "ui/values/RuntimeColorValues.h"
#include "ui/text/RuntimeSkirmishBitmapText.h"
#include "ui/surface/RuntimeMenuPixelSurface.h"
#include "ui/skirmish/win32/RuntimeSkirmishBackBufferWin32.h"
#include "ui/input/RuntimeObserver.h"
#include "data/maps/RuntimeRandomMap.h"

#include "assets/audio/AudioSampleResolver.h"
#include "data/campaign/CooperativeScenarioDatabase.h"
#include "core/io/IniFile.h"
#include "data/maps/MapDatabase.h"
#include "data/maps/RuntimeMapPackage.h"
#include "ui/main_menu/RuntimeMainMenu.h"
#include "assets/menu/MenuImageResources.h"
#include "assets/archive/MixArchive.h"
#include "ui/resources/RuntimeMenuResourceWatch.h"
#include "audio/events/RuntimeMenuSoundPool.h"
#include "runtime/performance/RuntimePerfTrace.h"

#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace {

namespace fs = std::filesystem;
using namespace concord::skirmish;



constexpr int kShellLeftWidth = 632;
constexpr int kShellStatusTop = 568;
constexpr int kShellSideLeft = 632;
constexpr int kShellTopHeight = 199;
constexpr int kShellSlotHeight = 42;



constexpr int kShellBottomTop = 535;
constexpr int kShellButtonLeft = 653;
constexpr int kShellButtonTop = 207;
constexpr int kShellBottomButtonTop = 543;


constexpr COLORREF kGold = RGB(238, 198, 80);
constexpr COLORREF kPaleGold = RGB(255, 232, 145);
constexpr COLORREF kText = RGB(218, 218, 199);
constexpr COLORREF kDimText = RGB(112, 119, 109);
constexpr COLORREF kPanel = RGB(8, 14, 13);
constexpr COLORREF kPanelInner = RGB(18, 25, 22);
constexpr COLORREF kPanelEdgeLight = RGB(128, 120, 87);
constexpr COLORREF kPanelEdgeDark = RGB(2, 4, 4);







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

bool parseBoolOr(const std::string& value, bool fallback) {
    const std::string lower = lowerAscii(trimAscii(value));
    if (lower == "yes" || lower == "true" || lower == "1" || lower == "on") {
        return true;
    }
    if (lower == "no" || lower == "false" || lower == "0" || lower == "off") {
        return false;
    }
    return fallback;
}

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return L"";
    }
    int count = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0
    );
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (count <= 0) {
        codePage = CP_ACP;
        flags = 0;
        count = MultiByteToWideChar(
            codePage,
            flags,
            text.data(),
            static_cast<int>(text.size()),
            nullptr,
            0
        );
    }
    if (count <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring wide(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(
        codePage,
        flags,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        count
    );
    return wide;
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
    const std::string& fallback = ""
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

#include "ui/skirmish/diagnostics/RuntimeSkirmishBitmapTextTrace.inl"

struct WestwoodBitFont {

    std::vector<unsigned char> bytes;
    std::uint32_t stride = 0;
    std::uint32_t bitmapRows = 0;
    std::uint32_t lineHeight = 0;
    std::uint32_t glyphCount = 0;
    std::uint32_t recordSize = 0;

    bool load(const fs::path& path) {
        *this = {};
        std::ifstream input(path, std::ios::binary);
        if (!input) return false;
        bytes.assign(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>()
        );
        concord::assets::WestwoodFontMetrics metrics;
        const bool sane = concord::assets::decodeWestwoodFontHeader(bytes, metrics);
        if (!sane) { *this = {}; return false; }
        stride = metrics.stride;
        bitmapRows = metrics.bitmapRows;
        lineHeight = metrics.lineHeight;
        glyphCount = metrics.glyphCount;
        recordSize = metrics.recordSize;
        return true;
    }

    bool valid() const {
        return !bytes.empty() && recordSize > 1 && lineHeight > 0;
    }

    const unsigned char* glyph(wchar_t character) const {
        if (!valid()) return nullptr;
        return concord::assets::westwoodFontGlyph(bytes,
            {stride, bitmapRows, lineHeight, glyphCount, recordSize}, static_cast<std::uint16_t>(character),
            concord::assets::WestwoodGlyphFallback::MissingReferenceOnly);
    }

    concord::ui::SkirmishBitmapFontView view() const {
        return {bytes, {stride, bitmapRows, lineHeight, glyphCount, recordSize}};
    }
    int measure(const std::wstring& text) const {
        return concord::ui::measureSkirmishBitmapText(view(), std::wstring_view(text));
    }
    void draw(unsigned char* pixels, const std::wstring& text, const RECT& rect,
        bool centered, bool verticallyCentered, COLORREF color,
        std::vector<SkirmishBitmapTextTraceCall>* trace = nullptr) const {
        if (trace != nullptr) trace->push_back({text, {rect.left, rect.top, rect.right, rect.bottom},
            centered, verticallyCentered, static_cast<std::uint32_t>(color)});
        if (!valid() || pixels == nullptr || text.empty()) return;
        concord::ui::drawSkirmishBitmapText(view(),
            std::span<unsigned char>(pixels, static_cast<std::size_t>(kBaseWidth * kBaseHeight * 4)),
            kBaseWidth, kBaseHeight, std::wstring_view(text),
            {rect.left, rect.top, rect.right, rect.bottom}, centered, verticallyCentered,
            RuntimeRgbColorValue::fromPackedRgb(static_cast<std::uint32_t>(color)));
    }
};

struct MapPacketEntry {
    std::string id;
    std::string descriptionKey;
    int minPlayers = 2;
    int maxPlayers = 8;
    std::vector<std::string> filters;
};









std::string flagNameForCountry(const std::string& id) {
    const std::string lower = lowerAscii(id);
    if (lower == "random") return "rani.shp";
    if (lower == "americans") return "usai.shp";
    if (lower == "alliance") return "japi.shp";
    if (lower == "french") return "frai.shp";
    if (lower == "germans") return "geri.shp";
    if (lower == "british") return "gbri.shp";
    if (lower == "africans") return "arbi.shp";
    if (lower == "arabs") return "djbi.shp";
    if (lower == "confederation") return "lati.shp";
    if (lower == "russians") return "rusi.shp";
    if (lower == "yuricountry") return "yrii.shp";
    return "";
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

bool readMapMetadata(const fs::path& path, MapInfo& map) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    bool hasMap = false;
    bool hasPreview = false;
    std::string section;
    std::string basicName;
    std::vector<std::optional<POINT>> headerPoints(kPlayerSlots);
    std::vector<std::optional<POINT>> waypointPoints(kPlayerSlots);
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
                POINT point{};
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
                POINT point{packed % 1000, packed / 1000};
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
    map.displayName = utf8ToWide(basicName.empty() ? map.id : basicName);
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
    if (path.empty() || !packet.loadFromFile(pathString(path))) {
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

std::vector<ModeInfo> loadModes(
    const fs::path& path,
    const CsfCatalog& csf,
    bool exposeCooperative
) {
    std::vector<ModeInfo> modes;
    IniFile ini;
    if (!path.empty() && ini.loadFromFile(pathString(path))) {
        for (const std::string& sectionName : ini.getSectionNames()) {
            const IniFile::Section* section = ini.getSection(sectionName);
            if (section == nullptr) {
                continue;
            }
            for (const auto& [rawId, rawValue] : *section) {
                const int id = parseIntOr(rawId, -1);
                const std::vector<std::string> fields = splitCsv(rawValue);
                if (id < 0 || fields.size() < 4) {
                    continue;
                }
                ModeInfo mode;
                mode.id = id;
                mode.key = fields[0];
                const std::size_t colon = mode.key.find(':');
                if (colon != std::string::npos && colon + 1 < mode.key.size()) {
                    mode.key = mode.key.substr(colon + 1);
                }
                mode.rulesOverride = fields.size() >= 3 ? fields[2] : "";
                mode.filter = fields[3];
                mode.randomMapsAllowed = fields.size() >= 5 && parseBoolOr(fields[4], false);
                // Cooperative is exposed only when its scenario database and
                // every advertised map have passed the strict data validator.
                // A missing or damaged data set must not silently start as an
                // ordinary skirmish.
                if (!exposeCooperative
                    && isUnavailableCooperativeMode(mode)) {
                    continue;
                }
                mode.displayName = csf.get(fields[0], utf8ToWide(mode.key));
                modes.push_back(std::move(mode));
            }
        }
    }
    if (modes.empty()) {
        struct FallbackMode {
            int id;
            const char* key;
            const char* rules;
            const char* filter;
        };
        const std::array<FallbackMode, 8> fallback{{
            {1, "Battle", "MPBattleMD.ini", "standard"},
            {2, "FreeForAll", "MPFreeForAllMD.ini", "standard"},
            {4, "UnholyAlliance", "MPUnholyMD.ini", "standard"},
            {5, "Megawealth", "MPMWMD.ini", "megawealth"},
            {6, "Duel", "MPDuelMD.ini", "duel"},
            {7, "MeatGrind", "MPMeatMD.ini", "meatgrind"},
            {8, "NavalWar", "MPNavalMD.ini", "navalwar"},
            {9, "TeamGame", "MPTeamMD.ini", "teamgame"}
        }};
        for (const FallbackMode& fallbackMode : fallback) {
            ModeInfo mode;
            mode.id = fallbackMode.id;
            mode.key = fallbackMode.key;
            mode.rulesOverride = fallbackMode.rules;
            mode.filter = fallbackMode.filter;
            mode.randomMapsAllowed = mode.id == 1 || mode.id == 2;
            mode.displayName = utf8ToWide(fallbackMode.key);
            modes.push_back(std::move(mode));
        }
    }
    std::sort(
        modes.begin(),
        modes.end(),
        [](const ModeInfo& left, const ModeInfo& right) {
            return left.id < right.id;
        }
    );
    modes.erase(
        std::unique(
            modes.begin(),
            modes.end(),
            [](const ModeInfo& left, const ModeInfo& right) {
                return left.id == right.id;
            }
        ),
        modes.end()
    );
    return modes;
}

std::vector<CountryInfo> loadCountries(
    const fs::path& rulesPath,
    const CsfCatalog& csf
) {
    std::vector<CountryInfo> countries;
    countries.push_back({
        "Random",
        csf.getAny({"GUI:Random", "GUI:RandomSide"}, L"Random"),
        flagNameForCountry("Random")
    });
    IniFile rules;
    if (!rulesPath.empty() && rules.loadFromFile(pathString(rulesPath))) {
        std::string countriesSection;
        for (const std::string& name : rules.getSectionNames()) {
            if (equalsAsciiNoCase(name, "Countries")) {
                countriesSection = name;
                break;
            }
        }
        const IniFile::Section* section = countriesSection.empty()
            ? nullptr
            : rules.getSection(countriesSection);
        if (section != nullptr) {
            std::vector<std::pair<int, std::string>> ordered;
            for (const auto& [rawIndex, rawCountry] : *section) {
                ordered.push_back({parseIntOr(rawIndex, 999), trimAscii(rawCountry)});
            }
            std::sort(ordered.begin(), ordered.end());
            for (const auto& [index, id] : ordered) {
                (void)index;
                if (id.empty()
                    || !parseBoolOr(iniValueNoCase(rules, id, "Multiplay"), false)) {
                    continue;
                }
                const std::string uiName = iniValueNoCase(rules, id, "UIName");
                countries.push_back({
                    id,
                    csf.get(uiName, utf8ToWide(id)),
                    flagNameForCountry(id)
                });
            }
        }
    }
    if (countries.size() == 1) {
        const std::array<const char*, 10> fallback{{
            "Americans", "Alliance", "French", "Germans", "British",
            "Africans", "Arabs", "Confederation", "Russians", "YuriCountry"
        }};
        for (const char* id : fallback) {
            countries.push_back({id, utf8ToWide(id), flagNameForCountry(id)});
        }
    }
    countries.push_back({"Observer", L"观察", ""});
    return countries;
}

using AssetImage = concord::assets::MenuImage;

bool loadResourceAsset(
    concord::assets::MenuImageResources& resources,
    const std::string& id,
    int frameIndex,
    const std::vector<RuntimePaletteColor>& palette,
    bool transparentZero,
    AssetImage& output
) {
    std::string error;
    return resources.loadFrame(id, frameIndex, palette, transparentZero, output, error);
}

bool decodePcxImage(
    const std::vector<unsigned char>& bytes,
    AssetImage& output,
    bool transparentBackground = false
) {
    output = {};
    if (bytes.size() < 128 + 769 || bytes[0] != 0x0a
        || bytes[2] != 1 || bytes[3] != 8
        || bytes[bytes.size() - 769] != 0x0c) {
        return false;
    }
    const auto u16 = [&bytes](std::size_t at) {
        return static_cast<int>(
            static_cast<unsigned int>(bytes[at])
            | (static_cast<unsigned int>(bytes[at + 1]) << 8)
        );
    };
    const int width = u16(8) - u16(4) + 1;
    const int height = u16(10) - u16(6) + 1;
    const int planes = bytes[65];
    const int bytesPerLine = u16(66);
    if (width <= 0 || height <= 0 || width > 2048 || height > 2048
        || planes != 1 || bytesPerLine < width) {
        return false;
    }
    const std::size_t decodedSize = static_cast<std::size_t>(
        height * planes * bytesPerLine
    );
    std::vector<unsigned char> decoded;
    decoded.reserve(decodedSize);
    std::size_t cursor = 128;
    const std::size_t dataEnd = bytes.size() - 769;
    while (cursor < dataEnd && decoded.size() < decodedSize) {
        unsigned char value = bytes[cursor++];
        int count = 1;
        if ((value & 0xc0U) == 0xc0U) {
            count = value & 0x3fU;
            if (cursor >= dataEnd) return false;
            value = bytes[cursor++];
        }
        while (count-- > 0 && decoded.size() < decodedSize) decoded.push_back(value);
    }
    if (decoded.size() != decodedSize) return false;
    const unsigned char* palette = bytes.data() + bytes.size() - 768;
    output.width = width;
    output.height = height;
    output.bgra.resize(static_cast<std::size_t>(width * height * 4));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char index = decoded[static_cast<std::size_t>(
                y * bytesPerLine + x
            )];
            const std::size_t at = static_cast<std::size_t>((y * width + x) * 4);
            output.bgra[at] = palette[index * 3 + 2];
            output.bgra[at + 1] = palette[index * 3 + 1];
            output.bgra[at + 2] = palette[index * 3];
            const unsigned char red = palette[index * 3];
            const unsigned char green = palette[index * 3 + 1];
            const unsigned char blue = palette[index * 3 + 2];
            const bool chromaKey = red >= 240 && blue >= 240 && green <= 32;
            output.bgra[at + 3] = transparentBackground
                && (index == 0 || chromaKey) ? 0 : 255;
        }
    }
    return true;
}

bool loadPcxImage(
    const fs::path& path,
    AssetImage& output,
    bool transparentBackground = false
) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    const std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    return decodePcxImage(bytes, output, transparentBackground);
}









#include "audio/events/RuntimeMenuSoundMci.inl"

struct MenuAudio {
    explicit MenuAudio(const RuntimeUiClock& clock = runtimeWin32UiClock())
        : musicDevice(concord::audio::createSkirmishMusicDeviceWin32(
            +[](const fs::path& path) { return utf8ToWide(pathString(path)); })),
          music(clock,regularFile) {}
    bool enabled = true;
    std::vector<std::string>* eventTrace = nullptr;
    fs::path assetRoot;
    fs::path cacheRoot;
    std::unordered_map<std::string, std::string> samples;
    std::unordered_map<std::string, std::optional<std::wstring>> wavePaths;
    std::unique_ptr<concord::audio::MenuMusicDevice> musicDevice;
    concord::ui::SkirmishMusicPolicy music;

    void initialize(const fs::path& soundIniPath) {
        samples = {
            {"MenuClick", "umenucl1"},
            {"MenuTab", "utab"},
            {"MenuACBOpen", "uacbopen"},
            {"MenuACBClose", "uacbopen"},
            {"MenuSlideIn", "uslide1"},
            {"MenuSlideOut", "uslide2"}
        };
        IniFile sound;
        if (!soundIniPath.empty() && sound.loadFromFile(pathString(soundIniPath))) {
            for (auto& [eventName, sample] : samples) {
                const std::vector<std::string> choices = splitCsv(
                    iniValueNoCase(sound, eventName, "Sounds")
                );
                if (!choices.empty()) {
                    sample = choices.front();
                    while (!sample.empty()
                           && (sample.front() == '$' || sample.front() == '#')) {
                        sample.erase(sample.begin());
                    }
                }
            }
        }
    }

    void play(const std::string& eventName, bool beepOnFailure = false) {
        if (eventTrace != nullptr) eventTrace->push_back(eventName);
        if (!enabled) {
            return;
        }
        const auto sampleIt = samples.find(eventName);
        if (sampleIt == samples.end() || sampleIt->second.empty()) {
            if (beepOnFailure) MessageBeep(MB_OK);
            return;
        }
        auto cached = wavePaths.find(eventName);
        if (cached == wavePaths.end()) {
            const AudioSampleResolveResult result = resolveAudioSampleToWave(
                pathString(assetRoot),
                pathString(cacheRoot),
                sampleIt->second
            );
            std::optional<std::wstring> resolved;
            if (result.success && !result.wavPath.empty()) {
                resolved = utf8ToWide(result.wavPath);
            }
            cached = wavePaths.emplace(eventName, std::move(resolved)).first;
        }
        if (cached->second) {
            runtimeMenuSoundPlayback().play(*cached->second);
        } else if (beepOnFailure) {
            MessageBeep(MB_OK);
        }
    }

    void startMusic(const fs::path& path) { music.start(path,enabled,*musicDevice); }
    void ensureMusicPlaying() {
        if(enabled) runtimeMenuSoundPlayback().collect();
        music.poll(enabled,*musicDevice);
    }
    void stopMusic() { music.stop(*musicDevice); }
};



struct RandomMapJob {
    bool success = false;
    RuntimeRandomMapResult result;
    std::string error;
};





// Private diagnostics observes the existing disposal calls; ordinary owners leave this null.
using concord::ui::win32::SkirmishGdiReleaseTrace;

struct MenuState : RuntimeSkirmishServices, RuntimeSkirmishController {
    MenuState() : RuntimeSkirmishController(static_cast<RuntimeSkirmishServices&>(*this)) {}
    void requestRepaint() override;
    void playSound(const std::string&, bool) override;
    void stopMusic() override;
    std::uint64_t nowMs() const override;
    void destroyWindow() override;
    void prewarmMainMenu() override;
    bool saveCratePreference(const RuntimeConfig&, bool, std::string&) override;
    bool saveNoCooldownPreference(const RuntimeConfig&, bool, std::string&) override;
    bool ensurePreview(int) override;
    void loadDefaults(const RuntimeConfig&, RuntimeSkirmishSelection&) override;
    std::wstring text(const std::string&, const std::wstring&) const override;
    std::wstring label(std::initializer_list<std::string>, const wchar_t*) const override;
    std::wstring widenUtf8(const std::string&) const override;
    std::string loadingBriefKey(const std::string&) const override;
    std::string loadingScreenAsset(const std::string&) const override;
    const CooperativeScenarioDefinition* cooperativeScenario(const std::string&) const override;
    bool generatedMapFile(const std::string&) const override;
    int sliderGripWidth() const override;
    bool cursorAvailable() const override;
    bool activateRandomTarget(HitTarget, int) override;
    bool randomMapReady() const override;
    void warnInvalidSelection() override;
    void focus() override;
    void capture() override;
    void releaseCaptureIfOwned() override;


    std::unique_ptr<RuntimeMenuResourceWatch> resourceWatch;

    RuntimeRandomMapOptions randomOptions;
    std::optional<RuntimeRandomMapResult> randomResult;
    std::future<RandomMapJob> pendingRandomMap;
    bool randomDirty = true;
    std::wstring randomName;
    HWND randomNameWindow = nullptr;
    std::vector<MapInfo> savedRandomMaps;
    int selectedSavedRandomMap = 0;
    int randomSavedScroll = 0;
    CsfCatalog csf;




    CooperativeScenarioDatabase cooperativeScenarios;



















    concord::window::SoftwareWindow* softwareWindow = nullptr;
    std::string softwareFailure;
    HWND hwnd = nullptr;






    bool initialized = false;










    fs::path exportRoot;
    fs::path localUiRoot;
    fs::path palettePath;
    AssetImage background;
    AssetImage mouse;
    std::array<AssetImage, 3> buttons;
    concord::assets::MenuImageResources shellResources;
    std::vector<RuntimePaletteColor> shellPalette;
    std::vector<RuntimePaletteColor> mainButtonPalette;
    std::vector<RuntimePaletteColor> slotPalette;
    AssetImage statusShell;
    std::array<AssetImage, 2> sideTopShell;
    AssetImage slotBackgroundShell;
    std::vector<AssetImage> slotAnimationShell;
    AssetImage sideBottomShell;
    AssetImage mpInfoShell;
    AssetImage checkOff;
    AssetImage checkOn;
    AssetImage trackGrip;
    AssetImage dropArrow;
    AssetImage dropArrowPressed;
    AssetImage scrollUp;
    AssetImage scrollUpPressed;
    AssetImage scrollDown;
    AssetImage scrollDownPressed;
    AssetImage scrollGripTop;
    AssetImage scrollGripMiddle;
    AssetImage scrollGripBottom;
    MixArchive flagPcxArchive;
    bool flagPcxArchiveAttempted = false;
    bool flagPcxArchiveLoaded = false;
    WestwoodBitFont gameFont;
    std::vector<SkirmishBitmapTextTraceCall>* bitmapTextTrace = nullptr;
    concord::ui::win32::SkirmishBackBufferWin32 backBuffer;
    concord::ui::MenuPixelBuffer composeFrame;

    std::unordered_map<std::string, AssetImage> flagImages;
    std::set<std::string> flagLoadAttempts;
    HFONT titleFont = nullptr;
    HFONT captionFont = nullptr;
    HFONT normalFont = nullptr;
    HFONT smallFont = nullptr;
    MenuAudio audio;

    SkirmishGdiReleaseTrace* gdiReleaseTrace = nullptr;

    ~MenuState() override;
};

#include "ui/skirmish/win32/RuntimeSkirmishGdiLifetimeWin32.inl"

AssetImage* flagFor(MenuState& state, const std::string& countryId);

std::unique_ptr<MenuState> prewarmedMenuState;
std::future<std::unique_ptr<MenuState>> pendingMenuState;

std::wstring label(
    const MenuState& state,
    std::initializer_list<std::string> keys,
    const wchar_t* fallback
) {
    return state.csf.getAny(keys, fallback);
}

void loadOriginalSkirmishDefaults(
    const RuntimeConfig& config,
    RuntimeSkirmishSelection& selection
) {
    selection.startingCredits = std::clamp(
        config.startingCredits,
        5000,
        200000
    );
    selection.unitCount = 10;
    // RuntimeConfig carries current-session preferences, including values
    // just applied by the runtime UI or configured as the preferred lobby
    // defaults. RA2MD.INI remains the source for the other original options.
    selection.gameSpeed = std::clamp(config.gameSpeed, 0, 6);
    selection.shortGame = true;
    selection.mcvRepacks = true;
    selection.crates = true;
    selection.superweapons = false;
    selection.buildOffAlly = false;
    selection.computerAiEnabled = config.computerAiEnabled;

    const fs::path configuredRoot(config.resourceRootPath);
    const fs::path iniPath = firstExisting({
        configuredRoot / "GameMD" / "RA2MD.INI",
        fs::current_path() / configuredRoot / "GameMD" / "RA2MD.INI",
        fs::current_path() / "RedAlert2" / "GameMD" / "RA2MD.INI",
        fs::path(config.localDataRootPath) / "RA2MD.INI"
    });
    IniFile ini;
    if (iniPath.empty() || !ini.loadFromFile(pathString(iniPath))) {
        return;
    }
    selection.unitCount = std::clamp(
        parseIntOr(
            iniValueNoCase(ini, "Skirmish", "UnitCount"),
            selection.unitCount
        ),
        0,
        10
    );
    selection.shortGame = parseBoolOr(
        iniValueNoCase(ini, "Skirmish", "ShortGame"),
        selection.shortGame
    );
    selection.mcvRepacks = parseBoolOr(
        iniValueNoCase(ini, "Skirmish", "MCVRepacks"),
        selection.mcvRepacks
    );
    selection.crates = parseBoolOr(
        iniValueNoCase(ini, "Skirmish", "CratesAppear"),
        selection.crates
    );
    selection.superweapons = parseBoolOr(
        iniValueNoCase(ini, "Skirmish", "SuperWeaponsAllowed"),
        selection.superweapons
    );
    selection.buildOffAlly = parseBoolOr(
        iniValueNoCase(ini, "Skirmish", "BuildOffAlly"),
        selection.buildOffAlly
    );
}

void loadSkirmishDefaults(
    const RuntimeConfig& config,
    RuntimeSkirmishSelection& selection
) {
    loadOriginalSkirmishDefaults(config, selection);
    selection.noCooldown = config.noCooldown;
    // The installed game's INI provides the first-run default. An accepted
    // Concord lobby choice takes precedence without modifying original files.
    IniFile preferences;
    if (preferences.loadFromFile(config.configPath)) {
        selection.crates = parseBoolOr(
            iniValueNoCase(preferences, "Skirmish", "Crates"), selection.crates);
        selection.noCooldown = parseBoolOr(
            iniValueNoCase(preferences, "Skirmish", "NoCooldown"), selection.noCooldown);
    }
    if (config.multiplayerActive) selection.noCooldown = false;
}



std::vector<MapInfo> scanMaps(
    const fs::path& directory,
    const std::unordered_map<std::string, MapPacketEntry>& packet,
    const CsfCatalog& csf
) {
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
        map.displayName = utf8ToWide(map.id);
        const auto packetEntry = packet.find(lowerAscii(map.id));
        // This directory is an extraction of MULTI*.MIX, which also contains
        // maps Westwood deliberately commented out of missionsmd.pkt.  The
        // original skirmish browser only exposes active packet entries.
        if (!packet.empty() && packetEntry == packet.end()) {
            continue;
        }
        if (packet.empty() && !readMapMetadata(path, map)) {
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

void appendPackagedMaps(
    std::vector<MapInfo>& maps,
    const RuntimeMapPackageScanResult& packageScan,
    const CsfCatalog& csf
) {
    std::set<std::string> knownIds;
    for (const MapInfo& map : maps) {
        knownIds.insert(lowerAscii(map.id));
    }

    for (const RuntimeMapPackageMap& packaged : packageScan.maps) {
        if (!knownIds.insert(lowerAscii(packaged.id)).second) {
            continue;
        }

        MapInfo map;
        map.path = packaged.mapPath;
        map.minPlayers = packaged.minPlayers;
        map.maxPlayers = packaged.maxPlayers;
        if (!readMapMetadata(packaged.mapPath, map)) {
            continue;
        }
        map.id = packaged.id;
        map.minPlayers = packaged.minPlayers;
        map.maxPlayers = packaged.maxPlayers;
        map.filters = packaged.filters;
        if (map.filters.empty()) {
            map.filters.push_back("standard");
        }
        if (!packaged.descriptionKey.empty()) {
            map.displayName = csf.get(
                packaged.descriptionKey,
                map.displayName.empty()
                    ? utf8ToWide(packaged.id)
                    : map.displayName
            );
        } else if (map.displayName.empty()) {
            map.displayName = utf8ToWide(packaged.id);
        }
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
}





















void prepareCooperativeData(MenuState& state) {
    state.cooperativeValidationByMap.clear();
    state.cooperativeDataComplete = false;
    const auto quarantineCooperativeMaps = [&state]() {
        for (MapInfo& map : state.maps) {
            for (std::string& filter : map.filters) {
                if (equalsAsciiNoCase(filter, "cooperative")) {
                    filter = "cooperative-invalid";
                }
            }
        }
    };

    const fs::path scenarioPath = firstExisting({
        state.localUiRoot / "coopcampmd.ini",
        state.exportRoot / "expandmd01" / "coopcampmd.ini",
        fs::path(state.config.localDataRootPath) / "coopcampmd.ini"
    });
    IniFile scenarioIni;
    if (scenarioPath.empty()
        || !scenarioIni.loadFromFile(pathString(scenarioPath))
        || !state.cooperativeScenarios.loadFromIni(scenarioIni)) {
        quarantineCooperativeMaps();
        return;
    }

    for (const CooperativeScenarioDefinition& scenario :
            state.cooperativeScenarios.scenarios()) {
        for (const std::vector<std::string>* countries : {
                &scenario.playerCountries,
                &scenario.enemyCountries
            }) {
            for (const std::string& country : *countries) {
                const bool available = std::any_of(
                    state.countries.begin(),
                    state.countries.end(),
                    [&country](const CountryInfo& candidate) {
                        return equalsAsciiNoCase(candidate.id, country);
                    }
                );
                if (!available) {
                    quarantineCooperativeMaps();
                    return;
                }
            }
        }
    }

    std::set<std::string> validatedScenarioMaps;
    bool advertisedMapsValid = true;
    for (MapInfo& map : state.maps) {
        auto cooperativeFilter = std::find_if(
            map.filters.begin(),
            map.filters.end(),
            [](const std::string& filter) {
                return equalsAsciiNoCase(filter, "cooperative");
            }
        );
        if (cooperativeFilter == map.filters.end()) {
            continue;
        }
        const CooperativeScenarioDefinition* scenario =
            state.cooperativeScenarios.findByMapId(map.id);
        IniFile mapIni;
        MapDatabase mapDatabase;
        if (scenario == nullptr
            || !mapIni.loadFromFile(pathString(map.path))
            || !mapDatabase.loadFromIni(mapIni)) {
            *cooperativeFilter = "cooperative-invalid";
            advertisedMapsValid = false;
            continue;
        }
        CooperativeScenarioMapValidation validation =
            state.cooperativeScenarios.validateMap(
                *scenario,
                mapDatabase,
                mapIni
            );
        if (!validation.valid) {
            *cooperativeFilter = "cooperative-invalid";
            advertisedMapsValid = false;
            continue;
        }
        const std::string key = lowerAscii(map.id);
        state.cooperativeValidationByMap.emplace(key, std::move(validation));
        validatedScenarioMaps.insert(key);
    }

    for (const CooperativeScenarioDefinition& scenario :
            state.cooperativeScenarios.scenarios()) {
        if (validatedScenarioMaps.find(lowerAscii(scenario.mapId))
            == validatedScenarioMaps.end()) {
            advertisedMapsValid = false;
            break;
        }
    }
    state.cooperativeDataComplete = advertisedMapsValid
        && validatedScenarioMaps.size()
            == state.cooperativeScenarios.scenarioCount();
    if (!state.cooperativeDataComplete) {
        state.cooperativeValidationByMap.clear();
        quarantineCooperativeMaps();
    }
}













bool ensurePreview(MenuState& state, int mapIndex) {
    if (mapIndex < 0 || mapIndex >= static_cast<int>(state.maps.size())) {
        return false;
    }
    MapInfo& map = state.maps[static_cast<std::size_t>(mapIndex)];
    if (map.previewAttempted) {
        return map.preview.width > 0 && map.preview.height > 0;
    }
    map.previewAttempted = true;
    if (!map.metadataLoaded) {
        MapInfo metadata;
        metadata.path = map.path;
        if (!readMapMetadata(map.path, metadata)) {
            map.previewError = "map metadata load failed";
            return false;
        }
        map.theater = std::move(metadata.theater);
        map.size = std::move(metadata.size);
        map.previewWidth = metadata.previewWidth;
        map.previewHeight = metadata.previewHeight;
        map.headerX = metadata.headerX;
        map.headerY = metadata.headerY;
        map.headerWidth = metadata.headerWidth;
        map.headerHeight = metadata.headerHeight;
        map.startPoints = std::move(metadata.startPoints);
        map.metadataLoaded = true;
    }
    IniFile ini;
    if (!ini.loadFromFile(pathString(map.path))) {
        map.previewError = "map INI load failed";
        return false;
    }
    return decodeMapPreviewImage(ini, map.preview, map.previewError);
}







void initializeFonts(MenuState& state) {
    state.titleFont = CreateFontW(
        -26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
    state.captionFont = CreateFontW(
        -14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
    state.normalFont = CreateFontW(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
    state.smallFont = CreateFontW(
        -11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FF_DONTCARE, L"Microsoft YaHei UI"
    );
}

void resetMenuForEntry(MenuState& state, const RuntimeConfig& config) {
    RuntimePerfTraceScope trace("skirmish.reenter", 0);
    resetControllerForEntry(state, config);
}

bool initializeState(MenuState& state, const RuntimeConfig& config, bool smoke, bool nativeFonts = true) {
    RuntimePerfTraceScope initializeTrace("skirmish.initialize", 0);
    state.config = config;
    state.resourceWatch = std::make_unique<RuntimeMenuResourceWatch>(config);
    state.smoke = smoke;
    state.audio.enabled = !smoke;
    state.exportRoot = fs::path(config.localDataRootPath)
        / "resources" / "mix_exports";
    state.localUiRoot = state.exportRoot / "ra2md" / "localmd";
    state.audio.assetRoot = state.exportRoot;
    state.audio.cacheRoot = fs::path(config.localDataRootPath) / "runtime_audio_cache";

    const fs::path csfPath = firstExisting({
        fs::path(config.resourceRootPath) / "GameMD" / "ra2md.csf",
        state.exportRoot / "langmd" / "ra2md.csf",
        state.exportRoot / "lang" / "ra2.csf",
        state.localUiRoot / "ra2md.csf"
    });
    if (!csfPath.empty()) {
        state.csf.load(csfPath);
    }

    const fs::path packetPath = firstExisting({
        state.exportRoot / "langmd" / "missionsmd.pkt",
        state.exportRoot / "lang" / "missions.pkt",
        state.localUiRoot / "missionsmd.pkt"
    });
    const auto packet = loadMapPacket(packetPath);
    {
        RuntimePerfTraceScope trace("skirmish.maps", 0);
        state.maps = scanMaps(state.exportRoot / "multimd", packet, state.csf);
    }
    const fs::path resourceRoot(config.resourceRootPath);
    {
        RuntimePerfTraceScope trace("skirmish.packages", 0);
        const RuntimeMapPackageScanResult packageScan = scanRuntimeMapPackages(
            {
                resourceRoot / "GameMD",
                resourceRoot / "Game",
                resourceRoot
            },
            fs::path(config.localDataRootPath)
                / "runtime_map_cache"
                / "original_packages"
        );
        appendPackagedMaps(state.maps, packageScan, state.csf);
        for (const std::string& warning : packageScan.warnings) {
            std::cerr << "Map package warning: " << warning << '\n';
        }
    }

    for (auto map : scanMaps(runtimeSavedRandomMapDirectory(config), {}, state.csf)) {
        RuntimeRandomMapOptions options;
        if (!runtimeRandomMapOptionsFromFileName(map.path.filename().string(), options)) continue;
        map.filters = {"standard"};
        state.maps.push_back(std::move(map));
    }

    const fs::path rulesPath = firstExisting({
        fs::path(config.localDataRootPath) / "rulesmd.ini",
        state.exportRoot / "expandmd01" / "rulesmd.ini",
        state.localUiRoot / "rulesmd.ini"
    });
    {
        RuntimePerfTraceScope trace("skirmish.countries", 0);
        state.countries = loadCountries(rulesPath, state.csf);
    }
    {
        RuntimePerfTraceScope trace("skirmish.cooperative", 0);
        prepareCooperativeData(state);
    }

    const fs::path modesPath = firstExisting({
        state.exportRoot / "expandmd01" / "mpmodesmd.ini",
        state.localUiRoot / "mpmodesmd.ini",
        fs::path(config.localDataRootPath) / "mpmodesmd.ini"
    });
    state.modes = loadModes(
        modesPath,
        state.csf,
        state.cooperativeDataComplete
    );

    resetMenuForEntry(state, config);

    RuntimePerfTraceScope assetsTrace("skirmish.assets", 0);
    state.palettePath = firstExisting({
        state.exportRoot / "expandmd01" / "mpyscrn.pal",
        state.localUiRoot / "mpyscrn.pal"
    });
    concord::assets::MapPreviewResourceContext shellRoots;
    shellRoots.assetRootPath = state.exportRoot.string();
    shellRoots.tileRootPath = state.exportRoot.string();
    state.shellResources.configureRoots(std::move(shellRoots));
    std::string paletteSource;
    std::string paletteError;
    const bool palettesLoaded =
        state.shellResources.loadPalette(
 "shell.pal",
            state.shellPalette, paletteSource, paletteError
        )
        && state.shellResources.loadPalette(
 "mainbttn.pal",
            state.mainButtonPalette, paletteSource, paletteError
        )
        && state.shellResources.loadPalette(
 "sdbtnanm.pal",
            state.slotPalette, paletteSource, paletteError
        );
    bool shellLoaded = palettesLoaded;
    std::vector<RuntimePaletteColor> mousePalette;
    if (state.shellResources.loadPalette(
 "mousepal.pal",
            mousePalette, paletteSource, paletteError)) {
        loadResourceAsset(
            state.shellResources, "mouse.shp", 0,
            mousePalette, true, state.mouse
        );
    }
    shellLoaded = loadResourceAsset(
        state.shellResources, "mnscrnl.shp", 0,
        state.shellPalette, false, state.background
    ) && shellLoaded;
    shellLoaded = loadResourceAsset(
        state.shellResources, "lwscrnl.shp", 0,
        state.shellPalette, false, state.statusShell
    ) && shellLoaded;
    for (int frame = 0; frame < 2; ++frame) {
        shellLoaded = loadResourceAsset(
            state.shellResources, "sdtp.shp", frame,
            state.shellPalette, false,
            state.sideTopShell[static_cast<std::size_t>(frame)]
        ) && shellLoaded;
    }
    shellLoaded = loadResourceAsset(
        state.shellResources, "sdbtnbkgd.shp", 0,
        state.shellPalette, false, state.slotBackgroundShell
    ) && shellLoaded;
    state.slotAnimationShell.clear();
    for (int frame = 0; frame < 17; ++frame) {
        AssetImage image;
        if (!loadResourceAsset(
                state.shellResources, "sdbtnanm.shp", frame,
                state.slotPalette, true, image)) {
            break;
        }
        state.slotAnimationShell.push_back(std::move(image));
    }
    shellLoaded = state.slotAnimationShell.size() >= 17 && shellLoaded;
    shellLoaded = loadResourceAsset(
        state.shellResources, "sdbtm.shp", 0,
        state.shellPalette, false, state.sideBottomShell
    ) && shellLoaded;
    shellLoaded = loadResourceAsset(
        state.shellResources, "sdmpbtn.shp", 0,
        state.shellPalette, true, state.mpInfoShell
    ) && shellLoaded;
    for (int frame = 0; frame < 3; ++frame) {
        shellLoaded = loadResourceAsset(
            state.shellResources, "mnbttn.shp", frame,
            state.mainButtonPalette, true,
            state.buttons[static_cast<std::size_t>(frame)]
        ) && shellLoaded;
    }
    shellLoaded = loadPcxImage(
        state.localUiRoot / "cue_i.pcx", state.checkOff
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "cce_i.pcx", state.checkOn
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "trakgrip.pcx", state.trackGrip
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "dnarrowr.pcx", state.dropArrow
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "dnarrowp.pcx", state.dropArrowPressed
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "uparrowr.pcx", state.scrollUp
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "uparrowp.pcx", state.scrollUpPressed
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "dnarrowr.pcx", state.scrollDown
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "dnarrowp.pcx", state.scrollDownPressed
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "sbgript.pcx", state.scrollGripTop
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "sbgripm.pcx", state.scrollGripMiddle
    ) && shellLoaded;
    shellLoaded = loadPcxImage(
        state.localUiRoot / "sbgripb.pcx", state.scrollGripBottom
    ) && shellLoaded;
    const fs::path fontPath = firstExisting({
        fs::path(config.resourceRootPath) / "GameMD" / "game.fnt",
        fs::path(config.resourceRootPath) / "Game" / "game.fnt"
    });
    if (!fontPath.empty()) state.gameFont.load(fontPath);
    // Loading PCX/SHP flags mutates the flag cache.  Keep that I/O out of the
    // paint pass so opening the country dropdown always composites over the
    // complete shell frame, including on its very first display.
    state.flagImages.reserve(state.countries.size());
    for (const CountryInfo& country : state.countries) {
        (void)flagFor(state, country.id);
    }
    const fs::path soundPath = firstExisting({
        state.exportRoot / "expandmd01" / "soundmd.ini",
        state.localUiRoot / "soundmd.ini",
        fs::path(config.localDataRootPath) / "soundmd.ini"
    });
    state.audio.initialize(soundPath);
    if (nativeFonts) initializeFonts(state);
    state.initialized = !state.maps.empty() && shellLoaded && state.gameFont.valid();
    return state.initialized;
}

RECT rectOf(int left, int top, int right, int bottom) {
    return RECT{left, top, right, bottom};
}

bool pointIn(const RECT& rect, POINT point) {
    return point.x >= rect.left && point.x < rect.right
        && point.y >= rect.top && point.y < rect.bottom;
}

void fillRectColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void frameRectColor(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FrameRect(dc, &rect, brush);
    DeleteObject(brush);
}

void drawLine(HDC dc, int x1, int y1, int x2, int y2, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, nullptr);
    LineTo(dc, x2, y2);
    SelectObject(dc, old);
    DeleteObject(pen);
}

void drawBevel(HDC dc, const RECT& rect, bool sunken) {
    const COLORREF topLeft = sunken ? kPanelEdgeDark : kPanelEdgeLight;
    const COLORREF bottomRight = sunken ? kPanelEdgeLight : kPanelEdgeDark;
    drawLine(dc, rect.left, rect.bottom - 1, rect.left, rect.top, topLeft);
    drawLine(dc, rect.left, rect.top, rect.right - 1, rect.top, topLeft);
    drawLine(dc, rect.right - 1, rect.top, rect.right - 1, rect.bottom, bottomRight);
    drawLine(dc, rect.right - 1, rect.bottom - 1, rect.left, rect.bottom - 1, bottomRight);
}

void drawTextIn(
    HDC dc,
    const std::wstring& value,
    RECT rect,
    HFONT font,
    COLORREF color,
    UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
) {
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
    const COLORREF oldColor = SetTextColor(dc, color);
    const int oldMode = SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect, format);
    SetBkMode(dc, oldMode);
    SetTextColor(dc, oldColor);
    if (oldFont != nullptr) SelectObject(dc, oldFont);
}

void drawShadowText(
    HDC dc,
    const std::wstring& value,
    RECT rect,
    HFONT font,
    COLORREF color,
    UINT format
) {
    RECT shadow = rect;
    OffsetRect(&shadow, 1, 1);
    drawTextIn(dc, value, shadow, font, RGB(0, 0, 0), format);
    drawTextIn(dc, value, rect, font, color, format);
}

void drawAsset(HDC dc, const RECT& destination, const AssetImage& image) {
    if (!image.valid()) {
        return;
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width;
    info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(
        dc,
        destination.left,
        destination.top,
        destination.right - destination.left,
        destination.bottom - destination.top,
        0,
        0,
        image.width,
        image.height,
        image.bgra.data(),
        &info,
        DIB_RGB_COLORS,
        SRCCOPY
    );
}

void drawPanel(
    HDC dc,
    const RECT& rect,
    const std::wstring& title,
    HFONT font
) {
    fillRectColor(dc, rect, kPanel);
    drawBevel(dc, rect, true);
    RECT titleRect{rect.left + 7, rect.top + 1, rect.right - 6, rect.top + 20};
    drawShadowText(
        dc,
        title,
        titleRect,
        font,
        kGold,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
    drawLine(
        dc,
        rect.left + 4,
        rect.top + 20,
        rect.right - 4,
        rect.top + 20,
        RGB(75, 72, 54)
    );
}

void addHit(MenuState& state, const RECT& rect, HitKind kind, int index = -1) {
    state.hits.push_back({{rect.left, rect.top, rect.right, rect.bottom}, HitTarget{kind, index}});
}

const HitRegion* hitTest(const MenuState& state, POINT point) {
    return concord::skirmish::hitTest(state, {point.x, point.y});
}

COLORREF colorForId(const std::string& id) {
    const std::string lower = lowerAscii(id);
    if (lower == "gold") return RGB(244, 208, 42);
    if (lower == "darkred") return RGB(190, 28, 26);
    if (lower == "darkblue") return RGB(41, 91, 211);
    if (lower == "darkgreen") return RGB(42, 169, 75);
    if (lower == "orange") return RGB(238, 111, 30);
    if (lower == "darksky") return RGB(76, 193, 208);
    if (lower == "purple") return RGB(129, 62, 181);
    if (lower == "magenta") return RGB(218, 75, 148);
    return RGB(145, 145, 145);
}

std::wstring colorDisplayName(const MenuState& state, const std::string& id) {
    const std::string lower = lowerAscii(id);
    if (lower == "gold") {
        return label(state, {"GUI:GoldAbbrev"}, L"Gold");
    }
    if (lower == "darkred") {
        return label(state, {"GUI:RedAbbrev"}, L"Red");
    }
    if (lower == "darkblue") {
        return label(state, {"GUI:BlueAbbrev"}, L"Blue");
    }
    if (lower == "darkgreen") {
        return label(state, {"GUI:GreenAbbrev"}, L"Green");
    }
    if (lower == "orange") {
        return label(state, {"GUI:OrangeAbbrev"}, L"Orange");
    }
    if (lower == "darksky") {
        return label(state, {"GUI:SkyBlueAbbrev"}, L"Sky Blue");
    }
    if (lower == "purple") {
        return label(state, {"GUI:PurpleAbbrev"}, L"Purple");
    }
    if (lower == "magenta") {
        return label(state, {"GUI:PinkAbbrev"}, L"Pink");
    }
    return utf8ToWide(id);
}

std::wstring difficultyDisplayName(
    const MenuState& state,
    const std::string& difficulty
) {
    const std::string lower = lowerAscii(difficulty);
    if (lower == "easy") {
        return label(state, {"GUI:AIEasy"}, L"Easy Enemy");
    }
    if (lower == "hard") {
        return label(state, {"GUI:AIHard"}, L"Brutal Enemy");
    }
    return label(state, {"GUI:AINormal", "GUI:Medium"}, L"Medium Enemy");
}

AssetImage* flagFor(MenuState& state, const std::string& countryId) {
    const int countryIndex = findCountryIndex(state, countryId);
    if (countryIndex < 0
        || countryIndex >= static_cast<int>(state.countries.size())) {
        return nullptr;
    }
    const CountryInfo& country = state.countries[static_cast<std::size_t>(countryIndex)];
    if (country.flagName.empty()) {
        return nullptr;
    }
    const std::string key = lowerAscii(country.flagName);
    auto loaded = state.flagImages.find(key);
    if (loaded != state.flagImages.end()) {
        return loaded->second.valid() ? &loaded->second : nullptr;
    }
    if (!state.flagLoadAttempts.insert(key).second) {
        return nullptr;
    }
    AssetImage image;
    fs::path pcxName(country.flagName);
    pcxName.replace_extension(".pcx");
    if (loadPcxImage(state.localUiRoot / pcxName, image, true)) {
        auto result = state.flagImages.emplace(key, std::move(image));
        return result.first->second.valid() ? &result.first->second : nullptr;
    }
    if (!state.flagPcxArchiveAttempted) {
        state.flagPcxArchiveAttempted = true;
        state.flagPcxArchiveLoaded = state.flagPcxArchive.loadFromFile(
            pathString(state.exportRoot / "ra2" / "local.mix")
        ) && state.flagPcxArchive.isIndexReadable();
    }
    if (state.flagPcxArchiveLoaded) {
        const MixEntry* entry = state.flagPcxArchive.findEntryByName(
            pcxName.filename().string()
        );
        if (entry != nullptr) {
            std::vector<std::uint8_t> entryBytes;
            std::string error;
            if (state.flagPcxArchive.readEntryData(*entry, entryBytes, error)) {
                const std::vector<unsigned char> pcxBytes(
                    entryBytes.begin(), entryBytes.end()
                );
                if (decodePcxImage(pcxBytes, image, true)) {
                    auto result = state.flagImages.emplace(key, std::move(image));
                    return result.first->second.valid()
                        ? &result.first->second : nullptr;
                }
            }
        }
    }
    const fs::path shpPath = firstExisting({
        state.localUiRoot / country.flagName,
        state.exportRoot / "expandmd01" / country.flagName
    });
    fs::path palette = state.palettePath;
    if (equalsAsciiNoCase(country.id, "YuriCountry")) {
        palette = firstExisting({
            state.localUiRoot / "yrii.pal",
            state.palettePath
        });
    }
    concord::assets::loadMenuFrameLocalShp(shpPath, palette, 0, image, true);
    auto result = state.flagImages.emplace(key, std::move(image));
    return result.first->second.valid() ? &result.first->second : nullptr;
}

void drawFallbackBackground(HDC dc) {
    for (int y = 0; y < kBaseHeight; y += 8) {
        const int shade = std::max(0, 38 - y / 26);
        RECT band{0, y, kBaseWidth, std::min(kBaseHeight, y + 8)};
        fillRectColor(dc, band, RGB(24 + shade, 21 + shade / 2, 12));
    }
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(85, 68, 36));
    HGDIOBJ old = SelectObject(dc, pen);
    for (int x = -kBaseHeight; x < kBaseWidth; x += 28) {
        MoveToEx(dc, x, kBaseHeight, nullptr);
        LineTo(dc, x + kBaseHeight, 0);
    }
    SelectObject(dc, old);
    DeleteObject(pen);
    frameRectColor(dc, rectOf(2, 2, kBaseWidth - 2, kBaseHeight - 2), RGB(165, 151, 112));
    frameRectColor(dc, rectOf(5, 5, kBaseWidth - 5, kBaseHeight - 5), RGB(24, 24, 19));
}

void drawModeList(MenuState& state, HDC dc) {
    const RECT panel = rectOf(17, 44, 151, 229);
    drawPanel(
        dc,
        panel,
        label(state, {"GUI:GameType", "GUI:GameMode"}, L"GAME TYPE"),
        state.captionFont
    );
    const int rowTop = 66;
    const int rowHeight = 17;
    for (std::size_t i = 0; i < state.modes.size() && i < 9; ++i) {
        RECT row{panel.left + 4, rowTop + static_cast<int>(i) * rowHeight,
                 panel.right - 4, rowTop + static_cast<int>(i + 1) * rowHeight};
        const HitTarget target{HitKind::Mode, static_cast<int>(i)};
        const bool selected = state.selectedMode == static_cast<int>(i);
        const bool hot = state.hover == target || state.pressed == target;
        if (selected) {
            fillRectColor(dc, row, RGB(81, 62, 26));
            frameRectColor(dc, row, kGold);
        } else if (hot) {
            fillRectColor(dc, row, RGB(44, 46, 32));
        }
        RECT textRect = row;
        textRect.left += 6;
        drawTextIn(
            dc,
            state.modes[i].displayName,
            textRect,
            state.normalFont,
            selected || hot ? kPaleGold : kText
        );
        addHit(state, row, HitKind::Mode, static_cast<int>(i));
    }
}

void drawScrollArrow(
    MenuState& state,
    HDC dc,
    const RECT& rect,
    HitKind kind,
    bool up,
    bool enabled
) {
    const HitTarget target{kind, -1};
    const bool hot = enabled && (state.hover == target || state.pressed == target);
    fillRectColor(dc, rect, hot ? RGB(68, 58, 32) : RGB(28, 34, 29));
    drawBevel(dc, rect, state.pressed == target);
    const int midX = (rect.left + rect.right) / 2;
    const int midY = (rect.top + rect.bottom) / 2;
    POINT points[3]{};
    if (up) {
        points[0] = {midX, midY - 3};
        points[1] = {midX - 4, midY + 3};
        points[2] = {midX + 4, midY + 3};
    } else {
        points[0] = {midX, midY + 3};
        points[1] = {midX - 4, midY - 3};
        points[2] = {midX + 4, midY - 3};
    }
    HBRUSH brush = CreateSolidBrush(enabled ? kGold : kDimText);
    HPEN pen = CreatePen(PS_SOLID, 1, enabled ? kGold : kDimText);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    Polygon(dc, points, 3);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    addHit(state, rect, kind);
}

void drawMapList(MenuState& state, HDC dc) {
    const RECT panel = rectOf(155, 44, 394, 229);
    drawPanel(
        dc,
        panel,
        label(state, {"GUI:Map", "GUI:ChooseMap"}, L"MAPS"),
        state.captionFont
    );
    const int rowTop = 66;
    const int rowHeight = 17;
    const int maxScroll = std::max(
        0,
        static_cast<int>(state.filteredMaps.size()) - kVisibleMapRows
    );
    state.mapScroll = std::clamp(state.mapScroll, 0, maxScroll);
    for (int visible = 0; visible < kVisibleMapRows; ++visible) {
        const int filteredIndex = state.mapScroll + visible;
        if (filteredIndex >= static_cast<int>(state.filteredMaps.size())) {
            break;
        }
        const int mapIndex = state.filteredMaps[static_cast<std::size_t>(filteredIndex)];
        const MapInfo& map = state.maps[static_cast<std::size_t>(mapIndex)];
        RECT row{panel.left + 4, rowTop + visible * rowHeight,
                 panel.right - 18, rowTop + (visible + 1) * rowHeight};
        const HitTarget target{HitKind::Map, filteredIndex};
        const bool selected = state.selectedMap == mapIndex;
        const bool hot = state.hover == target || state.pressed == target;
        if (selected) {
            fillRectColor(dc, row, RGB(81, 62, 26));
            frameRectColor(dc, row, kGold);
        } else if (hot) {
            fillRectColor(dc, row, RGB(44, 46, 32));
        }
        RECT textRect = row;
        textRect.left += 7;
        textRect.right -= 3;
        drawTextIn(
            dc,
            map.displayName,
            textRect,
            state.normalFont,
            selected || hot ? kPaleGold : kText
        );
        addHit(state, row, HitKind::Map, filteredIndex);
    }
    drawScrollArrow(
        state,
        dc,
        rectOf(panel.right - 16, rowTop, panel.right - 4, rowTop + 16),
        HitKind::MapScrollUp,
        true,
        state.mapScroll > 0
    );
    drawScrollArrow(
        state,
        dc,
        rectOf(panel.right - 16, rowTop + 8 * rowHeight, panel.right - 4,
               rowTop + 8 * rowHeight + 16),
        HitKind::MapScrollDown,
        false,
        state.mapScroll < maxScroll
    );
    const RECT track = rectOf(
        panel.right - 14,
        rowTop + 18,
        panel.right - 6,
        rowTop + 8 * rowHeight - 2
    );
    fillRectColor(dc, track, RGB(12, 17, 15));
    if (maxScroll > 0) {
        const int trackHeight = track.bottom - track.top;
        const int thumbHeight = std::max(14, trackHeight * kVisibleMapRows
            / static_cast<int>(state.filteredMaps.size()));
        const int thumbTop = track.top
            + (trackHeight - thumbHeight) * state.mapScroll / maxScroll;
        RECT thumb{track.left, thumbTop, track.right, thumbTop + thumbHeight};
        fillRectColor(dc, thumb, RGB(113, 98, 56));
        drawBevel(dc, thumb, false);
    }
}

void drawPreviewImage(MenuState& state, HDC dc, const RECT& rect, MapInfo& map) {
    fillRectColor(dc, rect, RGB(3, 6, 6));
    const bool decoded = ensurePreview(
        state,
        static_cast<int>(&map - state.maps.data())
    );
    RECT destination = rect;
    if (decoded) {
        const double scale = std::min(
            static_cast<double>(rect.right - rect.left) / map.preview.width,
            static_cast<double>(rect.bottom - rect.top) / map.preview.height
        );
        const int width = std::max(1, static_cast<int>(std::lround(map.preview.width * scale)));
        const int height = std::max(1, static_cast<int>(std::lround(map.preview.height * scale)));
        destination.left = rect.left + ((rect.right - rect.left) - width) / 2;
        destination.top = rect.top + ((rect.bottom - rect.top) - height) / 2;
        destination.right = destination.left + width;
        destination.bottom = destination.top + height;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = map.preview.width;
        info.bmiHeader.biHeight = -map.preview.height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        StretchDIBits(
            dc,
            destination.left,
            destination.top,
            destination.right - destination.left,
            destination.bottom - destination.top,
            0,
            0,
            map.preview.width,
            map.preview.height,
            map.preview.pixels.data(),
            &info,
            DIB_RGB_COLORS,
            SRCCOPY
        );
    } else {
        for (int y = rect.top; y < rect.bottom; y += 10) {
            RECT band{
                rect.left,
                y,
                rect.right,
                std::min<int>(static_cast<int>(rect.bottom), y + 10)
            };
            fillRectColor(
                dc,
                band,
                (y / 10) % 2 ? RGB(40, 50, 35) : RGB(30, 42, 31)
            );
        }
        drawTextIn(
            dc,
            L"Preview unavailable",
            destination,
            state.normalFont,
            kDimText,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );
    }
    frameRectColor(dc, destination, RGB(176, 151, 79));

    const int markerCount = std::min(map.maxPlayers, kPlayerSlots);
    for (int i = 0; i < markerCount; ++i) {
        double nx = 0.5 + std::cos(i * 6.283185307179586 / markerCount) * 0.34;
        double ny = 0.5 + std::sin(i * 6.283185307179586 / markerCount) * 0.34;
        if (i < static_cast<int>(map.startPoints.size())
            && map.headerWidth > 0 && map.headerHeight > 0) {
            const auto point = map.startPoints[static_cast<std::size_t>(i)];
            nx = static_cast<double>(point.x - map.headerX) / map.headerWidth;
            ny = static_cast<double>(point.y - map.headerY) / map.headerHeight;
            nx = std::clamp(nx, 0.04, 0.96);
            ny = std::clamp(ny, 0.06, 0.94);
        }
        const int x = destination.left
            + static_cast<int>(std::lround(nx * (destination.right - destination.left)));
        const int y = destination.top
            + static_cast<int>(std::lround(ny * (destination.bottom - destination.top)));
        RECT marker{x - 7, y - 7, x + 8, y + 8};
        HBRUSH brush = CreateSolidBrush(colorForId(
            i < static_cast<int>(state.selection.slots.size())
                ? state.selection.slots[static_cast<std::size_t>(i)].color
                : "Gold"
        ));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HGDIOBJ oldBrush = SelectObject(dc, brush);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        Ellipse(dc, marker.left, marker.top, marker.right, marker.bottom);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);
        drawTextIn(
            dc,
            std::to_wstring(i + 1),
            marker,
            state.smallFont,
            RGB(255, 255, 255),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );
    }
}

void drawPreviewPanel(MenuState& state, HDC dc) {
    const RECT panel = rectOf(398, 44, 615, 229);
    drawPanel(
        dc,
        panel,
        label(state, {"GUI:PreviewMap", "GUI:Map"}, L"MAP PREVIEW"),
        state.captionFont
    );
    if (state.selectedMap < 0
        || state.selectedMap >= static_cast<int>(state.maps.size())) {
        RECT message = rectOf(404, 68, 609, 218);
        drawTextIn(
            dc,
            L"No map selected",
            message,
            state.normalFont,
            kDimText,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE
        );
        return;
    }
    MapInfo& map = state.maps[static_cast<std::size_t>(state.selectedMap)];
    drawPreviewImage(state, dc, rectOf(404, 66, 609, 190), map);
    RECT nameRect = rectOf(404, 192, 609, 209);
    drawTextIn(
        dc,
        map.displayName,
        nameRect,
        state.normalFont,
        kPaleGold,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
    std::wstring info = std::to_wstring(map.minPlayers)
        + L"-" + std::to_wstring(map.maxPlayers) + L" "
        + label(state, {"GUI:Players"}, L"players");
    if (!map.theater.empty()) {
        const std::string theater = lowerAscii(map.theater);
        std::wstring theaterName = utf8ToWide(map.theater);
        if (theater == "temperate") {
            theaterName = state.csf.get("Name:Temperate", theaterName);
        } else if (theater == "snow") {
            theaterName = state.csf.get("Name:Snow", theaterName);
        } else if (theater == "urban" || theater == "newurban") {
            theaterName = state.csf.get("Name:Urban", theaterName);
        }
        info += L"  |  " + theaterName;
    }
    drawTextIn(
        dc,
        info,
        rectOf(404, 209, 609, 225),
        state.smallFont,
        RGB(157, 161, 145),
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
}

void drawCheckBox(HDC dc, const RECT& rect, bool checked, bool enabled, bool hot) {
    fillRectColor(dc, rect, hot ? RGB(57, 52, 31) : RGB(15, 21, 18));
    drawBevel(dc, rect, true);
    if (checked) {
        HPEN pen = CreatePen(
            PS_SOLID,
            2,
            enabled ? RGB(245, 216, 93) : RGB(112, 112, 96)
        );
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, rect.left + 3, rect.top + 7, nullptr);
        LineTo(dc, rect.left + 6, rect.bottom - 3);
        LineTo(dc, rect.right - 2, rect.top + 2);
        SelectObject(dc, old);
        DeleteObject(pen);
    }
}

void drawCellBackground(
    MenuState& state,
    HDC dc,
    const RECT& rect,
    const HitTarget& target,
    bool enabled
) {
    const bool hot = enabled && (state.hover == target || state.pressed == target);
    fillRectColor(
        dc,
        rect,
        hot ? RGB(61, 51, 27) : (enabled ? kPanelInner : RGB(11, 14, 13))
    );
    frameRectColor(dc, rect, hot ? kGold : RGB(45, 50, 42));
}

std::wstring slotName(
    const MenuState& state,
    const RuntimeSkirmishPlayerSlot& slot,
    int index
) {
    if (!slot.name.empty()) {
        return utf8ToWide(slot.name);
    }
    if (slot.human) {
        return label(state, {"GUI:You", "GUI:Player"}, L"Commander");
    }
    return label(state, {"GUI:Computer", "GUI:AIPlayer"}, L"Computer")
        + L" " + std::to_wstring(index);
}

void drawPlayerSlots(MenuState& state, HDC dc) {
    const RECT panel = rectOf(17, 233, 615, 410);
    drawPanel(
        dc,
        panel,
        label(state, {"GUI:Players", "GUI:PlayerList"}, L"PLAYERS"),
        state.captionFont
    );
    const int headerTop = 255;
    const int rowTop = 274;
    const int rowHeight = 16;
    struct Column {
        int left;
        int right;
        std::wstring name;
    };
    const std::array<Column, 6> columns{{
        {23, 42, L""},
        {44, 133, label(state, {"GUI:Player"}, L"PLAYER")},
        {135, 281, label(state, {"GUI:Country", "GUI:Side"}, L"COUNTRY")},
        {283, 376, label(state, {"GUI:Color"}, L"COLOR")},
        {378, 442, label(state, {"GUI:Team"}, L"TEAM")},
        {444, 608, label(state, {"GUI:Difficulty"}, L"DIFFICULTY")}
    }};
    for (const Column& column : columns) {
        drawTextIn(
            dc,
            column.name,
            rectOf(column.left, headerTop, column.right, rowTop - 1),
            state.smallFont,
            RGB(171, 163, 125),
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
    }

    const int mapLimit = selectedMapLimit(state);
    for (int index = 0; index < kPlayerSlots; ++index) {
        RuntimeSkirmishPlayerSlot& slot =
            state.selection.slots[static_cast<std::size_t>(index)];
        const bool available = index < mapLimit;
        const bool active = available && slot.enabled;
        const int top = rowTop + index * rowHeight;
        const RECT enabledRect = rectOf(25, top + 2, 37, top + 14);
        const HitTarget enabledTarget{HitKind::SlotEnabled, index};
        drawCheckBox(
            dc,
            enabledRect,
            active,
            available && !slot.human,
            state.hover == enabledTarget || state.pressed == enabledTarget
        );
        if (!slot.human) {
            addHit(state, rectOf(22, top, 42, top + rowHeight), HitKind::SlotEnabled, index);
        }

        const COLORREF rowText = active ? kText : kDimText;
        RECT nameRect = rectOf(44, top, 133, top + rowHeight - 1);
        fillRectColor(dc, nameRect, active ? RGB(20, 27, 23) : RGB(10, 13, 12));
        drawTextIn(
            dc,
            available ? slotName(state, slot, index) : L"---",
            nameRect,
            state.smallFont,
            rowText,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );

        const RECT countryRect = rectOf(135, top, 281, top + rowHeight - 1);
        const HitTarget countryTarget{HitKind::SlotCountry, index};
        drawCellBackground(state, dc, countryRect, countryTarget, active);
        if (active) {
            AssetImage* flag = flagFor(state, slot.country);
            int textLeft = countryRect.left + 4;
            if (flag != nullptr) {
                const RECT flagRect = rectOf(
                    countryRect.left + 3,
                    countryRect.top + 2,
                    countryRect.left + 31,
                    countryRect.bottom - 2
                );
                drawAsset(dc, flagRect, *flag);
                textLeft = flagRect.right + 3;
            }
            const int countryIndex = findCountryIndex(state, slot.country);
            const std::wstring display = countryIndex >= 0
                && countryIndex < static_cast<int>(state.countries.size())
                ? state.countries[static_cast<std::size_t>(countryIndex)].displayName
                : utf8ToWide(slot.country);
            drawTextIn(
                dc,
                display,
                rectOf(textLeft, countryRect.top, countryRect.right - 3, countryRect.bottom),
                state.smallFont,
                rowText,
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
            );
            addHit(state, countryRect, HitKind::SlotCountry, index);
        }

        const RECT colorRect = rectOf(283, top, 376, top + rowHeight - 1);
        const HitTarget colorTarget{HitKind::SlotColor, index};
        drawCellBackground(state, dc, colorRect, colorTarget, active);
        if (active) {
            RECT swatch = rectOf(
                colorRect.left + 4,
                colorRect.top + 3,
                colorRect.left + 14,
                colorRect.bottom - 3
            );
            fillRectColor(dc, swatch, colorForId(slot.color));
            frameRectColor(dc, swatch, RGB(0, 0, 0));
            drawTextIn(
                dc,
                colorDisplayName(state, slot.color),
                rectOf(swatch.right + 3, colorRect.top, colorRect.right - 2, colorRect.bottom),
                state.smallFont,
                rowText
            );
            addHit(state, colorRect, HitKind::SlotColor, index);
        }

        const RECT teamRect = rectOf(378, top, 442, top + rowHeight - 1);
        const HitTarget teamTarget{HitKind::SlotTeam, index};
        drawCellBackground(state, dc, teamRect, teamTarget, active);
        if (active) {
            drawTextIn(
                dc,
                utf8ToWide(slot.team),
                teamRect,
                state.smallFont,
                rowText,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE
            );
            addHit(state, teamRect, HitKind::SlotTeam, index);
        }

        const RECT difficultyRect = rectOf(444, top, 608, top + rowHeight - 1);
        const HitTarget difficultyTarget{HitKind::SlotDifficulty, index};
        drawCellBackground(
            state,
            dc,
            difficultyRect,
            difficultyTarget,
            active && !slot.human
        );
        drawTextIn(
            dc,
            slot.human
                ? label(state, {"STT:PlayerHuman", "GUI:Player"}, L"Human")
                : (active ? difficultyDisplayName(state, slot.difficulty) : L"---"),
            difficultyRect,
            state.smallFont,
            rowText,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
        );
        if (active && !slot.human) {
            addHit(state, difficultyRect, HitKind::SlotDifficulty, index);
        }
    }
}

void drawStepper(
    MenuState& state,
    HDC dc,
    int x,
    int y,
    int width,
    const std::wstring& caption,
    const std::wstring& value,
    HitKind minusKind,
    HitKind plusKind
) {
    const int labelWidth = width - 70;
    drawTextIn(
        dc,
        caption,
        rectOf(x, y, x + labelWidth, y + 22),
        state.normalFont,
        kText,
        DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
    RECT minus = rectOf(x + labelWidth + 4, y + 2, x + labelWidth + 22, y + 20);
    RECT valueRect = rectOf(minus.right + 1, y + 2, minus.right + 32, y + 20);
    RECT plus = rectOf(valueRect.right + 1, y + 2, valueRect.right + 19, y + 20);
    const HitTarget minusTarget{minusKind, -1};
    const HitTarget plusTarget{plusKind, -1};
    fillRectColor(
        dc,
        minus,
        state.hover == minusTarget ? RGB(71, 58, 27) : RGB(27, 33, 28)
    );
    fillRectColor(dc, valueRect, RGB(6, 10, 9));
    fillRectColor(
        dc,
        plus,
        state.hover == plusTarget ? RGB(71, 58, 27) : RGB(27, 33, 28)
    );
    drawBevel(dc, minus, state.pressed == minusTarget);
    drawBevel(dc, valueRect, true);
    drawBevel(dc, plus, state.pressed == plusTarget);
    drawTextIn(
        dc,
        L"-",
        minus,
        state.captionFont,
        kPaleGold,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );
    drawTextIn(
        dc,
        value,
        valueRect,
        state.smallFont,
        kPaleGold,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
    drawTextIn(
        dc,
        L"+",
        plus,
        state.captionFont,
        kPaleGold,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE
    );
    addHit(state, minus, minusKind);
    addHit(state, plus, plusKind);
}

void drawOptionCheck(
    MenuState& state,
    HDC dc,
    int x,
    int y,
    int width,
    const std::wstring& caption,
    bool checked,
    HitKind kind
) {
    const RECT area = rectOf(x, y, x + width, y + 20);
    const RECT box = rectOf(x + 1, y + 3, x + 15, y + 17);
    const HitTarget target{kind, -1};
    drawCheckBox(
        dc,
        box,
        checked,
        true,
        state.hover == target || state.pressed == target
    );
    drawTextIn(
        dc,
        caption,
        rectOf(box.right + 4, y, area.right, y + 20),
        state.smallFont,
        state.hover == target ? kPaleGold : kText
    );
    addHit(state, area, kind);
}

void drawOptions(MenuState& state, HDC dc) {
    const RECT panel = rectOf(17, 414, 615, 523);
    drawPanel(
        dc,
        panel,
        label(state, {"GUI:GameOptions", "GUI:Options"}, L"GAME OPTIONS"),
        state.captionFont
    );
    drawStepper(
        state, dc, 23, 438, 183,
        label(state, {"GUI:Credits", "GUI:StartingCredits"}, L"Credits"),
        std::to_wstring(state.selection.startingCredits),
        HitKind::CreditsMinus, HitKind::CreditsPlus
    );
    drawStepper(
        state, dc, 214, 438, 174,
        label(state, {"GUI:UnitCount", "GUI:StartingUnits"}, L"Units"),
        std::to_wstring(state.selection.unitCount),
        HitKind::UnitsMinus, HitKind::UnitsPlus
    );
    drawStepper(
        state, dc, 396, 438, 207,
        label(state, {"GUI:GameSpeed"}, L"Speed"),
        std::to_wstring(state.selection.gameSpeed),
        HitKind::SpeedMinus, HitKind::SpeedPlus
    );
    drawOptionCheck(
        state, dc, 25, 469, 120,
        label(state, {"GUI:ShortGame"}, L"Short Game"),
        state.selection.shortGame,
        HitKind::ToggleShortGame
    );
    drawOptionCheck(
        state, dc, 145, 469, 135,
        label(state, {"GUI:MCVRepacks"}, L"MCV Repacks"),
        state.selection.mcvRepacks,
        HitKind::ToggleMcvRepacks
    );
    drawOptionCheck(
        state, dc, 280, 469, 100,
        L"奖励箱子",
        state.selection.crates,
        HitKind::ToggleCrates
    );
    drawOptionCheck(
        state, dc, 380, 469, 155,
        label(
            state,
            {"GUI:SuperWeaponsAllowed", "GUI:SuperWeapons"},
            L"Superweapons"
        ),
        state.selection.superweapons,
        HitKind::ToggleSuperweapons
    );
    drawOptionCheck(
        state, dc, 535, 469, 72,
        L"AI",
        state.selection.computerAiEnabled,
        HitKind::ToggleComputerAi
    );
    if (!state.config.multiplayerActive) drawOptionCheck(
        state, dc, 502, 494, 105, L"无冷却",
        state.selection.noCooldown, HitKind::ToggleNoCooldown);
    drawTextIn(
        dc,
        state.status,
        rectOf(25, 494, 495, 518),
        state.smallFont,
        state.status == L"Ready" ? RGB(145, 171, 123) : RGB(199, 174, 98),
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
}

void drawMainButton(
    MenuState& state,
    HDC dc,
    const RECT& rect,
    const std::wstring& text,
    HitKind kind
) {
    const HitTarget target{kind, -1};
    const bool pressed = state.pressed == target;
    const bool hover = state.hover == target;
    const int frame = pressed ? 2 : (hover ? 1 : 0);
    if (state.buttons[static_cast<std::size_t>(frame)].valid()) {
        drawAsset(dc, rect, state.buttons[static_cast<std::size_t>(frame)]);
    } else {
        fillRectColor(
            dc,
            rect,
            pressed ? RGB(50, 40, 21) : (hover ? RGB(87, 69, 31) : RGB(45, 47, 36))
        );
        drawBevel(dc, rect, pressed);
    }
    RECT textRect = rect;
    if (pressed) OffsetRect(&textRect, 1, 1);
    drawShadowText(
        dc,
        text,
        textRect,
        state.captionFont,
        hover ? RGB(255, 238, 155) : kPaleGold,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
    addHit(state, rect, kind);
}

void drawBase(MenuState& state, HDC dc) {
    state.hits.clear();
    SetStretchBltMode(dc, HALFTONE);
    if (state.background.valid()) {
        drawAsset(dc, rectOf(0, 0, kBaseWidth, kBaseHeight), state.background);
    } else {
        drawFallbackBackground(dc);
    }
    fillRectColor(dc, rectOf(9, 6, kBaseWidth - 9, 41), RGB(7, 10, 9));
    drawLine(dc, 11, 40, kBaseWidth - 11, 40, RGB(135, 113, 62));
    drawShadowText(
        dc,
        label(state, {"GUI:SkirmishGame", "GUI:Skirmish"}, L"SKIRMISH GAME"),
        rectOf(15, 7, kBaseWidth - 15, 39),
        state.titleFont,
        kPaleGold,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS
    );
    drawModeList(state, dc);
    drawMapList(state, dc);
    drawPreviewPanel(state, dc);
    drawPlayerSlots(state, dc);
    drawOptions(state, dc);
    drawMainButton(
        state,
        dc,
        rectOf(350, 535, 476, 560),
        label(state, {"GUI:StartGame", "GUI:Play"}, L"START GAME"),
        HitKind::Start
    );
    drawMainButton(
        state,
        dc,
        rectOf(484, 535, 610, 560),
        label(state, {"GUI:Back", "GUI:Cancel"}, L"BACK"),
        HitKind::Back
    );
}

void computeViewport(MenuState& state, int clientWidth, int clientHeight) {
    clientWidth = std::max(1, clientWidth);
    clientHeight = std::max(1, clientHeight);
    const double scale = std::min(
        static_cast<double>(clientWidth) / kBaseWidth,
        static_cast<double>(clientHeight) / kBaseHeight
    );
    state.viewWidth = std::max(1, static_cast<int>(std::lround(kBaseWidth * scale)));
    state.viewHeight = std::max(1, static_cast<int>(std::lround(kBaseHeight * scale)));
    state.viewLeft = (clientWidth - state.viewWidth) / 2;
    state.viewTop = (clientHeight - state.viewHeight) / 2;
}

bool renderClient(MenuState& state, HDC targetDc, int width, int height) {
    if (targetDc == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    HDC clientDc = CreateCompatibleDC(targetDc);
    HBITMAP clientBitmap = CreateCompatibleBitmap(targetDc, width, height);
    if (clientDc == nullptr || clientBitmap == nullptr) {
        if (clientBitmap != nullptr) DeleteObject(clientBitmap);
        if (clientDc != nullptr) DeleteDC(clientDc);
        return false;
    }
    HGDIOBJ oldClientBitmap = SelectObject(clientDc, clientBitmap);
    fillRectColor(clientDc, rectOf(0, 0, width, height), RGB(0, 0, 0));

    HDC baseDc = CreateCompatibleDC(targetDc);
    BITMAPINFO baseInfo{};
    baseInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    baseInfo.bmiHeader.biWidth = kBaseWidth;
    baseInfo.bmiHeader.biHeight = -kBaseHeight;
    baseInfo.bmiHeader.biPlanes = 1;
    baseInfo.bmiHeader.biBitCount = 32;
    baseInfo.bmiHeader.biCompression = BI_RGB;
    void* basePixels = nullptr;
    HBITMAP baseBitmap = CreateDIBSection(
        targetDc,
        &baseInfo,
        DIB_RGB_COLORS,
        &basePixels,
        nullptr,
        0
    );
    if (baseDc == nullptr || baseBitmap == nullptr || basePixels == nullptr) {
        if (baseBitmap != nullptr) DeleteObject(baseBitmap);
        if (baseDc != nullptr) DeleteDC(baseDc);
        SelectObject(clientDc, oldClientBitmap);
        DeleteObject(clientBitmap);
        DeleteDC(clientDc);
        return false;
    }
    HGDIOBJ oldBaseBitmap = SelectObject(baseDc, baseBitmap);
    drawBase(state, baseDc);

    computeViewport(state, width, height);
    SetStretchBltMode(clientDc, HALFTONE);
    SetBrushOrgEx(clientDc, 0, 0, nullptr);
    StretchBlt(
        clientDc,
        state.viewLeft,
        state.viewTop,
        state.viewWidth,
        state.viewHeight,
        baseDc,
        0,
        0,
        kBaseWidth,
        kBaseHeight,
        SRCCOPY
    );
    BitBlt(targetDc, 0, 0, width, height, clientDc, 0, 0, SRCCOPY);

    SelectObject(baseDc, oldBaseBitmap);
    DeleteObject(baseBitmap);
    DeleteDC(baseDc);
    SelectObject(clientDc, oldClientBitmap);
    DeleteObject(clientBitmap);
    DeleteDC(clientDc);
    return true;
}

POINT clientToBase(const MenuState& state, POINT clientPoint) {
    if (state.viewWidth <= 0 || state.viewHeight <= 0) {
        return POINT{-1, -1};
    }
    if (clientPoint.x < state.viewLeft
        || clientPoint.y < state.viewTop
        || clientPoint.x >= state.viewLeft + state.viewWidth
        || clientPoint.y >= state.viewTop + state.viewHeight) {
        return POINT{-1, -1};
    }
    return POINT{
        (clientPoint.x - state.viewLeft) * kBaseWidth / state.viewWidth,
        (clientPoint.y - state.viewTop) * kBaseHeight / state.viewHeight
    };
}

void invalidate(MenuState& state) {
    state.backDirty = true;
    if (state.hwnd != nullptr) {
        InvalidateRect(state.hwnd, nullptr, FALSE);
    }
}











std::wstring teamDisplay(const MenuState& state, const std::string& team);













std::wstring dropdownOptionText(
    const MenuState& state,
    DropdownKind kind,
    int option
) {
    switch (kind) {
    case DropdownKind::Player:
        return option <= 0
            ? label(state, {"GUI:None"}, L"Closed")
            : difficultyDisplayName(
                state,
                state.difficulties[static_cast<std::size_t>(option - 1)]
            );
    case DropdownKind::Country:
        if (const int countryIndex = countryIndexForDropdownOption(
                state,
                option
            ); countryIndex >= 0
                && countryIndex < static_cast<int>(state.countries.size())) {
            return state.countries[
                static_cast<std::size_t>(countryIndex)
            ].displayName;
        }
        return L"";
    case DropdownKind::Color:
        return colorDisplayName(
            state,
            state.colorIds[static_cast<std::size_t>(option)]
        );
    case DropdownKind::Start:
        return option == 0
            ? label(state, {"GUI:Random"}, L"Random")
            : std::to_wstring(option);
    case DropdownKind::Team:
        return teamDisplay(state, state.teams[static_cast<std::size_t>(option)]);
    default:
        return L"";
    }
}



























LRESULT CALLBACK skirmishWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    MenuState* state = reinterpret_cast<MenuState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<MenuState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        if (state != nullptr) {
            state->hwnd = window;
        }
    }
    if (state == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    switch (message) {
    case WM_CREATE:
        SetTimer(window, 2, 500, nullptr);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* limits = reinterpret_cast<MINMAXINFO*>(lParam);
        limits->ptMinTrackSize.x = 520;
        limits->ptMinTrackSize.y = 490;
        return 0;
    }
    case WM_SIZE: {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        computeViewport(*state, width, height);
        invalidate(*state);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        renderClient(*state, dc, client.right, client.bottom);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_TIMER:
        if (wParam == 2) {
            state->audio.ensureMusicPlaying();
        }
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{};
        tracking.cbSize = sizeof(tracking);
        tracking.dwFlags = TME_LEAVE;
        tracking.hwndTrack = window;
        TrackMouseEvent(&tracking);
        POINT clientPoint{
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam))
        };
        const POINT basePoint = clientToBase(*state, clientPoint);
        const HitRegion* hit = hitTest(*state, basePoint);
        const HitTarget next = hit == nullptr ? HitTarget{} : hit->target;
        if (next != state->hover) {
            state->hover = next;
            invalidate(*state);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (state->hover.kind != HitKind::None) {
            state->hover = HitTarget{};
            invalidate(*state);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        SetFocus(window);
        POINT clientPoint{
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam))
        };
        const HitRegion* hit = hitTest(*state, clientToBase(*state, clientPoint));
        state->pressed = hit == nullptr ? HitTarget{} : hit->target;
        if (state->pressed.kind != HitKind::None) {
            SetCapture(window);
        }
        invalidate(*state);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT clientPoint{
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam))
        };
        const HitRegion* hit = hitTest(*state, clientToBase(*state, clientPoint));
        const HitTarget released = hit == nullptr ? HitTarget{} : hit->target;
        const HitTarget pressed = state->pressed;
        state->pressed = HitTarget{};
        if (GetCapture() == window) ReleaseCapture();
        if (released == pressed && pressed.kind != HitKind::None) {
            activateTarget(*state, pressed, 1);
        } else {
            invalidate(*state);
        }
        return 0;
    }
    case WM_RBUTTONUP: {
        POINT clientPoint{
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam))
        };
        const HitRegion* hit = hitTest(*state, clientToBase(*state, clientPoint));
        if (hit != nullptr) {
            activateTarget(*state, hit->target, -1);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (state->pressed.kind != HitKind::None) {
            state->pressed = HitTarget{};
            invalidate(*state);
        }
        return 0;
    case WM_MOUSEWHEEL: {
        POINT point{
            static_cast<short>(LOWORD(lParam)),
            static_cast<short>(HIWORD(lParam))
        };
        ScreenToClient(window, &point);
        const POINT base = clientToBase(*state, point);
        if (pointIn(rectOf(155, 44, 394, 229), base)) {
            const int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            scrollMapList(*state, -notches * 3, false);
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            activateTarget(*state, HitTarget{HitKind::Back, -1}, 1);
            return 0;
        }
        if (wParam == VK_RETURN) {
            activateTarget(*state, HitTarget{HitKind::Start, -1}, 1);
            return 0;
        }
        if ((wParam == VK_UP || wParam == VK_DOWN)
            && !state->filteredMaps.empty()) {
            const auto current = std::find(
                state->filteredMaps.begin(),
                state->filteredMaps.end(),
                state->selectedMap
            );
            int position = current == state->filteredMaps.end()
                ? 0
                : static_cast<int>(current - state->filteredMaps.begin());
            position += wParam == VK_UP ? -1 : 1;
            position = std::clamp(
                position,
                0,
                static_cast<int>(state->filteredMaps.size()) - 1
            );
            selectFilteredMap(*state, position, true);
            return 0;
        }
        if (wParam == VK_PRIOR || wParam == VK_NEXT) {
            scrollMapList(*state, wParam == VK_PRIOR ? -kVisibleMapRows : kVisibleMapRows, true);
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(window, &point);
            const HitRegion* hit = hitTest(*state, clientToBase(*state, point));
            if (hit != nullptr) {
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
                return TRUE;
            }
        }
        break;
    case WM_CLOSE:
        completeMenu(*state, false);
        return 0;
    case WM_NCDESTROY:
        KillTimer(window, 2);
        state->audio.stopMusic();
        state->hwnd = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerSkirmishWindowClass(HINSTANCE instance) {
    constexpr const wchar_t* className = L"ConcordRTS.RuntimeSkirmishMenu";
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = skirmishWindowProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = className;
    if (RegisterClassExW(&windowClass) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool writeBgraBmp(
    const std::string& outputPath,
    const void* pixels,
    int width,
    int height,
    std::string& errorMessage
) {
    errorMessage.clear();
    if (outputPath.empty() || pixels == nullptr || width <= 0 || height <= 0) {
        errorMessage = "invalid BMP output arguments";
        return false;
    }
    const std::uint64_t pixelBytes =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4ULL;
    if (pixelBytes > std::numeric_limits<DWORD>::max()) {
        errorMessage = "BMP dimensions overflow the 32-bit file format";
        return false;
    }
    std::ofstream output(outputPath, std::ios::binary);
    if (!output) {
        errorMessage = "failed to open BMP output path: " + outputPath;
        return false;
    }
    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};
    fileHeader.bfType = 0x4d42;
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + static_cast<DWORD>(pixelBytes);
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = width;
    infoHeader.biHeight = -height;
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = static_cast<DWORD>(pixelBytes);
    output.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
    output.write(reinterpret_cast<const char*>(&infoHeader), sizeof(infoHeader));
    output.write(static_cast<const char*>(pixels), static_cast<std::streamsize>(pixelBytes));
    if (!output) {
        errorMessage = "failed while writing BMP data: " + outputPath;
        return false;
    }
    return true;
}

bool hasHitKind(const MenuState& state, HitKind kind) {
    return std::any_of(
        state.hits.begin(),
        state.hits.end(),
        [kind](const HitRegion& hit) { return hit.target.kind == kind; }
    );
}

using ShellCanvas = concord::ui::MenuPixelSurfaceView;
RuntimeInputPixelRect pixelRect(const RECT& r) { return {r.left,r.top,r.right,r.bottom}; }
RuntimeRgbColorValue pixelColor(std::uint32_t c) { return RuntimeRgbColorValue::fromPackedRgb(c); }
void shellClear(ShellCanvas& canvas) { concord::ui::clearMenuPixels(canvas); }
void shellFill(ShellCanvas& canvas,const RECT& r,COLORREF c) {
    concord::ui::fillMenuPixels(canvas,pixelRect(r),pixelColor(c));
}
void shellDarken(ShellCanvas& canvas,const RECT& r,unsigned int opacity) {
    concord::ui::darkenMenuPixels(canvas,pixelRect(r),opacity);
}
void shellFrame(ShellCanvas& canvas,const RECT& r,COLORREF c) {
    concord::ui::frameMenuPixels(canvas,pixelRect(r),pixelColor(c));
}
void shellBlit(ShellCanvas& canvas,const AssetImage& image,int left,int top,
    int clipHeight=std::numeric_limits<int>::max()) {
    if(!image.valid()) return;
    concord::ui::blitMenuPixels(canvas,
        {image.bgra,image.width,image.height,static_cast<std::size_t>(image.width)*4},left,top,clipHeight);
}
void shellBlitScaled(ShellCanvas& canvas,const unsigned char* sourcePixels,int width,int height,const RECT& r) {
    if(!sourcePixels||width<=0||height<=0) return;
    concord::ui::scaleMenuPixels(canvas,
        {{sourcePixels,static_cast<std::size_t>(width)*static_cast<std::size_t>(height)*4},
            width,height,static_cast<std::size_t>(width)*4},pixelRect(r));
}

void shellText(
    MenuState& state,
    ShellCanvas& canvas,
    const std::wstring& text,
    const RECT& rect,
    bool centered = false,
    COLORREF color = RGB(255, 232, 0)
) {
    state.gameFont.draw(canvas.bytes.data(), text, rect, centered, true, color, state.bitmapTextTrace);
}

void shellSelectBox(
    MenuState& state,
    ShellCanvas& canvas,
    const RECT& rect,
    HitTarget target,
    bool enabled = true,
    bool dropdown = false
) {
    const bool expanded = dropdown
        && dropdownKindForTarget(target.kind) == state.openDropdown
        && state.openDropdownSlot == target.index;
    const bool hot = state.hover == target || state.pressed == target || expanded;
    shellFill(canvas, rect, enabled ? RGB(7, 5, 5) : RGB(19, 19, 19));
    shellFrame(
        canvas,
        rect,
        hot ? RGB(255, 178, 28) : (enabled ? RGB(117, 17, 11) : RGB(55, 55, 55))
    );
    if (dropdown && enabled) {
        const AssetImage& arrow = state.pressed == target || expanded
            ? state.dropArrowPressed
            : state.dropArrow;
        if (arrow.valid()) {
            const int arrowX = static_cast<int>(rect.right) - arrow.width;
            const int arrowY = static_cast<int>(rect.top)
                + std::max<int>(
                    0,
                    (static_cast<int>(rect.bottom - rect.top) - arrow.height) / 2
                );
            shellBlit(
                canvas,
                arrow,
                arrowX,
                arrowY
            );
        }
    }
}

int shellButtonFrame(const MenuState& state, HitKind kind) {
    const HitTarget target{kind, -1};
    if (state.pressed == target) return 2;
    if (state.hover == target) return 1;
    return 0;
}

void drawShellButton(
    MenuState& state,
    ShellCanvas& canvas,
    HitKind kind,
    int y,
    const std::wstring& text,
    bool bottom = false
) {
    if (state.phase != ShellMenuPhase::Idle) {
        return;
    }
    const int frame = shellButtonFrame(state, kind);
    const int buttonY = bottom ? y : y + 8;
    if (frame >= 0 && frame < static_cast<int>(state.buttons.size())) {
        shellBlit(
            canvas,
            state.buttons[static_cast<std::size_t>(frame)],
            kShellButtonLeft,
            buttonY
        );
    }
    shellText(
        state,
        canvas,
        text,
        RECT{kShellButtonLeft, buttonY, kShellButtonLeft + 126, buttonY + 25},
        true
    );
    addHit(
        state,
        bottom
            ? RECT{644, kShellBottomTop, 790, kBaseHeight}
            : RECT{644, y, 790, y + kShellSlotHeight},
        kind
    );
}

void drawShellMapPreview(
    MenuState& state,
    ShellCanvas& canvas,
    const RECT& rect
) {
    shellFill(canvas, rect, RGB(2, 2, 2));
    if (state.selectedMap < 0
        || state.selectedMap >= static_cast<int>(state.maps.size())) {
        shellFrame(canvas, rect, RGB(117, 17, 11));
        return;
    }
    MapInfo& map = state.maps[static_cast<std::size_t>(state.selectedMap)];
    if (ensurePreview(state, state.selectedMap)) {
        const double scale = std::min(
            static_cast<double>(rect.right - rect.left) / map.preview.width,
            static_cast<double>(rect.bottom - rect.top) / map.preview.height
        );
        const int width = std::max(1, static_cast<int>(std::lround(
            map.preview.width * scale
        )));
        const int height = std::max(1, static_cast<int>(std::lround(
            map.preview.height * scale
        )));
        RECT destination{
            rect.left + ((rect.right - rect.left) - width) / 2,
            rect.top + ((rect.bottom - rect.top) - height) / 2,
            0,
            0
        };
        destination.right = destination.left + width;
        destination.bottom = destination.top + height;
        shellBlitScaled(
            canvas,
            map.preview.pixels.data(),
            map.preview.width,
            map.preview.height,
            destination
        );
        const int markers = std::min(map.maxPlayers, kPlayerSlots);
        for (int i = 0; i < markers; ++i) {
            double nx = 0.5 + std::cos(
                static_cast<double>(i) * 6.283185307179586 / std::max(1, markers)
            ) * 0.34;
            double ny = 0.5 + std::sin(
                static_cast<double>(i) * 6.283185307179586 / std::max(1, markers)
            ) * 0.34;
            if (i < static_cast<int>(map.startPoints.size())
                && map.headerWidth > 0 && map.headerHeight > 0) {
                const auto point = map.startPoints[static_cast<std::size_t>(i)];
                nx = std::clamp(
                    static_cast<double>(point.x - map.headerX) / map.headerWidth,
                    0.03,
                    0.97
                );
                ny = std::clamp(
                    static_cast<double>(point.y - map.headerY) / map.headerHeight,
                    0.05,
                    0.95
                );
            }
            const int x = destination.left + static_cast<int>(std::lround(
                nx * (destination.right - destination.left)
            ));
            const int y = destination.top + static_cast<int>(std::lround(
                ny * (destination.bottom - destination.top)
            ));
            shellText(
                state,
                canvas,
                std::to_wstring(i + 1),
                RECT{x - 7, y - 9, x + 8, y + 9},
                true,
                RGB(255, 232, 0)
            );
        }
    }
    shellFrame(canvas, rect, RGB(117, 17, 11));
}

int shellPhaseFrameForSlot(
    const MenuState& state,
    int slot,
    bool occupied
) {
    if (state.phase == ShellMenuPhase::Idle) {
        return occupied ? 2 : 0;
    }
    const int tick = state.phaseTick - slot;
    if (state.phase == ShellMenuPhase::SlideIn) {
        if (tick < 0) return occupied ? 10 : 16;
        if (tick >= kShellAnimationFrames) return occupied ? 1 : 0;
        return (occupied ? 10 : 16) - tick;
    }
    if (tick < 0) return occupied ? 5 : 11;
    if (tick >= kShellAnimationFrames) return occupied ? 10 : 16;
    return (occupied ? 5 : 11) + tick;
}

void drawShellChrome(MenuState& state, ShellCanvas& canvas) {
    shellClear(canvas);
    shellBlit(canvas, state.background, 0, 0);
    shellBlit(canvas, state.statusShell, 0, kShellStatusTop);
    shellBlit(canvas, state.sideTopShell[1], kShellSideLeft, 0);
    for (int slot = 0; slot < kShellSlotCount; ++slot) {
        shellBlit(
            canvas,
            state.slotBackgroundShell,
            kShellSideLeft,
            kShellTopHeight + slot * kShellSlotHeight
        );
    }
    shellBlit(
        canvas,
        state.sideBottomShell,
        kShellSideLeft,
        kShellBottomTop,
        kBaseHeight - kShellBottomTop
    );

    const std::array<int, 2> occupied = state.page == SkirmishPage::Lobby
        ? std::array<int, 2>{1, 2}
        : std::array<int, 2>{1, -1};
    for (int slot = 0; slot < kShellSlotCount; ++slot) {
        const bool used = slot == occupied[0] || slot == occupied[1];
        const int frame = shellPhaseFrameForSlot(state, slot, used);
        if (frame < static_cast<int>(state.slotAnimationShell.size())) {
            shellBlit(
                canvas,
                state.slotAnimationShell[static_cast<std::size_t>(frame)],
                644,
                kShellTopHeight + slot * kShellSlotHeight
            );
        }
    }
    shellText(
        state,
        canvas,
        state.page == SkirmishPage::Lobby
            ? label(state, {"GUI:SkirmishGame"}, L"Skirmish Game")
            : label(state, {"GUI:ChooseMap"}, L"Choose Map"),
        RECT{657, 3, 775, 21},
        true
    );
    drawShellMapPreview(state, canvas, RECT{644, 40, 790, 152});
}

std::wstring teamDisplay(const MenuState& state, const std::string& team) {
    if (team.empty() || team == "-" || team == "0") {
        return label(state, {"GUI:None"}, L"None");
    }
    const int value = parseIntOr(team, 0);
    if (value >= 1 && value <= 4) {
        return std::wstring(1, static_cast<wchar_t>(L'A' + value - 1));
    }
    return utf8ToWide(team);
}

void drawLobbyPlayers(MenuState& state, ShellCanvas& canvas) {
    constexpr int headerY = 40;
    constexpr int rowTop = 61;
    constexpr int rowHeight = 27;
    const std::array<RECT, 5> headers{{
        RECT{59, headerY, 209, headerY + 20},
        RECT{266, headerY, 386, headerY + 20},
        RECT{393, headerY, 448, headerY + 20},
        RECT{455, headerY, 510, headerY + 20},
        RECT{517, headerY, 572, headerY + 20}
    }};
    const std::array<std::wstring, 5> labels{{
        label(state, {"GUI:Player"}, L"Player"),
        label(state, {"GUI:Country"}, L"Country"),
        label(state, {"GUI:Color"}, L"Color"),
        label(state, {"GUI:StartPosition"}, L"Start Position"),
        label(state, {"GUI:Team"}, L"Team")
    }};
    for (std::size_t i = 0; i < headers.size(); ++i) {
        shellText(state, canvas, labels[i], headers[i], true, RGB(255, 222, 25));
    }

    const int limit = selectedMapLimit(state);
    for (int index = 0; index < kPlayerSlots; ++index) {
        RuntimeSkirmishPlayerSlot& slot =
            state.selection.slots[static_cast<std::size_t>(index)];
        const int y = rowTop + index * rowHeight;
        const bool available = index < limit;
        const bool enabled = available && slot.enabled;
        const bool combatant = enabled && !runtimeObserverSlot(slot);
        const HitTarget playerTarget{HitKind::SlotEnabled, index};
        shellSelectBox(
            state, canvas, RECT{59, y, 209, y + 26},
            playerTarget, available, index > 0 && !slot.human
        );
        std::wstring playerText;
        if (index == 0) {
            playerText = utf8ToWide(slot.name.empty() ? "Commander" : slot.name);
        } else if (!enabled) {
            playerText = label(state, {"GUI:None"}, L"Closed");
        } else {
            playerText = difficultyDisplayName(state, slot.difficulty);
        }
        shellText(
            state, canvas, playerText, RECT{64, y, 188, y + 26}, false,
            enabled ? RGB(255, 232, 0) : RGB(112, 112, 112)
        );
        if (index > 0 && available) addHit(state, RECT{59, y, 209, y + 26}, HitKind::SlotEnabled, index);

        shellSelectBox(
            state, canvas, RECT{212, y + 1, 259, y + 24},
            HitTarget{HitKind::SlotCountry, index}, enabled
        );
        if (enabled) {
            if (AssetImage* flag = flagFor(state, slot.country)) {
                const int flagX = 212 + std::max(0, (47 - flag->width) / 2);
                const int flagY = y + 1 + std::max(0, (23 - flag->height) / 2);
                shellBlit(canvas, *flag, flagX, flagY, 23);
            }
        }

        const int countryIndex = findCountryIndex(state, slot.country);
        shellSelectBox(state, canvas, RECT{266, y, 386, y + 26},
            HitTarget{HitKind::SlotCountry, index}, enabled, true);
        shellText(
            state, canvas,
            countryIndex >= 0 && countryIndex < static_cast<int>(state.countries.size())
                ? state.countries[static_cast<std::size_t>(countryIndex)].displayName
                : utf8ToWide(slot.country),
            RECT{271, y, 366, y + 26}, false,
            enabled ? RGB(255, 232, 0) : RGB(105, 105, 105)
        );
        shellSelectBox(state, canvas, RECT{393, y, 448, y + 26},
            HitTarget{HitKind::SlotColor, index}, enabled, true);
        if (enabled) shellFill(canvas, RECT{399, y + 6, 429, y + 20}, colorForId(slot.color));

        shellSelectBox(state, canvas, RECT{455, y, 510, y + 26},
            HitTarget{HitKind::SlotStart, index}, combatant, combatant);
        const int start = state.startingSpots[static_cast<std::size_t>(index)];
        shellText(state, canvas,
            runtimeObserverSlot(slot) ? L"-"
                : start == 0 ? label(state, {"GUI:Random"}, L"Random") : std::to_wstring(start),
            RECT{458, y, 490, y + 26}, true,
            combatant ? RGB(255, 232, 0) : RGB(105, 105, 105));

        shellSelectBox(state, canvas, RECT{517, y, 572, y + 26},
            HitTarget{HitKind::SlotTeam, index}, combatant, combatant);
        shellText(state, canvas, teamDisplay(state, slot.team),
            RECT{520, y, 552, y + 26}, true,
            combatant ? RGB(255, 232, 0) : RGB(105, 105, 105));

        if (enabled) {
            addHit(state, RECT{266, y, 386, y + 26}, HitKind::SlotCountry, index);
            addHit(state, RECT{393, y, 448, y + 26}, HitKind::SlotColor, index);
        }
        if (combatant) {
            addHit(state, RECT{455, y, 510, y + 26}, HitKind::SlotStart, index);
            addHit(state, RECT{517, y, 572, y + 26}, HitKind::SlotTeam, index);
        }
    }
}

RECT dropdownSourceRect(DropdownKind kind, int slotIndex) {
    constexpr int rowTop = 61;
    constexpr int playerRowHeight = 27;
    const int top = rowTop + slotIndex * playerRowHeight;
    switch (kind) {
    case DropdownKind::Player: return RECT{59, top, 209, top + 26};
    case DropdownKind::Country: return RECT{266, top, 386, top + 26};
    case DropdownKind::Color: return RECT{393, top, 448, top + 26};
    case DropdownKind::Start: return RECT{455, top, 510, top + 26};
    case DropdownKind::Team: return RECT{517, top, 572, top + 26};
    default: return RECT{0, 0, 0, 0};
    }
}

void drawOpenDropdown(MenuState& state, ShellCanvas& canvas) {
    if (!dropdownCanOpen(
            state,
            state.openDropdown,
            state.openDropdownSlot
        )) {
        return;
    }

    constexpr int selectHeight = 26;
    constexpr int optionHeight = 24;
    const int optionCount = dropdownOptionCount(state, state.openDropdown);
    if (optionCount <= 0) return;

    const RECT selectRect = dropdownSourceRect(
        state.openDropdown,
        state.openDropdownSlot
    );
    const int layerHeight = optionCount * optionHeight;
    int layerTop = selectRect.top + selectHeight;
    if (layerTop + layerHeight > kShellStatusTop) {
        layerTop = std::max(1, static_cast<int>(selectRect.top) - layerHeight);
    }
    int layerWidth = static_cast<int>(selectRect.right - selectRect.left);
    if (state.openDropdown == DropdownKind::Color) layerWidth = 120;
    if (state.openDropdown == DropdownKind::Start) layerWidth = 90;
    if (state.openDropdown == DropdownKind::Team) layerWidth = 100;
    const RECT layer{
        selectRect.left,
        layerTop,
        std::min(kShellLeftWidth - 2, static_cast<int>(selectRect.left) + layerWidth),
        std::min(kShellStatusTop, layerTop + layerHeight)
    };
    shellDarken(canvas, layer, 192);

    const int current = currentDropdownOption(state);
    const int highlighted = state.dropdownHover >= 0
        && state.dropdownHover < optionCount
        ? state.dropdownHover
        : current;
    for (int optionIndex = 0; optionIndex < optionCount; ++optionIndex) {
        const int optionTop = layerTop + optionIndex * optionHeight;
        const RECT option{
            layer.left + 1,
            optionTop,
            layer.right - 1,
            std::min(optionTop + optionHeight, kShellStatusTop)
        };
        if (option.top >= option.bottom) break;
        if (optionIndex == highlighted) {
            shellFill(canvas, option, RGB(104, 0, 0));
        }
        int textLeft = option.left + 3;
        if (state.openDropdown == DropdownKind::Color) {
            shellFill(
                canvas,
                RECT{option.left + 4, option.top + 5,
                    option.left + 24, option.bottom - 5},
                colorForId(
                    state.colorIds[static_cast<std::size_t>(optionIndex)]
                )
            );
            textLeft = option.left + 28;
        } else if (state.openDropdown == DropdownKind::Country) {
            const int countryIndex = countryIndexForDropdownOption(
                state,
                optionIndex
            );
            AssetImage* flag = countryIndex >= 0
                    && countryIndex < static_cast<int>(state.countries.size())
                ? flagFor(
                    state,
                    state.countries[
                        static_cast<std::size_t>(countryIndex)
                    ].id
                  )
                : nullptr;
            if (flag != nullptr) {
                const int flagLeft = option.left + 3;
                shellBlit(canvas, *flag, flagLeft, option.top + 2, 20);
                // Country SHP/PCX flags do not all share the same native
                // width.  Using a fixed text offset let wider flags paint
                // underneath the country name.  Reserve the actual rendered
                // flag width plus a small gap so the label always starts
                // after the complete flag.
                textLeft = flagLeft + flag->width + 4;
            }
        }
        shellText(
            state,
            canvas,
            dropdownOptionText(state, state.openDropdown, optionIndex),
            RECT{textLeft, option.top, option.right - 3, option.bottom},
            false,
            optionIndex == highlighted
                ? RGB(255, 232, 0)
                : RGB(206, 206, 187)
        );
        addHit(state, option, HitKind::DropdownOption, optionIndex);
    }
    shellFrame(canvas, layer, RGB(185, 0, 0));
}

void drawLobbyStepper(
    MenuState& state,
    ShellCanvas& canvas,
    int y,
    const std::wstring& labelText,
    const std::wstring& value,
    int position,
    int maximum,
    HitKind sliderKind
) {
    shellText(state, canvas, labelText, RECT{273, y, 369, y + 22}, false);
    constexpr int trackLeft = 373;
    constexpr int trackWidth = 79;
    shellFill(canvas, RECT{trackLeft, y + 9, trackLeft + trackWidth, y + 13},
        RGB(8, 3, 3));
    shellFrame(canvas, RECT{trackLeft, y + 8, trackLeft + trackWidth, y + 14},
        RGB(117, 17, 11));
    const int gripWidth = state.trackGrip.valid() ? state.trackGrip.width : 12;
    const int travel = std::max(0, trackWidth - gripWidth);
    const int clamped = std::clamp(position, 0, std::max(1, maximum));
    const int gripX = trackLeft + travel * clamped / std::max(1, maximum);
    if (state.trackGrip.valid()) shellBlit(canvas, state.trackGrip, gripX, y);
    else {
        shellFill(canvas, RECT{gripX, y, gripX + gripWidth, y + 22}, RGB(126, 22, 14));
        shellFrame(canvas, RECT{gripX, y, gripX + gripWidth, y + 22}, RGB(255, 177, 38));
    }
    const RECT valueBox{456, y, 506, y + 22};
    shellSelectBox(state, canvas, valueBox, HitTarget{});
    shellText(state, canvas, value, valueBox, true);
    addHit(
        state,
        RECT{trackLeft, y, trackLeft + trackWidth, y + 22},
        sliderKind
    );
}

void drawLobbyOption(
    MenuState& state,
    ShellCanvas& canvas,
    int x,
    int y,
    const std::wstring& text,
    bool checked,
    HitKind kind,
    int width = 155
) {
    const AssetImage& image = checked ? state.checkOn : state.checkOff;
    if (image.valid()) shellBlit(canvas, image, x, y);
    else {
        shellFill(canvas, RECT{x, y, x + 18, y + 18}, RGB(5, 5, 5));
        shellFrame(canvas, RECT{x, y, x + 18, y + 18}, RGB(117, 17, 11));
        if (checked) shellFill(canvas, RECT{x + 4, y + 4, x + 14, y + 14}, RGB(255, 222, 0));
    }
    shellText(state, canvas, text, RECT{x + 22, y - 2, x + width, y + 20}, false);
    addHit(state, RECT{x, y - 2, x + width, y + 21}, kind);
}

void drawLobbyOptions(MenuState& state, ShellCanvas& canvas) {
    const std::array<int, 7>& credits = startingCreditValues();
    const auto credit = std::lower_bound(
        credits.begin(), credits.end(), state.selection.startingCredits
    );
    const int creditPosition = credit == credits.end()
        ? static_cast<int>(credits.size()) - 1
        : static_cast<int>(credit - credits.begin());
    drawLobbyStepper(state, canvas, 313,
        label(state, {"GUI:GameSpeed"}, L"Game Speed"),
        std::to_wstring(state.selection.gameSpeed),
        state.selection.gameSpeed, 6,
        HitKind::SpeedSlider);
    drawLobbyStepper(state, canvas, 343,
        label(state, {"GUI:Credits"}, L"Credits"),
        std::to_wstring(state.selection.startingCredits),
        creditPosition, static_cast<int>(credits.size()) - 1,
        HitKind::CreditsSlider);
    drawLobbyStepper(state, canvas, 373,
        label(state, {"GUI:UnitCount"}, L"Starting Units"),
        std::to_wstring(state.selection.unitCount),
        state.selection.unitCount, 10,
        HitKind::UnitsSlider);
    drawLobbyOption(state, canvas, 59, 313,
        label(state, {"GUI:ShortGame"}, L"Short Game"),
        state.selection.shortGame, HitKind::ToggleShortGame);
    drawLobbyOption(state, canvas, 59, 343,
        label(state, {"GUI:MCVRepacks"}, L"MCV Repacks"),
        state.selection.mcvRepacks, HitKind::ToggleMcvRepacks);
    drawLobbyOption(state, canvas, 59, 373,
        L"奖励箱子",
        state.selection.crates, HitKind::ToggleCrates);
    drawLobbyOption(state, canvas, 59, 403,
        label(state, {"GUI:SuperWeaponsAllowed"}, L"Superweapons"),
        state.selection.superweapons, HitKind::ToggleSuperweapons);
    drawLobbyOption(state, canvas, 273, 415,
        label(state, {"GUI:BuildOffAlly"}, L"Build Off Ally"),
        state.selection.buildOffAlly, HitKind::ToggleBuildOffAlly, 233);
    drawLobbyOption(state, canvas, 59, 433, L"AI",
        state.selection.computerAiEnabled, HitKind::ToggleComputerAi);
    if (!state.config.multiplayerActive) drawLobbyOption(state, canvas, 273, 445,
        L"无冷却", state.selection.noCooldown, HitKind::ToggleNoCooldown, 233);
}

void drawLobbyPage(MenuState& state, ShellCanvas& canvas) {
    drawLobbyPlayers(state, canvas);
    drawLobbyOptions(state, canvas);
    if (state.mpInfoShell.valid()) shellBlit(canvas, state.mpInfoShell, 644, 157);
    std::wstring mapName;
    if (state.selectedMap >= 0 && state.selectedMap < static_cast<int>(state.maps.size())) {
        mapName = state.maps[static_cast<std::size_t>(state.selectedMap)].displayName;
    }
    const std::wstring mode = state.selectedMode >= 0
        && state.selectedMode < static_cast<int>(state.modes.size())
        ? state.modes[static_cast<std::size_t>(state.selectedMode)].displayName
        : L"";
    shellText(state, canvas, mode, RECT{650, 162, 794, 181}, true,
        RGB(214, 214, 194));
    shellText(state, canvas, mapName, RECT{650, 195, 794, 217}, true);
    drawShellButton(state, canvas, HitKind::Start, 241,
        label(state, {"GUI:StartGame"}, L"Start Game"));
    drawShellButton(state, canvas, HitKind::ChooseMap, 283,
        label(state, {"GUI:ChooseMap"}, L"Choose Map"));
    drawShellButton(state, canvas, HitKind::Back, kShellBottomButtonTop,
        label(state, {"GUI:Back"}, L"Back"), true);
}

#include "ui/skirmish/RuntimeRandomMapMenu.inl"

void drawMapSelectionPage(MenuState& state, ShellCanvas& canvas) {
    shellText(state, canvas,
        label(state, {"GUI:SelectEngagement"}, L"Select Engagement"),
        RECT{86, 86, 506, 112}, true);
    shellText(state, canvas,
        label(state, {"GUI:GameType"}, L"Game Type"),
        RECT{86, 119, 236, 141}, true);
    shellText(state, canvas,
        label(state, {"GUI:Map"}, L"Map"),
        RECT{256, 119, 506, 141}, true);
    shellFill(canvas, RECT{86, 143, 236, 443}, RGB(3, 3, 3));
    shellFrame(canvas, RECT{86, 143, 236, 443}, RGB(117, 17, 11));
    shellFill(canvas, RECT{256, 143, 506, 443}, RGB(3, 3, 3));
    shellFrame(canvas, RECT{256, 143, 506, 443}, RGB(117, 17, 11));

    constexpr int rowHeight = 31;
    for (std::size_t i = 0; i < state.modes.size() && i < 9; ++i) {
        RECT row{88, 145 + static_cast<int>(i) * rowHeight,
            234, 145 + static_cast<int>(i + 1) * rowHeight};
        const HitTarget target{HitKind::Mode, static_cast<int>(i)};
        if (state.selectedMode == static_cast<int>(i)) {
            shellFill(canvas, row, RGB(74, 7, 5));
        } else if (state.hover == target) {
            shellFill(canvas, row, RGB(38, 14, 10));
        }
        shellText(state, canvas, state.modes[i].displayName,
            RECT{93, row.top, 230, row.bottom}, false,
            state.selectedMode == static_cast<int>(i)
                ? RGB(255, 232, 0) : RGB(206, 206, 187));
        addHit(state, row, HitKind::Mode, static_cast<int>(i));
    }

    const int maxScroll = std::max(0,
        static_cast<int>(state.filteredMaps.size()) - kVisibleMapRows);
    state.mapScroll = std::clamp(state.mapScroll, 0, maxScroll);
    for (int visible = 0; visible < kVisibleMapRows; ++visible) {
        const int filtered = state.mapScroll + visible;
        if (filtered >= static_cast<int>(state.filteredMaps.size())) break;
        const int mapIndex = state.filteredMaps[static_cast<std::size_t>(filtered)];
        RECT row{258, 145 + visible * rowHeight, 488, 145 + (visible + 1) * rowHeight};
        const HitTarget target{HitKind::Map, filtered};
        if (state.selectedMap == mapIndex) shellFill(canvas, row, RGB(74, 7, 5));
        else if (state.hover == target) shellFill(canvas, row, RGB(38, 14, 10));
        shellText(state, canvas, state.maps[static_cast<std::size_t>(mapIndex)].displayName,
            RECT{263, row.top, 484, row.bottom}, false,
            state.selectedMap == mapIndex ? RGB(255, 232, 0) : RGB(206, 206, 187));
        addHit(state, row, HitKind::Map, filtered);
    }
    const HitTarget scrollUpTarget{HitKind::MapScrollUp, -1};
    const HitTarget scrollDownTarget{HitKind::MapScrollDown, -1};
    const AssetImage& upArrow = state.pressed == scrollUpTarget
        ? state.scrollUpPressed
        : state.scrollUp;
    const AssetImage& downArrow = state.pressed == scrollDownTarget
        ? state.scrollDownPressed
        : state.scrollDown;
    if (upArrow.valid()) shellBlit(canvas, upArrow, 488, 143);
    if (downArrow.valid()) shellBlit(canvas, downArrow, 488, 421);
    shellFill(canvas, RECT{488, 165, 506, 421}, RGB(5, 4, 4));
    shellFrame(canvas, RECT{488, 165, 506, 421}, RGB(82, 15, 10));
    if (maxScroll > 0
        && state.scrollGripTop.valid()
        && state.scrollGripMiddle.valid()
        && state.scrollGripBottom.valid()) {
        constexpr int gripHeight = 34;
        constexpr int trackTop = 165;
        constexpr int trackHeight = 256;
        const int gripY = trackTop
            + state.mapScroll * (trackHeight - gripHeight) / maxScroll;
        shellBlit(canvas, state.scrollGripTop, 488, gripY);
        shellBlit(canvas, state.scrollGripMiddle, 488, gripY + 2);
        shellBlit(canvas, state.scrollGripBottom, 488, gripY + 32);
    }
    addHit(state, RECT{488, 143, 506, 165}, HitKind::MapScrollUp);
    addHit(state, RECT{488, 421, 506, 443}, HitKind::MapScrollDown);

    if (state.selectedMap >= 0 && state.selectedMap < static_cast<int>(state.maps.size())) {
        const MapInfo& map = state.maps[static_cast<std::size_t>(state.selectedMap)];
        shellText(state, canvas, map.displayName, RECT{644, 158, 790, 184}, true);
        shellText(state, canvas,
            std::to_wstring(map.minPlayers) + L"-" + std::to_wstring(map.maxPlayers)
                + L" " + label(state, {"GUI:Players"}, L"Players"),
            RECT{644, 184, 790, 205}, true, RGB(206, 206, 187));
    }
    drawShellButton(state, canvas, HitKind::UseMap, 241,
        label(state, {"GUI:UseMap"}, L"Use Map"));
    if (state.selectedMode >= 0 && state.selectedMode < static_cast<int>(state.modes.size())
        && state.modes[state.selectedMode].randomMapsAllowed) {
        drawShellButton(state, canvas, HitKind::CreateRandomMap, 283,
            label(state, {"GUI:CreateRandomMap"}, L"建立随机地图"));
    }
    drawShellButton(state, canvas, HitKind::CancelMap, kShellBottomButtonTop,
        label(state, {"GUI:Cancel"}, L"Cancel"), true);
}

bool ensureShellBackBuffer(MenuState& state,HDC reference) {
    const auto frame=state.composeFrame.view();
    if(state.backBuffer.ready() && frame.valid() && frame.width==kBaseWidth
        && frame.height==kBaseHeight && frame.rowStride==static_cast<std::size_t>(kBaseWidth)*4) return true;
    if(!state.backBuffer.ensure(reference,kBaseWidth,kBaseHeight)) return false;
    if(!state.composeFrame.reset(kBaseWidth,kBaseHeight)) {
        state.backBuffer.release(); return false;
    }
    state.backDirty=true;return true;
}

bool composeShellPixels(MenuState& state,ShellCanvas canvas) {
    // Actual layout/font needs this contiguous logical canvas; no native object.
    if(!canvas.valid() || canvas.width!=kBaseWidth || canvas.height!=kBaseHeight
        || canvas.rowStride!=static_cast<std::size_t>(kBaseWidth)*4) return false;
    state.hits.clear();
    drawShellChrome(state, canvas);
    if (state.page == SkirmishPage::Lobby) {
        drawLobbyPage(state, canvas);
        drawOpenDropdown(state, canvas);
    } else if (state.page == SkirmishPage::RandomMap) {
        drawRandomMapPage(state, canvas);
    } else if (state.page == SkirmishPage::RandomMapPreview) {
        drawRandomMapPreviewPage(state, canvas);
    } else if (state.page == SkirmishPage::RandomMapLoad) {
        drawSavedRandomMapPage(state, canvas);
    } else {
        drawMapSelectionPage(state, canvas);
    }
    shellText(state, canvas, state.status,
        RECT{10, kShellStatusTop + 3, kShellLeftWidth - 8, kBaseHeight - 1},
        false);
    if (state.mouseVisible) {
        shellBlit(canvas, state.mouse, state.mousePoint.x, state.mousePoint.y);
    }
    return true;
}

bool renderShellBackBuffer(MenuState& state) {
    if(!state.backBuffer.ready() || !composeShellPixels(state,state.composeFrame.view())) return false;
    const auto& frame=state.composeFrame;
    if(!state.backBuffer.upload(frame.view())) {
        state.backDirty=true;return false;
    }
    state.backDirty=false;return true;
}

bool renderShellClient(MenuState& state, HDC target, int width, int height) {
    computeShellViewport(state, width, height);
    if (!ensureShellBackBuffer(state, target)) return false;
    if (state.backDirty && !renderShellBackBuffer(state)) return false;
    BOOL copied;
    if (state.viewWidth == kBaseWidth && state.viewHeight == kBaseHeight) {
        copied = BitBlt(target, state.viewLeft, state.viewTop,
            kBaseWidth, kBaseHeight, state.backBuffer.dc(), 0, 0, SRCCOPY);
    } else {
        SetStretchBltMode(target, COLORONCOLOR);
        copied = StretchBlt(target, state.viewLeft, state.viewTop,
            state.viewWidth, state.viewHeight, state.backBuffer.dc(), 0, 0,
            kBaseWidth, kBaseHeight, SRCCOPY);
    }
    const HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RECT bar{0, 0, width, state.viewTop};
    if (bar.bottom > bar.top) FillRect(target, &bar, black);
    bar = RECT{0, state.viewTop + state.viewHeight, width, height};
    if (bar.bottom > bar.top) FillRect(target, &bar, black);
    bar = RECT{0, state.viewTop, state.viewLeft, state.viewTop + state.viewHeight};
    if (bar.right > bar.left) FillRect(target, &bar, black);
    bar = RECT{state.viewLeft + state.viewWidth, state.viewTop,
        width, state.viewTop + state.viewHeight};
    if (bar.right > bar.left) FillRect(target, &bar, black);
    return copied != FALSE;
}

POINT shellClientToBase(const MenuState& state, POINT point) {
    const auto value = concord::skirmish::shellClientToBase(state, {point.x, point.y});
    return {value.x, value.y};
}

bool updateShellMouse(MenuState& state, POINT point) {
    return concord::skirmish::updateShellMouse(state, {point.x, point.y});
}

void synchronizeShellMouse(MenuState& state, HWND window) {
    POINT client{};
    if (GetCursorPos(&client) && WindowFromPoint(client) == window
        && ScreenToClient(window, &client)) {
        updateShellMouse(state, client);
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
    } else {
        updateShellMouse(state, POINT{-1, -1});
    }
}





void MenuState::requestRepaint() { invalidate(*this); }
void MenuState::playSound(const std::string& event, bool beep) { audio.play(event, beep); }
void MenuState::stopMusic() { audio.stopMusic(); }
std::uint64_t MenuState::nowMs() const { return softwareWindow ? softwareWindow->nowMs() : GetTickCount64(); }
void MenuState::destroyWindow() { if(softwareWindow) softwareWindow->close(); else if(hwnd != nullptr && IsWindow(hwnd)) DestroyWindow(hwnd); }
void MenuState::prewarmMainMenu() { if(!softwareWindow) prewarmRuntimeMainMenu(config, hwnd == nullptr ? nullptr : GetParent(hwnd)); }
bool MenuState::saveCratePreference(const RuntimeConfig& c, bool value, std::string& error) { return saveRuntimeSkirmishCratePreference(c,value,error); }
bool MenuState::saveNoCooldownPreference(const RuntimeConfig& c, bool value, std::string& error) { return saveRuntimeSkirmishNoCooldownPreference(c,value,error); }
bool MenuState::ensurePreview(int index) { return ::ensurePreview(*this,index); }
void MenuState::loadDefaults(const RuntimeConfig& c, RuntimeSkirmishSelection& value) { loadSkirmishDefaults(c,value); }
std::wstring MenuState::text(const std::string& key, const std::wstring& fallback) const { return csf.get(key,fallback); }
std::wstring MenuState::label(std::initializer_list<std::string> keys, const wchar_t* fallback) const { return csf.getAny(keys,fallback); }
std::wstring MenuState::widenUtf8(const std::string& value) const { return utf8ToWide(value); }
std::string MenuState::loadingBriefKey(const std::string& country) const { return runtimeLoadingBriefKeyForCountry(country); }
std::string MenuState::loadingScreenAsset(const std::string& country) const { return runtimeLoadingScreenAssetForCountry(country); }
const CooperativeScenarioDefinition* MenuState::cooperativeScenario(const std::string& id) const { return cooperativeScenarios.findByMapId(id); }
bool MenuState::generatedMapFile(const std::string& file) const { RuntimeRandomMapOptions options; return runtimeRandomMapOptionsFromFileName(file,options); }
int MenuState::sliderGripWidth() const { return trackGrip.valid() ? trackGrip.width : 12; }
bool MenuState::cursorAvailable() const { return mouse.valid(); }
bool MenuState::activateRandomTarget(HitTarget target, int direction) {
    if(softwareWindow) {
        const bool randomTarget = target.kind >= HitKind::CreateRandomMap && target.kind <= HitKind::RandomCancel;
        if(randomTarget) { status=L"此窗口暂不支持随机地图编辑，请返回原窗口使用。"; concord::skirmish::invalidate(*this); }
        return randomTarget;
    }
    return activateRandomMapTarget(*this,target,direction);
}
bool MenuState::randomMapReady() const { return randomResult.has_value() && !randomDirty; }
void MenuState::warnInvalidSelection() { MessageBeep(MB_ICONWARNING); }
void MenuState::focus() { if(softwareWindow) { if(!softwareWindow->focus()) softwareFailure=softwareWindow->error(); } else SetFocus(hwnd); }
void MenuState::capture() { if(softwareWindow) { if(!softwareWindow->capture()) softwareFailure=softwareWindow->error(); } else SetCapture(hwnd); }
void MenuState::releaseCaptureIfOwned() { if(softwareWindow) softwareWindow->releaseCapture(); else if(GetCapture()==hwnd) ReleaseCapture(); }

LRESULT CALLBACK shellSkirmishWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
) {
    MenuState* state = reinterpret_cast<MenuState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA)
    );
    if (message == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        state = static_cast<MenuState*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        state->hwnd = window;
    }
    if (state == nullptr) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE:
        SetTimer(window, 2, 16, nullptr);
        synchronizeShellMouse(*state, window);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        computeShellViewport(*state, LOWORD(lParam), HIWORD(lParam));
        synchronizeShellMouse(*state, window);
        state->backDirty = true;
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        renderShellClient(*state, dc, client.right, client.bottom);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 47014 && HIWORD(wParam) == EN_CHANGE) {
            wchar_t name[64]{};
            GetWindowTextW(state->randomNameWindow, name, 64);
            state->randomName = name;
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:
        if (reinterpret_cast<HWND>(lParam) == state->randomNameWindow) {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, RGB(255, 232, 0)); SetBkColor(dc, RGB(0, 0, 0));
            return reinterpret_cast<LRESULT>(GetStockObject(BLACK_BRUSH));
        }
        break;
    case WM_TIMER:
        if (wParam == 2) {
            if (pollRandomMap(*state)) InvalidateRect(window, nullptr, FALSE);
            updateRandomMapNameWindow(*state);
            state->audio.ensureMusicPlaying();
            if (updateMenuTransition(*state, GetTickCount64())
                && IsWindow(window)) {
                state->backDirty = true;
                InvalidateRect(window, nullptr, FALSE);
            }
        }
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
        handleSkirmishMouseMove(*state, runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam)));
        return 0;
    }
    case WM_MOUSELEAVE:
        handleSkirmishMouseLeave(*state);
        return 0;
    case WM_LBUTTONDOWN: {
        handleSkirmishLeftDown(*state, runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam)));
        return 0;
    }
    case WM_LBUTTONUP: {
        handleSkirmishLeftUp(*state, runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam)));
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        handleSkirmishDoubleClick(*state, runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam)));
        return 0;
    }
    case WM_RBUTTONUP: {
        handleSkirmishRightUp(*state, runtimeInputPixelPointFromPackedPosition(static_cast<std::uint32_t>(lParam)));
        return 0;
    }
    case WM_CAPTURECHANGED:
        handleSkirmishCaptureLost(*state);
        return 0;
    case WM_MOUSEWHEEL:
        handleSkirmishWheel(*state, GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA);
        return 0;
    case WM_KEYDOWN: {
        RuntimeSkirmishKey key = RuntimeSkirmishKey::Other;
        switch(wParam) {
        case VK_ESCAPE: key=RuntimeSkirmishKey::Escape; break;
        case VK_RETURN: key=RuntimeSkirmishKey::Return; break;
        case VK_SPACE: key=RuntimeSkirmishKey::Space; break;
        case VK_UP: key=RuntimeSkirmishKey::Up; break;
        case VK_DOWN: key=RuntimeSkirmishKey::Down; break;
        case VK_HOME: key=RuntimeSkirmishKey::Home; break;
        case VK_END: key=RuntimeSkirmishKey::End; break;
        default: break;
        }
        if (handleSkirmishKey(*state, key)) return 0;
        break;
    }
    case WM_CLOSE:
        completeMenu(*state, false);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT
            && reinterpret_cast<HWND>(wParam) == window && state->mouse.valid()) {
            synchronizeShellMouse(*state, window);
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_NCDESTROY:
        KillTimer(window, 2);
        state->audio.stopMusic();
        state->hwnd = nullptr;
        state->randomNameWindow = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return DefWindowProcW(window, message, wParam, lParam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool registerShellSkirmishClass(HINSTANCE instance) {
    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    cls.lpfnWndProc = shellSkirmishWindowProc;
    cls.hInstance = instance;
    cls.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    cls.lpszClassName = L"ConcordRTS.RuntimeSkirmishShell";
    if (RegisterClassExW(&cls) != 0) return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND createShellSkirmishWindow(
    MenuState& state,
    const RuntimeConfig& config,
    HWND hostWindow
) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!registerShellSkirmishClass(instance)) return nullptr;

    if (hostWindow != nullptr && IsWindow(hostWindow)) {
        RECT hostClient{};
        GetClientRect(hostWindow, &hostClient);
        return CreateWindowExW(
            0,
            L"ConcordRTS.RuntimeSkirmishShell",
            L"",
            WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
            0,
            0,
            std::max(1, static_cast<int>(hostClient.right)),
            std::max(1, static_cast<int>(hostClient.bottom)),
            hostWindow,
            nullptr,
            instance,
            &state
        );
    }

    int clientWidth = config.windowWidth > 0 ? config.windowWidth : kBaseWidth;
    int clientHeight = config.windowHeight > 0 ? config.windowHeight : kBaseHeight;
    clientWidth = std::max(kBaseWidth, clientWidth);
    clientHeight = std::max(kBaseHeight, clientHeight);
    RECT windowRect{0, 0, clientWidth, clientHeight};
    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRectEx(&windowRect, style, FALSE, 0);
    int windowWidth = windowRect.right - windowRect.left;
    int windowHeight = windowRect.bottom - windowRect.top;
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    windowWidth = std::min<int>(
        windowWidth,
        static_cast<int>(workArea.right - workArea.left)
    );
    windowHeight = std::min<int>(
        windowHeight,
        static_cast<int>(workArea.bottom - workArea.top)
    );
    const int x = workArea.left
        + ((workArea.right - workArea.left) - windowWidth) / 2;
    const int y = workArea.top
        + ((workArea.bottom - workArea.top) - windowHeight) / 2;

    return CreateWindowExW(
        0,
        L"ConcordRTS.RuntimeSkirmishShell",
        L"Command & Conquer: Yuri's Revenge - Skirmish",
        style,
        x,
        y,
        windowWidth,
        windowHeight,
        nullptr,
        nullptr,
        instance,
        &state
    );
}

#include "ui/skirmish/diagnostics/RuntimeSkirmishCursorSmoke.inl"
#include "ui/skirmish/RuntimeSkirmishSoftwareWindow.inl"

} // namespace

int resolveRuntimeMapStartingPointLimit(const RuntimeConfig& config) {
    if (config.mapStartingPointLimit > 0) {
        return std::min(config.mapStartingPointLimit, kPlayerSlots);
    }
    const fs::path actualPath(config.defaultMapPath);
    IniFile actualMap;
    if (!actualMap.loadFromFile(pathString(actualPath))) return 0;
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
        for (const MapInfo& map : scanMaps(exportRoot / "multimd", packet, unusedLabels)) {
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

std::string runtimeLoadingScreenAssetForCountry(
    const std::string& countryId
) {
    const std::string country = lowerAscii(trimAscii(countryId));
    if (country == "yuri" || country == "yuricountry") {
        return "ls800yuri.shp";
    }
    if (country == "observer" || country == "obs" || country == "random") {
        return "ls800obs.shp";
    }
    if (country == "americans" || country == "america"
        || country == "usa" || country == "ustates") {
        return "ls800ustates.shp";
    }
    if (country == "russians" || country == "russia") {
        return "ls800russia.shp";
    }
    if (country == "africans" || country == "libya" || country == "lybia") {
        return "ls800libya.shp";
    }
    if (country == "alliance" || country == "korea") {
        return "ls800korea.shp";
    }
    if (country == "arabs" || country == "iraq") {
        return "ls800iraq.shp";
    }
    if (country == "germans" || country == "germany") {
        return "ls800germany.shp";
    }
    if (country == "french" || country == "france") {
        return "ls800france.shp";
    }
    if (country == "confederation" || country == "cuba") {
        return "ls800cuba.shp";
    }
    if (country == "british" || country == "ukingdom"
        || country == "unitedkingdom") {
        return "ls800ukingdom.shp";
    }
    return "ls800obs.shp";
}

std::string runtimeLoadingPaletteAssetForCountry(
    const std::string& countryId
) {
    const std::string country = lowerAscii(trimAscii(countryId));
    if (country == "yuri" || country == "yuricountry") {
        return "mpyls.pal";
    }
    if (country == "observer" || country == "obs" || country == "random") {
        return "mplsobs.pal";
    }
    if (country == "americans" || country == "america"
        || country == "usa" || country == "ustates") {
        return "mplsu.pal";
    }
    if (country == "russians" || country == "russia") {
        return "mplsr.pal";
    }
    if (country == "africans" || country == "libya" || country == "lybia") {
        return "mplsl.pal";
    }
    if (country == "alliance" || country == "korea") {
        return "mplsk.pal";
    }
    if (country == "arabs" || country == "iraq") {
        return "mplsi.pal";
    }
    if (country == "germans" || country == "germany") {
        return "mplsg.pal";
    }
    if (country == "french" || country == "france") {
        return "mplsf.pal";
    }
    if (country == "confederation" || country == "cuba") {
        return "mplsc.pal";
    }
    if (country == "british" || country == "ukingdom"
        || country == "unitedkingdom") {
        return "mplsuk.pal";
    }
    return "mplsobs.pal";
}

std::string runtimeLoadingBriefKeyForCountry(const std::string& countryId) {
    const std::string country = lowerAscii(trimAscii(countryId));
    if (country == "yuri" || country == "yuricountry") {
        return "LoadBrief:YuriCountry";
    }
    if (country == "americans" || country == "america"
        || country == "usa" || country == "ustates") {
        return "LoadBrief:USA";
    }
    if (country == "russians" || country == "russia") {
        return "LoadBrief:Russia";
    }
    // Westwood's original CSF intentionally spells this key "Lybia".
    if (country == "africans" || country == "libya" || country == "lybia") {
        return "LoadBrief:Lybia";
    }
    if (country == "alliance" || country == "korea") {
        return "LoadBrief:Korea";
    }
    if (country == "arabs" || country == "iraq") {
        return "LoadBrief:Iraq";
    }
    if (country == "germans" || country == "germany") {
        return "LoadBrief:Germans";
    }
    if (country == "french" || country == "france") {
        return "LoadBrief:French";
    }
    if (country == "confederation" || country == "cuba") {
        return "LoadBrief:Cuba";
    }
    if (country == "british" || country == "ukingdom"
        || country == "unitedkingdom") {
        return "LoadBrief:British";
    }
    return "";
}

RuntimeSkirmishSelection runRuntimeSkirmishMenuSdl3(const RuntimeConfig& config, std::string& error) {
    return runSdlSkirmishMenu(config,error);
}

void prewarmRuntimeSkirmishMenu(const RuntimeConfig& config) {
    if (pendingMenuState.valid()) return;
    if (prewarmedMenuState != nullptr && prewarmedMenuState->initialized
        && prewarmedMenuState->resourceWatch->unchanged(config)) return;
    prewarmedMenuState.reset();
    // The worker owns its entire state, including renderer caches. It never
    // creates a window, paints, plays audio, or touches a live menu's state.
    pendingMenuState = std::async(std::launch::async, [config]() {
        auto state = std::make_unique<MenuState>();
        try {
            initializeState(*state, config, false);
        } catch (const std::exception& error) {
            std::cerr << "Skirmish preparation failed: " << error.what() << '\n';
            state->initialized = false;
        }
        return state;
    });
}

bool runtimeSkirmishMenuReady(const RuntimeConfig& config) {
    if (pendingMenuState.valid()) {
        if (pendingMenuState.wait_for(std::chrono::milliseconds(0))
            != std::future_status::ready) return false;
        prewarmedMenuState = pendingMenuState.get();
    }
    if (prewarmedMenuState != nullptr
        && prewarmedMenuState->config.resourceRootPath == config.resourceRootPath
        && prewarmedMenuState->config.localDataRootPath == config.localDataRootPath) {
        // A completed failure is also ready: the caller must handle it,
        // rather than leave the main menu in a never-ending transition.
        return true;
    }
    prewarmRuntimeSkirmishMenu(config);
    return false;
}

RuntimeSkirmishSelection runRuntimeSkirmishMenu(
    const RuntimeConfig& config,
    RuntimeWindowId parent
) {
    const auto resolvedParent = resolveRuntimeWindowParentWin32(parent);
    if (!resolvedParent.supported) return {};
    HWND requestedHost = resolvedParent.window;
    const bool startedWithHost = requestedHost != nullptr && IsWindow(requestedHost);
    if (!pendingMenuState.valid()
        && (prewarmedMenuState == nullptr || prewarmedMenuState->initialized)) {
        prewarmRuntimeSkirmishMenu(config);
    }
    // The regular main-menu transition only calls us once ready. Direct
    // lobby launches must also keep the host responsive during preparation.
    while (!runtimeSkirmishMenuReady(config)) {
        if (startedWithHost && !IsWindow(requestedHost)) return {};
        MsgWaitForMultipleObjectsEx(0, nullptr, 16, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                PostQuitMessage(static_cast<int>(message.wParam));
                return {};
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (startedWithHost && !IsWindow(requestedHost)) return {};
    std::unique_ptr<MenuState> stateOwner = std::move(prewarmedMenuState);
    MenuState& state = *stateOwner;
    if (!state.initialized) {
        state.selection.accepted = false;
        return state.selection;
    }
    resetMenuForEntry(state, config);

    if (requestedHost != nullptr && !IsWindow(requestedHost)) {
        requestedHost = nullptr;
    }
    HWND window = state.hwnd;
    if (window != nullptr
        && IsWindow(window)
        && GetParent(window) != requestedHost) {
        DestroyWindow(window);
        window = nullptr;
    }
    if (window == nullptr || !IsWindow(window)) {
        window = createShellSkirmishWindow(
            state,
            config,
            requestedHost
        );
    }
    if (window == nullptr) {
        state.selection.accepted = false;
        return state.selection;
    }
    state.closing = false;
    state.phase = ShellMenuPhase::SlideIn;
    state.phaseStarted = GetTickCount64();
    state.phaseTick = 0;
    state.backDirty = true;
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    if (!state.smoke) {
        SetForegroundWindow(requestedHost == nullptr ? window : requestedHost);
    }
    // YR's main menu and skirmish lobby both select the INTRO theme.  The
    // shipped thememd.ini resolves INTRO to Drok and marks it Repeat=yes.
    state.audio.startMusic(state.exportRoot / "thememd" / "drok.wav");
    state.audio.play("MenuSlideIn");

    MSG message{};
    while (IsWindow(window)) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            if (result == 0) {
                PostQuitMessage(static_cast<int>(message.wParam));
            }
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    state.audio.stopMusic();
    syncSelection(state);
    RuntimeSkirmishSelection selection = state.selection;
    prewarmedMenuState = std::move(stateOwner);
    return selection;
}

#include "ui/skirmish/diagnostics/RuntimeSkirmishCrateOptionSmoke.inl"
#include "ui/skirmish/diagnostics/RuntimeNoCooldownMenuSmoke.inl"
#include "ui/input/diagnostics/RuntimeObserverMenuSmoke.inl"
#include "ui/skirmish/diagnostics/RuntimeSkirmishPreloadSmoke.inl"
#include "audio/world/diagnostics/RuntimeSkirmishAudioSmoke.inl"
#include "simulation/scenarios/diagnostics/RuntimeSkirmishModeTransitionSmoke.inl"
#include "ui/skirmish/diagnostics/RuntimeRandomMapMenuSmoke.inl"
#include "ui/skirmish/diagnostics/RuntimeSkirmishWindowLifecycleSmoke.inl"
#include "ui/skirmish/diagnostics/RuntimeSkirmishPixelSurfaceSmoke.inl"
#include "audio/menu/diagnostics/RuntimeSkirmishMusicClockSmoke.inl"
#include "ui/skirmish/diagnostics/RuntimeSecondWindowContract.inl"

int runRuntimeSkirmishMenuSmokeTest(
    const RuntimeConfig& config,
    const std::string& bmpPath
) {
    if (const char* output = std::getenv("CONCORD_SECOND_WINDOW_CONTRACT_OUTPUT"))
        return runSecondWindowContract(config, fs::path(output));
    if (std::getenv("CONCORD_SKIRMISH_MUSIC_CLOCK_ONLY"))
        return runSkirmishMusicClockSmoke(fs::path(bmpPath));
    if (std::getenv("CONCORD_SKIRMISH_WINDOW_LIFECYCLE_ONLY"))
        return runSkirmishWindowLifecycleSmoke(config, fs::path(bmpPath));
    if (std::getenv("CONCORD_SKIRMISH_OBSERVER_ONLY")) {
        MenuState state; std::string error;
        if (!initializeState(state, config, true)
                || !runRuntimeObserverMenuSmoke(state, bmpPath, error)) {
            std::cerr << "Observer menu smoke failed: " << error << '\n';
            return 1;
        }
        return 0;
    }
    if (std::getenv("CONCORD_RANDOM_MAP_MENU_ONLY")) {
        MenuState state; std::string error;
        if (!initializeState(state, config, true) || !runRandomMapMenuSmoke(state, fs::path(bmpPath), error)) {
            std::cerr << error << "\n"; return 1;
        }
        return 0;
    }
    if (!runSkirmishPreloadSmoke(config, fs::path(bmpPath).parent_path())) {
        std::cerr << "Skirmish shell smoke failed: preload/reentry contract.\n";
        return 1;
    }
    std::vector<SkirmishBitmapTextTraceCall> bitmapTextTrace;
    MenuState state;
    if (!initializeState(state, config, true)) {
        std::cerr << "Skirmish shell smoke failed: maps or original shell assets are missing.\n";
        return 1;
    }
    state.bitmapTextTrace = &bitmapTextTrace;
    std::string cursorError;
    if (!runRuntimeSkirmishCursorSmoke(state, config, fs::path(bmpPath), cursorError)) {
        std::cerr << "Skirmish shell smoke failed: " << cursorError << '\n';
        return 1;
    }
    std::string modeTransitionError;
    if (!runRuntimeSkirmishModeTransitionSmoke(state, modeTransitionError)) {
        std::cerr << "Skirmish mode transition smoke failed: " << modeTransitionError << '\n';
        return 1;
    }
    if (std::getenv("CONCORD_SKIRMISH_MODE_TRANSITION_ONLY") != nullptr) return 0;
    resetMenuForEntry(state, config);
    const bool configuredSpeedApplied = state.selection.gameSpeed
        == std::clamp(config.gameSpeed, 0, 6);
    const bool configuredCreditsApplied = state.selection.startingCredits
        == std::clamp(config.startingCredits, 5000, 200000);
    const bool configuredComputerAiApplied =
        state.selection.computerAiEnabled == config.computerAiEnabled;
    const auto cooperativeMode = std::find_if(
        state.modes.begin(),
        state.modes.end(),
        isUnavailableCooperativeMode
    );
    const fs::path smokeModesPath = firstExisting({
        state.exportRoot / "expandmd01" / "mpmodesmd.ini",
        state.localUiRoot / "mpmodesmd.ini",
        fs::path(config.localDataRootPath) / "mpmodesmd.ini"
    });
    const std::vector<ModeInfo> unavailableModes = loadModes(
        smokeModesPath,
        state.csf,
        false
    );
    const bool unavailableFallbackHidden = std::none_of(
        unavailableModes.begin(),
        unavailableModes.end(),
        isUnavailableCooperativeMode
    );
    const int savedMode = state.selectedMode;
    const int savedMap = state.selectedMap;
    const std::vector<RuntimeSkirmishPlayerSlot> savedSlots =
        state.selection.slots;
    const std::array<int, kPlayerSlots> savedStarts = state.startingSpots;
    bool cooperativeSelectionContract = false;
    if (cooperativeMode != state.modes.end()) {
        state.selectedMode = static_cast<int>(
            cooperativeMode - state.modes.begin()
        );
        rebuildFilteredMaps(state, false);
        applyMapSlotLimit(state);
        syncSelection(state);
        const CooperativeScenarioDefinition* scenario =
            selectedCooperativeScenario(state);
        cooperativeSelectionContract = scenario != nullptr
            && state.filteredMaps.size()
                == state.cooperativeScenarios.scenarioCount()
            && state.filteredMaps.size()
                == state.cooperativeValidationByMap.size()
            && state.selection.slots.size() >= 4U;
        constexpr std::array<const char*, 4> expectedTeams{{
            "1", "1", "2", "2"
        }};
        for (std::size_t index = 0;
            cooperativeSelectionContract && index < 4U;
            ++index) {
            const RuntimeSkirmishPlayerSlot& slot =
                state.selection.slots[index];
            cooperativeSelectionContract = slot.enabled
                && slot.human == (index == 0U)
                && slot.team == expectedTeams[index]
                && state.startingSpots[index]
                    == static_cast<int>(index) + 1
                && cooperativeCountryAllowed(
                    *scenario,
                    static_cast<int>(index),
                    slot.country
                );
        }
        for (std::size_t index = 4U;
            cooperativeSelectionContract
                && index < state.selection.slots.size();
            ++index) {
            cooperativeSelectionContract =
                !state.selection.slots[index].enabled;
        }
        std::wstring cooperativeStartReason;
        cooperativeSelectionContract = cooperativeSelectionContract
            && canStart(state, cooperativeStartReason)
            && !dropdownCanOpen(state, DropdownKind::Player, 1)
            && !dropdownCanOpen(state, DropdownKind::Team, 0)
            && !dropdownCanOpen(state, DropdownKind::Start, 0)
            && dropdownCanOpen(state, DropdownKind::Country, 0)
            && cooperativeCountryOptionIndices(state, 0).size()
                == scenario->playerCountries.size();
    }
    state.selectedMode = savedMode;
    rebuildFilteredMaps(state, false);
    state.selectedMap = savedMap;
    state.selection.slots = savedSlots;
    state.startingSpots = savedStarts;
    syncSelection(state);
    const bool modeExposureContract = !state.modes.empty()
        && state.cooperativeDataComplete
        && cooperativeMode != state.modes.end()
        && cooperativeSelectionContract
        && unavailableFallbackHidden
        && !allowsAllMapsFallback("cooperative");
    if (!modeExposureContract) {
        std::cerr << "Skirmish shell smoke failed: Cooperative data, map, "
                  << "A-D slot, country constraint, or fallback contract "
                  << "did not hold.\n";
        return 1;
    }
    HWND hostedShell = CreateWindowExW(
        0,
        L"STATIC",
        L"ConcordRTS hosted shell smoke",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        0,
        0,
        kBaseWidth,
        kBaseHeight,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    HWND hostedPage = hostedShell == nullptr
        ? nullptr
        : createShellSkirmishWindow(state, config, hostedShell);
    const bool hostedParentMatched =
        hostedPage != nullptr
        && GetParent(hostedPage) == hostedShell
        && (static_cast<DWORD>(GetWindowLongPtrW(hostedPage, GWL_STYLE))
            & WS_CHILD) != 0
        && GetAncestor(hostedPage, GA_ROOT) == hostedShell;
    if (hostedPage != nullptr && IsWindow(hostedPage)) {
        DestroyWindow(hostedPage);
    }
    const bool hostedShellSurvived =
        hostedShell != nullptr && IsWindow(hostedShell);
    if (hostedShellSurvived) {
        DestroyWindow(hostedShell);
    }
    const bool hostedWindowContract =
        hostedParentMatched && hostedShellSurvived;
    if (!hostedWindowContract) {
        std::cerr << "Skirmish shell smoke failed: hosted child page did not "
                  << "preserve its top-level shell.\n";
        return 1;
    }

    state.phase = ShellMenuPhase::SlideIn;
    state.phaseTick = 0;
    const bool slideInStartsClosed =
        shellPhaseFrameForSlot(state, 0, true) == 10
        && shellPhaseFrameForSlot(state, 0, false) == 16;
    state.phaseTick = kShellAnimationFrames;
    const bool slideInEndsOpen =
        shellPhaseFrameForSlot(state, 0, true) == 1
        && shellPhaseFrameForSlot(state, 0, false) == 0;
    state.phase = ShellMenuPhase::SlideOut;
    state.phaseTick = 0;
    const bool slideOutStartsOpen =
        shellPhaseFrameForSlot(state, 0, true) == 5
        && shellPhaseFrameForSlot(state, 0, false) == 11;
    state.phaseTick = kShellAnimationFrames;
    const bool slideOutEndsClosed =
        shellPhaseFrameForSlot(state, 0, true) == 10
        && shellPhaseFrameForSlot(state, 0, false) == 16;
    const bool transitionFrameContract =
        state.slotAnimationShell.size() >= 17
        && slideInStartsClosed
        && slideInEndsOpen
        && slideOutStartsOpen
        && slideOutEndsClosed;
    state.phase = ShellMenuPhase::Idle;
    state.phaseTick = 0;
    if (!transitionFrameContract) {
        std::cerr << "Skirmish shell smoke failed: 17-frame transition did "
                  << "not match the main-menu SlideIn/SlideOut sequence.\n";
        return 1;
    }
    std::cout << "Skirmish hosted transition: child=yes hostSurvived=yes "
              << "slotFrames=17 steps=6 frameMs=42\n";

    if (!ensurePreview(state, state.selectedMap)) {
        std::cerr << "Skirmish shell smoke failed: selected map preview did not decode: "
                  << (state.selectedMap >= 0
                        ? state.maps[static_cast<std::size_t>(state.selectedMap)].previewError
                        : "no selection")
                  << "\n";
        return 1;
    }
    const bool originalGeometry =
        state.background.width == 632 && state.background.height == 568
        && state.statusShell.width == 632 && state.statusShell.height == 32
        && state.sideTopShell[1].width == 168
        && state.sideTopShell[1].height == 199
        && state.slotBackgroundShell.width == 168
        && state.slotBackgroundShell.height == 42
        && state.sideBottomShell.width == 168
        && state.sideBottomShell.height >= 65
        && state.buttons[0].width == 126 && state.buttons[0].height == 25
        && state.mpInfoShell.width == 156 && state.mpInfoShell.height == 84
        && state.checkOff.width == 18 && state.checkOff.height == 18
        && state.checkOn.width == 18 && state.checkOn.height == 18
        && state.trackGrip.width == 12 && state.trackGrip.height == 22
        && state.dropArrow.width == 18 && state.dropArrow.height == 22
        && state.scrollUp.width == 18 && state.scrollUp.height == 22
        && state.scrollGripTop.width == 18 && state.scrollGripTop.height == 2
        && state.scrollGripMiddle.width == 18 && state.scrollGripMiddle.height == 30
        && state.scrollGripBottom.width == 18 && state.scrollGripBottom.height == 2;
    if (!originalGeometry) {
        std::cerr << "Skirmish shell smoke failed: original shell geometry mismatch.\n";
        return 1;
    }
    if(!runMemoryOnlyShellFrameSmoke(config)) return 1;
    HDC screenDc = GetDC(nullptr);
    if (screenDc == nullptr || !ensureShellBackBuffer(state, screenDc)) {
        if (screenDc != nullptr) ReleaseDC(nullptr, screenDc);
        std::cerr << "Skirmish shell smoke failed: persistent buffer allocation failed.\n";
        return 1;
    }
    const std::string lobbyPath = bmpPath.empty()
        ? pathString(fs::path(config.localDataRootPath) / "runtime_skirmish_lobby.bmp")
        : bmpPath;
    fs::path mapPath(lobbyPath);
    mapPath.replace_filename(
        mapPath.stem().string() + "_map_selection" + mapPath.extension().string()
    );
    fs::path countryDropdownPath(lobbyPath);
    countryDropdownPath.replace_filename(
        countryDropdownPath.stem().string()
            + "_country_dropdown"
            + countryDropdownPath.extension().string()
    );
    fs::path loadingPath(lobbyPath);
    loadingPath.replace_filename(
        loadingPath.stem().string()
            + "_loading"
            + loadingPath.extension().string()
    );
    std::string error;

    state.page = SkirmishPage::Lobby;
    state.backDirty = true;
    const bool lobbyRendered = renderShellBackBuffer(state);
    const std::vector<unsigned char> closedLobbyFrame = state.composeFrame.bytes();
    const auto countVisiblePixels = [](const std::vector<unsigned char>& pixels) {
        std::size_t visible = 0;
        for (std::size_t at = 0; at + 3 < pixels.size(); at += 4) {
            if (pixels[at] > 4 || pixels[at + 1] > 4
                || pixels[at + 2] > 4) {
                ++visible;
            }
        }
        return visible;
    };
    const std::size_t lobbyVisiblePixels = countVisiblePixels(
        closedLobbyFrame
    );
    const std::array<HitKind, 17> lobbyHits{{
        HitKind::SlotEnabled,
        HitKind::SlotCountry,
        HitKind::SlotColor,
        HitKind::SlotStart,
        HitKind::SlotTeam,
        HitKind::CreditsSlider,
        HitKind::UnitsSlider,
        HitKind::SpeedSlider,
        HitKind::ToggleShortGame,
        HitKind::ToggleMcvRepacks,
        HitKind::ToggleCrates,
        HitKind::ToggleSuperweapons,
        HitKind::ToggleBuildOffAlly,
        HitKind::ToggleComputerAi,
        HitKind::Start,
        HitKind::ChooseMap,
        HitKind::Back
    }};
    bool lobbyHitData = true;
    for (const HitKind kind : lobbyHits) {
        lobbyHitData = lobbyHitData && hasHitKind(state, kind);
    }
    const bool lobbyWritten = writeBgraBmp(
        lobbyPath, state.composeFrame.bytes().data(), kBaseWidth, kBaseHeight, error
    );

    if (!runRuntimeSkirmishAudioSmoke(state, error)) {
        ReleaseDC(nullptr, screenDc);
        std::cerr << "Skirmish audio smoke failed: " << error << '\n';
        return 1;
    }

    const bool crateOptionValid = runSkirmishCrateOptionSmoke(state, lobbyPath, error);
    const bool noCooldownOptionValid = runNoCooldownMenuSmoke(state, lobbyPath, error);
    const bool observerOptionValid = runRuntimeObserverMenuSmoke(state, lobbyPath, error);

    const RuntimeSkirmishPlayerSlot originalHuman =
        state.selection.slots.front();
    const RuntimeSkirmishPlayerSlot originalComputer =
        state.selection.slots[1];
    const int originalHumanStart = state.startingSpots[0];

    const auto optionHitCount = [&state]() {
        return static_cast<int>(std::count_if(
            state.hits.begin(),
            state.hits.end(),
            [](const HitRegion& hit) {
                return hit.target.kind == HitKind::DropdownOption;
            }
        ));
    };
    const auto openAndRenderDropdown = [
        &state,
        &optionHitCount
    ](DropdownKind kind, int slot) {
        activateShellTarget(
            state,
            HitTarget{hitKindForDropdown(kind), slot},
            1
        );
        const bool opened = state.openDropdown == kind
            && state.openDropdownSlot == slot;
        state.backDirty = true;
        const bool rendered = renderShellBackBuffer(state);
        return opened && rendered
            && optionHitCount() == dropdownOptionCount(state, kind);
    };

    const std::string originalCountry = originalHuman.country;
    const int originalCountryIndex = findCountryIndex(state, originalCountry);
    const bool countryOpened = openAndRenderDropdown(
        DropdownKind::Country,
        0
    );
    const bool countryOpenedWithoutCycling = countryOpened
        && state.selection.slots.front().country == originalCountry;
    const bool dropdownWritten = writeBgraBmp(
        pathString(countryDropdownPath),
        state.composeFrame.bytes().data(),
        kBaseWidth,
        kBaseHeight,
        error
    );
    const std::size_t dropdownVisiblePixels = countVisiblePixels(
        state.composeFrame.bytes()
    );
    constexpr RECT countryDropdownLayer{266, 61, 386, 399};
    std::size_t dropdownOutsideDifferences = 0;
    if (closedLobbyFrame.size() == state.composeFrame.bytes().size()) {
        for (int y = 0; y < kBaseHeight; ++y) {
            for (int x = 0; x < kBaseWidth; ++x) {
                if (pointIn(countryDropdownLayer, POINT{x, y})) continue;
                const std::size_t at = static_cast<std::size_t>(
                    (y * kBaseWidth + x) * 4
                );
                if (!std::equal(
                        closedLobbyFrame.begin() + at,
                        closedLobbyFrame.begin() + at + 4,
                        state.composeFrame.bytes().begin() + at)) {
                    ++dropdownOutsideDifferences;
                }
            }
        }
    } else {
        dropdownOutsideDifferences = std::numeric_limits<std::size_t>::max();
    }
    const bool dropdownCompositeValid = dropdownWritten
        && lobbyVisiblePixels > static_cast<std::size_t>(
            kBaseWidth * kBaseHeight / 4
        )
        && dropdownVisiblePixels >= lobbyVisiblePixels * 9U / 10U
        && dropdownOutsideDifferences == 0;
    const int alternateCountry = state.countries.size() > 1
        ? (originalCountryIndex + 1) % static_cast<int>(state.countries.size())
        : originalCountryIndex;
    activateShellTarget(
        state,
        HitTarget{HitKind::DropdownOption, alternateCountry},
        1
    );
    const bool countrySelectionApplied =
        state.openDropdown == DropdownKind::None
        && state.selection.slots.front().country
            == state.countries[static_cast<std::size_t>(alternateCountry)].id;

    const std::string originalColor = originalHuman.color;
    const int originalColorIndex = findStringIndex(state.colorIds, originalColor);
    const bool colorOpened = openAndRenderDropdown(DropdownKind::Color, 0)
        && state.selection.slots.front().color == originalColor;
    const int alternateColor = state.colorIds.size() > 1
        ? (originalColorIndex + 1) % static_cast<int>(state.colorIds.size())
        : originalColorIndex;
    activateShellTarget(
        state,
        HitTarget{HitKind::DropdownOption, alternateColor},
        1
    );
    const bool colorSelectionApplied =
        state.selection.slots.front().color
            == state.colorIds[static_cast<std::size_t>(alternateColor)];

    const int startChoices = selectedMapLimit(state) + 1;
    const int alternateStart = startChoices > 1
        ? (originalHumanStart + 1) % startChoices
        : originalHumanStart;
    const bool startOpened = openAndRenderDropdown(DropdownKind::Start, 0)
        && state.startingSpots[0] == originalHumanStart;
    activateShellTarget(
        state,
        HitTarget{HitKind::DropdownOption, alternateStart},
        1
    );
    const bool startSelectionApplied =
        state.startingSpots[0] == alternateStart;

    const std::string originalTeam = originalHuman.team;
    const int originalTeamIndex = findStringIndex(state.teams, originalTeam);
    const int alternateTeam = state.teams.size() > 1
        ? (originalTeamIndex + 1) % static_cast<int>(state.teams.size())
        : originalTeamIndex;
    const bool teamOpened = openAndRenderDropdown(DropdownKind::Team, 0)
        && state.selection.slots.front().team == originalTeam;
    activateShellTarget(
        state,
        HitTarget{HitKind::DropdownOption, alternateTeam},
        1
    );
    const bool teamSelectionApplied =
        state.selection.slots.front().team
            == state.teams[static_cast<std::size_t>(alternateTeam)];

    const std::string originalDifficulty = originalComputer.difficulty;
    const int alternateDifficulty = state.difficulties.size() > 1
        ? (findStringIndex(state.difficulties, originalDifficulty) + 1)
            % static_cast<int>(state.difficulties.size())
        : 0;
    const bool playerOpened = openAndRenderDropdown(DropdownKind::Player, 1)
        && state.selection.slots[1].enabled == originalComputer.enabled
        && state.selection.slots[1].difficulty == originalDifficulty;
    activateShellTarget(
        state,
        HitTarget{HitKind::DropdownOption, alternateDifficulty + 1},
        1
    );
    const bool playerSelectionApplied = state.selection.slots[1].enabled
        && state.selection.slots[1].difficulty
            == state.difficulties[static_cast<std::size_t>(alternateDifficulty)];

    const bool dropdownsValid = countryOpenedWithoutCycling
        && dropdownCompositeValid && countrySelectionApplied
        && colorOpened && colorSelectionApplied
        && startOpened && startSelectionApplied
        && teamOpened && teamSelectionApplied
        && playerOpened && playerSelectionApplied;

    const int originalSpeed = state.selection.gameSpeed;
    const int originalCredits = state.selection.startingCredits;
    const int originalUnits = state.selection.unitCount;
    setSliderFromBaseX(state, HitKind::SpeedSlider, 373);
    const bool speedLeft = state.selection.gameSpeed == 0;
    setSliderFromBaseX(state, HitKind::SpeedSlider, 413);
    const bool speedMiddle = state.selection.gameSpeed == 3;
    setSliderFromBaseX(state, HitKind::SpeedSlider, 452);
    const bool speedRight = state.selection.gameSpeed == 6;
    setSliderFromBaseX(state, HitKind::CreditsSlider, 373);
    const bool creditsLeft = state.selection.startingCredits == 5000;
    setSliderFromBaseX(state, HitKind::CreditsSlider, 452);
    const bool creditsRight = state.selection.startingCredits == 200000;
    setSliderFromBaseX(state, HitKind::UnitsSlider, 373);
    const bool unitsLeft = state.selection.unitCount == 0;
    setSliderFromBaseX(state, HitKind::UnitsSlider, 452);
    const bool unitsRight = state.selection.unitCount == 10;
    const bool slidersValid = speedLeft && speedMiddle && speedRight
        && creditsLeft && creditsRight && unitsLeft && unitsRight;

    const bool originalComputerAiEnabled =
        state.selection.computerAiEnabled;
    activateShellTarget(
        state,
        HitTarget{HitKind::ToggleComputerAi, -1},
        1
    );
    const bool computerAiToggleValid =
        state.selection.computerAiEnabled != originalComputerAiEnabled;
    activateShellTarget(
        state,
        HitTarget{HitKind::ToggleComputerAi, -1},
        1
    );
    const bool computerAiToggleRestored =
        state.selection.computerAiEnabled == originalComputerAiEnabled;

    state.selection.slots.front() = originalHuman;
    state.selection.slots[1] = originalComputer;
    state.startingSpots[0] = originalHumanStart;
    state.selection.gameSpeed = originalSpeed;
    state.selection.startingCredits = originalCredits;
    state.selection.unitCount = originalUnits;
    syncSelection(state);

    struct LoadingAlias {
        const char* country;
        const char* image;
        const char* palette;
        const char* brief;
    };
    const std::vector<LoadingAlias> loadingAliases{
        {"Americans", "ls800ustates.shp", "mplsu.pal", "LoadBrief:USA"},
        {"USA", "ls800ustates.shp", "mplsu.pal", "LoadBrief:USA"},
        {"UStates", "ls800ustates.shp", "mplsu.pal", "LoadBrief:USA"},
        {"British", "ls800ukingdom.shp", "mplsuk.pal", "LoadBrief:British"},
        {"UKingdom", "ls800ukingdom.shp", "mplsuk.pal", "LoadBrief:British"},
        {"Germans", "ls800germany.shp", "mplsg.pal", "LoadBrief:Germans"},
        {"Germany", "ls800germany.shp", "mplsg.pal", "LoadBrief:Germans"},
        {"French", "ls800france.shp", "mplsf.pal", "LoadBrief:French"},
        {"France", "ls800france.shp", "mplsf.pal", "LoadBrief:French"},
        {"Alliance", "ls800korea.shp", "mplsk.pal", "LoadBrief:Korea"},
        {"Korea", "ls800korea.shp", "mplsk.pal", "LoadBrief:Korea"},
        {"Russians", "ls800russia.shp", "mplsr.pal", "LoadBrief:Russia"},
        {"Russia", "ls800russia.shp", "mplsr.pal", "LoadBrief:Russia"},
        {"Confederation", "ls800cuba.shp", "mplsc.pal", "LoadBrief:Cuba"},
        {"Cuba", "ls800cuba.shp", "mplsc.pal", "LoadBrief:Cuba"},
        {"Africans", "ls800libya.shp", "mplsl.pal", "LoadBrief:Lybia"},
        {"Libya", "ls800libya.shp", "mplsl.pal", "LoadBrief:Lybia"},
        {"Lybia", "ls800libya.shp", "mplsl.pal", "LoadBrief:Lybia"},
        {"Arabs", "ls800iraq.shp", "mplsi.pal", "LoadBrief:Iraq"},
        {"Iraq", "ls800iraq.shp", "mplsi.pal", "LoadBrief:Iraq"},
        {"Yuri", "ls800yuri.shp", "mpyls.pal", "LoadBrief:YuriCountry"},
        {"YuriCountry", "ls800yuri.shp", "mpyls.pal", "LoadBrief:YuriCountry"},
        {"Observer", "ls800obs.shp", "mplsobs.pal", ""},
        {"Obs", "ls800obs.shp", "mplsobs.pal", ""},
        {"Random", "ls800obs.shp", "mplsobs.pal", ""}
    };
    bool loadingAliasesValid = true;
    bool loadingBriefsValid = true;
    bool loadingCountryAssetsValid = true;
    int loadingCountryAssetsChecked = 0;
    std::set<std::pair<std::string, std::string>> checkedLoadingAssets;
    std::string loadingError;
    for (const LoadingAlias& alias : loadingAliases) {
        loadingAliasesValid = loadingAliasesValid
            && runtimeLoadingScreenAssetForCountry(alias.country) == alias.image
            && runtimeLoadingPaletteAssetForCountry(alias.country)
                == alias.palette
            && runtimeLoadingBriefKeyForCountry(alias.country) == alias.brief;
        if (alias.brief[0] != '\0') {
            loadingBriefsValid = loadingBriefsValid
                && !state.csf.get(alias.brief, L"").empty();
        }
        if (!checkedLoadingAssets.emplace(alias.image, alias.palette).second) {
            continue;
        }
        std::vector<RuntimePaletteColor> aliasPalette;
        std::string aliasPaletteSource;
        std::string aliasError;
        AssetImage aliasImage;
        const bool aliasAssetValid = state.shellResources.loadPalette(

                alias.palette,
                aliasPalette,
                aliasPaletteSource,
                aliasError
            )
            && loadResourceAsset(
                state.shellResources,
                alias.image,
                0,
                aliasPalette,
                false,
                aliasImage
            )
            && aliasImage.width == 800
            && aliasImage.height == 600
            && lowerAscii(aliasImage.source).find(".mix!")
                != std::string::npos;
        loadingCountryAssetsValid =
            loadingCountryAssetsValid && aliasAssetValid;
        ++loadingCountryAssetsChecked;
        if (!aliasAssetValid && loadingError.empty()) {
            loadingError = aliasError.empty()
                ? std::string("country loading resource pair failed: ")
                    + alias.image + "/" + alias.palette
                : aliasError;
        }
    }
    std::vector<RuntimePaletteColor> loadingPalette;
    std::string loadingPaletteSource;
    const bool loadingPaletteValid = state.shellResources.loadPalette(

        runtimeLoadingPaletteAssetForCountry(state.selection.playerCountry),
        loadingPalette,
        loadingPaletteSource,
        loadingError
    );
    AssetImage loadingImage;
    const bool loadingImageValid = loadingPaletteValid
        && loadResourceAsset(
            state.shellResources,
            state.selection.loadingScreenAssetId,
            0,
            loadingPalette,
            false,
            loadingImage
        )
        && loadingImage.width == 800
        && loadingImage.height == 600
        && lowerAscii(loadingImage.source).find(".mix!") != std::string::npos;
    const bool loadingWritten = loadingImageValid && writeBgraBmp(
        pathString(loadingPath),
        loadingImage.bgra.data(),
        loadingImage.width,
        loadingImage.height,
        error
    );
    const bool loadingValid = loadingAliasesValid
        && loadingBriefsValid && loadingCountryAssetsValid
        && loadingImageValid && loadingWritten
        && !state.selection.playerCountryDisplayName.empty()
        && !state.selection.playerCountryBrief.empty();

    // A map is selectable only when its own Preview/PreviewPack can be
    // decoded.  Exercising every discovered entry prevents the old lazy-cache
    // bug where 2 Peaks happened to work while changing the selection left
    // the preview pane blank for the remaining maps.
    int mapPreviewsDecoded = 0;
    std::vector<std::string> failedMapPreviews;
    for (std::size_t mapIndex = 0; mapIndex < state.maps.size(); ++mapIndex) {
        const bool decoded = ensurePreview(
            state,
            static_cast<int>(mapIndex)
        );
        const MapInfo& map = state.maps[mapIndex];
        if (decoded && map.preview.width > 0 && map.preview.height > 0
            && !map.preview.pixels.empty()) {
            ++mapPreviewsDecoded;
            continue;
        }
        if (failedMapPreviews.size() < 8U) {
            failedMapPreviews.push_back(
                map.id + ":" + map.previewError
            );
        }
    }
    const bool allMapPreviewsValid = !state.maps.empty()
        && mapPreviewsDecoded == static_cast<int>(state.maps.size());

    state.page = SkirmishPage::MapSelection;
    state.backDirty = true;
    const bool mapRendered = renderShellBackBuffer(state);
    const std::array<HitKind, 6> mapHits{{
        HitKind::Mode,
        HitKind::Map,
        HitKind::MapScrollUp,
        HitKind::MapScrollDown,
        HitKind::UseMap,
        HitKind::CancelMap
    }};
    bool mapHitData = true;
    for (const HitKind kind : mapHits) {
        mapHitData = mapHitData && hasHitKind(state, kind);
    }
    const bool mapWritten = writeBgraBmp(
        pathString(mapPath), state.composeFrame.bytes().data(),
        kBaseWidth, kBaseHeight, error
    );
    ReleaseDC(nullptr, screenDc);

    if (!configuredSpeedApplied
        || !configuredCreditsApplied
        || !configuredComputerAiApplied
        || !crateOptionValid || !noCooldownOptionValid || !observerOptionValid
        || !lobbyRendered || !lobbyHitData || !lobbyWritten
        || !dropdownsValid || !slidersValid
        || !computerAiToggleValid || !computerAiToggleRestored
        || !loadingValid
        || !allMapPreviewsValid
        || !mapRendered || !mapHitData || !mapWritten) {
        std::cerr << "Skirmish shell smoke failed: crates=" << crateOptionValid << " lobby="
                  << (lobbyRendered && lobbyHitData && lobbyWritten ? "yes" : "no")
                  << " dropdowns=" << (dropdownsValid ? "yes" : "no")
                  << " sliders=" << (slidersValid ? "yes" : "no")
                  << " computerAiToggle="
                  << (computerAiToggleValid && computerAiToggleRestored
                        ? "yes" : "no")
                  << " configuredSpeed="
                  << (configuredSpeedApplied ? "yes" : "no")
                  << " configuredCredits="
                  << (configuredCreditsApplied ? "yes" : "no")
                  << " configuredComputerAi="
                  << (configuredComputerAiApplied ? "yes" : "no")
                  << " originalLoading=" << (loadingValid ? "yes" : "no")
                  << " allMapPreviews="
                  << mapPreviewsDecoded << "/" << state.maps.size()
                  << " mapSelection="
                  << (mapRendered && mapHitData && mapWritten ? "yes" : "no")
                  << " error=" << (loadingError.empty() ? error : loadingError)
                  << " previewFailures=";
        for (const std::string& failure : failedMapPreviews) {
            std::cerr << " [" << failure << "]";
        }
        std::cerr
                  << "\n";
        return 1;
    }
    std::string bitmapTextTraceError;
    if (!writeSkirmishBitmapTextTrace(fs::path(bmpPath).parent_path() / "bitmap-text.trace.bin",
            state.gameFont.bytes, bitmapTextTrace, bitmapTextTraceError)) {
        std::cerr << "Skirmish bitmap text evidence failed: " << bitmapTextTraceError << "\n";
        return 1;
    }
    std::cout << "Skirmish shell smoke: observer=select/starts/team/AI/network/coop noCooldown=click/label/accepted/save/reopen/network-off crates=click/label/accepted/save/reopen/on/off maps=" << state.maps.size()
              << " allPreviews=" << mapPreviewsDecoded
              << " lobbyVisiblePixels=" << lobbyVisiblePixels
              << " dropdownVisiblePixels=" << dropdownVisiblePixels
              << " dropdownOutsideDifferences=" << dropdownOutsideDifferences
              << " preview="
              << state.maps[static_cast<std::size_t>(state.selectedMap)].preview.width
              << "x"
              << state.maps[static_cast<std::size_t>(state.selectedMap)].preview.height
              << " lobbyBmp=" << lobbyPath
              << " countryDropdownBmp=" << pathString(countryDropdownPath)
              << " loadingBmp=" << pathString(loadingPath)
              << " loadingSource=" << loadingImage.source
              << " loadingCountryPairs=" << loadingCountryAssetsChecked
              << " mapSelectionBmp=" << pathString(mapPath)
              << "\n";
    return 0;
}
