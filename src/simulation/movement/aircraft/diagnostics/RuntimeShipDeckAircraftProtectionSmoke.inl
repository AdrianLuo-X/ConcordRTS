// Native ship/deck contract. Original rules, real UI commands and real weapon
// projectiles are the only source of damage in these fixtures.
bool runRuntimeShipDeckAircraftProtectionSmoke(const RuntimeConfig& config, std::string& error) {
    std::cout << std::unitbuf;
    auto assets = std::make_unique<RuntimeWindowState>();
    assets->config = config; assets->config.playerSlots.clear();
    assets->config.computerAiEnabled = false; assets->config.gameModeRulesOverride.clear();
    assets->report = buildRuntimeStartupReport(assets->config, &assets->world, &assets->theater,
        &assets->art, &assets->rules, &assets->mapFacts);
    if (!assets->report.success) { error = "Ship deck assets: " + assets->report.errorMessage; return false; }
    IniFile stock;
    if (!stock.loadFromFile(assets->report.rulesPath) || !assets->rules.loadFromIni(stock)) {
        error = "Ship deck original rules unavailable"; return false;
    }
    assets->rulesLoaded = assets->artLoaded = assets->theaterLoaded = true;
    const auto rulesHash = assets->rules.getDetailRulesHash();
    int waterTile = -1, landTile = -1;
    const int waterSet = assets->theater.getGeneralTileSetIndex("WaterSet");
    for (const auto& set : assets->theater.getTileSets())
        if (set.index == waterSet && set.tilesInSet > 0) waterTile = set.startTileIndex;
    for (int tile=0;tile<static_cast<int>(assets->theater.getTotalTileSlots());++tile) {
        TerrainCell terrain; terrain.tileIndex=tile; terrain.subTileIndex=0;
        const auto info=runtimeTerrainInfoForCell(*assets,terrain);
        if (info.passable && !info.water && !info.shore && !info.cliff && !info.bridge) { landTile=tile; break; }
    }
    if (waterTile < 0 || landTile < 0) { error = "Ship deck original TMP water/land unavailable"; return false; }
    const auto* cannon = assets->rules.findWeaponType("155mm");
    const auto* aa = assets->rules.findWeaponType("Medusa");
    const auto* aaProjectile=assets->rules.findProjectileType("MedusaProjectile");
    const auto artyVerses=splitRuntimeTokenList(stock.getValue("ARTYHE","Verses",""));
    const auto aaVerses=splitRuntimeTokenList(stock.getValue("SAMWH","Verses",""));
    const auto missileVerses=splitRuntimeTokenList(stock.getValue("DMISLWH","Verses",""));
    if (!cannon || cannon->damage != 60 || cannon->projectile != "Ballistic" || cannon->warhead != "ARTYHE"
        || !aa || aa->damage != 100 || aa->warhead != "SAMWH" || aa->projectile != "MedusaProjectile"
        || !aaProjectile || !aaProjectile->aa || aaProjectile->ag || aaProjectile->rot!=20
        || artyVerses.size()!=11 || artyVerses[3]!="100%" || artyVerses[5]!="60%"
        || aaVerses.size()!=11 || aaVerses[3]!="100%"
        || missileVerses.size()!=11 || missileVerses[5]!="80%"
        || stock.getValue("AEGIS","DistributedFire","")!="yes"
        || stock.getValue("HORNET", "Strength", "") != "75" || stock.getValue("ASW", "Strength", "") != "30"
        || stock.getValue("HORNET", "Speed", "") != "12" || stock.getValue("ASW", "Speed", "") != "12"
        || stock.getValue("HORNET", "Ammo", "") != "1" || stock.getValue("ASW", "Ammo", "") != "1"
        || stock.getValue("DEST", "SpawnRegenRate", "") != "400"
        || stock.getValue("CARRIER", "SpawnRegenRate", "") != "600"
        || stock.getValue("DEST", "SpawnReloadRate", "") != "150"
        || stock.getValue("CARRIER", "SpawnReloadRate", "") != "150"
        || stock.getValue("HornetBomb", "Damage", "") != "40" || stock.getValue("HornetBomb", "ROF", "") != "3"
        || stock.getValue("ASWBomb", "Damage", "") != "50" || stock.getValue("ASWBomb", "ROF", "") != "3"
        || stock.getValue("General", "DMislDamage", "") != "300") {
        error = "Ship deck authored HP/Speed/Ammo/ROF/Regen/Reload/weapons changed"; return false;
    }
    const char* output = std::getenv("CONCORD_SHIP_DECK_AIRCRAFT_PROTECTION_OUTPUT");
    const char* selected = std::getenv("CONCORD_SHIP_DECK_AIRCRAFT_PROTECTION_CASE");
    // This path belongs to the current project, never to the shared Windows
    // temporary directory used by unrelated native tests.
    const std::filesystem::path evidence = output && *output ? std::filesystem::path(output)
        : std::filesystem::path(config.localDataRootPath).parent_path() / "build/smoke/deck-protection";
    std::filesystem::create_directories(evidence);
    std::ofstream cases(evidence / "cases.csv"), events(evidence / "events.csv"), checkpoints(evidence / "checkpoints.csv"),
        deathWitnesses(evidence / "aa_death_witnesses.csv"), sinkWitnesses(evidence / "ship_sink_missiles.csv"),
        shellWitnesses(evidence / "shell_hits.csv");
    if (!cases || !events || !checkpoints || !deathWitnesses || !sinkWitnesses || !shellWitnesses) { error = "Ship deck evidence unavailable"; return false; }
    cases << "scene,hz,moving,parent,child_type,child_hp,planes,cannon_damage,sortie_actors,sortie_damage,designated_aa_kills,collateral_aa_kills,saves,snapshots,paired_ticks,regen,reload,sunk,lethal_airborne_children,lethal_parked_children,result\n";
    events << std::setprecision(17) << "scene,stage,tick,object,type,hp,alive,airborne,phase,x,y,z,protected,actor,weapon,projectile,warhead,payload_damage,raw_rule_damage,raw_parameter_source,raw_parameter_damage\n";
    checkpoints << "scene,stage,format,tick,carrier_hash,file,result\n";
    deathWitnesses << "scene,stage,tick,aa_actor,dead_plane,type,hp_before,hp_after,alive_before,alive_after,airborne_before,airborne_after,protected_before,protected_after,phase_before,designated_victim\n";
    sinkWitnesses << std::setprecision(17) << "scene,event,tick,source,missile,actor,start_tick,release_tick,target,general_damage,payload_damage,heavy_damage,hp_before,hp_after,airborne_children,parked_children,origin_x,origin_y,origin_z,result\n";
    shellWitnesses << std::setprecision(17) << "scene,stage,tick,actor,raw_rule_damage,payload_damage,warhead,impact_x,impact_y,impact_z,impact_cell_x,impact_cell_y,parent_cell_x,parent_cell_y,parent_body_x,parent_body_y,distance,cell_spread,percent_at_max,expected_damage,hp_before,hp_after,actual_damage,result\n";
    std::string label, stage;
    int completed = 0, saves = 0, snapshots = 0, pairedTicks = 0, livePairedTicks = 0, aaKills = 0, aaCollateralKills = 0, sunk = 0;
    int aswAirborneShipSinks=0;
    const auto fail = [&](const std::string& reason) {
        error = "Ship deck " + label + " [" + stage + "]: " + reason; return false;
    };
    const auto loadRules = [&](RuntimeWindowState& s) {
        if (!s.rules.loadFromIni(stock) || s.rules.getDetailRulesHash() != rulesHash) return false;
        s.rulesLoaded = true; return true;
    };
    const auto add = [](RuntimeWindowState& s, int id, const std::string& type, WorldObjectKind kind,
        const std::string& owner, RuntimeCellPoint cell, bool aircraft = false) {
        WorldObjectInstance object; object.instanceId = id; object.mapId = "ShipDeck" + std::to_string(id);
        object.kind = kind; object.type = type; object.owner = owner; object.x = cell.x; object.y = cell.y;
        object.cell = runtimeCellKey(cell.x, cell.y); object.alive = object.poweredOperational = true;
        object.authoredAircraft = aircraft; object.mission = "Sleep"; object.facing = object.turretFacing = 96;
        object.health = runtimeMaxHealthForObject(s, object);
        initializeRuntimeObjectAmmo(object, runtimeAmmoRulesForObject(s.rules, object));
        applyRuntimeInitialObjectFlightState(s.rules, object);
        runtimeInitializeModExtensions(object, s.rules, s.config.fixedTickHz);
        s.world.accessObjects().push_back(std::move(object));
    };
    const auto make = [&](int hz, const std::string& ship) {
        auto storage = std::make_unique<RuntimeWindowState>(); auto& s = *storage;
        s.config = assets->config; s.config.fixedTickHz = hz; s.config.gameMode = "Battle";
        s.config.playerCountry = "Americans"; s.config.noCooldown = false;
        s.config.multiplayerActive = s.config.shortGame = s.config.crates = s.config.superweapons = false;
        s.config.skirmishLoadSavePath.clear(); s.config.startingUnits = 0; s.campaignMode = false;
        s.report = assets->report; s.art = assets->art; s.theater = assets->theater; s.world = assets->world;
        if (!loadRules(s)) return std::unique_ptr<RuntimeWindowState>{};
        s.artLoaded = s.theaterLoaded = true;
        s.world.accessObjects().clear(); s.world.accessTerrainCells().clear();
        s.world.accessTerrainObjects().clear(); s.world.accessSmudges().clear();
        s.mapFacts = {}; s.mapFacts.theater = assets->mapFacts.theater; s.mapFacts.lighting = assets->mapFacts.lighting;
        s.mapFactsLoaded = false; s.report.map.size.clear(); s.report.map.localSize.clear();
        s.report.map.width = s.report.map.height = -1;
        for (int y = 60; y <= 106; ++y) for (int x = 60; x <= 106; ++x) {
            TerrainCell cell; cell.x = x; cell.y = y; cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = x >= 87 ? landTile : waterTile; cell.subTileIndex = 0;
            s.world.accessTerrainCells().push_back(cell);
        }
        add(s, 1, ship, WorldObjectKind::Unit, "Americans", {75,75});
        add(s, 2, ship == "CARRIER" ? "NAWEAP" : "SUB",
            ship == "CARRIER" ? WorldObjectKind::Structure : WorldObjectKind::Unit,
            "Russians", ship == "CARRIER" ? RuntimeCellPoint{90,75} : RuntimeCellPoint{82,75});
        if (ship == "DEST") findMutableObjectById(s, 2)->submerged = true;
        add(s, 10, "DEST", WorldObjectKind::Unit, "Russians", {72,75});
        add(s, 12, "AEGIS", WorldObjectKind::Unit, "Russians", {76,77});
        add(s, 13, "DRED", WorldObjectKind::Unit, "Russians", {64,75});
        // Two original Sleep DREDs form only the later controlled sink array;
        // both keep their native HP, two DMISL slots and original launch clocks.
        add(s, 14, "DRED", WorldObjectKind::Unit, "Russians", {64,77});
        add(s, 998, "NAPOWR", WorldObjectKind::Structure, "Russians", {100,96});
        s.runtimeSoundsEnabled = s.runtimePreferencePersistenceEnabled = false;
        s.runtimeSuppressPlayerAudioFeedback = true; s.visionEnabled = false;
        loadRuntimeSoundDatabase(s); ensureRuntimeLightingInitialized(s); ensureRuntimeParticlesInitialized(s);
        initializeRuntimeNavigation(s); initializeRuntimeEconomy(s); initializeRuntimeTriggers(s);
        s.economy.skirmishStartApplied = true; s.triggers.bootstrapComplete = true;
        ensureRuntimeSubcellsInitialized(s); ensureRuntimeSpecialMechanismsInitialized(s);
        initializeRuntimeOfflineGameplayRouting(s);
        { RuntimeSnapshotActiveStateScope active(s); advanceRuntimeSpecialMechanisms(s); completeRuntimeWorldRefresh(s); }
        return storage;
    };
    const auto step = [](RuntimeWindowState& s) {
        RuntimeSnapshotActiveStateScope active(s); ++s.tick;
        advanceRuntimeSimulation(s, false); completeRuntimeWorldRefresh(s);
    };
    const auto facade = [](RuntimeWindowState& s, const std::string& owner) {
        s.economy.owner = owner; s.config.playerCountry = owner; s.humanHouseAuthorities.clear();
        RuntimeHumanHouseAuthority authority; authority.owner = owner;
        authority.canonicalOwner = runtimeCanonicalHumanHouseOwner(owner); authority.country = owner; authority.localFacade = true;
        s.humanHouseAuthorities.push_back(std::move(authority));
    };
    const auto order = [&](RuntimeWindowState& s, int sourceId, int targetId, bool forced = false) {
        RuntimeSnapshotActiveStateScope active(s); const auto* source = findObjectById(s, sourceId);
        const auto* target = findObjectById(s, targetId); if (!source || !target) return false;
        facade(s, source->owner); s.selectedObjectIds = {sourceId};
        return dispatchRuntimeTargetOrderFromUi(s, forced ? RuntimeMultiplayerGameplayTargetOrderKind::Attack
            : RuntimeMultiplayerGameplayTargetOrderKind::Primary, runtimeCellPointForObject(*target), target)
            && s.lastCommand.acceptedCount == 1 && (!forced || (s.behaviors.contains(sourceId)
                && s.behaviors.at(sourceId).kind == RuntimeBehaviorKind::Attack
                && s.behaviors.at(sourceId).forceFire && s.behaviors.at(sourceId).targetObjectId == targetId));
    };
    const auto stop = [&](RuntimeWindowState& s, int id) {
        RuntimeSnapshotActiveStateScope active(s); auto* object = findMutableObjectById(s, id);
        if (!object) return false; facade(s, object->owner); s.selectedObjectIds = {id};
        if (!dispatchRuntimeSelectionCommandFromUi(s, RuntimeMultiplayerGameplayCommandType::Stop)
            || s.lastCommand.acceptedCount != 1) return false;
        // Stop itself sets Guard; Sleep makes this controlled firing source
        // inert after its real projectile has already left the barrel.
        object = findMutableObjectById(s, id); if (object) object->mission = "Sleep";
        return true;
    };
    const auto children = [](const RuntimeWindowState& s) {
        std::set<int> ids; const auto it = s.specialMechanisms.carrierParents().find(1);
        if (it != s.specialMechanisms.carrierParents().end())
            for (const auto& [slot,id] : it->second.childBySlot) if (id >= 0) ids.insert(id);
        return ids;
    };
    const auto describe = [](const RuntimeWindowState& s, int id) {
        std::ostringstream text; const auto* object = findRuntimeObjectByIdIncludingInactive(s, id);
        text << "tick=" << s.tick << " id=" << id << " accepted=" << s.lastCommand.acceptedCount;
        if (object) text << " hp=" << object->health << " alive=" << object->alive << " air=" << object->airborne
            << " dock=" << object->dockedToObjectId << " xyz=" << runtimeObjectRenderX(*object) << ','
            << runtimeObjectRenderY(*object) << ',' << runtimeObjectRenderLevel(*object) << " mission=" << object->mission
            << " logical=" << object->x << ',' << object->y << " cell=" << object->cell
            << " dockIndex=" << object->dockIndex << " renderMoveActive=" << object->renderMoveActive;
        const auto child = s.specialMechanisms.carrierChildren().find(id);
        if (child != s.specialMechanisms.carrierChildren().end()) text << " phase=" << static_cast<int>(child->second.phase)
            << " action=" << child->second.actionTick << " shots=" << child->second.sortieShotsRemaining
            << " childParent=" << child->second.parentObjectId << " slot=" << child->second.slot
            << " childTarget=" << child->second.targetObjectId;
        const auto behavior = s.behaviors.find(id);
        if (behavior != s.behaviors.end()) text << " behavior=" << static_cast<int>(behavior->second.kind)
            << '/' << static_cast<int>(behavior->second.state) << " target=" << behavior->second.targetObjectId;
        return text.str();
    };
    const auto row = [&](const RuntimeWindowState& s, int id, const RuntimeProjectileEffect* projectile = nullptr) {
        const auto* object = findRuntimeObjectByIdIncludingInactive(s,id); if (!object) return;
        const auto child = s.specialMechanisms.carrierChildren().find(id);
        const auto* rawWeapon=projectile ? s.rules.findWeaponType(projectile->weaponId) : nullptr;
        const auto* rawDebris=projectile && projectile->weaponId=="UnitDebris"
            && projectile->renderIntent=="unitDebris" ? s.art.findArtType(projectile->projectileId) : nullptr;
        const bool originalDmisl=projectile && projectile->renderIntent=="spawnedMissileImpact"
            && projectile->projectileImageId=="DMISL" && projectile->spawnedMissileDamage;
        events << label << ',' << stage << ',' << s.tick << ',' << id << ',' << object->type << ',' << object->health
            << ',' << object->alive << ',' << object->airborne << ','
            << (child == s.specialMechanisms.carrierChildren().end() ? -1 : static_cast<int>(child->second.phase))
            << ',' << runtimeObjectRenderX(*object) << ',' << runtimeObjectRenderY(*object) << ',' << runtimeObjectRenderLevel(*object)
            << ',' << runtimeCarrierAircraftHasDeckProtection(s,*object) << ',' << (projectile ? projectile->actorId : -1)
            << ',' << (projectile ? projectile->weaponId : "") << ',' << (projectile ? projectile->projectileId : "")
            << ',' << (projectile ? projectile->warheadId : "") << ',' << (projectile ? projectile->damage : 0)
            << ',' << (rawWeapon ? rawWeapon->damage : 0)
            << ',' << (originalDmisl ? "rules:General.DMislDamage" : rawWeapon ? "rules:"+rawWeapon->id+".Damage"
                : rawDebris ? "art:"+rawDebris->id+".Damage" : "none")
            << ',' << (originalDmisl ? 300 : rawWeapon ? rawWeapon->damage : rawDebris ? rawDebris->damage : 0) << '\n';
    };
    const auto parked = [&](const RuntimeWindowState& s, const std::set<int>& ids, int hp,
        const RuntimeCellPoint* mechanismStartParentCell = nullptr) {
        const auto* parent = findObjectById(s,1);
        // The special-mechanism stage copies the parent cell before the later
        // ordinary Move stage commits a new logical cell in the same tick.
        // Only that measured stage-start cell is legal during a real Move.
        const auto expectedCell=mechanismStartParentCell ? *mechanismStartParentCell
            : parent ? runtimeCellPointForObject(*parent) : RuntimeCellPoint{-1,-1};
        return parent && parent->alive && !ids.empty() && std::all_of(ids.begin(),ids.end(),[&](int id) {
            const auto* object = findObjectById(s,id); const auto child = s.specialMechanisms.carrierChildren().find(id);
            return object && object->alive && object->health == hp && !object->airborne && object->flightLevel == 0
                && object->dockedToObjectId == 1 && object->x == expectedCell.x && object->y == expectedCell.y
                && object->cell == runtimeCellKey(expectedCell.x,expectedCell.y)
                && object->currentAmmo == 1 && child != s.specialMechanisms.carrierChildren().end()
                && child->second.parentObjectId == 1 && object->dockIndex == child->second.slot
                && child->second.phase == CarrierChildPhase::Docked && child->second.targetObjectId == -1
                && runtimeCarrierAircraftHasDeckProtection(s,*object) && runtimeSpecialObjectHiddenForInteraction(s,*object);
        });
    };
    const auto projectiles = [](const RuntimeWindowState& s) {
        std::map<int,RuntimeProjectileEffect> result;
        for (const auto& p : s.recentExpiredProjectiles) result[p.actorId] = p;
        for (const auto& p : s.projectiles) result[p.actorId] = p;
        return result;
    };
    // Native ballistic splash must hurt the ship while every parked child is
    // protected. Waiting is bounded; no direct impact or HP setter is used.
    const auto shellHit = [&](RuntimeWindowState& s, const std::set<int>& ids, int hp, bool requireParked,
        int* actualShellDamage = nullptr) {
        const auto* parent = findObjectById(s,1); if (!parent) return fail("shell victim missing");
        const int before = parent->health; const bool expectCenter=!parent->renderMoveActive;
        std::map<int,int> childHealth;
        for (int id : ids) childHealth[id] = findObjectById(s,id)->health;
        std::set<int> seen; for (const auto& [id,p] : projectiles(s)) seen.insert(id);
        if (!order(s,10,1)) return fail("real DEST 155mm Primary rejected");
        bool discharged = false, sourceStopped = false, actualImpact=false, verifiedImpact=false; int actor = -1;
        int impactActor=-1, impactBeforeHp=0, expectedDamage=0;
        double impactDistance=-1.0; RuntimeProjectileTargetPosition impactPosition;
        WorldObjectInstance impactBody; std::string impactFailure;
        s.runtimeProjectileImpactObserver=[&](const RuntimeProjectileEffect& p,bool after) {
            if (p.attackerObjectId!=10 || p.targetObjectId!=1 || p.weaponId!="155mm") return;
            if (p.projectileId!="Ballistic" || p.warheadId!="ARTYHE" || !p.ag || p.aa
                || p.damage!=36 || runtimeCombatProjectileAuthoredDamage(s,p,*cannon)!=60
                || p.veteranDamageMultiplier!=1.0 || p.parentActorId>=0 || p.generation!=0
                || p.particleDamagePulse || p.spawnedMissileDamage || p.voxelDebris.enabled
                || std::abs(p.cellSpread-1.0)>1e-10 || std::abs(p.percentAtMax-.25)>1e-10) {
                impactFailure="real 155mm60/Ballistic/ARTYHE original impact authority changed"; return;
            }
            const auto* victim=findObjectById(s,1);
            if (!victim || !victim->alive) { impactFailure="native shell impact victim missing"; return; }
            row(s,1,&p);
            if (!after) {
                if (actualImpact) { impactFailure="controlled cannon produced multiple actual impacts"; return; }
                actualImpact=true; impactActor=p.actorId; impactBody=*victim; impactBeforeHp=victim->health;
                impactPosition=runtimeProjectileTargetPosition(s,*victim);
                impactDistance=runtimeCellDistanceToObjectFootprint(s,impactPosition.cell,*victim);
                expectedDamage=runtimeAuthoredDamageAgainst(s,*victim,60,"ARTYHE",impactDistance,1.0);
                // Original ARTYHE uses the actual impact grid cell and the
                // current committed footprint, even during render interpolation.
                const RuntimeCellPoint independentCell{static_cast<int>(std::lround(runtimeObjectRenderX(*victim))),
                    static_cast<int>(std::lround(runtimeObjectRenderY(*victim)))};
                const double independentDistance=std::hypot(double(independentCell.x-victim->x),double(independentCell.y-victim->y));
                const int independent=static_cast<int>(std::lround(60.0*.6
                    *(1.0-.75*std::clamp(independentDistance,0.0,1.0))));
                if (impactPosition.cell!=independentCell || std::abs(impactDistance-independentDistance)>1e-10
                    || expectedDamage!=independent || expectedDamage<=0
                    || (expectCenter && (impactDistance!=0.0 || expectedDamage!=36)))
                    impactFailure="native shell geometry did not match exact original heavy/falloff damage";
                for (int id:ids) row(s,id,&p);
                return;
            }
            if (!actualImpact || impactActor!=p.actorId || victim->health!=impactBeforeHp-expectedDamage) {
                impactFailure="native shell HP did not decrease by its exact original geometric damage"; return;
            }
            verifiedImpact=true;
            shellWitnesses<<label<<','<<stage<<','<<s.tick<<','<<p.actorId<<",60,"<<p.damage
                <<",ARTYHE,"<<impactPosition.x<<','<<impactPosition.y<<','<<impactPosition.level
                <<','<<impactPosition.cell.x<<','<<impactPosition.cell.y<<','<<impactBody.x<<','<<impactBody.y
                <<','<<runtimeObjectRenderX(impactBody)<<','<<runtimeObjectRenderY(impactBody)
                <<','<<impactDistance<<",1,0.25,"<<expectedDamage<<','<<impactBeforeHp<<','<<victim->health
                <<','<<(impactBeforeHp-victim->health)<<",PASS\n";
        };
        for (int n = 0; n < s.config.fixedTickHz * 10; ++n) {
            step(s);
            if (!impactFailure.empty()) { s.runtimeProjectileImpactObserver={}; return fail(impactFailure+": "+describe(s,1)); }
            for (const auto& [id,p] : projectiles(s)) if (!seen.contains(id) && p.attackerObjectId == 10) {
                if (p.weaponId != "155mm" || p.projectileId != "Ballistic" || p.warheadId != "ARTYHE"
                    || runtimeCombatProjectileAuthoredDamage(s,p,*cannon) != 60 || !p.ag || p.aa) {
                    s.runtimeProjectileImpactObserver={}; return fail("real 155mm/ARTYHE authority changed");
                }
                discharged = true; actor = id; row(s,1,&p); seen.insert(id);
            }
            if (discharged && !sourceStopped) { if (!stop(s,10)) { s.runtimeProjectileImpactObserver={}; return fail("cannon real Stop rejected"); } sourceStopped = true; }
            for (int id : ids) {
                const auto* child = findObjectById(s,id);
                if (!child || !child->alive || child->health != childHealth[id]) {
                    s.runtimeProjectileImpactObserver={}; return fail("native ship splash damaged deck child: " + describe(s,id));
                }
                if (requireParked && !parked(s,ids,hp)) { s.runtimeProjectileImpactObserver={}; return fail("moving ship detached or exposed parked child"); }
            }
            parent = findObjectById(s,1);
            if (!parent || !parent->alive) { s.runtimeProjectileImpactObserver={}; return fail("single 155mm shell destroyed original ship"); }
            const bool live = std::any_of(s.projectiles.begin(),s.projectiles.end(),[&](const auto& p){return p.actorId==actor;});
            if (discharged && !live) {
                s.runtimeProjectileImpactObserver={};
                if (!actualImpact || !verifiedImpact || impactActor!=actor || before - parent->health != expectedDamage)
                    return fail("real heavy ship did not take its exact native ARTYHE60 heavy/falloff damage at actual impact");
                if (actualShellDamage) *actualShellDamage=expectedDamage;
                return true;
            }
        }
        s.runtimeProjectileImpactObserver={};
        return fail("real cannon projectile did not impact in 10 seconds: " + describe(s,1));
    };
    const auto aaRejected = [&](RuntimeWindowState& s, int id) {
        const auto* child = findObjectById(s,id); if (!child) return fail("AA rejection victim missing");
        RuntimeSnapshotActiveStateScope active(s); facade(s,"Russians"); s.selectedObjectIds = {12};
        const bool accepted = dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Primary,
            runtimeCellPointForObject(*child),child) && s.lastCommand.acceptedCount > 0;
        if (accepted || runtimeWeaponCanTargetObject(s,*aa,*child)
            || !runtimeCarrierAircraftHasDeckProtection(s,*child) || !runtimeSpecialObjectHiddenForInteraction(s,*child))
            return fail("protected physical deck child admitted real AA targeting");
        return true;
    };
    const auto aaKill = [&](RuntimeWindowState& s, int id, bool expectLaunching, bool expectReturning) {
        const auto* victim = findObjectById(s,id);
        if (!victim || !victim->alive || !victim->airborne || runtimeCarrierAircraftHasDeckProtection(s,*victim)
            || !runtimeWeaponCanTargetObject(s,*aa,*victim) || runtimeSpecialObjectHiddenForInteraction(s,*victim))
            return fail("genuinely airborne child is not exposed to native AA: " + describe(s,id));
        // A controlled fresh AA source must have no earlier command, pending
        // burst or real projectile. Diagnose those instead of deleting them.
        if (s.behaviors.contains(12) || s.combatDetails.hasPendingBurst(12)
            || std::any_of(s.projectiles.begin(),s.projectiles.end(),[](const auto& p){return p.attackerObjectId==12;}))
            return fail("AA source has prior native command/burst/projectile: "+describe(s,12));
        std::set<int> previous; for (const auto& [actor,p] : projectiles(s)) previous.insert(actor);
        bool observed = false, fired = false, stopped = false; int impactPhase = -1;
        std::set<int> nativeAaActors;
        struct PlaneImpactBefore { WorldObjectInstance body; bool protectedBody = false; int phase = -1; };
        std::map<int,std::vector<PlaneImpactBefore>> impactPlanes;
        std::map<int,std::set<int>> deathWitnessByAaActor;
        bool protectedNeighbourDamaged = false;
        // Read both sides of this exact native primary impact. A death anchor
        // is accepted only if this actor actually changed its live body to HP0.
        s.runtimeProjectileImpactObserver = [&](const RuntimeProjectileEffect& p, bool after) {
            if (p.attackerObjectId != 12 || p.targetObjectId != id || p.weaponId != "Medusa"
                || p.projectileId != "MedusaProjectile" || p.warheadId != "SAMWH" || !p.aa || p.ag
                || p.parentActorId >= 0 || p.generation != 0 || p.damage != 100
                || p.particleDamagePulse || p.voxelDebris.enabled
                || runtimeCombatProjectileAuthoredDamage(s,p,*aa) != 100) return;
            if (!after) {
                observed = true;
                nativeAaActors.insert(p.actorId);
                const auto child=s.specialMechanisms.carrierChildren().find(id);
                impactPhase=child==s.specialMechanisms.carrierChildren().end() ? -1 : static_cast<int>(child->second.phase);
                auto& before=impactPlanes[p.actorId]; before.clear();
                for (const auto& object:s.world.getObjects()) {
                    if (!object.alive || object.health<=0 || object.kind!=WorldObjectKind::Unit
                        || (object.type!="HORNET" && object.type!="ASW")) continue;
                    const auto registered=s.specialMechanisms.carrierChildren().find(object.instanceId);
                    before.push_back({object,runtimeCarrierAircraftHasDeckProtection(s,object),
                        registered==s.specialMechanisms.carrierChildren().end() ? -1 : static_cast<int>(registered->second.phase)});
                }
                row(s,id,&p); return;
            }
            row(s,id,&p);
            for (const auto& before:impactPlanes[p.actorId]) {
                const auto* afterBody=findRuntimeObjectByIdIncludingInactive(s,before.body.instanceId);
                if (!afterBody) continue; // No unobserved disappearance is a HP0 witness.
                if (before.protectedBody && afterBody->health<before.body.health) protectedNeighbourDamaged=true;
                if (afterBody->alive || afterBody->health>0) continue;
                if (!deathWitnessByAaActor[p.actorId].insert(before.body.instanceId).second) continue;
                row(s,before.body.instanceId,&p);
                deathWitnesses<<label<<','<<stage<<','<<s.tick<<','<<p.actorId<<','<<before.body.instanceId
                    <<','<<before.body.type<<','<<before.body.health<<','<<afterBody->health<<','<<before.body.alive
                    <<','<<afterBody->alive<<','<<before.body.airborne<<','<<afterBody->airborne
                    <<','<<before.protectedBody<<','<<runtimeCarrierAircraftHasDeckProtection(s,*afterBody)
                    <<','<<before.phase<<','<<(before.body.instanceId==id)<<"\n";
                if (before.body.instanceId!=id) ++aaCollateralKills;
            }
        };
        // Stock AEGIS DistributedFire intentionally reselects ordinary Primary
        // among legal aircraft (RuntimeShell advanceRuntimeAttackBehavior).
        // Use the real UI object ForceAttack for this precise-victim oracle.
        if (!order(s,12,id,true)) { s.runtimeProjectileImpactObserver = {};
            return fail("real AEGIS AA ForceAttack rejected or exact target/force flag differed"); }
        for (int n = 0; n < s.config.fixedTickHz * 10; ++n) {
            step(s);
            if (protectedNeighbourDamaged) { s.runtimeProjectileImpactObserver={}; return fail("actual native AA damaged a protected neighbouring plane"); }
            for (const auto& [actor,p] : projectiles(s)) if (!previous.contains(actor) && p.attackerObjectId == 12) {
                const auto* currentVictim=findRuntimeObjectByIdIncludingInactive(s,p.sourceAnchorObjectId);
                const auto witness=deathWitnessByAaActor.find(p.parentActorId);
                const auto* debrisArt=s.art.findArtType(p.projectileId);
                const bool identifiedDeathDebris=currentVictim && !currentVictim->alive && currentVictim->health==0
                    && nativeAaActors.contains(p.parentActorId) && p.generation==1
                    && witness!=deathWitnessByAaActor.end() && witness->second.contains(p.sourceAnchorObjectId)
                    && p.targetObjectId==-1
                    && p.weaponId=="UnitDebris" && p.weaponSlot=="debris" && p.renderIntent=="unitDebris"
                    && !p.aa && p.ag && !p.particleDamagePulse && !p.voxelDebris.enabled
                    && debrisArt && p.damage==std::max(1,debrisArt->damage);
                if (identifiedDeathDebris) { previous.insert(actor); row(s,p.sourceAnchorObjectId,&p); continue; }
                if (p.weaponId != "Medusa" || p.projectileId != "MedusaProjectile" || p.warheadId != "SAMWH"
                    || !p.aa || p.ag || p.parentActorId>=0 || p.generation!=0 || p.targetObjectId!=id
                    || p.particleDamagePulse || p.voxelDebris.enabled || p.damage!=100
                    || runtimeCombatProjectileAuthoredDamage(s,p,*aa) != 100) {
                    std::ostringstream detail;
                    const auto child=s.specialMechanisms.carrierChildren().find(id);
                    const auto legacy=s.combatDetails.legacyProjectileRounds.find(actor);
                    const auto* actualWeapon=s.rules.findWeaponType(p.weaponId);
                    detail << "native Medusa100/SAMWH AA actor changed weapon=" << p.weaponId
                        << " projectile=" << p.projectileId << " warhead=" << p.warheadId
                        << " aa=" << p.aa << " ag=" << p.ag << " payloadDamage=" << p.damage
                        << " computedAuthoredDamage=" << (actualWeapon ? runtimeCombatProjectileAuthoredDamage(s,p,*actualWeapon) : 0)
                        << " rawRuleDamage=" << (actualWeapon ? actualWeapon->damage : 0)
                        << " artDamage=" << (debrisArt ? debrisArt->damage : 0)
                        << " actor=" << actor << " target=" << p.targetObjectId << " sourceAnchor=" << p.sourceAnchorObjectId
                        << " victim=" << id << " parentActor=" << p.parentActorId << " generation=" << p.generation
                        << " particlePulse=" << p.particleDamagePulse << " voxelDebris=" << p.voxelDebris.enabled
                        << " intent=" << p.renderIntent << " spawn=" << p.spawnTick << " impact=" << p.impactTick
                        << " remaining=" << p.remainingTicks << " legacyRounds="
                        << (legacy==s.combatDetails.legacyProjectileRounds.end() ? -1 : legacy->second)
                        << " phase=" << (child==s.specialMechanisms.carrierChildren().end() ? -1 : static_cast<int>(child->second.phase))
                        << " victimState=" << describe(s,id) << " serialized=" << serializeRuntimeProjectileEffect(p);
                    row(s,id,&p);
                    s.runtimeProjectileImpactObserver = {}; return fail(detail.str());
                }
                fired = true; nativeAaActors.insert(actor); previous.insert(actor); row(s,id,&p);
            }
            if (fired && !stopped) { if (!stop(s,12)) { s.runtimeProjectileImpactObserver = {}; return fail("AA Stop rejected"); } stopped = true; }
            victim = findRuntimeObjectByIdIncludingInactive(s,id);
            if (!victim || !victim->alive) {
                s.runtimeProjectileImpactObserver = {};
                const bool witnessedTarget=std::any_of(deathWitnessByAaActor.begin(),deathWitnessByAaActor.end(),
                    [&](const auto& witness){return witness.second.contains(id);});
                if (!fired || !observed || !witnessedTarget || (expectLaunching && impactPhase != static_cast<int>(CarrierChildPhase::Launching))
                    || (expectReturning && impactPhase != static_cast<int>(CarrierChildPhase::Returning)))
                    return fail("aircraft death lacked real AA impact in the required physical phase");
                ++aaKills; return true;
            }
        }
        s.runtimeProjectileImpactObserver = {};
        return fail("native AA failed to kill unprotected original HP aircraft in 10 seconds: " + describe(s,id));
    };
    struct SavedPair { IniFile ordinary, exact; };
    const auto restore = [&](RuntimeWindowState& source, const SavedPair& saved, bool exact) {
        auto q = std::make_unique<RuntimeWindowState>(); seedRuntimeLoadScratch(*q,source,false);
        if (!loadRules(*q) || !materializeRuntimeLoadStateFromIni(*q,saved.ordinary,error)
            || (exact && !materializeRuntimeMultiplayerSnapshotExtension(*q,saved.exact,error))
            || q->rules.getDetailRulesHash()!=rulesHash)
            return std::unique_ptr<RuntimeWindowState>{};
        initializeRuntimeOfflineGameplayRouting(*q); return q;
    };
    const auto equal = [&](const RuntimeWindowState& a, const RuntimeWindowState& b, const std::set<int>& ids, bool exact) {
        if (a.tick != b.tick || a.specialMechanisms.stateHash() != b.specialMechanisms.stateHash()
            || a.projectiles.size() != b.projectiles.size()) return false;
        for (std::size_t n=0;n<a.projectiles.size();++n)
            if (serializeRuntimeProjectileEffect(a.projectiles[n]) != serializeRuntimeProjectileEffect(b.projectiles[n])) return false;
        for (int id : ids) {
            const auto* x=findRuntimeObjectByIdIncludingInactive(a,id); const auto* y=findRuntimeObjectByIdIncludingInactive(b,id);
            if ((!x)!=(!y)) return false; if (!x) continue;
            if (x->alive!=y->alive || x->health!=y->health || x->x!=y->x || x->y!=y->y || x->level!=y->level
                || x->airborne!=y->airborne || x->dockedToObjectId!=y->dockedToObjectId || x->dockIndex!=y->dockIndex
                || x->currentAmmo!=y->currentAmmo || x->flightLevel!=y->flightLevel || x->facing!=y->facing
                || x->weaponCooldownTicksRemaining!=y->weaponCooldownTicksRemaining
                || runtimeSerializeFacingClock(x->bodyFacingClock)!=runtimeSerializeFacingClock(y->bodyFacingClock)
                || std::abs(runtimeObjectRenderX(*x)-runtimeObjectRenderX(*y))>1e-8
                || std::abs(runtimeObjectRenderY(*x)-runtimeObjectRenderY(*y))>1e-8
                || std::abs(runtimeObjectRenderLevel(*x)-runtimeObjectRenderLevel(*y))>1e-8
                || runtimeCarrierAircraftHasDeckProtection(a,*x)!=runtimeCarrierAircraftHasDeckProtection(b,*y)) return false;
            const auto ax=a.behaviors.find(id), bx=b.behaviors.find(id);
            if ((ax==a.behaviors.end())!=(bx==b.behaviors.end())) return false;
            if (ax!=a.behaviors.end() && (ax->second.kind!=bx->second.kind || ax->second.targetObjectId!=bx->second.targetObjectId
                || ax->second.forceFire!=bx->second.forceFire || ax->second.attackGround!=bx->second.attackGround
                || ((exact || a.specialMechanisms.carrierChildren().contains(id)) && ax->second.state!=bx->second.state)
                || (a.specialMechanisms.carrierChildren().contains(id)
                    && (ax->second.moveCooldownTicks!=bx->second.moveCooldownTicks
                        || ax->second.weaponCooldownTicks!=bx->second.weaponCooldownTicks)))) return false;
        }
        return true;
    };
    const auto checkpoint = [&](RuntimeWindowState& source,const std::set<int>& childIds,SavedPair& saved, bool liveContinuation = false) {
        facade(source,"Americans"); std::ostringstream ordinary, extension, canonical;
        if (!writeRuntimeStateToStream(source,ordinary,error) || !writeRuntimeStateToStream(source,extension,error)
            || !writeRuntimeMultiplayerSnapshotExtension(source,extension,error)
            || !saved.ordinary.loadFromString(ordinary.str()) || !saved.exact.loadFromString(extension.str())
            || !writeRuntimeMultiplayerSnapshotCanonicalIni(saved.exact,canonical,error)
            || !saved.exact.loadFromString(canonical.str())) return fail("real checkpoint writer: "+error);
        for (int format=0;format<2;++format) {
            const auto name=label+'-'+stage+(format?"-canonical-extension.ini":"-ordinary14.ini");
            std::ofstream file(evidence/name,std::ios::binary); file<<(format?canonical.str():ordinary.str());
            if (!file.good()) return fail("full checkpoint file write failed");
            checkpoints<<label<<','<<stage<<','<<(format?"canonical-extension":"ordinary14")<<','<<source.tick
                <<','<<source.specialMechanisms.stateHash()<<','<<name<<",PASS\n";
        }
        auto ordinaryState=restore(source,saved,false), exactState=restore(source,saved,true), reference=restore(source,saved,true);
        if (!ordinaryState || !exactState || !reference) return fail("full checkpoint restore: "+error);
        auto ids=childIds; ids.insert(1); ids.insert(2);
        if (!equal(source,*ordinaryState,ids,false) || !equal(source,*exactState,ids,true))
            return fail("checkpoint changed HP/real flight/slot/protection/order authority");
        ++saves; ++snapshots;
        for (int n=0;n<12;++n) {
            step(*ordinaryState); step(*exactState); step(*reference); ++pairedTicks;
            if (liveContinuation) {
                step(source); ++livePairedTicks;
                if (!equal(source,*ordinaryState,ids,false) || !equal(source,*exactState,ids,true))
                    return fail("uninterrupted live deck trajectory differs after ordinary/canonical restore");
            }
            if (!equal(*reference,*ordinaryState,ids,false) || !equal(*reference,*exactState,ids,true))
                return fail("saved native trajectory/projectile/damage/protection diverged: "+describe(*ordinaryState,*childIds.begin()));
        }
        return true;
    };
    const auto checkStageDamage = [&](RuntimeWindowState& source,const SavedPair& saved,int id,bool protectedState,
        bool launching=false,bool returning=false) {
        for (int format=0;format<2;++format) {
            auto q=restore(source,saved,format!=0); if (!q) return fail("damage branch restore: "+error);
            if (protectedState) {
                if (!aaRejected(*q,id) || !shellHit(*q,children(*q),findObjectById(*q,id)->health,false)) return false;
            } else if (!aaKill(*q,id,launching,returning)) return false;
        }
        return true;
    };
    const auto sink = [&](RuntimeWindowState& s,const std::set<int>& ids,bool shipMovedBeforeSortie,
        int& lethalAirborneChildren,int& lethalParkedChildren) {
        const auto* shipAtStart=findObjectById(s,1);
        const bool aswShip=shipAtStart && shipAtStart->type=="DEST";
        const std::set<int> firingSources{13,14};
        for (int sourceId:firingSources) {
            const auto* body=findObjectById(s,sourceId);
            const auto registered=s.specialMechanisms.carrierParents().find(sourceId);
            if (!body || body->type!="DRED" || body->health!=800 || !body->alive
                || registered==s.specialMechanisms.carrierParents().end()
                || registered->second.childType!="DMISL" || registered->second.capacity!=2
                || registered->second.regenRate!=80 || registered->second.reloadRate!=0)
                return fail("original two-DRED sink array registration/HP/slots/clocks changed: "+describe(s,sourceId));
            if (!order(s,sourceId,1) || !s.behaviors.contains(sourceId)
                || s.behaviors.at(sourceId).kind!=RuntimeBehaviorKind::Attack
                || s.behaviors.at(sourceId).targetObjectId!=1 || s.behaviors.at(sourceId).forceFire)
                return fail("native DRED Primary ship attack rejected or changed: "+describe(s,sourceId));
        }
        std::map<int,SpawnedMissileState> released;
        std::set<int> releasedSources, impactedSources, usedMissiles;
        struct SinkImpactWitness { SpawnedMissileState missile; int beforeHp=0, airborne=0, parked=0; bool aliveBefore=false;
            std::map<int,int> parkedHealth; };
        std::map<int,SinkImpactWitness> nativeImpacts;
        bool deathPhaseWitness=false; std::string sinkFailure;
        s.runtimeProjectileImpactObserver = [&](const RuntimeProjectileEffect& p,bool after) {
            if (!firingSources.contains(p.attackerObjectId) || p.renderIntent!="spawnedMissileImpact") return;
            if (p.targetObjectId!=1 || p.weaponId!="DredLauncher" || p.projectileImageId!="DMISL"
                || p.warheadId!="DMISLWH" || !p.spawnedMissileDamage || p.spawnedMissileBaseDamage!=300
                || p.damage!=300 || p.veteranDamageMultiplier!=1.0 || p.aa || !p.ag
                || p.parentActorId>=0 || p.generation!=0 || p.particleDamagePulse || p.voxelDebris.enabled
                || !p.launchSourceValidated || p.launchSourceType!="DRED"
                || p.launchSourceKind!=static_cast<int>(WorldObjectKind::Unit)) {
                sinkFailure="native original DMISL300/DMISLWH impact authority changed"; return;
            }
            const auto* parent=findRuntimeObjectByIdIncludingInactive(s,1);
            if (!parent) { sinkFailure="native ship impact victim disappeared"; return; }
            if (!after) {
                auto actualMissile=released.end();
                for (auto it=released.begin();it!=released.end();++it) {
                    const auto& m=it->second;
                    if (!usedMissiles.contains(it->first) && m.parentObjectId==p.attackerObjectId
                        && m.releaseTick==p.spawnTick && std::abs(m.originX-p.sourceX)<1e-10
                        && std::abs(m.originY-p.sourceY)<1e-10 && std::abs(m.originZ-p.sourceZ)<1e-10) {
                        actualMissile=it; break;
                    }
                }
                if (actualMissile==released.end()) { sinkFailure="DMISL impact has no actual released missile identity"; return; }
                const int expectedDamage=runtimeAuthoredDamageAgainst(s,*parent,300,p.warheadId,0.0,1.0);
                if (expectedDamage!=240) { sinkFailure="original DMISL300 * heavy80% did not equal 240"; return; }
                auto& witness=nativeImpacts[p.actorId]; witness.missile=actualMissile->second; witness.beforeHp=parent->health;
                witness.aliveBefore=parent->alive;
                usedMissiles.insert(actualMissile->first); row(s,1,&p);
                if (witness.aliveBefore && witness.beforeHp>0) for (int id:ids) { const auto* child=findObjectById(s,id);
                    if (!child || !child->alive) { sinkFailure="original child missing before live mother impact"; return; }
                    row(s,id,&p);
                    if (child->airborne) {
                        if (aswShip && runtimeCarrierAircraftHasDeckProtection(s,*child)) {
                            sinkFailure="actual airborne ASW acquired deck protection"; return;
                        }
                        ++witness.airborne;
                    } else {
                        const int stockHp=aswShip?30:75;
                        if (!parked(s,{id},stockHp)) {
                            sinkFailure="non-airborne sink child is not full-HP protected Docked cargo: "+describe(s,id); return;
                        }
                        ++witness.parked; witness.parkedHealth.emplace(id,stockHp);
                    }
                }
                return;
            }
            const auto before=nativeImpacts.find(p.actorId);
            if (before==nativeImpacts.end()) { sinkFailure="DMISL impact lacks actual before-HP witness"; return; }
            const auto& witness=before->second; const auto& m=witness.missile;
            row(s,1,&p);
            if (!witness.aliveBefore || witness.beforeHp<=0) {
                if (witness.aliveBefore || witness.beforeHp!=0 || parent->alive || parent->health!=0) {
                    sinkFailure="dead-target DMISL changed a nonzero or living ship body"; return;
                }
                sinkWitnesses<<label<<",dead-target-impact,"<<s.tick<<','<<p.attackerObjectId<<','<<m.objectId<<','<<p.actorId
                    <<','<<m.startTick<<','<<m.releaseTick<<",1,300,"<<p.damage<<",0,0,0,"<<witness.airborne
                    <<','<<witness.parked<<','<<m.originX<<','<<m.originY<<','<<m.originZ<<",PASS\n";
                return; // A cached-endpoint residual is not a live hit or a death-phase witness.
            }
            if (parent->health!=std::max(0,witness.beforeHp-240)) {
                sinkFailure="native DMISL did not apply exact original heavy damage240 at its actual impact"; return;
            }
            if (witness.beforeHp>240) {
                if (!parent->alive) { sinkFailure="nonlethal original DMISL destroyed the mother"; return; }
                for (const auto& [id,stockHp]:witness.parkedHealth) {
                    row(s,id,&p);
                    if (!parked(s,{id},stockHp)) {
                        sinkFailure="nonlethal DMISL damaged or detached protected Docked child: "+describe(s,id); return;
                    }
                }
            }
            if (parent->health==0 && !parent->alive) {
                lethalAirborneChildren=witness.airborne; lethalParkedChildren=witness.parked;
                const bool allParked=witness.parked==static_cast<int>(ids.size());
                deathPhaseWitness=deathPhaseWitness || (shipMovedBeforeSortie
                    ? witness.airborne>0 || (aswShip && allParked) : allParked);
            }
            impactedSources.insert(p.attackerObjectId);
            sinkWitnesses<<label<<",impact,"<<s.tick<<','<<p.attackerObjectId<<','<<m.objectId<<','<<p.actorId
                <<','<<m.startTick<<','<<m.releaseTick<<",1,300,"<<p.damage<<",240,"<<witness.beforeHp
                <<','<<parent->health<<','<<witness.airborne<<','<<witness.parked
                <<','<<m.originX<<','<<m.originY<<','<<m.originZ<<",PASS\n";
        };
        for (int n=0;n<s.config.fixedTickHz*45;++n) {
            step(s);
            for (const auto& [id,missile] : s.specialMechanisms.spawnedMissiles()) {
                if (!firingSources.contains(missile.parentObjectId) || !missile.released || released.contains(id)) continue;
                if (missile.objectId!=id || missile.typeId!="DMISL" || missile.parentType!="DRED"
                    || missile.weaponId!="DredLauncher" || missile.warheadId!="DMISLWH"
                    || missile.damage!=300 || missile.damageMultiplier!=1.0 || missile.targetObjectId!=1) {
                    s.runtimeProjectileImpactObserver={}; return fail("original released sink missile authority changed");
                }
                released.emplace(id,missile); releasedSources.insert(missile.parentObjectId);
                sinkWitnesses<<label<<",release,"<<s.tick<<','<<missile.parentObjectId<<','<<id<<",-1,"
                    <<missile.startTick<<','<<missile.releaseTick<<",1,300,300,240,-1,-1,-1,-1,"
                    <<missile.originX<<','<<missile.originY<<','<<missile.originZ<<",PASS\n";
            }
            if (!sinkFailure.empty()) { s.runtimeProjectileImpactObserver={}; return fail(sinkFailure); }
            const auto* parent=findRuntimeObjectByIdIncludingInactive(s,1);
            if (parent && parent->alive) continue;
            // Cleanup can run at the next native mechanism stage after impact.
            step(s);
            if (!sinkFailure.empty()) { s.runtimeProjectileImpactObserver={}; return fail(sinkFailure); }
            for (int id : ids) {
                const auto* child=findRuntimeObjectByIdIncludingInactive(s,id);
                if ((child && child->alive) || s.specialMechanisms.carrierChildren().contains(id)) {
                    s.runtimeProjectileImpactObserver={}; return fail("sunk mother left a living/bound child: "+describe(s,id));
                }
            }
            s.runtimeProjectileImpactObserver={};
            if (releasedSources!=firingSources || impactedSources!=firingSources || !deathPhaseWitness)
                return fail("ship death lacked each DRED's actual DMISL300 release/impact or required airborne/parked child witness");
            if (shipMovedBeforeSortie && aswShip && lethalAirborneChildren>0) ++aswAirborneShipSinks;
            ++sunk; return true;
        }
        s.runtimeProjectileImpactObserver={};
        std::ostringstream detail; detail<<"two original DREDs failed to sink ship in 45 seconds: "<<describe(s,1);
        for (int sourceId:firingSources) {
            detail<<" | source{"<<describe(s,sourceId);
            const auto behavior=s.behaviors.find(sourceId);
            if (behavior!=s.behaviors.end()) detail<<" weaponCooldown="<<behavior->second.weaponCooldownTicks
                <<" path="<<behavior->second.path.size();
            const auto parent=s.specialMechanisms.carrierParents().find(sourceId);
            if (parent!=s.specialMechanisms.carrierParents().end())
                for (const auto& [slot,id]:parent->second.childBySlot) {
                    const auto ready=parent->second.slotReadyTick.find(slot);
                    detail<<" slot="<<slot<<" child="<<id<<" ready="
                        <<(ready==parent->second.slotReadyTick.end() ? -1 : ready->second);
                }
            for (const auto& [id,m]:s.specialMechanisms.spawnedMissiles()) if (m.parentObjectId==sourceId)
                detail<<" missile="<<id<<" target="<<m.targetObjectId<<" start="<<m.startTick<<" release="<<m.releaseTick
                    <<" released="<<m.released<<" stage="<<m.flightStage<<" xyz="<<m.x<<','<<m.y<<','<<m.z;
            detail<<'}';
        }
        return fail(detail.str());
    };
    const auto run = [&](const std::string& ship,int hz,bool moving) {
        label=ship+'-'+std::to_string(hz)+(moving?"-moving":"-static");
        if (selected && *selected && label!=selected) return true;
        const int saveStart=saves, snapshotStart=snapshots, pairedStart=pairedTicks, killStart=aaKills, collateralStart=aaCollateralKills, sunkStart=sunk;
        stage="authored deck"; auto storage=make(hz,ship); if (!storage) return fail("fixture rules load"); auto& s=*storage;
        const auto ids=children(s); const int hp=ship=="CARRIER"?75:30, capacity=ship=="CARRIER"?3:1;
        const auto childType=ship=="CARRIER"?"HORNET":"ASW";
        if (static_cast<int>(ids.size())!=capacity || !parked(s,ids,hp)) return fail("original loaded deck binding/HP/Ammo/protection");
        const auto& parentRules=s.specialMechanisms.carrierParents().at(1);
        if (parentRules.regenRate!=(ship=="CARRIER"?600:400) || parentRules.reloadRate!=150)
            return fail("original carrier regeneration/reload clocks changed");
        for (int id:ids) if (!aaRejected(s,id)) return false;
        if (moving) {
            RuntimeSnapshotActiveStateScope active(s); facade(s,"Americans"); s.selectedObjectIds={1};
            if (!dispatchRuntimeTargetOrderFromUi(s,RuntimeMultiplayerGameplayTargetOrderKind::Move,{76,75})
                || s.lastCommand.acceptedCount!=1) return fail("mother real Move rejected");
            bool activeMove=false;
            for (int n=0;n<hz*5;++n) {
                const auto* beforeParent=findObjectById(s,1);
                if (!beforeParent) return fail("native moving parent disappeared");
                const RuntimeCellPoint mechanismStartCell=runtimeCellPointForObject(*beforeParent);
                step(s); const auto* parent=findObjectById(s,1);
                if (!parked(s,ids,hp,&mechanismStartCell)) {
                    std::ostringstream detail; detail<<"native movement detached protected deck children: "<<describe(s,1);
                    row(s,1);
                    for (int id:ids) { detail<<" | child{"<<describe(s,id)<<'}'; row(s,id); }
                    return fail(detail.str());
                }
                if (parent && parent->renderMoveActive) { activeMove=true; break; }
            }
            if (!activeMove) return fail("mother did not begin a real move");
        }
        int nativeShellDamage=0;
        stage="actual ARTYHE ship splash"; if (!shellHit(s,ids,hp,true,&nativeShellDamage)) return false;
        for (int n=0;n<hz*5 && s.behaviors.contains(1);++n) step(s);
        if (!parked(s,ids,hp) || (moving && (findObjectById(s,1)->x!=76 || findObjectById(s,1)->y!=75)))
            return fail("moving ship did not finish carrying protected real children");
        stage="deck-save"; SavedPair deckSaved;
        if (!checkpoint(s,ids,deckSaved,true) || !checkStageDamage(s,deckSaved,*ids.begin(),true)) return false;
        // Unbound stock aircraft are a positive control: aircraft type alone
        // cannot confer protection. They are authored native objects, not
        // synthetic projectile victims or edited damage counters.
        stage="free-aircraft-control";
        for (const std::string type : {std::string("HORNET"),std::string("ASW")}) {
            auto q=restore(s,deckSaved,true); if (!q) return fail("free control restore");
            add(*q,9000,type,WorldObjectKind::Unit,"Americans",{77,78},true);
            initializeRuntimeNavigation(*q); completeRuntimeWorldRefresh(*q);
            const auto* free=findObjectById(*q,9000);
            if (!free || free->health!=(type=="HORNET"?75:30) || q->specialMechanisms.carrierChildren().contains(9000)
                || runtimeCarrierAircraftHasDeckProtection(*q,*free) || !aaKill(*q,9000,false,false)) return false;
        }
        stage="native launch-mouth";
        if (!order(s,1,2)) return fail("mother true Primary attack rejected");
        { RuntimeSnapshotActiveStateScope active(s); ++s.tick; advanceRuntimeSpecialMechanisms(s);
            advanceRuntimeParticles(s); completeRuntimeWorldRefresh(s); }
        int first=*ids.begin(), highest=first;
        for (int id:ids) {
            const auto& child=s.specialMechanisms.carrierChildren().at(id);
            if (child.slot>s.specialMechanisms.carrierChildren().at(highest).slot) highest=id;
            const auto* body=findObjectById(s,id);
            if (!body || !body->airborne || child.phase!=CarrierChildPhase::Launching)
                return fail("native Launch action did not create original transition");
            row(s,id);
        }
        if (ship=="CARRIER") {
            const auto mouth=runtimeCarrierDeckMouth(*findObjectById(s,1));
            for (int id:ids) {
                const auto* body=findObjectById(s,id);
                if (std::hypot(runtimeObjectRenderX(*body)-mouth[0],runtimeObjectRenderY(*body)-mouth[1])>.20
                    || std::abs(runtimeObjectRenderLevel(*body)-mouth[2])>1e-10
                    || !runtimeCarrierAircraftHasDeckProtection(s,*body) || !aaRejected(s,id))
                    return fail("first airborne boolean exposed equal-height physical mouth child");
            }
            stage="mouth-save"; SavedPair mouthSaved;
            if (!checkpoint(s,ids,mouthSaved) || !checkStageDamage(s,mouthSaved,first,true)) return false;
            step(s); stage="low-launch-save";
            const auto* body=findObjectById(s,first); const auto newMouth=runtimeCarrierDeckMouth(*findObjectById(s,1));
            if (!body || runtimeObjectRenderLevel(*body)<=newMouth[2] || runtimeObjectRenderLevel(*body)>=body->flightLevel
                || runtimeCarrierAircraftHasDeckProtection(s,*body)) return fail("first physical ascent remained invulnerable");
            SavedPair lowSaved;
            if (!checkpoint(s,ids,lowSaved) || !checkStageDamage(s,lowSaved,first,false,true,false)) return false;
        } else {
            const auto* body=findObjectById(s,first);
            if (!body || runtimeObjectRenderLevel(*body)!=body->flightLevel || runtimeCarrierAircraftHasDeckProtection(s,*body))
                return fail("ASW immediate real cruise retained deck immunity");
            stage="asw-launch-save"; SavedPair immediate;
            if (!checkpoint(s,ids,immediate) || !checkStageDamage(s,immediate,first,false)) return false;
        }
        std::map<int,std::set<int>> shots; std::set<int> bombActors; bool outboundSaved=false, queueSaved=ship!="CARRIER";
        bool returningSaved=false, parentStopped=false, returned=false;
        std::set<int> observedRedock;
        const int targetBefore=findObjectById(s,2)->health;
        SavedPair outbound, returning;
        const auto bomb=ship=="CARRIER"?"HornetBomb":"ASWBomb";
        for (int n=0;n<hz*45;++n) {
            step(s);
            for (int id:ids) {
                const auto& child=s.specialMechanisms.carrierChildren().at(id);
                if (child.phase==CarrierChildPhase::Docked && observedRedock.insert(id).second) {
                    const auto& parent=s.specialMechanisms.carrierParents().at(1);
                    if (child.actionTick!=s.tick || parent.slotReadyTick.at(child.slot)!=s.tick+150)
                        return fail("actual redock did not set original reload150 tick deadline");
                }
            }
            for (const auto& [actor,p] : projectiles(s)) if (ids.contains(p.attackerObjectId) && p.weaponId==bomb) {
                if (p.damage!=(ship=="CARRIER"?40:50) || p.projectileId!=(ship=="CARRIER"?"NormalBomb":"DepthCharge")
                    || p.warheadId!=(ship=="CARRIER"?"ORCAAP":"APSplash")) return fail("native aircraft bomb authority changed");
                if (bombActors.insert(actor).second) { shots[p.attackerObjectId].insert(actor); row(s,p.attackerObjectId,&p); }
                if (shots[p.attackerObjectId].size()>3) return fail("original Ammo1/three-shot sortie exceeded three actual actors");
            }
            if (!queueSaved) {
                const auto* body=findObjectById(s,highest); const auto& child=s.specialMechanisms.carrierChildren().at(highest);
                if (body && child.phase==CarrierChildPhase::Launching
                    && std::abs(runtimeObjectRenderLevel(*body)-body->flightLevel)<1e-8) {
                    stage="cruise-mouth-queue-save";
                    if (runtimeCarrierAircraftHasDeckProtection(s,*body)) return fail("raised cruise queue still protected by deck");
                    SavedPair queue;
                    if (!checkpoint(s,ids,queue) || !checkStageDamage(s,queue,highest,false)) return false;
                    queueSaved=true;
                }
            }
            if (!outboundSaved && std::all_of(ids.begin(),ids.end(),[&](int id) {
                const auto* body=findObjectById(s,id); const auto& child=s.specialMechanisms.carrierChildren().at(id);
                return body && child.phase==CarrierChildPhase::Attacking
                    && std::hypot(runtimeObjectRenderX(*body)-runtimeObjectRenderX(*findObjectById(s,1)),
                        runtimeObjectRenderY(*body)-runtimeObjectRenderY(*findObjectById(s,1)))>2.0;
            })) {
                stage="attacking-save";
                if (!checkpoint(s,ids,outbound) || !checkStageDamage(s,outbound,first,false)) return false;
                outboundSaved=true;
            }
            if (!returningSaved) for (int id:ids) {
                const auto* body=findObjectById(s,id); const auto& child=s.specialMechanisms.carrierChildren().at(id);
                if (body && child.phase==CarrierChildPhase::Returning && body->airborne
                    && std::hypot(runtimeObjectRenderX(*body)-runtimeObjectRenderX(*findObjectById(s,1)),
                        runtimeObjectRenderY(*body)-runtimeObjectRenderY(*findObjectById(s,1)))>(ship=="DEST"?2.0:3.0)) {
                    stage="returning-save";
                    if (!checkpoint(s,ids,returning) || !checkStageDamage(s,returning,id,false,false,true)) return false;
                    returningSaved=true; break;
                }
            }
            if (!parentStopped && bombActors.size()==static_cast<std::size_t>(capacity*3)) {
                if (!stop(s,1)) return fail("real mother Stop after complete sortie rejected"); parentStopped=true;
            }
            if (parentStopped && parked(s,ids,hp)
                && std::none_of(s.projectiles.begin(),s.projectiles.end(),[&](const auto& p){return ids.contains(p.attackerObjectId)&&p.weaponId==bomb;})) {
                returned=true; break;
            }
        }
        if (!outboundSaved || !returningSaved || !queueSaved || !returned || shots.size()!=ids.size()
            || !std::all_of(ids.begin(),ids.end(),[&](int id){return shots[id].size()==3;})
            || targetBefore-findObjectById(s,2)->health!=(ship=="CARRIER"?360:150))
            return fail("real three-shot sortie, target damage, exposed transitions or physical redock missing: "+describe(s,first));
        stage="redocked-save"; SavedPair redocked;
        if (!checkpoint(s,ids,redocked) || !checkStageDamage(s,redocked,first,true)) return false;
        stage="original reload150";
        const auto beforeReload=s.specialMechanisms.carrierParents().at(1).slotReadyTick;
        if (!order(s,1,2)) return fail("real reload attack rejected");
        const int nextReady=std::min_element(beforeReload.begin(),beforeReload.end(),[](const auto& a,const auto& b){return a.second<b.second;})->second;
        while (s.tick<nextReady-1) {
            step(s);
            for (const auto& [slot,deadline]:beforeReload) if (s.tick<deadline) {
                const int id=s.specialMechanisms.carrierParents().at(1).childBySlot.at(slot);
                if (s.specialMechanisms.carrierChildren().at(id).phase!=CarrierChildPhase::Docked)
                    return fail("child relaunched before original reload deadline");
            }
        }
        step(s);
        bool relaunched=false;
        for (const auto& [slot,deadline]:beforeReload) if (deadline==nextReady) {
            const int id=s.specialMechanisms.carrierParents().at(1).childBySlot.at(slot);
            if (s.specialMechanisms.carrierChildren().at(id).phase==CarrierChildPhase::Docked)
                return fail("child did not relaunch at original reload150 deadline");
            relaunched=true;
        }
        if (!relaunched || !stop(s,1)) return fail("native reload evidence missing");
        stage="native AA death and original regeneration";
        auto killed=restore(s,outbound,true); if (!killed) return fail("kill branch restore");
        if (!stop(*killed,1) || !aaKill(*killed,first,false,true)) return false;
                int deadSlot=-1;
        for (const auto& [slot,id]:s.specialMechanisms.carrierParents().at(1).childBySlot) if (id==first) deadSlot=slot;
        if (deadSlot<0) return fail("original killed slot not found");
        const auto parentAfter=killed->specialMechanisms.carrierParents().at(1);
        const int ready=parentAfter.slotReadyTick.at(deadSlot), deathTick=killed->tick;
        if (parentAfter.childBySlot.at(deadSlot)!=-1 || ready!=deathTick+parentAfter.regenRate)
            return fail("AA kill did not release slot with original regeneration deadline");
        for (int n=0;killed->tick<ready-1;++n) {
            step(*killed);
            if (killed->specialMechanisms.carrierParents().at(1).childBySlot.at(deadSlot)!=-1)
                return fail("aircraft regenerated before original timer");
        }
        step(*killed);
        const int replacement=killed->specialMechanisms.carrierParents().at(1).childBySlot.at(deadSlot);
        const auto* fresh=findObjectById(*killed,replacement);
        if (replacement<0 || replacement==first || !fresh || fresh->health!=hp || fresh->type!=childType
            || !runtimeCarrierAircraftHasDeckProtection(*killed,*fresh) || fresh->airborne || fresh->currentAmmo!=1)
            return fail("original regen boundary did not create protected fresh deck aircraft");
        stage=moving?(ship=="DEST"?"native ship sink after actual sortie":"native ship sink with airborne children")
            :"native ship sink with parked children";
        auto doomed=restore(s,moving?outbound:deckSaved,true); if (!doomed) return fail("sink branch restore");
        int lethalAirborneChildren=-1,lethalParkedChildren=-1;
        if (!sink(*doomed,ids,moving,lethalAirborneChildren,lethalParkedChildren)) return false;
        const int sinkTick=doomed->tick;
        for (int n=0;n<10;++n) step(*doomed);
        if (!children(*doomed).empty()) return fail("dead mother spawned or retained children after cleanup");
        cases<<label<<','<<hz<<','<<moving<<','<<ship<<','<<childType<<','<<hp<<','<<capacity<<','<<nativeShellDamage<<','
            <<bombActors.size()<<','<<(targetBefore-findObjectById(s,2)->health)<<','<<(aaKills-killStart)
            <<','<<(aaCollateralKills-collateralStart)<<','<<(saves-saveStart)<<','<<(snapshots-snapshotStart)<<','<<(pairedTicks-pairedStart)
            <<','<<parentAfter.regenRate<<",150,"<<(sunk-sunkStart)<<','<<lethalAirborneChildren
            <<','<<lethalParkedChildren<<",PASS\n";
        ++completed;
        std::cout<<"Ship deck "<<label<<" PASS native_shell_damage="<<nativeShellDamage<<" protected_hp="<<hp
            <<" sortie_actors="<<bombActors.size()<<" designated_aa_kills="<<(aaKills-killStart)
            <<" collateral_aa_kills="<<(aaCollateralKills-collateralStart)
            <<" ordinary="<<(saves-saveStart)<<" canonical="<<(snapshots-snapshotStart)
            <<" paired="<<(pairedTicks-pairedStart)<<" regen="<<parentAfter.regenRate
            <<" reload150=ok sinkTick="<<sinkTick<<" lethalAirborneChildren="<<lethalAirborneChildren
            <<" lethalParkedChildren="<<lethalParkedChildren<<'\n';
        return true;
    };
    for (const std::string ship:{std::string("CARRIER"),std::string("DEST")})
        for (int hz:{15,30,60}) for (bool moving:{false,true}) if (!run(ship,hz,moving)) {
            cases<<label<<",,,,,,,,,,,,,,,,,,,,FAIL\n"; return false;
        }
    const int expected=selected&&*selected?1:12;
    if (completed!=expected) return fail("unknown selected label or missing full twelve-case coverage");
    if ((!selected || !*selected) && (saves!=72 || snapshots!=72 || pairedTicks!=864 || livePairedTicks!=144 || sunk!=12 || aaKills!=120))
        return fail("complete native stage/save/AA/sink catalogue count differs");
    if ((!selected || !*selected) && aswAirborneShipSinks<1)
        return fail("complete unfiltered matrix lacks an actual airborne ASW at mother lethal impact");
    std::cout<<"Ship deck protection passed: cases="<<completed<<" ordinary="<<saves<<" canonical="<<snapshots
        <<" ordinaryVsCanonicalPairedTicks="<<pairedTicks<<" uninterruptedDeckPairedTicks="<<livePairedTicks<<" designatedVictimAAKills="<<aaKills<<" collateralAAKills="<<aaCollateralKills<<" nativeShipSinks="<<sunk<<" ASWAirborneShipSinks="<<aswAirborneShipSinks
        <<" stock155mm60/ARTYHE-heavy60%-center36/moving-exact-falloff stockMedusa100/MedusaProjectile/SAMWH-light100%=100"
        <<" designatedAA=real-object-ForceAttack stockAEGIS-DistributedFire=yes"
        <<" sink=two-stock-DRED-Primary/General-DMislDamage300/DMISLWH-heavy80%=240/45sec"
        <<" originalRules/realCommands/realProjectiles/physicalDeck/launch/return/save/regen/reload=ok\n";
    return true;
}
