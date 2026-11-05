#include "ui/resources/RuntimeMenuResourceWatch.h"
#include "runtime/config/RuntimeConfig.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

namespace {
namespace fs = std::filesystem;
int checks = 0;
int failures = 0;
void check(bool value, std::string_view label) {
    ++checks;
    if (!value) { ++failures; std::cerr << "FAIL " << label << '\n'; }
}
void write(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.close();
    check(!file.fail(), "fixture write");
}
RuntimeMenuResourcePaths roots(const fs::path& base) {
    RuntimeMenuResourcePaths result{base / "resource", base / "local"};
    fs::create_directories(result.resourceRoot / "nested");
    fs::create_directories(result.localDataRoot / "resources/mix_exports/nested");
    return result;
}
bool invalidated(const RuntimeMenuResourceWatch& watch, const RuntimeMenuResourcePaths& paths) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (watch.unchanged(paths) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return !watch.unchanged(paths);
}
}

int main(int argc, char** argv) {
    static_assert(!std::is_copy_constructible_v<RuntimeMenuResourceWatch>);
    static_assert(!std::is_copy_assignable_v<RuntimeMenuResourceWatch>);
    if (argc != 2) return 2;
    const fs::path base = fs::absolute(fs::path(argv[1]));
    if (fs::exists(base)) { std::cerr << "Use a new fixture directory\n"; return 2; }
    const auto native = roots(base / fs::path(u8"菜单监测-🌲"));
    RuntimeMenuResourceWatch watch(native);
    check(watch.sameRoots(native), "owned native roots");
    auto alias = native;
    alias.resourceRoot /= "nested/..";
    alias.localDataRoot /= "resources/..";
    check(watch.sameRoots(alias), "absolute lexical normalization");
    auto changed = native; changed.resourceRoot /= "different";
    check(!watch.sameRoots(changed) && !watch.unchanged(changed), "resource root mismatch");
    changed = native; changed.localDataRoot /= "different";
    check(!watch.sameRoots(changed) && !watch.unchanged(changed), "local root mismatch");
#ifdef _WIN32
    check(watch.unchanged(native), "native Unicode tree initially fresh");
    write(native.localDataRoot / "runtime_config.ini", "[Preferences]\nVolume=37\n");
    write(native.localDataRoot / "campaign_progress.ini", "[Progress]\nMission=1\n");
    write(native.localDataRoot / "RA2MD.INI", "[Skirmish]\nCrates=yes\n");
    write(native.localDataRoot / "runtime_audio_cache/menu.wav", "cache");
    check(watch.unchanged(native), "preferences and unrelated local caches excluded");
    write(native.resourceRoot / fs::path(u8"nested/变化-🌲.ini"), "resource");
    check(invalidated(watch, native), "recursive resource file change");
    check(!watch.unchanged(native) && !watch.unchanged(native), "signal remains stale without rearm");
    { RuntimeMenuResourceWatch reloaded(native); check(reloaded.unchanged(native), "new menu generation fresh"); }
    for (const char* name : {"rulesmd.ini", "mpmodesmd.ini", "coopcampmd.ini", "soundmd.ini"}) {
        RuntimeMenuResourceWatch local(native);
        check(local.unchanged(native), "local input baseline");
        write(native.localDataRoot / name, "[Input]\nValue=changed\n");
        check(!local.unchanged(native), "local input created invalidates");
        RuntimeMenuResourceWatch rewritten(native);
        write(native.localDataRoot / name, "[Input]\nValue=longer-again\n");
        check(!rewritten.unchanged(native), "local input size/mtime change invalidates");
    }
    { RuntimeMenuResourceWatch exports(native);
      write(native.localDataRoot / "resources/mix_exports/nested/input.pal", "palette");
      check(invalidated(exports,native), "recursive export change"); }
    { RuntimeMenuResourceWatch rename(native);
      fs::rename(native.resourceRoot / fs::path(u8"nested/变化-🌲.ini"), native.resourceRoot / "nested/renamed.ini");
      check(invalidated(rename,native), "resource rename invalidates"); }
    { RuntimeMenuResourceWatch directory(native);
      fs::create_directory(native.resourceRoot / "new-directory");
      check(invalidated(directory,native), "directory creation invalidates"); }
    const auto compatibility = roots(base / "legacy");
    RuntimeConfig config;
    config.resourceRootPath = compatibility.resourceRoot.string();
    config.localDataRootPath = compatibility.localDataRoot.string();
    RuntimeMenuResourceWatch legacy(config);
    config.configPath = (compatibility.localDataRoot / "other-preferences.ini").string();
    config.modRootPath = (base / "unwatched-mod").string();
    config.musicVolume = 23;
    check(legacy.sameRoots(config) && legacy.unchanged(config), "legacy roots; preference and mod policy unchanged");
    // Earlier cases deliberately changed native. Keep lifetime checks on a
    // separate untouched tree: Windows may publish a close/write notification
    // after the write operation has returned, correctly making that tree stale.
    const auto generations = roots(base / "generations");
    for (int i = 0; i < 128; ++i) {
        RuntimeMenuResourceWatch generation(generations);
        check(generation.unchanged(generations), "repeated owner create/destroy");
    }
#else
    check(!watch.unchanged(native), "unimplemented notifications require reload");
#endif
    auto missing = native; missing.resourceRoot = base / "missing-resource";
    RuntimeMenuResourceWatch unavailableResource(missing);
    check(!unavailableResource.unchanged(missing), "unavailable resource notification requires reload");
    missing = native; missing.localDataRoot = base / "missing-local";
    RuntimeMenuResourceWatch unavailableExports(missing);
    check(!unavailableExports.unchanged(missing), "unavailable export notification requires reload");
    write(base / "file-root", "not a directory");
    missing = native; missing.resourceRoot = base / "file-root";
    RuntimeMenuResourceWatch wrongType(missing);
    check(!wrongType.unchanged(missing), "regular file cannot establish tree freshness");
    std::cout << "Menu resource watch: checks=" << checks << " failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}