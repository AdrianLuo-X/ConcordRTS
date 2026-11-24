// Test-only original full Root parasite host/save qualifier. All original assertions retained.
namespace h02505_private_qualification {
using namespace concord::runtime::session_detail;
template<class T> decltype(auto) stunEntries(const T& values){if constexpr(requires {values.entries();})return values.entries();else return (values);}
bool saveRuntimeState(
    const IndependentRecoveryHost& state,
    const std::string& savePath,
    std::string& errorMessage
) {
    return saveRuntimeStateWithWriter(
        state,
        savePath,
        [&state](std::ostream& output, std::string& writerError) {
            return writeRuntimeStateToStream(state, output, writerError);
        },
        errorMessage
    );
}

unsigned long long runtimePersistentTransientStateHash(
    const IndependentRecoveryHost& state
) {
    std::uint64_t hash = 1469598103934665603ull;
    std::ostringstream transportEgress;
    writeRuntimeTransportTransferPresentation(state, transportEgress);
    if (!transportEgress.str().empty()) runtimeStableHashMixString(hash, transportEgress.str());
    if (!state.triggers.computerTacticsByOwner.empty()) {
        std::ostringstream computerTactics;
        writeRuntimeComputerTactics(computerTactics, state);
        runtimeStableHashMixString(hash, computerTactics.str());
    }

    mixRuntimeComputerMiningHash(hash, state.computerMiningByOwner);
    RuntimeWorldAudioSystemState hashedWorldAudio =
        state.worldAudioInitialized
        ? state.worldAudio
        : RuntimeWorldAudioSystemState(RuntimeWorldAudioConfig{
            std::max(1, state.config.fixedTickHz), 4096U
        });
    if (hashedWorldAudio.lastAdvancedTick() < state.tick
        && (!state.worldAudioInitialized
            || hashedWorldAudio.handles().empty())) {
        (void)hashedWorldAudio.tick(
            runtimeWorldAudioContext(state),
            state.tick
        );
    }
    RuntimeLightingSystemState hashedLighting = state.lightingInitialized
        ? state.lighting
        : RuntimeLightingSystemState(
            state.mapFacts.lighting,
            runtimeLightingConfigForState(state)
        );
    if (hashedLighting.lastAdvancedTick() < state.tick) {
        (void)hashedLighting.tick(state.tick);
    }
    RuntimeParticleSystemState hashedParticles = state.particlesInitialized
        ? state.particles
        : RuntimeParticleSystemState(std::max(1, state.config.fixedTickHz));
    if (hashedParticles.currentTick() < state.tick) {
        (void)hashedParticles.alignEmptyStateTick(state.tick);
    }
    SuperWeaponSystem hashedSuperWeapons;
    if (state.superWeaponsInitialized) {
        hashedSuperWeapons = state.superWeapons;
    } else {
        hashedSuperWeapons.reset(std::vector<SuperWeaponRule>{});
    }
    if (hashedSuperWeapons.lastTick() != state.tick) {
        (void)hashedSuperWeapons.alignEmptyStateTick(state.tick);
    }
    runtimeStableHashMixString(hash, "WorldAudioRuntime/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.worldAudioCatalog.stateHash())
    );
    runtimeStableHashMix(
        hash,
        static_cast<long long>(hashedWorldAudio.stateHash())
    );
    runtimeStableHashMixString(hash, "LightingRuntime/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(hashedLighting.stateHash())
    );
    for (const auto& [token, active] : state.activeLightingTokens) {
        runtimeStableHashMix(hash, static_cast<long long>(token));
        runtimeStableHashMix(
            hash,
            static_cast<long long>(active.mode)
        );
        runtimeStableHashMix(hash, active.startedTick);
    }
    runtimeStableHashMixString(hash, "ParticleRuntime/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(hashedParticles.stateHash())
    );
    runtimeStableHashMixString(hash, "SuperWeaponRuntime/2");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(hashedSuperWeapons.stateHash())
    );
    runtimeStableHashMixString(hash, "Projectiles/5");
    runtimeStableHashMix(hash, state.nextProjectileActorId);
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.projectiles.size())
    );
    for (const RuntimeProjectileEffect& projectile : state.projectiles) {
        auto authoritative = projectile;
        authoritative.sourceScreenOffsetX = authoritative.sourceScreenOffsetY = 0;
        authoritative.sourceNativeFixedX = authoritative.sourceNativeFixedY = 0;
        runtimeStableHashMixString(
            hash,
            serializeRuntimeProjectileEffect(authoritative)
        );
    }
    runtimeStableHashMixString(hash, "DeathWeaponRuntime/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.deathWeapons.stateHash())
    );
    runtimeStableHashMixString(hash, "CombatDetails/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.combatDetails.deterministicHash()));
    runtimeStableHashMixString(hash, "RuntimePsychicStatus/2");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.psychicStatuses.stateHash())
    );
    runtimeStableHashMixString(hash, "RuntimeMagBeam/2");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.magBeams.stateHash())
    );
    runtimeStableHashMixString(hash, "AirstrikeRuntime/3");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.airstrikes.stateHash())
    );
    runtimeStableHashMixString(hash, "AirstrikeHost/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.airstrikePlanesByToken.size())
    );
    for (const auto& [planeToken, binding] :
            state.airstrikePlanesByToken) {
        runtimeStableHashMix(hash, planeToken);
        runtimeStableHashMix(hash, binding.strikeId);
        runtimeStableHashMix(hash, binding.objectId);
        runtimeStableHashMix(hash, binding.callerObjectId);
        runtimeStableHashMix(hash, binding.targetObjectId);
        runtimeStableHashMix(hash, binding.spawnTick);
        runtimeStableHashMix(hash, binding.bombTick);
        runtimeStableHashMix(hash, binding.egressStartTick);
        runtimeStableHashMix(hash, binding.despawnTick);
        runtimeStableHashMix(hash, binding.elite ? 1 : 0);
        runtimeStableHashMixString(hash, binding.owner);
        runtimeStableHashMixString(hash, binding.aircraftTypeId);
        runtimeStableHashMixString(hash, binding.weaponId);
        runtimeStableHashMixString(hash, binding.warheadId);
        runtimeStableHashMix(hash, binding.route.entry.x);
        runtimeStableHashMix(hash, binding.route.entry.y);
        runtimeStableHashMix(hash, binding.route.target.x);
        runtimeStableHashMix(hash, binding.route.target.y);
        runtimeStableHashMix(hash, binding.route.exit.x);
        runtimeStableHashMix(hash, binding.route.exit.y);
        if(binding.hasPreciseEgressOrigin) {
            runtimeStableHashMixString(hash,"AirstrikeEgressOrigin/1");
            runtimeMultiplayerAuthoritativeMixDouble(hash,binding.preciseEgressOriginX);
            runtimeMultiplayerAuthoritativeMixDouble(hash,binding.preciseEgressOriginY);
        }
    }
    runtimeStableHashMix(
        hash,
        static_cast<long long>(
            state.airstrikeRootAuthoritiesByActor.size()
        )
    );
    for (const auto& [rootActorId, authority] :
            state.airstrikeRootAuthoritiesByActor) {
        runtimeStableHashMix(hash, rootActorId);
        runtimeStableHashMix(hash, authority.strikeId);
        runtimeStableHashMix(hash, authority.planeToken);
        runtimeStableHashMix(hash, authority.planeObjectId);
        runtimeStableHashMix(hash, authority.callerObjectId);
        runtimeStableHashMix(hash, authority.targetObjectId);
        runtimeStableHashMix(hash, authority.launchTick);
        runtimeStableHashMix(hash, authority.terminalTick);
        runtimeStableHashMix(hash, authority.elite ? 1 : 0);
        runtimeStableHashMix(hash, authority.bombCell.x);
        runtimeStableHashMix(hash, authority.bombCell.y);
        runtimeStableHashMixString(hash, authority.owner);
        runtimeStableHashMixString(hash, authority.aircraftTypeId);
        runtimeStableHashMixString(hash, authority.weaponId);
        runtimeStableHashMixString(hash, authority.warheadId);
        runtimeStableHashMixString(hash, authority.projectileId);
        runtimeStableHashMix(hash, authority.roundsRemaining);
        runtimeStableHashMix(hash, authority.nextFireTick);
        runtimeStableHashMix(hash, static_cast<long long>(authority.rounds.size()));
        for (const auto& round : authority.rounds) {
            runtimeStableHashMix(hash, round.actorId);
            runtimeStableHashMix(hash, round.launchTick);
            runtimeStableHashMix(hash, round.terminalTick);
            runtimeStableHashMix(hash, round.sourceCell.x);
            runtimeStableHashMix(hash, round.sourceCell.y);
            runtimeStableHashMix(hash, round.damage);
        }
        runtimeStableHashMix(hash, authority.damage);
        runtimeStableHashMix(
            hash,
            static_cast<long long>(std::llround(
                authority.veteranDamageMultiplier * 1000000.0
            ))
        );
    }

    std::vector<std::pair<int, int>> gattlingTimers;
    gattlingTimers.reserve(state.gattlingWeaponTimers.size());
    for (const auto& [objectId, timer] : state.gattlingWeaponTimers) {
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (timer <= 0 || object == nullptr || !object->alive) {
            continue;
        }
        const RuntimeCachedCapabilityProfile& cached =
            runtimeCachedCapabilityProfileForObject(state, *object);
        if (!cached.gattling) {
            continue;
        }
        gattlingTimers.emplace_back(
            objectId,
            std::clamp(
                timer,
                0,
                runtimeGattlingTimerCap(cached, object->veterancy >= 2)
            )
        );
    }
    std::sort(gattlingTimers.begin(), gattlingTimers.end());
    runtimeStableHashMixString(hash, "GattlingWeaponTimers/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(gattlingTimers.size())
    );
    for (const auto& [objectId, timer] : gattlingTimers) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, timer);
    }

    hashRuntimePublicBuildingLocations(hash, state);
    hashRuntimeWrecks(hash, state);
    hashRuntimeBuildingCollapses(hash, state);
    runtimeStableHashMixString(hash, "Crates/4");
    runtimeStableHashMix(hash, state.triggers.truckCrate ? 1 : 0);
    runtimeStableHashMix(hash, state.triggers.trainCrate ? 1 : 0);
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.skirmishCrates.size())
    );
    for (const RuntimeCrateState& crate : state.skirmishCrates) {
        runtimeStableHashMix(hash, crate.cell.x);
        runtimeStableHashMix(hash, crate.cell.y);
        runtimeStableHashMix(hash, crate.active ? 1 : 0);
        runtimeStableHashMix(hash, crate.respawnTick);
        runtimeStableHashMix(hash, crate.value);
        runtimeStableHashMix(hash, crate.water ? 1 : 0);
        runtimeStableHashMix(hash, crate.authored ? 1 : 0);
        runtimeStableHashMixString(hash, crate.powerupType);
        runtimeStableHashMixString(hash, crate.powerupAnimation);
        runtimeStableHashMixString(hash, crate.imageId);
        runtimeStableHashMix(
            hash,
            static_cast<long long>(std::llround(
                runtimeNormalizeCratePowerupData(crate.powerupData)
                    * 1000000.0
            ))
        );
    }
    runtimeStableHashMixString(hash, "Smudges/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.world.getSmudges().size())
    );
    for (const WorldSmudge& smudge : state.world.getSmudges()) {
        runtimeStableHashMixString(hash, smudge.mapId);
        runtimeStableHashMixString(hash, smudge.type);
        runtimeStableHashMix(hash, smudge.x);
        runtimeStableHashMix(hash, smudge.y);
        runtimeStableHashMix(hash, smudge.cell);
        runtimeStableHashMix(hash, smudge.level);
        runtimeStableHashMix(hash, smudge.data);
        runtimeStableHashMix(hash, smudge.authored ? 1 : 0);
    }
    std::vector<std::pair<int, int>> objectAmmo;
    objectAmmo.reserve(state.world.getObjects().size());
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        objectAmmo.emplace_back(object.instanceId, object.currentAmmo);
    }
    std::sort(objectAmmo.begin(), objectAmmo.end());
    runtimeStableHashMixString(hash, "ObjectAmmo/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(objectAmmo.size())
    );
    for (const auto& [objectId, currentAmmo] : objectAmmo) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, currentAmmo);
    }
    std::vector<std::tuple<int, int, int, int, int>> objectCombatStates;
    objectCombatStates.reserve(state.world.getObjects().size());
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        objectCombatStates.emplace_back(
            object.instanceId,
            object.prone ? 1 : 0,
            object.proneTicksRemaining,
            object.weaponCooldownTicksRemaining,
            object.deployStateTicksRemaining
        );
    }
    std::sort(objectCombatStates.begin(), objectCombatStates.end());
    runtimeStableHashMixString(hash, "ObjectCombatStates/2");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(objectCombatStates.size())
    );
    for (const auto& [objectId, prone, proneTicks, weaponCooldown,
                     deployStateTicks] : objectCombatStates) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, prone);
        runtimeStableHashMix(hash, proneTicks);
        runtimeStableHashMix(hash, weaponCooldown);
        runtimeStableHashMix(hash, deployStateTicks);
    }
    std::vector<int> capturedObjects;
    for (const auto& object : state.world.getObjects())
        if (object.hasBeenCaptured) capturedObjects.push_back(object.instanceId);
    if (!capturedObjects.empty()) {
        std::sort(capturedObjects.begin(), capturedObjects.end());
        runtimeStableHashMixString(hash, "CapturedCrew/1");
        for (int id : capturedObjects) runtimeStableHashMix(hash, id);
    }
    std::vector<int> suspendedHarvesters;
    for (const auto& object : state.world.getObjects()) {
        if (object.harvestAutoSearchSuspended) suspendedHarvesters.push_back(object.instanceId);
    }
    if (!suspendedHarvesters.empty()) {
        std::sort(suspendedHarvesters.begin(), suspendedHarvesters.end());
        runtimeStableHashMixString(hash, "HarvesterManualMove/1");
        for (int id : suspendedHarvesters) runtimeStableHashMix(hash, id);
    }
    std::vector<std::pair<int, int>> bioReactorAnimationStates;
    for (const auto& object : state.world.getObjects()) {
        if (object.bioReactorAnimationStartTick >= 0)
            bioReactorAnimationStates.emplace_back(object.instanceId, object.bioReactorAnimationStartTick);
    }
    if (!bioReactorAnimationStates.empty()) {
        std::sort(bioReactorAnimationStates.begin(), bioReactorAnimationStates.end());
        runtimeStableHashMixString(hash, "BioReactorAnimation/1");
        for (const auto& [id, tick] : bioReactorAnimationStates) {
            runtimeStableHashMix(hash, id);
            runtimeStableHashMix(hash, tick);
        }
    }
    std::vector<std::pair<int, int>> bioReactorPoweredAnimationStates;
    for (const auto& object : state.world.getObjects()) {
        if (object.bioReactorAnimationPoweredStartTick >= 0)
            bioReactorPoweredAnimationStates.emplace_back(object.instanceId, object.bioReactorAnimationPoweredStartTick);
    }
    if (!bioReactorPoweredAnimationStates.empty()) {
        std::sort(bioReactorPoweredAnimationStates.begin(), bioReactorPoweredAnimationStates.end());
        runtimeStableHashMixString(hash, "BioReactorPoweredAnimation/1");
        for (const auto& [id, tick] : bioReactorPoweredAnimationStates) {
            runtimeStableHashMix(hash, id);
            runtimeStableHashMix(hash, tick);
        }
    }
    std::vector<std::array<int, 5>> bunkerStates;
    for (const auto& object : state.world.getObjects()) {
        if (object.tankBunkerHostId >= 0 || object.tankBunkerOccupantId >= 0 || object.tankBunkerLowering) {
            bunkerStates.push_back({object.instanceId, object.tankBunkerHostId, object.tankBunkerOccupantId,
                object.tankBunkerAnimationStartTick, object.tankBunkerLowering ? 1 : 0});
        }
    }
    if (!bunkerStates.empty()) {
        std::sort(bunkerStates.begin(), bunkerStates.end());
        runtimeStableHashMixString(hash, "TankBunkers/1");
        for (const auto& row : bunkerStates) for (int value : row) runtimeStableHashMix(hash, value);
    }
    std::vector<std::pair<int, int>> objectSpecialCashDeadlines;
    objectSpecialCashDeadlines.reserve(state.world.getObjects().size());
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        objectSpecialCashDeadlines.emplace_back(
            object.instanceId,
            object.specialCashNextTick
        );
    }
    std::sort(
        objectSpecialCashDeadlines.begin(),
        objectSpecialCashDeadlines.end()
    );
    runtimeStableHashMixString(hash, "ObjectSpecialCashDeadlines/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(objectSpecialCashDeadlines.size())
    );
    for (const auto& [objectId, nextTick] : objectSpecialCashDeadlines) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, nextTick);
    }
    std::vector<std::pair<int, int>> objectCloneDeliveryLeases;
    objectCloneDeliveryLeases.reserve(state.world.getObjects().size());
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        objectCloneDeliveryLeases.emplace_back(
            object.instanceId,
            object.specialCloneDeliveryObjectId
        );
    }
    std::sort(
        objectCloneDeliveryLeases.begin(),
        objectCloneDeliveryLeases.end()
    );
    runtimeStableHashMixString(hash, "ObjectCloneDeliveryLeases/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(objectCloneDeliveryLeases.size())
    );
    for (const auto& [objectId, cloneId] : objectCloneDeliveryLeases) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, cloneId);
    }
    std::vector<std::tuple<int, long long, long long, long long, int>>
        objectCrateBonuses;
    objectCrateBonuses.reserve(state.world.getObjects().size());
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        objectCrateBonuses.emplace_back(
            object.instanceId,
            static_cast<long long>(std::llround(
                object.crateArmorMultiplier * 1000000.0
            )),
            static_cast<long long>(std::llround(
                object.crateFirepowerMultiplier * 1000000.0
            )),
            static_cast<long long>(std::llround(
                object.crateSpeedMultiplier * 1000000.0
            )),
            object.crateCloakable ? 1 : 0
        );
    }
    std::sort(objectCrateBonuses.begin(), objectCrateBonuses.end());
    runtimeStableHashMixString(hash, "ObjectCrateBonuses/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(objectCrateBonuses.size())
    );
    for (const auto& [objectId, armor, firepower, speed, cloakable] :
            objectCrateBonuses) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, armor);
        runtimeStableHashMix(hash, firepower);
        runtimeStableHashMix(hash, speed);
        runtimeStableHashMix(hash, cloakable);
    }
    std::vector<std::pair<int, int>> factoryClearanceSources;
    std::vector<std::tuple<int, int, int, std::int32_t>> movementTiming;
    movementTiming.reserve(state.behaviors.size());
    for (const auto& [objectId, behavior] : state.behaviors) {
        if (behavior.factoryExitClearanceHostId >= 0) {
            factoryClearanceSources.emplace_back(objectId, behavior.factoryExitClearanceHostId);
        }
        if (behavior.kind != RuntimeBehaviorKind::Move
            && behavior.kind != RuntimeBehaviorKind::Attack) {
            continue;
        }
        movementTiming.emplace_back(
            objectId,
            static_cast<int>(behavior.kind),
            std::max(0, behavior.moveCooldownTicks),
            behavior.moveIntervalRemainderQ16
        );
    }
    std::sort(movementTiming.begin(), movementTiming.end());
    runtimeStableHashMixString(hash, "MovementTiming/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(movementTiming.size())
    );
    for (const auto& [objectId, kind, cooldownTicks, remainderQ16] :
            movementTiming) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, kind);
        runtimeStableHashMix(hash, cooldownTicks);
        runtimeStableHashMix(hash, remainderQ16);
    }
    if (!factoryClearanceSources.empty()) {
        std::sort(factoryClearanceSources.begin(), factoryClearanceSources.end());
        runtimeStableHashMixString(hash, "FactoryExitClearance/1");
        runtimeStableHashMix(hash, static_cast<long long>(factoryClearanceSources.size()));
        for (const auto& [objectId, hostId] : factoryClearanceSources) {
            runtimeStableHashMix(hash, objectId);
            runtimeStableHashMix(hash, hostId);
        }
    }
    runtimeStableHashMixString(hash, "ProductionLanes/2");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(runtimeProductionLanesStateHash(
            state.economy.localProductionLanes
        ))
    );
    const std::optional<RuntimeSelectedPlacement> canonicalPlacement =
        runtimeSelectedPlacement(state);
    runtimeStableHashMix(
        hash,
        canonicalPlacement.has_value()
            ? static_cast<long long>(
                static_cast<std::uint8_t>(
                    canonicalPlacement->lane
                ) + 1
              )
            : 0
    );
    runtimeStableHashMix(
        hash,
        canonicalPlacement.has_value()
            ? static_cast<long long>(
                canonicalPlacement->order.serial
              )
            : 0
    );
    runtimeStableHashMixString(hash, "GarrisonRuntime/1");
    runtimeStableHashMix(
        hash,
        state.garrisonsInitialized
            ? static_cast<long long>(
                state.garrisons.stateHash(state.world.getObjects())
              )
            : 0
    );
    std::vector<std::pair<int, int>> occupyIntents(
        state.pendingOccupyTargets.begin(),
        state.pendingOccupyTargets.end()
    );
    std::sort(occupyIntents.begin(), occupyIntents.end());
    runtimeStableHashMixString(hash, "OccupyIntents/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(occupyIntents.size())
    );
    for (const auto& [occupierId, targetId] : occupyIntents) {
        runtimeStableHashMix(hash, occupierId);
        runtimeStableHashMix(hash, targetId);
    }
    std::vector<std::pair<int, RuntimeTimedBombIntent>> timedBombIntents(
        state.pendingTimedBombIntents.begin(),
        state.pendingTimedBombIntents.end()
    );
    std::sort(
        timedBombIntents.begin(),
        timedBombIntents.end(),
        [](const auto& left, const auto& right) {
            return left.first < right.first;
        }
    );
    runtimeStableHashMixString(hash, "TimedBombIntents/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(timedBombIntents.size())
    );
    for (const auto& [sourceId, intent] : timedBombIntents) {
        runtimeStableHashMix(hash, sourceId);
        runtimeStableHashMix(hash, intent.targetObjectId);
        runtimeStableHashMix(
            hash,
            static_cast<long long>(intent.kind)
        );
    }
    runtimeStableHashMixString(hash, "SubcellRuntime/1");
    runtimeStableHashMix(
        hash,
        state.subcellsInitialized
            ? static_cast<long long>(state.subcells.stateHash(
                state.world.getObjects(),
                state.world.getTerrainObjects(),
                runtimeGroundPoseContext(state)
              ))
            : 0
    );
    runtimeStableHashMixString(hash, "GateRuntime/1");
    runtimeStableHashMix(
        hash,
        state.gatesInitialized
            ? static_cast<long long>(state.gates.stateHash())
            : 0
    );
    runtimeStableHashMixString(hash, "RepairDepotRuntime/4");
    const RuntimeRepairDepotSystemState canonicalRepairDepots =
        state.repairDepotsInitialized
            ? state.repairDepots
            : RuntimeRepairDepotSystemState(
                std::max(1, state.config.fixedTickHz)
              );
    runtimeStableHashMix(
        hash,
        static_cast<long long>(canonicalRepairDepots.stateHash())
    );
    runtimeStableHashMixString(hash, "PlayerCommands/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.playerCommands.stateHash()));
    runtimeStableHashMixString(hash, "FunctionalDetailRules/1");
    runtimeStableHashMix(hash, static_cast<long long>(state.rules.getDetailRulesHash()));
    runtimeStableHashMixString(hash, "ClonePendingDeliveries/1");
    runtimeStableHashMix(hash, static_cast<long long>(runtimeClonePendingDeliveriesHash(state)));
    runtimeStableHashMixString(hash, "FactoryDeliveryRuntime/2");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.factoryDeliveries.stateHash())
    );
    runtimeStableHashMixString(hash, "GrindingRuntime/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.grinding.stateHash())
    );
    runtimeStableHashMixString(hash, "AircraftServiceRuntime/1");
    RuntimeAircraftServiceSystemState canonicalAircraftServices =
        state.aircraftServicesInitialized
            ? state.aircraftServices
            : RuntimeAircraftServiceSystemState(
                std::max(1, state.config.fixedTickHz)
              );
    canonicalAircraftServices.reconcile(
        makeRuntimeAircraftServiceContext(state)
    );
    runtimeStableHashMix(
        hash,
        static_cast<long long>(canonicalAircraftServices.stateHash())
    );
    runtimeStableHashMixString(hash, "SpecialMechanisms/2");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.specialMechanisms.stateHash())
    );
    runtimeStableHashMixString(hash, "SpyRuntime/2");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(state.spies.stateHash())
    );
    std::vector<std::pair<int, int>> parasiteStuns;
    parasiteStuns.reserve(
        stunEntries(state.parasiteStunUntilTickByObject).size()
    );
    for (const auto& [objectId, untilTick] :
            stunEntries(state.parasiteStunUntilTickByObject)) {
        const WorldObjectInstance* object = findObjectById(state, objectId);
        if (object != nullptr && object->alive && object->health > 0
            && object->mission == "ParasiteStunned"
            && untilTick > state.tick) {
            parasiteStuns.emplace_back(objectId, untilTick);
        }
    }
    std::sort(parasiteStuns.begin(), parasiteStuns.end());
    runtimeStableHashMixString(hash, "ParasiteStuns/1");
    runtimeStableHashMix(
        hash,
        static_cast<long long>(parasiteStuns.size())
    );
    for (const auto& [objectId, untilTick] : parasiteStuns) {
        runtimeStableHashMix(hash, objectId);
        runtimeStableHashMix(hash, untilTick);
    }
    return hash;
}

bool runRuntimeParasiteHostSmokeProbe(std::string& errorMessage, const std::filesystem::path& outputRoot) {
    IniFile rulesIni;
    if (!rulesIni.loadFromString(
            "[General]\nConditionRed=25%\nConditionYellow=50%\n"
            "[VehicleTypes]\n0=TDRONE\n1=PARASITEHOST\n"
            "[AircraftTypes]\n[InfantryTypes]\n[BuildingTypes]\n"
            "[WeaponTypes]\n0=PARASITEGUN\n"
            "[ProjectileTypes]\n0=PARASITEPROJ\n"
            "[WarheadTypes]\n0=ParasiteWH\n"
            "[OverlayTypes]\n[Tiberiums]\n[TerrainTypes]\n"
            "[TDRONE]\nStrength=100\nArmor=light\nSpeed=10\n"
            "Organic=yes\nPrimary=PARASITEGUN\n"
            "[PARASITEHOST]\nStrength=200\nArmor=light\nSpeed=5\n"
            "Parasiteable=yes\n"
            "[PARASITEGUN]\nDamage=35\nROF=6\nRange=6\n"
            "Projectile=PARASITEPROJ\nWarhead=ParasiteWH\n"
            "LimboLaunch=yes\n"
            "[PARASITEPROJ]\nInviso=yes\nAG=yes\nAA=no\n"
            "[ParasiteWH]\n"
            "Verses=100%,100%,100%,100%,100%,100%,100%,100%,100%,100%,100%\n"
            "Parasite=yes\nParalyzes=15\n"
        )) {
        errorMessage = "synthetic parasite rules INI did not parse";
        return false;
    }
    RulesDatabase rules;
    if (!rules.loadFromIni(rulesIni)) {
        errorMessage = "synthetic parasite rules did not load";
        return false;
    }
    const WeaponType* weapon = rules.findWeaponType("PARASITEGUN");
    const WarheadType* warhead = rules.findWarheadType("ParasiteWH");
    if (weapon == nullptr || warhead == nullptr
        || !runtimeParasiteWarheadIsExclusive(*weapon, *warhead)) {
        errorMessage = "synthetic parasite relationship was not authored";
        return false;
    }

    IndependentRecoveryHost probe;
    probe.rules = rules;
    probe.rulesLoaded = true;
    probe.config.fixedTickHz = 60;
    probe.tick = 100;
    probe.economy.owner = "HouseA";
    weapon = probe.rules.findWeaponType("PARASITEGUN");
    warhead = probe.rules.findWarheadType("ParasiteWH");
    if (weapon == nullptr || warhead == nullptr) {
        errorMessage = "copied parasite rules lost relationship identity";
        return false;
    }
    probe.economy.skirmishStartApplied = true;
    for (int y = 4; y <= 20; ++y) {
        for (int x = 4; x <= 20; ++x) {
            TerrainCell terrain;
            terrain.x = x;
            terrain.y = y;
            terrain.cellIndex512 = runtimeCellKey(x, y);
            terrain.tileIndex = 0;
            probe.world.accessTerrainCells().push_back(terrain);
            probe.terrainInfoByCell.emplace(
                runtimeCellKey(x, y),
                RuntimeTerrainInfo{}
            );
        }
    }
    probe.terrainByCell = buildRuntimeTerrainLookup(probe.world);
    const auto addObject = [&probe](
        int id,
        const std::string& type,
        const std::string& owner,
        int health,
        int x,
        int y
    ) {
        WorldObjectInstance object;
        object.instanceId = id;
        object.kind = WorldObjectKind::Unit;
        object.type = type;
        object.owner = owner;
        object.health = health;
        object.alive = true;
        object.x = x;
        object.y = y;
        object.cell = runtimeCellKey(x, y);
        object.mission = "Guard";
        object.currentAmmo = -1;
        probe.world.accessObjects().push_back(std::move(object));
    };
    addObject(604, "TDRONE", "HouseA", 100, 10, 10);
    addObject(605, "PARASITEHOST", "HouseB", 200, 12, 10);
    ensureRuntimeSpecialMechanismsInitialized(probe);
    ensureRuntimeGarrisonsInitialized(probe);
    ensureRuntimeGatesInitialized(probe);
    ensureRuntimeSubcellsInitialized(probe);
    ensureRuntimeRepairDepotsInitialized(probe);
    (void)ensureRuntimeAircraftServicesInitialized(probe);

    const WorldObjectInstance* source = findObjectById(probe, 604);
    const WorldObjectInstance* target = findObjectById(probe, 605);
    if (source == nullptr || target == nullptr) {
        errorMessage = "parasite fixture objects disappeared";
        return false;
    }
    const RuntimeWeaponProfile profile = runtimeWeaponProfileForObject(
        probe,
        *source,
        target
    );
    if (profile.weapon != weapon
        || !launchRuntimeParasiteProjectile(
            probe,
            *source,
            *target,
            *weapon,
            profile
        )
        || probe.projectiles.size() != 1U) {
        errorMessage = "parasite host launch was not accepted";
        return false;
    }
    const int projectileId = probe.projectiles.front().actorId;
    const WorldObjectInstance* limboSource =
        findRuntimeObjectByIdIncludingInactive(probe, 604);
    if (limboSource == nullptr || limboSource->alive
        || limboSource->health <= 0
        || limboSource->mission != "ParasiteLimbo"
        || !probe.specialMechanisms.pendingParasiteLaunches().contains(
            projectileId
        )) {
        errorMessage = "parasite launch did not enter exact limbo authority";
        return false;
    }

    const std::filesystem::path pendingPath =
        outputRoot / "runtime_parasite_pending_smoke.ini";
    const std::filesystem::path linkPath =
        outputRoot / "runtime_parasite_link_smoke.ini";
    const std::filesystem::path stunPath =
        outputRoot / "runtime_parasite_stun_smoke.ini";
    const std::filesystem::path badPath =
        outputRoot / "runtime_parasite_bad_smoke.ini";


    const std::uint64_t pendingSpecialHash =
        probe.specialMechanisms.stateHash();
    const std::uint64_t pendingPersistentHash =
        runtimePersistentTransientStateHash(probe);
    RuntimeWorldAudioSystemState pendingAudioCandidate;
    RuntimeLightingSystemState pendingLightingCandidate;
    RuntimeParticleSystemState pendingParticleCandidate;
    std::string pendingAvError;
    if (!prepareRuntimeAudiovisualSaveCandidates(
            probe,
            pendingAudioCandidate,
            pendingLightingCandidate,
            pendingParticleCandidate,
            pendingAvError
        )) {
        errorMessage = "could not prepare pending audiovisual authority: "
            + pendingAvError;
        return false;
    }
    SuperWeaponSystem pendingSuperWeaponCandidate;
    if (probe.superWeaponsInitialized) {
        pendingSuperWeaponCandidate = probe.superWeapons;
    } else {
        pendingSuperWeaponCandidate.reset(std::vector<SuperWeaponRule>{});
    }
    if (pendingSuperWeaponCandidate.lastTick() != probe.tick
        && !pendingSuperWeaponCandidate.alignEmptyStateTick(probe.tick)) {
        errorMessage = "could not prepare pending SuperWeapon authority";
        return false;
    }
    RuntimeSpecialMechanismState pendingSpecial = probe.specialMechanisms;
    probe.specialMechanisms.reset(probe.rules.getSpecialMechanismRules());
    if (runtimePersistentTransientStateHash(probe)
        == pendingPersistentHash) {
        errorMessage = "persistent hash ignored pending parasite authority";
        return false;
    }
    probe.specialMechanisms = std::move(pendingSpecial);
    if (!saveRuntimeState(probe, pathString(pendingPath), errorMessage)) {
        errorMessage = "pending parasite save failed: " + errorMessage;
        return false;
    }
    std::ifstream pendingInput(pendingPath, std::ios::binary);
    std::string pendingText(
        (std::istreambuf_iterator<char>(pendingInput)),
        std::istreambuf_iterator<char>()
    );
    const bool pendingSectionsPresent =
        pendingText.find("[SpecialParasitePending]") != std::string::npos
        && pendingText.find("[SpecialParasiteResolved]")
            != std::string::npos;
    const bool pendingLoaded = loadRuntimeState(
        probe,
        pathString(pendingPath),
        errorMessage
    );
    const std::uint64_t loadedPendingSpecialHash =
        probe.specialMechanisms.stateHash();
    const std::uint64_t loadedPendingPersistentHash =
        runtimePersistentTransientStateHash(probe);
    if (!pendingSectionsPresent
        || !pendingLoaded
        || loadedPendingSpecialHash != pendingSpecialHash
        || loadedPendingPersistentHash != pendingPersistentHash
        || probe.projectiles.size() != 1U) {
        errorMessage = "pending parasite round-trip/hash drifted: "
            + errorMessage
            + " sections=" + (pendingSectionsPresent ? "yes" : "no")
            + " loaded=" + (pendingLoaded ? "yes" : "no")
            + " special=" + std::to_string(loadedPendingSpecialHash)
            + "/" + std::to_string(pendingSpecialHash)
            + " persistent="
            + std::to_string(loadedPendingPersistentHash)
            + "/" + std::to_string(pendingPersistentHash)
            + " audio=" + std::to_string(probe.worldAudio.stateHash())
            + "/" + std::to_string(pendingAudioCandidate.stateHash())
            + " lighting=" + std::to_string(probe.lighting.stateHash())
            + "/" + std::to_string(pendingLightingCandidate.stateHash())
            + " particles=" + std::to_string(probe.particles.stateHash())
            + "/" + std::to_string(pendingParticleCandidate.stateHash())
            + " super=" + std::to_string(probe.superWeapons.stateHash())
            + "/" + std::to_string(pendingSuperWeaponCandidate.stateHash())
            + " projectiles=" + std::to_string(probe.projectiles.size());
        return false;
    }

    RuntimeProjectileEffect impact = probe.projectiles.front();
    probe.projectiles.clear();
    const std::size_t ordinaryImpactsBefore = probe.impactEffects.size();
    if (!applyRuntimeProjectileImpact(probe, impact)
        || !probe.specialMechanisms.pendingParasiteLaunches().empty()
        || !probe.specialMechanisms.parasiteLinks().contains(605)
        || probe.impactEffects.size() != ordinaryImpactsBefore
        || findObjectById(probe, 605) == nullptr
        || findObjectById(probe, 605)->mission != "ParasiteParalyzed") {
        errorMessage = "exclusive parasite impact did not attach cleanly";
        return false;
    }
    const std::uint64_t linkSpecialHash =
        probe.specialMechanisms.stateHash();
    const std::uint64_t linkPersistentHash =
        runtimePersistentTransientStateHash(probe);
    if (!saveRuntimeState(probe, pathString(linkPath), errorMessage)
        || !loadRuntimeState(probe, pathString(linkPath), errorMessage)
        || probe.specialMechanisms.stateHash() != linkSpecialHash
        || runtimePersistentTransientStateHash(probe)
            != linkPersistentHash
        || !probe.specialMechanisms.parasiteLinks().contains(605)) {
        errorMessage = "attached parasite round-trip/hash drifted: "
            + errorMessage;
        return false;
    }

    if (!handleRuntimeParasiteHostEvent(
            probe,
            605,
            ParasiteHostEventKind::SonicHit,
            7
        )) {
        errorMessage = "parasite eject host event was rejected";
        return false;
    }
    const WorldObjectInstance* ejected = findObjectById(probe, 604);
    const WorldObjectInstance* releasedHost = findObjectById(probe, 605);
    const auto stun = stunEntries(probe.parasiteStunUntilTickByObject).find(604);
    if (!probe.specialMechanisms.parasiteLinks().empty()
        || ejected == nullptr || !ejected->alive
        || ejected->mission != "ParasiteStunned"
        || releasedHost == nullptr || releasedHost->mission != "Guard"
        || stun == stunEntries(probe.parasiteStunUntilTickByObject).end()
        || stun->second != probe.tick + 7) {
        errorMessage = "parasite eject did not release host and stun source"
            " links="
            + std::to_string(
                probe.specialMechanisms.parasiteLinks().size()
            )
            + " source="
            + (ejected == nullptr
                ? std::string("missing")
                : (runtimeBoolText(ejected->alive) + "/"
                    + ejected->mission + "/"
                    + std::to_string(ejected->x) + ","
                    + std::to_string(ejected->y)))
            + " host="
            + (releasedHost == nullptr
                ? std::string("missing")
                : releasedHost->mission)
            + " stun="
            + (stun == stunEntries(probe.parasiteStunUntilTickByObject).end()
                ? std::string("missing")
                : std::to_string(stun->second))
            + " tick=" + std::to_string(probe.tick)
            + " status=" + probe.simulationStatus;
        return false;
    }
    const int expectedStunUntil = stun->second;
    const std::uint64_t stunSpecialHash =
        probe.specialMechanisms.stateHash();
    const std::uint64_t stunPersistentHash =
        runtimePersistentTransientStateHash(probe);
    if (!saveRuntimeState(probe, pathString(stunPath), errorMessage)
        || !loadRuntimeState(probe, pathString(stunPath), errorMessage)
        || probe.specialMechanisms.stateHash() != stunSpecialHash
        || runtimePersistentTransientStateHash(probe)
            != stunPersistentHash
        || !stunEntries(probe.parasiteStunUntilTickByObject).contains(604)
        || stunEntries(probe.parasiteStunUntilTickByObject).at(604)
            != expectedStunUntil
        || findObjectById(probe, 605) == nullptr
        || findObjectById(probe, 605)->mission != "Guard") {
        errorMessage = "terminal parasite/stun round-trip drifted: "
            + errorMessage;
        return false;
    }

    std::string badText = pendingText;
    const std::size_t pendingSection = badText.find(
        "[SpecialParasitePending]"
    );
    const std::size_t pendingRecord = pendingSection == std::string::npos
        ? std::string::npos
        : badText.find("\n0=", pendingSection);
    const std::size_t pendingEnd = pendingRecord == std::string::npos
        ? std::string::npos
        : badText.find_first_of("\r\n", pendingRecord + 3U);
    if (pendingRecord == std::string::npos
        || pendingEnd == std::string::npos) {
        errorMessage = "could not locate pending parasite bad fixture";
        return false;
    }
    std::vector<std::string> badFields = splitRuntimeSaveFields(
        badText.substr(
            pendingRecord + 3U,
            pendingEnd - (pendingRecord + 3U)
        )
    );
    if (badFields.size() != 21U) {
        errorMessage = "pending parasite bad fixture width drifted";
        return false;
    }
    badFields[0] = std::to_string(probe.nextProjectileActorId);
    std::ostringstream badRecord;
    for (std::size_t index = 0; index < badFields.size(); ++index) {
        if (index != 0U) {
            badRecord << '|';
        }
        badRecord << badFields[index];
    }
    badText.replace(
        pendingRecord + 3U,
        pendingEnd - (pendingRecord + 3U),
        badRecord.str()
    );
    std::ofstream badOutput(badPath, std::ios::binary | std::ios::trunc);
    badOutput.write(badText.data(), static_cast<std::streamsize>(badText.size()));
    badOutput.close();
    const int atomicTick = probe.tick;
    const std::uint64_t atomicSpecialHash =
        probe.specialMechanisms.stateHash();
    const std::uint64_t atomicPersistentHash =
        runtimePersistentTransientStateHash(probe);
    std::string rejectedError;
    if (!badOutput.good()
        || loadRuntimeState(probe, pathString(badPath), rejectedError)
        || probe.tick != atomicTick
        || probe.specialMechanisms.stateHash() != atomicSpecialHash
        || runtimePersistentTransientStateHash(probe)
            != atomicPersistentHash
        || !stunEntries(probe.parasiteStunUntilTickByObject).contains(604)
        || findObjectById(probe, 605) == nullptr
        || findObjectById(probe, 605)->mission != "Guard") {
        errorMessage =
            "bad parasite cross-section was accepted or mutated live state";
        return false;
    }
    return true;
}

int run(const std::filesystem::path& outputRoot){
 std::filesystem::create_directories(outputRoot);std::string error;
 if(!runRuntimeParasiteHostSmokeProbe(error,outputRoot)){std::cerr<<"private-parasite-stun FAIL "<<error<<std::endl;return 1;}
 std::cout<<"private-parasite-stun original-full-host-probe=pending-link-eject7-stun-roundtrip-bad-save-atomic full-owned-state=exact retained-files=4 failures=0"<<std::endl;return 0;
}
}
