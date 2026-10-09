#pragma once
namespace RuntimeSimulationStageAdmissionContractDetail {
std::unordered_map<int, int> legacyOccupancy(
    const RuntimeWindowState& state,
    int ignoredObjectId = -1,
    RuntimeNavigationStats* stats = nullptr,
    const std::unordered_set<int>* ignoredObjectIds = nullptr,
    bool includeStructures = true,
    std::unordered_map<int, int>* occupantCountByCell = nullptr,
    RuntimeStructureOccupancyMode structureMode =
        RuntimeStructureOccupancyMode::FullFoundation
) {
    RuntimePerfTraceScope trace("buildRuntimeOccupancyLookup", state.tick);
    std::unordered_map<int, int> occupiedByCell;
    occupiedByCell.reserve(
        (state.world.getObjects().size() * 4)
            + state.world.getTerrainObjects().size()
    );
    if (occupantCountByCell != nullptr) {
        occupantCountByCell->clear();
        occupantCountByCell->reserve(
            (state.world.getObjects().size() * 4)
                + state.world.getTerrainObjects().size()
        );
    }
    int structureFootprintCells = 0;
    // Terrain objects are inserted first so an authored unit/tree overlap
    // keeps the indestructible navigation representative until the terrain
    // object actually dies.  Runtime movement is currently cell-granular;
    // any occupied infantry subcell therefore blocks the whole cell for a
    // non-air mover.
    for (std::size_t index = 0;
        index < state.world.getTerrainObjects().size();
        ++index) {
        const WorldTerrainObject& object =
            state.world.getTerrainObjects()[index];
        if (!runtimeTerrainObjectBlocksGround(object)) {
            continue;
        }
        for (const RuntimeCellPoint& cell :
                runtimeTerrainObjectFootprintCells(object)) {
            const int key = runtimeCellKey(cell.x, cell.y);
            if (key < 0) {
                continue;
            }
            occupiedByCell.emplace(key, runtimeTerrainOccupantId(index));
            if (occupantCountByCell != nullptr) {
                ++(*occupantCountByCell)[key];
            }
        }
    }
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.instanceId == ignoredObjectId
            || runtimeObjectOffGroundByMagBeam(state, object.instanceId)
            || (!includeStructures
                && object.kind == WorldObjectKind::Structure)
            || (ignoredObjectIds != nullptr
                && ignoredObjectIds->find(object.instanceId)
                    != ignoredObjectIds->end())) {
            continue;
        }

        // Aircraft occupy the air domain rather than a ground navigation
        // cell. Parked planes are already covered by their airfield's
        // foundation, and airborne planes must never block land movement.
        if (object.kind != WorldObjectKind::Structure
            && runtimeObjectMovementMode(state, object)
                == PathMovementMode::Air
            && (object.airborne || object.dockedToObjectId >= 0)) {
            continue;
        }

        const std::vector<RuntimeCellPoint> cells =
            object.kind == WorldObjectKind::Structure
                && structureMode
                    == RuntimeStructureOccupancyMode::VehicleNavigation
            ? runtimeStructureNavigationBlockerCellsForObject(state, object)
            : runtimeStructureFootprintCellsForObject(state, object);
        for (const RuntimeCellPoint& cell : cells) {
            const int key = runtimeCellKey(cell.x, cell.y);
            if (key < 0) {
                continue;
            }
            occupiedByCell.emplace(key, object.instanceId);
            if (occupantCountByCell != nullptr) {
                ++(*occupantCountByCell)[key];
            }
            if (object.kind == WorldObjectKind::Structure) {
                ++structureFootprintCells;
            }
        }
    }

    if (stats != nullptr) {
        stats->occupiedCells = static_cast<int>(occupiedByCell.size());
        stats->structureFootprintCells = structureFootprintCells;
    }
    return occupiedByCell;
}

bool legacyCloak(RuntimeWindowState& state) {
    RuntimePerfTraceScope trace("advanceRuntimeCloakState", state.tick);
    std::vector<const WorldObjectInstance*> detectors;
    detectors.reserve(state.world.getObjects().size());
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (object.alive && runtimeObjectSensorsSight(state, object) > 0) {
            detectors.push_back(&object);
        }
    }

    bool changed = false;
    std::unordered_set<int> aliveObjectIds;
    aliveObjectIds.reserve(state.world.getObjects().size());
    for (WorldObjectInstance& object : state.world.accessObjects()) {
        if (!object.alive) {
            continue;
        }
        aliveObjectIds.insert(object.instanceId);
        if (runtimeObjectSuspendedByMagBeam(state, object.instanceId)) {
            // A suspended submarine is physically out of the water. Freeze
            // its dive/cloak timers until the temporary locomotor releases it.
            changed = runtimeSetObjectSubmerged(state, object, false, "magBeamSuspended") || changed;
            changed = runtimeSetObjectCloaked(state, object, false, "magBeamSuspended") || changed;
            state.cloakObservedHealthByObject[object.instanceId] = object.health;
            continue;
        }
        const auto healthIt = state.cloakObservedHealthByObject.find(
            object.instanceId
        );
        const bool damagedSinceLastTick =
            healthIt != state.cloakObservedHealthByObject.end()
            && object.health < healthIt->second;

        if (damagedSinceLastTick) {
            changed = runtimeRevealObjectAfterDamage(
                state,
                object,
                "damageObserved"
            ) || changed;
            state.cloakObservedHealthByObject[object.instanceId] =
                object.health;
            continue;
        }

        const UnitType* unit = findRuntimeUnitType(state.rules, object);
        const bool underwaterType = unit != nullptr && unit->underwater;
        const bool temporallyFrozen = state.specialMechanismsInitialized
            && state.specialMechanisms.isTemporalFrozen(object.instanceId);
        const bool detected = runtimeObjectCanCloak(state, object)
            && runtimeCloakSuppressedBySensors(state, object, detectors);
        if (detected && runtimeObjectUsesSubmarineReveal(state, object)) {
            changed = runtimeRevealSubmarineForActivity(state, object, "submarineSensors") || changed;
            state.cloakObservedHealthByObject[object.instanceId] = object.health;
            continue;
        }
        if (underwaterType && !object.submerged) {
            const auto behaviorIt = state.behaviors.find(object.instanceId);
            const bool stationaryAttack =
                behaviorIt != state.behaviors.end()
                && behaviorIt->second.kind == RuntimeBehaviorKind::Attack
                && behaviorIt->second.state != RuntimeBehaviorState::Chasing
                && behaviorIt->second.state != RuntimeBehaviorState::Complete
                && !object.renderMoveActive;
            if (stationaryAttack) {
                object.submergeCooldownTicks = std::max(
                    object.submergeCooldownTicks,
                    std::max(1, state.config.fixedTickHz) * 5
                );
            } else if (object.submergeCooldownTicks > 0) {
                --object.submergeCooldownTicks;
            }
            if (object.submergeCooldownTicks <= 0 && !temporallyFrozen) {
                changed = runtimeSetObjectSubmerged(
                    state,
                    object,
                    true,
                    "submergeDelayElapsed"
                ) || changed;
            }
        } else if (!underwaterType) {
            object.submergeCooldownTicks = 0;
        }

        if (!runtimeObjectCanCloak(state, object)) {
            if (object.cloaked) {
                changed = runtimeSetObjectCloaked(
                    state,
                    object,
                    false,
                    "cloakCapabilityLost"
                ) || changed;
            }
            object.cloakCooldownTicks = 0;
            state.cloakObservedHealthByObject[object.instanceId] =
                object.health;
            continue;
        }


        if (detected) {
            changed = runtimeDecloakObject(
                state,
                object,
                "sensors"
            ) || changed;
            state.cloakObservedHealthByObject[object.instanceId] =
                object.health;
            continue;
        }

        if (object.cloakCooldownTicks > 0) {
            --object.cloakCooldownTicks;
        }
        const bool mayCloak = object.cloakCooldownTicks <= 0
            && (!underwaterType || object.submerged)
            && !temporallyFrozen;
        if (mayCloak && !object.cloaked) {
            changed = runtimeSetObjectCloaked(
                state,
                object,
                true,
                "cloakDelayElapsed"
            ) || changed;
        }
        state.cloakObservedHealthByObject[object.instanceId] = object.health;
    }

    for (auto it = state.cloakObservedHealthByObject.begin();
        it != state.cloakObservedHealthByObject.end();) {
        if (aliveObjectIds.find(it->first) == aliveObjectIds.end()) {
            it = state.cloakObservedHealthByObject.erase(it);
        } else {
            ++it;
        }
    }
    return changed;
}
}
