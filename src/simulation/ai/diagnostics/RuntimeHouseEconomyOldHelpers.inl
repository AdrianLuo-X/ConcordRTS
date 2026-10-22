bool runtimeHarvesterCanUnloadAtRefinery(
    const RuntimeWindowState& state,
    const WorldObjectInstance& harvester,
    const WorldObjectInstance& refinery
) {
    if (!harvester.alive
        || !refinery.alive
        || harvester.owner != state.economy.owner
        || refinery.owner != state.economy.owner
        || refinery.kind != WorldObjectKind::Structure) {
        return false;
    }

    const UnitType* unit = findRuntimeUnitType(state.rules, harvester);
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, refinery.type);
    if (unit == nullptr
        || building == nullptr
        || !runtimeBuildingAcceptsHarvesterUnload(*building)) {
        return false;
    }

    const std::vector<std::string> dockTypes =
        splitRuntimeTokenList(unit->dock);
    if (dockTypes.empty()) {
        return true;
    }

    const std::string refineryType = toUpperAsciiLocal(refinery.type);
    for (const std::string& dockType : dockTypes) {
        if (toUpperAsciiLocal(dockType) == refineryType) {
            return true;
        }
    }
    return false;
}

int runtimeResourceStorageCapacityForObject(
    const RuntimeWindowState& state,
    const WorldObjectInstance& object
) {
    if (!object.alive
        || object.owner != state.economy.owner
        || object.kind != WorldObjectKind::Structure) {
        return 0;
    }

    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, object.type);
    if (building == nullptr || !runtimeBuildingStoresResources(*building)) {
        return 0;
    }

    return std::max(0, building->storage);
}

int runtimeOwnedResourceStorageCapacity(const RuntimeWindowState& state) {
    int capacity = 0;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        capacity += runtimeResourceStorageCapacityForObject(state, object);
    }
    return capacity;
}

void refreshRuntimeResourceStorageCapacity(RuntimeWindowState& state) {
    const int capacity = runtimeOwnedResourceStorageCapacity(state);
    state.economy.resourceStorageCapacity = capacity;
    state.economy.storedResourceUnits =
        std::max(0, state.economy.storedResourceUnits);
    state.economy.storedResourceValue =
        std::max(0, state.economy.storedResourceValue);
    state.economy.storedResourceGemUnits =
        std::clamp(
            state.economy.storedResourceGemUnits,
            0,
            state.economy.storedResourceUnits
        );
    if (capacity <= 0) {
        return;
    }
    if (state.economy.storedResourceUnits <= capacity) {
        return;
    }

    const int previousUnits = state.economy.storedResourceUnits;
    state.economy.storedResourceUnits = capacity;
    state.economy.storedResourceValue =
        runtimeScaleResourceValue(
            state.economy.storedResourceValue,
            capacity,
            previousUnits
        );
    state.economy.storedResourceGemUnits =
        runtimeScaleResourceValue(
            state.economy.storedResourceGemUnits,
            capacity,
            previousUnits
        );
}

void recordRuntimeResourceDeliveryToStorage(
    RuntimeWindowState& state,
    int cargoUnits,
    int cargoGemUnits,
    int deliveredValue
) {
    refreshRuntimeResourceStorageCapacity(state);

    const int units = std::max(0, cargoUnits);
    const int gemUnits =
        std::clamp(cargoGemUnits, 0, units);
    const int value = std::max(0, deliveredValue);
    state.economy.lastResourceStorageCapacity =
        state.economy.resourceStorageCapacity;
    state.economy.lastResourceStoredUnits = 0;
    state.economy.lastResourceStoredValue = 0;
    state.economy.lastResourceStoredGemUnits = 0;
    state.economy.lastResourceStorageOverflowUnits = 0;
    if (units <= 0 && value <= 0) {
        return;
    }

    int acceptedUnits = units;
    if (state.economy.resourceStorageCapacity > 0) {
        const int availableUnits =
            std::max(
                0,
                state.economy.resourceStorageCapacity
                    - state.economy.storedResourceUnits
            );
        acceptedUnits = std::min(units, availableUnits);
        state.economy.lastResourceStorageOverflowUnits =
            std::max(0, units - acceptedUnits);
    }

    const int acceptedValue =
        runtimeScaleResourceValue(value, acceptedUnits, units);
    const int acceptedGemUnits =
        runtimeScaleResourceValue(gemUnits, acceptedUnits, units);
    state.economy.storedResourceUnits += acceptedUnits;
    state.economy.storedResourceValue += acceptedValue;
    state.economy.storedResourceGemUnits =
        std::clamp(
            state.economy.storedResourceGemUnits + acceptedGemUnits,
            0,
            state.economy.storedResourceUnits
        );
    state.economy.lastResourceStoredUnits = acceptedUnits;
    state.economy.lastResourceStoredValue = acceptedValue;
    state.economy.lastResourceStoredGemUnits = acceptedGemUnits;
}

void refreshRuntimeEconomyMetricsForCurrentOwner(
    RuntimeWindowState& state
) {
    const std::string ownerKey =
        runtimeCanonicalHumanHouseOwner(state.economy.owner);
    int produced = 0;
    int used = 0;
    int harvesters = 0;
    int refineries = 0;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || runtimeCanonicalHumanHouseOwner(object.owner) != ownerKey) {
            continue;
        }
        if (object.kind == WorldObjectKind::Structure) {
            const BuildingType* building =
                findRuntimeBuildingTypeById(state.rules, object.type);
            if (building == nullptr) {
                continue;
            }
            const int power = runtimeBuildingPowerOutput(*building, object);
            if (power >= 0) {
                produced += power;
            } else {
                used += -power;
            }
            if (runtimeBuildingCountsAsRefinery(*building)) {
                ++refineries;
            }
        } else {
            const UnitType* unit =
                findRuntimeUnitTypeById(state.rules, object.type);
            if (unit != nullptr && unit->harvester) {
                ++harvesters;
            }
        }
    }
    if (runtimeDiskDrainOwnerBlackout(state, state.economy.owner)) produced = 0;
    state.economy.powerProduced = produced;
    state.economy.powerUsed = used;
    state.economy.harvesters = harvesters;
    state.economy.refineries = refineries;
    refreshRuntimeResourceStorageCapacity(state);
}

int runtimeHarvesterDepartureRefineryId(
    const RuntimeWindowState& state, const WorldObjectInstance& harvester
) {
    if (const auto worker = state.slaveMinerTasks.find(harvester.instanceId);
        worker != state.slaveMinerTasks.end()) {
        const auto* parent = findObjectById(state, worker->second.refineryId);
        if (parent && parent->owner == harvester.owner
            && runtimeCellDistanceToObjectFootprint(state, runtimeCellPointForObject(harvester), *parent) == 0.0)
            return parent->instanceId;
    }
    const auto task = state.harvesterTasks.find(harvester.instanceId);
    if (task == state.harvesterTasks.end()) return -1;
    const auto* refinery = findObjectById(state, task->second.targetRefineryId);
    // The dock can replace impassable ground with the refinery's drive row.
    // Retain that same passage for the first journey out after unloading.
    return refinery != nullptr
        && runtimeHarvesterCanUnloadAtRefinery(state, harvester, *refinery)
        && runtimeStructureCellIsVehicleDriveRow(
            state, *refinery, runtimeCellPointForObject(harvester), harvester)
        ? refinery->instanceId : -1;
}

// Harvesting must have ground contact with the resource. A nearby empty cell
// on the other side of a cliff/wall is not a valid mining approach.
struct RuntimeHarvesterResourceAccess {
    const RuntimeWindowState& state;
    const WorldObjectInstance& harvester;
    int departureRefineryId;
    std::unordered_map<int, const TerrainCell*> generatedTerrain;
    const std::unordered_map<int, const TerrainCell*>* terrain;
    std::unordered_map<int, int> staticOccupancy;
    std::optional<std::unordered_map<int, int>> pathOccupancy;
    std::optional<std::unordered_set<int>> reachableAfterFailure;

    RuntimeHarvesterResourceAccess(const RuntimeWindowState& worldState, const WorldObjectInstance& miner)
        : state(worldState), harvester(miner),
          departureRefineryId(runtimeHarvesterDepartureRefineryId(state, miner)),
          terrain(&state.terrainByCell),
          staticOccupancy(buildRuntimeOccupancyLookup(state, miner.instanceId)) {
        if (terrain->empty()) {
            generatedTerrain = buildRuntimeTerrainLookup(state.world);
            terrain = &generatedTerrain;
        }
        // Another miner can share the same ore deposit. Mobile traffic must not
        // make a stationary harvester change its intake direction every tick.
        for (auto it = staticOccupancy.begin(); it != staticOccupancy.end();) {
            const auto* occupant = runtimeOccupantIdIsTerrain(it->second)
                ? nullptr : findObjectById(state, it->second);
            if (occupant != nullptr && occupant->kind != WorldObjectKind::Structure)
                it = staticOccupancy.erase(it);
            else ++it;
        }
    }

    bool canGatherFrom(const RuntimeCellPoint& from, const RuntimeCellPoint& resource) const {
        if (!runtimeCellHasHarvestableResource(state, resource)
            || std::abs(from.x - resource.x) > 1 || std::abs(from.y - resource.y) > 1)
            return false;
        const auto source = terrain->find(runtimeCellKey(from.x, from.y));
        if (source == terrain->end() || source->second == nullptr
            || runtimeCachedTerrainInfoForCell(state, *source->second).bridge)
            return false;
        // A delivery point inside a refinery is valid for unloading and
        // departure, but cannot become a stationary mining approach.
        if (!runtimeCellIsPassable(state, *terrain, staticOccupancy, from, harvester))
            return false;
        if (from == resource) return runtimeCellIsPassable(
            state, *terrain, staticOccupancy, resource, harvester);
        return runtimeCellTransitionIsPassable(
            state, *terrain, staticOccupancy, from, resource, harvester, -1, nullptr, false);
    }

    const std::unordered_map<int, int>& occupiedForPath() {
        if (!pathOccupancy) {
            pathOccupancy = buildRuntimeOccupancyLookup(state, harvester.instanceId);
        }
        return *pathOccupancy;
    }

    void rememberFailedSearch() {
        if (!reachableAfterFailure) {
            // One blocked worker can see hundreds of ore cells outside its
            // pocket. Discover its reachable ground once for this search,
            // rather than rebuilding a path request for every ore approach.
            reachableAfterFailure = buildRuntimeReachableMoveCells(
                state, harvester, {runtimeCellPointForObject(harvester)},
                *terrain, occupiedForPath(), departureRefineryId);
        }
    }
};

std::optional<RuntimeCellPoint> findRuntimeHarvesterResourceApproach(
    RuntimeHarvesterResourceAccess& access,
    const RuntimeCellPoint& resource
) {
    const auto& state = access.state;
    const auto& harvester = access.harvester;
    const RuntimeCellPoint from = runtimeCellPointForObject(harvester);
    if (access.canGatherFrom(from, resource)) return from;
    // Invalid resource terrain cannot become reachable by choosing a distant
    // fallback destination. Check it before doing any path searches.
    if (!access.canGatherFrom(resource, resource)) return std::nullopt;
    const auto& occupied = access.occupiedForPath();
    std::vector<RuntimeCellPoint> approaches{resource};
    const auto neighbors = runtimeNeighborsOf(resource);
    approaches.insert(approaches.end(), neighbors.begin(), neighbors.end());
    for (const auto& approach : approaches) {
        if (!access.canGatherFrom(approach, resource)
            || !runtimeCellIsPassable(state, *access.terrain, occupied, approach, harvester))
            continue;
        if (access.reachableAfterFailure
            && !access.reachableAfterFailure->contains(runtimeCellKey(approach.x, approach.y)))
            continue;
        const auto path = findRuntimePath(
            state, harvester, from, approach, access.departureRefineryId);
        if (!path.empty() && path.back() == approach) return approach;
        access.rememberFailedSearch();
    }
    return std::nullopt;
}

std::optional<RuntimeCellPoint> findRuntimeHarvesterResourceApproach(
    const RuntimeWindowState& state, const WorldObjectInstance& harvester,
    const RuntimeCellPoint& resource
) {
    RuntimeHarvesterResourceAccess access(state, harvester);
    return findRuntimeHarvesterResourceApproach(access, resource);
}
std::optional<RuntimeCellPoint> findHarvestableRuntimeResourceNear(
    const RuntimeWindowState& state,
    const WorldObjectInstance& harvester,
    const RuntimeCellPoint& preferred
) {
    const RuntimeCellPoint current = runtimeCellPointForObject(harvester);
    RuntimeHarvesterResourceAccess access(state, harvester);
    if (access.canGatherFrom(current, preferred)) {
        return preferred;
    }
    if (access.canGatherFrom(current, current)) {
        return current;
    }

    struct NearbyResourceCandidate {
        RuntimeCellPoint cell;
        int density = -1;
        int priority = 0;
    };
    std::optional<NearbyResourceCandidate> bestCandidate;
    constexpr int priorityMatrix[3][3] = {
        {8, 5, 6},
        {3, 0, 2},
        {7, 4, 1},
    };

    constexpr int radius = 1;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (std::abs(dx) != radius && std::abs(dy) != radius) {
                continue;
            }
            const RuntimeCellPoint candidate{
                current.x + dx,
                current.y + dy
            };
            const int density = runtimeResourceDensityAtCell(state, candidate);
            if (density < 0 || !access.canGatherFrom(current, candidate)) {
                continue;
            }
            const int priority = priorityMatrix[dy + 1][dx + 1];
            if (!bestCandidate.has_value()
                || density > bestCandidate->density
                || (density == bestCandidate->density
                    && priority < bestCandidate->priority)) {
                bestCandidate = NearbyResourceCandidate{
                    candidate,
                    density,
                    priority
                };
            }
        }
    }
    if (bestCandidate.has_value()) {
        return bestCandidate->cell;
    }
    return std::nullopt;
}

std::optional<RuntimeCellPoint> findNearestRuntimeOreCell(
    const RuntimeWindowState& state,
    const WorldObjectInstance& harvester,
    std::optional<RuntimeCellPoint> scanOrigin = std::nullopt,
    bool farScan = true
) {
    const RuntimeCellPoint from = runtimeCellPointForObject(harvester);
    const RuntimeCellPoint origin =
        (scanOrigin.has_value() && scanOrigin->x >= 0 && scanOrigin->y >= 0)
            ? *scanOrigin
            : from;
    const int maxRadius =
        std::max(1, farScan
            ? state.rules.getTiberiumFarScan()
            : state.rules.getTiberiumNearScan());
    const int startRadius = farScan ? 1 : 0;

    struct OreCandidate {
        RuntimeCellPoint cell;
        int value = 0;
        int priority = 0;
        long long distanceScore = 0;
    };

    const auto candidateIfHarvestable =
        [&](const RuntimeCellPoint& cell) -> std::optional<OreCandidate> {
            const TerrainCell* terrain = findRuntimeTerrainCellAt(state, cell);
            if (terrain == nullptr
                || !terrain->hasOverlay
                || (!terrain->overlayIsOre && !terrain->overlayIsGem)) {
                return std::nullopt;
            }

            const long long dxFromHarvester =
                static_cast<long long>(cell.x - from.x);
            const long long dyFromHarvester =
                static_cast<long long>(cell.y - from.y);
            int priority = 4;
            const int dxFromOrigin = cell.x - origin.x;
            const int dyFromOrigin = cell.y - origin.y;
            if (std::abs(dxFromOrigin) <= 1
                && std::abs(dyFromOrigin) <= 1) {
                constexpr int priorityMatrix[3][3] = {
                    {8, 5, 6},
                    {3, 0, 2},
                    {7, 4, 1},
                };
                priority = priorityMatrix[dyFromOrigin + 1][dxFromOrigin + 1];
            }

            return OreCandidate{
                cell,
                runtimeResourceValueForCell(state, *terrain),
                priority,
                (dxFromHarvester * dxFromHarvester)
                    + (dyFromHarvester * dyFromHarvester)
            };
        };

    // All candidates observe one world snapshot. Reuse its occupancy instead
    // of rebuilding the entire world for each unreachable ore cell. Construct
    // it lazily so an empty scan needs no occupancy work at all.
    std::optional<RuntimeHarvesterResourceAccess> resourceAccess;
    const auto reachable = [&](const RuntimeCellPoint& cell) {
        if (!resourceAccess) resourceAccess.emplace(state, harvester);
        return findRuntimeHarvesterResourceApproach(*resourceAccess, cell).has_value();
    };

    if (const std::optional<OreCandidate> originCandidate =
        candidateIfHarvestable(origin)) {
        if (reachable(originCandidate->cell)) {
            return originCandidate->cell;
        }
    }

    std::vector<OreCandidate> candidates;
    for (int radius = startRadius; radius <= maxRadius; ++radius) {
        candidates.clear();
        candidates.reserve(static_cast<std::size_t>(std::max(8, radius * 8)));

        for (int dy = -radius; radius > 0 && dy <= radius; ++dy) {
            // Keep the original row-major perimeter order, skipping the
            // interior directly instead of visiting every square at every
            // radius (cubic work when nearby ore has been exhausted).
            const int step = std::abs(dy) == radius ? 1 : radius * 2;
            for (int dx = -radius; dx <= radius; dx += step) {
                const RuntimeCellPoint candidateCell{
                    origin.x + dx,
                    origin.y + dy
                };
                if (const std::optional<OreCandidate> candidate =
                    candidateIfHarvestable(candidateCell)) {
                    candidates.push_back(*candidate);
                }
            }
        }

        if (candidates.empty()) {
            continue;
        }

        std::sort(
            candidates.begin(),
            candidates.end(),
            [farScan](const OreCandidate& left, const OreCandidate& right) {
                if (!farScan && left.value != right.value) {
                    return left.value > right.value;
                }
                if (left.priority != right.priority) {
                    return left.priority < right.priority;
                }
                if (left.distanceScore != right.distanceScore) {
                    return left.distanceScore < right.distanceScore;
                }
                return RuntimeCellPointLess{}(left.cell, right.cell);
            }
        );

        for (const OreCandidate& candidate : candidates) {
            if (reachable(candidate.cell)) {
                return candidate.cell;
            }
        }
    }
    return std::nullopt;
}

bool issueRuntimeHarvesterMoveToCell(
    RuntimeWindowState& state,
    WorldObjectInstance& harvester,
    const RuntimeCellPoint& targetCell,
    const std::string& mission,
    std::optional<RuntimeCellPoint>* actualTarget = nullptr,
    int allowedOccupiedObjectId = -1,
    bool exactTarget = false
) {
    const bool miningApproach = mission == "Harvest"
        && (runtimeObjectIsHarvester(state, harvester) || state.slaveMinerTasks.contains(harvester.instanceId));
    if (miningApproach && allowedOccupiedObjectId < 0)
        allowedOccupiedObjectId = runtimeHarvesterDepartureRefineryId(state, harvester);
    const std::optional<RuntimeCellPoint> passableTarget =
        miningApproach
            ? findRuntimeHarvesterResourceApproach(state, harvester, targetCell)
            : exactTarget
            ? findRuntimePassableCellNear(
                state,
                harvester,
                targetCell,
                0,
                allowedOccupiedObjectId
            )
            : findNearestRuntimePassableCell(
                state,
                harvester,
                targetCell,
                allowedOccupiedObjectId
            );
    if (!passableTarget.has_value()) {
        return false;
    }
    if (actualTarget != nullptr) {
        *actualTarget = passableTarget;
    }

    RuntimeObjectBehavior behavior;
    behavior.kind = RuntimeBehaviorKind::Move;
    behavior.state = RuntimeBehaviorState::Queued;
    behavior.moveTarget = *passableTarget;
    if (state.slaveMinerTasks.contains(harvester.instanceId)) {
        std::optional<RuntimeObjectBehavior> previous;
        if (const auto old = state.behaviors.find(harvester.instanceId); old != state.behaviors.end())
            previous = old->second;
        eraseRuntimeBehavior(state, harvester.instanceId);
        if (previous) {
            behavior.weaponCooldownTicks = previous->weaponCooldownTicks;
            if (previous->moveCooldownTicks > 0)
                inheritRuntimeVehicleMoveSegment(harvester, behavior, *previous);
        }
    }
    harvester.mission = mission;
    state.behaviors[harvester.instanceId] = std::move(behavior);
    RuntimeObjectBehavior& queuedBehavior =
        state.behaviors[harvester.instanceId];
    submitRuntimeMovePathRequest(
        state,
        harvester,
        queuedBehavior,
        runtimeCellPointForObject(harvester),
        queuedBehavior.moveTarget,
        allowedOccupiedObjectId,
        nullptr,
        !exactTarget && !miningApproach
    );
    return true;
}

const WorldObjectInstance* findRuntimeHarvesterRefineryById(
    const RuntimeWindowState& state,
    const WorldObjectInstance& harvester,
    int objectId
) {
    const WorldObjectInstance* object = findObjectById(state, objectId);
    if (object == nullptr
        || !object->alive
        || object->owner != state.economy.owner
        || object->kind != WorldObjectKind::Structure) {
        return nullptr;
    }
    if (!runtimeHarvesterCanUnloadAtRefinery(state, harvester, *object)) {
        return nullptr;
    }
    return object;
}

std::optional<int> findNearestRuntimeRefineryId(
    const RuntimeWindowState& state,
    const WorldObjectInstance& harvester
) {
    const RuntimeCellPoint from = runtimeCellPointForObject(harvester);
    double bestDistance = std::numeric_limits<double>::max();
    std::optional<int> bestObjectId;
    for (const WorldObjectInstance& object : state.world.getObjects()) {
        if (!object.alive
            || object.owner != state.economy.owner
            || object.kind != WorldObjectKind::Structure) {
            continue;
        }

        const BuildingType* building =
            findRuntimeBuildingTypeById(state.rules, object.type);
        if (building == nullptr
            || !runtimeHarvesterCanUnloadAtRefinery(
                state,
                harvester,
                object
            )) {
            continue;
        }

        const double distance =
            runtimeCellDistance(from, runtimeCellPointForObject(object));
        if (distance < bestDistance) {
            bestDistance = distance;
            bestObjectId = object.instanceId;
        }
    }
    return bestObjectId;
}

bool teleportRuntimeHarvesterToCell(
    RuntimeWindowState& state,
    WorldObjectInstance& harvester,
    const RuntimeCellPoint& target
) {
    if (!handleRuntimeParasiteHostEvent(
            state,
            harvester.instanceId,
            ParasiteHostEventKind::Teleporting,
            std::max(1, state.config.fixedTickHz)
        )) {
        return false;
    }
    const UnitType* unit = findRuntimeUnitType(state.rules, harvester);
    if (unit != nullptr && !trimAsciiLocal(unit->chronoOutSound).empty()) {
        emitRuntimeSound(
            state,
            "sound:" + trimAsciiLocal(unit->chronoOutSound)
        );
    }
    spawnRuntimeChronoWarpEffect(state, {"ChronoWarpOut", state.tick, harvester.instanceId,
        -1, 0, {harvester.x, harvester.y}, "chrono miner return"}, false);
    state.behaviors.erase(harvester.instanceId);
    harvester.x = target.x;
    harvester.y = target.y;
    harvester.cell = runtimeCellKey(target.x, target.y);
    harvester.level = terrainLevelAtRuntimeCell(state, target.x, target.y);
    harvester.onBridge = runtimeCellIsBridge(state, target.x, target.y);
    clearRuntimeRenderPosition(harvester);
    refreshRuntimeObjectOccupancyAfterLocalChange(state);
    invalidateRuntimeObjectProjectionCache(state);
    ++state.economy.harvesterTeleports;
    spawnRuntimeChronoWarpEffect(state, {"ChronoWarpIn", state.tick, harvester.instanceId,
        -1, 0, {harvester.x, harvester.y}, "chrono miner return"}, false);
    if (unit != nullptr && !trimAsciiLocal(unit->chronoInSound).empty()) {
        emitRuntimeSound(
            state,
            "sound:" + trimAsciiLocal(unit->chronoInSound)
        );
    }
    return true;
}

void spawnRuntimeRefinerySmokeEffects(
    RuntimeWindowState& state,
    const WorldObjectInstance& refinery,
    const WorldObjectInstance& harvester
) {
    const BuildingType* building =
        findRuntimeBuildingTypeById(state.rules, refinery.type);
    if (building == nullptr
        || building->refinerySmoke.offsets.empty()
        || building->refinerySmoke.particleSystem.empty()) {
        return;
    }

    const RuntimeCellPoint origin = runtimeCellPointForObject(refinery);
    // RefinerySmokeOffset is authored relative to the SHP's foundation origin,
    // the same anchor used by the building renderer, not the foundation center.
    const double baseX = static_cast<double>(origin.x);
    const double baseY = static_cast<double>(origin.y);
    ensureRuntimeParticlesInitialized(state);
    int spawned = 0;
    std::size_t offsetOrdinal = 0;

    for (const RefinerySmokeOffset& offset :
            building->refinerySmoke.offsets) {
        RuntimeParticleSpawnContext particleContext;
        particleContext.sourceObjectId = refinery.instanceId;
        particleContext.owner = refinery.owner;
        particleContext.sourceTag = "refinery-smoke:"
            + building->refinerySmoke.particleSystem;
        // The stock refinery chimney art aligns half a lepton cell below
        // the centered canvas origin in the vertical projection. Keep this
        // attachment correction local to refinery smoke.
        particleContext.position = {
            (baseX * 256.0) + static_cast<double>(offset.x),
            (baseY * 256.0) + static_cast<double>(offset.y),
            (static_cast<double>(refinery.level)
                + runtimeWeaponHeightInLevels(offset.z - 128)) * 256.0
        };
        particleContext.seed =
            (static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(refinery.instanceId)
             ) << 32U)
            ^ static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(harvester.instanceId)
              )
            ^ static_cast<std::uint64_t>(
                static_cast<std::uint32_t>(state.tick)
              )
            ^ ((static_cast<std::uint64_t>(offsetOrdinal) + 1ULL)
                * 0x9e3779b97f4a7c15ULL)
            ^ 0x726566696e657279ULL;
        particleContext.systemLifetimeOverrideFrames = std::max(
            8,
            building->refinerySmoke.frames
        );
        ++offsetOrdinal;

        if (state.particles.spawnSystem(
                state.rules,
                building->refinerySmoke.particleSystem,
                particleContext
            ) != 0) {
            ++spawned;
        }
    }

    if (spawned > 0) {
        state.economy.lastRefinerySmokeCell = origin;
        state.economy.lastRefinerySmokeEffects = spawned;
        state.economy.lastRefinerySmokeParticleSystem =
            building->refinerySmoke.particleSystem;
        state.economy.lastRefinerySmokeParticle =
            building->refinerySmoke.particle;
        state.economy.lastRefinerySmokeImage =
            building->refinerySmoke.image;
    }
}

