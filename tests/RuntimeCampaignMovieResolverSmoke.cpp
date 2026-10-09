#include "IniFile.h"
#include "RuntimeCampaignDataSource.h"
#include "RuntimeCampaignMovieResolver.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string lowerAscii(std::string value) {
    for (char& character : value) {
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)
        ));
    }
    return value;
}

bool ra2ArchiveReference(const RuntimeCampaignMovieResolution& resolution) {
    const std::string path = lowerAscii(resolution.archivePath);
    return resolution.found
        && resolution.mixBacked
        && resolution.sourceId == "ra2"
        && resolution.byteSize > 0
        && isRuntimeCampaignMovieMixReference(resolution.referencePath)
        && path.find("movmd03.mix") == std::string::npos
        && (path.find("movies01.mix") != std::string::npos
            || path.find("movies02.mix") != std::string::npos);
}

bool yrReference(const RuntimeCampaignMovieResolution& resolution) {
    const std::string path = lowerAscii(resolution.referencePath);
    return resolution.found
        && resolution.sourceId == "yr"
        && resolution.byteSize > 0
        && path.find("movmd03") != std::string::npos
        && path.find("movies01") == std::string::npos
        && path.find("movies02") == std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: RuntimeCampaignMovieResolverSmoke "
                  << "<resource-root> <local-data-root> <cache-root>\n";
        return 2;
    }

    RuntimeCampaignMovieResolverOptions options;
    options.resourceRootPath = argv[1];
    options.localDataRootPath = argv[2];

    bool ok = true;
    std::vector<RuntimeCampaignMovieResolution> ra2Movies;
    for (const std::string& movieId : {
             "A01_f00e",
             "S01_f00e",
             "A13_f02e",
             "S13_f01e"
         }) {
        RuntimeCampaignMovieResolution resolution =
            resolveRuntimeCampaignMovie(options, "ra2", movieId);
        if (!ra2ArchiveReference(resolution)) {
            std::cerr << "RA2 movie did not resolve through movies01/02: "
                      << movieId << " -> " << resolution.referencePath
                      << "\n";
            ok = false;
        }
        ra2Movies.push_back(std::move(resolution));
    }

    const RuntimeCampaignMovieResolution yrLoose =
        resolveRuntimeCampaignMovie(options, "yr", "A01_f00e");
    if (!yrReference(yrLoose)
        || yrLoose.referencePath == ra2Movies.front().referencePath) {
        std::cerr << "YR same-name movie was not isolated from RA2: "
                  << yrLoose.referencePath << "\n";
        ok = false;
    }

    RuntimeCampaignMovieResolverOptions archiveOnlyOptions;
    archiveOnlyOptions.resourceRootPath = argv[1];
    const RuntimeCampaignMovieResolution yrArchive =
        resolveRuntimeCampaignMovie(
            archiveOnlyOptions,
            "yr",
            "A01_f00e"
        );
    if (!yrReference(yrArchive) || !yrArchive.mixBacked
        || lowerAscii(yrArchive.archivePath).find("movmd03.mix")
            == std::string::npos) {
        std::cerr << "YR movmd03 MIX lookup failed: "
                  << yrArchive.referencePath << "\n";
        ok = false;
    }

    if (resolveRuntimeCampaignMovie(options, "unknown", "A01_f00e").found) {
        std::cerr << "unknown campaign edition crossed into a movie archive\n";
        ok = false;
    }

    RuntimeCampaignDataSourceOptions dataOptions;
    dataOptions.resourceRootPath = argv[1];
    dataOptions.localDataRootPath = argv[2];
    dataOptions.sourceId = "ra2";
    IniFile baseArt;
    const RuntimeIniSource baseArtSource = resolveRuntimeCampaignIniSource(
        dataOptions,
        RuntimeCampaignIniKind::Art
    );
    int authoredIndexCount = 0;
    int resolvedIndexCount = 0;
    std::vector<std::string> unresolvedIds;
    // The base art.ini table carries legacy TS/cut-film identifiers that are
    // not present in the retail RA2 movies01/02 archives, plus three authored
    // RA2 names absent from this installed media set.  This is the only
    // unresolved allowlist: every newly unresolved identifier fails CI.
    const std::set<std::string> allowedRa2Unresolved{
        "cap_trat", "coup", "vegawin", "diskdest", "intro",
        "gdi_m02", "gdi_m03", "gdi_m04", "gdi_m05", "gdi_m06",
        "gdi_m07", "gdi_m08", "gdi_m09a", "gdi_m09b", "gdi_m09c",
        "gdi_m10a", "gdim09cw", "gdi_m11", "gdi_m12a", "hideseek",
        "iceskate", "mechatak", "eva", "nod_m02", "nod_m03",
        "nod_m04", "nod_m06", "nowcnot", "orcastrk", "podasslt",
        "retrbtn", "tenevict", "trainrob", "nod06abw", "empulse",
        "nod_m09", "startup", "icbmlnch", "beachead", "gdi_finl",
        "nod_m05", "gennodl1", "gdim09d1", "gdi01_sb", "gdi02_sb",
        "gdi03_sb", "nod_m07", "nod_m08", "nod_m10", "nod_m11",
        "nod_m12", "nod_finl", "nod01_sb", "nod02_sb", "genwin01",
        "ufoguard", "wwlogo", "kill_gdi", "killmech", "unstpble",
        "n_logo_w", "n_logo_l", "nod_flag", "gdi_logo", "gdi_flag",
        "dambreak", "a07_p02e", "a00_f01e", "s11_p02e"
    };
    if (!baseArtSource.load(baseArt)) {
        std::cerr << "base art.ini could not be loaded for index audit\n";
        ok = false;
    } else if (const IniFile::Section* movies = baseArt.getSection("Movies")) {
        for (const auto& [indexText, movieId] : *movies) {
            (void)indexText;
            if (movieId.empty()) {
                continue;
            }
            ++authoredIndexCount;
            if (resolveRuntimeCampaignMovie(options, "ra2", movieId).found) {
                ++resolvedIndexCount;
            } else {
                unresolvedIds.push_back(movieId);
            }
        }
    } else {
        std::cerr << "base art.ini has no [Movies] section\n";
        ok = false;
    }
    std::vector<std::string> unexpectedUnresolved;
    for (const std::string& movieId : unresolvedIds) {
        if (allowedRa2Unresolved.find(lowerAscii(movieId))
            == allowedRa2Unresolved.end()) {
            unexpectedUnresolved.push_back(movieId);
        }
    }
    if (!unexpectedUnresolved.empty()) {
        std::cerr << "new RA2 [Movies] unresolved entries are not whitelisted:";
        for (const std::string& movieId : unexpectedUnresolved) {
            std::cerr << ' ' << movieId;
        }
        std::cerr << "\n";
        ok = false;
    }
    std::cout << "RA2 indexed movie audit: " << resolvedIndexCount
              << "/" << authoredIndexCount
              << " whitelisted=" << unresolvedIds.size()
              << " unexpected=" << unexpectedUnresolved.size()
              << " unresolved=";
    for (const std::string& movieId : unresolvedIds) {
        std::cout << " " << movieId;
    }
    std::cout << "\n";

    RuntimeCampaignMovieResolverOptions materializeOptions;
    materializeOptions.resourceRootPath = argv[1];
    materializeOptions.localDataRootPath = argv[3];
    int materializedCount = 0;
    for (const RuntimeCampaignMovieResolution& resolution : ra2Movies) {
        std::string materializedPath;
        std::string materializeError;
        if (!resolution.found
            || !materializeRuntimeCampaignMovie(
                materializeOptions,
                resolution.referencePath,
                materializedPath,
                materializeError
            )) {
            std::cerr << "RA2 movie materialization failed for "
                      << resolution.movieId << ": "
                      << materializeError << "\n";
            ok = false;
            continue;
        }
        std::error_code sizeError;
        const std::uintmax_t size = fs::file_size(materializedPath, sizeError);
        std::array<char, 3> signature{};
        std::ifstream input(materializedPath, std::ios::binary);
        input.read(signature.data(), static_cast<std::streamsize>(signature.size()));
        if (sizeError || size != resolution.byteSize
            || input.gcount() != static_cast<std::streamsize>(signature.size())
            || std::string(signature.data(), signature.size()) != "BIK") {
            std::cerr << "materialized RA2 Bink failed validation for "
                      << resolution.movieId << "\n";
            ok = false;
            continue;
        }
        std::string cachedPath;
        std::string cachedError;
        if (!materializeRuntimeCampaignMovie(
                materializeOptions,
                resolution.referencePath,
                cachedPath,
                cachedError
            ) || cachedPath != materializedPath) {
            std::cerr << "RA2 movie cache reuse failed for "
                      << resolution.movieId << ": "
                      << cachedError << "\n";
            ok = false;
            continue;
        }
        ++materializedCount;
    }

    std::cout << "runtime campaign movie resolver smoke: "
              << (ok ? "PASS" : "FAIL")
              << " ra2=4/4 yrLoose=" << (yrLoose.found ? "yes" : "no")
              << " yrMix=" << (yrArchive.mixBacked ? "yes" : "no")
              << " indexes=" << resolvedIndexCount
              << "/" << authoredIndexCount
              << " materialized=" << materializedCount
              << "/" << ra2Movies.size()
              << "\n";
    return ok ? 0 : 1;
}
