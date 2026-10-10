void runMissingMapLobby(SmokeContext& context) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("concord_map_lobby_"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "client");
    const auto hostPath = root / "host.map";
    const auto secondPath = root / "second.map";
    const auto thirdPath = root / "third.map";
    const std::string padding(runtimeMultiplayerSnapshotChunkBytes * 2 + 23, 'x');
    { std::ofstream out(hostPath, std::ios::binary); out << "[Map]\nTheater=TEMPERATE\n;" << padding; }
    { std::ofstream out(secondPath, std::ios::binary); out << "[Map]\nTheater=SNOW\n;" << padding; }
    { std::ofstream out(thirdPath, std::ios::binary); out << "[Map]\nTheater=URBAN\n;" << padding; }
    const auto options = lobbyOptions(mapFingerprint(hostPath, "multimd/lobby-flow.map", context));
    RuntimeMultiplayerLobbyFlow host, client;
    std::string error;
    if (!host.startHost(networkOptions(), options, error) || !host.verifyLocalMap(hostPath, error)
        || !client.join(multiClientJoin(host, "map-client", "Map client"), error)) {
        context.expect(false, "map lobby start: " + error); return;
    }
    if (!pumpUntil(host, client, [&]() { return client.canonicalSetupPublished()
        && host.lobby().setup() == client.lobby().setup(); }, context, "missing map admission")) return;
    fs::path downloaded;
    context.expect(!client.downloadMissingMap(root / "client", downloaded, error) && downloaded.empty()
        && !client.setReady(true).succeeded && !client.bridge()->lan().setReady(true).succeeded,
        "request is asynchronous; no partial map or ready state");
    bool sawPartial = false;
    if (!pumpUntil(host, client, [&]() {
        if (client.bridge()->lan().lobbyMapOffer() && client.bridge()->lan().lobbyMapBytes().empty()) {
            sawPartial = true;
            context.expect(!host.bridge()->lan().lobbyMapTransfersComplete() && !host.startMatch().succeeded,
                "requested chunks block start");
        }
        return client.downloadMissingMap(root / "client", downloaded, error)
            && host.bridge()->lan().lobbyMapTransfersComplete();
    }, context, "host map download")) return;
    context.expect(sawPartial && client.localMapVerified() && fs::is_regular_file(downloaded)
        && downloaded.parent_path() == fs::canonical(root / "client") / "multiplayer" / "maps",
        "verified complete map is committed only to the private cache");
    const auto initialCached = downloaded;
    const auto initialOffer = *client.bridge()->lan().lobbyMapOffer();
    const auto initialBytes = client.bridge()->lan().lobbyMapBytes();
    fs::path cached;
    context.expect(findRuntimeMultiplayerMapCache(root / "client", options.map, cached, error)
        && cached == downloaded, "subsequent joins can reuse the verified map cache");

    auto second = options.map;
    second.contentFingerprint = mapFingerprint(secondPath, second.canonicalId, context);
    context.expect(host.setMapAndRules(second, options.rules, error).accepted && host.verifyLocalMap(secondPath, error),
        "host can publish a new map");
    if (!pumpUntil(host, client, [&]() { return client.lobby().setup().map == second; }, context, "map switch")) return;
    context.expect(!client.localMapVerified() && !client.setReady(true).succeeded, "map switch clears readiness proof");
    context.expect(!client.downloadMissingMap(root / "client", downloaded, error), "second map starts downloading");
    if (!pumpUntil(host, client, [&]() { const auto* offer = client.bridge()->lan().lobbyMapOffer();
        return offer && offer->request.map == second && client.bridge()->lan().lobbyMapBytes().empty();
    }, context, "second map partial transfer")) return;
    auto third = options.map;
    third.contentFingerprint = mapFingerprint(thirdPath, third.canonicalId, context);
    context.expect(host.setMapAndRules(third, options.rules, error).accepted && host.verifyLocalMap(thirdPath, error),
        "host switches maps during an in-flight download");
    if (!pumpUntil(host, client, [&]() {
        if (client.lobby().setup().map != third) return false;
        return client.downloadMissingMap(root / "client", downloaded, error)
            && host.bridge()->lan().lobbyMapTransfersComplete();
    }, context, "discard stale map chunks and download latest map")) return;
    context.expect(downloaded != initialCached && client.bridge()->lan().lobbyMapOffer()->request.map == third
        && mapFingerprint(downloaded, third.canonicalId, context) == third.contentFingerprint,
        "old in-flight bytes cannot satisfy the new map");
    context.expect(fs::is_regular_file(initialCached)
        && mapFingerprint(hostPath, options.map.canonicalId, context) == options.map.contentFingerprint,
        "download preserves existing maps");

    // File failures preserve both the caller's path and any prior cache entry.
    fs::path untouched = "sentinel";
    auto altered = initialBytes; altered.back() ^= 1;
    context.expect(!storeRuntimeMultiplayerMapCache(root / "client", initialOffer, altered, untouched, error)
        && untouched == fs::path("sentinel"), "corrupt bytes never publish a cache path");
    { std::ofstream out(initialCached, std::ios::binary | std::ios::trunc); out << "damaged fixture"; }
    context.expect(!findRuntimeMultiplayerMapCache(root / "client", options.map, untouched, error)
        && !storeRuntimeMultiplayerMapCache(root / "client", initialOffer, initialBytes, untouched, error)
        && untouched == fs::path("sentinel"), "damaged cache is neither trusted nor overwritten");
    context.expect(host.verifyLocalMap(thirdPath, error) && host.setReady(true).succeeded && client.setReady(true).succeeded,
        "ready succeeds only after full map verification");
    if (!pumpUntil(host, client, [&]() { return host.canStart(error); }, context, "download ready consensus")) return;
    context.expect(host.startMatch().succeeded, "start downloaded-map match");
    if (!pumpUntil(host, client, [&]() { return client.bridge()->lan().phase() == RuntimeMultiplayerLanPhase::Running; }, context, "downloaded map start")) return;
    RuntimeMultiplayerLaunchConfigResult hostLaunch, clientLaunch;
    context.expect(host.makeLaunchConfig({}, hostLaunch, error) && client.makeLaunchConfig({}, clientLaunch, error)
        && hostLaunch.authority == clientLaunch.authority
        && fs::path(clientLaunch.config.defaultMapPath) == downloaded, "launch uses the verified cache and identical authoritative map");
    std::cout << "Missing map lobby: three chunks, private cache, in-flight map switch, ready/start gates and launch passed.\n";
}
