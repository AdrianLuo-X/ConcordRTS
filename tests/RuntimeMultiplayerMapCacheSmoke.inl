RuntimeMultiplayerMapOffer mapCacheFixture(std::vector<std::uint8_t>& bytes, SmokeContext& context) {
    const std::string text = "[Map]\nTheater=TEMPERATE\nSize=0,0,32,32\n;downloaded custom map\n";
    bytes.assign(text.begin(), text.end());
    RuntimeMultiplayerMapRequest request{1, {"multimd/downloaded.map", 0}};
    std::string error;
    context.expect(runtimeMultiplayerContentFingerprint({{request.map.canonicalId, bytes}}, request.map.contentFingerprint, error), "cache fixture fingerprint");
    RuntimeMultiplayerMapOffer offer;
    context.expect(makeRuntimeMultiplayerMapOffer(request, bytes, offer, error), "cache fixture offer");
    return offer;
}

void testDownloadedMapResolution(SmokeContext& context) {
    const auto root = fs::temp_directory_path() / ("concord_downloaded_map_"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "resources" / "mix_exports" / "multimd");
    RuntimeConfig config; config.localDataRootPath = root.string(); config.resourceRootPath = root.string();
    const auto wrongFile = root / "resources" / "mix_exports" / "multimd" / "downloaded.map";
    context.expect(writeFile(wrongFile, "preserve wrong local map"), "different local map fixture");
    config.defaultMapPath = wrongFile.string();
    std::vector<std::uint8_t> bytes;
    const auto offer = mapCacheFixture(bytes, context);
    fs::path resolved = "sentinel", stored;
    std::string error;
    context.expect(!resolveRuntimeMultiplayerMenuMap(config, offer.request.map, resolved, error)
        && resolved == fs::path("sentinel"), "wrong local map cannot be used before downloading");
    context.expect(storeRuntimeMultiplayerMapCache(root, offer, bytes, stored, error), "verified download stored: " + error);
    context.expect(resolveRuntimeMultiplayerMenuMap(config, offer.request.map, resolved, error)
        && resolved == stored && resolved != wrongFile, "actual menu resolves private downloaded map cache");
    RuntimeMultiplayerContentComponent original;
    context.expect(loadRuntimeMultiplayerContentComponentFile(wrongFile, offer.request.map.canonicalId, original, error)
        && std::string(original.bytes.begin(), original.bytes.end()) == "preserve wrong local map", "wrong original map preserved");
    context.expect(storeRuntimeMultiplayerMapCache(root, offer, bytes, resolved, error) && resolved == stored,
        "verified cache reused without changing its name");
    auto changedOffer = offer; changedOffer.request.map.canonicalId = "multimd/../../outside.map";
    resolved = "sentinel";
    context.expect(!storeRuntimeMultiplayerMapCache(root, changedOffer, bytes, resolved, error) && resolved == fs::path("sentinel"),
        "remote traversal cannot determine any output path");
    changedOffer = offer; changedOffer.transfer.sha256[0] ^= 1;
    context.expect(!storeRuntimeMultiplayerMapCache(root, changedOffer, bytes, resolved, error) && resolved == fs::path("sentinel"),
        "bad SHA-256 cannot enter the map cache");
    context.expect(!storeRuntimeMultiplayerMapCache(root, offer, std::span(bytes).first(bytes.size() - 1), resolved, error),
        "truncated download cannot enter the map cache");
    const auto obstructed = root / "obstructed";
    fs::create_directory(obstructed);
    context.expect(writeFile(obstructed / "multiplayer", "keep this file"), "cache path obstruction fixture");
    context.expect(!storeRuntimeMultiplayerMapCache(obstructed, offer, bytes, resolved, error)
        && resolved == fs::path("sentinel"), "cache creation rejects existing files in directory positions");
    std::cout << "Map cache: actual menu lookup, size/SHA/content checks, path bounds and preserved files passed.\n";
}

int testMapCacheReparse(const fs::path& root) {
    SmokeContext context;
    std::vector<std::uint8_t> bytes;
    const auto offer = mapCacheFixture(bytes, context);
    fs::path output = "sentinel";
    std::string error;
    const bool rejected = !storeRuntimeMultiplayerMapCache(root, offer, bytes, output, error);
    context.expect(rejected && output == fs::path("sentinel") && !error.empty(),
        "cache refuses a Windows junction: " + error);
    if (context.failures) return 1;
    std::cout << "Map cache: Windows junction rejected before writing outside the cache.\n"; return 0;
}
