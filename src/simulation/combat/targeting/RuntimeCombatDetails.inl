// Included after spawnRuntimeProjectile and before spawnRuntimeWeaponProjectiles.
// The ordinary attack path, garrisons, opportunity fire and OpenTopped all
// enter this scheduler; specialized spawned-missile volleys keep their system.
bool runtimeCombatCanAct(const RuntimeWindowState& state, const WorldObjectInstance& object) {
    return object.health > 0 && runtimeObjectHasOperationalPower(state, object)
        && !state.combatDetails.panics.contains(object.instanceId)
        && !runtimeObjectDisabledByParasite(state, object.instanceId)
        && (!state.specialMechanismsInitialized
            || (!state.specialMechanisms.isTemporalFrozen(object.instanceId)
                && !state.specialMechanisms.isChronoMovementLocked(object.instanceId, state.tick)))
        && !runtimeComputerAiPausedForOwner(state, object.owner);
}

bool runtimeCombatValidPassenger(const WorldObjectInstance& passenger,
    const WorldObjectInstance& host) {
    return host.alive && host.health > 0 && passenger.health > 0
        && !passenger.alive && passenger.mission == "InsideTransport"
        && std::find(host.transportPassengerIds.begin(), host.transportPassengerIds.end(),
            passenger.instanceId) != host.transportPassengerIds.end();
}

bool runtimeCombatSourceAvailable(const RulesDatabase& rules,
    const std::vector<WorldObjectInstance>& objects, const WorldObjectInstance& source) {
    if (source.health <= 0) return false;
    if (source.alive) return true;
    if (source.mission != "InsideTransport") return false;
    for (const auto& host : objects) {
        // Boarding and owner-change transactions own alliance validation.
        // Allied houses may legally share a transport while keeping each
        // passenger's own owner, weapon experience and kill attribution.
        if (!runtimeCombatValidPassenger(source, host)) continue;
        const UnitType* hostType = rules.findUnitType("UnitType", host.type);
        if (hostType == nullptr) hostType = rules.findUnitType("AircraftType", host.type);
        if (hostType != nullptr && hostType->openTopped) return true;
    }
    return false;
}

void cancelRuntimeCombatFire(RuntimeWindowState& state, int objectId) {
    if (state.combatDetails.panics.erase(objectId) != 0) {
        if (auto* object = findMutableObjectById(state, objectId); object && object->mission == "Panic")
            object->mission = "Guard";
    }
    for (auto it = state.combatDetails.bursts.begin(); it != state.combatDetails.bursts.end();) {
        if (it->first == objectId || it->second.sourceAnchorObjectId == objectId)
            it = state.combatDetails.bursts.erase(it);
        else ++it;
    }
    for (auto it = state.combatDetails.fireCursors.begin(); it != state.combatDetails.fireCursors.end();) {
        if (it->first == objectId || it->second.sourceAnchorObjectId == objectId) {
            if (state.specialMechanismsInitialized && it->second.lastTargetObjectId >= 0)
                state.specialMechanisms.clearTemporalAttacker(it->first, it->second.lastTargetObjectId,
                    state.tick, runtimeSpecialEventSink(state), "combat source stopped or changed identity");
            it = state.combatDetails.fireCursors.erase(it);
        } else ++it;
    }
}

WorldObjectInstance runtimeCombatFiringPose(const WorldObjectInstance& shooter,
    const WorldObjectInstance* host) {
    WorldObjectInstance pose = shooter;
    if (host != nullptr) {
        pose.x = host->x; pose.y = host->y; pose.cell = host->cell;
        pose.level = host->level; pose.onBridge = host->onBridge;
        pose.hasRenderPosition = host->hasRenderPosition;
        pose.renderX = host->renderX; pose.renderY = host->renderY;
        pose.renderLevel = host->renderLevel; pose.renderOnBridge = host->renderOnBridge;
        pose.facing = host->facing;
        pose.airborne = host->airborne;
        pose.subCell = -1;
        pose.alive = true;
    }
    return pose;
}

RuntimeWeaponProfile runtimeCombatPassengerWeaponProfile(const RuntimeWindowState& state,
    const WorldObjectInstance& passenger, const WorldObjectInstance* target, bool forceFire = false) {
    const UnitType* unit = findRuntimeUnitType(state.rules, passenger);
    if (unit == nullptr || unit->openTransportWeapon < 0) {
        auto profile = runtimeWeaponProfileForObject(state, passenger, target, forceFire);
        return profile.weapon != nullptr && !profile.weapon->fireInTransport
            ? RuntimeWeaponProfile{} : profile;
    }
    const bool elite = passenger.veterancy >= 2;
    const bool secondary = unit->openTransportWeapon == 1 || unit->openTransportWeapon == 3;
    const std::string& preferred = secondary
        ? ((elite && !unit->eliteSecondaryWeapon.empty()) ? unit->eliteSecondaryWeapon : unit->secondaryWeapon)
        : ((elite && !unit->elitePrimaryWeapon.empty()) ? unit->elitePrimaryWeapon : unit->primaryWeapon);
    RuntimeWeaponProfile profile;
    const WeaponType* weapon = findRuntimeWeaponType(state.rules, preferred);
    if (weapon != nullptr && weapon->fireInTransport) finalizeRuntimeWeaponProfile(profile, state,
        RuntimeWeaponSlot::OpenTransport, *weapon, "open topped passenger");
    return profile;
}

bool runtimeCombatTargetInRange(const RuntimeWindowState& state,
    const WorldObjectInstance& pose, const WorldObjectInstance& target,
    const WeaponType& weapon, double rangeBonus, bool forceFire) {
    const bool hostile = runtimeOwnersAreHostileForAutoTarget(state, pose.owner, target.owner);
    if (!target.alive || target.health <= 0 || target.instanceId == pose.instanceId
        || !runtimeWeaponCanTargetObject(state, weapon, target)
        || (!forceFire && !runtimePsychicCanAttackTarget(state, pose, target, true, hostile))
        || (!forceFire && runtimeSpyDisguiseBlocksAttack(state, pose, target, true))
        || (!runtimeOwnersAreAllied(state, pose.owner, target.owner)
            && !runtimeObjectVisibleToOwner(state, pose.owner, target))) return false;
    const double distance = runtimeWeaponDistanceToTarget(state, pose, target, weapon);
    const double range = runtimeEffectiveWeaponRange(state, pose, target, weapon) + rangeBonus;
    return distance + 0.001 >= weapon.minimumRange && distance <= range
        && runtimeWeaponHasLineOfSight(state, pose, target, weapon);
}

#include "simulation/combat/targeting/RuntimeCombatOpportunityOwnerSummary.inl"
#include "simulation/combat/targeting/RuntimeCombatOpportunityRangeBound.inl"
#include "simulation/combat/targeting/RuntimeCombatOpportunityCandidates.inl"

const WorldObjectInstance* selectRuntimeCombatOpportunityTarget(
    const RuntimeWindowState& state, const WorldObjectInstance& pose,
    const WeaponType* fixedWeapon, double rangeBonus, bool distributed,
    int preferredTargetId = -1, bool forceFire = false, bool passengerFire = false,
    bool forceReference = false,
    RuntimeCombatOpportunityOwnerSummary* ownerSummary = nullptr,
    RuntimeCombatOpportunityCandidates* candidateIndex = nullptr
) {
    static const bool useReference = [] {
        const char* value = std::getenv("CONCORD_MOVEMENT_REFERENCE");
        return value != nullptr && value[0] == '1' && value[1] == '\0';
    }();
    if (!useReference && !forceReference && !runtimeFrameScalingReferenceMovement()
        && ownerSummary != nullptr && ownerSummary->provesNoCandidate(state, pose, forceFire))
        return nullptr;
    std::optional<RuntimeCombatOpportunityRangeBound> rangeBound;
    if (!useReference && !forceReference && !runtimeFrameScalingReferenceMovement())
        rangeBound.emplace(state, pose, fixedWeapon, rangeBonus, passengerFire);
    const std::vector<std::size_t>* indexedCandidates = nullptr;
    if (candidateIndex != nullptr && rangeBound && !runtimeCombatOpportunityCandidatesReference())
        indexedCandidates = candidateIndex->query(state, pose, *rangeBound);
    std::vector<const WorldObjectInstance*> eligible;
    std::unordered_map<std::string, bool> hostilityByOwner;
    const auto cursor = state.combatDetails.fireCursors.find(pose.instanceId);
    const int previous = cursor == state.combatDetails.fireCursors.end()
        ? -1 : cursor->second.lastTargetObjectId;
    const WorldObjectInstance* nearest = nullptr;
    double nearestDistance = std::numeric_limits<double>::infinity();
    const auto& objects = state.world.getObjects();
    const std::size_t candidateCount = indexedCandidates ? indexedCandidates->size() : objects.size();
    for (std::size_t candidatePosition = 0; candidatePosition < candidateCount; ++candidatePosition) {
        const auto& candidate = objects[indexedCandidates ? (*indexedCandidates)[candidatePosition] : candidatePosition];
        if (!candidate.alive || candidate.health <= 0) continue;
        const bool preferred = candidate.instanceId == preferredTargetId;
        if (!useReference && !forceReference) {
            // Weapon selection can inspect many authored weapon variants.
            // Apply its later, weapon-independent relation gate first so a
            // moving formation does not select weapons against every ally.
            // Keep the psychic query per candidate: berserk may allow allies,
            // and a forced preferred target bypasses this gate altogether.
            if (candidate.instanceId == pose.instanceId) continue;
            if (!(preferred && forceFire)) {
                auto relation = hostilityByOwner.find(candidate.owner);
                if (relation == hostilityByOwner.end()) {
                    relation = hostilityByOwner.emplace(candidate.owner,
                        runtimeOwnersAreHostileForAutoTarget(
                            state, pose.owner, candidate.owner)).first;
                }
                if (!runtimePsychicCanAttackTarget(
                        state, pose, candidate, true, relation->second)) continue;
            }
        }
        if (rangeBound && rangeBound->excludes(state, pose, candidate)) continue;
        const WeaponType* weapon = fixedWeapon;
        RuntimeWeaponProfile chosen;
        if (weapon == nullptr) {
            chosen = passengerFire ? runtimeCombatPassengerWeaponProfile(state, pose, &candidate, preferred && forceFire)
                : runtimeWeaponProfileForObject(state, pose, &candidate, preferred && forceFire);
            weapon = chosen.weapon;
        }
        if (weapon == nullptr || weapon->damage <= 0 || weapon->neverUse) continue;
        if (!runtimeCombatTargetInRange(state, pose, candidate, *weapon, rangeBonus,
                preferred && forceFire)) continue;
        if (preferred && !distributed) return &candidate;
        eligible.push_back(&candidate);
        const double distance = runtimeWeaponDistanceToTarget(state, pose, candidate, *weapon);
        if (distance < nearestDistance || (distance == nearestDistance
            && (nearest == nullptr || candidate.instanceId < nearest->instanceId))) {
            nearestDistance = distance;
            nearest = &candidate;
        }
    }
    if (!distributed || eligible.empty()) return nearest;
    std::sort(eligible.begin(), eligible.end(), [](const auto* left, const auto* right) {
        return left->instanceId < right->instanceId;
    });
    for (const auto* candidate : eligible) if (candidate->instanceId > previous) return candidate;
    return eligible.front();
}

int fireRuntimeCombatRound(RuntimeWindowState& state, const WorldObjectInstance& pose,
    const WorldObjectInstance& target, const WeaponType& weapon,
    const RuntimeWeaponProfile& profile, double multiplier, int sourceAnchorId,
    bool forceFire) {
    const auto previousCursor = state.combatDetails.fireCursors.find(pose.instanceId);
    if (profile.warhead != nullptr && profile.warhead->temporal
        && state.specialMechanismsInitialized && previousCursor != state.combatDetails.fireCursors.end()
        && previousCursor->second.lastTargetObjectId >= 0
        && previousCursor->second.lastTargetObjectId != target.instanceId) {
        state.specialMechanisms.clearTemporalAttacker(pose.instanceId,
            previousCursor->second.lastTargetObjectId, state.tick,
            runtimeSpecialEventSink(state), "open topped temporal target changed");
    }
    const int damage = weapon.areaFire
        ? static_cast<int>(std::lround(std::clamp(static_cast<double>(weapon.damage) * multiplier,
            -static_cast<double>(std::numeric_limits<int>::max()),
            static_cast<double>(std::numeric_limits<int>::max()))))
        : runtimeAuthoredDamageAgainst(state, target, weapon.damage,
            weapon.warhead, 0.0, multiplier);
    const bool anchoredBeam = weapon.laser || weapon.electricBolt
        || (profile.warhead != nullptr && profile.warhead->temporal);
    RuntimeWeaponProfile firingProfile = profile;
    // A loading/unloading mission is not proof that the shooter is aboard.
    // The firing scheduler provides the actual passenger host for this round.
    firingProfile.openTransport = sourceAnchorId >= 0 && sourceAnchorId != pose.instanceId;
    spawnRuntimeProjectile(state, pose, target, weapon, firingProfile, damage,
        -1, 0, false, anchoredBeam ? sourceAnchorId : -1, multiplier,
        0, 0, 0, true, nullptr, -1, 0, 0, forceFire);
    if (!firingProfile.openTransport && pose.kind == WorldObjectKind::Infantry) {
        if (auto* actual = findMutableObjectById(state, pose.instanceId);
                actual && actual->alive && !actual->renderMoveActive) {
            const auto next = runtimeWeaponFiringPose(state, *actual, firingProfile).mission;
            const auto previous = actual->mission;
            actual->mission = next;
            recordRuntimeMissionTransition(state, *actual, previous, next, "weaponFiringPose");
        }
    }
    const UnitType* unit = findRuntimeUnitType(state.rules, pose);
    if (unit && unit->organic && unit->naval && unit->navalTargeting == 3
        && !weapon.limboLaunch) {
        const auto* art = runtimeArtTypeForObject(state, pose);
        if (art && !art->voxel && art->firingFrames > 0) {
            if (auto* actual = findMutableObjectById(state, pose.instanceId))
                actual->spriteFiringStartTick = state.tick;
        }
    }
    const auto* lifecycle = state.rules.findWeaponLifecycleProfile(weapon.id);
    if (sourceAnchorId >= 0 || (profile.warhead && profile.warhead->temporal)
        || (unit != nullptr && unit->distributedFire)
        || (lifecycle != nullptr && lifecycle->distributedFire)) {
        state.combatDetails.fireCursors[pose.instanceId].lastTargetObjectId = std::max(-1, target.instanceId);
        state.combatDetails.fireCursors[pose.instanceId].sourceAnchorObjectId = sourceAnchorId;
    }
    return 1;
}

int runtimeNextBurstDelayTicks(const RuntimeWindowState& state, const WorldObjectInstance& attacker,
    const WeaponType& weapon, int shotIndex) {
    const auto seed = runtimeCombatRandomMix(static_cast<std::uint32_t>(state.nextProjectileActorId)
        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(attacker.instanceId)) << 32U)
        ^ runtimeCombatRandomMix(static_cast<std::uint64_t>(state.config.simulationRandomSeed))
        ^ static_cast<std::uint32_t>(shotIndex) ^ 0x4255525354ULL);
    const int fallback = weapon.burstDelaySpecified ? weapon.burstDelay : 3 + static_cast<int>(seed % 3);
    return runtimeCombatBurstDelayTicks(fallback, weapon.burstDelays, shotIndex, state.config.fixedTickHz);
}

int beginRuntimeCombatBurst(RuntimeWindowState& state, const WorldObjectInstance& attacker,
    const WorldObjectInstance& target, const WeaponType& weapon,
    const RuntimeWeaponProfile& profile, double multiplier, int sourceAnchorId = -1,
    bool forceFire = false, bool independentFire = false) {
    if (weapon.drainWeapon || (sourceAnchorId >= 0 && sourceAnchorId != attacker.instanceId
            && !weapon.fireInTransport)) return 0;
    if (state.combatDetails.hasPendingBurst(attacker.instanceId)) return 0;
    if (!std::isfinite(multiplier)) multiplier = 1.0;
    multiplier = std::clamp(multiplier, 0.0, 1000000.0);
    fireRuntimeCombatRound(state, attacker, target, weapon, profile, multiplier, sourceAnchorId, forceFire);
    const int count = std::clamp(weapon.burst, 1, 256);
    if (count > 1) {
        RuntimePendingWeaponBurst burst;
        burst.attackerObjectId = attacker.instanceId;
        burst.sourceAnchorObjectId = sourceAnchorId;
        burst.targetObjectId = target.instanceId >= 0 ? target.instanceId : (target.type == "BRIDGE" ? -2 : -1);
        burst.targetX = target.x; burst.targetY = target.y; burst.targetLevel = target.level;
        burst.targetKind = static_cast<int>(target.kind);
        burst.weaponSlot = static_cast<int>(profile.slot);
        burst.occupantMuzzleIndex = profile.occupantMuzzleIndex;
        burst.roundsRemaining = count - 1;
        burst.nextFireTick = static_cast<int>(std::min<std::int64_t>(
            std::numeric_limits<int>::max(), static_cast<std::int64_t>(state.tick)
                + runtimeNextBurstDelayTicks(state, attacker, weapon, 1)));
        burst.forceFire = forceFire; burst.independentFire = independentFire;
        burst.outgoingDamageMultiplier = multiplier;
        burst.owner = attacker.owner; burst.sourceType = attacker.type; burst.weaponId = weapon.id;
        if (RuntimeCombatDetailsState::validBurst(burst))
            state.combatDetails.bursts.emplace(attacker.instanceId, std::move(burst));
    }
    return 1;
}

bool advanceRuntimeCombatBursts(RuntimeWindowState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeCombatBursts", state.tick);
    bool changed = false;
    if (!state.combatDetails.legacyProjectileRounds.empty()) {
        std::set<int> activeActors;
        for (const auto& projectile : state.projectiles) activeActors.insert(projectile.actorId);
        for (auto it = state.combatDetails.legacyProjectileRounds.begin();
            it != state.combatDetails.legacyProjectileRounds.end();) {
            if (!activeActors.contains(it->first)) {
                it = state.combatDetails.legacyProjectileRounds.erase(it); changed = true;
            } else ++it;
        }
    }
    for (auto it = state.combatDetails.bursts.begin(); it != state.combatDetails.bursts.end();) {
        auto& burst = it->second;
        WorldObjectInstance* shooter = findMutableRuntimeObjectByIdIncludingInactive(state, burst.attackerObjectId);
        WorldObjectInstance* host = burst.sourceAnchorObjectId < 0 ? nullptr
            : findMutableObjectById(state, burst.sourceAnchorObjectId);
        const bool isPassenger = shooter != nullptr && host != nullptr && runtimeCombatValidPassenger(*shooter, *host);
        const bool isGarrison = shooter != nullptr && host != nullptr
            && shooter->instanceId == host->instanceId && !host->garrisonOccupantIds.empty();
        const auto behavior = state.behaviors.find(burst.attackerObjectId);
        const bool cancelledCommand = !burst.independentFire && burst.sourceAnchorObjectId < 0
            && behavior != state.behaviors.end() && behavior->second.kind != RuntimeBehaviorKind::Attack;
        const WeaponType* weapon = findRuntimeWeaponType(state.rules, burst.weaponId);
        if (shooter == nullptr || weapon == nullptr || shooter->owner != burst.owner
            || shooter->type != burst.sourceType || shooter->health <= 0
            || (burst.sourceAnchorObjectId >= 0 && !isPassenger && !isGarrison)
            || (isPassenger && !weapon->fireInTransport)
            || (burst.sourceAnchorObjectId < 0 && !shooter->alive) || cancelledCommand) {
            it = state.combatDetails.bursts.erase(it); changed = true; continue;
        }
        if (!runtimeCombatCanAct(state, host == nullptr ? *shooter : *host)
            || !runtimeCombatCanAct(state, *shooter)) { ++it; continue; }
        if (state.tick < burst.nextFireTick) { ++it; continue; }
        WorldObjectInstance pose = runtimeCombatFiringPose(*shooter, isPassenger ? host : nullptr);
        const UnitType* unit = findRuntimeUnitType(state.rules, pose);
        const auto* lifecycle = state.rules.findWeaponLifecycleProfile(weapon->id);
        const bool distributed = (unit != nullptr && unit->distributedFire)
            || (lifecycle != nullptr && lifecycle->distributedFire);
        const double bonus = isPassenger ? state.rules.getCombatDetailRules().openToppedRangeBonus : 0.0;
        std::optional<WorldObjectInstance> terrain;
        const WorldObjectInstance* target = nullptr;
        if (burst.targetObjectId < 0) {
            terrain = burst.targetObjectId == -2
                ? runtimeBridgeAttackTargetObject(state, {burst.targetX, burst.targetY})
                : runtimeGroundAttackTargetObject(state, {burst.targetX, burst.targetY});
            target = terrain.has_value() ? &*terrain : nullptr;
        } else if (distributed) {
            target = selectRuntimeCombatOpportunityTarget(state, pose, weapon, bonus, true);
        } else {
            target = findObjectById(state, burst.targetObjectId);
            if (target != nullptr && !runtimeCombatTargetInRange(state, pose, *target, *weapon, bonus, burst.forceFire))
                target = nullptr;
        }
        if (target == nullptr) {
            it = state.combatDetails.bursts.erase(it); changed = true; continue;
        }
        RuntimeWeaponProfile profile;
        finalizeRuntimeWeaponProfile(profile, state, static_cast<RuntimeWeaponSlot>(burst.weaponSlot), *weapon, "burst continuation");
        profile.occupied = isGarrison;
        // Schema 1 did not save a window. Its already-advanced host cursor is
        // the best available source for an old pending burst.
        profile.occupantMuzzleIndex = burst.occupantMuzzleIndex;
        if (isGarrison && profile.occupantMuzzleIndex < 0) {
            const int count = static_cast<int>(host->garrisonOccupantIds.size());
            profile.occupantMuzzleIndex = (host->garrisonFireCursor + count - 1) % count;
        }
        if (profile.slot == RuntimeWeaponSlot::Indexed || profile.slot == RuntimeWeaponSlot::EliteIndexed) {
            // Keep the purchased round's slot even when the gattling stage
            // advances between its two shots. FLH lookup needs that index.
            const auto& cached = runtimeCachedCapabilityProfileForObject(state, pose);
            const auto& candidates = profile.slot == RuntimeWeaponSlot::EliteIndexed
                ? cached.eliteIndexedWeaponCandidates : cached.indexedWeaponCandidates;
            for (std::size_t index = 0; index < candidates.size(); ++index)
                if (toLowerAsciiLocal(candidates[index].second) == toLowerAsciiLocal(weapon->id)) {
                    profile.indexedWeaponIndex = static_cast<int>(index);
                    break;
                }
        }
        fireRuntimeCombatRound(state, pose, *target, *weapon, profile, burst.outgoingDamageMultiplier,
            burst.sourceAnchorObjectId, burst.forceFire);
        triggerRuntimeWeaponVisuals(state, host == nullptr ? *shooter : *host);
        ++state.actionSummary.attacksThisTick;
        changed = true;
        --burst.roundsRemaining;
        if (burst.roundsRemaining <= 0) {
            // A complete volley starts ROF at its last actual shot. Garrison
            // host cadence is owned separately by RuntimeGarrisonSystem.
            if (!isGarrison) {
                shooter->weaponCooldownTicksRemaining = runtimeWeaponCooldownTicks(state, *shooter, *weapon);
                if (auto order = state.behaviors.find(shooter->instanceId); order != state.behaviors.end())
                    order->second.weaponCooldownTicks = shooter->weaponCooldownTicksRemaining;
            }
            it = state.combatDetails.bursts.erase(it); continue;
        }
        ++burst.nextShotIndex;
        burst.nextFireTick = static_cast<int>(std::min<std::int64_t>(std::numeric_limits<int>::max(),
            static_cast<std::int64_t>(state.tick) + runtimeNextBurstDelayTicks(state, pose, *weapon, burst.nextShotIndex)));
        ++it;
    }
    for (auto it = state.combatDetails.fireCursors.begin(); it != state.combatDetails.fireCursors.end();) {
        const auto* object = findRuntimeObjectByIdIncludingInactive(state, it->first);
        auto& cursor = it->second;
        const auto* anchor = cursor.sourceAnchorObjectId < 0 ? nullptr
            : findObjectById(state, cursor.sourceAnchorObjectId);
        const auto* target = cursor.lastTargetObjectId < 0 ? nullptr
            : findObjectById(state, cursor.lastTargetObjectId);
        const bool invalidPassengerAnchor = cursor.sourceAnchorObjectId >= 0
            && cursor.sourceAnchorObjectId != it->first
            && (object == nullptr || anchor == nullptr || !runtimeCombatValidPassenger(*object, *anchor));
        const bool temporalOutOfRange = anchor != nullptr && object != nullptr
            && anchor->instanceId != object->instanceId && (target == nullptr
                || runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(*anchor), *target)
                    > state.rules.getCombatDetailRules().openToppedWarpDistance
                || !runtimeObjectVisibleToOwner(state, object->owner, *target));
        if ((invalidPassengerAnchor || temporalOutOfRange) && state.specialMechanismsInitialized
            && cursor.lastTargetObjectId >= 0) {
            state.specialMechanisms.clearTemporalAttacker(it->first, cursor.lastTargetObjectId, state.tick,
                runtimeSpecialEventSink(state), "open topped source moved out of range or left transport");
        }
        if (object == nullptr || object->health <= 0 || invalidPassengerAnchor) {
            it = state.combatDetails.fireCursors.erase(it); changed = true;
        } else ++it;
    }
    return changed;
}

bool fireRuntimeIndependentCombatWeapon(RuntimeWindowState& state,
    WorldObjectInstance& shooter, const WorldObjectInstance* host,
    const WorldObjectInstance& target, const RuntimeWeaponProfile& profile, bool forceFire = false,
    RuntimeCombatOpportunityCandidates* candidateIndex = nullptr) {
    const WeaponType* weapon = profile.weapon;
    if (profile.warhead && profile.warhead->temporal
        && state.specialMechanisms.hasTemporalAttacker(shooter.instanceId, target.instanceId)) return false;
    if (weapon == nullptr || weapon->spawner || weapon->limboLaunch || weapon->suicide
        || weapon->neverUse || weapon->areaFire || weapon->damage <= 0
        || (host != nullptr && !weapon->fireInTransport)
        || (profile.warhead != nullptr && profile.warhead->parasite)
        || shooter.weaponCooldownTicksRemaining > 0
        || state.combatDetails.hasPendingBurst(shooter.instanceId)) return false;
    const UnitType* ammo = runtimeAmmoRulesForObject(state.rules, shooter);
    if (ammo != nullptr && ammo->ammo >= 0 && shooter.currentAmmo <= 0) return false;
    WorldObjectInstance pose = runtimeCombatFiringPose(shooter, host);
    const auto* lifecycle = state.rules.findWeaponLifecycleProfile(weapon->id);
    if (host == nullptr && (lifecycle == nullptr || !lifecycle->omniFire)) {
        // Opportunity fire may turn a turret, but never turn a moving hull
        // away from its path. Infantry aboard a transport have free aim.
        if (runtimeObjectHasTurret(state, shooter)) {
            if (!updateRuntimeWeaponFacingToward(state, shooter, target, forceFire)) return false;
            pose.turretFacing = shooter.turretFacing;
        } else {
            const int facing = runtimeFacingFromDelta(target.x - pose.x, target.y - pose.y);
            if (runtimeFacingAbsDifference(pose.facing, facing) > 8) return false;
        }
    }
    const double multiplier = runtimeVeterancyAbilityMultiplier(state, shooter, VeteranAbility::Firepower)
        * (host != nullptr ? state.rules.getCombatDetailRules().openToppedDamageMultiplier : 1.0);
    // Aiming-only attempts change facing, not indexed geometry. Revoke the
    // snapshot before the first round can cause damage, warps or world actions.
    if (candidateIndex != nullptr) candidateIndex->invalidateForWorldAction();
    if (beginRuntimeCombatBurst(state, pose, target, *weapon, profile, multiplier,
            host == nullptr ? -1 : host->instanceId, forceFire, true) == 0) return false;
    if (ammo != nullptr && ammo->ammo >= 0) shooter.currentAmmo = std::max(0, shooter.currentAmmo - 1);
    shooter.weaponCooldownTicksRemaining = runtimeWeaponCooldownTicks(state, shooter, *weapon);
    if (weapon->decloakToFire) {
        runtimeDecloakObject(state, shooter, "fire");
        if (host != nullptr) {
            if (auto* mutableHost = findMutableObjectById(state, host->instanceId))
                runtimeDecloakObject(state, *mutableHost, "passenger fire");
        }
    }
    ensureRuntimeSpecialMechanismsInitialized(state);
    // Camouflage belongs to an active object on the map. A hidden passenger
    // must not create an entry that reconciliation rejects when loading a save.
    if (host == nullptr)
        state.specialMechanisms.notifyActivity(shooter.instanceId, state.tick, "fire",
            weapon->disguiseFakeBlinkTime > 0 ? weapon->disguiseFakeBlinkTime : -1);
    ensureRuntimeSpiesInitialized(state);
    const auto fired = state.spies.notifyFired(makeRuntimeSpyContext(state), shooter.instanceId, state.tick);
    (void)applyRuntimeSpyActions(state, fired.actions);
    triggerRuntimeWeaponVisuals(state, shooter);
    ++state.actionSummary.attacksThisTick;
    return true;
}

bool advanceRuntimeCombatIndependentFire(RuntimeWindowState& state) {
    RuntimePerfTraceScope stageTrace("advanceRuntimeCombatIndependentFire", state.tick);
    bool changed = false;
    RuntimeCombatOpportunityOwnerSummary ownerSummary;
    RuntimeCombatOpportunityCandidates candidateIndex;
    // Stable order matters for distributed fire and projectile actor IDs.
    std::vector<int> sourceIds;
    for (const auto& object : state.world.getObjects()) {
        if (!object.alive) continue;
        const UnitType* unit = findRuntimeUnitType(state.rules, object);
        if (unit != nullptr && (unit->openTopped || unit->opportunityFire)) sourceIds.push_back(object.instanceId);
    }
    std::sort(sourceIds.begin(), sourceIds.end());
    for (int sourceId : sourceIds) {
        auto* source = findMutableObjectById(state, sourceId);
        if (source == nullptr || !runtimeCombatCanAct(state, *source)) continue;
        const UnitType* unit = findRuntimeUnitType(state.rules, *source);
        if (unit == nullptr) continue;
        const auto behavior = state.behaviors.find(sourceId);
        const bool moving = behavior != state.behaviors.end() && behavior->second.kind == RuntimeBehaviorKind::Move;
        const bool attacking = behavior != state.behaviors.end() && behavior->second.kind == RuntimeBehaviorKind::Attack;
        const int preferred = attacking ? behavior->second.targetObjectId : -1;
        if (unit->opportunityFire && moving && source->weaponCooldownTicksRemaining <= 0
            && !unit->noMovingFire && !state.combatDetails.hasPendingBurst(sourceId)) {
            const auto* target = selectRuntimeCombatOpportunityTarget(state, *source, nullptr, 0.0, unit->distributedFire, -1, false, false, false, &ownerSummary, &candidateIndex);
            if (target != nullptr) {
                const auto profile = runtimeWeaponProfileForObject(state, *source, target);
                ownerSummary.invalidate();
                changed = fireRuntimeIndependentCombatWeapon(state, *source, nullptr, *target, profile, false, &candidateIndex) || changed;
            }
        }
        if (!unit->openTopped || source->transportPassengerIds.empty()) continue;
        const auto passengerIds = source->transportPassengerIds;
        for (int passengerId : passengerIds) {
            source = findMutableObjectById(state, sourceId);
            auto* passenger = findMutableRuntimeObjectByIdIncludingInactive(state, passengerId);
            if (source == nullptr || passenger == nullptr || !runtimeCombatValidPassenger(*passenger, *source)
                || !runtimeCombatCanAct(state, *passenger) || passenger->weaponCooldownTicksRemaining > 0
                || state.combatDetails.hasPendingBurst(passengerId)) continue;
            const auto pose = runtimeCombatFiringPose(*passenger, source);
            if (attacking && behavior->second.attackGround) {
                // A forced cell is not in world.getObjects(). Forward it to
                // each passenger before ordinary independent target search.
                const auto ground = runtimeGroundAttackTargetObject(state, behavior->second.attackBridgeCell);
                if (ground) {
                    const auto groundProfile = runtimeCombatPassengerWeaponProfile(state, pose, &*ground, true);
                    if (groundProfile.weapon && runtimeCombatTargetInRange(state, pose, *ground,
                            *groundProfile.weapon, state.rules.getCombatDetailRules().openToppedRangeBonus, true)) {
                        ownerSummary.invalidate();
                        changed = fireRuntimeIndependentCombatWeapon(state, *passenger, source,
                            *ground, groundProfile, true, &candidateIndex) || changed;
                    }
                }
                continue;
            }
            const auto profile = runtimeCombatPassengerWeaponProfile(state, pose, nullptr);
            const UnitType* passengerType = findRuntimeUnitType(state.rules, pose);
            const bool fixedSlot = passengerType != nullptr && passengerType->openTransportWeapon >= 0;
            if (fixedSlot && profile.weapon == nullptr) continue;
            const WeaponType* fixed = fixedSlot ? profile.weapon : nullptr;
            const auto* target = selectRuntimeCombatOpportunityTarget(state, pose, fixed,
                state.rules.getCombatDetailRules().openToppedRangeBonus, false, preferred,
                attacking && behavior->second.forceFire, true, false, &ownerSummary, &candidateIndex);
            if (target == nullptr) continue;
            const auto targetProfile = fixed == nullptr ? runtimeCombatPassengerWeaponProfile(state, pose, target,
                attacking && behavior->second.forceFire && target->instanceId == preferred) : profile;
            ownerSummary.invalidate();
            changed = fireRuntimeIndependentCombatWeapon(state, *passenger, source, *target, targetProfile,
                attacking && behavior->second.forceFire && target->instanceId == preferred, &candidateIndex) || changed;
        }
    }
    return changed;
}
