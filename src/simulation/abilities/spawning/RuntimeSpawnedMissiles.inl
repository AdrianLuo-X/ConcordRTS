std::string runtimeSpawnedMissileImage(const RuntimeWindowState& state, const WorldObjectInstance& object) {
    const ArtType* art = runtimeArtTypeForObject(state, object);
    return art == nullptr ? object.type : (art->image.empty() ? art->id : art->image);
}

RuntimeProjectileSourcePosition runtimeMissileDeckPosition(
    const RuntimeWindowState& state, const WorldObjectInstance& parent, int slot) {
    const int facing = std::max(0, parent.facing);
    RuntimeWeaponProfile launcher;
    launcher.slot = RuntimeWeaponSlot::Primary;
    if (const auto* unit = findRuntimeUnitType(state.rules, parent)) {
        const auto* secondary = state.rules.findWeaponType(unit->secondaryWeapon);
        const auto* primary = state.rules.findWeaponType(unit->primaryWeapon);
        if ((primary == nullptr || !primary->spawner) && secondary != nullptr && secondary->spawner)
            launcher.slot = parent.veterancy >= 2 && !unit->eliteSecondaryWeapon.empty()
                ? RuntimeWeaponSlot::EliteSecondary : RuntimeWeaponSlot::Secondary;
        else if (parent.veterancy >= 2 && !unit->elitePrimaryWeapon.empty())
            launcher.slot = RuntimeWeaponSlot::ElitePrimary;
    }
    auto point = runtimeProjectileSourcePosition(state, parent, launcher, facing);
    const auto* art = runtimeArtTypeForObject(state, parent);
    const auto flh = runtimeWeaponFireFlh(state, parent, launcher, art);
    const auto secondOffset = art == nullptr ? std::optional<RuntimeWeaponFireFlh>{}
        : parseRuntimeWeaponFireFlh(art->secondSpawnOffset);
    if (secondOffset && slot > 0) {
        const auto forward = runtimeWorldForwardForFacing(facing);
        point.x += (forward[0] * secondOffset->forwardLeptons - forward[1] * secondOffset->lateralLeptons) / 256.0;
        point.y += (forward[1] * secondOffset->forwardLeptons + forward[0] * secondOffset->lateralLeptons) / 256.0;
        point.z += runtimeWeaponHeightInLevels(secondOffset->heightLeptons);
    } else if (flh && slot % 2 != 0) {
        const auto forward = runtimeWorldForwardForFacing(facing);
        point.x += 2.0 * forward[1] * flh->lateralLeptons / 256.0;
        point.y -= 2.0 * forward[0] * flh->lateralLeptons / 256.0;
    }
    return point;
}

#include "simulation/abilities/spawning/RuntimeSpawnedMissileMuzzle.inl"

bool launchRuntimeSpawnedMissileVolley(RuntimeWindowState& state,
    const WorldObjectInstance& parent, const WorldObjectInstance& target,
    const WeaponType& weapon, double damageMultiplier) {
    const auto* profile = runtimeSpawnedMissileProfileForWeapon(state, parent, weapon);
    if (profile == nullptr) return false;
    ensureRuntimeSpecialMechanismsInitialized(state);
    const auto parentState = state.specialMechanisms.carrierParents().find(parent.instanceId);
    if (parentState == state.specialMechanisms.carrierParents().end()) return false;
    auto& missiles = state.specialMechanisms.accessSpawnedMissiles();
    // A replenished deck slot does not finish its parent's previous volley.
    // Keep the complete burst together, and wait for every preparing/flying
    // missile to end. This saved authority also survives Stop, retargeting,
    // ownership changes and reloads without a separate cooldown flag.
    if (std::any_of(missiles.begin(), missiles.end(), [&](const auto& entry) {
            return entry.second.parentObjectId == parent.instanceId;
        })) return false;
    const auto targetPoint = runtimeProjectileTargetPosition(state, target);
    int launched = 0;
    for (const auto& [slot, id] : parentState->second.childBySlot) {
        const auto childState = state.specialMechanisms.carrierChildren().find(id);
        const auto* child = findObjectById(state, id);
        if (child == nullptr || !child->alive || child->health <= 0 || missiles.contains(id)
            || childState == state.specialMechanisms.carrierChildren().end()
            || childState->second.phase != CarrierChildPhase::Docked
            || parentState->second.slotReadyTick.at(slot) > state.tick) continue;
        const ArtType* missileArt = runtimeArtTypeForObject(state, *child);
        SpawnedMissileState m;
        m.objectId = id; m.parentObjectId = parent.instanceId; m.slot = slot;
        m.targetObjectId = target.instanceId < 0 ? -1 : target.instanceId;
        m.typeId = profile->typeId; m.parentType = parent.type;
        m.owner = parent.owner; m.weaponId = weapon.id;
        m.warheadId = parent.veterancy >= 2 ? profile->eliteWarhead : profile->warhead;
        m.damage = parent.veterancy >= 2 ? profile->eliteDamage : profile->damage;
        m.damageMultiplier = damageMultiplier;
        const int delay = launched * (missileArt == nullptr ? 0 : missileArt->spawnDelay);
        m.startTick = state.tick + (delay <= 0 ? 0 : runtimeAuthoredFramesToFixedTicks(state, delay));
        m.releaseTick = m.startTick + runtimeAuthoredFramesToFixedTicks(state,
            profile->pauseFrames + profile->tiltFrames);
        m.lastTick = state.tick;
        const auto point = runtimeMissileDeckPosition(state, parent, slot);
        m.x = m.originX = point.x; m.y = m.originY = point.y; m.z = m.originZ = point.z;
        const auto visual = runtimeMissileDeckPresentationOffset(state, parent, slot);
        m.originScreenOffsetX = visual.x; m.originScreenOffsetY = visual.y;
        m.originNativeFixedX = visual.fixedX; m.originNativeFixedY = visual.fixedY;
        m.targetX = targetPoint.x; m.targetY = targetPoint.y; m.targetZ = targetPoint.level;
        m.pitch = profile->pitchInitial * runtimeMissileHalfPi;
        const auto forward = runtimeWorldForwardForFacing(std::max(0, parent.facing));
        m.heading = std::atan2(forward[1], forward[0]);
        const auto [missileIt, inserted] = missiles.emplace(id, std::move(m));
        if (!inserted) continue;
        if (auto* launchedChild = findMutableObjectById(state, id)) {
            applyRuntimeMissilePose(*launchedChild, missileIt->second);
        }
        ++launched;
        if (launched >= std::max(1, weapon.burst)) break;
    }
    if (launched > 0) notifyRuntimeWeaponFired(state, parent, target, weapon);
    return launched > 0;
}

void applyRuntimeMissilePose(WorldObjectInstance& object, const SpawnedMissileState& m) {
    object.x = static_cast<int>(std::lround(m.x));
    object.y = static_cast<int>(std::lround(m.y));
    object.cell = runtimeCellKey(object.x, object.y);
    object.hasRenderPosition = true;
    object.renderMoveActive = true;
    object.renderX = m.x; object.renderY = m.y; object.renderLevel = m.z;
    object.flightLevel = static_cast<int>(std::lround(m.z));
    object.airborne = m.released;
    if (m.released) {
        object.level = object.flightLevel;
        object.onBridge = object.renderOnBridge = false;
    }
    object.facing = runtimeFacingFromDelta(static_cast<int>(std::lround(std::cos(m.heading) * 100000)),
        static_cast<int>(std::lround(std::sin(m.heading) * 100000)));
    object.turretFacing = object.facing;
    object.mission = m.released ? "MissileFlight" : "MissilePreparing";
}

void consumeRuntimeSpawnedMissile(RuntimeWindowState& state, const SpawnedMissileState& m,
    bool impact) {
    // Copy/remove before running object exits or warheads: both may mutate
    // the graph and destroy other missiles in this volley.
    state.specialMechanisms.accessSpawnedMissiles().erase(m.objectId);
    RuntimeObjectExitRequest exit;
    exit.objectId = m.objectId; exit.kind = RuntimeObjectExitKind::Consumed;
    exit.tick = state.tick; exit.mission = "MissileConsumed";
    exit.reason = impact ? "missile impact" : "missile flight expired";
    exit.sourceObjectId = m.parentObjectId; exit.sourceOwner = m.owner;
    exit.suppressDeathWeapon = true;
    if (queueRuntimeObjectExit(state, std::move(exit)) == RuntimeObjectExitQueueResult::Queued)
        (void)drainRuntimeObjectExitQueue(state);
    if (!impact) return;
    const auto* warhead = findRuntimeWarheadType(state.rules, m.warheadId);
    const auto* weapon = findRuntimeWeaponType(state.rules, m.weaponId);
    if (warhead == nullptr || weapon == nullptr) return;
    RuntimeProjectileEffect hit;
    hit.actorId = nextRuntimeProjectileActorId(state);
    hit.attackerObjectId = m.parentObjectId; hit.owner = m.owner;
    hit.launchSourceType = m.parentType;
    hit.launchSourceKind = static_cast<int>(WorldObjectKind::Unit);
    hit.launchSourceValidated = true;
    hit.targetObjectId = m.targetObjectId;
    hit.weaponId = m.weaponId; hit.projectileImageId = m.typeId;
    hit.warheadId = m.warheadId; hit.warheadImpactIntentId = m.warheadId;
    hit.hasSourcePosition = hit.hasTargetPosition = true;
    hit.sourceX = m.originX; hit.sourceY = m.originY; hit.sourceZ = m.originZ;
    hit.targetX = hit.currentX = m.targetX;
    hit.targetY = hit.currentY = m.targetY;
    hit.targetZ = hit.currentZ = m.targetZ;
    hit.fromCell = {static_cast<int>(std::lround(m.originX)), static_cast<int>(std::lround(m.originY))};
    hit.toCell = hit.currentCell = {static_cast<int>(std::lround(m.targetX)), static_cast<int>(std::lround(m.targetY))};
    hit.fromLevel = static_cast<int>(std::lround(m.originZ));
    hit.toLevel = hit.currentLevel = static_cast<int>(std::lround(m.targetZ));
    hit.spawnTick = m.releaseTick; hit.impactTick = state.tick;
    hit.damage = static_cast<int>(std::clamp(std::round(m.damage * m.damageMultiplier),
        0.0, static_cast<double>(std::numeric_limits<int>::max())));
    hit.spawnedMissileDamage = true; hit.spawnedMissileBaseDamage = m.damage;
    hit.veteranDamageMultiplier = m.damageMultiplier;
    hit.aa = false; hit.ag = true;
    hit.cellSpread = warhead->cellSpread; hit.percentAtMax = warhead->percentAtMax;
    hit.affectsAllies = warhead->affectsAllies; hit.affectsEnemies = warhead->affectsEnemies;
    hit.impactAnimId = chooseRuntimeImpactAnim(warhead, *weapon);
    hit.bright = warhead->bright; hit.particleId = warhead->particle;
    hit.renderIntent = "spawnedMissileImpact";
    (void)applyRuntimeProjectileImpact(state, hit);
}

bool advanceRuntimeSpawnedMissiles(RuntimeWindowState& state) {
    std::vector<int> ids;
    for (const auto& [id, m] : state.specialMechanisms.spawnedMissiles()) ids.push_back(id);
    bool changed = false;
    for (const int id : ids) {
        auto& missiles = state.specialMechanisms.accessSpawnedMissiles();
        const auto found = missiles.find(id);
        if (found == missiles.end()) continue;
        SpawnedMissileState m = found->second;
        WorldObjectInstance* object = findMutableObjectById(state, id);
        const auto* profile = state.rules.findSpawnedMissileProfile(m.typeId);
        const auto* unit = state.rules.findUnitType("AircraftType", m.typeId);
        if (object == nullptr || !object->alive || object->health <= 0 || profile == nullptr || unit == nullptr) {
            missiles.erase(id);
            changed = true;
            continue;
        }
        if (!m.released) {
            const auto* parent = findObjectById(state, m.parentObjectId);
            const auto behavior = state.behaviors.find(m.parentObjectId);
            bool stopped = behavior == state.behaviors.end()
                || behavior->second.kind != RuntimeBehaviorKind::Attack
                || (m.targetObjectId >= 0 && behavior->second.targetObjectId != m.targetObjectId);
            if (!stopped && m.targetObjectId == -1) {
                std::optional<WorldObjectInstance> fixedTarget;
                if (behavior->second.attackGround) fixedTarget = runtimeAttackGroundTargetObject(state, behavior->second);
                else if (behavior->second.attackBridge)
                    fixedTarget = runtimeBridgeAttackTargetObject(state, behavior->second.attackBridgeCell);
                if (!fixedTarget) stopped = true;
                else {
                    const auto point = runtimeProjectileTargetPosition(state, *fixedTarget);
                    stopped = std::abs(point.x - m.targetX) > 1e-9
                        || std::abs(point.y - m.targetY) > 1e-9
                        || std::abs(point.level - m.targetZ) > 1e-9;
                }
            }
            if (parent == nullptr || !parent->alive || stopped) {
                missiles.erase(id);
                object->mission = "CarrierDocked";
                changed = true;
                continue;
            }
            const double elapsedFrames = std::max(0, state.tick - m.startTick)
                * 15.0 / std::max(1, state.config.fixedTickHz);
            const double tiltFrames = std::clamp(elapsedFrames - profile->pauseFrames,
                0.0, static_cast<double>(profile->tiltFrames));
            const double progress = profile->tiltFrames <= 0 ? 1.0 : tiltFrames / profile->tiltFrames;
            const double launchPitch = profile->lazyCurve ? profile->pitchFinal : 1.0;
            m.pitch = (profile->pitchInitial + (launchPitch - profile->pitchInitial) * progress)
                * runtimeMissileHalfPi;
            const auto point = runtimeMissileDeckPosition(state, *parent, m.slot);
            const auto visual = runtimeMissileDeckPresentationOffset(state, *parent, m.slot);
            m.originScreenOffsetX = visual.x; m.originScreenOffsetY = visual.y;
            m.originNativeFixedX = visual.fixedX; m.originNativeFixedY = visual.fixedY;
            m.x = m.originX = point.x; m.y = m.originY = point.y;
            m.z = m.originZ = point.z + tiltFrames * profile->raiseRate * runtimeMissileLevelsPerCell / 256.0;
            const auto forward = runtimeWorldForwardForFacing(std::max(0, parent->facing));
            m.heading = std::atan2(forward[1], forward[0]);
            m.owner = parent->owner;
            if (state.tick >= m.releaseTick) {
                if (!state.specialMechanisms.releaseCarrierMissile(id, state.tick, runtimeSpecialEventSink(state))) {
                    missiles.erase(id);
                    continue;
                }
                m.released = true;
                object->dockedToObjectId = -1; object->dockIndex = -1;
                if (!profile->launchSound.empty()) emitRuntimeSound(state, "sound:" + profile->launchSound, runtimeSpatialAudioPositionForObject(*object));
            }
        } else if (state.tick > m.lastTick) {
            if (const auto* target = findObjectById(state, m.targetObjectId); target && target->alive) {
                const auto point = runtimeProjectileTargetPosition(state, *target);
                m.targetX = point.x; m.targetY = point.y; m.targetZ = point.level;
            }
            const double frames = (state.tick - m.lastTick) * 15.0 / std::max(1, state.config.fixedTickHz);
            m.speed = std::min(static_cast<double>(std::max(1, unit->speed)),
                m.speed + profile->acceleration * frames);
            const double dx = m.targetX - m.x, dy = m.targetY - m.y;
            const double horizontal = std::hypot(dx, dy);
            const double dz = (m.targetZ - m.z) / runtimeMissileLevelsPerCell;
            const double step = m.speed * frames / 256.0;
            if (std::hypot(horizontal, dz) <= step + 0.08) {
                consumeRuntimeSpawnedMissile(state, m, true);
                changed = true;
                continue;
            }
            if (state.tick - m.releaseTick > std::max(1, state.config.fixedTickHz) * 90) {
                consumeRuntimeSpawnedMissile(state, m, false);
                changed = true;
                continue;
            }
            const double desiredHeading = std::atan2(dy, dx);
            double delta = std::remainder(desiredHeading - m.heading, 4.0 * runtimeMissileHalfPi);
            m.heading = std::remainder(m.heading + std::clamp(delta,
                -profile->turnRate * frames, profile->turnRate * frames), 4.0 * runtimeMissileHalfPi);
            const double cruiseZ = std::max(m.originZ, m.targetZ)
                + profile->altitudeLeptons * runtimeMissileLevelsPerCell / 256.0;
            if (!profile->lazyCurve && m.flightStage == 0 && m.z < cruiseZ) {
                // A launch is a genuinely vertical segment: do not start
                // steering pitch toward a point two cells ahead of the ship.
                m.pitch = runtimeMissileHalfPi;
                m.z = std::min(cruiseZ, m.z + step * runtimeMissileLevelsPerCell);
                if (m.z >= cruiseZ) m.flightStage = 1;
            } else {
                double desiredPitch = std::atan2(dz, std::max(0.0001, horizontal));
                if (profile->lazyCurve && horizontal > 1.0) {
                    // Preserve the V3's authored broad curve.
                    const double originalDistance = std::max(0.1,
                        std::hypot(m.targetX - m.originX, m.targetY - m.originY));
                    const double aimDistance = std::min(2.0, horizontal);
                    const double t = std::clamp(1.0 - (horizontal - aimDistance) / originalDistance, 0.0, 1.0);
                    const double height = profile->altitudeLeptons * runtimeMissileLevelsPerCell / 256.0;
                    const double aimZ = m.originZ + (m.targetZ - m.originZ) * t + 4.0 * height * t * (1.0 - t);
                    desiredPitch = std::atan2((aimZ - m.z) / runtimeMissileLevelsPerCell, aimDistance);
                } else if (!profile->lazyCurve) {
                    if (m.flightStage == 0) m.flightStage = 1;
                    // A quarter turn consumes approximately speed / turn-rate
                    // of horizontal distance. Begin the dive near the target,
                    // after an actual horizontal cruise instead of a broad arc.
                    const double turnRadius = (m.speed / 256.0) / std::max(0.001, profile->turnRate);
                    if (m.flightStage == 1 && horizontal <= turnRadius + step) m.flightStage = 2;
                    if (m.flightStage == 1) desiredPitch = 0.0;
                }
                m.pitch += std::clamp(desiredPitch - m.pitch,
                    -profile->turnRate * frames, profile->turnRate * frames);
                m.x += std::cos(m.heading) * std::cos(m.pitch) * step;
                m.y += std::sin(m.heading) * std::cos(m.pitch) * step;
                m.z += std::sin(m.pitch) * step * runtimeMissileLevelsPerCell;
            }
        }
        m.lastTick = state.tick;
        missiles[id] = m;
        applyRuntimeMissilePose(*object, m);
        if (m.released) {
            emitRuntimeMissileExhaust(state, "V3TRAIL", m.owner, m.parentObjectId, m.typeId,
                runtimeMissileExhaustOrigin(m.x, m.y, m.z, m.heading, m.pitch, profile->bodyLength),
                state.tick - m.releaseTick, runtimeSpawnedMissileSpriteAttachment(m));
        }
        changed = true;
    }
    return changed;
}

void resolveRuntimeSpawnedMissileRenderSnapshots(RuntimeWindowState& state) {
    for (const auto& [id, parentState] : state.specialMechanisms.carrierParents()) {
        const auto* parent = findObjectById(state, id);
        if (parent == nullptr || !parent->alive) continue;
        const bool missileSpawn =
            state.rules.findSpawnedMissileProfile(parentState.childType) != nullptr;
        const bool hasDockedChild = std::any_of(parentState.childBySlot.begin(),
            parentState.childBySlot.end(), [&state](const auto& entry) {
                const auto* child = findObjectById(state, entry.second);
                return child != nullptr && child->alive
                    && state.specialMechanisms.isCarrierChildDocked(entry.second);
            });
        const auto* rules = findRuntimeUnitType(state.rules, *parent);
        if (rules && rules->noSpawnAlt && (missileSpawn || !hasDockedChild)) {
            SpecialMechanismRenderSnapshot deck;
            deck.kind = missileSpawn ? "missileDeck" : "noSpawnDeck"; deck.objectId = id;
            state.specialMechanismRenderSnapshots.push_back(deck);
        }
        if (!missileSpawn) continue;
        // Submarine missiles stay inside their launch bays until release.
        // Exposed launch rails (V3/DRED) retain their docked missile models.
        if (rules != nullptr && rules->underwater) continue;
        for (const auto& [slot, childId] : parentState.childBySlot) {
            if (childId < 0 || state.specialMechanisms.spawnedMissiles().contains(childId)) continue;
            const auto* child = findObjectById(state, childId);
            if (child == nullptr || !child->alive || !state.specialMechanisms.isCarrierChildDocked(childId)) continue;
            const auto point = runtimeMissileDeckPosition(state, *parent, slot);
            SpecialMechanismRenderSnapshot snapshot;
            snapshot.kind = "spawnedMissile"; snapshot.objectId = childId;
            snapshot.sourceX = point.x; snapshot.sourceY = point.y; snapshot.sourceLevel = point.z;
            const auto visual = runtimeMissileDeckPresentationOffset(state, *parent, slot);
            snapshot.sourceScreenOffsetX = visual.x; snapshot.sourceScreenOffsetY = visual.y;
            snapshot.sourceNativeFixedX = visual.fixedX; snapshot.sourceNativeFixedY = visual.fixedY;
            snapshot.resourceId = runtimeSpawnedMissileImage(state, *child); snapshot.owner = parent->owner;
            snapshot.phase = "Docked"; snapshot.missileFacing = parent->facing;
            snapshot.missilePitch = state.rules.findSpawnedMissileProfile(parentState.childType)->pitchInitial * runtimeMissileHalfPi;
            state.specialMechanismRenderSnapshots.push_back(snapshot);
        }
    }
    for (const auto& [id, m] : state.specialMechanisms.spawnedMissiles()) {
        const auto* object = findObjectById(state, id);
        if (object == nullptr || !object->alive) continue;
        const auto* parentRules = findRuntimeUnitTypeById(state.rules, m.parentType);
        if (!m.released && parentRules != nullptr && parentRules->underwater) continue;
        SpecialMechanismRenderSnapshot snapshot;
        snapshot.kind = "spawnedMissile"; snapshot.objectId = id;
        snapshot.sourceX = m.x; snapshot.sourceY = m.y; snapshot.sourceLevel = m.z;
        const auto attachment = runtimeSpawnedMissileSpriteAttachment(m);
        snapshot.sourceScreenOffsetX = attachment.screenOffsetX;
        snapshot.sourceScreenOffsetY = attachment.screenOffsetY;
        snapshot.sourceNativeFixedX = attachment.nativeFixedX;
        snapshot.sourceNativeFixedY = attachment.nativeFixedY;
        snapshot.resourceId = runtimeSpawnedMissileImage(state, *object); snapshot.owner = m.owner;
        snapshot.phase = m.released ? "Flight" : "Preparing";
        snapshot.missilePitch = m.pitch;
        snapshot.missileFacing = runtimeFacingFromDelta(static_cast<int>(std::lround(std::cos(m.heading) * 100000)),
            static_cast<int>(std::lround(std::sin(m.heading) * 100000)));
        state.specialMechanismRenderSnapshots.push_back(snapshot);
    }
}
