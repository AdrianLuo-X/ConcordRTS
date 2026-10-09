bool runRuntimeDestroyerSmokeProbe(RuntimeWindowState& live, std::string& error) {
    const auto fail = [&](const std::string& message) {
        error = "destroyer: " + message;
        return false;
    };
    const auto* destroyerRules = live.rules.findUnitType("Vehicle", "DEST");
    const auto* aircraftRules = live.rules.findUnitType("Aircraft", "ASW");
    const auto* launcher = live.rules.findWeaponType("ASWLauncher");
    if (destroyerRules == nullptr || aircraftRules == nullptr || launcher == nullptr
        || destroyerRules->spawns != "ASW" || destroyerRules->spawnsNumber != 1
        || !destroyerRules->noSpawnAlt || !launcher->spawner || launcher->range != -2.0) {
        return fail("stock DEST/ASW rules are missing");
    }

    for (const std::string mode : {"manual", "auto", "surface", "land", "cancel"}) {
        auto storage = std::make_unique<RuntimeWindowState>();
        auto& probe = *storage;
        probe.config = live.config;
        probe.report = live.report;
        probe.rules = live.rules;
        probe.rulesLoaded = true;
        probe.art = live.art;
        probe.artLoaded = true;
        probe.theater = live.theater;
        probe.theaterLoaded = live.theaterLoaded;
        probe.world = live.world;
        probe.world.accessTerrainCells().clear();
        probe.world.accessTerrainObjects().clear();
        probe.world.accessSmudges().clear();
        probe.economy.owner = "Americans";
        probe.runtimeSuppressPlayerAudioFeedback = true;
        probe.triggers.bootstrapComplete = true;
        probe.economy.skirmishStartApplied = true;
        for (int y = 1; y <= 24; ++y) for (int x = 1; x <= 30; ++x) {
            TerrainCell cell;
            cell.x = x;
            cell.y = y;
            cell.cellIndex512 = runtimeCellKey(x, y);
            cell.tileIndex = 0;
            cell.subTileIndex = 0;
            probe.world.accessTerrainCells().push_back(cell);
        }
        WorldObjectInstance parent;
        parent.instanceId = 1;
        parent.kind = WorldObjectKind::Unit;
        parent.type = "DEST";
        parent.owner = "Americans";
        parent.health = destroyerRules->strength;
        parent.alive = true;
        parent.x = 8;
        parent.y = 10;
        parent.cell = runtimeCellKey(parent.x, parent.y);
        parent.facing = parent.turretFacing = runtimeFacingFromDelta(1, 0);
        parent.mission = "Guard";
        auto target = parent;
        target.instanceId = 2;
        target.type = mode == "surface" ? "DRED" : mode == "land" ? "MTNK" : "SUB";
        target.owner = "Russians";
        target.health = 600;
        target.x = mode == "manual" ? 16 : 13;
        target.cell = runtimeCellKey(target.x, target.y);
        target.submerged = target.type == "SUB";
        target.mission = "Sleep";
        probe.world.accessObjects() = {parent, target};
        initializeRuntimeNavigation(probe);
        for (auto& [cell, terrain] : probe.terrainInfoByCell) {
            terrain.water = mode != "land" || cell != target.cell;
            terrain.kind = terrain.water ? RuntimeTerrainKind::Water : RuntimeTerrainKind::Land;
        }
        advanceRuntimeSpecialMechanisms(probe);
        const auto& slots = probe.specialMechanisms.carrierParents().at(1).childBySlot;
        const int childId = slots.at(0);
        const auto* child = findObjectById(probe, childId);
        if (child == nullptr || child->type != "ASW" || child->airborne
            || child->dockedToObjectId != 1 || child->currentAmmo != aircraftRules->ammo) {
            return fail(mode + " did not create a loaded deck aircraft");
        }
        const auto emptyDeck = [&]() {
            refreshRuntimeSpecialMechanismRenderSnapshots(probe);
            return std::any_of(probe.specialMechanismRenderSnapshots.begin(),
                probe.specialMechanismRenderSnapshots.end(), [](const auto& snapshot) {
                    return snapshot.objectId == 1 && snapshot.kind == "noSpawnDeck";
                });
        };
        if (emptyDeck()) return fail(mode + " hid the parked aircraft");
        if (mode != "auto") {
            probe.selectedObjectIds = {1};
            if (runtimeTargetCursorIntent(probe, *findObjectById(probe, 2))
                != RuntimeTargetCursorIntent::Attack) {
                return fail(mode + " did not offer the attack cursor");
            }
            issueRuntimeAttackCommand(probe, *findObjectById(probe, 2));
            if (probe.lastCommand.acceptedCount != 1) {
                return fail(mode + " attack command was rejected");
            }
        }
        if (mode == "cancel") {
            advanceRuntimeSpecialMechanisms(probe);
            if (!findObjectById(probe, childId)->airborne) {
                return fail("cancel fixture never launched");
            }
            eraseRuntimeBehavior(probe, 1);
            findMutableObjectById(probe, 1)->mission = "Sleep";
            advanceRuntimeSpecialMechanisms(probe);
            if (probe.specialMechanisms.carrierChildren().at(childId).phase
                != CarrierChildPhase::Returning) {
                return fail("clearing a target during takeoff did not recall the aircraft");
            }
        }

        bool launched = mode == "cancel";
        bool fired = false;
        bool damaged = false;
        bool returned = false;
        bool renderChecked = false;
        struct SurfaceShellWitness {
            RuntimeProjectileEffect shell;
            std::vector<WorldObjectInstance> missilesBefore;
            std::set<int> killedMissiles;
            int impactTick = 0;
            bool complete = false;
        };
        std::map<int, SurfaceShellWitness> surfaceShellWitnesses;
        std::set<int> surfaceDebrisActors;
        std::string surfaceWitnessError;
        struct ImpactObserverScope {
            RuntimeWindowState& state;
            std::function<void(const RuntimeProjectileEffect&, bool)> previous;
            ~ImpactObserverScope() { state.runtimeProjectileImpactObserver = std::move(previous); }
        } impactObserverScope{probe, std::move(probe.runtimeProjectileImpactObserver)};
        if (mode == "surface") probe.runtimeProjectileImpactObserver = [&](const RuntimeProjectileEffect& shell, bool after) {
            if (impactObserverScope.previous) impactObserverScope.previous(shell, after);
            const auto* cannon = probe.rules.findWeaponType("155mm");
            if (!cannon || cannon->damage != 60 || destroyerRules->primaryWeapon != "155mm"
                || shell.attackerObjectId != 1 || shell.targetObjectId != 2 || shell.weaponId != "155mm"
                || shell.projectileId != "Ballistic" || shell.warheadId != "ARTYHE" || shell.damage != 36
                || shell.parentActorId >= 0 || shell.generation != 0 || shell.aa || !shell.ag
                || shell.particleDamagePulse || shell.voxelDebris.enabled
                || runtimeCombatProjectileAuthoredDamage(probe, shell, *cannon) != 60) return;
            const auto* mother = findObjectById(probe, 1);
            const auto* enemy = findObjectById(probe, 2);
            if (!mother || mother->health <= 0 || !enemy || enemy->type != "DRED"
                || enemy->health != (after ? 564 : 600)) {
                surfaceWitnessError = "surface native 155mm impact lost living DEST or exact DRED600-to564 witness";
                return;
            }
            if (!after) {
                auto& witness = surfaceShellWitnesses[shell.actorId];
                witness.shell = shell; witness.impactTick = probe.tick;
                const auto parentSlot = probe.specialMechanisms.carrierParents().find(2);
                if (parentSlot == probe.specialMechanisms.carrierParents().end()) return;
                for (const auto& [slot, id] : parentSlot->second.childBySlot) {
                    const auto* missile = findObjectById(probe, id);
                    const auto registered = probe.specialMechanisms.carrierChildren().find(id);
                    if (missile && missile->type == "DMISL" && missile->health == 50 && missile->owner == enemy->owner
                        && !missile->airborne && missile->dockedToObjectId == 2 && missile->dockIndex == slot
                        && registered != probe.specialMechanisms.carrierChildren().end()
                        && registered->second.parentObjectId == 2 && registered->second.slot == slot && registered->second.phase == CarrierChildPhase::Docked)
                        witness.missilesBefore.push_back(*missile);
                }
                return;
            }
            const auto found = surfaceShellWitnesses.find(shell.actorId);
            if (found == surfaceShellWitnesses.end()) {
                surfaceWitnessError = "surface native 155mm impact lacks its before witness"; return;
            }
            for (const auto& before : found->second.missilesBefore) {
                const auto* body = findRuntimeObjectByIdIncludingInactive(probe, before.instanceId);
                if (body && body->type == "DMISL" && !body->alive && body->health == 0)
                    found->second.killedMissiles.insert(before.instanceId);
            }
            found->second.complete = true;
        };
        for (int step = 0; step < 1000; ++step) {
            ++probe.tick;
            advanceRuntimeSimulation(probe);
            if (!surfaceWitnessError.empty()) return fail(surfaceWitnessError);
            const auto* ship = findObjectById(probe, 1);
            child = findObjectById(probe, childId);
            const auto* victim = findObjectById(probe, 2);
            if (ship == nullptr || child == nullptr || victim == nullptr) {
                return fail(mode + " unexpectedly removed a fixture object");
            }
            if (mode != "surface" && mode != "land"
                && (ship->x != parent.x || ship->y != parent.y)) {
                return fail(mode + " drove the destroyer toward an infinite-range ASW target");
            }
            if (child->airborne) {
                launched = true;
                if (!emptyDeck()) return fail(mode + " left a duplicate aircraft on deck");
                if (!renderChecked) {
                    const char* output = std::getenv("CONCORD_DESTROYER_OUTPUT");
                    const auto stats = drawRuntimeStateOffscreenAtCell(
                        probe, {12, 10}, 640, 420, error,
                        output == nullptr ? "" : std::string(output) + "/" + mode + "-flight.bmp", false);
                    if (!stats) return false;
                    if (stats->visibleVoxelBodies < 2 || stats->missingVoxelProxies != 0) {
                        return fail(mode + " could not render the ship and aircraft voxels");
                    }
                    renderChecked = true;
                }
            }
            for (const auto& projectile : probe.projectiles) {
                if (projectile.attackerObjectId == 1 && projectile.weaponId == "ASWLauncher") {
                    return fail(mode + " emitted a virtual launcher projectile");
                }
                const bool asw = mode != "surface" && mode != "land";
                if ((asw && projectile.attackerObjectId == childId)
                    || (!asw && projectile.attackerObjectId == 1)) {
                    const std::string expected = asw ? "ASWBomb" : destroyerRules->primaryWeapon;
                    if (mode == "surface" && projectile.weaponId == "UnitDebris") {
                        const auto cause = surfaceShellWitnesses.find(projectile.parentActorId);
                        const auto* body = findRuntimeObjectByIdIncludingInactive(probe, projectile.sourceAnchorObjectId);
                        const auto* debrisArt = probe.art.findArtType(projectile.projectileId);
                        if (fired && ship->alive && ship->health > 0 && victim->health == 564
                            && cause != surfaceShellWitnesses.end() && cause->second.complete
                            && cause->second.killedMissiles.contains(projectile.sourceAnchorObjectId)
                            && body && body->type == "DMISL" && !body->alive && body->health == 0
                            && projectile.actorId > projectile.parentActorId && projectile.generation == 1
                            && projectile.targetObjectId == -1 && projectile.owner == cause->second.shell.owner
                            && projectile.spawnTick == cause->second.impactTick && projectile.weaponSlot == "debris"
                            && projectile.renderIntent == "unitDebris" && projectile.warheadId.empty()
                            && !projectile.aa && projectile.ag && !projectile.particleDamagePulse && !projectile.voxelDebris.enabled
                            && projectile.projectileId.rfind("DBRIS", 0) == 0 && debrisArt && debrisArt->damage == 10 && projectile.damage == 10) {
                            surfaceDebrisActors.insert(projectile.actorId);
                            continue; // A witnessed death child cannot establish fired or damaged.
                        }
                    }
                    if (projectile.weaponId != expected) {
                        std::ostringstream detail;
                        detail << mode << " used " << projectile.weaponId << " tick=" << probe.tick << " step=" << step
                            << " fired=" << fired << " damaged=" << damaged << " expected=" << expected
                            << " actor=" << projectile.actorId << " projectile=" << projectile.projectileId << " damage=" << projectile.damage
                            << " attacker=" << projectile.attackerObjectId << " sourceAnchor=" << projectile.sourceAnchorObjectId
                            << " parentActor=" << projectile.parentActorId << " generation=" << projectile.generation << " target=" << projectile.targetObjectId;
                        for (const int id : {1, 2, childId, projectile.sourceAnchorObjectId}) {
                            const auto* body = findRuntimeObjectByIdIncludingInactive(probe, id);
                            detail << " object[" << id << "]={";
                            if (body) detail << body->type << " hp=" << body->health << " alive=" << body->alive << " cell=" << body->x << ',' << body->y
                                << " air=" << body->airborne << " dock=" << body->dockedToObjectId << " slot=" << body->dockIndex << " mission=" << body->mission;
                            else detail << "missing";
                            detail << '}';
                        }
                        detail << " serialized={" << serializeRuntimeProjectileEffect(projectile) << '}';
                        return fail(detail.str());
                    }
                    fired = true;
                }
            }
            if (victim->health < 600) {
                if (!fired) return fail(mode + " caused damage before its real weapon fired");
                damaged = true;
            }
            if (mode == "surface" || mode == "land") {
                if (launched || !probe.specialMechanisms.isCarrierChildDocked(childId)) {
                    return fail(mode + " launched an ASW aircraft for a deck-gun target");
                }
                if (fired && damaged) break;
            } else if (launched && probe.specialMechanisms.isCarrierChildDocked(childId)) {
                if (child->airborne || child->dockedToObjectId != 1
                    || child->currentAmmo != aircraftRules->ammo || emptyDeck()) {
                    return fail(mode + " did not restore the loaded aircraft on deck");
                }
                returned = true;
                if (mode == "cancel" || damaged) break;
            }
        }
        if (mode == "cancel") {
            if (!returned || fired || damaged) return fail("recalled aircraft still attacked");
        } else if (!fired || !damaged
            || ((mode == "manual" || mode == "auto") && (!launched || !returned))) {
            return fail(mode + " incomplete sortie: fired=" + formatBool(fired)
                + " damaged=" + formatBool(damaged) + " launched=" + formatBool(launched)
                + " returned=" + formatBool(returned) + " " + probe.simulationStatus);
        }
        std::cout << "Destroyer " << mode << ": targeting=ok weapon=ok deck=ok"
                  << " launched=" << formatBool(launched)
                  << " returned=" << formatBool(returned)
                  << " debrisWitnesses=" << surfaceDebrisActors.size() << '\n';
    }
    return true;
}
