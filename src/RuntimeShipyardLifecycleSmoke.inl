// Real commands on stock water/rules. Geometry and pixel ownership are tested
// separately; this probe owns fractional movement, cancellation and persistence.
bool runRuntimeShipyardLifecycleSmoke(RuntimeWindowState& assets, std::string& error) {
    std::cout << std::unitbuf;
    std::string label;
    const auto fail = [&](const std::string& why) { error = "Shipyard lifecycle " + label + ": " + why; return false; };
    constexpr int yardId = 101, shipId = 102;
    int waterTile = -1, landTile = -1, firstVisualWaterTile = -1;
    const int waterSet = assets.theater.getGeneralTileSetIndex("WaterSet");
    const auto metadata = runtimeTerrainMetadataContext(assets);
    for (int tile = 0; tile < static_cast<int>(assets.theater.getTotalTileSlots()); ++tile) {
        TerrainCell cell; cell.tileIndex = tile; cell.subTileIndex = 0;
        const auto info = runtimeTerrainInfoForCell(assets, cell);
        if (!info.passable || info.shore || info.cliff || info.bridge) continue;
        if (info.water && firstVisualWaterTile < 0) firstVisualWaterTile = tile;
        // Visual water membership alone also covers TMPs whose movement byte
        // is Clear/Beach. Match General.WaterSet and inspect the real sub-tile.
        if (info.water && waterTile < 0 && waterSet >= 0
            && assets.theater.resolveTileIndex(tile).tileSetIndex == waterSet
            && resolveRuntimeTerrainSubTileLandType(cell,metadata,assets.renderCache) == RuntimeTerrainLandType::Water
            && resolveRuntimeTerrainSubTileRampType(cell,metadata,assets.renderCache) == 0) waterTile = tile;
        if (!info.water && landTile < 0) landTile = tile;
        if (waterTile >= 0 && landTile >= 0) break;
    }
    if (waterTile < 0 || landTile < 0) return fail("stock terrain unavailable");
    const auto* waterRules = assets.rules.findLandType("Water");
    if (!waterRules || waterRules->floatSpeed != 100.0) return fail("stock Water.Float is not 100%");
    std::cout << "Shipyard lifecycle terrain: visualWater=" << firstVisualWaterTile
        << " movementWater=" << waterTile << " WaterSet=" << waterSet
        << " resource=" << assets.theater.resolveTileIndex(waterTile).expectedFileName << '\n';
    const auto position = [](const RuntimeWindowState& s) {
        const auto* unit = findObjectById(s, shipId);
        return std::array<double, 2>{runtimeObjectRenderX(*unit), runtimeObjectRenderY(*unit)};
    };
    const auto distance = [](const auto& a, const auto& b) { return std::hypot(a[0]-b[0], a[1]-b[1]); };
    const auto diagnostic = [&](const RuntimeWindowState& s) {
        std::ostringstream out;
        out << "tick=" << s.tick << " finished=" << s.skirmishMatchFinished
            << " result=" << s.skirmishResult << " owner=" << s.economy.owner
            << " power=" << s.economy.powerProduced << '/' << s.economy.powerUsed
            << " credits=" << s.economy.credits << " slots=" << s.config.playerSlots.size();
        if (const auto* ship = findObjectById(s,shipId)) {
            out << " ship[alive=" << ship->alive << " health=" << ship->health
                << " owner=" << ship->owner << " paused=" << runtimeComputerAiPausedForOwner(s,ship->owner)
                << " cell=" << ship->x << ',' << ship->y << " pose=" << position(s)[0] << ',' << position(s)[1]
                << " facing=" << ship->facing << " active=" << ship->renderMoveActive
                << " docked=" << ship->dockedToObjectId << ']';
        } else out << " ship[missing]";
        if (const auto move = s.behaviors.find(shipId); move != s.behaviors.end()) {
            const auto& b = move->second;
            out << " behavior[kind=" << static_cast<int>(b.kind) << " state=" << static_cast<int>(b.state)
                << " target=" << b.moveTarget.x << ',' << b.moveTarget.y << " object=" << b.targetObjectId
                << " path=" << b.path.size() << " pending=" << b.pathRequestPending
                << " cooldown=" << b.moveCooldownTicks << " render=" << b.renderMoveTicksRemaining
                << '/' << b.renderMoveTicksTotal << ']';
        } else out << " behavior[none]";
        if (const auto* relation = findRuntimeRepairDepotRelationForUnit(s.repairDepots,shipId))
            out << " relation[phase=" << static_cast<int>(relation->phase) << " host=" << relation->hostId
                << " dock=" << relation->dockCell.x << ',' << relation->dockCell.y
                << " hostOwner=" << relation->hostOwner << " unitOwner=" << relation->unitOwner << ']';
        else out << " relation[none]";
        if (const auto* yard = findObjectById(s,yardId))
            out << " yard[alive=" << yard->alive << " owner=" << yard->owner
                << " powered=" << yard->poweredOperational << ']';
        if (const auto* enemy = findObjectById(s,104); enemy && enemy->type == "NAPOWR")
            out << " enemy[alive=" << enemy->alive << " owner=" << enemy->owner
                << " allied=" << runtimeOwnersAreAllied(s,s.economy.owner,enemy->owner)
                << " defeated=" << runtimeSkirmishOwnerDefeated(s,enemy->owner) << ']';
        return out.str();
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSimulation(s, false);
    };
    const auto make = [&](int hz, bool moving, bool opponent = true) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        seedRuntimeLoadScratch(s, assets, false);
        RuntimeSnapshotActiveStateScope active(s);
        s.world = assets.world; s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.terrainInfoByCell.clear(); s.terrainByCell.clear(); s.mapFacts = {}; s.mapFactsLoaded = false;
        s.report.map.size.clear(); s.report.map.localSize.clear(); s.report.map.width = -1;
        s.config.fixedTickHz = hz; s.config.noCooldown = false; s.config.playerSlots.clear();
        s.config.playerSlots.resize(opponent ? 2 : 1);
        for (std::size_t i = 0; i < s.config.playerSlots.size(); ++i) {
            auto& slot = s.config.playerSlots[i];
            slot.enabled = slot.human = true; slot.local = i == 0;
            slot.startPosition = static_cast<int>(i)+1; slot.country = "Russians"; slot.team = "-";
            slot.color = i == 0 ? "Gold" : "Red";
            slot.peerId = i == 0 ? "shipyard-local" : "shipyard-remote";
            slot.name = i == 0 ? "Shipyard lifecycle" : "Distant opponent";
        }
        s.config.computerAiEnabled = s.config.multiplayerActive = s.config.shortGame = s.config.crates = false;
        s.campaignMode = false; s.config.gameMode = "Battle"; s.config.playerCountry = "Russians";
        s.skirmishMatchFinished = s.skirmishVictory = false; s.skirmishResult.clear();
        initializeRuntimeSkirmishPlayerIdentities(s,"Russians");
        s.economy.credits = 10000; s.economy.skirmishStartApplied = true;
        s.triggers.loaded = s.triggers.bootstrapComplete = true; s.visionEnabled = false;
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true;
        for (int y = 10; y <= 60; ++y) for (int x = 10; x <= 60; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x,y);
            cell.tileIndex = x >= 34 && y >= 34 ? landTile : waterTile; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        const auto add = [&](int id, const char* type, WorldObjectKind kind, int x, int y) {
            WorldObjectInstance unit; unit.instanceId = id; unit.type = type; unit.kind = kind;
            unit.mapId = "naval-lifecycle-" + std::to_string(id); unit.owner = s.economy.owner;
            unit.x = x; unit.y = y; unit.cell = runtimeCellKey(x,y); unit.alive = unit.poweredOperational = true;
            unit.health = runtimeMaxHealthForObject(s,unit); unit.mission = "Guard"; unit.currentAmmo = -1;
            unit.facing = 0; clearRuntimeRenderPosition(unit); s.world.accessObjects().push_back(unit);
        };
        add(yardId, "NAYARD", WorldObjectKind::Structure, 20, 20);
        add(shipId, "CARRIER", WorldObjectKind::Unit, moving ? 27 : 23, 21);
        add(103, "NAPOWR", WorldObjectKind::Structure, 35, 35);
        if (opponent) {
            // A second human house with a distant passive building keeps the
            // normal elimination rules active without introducing AI orders.
            add(104, "NAPOWR", WorldObjectKind::Structure, 57, 57);
            findMutableObjectById(s,104)->owner = runtimeSkirmishOwnerForSlot(1);
        }
        findMutableObjectById(s,shipId)->health -= 128;
        initializeRuntimeNavigation(s); initializeRuntimeLocalProductionLanes(s.economy);
        s.structureFootprints = buildRuntimeStructureFootprints(s); rebuildRuntimeEconomyPower(s);
        // Use the real slot/house/country mapping and initialize each human's
        // economy/tech authority after both houses' world objects exist.
        initializeRuntimeHumanHouseAuthorities(s);
        ensureRuntimeRepairDepotsInitialized(s); s.selectedObjectIds = {shipId};
        return storage;
    };
    int cases = 0, saves = 0, snapshots = 0, rejections = 0, pairedTicks = 0;
    const auto compare = [&](const RuntimeWindowState& a, const RuntimeWindowState& b) {
        const auto* left = findObjectById(a,shipId); const auto* right = findObjectById(b,shipId);
        if (!left || !right || distance(position(a),position(b)) > 1e-8
            || left->x != right->x || left->y != right->y || left->facing != right->facing
            || left->health != right->health || left->dockedToObjectId != right->dockedToObjectId
            || a.economy.credits != b.economy.credits || a.repairDepots.stateHash() != b.repairDepots.stateHash())
            return fail("restored pose/repair state diverged: live{" + diagnostic(a) + "} restored{" + diagnostic(b) + "}");
        const auto am = a.behaviors.find(shipId), bm = b.behaviors.find(shipId);
        if ((am == a.behaviors.end()) != (bm == b.behaviors.end())
            || (am != a.behaviors.end() && (am->second.moveCooldownTicks != bm->second.moveCooldownTicks
                || am->second.renderMoveTicksRemaining != bm->second.renderMoveTicksRemaining)))
            return fail("restored movement clocks diverged: live{" + diagnostic(a) + "} restored{" + diagnostic(b) + "}");
        return true;
    };
    const auto fullSave = [&](RuntimeWindowState& s, std::unique_ptr<RuntimeWindowState>& clone) {
        std::ostringstream saved; IniFile ini;
        clone = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*clone,s,false);
        {
            RuntimeSnapshotActiveStateScope source(s);
            if (!writeRuntimeStateToStream(s,saved,error) || !ini.loadFromString(saved.str()))
                return fail("full save write: " + error);
        }
        {
            RuntimeSnapshotActiveStateScope destination(*clone);
            if (!materializeRuntimeLoadStateFromIni(*clone,ini,error)) return fail("full save restore: " + error);
        }
        ++saves; return compare(s,*clone);
    };
    const auto snapshot = [&](RuntimeWindowState& s, bool stopped) {
        RuntimeSnapshotActiveStateScope source(s);
        s.config.multiplayerActive = true; s.config.multiplayerSetupHash = 1;
        s.config.multiplayerCanonicalMapId = "stock-naval-lifecycle"; s.config.multiplayerMapContentFingerprint = 2;
        s.config.multiplayerTimelineEpoch = 1; s.config.simulationRandomSeed = 1;
        completeRuntimeWorldRefresh(s);
        s.particles.reset(s.config.fixedTickHz); s.particlesInitialized = true;
        s.superWeapons.reset(std::vector<SuperWeaponRule>{}); s.superWeaponsInitialized = true;
        if (!s.particles.alignEmptyStateTick(s.tick) || !s.superWeapons.alignEmptyStateTick(s.tick))
            return fail("snapshot clocks");
        const auto before = position(s); const auto hash = runtimeMultiplayerAuthoritativeStateHash(s,error);
        RuntimeMultiplayerSnapshotIdentity identity; identity.proposalId = 1; identity.sourceTimelineEpoch = 1;
        identity.kind = RuntimeMultiplayerSnapshotKind::Load;
        identity.snapshotTick = identity.barrierTick = s.tick; identity.snapshotToken = label;
        identity.authoritativeStateHash = hash; identity.gameplaySetupFingerprint = 1;
        RuntimeMultiplayerSnapshotMemoryImage image; RuntimeMultiplayerSnapshotStagedState staged;
        if (!hash || !exportRuntimeMultiplayerSnapshotV15(s,identity,image,error)
            || !stageRuntimeMultiplayerSnapshot(s,image.bytes,image.descriptor,staged,error))
            return fail("v15 snapshot: " + error);
        if (!staged.scratch) return fail("v15 stage returned no state");
        if (!compare(s,*staged.scratch)) return false;
        if (findObjectById(s,shipId)->renderMoveActive) {
            const auto& liveMove = s.behaviors.at(shipId);
            const auto& stagedMove = staged.scratch->behaviors.at(shipId);
            if (liveMove.renderMoveTicksTotal != stagedMove.renderMoveTicksTotal
                || liveMove.renderFrom != stagedMove.renderFrom || liveMove.renderTo != stagedMove.renderTo
                || liveMove.renderFromOffsetX != stagedMove.renderFromOffsetX
                || liveMove.renderFromOffsetY != stagedMove.renderFromOffsetY
                || liveMove.renderToOffsetX != stagedMove.renderToOffsetX
                || liveMove.renderToOffsetY != stagedMove.renderToOffsetY)
                return fail("v15 stage lost active segment anchors");
        }
        // Recompute the envelope digest so rejection exercises state parsing,
        // not merely the byte checksum gate; live state must remain untouched.
        IniFile original, damaged; std::ostringstream rewritten, canonical;
        if (!original.loadFromString(std::string(image.bytes.begin(),image.bytes.end())))
            return fail("bad v15 fixture could not parse the valid image");
        const std::string changedSection = stopped ? "NAVALDOCKSTOPPOSES" : "VEHICLEMOVEPRESENTATION";
        int replacements = 0;
        // appendFrom preserves both copies of a key. Replace the existing
        // row while rebuilding the fixture so canonicalization remains valid
        // and the negative probe reaches the presentation-state validator.
        for (const auto& section : original.getSectionNames()) {
            rewritten << '[' << section << "]\n";
            for (const auto& [key,value] : *original.getSection(section)) {
                const bool replace = toUpperAsciiLocal(section) == changedSection && key == "0";
                rewritten << key << '=' << (replace ? "bad" : value) << '\n';
                replacements += replace ? 1 : 0;
            }
        }
        if (replacements != 1 || !damaged.loadFromString(rewritten.str()))
            return fail("bad v15 fixture did not replace exactly one presentation row");
        if (!writeRuntimeMultiplayerSnapshotCanonicalIni(damaged,canonical,error))
            return fail("bad v15 fixture canonicalization: " + error);
        const auto text = canonical.str(); std::vector<std::uint8_t> bytes(text.begin(),text.end());
        RuntimeMultiplayerSnapshotDescriptor descriptor; RuntimeMultiplayerSnapshotStagedState rejected;
        if (!makeRuntimeMultiplayerSnapshotDescriptor(identity,bytes,descriptor,error))
            return fail("bad v15 fixture descriptor: " + error);
        std::string rejection;
        if (stageRuntimeMultiplayerSnapshot(s,bytes,descriptor,rejected,rejection))
            return fail("bad v15 presentation was accepted");
        if (rejection != "invalid vehicle move presentation")
            return fail("bad v15 presentation rejected at the wrong stage: " + rejection);
        if (distance(before,position(s)) > 1e-8 || runtimeMultiplayerAuthoritativeStateHash(s,error) != hash)
            return fail("bad v15 presentation changed live state");
        ++rejections;
        commitRuntimeMultiplayerSnapshotAuthority(s,std::move(staged));
        if (distance(before,position(s)) > 1e-8 || runtimeMultiplayerAuthoritativeStateHash(s,error) != hash)
            return fail("v15 commit changed continuous pose/authority");
        s.config.multiplayerActive = false; ++snapshots; return true;
    };
    const auto malformed = [&](RuntimeWindowState& s) {
        std::ostringstream baseline; writeRuntimeVehicleArrivals(s,baseline);
        IniFile original; original.loadFromString(baseline.str());
        const auto fields = splitRuntimeSaveFields(original.getValue("VehicleMovePresentation","0",""));
        if (fields.size() != 17) return fail("malformed probe requires an active offset segment");
        const auto before = position(s);
        const auto reject = [&](std::vector<std::string> row, bool late) {
            std::ostringstream beforeSerialization; writeRuntimeVehicleArrivals(s,beforeSerialization);
            std::ostringstream value; value << "[VehicleMovePresentation]\nSchema=2\nCount=" << (late ? 2 : 1) << "\n0=";
            for (std::size_t i = 0; i < row.size(); ++i) value << (i ? "|" : "") << row[i];
            value << '\n'; if (late) value << "1=bad\n";
            IniFile broken; broken.loadFromString(value.str()); std::string why;
            if (loadRuntimeVehicleArrivals(s,broken,why)) return fail("malformed offset accepted");
            std::ostringstream after; writeRuntimeVehicleArrivals(s,after);
            if (after.str() != beforeSerialization.str() || distance(before,position(s)) > 1e-8)
                return fail("malformed offset partially committed");
            ++rejections; return true;
        };
        for (const auto [column,value] : std::vector<std::pair<int,std::string>>{
                {12,"0"}, {13,"nan"}, {15,"16"}, {5,"1"}, {7,"true"}, {10,"0"}}) {
            auto row = fields; row[column] = value; if (!reject(row,false)) return false;
        }
        auto row = fields; row[13] = row[15]; row[14] = row[16];
        if (!reject(row,false)) return false;
        row = fields; row[11] = "1"; if (!reject(row,true)) return false;
        auto* ship = findMutableObjectById(s,shipId); const auto type = ship->type;
        ship->type = "MTNK"; const bool groundRejected = reject(fields,false); ship->type = type;
        if (!groundRejected) return false;
        return true;
    };
    {
        // Diagnose the former single-slot fixture using the unchanged real
        // simulation: normal elimination ends it at tick 10, before docking.
        label = "single-slot-control";
        auto storage = make(15,false,false); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
        if (!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {20,20},findObjectById(s,yardId)) || s.lastCommand.acceptedCount != 1)
            return fail("control repair rejected: " + diagnostic(s));
        for (int n = 0; n < 10; ++n) step(s);
        std::cout << "Shipyard lifecycle " << label << ": " << diagnostic(s) << '\n';
        if (s.tick != 10 || !s.skirmishMatchFinished || !s.skirmishVictory || !s.behaviors.empty())
            return fail("single-slot result did not reproduce early settlement");
    }
    for (int hz : {15,30,60}) for (int scenario = 0; scenario < 5; ++scenario) {
        label = std::to_string(hz) + "Hz-" + std::array<const char*,5>{"same","moving","move-cancel","stop","guard"}[scenario];
        auto storage = make(hz,scenario == 1); auto& s = *storage; RuntimeSnapshotActiveStateScope active(s);
        if (runtimeOwnersAreAllied(s,s.economy.owner,runtimeSkirmishOwnerForSlot(1))
            || runtimeSkirmishOwnerDefeated(s,runtimeSkirmishOwnerForSlot(1)))
            return fail("fixture needs a surviving hostile house: " + diagnostic(s));
        const auto repair = [&](RuntimeWindowState& target) {
            target.selectedObjectIds = {shipId}; const auto before = position(target);
            RuntimeSnapshotActiveStateScope scope(target);
            return dispatchRuntimeTargetOrderFromUi(target,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
                {20,20},findObjectById(target,yardId)) && target.lastCommand.acceptedCount == 1
                && target.lastCommand.verb == "unitRepair" && distance(before,position(target)) < 1e-8;
        };
        if (scenario == 1) {
            if (!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{27,25}))
                return fail("initial sailing order");
            for (int n = 0; n < hz*30; ++n) {
                step(s); const auto move = s.behaviors.find(shipId);
                if (move != s.behaviors.end() && move->second.renderMoveTicksRemaining > 1
                    && move->second.renderMoveTicksRemaining < move->second.renderMoveTicksTotal) break;
            }
            if (!findObjectById(s,shipId)->renderMoveActive) return fail("sailing never began: " + diagnostic(s));
            const int remaining = s.behaviors.at(shipId).moveCooldownTicks;
            if (!repair(s) || s.behaviors.at(shipId).moveCooldownTicks != remaining)
                return fail("moving repair lost committed segment");
        } else if (!repair(s)) return fail("same-cell repair rejected/jumped");
        bool found = false; int duration = 0;
        for (int n = 0; n < hz*45; ++n) {
            const auto previous = position(s); step(s); const auto* ship = findObjectById(s,shipId);
            const auto move = s.behaviors.find(shipId);
            if (s.skirmishMatchFinished) return fail("match ended during approach: " + diagnostic(s));
            if (ship->dockedToObjectId >= 0) return fail("attached before sampling real approach: " + diagnostic(s));
            if (move != s.behaviors.end() && ship->x == 23 && ship->y == 21 && ship->renderMoveActive) {
                duration = move->second.renderMoveTicksTotal;
                if (move->second.renderMoveTicksRemaining > 1
                    && move->second.renderMoveTicksRemaining < duration) { found = true; break; }
            } else if (scenario != 1 && distance(previous,position(s)) > 1e-8)
                return fail("body moved before completing turn");
        }
        if (!found) return fail("missing continuous authored approach: " + diagnostic(s));
        if (scenario != 1) {
            const auto* rules = findRuntimeUnitType(s.rules,*findObjectById(s,shipId));
            const int leptons = std::min(255,std::clamp(rules->speed,0,100)*256/100);
            const double nativeTicks = std::sqrt(0.5)*256.0*hz/(15.0*leptons);
            const auto* ship = findObjectById(s,shipId);
            const auto info = runtimeCachedTerrainInfoForCell(s,*s.terrainByCell.at(runtimeCellKey(23,21)));
            const double multiplier = runtimeTerrainMovementSpeedMultiplier(s,*ship,{23,21},{23,21});
            std::cout << "Shipyard lifecycle " << label << " duration=" << duration << " nativeTicks=" << nativeTicks
                << " speed=" << rules->speed << " speedType=" << rules->speedType
                << " health=" << ship->health << '/' << runtimeMaxHealthForObject(s,*ship)
                << " tile=" << waterTile << " movementLand=" << static_cast<int>(info.movementLandType)
                << " authoredLand=" << static_cast<int>(info.authoredLandType) << " multiplier=" << multiplier << '\n';
            if (std::abs(duration-nativeTicks) > 0.51) return fail("same-cell distance does not use stock speed: actual="
                +std::to_string(duration)+" expected="+std::to_string(nativeTicks)+" terrainMultiplier="+std::to_string(multiplier));
            if (findObjectById(s,shipId)->facing != runtimeFacingFromDelta(1,1)) return fail("same-cell facing");
        }
        if (hz == 30 && scenario == 0 && !malformed(s)) return false;
        if (scenario == 0 && !snapshot(s,false)) return false;
        if (scenario >= 2) {
            const auto before = position(s); const int remaining = s.behaviors.at(shipId).moveCooldownTicks;
            if (scenario == 2) {
                if (!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{28,25})
                    || s.lastCommand.acceptedCount != 1 || !s.behaviors.contains(shipId)
                    || s.behaviors.at(shipId).moveCooldownTicks != remaining) return fail("cancel Move lost segment");
            } else if (scenario == 3) issueRuntimeStopCommand(s); else issueRuntimeGuardCommand(s);
            if (distance(before,position(s)) > 1e-8 || findRuntimeRepairDepotRelationForUnit(s.repairDepots,shipId)
                || findObjectById(s,shipId)->dockedToObjectId >= 0) return fail("cancellation jumped/retained dock");
            if (scenario >= 3) {
                if (findObjectById(s,shipId)->renderMoveActive || s.behaviors.contains(shipId))
                    return fail("stop/guard retained movement");
                if (hz == 30 && scenario == 3) {
                    std::ostringstream saved; writeRuntimeVehicleArrivals(s,saved); IniFile ini;
                    ini.loadFromString(saved.str());
                    const auto row = splitRuntimeSaveFields(ini.getValue("NavalDockStopPoses","0",""));
                    if (row.size() != 5) return fail("stopped pose was not serialized");
                    const auto reject = [&](std::vector<std::string> values, bool late) {
                        std::ostringstream prior; writeRuntimeVehicleArrivals(s,prior);
                        std::ostringstream text; text << "[NavalDockStopPoses]\nSchema=1\nCount=" << (late ? 2 : 1) << "\n0=";
                        for (std::size_t i = 0; i < values.size(); ++i) text << (i ? "|" : "") << values[i];
                        text << '\n'; if (late) text << "1=bad\n";
                        IniFile bad; bad.loadFromString(text.str()); std::string why;
                        if (loadRuntimeVehicleArrivals(s,bad,why)) return fail("bad stopped pose accepted");
                        std::ostringstream after; writeRuntimeVehicleArrivals(s,after);
                        if (after.str() != prior.str() || distance(before,position(s)) > 1e-8)
                            return fail("bad stopped pose partially committed");
                        ++rejections; return true;
                    };
                    for (const auto [column,value] : std::vector<std::pair<int,std::string>>{
                            {1,"nan"}, {1,"26"}, {3,"1"}, {4,"true"}}) {
                        auto bad = row; bad[column] = value; if (!reject(bad,false)) return false;
                    }
                    auto late = row; late[1] = "23.125"; if (!reject(late,true)) return false;
                    auto* ship = findMutableObjectById(s,shipId); const auto type = ship->type;
                    ship->type = "MTNK"; const bool rejected = reject(row,false); ship->type = type;
                    if (!rejected) return false;
                }
                if (scenario == 3 && !snapshot(s,true)) return false;
                std::unique_ptr<RuntimeWindowState> stopped;
                if (!fullSave(s,stopped)) return false;
                for (int n = 0; n < 5; ++n) {
                    step(s); step(*stopped); ++pairedTicks;
                    if (!compare(s,*stopped) || distance(before,position(s)) > 1e-8)
                        return fail("stopped fractional pose drifted");
                }
            }
            if (!repair(s)) return fail("repair after cancellation rejected/jumped");
        }
        std::unique_ptr<RuntimeWindowState> clone;
        if (!fullSave(s,clone)) return false;
        const int health = findObjectById(s,shipId)->health, credits = s.economy.credits;
        bool docked = false;
        for (int n = 0; n < hz*45; ++n) {
            const auto before = position(s); step(s); step(*clone); ++pairedTicks;
            if (s.skirmishMatchFinished || clone->skirmishMatchFinished)
                return fail("match ended during continuation: live{" + diagnostic(s) + "} restored{" + diagnostic(*clone) + "}");
            if (!compare(s,*clone)) return false;
            const double stepLimit = 1.0/std::max(1.0,std::floor(runtimeMovementBaseIntervalTicks(s,*findObjectById(s,shipId)))) + 0.01;
            if (distance(before,position(s)) > stepLimit) return fail("continued approach jumped");
            if (findObjectById(s,shipId)->dockedToObjectId == yardId) { docked = true; break; }
        }
        if (!docked || distance(position(s),std::array<double,2>{23.5,21.5}) > 1e-8)
            return fail("did not reach authored half-cell dock: " + diagnostic(s));
        for (int n = 0; n < hz*6 && findObjectById(s,shipId)->health == health; ++n) step(s);
        if (findObjectById(s,shipId)->health <= health || s.economy.credits >= credits)
            return fail("paid repair absent: " + diagnostic(s));
        ++cases; std::cout << "Shipyard lifecycle " << label << " passed\n";
    }
    std::cout << "Shipyard lifecycle passed: " << cases << " cases, " << saves << " saves, " << snapshots
        << " v15 snapshots, " << rejections << " atomic rejections, " << pairedTicks << " continuation ticks\n";
    return true;
}
