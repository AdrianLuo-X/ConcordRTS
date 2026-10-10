// Included after the existing Mixed/native and complete-writer contract helpers.
// This is a private API/actual renderer contract, not a production fixture.
namespace RuntimePaintSecretLabListContractDetail {
bool equal(const std::vector<RuntimeSpecialBuildingUnlock>& a,
    const std::vector<RuntimeSpecialBuildingUnlock>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i != a.size(); ++i)
        if (a[i].sourceObjectId != b[i].sourceObjectId || a[i].owner != b[i].owner
            || a[i].kind != b[i].kind || a[i].typeId != b[i].typeId) return false;
    return true;
}
struct Query { std::string owner, type; RuntimeSpecialBuildingUnlockKind kind; };
std::vector<Query> queries(const std::string& owner,
    const std::vector<RuntimeSpecialBuildingUnlock>& original) {
    std::vector<Query> output;
    if (!original.empty()) output.push_back({original.front().owner,
        original.front().typeId, original.front().kind});
    const std::array<Query, 16> stock{{
        {owner,"MTNK",RuntimeSpecialBuildingUnlockKind::Unit},
        {owner,"SREF",RuntimeSpecialBuildingUnlockKind::Unit},
        {owner,"TTNK",RuntimeSpecialBuildingUnlockKind::Unit},
        {owner,"FV",RuntimeSpecialBuildingUnlockKind::Unit},
        {owner,"SCHP",RuntimeSpecialBuildingUnlockKind::Unit},
        {owner,"HTNK",RuntimeSpecialBuildingUnlockKind::Unit},
        {owner,"E1",RuntimeSpecialBuildingUnlockKind::Infantry},
        {owner,"CLEG",RuntimeSpecialBuildingUnlockKind::Infantry},
        {owner,"JUMPJET",RuntimeSpecialBuildingUnlockKind::Infantry},
        {owner,"SNIPE",RuntimeSpecialBuildingUnlockKind::Infantry},
        {owner,"GHOST",RuntimeSpecialBuildingUnlockKind::Infantry},
        {owner,"E2",RuntimeSpecialBuildingUnlockKind::Infantry},
        {owner,"GAPOWR",RuntimeSpecialBuildingUnlockKind::Building},
        {owner,"GATECH",RuntimeSpecialBuildingUnlockKind::Building},
        {owner,"GAWEAP",RuntimeSpecialBuildingUnlockKind::Building},
        {owner,"GACNST",RuntimeSpecialBuildingUnlockKind::Building}}};
    for (const auto& query : stock) {
        if (output.size() == 14) break;
        if (std::none_of(output.begin(), output.end(), [&](const auto& prior) {
            return prior.owner == query.owner && prior.type == query.type && prior.kind == query.kind;
        })) output.push_back(query);
    }
    return output;
}
bool primitives(const RuntimeWindowState& assets, std::string& error,
    std::uint64_t& boundaries) {
    const auto fail = [&](const char* why) { error = std::string("paint secret lab API: ") + why; return false; };
    const auto* labRules = assets.rules.findBuildingType("CASLAB");
    if (!labRules || !labRules->secretLab) return fail("actual stock CASLAB rules missing");
    std::vector<WorldObjectInstance> objects;
    WorldObjectInstance lab; lab.instanceId = 101; lab.kind = WorldObjectKind::Structure;
    lab.type = "CASLAB"; lab.owner = "Americans"; lab.mapId = "OriginalMapLab";
    lab.alive = true; lab.health = std::max(1, labRules->strength); objects.push_back(lab);
    RuntimeSpecialBuildingContext context;
    context.rules = &assets.rules; context.objects = &objects;
    context.canonicalOwnerId = [](const std::string& owner) { return owner; };
    context.ownerIsNeutral = [](const std::string& owner) { return owner == "Neutral"; };
    const auto oracle = [&] { return RuntimeSpecialBuildingSystem::secretLabUnlocks(context); };
    const auto call = [&](std::vector<RuntimeSpecialBuildingUnlock>& scratch) -> const auto& {
        return runtimeSecretLabUnlocksForQuery(context, scratch);
    };
    const auto original = oracle();
    if (original.empty()) return fail("actual stock lab must grant a decoded candidate");
    RuntimeSecretLabMemoReferenceScope optimized(false);
    for (int mode = 0; mode != 3; ++mode) {
        RuntimeSecretLabMemoObservation observation;
        RuntimeSecretLabMemoObservationScope observe(observation);
        RuntimeSecretLabMemoReferenceScope reference(mode == 2);
        RuntimeSecretLabReadOnlyMemoScope lease(context, mode != 0);
        std::vector<RuntimeSpecialBuildingUnlock> a, b;
        if (!equal(call(a), original) || !equal(call(b), original)
            || observation.fullBuilds != (mode == 1 ? 1 : 2)
            || observation.hits != (mode == 1 ? 1 : 0)) return fail("default/ref/positive complete list");
        ++boundaries;
    }
    {
        RuntimeSecretLabMemoObservation observation;
        RuntimeSecretLabMemoObservationScope observe(observation);
        RuntimeSecretLabReadOnlyMemoScope lease(context, true);
        std::vector<RuntimeSpecialBuildingUnlock> scratch;
        if (!equal(call(scratch), original)) return fail("foreign prime");
        auto foreign = context; std::vector<RuntimeSpecialBuildingUnlock> foreignScratch;
        if (!equal(runtimeSecretLabUnlocksForQuery(foreign, foreignScratch), original)
            || !equal(call(scratch), original) || observation.fullBuilds != 2 || observation.hits != 1)
            return fail("foreign context address is not a closure identity");
        ++boundaries;
    }
    {
        RuntimeSecretLabMemoObservation observation;
        RuntimeSecretLabMemoObservationScope observe(observation);
        RuntimeSecretLabReadOnlyMemoScope outer(context, true); std::vector<RuntimeSpecialBuildingUnlock> scratch;
        if (!equal(call(scratch), original)) return fail("nested prime");
        { RuntimeSecretLabReadOnlyMemoScope nested(context, true);
          if (!equal(call(scratch), original)) return fail("nested original"); }
        if (RuntimeSecretLabReadOnlyMemoScope::active != &outer || !equal(call(scratch), original)
            || observation.fullBuilds != 3 || observation.hits || observation.invalidations != 1)
            return fail("nested restore cannot revive invalidated outer");
        ++boundaries;
    }
    // Original general callback sequence remains exact when no readonly lease
    // is granted. The second public query must really repeat the producer.
    {
        std::vector<std::string> originalEvents, actualEvents;
        const auto invoke = [&](std::vector<std::string>& events, bool leased) {
            auto general = context;
            general.canonicalOwnerId = [&](const std::string& owner) { events.push_back("owner:" + owner); return owner; };
            general.ownerIsNeutral = [&](const std::string& owner) { events.push_back("neutral:" + owner); return owner == "Neutral"; };
            RuntimeSecretLabReadOnlyMemoScope lease(general, leased);
            const auto& q = original.front();
            for (int repeat = 0; repeat != 2; ++repeat)
                if (!RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(general, q.owner, q.kind, q.typeId)) return false;
            return true;
        };
        { RuntimeSecretLabMemoReferenceScope reference(true); if (!invoke(originalEvents, false)) return fail("callback reference"); }
        if (!invoke(actualEvents, false) || originalEvents != actualEvents) return fail("public arbitrary callbacks preserved");
        ++boundaries;
    }
    for (int mutation = 0; mutation != 12; ++mutation) {
        objects = {lab}; context.rules = &assets.rules; context.objects = &objects;
        RuntimeSecretLabReadOnlyMemoScope lease(context, true); std::vector<RuntimeSpecialBuildingUnlock> scratch;
        (void)call(scratch); lease.invalidate(); // BEFORE every in-place/source/closure write.
        switch (mutation) {
        case 0: objects[0].owner = "Neutral"; break;
        case 1: objects[0].owner = " Russians "; break;
        case 2: objects[0].alive = false; break;
        case 3: objects[0].health = 0; break;
        case 4: objects[0].rubble = true; break;
        case 5: objects[0].kind = WorldObjectKind::Unit; break;
        case 6: objects[0].mapId = "DifferentMapSeed"; break;
        case 7: ++objects[0].instanceId; break;
        case 8: objects[0].type = "caslab"; break;
        case 9: objects[0].cloaked = true; objects[0].poweredOperational = false; break;
        case 10: objects[0].crateArmorMultiplier = 2; objects[0].crateFirepowerMultiplier = 3; break;
        case 11: objects.push_back(objects[0]); objects.back().owner = "Russians"; break;
        }
        if (!equal(call(scratch), oracle())) return fail("source fields/duplicate order/hidden/upgrades original equivalence");
        ++boundaries;
    }
    objects = {lab};
    {
        RuntimeSecretLabMemoObservation observation; RuntimeSecretLabMemoObservationScope observe(observation);
        RuntimeSecretLabReadOnlyMemoScope lease(context, true); std::vector<RuntimeSpecialBuildingUnlock> scratch;
        (void)call(scratch); auto replacement = objects; replacement[0].owner = "Russians";
        const auto* prior = objects.data(); objects.swap(replacement);
        if (objects.data() == prior || !equal(call(scratch), oracle()) || observation.hits)
            return fail("same-count new storage fallback");
        ++boundaries;
    }
    objects = {lab};
    {
        RuntimeSecretLabMemoObservation observation; RuntimeSecretLabMemoObservationScope observe(observation);
        RuntimeSecretLabReadOnlyMemoScope lease(context, true); std::vector<RuntimeSpecialBuildingUnlock> scratch;
        (void)call(scratch); objects.push_back(lab);
        if (!equal(call(scratch), oracle()) || observation.hits) return fail("changed count fallback");
        ++boundaries;
    }
    objects.assign(RuntimeSecretLabReadOnlyMemoScope::maximumUnlocks + 1, lab);
    for (std::size_t i = 0; i != objects.size(); ++i) {
        objects[i].instanceId += static_cast<int>(i); objects[i].mapId += std::to_string(i);
    }
    {
        RuntimeSecretLabMemoObservation observation; RuntimeSecretLabMemoObservationScope observe(observation);
        RuntimeSecretLabReadOnlyMemoScope lease(context, true); std::vector<RuntimeSpecialBuildingUnlock> a, b;
        const auto expected = oracle();
        if (expected.size() <= RuntimeSecretLabReadOnlyMemoScope::maximumUnlocks
            || !equal(call(a), expected) || !equal(call(b), expected) || observation.hits
            || observation.publications || observation.capFallbacks != 1 || observation.fullBuilds != 2)
            return fail("complete-list cap retains original fallback, never partial list");
        ++boundaries;
    }
    objects = {lab};
    {
        RuntimeSecretLabReadOnlyMemoScope lease(context, true); std::vector<RuntimeSpecialBuildingUnlock> scratch;
        (void)call(scratch); context.rules = nullptr;
        if (!equal(call(scratch), oracle())) return fail("rules pointer defensive fallback");
        context.rules = &assets.rules; ++boundaries;
    }
    {
        RuntimeSecretLabReadOnlyMemoScope lease(context, true); std::vector<RuntimeSpecialBuildingUnlock> scratch;
        (void)call(scratch); ++context.fixedTickHz;
        if (!equal(call(scratch), oracle())) return fail("context fixedTickHz fallback");
        ++boundaries;
    }
    {
        auto changedRules = assets.rules; auto changedContext = context; changedContext.rules = &changedRules;
        RuntimeSecretLabReadOnlyMemoScope lease(changedContext, true);
        std::vector<RuntimeSpecialBuildingUnlock> scratch;
        if (runtimeSecretLabUnlocksForQuery(changedContext, scratch).empty()) return fail("same-address rules prime");
        lease.invalidate();
        auto* changedLab = const_cast<BuildingType*>(changedRules.findBuildingType("CASLAB"));
        if (!changedLab) return fail("private rules readback");
        changedLab->secretLab = false;
        if (!equal(runtimeSecretLabUnlocksForQuery(changedContext, scratch),
                RuntimeSpecialBuildingSystem::secretLabUnlocks(changedContext)))
            return fail("explicit invalidate before same-address rule mutation");
        ++boundaries;
    }
    {
        auto aliases = context;
        aliases.canonicalOwnerId = [](const std::string& owner) {
            return owner == "LocalAlias" ? std::string("AMERICANS") : owner;
        };
        RuntimeSecretLabReadOnlyMemoScope lease(aliases, true);
        const auto& grant = original.front();
        const std::array<Query, 5> test{{
            {"LocalAlias", grant.typeId, grant.kind},
            {"  americans  ", grant.typeId, grant.kind},
            {"", grant.typeId, grant.kind},
            {"Americans", "", grant.kind},
            {"Russians", grant.typeId, grant.kind}}};
        std::array<bool, 5> expected{}, actual{};
        { RuntimeSecretLabMemoReferenceScope reference(true);
          auto originalContext = aliases;
          for (std::size_t i = 0; i != test.size(); ++i) expected[i] = RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(
              originalContext, test[i].owner, test[i].kind, test[i].type); }
        for (std::size_t i = 0; i != test.size(); ++i) actual[i] = RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(
            aliases, test[i].owner, test[i].kind, test[i].type);
        if (expected != actual || !actual[0] || !actual[1] || actual[2] || actual[3] || actual[4])
            return fail("original owner aliases/trim/case and empty/foreign negatives");
        ++boundaries;
    }
    return boundaries == 25;
}
}

bool runRuntimePaintSecretLabListContract(const RuntimeConfig& config, std::string& error) {
    using namespace RuntimePaintSecretLabListContractDetail;
    const auto fail = [&](const std::string& why) { error = "paint secret lab UI: " + why; return false; };
    auto assets = std::make_unique<RuntimeWindowState>(); assets->config = config;
    assets->report = buildRuntimeStartupReport(config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success || !assets->report.rulesLoaded || !assets->report.artLoaded)
        return fail("normal stock startup unavailable");
    std::uint64_t boundaries = 0;
    if (!primitives(*assets, error, boundaries)) return false;
    const auto* stockLab = assets->rules.findBuildingType("CASLAB");
    if (!stockLab || !stockLab->secretLab) return fail("CASLAB stock source");
    struct Screen { HDC dc = GetDC(nullptr); ~Screen() { if (dc) ReleaseDC(nullptr, dc); } } screen;
    if (!screen.dc) return fail("screen DC");
    std::uint64_t pairs = 0, apiQueries = 0, savedBuilds = 0, positiveLabs = 0, writerCaptures = 0;
    std::uint64_t sidebarHits = 0;
    constexpr int width = 1024, height = 640;
    const RECT client{0, 0, width, height};
    for (int scene = 0; scene != 4; ++scene) {
        // Each route gets its own normal decoder/initialization. Clearing only
        // renderCache on one state preserves its UI frame/bitmap caches and
        // makes the second route skip original auxiliary SHP load requests.
        std::array<std::unique_ptr<RuntimeWindowState>, 2> states;
        std::array<std::vector<int>, 2> ids;
        struct ReleaseStateBuffers {
            decltype(states)& values;
            ~ReleaseStateBuffers() {
                GdiFlush();
                for (auto& value : values) if (value) {
                    releaseRuntimeBackBuffer(value->backBuffer);
                    releaseRuntimeBackBuffer(value->sidebarStaticBuffer);
                    releaseRuntimeBackBuffer(value->radarTerrainBuffer);
                }
            }
        } releaseStateBuffers{states};
        for (int route = 0; route != 2; ++route) {
            states[route] = std::make_unique<RuntimeWindowState>();
            auto& state = states[route];
            if (!RuntimeMixedLocalComponentContractDetail::prepareNative(*state, *assets, config,
                240, (scene & 1) ? 10000 : 0, false, ids[route], error)) return false;
            RuntimeSnapshotActiveStateScope activeRoute(*state);
            state->runtimePreferencePersistenceEnabled = false;
            state->runtimeSuppressPlayerAudioFeedback = true; state->runtimeSoundsEnabled = false;
            if (scene >= 2) {
                // Private stock rule/asset fixture, not a claim of legitimate player
                // production. The original API sees the full real authored identity.
                WorldObjectInstance lab; lab.instanceId = nextRuntimeObjectInstanceId(*state);
                if (lab.instanceId <= 0) return fail("unique private stock lab ID");
                lab.kind = WorldObjectKind::Structure; lab.type = "CASLAB";
                lab.owner = state->economy.owner; lab.mapId = "SecretLabUiStockFixture";
                const auto foundCell = state->terrainByCell.find(runtimeCellKey(142, 144));
                if (foundCell == state->terrainByCell.end() || !foundCell->second)
                    return fail("actual interior lab terrain cell");
                const auto& cell = *foundCell->second;
                lab.x = cell.x; lab.y = cell.y; lab.level = cell.level;
                lab.cell = runtimeCellKey(lab.x, lab.y); lab.alive = true;
                lab.health = std::max(1, stockLab->strength); lab.mission = "Guard";
                state->world.accessObjects().push_back(lab);
                auto metadata = std::move(state->terrainInfoByCell);
                state->objectIndex = RuntimeObjectIndex{}; initializeRuntimeNavigation(*state);
                state->terrainInfoByCell = std::move(metadata);
                (void)completeRuntimeWorldRefresh(*state);
            }
            if (!state->uiShpByKey.empty() || !state->uiShpFrameCountByKey.empty()
                || !state->uiPalettesByName.empty() || !state->uiPaletteLoadAttempts.empty()
                || !state->cameoIconsByKey.empty() || !state->superWeaponIconsByKey.empty()
                || !state->cameoCacheKeyByItem.empty() || state->cameoPaletteLoadAttempted
                || state->cameoPaletteLoaded || !state->cameoPalette.empty()
                || state->uiResourceRenderSummaryValid || !state->sidebarStaticKey.empty()
                || !state->radarTerrainKey.empty() || state->backBuffer.bitmap
                || state->sidebarStaticBuffer.bitmap || state->radarTerrainBuffer.bitmap)
                return fail("independent normal renderer states must begin with cold UI caches");
        }
        if (ids[0] != ids[1]) return fail("independent normal actor ID/order mismatch");
        auto& state = states[0];
        RuntimeSnapshotActiveStateScope active(*state);
        if (scene == 0) {
            { RuntimePaintSecretLabPhaseScope phase(*state);
              if (phase.contextFor(*state)) return fail("unqualified phase must not opt in"); ++boundaries; }
            { RuntimeSecretLabMemoReferenceScope reference(true); RuntimePaintSecretLabPhaseScope phase(*state, true);
              if (phase.contextFor(*state)) return fail("original reference phase"); ++boundaries; }
            { RuntimeSecretLabMemoReferenceScope reference(false); RuntimePaintSecretLabPhaseScope phase(*state, true);
              const auto* context = phase.contextFor(*state);
              if (!context || phase.contextFor(*assets) || phase.contextFor(*state) != context)
                  return fail("foreign state cannot borrow phase"); ++boundaries; }
            { RuntimeSecretLabMemoReferenceScope reference(false); RuntimePaintSecretLabPhaseScope outer(*state, true);
              if (!outer.contextFor(*state)) return fail("phase nested prime");
              { RuntimePaintSecretLabPhaseScope nested(*state, true);
                if (nested.contextFor(*state)) return fail("phase nested fallback"); }
              if (RuntimePaintSecretLabPhaseScope::active != &outer || outer.contextFor(*state))
                  return fail("phase nested restore must leave outer revoked"); ++boundaries; }
        }
        const auto originalContext = makeRuntimeSpecialBuildingContext(*state);
        const auto originalList = RuntimeSpecialBuildingSystem::secretLabUnlocks(originalContext);
        if ((scene >= 2) != !originalList.empty()) return fail("stock no-lab/positive lab original API witness");
        positiveLabs += scene >= 2;
        const auto queries14 = queries(state->economy.owner, originalList);
        if (queries14.size() != 14) return fail("fourteen distinct original kind/type queries");
        std::array<RuntimeSecretLabMemoObservation, 2> observations;
        std::array<std::vector<bool>, 2> decisions;
        for (int route = 0; route != 2; ++route) {
            auto& state = states[route];
            RuntimeSnapshotActiveStateScope activeRoute(*state);
            RuntimeSecretLabMemoReferenceScope reference(route == 0);
            RuntimeSecretLabMemoObservationScope observe(observations[route]);
            RuntimePaintSecretLabPhaseScope phase(*state, true);
            for (const auto& query : queries14) {
                const auto* context = phase.contextFor(*state);
                const auto fallback = context ? RuntimeSpecialBuildingContext{} : makeRuntimeSpecialBuildingContext(*state);
                decisions[route].push_back(RuntimeSpecialBuildingSystem::ownerHasSecretLabUnlock(
                    context ? *context : fallback, query.owner, query.kind, query.type));
            }
        }
        if (decisions[0] != decisions[1] || (scene >= 2 && !decisions[0].front())
            || observations[0].queries != 14 || observations[1].queries != 14
            || observations[0].fullBuilds != 14 || observations[1].fullBuilds != 1
            || observations[0].hits || observations[1].hits != 13
            || observations[1].publications != 1) return fail("14 unique public API queries omit exactly 13 complete scans");
        apiQueries += 14; savedBuilds += 13;
        RuntimePlannerPassabilityContractDetail::Authority before, after;
        if (!RuntimePlannerPassabilityContractDetail::capture(*state, before, error)) return false;
        ++writerCaptures;
        std::array<RuntimeMapRenderStats, 2> stats;
        std::array<RuntimeMapRenderCache, 2> caches;
        std::array<std::vector<std::uint32_t>, 2> raw;
        std::array<RuntimeSecretLabMemoObservation, 2> sidebarObservations;
        std::array<std::array<std::uint64_t, 7>, 2> radar{};
        for (int route = 0; route != 2; ++route) {
            auto& state = states[route];
            RuntimeSnapshotActiveStateScope activeRoute(*state);
            state->renderCache = {}; state->projectedObjectsCache.clear(); invalidateRuntimeObjectProjectionCache(*state);
            RuntimeSecretLabMemoReferenceScope reference(route == 0);
            std::vector<std::uint32_t> mapPixels;
            const auto frame = drawRuntimeStateOffscreenAtCell(*state, RuntimeCellPoint{136, 140},
                width, height, error, "", false, false, -1, &mapPixels, false, nullptr, false, true);
            if (!frame || !frame->rendered || mapPixels.size() != static_cast<std::size_t>(width) * height)
                return fail("complete production map pixels: " + error);
            stats[route] = *frame;
            if (scene >= 2 && (frame->visibleStructureSprites == 0
                || std::none_of(state->renderCache.objectShpSpritesByType.begin(),
                    state->renderCache.objectShpSpritesByType.end(), [](const auto& entry) {
                        return toLowerAsciiLocal(entry.first).find("caslab") != std::string::npos
                            && entry.second.loaded && !entry.second.image.pixels.empty();
                    }))) return fail("actual visible stock CASLAB SHP load witness");
            RuntimeBackBuffer buffer;
            struct Release { RuntimeBackBuffer& b; ~Release() { releaseRuntimeBackBuffer(b); } } release{buffer};
            if (!ensureRuntimeBackBuffer(buffer, screen.dc, width, height)) return fail("actual top-down UI DIB");
            GdiFlush(); std::copy(mapPixels.begin(), mapPixels.end(), static_cast<std::uint32_t*>(buffer.pixels));
            {
                RuntimeSecretLabMemoObservationScope observe(sidebarObservations[route]);
                RuntimePaintTechTreeMemoScope decisionsMemo;
                // Executes the real sidebar entry and its scoped memo; the
                // normal fixed presentation time avoids clock blink differences.
                drawRuntimeSidebar(buffer.memoryDc, client, *state, 1000);
            }
            GdiFlush(); const auto* pixels = static_cast<const std::uint32_t*>(buffer.pixels);
            raw[route].assign(pixels, pixels + mapPixels.size());
            caches[route] = state->renderCache;
            radar[route] = {static_cast<std::uint64_t>(state->lastRadarResourceCells),
                static_cast<std::uint64_t>(state->lastRadarResourceOreCells),
                static_cast<std::uint64_t>(state->lastRadarResourceGemCells),
                static_cast<std::uint64_t>(state->lastRadarResourcePixels),
                static_cast<std::uint64_t>(state->lastRadarResourceRuleColors),
                static_cast<std::uint64_t>(state->lastRadarResourceFallbackColors), state->lastRadarResourceHash};
            if (!RuntimePlannerPassabilityContractDetail::capture(*state, after, error)) return false;
            ++writerCaptures;
            if (before != after || state->tick != 0) return fail("actual completed tick/full writer changed during readonly UI");
        }
        std::string field;
        if (raw[0] != raw[1]) return fail("scene=" + std::to_string(scene) + " complete raw32 pixels");
        if (radar[0] != radar[1]) return fail("scene=" + std::to_string(scene) + " all seven radar resource fields");
        if (!runtimePaintProjectionSignatureContractRenderEqual(stats[0], stats[1], caches[0], caches[1], field))
            return fail("scene=" + std::to_string(scene) + " full353/deepResources " + field
                + " auxiliaryShpBytesCacheMisses(original=" + std::to_string(stats[0].auxiliaryShpBytesCacheMisses)
                + ",memoized=" + std::to_string(stats[1].auxiliaryShpBytesCacheMisses) + ")");
        const auto& oldUi = sidebarObservations[0]; const auto& newUi = sidebarObservations[1];
        if (oldUi.queries != newUi.queries || newUi.queries < 2 || oldUi.fullBuilds != oldUi.queries
            || oldUi.hits || newUi.fullBuilds != 1 || newUi.publications != 1
            || newUi.hits != newUi.queries - 1) return fail("actual sidebar entry positive complete-list reuse");
        sidebarHits += newUi.hits;
        ++pairs;
    }
    if (boundaries != 29 || pairs != 4 || apiQueries != 56 || savedBuilds != 52
        || positiveLabs != 2 || writerCaptures != 12 || sidebarHits == 0) return fail("strict coverage totals");
    std::cout << "Paint secret lab list: boundaries=29 uniqueQueries=56 pairs=4 originalScansOmitted=52 "
        "positiveLabs=2 fullWriterCaptures=12 completedTick=0 actualSidebarPairs=4 actualSidebarHits="
        << sidebarHits << " originalPublicAPI/raw32/353stats/deepResources/radar=exact passed=yes\n";
    return true;
}
