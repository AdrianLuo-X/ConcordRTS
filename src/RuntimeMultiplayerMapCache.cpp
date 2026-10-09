#include "RuntimeMultiplayerMapCache.h"
#include "RuntimeMultiplayerCompatibilityFingerprint.h"
#include "RuntimeAtomicFile.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
bool fail(std::string& error, const char* reason) { error = reason; return false; }
bool safeEntry(const fs::path& path, bool directory, std::string& error) {
    std::error_code ec;
    const auto status = fs::symlink_status(path, ec);
    if (ec) return fail(error, "could not inspect the multiplayer map cache path");
    if (fs::is_symlink(status) || (directory ? !fs::is_directory(status) : !fs::is_regular_file(status)))
        return fail(error, "multiplayer map cache path has an unexpected type");
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return fail(error, "multiplayer map cache may not follow reparse points");
#endif
    return true;
}
bool cacheDirectory(const fs::path& root, bool create, fs::path& result, std::string& error) {
    if (root.empty()) return fail(error, "local data root is empty");
    std::error_code ec;
    fs::path path = fs::canonical(root, ec);
    if (ec || !fs::is_directory(path, ec)) return fail(error, "local data root is unavailable");
    // Resolve the trusted root once, then reject links at each cache component.
    for (const char* component : {"multiplayer", "maps"}) {
        path /= component;
        if (create && !fs::exists(path, ec)) {
            if (!fs::create_directory(path, ec) || ec) return fail(error, "could not create multiplayer map cache");
        }
        if (!safeEntry(path, true, error)) return false;
    }
    result = std::move(path); return true;
}
std::string prefix(const RuntimeMultiplayerMatchMap& map) {
    std::ostringstream text; text << std::hex << std::setfill('0') << std::setw(16) << map.contentFingerprint << '-';
    return text.str();
}
std::string name(const RuntimeMultiplayerMatchMap& map, const RuntimeMultiplayerSnapshotSha256& digest) {
    std::ostringstream text; text << prefix(map) << std::hex << std::setfill('0');
    for (const auto byte : digest) text << std::setw(2) << static_cast<unsigned int>(byte);
    return text.str() + ".map";
}
bool validFile(const fs::path& path, const RuntimeMultiplayerMatchMap& map, std::string& error) {
    if (!safeEntry(path, false, error)) return false;
    RuntimeMultiplayerContentComponent component;
    if (!loadRuntimeMultiplayerContentComponentFile(path, map.canonicalId, component, error)
        || !validateRuntimeMultiplayerMapBytes(map, component.bytes, error)) return false;
    if (path.filename().string() != name(map, runtimeMultiplayerSnapshotSha256(component.bytes)))
        return fail(error, "map cache name does not match the verified SHA-256");
    return true;
}
}

bool storeRuntimeMultiplayerMapCache(const fs::path& root, const RuntimeMultiplayerMapOffer& offer,
    std::span<const std::uint8_t> bytes, fs::path& output, std::string& error) {
    if (!validateRuntimeMultiplayerMapOffer(offer, error)
        || bytes.size() != offer.transfer.byteCount
        || runtimeMultiplayerSnapshotFingerprint(bytes) != offer.transfer.snapshotFingerprint
        || runtimeMultiplayerSnapshotSha256(bytes) != offer.transfer.sha256
        || !validateRuntimeMultiplayerMapBytes(offer.request.map, bytes, error)) {
        if (error.empty()) error = "map transfer size or digest did not match";
        return false;
    }
    fs::path directory;
    if (!cacheDirectory(root, true, directory, error)) return false;
    const auto target = directory / name(offer.request.map, offer.transfer.sha256);
    std::error_code ec;
    const auto status = fs::symlink_status(target, ec);
    // An existing cache entry must already be valid. Never overwrite unrelated
    // files, damaged entries, or a link masquerading as a cache file.
    if (status.type() != fs::file_type::not_found) {
        if (ec) return fail(error, "could not inspect the existing multiplayer map cache entry");
        if (!validFile(target, offer.request.map, error)) return false;
    } else {
        if (!runtimeWriteFileAtomically(target, [&](std::ostream& stream, std::string&) {
            stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return stream.good();
        }, error) || !validFile(target, offer.request.map, error)) return false;
    }
    output = target; error.clear(); return true;
}

bool findRuntimeMultiplayerMapCache(const fs::path& root, const RuntimeMultiplayerMatchMap& map,
    fs::path& output, std::string& error) {
    if (!validateRuntimeMultiplayerMapRequest({1, map}, error)) return false;
    fs::path directory;
    if (!cacheDirectory(root, false, directory, error)) return false;
    std::error_code ec;
    fs::directory_iterator it(directory, ec);
    for (; !ec && it != fs::directory_iterator{}; it.increment(ec)) {
        const auto filename = it->path().filename().string();
        if (filename.size() != 85 || !filename.starts_with(prefix(map)) || !filename.ends_with(".map")) continue;
        if (!std::all_of(filename.begin() + 17, filename.begin() + 81,
                [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) continue;
        std::string candidateError;
        if (validFile(it->path(), map, candidateError)) { output = it->path(); error.clear(); return true; }
    }
    return fail(error, "no verified downloaded map is cached locally");
}
