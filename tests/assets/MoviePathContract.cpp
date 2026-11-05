#include "assets/archive/MixArchive.h"
#include "assets/video/BinkPlaybackPaths.h"
#include "runtime/campaign/RuntimeCampaignMovieResolver.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
std::string utf8(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
void write(const fs::path& path, const std::string& bytes) {
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    require(!!stream, "fixture write");
}
std::string bytes(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(!!stream, "read selected path");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void le(std::string& result, std::uint32_t value, unsigned count) {
    for (unsigned n = 0; n < count; ++n) result.push_back(static_cast<char>((value >> (8U * n)) & 255U));
}
void mix(const fs::path& path, const std::string& name, const std::string& payload) {
    std::string result;
    le(result, 1, 2); le(result, static_cast<std::uint32_t>(payload.size()), 4);
    le(result, mixFileIdFromName(name), 4); le(result, 0, 4);
    le(result, static_cast<std::uint32_t>(payload.size()), 4);
    write(path, result + payload);
}
using Candidate = std::pair<fs::path, bool>;
std::vector<Candidate> candidates(const RuntimeCampaignMoviePaths& paths, bool ra2) {
    const std::vector<fs::path> loose = ra2
        ? std::vector<fs::path>{"resources/mix_exports/movies01", "resources/mix_exports/movies02",
            "movies01", "movies02", "Game/Movies", "Game"}
        : std::vector<fs::path>{"resources/mix_exports/movmd03", "resources/mix_exports/ra2md/movmd03",
            "movmd03", "GameMD/Movies", "GameMD"};
    const std::vector<fs::path> packed = ra2
        ? std::vector<fs::path>{"movies01.mix", "movies02.mix", "Game/movies01.mix", "Game/movies02.mix",
            "resources/mix_exports/movies01.mix", "resources/mix_exports/movies02.mix"}
        : std::vector<fs::path>{"movmd03.mix", "GameMD/movmd03.mix", "resources/mix_exports/movmd03.mix",
            "resources/mix_exports/ra2md/movmd03.mix"};
    std::vector<Candidate> result;
    for (const auto& root : {paths.modRoot, paths.localDataRoot, paths.resourceRoot}) {
        for (const auto& suffix : loose) result.emplace_back(root / suffix / "Probe.BIK", false);
        for (const auto& suffix : packed) result.emplace_back(root / suffix, true);
    }
    return result;
}
void search(const fs::path& base) {
    for (bool ra2 : {true, false}) {
        RuntimeCampaignMoviePaths seed{base / "resource", base / "local", base / "mod"};
        for (std::size_t i = 0; i < candidates(seed, ra2).size(); ++i) {
            for (bool competing : {false, true}) {
                const auto round = base / ((ra2 ? "ra2-" : "yr-") + std::to_string(i) + (competing ? "-pair" : "-single"));
                RuntimeCampaignMoviePaths paths{round / "resource", round / "local", round / "mod"};
                auto order = candidates(paths, ra2);
                const auto put = [&](std::size_t at, const std::string& payload) {
                    if (order[at].second) mix(order[at].first, "Probe.BIK", payload);
                    else write(order[at].first, payload);
                };
                put(i, "BIK-winner");
                if (competing && i + 1 < order.size()) put(i + 1, "BIK-loser");
                const auto value = resolveRuntimeCampaignMovieFromPaths(paths, ra2 ? "ra2" : "yr", " Probe.BIK ");
                const auto expected = fs::absolute(order[i].first).lexically_normal();
                require(value.found && value.mixBacked == order[i].second, "candidate found/type");
                require((value.mixBacked ? value.archiveFilePath : value.filePath) == expected, "ordered native identity");
                require(value.referencePath == utf8(expected) + (value.mixBacked ? "!Probe.BIK" : ""), "UTF-8 reference preserves case");
                require(value.byteSize == 10 && value.movieId == "Probe", "byte size/logical identity");
                fs::path materialized;
                std::string error;
                require(materializeRuntimeCampaignMovieFromPaths(paths, value.referencePath, materialized, error), "native materialization");
                require(error.empty() && bytes(materialized) == "BIK-winner", "selected movie bytes exact");
            }
        }
    }
    RuntimeCampaignMoviePaths paths{base / "edition/resource", base / "edition/local", base / "edition/mod"};
    mix(paths.modRoot / "movies01.mix", "Probe.BIK", "ra2");
    mix(paths.resourceRoot / "movmd03.mix", "Probe.BIK", "yr");
    require(resolveRuntimeCampaignMovieFromPaths(paths, "", "Probe").sourceId == "yr", "edition outer order before mod");
    require(resolveRuntimeCampaignMovieFromPaths(paths, "Red Alert 2", "Probe").sourceId == "ra2", "edition aliases");
    require(resolveRuntimeCampaignMovieFromPaths(paths, "yuri's revenge", "Probe").sourceId == "yr", "YR alias");
    require(!resolveRuntimeCampaignMovieFromPaths(paths, "unknown", "Probe").found, "unknown edition no fallback");
    for (const std::string logical : {"", "../Probe", "x/Probe", "x\\Probe", "Probe.wav", "!Probe", "Probe?.bik"})
        require(!resolveRuntimeCampaignMovieFromPaths(paths, "ra2", logical).found, "invalid logical ID");
    require(!resolveRuntimeCampaignMovieFromPaths(paths, "ra2", "Missing").found, "missing entry");
    write(paths.modRoot / "movmd03.mix", "broken");
    require(resolveRuntimeCampaignMovieFromPaths(paths, "yr", "Probe").archiveFilePath == paths.resourceRoot / "movmd03.mix", "bad MIX fallback");
    write(paths.modRoot / "GameMD/Probe.BIK", "");
    require(resolveRuntimeCampaignMovieFromPaths(paths, "yr", "Probe.BIK").filePath == paths.modRoot / "GameMD/Probe.BIK", "empty loose still wins");
}
void cache(const fs::path& base) {
    RuntimeCampaignMoviePaths paths{base / "resource", base / "cache", {}};
    const auto archive = paths.resourceRoot / "movies01.mix";
    const std::string payload(1024U * 1024U + 17U, 'x');
    mix(archive, "Probe.bik", payload);
    auto result = resolveRuntimeCampaignMovieFromPaths(paths, "ra2", "Probe");
    fs::path file;
    std::string error;
    require(materializeRuntimeCampaignMovieFromPaths(paths, result.referencePath, file, error), "stream over one buffer");
    require(bytes(file) == payload && file.filename() == "probe.bik", "cache bytes/lowercase name");
    require(file.parent_path().filename().string().starts_with("movies01_"), "cache archive stem");
    const auto initial = file;
    write(file, std::string(payload.size(), 'y'));
    require(materializeRuntimeCampaignMovieFromPaths(paths, result.referencePath, file, error) && file == initial, "size-only cache hit");
    require(bytes(file) == std::string(payload.size(), 'y'), "same-size cache intentionally not revalidated");
    write(file, "wrong-size");
    require(materializeRuntimeCampaignMovieFromPaths(paths, result.referencePath, file, error) && bytes(file) == payload, "wrong-size replacement");
    const auto stamp = fs::last_write_time(archive);
    fs::last_write_time(archive, stamp + std::chrono::seconds(5));
    require(materializeRuntimeCampaignMovieFromPaths(paths, result.referencePath, file, error) && file != initial, "mtime cache identity");
    const auto second = file;
    mix(archive, "Probe.bik", "new-size");
    require(materializeRuntimeCampaignMovieFromPaths(paths, result.referencePath, file, error) && file != second && bytes(file) == "new-size", "size cache identity");
    write(paths.localDataRoot / "blocked", "regular file");
    auto blocked = paths;
    blocked.localDataRoot /= "blocked";
    require(!materializeRuntimeCampaignMovieFromPaths(blocked, result.referencePath, file, error) && file.empty(), "cache directory failure");
    require(error.starts_with("campaign movie cache directory could not be created:"), "cache directory error category");
    require(!materializeRuntimeCampaignMovieFromPaths(paths, "", file, error) && error == "campaign has no authored Bink movie", "empty reference");
    require(!materializeRuntimeCampaignMovieFromPaths(paths, utf8(base / "absent.bik"), file, error) && file.empty(), "missing loose");
    require(!materializeRuntimeCampaignMovieFromPaths(paths, utf8(archive) + "!Missing.bik", file, error) && file.empty(), "missing MIX entry");
    auto tempPaths = paths;
    tempPaths.localDataRoot.clear();
    require(materializeRuntimeCampaignMovieFromPaths(tempPaths, result.referencePath, file, error), "temp root fallback");
    require(utf8(file).find("ConcordRTS/runtime_campaign_cache/movies/") != std::string::npos, "temp root suffix");
}
void helper(const fs::path& base) {
    for (std::size_t selected = 0; selected < 5; ++selected) {
        auto round = base / std::to_string(selected);
        BinkPlaybackPathInputs in{round / "resource", round / "export", {}, round / "exe/bin", round / "cwd"};
        const std::vector<fs::path> order{in.executableDirectory / "BinkFrameExtractor.exe",
            in.executableDirectory.parent_path() / "BinkFrameExtractor.exe",
            in.executableDirectory.parent_path() / "bink_decoder32/Debug/BinkFrameExtractor.exe",
            in.currentDirectory / "build/BinkFrameExtractor.exe", in.currentDirectory / "BinkFrameExtractor.exe"};
        for (std::size_t n = selected; n < order.size(); ++n) write(order[n], "helper");
        for (std::size_t n = 0; n < selected; ++n) fs::create_directories(order[n]);
        write(in.exportRoot / "langmd/ra2ts_l.bik", "bad-header-selected");
        write(in.exportRoot / "lang/ra2ts_l.bik", "alternate");
        write(in.resourceRoot / "GameMD/Binkw32.dll", "dll");
        write(in.resourceRoot / "Game/Binkw32.dll", "alternate");
        auto paths = resolveBinkPlaybackPaths(in);
        require(paths.helper == order[selected], "helper candidate/directory rejection order");
        require(paths.movie == in.exportRoot / "langmd/ra2ts_l.bik", "movie priority no header fallback");
        require(paths.decoderDll == in.resourceRoot / "GameMD/Binkw32.dll", "decoder edition priority");
        in.requestedMovie = round / "absent.bik";
        require(resolveBinkPlaybackPaths(in).movie.empty(), "explicit missing movie no default fallback");
        write(in.requestedMovie, "requested");
        require(resolveBinkPlaybackPaths(in).movie == in.requestedMovie, "explicit movie wins");
    }
    auto current = fs::current_path();
    fs::create_directories(base / "relative");
    fs::current_path(base / "relative");
    write("BinkFrameExtractor.exe", "helper");
    require(resolveBinkPlaybackPaths({}).helper == "BinkFrameExtractor.exe", "empty discovery preserves relative candidate");
    fs::current_path(current);
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) return 2;
        const auto root = fs::absolute(fs::path(argv[1])) / fs::path(u8"影片路径-🌲");
        if (argc == 3) {
            const std::string mode = argv[2];
            RuntimeCampaignMoviePaths paths{root / "locks", root / "lock-cache", {}};
            if (mode == "--prepare-locks") {
                mix(paths.resourceRoot / "movies01.mix", "Probe.bik", "BIKlocked");
                write(paths.resourceRoot / "Game/Loose.bik", "BIKloose");
            } else {
                const auto result = resolveRuntimeCampaignMovieFromPaths(paths, "ra2", "Probe");
                fs::path file;
                std::string error;
                if (mode == "--locked-mix") {
                    require(!result.found, "locked archive skipped");
                    require(!materializeRuntimeCampaignMovieFromPaths(paths, utf8(paths.resourceRoot / "movies01.mix") + "!Probe.bik", file, error), "locked archive read fails");
                } else if (mode == "--readonly") {
                    require(result.found && materializeRuntimeCampaignMovieFromPaths(paths, result.referencePath, file, error), "readonly archive readable");
                    require(bytes(file) == "BIKlocked", "readonly exact bytes");
                } else return 2;
            }
        } else {
            require(!fs::exists(root), "use unique fixture output");
            search(root / "search"); cache(root / "cache"); helper(root / "helper");
        }
        std::cout << "Movie path contract passed: checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Movie path contract failed after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
