// Integrated after the unchanged production baseline finished. Uses the
// public save/export/stage/commit
// paths, not a serialization-only copy of the effect under test.
bool runRuntimeCrateFeedbackSaveSmoke(
    RuntimeWindowState& live,
    const std::filesystem::path& output,
    std::string& error
) {
    const auto fail = [&](const std::string& why) {
        error = "crate feedback save: " + why;
        return false;
    };
    const auto root = output / "save";
    std::filesystem::create_directories(root);
    std::ofstream records(root / "continuation.csv");
    if (!records) return fail("evidence file");
    records << "reward,tick,age,present,frame,remaining,world_level,source_hash,restored_hash,pixel_mismatch,pass\n";
    int clearTile = -1;
    const int clearSet = live.theater.getGeneralTileSetIndex("ClearTile");
    const auto terrainContext = runtimeTerrainMetadataContext(live);
    for (int tile = 0; tile < static_cast<int>(live.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        if (live.theater.resolveTileIndex(tile).tileSetIndex == clearSet
            && resolveRuntimeTerrainSubTileLandType(cell, terrainContext, live.renderCache) == RuntimeTerrainLandType::Clear
            && resolveRuntimeTerrainSubTileRampType(cell, terrainContext, live.renderCache) == 0) {
            clearTile = tile; break;
        }
    }
    if (clearTile < 0) return fail("original flat Clear TMP");
    constexpr RuntimeCellPoint crateCell{20, 20};
    constexpr int collectorId = 101, hz = 30;
    // Both selected stock animations have 23 frames and Rate=400. The raw
    // resource oracle in the companion test pins these facts independently.
    constexpr int delay = 4, total = 92, saveAge = 20;
    const double expectedLevel = 200.0 * std::sqrt(6.0) / 256.0;
    const auto make = [&]() {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& s = *storage; seedRuntimeLoadScratch(s, live, false);
        RuntimeSnapshotActiveStateScope active(s);
        s.world = live.world; s.world.accessObjects().clear();
        s.world.accessTerrainCells().clear(); s.world.accessTerrainObjects().clear();
        s.world.accessSmudges().clear(); s.terrainInfoByCell.clear(); s.terrainByCell.clear();
        s.mapFacts = {}; s.mapFactsLoaded = false;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.playerSlots.clear(); s.config.playerSlots.resize(2);
        for (int i = 0; i < 2; ++i) {
            auto& slot = s.config.playerSlots[i];
            slot.enabled = slot.human = true; slot.local = i == 0;
            slot.startPosition = i + 1; slot.country = i ? "Russians" : "Americans";
            slot.team = "-"; slot.color = i ? "Red" : "Gold";
            slot.peerId = i ? "crate-save-remote" : "crate-save-local";
            slot.name = i ? "Distant opponent" : "Crate collector";
        }
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.shortGame = false;
        s.config.crates = s.config.superweapons = false;
        s.campaignMode = false; s.config.gameMode = "Battle"; s.config.playerCountry = "Americans";
        s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        initializeRuntimeSkirmishPlayerIdentities(s, "Americans");
        s.economy.credits = 10000; s.economy.skirmishStartApplied = true;
        s.triggers.loaded = s.triggers.bootstrapComplete = true;
        s.visionEnabled = s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        s.skirmishCrates.clear(); s.initialSkirmishCrates.clear(); s.impactEffects.clear();
        for (int y = 0; y < 40; ++y) for (int x = 0; x < 40; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = y * 512 + x;
            cell.tileIndex = clearTile; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        const auto add = [&](int id, const char* type, WorldObjectKind kind, int x, int y) {
            WorldObjectInstance object;
            object.instanceId = id; object.type = type; object.kind = kind;
            object.mapId = "crate-save-" + std::to_string(id); object.owner = s.economy.owner;
            object.x = x; object.y = y; object.cell = runtimeCellKey(x, y);
            object.alive = object.poweredOperational = true; object.mission = "Guard";
            object.currentAmmo = -1; object.health = runtimeMaxHealthForObject(s, object);
            clearRuntimeRenderPosition(object); s.world.accessObjects().push_back(object);
        };
        add(collectorId, "MTNK", WorldObjectKind::Unit, 18, 20);
        add(102, "GAPOWR", WorldObjectKind::Structure, 5, 5); // Suppresses FreeMCV substitution.
        add(103, "NAPOWR", WorldObjectKind::Structure, 35, 35);
        findMutableObjectById(s, 103)->owner = runtimeSkirmishOwnerForSlot(1);
        initializeRuntimeNavigation(s); initializeRuntimeLocalProductionLanes(s.economy);
        s.structureFootprints = buildRuntimeStructureFootprints(s);
        rebuildRuntimeEconomyPower(s); initializeRuntimeHumanHouseAuthorities(s);
        s.vision.reset(); s.vision.update(s.world, &s.rules, {{s.economy.owner, {}}}, s.tick);
        return storage;
    };
    const auto step = [](RuntimeWindowState& state) {
        RuntimeSnapshotActiveStateScope scope(state); ++state.tick; advanceRuntimeSimulation(state, false);
    };
    const auto exactHash = [&](RuntimeWindowState& state) {
        RuntimeSnapshotActiveStateScope scope(state);
        return runtimeMultiplayerAuthoritativeStateHash(state, error);
    };
    const auto capture = [&](RuntimeWindowState& state, std::vector<std::uint32_t>& pixels) {
        RuntimeSnapshotActiveStateScope scope(state);
        return drawRuntimeStateOffscreenAtCell(state, crateCell, 320, 240,
            error, "", false, false, -1, &pixels).has_value();
    };
    int ordinarySaves = 0, exactSaves = 0, pairedTicks = 0, renderedPairs = 0;
    for (const char* reward : {"Money", "Armor"}) {
        const std::string label = reward;
        const std::string animation = label == "Money" ? "MONEY" : "ARMOR";
        auto owned = make(); auto& s = *owned; RuntimeSnapshotActiveStateScope active(s);
        const auto* rule = runtimeCrateRuleForType(s, reward);
        if (!rule) return fail(label + " stock rule");
        RuntimeCrateState crate; crate.cell = crateCell; crate.authored = true; crate.imageId = "CRATE";
        runtimeConfigureCratePowerup(crate, *rule);
        s.skirmishCrates = {crate}; s.initialSkirmishCrates = s.skirmishCrates;
        const int beforeCredits = s.economy.credits;
        applySelection(s, {collectorId}, false, false);
        if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, crateCell)
            || s.lastCommand.acceptedCount != 1) return fail(label + " UI Move");
        int waited = 0;
        while (s.skirmishCrates.front().active && waited++ < hz * 40) step(s);
        if (s.skirmishCrates.front().active) return fail(label + " real pickup never happened");
        const int pickupTick = s.tick;
        const auto* collector = findObjectById(s, collectorId);
        if (!collector || s.interactionStatus.find("collected " + label + " crate") == std::string::npos
            || (label == "Money" && (s.economy.credits - beforeCredits < 2000 || s.economy.credits - beforeCredits > 2900))
            || (label == "Armor" && collector->crateArmorMultiplier != 1.5)) return fail(label + " actual reward");
        const int awardedCredits = s.economy.credits;
        const double awardedArmor = collector->crateArmorMultiplier;
        applySelection(s, {}, false, false);
        while (s.tick - pickupTick < saveAge) step(s);
        if (s.impactEffects.size() != 1 || s.impactEffects.front().animId != animation
            || s.impactEffects.front().totalTicks != total
            || s.impactEffects.front().remainingTicks != total - saveAge
            || s.impactEffects.front().frameIndex != saveAge / delay
            || !s.impactEffects.front().hasWorldPosition
            || std::abs(s.impactEffects.front().worldLevel - expectedLevel) > 1e-12)
            return fail(label + " no real mid-animation save window");
        const auto rewardStable = [&](const RuntimeWindowState& state) {
            const auto* unit = findObjectById(state, collectorId);
            return unit && unit->alive && unit->crateArmorMultiplier == awardedArmor
                && state.economy.credits == awardedCredits && state.skirmishCrates.size() == 1
                && !state.skirmishCrates.front().active && state.skirmishCrates.front().authored
                && state.skirmishCrates.front().cell == crateCell;
        };

        // v14 deliberately clears ordinary transient effects. It must still
        // retain the reward and the consumed one-shot crate; never replay it.
        const auto ordinaryPath = root / (label + "-v14.ini");
        if (!saveRuntimeState(s, pathString(ordinaryPath), error)) return fail(label + " v14 write: " + error);
        auto ordinary = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*ordinary, s, false);
        { RuntimeSnapshotActiveStateScope destination(*ordinary);
            if (!loadRuntimeState(*ordinary, pathString(ordinaryPath), error)) return fail(label + " v14 load: " + error); }
        if (!ordinary->impactEffects.empty() || !rewardStable(*ordinary)) return fail(label + " v14 transient/reward contract");
        for (int n = 0; n < total + delay; ++n) {
            step(*ordinary);
            if (!ordinary->impactEffects.empty() || !rewardStable(*ordinary)) return fail(label + " v14 repeated reward/feedback");
        }
        ++ordinarySaves;

        // The same mature two-human slot identities as the MCV exact-snapshot
        // fixture, with real authority clocks and a nonzero shared map recipe.
        s.config.multiplayerActive = true; s.config.multiplayerSetupHash = 1;
        s.config.multiplayerCanonicalMapId = "stock-crate-feedback";
        s.config.multiplayerMapContentFingerprint = 2; s.config.multiplayerTimelineEpoch = 1;
        s.config.simulationRandomSeed = 1;
        completeRuntimeWorldRefresh(s);
        s.particles.reset(hz); s.particlesInitialized = true;
        s.superWeapons.reset(std::vector<SuperWeaponRule>{}); s.superWeaponsInitialized = true;
        if (!s.particles.alignEmptyStateTick(s.tick) || !s.superWeapons.alignEmptyStateTick(s.tick))
            return fail(label + " v15 authority clocks");
        const auto hash = exactHash(s);
        RuntimeMultiplayerSnapshotIdentity identity;
        identity.proposalId = 1; identity.sourceTimelineEpoch = 1;
        identity.kind = RuntimeMultiplayerSnapshotKind::Load;
        identity.snapshotTick = identity.barrierTick = s.tick;
        identity.snapshotToken = "crate-feedback-" + label;
        identity.authoritativeStateHash = hash; identity.gameplaySetupFingerprint = 1;
        RuntimeMultiplayerSnapshotMemoryImage image; RuntimeMultiplayerSnapshotStagedState staged;
        if (!hash || !exportRuntimeMultiplayerSnapshotV15(s, identity, image, error)
            || !stageRuntimeMultiplayerSnapshot(s, image.bytes, image.descriptor, staged, error) || !staged.scratch)
            return fail(label + " v15 export/stage: " + error);
        if (exactHash(*staged.scratch) != hash) return fail(label + " staged authority hash");
        { std::ofstream file(root / (label + "-v15.ini"), std::ios::binary);
            file.write(reinterpret_cast<const char*>(image.bytes.data()), static_cast<std::streamsize>(image.bytes.size()));
            if (!file) return fail(label + " v15 evidence"); }
        auto exact = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*exact, s, false);
        { RuntimeSnapshotActiveStateScope destination(*exact);
            commitRuntimeMultiplayerSnapshotAuthority(*exact, std::move(staged)); }
        ++exactSaves;
        while (s.tick - pickupTick <= total + delay) {
            const int age = s.tick - pickupTick; const bool present = age < total;
            const auto leftHash = exactHash(s), rightHash = exactHash(*exact);
            bool equal = leftHash && leftHash == rightHash && s.tick == exact->tick
                && rewardStable(s) && rewardStable(*exact)
                && s.impactEffects.size() == (present ? 1U : 0U)
                && exact->impactEffects.size() == s.impactEffects.size();
            if (present && equal) {
                const auto& left = s.impactEffects.front(); const auto& right = exact->impactEffects.front();
                equal = runtimeMultiplayerSnapshotImpactRecord(left) == runtimeMultiplayerSnapshotImpactRecord(right)
                    && left.animId == animation && right.animId == animation
                    && left.renderIntent == right.renderIntent && left.frameIndex == right.frameIndex
                    && left.frameIndex == age / delay && left.totalTicks == total && left.remainingTicks == total - age
                    && left.hasWorldPosition && right.hasWorldPosition
                    && left.worldX == crateCell.x && left.worldY == crateCell.y
                    && std::abs(left.worldLevel - expectedLevel) < 1e-12;
            }
            std::size_t mismatch = 0;
            if (equal && (age == saveAge || age == 10 * delay || age == 21 * delay || age == total)) {
                std::vector<std::uint32_t> left, right;
                if (!capture(s, left) || !capture(*exact, right)) return fail(label + " continuation render: " + error);
                if (left.size() != right.size()) mismatch = std::max(left.size(), right.size());
                else for (std::size_t pixel = 0; pixel < left.size(); ++pixel)
                    mismatch += ((left[pixel] ^ right[pixel]) & 0x00ffffffU) != 0;
                ++renderedPairs;
            }
            equal = equal && mismatch == 0;
            records << label << ',' << s.tick << ',' << age << ',' << present << ','
                << (present && !s.impactEffects.empty() ? s.impactEffects.front().frameIndex : -1) << ','
                << (present && !s.impactEffects.empty() ? s.impactEffects.front().remainingTicks : 0) << ','
                << std::setprecision(17) << (present && !s.impactEffects.empty() ? s.impactEffects.front().worldLevel : 0.0)
                << ',' << leftHash << ',' << rightHash << ',' << mismatch << ',' << equal << '\n';
            if (!equal) return fail(label + " exact continuation at age " + std::to_string(age) + ": " + error);
            if (age == total + delay) break;
            step(s); step(*exact); ++pairedTicks;
        }
    }
    std::cout << "Crate feedback save: v14=" << ordinarySaves << " v15=" << exactSaves
        << " pairedTicks=" << pairedTicks << " renderedPairs=" << renderedPairs
        << " exactWorldPosition=yes rewardOnce=yes expiredTogether=yes\n";
    error.clear(); return true;
}
