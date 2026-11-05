#include "assets/archive/MixArchive.h"
#include "core/io/IniFile.h"
#include "runtime/campaign/RuntimeCampaignDataSource.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
int checks = 0;
void require(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}
std::string utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
void write(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    require(!!output, "fixture write");
}
void le(std::string& bytes, std::uint32_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(static_cast<char>((value >> (8U * i)) & 255U));
}
using Member = std::pair<std::string, std::string>;
std::string archiveBytes(const std::vector<Member>& members, bool legacy = false, bool checksum = false) {
    std::string header, payload;
    if (!legacy) le(header, checksum ? 0x10000U : 0U, 4);
    le(header, static_cast<std::uint32_t>(members.size()), 2);
    for (const auto& member : members) payload += member.second;
    le(header, static_cast<std::uint32_t>(payload.size()), 4);
    std::uint32_t offset = 0;
    for (const auto& member : members) {
        le(header, mixFileIdFromName(member.first), 4);
        le(header, offset, 4);
        le(header, static_cast<std::uint32_t>(member.second.size()), 4);
        offset += static_cast<std::uint32_t>(member.second.size());
    }
    return header + payload + (checksum ? std::string(20, '\0') : std::string());
}
void mix(const fs::path& path, const std::string& name, const std::string& text) {
    write(path, archiveBytes({{name, text}}));
}
RuntimeIniSource resolve(const RuntimeCampaignDataSourcePaths& paths, bool theater = false) {
    return theater ? resolveRuntimeCampaignTheaterSourceFromPaths(paths, " temperate ")
                   : resolveRuntimeCampaignIniSourceFromPaths(paths, RuntimeCampaignIniKind::Rules);
}
void selected(const RuntimeIniSource& source, const fs::path& path, bool packed, const std::string& text) {
    require(source.available() && source.filePath == path && source.mixEntry == packed, "selected native path/loose-MIX");
    require(source.backingPath == utf8(path), "UTF-8 backing label");
    require(source.displayPath == utf8(path) + (packed ? "!" + source.entryName : ""), "display label unchanged");
    for (const bool reference : {false, true}) {
        RuntimeIniTextReadReferenceScope scope(reference);
        std::string actual = "previous", error = "previous";
        require(source.readText(actual, &error) && actual == text && error.empty(), "native text read exact in both branches");
        IniFile ini;
        require(source.load(ini, &error) && error.empty(), "native parsed consumer");
    }
}

// Frozen ordered candidates, including the historically surprising rule that
// resource loose files win over mod MIX. No enumerator or filesystem sorting.
std::vector<std::pair<fs::path, bool>> candidates(const RuntimeCampaignDataSourcePaths& paths,
                                               const std::string& edition, const std::string& name) {
    std::vector<std::pair<fs::path, bool>> result;
    const bool ra2 = edition == "ra2";
    const std::vector<fs::path> ordinary = ra2 ? std::vector<fs::path>{"", "Game", "ra2/local", "ra2"}
        : std::vector<fs::path>{"", "GameMD", "ra2md/localmd", "localmd"};
    const std::vector<fs::path> local = ra2 ? std::vector<fs::path>{"resources/mix_exports/ra2/local", "resources/mix_exports/ra2"}
        : std::vector<fs::path>{"resources/mix_exports/ra2md/localmd", "resources/mix_exports/expandmd02",
                              "resources/mix_exports/expandmd01", "resources/mix_exports/ra2md", "resources/mix_exports/localmd"};
    const std::vector<fs::path> packed = ra2 ? std::vector<fs::path>{"local.mix", "Game/local.mix", "GameMD/local.mix", "ra2/local.mix"}
        : std::vector<fs::path>{"localmd.mix", "GameMD/localmd.mix", "ra2md/localmd.mix"};
    for (const fs::path& root : {paths.modRootPath, paths.localDataRootPath, paths.resourceRootPath}) {
        for (const auto& suffix : ordinary) result.push_back({root / suffix / name, false});
        if (root == paths.localDataRootPath)
            for (const auto& suffix : local) result.push_back({root / suffix / name, false});
    }
    for (const fs::path& root : {paths.modRootPath, paths.localDataRootPath, paths.resourceRootPath}) {
        for (const auto& suffix : packed) result.push_back({root / suffix, true});
        if (root == paths.localDataRootPath)
            result.push_back({root / "resources/mix_exports" / (ra2 ? "ra2/local.mix" : "ra2md/localmd.mix"), true});
    }
    return result;
}
void searchContract(const fs::path& base) {
    for (const std::string edition : {"ra2", "yr"}) {
        for (const bool theater : {false, true}) {
            const std::string name = theater ? (edition == "ra2" ? "temperat.ini" : "temperatmd.ini")
                                             : (edition == "ra2" ? "rules.ini" : "rulesmd.ini");
            const auto stem = base / (edition + (theater ? "-theater" : "-rules"));
            RuntimeCampaignDataSourcePaths oracle{stem / "resource", stem / "local", stem / "mod", edition};
            const auto order = candidates(oracle, edition, name);
            // Each candidate alone, and each candidate against its successor.
            for (std::size_t i = 0; i < order.size(); ++i) {
                for (const bool competitor : {false, true}) {
                    const auto round = stem / (std::to_string(i) + (competitor ? "-pair" : "-single"));
                    RuntimeCampaignDataSourcePaths paths{round / "resource", round / "local", round / "mod", edition};
                    const auto current = candidates(paths, edition, name);
                    const auto put = [&](std::size_t at, const std::string& text) {
                        if (current[at].second) mix(current[at].first, name, text);
                        else write(current[at].first, text);
                    };
                    const std::string text = "[Priority]\r\nChosen=" + std::to_string(i) + "\r\n";
                    put(i, text);
                    if (competitor && i + 1 < current.size()) put(i + 1, "[Priority]\nChosen=loser\n");
                    selected(resolve(paths, theater), current[i].first, current[i].second, text);
                }
            }
        }
    }
    const auto root = base / "editions";
    RuntimeCampaignDataSourcePaths paths{root / "resource", root / "local", root / "mod", ""};
    write(paths.modRootPath / "rules.ini", "[Edition]\nName=mod-ra2\n");
    mix(paths.resourceRootPath / "localmd.mix", "rulesmd.ini", "[Edition]\nName=resource-yr\n");
    selected(resolve(paths), paths.resourceRootPath / "localmd.mix", true, "[Edition]\nName=resource-yr\n");
    paths.sourceId = " RA2 ";
    selected(resolve(paths), paths.modRootPath / "rules.ini", false, "[Edition]\nName=mod-ra2\n");
    paths.sourceId = "unknown";
    require(!resolve(paths).available(), "unknown edition never falls back");
    paths.sourceId = "yr";
    require(!resolveRuntimeCampaignTheaterSourceFromPaths(paths, "unknown").available(), "unknown theater");
    paths = {base / "fallback", {}, {}, ""};
    write(paths.resourceRootPath / "rules.ini", "[Fallback]\nRA2=yes\n");
    selected(resolve(paths), paths.resourceRootPath / "rules.ini", false, "[Fallback]\nRA2=yes\n");
    paths.sourceId = "yr";
    require(!resolve(paths).available(), "strict YR cannot fall back to RA2");
    paths = {{}, {}, {}, "ra2"};
    require(!resolve(paths).available(), "empty roots do not search cwd");
    paths = {base / "physical-case", {}, {}, "yr"};
    write(paths.resourceRootPath / "RuLeSmD.InI", "[Case]\nV=loose\n");
    const auto caseSource = resolve(paths);
#ifdef _WIN32
    selected(caseSource, paths.resourceRootPath / "rulesmd.ini", false, "[Case]\nV=loose\n");
#else
    require(!caseSource.available(), "case-sensitive filesystem keeps candidate spelling");
#endif
}

void archiveContract(const fs::path& base) {
    const auto file = base / "entry/local.mix";
    const std::string raw = "[Bytes]\r\nValue=" + std::string("\xD6\xD0\xCE\xC4")
        + "|" + std::string(reinterpret_cast<const char*>(u8"中文")) + "\r\n";
    const std::vector<Member> members{{"rules.ini", raw}, {"unit.shp", "shp"}, {"unit.vxl", "vxl"},
        {"unit", "exact"}, {"dupe.ini", "first"}, {"dupe.ini", "second"}};
    write(file, archiveBytes(members));
    MixArchive archive;
    require(archive.loadFromPath(file) && archive.isIndexReadable(), "native MIX load");
    require(archive.getPath() == utf8(file), "native MIX UTF-8 label");
    require(archive.getEntries().size() == members.size(), "on-disk index order/count");
    for (std::size_t i = 0; i < members.size(); ++i) {
        std::vector<std::uint8_t> bytes;
        std::string error = "old";
        const auto& entry = archive.getEntries()[i];
        require(entry.id == mixFileIdFromName(members[i].first), "index order unchanged");
        require(archive.readEntryData(entry, bytes, error) && error.empty()
            && std::string(bytes.begin(), bytes.end()) == members[i].second, "native entry bytes exact");
    }
    require(archive.findEntryByName(" \"folder\\rUlEs.InI/\" ") == &archive.getEntries()[0], "logical trim/quotes/basename/ASCII case");
    require(archive.findEntryByName("unit") == &archive.getEntries()[3], "exact logical name wins before extensions");
    require(archive.findEntryByName("dupe.ini") == &archive.getEntries()[4], "duplicate ID first entry");
    require(!archive.findEntryByName("rules.bad") && !archive.findEntryByName("missing"), "explicit extension/missing");
    const std::vector<std::string> extensions{".shp", ".vxl", ".hva", ".tmp", ".pal", ".ini", ".map", ".mpr",
        ".mmx", ".yrm", ".yro", ".yrx", ".tem", ".sno", ".urb", ".ubn", ".des", ".lun", ".pcx", ".fnt", ".csf", ".wav", ".aud"};
    for (std::size_t i = 0; i < extensions.size(); ++i) {
        const auto path = base / "extensions" / (std::to_string(i) + ".mix");
        std::vector<Member> entries{{"x" + extensions[i], "first"}};
        if (i + 1 < extensions.size()) entries.push_back({"x" + extensions[i + 1], "second"});
        // Physical order deliberately reversed; logical fallback is frozen.
        std::reverse(entries.begin(), entries.end());
        write(path, archiveBytes(entries));
        MixArchive current;
        require(current.loadFromPath(path), "extension fixture load");
        require(current.findEntryByName("x")->id == mixFileIdFromName("x" + extensions[i]), "extension fallback order");
    }
    for (const bool legacy : {false, true}) {
        const auto path = base / (legacy ? "legacy.mix" : "checksum.mix");
        write(path, archiveBytes({{"rules.ini", raw}}, legacy, !legacy));
        MixArchive current;
        require(current.loadFromPath(path) && current.isIndexReadable() && current.hasChecksum() == !legacy, "legacy/flagged checksum layout");
    }
    const auto invalid = base / "bad.mix";
    write(invalid, "bad");
    require(!archive.loadFromPath(invalid) && !archive.isIndexReadable() && archive.getEntries().empty()
        && archive.getErrorMessage() == "Failed to read MIX flags.", "failed reload resets original state/error");
    require(!archive.loadFromPath(base / "absent.mix") && archive.getErrorMessage() == "Failed to open MIX file: " + utf8(base / "absent.mix"), "missing native MIX diagnostic");
    const std::string highName("\xD6\xD0.ini");
    require(mixFileIdFromName(highName) == mixFileIdFromName("'folder/" + highName + "'"), "logical high bytes not transcoded");
    // Independent CRC constants are also checked by the Python evidence audit.
    std::cout << "Logical CRC: rules.ini=" << formatMixId(mixFileIdFromName("rules.ini"))
              << " high=" << formatMixId(mixFileIdFromName(highName)) << '\n';
}

void failureContract(const fs::path& base) {
    RuntimeCampaignDataSourcePaths paths{base / "resource", base / "local", base / "mod", "yr"};
    mix(paths.modRootPath / "localmd.mix", "rulesmd.ini", "");
    write(paths.localDataRootPath / "localmd.mix", "bad");
    mix(paths.resourceRootPath / "localmd.mix", "rulesmd.ini", "[Good]\nValue=yes\n");
    selected(resolve(paths), paths.resourceRootPath / "localmd.mix", true, "[Good]\nValue=yes\n");
    // Empty loose files are selected before MIX; invalid content must fail at
    // read time without silently trying the next root/archive.
    write(paths.modRootPath / "rulesmd.ini", "");
    auto source = resolve(paths);
    std::string text = "old", error;
    IniFile prior;
    prior.loadFromString("[State]\nValue=preserved\n");
    require(source.available() && source.filePath == paths.modRootPath / "rulesmd.ini", "empty loose still selected");
    require(!source.readText(text, &error) && text.empty() && error == "runtime INI has an invalid size: " + source.backingPath, "empty loose exact error");
    require(!source.load(prior, &error) && prior.getValue("State", "Value") == "preserved", "failed load preserves destination");
    const auto tail = base / "tail/localmd.mix";
    mix(tail, "rulesmd.ini", std::string("[Tail]\nV=ok\n\0\0", 14));
    RuntimeCampaignDataSourcePaths tailPaths{tail.parent_path(), {}, {}, "yr"};
    selected(resolve(tailPaths), tail, true, "[Tail]\nV=ok\n");
    const auto zeros = base / "zeros/localmd.mix";
    mix(zeros, "rulesmd.ini", std::string(2, '\0'));
    auto zeroSource = resolve({zeros.parent_path(), {}, {}, "yr"});
    require(!zeroSource.readText(text, &error) && error == "runtime INI entry is empty: " + zeroSource.displayPath, "MIX all-NUL original failure");
    const auto removed = base / "replace/localmd.mix";
    mix(removed, "rulesmd.ini", "[First]\nV=yes\n");
    auto changed = resolve({removed.parent_path(), {}, {}, "yr"});
    mix(removed, "artmd.ini", "[Second]\nV=yes\n");
    require(!changed.readText(text, &error) && error == "runtime INI entry is missing or invalid: " + changed.displayPath, "reopen observes replacement/no fallback");
    // A manually built legacy source continues to interpret narrow filenames
    // exactly as before; resolved sources own their native path across copies.
    const auto manualFile = base / "manual.ini";
    write(manualFile, "[Manual]\nV=yes\n");
    RuntimeIniSource manual;
    manual.backingPath = manualFile.string();
    manual.displayPath = manual.backingPath;
    require(manual.readText(text, &error) && text == "[Manual]\nV=yes\n", "legacy manual ACP backing path");
    RuntimeCampaignDataSourceOptions legacy{base.string(), {}, {}, "yr"};
    write(base / "rulesmd.ini", "[Legacy]\nV=yes\n");
    const auto compatibility = resolveRuntimeCampaignIniSource(legacy, RuntimeCampaignIniKind::Rules);
    selected(compatibility, base / "rulesmd.ini", false, "[Legacy]\nV=yes\n");
    legacy = {std::string("\x81"), std::string("\x81"), std::string("\x81"), "unknown"};
    require(!resolveRuntimeCampaignIniSource(legacy, RuntimeCampaignIniKind::Rules).available(), "unknown edition returns before decoding roots");
    legacy.sourceId = "yr";
    require(!resolveRuntimeCampaignTheaterSource(legacy, "unknown").available(), "unknown theater returns before decoding roots");
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 3) return 2;
        const auto base = fs::path(argv[1]);
        const auto native = base / fs::path(u8"文件路径-🌲");
        if (argc == 3) {
            const auto file = native / "entry/local.mix";
            MixArchive archive;
            if (std::string(argv[2]) == "--locked") {
                require(!archive.loadFromPath(file) && !archive.isIndexReadable()
                    && archive.getErrorMessage() == "Failed to open MIX file: " + utf8(file), "sharing denial native archive");
                auto source = resolve({file.parent_path(), {}, {}, "ra2"});
                require(!source.available(), "unreadable MIX skipped in search");
            } else if (std::string(argv[2]) == "--locked-loose") {
                const auto source = resolve({native / "loose", {}, {}, "ra2"});
                require(source.available() && !source.mixEntry, "locked loose metadata still selected");
                std::string text = "previous", error;
                IniFile ini;
                ini.loadFromString("[State]\nV=preserved\n");
                require(!source.readText(text, &error) && text.empty()
                    && error == "runtime INI could not be opened: " + source.backingPath, "locked loose original error/no fallback");
                require(!source.load(ini, &error) && ini.getValue("State", "V") == "preserved", "locked source preserves parsed state");
            } else {
                require(std::string(argv[2]) == "--readonly", "known native mode");
                require(archive.loadFromPath(file), "readonly native archive");
                std::vector<std::uint8_t> bytes;
                std::string error;
                require(archive.readEntryData(archive.getEntries().front(), bytes, error) && !bytes.empty(), "readonly native entry");
                selected(resolve({native / "loose", {}, {}, "ra2"}), native / "loose/rules.ini", false, "[ReadOnly]\nV=yes\n");
            }
        } else {
            archiveContract(native);
            write(native / "loose/rules.ini", "[ReadOnly]\nV=yes\n");
            searchContract(native / "search");
            // Legacy narrow filenames use an ACP-representable directory.
            failureContract(base / "failures-中文");
            auto owned = resolve({native / "entry", {}, {}, "ra2"});
            require(owned.available(), "native result from temporary roots");
            auto copy = owned;
            owned = {};
            require(copy.filePath == native / "entry/local.mix", "owned source copy lifetime");
            std::string text;
            require(copy.readText(text), "copied native source reads after original destroyed");
        }
        std::cout << "Resource path contract passed: checks=" << checks
                  << " nativePaths=yes labelsUtf8=yes legacyACP=yes logicalNames=bytes"
                  << " priority=edition-looseRoots-mixRoots enumeration=none payload=unchanged\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Resource path contract failed after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
