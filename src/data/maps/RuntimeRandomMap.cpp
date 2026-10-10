#include "data/maps/RuntimeRandomMap.h"

#include "core/io/IniFile.h"
#include "core/io/RuntimeAtomicFile.h"
#include "runtime/campaign/RuntimeCampaignDataSource.h"
#include "runtime/host/RuntimeShell.h"
#include "data/art/TheaterDatabase.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
constexpr int grid = 512;
constexpr double pi = 3.14159265358979323846;
int key(int x, int y) { return y * grid + x; }
bool fail(std::string& error, const std::string& message) { error = message; return false; }
std::string pathText(const fs::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::uint32_t hash(std::uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352dU;
    value ^= value >> 15; value *= 0x846ca68bU;
    return value ^ (value >> 16);
}
struct Random {
    std::uint32_t state;
    std::uint32_t next() { state += 0x9e3779b9U; return hash(state); }
    int between(int low, int high) {
        return low + static_cast<int>(next() % static_cast<unsigned int>(high - low + 1));
    }
};
int noise(int x, int y, int scale, std::uint32_t seed) {
    const int ax = x / scale, ay = y / scale;
    const int rx = x % scale, ry = y % scale;
    const auto at = [&](int a, int b) {
        return static_cast<int>(hash(seed ^ (static_cast<std::uint32_t>(a) * 73856093U)
            ^ (static_cast<std::uint32_t>(b) * 19349663U)) % 2001U) - 1000;
    };
    const int top = at(ax, ay) * (scale - rx) + at(ax + 1, ay) * rx;
    const int bottom = at(ax, ay + 1) * (scale - rx) + at(ax + 1, ay + 1) * rx;
    return (top * (scale - ry) + bottom * ry) / (scale * scale);
}
int distance2(int x, int y, int a, int b) {
    return (x - a) * (x - a) + (y - b) * (y - b);
}
int number(const std::string& value, int fallback) {
    int result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc() && parsed.ptr == value.data() + value.size()
        ? result : fallback;
}
std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> result;
    std::istringstream input(text); std::string part;
    while (std::getline(input, part, separator)) result.push_back(part);
    return result;
}
int setting(const IniFile& ini, const std::string& name, int index, int fallback) {
    const auto entries = split(ini.getValue("General", name), ',');
    return index < static_cast<int>(entries.size()) ? number(entries[index], fallback) : fallback;
}
void u16(Bytes& output, unsigned int value) {
    output.push_back(static_cast<unsigned char>(value));
    output.push_back(static_cast<unsigned char>(value >> 8));
}
void u32(Bytes& output, unsigned int value) { u16(output, value); u16(output, value >> 16); }
std::string base64(const Bytes& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text; text.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int bits = (bytes[i] << 16)
            | (i + 1 < bytes.size() ? bytes[i + 1] << 8 : 0)
            | (i + 2 < bytes.size() ? bytes[i + 2] : 0);
        text += alphabet[(bits >> 18) & 63]; text += alphabet[(bits >> 12) & 63];
        text += i + 1 < bytes.size() ? alphabet[(bits >> 6) & 63] : '=';
        text += i + 2 < bytes.size() ? alphabet[bits & 63] : '=';
    }
    return text;
}
// Format5 uses independent <=8192-byte LZO streams. A literal stream is legal
// LZO1X and avoids introducing a platform-dependent compression dependency.
Bytes format5(const Bytes& raw) {
    Bytes packed;
    for (std::size_t at = 0; at < raw.size(); at += 8192) {
        const std::size_t count = std::min<std::size_t>(8192, raw.size() - at);
        Bytes chunk;
        if (count <= 238) chunk.push_back(static_cast<unsigned char>(17 + count));
        else {
            chunk.push_back(0); std::size_t extra = count - 18;
            while (extra > 255) { chunk.push_back(0); extra -= 255; }
            chunk.push_back(static_cast<unsigned char>(extra));
        }
        chunk.insert(chunk.end(), raw.begin() + at, raw.begin() + at + count);
        chunk.insert(chunk.end(), {17, 0, 0});
        u16(packed, static_cast<unsigned int>(chunk.size()));
        u16(packed, static_cast<unsigned int>(count));
        packed.insert(packed.end(), chunk.begin(), chunk.end());
    }
    return packed;
}
Bytes format80(const Bytes& raw) {
    Bytes packed;
    for (std::size_t begin = 0; begin < raw.size(); begin += 8192) {
        const std::size_t end = std::min(raw.size(), begin + 8192); Bytes chunk;
        for (std::size_t at = begin; at < end;) {
            std::size_t run = 1;
            while (at + run < end && raw[at + run] == raw[at]) ++run;
            if (run >= 4) {
                chunk.push_back(0xfe); u16(chunk, static_cast<unsigned int>(run));
                chunk.push_back(raw[at]); at += run;
            } else {
                const std::size_t first = at;
                do {
                    ++at;
                    if (at >= end || at - first == 63) break;
                    if (at + 3 < end && raw[at] == raw[at + 1]
                        && raw[at] == raw[at + 2] && raw[at] == raw[at + 3]) break;
                } while (true);
                chunk.push_back(static_cast<unsigned char>(0x80 | (at - first)));
                chunk.insert(chunk.end(), raw.begin() + first, raw.begin() + at);
            }
        }
        chunk.push_back(0x80);
        u16(packed, static_cast<unsigned int>(chunk.size()));
        u16(packed, static_cast<unsigned int>(end - begin));
        packed.insert(packed.end(), chunk.begin(), chunk.end());
    }
    return packed;
}
void packedSection(std::ostream& out, const char* name, const Bytes& bytes) {
    const std::string encoded = base64(bytes);
    out << '\n' << '[' << name << "]\n";
    for (std::size_t at = 0; at < encoded.size(); at += 70)
        out << at / 70 + 1 << '=' << encoded.substr(at, 70) << '\n';
}
struct Cell {
    int tile = 0, subTile = 0, level = 0;
    bool valid = false, land = true, shore = false, blocked = false;
};
int tileBase(const TheaterDatabase& theater, const char* keyName) {
    const int set = theater.getGeneralTileSetIndex(keyName);
    for (const auto& type : theater.getTileSets())
        if (type.index == set && type.tilesInSet > 0) return type.startTileIndex;
    return -1;
}
std::string lower(std::string value) {
    for (char& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return value;
}
} // namespace

bool runtimeValidateRandomMapOptions(const RuntimeRandomMapOptions& o, std::string& error) {
    if (o.environment < 0 || o.environment > 4 || o.theater < 0 || o.theater > 1
        || o.size < 0 || o.size > 3 || o.resources < 0 || o.resources > 3
        || o.timeOfDay < 0 || o.timeOfDay > 3 || o.players < 2 || o.players > 8)
        return fail(error, "随机地图参数超出可用范围。");
    if (o.name.size() > 60 || o.name.find_first_of("\r\n\t;[]=\\/\"") != std::string::npos)
        return fail(error, "地图名称最多 20 个汉字，请勿使用换行或特殊分隔符。");
    for (unsigned char c : o.name) if (c < 32) return fail(error, "地图名称含有无效字符。");
    error.clear(); return true;
}
int runtimeRandomMapDimension(const RuntimeRandomMapOptions& o) {
    constexpr std::array<int, 4> sizes{64, 80, 100, 128};
    return std::max(sizes[std::clamp(o.size, 0, 3)], o.players >= 6 ? 100 : o.players >= 4 ? 80 : 64);
}
std::string runtimeRandomMapFileName(const RuntimeRandomMapOptions& o) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "rmg1_" << std::hex << std::setfill('0') << std::setw(8) << o.seed << '_'
        << o.environment << o.theater << o.size << o.resources << o.timeOfDay << o.players
        << '_' << lower(iniHexEncode(o.name)) << ".map";
    return out.str();
}
bool runtimeRandomMapOptionsFromFileName(const std::string& name, RuntimeRandomMapOptions& o) {
    if (name != lower(name) || name.size() > 192 || !name.starts_with("rmg1_")
        || !name.ends_with(".map")) return false;
    const auto parts = split(name.substr(0, name.size() - 4), '_');
    // std::getline omits the empty final name field.
    if ((parts.size() != 3 && parts.size() != 4) || parts[1].size() != 8 || parts[2].size() != 6)
        return false;
    RuntimeRandomMapOptions candidate;
    const auto parsed = std::from_chars(parts[1].data(), parts[1].data() + 8, candidate.seed, 16);
    if (parsed.ec != std::errc() || parsed.ptr != parts[1].data() + 8) return false;
    candidate.environment = parts[2][0] - '0'; candidate.theater = parts[2][1] - '0';
    candidate.size = parts[2][2] - '0'; candidate.resources = parts[2][3] - '0';
    candidate.timeOfDay = parts[2][4] - '0'; candidate.players = parts[2][5] - '0';
    if (parts.size() == 4 && !iniHexDecode(parts[3], candidate.name, 60)) return false;
    std::string error;
    if (!runtimeValidateRandomMapOptions(candidate, error) || runtimeRandomMapFileName(candidate) != name)
        return false;
    o = std::move(candidate); return true;
}
fs::path runtimeSavedRandomMapDirectory(const RuntimeConfig& config) {
    return fs::path(config.localDataRootPath) / "random_maps";
}
fs::path runtimeRandomMapCacheDirectory(const RuntimeConfig& config) {
    return fs::path(config.localDataRootPath) / "runtime_map_cache" / "random_maps";
}

bool runtimeGenerateRandomMap(const RuntimeConfig& config, const RuntimeRandomMapOptions& options,
    RuntimeRandomMapResult& result, std::string& error) {
    if (!runtimeValidateRandomMapOptions(options, error)) return false;
    const RuntimeCampaignDataSourceOptions sourceOptions{
        config.resourceRootPath, config.localDataRootPath, config.modRootPath, "yr"};
    IniFile theaterIni, rulesIni, rmg;
    const std::string climate = options.theater == 0 ? "TEMPERATE" : "SNOW";
    const auto source = resolveRuntimeCampaignTheaterSource(sourceOptions, climate);
    if (!source.load(theaterIni, &error)
        || !resolveRuntimeCampaignIniSource(sourceOptions, RuntimeCampaignIniKind::Rules).load(rulesIni, &error))
        return false;
    TheaterDatabase theater; RulesDatabase rules;
    if (!theater.loadFromIni(theaterIni, source.displayPath) || !rules.loadFromIni(rulesIni))
        return fail(error, "无法读取原版地形或单位规则。");
    const fs::path exports = fs::path(config.localDataRootPath) / "resources" / "mix_exports";
    std::vector<fs::path> rmgPaths;
    if (!config.modRootPath.empty()) rmgPaths.emplace_back(fs::path(config.modRootPath) / "rmgmd.ini");
    rmgPaths.insert(rmgPaths.end(), {fs::path(config.localDataRootPath) / "rmgmd.ini",
        exports / "expandmd01" / "rmgmd.ini", exports / "ra2md" / "localmd" / "rmgmd.ini"});
    for (const auto& path : rmgPaths) { std::error_code ec;
        if (fs::is_regular_file(path, ec) && rmg.loadFromFile(pathText(path))) break;
    }
    const int clear = tileBase(theater, "ClearTile"), water = tileBase(theater, "WaterSet");
    const int shore = tileBase(theater, "ShorePieces"), ramp = tileBase(theater, "RampBase");
    const int green = tileBase(theater, "GreenTile"), greenLat = tileBase(theater, "ClearToGreenLat");
    const int rough = tileBase(theater, "RoughTile"), roughLat = tileBase(theater, "ClearToRoughLat");
    if (clear < 0 || water < 0 || shore < 0 || ramp < 0)
        return fail(error, "此气候缺少随机地图所需的原版地形。");
    int gold = -1, gem = -1;
    for (const auto& overlay : rules.getOverlayTypes()) {
        if (gold < 0 && overlay.ore && !overlay.gem && overlay.listIndex < 255) gold = overlay.listIndex;
        if (gem < 0 && overlay.gem && overlay.listIndex < 255) gem = overlay.listIndex;
    }
    if (gold < 0) return fail(error, "原版矿石覆盖层未找到。");

    RuntimeRandomMapResult map; map.options = options; map.fileName = runtimeRandomMapFileName(options);
    map.width = map.height = runtimeRandomMapDimension(options);
    const int w = map.width, h = map.height, limit = w + h + 3;
    std::vector<Cell> cells(grid * grid);
    std::vector<unsigned char> nodes(grid * grid, 0), heights(grid * grid, 0);
    Bytes overlays(grid * grid, 255), amounts(grid * grid, 0);
    Random random{options.seed};
    const auto inside = [&](int x, int y, int margin = 0) {
        const int u = x - y + w, v = x + y - w;
        return x > 0 && y > 0 && x < grid - 1 && y < grid - 1
            && u >= margin && u < 2 * w - margin && v >= margin + 1 && v < 2 * h - margin;
    };
    for (int i = 0; i < options.players; ++i) {
        double u = 0, v = 0;
        if (options.environment == 2) {
            const int sideCount = (options.players + (i % 2 == 0 ? 1 : 0)) / 2;
            u = i % 2 == 0 ? -0.54 : 0.54;
            v = sideCount == 1 ? 0 : -0.62 + 1.24 * (i / 2) / (sideCount - 1);
        } else {
            const double angle = (2.0 * pi * i / options.players) + pi / 4;
            u = std::cos(angle) * 0.70; v = std::sin(angle) * 0.70;
        }
        const int sx = std::clamp(static_cast<int>(std::lround((w + h + u * w + v * h) / 2)), 4, limit - 4);
        const int sy = std::clamp(static_cast<int>(std::lround((w + h - u * w + v * h) / 2)), 4, limit - 4);
        map.starts.push_back({sx, sy, sy * 1000 + sx, true});
    }
    const auto nearStart = [&](int x, int y, int radius) {
        return std::any_of(map.starts.begin(), map.starts.end(), [&](const auto& p) {
            return distance2(x, y, p.x, p.y) <= radius * radius;
        });
    };
    // A shared land route is retained in the three land-connected environments.
    const auto corridor = [&](int x, int y, int radius) {
        if (options.environment == 0 || options.environment == 2) return false;
        const double cx = (w + h) / 2.0, cy = cx;
        for (const auto& p : map.starts) {
            const double dx = p.x - cx, dy = p.y - cy;
            const double t = std::clamp(((x - cx) * dx + (y - cy) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
            const double ax = x - cx - t * dx, ay = y - cy - t * dy;
            if (ax * ax + ay * ay <= radius * radius) return true;
        }
        return false;
    };
    for (int y = 0; y < limit; ++y) for (int x = 0; x < limit; ++x) {
        const int u = (x - y) * 1000 / w, v = (x + y - w - h) * 1000 / h;
        const int n = noise(x, y, 10, options.seed);
        bool land = true;
        if (options.environment == 0) land = nearStart(x, y, 16)
            || (n > 650 && std::abs(u) < 750 && std::abs(v) < 750);
        else if (options.environment == 1) land = u * u + v * v < 820000 + n * 200;
        else if (options.environment == 2) {
            const int du = std::abs(u) - 540;
            land = du * du * 6 + v * v < 680000 + n * 90 && std::abs(u) > 145;
        } else if (options.environment == 3) land = n < 650 || std::abs(u) > 750 || std::abs(v) > 750;
        if (nearStart(x, y, 15) || corridor(x, y, 5)) land = true;
        nodes[key(x, y)] = static_cast<unsigned char>(land);
    }
    // Marching-square shore pieces are complete 2x2 TMPs. Remove ambiguous
    // diagonal crossings instead of cutting an original tile into fragments.
    for (int y = 0; y + 1 < limit; ++y) for (int x = 0; x + 1 < limit; ++x) {
        const int mask = nodes[key(x, y)] | nodes[key(x + 1, y)] << 1
            | nodes[key(x, y + 1)] << 2 | nodes[key(x + 1, y + 1)] << 3;
        if (mask == 6 || mask == 9) nodes[key(x, y)] = nodes[key(x + 1, y)];
    }
    // Land-state masks of the original Shore01..40 TEM/SNO templates.
    constexpr std::array<int, 16> shores{-1, 6, 30, 0, 14, 8, -1, 32, 22, -1, 24, 38, 16, 34, 36, -1};
    for (int by = 0; by < limit; by += 2) for (int bx = 0; bx < limit; bx += 2) {
        const int mask = nodes[key(bx, by)] | nodes[key(bx + 2, by)] << 1
            | nodes[key(bx, by + 2)] << 2 | nodes[key(bx + 2, by + 2)] << 3;
        const int resolvedMask = mask == 6 || mask == 9 ? 15 : mask;
        const int variant = static_cast<int>(hash(options.seed ^ key(bx, by)) % 2);
        for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
            const int x = bx + dx, y = by + dy; if (!inside(x, y)) continue;
            Cell& cell = cells[key(x, y)]; cell.valid = true;
            cell.land = (resolvedMask & (1 << (dy * 2 + dx))) != 0;
            cell.shore = resolvedMask != 0 && resolvedMask != 15;
            cell.tile = cell.shore ? shore + shores[resolvedMask] + variant : cell.land ? clear : water + variant;
            cell.subTile = cell.shore || !cell.land ? dy * 2 + dx : 0;
        }
    }
    // Elevation is built from shared corner heights; every cell uses a real
    // ramp tile. Starts and the connecting routes remain flat and buildable.
    if (options.environment == 4 || options.environment == 3) {
        const int hills = options.environment == 4 ? w / 2 : w / 12;
        for (int i = 0; i < hills; ++i) {
            const int cx = random.between(12, limit - 12), cy = random.between(12, limit - 12);
            const int radius = random.between(6, 12), peak = options.environment == 4 ? random.between(4, 7) : 2;
            if (!inside(cx, cy, 14) || nearStart(cx, cy, 22) || corridor(cx, cy, 12)) continue;
            for (int y = cy - radius; y <= cy + radius; ++y) for (int x = cx - radius; x <= cx + radius; ++x) {
                if (!inside(x, y, 6) || nearStart(x, y, 16) || corridor(x, y, 5)) continue;
                bool dry = true;
                for (int dy = -1; dy <= 0; ++dy) for (int dx = -1; dx <= 0; ++dx) {
                    const auto& adjacent = cells[key(x+dx,y+dy)];
                    dry = dry && adjacent.valid && adjacent.land && !adjacent.shore;
                }
                if (!dry) continue;
                const int value = std::clamp(radius - std::max(std::abs(x - cx), std::abs(y - cy)), 0, peak);
                heights[key(x, y)] = static_cast<unsigned char>(std::max<int>(heights[key(x, y)], value));
            }
        }
    }
    // Adjacent corner heights cannot differ by more than one level, including
    // at the protected base/route margins. Lowering only preserves that margin.
    for (int pass = 0; pass < 8; ++pass) for (int y = 1; y + 1 < limit; ++y) for (int x = 1; x + 1 < limit; ++x) {
        int height = heights[key(x, y)];
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx)
            height = std::min(height, heights[key(x + dx, y + dy)] + 1);
        heights[key(x, y)] = static_cast<unsigned char>(height);
        const std::array<int,4> corners{heights[key(x,y)],heights[key(x+1,y)],
            heights[key(x+1,y+1)],heights[key(x,y+1)]};
        const int base = *std::min_element(corners.begin(),corners.end());
        int mask = 0; for (int i=0;i<4;++i) if (corners[i]>base) mask |= 1<<i;
        if (mask==5) heights[key(x,y)]=static_cast<unsigned char>(base);
        if (mask==10) heights[key(x+1,y)]=static_cast<unsigned char>(base);
    }
    const auto groundPatch = [&](int x,int y) {
        const auto& cell=cells[key(x,y)];
        if (!cell.valid || !cell.land || cell.shore || nearStart(x,y,7)
            || heights[key(x,y)] || heights[key(x+1,y)] || heights[key(x,y+1)] || heights[key(x+1,y+1)]) return 0;
        const int value=noise(x,y,8,options.seed^441U);
        if (options.theater==0 && value>300) return 1;
        return value < -350 ? 2 : 0;
    };
    constexpr std::array<int, 16> ramps{-1, 6, 7, 3, 4, 4, 0, 11, 5, 2, 5, 10, 1, 9, 8, -1};
    for (int y = 1; y + 1 < limit; ++y) for (int x = 1; x + 1 < limit; ++x) {
        Cell& cell = cells[key(x, y)]; if (!cell.valid || !cell.land || cell.shore) continue;
        const std::array<int, 4> corners{heights[key(x, y)], heights[key(x + 1, y)],
            heights[key(x + 1, y + 1)], heights[key(x, y + 1)]};
        const int base = *std::min_element(corners.begin(), corners.end());
        int mask = 0; for (int i = 0; i < 4; ++i) if (corners[i] > base) mask |= 1 << i;
        cell.level = base;
        if (mask != 0) { cell.tile = ramp + ramps[mask]; cell.subTile = 0; }
        else if (cell.level == 0) {
            const int patch=groundPatch(x,y);
            if (patch==1 && green>=0) cell.tile=green;
            else if (patch==2 && rough>=0) cell.tile=rough;
            else {
                // Retail LAT bits describe the clear adjoining edges, in
                // NE, SE, SW, NW order. The transition belongs to the clear
                // cell beside a patch, not to every cell within the patch.
                constexpr std::array<std::pair<int,int>,4> delta{{{0,-1},{1,0},{0,1},{-1,0}}};
                for (int adjacentPatch=1;adjacentPatch<=2;++adjacentPatch) {
                    const int baseTile=adjacentPatch==1?green:rough,lat=adjacentPatch==1?greenLat:roughLat;
                    if (baseTile<0 || lat<0) continue;
                    int neighbors=0;
                    for (int i=0;i<4;++i) if (groundPatch(x+delta[i].first,y+delta[i].second)==adjacentPatch) neighbors|=1<<i;
                    if (!neighbors) continue;
                    cell.tile=neighbors==15?baseTile:lat+(15-neighbors);
                    break;
                }
            }
        }
        if (cell.level > 0 || mask != 0) ++map.raisedCells;
    }
    std::ostringstream structures, terrainObjects;
    int structureId = 0;
    const auto structure = [&](const std::string& id, int x, int y) {
        structures << structureId++ << "=Neutral," << id << ",256," << x << ',' << y
            << ",0,None,1,0,0,0,0,None,None,None,0,0\n";
        ++map.buildings;
    };
    const std::string prefix = options.theater == 0 ? "Temperate" : "Snow";
    const auto authoredLamps = split(rmg.getValue("General", prefix + "OrePatchLamps"), ',');
    constexpr std::array<const char*, 4> temLamps{"TEMMORLAMP","TEMDAYLAMP","TEMDUSLAMP","TEMNITLAMP"};
    constexpr std::array<const char*, 4> snoLamps{"SNOMORLAMP","SNODAYLAMP","SNODUSLAMP","SNONITLAMP"};
    const std::string lamp = options.timeOfDay < static_cast<int>(authoredLamps.size())
        ? authoredLamps[options.timeOfDay] : (options.theater == 0 ? temLamps : snoLamps)[options.timeOfDay];
    const int minOre = std::clamp(setting(rmg, "RMGMinimumTiberium", 0, 900), 100, 10000);
    const int maxOre = std::clamp(setting(rmg, "RMGMaximumTiberium", 0, 1050), minOre, 10000);
    const int oreBudget = random.between(minOre, maxOre) * std::array<int,4>{65,100,150,220}[options.resources] / 100;
    for (const auto& start : map.starts) {
        const int dx = start.x < (w + h) / 2 ? 1 : -1;
        const int cx = start.x + dx * 10, cy = start.y;
        int remaining = oreBudget;
        // Sort by distance for a compact mine. Every player receives the same
        // budget, and ore never consumes the clear construction-yard footprint.
        std::vector<std::pair<int,int>> spots;
        for (int y = start.y - 18; y <= start.y + 18; ++y) for (int x = start.x - 18; x <= start.x + 18; ++x) {
            const Cell& cell = cells[key(x, y)];
            if (cell.valid && cell.land && !cell.shore && cell.level == 0 && !nearStart(x, y, 6))
                spots.emplace_back(distance2(x, y, cx, cy), key(x, y));
        }
        std::sort(spots.begin(), spots.end());
        for (const auto& [distance, cellKey] : spots) {
            (void)distance;
            if (remaining <= 0) break;
            if (overlays[cellKey] != 255) continue;
            const int quantity = std::min(11, remaining);
            overlays[cellKey] = static_cast<unsigned char>(gold);
            amounts[cellKey] = static_cast<unsigned char>(quantity - 1);
            remaining -= quantity; ++map.oreCells;
        }
        if (remaining > 0) return fail(error, "出生区域的矿区空间不足，请重新生成或选择更大的地图。");
        if (rules.findBuildingType(lamp) != nullptr) structure(lamp, cx, cy);
    }
    // Extra contested minerals, including gems at the higher resource levels.
    for (int i = 0; i < options.players * options.resources * 2; ++i) {
        const int x = random.between(6, limit - 6), y = random.between(6, limit - 6);
        Cell& cell = cells[key(x,y)];
        if (!cell.valid || !cell.land || cell.shore || cell.level != 0 || nearStart(x,y,18)) continue;
        for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
            const int k = key(x+dx,y+dy); const Cell& candidate = cells[k];
            if (!candidate.valid || !candidate.land || candidate.shore || candidate.level || overlays[k] != 255) continue;
            overlays[k] = static_cast<unsigned char>(options.resources >= 2 && gem >= 0 && i % 5 == 0 ? gem : gold);
            amounts[k] = 7; ++map.oreCells;
        }
    }
    const int vegetationMin = std::clamp(setting(rmg, "RMGVegetationMinimums", options.environment, 60), 0, 100);
    const int vegetationMax = std::clamp(setting(rmg, "RMGVegetationMaximums", options.environment, 100), vegetationMin, 100);
    const int treeLimit = std::clamp(setting(rmg, "MaxTrees", 0, 600), 0, 5000);
    const int treeTarget = std::min(treeLimit, w * h * random.between(vegetationMin, vegetationMax) / 1600);
    for (int attempt = 0; attempt < treeTarget * 30 && map.trees < treeTarget; ++attempt) {
        const int x = random.between(5, limit - 5), y = random.between(5, limit - 5), k = key(x,y);
        Cell& cell = cells[k];
        if (!cell.valid || !cell.land || cell.shore || cell.blocked || overlays[k] != 255
            || nearStart(x,y,10) || corridor(x,y,4)) continue;
        const int typeIndex = random.between(1, 12);
        const std::string type = "TREE" + std::string(typeIndex < 10 ? "0" : "") + std::to_string(typeIndex);
        if (rules.findTerrainType(type) == nullptr) continue;
        terrainObjects << y * 1000 + x << '=' << type << '\n'; cell.blocked = true; ++map.trees;
    }
    const bool cities = lower(rmg.getValue("General", "PlaceUrbanAreas", "yes")) != "no";
    if (cities) for (int attempt = 0, placed = 0; attempt < 500 && placed < options.players; ++attempt) {
        const int x = random.between(10, limit - 10), y = random.between(10, limit - 10);
        if (!inside(x,y,15) || nearStart(x,y,20) || corridor(x,y,5)) continue;
        const std::string type = options.theater == 0 ? "CAHSE01" : "CAHSE07";
        if (rules.findBuildingType(type) == nullptr) break;
        bool free = true;
        for (int dy = -2; dy <= 3; ++dy) for (int dx = -2; dx <= 3; ++dx) {
            const int k = key(x+dx,y+dy); const Cell& cell = cells[k];
            free = free && cell.valid && cell.land && !cell.shore && !cell.blocked
                && cell.level == 0 && overlays[k] == 255 && cell.tile < ramp;
        }
        if (!free) continue;
        for (int dy = -1; dy <= 2; ++dy) for (int dx = -1; dx <= 2; ++dx) cells[key(x+dx,y+dy)].blocked = true;
        structure(type,x,y); ++placed;
    }
    // Store an ordinary original-format map, including an embedded radar
    // preview. The runtime, save loader and LAN room consume this same file.
    std::ostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(6);
    const std::string displayName = options.name.empty() ? "随机地图 " + std::to_string(options.seed) : options.name;
    out << "; ConcordRTS random map format 1\n[Basic]\nName=" << displayName
        << "\nMultiplayerOnly=yes\nNewINIFormat=4\nOfficial=no\nPercent=0\nGameMode=standard\n"
        << "\n[Map]\nTheater=" << climate << "\nSize=0,0," << w << ',' << h
        << "\nLocalSize=2,4," << w-4 << ',' << h-8 << '\n'
        << "\n[Header]\nName=" << displayName << "\nMinPlayer=2\nMaxPlayer=" << options.players
        << "\nNumberStartingPoints=" << options.players << "\nStartX=0\nStartY=0\nWidth=" << w*2 << "\nHeight=" << h*2 << '\n';
    for (std::size_t i = 0; i < map.starts.size(); ++i) {
        const auto& p = map.starts[i]; out << "Waypoint" << i+1 << '=' << p.x-p.y+w << ',' << p.x+p.y-w << '\n';
    }
    out << "\n[RandomMap]\nVersion=1\nFileName=" << map.fileName << "\nSeed=" << options.seed << '\n';
    out << "\n[Waypoints]\n";
    for (std::size_t i = 0; i < map.starts.size(); ++i) out << i << '=' << map.starts[i].cell << '\n';
    constexpr std::array<int,4> temAmbient{75,100,75,35}, snowAmbient{75,100,75,55};
    constexpr std::array<int,4> temRed{109,108,71,51}, temGreen{80,94,81,46}, temBlue{49,68,119,141};
    constexpr std::array<int,4> snoRed{112,99,106,76}, snoGreen{80,104,107,74}, snoBlue{133,107,154,118};
    const int t = options.timeOfDay;
    const double ambient = setting(rmg, prefix+"AmbientLight", t, (options.theater == 0 ? temAmbient : snowAmbient)[t]) / 100.0;
    const double red = setting(rmg, prefix+"AmbientRed", t, (options.theater == 0 ? temRed : snoRed)[t]) / 100.0;
    const double grn = setting(rmg, prefix+"AmbientGreen", t, (options.theater == 0 ? temGreen : snoGreen)[t]) / 100.0;
    const double blue = setting(rmg, prefix+"AmbientBlue", t, (options.theater == 0 ? temBlue : snoBlue)[t]) / 100.0;
    out << "\n[Lighting]\nAmbient=" << ambient << "\nRed=" << red << "\nGreen=" << grn << "\nBlue=" << blue
        << "\nGround=0\nLevel=" << setting(rmg,"RMGLevelLightSettings",t,3)/100.0 << '\n';
    out << "\n[Structures]\n" << structures.str() << "\n[Terrain]\n" << terrainObjects.str();
    Bytes iso;
    map.preview.width = w*2; map.preview.height = h*2;
    map.preview.pixels.assign(static_cast<std::size_t>(w*h*16), 0);
    const auto paint = [&](int px, int py, std::array<int,3> rgb) {
        if (px < 0 || py < 0 || px >= w*2 || py >= h*2) return;
        const std::size_t p = static_cast<std::size_t>((py*w*2+px)*4);
        map.preview.pixels[p] = static_cast<unsigned char>(std::clamp(static_cast<int>(rgb[2]*blue*ambient),0,255));
        map.preview.pixels[p+1] = static_cast<unsigned char>(std::clamp(static_cast<int>(rgb[1]*grn*ambient),0,255));
        map.preview.pixels[p+2] = static_cast<unsigned char>(std::clamp(static_cast<int>(rgb[0]*red*ambient),0,255));
        map.preview.pixels[p+3] = 255;
    };
    for (int y = 1; y < limit; ++y) for (int x = 1; x < limit; ++x) {
        const Cell& c = cells[key(x,y)]; if (!c.valid) continue;
        u16(iso,x); u16(iso,y); u32(iso,c.tile); iso.push_back(static_cast<unsigned char>(c.subTile));
        iso.push_back(static_cast<unsigned char>(c.level)); iso.push_back(0);
        if (c.shore) ++map.shoreCells;
        if (c.land) ++map.landCells; else ++map.waterCells;
        std::array<int,3> color = !c.land ? std::array<int,3>{25,72,118}
            : options.theater == 0 ? std::array<int,3>{100+c.level*6,98+c.level*5,52+c.level*4}
                : std::array<int,3>{170+c.level*5,183+c.level*4,190+c.level*4};
        if (c.shore && c.land) color = {160,145,100};
        if (c.blocked) color = {44,73,39};
        if (overlays[key(x,y)] != 255) color = overlays[key(x,y)] == gem
            ? std::array<int,3>{175,66,203} : std::array<int,3>{226,181,48};
        const int px=x-y+w, py=x+y-w;
        paint(px,py,color); paint(px-1,py,color);
    }
    u32(iso,0); packedSection(out,"IsoMapPack5",format5(iso));
    packedSection(out,"OverlayPack",format80(overlays)); packedSection(out,"OverlayDataPack",format80(amounts));
    Bytes previewRgb; previewRgb.reserve(map.preview.pixels.size()/4*3);
    for (std::size_t p = 0; p < map.preview.pixels.size(); p += 4) {
        previewRgb.push_back(map.preview.pixels[p+2]); previewRgb.push_back(map.preview.pixels[p+1]); previewRgb.push_back(map.preview.pixels[p]);
        map.preview.pixels[p+3] = 255;
    }
    out << "\n[Preview]\nSize=0,0," << w*2 << ',' << h*2 << '\n';
    packedSection(out,"PreviewPack",format5(previewRgb)); map.iniText = out.str();
    IniFile decoded; MapDatabase database; MapPreviewImage preview; std::string decodeError;
    if (!decoded.loadFromString(map.iniText) || !database.loadFromIni(decoded,&rulesIni)
        || !database.getIsoMapPackSummary().payloadDecoded || !decodeMapPreviewImage(decoded,preview,decodeError))
        return fail(error, "生成地图未能重新读取：" + decodeError);
    result = std::move(map); error.clear(); return true;
}

bool runtimeStoreRandomMap(const RuntimeRandomMapResult& map, const fs::path& directory,
    fs::path& mapPath, std::string& error) {
    if (map.iniText.empty() || map.fileName != runtimeRandomMapFileName(map.options))
        return fail(error,"请先生成地图。");
    std::error_code ec; fs::create_directories(directory,ec);
    if (ec) return fail(error,"无法创建随机地图目录：" + ec.message());
    const fs::path target = directory / map.fileName;
    if (fs::exists(target,ec)) {
        std::ifstream input(target,std::ios::binary);
        const std::string existing((std::istreambuf_iterator<char>(input)),{});
        if (!input || existing != map.iniText) return fail(error,"同名地图内容不同，请换一个地图名称保存。");
    } else if (!runtimeWriteFileAtomically(target,[&](std::ostream& stream,std::string&) {
        stream.write(map.iniText.data(),static_cast<std::streamsize>(map.iniText.size())); return stream.good();
    },error)) return false;
    mapPath = target; error.clear(); return true;
}
bool runtimeLoadRandomMap(const fs::path& path, RuntimeRandomMapResult& result, std::string& error) {
    RuntimeRandomMapOptions options;
    if (!runtimeRandomMapOptionsFromFileName(path.filename().string(),options))
        return fail(error,"这不是已保存的随机地图。");
    std::error_code ec; const auto size = fs::file_size(path,ec);
    if (ec || size > 8U*1024U*1024U) return fail(error,"无法读取随机地图。");
    std::ifstream input(path,std::ios::binary);
    RuntimeRandomMapResult map; map.iniText.assign(std::istreambuf_iterator<char>(input),{});
    IniFile ini; MapDatabase database;
    if (!input || !ini.loadFromString(map.iniText) || !database.loadFromIni(ini)
        || ini.getValue("RandomMap","FileName") != path.filename().string()
        || !database.getIsoMapPackSummary().payloadDecoded || !decodeMapPreviewImage(ini,map.preview,error))
        return fail(error,"保存的随机地图数据不完整。");
    map.options=options; map.fileName=path.filename().string();
    map.width=map.height=runtimeRandomMapDimension(options);
    for (const auto& waypoint : database.getWaypoints()) map.starts.push_back(waypoint.position);
    result=std::move(map); error.clear(); return true;
}

bool runtimeRenameRandomMap(RuntimeRandomMapResult& map, const std::string& name, std::string& error) {
    auto options = map.options; options.name = name;
    if (!runtimeValidateRandomMapOptions(options,error) || map.iniText.empty()) return false;
    std::string text = map.iniText;
    const std::string display = name.empty() ? "随机地图 " + std::to_string(options.seed) : name;
    const auto replaceValue = [&](const std::string& prefix, const std::string& value) {
        const auto start = text.find(prefix);
        if (start == std::string::npos) return false;
        const auto end = text.find('\n',start+prefix.size());
        if (end == std::string::npos) return false;
        text.replace(start+prefix.size(),end-start-prefix.size(),value); return true;
    };
    const std::string fileName = runtimeRandomMapFileName(options);
    if (!replaceValue("[Basic]\nName=",display) || !replaceValue("[Header]\nName=",display)
        || !replaceValue("[RandomMap]\nVersion=1\nFileName=",fileName))
        return fail(error,"此地图的名称记录无法修改。");
    map.options=std::move(options); map.fileName=fileName; map.iniText=std::move(text); error.clear(); return true;
}
