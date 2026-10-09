// Genuine player movement on original flat land/water, followed by the real
// ordinary and canonical snapshot restore paths. Startup authority must also
// survive a boundary without an active interpolation record.
bool runRuntimeVehicleStartupSaveSmoke(RuntimeWindowState& assets, std::string& error) {
    std::cout << std::unitbuf;
    std::string label;
    const auto fail = [&](const std::string& why) {
        error = "Vehicle startup save " + label + ": " + why; return false;
    };
    int landTile = -1, waterTile = -1;
    const int waterSet = assets.theater.getGeneralTileSetIndex("WaterSet");
    const auto metadata = runtimeTerrainMetadataContext(assets);
    auto metadataCache = std::make_unique<RuntimeMapRenderCache>();
    for (int tile = 0; tile < static_cast<int>(assets.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(assets, cell);
        if (!info.passable || info.shore || info.cliff || info.bridge
            || resolveRuntimeTerrainSubTileRampType(cell, metadata, *metadataCache) != 0) continue;
        if (!info.water && landTile < 0) landTile = tile;
        if (info.water && waterTile < 0 && waterSet >= 0
            && assets.theater.resolveTileIndex(tile).tileSetIndex == waterSet
            && resolveRuntimeTerrainSubTileLandType(cell, metadata, *metadataCache) == RuntimeTerrainLandType::Water)
            waterTile = tile;
        if (landTile >= 0 && waterTile >= 0) break;
    }
    if (landTile < 0 || waterTile < 0) return fail("original flat land/water unavailable");
    const auto make = [&](const char* type, bool naval) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, assets, false);
        s.config = assets.config; s.config.fixedTickHz = 30; s.config.playerCountry = "Americans";
        s.config.gameMode = "Battle"; s.config.playerSlots.clear(); s.config.gameModeRulesOverride.clear();
        s.config.skirmishLoadSavePath.clear(); s.config.startingUnits = 0;
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.noCooldown = false;
        s.config.shortGame = s.config.crates = s.config.superweapons = false; s.campaignMode = false;
        s.rules = assets.rules; s.art = assets.art;
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts = {}; s.mapFactsLoaded = false;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = s.report.map.height = -1;
        s.terrainByCell.clear(); s.terrainInfoByCell.clear();
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = s.visionEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.economy.owner = "Americans";
        for (int y = 12; y <= 36; ++y) for (int x = 12; x <= 52; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = naval ? waterTile : landTile; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        for (const RuntimeCellPoint cell : {RuntimeCellPoint{20,20}, RuntimeCellPoint{20,24}}) {
            if (auto* object = addRuntimeObjectInstance(s, RuntimeBuildKind::Unit, type, cell)) {
                object->facing = runtimeFacingFromDelta(1,0); object->harvestAutoSearchSuspended = true;
                object->mission = "Sleep";
            }
        }
        initializeRuntimeNavigation(s); ensureRuntimeSubcellsInitialized(s);
        initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        s.skirmishCountryByOwner.clear(); s.skirmishTeamByOwner.clear();
        s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        s.humanHouseAuthorities.clear(); RuntimeHumanHouseAuthority house;
        house.owner = house.country = "Americans"; house.canonicalOwner = "americans";
        house.localFacade = true; s.humanHouseAuthorities.push_back(house);
        ensureRuntimeSpecialMechanismsInitialized(s); ensureRuntimeSuperWeaponsInitialized(s);
        ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s);
        return storage;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false);
        completeRuntimeWorldRefresh(s);
    };
    const auto pose = [](const WorldObjectInstance& object) {
        return std::array<double, 3>{runtimeObjectRenderX(object), runtimeObjectRenderY(object),
            object.hasRenderPosition ? object.renderLevel : double(object.level)};
    };
    const auto behaviorHash = [](const RuntimeWindowState& s, int id) {
        std::uint64_t hash = 1469598103934665603ull;
        runtimeMultiplayerAuthoritativeMixBehavior(hash, s, id, s.behaviors.at(id)); return hash;
    };
    const auto checkpoint = [&](RuntimeWindowState& s, bool exact) {
        std::ostringstream ordinary, payload, canonical; IniFile base, snapshot;
        if (!writeRuntimeStateToStream(s, ordinary, error) || !base.loadFromString(ordinary.str()))
            return std::unique_ptr<RuntimeWindowState>{};
        if (exact && (!writeRuntimeStateToStream(s, payload, error)
            || !writeRuntimeMultiplayerSnapshotExtension(s, payload, error)
            || !snapshot.loadFromString(payload.str())
            || !writeRuntimeMultiplayerSnapshotCanonicalIni(snapshot, canonical, error)
            || !snapshot.loadFromString(canonical.str()))) return std::unique_ptr<RuntimeWindowState>{};
        auto restored = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*restored, s, false);
        RuntimeSnapshotActiveStateScope active(*restored);
        if (!materializeRuntimeLoadStateFromIni(*restored, base, error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*restored, snapshot, error)))
            return std::unique_ptr<RuntimeWindowState>{};
        restored->runtimeSoundsEnabled = restored->runtimePreferencePersistenceEnabled = restored->visionEnabled = false;
        initializeRuntimeOfflineGameplayRouting(*restored); return restored;
    };
    const auto replaceSection = [](const IniFile& source, const std::string& name, const std::string& replacement) {
        std::ostringstream out;
        for (const auto& section : source.getSectionNames()) {
            if (toUpperAsciiLocal(section) == toUpperAsciiLocal(name)) continue;
            out << '[' << section << "]\n";
            if (const auto* entries = source.getSection(section))
                for (const auto& [key, value] : *entries) out << key << '=' << value << '\n';
        }
        if (!replacement.empty()) out << '[' << name << "]\n" << replacement;
        return out.str();
    };
    int ordinaryChecks = 0, canonicalChecks = 0, invalidChecks = 0, boundaryChecks = 0, legacyChecks = 0;
    for (const bool naval : {false, true}) {
        label = naval ? "DEST" : "AMCV"; auto scene = make(naval ? "DEST" : "AMCV", naval);
        auto& s = *scene;
        if (s.world.getObjects().size() != 2) return fail("original units were not created");
        std::vector<int> ids;
        for (const auto& object : s.world.getObjects()) ids.push_back(object.instanceId);
        for (int id : ids) {
            const auto* object = findObjectById(s, id);
            if (!object || !runtimeObjectUsesVehicleStartup(s, *object)) return fail("original startup profile unavailable");
            const int y = object->y;
            applySelection(s, {id}, false, false); issueRuntimeMoveCommand(s, {44,y});
            if (s.lastCommand.acceptedCount != 1 || !s.behaviors.contains(id)) return fail("real Move rejected");
            std::vector<RuntimeCellPoint> route;
            for (int x = 21; x <= 44; ++x) route.push_back({x,y});
            replaceRuntimeBehaviorPath(s.behaviors.at(id), route); s.behaviors.at(id).pathRequestPending = false;
        }
        for (int tick = 0; tick < 100; ++tick) {
            step(s); bool captured = true;
            for (int id : ids) {
                const auto found = s.behaviors.find(id);
                captured = captured && found != s.behaviors.end() && found->second.renderStartupAccelerationDistance > 0.0
                    && found->second.renderMoveTicksRemaining < found->second.renderMoveTicksTotal
                    && found->second.renderMoveTicksRemaining > 3;
            }
            if (captured) break;
        }
        for (int id : ids) if (!s.behaviors.contains(id)
            || !(s.behaviors.at(id).renderStartupAccelerationDistance > 0.0)) return fail("active acceleration was not reached");
        auto ordinary = checkpoint(s, false), canonical = checkpoint(s, true);
        if (!ordinary || !canonical) return fail("actual checkpoint restore: " + error);
        for (int id : ids) {
            const auto& source = s.behaviors.at(id);
            for (auto* restored : {ordinary.get(), canonical.get()}) {
                const auto found = restored->behaviors.find(id);
                if (found == restored->behaviors.end()
                    || found->second.moveStartupDistanceQ16 != source.moveStartupDistanceQ16
                    || found->second.renderStartupAccelerationDistance != source.renderStartupAccelerationDistance
                    || found->second.moveCooldownTicks != source.moveCooldownTicks
                    || pose(*findObjectById(*restored,id)) != pose(*findObjectById(s,id)))
                    return fail("restored startup authority/pose/clock changed");
            }
            if (behaviorHash(s,id) != behaviorHash(*canonical,id)) return fail("canonical behavior hash changed");
            const auto before = behaviorHash(s,id); ++s.behaviors.at(id).moveStartupDistanceQ16;
            if (behaviorHash(s,id) == before) return fail("startup distance absent from authority hash");
            --s.behaviors.at(id).moveStartupDistanceQ16;
        }
        ++ordinaryChecks; ++canonicalChecks;
        const int currentTicks = std::min(10, s.behaviors.at(ids.front()).renderMoveTicksRemaining-1);
        for (int tick = 0; tick < currentTicks; ++tick) {
            step(s); step(*ordinary); step(*canonical);
            for (int id : ids) if (pose(*findObjectById(s,id)) != pose(*findObjectById(*ordinary,id))
                || pose(*findObjectById(s,id)) != pose(*findObjectById(*canonical,id)))
                return fail("accepted startup segment diverged after restore");
        }
        for (int tick = 0; tick < 80; ++tick) {
            step(s); step(*canonical);
            for (int id : ids) if (pose(*findObjectById(s,id)) != pose(*findObjectById(*canonical,id))
                || behaviorHash(s,id) != behaviorHash(*canonical,id))
                return fail("canonical continued route diverged");
        }
        std::ostringstream saved; writeRuntimeVehicleArrivals(s, saved); IniFile original;
        if (!original.loadFromString(saved.str())) return fail("sidecar parse");
        const int first = ids.front(), second = ids.back();
        const auto& a = s.behaviors.at(first);
        std::ostringstream validFirst; validFirst << std::setprecision(17) << "Schema=1\nCount=2\n0="
            << first << '|' << (a.moveStartupDistanceQ16+1) << '|' << a.renderStartupAccelerationDistance
            << "\n1=" << second << "|not-a-distance|0\n";
        IniFile damaged; damaged.loadFromString(replaceSection(original, "VehicleStartupMotion", validFirst.str()));
        std::string rejected;
        if (loadRuntimeVehicleArrivals(s, damaged, rejected) || rejected.empty()) return fail("invalid late startup row accepted");
        std::ostringstream after; writeRuntimeVehicleArrivals(s, after);
        if (after.str() != saved.str()) return fail("invalid late row partly changed earlier authority/pose");
        ++invalidChecks;
        const auto rejectUnchanged = [&](const std::string& bytes, const char* why) {
            IniFile corrupt; std::string rejection;
            if (!corrupt.loadFromString(bytes) || loadRuntimeVehicleArrivals(s, corrupt, rejection)
                || rejection.empty()) return fail(std::string(why) + " accepted");
            std::ostringstream unchanged; writeRuntimeVehicleArrivals(s, unchanged);
            if (unchanged.str() != saved.str()) return fail(std::string(why) + " changed live startup/presentation");
            ++invalidChecks; return true;
        };
        for (const auto& values : std::vector<std::pair<std::string,std::string>>{
                {"-1", "0"}, {std::to_string(RuntimeVehicleStartupMotion::maximumDistanceQ16+1), "0"},
                {std::to_string(a.moveStartupDistanceQ16), "nan"},
                {std::to_string(a.moveStartupDistanceQ16), "-0.1"}}) {
            const std::string row = "Schema=1\nCount=1\n0=" + std::to_string(first) + '|'
                + values.first + '|' + values.second + '\n';
            if (!rejectUnchanged(replaceSection(original,"VehicleStartupMotion",row), "invalid startup bounds")) return false;
        }
        if (original.getValue("VehicleMovePresentation","Count","") != "2")
            return fail("two active presentation rows were not exported");
        std::ostringstream badPresentation; badPresentation << "Schema=2\nCount=2\n0="
            << original.getValue("VehicleMovePresentation","0","") << "\n1=bad\n";
        IniFile changedStartup;
        std::ostringstream changedRow; changedRow << std::setprecision(17) << "Schema=1\nCount=1\n0="
            << first << '|' << (a.moveStartupDistanceQ16+1) << '|' << a.renderStartupAccelerationDistance << '\n';
        changedStartup.loadFromString(replaceSection(original,"VehicleStartupMotion",changedRow.str()));
        if (!rejectUnchanged(replaceSection(changedStartup,"VehicleMovePresentation",badPresentation.str()),
                "invalid late presentation")) return false;
        const std::string positiveRamp = "[VehicleStartupMotion]\nSchema=1\nCount=1\n0="
            + std::to_string(first) + '|' + std::to_string(a.moveStartupDistanceQ16) + "|1\n";
        if (!rejectUnchanged(positiveRamp,"startup curve without its committed presentation")) return false;
        // Reconstruct a complete pre-startup v14 save, preserving its ordinary
        // clock, poses and all other sections, then use the real load pipeline.
        std::ostringstream legacyPayload; IniFile modernV14, legacyV14;
        if (!writeRuntimeStateToStream(s, legacyPayload, error)
            || !modernV14.loadFromString(legacyPayload.str())
            || modernV14.getValue("RuntimeSave","Version","") != "14"
            || !legacyV14.loadFromString(replaceSection(modernV14,"VehicleStartupMotion",""))
            || legacyV14.getSection("VehicleStartupMotion") != nullptr)
            return fail("complete legacy v14 fixture: " + error);
        auto legacyRestored = std::make_unique<RuntimeWindowState>();
        seedRuntimeLoadScratch(*legacyRestored,s,false);
        {
            RuntimeSnapshotActiveStateScope active(*legacyRestored);
            if (!materializeRuntimeLoadStateFromIni(*legacyRestored,legacyV14,error))
                return fail("complete legacy v14 materialize: " + error);
        }
        legacyRestored->runtimeSoundsEnabled = legacyRestored->runtimePreferencePersistenceEnabled
            = legacyRestored->visionEnabled = false;
        initializeRuntimeOfflineGameplayRouting(*legacyRestored);
        for (int id : ids) {
            const auto found = legacyRestored->behaviors.find(id);
            const auto* object = findObjectById(*legacyRestored,id);
            if (found == legacyRestored->behaviors.end() || !object) return fail("legacy v14 lost moving unit");
            const auto& b = found->second;
            const double ramp = runtimeVehicleStartupAccelerationDistance(*legacyRestored,*object);
            if (b.renderStartupAccelerationDistance != 0.0
                || b.moveStartupDistanceQ16 < RuntimeVehicleStartupMotion::rampDistanceQ16(ramp)
                || b.moveCooldownTicks != s.behaviors.at(id).moveCooldownTicks
                || pose(*object) != pose(*findObjectById(s,id)))
                return fail("legacy v14 changed its active cruise clock/pose");
        }
        std::unordered_set<int> continuedCruise;
        for (int tick = 0; tick < 256 && continuedCruise.size() < ids.size(); ++tick) {
            step(*legacyRestored);
            for (int id : ids) {
                if (continuedCruise.contains(id)) continue;
                const auto found = legacyRestored->behaviors.find(id);
                if (found == legacyRestored->behaviors.end()) return fail("legacy route ended before its next cruise segment");
                const auto& b = found->second;
                if (b.moveCooldownTicks <= 0 || b.renderTo == s.behaviors.at(id).renderTo) continue;
                const auto* object = findObjectById(*legacyRestored,id);
                const double cruise = runtimeMovementSegmentIntervalQ16(*legacyRestored,*object,b.renderFrom,b.renderTo)
                    / (65536.0 * runtimeTerrainMovementSpeedMultiplier(*legacyRestored,*object,b.renderFrom,b.renderTo));
                if (b.renderStartupAccelerationDistance != 0.0 || b.renderArrivalDistanceAfter >= 0.0
                    || std::abs(b.renderMoveTicksTotal-cruise) >= 1.0)
                    return fail("legacy v14 restarted acceleration on its next segment");
                continuedCruise.insert(id);
            }
        }
        if (continuedCruise.size() != ids.size()) return fail("legacy v14 did not resume its next cruise segment");
        ++legacyChecks;
        // Exercise the valid boundary form separately from interpolation.
        // Normal open-road dispatch can accept the next cell in the same tick;
        // queued/held routes can instead persist this clock-free authority.
        auto& boundary = s.behaviors.at(first);
        if (boundary.moveStartupDistanceQ16 <= 0) return fail("accepted route lost startup distance");
        boundary.moveCooldownTicks = boundary.renderMoveTicksTotal = boundary.renderMoveTicksRemaining = 0;
        boundary.moveSegmentFrom = {-1,-1}; boundary.renderStartupAccelerationDistance = 0.0;
        clearRuntimeRenderPosition(*findMutableObjectById(s, first));
        auto ordinaryBoundary = checkpoint(s, false), canonicalBoundary = checkpoint(s, true);
        if (!ordinaryBoundary || !canonicalBoundary) return fail("boundary checkpoint: " + error);
        if (ordinaryBoundary->behaviors.at(first).moveStartupDistanceQ16 != boundary.moveStartupDistanceQ16
            || canonicalBoundary->behaviors.at(first).moveStartupDistanceQ16 != boundary.moveStartupDistanceQ16)
            return fail("inactive interpolation lost startup distance");
        ++boundaryChecks;
        std::cout << "Vehicle startup save " << label << " ordinary/canonical/legacy/invalid/boundary PASS\n";
    }
    std::cout << "Vehicle startup save: ordinary=" << ordinaryChecks << " canonical=" << canonicalChecks
        << " legacyV14=" << legacyChecks << " invalidLate=" << invalidChecks << " boundaries=" << boundaryChecks << '\n';
    return true;
}
