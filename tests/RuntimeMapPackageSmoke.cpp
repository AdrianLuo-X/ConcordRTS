#include "MixArchive.h"
#include "RuntimeMapPackage.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;

struct MixFixtureEntry {
    std::string name;
    std::string bytes;
};

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "Runtime map package smoke failure: " << message << '\n';
    }
    return condition;
}

std::uint64_t processId() {
#ifdef _WIN32
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(getpid());
#endif
}

fs::path createUniqueRoot(bool& success) {
    std::error_code error;
    const fs::path temporaryRoot = fs::temp_directory_path(error);
    if (error) {
        success &= expect(false, "could not locate the temporary directory");
        return {};
    }

    const auto timestamp = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()
    );
    for (std::uint64_t attempt = 0; attempt < 64U; ++attempt) {
        std::ostringstream leaf;
        leaf << "concord-runtime-map-package-smoke-"
             << std::hex << processId() << '-'
             << timestamp << '-' << attempt;
        const fs::path candidate = temporaryRoot / leaf.str();
        error.clear();
        if (fs::create_directory(candidate, error)) {
            return candidate;
        }
        if (error) {
            success &= expect(
                false,
                "could not create isolated smoke directory: "
                    + error.message()
            );
            return {};
        }
    }

    success &= expect(false, "unique smoke-directory attempts were exhausted");
    return {};
}

void writeUInt16(std::ostream& output, std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU)
    };
    output.write(bytes, sizeof(bytes));
}

void writeUInt32(std::ostream& output, std::uint32_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU)
    };
    output.write(bytes, sizeof(bytes));
}

bool writeLegacyMix(
    const fs::path& path,
    const std::vector<MixFixtureEntry>& entries,
    std::string& error
) {
    error.clear();
    if (entries.empty()
        || entries.size() > std::numeric_limits<std::uint16_t>::max()) {
        error = "fixture entry count is outside the legacy MIX range";
        return false;
    }

    std::uint64_t totalBytes = 0;
    for (const MixFixtureEntry& entry : entries) {
        if (entry.bytes.empty()
            || entry.bytes.size() > std::numeric_limits<std::uint32_t>::max()
            || totalBytes > std::numeric_limits<std::uint32_t>::max()
                - entry.bytes.size()) {
            error = "fixture data is outside the legacy MIX range";
            return false;
        }
        totalBytes += entry.bytes.size();
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "could not open fixture package";
        return false;
    }

    writeUInt16(output, static_cast<std::uint16_t>(entries.size()));
    writeUInt32(output, static_cast<std::uint32_t>(totalBytes));

    std::uint32_t offset = 0;
    for (const MixFixtureEntry& entry : entries) {
        writeUInt32(output, mixFileIdFromName(entry.name));
        writeUInt32(output, offset);
        writeUInt32(output, static_cast<std::uint32_t>(entry.bytes.size()));
        offset += static_cast<std::uint32_t>(entry.bytes.size());
    }
    for (const MixFixtureEntry& entry : entries) {
        output.write(
            entry.bytes.data(),
            static_cast<std::streamsize>(entry.bytes.size())
        );
    }
    output.flush();
    if (!output) {
        error = "could not write the complete fixture package";
        return false;
    }
    return true;
}

std::string readText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()
    );
}

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

fs::path expectedCachePath(
    const fs::path& cacheDirectory,
    const std::string& mapId,
    const std::string& embeddedMapName
) {
    std::string id = formatMixId(mixFileIdFromName(embeddedMapName));
    if (id.rfind("0x", 0U) == 0U) {
        id.erase(0U, 2U);
    }
    return cacheDirectory / (mapId + '_' + lowerAscii(id) + ".map");
}

bool sameMetadata(
    const RuntimeMapPackageMap& actual,
    const fs::path& sourcePackage,
    const fs::path& cachedMap
) {
    return actual.sourcePackagePath == sourcePackage
        && actual.mapPath == cachedMap
        && actual.id == "InternalArena"
        && actual.descriptionKey == "GUI:InternalArena"
        && actual.minPlayers == 3
        && actual.maxPlayers == 6
        && actual.filters
            == std::vector<std::string>{
                "standard", "megawealth", "navalwar"
            };
}

bool warningsContain(
    const RuntimeMapPackageScanResult& result,
    const std::string& text
) {
    return std::any_of(
        result.warnings.begin(),
        result.warnings.end(),
        [&text](const std::string& warning) {
            return warning.find(text) != std::string::npos;
        }
    );
}

bool removeKnownPathIfPresent(const fs::path& path, bool directory) {
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (error) {
        return expect(
            false,
            "could not inspect known smoke path: "
                + path.filename().string() + " (" + error.message() + ')'
        );
    }
    if (!exists) {
        return true;
    }

    const bool removed = fs::remove(path, error);
    return expect(
        removed && !error,
        std::string{"could not remove known smoke "}
            + (directory ? "directory" : "file") + ": "
            + path.filename().string()
            + (error ? " (" + error.message() + ')' : "")
    );
}

} // namespace

int main() {
    bool success = true;
    const fs::path root = createUniqueRoot(success);
    if (root.empty()) {
        return 1;
    }

#ifdef _WIN32
    const fs::path priorityDirectory = root / fs::path(L"\u5730\u56fe-priority");
#else
    const fs::path priorityDirectory = root / "priority";
#endif
    const fs::path fallbackDirectory = root / "fallback";
    const fs::path cacheDirectory = root / "cache";
    const fs::path priorityPackage = priorityDirectory / "priority-pack.mmx";
    const fs::path fallbackPackage = fallbackDirectory / "fallback-pack.yro";
    const std::string priorityMapName = "InternalArena.mpr";
    const std::string fallbackMapName = "internalarena.yrm";
    const std::string recoveryMapName = "FallbackRecovery.map";
    const fs::path cachedMap = expectedCachePath(
        cacheDirectory,
        "InternalArena",
        priorityMapName
    );
    const fs::path cachedRecoveryMap = expectedCachePath(
        cacheDirectory,
        "FallbackRecovery",
        recoveryMapName
    );

    std::error_code fileError;
    success &= expect(
        fs::create_directory(priorityDirectory, fileError),
        "could not create priority fixture directory"
    );
    fileError.clear();
    success &= expect(
        fs::create_directory(fallbackDirectory, fileError),
        "could not create fallback fixture directory"
    );

    const std::string priorityPacket =
        "[MultiMaps]\n"
        "1=InternalArena\n"
        "2=FallbackRecovery\n"
        "\n"
        "[InternalArena]\n"
        "Description=GUI:InternalArena\n"
        "MinPlayers=3\n"
        "MaxPlayers=6\n"
        "GameMode= standard, megawealth , navalwar \n"
        "\n"
        "[FallbackRecovery]\n"
        "Description=GUI:BrokenPriorityMustNotWin\n"
        "MinPlayers=1\n"
        "MaxPlayers=8\n"
        "GameMode=broken\n";
    const std::string priorityMap =
        "[Basic]\n"
        "Name=Priority package map\n"
        "[Map]\n"
        "Size=0,0,64,64\n"
        "LocalSize=2,2,60,60\n"
        "[Preview]\n"
        "Size=0,0,64,64\n";
    const std::string invalidPriorityMap =
        "[Map]\n"
        "Size=0,0,16,16\n"
        "[Preview]\n"
        "Size=invalid\n";
    const std::string fallbackPacket =
        "[MultiMaps]\n"
        "1=internalarena\n"
        "2=FallbackRecovery\n"
        "\n"
        "[internalarena]\n"
        "Description=GUI:DuplicateMustNotWin\n"
        "MinPlayers=1\n"
        "MaxPlayers=8\n"
        "GameMode=duplicate\n"
        "\n"
        "[FallbackRecovery]\n"
        "Description=GUI:RecoveredFallback\n"
        "MinPlayers=2\n"
        "MaxPlayers=4\n"
        "GameMode=standard, meatgrind\n";
    const std::string fallbackMap =
        "[Basic]\n"
        "Name=Fallback duplicate map\n"
        "[Map]\n"
        "Size=0,0,32,32\n"
        "[Preview]\n"
        "Size=0,0,32,32\n";
    const std::string recoveryMap =
        "[Basic]\n"
        "Name=Recovered fallback map\n"
        "[Map]\n"
        "Size=0,0,48,48\n"
        "[Preview]\n"
        "Size=0,0,48,48\n";

    std::string fixtureError;
    success &= expect(
        writeLegacyMix(
            priorityPackage,
            {
                {"priority-pack.pkt", priorityPacket},
                {priorityMapName, priorityMap},
                {recoveryMapName, invalidPriorityMap}
            },
            fixtureError
        ),
        "could not create .mmx fixture: " + fixtureError
    );
    fixtureError.clear();
    success &= expect(
        writeLegacyMix(
            fallbackPackage,
            {
                {"fallback-pack.pkt", fallbackPacket},
                {fallbackMapName, fallbackMap},
                {recoveryMapName, recoveryMap}
            },
            fixtureError
        ),
        "could not create .yro fixture: " + fixtureError
    );

    success &= expect(
        isRuntimeMapPackageExtension(priorityPackage)
            && isRuntimeMapPackageExtension(fallbackPackage)
            && isRuntimeMapPackageExtension("case-insensitive.MMX")
            && !isRuntimeMapPackageExtension("ordinary.map"),
        "runtime package extension filtering was incorrect"
    );

    const std::vector<fs::path> searchDirectories = {
        priorityDirectory,
        fallbackDirectory,
        priorityDirectory
    };
    const RuntimeMapPackageScanResult first = scanRuntimeMapPackages(
        searchDirectories,
        cacheDirectory
    );
    success &= expect(
        first.packageFileCount == 2U
            && first.readablePackageCount == 2U
            && first.duplicateMapCount == 1U,
        "package counts or cross-directory duplicate count were incorrect"
    );
    success &= expect(
        first.warnings.size() == 1U
            && warningsContain(first, "lacks Map or Preview data"),
        "invalid priority metadata did not produce the expected warning"
    );
    success &= expect(
        first.maps.size() == 2U,
        "duplicate collapse or valid fallback recovery was incorrect"
    );
    const auto firstArena = std::find_if(
        first.maps.begin(),
        first.maps.end(),
        [](const RuntimeMapPackageMap& map) {
            return map.id == "InternalArena";
        }
    );
    if (firstArena != first.maps.end()) {
        success &= expect(
            sameMetadata(*firstArena, priorityPackage, cachedMap),
            "embedded ID, metadata, filters, or priority source were incorrect"
        );
    } else {
        success &= expect(false, "priority map was absent from scan results");
    }
    const auto firstRecovery = std::find_if(
        first.maps.begin(),
        first.maps.end(),
        [](const RuntimeMapPackageMap& map) {
            return map.id == "FallbackRecovery";
        }
    );
    if (firstRecovery != first.maps.end()) {
        success &= expect(
            firstRecovery->sourcePackagePath == fallbackPackage
                && firstRecovery->mapPath == cachedRecoveryMap
                && firstRecovery->descriptionKey == "GUI:RecoveredFallback"
                && firstRecovery->minPlayers == 2
                && firstRecovery->maxPlayers == 4
                && firstRecovery->filters
                    == std::vector<std::string>{"standard", "meatgrind"},
            "valid fallback package did not recover the rejected priority map"
        );
    } else {
        success &= expect(false, "fallback recovery map was absent");
    }

    fileError.clear();
    success &= expect(
        fs::is_regular_file(cachedMap, fileError)
            && !fileError
            && cachedMap.extension() == ".map"
            && readText(cachedMap) == priorityMap,
        "the selected .mpr entry was not cached as an ordinary .map path"
    );
    fileError.clear();
    success &= expect(
        fs::is_regular_file(cachedRecoveryMap, fileError)
            && !fileError
            && readText(cachedRecoveryMap) == recoveryMap,
        "valid lower-priority fallback map was not cached"
    );

    fs::file_time_type preservedWriteTime{};
    if (fs::is_regular_file(cachedMap, fileError) && !fileError) {
        fileError.clear();
        const fs::file_time_type oldWriteTime = fs::file_time_type::clock::now()
            - std::chrono::hours(24);
        fs::last_write_time(cachedMap, oldWriteTime, fileError);
        success &= expect(
            !fileError,
            "could not set the cached map timestamp probe"
        );
        fileError.clear();
        preservedWriteTime = fs::last_write_time(cachedMap, fileError);
        success &= expect(
            !fileError,
            "could not read the cached map timestamp probe"
        );
    } else {
        success &= expect(false, "cached map was unavailable for rewrite probe");
    }

    const RuntimeMapPackageScanResult second = scanRuntimeMapPackages(
        searchDirectories,
        cacheDirectory
    );
    success &= expect(
        second.packageFileCount == first.packageFileCount
            && second.readablePackageCount == first.readablePackageCount
            && second.duplicateMapCount == first.duplicateMapCount
            && second.warnings.size() == first.warnings.size()
            && warningsContain(second, "lacks Map or Preview data")
            && second.maps.size() == 2U,
        "second package scan did not reproduce the first scan result"
    );
    const auto secondArena = std::find_if(
        second.maps.begin(),
        second.maps.end(),
        [](const RuntimeMapPackageMap& map) {
            return map.id == "InternalArena";
        }
    );
    if (secondArena != second.maps.end()) {
        success &= expect(
            sameMetadata(*secondArena, priorityPackage, cachedMap),
            "second scan changed the selected packaged map"
        );
    } else {
        success &= expect(false, "second scan lost the priority map");
    }
    fileError.clear();
    const fs::file_time_type secondWriteTime = fs::last_write_time(
        cachedMap,
        fileError
    );
    success &= expect(
        !fileError
            && secondWriteTime == preservedWriteTime
            && readText(cachedMap) == priorityMap,
        "second scan rewrote or changed an identical cached map"
    );

    success &= removeKnownPathIfPresent(cachedMap, false);
    success &= removeKnownPathIfPresent(cachedRecoveryMap, false);
    success &= removeKnownPathIfPresent(cacheDirectory, true);
    success &= removeKnownPathIfPresent(priorityPackage, false);
    success &= removeKnownPathIfPresent(fallbackPackage, false);
    success &= removeKnownPathIfPresent(priorityDirectory, true);
    success &= removeKnownPathIfPresent(fallbackDirectory, true);
    success &= removeKnownPathIfPresent(root, true);

    if (!success) {
        return 1;
    }
    std::cout << "Runtime map package smoke passed\n";
    return 0;
}
