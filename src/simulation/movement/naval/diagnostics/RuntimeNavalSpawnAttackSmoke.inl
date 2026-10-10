// Exercise SpawnManager aircraft through the same orders, simulation stages,
// projectiles and return legs used by a running battle.
bool runRuntimeNavalSpawnAttackSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto assets = std::make_unique<RuntimeWindowState>();
    assets->config = config;
    assets->config.gameModeRulesOverride.clear();
    assets->config.playerSlots.clear();
    assets->config.computerAiEnabled = false;
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world,
        &assets->theater, &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) { error = "Naval spawn assets: " + assets->report.errorMessage; return false; }
    IniFile stock;
    if (!stock.loadFromFile(assets->report.rulesPath) || !assets->rules.loadFromIni(stock)) {
        error = "Naval spawn stock rules unavailable"; return false;
    }
    int waterTile = -1;
    const int waterSet = assets->theater.getGeneralTileSetIndex("WaterSet");
    for (const auto& set : assets->theater.getTileSets())
        if (set.index == waterSet && set.tilesInSet > 0) waterTile = set.startTileIndex;
    if (waterTile < 0) { error = "Naval spawn original water tile unavailable"; return false; }
    const auto stockRulesHash = assets->rules.getDetailRulesHash();
    const auto loadStockRules = [&](RuntimeWindowState& s) {
        // Rule lifecycle caches are indexed by the real database owner.
        if (!s.rules.loadFromIni(stock) || s.rules.getDetailRulesHash() != stockRulesHash) return false;
        s.rulesLoaded = true; return true;
    };
    const auto* destroyer = findRuntimeUnitTypeById(assets->rules, "DEST");
    const auto* osprey = findRuntimeUnitTypeById(assets->rules, "ASW");
    const auto* aswBomb = assets->rules.findWeaponType("ASWBomb");
    const auto verses = splitRuntimeTokenList(stock.getValue("APSplash", "Verses", ""));
    if (!destroyer || !osprey || !aswBomb || destroyer->strength != 600
        || destroyer->spawns != "ASW" || destroyer->spawnsNumber != 1 || osprey->strength != 30
        || stock.getValue("ASW", "Ammo", "") != "1" || aswBomb->damage != 50 || aswBomb->rof != 3
        || aswBomb->range != 3 || aswBomb->projectile != "DepthCharge" || aswBomb->warhead != "APSplash"
        || std::max(1, aswBomb->burst) != 1 || stock.getValue("DEST", "SpawnReloadRate", "") != "150"
        || stock.getValue("DEST", "SpawnRegenRate", "") != "400"
        || stock.getValue("SUB", "Strength", "") != "600" || stock.getValue("SUB", "Armor", "") != "heavy"
        || verses.size() != 11 || verses[5] != "100%") {
        error = "Naval spawn original DEST/ASW/ASWBomb/APSplash/SUB authority differs"; return false;
    }
    const char* output = std::getenv("CONCORD_NAVAL_SPAWN_OUTPUT");
    const std::filesystem::path evidence = output && *output ? output : "build/naval-spawn";
    std::filesystem::create_directories(evidence);
    std::ofstream salvoCases(evidence / "salvo_cases.csv"), salvoShots(evidence / "salvo_shots.csv"),
        salvoMoves(evidence / "salvo_movement.csv"), salvoCheckpoints(evidence / "salvo_checkpoints.csv");
    if (!salvoCases || !salvoShots || !salvoMoves || !salvoCheckpoints) {
        error = "Naval spawn evidence streams unavailable"; return false;
    }
    salvoCases << "scene,hz,parent,child_type,round,planes,shots_per_plane,actors,before_hp,after_hp,damage,returned,return_tick,ready_tick,raw_reload,raw_regen,result\n";
    salvoShots << std::setprecision(17) << "scene,hz,round,tick,actor,child,slot,ordinal,spawn_tick,weapon,projectile,warhead,damage,raw_rof,raw_ammo,ammo_remaining,sortie_remaining,phase,source_x,source_y,source_z,target_x,target_y,target_z,target_hp\n";
    salvoMoves << std::setprecision(17) << "scene,hz,round,tick,child,type,hp,alive,x,y,z,airborne,docked_to,current_ammo,phase,ammo_remaining,sortie_remaining,action_tick,weapon_cooldown,move_cooldown,target_hp\n";
    salvoCheckpoints << "scene,stage,format,tick,carrier_hash,children,child_records_hex,file,result\n";
    std::string label, stage;
    int scenes = 0, sorties = 0, saves = 0, snapshots = 0, pairedTicks = 0, captures = 0, rejectedLoads = 0;
    std::vector<std::string> failures;
    const auto fail = [&](const std::string& reason) {
        error = "Naval spawn " + label + " [" + stage + "]: " + reason; return false;
    };
    const auto add = [](RuntimeWindowState& s, int id, const std::string& type,
        const std::string& owner, RuntimeCellPoint cell) {
        WorldObjectInstance object;
        object.instanceId = id; object.mapId = "NavalSpawn" + std::to_string(id);
        object.kind = type == "NAWEAP" || type == "NAPOWR" ? WorldObjectKind::Structure : WorldObjectKind::Unit;
        object.type = type; object.owner = owner; object.x = cell.x; object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y); object.alive = object.poweredOperational = true;
        object.mission = "Sleep"; object.facing = object.turretFacing = runtimeFacingFromDelta(1, 0);
        object.health = runtimeMaxHealthForObject(s, object);
        initializeRuntimeObjectAmmo(object, runtimeAmmoRulesForObject(s.rules, object));
        s.world.accessObjects().push_back(std::move(object));
    };
    const auto make = [&](int hz, const std::string& ship, const std::string& targetType,
        const std::string& targetOwner, bool secondShip = false) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = assets->config; s.config.fixedTickHz = hz; s.config.noCooldown = false;
        s.config.gameMode = "Battle"; s.config.playerCountry = "Americans";
        s.config.multiplayerActive = s.config.shortGame = s.config.crates = s.config.superweapons = false;
        s.config.gameModeRulesOverride.clear(); s.config.skirmishLoadSavePath.clear();
        s.report = assets->report; s.art = assets->art; s.theater = assets->theater;
        if (!loadStockRules(s)) return std::unique_ptr<RuntimeWindowState>{};
        s.rulesLoaded = s.artLoaded = s.theaterLoaded = true;
        s.world = assets->world;
        // A small authored coast gives ships legal water through navigation
        // rebuilds and saves. Retain original TMP material, not a cache override.
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts.theater = assets->mapFacts.theater; s.mapFacts.lighting = assets->mapFacts.lighting;
        for (int y = 64; y <= 112; ++y) for (int x = 64; x <= 112; ++x) {
            TerrainCell terrain; terrain.x = x; terrain.y = y;
            terrain.cellIndex512 = runtimeCellKey(x, y); terrain.subTileIndex = 0;
            terrain.tileIndex = x >= 88 && (y < 83 || (secondShip && targetType == "NAWEAP" && y >= 92 && y <= 99)) ? 0 : waterTile;
            s.world.accessTerrainCells().push_back(terrain);
        }
        add(s, 1, ship, "Americans", {75, 75});
        add(s, 2, targetType, targetOwner, targetType == "SUB" ? RuntimeCellPoint{82, 75}
            : targetType == "NAWEAP" ? RuntimeCellPoint{90, 75} : RuntimeCellPoint{90, 85});
        // A harmless distant enemy remains present in friendly/neutral scenes.
        add(s, 998, "NAPOWR", "Russians", {103, 75});
        if (secondShip) {
            add(s, 10, ship, "Americans", {75, 95});
            add(s, 20, targetType, "Russians", targetType == "SUB" ? RuntimeCellPoint{82, 95}
                : RuntimeCellPoint{90, 95});
        }
        s.runtimeSoundsEnabled = false; s.runtimeSuppressPlayerAudioFeedback = true;
        s.runtimePreferencePersistenceEnabled = false; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s);
        initializeRuntimeTriggers(s); s.triggers.bootstrapComplete = true;
        // These objects are the scenario's authored start, so no MCV is added.
        s.economy.skirmishStartApplied = true;
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        return storage;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s);
        ++s.tick; advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto children = [](const RuntimeWindowState& s, int parentId) {
        std::set<int> result;
        const auto parent = s.specialMechanisms.carrierParents().find(parentId);
        if (parent != s.specialMechanisms.carrierParents().end())
            for (const auto& [slot, id] : parent->second.childBySlot) if (id >= 0) result.insert(id);
        return result;
    };
    const auto describe = [&](const RuntimeWindowState& s, const std::set<int>& ids) {
        std::ostringstream text; text << "tick=" << s.tick << " status=" << s.simulationStatus
            << " command=" << s.lastCommand.acceptedCount << '/' << s.lastCommand.rejectedCount
            << " behaviors=" << s.behaviors.size() << " shots=" << s.projectiles.size();
        for (int id : ids) {
            const auto* object = findObjectById(s, id);
            const auto child = s.specialMechanisms.carrierChildren().find(id);
            const auto behavior = s.behaviors.find(id);
            text << " child=" << id;
            if (object) text << " at=" << runtimeObjectRenderX(*object) << ',' << runtimeObjectRenderY(*object)
                << " z=" << object->level << " air=" << object->airborne << " dock=" << object->dockedToObjectId
                << " ammo=" << object->currentAmmo << " mission=" << object->mission;
            else text << " missing";
            if (child != s.specialMechanisms.carrierChildren().end())
                text << " phase=" << static_cast<int>(child->second.phase) << " target=" << child->second.targetObjectId
                    << " remainingShots=" << child->second.sortieShotsRemaining;
            if (behavior != s.behaviors.end()) text << " behavior=" << static_cast<int>(behavior->second.kind)
                << '/' << static_cast<int>(behavior->second.state) << " force=" << behavior->second.forceFire
                << " auto=" << behavior->second.autoAcquired;
        }
        return text.str();
    };
    const auto docked = [](const RuntimeWindowState& s, const std::set<int>& ids, int parentId) {
        const auto* parent = findObjectById(s, parentId);
        return parent && !ids.empty() && std::all_of(ids.begin(), ids.end(), [&](int id) {
            const auto* object = findObjectById(s, id);
            const auto child = s.specialMechanisms.carrierChildren().find(id);
            return object && object->alive && !object->airborne && object->flightLevel == 0
                && object->dockedToObjectId == parentId && object->currentAmmo == 1
                && object->x == parent->x && object->y == parent->y
                && child != s.specialMechanisms.carrierChildren().end()
                && child->second.phase == CarrierChildPhase::Docked
                && child->second.targetObjectId == -1
                && !s.behaviors.contains(id) && runtimeSpecialObjectHiddenForInteraction(s, *object);
        });
    };
    const auto order = [&](RuntimeWindowState& s, int parentId, int targetId, bool forced) {
        RuntimeSnapshotActiveStateScope active(s); s.selectedObjectIds = {parentId};
        const auto* target = findObjectById(s, targetId);
        return target && dispatchRuntimeTargetOrderFromUi(s, forced ? RuntimeMultiplayerGameplayTargetOrderKind::Attack
            : RuntimeMultiplayerGameplayTargetOrderKind::Primary, {target->x, target->y}, target)
            && s.lastCommand.acceptedCount == 1 && s.behaviors.contains(parentId)
            && s.behaviors.at(parentId).forceFire == forced;
    };
    const auto stop = [&](RuntimeWindowState& s, int parentId) {
        RuntimeSnapshotActiveStateScope active(s); s.selectedObjectIds = {parentId};
        return dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop)
            && s.lastCommand.acceptedCount == 1;
    };
    struct FlightObservation {
        double x = 0, y = 0, level = 0;
        bool airborne = false;
        CarrierChildPhase phase = CarrierChildPhase::Docked;
    };
    const auto observeFlights = [](const RuntimeWindowState& s, const std::set<int>& ids) {
        std::map<int, FlightObservation> result;
        for (int id : ids) {
            const auto* object = findObjectById(s, id);
            const auto child = s.specialMechanisms.carrierChildren().find(id);
            if (object && child != s.specialMechanisms.carrierChildren().end()) result[id] = {
                runtimeObjectRenderX(*object), runtimeObjectRenderY(*object), runtimeObjectRenderLevel(*object),
                object->airborne, child->second.phase};
        }
        return result;
    };
    const auto continuousReturn = [&](const RuntimeWindowState& s, const std::map<int, FlightObservation>& previous) {
        for (const auto& [id, before] : previous) {
            const auto* object = findObjectById(s, id);
            const auto child = s.specialMechanisms.carrierChildren().find(id);
            if (!object || !object->airborne || !before.airborne
                || child == s.specialMechanisms.carrierChildren().end()
                || child->second.phase != CarrierChildPhase::Returning
                || before.phase == CarrierChildPhase::Returning) continue;
            // Permit a completed grid segment to remove its render flag;
            // compare actual positions, including diagonal movement. The
            // return transition cannot spend more than one normal air step.
            const double travel = std::hypot(runtimeObjectRenderX(*object) - before.x,
                runtimeObjectRenderY(*object) - before.y);
            const double maximum = 1.6 / std::max(1, runtimeMovementIntervalTicks(s, *object));
            const auto* parent = findObjectById(s, child->second.parentObjectId);
            const bool continuousHornet = parent && parent->type == "CARRIER" && object->type == "HORNET";
            // Original carrier.vxl black-floor voxel (76,22,7), independently
            // mapped through its native bounds: these are world-cell offsets,
            // not the commented-out carrier PrimaryFireFLH.
            const double theta = parent ? (parent->facing - 96) * 6.28318530717958647692 / 256.0 : 0.0;
            const double mouthX = parent ? runtimeObjectRenderX(*parent)
                + .09734963791237937 * std::cos(theta) - .02138275624245635 * std::sin(theta) : 0.0;
            const double mouthY = parent ? runtimeObjectRenderY(*parent)
                + .09734963791237937 * std::sin(theta) + .02138275624245635 * std::cos(theta) : 0.0;
            const double approach = std::hypot(before.x - mouthX, before.y - mouthY);
            const double airStep = 1.0 / std::max(1, runtimeMovementIntervalTicks(s, *object));
            // Only an actual near-mouth return may change height in the first
            // return tick, including a canceled partial launch. A distant
            // cruising aircraft still has zero vertical jump.
            const double verticalLimit = continuousHornet && approach <= std::max(.20, airStep * 1.25) + 1e-8
                ? 50.0 / 256.0 * 15.0 / std::max(1, s.config.fixedTickHz) + 1.0 / 65536.0 : 0.0;
            if (travel > maximum + 1e-8 || std::abs(runtimeObjectRenderLevel(*object) - before.level) > verticalLimit + 1e-8) {
                std::ostringstream detail;
                detail << "return transition snapped child=" << id << " travel=" << travel << " allowed=" << maximum
                    << " allowedVertical=" << verticalLimit
                    << " pose=" << before.x << ',' << before.y << ',' << before.level << " -> "
                    << runtimeObjectRenderX(*object) << ',' << runtimeObjectRenderY(*object) << ',' << runtimeObjectRenderLevel(*object);
                return fail(detail.str());
            }
        }
        return true;
    };
    const auto automaticReturnAtShot = [&](const RuntimeWindowState& s, int id, const std::set<int>& ids, int discharged) {
        const auto* object = findObjectById(s, id);
        const auto child = s.specialMechanisms.carrierChildren().find(id);
        if (!object || !object->airborne || child == s.specialMechanisms.carrierChildren().end())
            return fail("real shot lost its native aircraft: " + describe(s, ids));
        const auto* parent = findObjectById(s, child->second.parentObjectId);
        const bool threeShotAircraft = parent && ((parent->type == "CARRIER" && object->type == "HORNET")
            || (parent->type == "DEST" && object->type == "ASW"));
        if (threeShotAircraft && discharged < 3) {
            if (discharged < 1 || object->currentAmmo != 1 || child->second.ammoRemaining != 1
                || child->second.phase != CarrierChildPhase::Attacking
                || child->second.sortieShotsRemaining != 3 - discharged)
                return fail("first/second native aircraft shot did not retain its magazine and remaining sortie budget: " + describe(s, ids));
        } else if ((threeShotAircraft && discharged != 3) || object->currentAmmo != 0
            || child->second.ammoRemaining != 0 || child->second.phase != CarrierChildPhase::Returning
            || child->second.sortieShotsRemaining != 0)
            return fail("last real shot did not consume the magazine and automatically return: " + describe(s, ids));
        return true;
    };
    const auto completeShots = [](const std::set<int>& ids, const std::map<int, std::set<int>>& byChild, int perChild) {
        return byChild.size() == ids.size() && std::all_of(ids.begin(), ids.end(), [&](int id) {
            const auto found = byChild.find(id);
            return found != byChild.end() && static_cast<int>(found->second.size()) == perChild;
        });
    };
    const auto sameActors = [&](const RuntimeWindowState& a, const RuntimeWindowState& b, const std::set<int>& ids,
        bool exactBehaviorStates = true) {
        if (a.tick != b.tick) return false;
        for (int id : ids) {
            const auto* x = findObjectById(a, id); const auto* y = findObjectById(b, id);
            if ((!x) != (!y)) return false;
            if (!x) continue;
            if (x->health != y->health || x->x != y->x || x->y != y->y || x->level != y->level
                || x->airborne != y->airborne || x->dockedToObjectId != y->dockedToObjectId
                || x->dockIndex != y->dockIndex || x->currentAmmo != y->currentAmmo
                || x->weaponCooldownTicksRemaining != y->weaponCooldownTicksRemaining
                || std::abs(runtimeObjectRenderX(*x) - runtimeObjectRenderX(*y)) > 1e-8
                || std::abs(runtimeObjectRenderY(*x) - runtimeObjectRenderY(*y)) > 1e-8
                || std::abs(runtimeObjectRenderLevel(*x) - runtimeObjectRenderLevel(*y)) > 1e-8) return false;
            const auto cx = a.specialMechanisms.carrierChildren().find(id), cy = b.specialMechanisms.carrierChildren().find(id);
            if ((cx == a.specialMechanisms.carrierChildren().end()) != (cy == b.specialMechanisms.carrierChildren().end())) return false;
            if (cx != a.specialMechanisms.carrierChildren().end()
                && (cx->second.phase != cy->second.phase || cx->second.targetObjectId != cy->second.targetObjectId
                    || cx->second.actionTick != cy->second.actionTick || cx->second.ammoRemaining != cy->second.ammoRemaining
                    || cx->second.sortieShotsRemaining != cy->second.sortieShotsRemaining)) return false;
            const auto bx = a.behaviors.find(id), by = b.behaviors.find(id);
            if ((bx == a.behaviors.end()) != (by == b.behaviors.end())) return false;
            if (bx != a.behaviors.end() && (bx->second.kind != by->second.kind
                // Ordinary saves reconstruct generic parent Queued state.
                // Aircraft state and every full snapshot remain exact.
                || ((exactBehaviorStates || cx != a.specialMechanisms.carrierChildren().end())
                    && bx->second.state != by->second.state)
                || (cx != a.specialMechanisms.carrierChildren().end()
                    && (bx->second.weaponCooldownTicks != by->second.weaponCooldownTicks
                        || bx->second.moveCooldownTicks != by->second.moveCooldownTicks))
                || bx->second.targetObjectId != by->second.targetObjectId || bx->second.forceFire != by->second.forceFire
                || bx->second.autoAcquired != by->second.autoAcquired
                || bx->second.attackGround != by->second.attackGround || bx->second.attackBridge != by->second.attackBridge
                || ((bx->second.attackGround || bx->second.attackBridge)
                    && bx->second.attackBridgeCell != by->second.attackBridgeCell))) return false;
        }
        return true;
    };
    // Restore the complete ordinary authority first, then the canonical exact
    // multiplayer extension. Canonical export excludes process-local audio and
    // lighting hosts; it is not a standalone v14 save. The exact branch is a
    // frozen reference, leaving observations in the live scenario untouched.
    const auto persistence = [&](RuntimeWindowState& source, const std::set<int>& childIds) {
        std::ostringstream ordinaryText, exactText; IniFile ordinaryIni, exactIni;
        if (!writeRuntimeStateToStream(source, ordinaryText, error)
            || !writeRuntimeStateToStream(source, exactText, error)
            || !writeRuntimeMultiplayerSnapshotExtension(source, exactText, error)
            || !ordinaryIni.loadFromString(ordinaryText.str()) || !exactIni.loadFromString(exactText.str()))
            return fail("checkpoint write: " + error);
        std::ostringstream canonical;
        if (!writeRuntimeMultiplayerSnapshotCanonicalIni(exactIni, canonical, error)
            || !exactIni.loadFromString(canonical.str())) return fail("canonical snapshot write: " + error);
        const auto restore = [&](const IniFile& ini, bool exact) {
            auto state = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*state, source, false);
            if (!loadStockRules(*state)) return std::unique_ptr<RuntimeWindowState>{};
            if (!materializeRuntimeLoadStateFromIni(*state, ordinaryIni, error)) return std::unique_ptr<RuntimeWindowState>{};
            if (exact && !materializeRuntimeMultiplayerSnapshotExtension(*state, ini, error))
                return std::unique_ptr<RuntimeWindowState>{};
            if (state->rules.getDetailRulesHash() != stockRulesHash) return std::unique_ptr<RuntimeWindowState>{};
            return state;
        };
        auto ordinary = restore(ordinaryIni, false);
        if (!ordinary) return fail("ordinary checkpoint restore: " + error);
        auto replica = restore(exactIni, true);
        if (!replica) {
            // Keep the exact failed payload and independently round-trip each
            // source expired actor. This diagnoses authority/clock defects
            // without removing effects or changing the load contract.
            std::ostringstream diagnostic;
            diagnostic << "Naval checkpoint " << label << " tick=" << source.tick
                << " live=" << source.projectiles.size() << " expired=" << source.recentExpiredProjectiles.size()
                << " failure=" << error << '\n';
            std::set<int> actorIds;
            for (const auto& projectile : source.projectiles) actorIds.insert(projectile.actorId);
            for (std::size_t n = 0; n < source.recentExpiredProjectiles.size(); ++n) {
                const auto& projectile = source.recentExpiredProjectiles[n];
                const auto serialized = serializeRuntimeProjectileEffect(projectile);
                RuntimeProjectileEffect parsed; std::string payload;
                const bool decoded = iniHexDecode(exactIni.getValue("MultiplayerTransientEffects",
                    "RecentExpired" + std::to_string(n), ""), payload);
                diagnostic << "expired[" << n << "] actor=" << projectile.actorId
                    << " duplicate=" << !actorIds.insert(projectile.actorId).second
                    << " fields=" << splitRuntimeSaveFields(serialized).size()
                    << " sourceParses=" << parseRuntimeProjectileEffect(serialized, parsed, runtimeProjectileSaveSchemaV6, true)
                    << " payloadDecoded=" << decoded << " payloadExact=" << (decoded && payload == serialized)
                    << " lifecycle=" << static_cast<int>(projectile.lifecycle) << ':' << projectile.lifecycleState
                    << " spawn=" << projectile.spawnTick << " impact=" << projectile.impactTick
                    << " total=" << projectile.totalTicks << " remaining=" << projectile.remainingTicks
                    << " elapsed=" << projectile.elapsedTicks << " intent=" << projectile.renderIntent
                    << " weapon=" << projectile.weaponId << " type=" << projectile.projectileId
                    << " progress=" << projectile.progress << " xyz=" << projectile.currentX << ','
                    << projectile.currentY << ',' << projectile.currentZ << " nativeSource="
                    << projectile.sourceNativeFixedX << ',' << projectile.sourceNativeFixedY << '\n';
            }
            std::cout << diagnostic.str();
            if (output) {
                const auto prefix = std::filesystem::path(output) / (label + "-tick-" + std::to_string(source.tick));
                std::ofstream ordinaryDump(prefix.string() + "-ordinary-v14.ini", std::ios::binary);
                ordinaryDump << ordinaryText.str();
                std::ofstream exactDump(prefix.string() + "-canonical-mp.ini", std::ios::binary);
                exactDump << canonical.str();
                std::ofstream diagnosticDump(prefix.string() + "-checkpoint.txt", std::ios::binary);
                diagnosticDump << diagnostic.str();
            }
            return fail("snapshot checkpoint restore: " + error);
        }
        auto reference = restore(exactIni, true);
        if (!reference) return fail("snapshot reference restore: " + error);
        auto ids = childIds; ids.insert(1); ids.insert(2);
        if (!sameActors(source, *ordinary, ids, false))
            return fail("ordinary save changed active continuous pose/order: " + describe(*ordinary, childIds));
        if (!sameActors(source, *replica, ids) || !sameActors(source, *reference, ids))
            return fail("snapshot changed active continuous pose/order");
        // Persist the real payload, including the 8-field child magazine /
        // sortie budget and full active aircraft AttackOrder. No seeded effects
        // are described as ordinary animation persistence.
        const std::string saveBase = label + "-" + std::to_string(source.tick) + "-" + std::to_string(saves);
        std::string childRecords;
        if (const auto* rows = ordinaryIni.getSection("SpecialCarrierChildren"))
            for (const auto& [key, value] : *rows) childRecords += key + "=" + value + "\n";
        for (int format = 0; format < 2; ++format) {
            const auto name = saveBase + (format ? "-canonical-extension.ini" : "-ordinary14.ini");
            const auto targetPath = evidence / name;
            if (std::filesystem::exists(targetPath)) return fail("checkpoint evidence already exists");
            std::ofstream saved(targetPath, std::ios::binary);
            saved << (format ? canonical.str() : ordinaryText.str());
            if (!saved.good()) return fail("checkpoint evidence write failed");
            salvoCheckpoints << label << ',' << stage << ',' << (format ? "canonical-extension" : "ordinary14")
                << ',' << source.tick << ',' << source.specialMechanisms.stateHash() << ',' << childIds.size()
                << ',' << iniHexEncode(childRecords) << ',' << name << ",PASS\n";
        }
        ++saves; ++snapshots;
        const auto sameProjectiles = [](const RuntimeWindowState& a, const RuntimeWindowState& b) {
            if (a.projectiles.size() != b.projectiles.size()) return false;
            for (std::size_t i = 0; i < a.projectiles.size(); ++i)
                if (serializeRuntimeProjectileEffect(a.projectiles[i]) != serializeRuntimeProjectileEffect(b.projectiles[i])) return false;
            return true;
        };
        if (!sameProjectiles(source, *ordinary) || !sameProjectiles(source, *replica))
            return fail("saved actual projectile authority/clock changed");
        // The first/second ASW checkpoint must continue across both possible
        // ROF3+2 intervals; 18 ticks alone can precede even the next 60Hz shot.
        const bool partialAswSalvo = stage == "first-shot budget2" || stage == "second-shot budget1";
        const int continuationTicks = partialAswSalvo ? std::max(18, source.config.fixedTickHz * 2 / 3 + 2) : 18;
        for (int n = 0; n < continuationTicks; ++n) {
            step(*reference); step(*ordinary); step(*replica); ++pairedTicks;
            if (!sameActors(*reference, *ordinary, ids, false))
                return fail("ordinary restored flight diverged: " + describe(*ordinary, childIds)
                    + " reference: " + describe(*reference, childIds));
            if (!sameActors(*reference, *replica, ids)) return fail("snapshot restored flight diverged");
            if (!sameProjectiles(*reference, *ordinary) || !sameProjectiles(*reference, *replica))
                return fail("restored real projectile/ROF/impact timing diverged");
        }
        const auto activeGroundChild = std::find_if(childIds.begin(), childIds.end(), [&](int id) {
            const auto found = source.behaviors.find(id);
            return found != source.behaviors.end() && found->second.kind == RuntimeBehaviorKind::Attack
                && found->second.attackGround;
        });
        if (activeGroundChild != childIds.end()) {
            const int id = *activeGroundChild;
            const auto& intent = source.behaviors.at(id);
            const auto otherTarget = runtimeGroundAttackTargetObject(source,
                {intent.attackBridgeCell.x + 1, intent.attackBridgeCell.y});
            if (!otherTarget) return fail("invalid-save fixture has no adjacent legal ground cell");
            const auto replaceValue = [](const IniFile& ini, const std::string& sectionName,
                const std::string& key, const std::string& value, IniFile& replaced) {
                const auto* wanted = ini.getSection(sectionName);
                bool changed = false; std::ostringstream text;
                for (const auto& name : ini.getSectionNames()) {
                    const auto* section = ini.getSection(name); text << '[' << name << "]\n";
                    for (const auto& [rowKey, rowValue] : *section) {
                        const bool matches = section == wanted && rowKey == key;
                        text << rowKey << '=' << (matches ? value : rowValue) << '\n';
                        changed = changed || matches;
                    }
                    text << '\n';
                }
                return changed && replaced.loadFromString(text.str());
            };
            bool changed = false; IniFile corrupted;
            if (const auto* section = ordinaryIni.getSection("SpecialCarrierChildren")) for (const auto& [key, value] : *section) {
                auto fields = splitRuntimeSaveFields(value);
                if ((fields.size() != 7 && fields.size() != 8) || parseRuntimeIntOrDefault(fields[0], -1) != id) continue;
                fields[4] = std::to_string(otherTarget->instanceId);
                std::ostringstream record;
                for (std::size_t n = 0; n < fields.size(); ++n) record << (n ? "|" : "") << fields[n];
                changed = replaceValue(ordinaryIni, "SpecialCarrierChildren", key, record.str(), corrupted); break;
            }
            if (!changed) return fail("invalid-save child record missing");
            auto rejected = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*rejected, source, false);
            if (!loadStockRules(*rejected)) return fail("rejected-load fixture original owner rules");
            std::string rejection;
            if (materializeRuntimeLoadStateFromIni(*rejected, corrupted, rejection))
                return fail("ordinary load accepted inconsistent child ground ID/behavior cell");
            ++rejectedLoads;
            changed = false;
            if (const auto* section = exactIni.getSection("MultiplayerBehaviors")) for (const auto& [key, value] : *section) {
                int objectId = -1; RuntimeObjectBehavior behavior;
                if (!parseRuntimeMultiplayerSnapshotBehaviorRecord(value, objectId, behavior) || objectId != id) continue;
                ++behavior.attackBridgeCell.x;
                changed = replaceValue(exactIni, "MultiplayerBehaviors", key,
                    runtimeMultiplayerSnapshotBehaviorRecord(id, behavior), corrupted); break;
            }
            if (!changed) return fail("invalid-snapshot child behavior missing");
            rejected = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*rejected, source, false);
            if (!loadStockRules(*rejected)) return fail("rejected-snapshot fixture original owner rules");
            if (!materializeRuntimeLoadStateFromIni(*rejected, ordinaryIni, rejection))
                return fail("snapshot base failed before isolated intent corruption check: " + rejection);
            if (materializeRuntimeMultiplayerSnapshotExtension(*rejected, corrupted, rejection))
                return fail("snapshot accepted inconsistent child ground ID/behavior cell");
            ++rejectedLoads;
        }
        return true;
    };
    const auto capture = [&](RuntimeWindowState& s, const std::string& phase) {
        if (!output) return true;
        const auto stats = drawRuntimeStateOffscreenAtCell(s, {83, 80}, 1280, 720, error,
            std::string(output) + "/" + label + "-" + phase + ".bmp", false);
        if (!stats || !stats->rendered || stats->missingVoxelProxies != 0) return fail("native render: " + error);
        ++captures; return true;
    };
    const auto finishScene = [&](bool passed) {
        if (passed) { ++scenes; std::cout << "Naval spawn " << label << " PASS\n"; }
        else { failures.push_back(error); std::cout << error << '\n'; error.clear(); }
    };
    for (const std::string ship : {"CARRIER", "DEST"}) for (int hz : {30, 60})
    // Two complete native Hornet sorties deal 720 damage. Original DRED
    // Strength 800 survives both; original DEST Strength 600 cannot. DEST
    // remains unchanged in the moving/cancel/retarget partial-sortie controls.
    for (const std::string targetType : (ship == "CARRIER" ? std::vector<std::string>{"NAWEAP", "DRED"}
        : std::vector<std::string>{"SUB"})) for (int relation = 0; relation < 4; ++relation) {
        const std::string owner = relation == 1 ? "Americans" : relation == 2 ? "Neutral" : "Russians";
        const bool forced = relation != 0;
        label = ship + '-' + targetType + '-' + std::to_string(hz) + (forced ? "-force-" : "-normal-") + owner;
        const bool passed = [&]() {
            stage = "startup";
            auto storage = make(hz, ship, targetType, owner);
            if (!storage) return fail("real stock rule-owner load failed");
            auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            const auto ids = children(s, 1); const int count = ship == "CARRIER" ? 3 : 1;
            if (static_cast<int>(ids.size()) != count || !docked(s, ids, 1)) return fail("initial deck state: " + describe(s, ids));
            const auto* parent = findObjectById(s, 1); const auto* target = findObjectById(s, 2);
            if (!parent || !target || !runtimeCellIsPassable(s, s.terrainByCell,
                buildRuntimeOccupancyLookup(s), {76, 76}, *parent, 1))
                return fail("original water is not navigable");
            const auto profile = runtimeWeaponProfileForObject(s, *parent, target);
            if (!profile.weapon || !profile.weapon->spawner || profile.weaponId != (ship == "CARRIER" ? "HornetLauncher" : "ASWLauncher"))
                return fail("stock spawner selection");
            const std::string bomb = ship == "CARRIER" ? "HornetBomb" : "ASWBomb";
            const bool saveScene = relation == 1 && targetType != "DRED";
            const bool imageScene = hz == 30 && relation == 1 && targetType != "DRED";
            for (int round = 0; round < 2; ++round) {
                stage = "sortie " + std::to_string(round + 1);
                const int healthBefore = findObjectById(s, 2)->health;
                if (!order(s, 1, 2, forced)) return fail("real target command rejected: " + describe(s, ids));
                std::set<int> launched, fired, projectiles, impacts; bool recalled = false, returned = false;
                std::map<int, std::set<int>> shotsByChild;
                const int perChild = 3;
                std::map<int, int> previousShotTick;
                bool savedFirst = false, savedSecond = false;
                bool savedOutbound = false, savedReturn = false, capturedOutbound = false, capturedReturn = false;
                int recalledTick = -1;
                double maximumDistance = 0;
                for (int n = 0; n < hz * 35; ++n) {
                    const auto beforeFlight = observeFlights(s, ids);
                    step(s);
                    if (!continuousReturn(s, beforeFlight)) return false;
                    parent = findObjectById(s, 1);
                    if (!parent) return fail("parent vanished");
                    for (int id : ids) {
                        const auto* child = findObjectById(s, id);
                        if (!child) return fail("child vanished");
                        if (child->airborne) {
                            launched.insert(id);
                            maximumDistance = std::max(maximumDistance, std::hypot(runtimeObjectRenderX(*child) - runtimeObjectRenderX(*parent),
                                runtimeObjectRenderY(*child) - runtimeObjectRenderY(*parent)));
                        }
                        const auto behavior = s.behaviors.find(id);
                        const auto state = s.specialMechanisms.carrierChildren().find(id);
                        if (state == s.specialMechanisms.carrierChildren().end()) return fail("native slot state disappeared");
                        salvoMoves << label << ',' << hz << ',' << round + 1 << ',' << s.tick << ',' << id << ','
                            << child->type << ',' << child->health << ',' << child->alive << ','
                            << runtimeObjectRenderX(*child) << ',' << runtimeObjectRenderY(*child) << ','
                            << runtimeObjectRenderLevel(*child) << ',' << child->airborne << ',' << child->dockedToObjectId
                            << ',' << child->currentAmmo << ',' << static_cast<int>(state->second.phase)
                            << ',' << state->second.ammoRemaining << ',' << state->second.sortieShotsRemaining
                            << ',' << state->second.actionTick << ',' << child->weaponCooldownTicksRemaining
                            << ',' << (behavior == s.behaviors.end() ? -1 : behavior->second.moveCooldownTicks)
                            << ',' << findObjectById(s, 2)->health << '\n';
                        if (behavior != s.behaviors.end() && behavior->second.kind == RuntimeBehaviorKind::Attack
                            && (behavior->second.forceFire != forced || behavior->second.autoAcquired))
                            return fail("child did not inherit the actual parent attack intent");
                    }
                    for (const auto& p : s.projectiles) {
                        if (p.attackerObjectId == 1 && p.weaponId == profile.weaponId)
                            return fail("parent fired a virtual spawner as a damage projectile");
                        if (ids.contains(p.attackerObjectId) && p.weaponId == bomb) {
                            if (p.targetObjectId != 2 || p.affectsAllies != forced)
                                return fail("real child bomb lost selected target/force intent");
                            if (projectiles.insert(p.actorId).second) {
                                // Actor IDs count real discharges, not firing planes.
                                shotsByChild[p.attackerObjectId].insert(p.actorId);
                                if (!automaticReturnAtShot(s, p.attackerObjectId, ids,
                                    static_cast<int>(shotsByChild[p.attackerObjectId].size()))) return false;
                                fired.insert(p.attackerObjectId);
                                const auto* plane = findObjectById(s, p.attackerObjectId);
                                const auto& child = s.specialMechanisms.carrierChildren().at(p.attackerObjectId);
                                const auto* shotRules = s.rules.findWeaponType(bomb);
                                const int ordinal = static_cast<int>(shotsByChild[p.attackerObjectId].size());
                                if (!plane || !shotRules || p.projectileId != shotRules->projectile
                                    || p.warheadId != shotRules->warhead || p.damage != shotRules->damage)
                                    return fail("real aircraft discharge changed original damage/projectile/warhead");
                                if (ship == "DEST" && ordinal > 1) {
                                    const double gap = (p.spawnTick - previousShotTick.at(p.attackerObjectId)) * 15.0 / hz;
                                    // Original GetROF adds 0..2 rule frames to rookie ROF3.
                                    if (gap < 3.0 || gap > 5.0) return fail("ASWBomb original ROF3+0..2 clock changed");
                                }
                                previousShotTick[p.attackerObjectId] = p.spawnTick;
                                salvoShots << label << ',' << hz << ',' << round + 1 << ',' << s.tick << ','
                                    << p.actorId << ',' << p.attackerObjectId << ',' << child.slot << ',' << ordinal
                                    << ',' << p.spawnTick << ',' << p.weaponId << ',' << p.projectileId << ',' << p.warheadId
                                    << ',' << p.damage << ',' << shotRules->rof << ",1," << child.ammoRemaining
                                    << ',' << child.sortieShotsRemaining << ',' << static_cast<int>(child.phase)
                                    << ',' << p.sourceX << ',' << p.sourceY << ',' << p.sourceZ
                                    << ',' << p.targetX << ',' << p.targetY << ',' << p.targetZ
                                    << ',' << findObjectById(s, 2)->health << '\n';
                            }
                        }
                    }
                    if (ship == "DEST" && round == 0 && saveScene && shotsByChild.size() == 1) {
                        const int discharged = static_cast<int>(shotsByChild.begin()->second.size());
                        if ((discharged == 1 && !savedFirst) || (discharged == 2 && !savedSecond)) {
                            stage = discharged == 1 ? "first-shot budget2" : "second-shot budget1";
                            if (!persistence(s, ids)) return false;
                            if (discharged == 1) savedFirst = true; else savedSecond = true;
                            stage = "sortie " + std::to_string(round + 1);
                        }
                    }
                    for (const auto& impact : s.impactEffects) if (projectiles.contains(impact.projectileActorId)
                        && impact.targetObjectId == 2 && impact.weaponId == bomb && impact.damage > 0
                        && impact.lifecycleState == "Impact") impacts.insert(impact.projectileActorId);
                    if (round == 0 && !savedOutbound && launched.size() == ids.size() && fired.empty() && maximumDistance > .5) {
                        if (saveScene && !persistence(s, ids)) return false;
                        savedOutbound = true;
                        if (imageScene && !capture(s, "outbound")) return false;
                        capturedOutbound = true;
                    }
                    if (!recalled && completeShots(ids, shotsByChild, perChild)) {
                        // Both stock HORNET and ASW slots have discharged
                        // all three actual actors before the explicit Stop.
                        // Stop prevents another sortie while the slots drain.
                        if (!stop(s, 1)) return fail("actual Stop rejected after real bombs");
                        recalled = true; recalledTick = s.tick;
                    }
                    const bool inReturn = std::any_of(ids.begin(), ids.end(), [&](int id) {
                        const auto found = s.specialMechanisms.carrierChildren().find(id);
                        return found != s.specialMechanisms.carrierChildren().end() && found->second.phase == CarrierChildPhase::Returning;
                    });
                    if (round == 0 && recalled && inReturn && s.tick > recalledTick && !savedReturn) {
                        if (saveScene && !persistence(s, ids)) return false;
                        savedReturn = true;
                        if (imageScene && !capture(s, "return")) return false;
                        capturedReturn = true;
                    }
                    if (recalled && docked(s, ids, 1) && impacts.size() == projectiles.size()
                        && std::none_of(s.projectiles.begin(), s.projectiles.end(),
                        [&](const auto& p) { return ids.contains(p.attackerObjectId) && p.weaponId == bomb; })) {
                        returned = true; break;
                    }
                }
                target = findObjectById(s, 2);
                const int expectedDamage = ship == "DEST" ? 150 : 360;
                if (!target || healthBefore - target->health != expectedDamage || launched.size() != ids.size() || fired.size() != ids.size()
                    || !completeShots(ids, shotsByChild, perChild)
                    || projectiles.size() != ids.size() * perChild || impacts.size() != projectiles.size() || !returned || maximumDistance < 1.0)
                    return fail("fly/fire/impact/physical hidden return incomplete: " + describe(s, ids));
                int readyAtReturn = s.tick;
                for (const auto& [slot, readyTick] : s.specialMechanisms.carrierParents().at(1).slotReadyTick)
                    readyAtReturn = std::max(readyAtReturn, readyTick);
                if (ship == "DEST" && readyAtReturn != s.tick + 150)
                    return fail("original DEST reload150 tick deadline changed");
                salvoCases << label << ',' << hz << ',' << ship << ',' << (ship == "DEST" ? "ASW" : "HORNET")
                    << ',' << round + 1 << ',' << ids.size() << ',' << perChild << ',' << projectiles.size()
                    << ',' << healthBefore << ',' << target->health << ',' << expectedDamage << ",1," << s.tick
                    << ',' << readyAtReturn << ",150," << (ship == "DEST" ? 400 : 600) << ",PASS\n";
                sorties += static_cast<int>(projectiles.size());
                if (round == 0 && (!savedOutbound || !savedReturn
                    || (ship == "DEST" && saveScene && (!savedFirst || !savedSecond))
                    || (imageScene && (!capturedOutbound || !capturedReturn))))
                    return fail("outbound/return checkpoint boundary missing");
                if (round == 0) {
                    const auto& parentState = s.specialMechanisms.carrierParents().at(1);
                    int ready = s.tick;
                    for (const auto& [slot, readyTick] : parentState.slotReadyTick) ready = std::max(ready, readyTick);
                    if (ready <= s.tick) return fail("stock reload delay missing");
                    // Guard may autonomously re-acquire an enemy after Stop.
                    // Stay inside through the last ineligible tick, then issue
                    // the second explicit order before the next ready tick.
                    while (s.tick + 1 < ready) {
                        step(s);
                        if (!docked(s, ids, 1)) return fail("aircraft did not stay inside ship during reload");
                    }
                    if (children(s, 1) != ids) return fail("return regenerated aircraft instead of rearming same slots");
                    if (imageScene && !capture(s, "inside-ready")) return false;
                }
            }
            return true;
        }();
        finishScene(passed);
    }

    // Return legs follow an actual moving parent and attack legs follow an
    // actual moving target. Target movement uses its normal movement behavior.
    for (const std::string ship : {"CARRIER", "DEST"}) for (int hz : {30, 60}) {
        label = ship + '-' + std::to_string(hz) + "-moving-target-and-parent";
        const bool passed = [&]() {
            stage = "moving target";
            auto storage = make(hz, ship, ship == "CARRIER" ? "DEST" : "SUB", "Americans");
            if (!storage) return fail("real stock rule-owner load failed");
            auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            const auto ids = children(s, 1);
            s.selectedObjectIds = {2};
            const auto* victim = findObjectById(s, 2);
            const auto destination = RuntimeCellPoint{victim->x, victim->y + 2};
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, destination)
                || s.lastCommand.acceptedCount != 1 || !order(s, 1, 2, true)) return fail("actual move/force order");
            const int healthBefore = findObjectById(s, 2)->health;
            const auto startTarget = runtimeCellPointForObject(*findObjectById(s, 2));
            std::set<int> fired; bool movingParent = false, returned = false;
            const auto startParent = runtimeCellPointForObject(*findObjectById(s, 1));
            for (int n = 0; n < hz * 35; ++n) {
                const auto beforeFlight = observeFlights(s, ids);
                step(s);
                if (!continuousReturn(s, beforeFlight)) return false;
                for (const auto& p : s.projectiles)
                    if (ids.contains(p.attackerObjectId) && p.weaponId == (ship == "CARRIER" ? "HornetBomb" : "ASWBomb")) fired.insert(p.attackerObjectId);
                if (!movingParent && fired.size() == ids.size()) {
                    stage = "moving parent"; s.selectedObjectIds = {1};
                    if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Move, {78, 78})
                        || s.lastCommand.acceptedCount != 1) return fail("parent move rejected");
                    movingParent = true;
                }
                if (movingParent && docked(s, ids, 1)
                    && runtimeCellPointForObject(*findObjectById(s, 1)) != startParent
                    && std::none_of(s.projectiles.begin(), s.projectiles.end(), [&](const auto& p) { return ids.contains(p.attackerObjectId); })) {
                    returned = true; break;
                }
            }
            victim = findObjectById(s, 2);
            if (!victim || victim->health >= healthBefore || runtimeCellPointForObject(*victim) == startTarget
                || fired.size() != ids.size() || !returned) return fail("moving rendezvous incomplete: " + describe(s, ids));
            sorties += static_cast<int>(ids.size()); return true;
        }(); finishScene(passed);
    }

    for (const std::string ship : {"CARRIER", "DEST"}) for (int hz : {30, 60}) for (int boundary = 0; boundary < 3; ++boundary) {
        label = ship + '-' + std::to_string(hz) + (boundary == 0 ? "-cancel" : boundary == 1 ? "-target-removed" : "-retarget-intent");
        const bool passed = [&]() {
            stage = "outbound boundary";
            auto storage = make(hz, ship, ship == "CARRIER" ? "NAWEAP" : "SUB", "Americans");
            if (!storage) return fail("real stock rule-owner load failed");
            auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            const auto ids = children(s, 1);
            if (!order(s, 1, 2, true)) return fail("actual forced attack");
            bool outbound = false;
            for (int n = 0; n < hz * 8; ++n) {
                step(s); const auto* parent = findObjectById(s, 1);
                outbound = std::any_of(ids.begin(), ids.end(), [&](int id) {
                    const auto* child = findObjectById(s, id);
                    return child && child->airborne && std::hypot(runtimeObjectRenderX(*child) - runtimeObjectRenderX(*parent),
                        runtimeObjectRenderY(*child) - runtimeObjectRenderY(*parent)) > .5;
                });
                if (outbound) break;
            }
            if (!outbound) return fail("never left deck: " + describe(s, ids));
            const int originalHealth = findObjectById(s, 2)->health;
            if (boundary == 0) { stage = "Stop cancellation"; if (!stop(s, 1)) return fail("actual Stop rejected"); }
            else if (boundary == 1) {
                stage = "dead target";
                RuntimeObjectExitRequest request; request.objectId = 2; request.kind = RuntimeObjectExitKind::Destroyed;
                request.tick = s.tick; request.mission = "Destroyed"; request.reason = "naval sortie target removed by another actor";
                if (queueRuntimeObjectExit(s, std::move(request)) != RuntimeObjectExitQueueResult::Queued) return fail("target exit queue");
                drainRuntimeObjectExitQueue(s); completeRuntimeWorldRefresh(s);
            } else {
                stage = "forced to ordinary retarget";
                add(s, 3, ship == "CARRIER" ? "NAWEAP" : "SUB", "Russians", ship == "CARRIER" ? RuntimeCellPoint{91, 79} : RuntimeCellPoint{82, 77});
                initializeRuntimeObjectCloakState(s, *findMutableObjectById(s, 3));
                refreshRuntimeObjectOccupancyAfterLocalChange(s);
                if (!order(s, 1, 3, false)) return fail("actual ordinary retarget");
            }
            std::set<int> fired; bool recalled = boundary != 2, returned = false;
            for (int n = 0; n < hz * 30; ++n) {
                const auto beforeFlight = observeFlights(s, ids);
                step(s);
                if (!continuousReturn(s, beforeFlight)) return false;
                for (const auto& p : s.projectiles) if (ids.contains(p.attackerObjectId)
                    && p.weaponId == (ship == "CARRIER" ? "HornetBomb" : "ASWBomb")) {
                    if (boundary != 2 || p.targetObjectId != 3 || p.affectsAllies) return fail("stale/cancelled child fired");
                    fired.insert(p.attackerObjectId);
                }
                if (boundary == 2) for (int id : ids) {
                    const auto found = s.behaviors.find(id);
                    if (found != s.behaviors.end() && found->second.kind == RuntimeBehaviorKind::Attack
                        && (found->second.targetObjectId != 3 || found->second.forceFire)) return fail("active child intent did not follow parent retarget");
                }
                if (!recalled && fired.size() == ids.size()) { if (!stop(s, 1)) return fail("post-retarget Stop"); recalled = true; }
                if (recalled && docked(s, ids, 1)
                    && std::none_of(s.projectiles.begin(), s.projectiles.end(), [&](const auto& p) { return ids.contains(p.attackerObjectId); })) {
                    returned = true; break;
                }
            }
            if (!returned) return fail("abandoned/retargeted flight did not return: " + describe(s, ids));
            const auto* oldTarget = findObjectById(s, 2);
            if (boundary != 1 && (!oldTarget || oldTarget->health != originalHealth)) return fail("old forced target was damaged after cancellation/retarget");
            if (boundary == 2) {
                if (fired.size() != ids.size() || !findObjectById(s, 3)
                    || findObjectById(s, 3)->health >= runtimeMaxHealthForObject(s, *findObjectById(s, 3))) return fail("retarget bomb did not actually damage enemy");
                sorties += static_cast<int>(ids.size());
            }
            return true;
        }(); finishScene(passed);
    }

    // A dead ship disposes only its own dependent aircraft. A second real
    // sortie remains independent and finishes its attack/return.
    for (const std::string ship : {"CARRIER", "DEST"}) for (int hz : {30, 60}) {
        label = ship + '-' + std::to_string(hz) + "-parent-loss-isolation";
        const bool passed = [&]() {
            stage = "two parents";
            auto storage = make(hz, ship, ship == "CARRIER" ? "NAWEAP" : "SUB", "Russians", true);
            if (!storage) return fail("real stock rule-owner load failed");
            auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            const auto lost = children(s, 1), survivors = children(s, 10);
            if (!order(s, 1, 2, false) || !order(s, 10, 20, false)) return fail("two actual attacks rejected");
            for (int n = 0; n < 3; ++n) step(s);
            RuntimeObjectExitRequest request; request.objectId = 1; request.kind = RuntimeObjectExitKind::Destroyed;
            request.tick = s.tick; request.mission = "Destroyed"; request.reason = "naval sortie parent removed";
            if (queueRuntimeObjectExit(s, std::move(request)) != RuntimeObjectExitQueueResult::Queued) return fail("parent exit queue");
            drainRuntimeObjectExitQueue(s); completeRuntimeWorldRefresh(s); stage = "dependent cleanup";
            for (int id : lost) if (findObjectById(s, id) || s.behaviors.contains(id)
                || s.specialMechanisms.carrierChildren().contains(id)) return fail("lost ship left orphan aircraft");
            const int before = findObjectById(s, 20)->health; std::set<int> fired; bool recalled = false, returned = false;
            for (int n = 0; n < hz * 35; ++n) {
                const auto beforeFlight = observeFlights(s, survivors);
                step(s);
                if (!continuousReturn(s, beforeFlight)) return false;
                for (const auto& p : s.projectiles) if (survivors.contains(p.attackerObjectId)
                    && p.weaponId == (ship == "CARRIER" ? "HornetBomb" : "ASWBomb")) fired.insert(p.attackerObjectId);
                if (!recalled && fired.size() == survivors.size()) { if (!stop(s, 10)) return fail("survivor Stop"); recalled = true; }
                if (recalled && docked(s, survivors, 10)
                    && std::none_of(s.projectiles.begin(), s.projectiles.end(), [&](const auto& p) { return survivors.contains(p.attackerObjectId); })) { returned = true; break; }
            }
            if (!returned || fired.size() != survivors.size() || !findObjectById(s, 20)
                || findObjectById(s, 20)->health >= before) return fail("other ship's sortie was disturbed: " + describe(s, survivors));
            sorties += static_cast<int>(survivors.size()); return true;
        }(); finishScene(passed);
    }

    // DEST's stock primary remains available for surface targets. Its ASW
    // plane stays inside; HORNET's stock ground-only weapon rejects aircraft.
    for (int hz : {30, 60}) for (const std::string targetType : {"NAWEAP", "DEST"}) {
        label = "DEST-" + targetType + '-' + std::to_string(hz) + "-primary-only";
        const bool passed = [&]() {
            stage = "stock targeting";
            auto storage = make(hz, "DEST", targetType, "Americans");
            if (!storage) return fail("real stock rule-owner load failed");
            auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            const auto ids = children(s, 1); const auto* victim = findObjectById(s, 2);
            // Bring the target into authored 155mm range on legal terrain.
            auto* mutableVictim = findMutableObjectById(s, 2);
            mutableVictim->x = targetType == "NAWEAP" ? 89 : 79;
            mutableVictim->y = targetType == "NAWEAP" ? 75 : 78;
            mutableVictim->cell = runtimeCellKey(mutableVictim->x, mutableVictim->y);
            auto* parent = findMutableObjectById(s, 1);
            if (targetType == "NAWEAP") { parent->x = 85; parent->cell = runtimeCellKey(parent->x, parent->y); }
            refreshRuntimeObjectOccupancyAfterLocalChange(s);
            victim = findObjectById(s, 2); parent = findMutableObjectById(s, 1);
            const int before = victim->health;
            const auto profile = runtimeWeaponProfileForObject(s, *parent, victim);
            if (profile.weaponId != "155mm" || !order(s, 1, 2, true)) return fail("ordinary primary unavailable");
            bool cannon = false;
            for (int n = 0; n < hz * 12; ++n) {
                step(s);
                if (!docked(s, ids, 1)) return fail("ASW launched against surface/land target");
                for (const auto& p : s.projectiles) if (p.attackerObjectId == 1 && p.weaponId == "155mm") cannon = true;
                const auto* target = findObjectById(s, 2);
                if (target && target->health < before && cannon) return true;
            }
            return fail("real 155mm did not damage legal target");
        }(); finishScene(passed);
    }
    for (int hz : {30, 60}) {
        label = "CARRIER-" + std::to_string(hz) + "-air-rejected";
        const bool passed = [&]() {
            stage = "stock anti-air restriction";
            auto storage = make(hz, "CARRIER", "DEST", "Russians");
            if (!storage) return fail("real stock rule-owner load failed");
            auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            auto* victim = findMutableObjectById(s, 2); victim->type = "ORCA"; victim->authoredAircraft = true;
            victim->airborne = true; victim->level = victim->flightLevel = runtimeAircraftCruiseLevel(s);
            victim->health = runtimeMaxHealthForObject(s, *victim);
            refreshRuntimeObjectOccupancyAfterLocalChange(s); const auto ids = children(s, 1);
            s.selectedObjectIds = {1};
            dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::Attack, {victim->x, victim->y}, victim);
            if (s.lastCommand.acceptedCount != 0) return fail("stock ground-only carrier accepted aircraft target");
            for (int n = 0; n < hz * 2; ++n) { step(s); if (!docked(s, ids, 1)) return fail("air-rejected order launched aircraft"); }
            return true;
        }(); finishScene(passed);
    }
    // Retail carriers also target empty cells and destructible bridges. These
    // commands must produce aircraft bombs, with no fabricated world target.
    for (int hz : {30, 60}) for (int surface = 0; surface < 3; ++surface) {
        label = "CARRIER-" + std::to_string(hz) + (surface == 0 ? "-empty-land" : surface == 1 ? "-empty-water" : "-bridge");
        const bool passed = [&]() {
            stage = "terrain command";
            auto storage = make(hz, "CARRIER", "NAWEAP", "Russians");
            if (!storage) return fail("real stock rule-owner load failed");
            auto& s = *storage;
            RuntimeSnapshotActiveStateScope active(s);
            const RuntimeCellPoint requested = surface == 0 ? RuntimeCellPoint{95, 79}
                : surface == 1 ? RuntimeCellPoint{90, 90} : RuntimeCellPoint{90, 89};
            if (surface == 2) {
                // Original three-lane wood deck over actual water TMP tiles.
                // Every lane uses its real overlay frame and damage mapping.
                for (int x = 88; x <= 96; ++x) for (int lane = 0; lane < 3; ++lane) {
                    auto* terrain = findMutableRuntimeTerrainCellAt(s, {x, 88 + lane});
                    if (!terrain) return fail("bridge authored water missing");
                    runtimeSetLowBridgeOverlayOffset(*terrain, RuntimeBridgeMaterial::Wood, 0, lane);
                }
                s.destroyableBridges = true; initializeRuntimeNavigation(s);
            }
            const auto synthetic = surface == 2 ? runtimeBridgeAttackTargetObject(s, requested)
                : runtimeGroundAttackTargetObject(s, requested);
            if (!synthetic || synthetic->instanceId >= -1 || findObjectById(s, synthetic->instanceId))
                return fail("canonical terrain target mapping");
            const auto ids = children(s, 1); const auto objectCount = s.world.getObjects().size();
            const auto targetCell = runtimeCellPointForObject(*synthetic);
            s.selectedObjectIds = {1};
            if (!dispatchRuntimeTargetOrderFromUi(s, RuntimeMultiplayerGameplayTargetOrderKind::ForceFireCell, requested, nullptr)
                || s.lastCommand.acceptedCount != 1 || !s.behaviors.contains(1)) return fail("real Ctrl empty-cell order rejected");
            const auto parentIntent = s.behaviors.at(1);
            if (parentIntent.targetObjectId != synthetic->instanceId || parentIntent.attackGround != (surface != 2)
                || parentIntent.attackBridge != (surface == 2) || parentIntent.attackBridgeCell != targetCell)
                return fail("real terrain order lost original target encoding");
            std::set<int> launched, fired, projectiles, impacts;
            std::map<int, std::set<int>> shotsByChild;
            bool savedOutbound = false, savedReturn = false, stopped = false, returned = false;
            int stoppedTick = -1; double maximumDistance = 0;
            for (int n = 0; n < hz * 40; ++n) {
                const auto beforeFlight = observeFlights(s, ids);
                step(s);
                if (!continuousReturn(s, beforeFlight)) return false;
                const auto* parent = findObjectById(s, 1);
                if (!parent || s.world.getObjects().size() != objectCount) return fail("terrain order fabricated/lost a world object");
                for (int id : ids) {
                    const auto* child = findObjectById(s, id);
                    if (!child) return fail("terrain sortie child disappeared");
                    if (child->airborne) {
                        launched.insert(id);
                        maximumDistance = std::max(maximumDistance, std::hypot(runtimeObjectRenderX(*child) - runtimeObjectRenderX(*parent),
                            runtimeObjectRenderY(*child) - runtimeObjectRenderY(*parent)));
                    }
                    const auto found = s.behaviors.find(id);
                    if (found != s.behaviors.end() && found->second.kind == RuntimeBehaviorKind::Attack
                        && (found->second.forceFire != parentIntent.forceFire
                            || found->second.attackGround != parentIntent.attackGround
                            || found->second.attackBridge != parentIntent.attackBridge
                            || found->second.attackBridgeCell != parentIntent.attackBridgeCell))
                        return fail("child did not inherit the actual terrain intent");
                }
                for (const auto& p : s.projectiles) {
                    if (p.attackerObjectId == 1 && p.weaponId == "HornetLauncher") return fail("virtual launcher damaged terrain");
                    if (!ids.contains(p.attackerObjectId) || p.weaponId != "HornetBomb") continue;
                    if (p.targetObjectId >= 0 || p.toCell != targetCell || p.damage != 40
                        || p.affectsAllies != (parentIntent.forceFire || parentIntent.attackGround || parentIntent.attackBridge))
                        return fail("real bomb did not aim at the original empty cell/bridge");
                    if (projectiles.insert(p.actorId).second) {
                        shotsByChild[p.attackerObjectId].insert(p.actorId);
                        if (!automaticReturnAtShot(s, p.attackerObjectId, ids,
                            static_cast<int>(shotsByChild[p.attackerObjectId].size()))) return false;
                        fired.insert(p.attackerObjectId);
                    }
                }
                for (const auto& impact : s.impactEffects) if (projectiles.contains(impact.projectileActorId)
                    && impact.weaponId == "HornetBomb" && impact.cell == targetCell && impact.damage == 40
                    && impact.lifecycleState == "Impact") impacts.insert(impact.projectileActorId);
                if (!savedOutbound && launched.size() == ids.size() && fired.empty() && maximumDistance > .5) {
                    stage = "terrain outbound save";
                    if (!persistence(s, ids)) return false;
                    savedOutbound = true;
                    if (hz == 30 && !capture(s, "outbound")) return false;
                }
                if (!stopped && projectiles.size() == 9 && completeShots(ids, shotsByChild, 3)) {
                    stage = "terrain automatic return";
                    if (!stop(s, 1)) return fail("terrain post-shot Stop rejected");
                    stopped = true; stoppedTick = s.tick;
                }
                if (stopped && s.tick > stoppedTick && !savedReturn && std::any_of(ids.begin(), ids.end(), [&](int id) {
                    const auto found = s.specialMechanisms.carrierChildren().find(id);
                    return found != s.specialMechanisms.carrierChildren().end() && found->second.phase == CarrierChildPhase::Returning;
                })) {
                    stage = "terrain return save";
                    if (!persistence(s, ids)) return false;
                    savedReturn = true;
                }
                if (stopped && docked(s, ids, 1) && impacts.size() == projectiles.size()
                    && std::none_of(s.projectiles.begin(), s.projectiles.end(), [&](const auto& p) { return ids.contains(p.attackerObjectId); })) {
                    returned = true; break;
                }
            }
            if (!savedOutbound || !savedReturn || !returned || launched.size() != 3 || fired.size() != 3
                || projectiles.size() != 9 || !completeShots(ids, shotsByChild, 3)
                || impacts.size() != projectiles.size() || maximumDistance < 5)
                return fail("terrain fly/bomb/impact/automatic hidden return incomplete: " + describe(s, ids));
            if (surface == 2) {
                const auto health = s.bridgeHealthByCell.find(runtimeCellKey(targetCell.x, targetCell.y));
                if (health == s.bridgeHealthByCell.end() || health->second >= s.rules.getBridgeStrength())
                    return fail("real bombs did not damage the original bridge");
            }
            if (hz == 30 && !capture(s, "inside-after-impact")) return false;
            sorties += static_cast<int>(projectiles.size()); return true;
        }(); finishScene(passed);
    }
    if (!failures.empty()) {
        std::ostringstream summary; summary << failures.size() << " naval spawn scene(s) failed:";
        for (const auto& failure : failures) summary << '\n' << failure;
        error = summary.str(); return false;
    }
    std::cout << "Naval spawn passed: scenes=" << scenes << " realAircraftSorties=" << sorties
        << " ordinarySaves=" << saves << " exactSnapshots=" << snapshots << " pairedTicks=" << pairedTicks
        << " nativeCaptures=" << captures << " malformedIntentLoadsRejected=" << rejectedLoads
        << " force/normal/stock-targeting/three-shots-per-Hornet-and-ASW/first-second-magazine-budget/impact/hidden-return/reload/retarget/loss/cell/bridge=ok\n";
    return true;
}
